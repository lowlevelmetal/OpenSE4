#pragma once

// Orders as they are given (docs/spec/03 §8). Several orders are expanded
// into simpler ones the moment they are given and never exist as stored
// orders (confirmed: binary):
//   - Explore becomes Move To plus Warp for the nearest explorable warp point;
//   - Resupply becomes Move To the nearest depot, Repair Move To the nearest
//     repair source;
//   - the composite orders (Warp, Colonize, Load Cargo, Drop Cargo, Launch
//     Units, Recover Units) become Move To the target sector, when it is
//     elsewhere, plus the order; Colonize first adds Load Cargo (population)
//     where it starts when the ship carries no population.
// cmd::SetOrders expands the orders it adds; movement expands an Explore,
// Resupply or Repair that reached a list some other way when it comes up.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <span>
#include <vector>

namespace opense4::game {

// Who receives orders and where the next order starts from.
struct OrderContext {
    EmpireId owner;
    std::vector<VehicleId> members;  // the vehicle, or the fleet's members in its sector
    VehicleId lead;                  // the group's leading ship (the vehicle, or the fleet's leader); invalid: the first member
    Location at;                     // where the next order starts
    bool carriesPopulation = false;  // some member carries population there
};
OrderContext orderContextOf(const GameState& s, const Vehicle& v);
OrderContext orderContextOf(const GameState& s, const Fleet& f);

// Where `o` leaves the group, as far as it is known when the order is given
// (a simple order's target; unchanged for orders that do not move it).
void advanceOrderContext(const GameState& s, OrderContext& ctx, const Order& o);

// Appends the orders `o` stands for to `out` and moves `ctx` past them.
// Simple orders are appended unchanged. An Explore, Resupply or Repair with
// nowhere to go adds nothing.
void expandOrder(const Rules& r, const GameState& s, OrderContext& ctx, const Order& o, std::vector<Order>& out);

// cmd::SetOrders: `given` replaces `current`. The leading orders that repeat
// `current` are kept as they are; every order after them is expanded.
std::vector<Order> expandGivenOrders(const Rules& r, const GameState& s, OrderContext ctx, std::span<const Order> current,
                                     std::span<const Order> given);

// Colonize orders expanded when given carry this in Order::amount: their
// colonists were handled by the Load Cargo in front of them.
inline constexpr int kColonizeExpanded = 1;

} // namespace opense4::game
