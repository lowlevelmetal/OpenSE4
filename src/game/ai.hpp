#pragma once

// Computer players and ministers (docs/spec/05 §7), driven by the data in the
// install's Ai/ and Pictures/Races/<Race>/ files where present (ai_data.hpp),
// with built-in defaults otherwise.
//
// Everything the computer does is a Command: planTurn() and ministerCommands()
// only read the state and return the orders an empire would send; the turn
// pipeline applies them like anyone else's. Long-lived AI memory is limited to
// Empire::aiState / aiTurnsInState and Relation::anger, which updateAnger()
// maintains in turn phase 12. Everything else is re-derived from the state
// each turn, so saving and loading a game never loses AI plans.

#include "core/rng.hpp"
#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::ai {

// AI states (spec 05 §7.2). Stored as an int in Empire::aiState; most AI
// tables filter their rows by state.
enum class AiState : uint8_t {
    Exploration,
    Infrastructure,
    PrepareForAttack,
    Attack,
    SecureHoldings,     // "Secure Holdings After Attack"
    Incursion,
    PrepareForDefense,
    DefendShortTerm,    // "Defend (Short Term)"
    DefendLongTerm,     // "Defend (Long Term)"
    NotConnected,
    Count
};
inline constexpr size_t kAiStates = static_cast<size_t>(AiState::Count);
std::string_view displayName(AiState s);  // the identifier used in the AI files
bool parseAiState(std::string_view text, AiState& out);
AiState stateOf(const Empire& e);

// A full turn of orders for a computer-controlled empire, or for a human
// empire whose orders are missing (`minimal`: only keep things running).
std::vector<Command> planTurn(const Rules& r, const GameState& s, EmpireId e, bool minimal = false);
// Orders for the colonies and vehicles a human put under minister control
// (everything when Empire::ministerAll is set). Empty for computer empires.
std::vector<Command> ministerCommands(const Rules& r, const GameState& s, EmpireId e);
// Turn phase 12: anger (decay, borders, coveted planets, combat, intelligence,
// received messages, Mega Evil Empire) and the AI state transitions.
void updateAnger(TurnContext& ctx);

// The same plans with the planner's own bookkeeping, for tests and tools:
// commands the planner considered but that the rules refused (it drops them).
struct PlanReport {
    std::vector<Command> commands;
    std::vector<std::string> dropped;
};
PlanReport planTurnReport(const Rules& r, const GameState& s, EmpireId e, bool minimal = false);

// ---- Helpers shared with the UI and other modules ---------------------------------------

// The state the empire's computer player should be in, from the facts of the
// current state and its previous state (phase 12 applies it).
AiState nextState(const Rules& r, const GameState& s, EmpireId e);
// Score used for AI politics and the Mega Evil Empire: score::empireScore when
// the score module computes one, otherwise an estimate from the same statistics.
int64_t politicalScore(const Rules& r, const GameState& s, EmpireId e);
std::vector<int64_t> politicalScores(const Rules& r, const GameState& s);  // index = EmpireId
// The Mega Evil Empire (spec 05 §7.6), or an invalid id when there is none.
EmpireId megaEvilEmpire(const Rules& r, const GameState& s);
// Leader mood shown in the Empires window for an anger value (inferred bands).
std::string_view moodLabel(int anger);
// Spec 05 §7.1 "random AIs": how many random computer (or neutral) players a
// Low/Medium/High setting (0..2) brings, and a race preset folder for each.
// Computer races are drawn by personality group weight; group 0 is never drawn.
std::vector<std::string> randomComputerPresets(const Rules& r, int setting, bool neutral, Rng& rng);
// "Computer Player Bonus" as a production/research percentage for the economy
// and research modules (0 for humans). The size of the bonus is open (spec 05
// §7.1); we use 10 % per bonus step (inferred).
int bonusPercent(const GameState& s, EmpireId e);

} // namespace opense4::game::ai
