#pragma once

// When each order button of the main window is lit (docs/spec/06 §2.8,
// confirmed: binary), and the order strip's names and keys. Headless: the
// facts are read from the game state, the rule is plain logic over them, so
// both are tested without a window (tests/test_client_logic.cpp).

#include "client/input.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace opense4::client::classic {

// Every order of the order strip, in our own names.
enum class OrderId : uint8_t {
    MoveTo, Warp, MoveToWaypoint, Colonize, Attack, FleetTransfer, Resupply, Repair, ClearOrders, BuildQueue,
    CargoTransfer, LaunchRecover, LoadCargo, DropCargo, LaunchRemote, RecoverRemote, Sentry, Explore, Patrol, RepeatOrders,
    StellarManipulation, ChangeName, Scrap, Strategy, ViewOrders, SweepMines, ScrapFacilities, Jettison, Cloak, Decloak,
    UseComponent, UseFacility, AbandonPlanet, ConvertResources, Minister,
    // The movement log of a simultaneous game (§2.7, Ctrl+P/O/I/U).
    ReplayPlay, ReplayShip, ReplayStep, ReplayRewind,
    Count
};
inline constexpr size_t kOrderCount = static_cast<size_t>(OrderId::Count);
using LitOrders = std::array<bool, kOrderCount>;

// The button's name as the hover hint shows it ("Move To"), and the key the
// order strip uses for it in the lessons' tags (learn::orderStripId: "Move").
std::string_view orderName(OrderId o);
std::string_view orderSlotKey(OrderId o);
// The rebindable hotkey that runs an order (docs/spec/06 §3.1).
std::optional<Action> orderAction(OrderId o);

// What the report panel shows, as far as the order buttons care.
enum class OrderTarget : uint8_t {
    Nothing,         // no selection
    Other,           // a list of several objects, a system report, a star, a storm, a warp point, another empire's object
    Ship,            // an own ship or base, not mothballed
    MothballedShip,
    Fleet,           // an own fleet
    Colony,          // an own colony
    Fighters, Satellites, Mines, Drones,   // own unit groups in space
    Tagged,          // the tagged group (Shift+click, Shift+A; §2.5)
};

// The facts the rules look at. For a fleet or a tagged group they are
// gathered over the members as §2.8 says (any member / every member).
struct OrderFacts {
    OrderTarget target = OrderTarget::Nothing;
    bool locked = false;          // End Turn running, no game, or the host's between-turns screen
    bool turnBased = true;
    bool mobile = false;          // speed above 0 (a fleet: the fleet's speed)
    bool canWarp = false;         // a hull that can warp (fighters cannot)
    bool waypointSet = false;     // one of the empire's ten waypoints is set
    bool hasOrders = false;       // a fleet: any member (the fleet's list)
    bool canColonize = false;     // a Colonize Planet ability; a fleet: any member
    bool spaceYard = false;
    bool cloaked = false;
    bool cloak = false;           // the Cloak button's condition, gathered for the target's kind
    bool decloak = false;         // the Decloak button's condition
    bool cargoCapacity = false;   // capacity above 0
    bool cargoHolds = false;      // the cargo holds anything
    bool mineSweeping = false;
    bool lowSupply = false;       // below Supply Amount for Low Supply Warning
    bool stellar = false;         // any stellar-manipulation ability
    bool emergency = false;       // Emergency Resupply or Emergency Energy (a ship's parts, a colony's planet or facilities)
    bool facilities = false;      // the colony has facilities
    bool conversion = false;      // Resource Conversion on the planet or its facilities
    bool droneTagged = false;     // a drone group is among the tagged objects
};

// The rule of §2.8: which buttons are lit for these facts.
LitOrders litOrders(const OrderFacts& f);

// What is selected, in the game's terms.
struct OrderSelection {
    std::optional<game::VehicleId> vehicle;    // the object whose report is shown
    std::optional<game::FleetId> fleet;        // its own fleet (orders go to the fleet)
    std::optional<game::ObjectId> planet;      // a planet or asteroid field
    std::span<const game::VehicleId> tagged;   // the tagged group, fleets expanded
    bool other = false;                        // a list, a system report, a star, a storm or a warp point
};

// The facts for the viewer's selection.
OrderFacts orderFacts(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, const OrderSelection& sel, bool turnBased,
                      bool locked);

// Whether a vehicle can cloak now: a working part giving cloak level 2 or more
// in some sight type, and supplies above 0 (spec 03 §8 Cloak).
bool vehicleCanCloak(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);

// ---- The ship selection cycle (docs/spec/06 §2.3, confirmed: binary) ---------------------------
// Space, Ctrl+N, Ctrl+B and the top selector's arrows all run one cycle over
// the same ships: the viewer's own ships and bases (never unit groups) that
// can move (maximum movement above 0, so no base) and are not mothballed;
// in a turn-based game those with movement left, in a simultaneous game
// those whose own list is empty; never one whose first order is Sentry. The
// Empire Options' Next/Previous switches then filter (spec 06 §1.9): skip
// damaged ships, skip ships in fleets, stop once per location.
struct ShipCycleOptions {
    bool turnBased = true;
    bool skipDamaged = false;
    bool skipInFleets = false;
    bool stopOncePerLocation = false;
};
// What the cycle remembers from one press to the next (spec 06 §2.3,
// confirmed: binary): the place in the game's object list (the slot) of the
// last ship it selected itself, not of a ship selected another way, kept from
// turn to turn and forgotten when another player's turn begins on this
// computer (or the main window is set up afresh); and the places "Stop once
// per location" has stopped at, up to 100, forgotten when the walk passes
// either end of the list and when the player or the direction changes.
struct ShipCycleMemory {
    game::EmpireId player;
    int direction = 0;
    std::optional<uint32_t> place;
    std::vector<game::Location> visited;
};
inline constexpr size_t kShipCycleStops = 100;
bool shipCycleCandidate(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, const game::Vehicle& v, bool turnBased);
// The ship the cycle selects next, going forward (`direction` 1) or back (-1)
// through the vehicles in object order from the place it remembers; with
// none, forward from the list's start and back from one before its end (the
// last object looked at last); nothing when no ship qualifies.
std::optional<game::VehicleId> cycleShip(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, int direction,
                                         const ShipCycleOptions& options, ShipCycleMemory& memory);

// ---- The fleet cycle (Ctrl+F, Ctrl+D and the middle selector; spec 06 §2.3, confirmed: binary) --
// The viewer's fleets by fleet number, round the end; a fleet counts only when
// one of its members still has movement left this turn, and the vehicle
// selected is the first such member in object order (its fleet's report
// shows). The cycle goes on from the fleet it chose last, forgotten when
// another player's turn begins.
struct FleetCycleMemory {
    game::EmpireId player;
    std::optional<game::FleetId> last;
};
std::optional<game::VehicleId> cycleFleet(const game::GameState& s, game::EmpireId viewer, int direction, FleetCycleMemory& memory);

// ---- The colony cycle (Ctrl+C, Ctrl+X and the bottom selector; spec 06 §2.3, confirmed: binary) -
// The viewer's colonies sorted by planet name, by character code (letter case
// counts), round the end. The cycle goes on from the colony it chose last,
// remembered by its system and sector and forgotten when another player's
// turn begins; the main window then selects that sector as a click does.
// Two colonies in one sector: the cycle goes on past every colony of the
// remembered sector (inferred, spec 06 §7 Q125).
struct ColonyCycleMemory {
    game::EmpireId player;
    std::optional<game::Location> last;
};
std::optional<game::ObjectId> cycleColony(const game::GameState& s, game::EmpireId viewer, int direction, ColonyCycleMemory& memory);

} // namespace opense4::client::classic
