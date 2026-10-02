#pragma once

// Computer players and ministers (docs/spec/05 §7), driven by the data in the
// install's Ai/ and Pictures/Races/<Race>/ files where present (ai_data.hpp),
// with built-in defaults otherwise.
//
// Everything the computer does to its own empire is a Command: the planning
// entry points only read the state and return the orders an empire would
// send; the turn pipeline applies them like anyone else's. The AI's memory
// (Empire::aiState, aiTurnsInState, aiMemory, aiDifficulty and the AI fields
// of Relation, including anger) is kept by the mutable steps below.
//
// Turn order (spec 05 §7.1, §8; processTurn). The original runs the
// ministers in two groups:
//   1. at the start of the turn, for each empire in turn, before movement:
//      the AI state update (updateAiState), the political step
//      (politicalStep: territory and anger), then Politics, Troops,
//      Transports, Colonization, Space Yard Ships, Carriers,
//      Mines/Satellites/Drones, Fleets, Defense, Attack, Exploration, Patrol,
//      Resupply, Repair, Scrap, Retrofit and Stellar Manipulation
//      -> planOrders();
//   2. at the start of the empire's end-of-turn processing, before income:
//      Design, Research, Intelligence, Facility Construction, Ship
//      Construction and Facility Construction again (the first facility pass
//      is skipped every fifth turn) -> planEconomyStep().
// After group 1 of every empire, recordAiDecisions notes what was decided;
// at the end of the turn rememberAiEvents keeps what the AI remembers of it.
// planTurn() runs both groups on one private copy (orders first) and
// updateAnger() runs the four mutable steps together at the end of a turn;
// tests and tools use them.

#include "core/rng.hpp"
#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <optional>
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
// Group 1 in its two parts, as the turn runs it (spec 05 §8 step 4): the
// Politics minister alone, whose messages take effect as they are sent, then
// the other ministers, which already see the treaties it changed.
std::vector<Command> planPoliticsOrders(const Rules& r, const GameState& s, EmpireId e);
std::vector<Command> planOrdersAfterPolitics(const Rules& r, const GameState& s, EmpireId e);
// Group 2 above: Design, Research, Intelligence and the construction ministers.
// `unitReserve`: the percentage the vehicle list holds back for units, which
// the turn passes on (the reserve quirk of spec 05 §7.5 "Units file"; see
// unitReserveLeft).
std::vector<Command> planEconomyStep(const Rules& r, const GameState& s, EmpireId e, int64_t unitReserve = 0);
// The reserve quirk (spec 05 §7.5 "Units file", confirmed: binary): the
// reserve is one value shared by all empires. Each empire's start-of-turn AI
// step resets it to 0; the units step of every empire whose Ship
// Construction minister acts sets it to its units file's `Percentage of
// Resources To Reserve For Unit Construction`, or 0 without a units file
// (this function); an empire whose ministers do not act leaves it alone. So
// in a simultaneous game an empire's vehicle list applies the value left by
// the nearest empire before it whose units step ran; in a turn-based game,
// and with the stock install, it is always 0.
int64_t unitReserveLeft(const Rules& r, const Empire& e);
// Orders for a human empire's active ministers (both groups): the global
// ministers switched on in Empire::ministers take over their area, the
// individual ones act on the colonies and vehicles whose minister flag is on.
// Everything is theirs with Empire::ministerAll. Empty for computer empires.
std::vector<Command> ministerCommands(const Rules& r, const GameState& s, EmpireId e);
// Whether a human empire has any minister at work: complete control, a
// global minister switched on, or a colony, vehicle or fleet under minister
// control. False for computer empires (they are all ministers).
bool ministersActive(const GameState& s, EmpireId e);

// The same plans with the planner's own bookkeeping, for tests and tools:
// commands the planner considered but that the rules refused (it drops them).
struct PlanReport {
    std::vector<Command> commands;
    std::vector<std::string> dropped;
};
PlanReport planTurnReport(const Rules& r, const GameState& s, EmpireId e, bool minimal = false);

// ---- Memory (mutable, once per turn) --------------------------------------------------------

// All of the AI's memory steps at once, for every living empire, as at the
// end of a turn (tests and tools; processTurn runs the parts at their own
// places): records what the computer players decided this turn (war
// declarations set anger to 100, accepted demands are carried out half of
// the time), keeps the per-empire counters, runs the AI state machine (spec
// 05 §7.2), the political step (territory and anger, §7.3) for empires whose
// Politics minister is on, counting this turn's battles, reports and
// messages, and what the AI remembers of the turn.
void updateAnger(TurnContext& ctx);
// The parts of updateAnger:
// after the AI's commands were applied (difficulty, counters, war declarations and
// accepted demands);
void recordAiDecisions(TurnContext& ctx);
// ... or one empire's, after its own group 1 (turn-based games, spec 05 §8).
void recordAiDecisions(TurnContext& ctx, EmpireId e);
// before the ministers act (territory and the state machine of §7.2): every
// empire, or one empire at the start of its turn;
void updateAiStates(TurnContext& ctx);
void updateAiState(TurnContext& ctx, EmpireId e);
// then the political step (anger, §7.3) before Politics decides: every
// empire counting this turn's events, or one empire counting the events of
// `eventsTurn` (the turn processed before, as a simultaneous game does; none
// on the first turn), whose battles GameState::combats still holds, or those
// of a window (a turn-based game: everything since the empire's previous
// political step);
struct PoliticalWindow {
    std::optional<uint32_t> turn;     // none: nothing counts
    uint32_t battles = 0;             // of the battles dated `turn` (GameState::combats order), the first ones not counted
    std::vector<uint32_t> logs;       // per EmpireId: of its log entries dated `turn`, the first ones not counted
    uint32_t firstMessage = 0;        // messages with a lower id do not count
    bool andLater = false;            // everything dated after `turn` counts too
    // Simultaneous games: the messages count by delivery instead of `turn`:
    // those delivered since the empire's previous political step (id at
    // least firstMessage) whose political entry is dated `messagesFrom` or
    // later (DiplomaticMessage::dated).
    bool messagesByDelivery = false;
    uint32_t messagesFrom = 0;
};
// The window of a simultaneous turn's political step (spec 05 §7.3 "What it
// counts", confirmed: binary): the battles and log entries of the turn
// processed before, and the messages delivered since the empire's previous
// political step that are dated this turn or the turn before (the players'
// messages of this turn's step 2, and the computer players' messages
// delivered after that step). recordPoliticalStep marks them as counted.
PoliticalWindow simultaneousWindow(const GameState& s, EmpireId e);
void recordPoliticalStep(GameState& s, EmpireId e);
void politicalStep(TurnContext& ctx);
void politicalStep(TurnContext& ctx, EmpireId e, std::optional<uint32_t> eventsTurn);
void politicalStep(TurnContext& ctx, EmpireId e, const PoliticalWindow& window);
// and once per turn after combat and every empire's end-of-turn processing
// (combat counts, traced spies, mine fields met).
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

// The date the ministers and the AI state step see (spec 05 §7.5 "The date",
// confirmed: binary): the advanced date in a simultaneous game, whose date
// advances before the ministers act (§8 step 3), and GameState::turn in a
// turn-based game, whose date advances only after the last player. Every
// "every N turns", "first 50 turns", fleet-delay and early size-cap rule reads it.
uint32_t aiDate(const GameState& s);

// Whether an empire's minister for an area acts this turn: always for
// computer players, else Empire::ministerAll or the area's bit.
bool ministerOn(const Empire& e, Minister m);

// The minister style whose AI files the empire reads (spec 05 §7.1, §7.2):
// Empire::ministerStyle unless "Use Race Minister Style" is ticked. Empty:
// the race's own files.
std::string_view ministerStyleOf(const Empire& e);

// A human empire whose orders are missing is played by the computer for the
// turn (spec 05 §7.1, §8 step 1). standIn() switches all its ministers on and
// returns the player's own settings; restoreMinisters() puts them back after
// the turn. In between, the AI state update, the political step and the
// ministers treat the empire like a computer player (Empire::ministerAll).
// processTurn calls both around the turn and plans such an empire once
// (planOrders, then planEconomyStep), never again through ministerCommands. The minimal-changes option
// (Empire::aiMinimalChanges) is the caller's to honour: the stand-in then
// plans nothing, but its bookkeeping (AI state, anger) still runs.
struct MinisterSettings {
    bool all = false;
    uint32_t areas = 0;
};
MinisterSettings standIn(Empire& e);
void restoreMinisters(Empire& e, const MinisterSettings& saved);

// ---- Player Computer Control (spec 06 §1.2.1, spec 05 §7.1, confirmed: binary) ------------------
//
// Not player commands: the window changes the game on the machine it is used
// on (a local or hotseat game, or a player's own copy), and a network host
// changes its game.

// The window's switch. To computer: the empire is marked computer-controlled,
// all 25 ministers and the minister flag of every one of its vehicles,
// fleets and colonies are switched on. To human: the mark is cleared and they
// are all switched off (the player's earlier settings are not restored). The
// AI state and memory, the stored difficulty, the minister style, the
// password and the options "use individual ministers for newly built
// vehicles" and "AI should not make changes" stay. A human empire that never
// had a stored difficulty gets Medium, the level a human's ministers play at
// (inferred: ours stores none until needed). A neutral empire keeps its kind:
// OpenSE4 keeps neutrality in the same field as the mark (inferred). False
// when nothing was switched (no such empire, or a neutral one).
bool setComputerControl(GameState& s, EmpireId e, bool computer);
// The TCP/IP host's toggle (spec 05 §9.4): only the mark changes; ministers
// and individual flags are untouched. Same conditions as above.
bool setComputerMark(GameState& s, EmpireId e, bool computer);
// Whether any living empire is human-controlled. Local and hotseat games
// end without one (spec 06 §1.2.1 "No human left").
bool anyHumanLeft(const GameState& s);

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
// A planet type and atmosphere pair that is not allowed is redrawn from `rng`.
Race randomPlayerRace(const Rules& r, const ruleset::RacePreset& preset, int racialPoints, Rng& rng);

// Spec 05 §7.1 "random AIs": how many random computer (or neutral) players a
// Low/Medium/High setting (0..2) brings, and a race preset folder for each.
// Races are drawn with pickRandomRace; none is drawn twice.
std::vector<std::string> randomComputerPresets(const Rules& r, int setting, bool neutral, Rng& rng);
// "Computer Player Bonus" (spec 02 §5.4, §6.2; spec 05 §8; confirmed: binary).
// A computer-controlled empire's income of each kind, after tariffs, is
// multiplied by 1, 2, 3 or 5 for None, Low, Medium or High and rounded; its
// construction rates are multiplied by 1, 1.5, 2 or 3 and truncated. There is
// no bonus to planet output percentages. Human empires get factor 1 / 100 %.
int incomeBonusFactor(const GameState& s, EmpireId e);
int constructionBonusPercent(const GameState& s, EmpireId e);

} // namespace opense4::game::ai
