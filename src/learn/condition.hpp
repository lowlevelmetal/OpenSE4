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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::learn {

enum class Fact : uint8_t {
    // Client facts.
    Window, Selected, Command, Order,
    // Time.
    Turn, TurnsPassed,
    // The player's empire.
    Colonies, Population, Ships, Bases, Units, Fleets, Designs, ResearchQueued, ConstructionQueued, TechsResearched,
    SystemsExplored, EmpiresMet, Treaties, EnemyShipsDestroyed, Score, Minerals, Organics, Radioactives,
    Count
};

struct FactInfo {
    Fact fact;
    std::string_view key;          // the TOML key
    bool text;                     // takes a string (else a whole number)
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
    int64_t number = 0;            // numeric facts: at least this
    std::string text;              // text facts: the window id, kind, command or order
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
};

// The player's commands and the enemy losses seen during a lesson: what the
// game state alone cannot tell afterwards.
class Tracker {
public:
    // A command the player issued successfully.
    void issued(const game::Command& c) { commands_.push_back(c); }
    // Counts the enemy ships and bases destroyed in the battles of `state`
    // the empire fought in, each battle once (call whenever the state changed).
    void observe(const game::GameState& state, game::EmpireId empire);

    const std::vector<game::Command>& commands() const { return commands_; }
    int64_t enemyShipsDestroyed() const { return destroyed_; }

private:
    std::vector<game::Command> commands_;
    std::vector<uint64_t> battles_;   // signatures of the battles counted (sorted)
    int64_t destroyed_ = 0;
};

// Where a step (or the game) began: the counters `sinceMark` facts start from.
struct Mark {
    uint32_t turn = 0;
    size_t commands = 0;
    int techLevels = 0;
    int64_t enemyShipsDestroyed = 0;
};
Mark markNow(const game::Rules& rules, const game::GameState& state, game::EmpireId empire, const Tracker& tracker);

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
