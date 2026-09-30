#pragma once

// The quadrant (galaxy) model of the classic-compatible rules engine.
// See docs/spec/01-galaxy-and-setup.md. Plain data; behavior lives in
// free functions (generate.hpp, ...).

#include "game/types.hpp"
#include "ruleset/ruleset.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace opense4::game {

// Every system is a 13×13 sector grid, x to the right and y downward, with
// the center at (6, 6) (confirmed by observation, docs/spec/07-observations.md).
inline constexpr int kSystemSize = 13;
inline constexpr int kSystemCenter = kSystemSize / 2;

struct Sector {
    int8_t x = kSystemCenter;
    int8_t y = kSystemCenter;
    constexpr Sector() = default;
    constexpr Sector(int x_, int y_) : x(static_cast<int8_t>(x_)), y(static_cast<int8_t>(y_)) {}
    constexpr bool valid() const { return x >= 0 && y >= 0 && x < kSystemSize && y < kSystemSize; }
    constexpr auto operator<=>(const Sector&) const = default;
};

constexpr int chebyshev(Sector a, Sector b) {
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}

// A sector of a system: where ships, planets and everything else live.
struct Location {
    SystemId system;
    Sector sector;
    constexpr auto operator<=>(const Location&) const = default;
};

// Integer position on the quadrant map (one square ~ 10 light years).
struct GalaxyPos {
    int x = 0;
    int y = 0;
    constexpr auto operator<=>(const GalaxyPos&) const = default;
};

enum class ObjectKind : uint8_t { Star, Planet, Asteroids, Storm, WarpPoint, DestroyedStar, Comet, Count };
std::string_view displayName(ObjectKind k);
std::optional<ObjectKind> parseObjectKind(std::string_view physicalType);  // accepts "Sun", "Star", ...

struct SpaceObject {
    ObjectId id;
    ObjectKind kind = ObjectKind::Planet;
    SystemId system;
    Sector sector;
    uint32_t sectorType = 0;  // index into Ruleset::sectorObjectTypes (picture, description)
    std::string name;
    std::vector<ruleset::Ability> abilities;  // rolled stellar abilities

    // Planets and asteroids.
    std::string size;           // PlanetSize name: "Tiny".."Huge" or a constructed size
    std::string surface;        // "Rock", "Ice", "Gas Giant"
    std::string atmosphere;     // "None", "Oxygen", ...
    int conditions = 50;        // percent, higher is better (band names: see spec 02)
    std::array<int, 3> value{}; // per resource: percent, or remaining stock in finite games

    // Stars.
    std::string starAge, starColor, starLuminosity;

    // Warp points.
    ObjectId destination;  // the paired warp point
    bool oneWay = false;
};

struct StarSystem {
    SystemId id;
    std::string name;
    GalaxyPos position;
    ruleset::SystemTypeId type;
    std::string physicalType;                 // "Normal", "Nebulae", "Black Hole"
    std::vector<ruleset::Ability> abilities;  // system-wide, always present
    std::vector<ObjectId> objects;            // in creation order
};

struct Galaxy {
    int width = 0;   // quadrant grid size in squares
    int height = 0;
    std::string quadrantType;
    std::vector<StarSystem> systems;
    std::vector<SpaceObject> objects;

    StarSystem& system(SystemId id) { return systems[id.index()]; }
    const StarSystem& system(SystemId id) const { return systems[id.index()]; }
    SpaceObject& object(ObjectId id) { return objects[id.index()]; }
    const SpaceObject& object(ObjectId id) const { return objects[id.index()]; }

    // Warp points of a system, in creation order.
    std::vector<ObjectId> warpPoints(SystemId sys) const;
    // Systems reachable by one jump from `sys`.
    std::vector<SystemId> neighbors(SystemId sys) const;
};

} // namespace opense4::game
