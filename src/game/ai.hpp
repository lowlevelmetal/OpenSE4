#pragma once

// Computer players and ministers (docs/spec/05 §7), driven by the data in the
// install's Ai/ and Pictures/Races/<Race>/ files where present (ai_data.hpp),
// with built-in defaults otherwise.
//
// Everything the computer does to its own empire is a Command: the planning
// entry points only read the state and return the orders an empire would
// send; the turn pipeline applies them like anyone else's. The AI's memory
// (Empire::aiState, aiTurnsInState, aiMemory, aiDifficulty and the AI fields
// of Relation, including anger) is kept by updateAnger(), which runs with a
// mutable state once per turn.
//
// Turn order (spec 05 §7.1, §8). The original runs the ministers in two
// groups:
//   1. while orders are given, before movement: the AI state update, then
//      Politics, Troops, Transports, Colonization, Space Yard Ships,
//      Carriers, Mines/Satellites/Drones, Fleets, Defense, Attack,
//      Exploration, Patrol, Resupply, Repair, Scrap, Retrofit and Stellar
//      Manipulation -> planOrders();
//   2. at the start of the empire's economy step, before income: Design,
//      Research, Intelligence, Facility Construction, Ship Construction and
//      Facility Construction again (the first facility pass is skipped every
//      fifth turn) -> planEconomyStep().
// planTurn() runs both groups on one private copy (orders first), which is
// what the turn pipeline calls today. The AI state update and the political
// step (territory, anger) need a mutable state and run in updateAnger(); it
// is called at the end of a turn, which is the same point in the cycle as
// "before the ministers of the next turn".

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
    Incursion,          // never entered (confirmed: binary)
    PrepareForDefense,  // never entered
    DefendShortTerm,    // "Defend (Short Term)"
    DefendLongTerm,     // never entered
    NotConnected,
    Count
};
inline constexpr size_t kAiStates = static_cast<size_t>(AiState::Count);
std::string_view displayName(AiState s);  // the identifier used in the AI files
bool parseAiState(std::string_view text, AiState& out);
AiState stateOf(const Empire& e);

// ---- Planning (read-only; the pipeline applies the commands) ------------------------------

// A full turn of orders for a computer-controlled empire, or for a human
// empire whose orders are missing. `minimal`: the absent player forbade AI
// changes, so nothing is planned (spec 05 §7.1).
std::vector<Command> planTurn(const Rules& r, const GameState& s, EmpireId e, bool minimal = false);
// Group 1 above: the ministers that act while orders are given.
std::vector<Command> planOrders(const Rules& r, const GameState& s, EmpireId e);
// Group 2 above: Design, Research, Intelligence and the construction ministers.
std::vector<Command> planEconomyStep(const Rules& r, const GameState& s, EmpireId e);
// Orders for a human empire's active ministers (both groups): the global
// ministers switched on in Empire::ministers take over their area, the
// individual ones act on the colonies and vehicles whose minister flag is on.
// Everything is theirs with Empire::ministerAll. Empty for computer empires.
std::vector<Command> ministerCommands(const Rules& r, const GameState& s, EmpireId e);

// The same plans with the planner's own bookkeeping, for tests and tools:
// commands the planner considered but that the rules refused (it drops them).
struct PlanReport {
    std::vector<Command> commands;
    std::vector<std::string> dropped;
};
PlanReport planTurnReport(const Rules& r, const GameState& s, EmpireId e, bool minimal = false);

// ---- Memory (mutable, once per turn) --------------------------------------------------------

// The AI step of the turn (currently phase 12), for every living empire:
// records what the computer players decided this turn (war declarations set
// anger to 100, accepted demands are carried out half of the time), keeps
// the per-empire counters, runs the AI state machine (spec 05 §7.2) for
// computer players and humans with active ministers, and the political step
// (territory and anger, §7.3) for empires whose Politics minister is on.
void updateAnger(TurnContext& ctx);
// The parts of updateAnger, in its order, for a turn order that runs them apart:
// after the AI's commands were applied (difficulty, counters, war declarations and
// accepted demands);
void recordAiDecisions(TurnContext& ctx);
// before the ministers act (territory and the state machine of §7.2);
void updateAiStates(TurnContext& ctx);
// then the political step (anger, §7.3) before Politics decides;
void politicalStep(TurnContext& ctx);
// and once per turn after combat (combat counts, traced spies, mine fields met).
void rememberAiEvents(TurnContext& ctx);

// ---- Helpers shared with the UI and other modules ---------------------------------------

// The state the empire's computer player moves to from its current state
// (the transition part of updateAnger, without changing anything).
AiState nextState(const Rules& r, const GameState& s, EmpireId e);
// Scores used by AI politics and the Mega Evil Empire: score::empireScore
// (spec 05 §5), 0 for eliminated empires. Index = EmpireId.
std::vector<int64_t> politicalScores(const Rules& r, const GameState& s);
int64_t politicalScore(const Rules& r, const GameState& s, EmpireId e);
// The Mega Evil Empire as `viewer` sees it (spec 05 §7.6), or an invalid id.
EmpireId megaEvilEmpire(const Rules& r, const GameState& s, EmpireId viewer);
EmpireId megaEvilEmpire(const Rules& r, const std::vector<int64_t>& scores, const GameState& s, EmpireId viewer);
// The leader's mood word shown in the Empires window for an anger value (spec 05 §7.3).
std::string_view moodLabel(int anger);

// Whether an empire's minister for an area acts this turn: always for
// computer players, else Empire::ministerAll or the area's bit.
bool ministerOn(const Empire& e, Minister m);

// Difficulty (spec 05 §7.1): the empire's level, kDifficultyLow..High.
// Until the AI step assigns it (Empire::aiDifficulty < 0): the chosen level
// for random AI players (GameOptions::randomAiPlayers), Medium otherwise.
int difficultyOf(const GameState& s, EmpireId e);
// An empire founded by a revolt: the highest level among the computer
// empires, or Medium when there is none.
int rebelDifficulty(const GameState& s);

// The colony type the computer gives a new colony at colonization (spec 05
// §7.5): the two pre-rules, then the first AI_Planet_Types row that passes,
// else Mining Colony. For every empire whose player is not asked.
std::string colonyTypeAtColonization(const Rules& r, const GameState& s, EmpireId e, ObjectId planet);

// One random race (spec 05 §7.1): computer players pick the personality group
// furthest below its target share, then a race of that group; neutral players
// draw from the neutral races. `used` are the race folders already in the
// game (the new player is not in it). nullptr when no race is left.
const ruleset::RacePreset* pickRandomRace(const Rules& r, Rng& rng, bool neutral, const std::vector<std::string>& used);
// The race a random computer player plays: the preset's `Race Opt` set of the
// racial-point level (1 for 2000, 2 for 3000, 3 for 5000; none for 0),
// characteristics applied while they fit the budget, then traits that fit.
Race randomPlayerRace(const Rules& r, const ruleset::RacePreset& preset, int racialPoints);

// Spec 05 §7.1 "random AIs": how many random computer (or neutral) players a
// Low/Medium/High setting (0..2) brings, and a race preset folder for each.
// Races are drawn with pickRandomRace; none is drawn twice.
std::vector<std::string> randomComputerPresets(const Rules& r, int setting, bool neutral, Rng& rng);
// "Computer Player Bonus" as a production/research percentage for the economy
// and research modules (0 for humans). The size of the bonus is open (spec 05
// §7.1); we use 10 % per bonus step (inferred).
int bonusPercent(const GameState& s, EmpireId e);

} // namespace opense4::game::ai
