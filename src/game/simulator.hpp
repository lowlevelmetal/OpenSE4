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
// empire that owns the side's first item (race, culture, technology), at war
// with every other side. The battle stands for one in the viewer's home
// sector: that sector's `Sector - Sensor Interference` and `Sector - Shield
// Disruption` apply, and no system modifier totals are worked out (they are
// 0). (inferred) It is fought in a new, empty system of the home system's
// type, so that nothing else in the home sector takes part, and every side
// uses the viewer's strategies, which the simulator's items and fleets pick
// from. Each design used is copied for the side that uses it, with the item's
// strategy; vehicles start undamaged with full supplies and no experience.
// Sample planets are copies of the colonies (population, facilities and
// stored units) moved into the battle sector. Start positions go by side
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
    ObjectId planet;               // Planet: a colony of the viewer in its home system
    int side = 0;                  // index into SimulatorSetup::sides
    int count = 1;                 // ships of the design; unit designs: units in one group
    // Units carried (the viewer's own unit designs). Planets: replaces the
    // colony's stored units when `replaceCargo`.
    std::vector<UnitStack> cargo;
    bool replaceCargo = false;
    int fleet = -1;                // index into SimulatorSetup::fleets (same side), -1: none
    uint32_t strategy = 0;         // index into the viewer's strategies
};

struct SimulatorSide {
    std::string name;
    bool computer = true;          // the computer controls it; otherwise the player (tactical runs)
};

struct SimulatorFleet {
    int side = 0;
    std::string name;
    uint32_t formation = 0;        // Formations.txt index
    uint32_t strategy = 0;         // index into the viewer's strategies
};

struct SimulatorSetup {
    EmpireId viewer;
    std::vector<SimulatorSide> sides;
    std::vector<SimulatorFleet> fleets;
    std::vector<SimulatorItem> items;
    uint64_t seed = 0;             // 0: the game's random numbers
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
// The sample planets: the viewer's colonies in its home system (the system
// of its capital).
std::vector<ObjectId> simulatorPlanets(const GameState& s, EmpireId viewer);
// Cargo space an item has, and what its cargo takes up.
int64_t simulatorCargoCapacity(const Rules& r, const GameState& s, const SimulatorItem& item);
int64_t simulatorCargoUsed(const Rules& r, const GameState& s, const SimulatorItem& item);

// Why the setup cannot be fought (empty: it can).
std::string simulatorProblem(const Rules& r, const GameState& s, const SimulatorSetup& setup);

struct Simulation {
    GameState state;                 // the sandbox
    Location where;                  // the battle sector
    std::vector<EmpireId> sides;     // the virtual empire of each side
    std::vector<EmpireId> players;   // the sides the player controls
    int interference = 0;            // the home sector's, applied to the battle
    int disruption = 0;
};
// The sandbox for a valid setup (see simulatorProblem). `real` is not changed.
Simulation buildSimulation(const Rules& r, const GameState& real, const SimulatorSetup& setup);
// The battle, ready to fight: tactical for the player's sides (none: strategic,
// already fought to the end; call finish() for the results).
TacticalBattle startSimulation(const Rules& r, Simulation sim);

} // namespace opense4::game::combat
