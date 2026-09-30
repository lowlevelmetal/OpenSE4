#include "net/upnp.hpp"

#include "net/socket.hpp"
#include "net/upnp_backend.hpp"

#include <format>

namespace opense4::net {

std::string manualForwardingAdvice(uint16_t port, const std::string& localAddress) {
    return std::format("Players on your local network can join at {}:{}. For Internet play, forward TCP port {} on your router to {} "
                       "(see docs/MULTIPLAYER.md).",
                       localAddress.empty() ? "this computer's address" : localAddress, port, port,
                       localAddress.empty() ? "this computer" : localAddress);
}

PortMapper::PortMapper() : impl_(std::make_unique<Impl>()) {}

PortMapper::~PortMapper() { stop(); }

bool PortMapper::supported() { return detail::upnpCompiledIn(); }

void PortMapper::start(uint16_t port, PortMapperOptions options) {
    stop();
    impl_->port = port;
    impl_->options = std::move(options);
    PortMapStatus s;
    s.internalPort = port;
    if (!impl_->options.enabled || !supported()) {
        s.state = PortMapState::Disabled;
        s.localAddress = localAddressGuess();
        s.message = std::format("{} {}", supported() ? "UPnP port mapping is off." : "This build has no UPnP support.",
                                manualForwardingAdvice(port, s.localAddress));
        impl_->publish(std::move(s));
        return;
    }
    s.state = PortMapState::Discovering;
    s.message = std::format("Looking for a UPnP router to forward TCP port {}...", port);
    impl_->publish(std::move(s));
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopRequested = false;
    }
    impl_->worker = std::thread([impl = impl_.get()] { detail::runPortMapping(*impl); });
}

void PortMapper::stop() {
    if (!impl_->worker.joinable()) return;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopRequested = true;
    }
    impl_->wake.notify_all();
    impl_->worker.join();
}

PortMapStatus PortMapper::status() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->status;
}

std::optional<PortMapStatus> PortMapper::takeUpdate() {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->changed) return std::nullopt;
    impl_->changed = false;
    return impl_->status;
}

} // namespace opense4::net
