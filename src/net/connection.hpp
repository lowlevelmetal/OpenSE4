#pragma once

// One framed, non-blocking TCP connection (protocol.hpp describes frames).
// Outgoing frames are queued and written as the socket accepts them;
// incoming bytes are buffered until whole frames are available.
//
// After startEncryption() every frame is sealed (net/crypto.hpp): the
// message type and payload are encrypted and authenticated with the
// direction's key, and the frame's header is authenticated too. The nonce is
// the direction's message counter, so a frame that is replayed, reordered,
// dropped or changed fails to open and ends the connection.

#include "net/crypto.hpp"
#include "net/protocol.hpp"
#include "net/socket.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace opense4::net {

using Clock = std::chrono::steady_clock;

struct Frame {
    proto::MsgType type{};
    std::vector<uint8_t> payload;
    bool sealed = false;   // arrived encrypted (false: in the clear)
};

class Connection {
public:
    Connection(Socket socket, size_t maxIncoming);
    ~Connection();
    Connection(Connection&&) = default;
    Connection& operator=(Connection&&) = default;

    Socket& socket() { return socket_; }
    const Socket& socket() const { return socket_; }
    void setMaxIncoming(size_t bytes) { maxIncoming_ = bytes; }

    // Queues a frame (sealed once encryption has started).
    void sendRaw(proto::MsgType type, std::span<const uint8_t> payload);
    // Queues a frame in the clear even after encryption started: only the
    // refusal of a client whose sealed messages cannot be opened.
    void sendPlain(proto::MsgType type, std::span<const uint8_t> payload);
    template <class T>
    void sendPlain(proto::MsgType type, const T& message) {
        const std::vector<uint8_t> payload = proto::encode(message);
        sendPlain(type, std::span<const uint8_t>(payload));
    }

    // Seals every frame queued from now on with `send`, and opens incoming
    // ones with `receive`; each direction counts its messages from 0. Until
    // the first sealed frame has arrived, frames in the clear still come
    // through (a refusal from a peer that could not open ours); after it,
    // one ends the connection.
    void startEncryption(const crypto::Key& send, const crypto::Key& receive);
    bool encrypted() const { return encrypted_; }
    // The first sealed frame from the peer did not open: the two sides do
    // not share keys (a wrong join password, or a man in the middle).
    // Nothing more is read.
    bool keysDiffer() const { return keysDiffer_; }
    template <class T>
    void send(proto::MsgType type, const T& message) {
        const std::vector<uint8_t> payload = proto::encode(message);
        sendRaw(type, payload);
    }

    // Reads what the socket has (call when poll reports it readable).
    void receive();
    // Writes queued bytes until the socket would block.
    void flush();
    // The next complete incoming frame, if any. A frame over the size limit
    // fails the connection.
    std::optional<Frame> nextFrame();

    bool wantsWrite() const { return outPos_ < out_.size(); }
    size_t pendingOut() const { return out_.size() - outPos_; }
    // Peer closed its side (after any frames still buffered) or an error happened.
    bool finished() const { return failed() || (peerClosed_ && inPos_ == in_.size()); }
    bool failed() const { return !error_.empty(); }
    bool peerClosed() const { return peerClosed_; }
    const std::string& error() const { return error_; }
    void fail(std::string why) {
        if (error_.empty()) error_ = std::move(why);
    }

    Clock::time_point lastReceive() const { return lastReceive_; }
    Clock::time_point lastSend() const { return lastSend_; }

private:
    Socket socket_;
    size_t maxIncoming_;
    std::vector<uint8_t> in_;
    size_t inPos_ = 0;
    std::vector<uint8_t> out_;
    size_t outPos_ = 0;
    bool peerClosed_ = false;
    std::string error_;
    Clock::time_point lastReceive_;
    Clock::time_point lastSend_;
    bool encrypted_ = false;
    crypto::Key sendKey_{};
    crypto::Key receiveKey_{};
    uint64_t sent_ = 0;          // sealed frames sent (the next nonce)
    uint64_t received_ = 0;      // sealed frames opened
    bool keysDiffer_ = false;

    void queue(proto::MsgType type, std::span<const uint8_t> payload, bool seal);
};

} // namespace opense4::net
