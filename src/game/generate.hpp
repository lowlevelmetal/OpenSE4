#pragma once

// Quadrant generation driven entirely by the data set (QuadrantTypes,
// SystemTypes, SectType, StellarAbilityTypes, PlanetSize, Settings,
// SystemNames), and empire placement. See docs/spec/01-galaxy-and-setup.md
// §2.2, §3-§5 (confirmed: binary).
//
// Generation is deterministic and uses no host floating point: every random
// draw comes from the Rng passed in, in a fixed order, and the few
// floating-point formulas of the original (square roots, arctangents) are
// reproduced exactly with integers and xmath.hpp.

#include "core/rng.hpp"
#include "game/galaxy.hpp"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace opense4::game {

// Quadrant Size (spec 01 §2.2).
enum class QuadrantSize : uint8_t { Small, Medium, Large };

struct QuadrantOptions {
    std::string quadrantType;  // QuadrantTypes.txt name; empty = the first one
    // The number of systems. 0 rolls it from `size` as the original does
    // (Small q..2q−1, Medium 2q..4q−1, Large 4q..5q−1 with q = Maximum Number
    // Of Systems div 5); a positive count is used as given (our tests and the
    // command line).
    int systemCount = 0;
    QuadrantSize size = QuadrantSize::Medium;
    bool allWarpPointsConnected = true;
    bool noWarpPoints = false;
    bool warpPointsAnywhere = false;
    bool noRuins = false;
    bool finiteResources = false;
};

struct Generated {
    Galaxy galaxy;
    std::vector<std::string> warnings;  // data problems worked around (constraints relaxed, ...)
};

std::expected<Generated, std::string> generateQuadrant(const ruleset::Ruleset& rs, const QuadrantOptions& options, Rng& rng);

// The range of system counts a quadrant size rolls (inclusive).
std::pair<int, int> systemCountRange(const ruleset::Ruleset& rs, QuadrantSize size);
// Settings `Maximum Number Of Systems` (stock 100), at most 255.
int maxSystemCount(const ruleset::Ruleset& rs);

// ---- Empire placement (spec 01 §3.6) --------------------------------------------------------------

struct EmpireStart {
    std::string name;
    std::string surface;     // native planet physical type: "Rock", "Ice", "Gas Giant"
    std::string atmosphere;  // breathed atmosphere
};

// Home Planet Value: Bad, Average, Good.
enum class HomeValue { Low, Medium, High };

struct PlacementOptions {
    bool evenlyDistributed = true;
    bool allowSameSystem = false;
    HomeValue homeValue = HomeValue::Medium;
    bool finiteResources = false;
    bool allPlanetsSameSize = true;  // "All player planets the same size"
};

// Picks each empire's homeworld in player order: a natural planet of the
// empire's atmosphere and planet type, of the home size, in a start-eligible
// system, spread out by warp jumps; a planet is created when none fits. The
// planet itself is not changed (setup gives it its values and colony).
std::expected<std::vector<ObjectId>, std::string> placeHomeworlds(Galaxy& galaxy, const ruleset::Ruleset& rs,
                                                                  std::span<const EmpireStart> empires,
                                                                  const PlacementOptions& options, Rng& rng);

// A planet created for an empire start (spec 01 §3.6): a random natural Planet
// record with this planet type and atmosphere (and stellar size `size`, 0 =
// any), values and conditions as for a natural planet, named after the system
// with the numeral one above the number of occupied sectors. Appends to the
// galaxy's objects.
ObjectId createStartingPlanet(Galaxy& g, const ruleset::Ruleset& rs, SystemId sys, Sector where, std::string_view surface,
                              std::string_view atmosphere, int size, bool finiteResources, Rng& rng);

// ---- Shared by setup and stellar manipulation -------------------------------------------------------

// 1 Tiny .. 5 Huge; 0 for an unknown name.
int sizeOrdinal(std::string_view stellarSize);
// The Stellar Size (as 1..5) of a planet or asteroid object's PlanetSize record.
int stellarSizeOf(const ruleset::Ruleset& rs, const SpaceObject& obj);
// The home size: Small, Medium or Large for Bad, Average or Good, raised to the
// smallest size any natural planet record of that atmosphere and type has.
int homePlanetSize(const ruleset::Ruleset& rs, HomeValue value, std::string_view surface, std::string_view atmosphere);
// SectType records of a kind that are natural (their PlanetSize is not
// Constructed), optionally of one stellar size (0 = any), planet type and
// atmosphere (empty = any).
std::vector<uint32_t> naturalSectorTypes(const ruleset::Ruleset& rs, ObjectKind kind, int size = 0, std::string_view surface = {},
                                         std::string_view atmosphere = {});
// Copies a SectType record's attributes (size, type, atmosphere, star data) onto an object.
void applySectorType(const ruleset::Ruleset& rs, SpaceObject& obj, uint32_t sectorType);
// Conditions of a natural planet (spec 01 §5.6): R[0,10]/10 + 0.5, halved for
// asteroid fields. Stored in hundredths (1.5 = 150), see SpaceObject::conditions.
int rollConditions(bool asteroids, Rng& rng);
// Values and conditions as for a natural planet or asteroid field.
void rollNaturalValues(const ruleset::Ruleset& rs, SpaceObject& obj, bool finiteResources, Rng& rng);
// The sectors of a system that hold no object.
std::vector<Sector> emptySectors(const Galaxy& g, SystemId sys);
// "System IV" style numerals.
std::string romanNumeral(int n);
// The numeral a planet made in this system during play takes: one above the
// highest numeral its planets and asteroid fields carry (inferred, spec 01 §9).
int nextPlanetNumeral(const Galaxy& g, SystemId sys);
// Existing warp points of a system with the bearing toward their destination.
std::vector<PlacedWarpPoint> placedWarpPoints(const Galaxy& g, SystemId sys);
// Warp jumps from `from` to every system (-1: no path).
std::vector<int> warpJumps(const Galaxy& g, SystemId from);

} // namespace opense4::game
