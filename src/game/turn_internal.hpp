#pragma once

// Helpers shared by the simultaneous turn (turn.cpp) and the turn-based
// game (turn_based.cpp). Not part of the engine's public interface.

#include "game/players.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace opense4::game::detail {

// How an empire is played this turn (spec 05 §7.1, §9.2).
enum class Control : uint8_t {
    Player,    // a human who sent orders (or whose missing orders nobody covers)
    Computer,  // a computer or neutral empire: every minister acts
    StandIn,   // a human whose orders are missing: all ministers on for the turn
    Absent,    // the same, but the player forbade AI changes: bookkeeping only
};

// Whether the empire's ministers plan orders this turn: always for a
// computer player, for a human only while some minister is at work (a
// stand-in has them all switched on).
bool ministersPlan(const GameState& s, EmpireId e, Control c);

bool living(const GameState& s, EmpireId e);

// Step 16 of an empire's end-of-turn processing (spec 05 §8): each of its
// ships, bases, fighter groups and drone groups records its current sector
// as the one it comes from (Vehicle::cameFrom).
void resetCameFrom(const Rules& r, GameState& s, EmpireId e);

// Applies the commands as the empire's orders, collecting rejections.
void applyCommands(TurnContext& ctx, EmpireId e, std::vector<Command> commands);
// Applies one command as the empire's order, collecting its rejection.
CommandResult applyCommand(TurnContext& ctx, EmpireId e, const Command& c);

// One planning call of an empire (players.hpp): its player's answer when
// its controller plays it, through `sink`; else, or when the player gives
// none, `classic`, which plans with the built-in AI and carries the orders
// out as the moment does.
void planCall(TurnContext& ctx, EmpireId e, PlanCall call, const CommandSink& sink, const std::function<void()>& classic);

// Keeps the figures an empire's start-of-turn step worked out for its
// economy step (TurnContext::aiStartFigures).
void keepStartFigures(TurnContext& ctx, EmpireId e, const std::optional<ai::StartOfTurnFigures>& figures);

// Thrown by space combat when a battle's answer is missing (turn.hpp,
// "Tactical combat in turn-based games"); the turn-based calls catch it and
// put the state back as it was before the call.
struct BattleQuestionRaised {
    BattleQuestion question;
};

// Runs a call that plays the game with the answers of its stops (turn.hpp,
// "Battles shown as they happen"): `body` gets the answers to hand out, or
// null when `answers` is (nothing stops; no copy is kept). A stop whose
// answer is missing stops the call: the state goes back to what it was
// before, and the result holds the question. Any other exception (a fault)
// also puts the state back before it goes on to the caller, so that a game
// half carried through a call is never shown or saved.
template <class Body>
TurnResult withBattles(GameState& s, const std::vector<BattleAnswer>* answers, Body&& body) {
    if (!answers) return body(nullptr);
    GameState before = s;
    TurnContext::Battles battles{answers, 0};
    try {
        return body(&battles);
    } catch (BattleQuestionRaised& raised) {
        // The players' answers so far wait for the call made again with the
        // answer, which gives them again without asking (players.hpp).
        const size_t kept = std::min(before.journal.entries.size(), s.journal.entries.size());
        std::vector<JournalEntry> made(s.journal.entries.begin() + static_cast<std::ptrdiff_t>(kept), s.journal.entries.end());
        s = std::move(before);
        s.journal.replay = std::move(made);
        TurnResult out;
        out.battle = std::move(raised.question);
        return out;
    } catch (...) {
        s = std::move(before);
        throw;
    }
}

// The turn-based game turn (turn_based.cpp): processTurn's path when the
// game is not simultaneous.
TurnResult playTurnBasedTurn(const Rules& r, GameState& s, std::span<const EmpireOrders> orders, const TurnOptions& options);

} // namespace opense4::game::detail
