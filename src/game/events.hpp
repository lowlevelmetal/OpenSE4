#pragma once

// Random and timed events (docs/spec/05 §4).

#include "game/rules.hpp"
#include "game/state.hpp"

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::events {

// Turn phase 9: rolls new events and fires timed events that are due.
void runEvents(TurnContext& ctx);

} // namespace opense4::game::events
