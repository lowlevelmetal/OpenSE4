// Computer player: the Colonization and Exploration ministers (spec 05 §7.5,
// confirmed: binary unless marked). There is no scout design type: attack
// ships and loaded carriers explore.

#include "game/ai_planner.hpp"
#include "game/movement.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <tuple>
#include <vector>

namespace opense4::game::ai::detail {

namespace {

Order warpThrough(const GameState& s, ObjectId wp) {
    Order o;
    o.kind = OrderKind::Warp;
    o.object = wp;
    o.location = locationOf(s.galaxy, wp);
    return o;
}

int damagedComponents(const Rules& r, const GameState& s, const Vehicle& v) {
    int n = 0;
    const size_t entries = s.design(v.design).entries.size();
    for (size_t i = 0; i < entries; ++i) n += !entryIntact(r, s, v, i);
    return n;
}

} // namespace

int movementNow(const Planner& p, const Vehicle& v) {
    return p.st.options.simultaneous ? v.movement : movement::detail::turnMovement(p.r, p.st, v);
}

void planColonization(Planner& p) {
    if (!p.on(Minister::Colonization)) return;
    // Idle colony ships: no orders, or their target has become one of our colonies.
    std::vector<VehicleId> ships;
    for (VehicleId id : p.ownVehicles(Minister::Colonization)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || p.busy.contains(id) || v->status == VehicleStatus::Mothballed) continue;
        if (p.info(v->design).role != Role::Colonizer) continue;
        bool idle = v->orders.empty();
        if (!idle && v->orders.back().kind == OrderKind::Colonize)
            if (const Colony* c = p.st.colony(v->orders.back().object); c && c->owner == p.id) idle = true;
        if (idle) ships.push_back(id);
    }
    if (ships.empty()) return;
    std::map<uint32_t, std::vector<int>> jumps;
    for (VehicleId id : ships) jumps[id.value] = p.jumpsFrom(p.st.vehicle(id)->location.system);

    for (const ColonyTarget& t : p.sit.colonyTargets) {
        if (ships.empty()) break;
        if (!t.settleable || p.reservedPlanets.contains(t.planet) || !p.mayEnter(t.system)) continue;
        const SpaceObject& planet = p.st.galaxy.object(t.planet);
        std::optional<size_t> best;
        int bestJumps = 0;
        for (size_t i = 0; i < ships.size(); ++i) {
            const Vehicle* v = p.st.vehicle(ships[i]);
            if (!p.info(v->design).stats.canColonize(planet.surface)) continue;
            const int j = jumps[ships[i].value][t.system.index()];  // over every link (spec 05 §7.2)
            if (j == kUnreachable) continue;
            if (!best || j < bestJumps) {
                best = i;
                bestJumps = j;
            }
        }
        if (!best) continue;
        const VehicleId ship = ships[*best];
        // "Move there, then colonize" is the Colonize order itself: it loads
        // colonists where it starts when the ship carries none, then travels
        // (spec 03 §8). A separate Move To first would load them at the
        // target instead, and the colony would start empty (inferred, spec
        // 05 open question 23).
        Order colonize;
        colonize.kind = OrderKind::Colonize;
        colonize.object = t.planet;
        colonize.location = locationOf(p.st.galaxy, t.planet);
        if (p.setOrders(ship, {colonize})) p.reservedPlanets.insert(t.planet);
        ships.erase(ships.begin() + static_cast<std::ptrdiff_t>(*best));
    }
}

// Spec 05 §7.5 "Exploration" (confirmed: binary).
void planExploration(Planner& p) {
    if (!p.on(Minister::Exploration) || p.neutral) return;
    // Nothing is done while no free frontier point is left (§7.2). (So the
    // rule that fills an empty list with every frontier point when the Attack
    // Ships outnumber them three times over never applies; spec 05 Q64.)
    if (p.sit.freeFrontier.empty()) return;

    // Explorers, in the game's object order: Attack Ships and Attack Bases (a
    // base cannot move, so its orders do nothing), and Carriers and Drone
    // Carriers whose cargo holds their kind of unit and is more than half
    // full; each in normal status, with fewer than 4 destroyed parts and
    // supply above 0, outside fleets, with no orders or a Seek first.
    std::vector<VehicleId> explorers;
    for (VehicleId id : vehiclesInObjectOrder(p.st)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->count <= 0 || !p.controlsVehicle(*v, Minister::Exploration) || v->fleet.valid()) continue;
        if (v->status != VehicleStatus::Normal || v->supply <= 0) continue;
        if (!v->orders.empty() && v->orders.front().kind != OrderKind::Seek) continue;
        const DesignInfo& di = p.info(v->design);
        bool fits = di.aiType == "Attack Ship" || di.aiType == "Attack Base";
        if (di.role == Role::Carrier || di.role == Role::DroneCarrier) {
            const ruleset::VehicleType kind = di.role == Role::Carrier ? ruleset::VehicleType::Fighter : ruleset::VehicleType::Drone;
            const bool carries = std::any_of(v->cargo.units.begin(), v->cargo.units.end(), [&](const UnitStack& u) {
                return u.count > 0 && p.r.hull(p.st.design(u.design).hull).type == kind;
            });
            const int capacity = vehicleCargoCapacity(p.r, p.st, *v);
            fits = carries && capacity > 0 && cargoSpaceUsed(p.r, p.st, v->cargo) * 2 > capacity;
        }
        if (!fits || damagedComponents(p.r, p.st, *v) >= 4) continue;
        explorers.push_back(id);
    }
    if (explorers.empty()) return;

    // The point list: the free frontier points in list order. With A the
    // empire's Attack Ships (all of them), when A exceeds 5 × the list's
    // length each point of the list as it was is entered once more while A
    // still exceeds 5 × the current length, and once more again while A
    // exceeds 8 × it.
    int64_t attackShips = 0;
    for (const Vehicle& v : p.st.vehicles) attackShips += v.owner == p.id && v.count > 0 && p.info(v.design).aiType == "Attack Ship";
    std::vector<ObjectId> list = p.sit.freeFrontier;
    if (attackShips > 5 * static_cast<int64_t>(list.size())) {
        const std::vector<ObjectId> was = list;
        for (ObjectId wp : was) {
            if (attackShips > 5 * static_cast<int64_t>(list.size())) list.push_back(wp);
            if (attackShips > 8 * static_cast<int64_t>(list.size())) list.push_back(wp);
        }
    }

    // Each explorer takes the point with the smallest travel distance (the
    // earlier one on a tie). On the point itself it gets no order this turn.
    // Otherwise a Seek toward the point's sector, and the Warp through it when
    // its movement points now (what its last movement left) reach the
    // distance (movementNow); the point leaves the list. A Seek lasts one
    // movement phase, so an explorer that cannot reach its point this turn is
    // planned again.
    for (VehicleId id : explorers) {
        if (list.empty()) break;
        const Vehicle* v = p.st.vehicle(id);
        std::vector<Location> goals;
        for (ObjectId wp : list) goals.push_back(locationOf(p.st.galaxy, wp));
        const auto near = movement::findPathToNearest(p.r, p.st, p.id, v->location, goals);
        if (!near || near->path.length == 0) continue;
        const ObjectId wp = list[near->goal];
        std::vector<Order> orders{seekOrder(goals[near->goal])};
        if (movementNow(p, *v) >= near->path.length) orders.push_back(warpThrough(p.st, wp));
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(near->goal));
        p.setOrders(id, std::move(orders));
    }
}

} // namespace opense4::game::ai::detail
