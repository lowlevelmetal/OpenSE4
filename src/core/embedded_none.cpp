// Builds without built-in resources (OPENSE4_EMBED_RESOURCES=OFF).

#include "core/embedded.hpp"

namespace opense4 {

std::span<const unsigned char> embeddedResource(std::string_view) { return {}; }
std::vector<std::string_view> embeddedResourcePaths(std::string_view) { return {}; }

} // namespace opense4
