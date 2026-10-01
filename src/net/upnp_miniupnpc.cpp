// PortMapper backend on miniupnpc (BSD-3-Clause, fetched by
// cmake/Dependencies.cmake).

#include "net/socket.hpp"
#include "net/upnp_backend.hpp"

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace opense4::net::detail {

namespace {

constexpr int kConflictInMappingEntry = 718;
constexpr int kOnlyPermanentLeasesSupported = 725;

std::string upnpError(int code) {
    const char* text = strupnperror(code);
    return text ? std::format("{} ({})", text, code) : std::format("error {}", code);
}

// Owns the IGD description for the lifetime of one mapping.
struct Gateway {
    UPNPUrls urls{};
    IGDdatas data{};
    bool valid = false;
    ~Gateway() {
        if (valid) FreeUPNPUrls(&urls);
    }
    const char* control() const { return urls.controlURL; }
    const char* service() const { return data.first.servicetype; }
};

} // namespace

bool upnpCompiledIn() { return true; }

void runPortMapping(PortMapper::Impl& impl) {
    const uint16_t port = impl.port;
    const PortMapperOptions opt = impl.options;
    PortMapStatus st;
    st.internalPort = port;

    auto noRouter = [&](std::string why) {
        st.state = PortMapState::NoRouter;
        if (st.localAddress.empty()) st.localAddress = localAddressGuess();
        st.message = std::format("{} {}", why, manualForwardingAdvice(port, st.localAddress));
        impl.publish(st);
    };

    int error = 0;
    UPNPDev* devices = upnpDiscover(opt.discoveryTimeoutMs, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, 2, &error);
    if (impl.stopping()) {
        freeUPNPDevlist(devices);
        st.state = PortMapState::Removed;
        st.message = "UPnP: cancelled.";
        impl.publish(st);
        return;
    }
    if (!devices) {
        noRouter("No UPnP router answered.");
        return;
    }
    Gateway gw;
    char lan[64] = {};
    char wan[64] = {};
    const int igd = UPNP_GetValidIGD(devices, &gw.urls, &gw.data, lan, sizeof lan, wan, sizeof wan);
    freeUPNPDevlist(devices);
    gw.valid = igd != UPNP_NO_IGD;
    st.localAddress = lan;
    if (igd == UPNP_NO_IGD || igd == UPNP_UNKNOWN_DEVICE) {
        noRouter("No UPnP Internet gateway was found (UPnP may be off on the router).");
        return;
    }

    char external[64] = {};
    if (UPNP_GetExternalIPAddress(gw.control(), gw.service(), external) != UPNPCOMMAND_SUCCESS || !external[0])
        std::copy_n(wan, sizeof external, external);
    st.externalAddress = external;

    int lease = std::max(0, opt.leaseSeconds);
    const std::string internalPort = std::to_string(port);
    auto add = [&](uint16_t extPort) {
        const std::string ext = std::to_string(extPort);
        int rc = UPNP_AddPortMapping(gw.control(), gw.service(), ext.c_str(), internalPort.c_str(), lan, opt.description.c_str(), "TCP",
                                     nullptr, std::to_string(lease).c_str());
        if (rc == kOnlyPermanentLeasesSupported && lease != 0) {
            lease = 0;
            rc = UPNP_AddPortMapping(gw.control(), gw.service(), ext.c_str(), internalPort.c_str(), lan, opt.description.c_str(), "TCP",
                                     nullptr, "0");
        }
        return rc;
    };

    uint16_t mapped = 0;
    std::string failure;
    for (int k = 0; k <= std::max(0, opt.alternativePorts) && port + k <= 65535; ++k) {
        const auto extPort = static_cast<uint16_t>(port + k);
        const int rc = add(extPort);
        if (rc == UPNPCOMMAND_SUCCESS) {
            mapped = extPort;
            break;
        }
        if (rc == kConflictInMappingEntry) {
            // Taken. If it is our own mapping left over from an earlier run, replace it.
            const std::string ext = std::to_string(extPort);
            char client[64] = {}, inPort[8] = {}, desc[80] = {}, enabled[4] = {}, duration[16] = {};
            if (UPNP_GetSpecificPortMappingEntry(gw.control(), gw.service(), ext.c_str(), "TCP", nullptr, client, inPort, desc, enabled,
                                                 duration) == UPNPCOMMAND_SUCCESS &&
                std::string_view(client) == lan && std::string_view(inPort) == internalPort) {
                UPNP_DeletePortMapping(gw.control(), gw.service(), ext.c_str(), "TCP", nullptr);
                if (add(extPort) == UPNPCOMMAND_SUCCESS) {
                    mapped = extPort;
                    break;
                }
            }
            failure = std::format("port {} is already forwarded to another computer", extPort);
            continue;
        }
        failure = upnpError(rc);
        break;
    }
    if (!mapped) {
        st.state = PortMapState::Failed;
        st.message = std::format("The router refused to forward TCP port {} ({}). {}", port, failure, manualForwardingAdvice(port, lan));
        impl.publish(st);
        return;
    }

    st.state = PortMapState::Mapped;
    st.externalPort = mapped;
    st.message = std::format("UPnP: the router forwards {}:{} to this computer ({}:{}). Players join at {}:{}.", st.externalAddress, mapped,
                             lan, port, st.externalAddress, mapped);
    if (igd == UPNP_PRIVATEIP_IGD)
        st.message += " Warning: the router's own address is private (double NAT or carrier-grade NAT), so players outside may still "
                      "not reach you.";
    else if (igd == UPNP_DISCONNECTED_IGD)
        st.message += " Warning: the router reports that it is not connected to the Internet.";
    impl.publish(st);

    // Renew at half the lease until stopped.
    const bool renew = lease > 0;
    const std::chrono::seconds period(renew ? std::max(30, lease / 2) : 3600);
    while (!impl.waitForStop(period)) {
        if (!renew) continue;
        if (const int rc = add(mapped); rc != UPNPCOMMAND_SUCCESS) {
            PortMapStatus failed = st;
            failed.state = PortMapState::Failed;
            failed.message = std::format("UPnP: renewing the forwarding of port {} failed ({}). {}", mapped, upnpError(rc),
                                         manualForwardingAdvice(port, lan));
            impl.publish(failed);
        }
    }
    const std::string ext = std::to_string(mapped);
    UPNP_DeletePortMapping(gw.control(), gw.service(), ext.c_str(), "TCP", nullptr);
    st.state = PortMapState::Removed;
    st.message = std::format("UPnP: removed the forwarding of port {}.", mapped);
    impl.publish(st);
}

} // namespace opense4::net::detail
