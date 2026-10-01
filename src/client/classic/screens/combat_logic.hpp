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
    std::vector<ForceRow> rows;   // hulls in the order they appeared, then up to kForcePlanets planets
};

inline constexpr size_t kForcePlanets = 5;

// Per empire of the battle: one row per hull with the ships and bases it has
// now (one each), and the living units of its unit groups under each unit
// design's hull; seekers and obstacles are not counted, nor units still in
// cargo. Lost is the highest count seen so far minus the current one, and a
// hull row stays once it has appeared, even at 0. Then up to kForcePlanets of
// the empire's colonized planets by name: 1 / 0 while it stands, 0 / 1 once
// lost (destroyed, taken or emptied). A unit group that mixes designs counts
// all its units under its first design's hull (inferred).
class CombatForces {
public:
    // Recounts from the pieces' state after the events played so far.
    void update(const game::Rules& r, const game::GameState& s, const game::CombatRecord& record,
                const std::vector<CombatPlayback::Piece>& pieces);
    const std::vector<ForceSide>& sides() const { return sides_; }
    void reset();

private:
    std::vector<ForceSide> sides_;
    std::map<std::pair<uint32_t, std::string>, int> highest_;   // (empire, hull) -> highest count seen
    std::vector<std::pair<uint32_t, std::string>> order_;        // hull rows in the order they appeared
};

// ---- Tactical Combat ---------------------------------------------------------------------------

// The Combat Piece Report's Detail lines (spec 06 §1.10.1): Movement
// (Population for a planet), Shields now/max, Damage (taken against the
// maximum), Supply, Max Targets, Combat Group ("Group N - Leader", "Group N -
// Wingman" or None; a drone shows Target instead), Formation (not for drones),
// and Conditions with the plague level for a planet with plague.
std::vector<std::pair<std::string, std::string>> pieceReportLines(const game::Rules& r, const game::GameState& s,
                                                                  const std::vector<game::combat::TacticalPiece>& pieces, int piece);

// Drop Troops (spec 06 §1.10.2): the adjacent colony of another empire that
// the piece's troops land on, at once, without a target click. The first such
// planet the battle accepts; `problem` says why none does (no adjacent
// colony, or another empire's troops already fight there).
struct DropTarget {
    int planet = -1;
    std::string problem;
};
DropTarget dropTroopsTarget(const game::combat::TacticalBattle& b, int piece);

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
};
std::vector<SimulatorRow> simulatorRows(const game::Rules& r, const game::GameState& s, const game::combat::SimulatorSetup& setup);

// Adds one click's worth of an item (spec 06 §1.10.4): one ship, or one unit
// for the side's group of that kind (a drone always makes a new group), or a
// planet once. Returns false when the object is already in the battle.
bool simulatorAdd(const game::Rules& r, const game::GameState& s, game::combat::SimulatorSetup& setup, game::combat::SimulatorItem item);
// Removes a row's items (a fleet left without members is simply not formed).
void simulatorRemove(game::combat::SimulatorSetup& setup, const SimulatorRow& row);

} // namespace opense4::client::classic
