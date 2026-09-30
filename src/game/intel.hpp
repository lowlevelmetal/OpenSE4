#pragma once

// Intelligence projects and counter-intelligence (docs/spec/05 §2).

#include "game/rules.hpp"
#include "game/state.hpp"

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::intel {

// Turn phase 7: spends Empire::economy.intelligence, executes finished projects.
void runIntel(TurnContext& ctx);
// Points an empire has in defense projects this turn (UI).
int64_t defensePoints(const Rules& r, const GameState& s, EmpireId e);

} // namespace opense4::game::intel
