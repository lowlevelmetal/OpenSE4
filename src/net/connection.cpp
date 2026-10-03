#include "net/connection.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace opense4::net {

namespace {

// Queued output beyond this means the peer stopped reading.
constexpr size_t kMaxQueuedOut = size_t{1} << 30;
constexpr size_t kReadChunk = 64 * 1024;
// A sealed frame adds the real message type (encrypted) and the MAC.
constexpr size_t kSealOverhead = 1 + sizeof(crypto::Mac);
constexpr size_t kHeader = 5;  // u32 length and the type byte

} // namespace

Connection::Connection(Socket socket, size_t maxIncoming)
    : socket_(std::move(socket)), maxIncoming_(maxIncoming), lastReceive_(Clock::now()), lastSend_(Clock::now()) {}

Connection::~Connection() {
    crypto::wipe(sendKey_.data(), sendKey_.size());
    crypto::wipe(receiveKey_.data(), receiveKey_.size());
}

void Connection::startEncryption(const crypto::Key& send, const crypto::Key& receive) {
    sendKey_ = send;
    receiveKey_ = receive;
    sent_ = 0;
    received_ = 0;
    encrypted_ = true;
}

void Connection::sendRaw(proto::MsgType type, std::span<const uint8_t> payload) { queue(type, payload, encrypted_); }

void Connection::sendPlain(proto::MsgType type, std::span<const uint8_t> payload) { queue(type, payload, false); }

void Connection::queue(proto::MsgType type, std::span<const uint8_t> payload, bool seal) {
    if (failed()) return;
    if (pendingOut() + payload.size() > kMaxQueuedOut) {
        fail("the other side is not reading (send queue full)");
        return;
    }
    if (outPos_ == out_.size()) {
        out_.clear();
        outPos_ = 0;
    }
    const size_t start = out_.size();
    const auto length = static_cast<uint32_t>(payload.size() + 1 + (seal ? kSealOverhead : 0));
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>(length >> (8 * i)));
    if (!seal) {
        out_.push_back(static_cast<uint8_t>(type));
        out_.insert(out_.end(), payload.begin(), payload.end());
        return;
    }
    // [length][kSealedFrame][ type, payload ][MAC]: the header is authenticated, the rest also encrypted.
    out_.push_back(proto::kSealedFrame);
    out_.push_back(static_cast<uint8_t>(type));
    out_.insert(out_.end(), payload.begin(), payload.end());
    crypto::Mac mac{};
    const std::span<const uint8_t> header(out_.data() + start, kHeader);
    crypto::seal(sendKey_, sent_++, header, std::span<uint8_t>(out_.data() + start + kHeader, 1 + payload.size()), mac);
    out_.insert(out_.end(), mac.begin(), mac.end());
}

void Connection::receive() {
    if (failed() || peerClosed_) return;
    std::array<uint8_t, kReadChunk> chunk{};
    // Stop once more than one maximal frame is buffered (back-pressure).
    while (in_.size() - inPos_ <= maxIncoming_ + 4 + kHeader + kSealOverhead) {
        const IoResult r = socket_.receive(chunk);
        if (r.status == IoStatus::WouldBlock) break;
        if (r.status == IoStatus::Closed) {
            peerClosed_ = true;
            break;
        }
        if (r.status == IoStatus::Error) {
            fail(std::format("connection lost ({})", r.error));
            break;
        }
        in_.insert(in_.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(r.bytes));
        lastReceive_ = Clock::now();
    }
}

void Connection::flush() {
    while (!failed() && outPos_ < out_.size()) {
        const IoResult r = socket_.send(std::span(out_).subspan(outPos_));
        if (r.status == IoStatus::WouldBlock) break;
        if (r.status != IoStatus::Ok) {
            fail(std::format("connection lost ({})", r.error.empty() ? "closed" : r.error));
            break;
        }
        outPos_ += r.bytes;
        lastSend_ = Clock::now();
    }
    if (outPos_ == out_.size()) {
        out_.clear();
        outPos_ = 0;
    } else if (outPos_ > (size_t{16} << 20)) {
        out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(outPos_));
        outPos_ = 0;
    }
}

std::optional<Frame> Connection::nextFrame() {
    if (failed() || keysDiffer_) return std::nullopt;
    const size_t available = in_.size() - inPos_;
    if (available < 4) return std::nullopt;
    uint32_t length = 0;
    for (size_t i = 0; i < 4; ++i) length |= static_cast<uint32_t>(in_[inPos_ + i]) << (8 * i);
    if (length == 0 || length - 1 > maxIncoming_ + (encrypted_ ? kSealOverhead : 0)) {
        fail(length == 0 ? std::string("protocol error (empty frame)")
                         : std::format("message too large ({} bytes, limit {})", length - 1, maxIncoming_));
        return std::nullopt;
    }
    if (available < 4 + size_t{length}) return std::nullopt;
    Frame f;
    const uint8_t typeByte = in_[inPos_ + 4];
    if (encrypted_ && typeByte == proto::kSealedFrame) {
        if (length < 1 + kSealOverhead) {
            fail("protocol error (a sealed frame too short)");
            return std::nullopt;
        }
        uint8_t* text = in_.data() + inPos_ + kHeader;
        const size_t textSize = length - 1 - sizeof(crypto::Mac);
        crypto::Mac mac{};
        std::copy_n(text + textSize, mac.size(), mac.begin());
        const std::span<const uint8_t> header(in_.data() + inPos_, kHeader);
        if (!crypto::open(receiveKey_, received_, header, std::span<uint8_t>(text, textSize), mac)) {
            // The first one: the sides derived different keys. Later: the
            // stream was changed, replayed or reordered on its way.
            if (received_ == 0) keysDiffer_ = true;
            else fail("the connection was tampered with or damaged (a message failed its authentication)");
            return std::nullopt;
        }
        ++received_;
        f.sealed = true;
        f.type = static_cast<proto::MsgType>(text[0]);
        f.payload.assign(text + 1, text + textSize);
    } else {
        if (encrypted_ && received_ > 0) {
            fail("protocol error (a message in the clear on an encrypted connection)");
            return std::nullopt;
        }
        f.type = static_cast<proto::MsgType>(typeByte);
        const auto begin = in_.begin() + static_cast<std::ptrdiff_t>(inPos_ + kHeader);
        f.payload.assign(begin, begin + static_cast<std::ptrdiff_t>(length - 1));
    }
    inPos_ += 4 + size_t{length};
    if (inPos_ == in_.size()) {
        in_.clear();
        inPos_ = 0;
    } else if (inPos_ > (size_t{1} << 20)) {
        in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(inPos_));
        inPos_ = 0;
    }
    return f;
}

} // namespace opense4::net
