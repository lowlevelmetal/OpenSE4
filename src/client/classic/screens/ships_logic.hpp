#pragma once

// Rules-side helpers of the ship order windows (Ships\Units, Fleet Transfer,
// Cargo Transfer, Launch\Recover Units, Scrap, View Orders, Stellar
// Manipulation). Nothing here draws: it only reads the game state, so it is
// unit tested on our own fixtures.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opense4::client::classic::shipui {

// ---- Orders ---------------------------------------------------------------------------------

// Who holds the orders of a vehicle: its fleet when it is in one, else itself
// (docs/spec/03 §8: an order given to a fleet goes to every member). A planet
// holds its own launch and recover orders (spec 03 §12).
struct OrderOwner {
    game::VehicleId vehicle;
    game::FleetId fleet;
    game::ObjectId planet;
    bool valid() const { return vehicle.valid() || fleet.valid() || planet.valid(); }
};
OrderOwner orderOwner(const game::GameState& s, game::VehicleId v);
const std::vector<game::Order>* ordersOf(const game::GameState& s, OrderOwner o);
bool repeatOf(const game::GameState& s, OrderOwner o);
game::cmd::SetOrders setOrders(OrderOwner o, std::vector<game::Order> orders, bool repeat);

// Appends an order to the owner's list (keeps Repeat as it is).
game::cmd::SetOrders withAppended(const game::GameState& s, OrderOwner o, const game::Order& order);
// Orders the classic game carries out at once from a window (launch, recover,
// stellar manipulation, self-destruct) run first here: they go to the head of
// the list, after any such orders already given for `here` (the vehicle's
// sector), so they execute where the vehicle is now.
bool immediateKind(game::OrderKind k);
void insertImmediate(std::vector<game::Order>& orders, const game::Order& order, game::Location here);
// Where the owner is: the vehicle's sector, or the fleet leader's.
std::optional<game::Location> ownerLocation(const game::GameState& s, OrderOwner o);
game::cmd::SetOrders withImmediate(const game::GameState& s, OrderOwner o, const game::Order& order);

// Moves an order up (delta < 0) or down; returns its new index.
size_t moveOrder(std::vector<game::Order>& orders, size_t index, int delta);

// ---- Transfer steps (Move One / Five / Ten / Hundred / All) ---------------------------------

// Move Hundred is the Transfer Cargo window's (spec 06 §1.3, §7 Q80).
enum class Step { One, Five, Ten, Hundred, All };
int64_t stepAmount(Step step, int64_t available);
const char* stepLabel(Step step);

// ---- Jettison Cargo (docs/spec/03 §8) -------------------------------------------------------

// One line of the Jettison Cargo window: a population race or a unit stack,
// with its amount.
struct JettisonLine {
    game::DesignId unit;   // invalid: population
    game::EmpireId race;
    int64_t amount = 0;
    bool operator==(const JettisonLine&) const = default;
};
// "Cargo Present" (the holder's cargo, one line per race, then one per unit
// stack, each race with its own amount: an OpenSE4 choice) and "Cargo To Be
// Jettisoned". A click on a line moves the step's amount of it, or what the
// line holds if that is less, to the other list: onto that entry's line when
// it is there, else onto a new line at the end; an emptied line disappears.
struct JettisonLists {
    std::vector<JettisonLine> present, chosen;
    void move(bool fromPresent, size_t line, Step step);
};
JettisonLists jettisonLists(const game::Cargo& cargo);
// OK: exactly the right-hand list (empty lists give no command).
std::optional<game::cmd::JettisonCargo> jettisonCommand(const JettisonLists& lists, game::VehicleId vehicle, game::ObjectId planet);
// Whether Jettison Cargo is for this holder: an own ship or base that is not
// mothballed, or an own colony.
bool canJettisonFrom(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::VehicleId vehicle, game::ObjectId planet);

// ---- Convert Resources (docs/spec/02 §5.6) ----------------------------------------------------

// One line of the window's "Conversions" list.
struct ConversionLine {
    game::Resource from = game::Resource::Minerals;
    game::Resource to = game::Resource::Minerals;
    int64_t amount = 0;
    bool operator==(const ConversionLine&) const = default;
};
// The Convert Resources window: the target resource (Minerals when it
// opens), the step (1,000; 10,000 or 100,000 with the "x 10000" or "x 100000"
// button down, pressing one releasing the other) and the lines. A click on a
// resource on the left adds a step from it to the target, onto the line with
// the same source and target or a new line at the end; a click on a line
// takes a step off it, and a line left below one step is removed. Neither the
// treasury nor source = target is checked.
struct ConversionWindow {
    game::Resource target = game::Resource::Minerals;
    int64_t step = 1000;
    std::vector<ConversionLine> lines;
    void add(game::Resource from);
    void remove(size_t line);
    // The "x 10000" (10'000) and "x 100000" (100'000) buttons.
    void press(int64_t bigStep);
};
// OK: each line in list order as Convert Resources orders of at most 65,000
// (economy::conversionOrders).
std::vector<game::Order> conversionOrders(const std::vector<ConversionLine>& lines);
// Whether Convert Resources is for this colony: an own colony whose planet or
// facilities give `Resource Conversion` of at least 1.
bool canConvertAt(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::ObjectId planet);

// ---- The Ships\Units list's columns (spec 06 §1.8.3, confirmed: binary) ----------------------

enum class ShipsTab : uint8_t { General, Orders, Cargo, Fleet, Maintenance, Count };

// Every column keeps one identity in every tab, and a sort key is that
// identity (InterfaceOptions::shipsSort holds it + 1), so a key goes on
// sorting by its quantity while another tab is shown. Name keeps the number 1
// that the window's key lists always gave it.
enum class ShipColumn : uint8_t {
    Picture, Name,
    Size, Type, Movement, Damage, Supplies,                                  // General
    Class, Orders,                                                           // Orders
    CargoSpace, CargoMax, CargoList,                                         // Cargo
    Experience, Fleet,                                                       // Fleet
    MineralsMaintenance, OrganicsMaintenance, RadioactivesMaintenance,       // Maintenance
    Count
};
// The columns a tab shows after the picture and Name, in order.
std::vector<ShipColumn> shipTabColumns(ShipsTab tab);
// Unlimited supply sorts as this much (between ships, lowest first).
inline constexpr int64_t kUnlimitedSupplyKey = 60000;

// What a ship or unit group is sorted by, whatever tab is shown.
struct ShipSortValues {
    std::string name;
    int hullNumber = 0;          // the hull's place in the data set; a unit group 100 + its units (picture and Size)
    std::string type;            // the design type
    int movement = 0;            // movement left
    int destroyed = 0;           // destroyed components
    int64_t supplies = 0;        // kUnlimitedSupplyKey with unlimited supply
    std::string designName;      // Class
    std::string orders;          // the order list as one line (the caller's words)
    int64_t cargoUsed = 0, cargoCapacity = 0;
    std::string cargo;           // the cargo in words (the caller's)
    int64_t experience = 0;      // in tenths
    int fleetNumber = 0;         // 0 outside a fleet
    game::Resources maintenance;
};
// Everything but `orders` and `cargo`, which the window writes in its words.
ShipSortValues shipSortValues(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);
// Negative, zero or positive in the column's own direction: Name A to Z
// ignoring case; the picture and Size by hull number, lowest first; Type,
// Class, Orders and Cargo List A to Z by character code; Move, Dmg, Space, Max
// and the maintenance columns highest first; Supplies and Experience lowest
// first; Fleet by fleet number, highest first. Zero for an unknown column.
int compareShips(ShipColumn c, const ShipSortValues& a, const ShipSortValues& b);
// The column a stored key stands for (slot value − 1); nullopt when unknown.
std::optional<ShipColumn> shipColumnOf(int key);
// The vehicle rows' order under the stored keys (newest first; Name alone
// while no key is stored), as indices into `rows`. Fleet rows are not sorted:
// the window puts them after every vehicle, in the empire's fleet order.
std::vector<size_t> shipRowOrder(const std::vector<ShipSortValues>& rows, const std::array<uint8_t, 5>& slots);
// "N/M": destroyed and total components of a vehicle (the Dmg column).
std::pair<int, int> destroyedComponents(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);

// ---- Units --------------------------------------------------------------------------------

bool isUnitVehicle(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);
bool isUnitDesign(const game::Rules& r, const game::GameState& s, game::DesignId d);
// Units a vehicle can launch per game turn by unit kind (Val 2 of its intact
// launchers, docs/spec/03 §12); -1 when it has no launcher for that kind.
struct LaunchRates {
    int fighters = -1, satellites = -1, mines = -1, drones = -1;
    bool any() const { return fighters >= 0 || satellites >= 0 || mines >= 0 || drones >= 0; }
};
LaunchRates launchRates(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);
// Whether the vehicle has a launcher for units of this design.
bool canLaunch(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, game::DesignId unit);

// ---- Scrap window ---------------------------------------------------------------------------

// Scrap refund for a vehicle (all units of a group) at its location (docs/spec/03 §15).
game::Resources scrapValue(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);
game::Resources unmothballCost(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);
game::Resources facilityScrapValue(const game::Rules& r, const game::GameState& s, const game::Colony& c, size_t slot);
// Whether the vehicle can self-destruct: a ship or base with Self-Destruct in
// its ability list; satellite groups, minefields and drone groups always;
// fighter groups never (spec 03 §12, §15).
bool canSelfDestruct(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);
bool vehicleArmed(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);
// Another own vehicle in the sector, outside `selection`, has an intact weapon.
bool canBeFiredOn(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, const std::vector<game::VehicleId>& selection);
// Components on the vehicles that the owner of the window cannot build yet.
struct ResearchPotential {
    int unknown = 0;
    int total = 0;
};
ResearchPotential researchPotential(const game::Rules& r, const game::GameState& s, const game::Empire& e,
                                    const std::vector<const game::Vehicle*>& vehicles);
const char* researchPotentialLabel(ResearchPotential p);

// Per-turn maintenance of one vehicle (docs/spec/02 §7): the engine reports
// only the empire total, so the Ships window estimates rows with the same rule.
game::Resources vehicleMaintenance(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);

// Validates a command against a scratch copy of the state: the result and
// what it would take from the stockpile (e.g. a retrofit's cost).
struct DryRun {
    game::CommandResult result;
    game::Resources cost;
};
DryRun dryRun(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::Command& c);

// ---- Stellar manipulation -------------------------------------------------------------------

struct StellarInfo {
    game::StellarAction action;
    const char* name;        // button label
    game::AbilityKind ability;
    const char* picture;     // film strip under Pictures/Stellar
    int frames;              // 128×128 frames in the strip
};
const StellarInfo& stellarInfo(game::StellarAction a);

struct StellarCheck {
    bool hasAbility = false;
    bool possible = false;   // ability and precondition
    std::string reason;      // why not, or what will happen
    game::ObjectId target;   // the object acted on, if any
    bool destroysSystem = false;  // wipes out the system, the ship included
    bool needsDestination = false;  // Open Warp Point: pick the other end
};
StellarCheck checkStellar(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, game::StellarAction a);
game::Order stellarOrder(const game::Vehicle& v, game::StellarAction a, game::ObjectId target);

} // namespace opense4::client::classic::shipui
