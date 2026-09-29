#include "sim/types.hpp"

#include <algorithm>

namespace opense4::sim {

namespace {

constexpr std::array<std::string_view, kResourceCount> kResourceNames{"Minerals", "Organics", "Radioactives"};
constexpr std::array<std::string_view, kResourceCount> kResourceKeys{"minerals", "organics", "radioactives"};

constexpr std::array<std::string_view, enumIndex(StarClass::Count)> kStarNames{
    "Blue Giant", "White Star", "Yellow Star", "Orange Star", "Red Dwarf", "White Dwarf", "Neutron Star"};

constexpr std::array<std::string_view, enumIndex(PlanetSurface::Count)> kSurfaceNames{"Rock", "Ice", "Gas Giant"};
constexpr std::array<std::string_view, enumIndex(PlanetSurface::Count)> kSurfaceKeys{"rock", "ice", "gas"};

constexpr std::array<std::string_view, enumIndex(Atmosphere::Count)> kAtmosphereNames{
    "None", "Oxygen", "Hydrogen", "Carbon Dioxide", "Methane"};
constexpr std::array<std::string_view, enumIndex(Atmosphere::Count)> kAtmosphereKeys{
    "none", "oxygen", "hydrogen", "carbon_dioxide", "methane"};

constexpr std::array<std::string_view, enumIndex(PlanetSize::Count)> kSizeNames{"Tiny", "Small", "Medium", "Large", "Huge"};
constexpr std::array<std::string_view, enumIndex(PlanetSize::Count)> kSizeKeys{"tiny", "small", "medium", "large", "huge"};

constexpr std::array<std::string_view, enumIndex(GalaxyShape::Count)> kShapeNames{"Spiral", "Elliptical", "Ring", "Clusters"};
constexpr std::array<std::string_view, enumIndex(GalaxyShape::Count)> kShapeKeys{"spiral", "elliptical", "ring", "clusters"};

template <class E, size_t N>
std::optional<E> parseKey(const std::array<std::string_view, N>& keys, std::string_view s) {
    const auto it = std::find(keys.begin(), keys.end(), s);
    if (it == keys.end()) return std::nullopt;
    return static_cast<E>(it - keys.begin());
}

} // namespace

std::string_view displayName(ResourceType r) { return kResourceNames[enumIndex(r)]; }
std::string_view displayName(StarClass v) { return kStarNames[enumIndex(v)]; }
std::string_view displayName(PlanetSurface v) { return kSurfaceNames[enumIndex(v)]; }
std::string_view displayName(Atmosphere v) { return kAtmosphereNames[enumIndex(v)]; }
std::string_view displayName(PlanetSize v) { return kSizeNames[enumIndex(v)]; }
std::string_view displayName(GalaxyShape v) { return kShapeNames[enumIndex(v)]; }

std::optional<PlanetSurface> parsePlanetSurface(std::string_view s) { return parseKey<PlanetSurface>(kSurfaceKeys, s); }
std::optional<Atmosphere> parseAtmosphere(std::string_view s) { return parseKey<Atmosphere>(kAtmosphereKeys, s); }
std::optional<PlanetSize> parsePlanetSize(std::string_view s) { return parseKey<PlanetSize>(kSizeKeys, s); }
std::optional<GalaxyShape> parseGalaxyShape(std::string_view s) { return parseKey<GalaxyShape>(kShapeKeys, s); }
std::optional<ResourceType> parseResourceType(std::string_view s) { return parseKey<ResourceType>(kResourceKeys, s); }

} // namespace opense4::sim
