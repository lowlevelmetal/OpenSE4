#pragma once

// Vehicle movement and order execution (docs/spec/03 §6-15, spec 01 §7-9,
// spec 05 §9.3): pathfinding, the 30-day simultaneous move, fleets,
// cargo/launch orders, colonization, stellar manipulation, supply and repair.
//
// Turn order (spec 05 §8, turn.cpp):
//   step 5  movement and space combat   startTurn + runMovementAndCombat,
//                                       then runColonization
//   step 6  each empire's end-of-turn processing (the numbers of that list):
//             11 repair                 repairEmpire
//             13 supply                 supplyEmpire
//             15 training               trainEmpire
//   step 7  design cleanup (new year)   purgeObsoleteDesigns
//   step 9  event step, first           runStellarHazards

#include "game/rules.hpp"
#include "game/state.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
    bool sweeper = false;       // led by a mine sweeper: tagged minefields are no obstacle (leadsSweeperGroup)
};

// A group led by this vehicle ignores the owner's tagged minefields: its
// design has the mine sweeper design type (spec 03 §6.2; the type is inferred).
bool leadsSweeperGroup(const GameState& s, const Vehicle& lead);

// Shortest route using only what empire `e` knows: systems it explored and
// warp links it traversed (an invalid empire routes omnisciently). Only the
// first 10 warp points of a system are used, and known hazard sectors lose
// ties. The empire's Ship Movement options (spec 03 §6.2):
// - avoid restricted systems: its systems to avoid are never crossed (except
//   the start and destination systems); when no other route exists there is
//   none;
// - avoid tagged minefields (not for a sweeper group): tagged sectors are never
//   entered unless they are the destination, and a warp link with a tagged
//   sector on either side is not used.
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
// and combat::resolveSpaceCombat; tests substitute their own. `resolve` gets
// the vehicles that stepped into the sector that day (the groups hostile mines
// strike, spec 04 §10.6); empty when nobody entered it.
struct CombatHooks {
    std::function<bool(const Rules&, const GameState&, Location)> possible;
    std::function<void(TurnContext&, Location, std::span<const VehicleId>)> resolve;
};
CombatHooks defaultCombatHooks();

// The 30-day movement phase (spec 03 §6.3). Each vehicle (fleet, planet with
// orders) keeps a day counter that gains speed/30 a day; when it reaches 1 the
// vehicle carries out one order execution. After each day every sector where
// something acted is offered to combat, unless everything there already
// fought there this turn. Each step records where the vehicle came from
// (Vehicle::cameFrom, cameFromTurn: combat's attackers and start boxes, spec 04
// §3). Combat neither stops movement nor clears orders; a Sentry order at the
// head of a participant's list is removed.
void runMovementAndCombat(TurnContext& ctx);
void runMovementAndCombat(TurnContext& ctx, const CombatHooks& hooks);

// The end of the movement phase: colony ships that reached their target
// colonize (the Colonize order waits at the planet during the 30 days).
void runColonization(TurnContext& ctx);

// ---- The turn-based move (spec 03 §6.3 "Turn-based", spec 04 §2) --------------------------------

// EntryQuestion (state.hpp): a human player's group stopped before a sector
// with enemy forces, waiting for cmd::EnterSector.

// What runLive carries out.
struct LiveMove {
    EmpireId empire;
    // Only these groups act: vehicles on their own orders, fleets, planets.
    // With all three empty, every group of the empire that has orders.
    std::vector<VehicleId> vehicles;
    std::vector<FleetId> fleets;
    std::vector<ObjectId> planets;
    // A human player's groups stop before a sector with visible enemy forces
    // and ask (the player's own orders; computer players decide themselves).
    bool ask = false;
    // An answered question: that group enters that sector without asking.
    std::optional<EntryQuestion> allowed;
};

// A turn-based player's turn starts: the empire's vehicles regain their
// movement (fleet members in the fleet's sector the fleet's lowest maximum),
// and the turn's records of steps, emergency movement and launches
// (GameState::playerTurn) start afresh.
void startTurn(TurnContext& ctx, EmpireId empire);

// Turn-based games: the groups carry out their orders at once, spending
// movement points, action after action until each has no movement left,
// waits, fails or runs out of orders (a repeating list that goes round
// without a step waits for the next turn). A group that steps into a sector
// where combat is possible fights there at once (mines strike first) and its
// order fails; the Attack order's target sector is fought by the order
// itself, after decloaking, and the order stays. An order carried out in a
// sector (cargo, launches, an attack) offers the sector to combat without
// failing. Groups that merely sit start no battle (spec 04 §2). Returns the
// questions of the groups that stopped before a sector with enemies.
std::vector<EntryQuestion> runLive(TurnContext& ctx, const LiveMove& move);
std::vector<EntryQuestion> runLive(TurnContext& ctx, const LiveMove& move, const CombatHooks& hooks);

// Turn-based games: the empire's colony ships waiting at their planet with
// movement left found their colonies now (spec 03 §8 Colonize); one with no
// movement left waits for its next turn.
void runColonization(TurnContext& ctx, EmpireId empire);

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

// The event step's hazards (spec 01 §7): black-hole pull, random drift toward
// one target sector shared by all systems, then centre damage. Moves every
// ship, base and unit group; spends no movement or supply; starts no combat.
void runStellarHazards(TurnContext& ctx);

// Why `vehicle` cannot carry out stellar manipulation order `o` (Order::amount
// = the StellarAction) when the next movement phase runs it, or empty when it
// can: the checks the turn makes (spec 01 §9, confirmed: binary) on the
// current state, with the movement the vehicle will have then. A working part
// with the ability and supply for it, movement (except Construct), not
// cloaked, no visible hostile in the sector, and the action's own conditions.
// Open Warp Point without a destination (invalid o.location.system) is
// checked for what does not depend on it. `target`, when given, receives the
// object the manipulation would act on (none for some actions).
std::string stellarProblem(const Rules& r, const GameState& s, VehicleId vehicle, const Order& o, ObjectId* target = nullptr);

// The Destroy Planet result (spec 01 §9, confirmed: binary): the colony is
// lost (its owner is told `cause`) and the planet becomes a random natural
// asteroid field of the same stellar size that keeps its name, values and
// conditions. The `Planet - Destroyed` event has the same result (spec 05 §4).
void destroyPlanet(TurnContext& ctx, ObjectId planet, std::string_view cause, Rng& rng);
// The Destroy Star result (spec 01 §9, confirmed: binary): the shockwave. Every
// planet and asteroid field of the star's system becomes a random natural
// asteroid field of any size that keeps its name, values and conditions, its
// colony lost; every other object but warp points is gone, and so is every
// vehicle there. No destroyed star remains. Also `Star - Destroyed` (spec 05 §4).
void destroyStar(TurnContext& ctx, ObjectId star, std::string_view cause, Rng& rng);
// The Close Warp Point result (spec 01 §8, §9): both ends leave their systems.
// Also `Warp Point - Closed` (spec 05 §4).
void closeWarpPoint(GameState& s, ObjectId warpPoint);

// ---- Shared helpers (AI, UI, other subsystems) --------------------------------------------------

// Whether an Events log title written by a stellar manipulation reports a
// destroyed planet or star (a new nebula or black hole reports the star it
// consumed, once): what the computer players' anger term 2 counts (spec 05 §7.3).
bool isDestructiveStellarReport(std::string_view title);

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
