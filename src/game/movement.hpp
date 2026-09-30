#pragma once

// Vehicle movement and order execution (docs/spec/03 §6-15, spec 01 §6,
// spec 05 §9.3): pathfinding, the 30-phase simultaneous move, fleets,
// cargo/launch orders, colonization, stellar manipulation, supply and repair.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <optional>
#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::movement {

// A route over sectors the empire knows; `steps` excludes the start.
struct Path {
    std::vector<Location> steps;
    int length = 0;             // movement points needed
};
std::optional<Path> findPath(const Rules& r, const GameState& s, EmpireId e, Location from, Location to);
// Estimated turns to reach `to` at the vehicle's current speed (-1 = unreachable).
int etaTurns(const Rules& r, const GameState& s, const Vehicle& v, Location to);

// Turn phase 3 (start): movement points, sentry checks, emergency energy.
void startTurn(TurnContext& ctx);
// Turn phase 3: executes orders over 30 phases; space combat is triggered via
// combat::resolveSpaceCombat where hostiles meet (spec 05 §9.3).
void runMovementAndCombat(TurnContext& ctx);
// Turn phase 4 (after ground combat): colonization orders that arrived.
void runColonization(TurnContext& ctx);
// Turn phase 10: supply generation and resupply, repair, unit supply use.
void runUpkeep(TurnContext& ctx);

// Places a newly built vehicle (economy uses it); applies the queue's
// automatic Move To waypoint.
Vehicle& spawnVehicle(const Rules& r, GameState& s, EmpireId owner, DesignId design, Location where, int autoWaypoint = -1);

} // namespace opense4::game::movement
