// PortMapper backend for builds without UPnP (OPENSE4_ENABLE_UPNP=OFF):
// PortMapper::start() reports that the port must be forwarded manually.

#include "net/upnp_backend.hpp"

namespace opense4::net::detail {

bool upnpCompiledIn() { return false; }

void runPortMapping(PortMapper::Impl&) {}

} // namespace opense4::net::detail
