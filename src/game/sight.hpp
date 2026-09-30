#pragma once

// Sight, cloaking and empire knowledge (docs/spec/01 §6).
//
// Sight is resolved per system. An empire's sensor vector in a system is the
// per-type maximum over everything its sight group (itself plus Partnership
// chains) has there; every asset carries the baseline EM Active 1. A foreign
// vehicle is seen when some sight type's sensor level reaches its obscuration.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <array>
#include <vector>

namespace opense4::game::sight {

using SightVector = std::array<int, kSightTypes>;

// Recomputes Knowledge::present / visibleVehicles / explored / lastSeen for
// every empire (partners share their live view; diplomacy shares their maps
// and designs) and learns designs of vehicles scanned by long range scanners.
// Omnipresence and the Galaxy Seen trait reveal every system and link.
void updateKnowledge(const Rules& r, GameState& s);

// Live checks (they reflect the current positions, also mid-turn).
bool canSeeVehicle(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& v);
bool canSeePlanet(const Rules& r, const GameState& s, EmpireId viewer, ObjectId planet);
// True when the empire's sight group has a vehicle or colony in the system
// (or the game is omnipresent).
bool hasPresence(const GameState& s, EmpireId viewer, SystemId sys);

// The viewer plus every empire linked to it by Partnership chains, sorted.
std::vector<EmpireId> sightGroup(const GameState& s, EmpireId viewer);
// The viewer's sensor levels in a system (all zero without presence).
SightVector sensorLevels(const Rules& r, const GameState& s, EmpireId viewer, SystemId sys);
// Obscuration per sight type: baseline 1, cloak (components only while
// cloaked, hull always), and storm/nebula obscuration (not for units).
SightVector obscuration(const Rules& r, const GameState& s, const Vehicle& v);
SightVector planetObscuration(const GameState& s, ObjectId planet);
constexpr bool detects(const SightVector& sensors, const SightVector& obsc) {
    for (size_t t = 0; t < kSightTypes; ++t)
        if (sensors[t] > 0 && sensors[t] >= obsc[t]) return true;
    return false;
}

// Knowledge updates used by movement (arrival, warp transit).
void markExplored(GameState& s, EmpireId e, SystemId sys);
void learnWarpLink(GameState& s, EmpireId e, ObjectId warpPoint);
bool knowsWarpLink(const GameState& s, EmpireId e, ObjectId warpPoint);

} // namespace opense4::game::sight
