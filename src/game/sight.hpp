#pragma once

// Sight, cloaking and empire knowledge (docs/spec/01 §6).

#include "game/rules.hpp"
#include "game/state.hpp"

namespace opense4::game::sight {

// Recomputes Knowledge::present / visibleVehicles / explored / lastSeen for
// every empire and learns designs of visible vehicles.
void updateKnowledge(const Rules& r, GameState& s);

bool canSeeVehicle(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& v);
bool canSeePlanet(const Rules& r, const GameState& s, EmpireId viewer, ObjectId planet);
// True when the empire currently has presence (a vehicle, colony or scanner reach) in the system.
bool hasPresence(const GameState& s, EmpireId viewer, SystemId sys);

} // namespace opense4::game::sight
