#pragma once

// Vehicle movement and order execution (docs/spec/03 §6-15, spec 01 §7-9,
// spec 05 §9.3): pathfinding, the 30-day simultaneous move, fleets,
// cargo/launch orders, colonization, stellar manipulation, supply and repair.
//
// Turn order (spec 05 §8, turn.cpp):
//   step 5  movement and space combat   startTurn + runMovementAndCombat
//                                       (Colonize orders found colonies in it)
//   step 6  each empire's end-of-turn processing (the numbers of that list):
//             11 repair                 repairEmpire
//             13 supply                 supplyEmpire
//             15 training               trainEmpire
//   step 7  design cleanup (10th turn)  purgeObsoleteDesigns
//   step 9  event step, first           runStellarHazards

#include "core/rng.hpp"
#include "game/combat.hpp"
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
    bool allowWarp = true;      // neutral empires never warp (estimates also leave warps out for fighters)
    bool sweeper = false;       // a mine sweeper group: warp links at tagged minefields are no obstacle (leadsSweeperGroup)
    bool operator==(const RouteOptions&) const = default;
};

// A group whose first member is this vehicle may use warp links at the
// owner's tagged minefields: its design has the design type named `Mine
// Sweeper` (spec 03 §6.2, confirmed: binary). For a fleet the first member is
// the first one at the fleet's location in object order (sweeperOf).
bool leadsSweeperGroup(const GameState& s, const Vehicle& first);
// The member a group's Mine Sweeper exemption is tested on: for a fleet
// member, the fleet's first member at its location in object order;
// otherwise the vehicle itself.
const Vehicle& sweeperOf(const GameState& s, const Vehicle& v);

// Shortest route using only what empire `e` knows: systems it explored and
// warp links it traversed (an invalid empire routes omnisciently). Only the
// first 10 warp points of a system are used, and known hazard sectors lose
// ties. The empire's Ship Movement options (spec 03 §6.2):
// - avoid restricted systems: its systems to avoid are never crossed (except
//   the start and destination systems); when no other route exists there is
//   none;
// - avoid tagged minefields: tagged sectors are never entered in a system
//   unless they are the destination, and a warp link whose warp-point sector
//   on either side is tagged is not used, even at the start or the goal
//   (a sweeper group may use such links).
// In a system with a destructive centre the steps follow its cost map
// (detail::centreCostMap), as the moving groups do.
std::optional<Path> findPath(const Rules& r, const GameState& s, EmpireId e, Location from, Location to);

// Route to the nearest of several goals; `goal` is the index of the one reached
// (equal routes: the earlier goal).
struct NearestPath {
    Path path;
    size_t goal = 0;
};
std::optional<NearestPath> findPathToNearest(const Rules& r, const GameState& s, EmpireId e, Location from,
                                             std::span<const Location> goals, RouteOptions options = {});

// The square an in-system step of `mover`'s group from `here` toward `target`
// goes to (spec 03 §6.2, confirmed: binary): around a destructive centre the
// centre's cost map chooses; otherwise the greedy step, diagonal first, a bad
// square (a tagged minefield, a damaging sector, a seen hostile object)
// replaced by a random neighbour drawn from `rng`. Nullopt after the 10th bad
// square: the group stays and its order fails. Movement passes the game's
// generator; a movement line passes one of its own (planRoute).
std::optional<Sector> inSystemStep(const Rules& r, const GameState& s, EmpireId mover, Location here, Sector target, Rng& rng);

// ---- Movement lines (spec 06 §2.4 "Movement lines", confirmed: binary) ----------------------

// The route a movement line shows for one of the viewer's vehicles or fleets,
// worked out with the movement rules but never changing the game.
struct PlannedRoute {
    // The start square (a fleet's location), then one point per square
    // entered; a warp jump gives one point, the exit warp point's square in
    // the next system. Consecutive repeats are dropped; every point after the
    // start is one movement point.
    std::vector<Location> points;
    int movementLeft = 0;      // A: the movement left now (a fleet: the lowest among its members at its location, 0 with none)
    int movementPerTurn = 0;   // B: the full movement per turn (0 when mothballed or held); a fleet's lowest, as A
    // The turn number shown at point `i` (s = i steps from the start): 0
    // when s <= A, else (s - A - 1) div B + 1, or 0 when B is 0. So 0 means
    // reached with the movement left now, 1 during the next turn, and so on.
    int turnOf(size_t i) const;
};

// The route follows the order list in the order it will be carried out (with
// Repeat on, once round the list from the order now due). Only Move To and
// Move To Waypoint (the waypoint's location now; an unset one adds nothing)
// add squares: in-system steps by inSystemStep, other systems by the route
// search (findPathToNearest), as the moving group would take them. A blocked
// step or an unreachable destination ends that order's part, and the next
// order goes on from there; a group with fighters stops at the warp point.
// Every random replacement square comes from `displayRng`, never the game's.
PlannedRoute planRoute(const Rules& r, const GameState& s, const Vehicle& v, Rng& displayRng);
PlannedRoute planRoute(const Rules& r, const GameState& s, const Fleet& f, Rng& displayRng);

// Estimated turns to reach `to` at the vehicle's current speed (its fleet's,
// if it is in one) and the moves that speed makes in a turn (movesPerTurn);
// 0 when already there, -1 = unreachable or immobile.
int etaTurns(const Rules& r, const GameState& s, const Vehicle& v, Location to);

// ---- The simultaneous turn (spec 03 §6.3) -------------------------------------------------------

inline constexpr int kDaysPerTurn = 30;
// Actions a vehicle of speed `speed` has made by the end of `day` (1..30) with
// an exact day counter: the k-th on day ceil(k * 30 / speed), at most one a
// day. The original's counter differs (actionDays).
constexpr int movesByDay(int speed, int day) {
    if (speed <= 0) return 0;
    const int exact = speed * day / kDaysPerTurn;
    return exact < day ? exact : day;
}

// The day counter gains speed/30 a day; the vehicle acts when it reaches 1
// (spec 03 §6.3, confirmed: binary). The original keeps it as a 64-bit double:
// each day speed / 30 and the addition are done in x87 extended precision and
// the sum is stored back as a double, so many speeds lose their day-30 step
// (speed 1 never moves) and some steps come a day later (confirmed: binary;
// the stored counter was observed bit for bit). `Double` reproduces it with
// xmath::Ext; `Exact` counts in thirtieths (kept for comparison).
enum class DayCounterMode : uint8_t { Exact, Double };
inline constexpr DayCounterMode kDayCounterMode = DayCounterMode::Double;
// The days (1..30) a vehicle of this constant speed acts on under a counter mode.
std::vector<int> actionDays(int speed, DayCounterMode mode = kDayCounterMode);
// Actions a vehicle of this speed makes in one turn: in a simultaneous game
// the days of actionDays, in a turn-based game its movement points.
int movesPerTurn(const GameState& s, int speed);

// The lowest maximum movement among the members at a fleet's location (its speed).
int fleetSpeed(const Rules& r, const GameState& s, const Fleet& f);

// Turn start: every vehicle's movement points reset to its maximum, fleet
// members at the fleet's location to the lowest maximum among them.
void startTurn(TurnContext& ctx);

// How movement asks for space combat. The default calls combat::combatPossible
// and combat::resolveSpaceCombat; tests substitute their own. `resolve` gets
// the vehicles that stepped into the sector that day (the groups hostile mines
// strike, spec 04 §10.6); empty when nobody entered it. Both get who runs the
// battle check (spec 04 §2): in turn-based games the group that stepped in,
// attacked or sought; empty in simultaneous games.
struct CombatHooks {
    std::function<bool(const Rules&, const GameState&, Location, const combat::BattleCheck&)> possible;
    std::function<void(TurnContext&, Location, std::span<const VehicleId>, const combat::BattleCheck&)> resolve;
};
CombatHooks defaultCombatHooks();

// The 30-day movement phase (spec 03 §6.3, confirmed: binary). Every ship,
// base and unit group keeps a day counter that gains its current movement
// points / 30 each day (a fleet member, wherever it is: the lowest among the
// members at the fleet's location, 0 when none is there); at 1 or more it
// acts and loses 1. Objects act in object order (objectOrder: planets and
// vehicles mixed); colonized planets, minefields, satellite groups and
// vehicles without movement act on day 1 only. A fleet acts when its first
// member in object order that is due and has orders acts: the members at the
// fleet's location carry out that member's order and each of their lists
// moves on (spec 03 §8, §19 Q61, Q65); ad-hoc groups form at every order
// execution (spec 03 §8). An action gives the acting vehicle exactly 1
// movement point (0 when its maximum is 0; the other members keep theirs) and
// runs its order list: orders that complete chain into the next, up to 21
// executions, until one waits or fails. Movement points are not spent: they
// come back after the action, unless the maximum fell below them during it. After each day every
// sector where an object carried out an order (any order, a waiting Sentry
// included) runs a battle check, unless the latest battle there this turn
// left every object in the sector as an undamaged survivor;
// pursuits of targets that are gone end. Each step records where the vehicle
// came from (Vehicle::cameFrom, cameFromTurn: combat's attackers and start
// boxes, spec 04 §3). Combat neither stops movement nor clears orders; a
// Sentry order at the head of a participant's list is removed.
void runMovementAndCombat(TurnContext& ctx);
void runMovementAndCombat(TurnContext& ctx, const CombatHooks& hooks);

// Colonize is carried out like any order during the movement phases, on an
// acting day with movement left, so a colony can be founded in any phase and
// a ship that arrives on its last acting day founds it next turn (spec 05 §8
// step 5, spec 03 §8). runColonization founds at once the colonies of every
// colony ship already waiting at its planet, whatever its movement (tools
// and tests; the turn does not use it).
void runColonization(TurnContext& ctx);

// ---- The turn-based move (spec 03 §6.3 "Turn-based", spec 04 §2) --------------------------------

// EntryQuestion (state.hpp): a human player's group stopped before a sector
// with enemy forces, waiting for cmd::EnterSector.

// What runLive carries out.
struct LiveMove {
    EmpireId empire;
    // Only these groups act: vehicles, fleets (a fleet member listed in
    // `vehicles` names its fleet), planets. With all three empty, every group
    // of the empire that has orders. A human player's vehicles listed
    // together move as one group when their head orders are identical (spec
    // 03 §8).
    std::vector<VehicleId> vehicles;
    std::vector<FleetId> fleets;
    std::vector<ObjectId> planets;
    // A human player's groups stop before an in-system step into a sector
    // with enemy objects they see, and ask (orders carried over included;
    // computer players decide themselves).
    bool ask = false;
    // An answered question: that group enters that sector without asking.
    std::optional<EntryQuestion> allowed;
};

// A turn-based player's turn starts: the empire's vehicles regain their
// movement (fleet members at the fleet's location the lowest maximum among them),
// and the turn's records of steps, emergency movement and launches
// (GameState::playerTurn) start afresh.
void startTurn(TurnContext& ctx, EmpireId empire);

// Turn-based games: the groups carry out their orders at once, spending
// movement points, action after action until each has no movement left,
// waits, fails or runs out of orders; orders that complete chain into the
// next, up to 21 executions an action, and a group completes at most 21
// orders a run (spec 05 §8 "Turn-based game"). A colony ship at its planet
// with movement left founds its colony. Only three things run a battle check
// (spec 04 §2, combat::BattleCheck, confirmed: binary): a group's movement
// step (a warp jump included), once the mines there have struck: a battle is
// fought at once, the order fails and every member's list is cleared (a
// pursuit's step only stops for this run and keeps its list, spec 04 §19.2
// Q76); the
// Attack order, in the sector its target was in when it was given (or where
// the group stands when none was recorded), for 1 movement point and with
// nobody decloaking but the vehicles under the Ship Cloaking minister, which
// cloak again afterwards: the order is used up, the rest of the list goes on,
// and without movement left it is removed doing nothing; and a drone group's
// pursuit (a Seek) at its target, which attacks every time the list runs and
// stays (a group with no drone pursuing there waits). Other participants lose
// only a Sentry at the head of their lists. No other order, Sentry included,
// starts a battle, nor do groups that merely sit. Returns the questions of
// the groups that stopped before a sector with enemies.
std::vector<EntryQuestion> runLive(TurnContext& ctx, const LiveMove& move);
std::vector<EntryQuestion> runLive(TurnContext& ctx, const LiveMove& move, const CombatHooks& hooks);

// Turn-based games: the empire's colony ships waiting at their planet with
// movement left found their colonies now (spec 03 §8 Colonize); one with no
// movement left waits for its next turn. runLive does this itself; tools
// and tests use it.
void runColonization(TurnContext& ctx, EmpireId empire);

// ---- End of turn -------------------------------------------------------------------------------

// Repair (spec 03 §13): each (empire, sector) pool restores destroyed components.
void repairEmpire(TurnContext& ctx, EmpireId e);
// Supply (spec 03 §7, §12): unit and cloak upkeep (a cloak at 0 drops at
// once), depot refills, fleet pooling, then drones at 0 are lost.
void supplyEmpire(TurnContext& ctx, EmpireId e);
// The training step (spec 03 §3.3, §7, §9): every own object in object order
// trains its sector and runs its solar collectors; then the system-wide
// training abilities, one source per explored system.
void trainEmpire(TurnContext& ctx, EmpireId e);
// Obsolete designs with no vehicles, no queue entries and no sighting by a
// living foreign empire less than 50 turns old are removed (spec 03 §4.1).
// Runs every 10th turn.
void purgeObsoleteDesigns(TurnContext& ctx);

// The event step's hazards (spec 01 §7): black-hole pull, random drift toward
// one target sector shared by all systems (drawn every turn), then centre
// damage through the combat damage routine. Moves every
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

// The Destroy Planet result (spec 01 §9, spec 03 §19 Q72, confirmed:
// binary): the planet is replaced by a random natural asteroid field of the
// same stellar size that keeps its name, values and conditions; the field is
// a new object, added first, so it takes the lowest empty slot, and then the
// planet is removed and its colony lost (its owner is told `cause`). The
// `Planet - Destroyed` event has the same result (spec 05 §4).
void destroyPlanet(TurnContext& ctx, ObjectId planet, std::string_view cause, Rng& rng);
// The Destroy Star result (spec 01 §9, spec 03 §19 Q72, confirmed: binary):
// the shockwave, one pass over the system's objects in slot order, vehicles
// included. Every planet and asteroid field is replaced by a new random
// natural asteroid field of any size that keeps its name, values and
// conditions (in the lowest slot empty at that moment), its colony lost; every
// vehicle and every other object but warp points and stars leaves the game in
// the pass; the stars go after it. No destroyed star remains. Also `Star -
// Destroyed` (spec 05 §4).
void destroyStar(TurnContext& ctx, ObjectId star, std::string_view cause, Rng& rng);
// The Close Warp Point result (spec 01 §8, §9): both ends leave their systems.
// Also `Warp Point - Closed` (spec 05 §4).
void closeWarpPoint(GameState& s, ObjectId warpPoint);

// ---- Shared helpers (AI, UI, other subsystems) --------------------------------------------------

// Whether an Events log title written by a stellar manipulation reports a
// destroyed planet or star (a new nebula or black hole reports the star it
// consumed, once): what the computer players' anger term 2 counts (spec 05 §7.3).
bool isDestructiveStellarReport(std::string_view title);
// The text of a stellar manipulation report, naming the vehicle and the
// empire responsible; and whether a log entry is a destructive report that
// names `culprit` (spec 05 §7.3 term 2: each empire present in the system
// gets the report in its own log).
std::string stellarReportText(const GameState& s, EmpireId culprit, std::string_view vehicle);
bool stellarReportNames(const GameState& s, const LogEntry& entry, EmpireId culprit);

// Why this vehicle cannot colonize that planet (empty = it can, ignoring distance).
std::string colonizeProblem(const Rules& r, const GameState& s, const Vehicle& v, ObjectId planet);
// A resupply depot `empire` may use in this sector: a colonized planet with
// Supply Generation, its own or of an empire it has a Military Alliance or
// Partnership with. No population needed (spec 03 §7).
bool resupplyDepotAt(const Rules& r, const GameState& s, EmpireId empire, Location where);
// Σ Component Repair of the empire's own ships, bases, unit groups and
// colonized planets in the sector; no population needed (spec 03 §13, confirmed: binary).
int64_t repairPoolAt(const Rules& r, const GameState& s, EmpireId empire, Location where);
// The empire's repair modifier R: racial Repair + (Repair Aptitude − 100) + culture Repair.
int64_t repairModifier(const Rules& r, const GameState& s, EmpireId empire);
// Components per turn the empire repairs in this sector: truncate(pool × (100 + R) %).
int repairCapacityAt(const Rules& r, const GameState& s, EmpireId empire, Location where);
// Supply one step costs this vehicle (spec 03 §7; a unit group pays for every unit).
int64_t moveSupplyCost(const Rules& r, const GameState& s, const Vehicle& v);
// An empire's units in space (not in cargo): what the units-per-player cap counts (§12).
int unitsInSpace(const Rules& r, const GameState& s, EmpireId owner);
// Applies `amount` Normal damage outside combat through the combat damage
// routine, with no shields, special armor, modifier or carried pool (spec 03
// §6.2, confirmed: binary): a ship or base loses whole components in the
// random, structure-weighted order with all armor first until the damage
// cannot cover the next one (spec 04 §9.1a); a unit group loses whole units
// (damageUnitGroup, which records them in the design statistics). The
// leftover is lost. Returns true when the vehicle was destroyed (count set to
// 0). A survivor's supply and cargo are cut back at once (fitToCapacity).
bool damageVehicle(const Rules& r, GameState& s, Vehicle& v, int amount);
// Damage outside combat to a unit group (spec 04 §9.4 with the pools at 0,
// confirmed: binary): up to 20 times one of the group's designs is drawn, all
// equally likely (one with no units left wastes the draw), and a unit of it
// dies when the damage left covers its hit points (structure plus shields,
// counted twice for fighters). The leftover is lost. Each unit killed counts
// as lost for its design. Returns the damage used.
int64_t damageUnitGroup(const Rules& r, GameState& s, Vehicle& v, int64_t amount, Rng& rng);
// Whether this vehicle can self-destruct (spec 03 §12, §15, confirmed:
// binary): a ship or base with Self-Destruct in its ability list (the hull
// counts; a mothballed vehicle has none); satellite groups, minefields and
// drone groups always; fighter groups never.
bool canSelfDestruct(const Rules& r, const GameState& s, const Vehicle& v);
// After damage: supply clamped to the capacity that is left (spec 03 §7) and
// cargo that no longer fits removed (§11). Unlimited supply is left alone
// (a vehicle that lost its reactor drops back when it next moves).
void fitToCapacity(const Rules& r, const GameState& s, Vehicle& v);

// Places a newly built vehicle (economy uses it); applies the queue's
// automatic Move To waypoint.
Vehicle& spawnVehicle(const Rules& r, GameState& s, EmpireId owner, DesignId design, Location where, int autoWaypoint = -1);

} // namespace opense4::game::movement
