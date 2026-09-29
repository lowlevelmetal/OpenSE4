#pragma once

// Fundamental simulation types shared by content (rules data) and game state.

#include "core/id.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

namespace opense4::sim {

// --- Game-state entity ids ---------------------------------------------------
using SystemId = Id<struct SystemTag>;
using PlanetId = Id<struct PlanetTag>;
using WarpPointId = Id<struct WarpPointTag>;
using EmpireId = Id<struct EmpireTag>;
using ShipId = Id<struct ShipTag>;
using DesignId = Id<struct DesignTag>;

// --- Content (rules database) indices ----------------------------------------
using TechIndex = Id<struct TechTag>;
using HullIndex = Id<struct HullTag>;
using ComponentIndex = Id<struct ComponentTag>;
using FacilityIndex = Id<struct FacilityTag>;
using RaceIndex = Id<struct RaceTag>;

// --- Resources -----------------------------------------------------------------
enum class ResourceType : uint8_t { Minerals, Organics, Radioactives };
inline constexpr int kResourceCount = 3;
inline constexpr std::array<ResourceType, kResourceCount> kAllResources{
    ResourceType::Minerals, ResourceType::Organics, ResourceType::Radioactives};

std::string_view displayName(ResourceType r);

struct Resources {
    std::array<int64_t, kResourceCount> amounts{};

    constexpr Resources() = default;
    constexpr Resources(int64_t minerals, int64_t organics, int64_t radioactives)
        : amounts{minerals, organics, radioactives} {}

    constexpr int64_t& operator[](ResourceType t) { return amounts[static_cast<size_t>(t)]; }
    constexpr int64_t operator[](ResourceType t) const { return amounts[static_cast<size_t>(t)]; }

    constexpr Resources operator+(const Resources& o) const {
        Resources r;
        for (size_t i = 0; i < amounts.size(); ++i) r.amounts[i] = amounts[i] + o.amounts[i];
        return r;
    }
    constexpr Resources operator-(const Resources& o) const {
        Resources r;
        for (size_t i = 0; i < amounts.size(); ++i) r.amounts[i] = amounts[i] - o.amounts[i];
        return r;
    }
    constexpr Resources operator*(int64_t s) const {
        Resources r;
        for (size_t i = 0; i < amounts.size(); ++i) r.amounts[i] = amounts[i] * s;
        return r;
    }
    constexpr Resources& operator+=(const Resources& o) { return *this = *this + o; }
    constexpr Resources& operator-=(const Resources& o) { return *this = *this - o; }
    constexpr bool operator==(const Resources&) const = default;

    // True if every amount is >= the corresponding amount in `cost`.
    constexpr bool covers(const Resources& cost) const {
        for (size_t i = 0; i < amounts.size(); ++i)
            if (amounts[i] < cost.amounts[i]) return false;
        return true;
    }
    constexpr bool isZero() const { return amounts[0] == 0 && amounts[1] == 0 && amounts[2] == 0; }
    constexpr int64_t total() const { return amounts[0] + amounts[1] + amounts[2]; }
};

constexpr Resources componentMin(const Resources& a, const Resources& b) {
    Resources r;
    for (size_t i = 0; i < a.amounts.size(); ++i) r.amounts[i] = a.amounts[i] < b.amounts[i] ? a.amounts[i] : b.amounts[i];
    return r;
}

// --- Positions ------------------------------------------------------------------
// Each star system is a square grid of sectors centered on the star at (0,0),
// spanning [-radius, radius] on both axes.
struct SectorPos {
    int16_t x = 0;
    int16_t y = 0;

    constexpr SectorPos() = default;
    constexpr SectorPos(int x_, int y_) : x(static_cast<int16_t>(x_)), y(static_cast<int16_t>(y_)) {}
    constexpr auto operator<=>(const SectorPos&) const = default;
};

// Moves are 8-directional, so distance is Chebyshev distance.
constexpr int sectorDistance(SectorPos a, SectorPos b) {
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}

struct Location {
    SystemId system;
    SectorPos sector;
    constexpr auto operator<=>(const Location&) const = default;
};

// --- Celestial classification ---------------------------------------------------
enum class StarClass : uint8_t { Blue, White, Yellow, Orange, Red, WhiteDwarf, Neutron, Count };
enum class PlanetSurface : uint8_t { Rock, Ice, Gas, Count };
enum class Atmosphere : uint8_t { None, Oxygen, Hydrogen, CarbonDioxide, Methane, Count };
enum class PlanetSize : uint8_t { Tiny, Small, Medium, Large, Huge, Count };
enum class GalaxyShape : uint8_t { Spiral, Elliptical, Ring, Clusters, Count };

std::string_view displayName(StarClass v);
std::string_view displayName(PlanetSurface v);
std::string_view displayName(Atmosphere v);
std::string_view displayName(PlanetSize v);
std::string_view displayName(GalaxyShape v);

// Parse lower_snake_case keys as used in data files ("carbon_dioxide", "gas").
std::optional<PlanetSurface> parsePlanetSurface(std::string_view s);
std::optional<Atmosphere> parseAtmosphere(std::string_view s);
std::optional<PlanetSize> parsePlanetSize(std::string_view s);
std::optional<GalaxyShape> parseGalaxyShape(std::string_view s);
std::optional<ResourceType> parseResourceType(std::string_view s);

template <class E>
constexpr size_t enumIndex(E e) { return static_cast<size_t>(e); }

} // namespace opense4::sim
