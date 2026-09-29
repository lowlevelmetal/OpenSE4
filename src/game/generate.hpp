#pragma once

// Quadrant generation driven entirely by the data set (QuadrantTypes,
// SystemTypes, SectType, StellarAbilityTypes, PlanetSize, Settings,
// SystemNames). See docs/spec/01-galaxy-and-setup.md §3-5.

#include "core/rng.hpp"
#include "game/galaxy.hpp"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace opense4::game {

struct QuadrantOptions {
    std::string quadrantType;  // QuadrantTypes.txt name; empty = the first one
    int systemCount = 40;
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

// Home placement (spec 01 §3.6).
struct EmpireStart {
    std::string name;
    std::string surface;     // native planet physical type: "Rock", "Ice", "Gas Giant"
    std::string atmosphere;  // breathed atmosphere
};

enum class HomeValue { Low, Medium, High };

struct PlacementOptions {
    bool evenlyDistributed = true;
    bool allowSameSystem = false;
    HomeValue homeValue = HomeValue::Medium;
    bool finiteResources = false;
};

// Picks home systems (start-eligible system types only) and converts one
// planet in each to suit the empire. Returns each empire's homeworld.
std::expected<std::vector<ObjectId>, std::string> placeHomeworlds(Galaxy& galaxy, const ruleset::Ruleset& rs,
                                                                  std::span<const EmpireStart> empires,
                                                                  const PlacementOptions& options, Rng& rng);

} // namespace opense4::game
