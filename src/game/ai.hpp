#pragma once

// Computer players and ministers (docs/spec/05 §7), driven by the data in the
// install's Ai/ and Pictures/Races/<Race>/ files where present.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::ai {

// A full turn of orders for a computer-controlled empire, or for a human
// empire whose orders are missing (`minimal`: only keep things running).
std::vector<Command> planTurn(const Rules& r, const GameState& s, EmpireId e, bool minimal = false);
// Orders for the colonies and vehicles a human put under minister control.
std::vector<Command> ministerCommands(const Rules& r, const GameState& s, EmpireId e);
// Turn phase 12: anger decay and border/incident updates, Mega Evil Empire.
void updateAnger(TurnContext& ctx);

} // namespace opense4::game::ai
