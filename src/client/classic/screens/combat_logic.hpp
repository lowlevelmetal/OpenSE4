#pragma once

// The questions behind the combat windows (docs/spec/06 §1.10), free of
// ImGui so the tests can use them: the Strategic Combat window's forces list,
// the Combat Piece Report's detail lines, the colony Drop Troops lands on,
// and the Combat Simulator's list of combat vehicles.

#include "client/classic/replay.hpp"
#include "game/rules.hpp"
#include "game/simulator.hpp"
#include "game/state.hpp"
#include "game/tactical.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace opense4::client::classic {

// ---- Strategic Combat: the forces list (spec 06 §1.10.5, confirmed: binary) ----------------------

struct ForceRow {
    std::string name;      // the hull (VehicleSize) name, or a planet's name
    bool planet = false;
    int current = 0;
    int lost = 0;
};

struct ForceSide {
    game::EmpireId empire;
    std::vector<ForceRow> rows;   // hulls in VehicleSize order, then up to kForcePlanets planets
};

inline constexpr size_t kForcePlanets = 5;

// The rows are made once, from the battle as it was set up: for each empire,
// in player-number order, that has any row, one row per hull (VehicleSize
// order) of the ships, bases and unit groups it had in the battle then, and
// its colonized planets, at most kForcePlanets of them, the first in piece
// order. Counting is redone after each combat turn: a ship or base counts 1
// under its design's hull, a unit group counts each stack's living units under
// the hull of that stack's design, seekers are not counted. Lost is the
// highest count seen minus the current one. A hull that first appears later
// (units launched from cargo, a captured ship of a new hull) gets no row,
// though launches raise the highest count of a row that exists. A planet row
// is 1 / 0 while a piece of that empire with the planet's name exists, and
// 0 / 1 otherwise (spec 06 §1.10.5, §7 Q31, confirmed: binary).
class CombatForces {
public:
    // From a live battle's pieces (combat::TacticalBattle::pieces()), whose unit groups list their stacks.
    void setup(const game::Rules& r, const game::GameState& s, const std::vector<game::combat::TacticalPiece>& pieces);
    void count(const game::Rules& r, const game::GameState& s, const std::vector<game::combat::TacticalPiece>& pieces);
    // From a record played back (a battle the engine fought already): a unit
    // group counts all its units under its first design's hull, the only one
    // the record keeps (inferred, a battle shown afterwards).
    void setup(const game::Rules& r, const game::GameState& s, const game::CombatRecord& record,
               const std::vector<CombatPlayback::Piece>& atStart);
    void count(const game::Rules& r, const game::GameState& s, const game::CombatRecord& record,
               const std::vector<CombatPlayback::Piece>& pieces);
    const std::vector<ForceSide>& sides() const { return sides_; }
    bool ready() const { return ready_; }
    void reset();

private:
    struct Hull {
        uint32_t hull = 0;
        int highest = 0;
    };
    struct Side {
        game::EmpireId empire;
        std::vector<Hull> hulls;
        std::vector<std::string> planets;
    };
    void build(const std::map<std::pair<uint32_t, uint32_t>, int>& atStart, const std::map<uint32_t, std::vector<std::string>>& planets);
    void apply(const game::Rules& r, const std::map<std::pair<uint32_t, uint32_t>, int>& now,
               const std::map<uint32_t, std::vector<std::string>>& standing);
    std::vector<Side> rows_;
    std::vector<ForceSide> sides_;
    bool ready_ = false;
};

// ---- Tactical Combat ---------------------------------------------------------------------------

// The Combat Piece Report's Detail lines (spec 06 §1.10.1, confirmed: binary):
// Movement "left/max" (Population for a planet), Shields now/max, Damage
// taken/full for every kind of piece (full as the overkill limit counts it),
// Supply (now/capacity with "K" thousands above 100000, "Endless", "Never"
// for planets and satellite groups, "None" for seekers), Max Targets, Combat
// Group ("Group N - Leader", "Group N - Wingman" or None, fleet groups
// numbered like the others; a drone group shows Target, its drone target,
// instead), Formation (not for drone groups: the formation of the group it
// leads, kept after it stops leading), and Conditions "Plague N" for a planet
// with plague.
std::vector<std::pair<std::string, std::string>> pieceReportLines(const game::Rules& r, const game::GameState& s,
                                                                  const std::vector<game::combat::TacticalPiece>& pieces, int piece);
// The Combat Piece Report's Ability page (spec 06 §1.10.1, §7 Q78, confirmed:
// binary): for a ship or base its hull's abilities, then its whole design's
// (every component, destroyed or not), then its own (none in ours); for a
// planet only the planet's own abilities, not its facilities' or its
// colony's. Each line is the identifier and its values; AI tags are left out.
std::vector<std::string> pieceReportAbilities(const game::Rules& r, const game::GameState& s, const game::combat::TacticalPiece& p);

// Drop Troops (spec 06 §1.10.2, spec 04 §11, confirmed: binary): no target
// click. The troops land on the colony of another empire adjacent to the
// ship that comes last in piece order, whatever the treaty; the engine picks
// it by that rule and refuses with the reason (no colony adjacent, another
// empire's troops already there, no troops aboard). dropTroopsColony() names
// that colony (-1: none); the order carries it in `target` for the record,
// and the engine does not rely on it.
int dropTroopsColony(const game::combat::TacticalBattle& b, int piece);
game::combat::TacticalOrder dropTroopsOrder(const game::combat::TacticalBattle& b, int piece);

// ---- Combat Simulator (spec 06 §1.10.4) ---------------------------------------------------------

// One row of the combat vehicles list: one per ship or base, one per unit
// group (the items of a side's fighters, or satellites, make one group; each
// drone item is a group of its own), one per colony; then the neutral objects.
struct SimulatorRow {
    int side = -1;                 // -1: a neutral object
    std::vector<size_t> items;     // the setup items it stands for
    std::string name;
    int units = 0;                 // a unit group's units (0 otherwise)
    game::DesignId design;         // for the picture (first item's)
    game::ObjectId object;
    // The Name column's small lines (spec 06 §1.10.4, §7 Q38): "Cargo:" what a
    // ship, base or colony holds, or "Units:" a unit group's stacks, and
    // "Fleet:" its simulator fleet; empty values read "None". A neutral
    // object has neither line.
    bool lines = false;
    bool unitsLine = false;        // "Units:" rather than "Cargo:"
    std::string cargo;
    std::string fleet;
};
std::vector<SimulatorRow> simulatorRows(const game::Rules& r, const game::GameState& s, const game::combat::SimulatorSetup& setup);

// Adds one click's worth of an item (spec 06 §1.10.4): one ship, or one unit
// for the side's group of that kind (a drone always makes a new group), or a
// planet once. Returns false when the object is already in the battle.
bool simulatorAdd(const game::Rules& r, const game::GameState& s, game::combat::SimulatorSetup& setup, game::combat::SimulatorItem item);
// Removes a row's items (a fleet left without members is simply not formed).
void simulatorRemove(game::combat::SimulatorSetup& setup, const SimulatorRow& row);

// Fleets For Plr and Change Cargo (spec 06 §1.10.4, confirmed: binary) open the
// real Fleet Transfer and Transfer Cargo windows; ours work on a sandbox built
// from the setup (game::combat::buildSimulation), never on the real game. For
// Fleets For Plr the window plays the chosen side: its ships and its fleets,
// with formation and strategy from its copied list. For Change Cargo (§7 Q80)
// the left list holds every row of the Combat Vehicles list as a holder, all
// sides together in that list's order, and the right list only a temporary
// Storehouse: a copy of the player's first colony (systems in order) owned by
// side 1, with Cargo Storage 500000000, 1000 of every unit design the player
// owns or has seen (sorted by the owner's empire name, then the design name)
// and 10000M of side 1's people on top of the copy's own. Cargo moves only
// between a holder and the Storehouse and no owner is checked, so in the
// sandbox every holder and the Storehouse are played by side 1. The sandbox
// state is simultaneous, so commands take effect at once. `anchor*`: what the
// window opens on.
struct SimulatorCargoHolder {
    game::VehicleId vehicle;
    game::ObjectId planet;         // a sample planet (a neutral object has no colony)
    int side = -1;                 // the setup side (-1: a neutral object)
    bool operator==(const SimulatorCargoHolder&) const = default;
};
struct SimulatorSandbox {
    game::combat::Simulation sim;
    int sideIndex = 0;
    game::EmpireId side;
    bool cargo = false;
    game::VehicleId anchorVehicle;
    game::ObjectId anchorPlanet;
    std::vector<SimulatorCargoHolder> holders;   // Change Cargo: the left list
    game::ObjectId storehouse;                   // Change Cargo: the right list (invalid: the player has no colony)
};
inline constexpr int64_t kStorehouseCargoStorage = 500'000'000;
inline constexpr int kStorehouseUnits = 1000;
inline constexpr int64_t kStorehousePeople = 10'000;
SimulatorSandbox simulatorSandbox(const game::Rules& r, const game::GameState& real, const game::combat::SimulatorSetup& setup, int side, bool cargo);
// What the window changed in `sandbox` comes back into the setup: the side's
// fleets (members, name, formation, strategy; unit groups are in none), or
// every ship's and colony's units and people (in real designs; people moved
// onto a ship stay aboard and fight, spec 06 §7 Q80).
void simulatorTakeBack(const game::Rules& r, const SimulatorSandbox& made, const game::GameState& sandbox, game::combat::SimulatorSetup& setup);

} // namespace opense4::client::classic
