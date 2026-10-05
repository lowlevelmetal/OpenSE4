#pragma once

// Commands for tests: a sample of every command kind with representative
// values (the save format's and the SDK codec's round trips), and the
// commands of a busy turn that the rules accept.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <vector>

namespace opense4::test {

// A frigate with a laser of the test rules (no id or owner).
game::Design warbirdDesign(const game::Rules& r);
// The vehicles an empire owns, in id order.
std::vector<game::VehicleId> vehiclesOf(const game::GameState& s, game::EmpireId e);
// Orders touching queues, fleets, designs, research, messages and lists:
// one empire's turn in a game of the engine fixture (round 0 adds a design,
// a fleet and lists).
game::EmpireOrders busyOrders(const game::Rules& r, const game::GameState& s, game::EmpireId me, int round);
// Every kind of game::Command at least once, with representative values
// (not meant to be applied): empire 1, turn 42.
game::EmpireOrders everyCommandSample(const game::Rules& r);

} // namespace opense4::test
