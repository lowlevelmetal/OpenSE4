// Planned routes for the movement lines (spec 06 §2.4 "Movement lines",
// confirmed: binary): the squares a vehicle or fleet will pass through,
// worked out with the movement rules of spec 03 §6.2. The state is only
// read; the replacement squares of blocked steps come from the caller's
// display-only generator, so showing a line never changes game results.

#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace opense4::game::movement {

using namespace detail;

namespace {

// A safety limit on the squares one order adds. A route crosses at most a few
// hundred squares; a detour that kept going round would stop here.
constexpr size_t kMaxSteps = 4096;

// The facts about the moving group that its route depends on.
struct RouteGroup {
    EmpireId owner;
    Location start;
    RouteOptions options;
    bool fighters = false;   // fighters travel to a warp point and fail there (spec 03 §6.2)
};

bool validGoal(const GameState& s, Location l) {
    return l.system.valid() && l.system.index() < s.galaxy.systems.size() && l.sector.valid();
}

void addPoint(std::vector<Location>& points, Location l) {
    if (points.empty() || points.back() != l) points.push_back(l);
}

// Where a Move To or Move To Waypoint leads now; nothing for other orders and
// for an unset waypoint.
std::optional<Location> goalOf(const GameState& s, EmpireId owner, const Order& o) {
    if (o.kind == OrderKind::MoveTo) return o.location;
    if (o.kind != OrderKind::MoveToWaypoint) return std::nullopt;
    const Empire& e = s.empire(owner);
    if (o.amount < 0 || static_cast<size_t>(o.amount) >= e.waypoints.size()) return std::nullopt;
    const Waypoint& w = e.waypoints[static_cast<size_t>(o.amount)];
    return w.set ? std::optional(w.location) : std::nullopt;
}

// The squares of one move from `here` (updated) to `goal`, as the group takes
// them: the route search picks the warp points, and inside a system the group
// steps toward the last square of the system on that route; a step off the
// planned squares plans again from there (as Mover::step does).
void walk(const Rules& r, const GameState& s, const RouteGroup& g, Location& here, Location goal, Rng& rng, std::vector<Location>& points) {
    std::vector<Location> steps;
    size_t pos = 0;
    for (size_t n = 0; here != goal && n < kMaxSteps; ++n) {
        if (pos >= steps.size()) {
            const Location goals[] = {goal};
            auto found = findPathToNearest(r, s, g.owner, here, goals, g.options);
            if (!found || found->path.steps.empty()) return;  // unreachable: nothing more for this order
            steps = std::move(found->path.steps);
            pos = 0;
        }
        const Location next = steps[pos];
        if (next.system != here.system) {
            if (g.fighters) return;
            ++pos;
            here = next;   // the jump: one point, the exit warp point's square
            addPoint(points, here);
            continue;
        }
        size_t last = pos;
        while (last + 1 < steps.size() && steps[last + 1].system == here.system) ++last;
        const auto chosen = inSystemStep(r, s, g.owner, here, steps[last].sector, rng);
        if (!chosen) return;  // blocked: the group stays and the order fails
        const Location to{here.system, *chosen};
        if (to == next) ++pos;
        else steps.clear();
        here = to;
        addPoint(points, here);
    }
}

std::vector<Location> routePoints(const Rules& r, const GameState& s, const RouteGroup& g, std::span<const Order> orders, Rng& rng) {
    std::vector<Location> points{g.start};
    Location here = g.start;
    for (const Order& o : orders) {
        const std::optional<Location> goal = goalOf(s, g.owner, o);
        if (goal && validGoal(s, *goal)) walk(r, s, g, here, *goal, rng, points);
    }
    return points;
}

RouteOptions optionsFor(const GameState& s, EmpireId owner, const Vehicle* first) {
    RouteOptions options;
    options.allowWarp = !(owner.valid() && owner.index() < s.empires.size() && isNeutral(s.empire(owner)));
    options.sweeper = first && leadsSweeperGroup(s, sweeperOf(s, *first));
    return options;
}

} // namespace

int PlannedRoute::turnOf(size_t i) const {
    const int64_t steps = static_cast<int64_t>(i);
    const int64_t left = std::max(0, movementLeft);
    if (steps <= left || movementPerTurn <= 0) return 0;
    return static_cast<int>((steps - left - 1) / movementPerTurn + 1);
}

PlannedRoute planRoute(const Rules& r, const GameState& s, const Vehicle& v, Rng& displayRng) {
    RouteGroup g;
    g.owner = v.owner;
    g.start = v.location;
    g.options = optionsFor(s, v.owner, &v);
    g.fighters = vehicleType(r, s, v) == ruleset::VehicleType::Fighter;
    PlannedRoute out;
    out.points = routePoints(r, s, g, v.orders, displayRng);
    out.movementLeft = std::max(0, v.movement);
    out.movementPerTurn = turnMovement(r, s, v);
    return out;
}

PlannedRoute planRoute(const Rules& r, const GameState& s, const Fleet& f, Rng& displayRng) {
    const std::vector<VehicleId> here = fleetMembersAt(s, f);
    RouteGroup g;
    g.owner = f.owner;
    g.start = f.location;
    g.options = optionsFor(s, f.owner, here.empty() ? nullptr : s.vehicle(here.front()));
    PlannedRoute out;
    int left = INT_MAX, perTurn = INT_MAX;
    for (VehicleId id : here) {
        const Vehicle& v = *s.vehicle(id);
        g.fighters = g.fighters || vehicleType(r, s, v) == ruleset::VehicleType::Fighter;
        left = std::min(left, v.movement);
        perTurn = std::min(perTurn, turnMovement(r, s, v));
    }
    out.movementLeft = here.empty() ? 0 : std::max(0, left);
    out.movementPerTurn = here.empty() ? 0 : perTurn;
    out.points = routePoints(r, s, g, fleetOrders(s, f), displayRng);
    return out;
}

} // namespace opense4::game::movement
