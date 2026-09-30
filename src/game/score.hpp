#pragma once

// Scores, statistics history and victory (docs/spec/05 §5-6).

#include "game/rules.hpp"
#include "game/state.hpp"

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::score {

int64_t empireScore(const Rules& r, const GameState& s, EmpireId e);
TurnStats currentStats(const Rules& r, const GameState& s, EmpireId e);

// Turn phase 13: statistics and history for every empire, then victory checks
// (sets GameState::gameOver / winner) and eliminations.
void endOfTurn(TurnContext& ctx);

} // namespace opense4::game::score
