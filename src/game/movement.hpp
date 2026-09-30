#pragma once

// Vehicle movement and order execution (docs/spec/03 §6-15, spec 01 §6-9,
// spec 05 §9.3): pathfinding, the 30-phase simultaneous move, fleets,
// cargo/launch orders, colonization, stellar manipulation, supply and repair.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::movement {

// ---- Pathfinding ---------------------------------------------------------------------------

// A route over sectors the empire knows; `steps` excludes the start. A warp
// jump is one step (its location is the far end of the link).
struct Path {
    std::vector<Location> steps;
    int length = 0;             // movement points needed
    bool operator==(const Path&) const = default;
};

struct RouteOptions {
    bool allowWarp = true;      // fighters cannot use warp points
};

// Shortest route using only what empire `e` knows: systems it explored and
// warp links it traversed (an invalid empire routes omnisciently). Systems
// to avoid are avoided whenever an avoid-free route exists, tagged
// minefields are never entered unless they are the destination, and known
// hazard sectors lose ties.
std::optional<Path> findPath(const Rules& r, const GameState& s, EmpireId e, Location from, Location to);

// Route to the nearest of several goals; `goal` is the index of the one reached
// (equal routes: the earlier goal).
struct NearestPath {
    Path path;
    size_t goal = 0;
};
std::optional<NearestPath> findPathToNearest(const Rules& r, const GameState& s, EmpireId e, Location from,
                                             std::span<const Location> goals, RouteOptions options = {});

// Estimated turns to reach `to` at the vehicle's current speed (its fleet's,
// if it is in one); 0 when already there, -1 = unreachable or immobile.
int etaTurns(const Rules& r, const GameState& s, const Vehicle& v, Location to);

// ---- The simultaneous turn (spec 05 §9.3) -------------------------------------------------------

inline constexpr int kDaysPerTurn = 30;
// Moves a vehicle of speed `speed` has made by the end of `day` (1..30): the
// k-th move happens on day ceil(k * 30 / speed).
constexpr int movesByDay(int speed, int day) { return speed <= 0 ? 0 : speed * day / kDaysPerTurn; }

// Movement points of the slowest member of a fleet (its speed).
int fleetSpeed(const Rules& r, const GameState& s, const Fleet& f);

// Turn phase 3 (start): movement points for the turn, sentry checks.
void startTurn(TurnContext& ctx);

// How movement asks for space combat. The default calls combat::combatPossible
// and combat::resolveSpaceCombat; tests substitute their own.
struct CombatHooks {
    std::function<bool(const Rules&, const GameState&, Location)> possible;
    std::function<void(TurnContext&, Location)> resolve;
};
CombatHooks defaultCombatHooks();

// Turn phase 3: executes orders over 30 phases (days). After every phase, each
// sector where a vehicle executed an order is offered to combat once (the
// v1.42 rule); vehicles that fought stop for the rest of the turn and lose
// their orders. Hazards (black hole pull, random drift, centre and sector
// damage) apply after the last phase.
void runMovementAndCombat(TurnContext& ctx);
void runMovementAndCombat(TurnContext& ctx, const CombatHooks& hooks);

// Turn phase 4 (after ground combat): colony ships that reached their target colonize.
void runColonization(TurnContext& ctx);
// Turn phase 10: repair, training, unit supply use, resupply, fleet supply pooling.
void runUpkeep(TurnContext& ctx);

// ---- Shared helpers (AI, UI, other subsystems) --------------------------------------------------

// Why this vehicle cannot colonize that planet (empty = it can, ignoring distance).
std::string colonizeProblem(const Rules& r, const GameState& s, const Vehicle& v, ObjectId planet);
// A resupply depot `empire` may use in this sector (own or allied colony with Supply Generation).
bool resupplyDepotAt(const Rules& r, const GameState& s, EmpireId empire, Location where);
// Component repair capacity (components per turn) `empire` has in this sector.
int repairCapacityAt(const Rules& r, const GameState& s, EmpireId empire, Location where);
// Supply one step costs this vehicle (engines' Supply Amount Used, racial Supply Cost applied).
int64_t moveSupplyCost(const Rules& r, const GameState& s, const Vehicle& v);
// Applies `amount` normal damage outside combat (no shields): armor first, then
// other components, picked at random. Unit groups take it once for all
// members. Returns true when the vehicle was destroyed (count set to 0).
bool damageVehicle(const Rules& r, GameState& s, Vehicle& v, int amount);

// Places a newly built vehicle (economy uses it); applies the queue's
// automatic Move To waypoint.
Vehicle& spawnVehicle(const Rules& r, GameState& s, EmpireId owner, DesignId design, Location where, int autoWaypoint = -1);

} // namespace opense4::game::movement
