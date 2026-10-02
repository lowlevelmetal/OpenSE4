#pragma once

// Conditions of lessons and training games (docs/LEARNING.md "Conditions"):
// `all`, `any` and `not` over keys that the game state, the player's empire
// and a few client facts answer. The set of keys is fixed here; numbers mean
// "at least". The evaluator only reads; lessons change the game only through
// the player's own commands.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::learn {

enum class Fact : uint8_t {
    // Client facts.
    Window, Selected, Command, Order, Tab,
    // Windows' work in progress.
    DesignComponents, DesignHullChosen, SimulatorOwners, SimulatorItems, BattleBegun, BattleOrder,
    // Time.
    Turn, TurnsPassed,
    // The player's empire.
    Colonies, Population, Ships, Bases, Units, Fleets, Designs, ResearchQueued, ConstructionQueued, TechsResearched,
    SystemsExplored, EmpiresMet, Treaties, Treaty, EnemyShipsDestroyed, PlanetsCaptured, Score, Minerals, Organics, Radioactives,
    Option,
    Count
};

// What a key takes: a whole number ("at least"), a string from a fixed
// vocabulary, or true / false.
enum class FactValue : uint8_t { Number, Text, Flag };

struct FactInfo {
    Fact fact;
    std::string_view key;          // the TOML key
    FactValue value;
    bool sinceMark;                // counts from the step's (or the game's) start
    std::string_view description;  // for the reference (docs/LEARNING.md)
};
std::span<const FactInfo> facts();
const FactInfo* findFact(std::string_view key);
const FactInfo& factInfo(Fact f);

struct Condition {
    enum class Op : uint8_t { All, Any, Not, Fact };
    Op op = Op::All;
    Fact fact = Fact::Turn;
    int64_t number = 0;            // numeric facts: at least this; flags: 1 true, 0 false
    std::string text;              // text facts: the window id, kind, command, order, tab, option or treaty
    std::vector<Condition> children;  // All, Any: any number; Not: one
    int line = 0;                  // where it is written (diagnostics)
};

// What the client knows beyond the game, gathered each frame.
struct ClientFacts {
    std::vector<std::string> openWindows;   // window ids (learn/ids.hpp), oldest first
    // Kinds of what the main window has selected (learn/ids.hpp selectionKinds):
    // "planet" (and "colony" when it is the player's), "ship", "base" or "unit"
    // (and "fleet" when it is in one of the player's fleets), "star",
    // "warp-point"; "sector" while the report lists everything in a sector;
    // "system" when nothing is selected and the report shows the system.
    std::vector<std::string> selected;
    // Selections the player made in the main window so far (the selection
    // a game starts with is not one): `selected` holds only for a selection
    // made since the step began.
    uint64_t selections = 0;
    // The tabs (and filters) the open windows show, as "<window>:<tab>"
    // (learn/ids.hpp windowTabs).
    std::vector<std::string> tabs;
    // The Create Design window, while it is open: the components on the
    // design being built, and whether the player picked a hull in its Size list.
    std::optional<int64_t> designComponents;
    bool designHullChosen = false;
    // The Combat Simulator, while it is open: the races ("owners") that have
    // items in the battle, and the items.
    int64_t simulatorOwners = 0;
    int64_t simulatorItems = 0;
    // The Tactical Combat window, while it is open: whether its battle has
    // begun. And every order the player gave in tactical battles so far, oldest
    // first, as battle order kinds (learn/ids.hpp battleOrderKinds).
    bool battleBegun = false;
    std::vector<std::string> battleOrders;
};

// The player's commands and the enemy losses seen during a lesson: what the
// game state alone cannot tell afterwards.
class Tracker {
public:
    // A command the player issued successfully.
    void issued(const game::Command& c) { commands_.push_back(c); }
    // Counts the enemy ships and bases destroyed in the battles of `state`
    // the empire fought in, each battle once, and the colonies it took from
    // empires it is hostile to (call whenever the state changed; the first
    // call only notes who owns what).
    void observe(const game::GameState& state, game::EmpireId empire);

    const std::vector<game::Command>& commands() const { return commands_; }
    int64_t enemyShipsDestroyed() const { return destroyed_; }
    int64_t planetsCaptured() const { return captured_; }

private:
    std::vector<game::Command> commands_;
    std::vector<uint64_t> battles_;   // signatures of the battles counted (sorted)
    int64_t destroyed_ = 0;
    std::vector<game::EmpireId> owners_;   // per ObjectId: the colony's owner when last observed
    bool seeded_ = false;
    int64_t captured_ = 0;
};

// Where a step (or the game) began: the counters `sinceMark` facts start from.
struct Mark {
    uint32_t turn = 0;
    size_t commands = 0;
    int techLevels = 0;
    int64_t enemyShipsDestroyed = 0;
    int64_t planetsCaptured = 0;
    uint64_t selections = 0;
    size_t battleOrders = 0;
};
// `selections` and `battleOrders`: ClientFacts::selections and the size of
// ClientFacts::battleOrders now.
Mark markNow(const game::Rules& rules, const game::GameState& state, game::EmpireId empire, const Tracker& tracker,
             uint64_t selections = 0, size_t battleOrders = 0);

struct EvalContext {
    const game::Rules& rules;
    const game::GameState& state;
    game::EmpireId empire;
    const ClientFacts& client;
    const Tracker& tracker;
    const Mark& mark;
};

// The value of a numeric fact now (for progress displays); 0 for text facts.
int64_t factValue(Fact f, const EvalContext& ctx);
bool holds(const Condition& c, const EvalContext& ctx);

// "colonies = 5", "all = [...]" (for messages and tests).
std::string describe(const Condition& c);

} // namespace opense4::learn
