// Computer player: the Colonization and Exploration ministers (spec 05 §7.5,
// confirmed: binary unless marked). There is no scout design type: idle
// attack ships explore.

#include "game/ai_planner.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <map>
#include <tuple>

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
            const int j = jumps[ships[i].value][t.system.index()];
            if (j < 0) continue;
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

void planExploration(Planner& p) {
    if (!p.on(Minister::Exploration) || p.neutral) return;
    // Explorers: idle attack ships, and loaded carriers and drone carriers,
    // outside fleets with fewer than 4 damaged components. Ships outside the
    // AI's design types that can only move (premade scouts) explore too (inferred).
    std::vector<VehicleId> explorers;
    for (VehicleId id : p.ownVehicles(Minister::Exploration)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v) || v->status != VehicleStatus::Normal) continue;
        const DesignInfo& di = p.info(v->design);
        if (di.stats.movement <= 0) continue;
        const bool loaded = !v->cargo.units.empty();
        const bool fits = di.role == Role::Attack || ((di.role == Role::Carrier || di.role == Role::DroneCarrier) && loaded) ||
                          (di.role == Role::Other && !di.stats.armed() && di.stats.cargoCapacity == 0);
        if (!fits || damagedComponents(p.r, p.st, *v) >= 4) continue;
        explorers.push_back(id);
    }
    if (explorers.empty() || p.sit.frontier.empty()) return;

    // How many ships each frontier point takes: free points one; when the
    // explorers outnumber the free points 3, 5 and 8 times over, one more each.
    std::map<uint32_t, int> room;
    for (ObjectId wp : p.sit.freeFrontier) room[wp.value] = 1;
    const size_t free = p.sit.freeFrontier.size();
    int extra = 0;
    for (size_t times : {3u, 5u, 8u}) extra += explorers.size() > times * free;
    for (ObjectId wp : p.sit.frontier) room[wp.value] += extra;

    for (VehicleId id : explorers) {
        const Vehicle* v = p.st.vehicle(id);
        const Location at = v->location;
        const std::vector<int> jumps = p.jumpsFrom(at.system);
        std::optional<ObjectId> best;
        std::tuple<int, int, uint32_t> bestKey{};
        for (ObjectId wp : p.sit.frontier) {
            if (room[wp.value] <= 0) continue;
            const SpaceObject& obj = p.st.galaxy.object(wp);
            const int j = jumps[obj.system.index()];
            if (j < 0) continue;
            const int within = obj.system == at.system ? chebyshev(obj.sector, at.sector) : 0;
            const std::tuple<int, int, uint32_t> key{j, within, wp.value};
            if (!best || key < bestKey) {
                best = wp;
                bestKey = key;
            }
        }
        if (!best) continue;
        --room[best->value];
        const SpaceObject& obj = p.st.galaxy.object(*best);
        std::vector<Order> orders;
        if (obj.system != at.system) orders.push_back(moveOrder(locationOf(p.st.galaxy, *best)));
        orders.push_back(warpThrough(p.st, *best));
        p.setOrders(id, std::move(orders));
    }
}

} // namespace opense4::game::ai::detail
