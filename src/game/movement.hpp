#pragma once

// Vehicle movement and order execution (docs/spec/03 §6-15, spec 01 §7-9,
// spec 05 §9.3): pathfinding, the 30-day simultaneous move, fleets,
// cargo/launch orders, colonization, stellar manipulation, supply and repair.
//
// Turn order (spec 05 §8). turn.cpp calls the aggregate entry points below.
// The per-empire steps exist so that the planned turn-order change can run
// them inside each empire's end-of-turn processing, in this order:
//   step 5  movement and space combat   startTurn + runMovementAndCombat
//   step 6  per empire, after its maintenance (step 7 of the list there):
//             11 repair                 repairEmpire
//             13 supply                 supplyEmpire
//             15 training               trainEmpire
//   step 7  design cleanup (new year)   purgeObsoleteDesigns
//   step 9  event step, first           runStellarHazards
// runUpkeep runs repair, supply and training for every empire in empire order,
// then the design cleanup when a new year starts.

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
// warp links it traversed (an invalid empire routes omnisciently). Systems to
// avoid are never crossed (except the start and destination systems); when no
// other route exists there is none (spec 03 §6.2). Only the first 10 warp
// points of a system are used. Tagged minefields are never entered unless they
// are the destination, and known hazard sectors lose ties.
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

// ---- The simultaneous turn (spec 03 §6.3) -------------------------------------------------------

inline constexpr int kDaysPerTurn = 30;
// Actions a vehicle of speed `speed` has made by the end of `day` (1..30) with
// the exact day counter: the k-th on day ceil(k * 30 / speed), at most one a day.
constexpr int movesByDay(int speed, int day) {
    if (speed <= 0) return 0;
    const int exact = speed * day / kDaysPerTurn;
    return exact < day ? exact : day;
}

// The day counter gains speed/30 a day; the vehicle acts when it reaches 1
// (spec 03 §6.3, confirmed: binary). The original keeps it in floating point,
// stored as a 64-bit double each day, which can lose a speed's last step
// depending on the x87 precision at run time. That is open (spec 03 §19 Q8),
// so the engine counts exactly, as the spec recommends; `Double` reproduces
// the stored double with extended-precision arithmetic (xmath::Ext).
enum class DayCounterMode : uint8_t { Exact, Double };
inline constexpr DayCounterMode kDayCounterMode = DayCounterMode::Exact;  // (inferred) until observed
// The days (1..30) a vehicle of this speed acts on under a counter mode.
std::vector<int> actionDays(int speed, DayCounterMode mode = kDayCounterMode);

// Movement points of the slowest member of a fleet (its speed).
int fleetSpeed(const Rules& r, const GameState& s, const Fleet& f);

// Turn start: every vehicle's movement points reset to its maximum, fleet
// members in the fleet's sector to the fleet's lowest maximum.
void startTurn(TurnContext& ctx);

// How movement asks for space combat. The default calls combat::combatPossible
// and combat::resolveSpaceCombat; tests substitute their own.
struct CombatHooks {
    std::function<bool(const Rules&, const GameState&, Location)> possible;
    std::function<void(TurnContext&, Location)> resolve;
};
CombatHooks defaultCombatHooks();

// The 30-day movement phase (spec 03 §6.3). Each vehicle (fleet, planet with
// orders) keeps a day counter that gains speed/30 a day; when it reaches 1 the
// vehicle carries out one order execution. After each day every sector where
// something acted is offered to combat, unless everything there already
// fought there this turn. Combat neither stops movement nor clears orders; a
// Sentry order at the head of a participant's list is removed.
void runMovementAndCombat(TurnContext& ctx);
void runMovementAndCombat(TurnContext& ctx, const CombatHooks& hooks);

// Turn phase 4 (after ground combat): colony ships that reached their target colonize.
void runColonization(TurnContext& ctx);

// ---- End of turn -------------------------------------------------------------------------------

// Repair (spec 03 §13): each (empire, sector) pool restores destroyed components.
void repairEmpire(TurnContext& ctx, EmpireId e);
// Supply (spec 03 §7, §12): unit and cloak upkeep, depot refills, fleet
// pooling, solar collectors, then drones at 0 are lost and cloaks at 0 drop.
void supplyEmpire(TurnContext& ctx, EmpireId e);
// Ship and fleet training (spec 03 §3.3): every own source in turn.
void trainEmpire(TurnContext& ctx, EmpireId e);
// Obsolete designs with no vehicles, no queue entries and no living foreign
// empire that knows them are removed (spec 03 §4.1). Runs when a year starts.
void purgeObsoleteDesigns(TurnContext& ctx);
// Repair, supply and training for every empire, then the design cleanup at a new year.
void runUpkeep(TurnContext& ctx);

// The event step's hazards (spec 01 §7): black-hole pull, random drift toward
// one target sector shared by all systems, then centre damage. Moves every
// ship, base and unit group; spends no movement or supply; starts no combat.
void runStellarHazards(TurnContext& ctx);

// ---- Shared helpers (AI, UI, other subsystems) --------------------------------------------------

// Why this vehicle cannot colonize that planet (empty = it can, ignoring distance).
std::string colonizeProblem(const Rules& r, const GameState& s, const Vehicle& v, ObjectId planet);
// A resupply depot `empire` may use in this sector: a colonized planet with
// Supply Generation, its own or of an empire it has a Military Alliance or
// Partnership with. No population needed (spec 03 §7).
bool resupplyDepotAt(const Rules& r, const GameState& s, EmpireId empire, Location where);
// Σ Component Repair of the empire's own ships, bases and populated colonies in the sector.
int64_t repairPoolAt(const Rules& r, const GameState& s, EmpireId empire, Location where);
// The empire's repair modifier R: racial Repair + (Repair Aptitude − 100) + culture Repair.
int64_t repairModifier(const Rules& r, const GameState& s, EmpireId empire);
// Components per turn the empire repairs in this sector: truncate(pool × (100 + R) %).
int repairCapacityAt(const Rules& r, const GameState& s, EmpireId empire, Location where);
// Supply one step costs this vehicle (spec 03 §7; a unit group pays for every unit).
int64_t moveSupplyCost(const Rules& r, const GameState& s, const Vehicle& v);
// An empire's units in space (not in cargo): what the units-per-player cap counts (§12).
int unitsInSpace(const Rules& r, const GameState& s, EmpireId owner);
// Applies `amount` normal damage outside combat (no shields): armor first, then
// other components, picked at random. Unit groups take it once for all
// members. Returns true when the vehicle was destroyed (count set to 0).
bool damageVehicle(const Rules& r, GameState& s, Vehicle& v, int amount);

// Places a newly built vehicle (economy uses it); applies the queue's
// automatic Move To waypoint.
Vehicle& spawnVehicle(const Rules& r, GameState& s, EmpireId owner, DesignId design, Location where, int autoWaypoint = -1);

} // namespace opense4::game::movement
