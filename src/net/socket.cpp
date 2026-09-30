#include "net/socket.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <mutex>
#include <vector>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600  // WSAPoll
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace opense4::net {

namespace {

#ifdef _WIN32
using SockLen = int;
int lastError() { return WSAGetLastError(); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK; }
bool inProgress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
std::string errorText(int e) {
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   static_cast<DWORD>(e), 0, reinterpret_cast<LPSTR>(&msg), 0, nullptr);
    std::string s = msg ? msg : std::format("error {}", e);
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '.')) s.pop_back();
    return s;
}
void closeNative(NativeSocket s) { closesocket(static_cast<SOCKET>(s)); }
bool setNonBlocking(NativeSocket s) {
    u_long on = 1;
    return ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &on) == 0;
}
void ensureInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}
constexpr int kSendFlags = 0;
#else
using SockLen = socklen_t;
int lastError() { return errno; }
bool wouldBlock(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
bool inProgress(int e) { return e == EINPROGRESS; }
std::string errorText(int e) { return std::strerror(e); }
void closeNative(NativeSocket s) { ::close(s); }
bool setNonBlocking(NativeSocket s) {
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
void ensureInit() {}
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
#endif

void configure(NativeSocket s) {
    setNonBlocking(s);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), static_cast<SockLen>(sizeof one));
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&one), static_cast<SockLen>(sizeof one));
#endif
}

std::string formatAddress(const sockaddr* addr) {
    char host[INET6_ADDRSTRLEN] = {};
    uint16_t port = 0;
    if (addr->sa_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(addr);
        inet_ntop(AF_INET, &in->sin_addr, host, sizeof host);
        port = ntohs(in->sin_port);
        return std::format("{}:{}", host, port);
    }
    if (addr->sa_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(addr);
        inet_ntop(AF_INET6, &in6->sin6_addr, host, sizeof host);
        port = ntohs(in6->sin6_port);
        return std::format("[{}]:{}", host, port);
    }
    return "?";
}

std::string resolveError(int rc) {
#ifdef _WIN32
    return gai_strerrorA(rc);
#else
    return gai_strerror(rc);
#endif
}

struct AddrInfo {
    addrinfo* list = nullptr;
    ~AddrInfo() {
        if (list) freeaddrinfo(list);
    }
};

} // namespace

void Socket::close() {
    if (valid()) closeNative(s_);
    s_ = kInvalidSocket;
}

void Socket::shutdownSend() {
    if (!valid()) return;
#ifdef _WIN32
    ::shutdown(static_cast<SOCKET>(s_), SD_SEND);
#else
    ::shutdown(s_, SHUT_WR);
#endif
}

IoResult Socket::send(std::span<const uint8_t> data) {
    if (!valid()) return {IoStatus::Error, 0, "socket is closed"};
    for (;;) {
#ifdef _WIN32
        const int n = ::send(static_cast<SOCKET>(s_), reinterpret_cast<const char*>(data.data()),
                             static_cast<int>(std::min<size_t>(data.size(), 1u << 30)), kSendFlags);
#else
        const ssize_t n = ::send(s_, data.data(), data.size(), kSendFlags);
#endif
        if (n >= 0) return {IoStatus::Ok, static_cast<size_t>(n), {}};
        const int e = lastError();
#ifndef _WIN32
        if (e == EINTR) continue;
#endif
        if (wouldBlock(e)) return {IoStatus::WouldBlock, 0, {}};
        return {IoStatus::Error, 0, errorText(e)};
    }
}

IoResult Socket::receive(std::span<uint8_t> data) {
    if (!valid()) return {IoStatus::Error, 0, "socket is closed"};
    for (;;) {
#ifdef _WIN32
        const int n = ::recv(static_cast<SOCKET>(s_), reinterpret_cast<char*>(data.data()), static_cast<int>(data.size()), 0);
#else
        const ssize_t n = ::recv(s_, data.data(), data.size(), 0);
#endif
        if (n > 0) return {IoStatus::Ok, static_cast<size_t>(n), {}};
        if (n == 0) return {IoStatus::Closed, 0, {}};
        const int e = lastError();
#ifndef _WIN32
        if (e == EINTR) continue;
#endif
        if (wouldBlock(e)) return {IoStatus::WouldBlock, 0, {}};
        return {IoStatus::Error, 0, errorText(e)};
    }
}

std::string Socket::peerAddress() const {
    sockaddr_storage addr{};
    auto len = static_cast<SockLen>(sizeof addr);
    if (getpeername(s_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return "?";
    return formatAddress(reinterpret_cast<const sockaddr*>(&addr));
}

uint16_t Socket::localPort() const {
    sockaddr_storage addr{};
    auto len = static_cast<SockLen>(sizeof addr);
    if (getsockname(s_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
    if (addr.ss_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(&addr)->sin_port);
    if (addr.ss_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&addr)->sin6_port);
    return 0;
}

std::string Socket::connectError() const {
    int err = 0;
    auto len = static_cast<SockLen>(sizeof err);
    if (getsockopt(s_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0) return errorText(lastError());
    return err == 0 ? std::string{} : errorText(err);
}

std::expected<Socket, std::string> listenTcp(const std::string& bindAddress, uint16_t port) {
    ensureInit();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
    AddrInfo res;
    const std::string service = std::to_string(port);
    const std::string host = bindAddress.empty() ? "0.0.0.0" : bindAddress;
    if (const int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &res.list); rc != 0 || !res.list)
        return std::unexpected(std::format("cannot resolve bind address '{}': {}", host, resolveError(rc)));

    std::string lastProblem = "no usable address";
    for (addrinfo* ai = res.list; ai; ai = ai->ai_next) {
        Socket s(static_cast<NativeSocket>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)));
        if (!s.valid()) {
            lastProblem = errorText(lastError());
            continue;
        }
        int one = 1;
#ifdef _WIN32
        setsockopt(static_cast<SOCKET>(s.native()), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&one), static_cast<SockLen>(sizeof one));
#else
        setsockopt(s.native(), SOL_SOCKET, SO_REUSEADDR, &one, static_cast<SockLen>(sizeof one));
#endif
        if (::bind(s.native(), ai->ai_addr, static_cast<SockLen>(ai->ai_addrlen)) != 0) {
            lastProblem = errorText(lastError());
            continue;
        }
        if (::listen(s.native(), 16) != 0) {
            lastProblem = errorText(lastError());
            continue;
        }
        setNonBlocking(s.native());
        return s;
    }
    return std::unexpected(std::format("cannot listen on {}:{}: {}", host, port, lastProblem));
}

std::expected<Socket, std::string> connectTcp(const std::string& host, uint16_t port) {
    ensureInit();
    AddrInfo res;
    const std::string service = std::to_string(port);
    // The host listens on IPv4 by default: prefer IPv4 addresses.
    int rc = 0;
    for (int family : {AF_INET, AF_UNSPEC}) {
        addrinfo hints{};
        hints.ai_family = family;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICSERV;
        rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &res.list);
        if (rc == 0 && res.list) break;
    }
    if (rc != 0 || !res.list) return std::unexpected(std::format("cannot find host '{}': {}", host, resolveError(rc)));

    const addrinfo* ai = res.list;
    Socket s(static_cast<NativeSocket>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)));
    if (!s.valid()) return std::unexpected(std::format("cannot create a socket: {}", errorText(lastError())));
    configure(s.native());
    if (::connect(s.native(), ai->ai_addr, static_cast<SockLen>(ai->ai_addrlen)) != 0) {
        const int e = lastError();
        if (!inProgress(e)) return std::unexpected(std::format("cannot connect to {}:{}: {}", host, port, errorText(e)));
    }
    return s;
}

Socket acceptConnection(const Socket& listener) {
    sockaddr_storage addr{};
    auto len = static_cast<SockLen>(sizeof addr);
    const auto raw = ::accept(listener.native(), reinterpret_cast<sockaddr*>(&addr), &len);
    Socket s(static_cast<NativeSocket>(raw));
    if (s.valid()) configure(s.native());
    return s;
}

int pollSockets(std::span<PollItem> items, int timeoutMs) {
#ifdef _WIN32
    std::vector<WSAPOLLFD> fds(items.size());
#else
    std::vector<pollfd> fds(items.size());
#endif
    for (size_t i = 0; i < items.size(); ++i) {
#ifdef _WIN32
        fds[i].fd = static_cast<SOCKET>(items[i].socket);
#else
        fds[i].fd = items[i].socket;
#endif
        fds[i].events = static_cast<short>((items[i].wantRead ? POLLIN : 0) | (items[i].wantWrite ? POLLOUT : 0));
        fds[i].revents = 0;
    }
    if (fds.empty()) return 0;
#ifdef _WIN32
    const int n = WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeoutMs);
#else
    int n = 0;
    do {
        n = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeoutMs);
    } while (n < 0 && errno == EINTR);
#endif
    if (n < 0) return -1;
    for (size_t i = 0; i < items.size(); ++i) {
        const short r = fds[i].revents;
        items[i].failed = (r & (POLLERR | POLLNVAL)) != 0;
        items[i].readable = (r & (POLLIN | POLLHUP | POLLERR)) != 0;
        items[i].writable = (r & POLLOUT) != 0 || items[i].failed;
    }
    return n;
}

std::string localAddressGuess() {
    ensureInit();
    // A UDP "connect" picks the outgoing interface without sending anything.
    Socket s(static_cast<NativeSocket>(::socket(AF_INET, SOCK_DGRAM, 0)));
    if (!s.valid()) return {};
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &target.sin_addr);  // TEST-NET-1, never routed
    if (::connect(s.native(), reinterpret_cast<const sockaddr*>(&target), static_cast<SockLen>(sizeof target)) != 0) return {};
    sockaddr_in local{};
    auto len = static_cast<SockLen>(sizeof local);
    if (getsockname(s.native(), reinterpret_cast<sockaddr*>(&local), &len) != 0) return {};
    char host[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &local.sin_addr, host, sizeof host);
    const std::string out = host;
    return out == "0.0.0.0" ? std::string{} : out;
}

std::expected<Socket, std::string> openUdp(uint16_t port, bool shared) {
    ensureInit();
    Socket s(static_cast<NativeSocket>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)));
    if (!s.valid()) return std::unexpected(errorText(lastError()));
    int one = 1;
    setsockopt(s.native(), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), static_cast<SockLen>(sizeof one));
    if (shared) {
        setsockopt(s.native(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), static_cast<SockLen>(sizeof one));
#ifdef SO_REUSEPORT
        setsockopt(s.native(), SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), static_cast<SockLen>(sizeof one));
#endif
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(s.native(), reinterpret_cast<const sockaddr*>(&addr), static_cast<SockLen>(sizeof addr)) != 0)
        return std::unexpected(std::format("cannot bind UDP port {}: {}", port, errorText(lastError())));
    setNonBlocking(s.native());
    return s;
}

bool sendDatagram(const Socket& s, const std::string& address, uint16_t port, std::span<const uint8_t> data) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &to.sin_addr) != 1) return false;
    const auto n = ::sendto(s.native(), reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), kSendFlags,
                            reinterpret_cast<const sockaddr*>(&to), static_cast<SockLen>(sizeof to));
    return n >= 0 && static_cast<size_t>(n) == data.size();
}

std::optional<Datagram> receiveDatagram(const Socket& s, std::span<uint8_t> buffer) {
    sockaddr_in from{};
    auto len = static_cast<SockLen>(sizeof from);
    const auto n = ::recvfrom(s.native(), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                              reinterpret_cast<sockaddr*>(&from), &len);
    if (n < 0) return std::nullopt;
    char host[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &from.sin_addr, host, sizeof host);
    return Datagram{host, ntohs(from.sin_port), static_cast<size_t>(n)};
}

} // namespace opense4::net
