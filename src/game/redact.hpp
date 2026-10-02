#pragma once

// Fog of war for network games: the state as one empire may know it. The
// host sends every player only this view, so a modified client cannot read
// other empires' fleets, plans or treasuries. The view keeps every reference
// valid (it passes validateState), so the client's windows and command
// checks work on it unchanged.
//
// The original sends every player the whole game and hides things only in
// its windows (spec 05 §9.2); this redaction is OpenSE4's own design, which
// gives the same visible result (spec 05 §9.5, spec 01 §6.9 "What other
// players see"). A colony hidden from the viewer by its cloak (its planet
// fails sight::canSeePlanet for the viewer) is left out of the view
// altogether: the colony is dropped and its planet taken out of its system's
// object list, as a removed object is, so the viewer's windows neither draw
// nor list the planet and a modified client learns nothing about the colony.

#include "game/rules.hpp"
#include "game/state.hpp"

namespace opense4::game {

// `viewer` invalid: a spectator view (only public information). `r`: the
// rules the game is played with (the sight tests need them). The result is
// a function of the state alone, so every machine that redacts the same
// state gets the same view and checksum.
GameState redactForEmpire(const Rules& r, const GameState& s, EmpireId viewer);

} // namespace opense4::game
