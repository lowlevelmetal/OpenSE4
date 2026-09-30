#pragma once

// Thin portable wrapper over TCP sockets (BSD sockets, Winsock on Windows).
// Every socket is non-blocking; readiness comes from pollSockets().

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>

namespace opense4::net {

#ifdef _WIN32
using NativeSocket = uintptr_t;  // SOCKET
inline constexpr NativeSocket kInvalidSocket = ~NativeSocket{0};
#else
using NativeSocket = int;
inline constexpr NativeSocket kInvalidSocket = -1;
#endif

enum class IoStatus : uint8_t { Ok, WouldBlock, Closed, Error };

struct IoResult {
    IoStatus status = IoStatus::Ok;
    size_t bytes = 0;
    std::string error;  // Error only
};

class Socket {
public:
    Socket() = default;
    explicit Socket(NativeSocket s) : s_(s) {}
    ~Socket() { close(); }
    Socket(Socket&& o) noexcept : s_(std::exchange(o.s_, kInvalidSocket)) {}
    Socket& operator=(Socket&& o) noexcept {
        if (this != &o) {
            close();
            s_ = std::exchange(o.s_, kInvalidSocket);
        }
        return *this;
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    bool valid() const { return s_ != kInvalidSocket; }
    NativeSocket native() const { return s_; }
    void close();
    // Half-close: tells the peer we are done sending (graceful disconnect).
    void shutdownSend();

    IoResult send(std::span<const uint8_t> data);
    IoResult receive(std::span<uint8_t> data);

    std::string peerAddress() const;  // "203.0.113.5:51234"
    uint16_t localPort() const;
    // After a non-blocking connect became writable: empty when connected.
    std::string connectError() const;

private:
    NativeSocket s_ = kInvalidSocket;
};

// Listens on `bindAddress` (empty = every IPv4 interface) and `port`
// (0 = an ephemeral port; see Socket::localPort).
std::expected<Socket, std::string> listenTcp(const std::string& bindAddress, uint16_t port);
// Starts a non-blocking connect: poll for writability, then check connectError().
std::expected<Socket, std::string> connectTcp(const std::string& host, uint16_t port);
// A pending connection, or an invalid socket when there is none.
Socket acceptConnection(const Socket& listener);

struct PollItem {
    NativeSocket socket = kInvalidSocket;
    bool wantRead = false;
    bool wantWrite = false;
    bool readable = false;  // also set on hang-up/error so the read reports it
    bool writable = false;
    bool failed = false;
};
// Waits up to timeoutMs (0 = just check) for readiness. Returns the number of
// ready sockets, or -1 on error.
int pollSockets(std::span<PollItem> items, int timeoutMs);

// The address other machines on the LAN most likely reach this one at
// (no packets are sent); empty if unknown.
std::string localAddressGuess();

} // namespace opense4::net
