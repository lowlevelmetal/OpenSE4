#pragma once

#include "sim/state.hpp"

namespace opense4::sim {

// Resolves one turn from everyone's current orders. Phases, in order:
//   1. Movement   - all ships move simultaneously, interleaved by speed: a ship
//                   of speed S steps at times 1/S, 2/S, ... S/S of the turn.
//                   Ships stop when they run into hostile ships.
//   2. Combat     - automatic battles wherever hostile ships share a sector.
//   3. Colonization
//   4. Economy    - resource income and research.
//   5. Construction - colony build queues spend from the empire stockpile.
//   6. Population growth, repairs, eliminations.
// Deterministic: the same state and orders always produce the same result.
void processTurn(GameState& s, const Content& c);

// Lets computer players issue their orders, then processes the turn.
void advanceTurn(GameState& s, const Content& c);

} // namespace opense4::sim
