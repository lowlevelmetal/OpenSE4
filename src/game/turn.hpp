#pragma once

// Turn processing (docs/spec/05 §8, confirmed: binary). A simultaneous turn:
//
//   1. orders, in player order (a missing player is played by the computer)
//   2. each player's messages
//   3. the date advances
//   4. start of turn, empire by empire: AI state, political step, the
//      ministers that act while orders are given (computer players: all)
//   5. movement and space combat: 30 phases; colonization; sight and contact
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
#include "game/state.hpp"

#include <span>
#include <string>
#include <vector>

namespace opense4::game {

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
};

using movement::EntryQuestion;

struct TurnResult {
    std::vector<std::pair<EmpireId, std::string>> rejected;
    // Turn-based games: moves of a human player's groups that stopped before
    // a sector with enemies, waiting for the answer (cmd::EnterSector).
    std::vector<EntryQuestion> questions;
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
// statistics, intelligence, research, income, treaties and trade,
// maintenance, planets, happiness, construction, repair, foreign designs
// last seen more than 50 turns ago forgotten (step 12), supply, storage
// cap, system-wide abilities and training, ground combat where its troops
// invade, and the log pruned to this turn's entries. The destruction check
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
//   1. starts: the empire's destruction check (spec 05 §6); its vehicles
//      regain their movement and first carry on with their orders; then its
//      start-of-turn step: AI state, political step and the ministers that act
//      while orders are given (all of them for a computer player), whose
//      orders are carried out at once and whose messages take effect;
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

// The empire whose turn it is (turn-based games): the one in progress, or
// the first living empire when the next game turn has not started. Invalid
// in simultaneous games, when the game is over or nobody is alive.
EmpireId activePlayer(const GameState& s);

// Plays until a human player can act: starts the turn of the player whose
// turn it is if it has not started, and plays the computer players' turns
// in sequence (each started, its orders carried out, then ended). Stops once
// a human player's turn has started, when the game is over, or, when no
// living human is left to play, at the end of the game turn.
TurnResult resumeTurnBased(const Rules& r, GameState& s);

// Applies one command of the player whose turn it is and carries out at once
// what it sets in motion: the vehicles, fleets or planets whose orders it
// set act now (moving, fighting, launching), colony ships at their planet
// with movement left found their colonies, and messages take effect. An
// EnterSector answer carries the stopped group on into the sector. Other
// empires' commands, and commands when the turn has not started, are refused.
TurnResult applyLive(const Rules& r, GameState& s, EmpireId e, const Command& c);

// Ends `e`'s turn: its end-of-turn processing; the turn passes to the next
// living empire, or after the last one the once-per-game-turn steps run.
// Then resumeTurnBased. Refused when it is not `e`'s turn.
TurnResult endPlayerTurn(const Rules& r, GameState& s, EmpireId e);

} // namespace opense4::game
