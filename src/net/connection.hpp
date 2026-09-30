#pragma once

// One framed, non-blocking TCP connection (protocol.hpp describes frames).
// Outgoing frames are queued and written as the socket accepts them;
// incoming bytes are buffered until whole frames are available.

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
};

class Connection {
public:
    Connection(Socket socket, size_t maxIncoming);

    Socket& socket() { return socket_; }
    const Socket& socket() const { return socket_; }
    void setMaxIncoming(size_t bytes) { maxIncoming_ = bytes; }

    // Queues a frame.
    void sendRaw(proto::MsgType type, std::span<const uint8_t> payload);
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
};

} // namespace opense4::net
