#pragma once

// The combat simulator (docs/spec/04 §17; the window is spec 06 §1.2): a mock
// battle in which nothing is really lost. The player picks own designs,
// foreign designs they have seen and sample planets of their home system,
// gives each to one of several virtual empires (all hostile to each other),
// edits cargo, forms fleets, picks strategies and which sides the computer
// controls, then fights it tactically or strategically. Minefields cannot
// be added. The battle is fought on a sandbox copy of the game, so the real
// game never changes.
//
// The sandbox (spec 04 §17, confirmed: binary unless marked): a copy of the
// game with one new empire per side (up to 10), each a copy of the real
// empire that owns the side's first item (race, culture, technology, and its
// strategy list), at war with every other side. The battle stands for one in
// the viewer's home sector: that sector's `Sector - Sensor Interference` and
// `Sector - Shield Disruption` apply, and no system modifier totals are worked
// out (they are 0). It is fought in a new, empty system of the home system's
// type, so that nothing else in the home sector takes part (the original's
// home sector with no real object taking part is the same, §19.2 Q71).
// Strategies (§17, §19.2 Q71): a ship outside any combat group uses its
// design's strategy from the list of the design's real owner, so an enemy
// design fights with that enemy's strategy (a unit group likewise, by its
// first design); a ship of a fleet formed in the simulator uses, while in its
// combat group, the fleet's strategy from its side's copied list; every
// planet uses the viewer's strategy for planets (the empire's first). A
// record a side's list lacks is added to that side's copy. Each design used
// is copied for the side that uses it; vehicles start undamaged with full
// supplies and no experience.
// Sample planets are copies of the colonies (population, facilities and
// stored units) moved into the battle sector; unowned objects of the home
// system are copied there as neutral objects. Start positions go by side
// number, as if arriving from a neighbouring sector: 1 north, 2 south, 3
// west, 4 east, 5 north-west, 6 south-west, 7 north-east, 8 south-east;
// sides 9 and 10, and a side that owns a planet or a base, start in the
// middle. Side 1 alone gets hand control back when Auto is released.

#include "game/rules.hpp"
#include "game/state.hpp"
#include "game/tactical.hpp"

#include <string>
#include <vector>

namespace opense4::game::combat {

struct SimulatorItem {
    enum class Kind : uint8_t { Design, Planet };
    Kind kind = Kind::Design;
    DesignId design;               // Design: own, or foreign and seen by the viewer
    // Planet: an object of the viewer's home system (simulatorPlanets): a
    // colony of any empire joins `side`; an unowned object (a star, warp
    // point, storm, empty planet...) stands in the battle as a neutral
    // obstacle, whatever `side` says (spec 06 §1.10.4).
    ObjectId planet;
    int side = 0;                  // index into SimulatorSetup::sides
    int count = 1;                 // ships of the design; unit designs: units in one group
    // Units carried (the viewer's own unit designs). Planets: replaces the
    // colony's stored units when `replaceCargo`.
    std::vector<UnitStack> cargo;
    bool replaceCargo = false;
    int fleet = -1;                // index into SimulatorSetup::fleets (same side), -1: none
    // Ships and bases: the serial number of its first ship, given when the
    // item was added (simulatorAddShips); 0: none given, the next free ones
    // of its side are used (inferred).
    int serial = 0;
};

struct SimulatorSide {
    std::string name;
    bool computer = true;          // the computer controls it; otherwise the player (tactical runs)
};

struct SimulatorFleet {
    int side = 0;
    std::string name;
    uint32_t formation = 0;        // Formations.txt index
    uint32_t strategy = 0;         // index into its side's strategies (the copied empire's list)
};

struct SimulatorSetup {
    EmpireId viewer;
    std::vector<SimulatorSide> sides;
    std::vector<SimulatorFleet> fleets;
    std::vector<SimulatorItem> items;
    uint64_t seed = 0;             // 0: the game's random numbers
    // Per side: the ships ever given to it, whatever their designs (spec 04
    // §17, confirmed: binary). Starts at 0 with the setup, never lowered by
    // removals; the setup keeps it across a tactical simulation.
    std::vector<int> shipCounters;
};

inline constexpr int kSimulatorMaxSides = 10;        // virtual empires (confirmed: binary)
inline constexpr int kSimulatorMaxCount = 100;       // per item
inline constexpr int kSimulatorMaxVehicles = 250;    // in all (inferred)

// The designs the viewer may put in: its own, then the foreign designs it has
// seen, without obsolete ones when asked; mines, troops and weapon platforms
// are left out (they are never combat pieces).
std::vector<DesignId> simulatorDesigns(const Rules& r, const GameState& s, EmpireId viewer, bool hideObsolete);
// Unit designs the viewer may load as cargo (fighters, satellites, drones,
// troops, weapon platforms; no mines).
std::vector<DesignId> simulatorCargoDesigns(const Rules& r, const GameState& s, EmpireId viewer, bool hideObsolete);
// The objects of the viewer's home system on offer (spec 06 §1.10.4,
// confirmed: binary): every colony, whoever owns it, and every unowned object.
std::vector<ObjectId> simulatorPlanets(const GameState& s, EmpireId viewer);
// Whether a Planet item is a colony (it joins its side) rather than a neutral object.
bool simulatorColony(const GameState& s, const SimulatorItem& item);
// Gives a ship or base item of `count` ships the side's next serial numbers:
// each ship takes the side's counter + 1 and the counter goes up (spec 04
// §17, confirmed: binary). Unit and planet items get none.
void simulatorNumberShips(const Rules& r, const GameState& s, SimulatorSetup& setup, SimulatorItem& item);
// The name of what each item puts on the field, as buildSimulation names it:
// a ship or base is "<design> NNNN", its serial (SimulatorItem::serial, so
// the first two ships given to a side are "A 0001" and "B 0002" whatever
// their designs); a unit item, its design's name; a Planet item, the
// object's name. An item with no serial takes the next numbers after its
// side's counter, in item order (inferred).
std::vector<std::string> simulatorItemNames(const Rules& r, const GameState& s, const SimulatorSetup& setup);
// Cargo space an item has, and what its cargo takes up.
int64_t simulatorCargoCapacity(const Rules& r, const GameState& s, const SimulatorItem& item);
int64_t simulatorCargoUsed(const Rules& r, const GameState& s, const SimulatorItem& item);

// Why the setup cannot be fought (empty: it can).
std::string simulatorProblem(const Rules& r, const GameState& s, const SimulatorSetup& setup);

struct Simulation {
    GameState state;                 // the sandbox
    Location where;                  // the battle sector
    std::vector<EmpireId> sides;     // the virtual empire of each side
    // Per setup item: the sandbox vehicles it put on the field (a unit item
    // joins its side's group of that kind, which then stands for several
    // items), and a Planet item's object in the sandbox.
    std::vector<std::vector<VehicleId>> itemVehicles;
    std::vector<ObjectId> itemObjects;
    // Each sandbox design copied from a real one (the design of a side's copy -> the real design).
    std::vector<std::pair<DesignId, DesignId>> designCopies;
    std::vector<EmpireId> players;   // the sides the player controls
    int interference = 0;            // the home sector's, applied to the battle
    int disruption = 0;
    std::vector<std::pair<EmpireId, uint32_t>> planetStrategies;   // each side's planets' strategy in its list
};
// The sandbox for a valid setup (see simulatorProblem). `real` is not changed.
Simulation buildSimulation(const Rules& r, const GameState& real, const SimulatorSetup& setup);
// The battle, ready to fight: tactical for the player's sides (none: strategic,
// already fought to the end, or with `stepped` set up and stopped before combat
// turn 1 for the Strategic Combat window to fight phase by phase,
// TacticalBattle::Setup::stepped; call finish() for the results).
TacticalBattle startSimulation(const Rules& r, Simulation sim, bool stepped = false);

} // namespace opense4::game::combat
