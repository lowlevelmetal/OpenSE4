#pragma once

// Space and ground combat (docs/spec/04). Strategic (auto-resolved) combat
// with a recorded replay in GameState::combats.

#include "game/rules.hpp"
#include "game/state.hpp"

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::combat {

// True when hostile empires that can see each other have combat-capable
// objects in the sector (spec 04 §1).
bool combatPossible(const Rules& r, const GameState& s, Location where);
// Fights a battle in one sector; appends a CombatRecord, applies damage,
// destruction, experience, mood events and logs.
void resolveSpaceCombat(TurnContext& ctx, Location where);
// Turn phase 4: troops against planets, capture of planets and ships.
void runGroundCombat(TurnContext& ctx);

// Chance (percent) that a weapon of `attacker` hits `defender` at `range`.
int toHitPercent(const Rules& r, const GameState& s, const Vehicle& attacker, size_t weaponEntry, const Vehicle& defender, int range);

} // namespace opense4::game::combat
