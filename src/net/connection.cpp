#include "net/connection.hpp"

#include <array>
#include <format>

namespace opense4::net {

namespace {

// Queued output beyond this means the peer stopped reading.
constexpr size_t kMaxQueuedOut = size_t{1} << 30;
constexpr size_t kReadChunk = 64 * 1024;

} // namespace

Connection::Connection(Socket socket, size_t maxIncoming)
    : socket_(std::move(socket)), maxIncoming_(maxIncoming), lastReceive_(Clock::now()), lastSend_(Clock::now()) {}

void Connection::sendRaw(proto::MsgType type, std::span<const uint8_t> payload) {
    if (failed()) return;
    if (pendingOut() + payload.size() > kMaxQueuedOut) {
        fail("the other side is not reading (send queue full)");
        return;
    }
    if (outPos_ == out_.size()) {
        out_.clear();
        outPos_ = 0;
    }
    const auto length = static_cast<uint32_t>(payload.size() + 1);
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>(length >> (8 * i)));
    out_.push_back(static_cast<uint8_t>(type));
    out_.insert(out_.end(), payload.begin(), payload.end());
}

void Connection::receive() {
    if (failed() || peerClosed_) return;
    std::array<uint8_t, kReadChunk> chunk{};
    // Stop once more than one maximal frame is buffered (back-pressure).
    while (in_.size() - inPos_ <= maxIncoming_ + 8) {
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
    if (failed()) return std::nullopt;
    const size_t available = in_.size() - inPos_;
    if (available < 4) return std::nullopt;
    uint32_t length = 0;
    for (size_t i = 0; i < 4; ++i) length |= static_cast<uint32_t>(in_[inPos_ + i]) << (8 * i);
    if (length == 0 || length - 1 > maxIncoming_) {
        fail(length == 0 ? std::string("protocol error (empty frame)")
                         : std::format("message too large ({} bytes, limit {})", length - 1, maxIncoming_));
        return std::nullopt;
    }
    if (available < 4 + size_t{length}) return std::nullopt;
    Frame f;
    f.type = static_cast<proto::MsgType>(in_[inPos_ + 4]);
    const auto begin = in_.begin() + static_cast<std::ptrdiff_t>(inPos_ + 5);
    f.payload.assign(begin, begin + static_cast<std::ptrdiff_t>(length - 1));
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
