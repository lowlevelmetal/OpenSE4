#pragma once

// Fog of war for network games: the state as one empire may know it. The
// host sends every player only this view, so a modified client cannot read
// other empires' fleets, plans or treasuries. The view keeps every reference
// valid (it passes validateState), so the client's windows and command
// checks work on it unchanged.

#include "game/state.hpp"

namespace opense4::game {

// `viewer` invalid: a spectator view (only public information).
GameState redactForEmpire(const GameState& s, EmpireId viewer);

} // namespace opense4::game
