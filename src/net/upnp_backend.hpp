#pragma once

// Internal: shared state of PortMapper and its backend (miniupnpc or none).

#include "net/upnp.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace opense4::net {

struct PortMapper::Impl {
    mutable std::mutex mutex;
    std::condition_variable wake;
    PortMapStatus status;
    bool changed = false;
    bool stopRequested = false;
    std::thread worker;
    uint16_t port = 0;
    PortMapperOptions options;

    void publish(PortMapStatus s) {
        std::lock_guard lock(mutex);
        status = std::move(s);
        changed = true;
    }
    bool stopping() const {
        std::lock_guard lock(mutex);
        return stopRequested;
    }
    // Sleeps until stop() or the timeout; true when stopping.
    bool waitForStop(std::chrono::seconds timeout) {
        std::unique_lock lock(mutex);
        return wake.wait_for(lock, timeout, [&] { return stopRequested; });
    }
};

namespace detail {
bool upnpCompiledIn();
// Worker thread body: discover, map, renew until stopping, then remove.
void runPortMapping(PortMapper::Impl& impl);
} // namespace detail

} // namespace opense4::net
