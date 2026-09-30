#include "game/combat.hpp"

#include "game/turn.hpp"

namespace opense4::game::combat {

// Stub: implemented by the combat work package (docs/spec/04).

bool combatPossible(const Rules&, const GameState&, Location) { return false; }
void resolveSpaceCombat(TurnContext&, Location) {}
void runGroundCombat(TurnContext&) {}
int toHitPercent(const Rules&, const GameState&, const Vehicle&, size_t, const Vehicle&, int) { return 0; }

} // namespace opense4::game::combat
