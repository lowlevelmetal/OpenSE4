#pragma once

#include "sim/state.hpp"

namespace opense4::sim {

// Issues orders for a computer-controlled empire through the regular command
// path (so the AI can never do anything a human could not). Deliberately
// simple for now: explore, colonize, grow the economy, build some warships.
void runAi(GameState& s, const Content& c, EmpireId empire);

} // namespace opense4::sim
