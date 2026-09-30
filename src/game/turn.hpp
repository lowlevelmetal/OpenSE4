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
// Deterministic: the same state and orders give the same result on every
// machine.

#include "game/commands.hpp"
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

struct TurnResult {
    std::vector<std::pair<EmpireId, std::string>> rejected;
};

// Processes one full turn: applies orders, runs every phase, advances the date.
TurnResult processTurn(const Rules& r, GameState& s, std::span<const EmpireOrders> orders, const TurnOptions& options = {});

// One empire's end-of-turn processing (spec 05 §8), in this order: its
// ministers' end-of-turn actions when `ministers` (planEconomyStep), its
// statistics, intelligence, research, income, treaties and trade,
// maintenance, planets, happiness, construction, repair, foreign designs
// last seen more than 50 turns ago forgotten (step 12), supply, storage
// cap, system-wide abilities and training, ground combat where its troops
// invade, and the log pruned to this turn's entries. The destruction check
// is the caller's: processTurn runs it right after. (A turn-based game would
// run this when the player ends the turn; OpenSE4 resolves every game
// simultaneously.)
void empireEndOfTurn(TurnContext& ctx, EmpireId e, bool ministers);

// Applies one empire's command list, collecting rejections.
void applyOrders(const Rules& r, GameState& s, const EmpireOrders& orders, std::vector<std::pair<EmpireId, std::string>>& rejected);

} // namespace opense4::game
