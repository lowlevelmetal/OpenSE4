#pragma once

// Automatic port forwarding for hosts behind a home router (UPnP IGD, via
// miniupnpc). Runs on its own thread: start() returns at once, and the
// result arrives through status()/takeUpdate(). The mapping is renewed while
// the mapper lives and removed by stop() or the destructor.
//
// Builds configured with OPENSE4_ENABLE_UPNP=OFF get a mapper that only
// reports how to forward the port manually.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace opense4::net {

struct PortMapperOptions {
    bool enabled = true;
    std::string description = "OpenSE4";
    int leaseSeconds = 3600;       // renewed at half-time; routers that only allow permanent leases get 0
    int discoveryTimeoutMs = 2000;
    int alternativePorts = 8;      // external ports tried after the host port when it is taken
};

enum class PortMapState : uint8_t {
    Disabled,      // switched off (or not compiled in); forward the port manually
    Discovering,   // looking for a router
    Mapped,        // externalAddress:externalPort reaches this machine
    NoRouter,      // no UPnP router answered
    Failed,        // the router refused the mapping
    Removed,       // the mapping was removed (shutdown)
};

struct PortMapStatus {
    PortMapState state = PortMapState::Disabled;
    uint16_t internalPort = 0;
    uint16_t externalPort = 0;
    std::string externalAddress;
    std::string localAddress;
    std::string message;           // one line for the player: result or manual-forwarding advice
};

class PortMapper {
public:
    PortMapper();
    ~PortMapper();
    PortMapper(const PortMapper&) = delete;
    PortMapper& operator=(const PortMapper&) = delete;

    // True when this build includes UPnP support.
    static bool supported();

    // Maps TCP `port` (a running mapping is removed first).
    void start(uint16_t port, PortMapperOptions options = {});
    // Removes the mapping; waits for the worker (a few seconds at most).
    void stop();

    PortMapStatus status() const;
    // The status, once after each change; nullopt when unchanged.
    std::optional<PortMapStatus> takeUpdate();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// Advice shown when the port has to be forwarded by hand.
std::string manualForwardingAdvice(uint16_t port, const std::string& localAddress);

} // namespace opense4::net
