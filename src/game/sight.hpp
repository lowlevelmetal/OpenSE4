#pragma once

// Sight, cloaking and empire knowledge (docs/spec/01 §6, confirmed: binary).
//
// Sight is resolved per system. An empire's sensor vector in a system is the
// per-type maximum over its sensor sources there (ships and bases, fighter,
// satellite and drone groups, owned planets; never mine fields), each with the
// baseline EM Active 1, raised to its partners' vectors. Sharing is one-way:
// an empire gets the sensors of every empire it holds a Partnership with, and
// this is repeated five times so chains pass sight along. An object the empire
// does not own is seen when the system is explored and some sight type's
// sensor level reaches the object's obscuration. Partners' objects get no
// exception.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <array>
#include <string>
#include <vector>

namespace opense4::game::sight {

using SightVector = std::array<int, kSightTypes>;

// Recomputes Knowledge::present / visibleVehicles / explored / lastSeen for
// every empire and learns designs of vehicles scanned by long range scanners.
// A system is explored by the empire's own sensor sources; partners' maps are
// shared by diplomacy. Omnipresence and the Galaxy Seen trait reveal every
// system and link.
void updateKnowledge(const Rules& r, GameState& s);

// Live checks (they reflect the current positions, also mid-turn).
bool canSeeVehicle(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& v);
// Planets, asteroid fields, stars, storms and warp points as the viewer's map
// shows them: the stellar bodies of an explored system are remembered, except
// planets and asteroid fields hidden by a storm or nebula, which need current
// sensors that pierce it. Stars, storms and warp points are never hidden.
bool canSeePlanet(const Rules& r, const GameState& s, EmpireId viewer, ObjectId planet);
// True when the viewer has a sensor source in the system (its own or a
// partner's whose sensors it gets), or the game is omnipresent.
bool hasPresence(const Rules& r, const GameState& s, EmpireId viewer, SystemId sys);

// The viewer plus every empire whose sensors reach it through Partnerships
// (the viewer's own Partnership, and chains of them), sorted.
std::vector<EmpireId> sightGroup(const GameState& s, EmpireId viewer);
// The viewer's sensor levels in a system (all zero without a sensor source).
SightVector sensorLevels(const Rules& r, const GameState& s, EmpireId viewer, SystemId sys);
// Obscuration per sight type: baseline 1; cloak levels (a ship's only while it
// is cloaked, a unit group's always); and the storm or nebula around it.
SightVector obscuration(const Rules& r, const GameState& s, const Vehicle& v);
// Obscuration of a stellar body: storms and nebulae hide planets, asteroid
// fields and comets, never stars, storms or warp points.
SightVector planetObscuration(const Rules& r, const GameState& s, ObjectId planet);
constexpr bool detects(const SightVector& sensors, const SightVector& obsc) {
    for (size_t t = 0; t < kSightTypes; ++t)
        if (sensors[t] > 0 && sensors[t] >= obsc[t]) return true;
    return false;
}
// Ships, bases, fighter, satellite and drone groups give sensors and presence;
// mine fields (and troops) do not.
bool isSensorSource(ruleset::VehicleType t);

// Knowledge updates used by movement (arrival, warp transit).
void markExplored(GameState& s, EmpireId e, SystemId sys);
void learnWarpLink(GameState& s, EmpireId e, ObjectId warpPoint);
bool knowsWarpLink(const GameState& s, EmpireId e, ObjectId warpPoint);
// A warp point's name for a viewer: the word for warp point, followed by the
// destination system's name once the viewer has explored it (spec 01 §5.4).
std::string warpPointName(const GameState& s, EmpireId viewer, ObjectId warpPoint);

} // namespace opense4::game::sight
