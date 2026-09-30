#pragma once

// The quadrant (galaxy) model of the classic-compatible rules engine.
// See docs/spec/01-galaxy-and-setup.md. Plain data; behavior lives in
// free functions (generate.hpp, ...).

#include "game/types.hpp"
#include "ruleset/ruleset.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
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
// The quadrant is 67 × 46 squares (spec 01 §3.2, confirmed: binary). The spec
// numbers the squares from 1 (x 1..67, y 1..46, y growing down the map);
// GalaxyPos counts from 0, so the spec's x is pos.x + 1. Distances and
// bearings do not depend on that offset.
struct GalaxyPos {
    int x = 0;
    int y = 0;
    constexpr auto operator<=>(const GalaxyPos&) const = default;
};

inline constexpr int kQuadrantWidth = 67;
inline constexpr int kQuadrantHeight = 46;
// A system holds at most this many warp points (spec 01 §3.5, §8; confirmed: binary).
inline constexpr int kMaxWarpPoints = 10;

// ---- Galaxy geometry (spec 01 §3.2 and §3.5, confirmed: binary) -------------------------------
// The original computes these in floating point. We reproduce them exactly
// without host floating point: square roots with integers, and the
// arctangents with the extended-precision emulation of xmath.hpp.

// round(√(dx² + dy²)), in squares.
int galaxyDistance(GalaxyPos a, GalaxyPos b);
// Whole degrees from `from` toward `to`: 0 points up the map (smaller y) and
// the angle grows clockwise, so 90 points toward larger x. 0 when they coincide.
int galaxyBearing(GalaxyPos from, GalaxyPos to);
// |a − b|, measured across north when one bearing is above 270 and the other
// below 90 (360 − the larger + the smaller). 260 and 10 differ by 250.
int bearingDifference(int a, int b);

// The square-outline function: maps a bearing (0..360) onto the outline of a
// square of side 2r whose top-left corner is (0, 0). r = twiceR / 2, so warp
// points (r = 6.5) pass 13.
struct OutlinePoint {
    int x = 0;
    int y = 0;
    constexpr bool operator==(const OutlinePoint&) const = default;
};
OutlinePoint squareOutline(int bearing, int twiceR);

// A warp point already in a system: where it is and the bearing toward the
// system it leads to.
struct PlacedWarpPoint {
    Sector sector;
    int bearing = 0;
};
// Default (edge) placement of a warp point whose destination lies at `bearing`
// (spec 01 §3.5): the outline point for r = 6.5, nudged half a square along the
// edge and rounded half to even; the nudge is reversed when a warp point in
// `existing` stands on that sector and `bearing` is smaller than its bearing.
Sector warpEdgeSector(int bearing, std::span<const PlacedWarpPoint> existing);
// With "warp points located anywhere": the outline point moved `inward`
// (0..4) squares straight toward the inside of the system.
Sector warpInwardSector(int bearing, int inward);

enum class ObjectKind : uint8_t { Star, Planet, Asteroids, Storm, WarpPoint, DestroyedStar, Comet, Count };
// Once generated, a destroyed star is a star for every rule: the star
// manipulations, the one-star limit of Create Star, the star Create Planet
// needs, and the per-star solar supply and solar resource generation
// (spec 01 §5.1, §5.4, confirmed: binary).
constexpr bool isStarKind(ObjectKind k) { return k == ObjectKind::Star || k == ObjectKind::DestroyedStar; }
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
    int conditions = 50;        // hundredths of the 0-1.5 scale (100 = 1.0), higher is better (spec 02 §2)
    std::array<int, 3> value{}; // per resource: percent, or remaining stock in finite games

    // Stars.
    std::string starAge, starColor, starLuminosity;

    // Warp points.
    ObjectId destination;  // the paired warp point; every link works both ways (the data's
                           // `Warp Point One-Way` flag is never used, spec 01 §3.5, confirmed: binary)
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

// A starting point of a map (spec 01 §12, §3.6): the sector where an empire's
// homeworld goes. `player` is the player slot (EmpireId index) the point is
// reserved for, or kCommonStart for a point any player may take.
inline constexpr int kCommonStart = -1;
struct StartingPoint {
    SystemId system;
    Sector sector;
    int player = kCommonStart;
    constexpr bool operator==(const StartingPoint&) const = default;
};

struct Galaxy {
    int width = 0;   // quadrant grid size in squares (kQuadrantWidth × kQuadrantHeight when generated)
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
