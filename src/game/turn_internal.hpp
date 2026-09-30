#pragma once

// Helpers shared by the simultaneous turn (turn.cpp) and the turn-based
// game (turn_based.cpp). Not part of the engine's public interface.

#include "game/turn.hpp"

#include <cstdint>
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

// Applies the commands as the empire's orders, collecting rejections.
void applyCommands(TurnContext& ctx, EmpireId e, std::vector<Command> commands);

// Thrown by space combat when a battle's answer is missing (turn.hpp,
// "Tactical combat in turn-based games"); the turn-based calls catch it and
// put the state back as it was before the call.
struct BattleQuestionRaised {
    BattleQuestion question;
};

// The turn-based game turn (turn_based.cpp): processTurn's path when the
// game is not simultaneous.
TurnResult playTurnBasedTurn(const Rules& r, GameState& s, std::span<const EmpireOrders> orders, const TurnOptions& options);

} // namespace opense4::game::detail
