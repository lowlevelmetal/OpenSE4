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
// Orders our engine cannot carry out yet: their buttons light by the rules,
// and using one says so (docs/spec/06 §7 Q4).
bool orderNotInEngine(OrderId o);

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

} // namespace opense4::client::classic
