#pragma once

// Turn processing (docs/spec/05 §8, confirmed: binary). A simultaneous turn:
//
//   1. orders, in player order (a missing player is played by the computer)
//   2. each player's messages
//   3. the date advances
//   4. start of turn, empire by empire: AI state, political step, the
//      ministers that act while orders are given (computer players: all),
//      the Politics minister first, its messages taking effect at once
//   5. movement and space combat: 30 phases (Colonize orders found colonies
//      in them); sight and contact
//   6. each empire's end-of-turn processing (empireEndOfTurn), in empire
//      order, each followed by that empire's destruction check
//   7. design cleanup when a new year starts
//   8. victory check
//   9. event step: hazards, due timed events, one galaxy-wide event roll
//  10. per-turn flags, sight, the AI's memory of the turn; the turn number
//      advances and the economy reports are projected for the next turn
//
// A turn-based game (GameOptions::simultaneous off) plays the same steps one
// player after another; see "Turn-based games" below.
//
// Deterministic: the same state and orders give the same result on every
// machine.

#include "game/commands.hpp"
#include "game/movement.hpp"
#include "game/rules.hpp"
#include "game/score.hpp"
#include "game/state.hpp"
#include "game/tactical.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opense4::game {

// ---- Battles shown as they happen (spec 04 §2, §3 step 1; spec 06 §1.10.5, §1.10.6) -----------
//
// On one machine the original stops whatever started a battle (a move, an
// Attack or a Seek) once the battle is set up, before combat turn 1, and
// shows it: in a turn-based game every battle with a piece of a
// human-controlled empire asks Tactical or Strategic (with the "No Tactical
// Combat" option it opens the Strategic Combat window with Begin and Close
// instead), and in a simultaneous game, when the Settings flag
// `Simultaneous Games Show Strategic Combat` is on, every such battle opens
// the Strategic Combat window. A turn-based game also shows the colony
// owner's end-of-turn ground combat when one of the two empires is human
// (a notice, then the Ground Combat window), and its processing waits.
//
// The engine cannot wait in the middle of a turn, so the calls that play the
// game (resumeTurnBased, applyLive, endPlayerTurn, and processTurn with
// TurnOptions::battles) take the answers in advance, one per stop, in the
// order the stops come up. When they run out, the call stops there: the
// state is left as it was before the call and TurnResult::battle holds the
// question with the game as the battle begins. The client shows it (a
// battle: a combat::TacticalBattle on that copy, fought by hand or by the
// strategies while the window shows it; a ground fight: the record the
// engine fought), then makes the same call again with the answer added; it
// replays, deterministically, to the same stop and goes on: a battle is
// fought with the answer (the tactical sides' orders are its script, and its
// results are applied by the same code as a strategic battle's), a ground
// fight is fought again the same way. Nothing a window shows can change the
// game, so the results are the same whether a window shows a battle or not,
// and a call without answers (network and PBEM hosts, automation) never
// stops.

// How one battle is fought.
struct BattleAnswer {
    std::vector<EmpireId> tactical;               // human sides that fight it tactically (none: strategic)
    std::vector<combat::TacticalOrder> orders;    // their orders (combat::TacticalBattle::script())
};

// A stop: a battle about to start with human participants, or a ground fight to show.
struct BattleQuestion {
    enum class Kind : uint8_t {
        Choose,   // Tactical or Strategic: the Strategic Combat window in its question form
        Show,     // the Strategic Combat window with Begin and Close; the strategies fight it while it shows it
        Ground,   // the colony owner's end-of-turn ground combat: the notice, then the Ground Combat window
    };
    Kind kind = Kind::Choose;
    Location where;
    // The vehicles that entered the sector (the mines' targets; see TacticalBattle::Setup).
    std::optional<std::vector<VehicleId>> entering;
    combat::BattleCheck check;                    // who ran the battle check (see TacticalBattle::Setup)
    std::vector<EmpireId> humans;                 // human sides that fight in it, each asked
    std::vector<EmpireId> participants;           // every side with pieces
    std::shared_ptr<const GameState> state;       // the game just before the battle (before the mines), or the ground fight
    size_t index = 0;                             // its place among the call's stops
    // Ground: the fight as the engine fought it (the answer only says it was shown).
    std::optional<GroundCombat> ground;
};

// Whether battles ask their human participants: turn-based games without
// the "No Tactical Combat" game option (simultaneous games never offer
// tactical combat, spec 04 §2).
bool tacticalOffered(const GameState& s);
// Whether a simultaneous game's battles are shown (Settings `Simultaneous
// Games Show Strategic Combat`, spec 04 §2).
bool simultaneousBattlesShown(const Rules& r);

// Transient data passed between the phases of one turn. Persistent results
// go into GameState; mood events that no happiness update used this turn
// move to GameState::pendingMood at the end of the turn and come back at the
// start of the next.
struct TurnContext {
    const Rules& rules;
    GameState& state;

    std::vector<MoodEvent> moodEvents;
    std::vector<Location> battleSites;          // sectors where space combat happened
    std::vector<std::pair<EmpireId, std::string>> rejected;  // commands refused
    // The answers of this call's stops (null: nothing stops, every battle is
    // strategic; see "Battles shown as they happen").
    struct Battles {
        const std::vector<BattleAnswer>* answers = nullptr;
        size_t next = 0;
    };
    Battles* battles = nullptr;
    // Human players' file lines made at step 2 of their end-of-turn
    // processing (score::recordStatistics), handed out in TurnResult::records.
    std::vector<score::PlayerRecords> records;
    // The units reserve shared by all empires (ai::unitReserveLeft): 0 after
    // the start-of-turn steps, then what the last units step left.
    int64_t unitReserve = 0;

    void mood(EmpireId e, std::string trigger, SystemId sys = {}, ObjectId planet = {}, int count = 1) {
        moodEvents.push_back({e, std::move(trigger), sys, planet, count});
    }
    void log(EmpireId e, LogCategory c, std::string title, std::string text = {}, std::optional<Location> where = std::nullopt,
             std::string picture = {}) {
        addLog(state, e, c, std::move(title), std::move(text), where, std::move(picture));
    }
};

struct TurnOptions {
    // Empires that sent no orders are played by the computer (spec 05 §9.2).
    bool aiForMissing = true;
    // A simultaneous game played on one machine: the answers of the battles
    // shown so far (see "Battles shown as they happen"; null: nothing stops).
    // The turn stops only when the Settings flag shows its battles.
    const std::vector<BattleAnswer>* battles = nullptr;
};

struct TurnResult {
    std::vector<std::pair<EmpireId, std::string>> rejected;
    // Turn-based games: moves of a human player's groups that stopped before
    // a sector with enemies, waiting for the answer (cmd::EnterSector).
    std::vector<EntryQuestion> questions;
    // The stop whose answer is missing (a battle to show or ask about, or a
    // ground fight to show). The call changed nothing; call it again with
    // the answer.
    std::optional<BattleQuestion> battle;
    // The lines to append to human players' statistics, history and log
    // text files, one entry per end-of-turn processing in the call (spec 05
    // §5, §8 step 2). The caller writes them (or not).
    std::vector<score::PlayerRecords> records;
};

// Processes one full turn: applies orders, runs every phase, advances the date.
// In a turn-based game it plays the rest of the game turn instead: every
// player's turn from the one in progress to the last, each player's orders
// carried out at its turn (without the Attack Sector question: orders given
// in advance enter, inferred), a human without orders played by the computer
// as in a simultaneous turn; then the once-per-game-turn steps.
TurnResult processTurn(const Rules& r, GameState& s, std::span<const EmpireOrders> orders, const TurnOptions& options = {});

// One empire's end-of-turn processing (spec 05 §8), in this order: its
// ministers' end-of-turn actions when `ministers` (planEconomyStep), its
// statistics (and a human's files, TurnOptions::records), intelligence,
// research, income, treaties and trade, maintenance, planets, happiness,
// construction, repair, foreign designs last seen more than 50 turns ago
// forgotten (step 12), supply, storage cap, system-wide abilities and
// training, its vehicles' came-from sectors reset (step 16), ground combat,
// and the log pruned to this turn's entries. The destruction check
// is the caller's: processTurn runs it right after; a turn-based game runs it
// when the player ends the turn and checks destruction when the empire's next
// turn comes up.
void empireEndOfTurn(TurnContext& ctx, EmpireId e, bool ministers);

// Applies one empire's command list, collecting rejections.
void applyOrders(const Rules& r, GameState& s, const EmpireOrders& orders, std::vector<std::pair<EmpireId, std::string>>& rejected);

// ---- Turn-based games (spec 05 §8 "Turn-based game", spec 03 §6.3) ------------------------------
//
// Players take their turns one after another in empire order (destroyed
// empires are skipped). A player's turn:
//   1. starts (spec 05 §8 "Turn-based game", confirmed: binary): a human's
//      destruction check (spec 05 §6); the start-of-turn step: AI state, the
//      political step (counting everything since the empire's previous one)
//      and the ministers that act while orders are given (all of them for a
//      computer player, the Politics minister first), whose messages take
//      effect at once; then its vehicles regain their movement and every
//      group carries out its orders, the ministers' new ones included, at
//      most 21 orders each; a computer player's destruction check;
//   2. goes on while the player gives orders: each executes as it is given
//      (applyLive). Moves spend movement points; a step into a sector with
//      enemies fights there at once and the order fails (a human is asked
//      first); messages take effect when sent;
//   3. ends (endPlayerTurn): the empire's end-of-turn processing
//      (empireEndOfTurn), and the turn passes to the next living empire.
// After the last player the date advances, then the design cleanup (a new
// year), the victory check and the event step run, and the per-turn flags
// are cleared, as in a simultaneous turn. Computer players take their turns
// in sequence the same way. GameState::playerTurn records whose turn it is.

inline bool turnBased(const GameState& s) { return !s.options.simultaneous; }

// Who plays the human empires' turns in resumeTurnBased and endPlayerTurn.
struct LiveOptions {
    // Human empires the computer plays for now (network games: players who
    // are gone or out of time, docs/MULTIPLAYER.md), the way processTurn plays
    // a human whose orders are missing (spec 05 §7.1, §9.2): all ministers on
    // for the turn, or only bookkeeping when the player asked for minimal
    // changes (Empire::aiMinimalChanges) (inferred for turn-based games).
    std::vector<EmpireId> computerPlays;

    bool computerPlaysFor(EmpireId e) const;
};

// The empire whose turn it is (turn-based games): the one in progress, or
// the first living empire when the next game turn has not started. Invalid
// in simultaneous games, when the game is over or nobody is alive.
EmpireId activePlayer(const GameState& s);

// Plays until a human player can act: starts the turn of the player whose
// turn it is if it has not started, and plays the computer players' turns
// in sequence (each started, its orders carried out, then ended), and those
// of humans the computer plays for now (`options`). Stops once a human
// player's turn has started, when the game is over, or, when no living human
// is left to play, at the end of the game turn.
// `battles`: see "Tactical combat in turn-based games" above (null: every
// battle is strategic; network and PBEM games always pass null).
TurnResult resumeTurnBased(const Rules& r, GameState& s, const LiveOptions& options = {},
                           const std::vector<BattleAnswer>* battles = nullptr);

// Applies one command of the player whose turn it is and carries out at once
// what it sets in motion: the vehicles, fleets or planets whose orders it
// set act now (moving, fighting, launching), colony ships at their planet
// with movement left found their colonies, and messages take effect. An
// EnterSector answer carries the stopped group on into the sector. Other
// empires' commands, and commands when the turn has not started, are refused.
//
// The Attack Sector questions still open stay in GameState::playerTurn
// (questions): new orders for a group, or an answer, drop its question, and
// so does the group's end (destroyed, or no orders left). A call rolled back
// for a missing battle answer leaves them as they were.
TurnResult applyLive(const Rules& r, GameState& s, EmpireId e, const Command& c, const std::vector<BattleAnswer>* battles = nullptr);

// Ends `e`'s turn: its end-of-turn processing; the turn passes to the next
// living empire, or after the last one the once-per-game-turn steps run.
// Then resumeTurnBased with `options`. When `options` has the computer play
// for `e`, the computer first plays the rest of its turn: its ministers
// (all of them, as a stand-in) plan once more and their orders are carried
// out, and its end-of-turn processing runs with them (inferred). Refused
// when it is not `e`'s turn.
TurnResult endPlayerTurn(const Rules& r, GameState& s, EmpireId e, const LiveOptions& options = {},
                         const std::vector<BattleAnswer>* battles = nullptr);

} // namespace opense4::game
