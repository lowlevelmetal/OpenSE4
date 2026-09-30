// Computer player: exploration and colonization.

#include "game/ai_planner.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <tuple>

namespace opense4::game::ai::detail {

namespace {

Order moveTo(Location where) {
    Order o;
    o.kind = OrderKind::MoveTo;
    o.location = where;
    return o;
}

Order warpThrough(const GameState& s, ObjectId wp) {
    Order o;
    o.kind = OrderKind::Warp;
    o.object = wp;
    o.location = locationOf(s.galaxy, wp);
    return o;
}

bool warEnemyColonyIn(const Planner& p, SystemId sys) {
    for (ObjectId o : p.st.galaxy.system(sys).objects)
        if (const Colony* c = p.st.colony(o); c && p.atWarWith(c->owner)) return true;
    return false;
}

bool canColonizeSurface(const DesignStats& st, std::string_view surface) {
    return surface == "Rock" ? st.canColonizeRock : surface == "Ice" ? st.canColonizeIce : st.canColonizeGas;
}

} // namespace

namespace {

// Planets our colony ships are already headed for.
std::set<ObjectId> claimedPlanets(const Planner& p) {
    std::set<ObjectId> out = p.reservedPlanets;
    for (const Vehicle& v : p.st.vehicles) {
        if (v.owner != p.id) continue;
        for (const Order& o : v.orders)
            if (o.kind == OrderKind::Colonize) out.insert(o.object);
    }
    return out;
}

} // namespace

bool planetClaimed(Planner& p, ObjectId planet) { return claimedPlanets(p).contains(planet); }

std::vector<ColonyTarget> colonyTargets(Planner& p, std::string_view surface, SystemId from) {
    std::vector<ColonyTarget> out;
    if (!from.valid()) return out;
    const std::vector<int> jumps = p.jumpsFrom(from);
    const std::set<ObjectId> claimed = claimedPlanets(p);
    for (size_t i = 0; i < p.st.galaxy.systems.size(); ++i) {
        const SystemId sys{i};
        if (!p.explored(sys) || jumps[i] < 0 || !p.mayEnter(sys)) continue;
        if (p.threat[i] > 0 || warEnemyColonyIn(p, sys)) continue;
        for (ObjectId o : p.st.galaxy.system(sys).objects) {
            const SpaceObject& obj = p.st.galaxy.object(o);
            if (obj.kind != ObjectKind::Planet || surfaceKey(obj.surface) != surface) continue;
            if (!colonizable(p.r, p.st, p.emp(), obj) || claimed.contains(o)) continue;
            out.push_back({o, colonyTargetValue(p.r, p.st, p.emp(), obj) - int64_t{jumps[i]} * 150, jumps[i]});
        }
    }
    std::sort(out.begin(), out.end(), [](const ColonyTarget& a, const ColonyTarget& b) {
        return a.value != b.value ? a.value > b.value : a.planet < b.planet;
    });
    return out;
}

std::vector<ObjectId> explorationFrontier(Planner& p) {
    std::vector<ObjectId> out;
    if (p.neutral) return out;
    const std::vector<int>& jumps = p.jumpsFromHome();
    for (size_t i = 0; i < p.st.galaxy.systems.size(); ++i) {
        if (!p.explored(SystemId{i}) || jumps[i] < 0) continue;
        for (const Link& l : p.links[i])
            if (!p.explored(l.to)) out.push_back(l.warpPoint);
    }
    return out;
}

void planExploration(Planner& p) {
    if (p.neutral) return;
    const std::vector<ObjectId> frontier = explorationFrontier(p);
    bool unexplored = false;
    for (size_t i = 0; i < p.st.galaxy.systems.size() && !unexplored; ++i) unexplored = !p.explored(SystemId{i});

    std::set<ObjectId> claimed;
    std::vector<VehicleId> scouts;
    for (VehicleId id : p.ownVehicles()) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || v->status != VehicleStatus::Normal || p.busy.contains(id)) continue;
        const DesignInfo& di = p.info(v->design);
        if (di.role != Role::Scout || di.stats.movement <= 0) continue;
        if (!v->orders.empty()) {
            const Order& last = v->orders.back();
            const bool stillUseful = (last.kind == OrderKind::Warp && std::find(frontier.begin(), frontier.end(), last.object) != frontier.end()) ||
                                     (last.kind == OrderKind::Explore && unexplored) || last.kind == OrderKind::Resupply ||
                                     last.kind == OrderKind::Repair;
            if (stillUseful) {
                if (last.kind == OrderKind::Warp) claimed.insert(last.object);
                p.busy.insert(id);
                continue;
            }
        }
        scouts.push_back(id);
    }

    for (VehicleId id : scouts) {
        const Vehicle* v = p.st.vehicle(id);
        const Location at = v->location;
        const std::vector<int> jumps = p.jumpsFrom(at.system);
        std::optional<ObjectId> best;
        std::tuple<int, int, uint32_t> bestKey{};
        for (ObjectId wp : frontier) {
            if (claimed.contains(wp)) continue;
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
        std::vector<Order> orders;
        if (best) {
            claimed.insert(*best);
            const SpaceObject& obj = p.st.galaxy.object(*best);
            if (obj.system != at.system) orders.push_back(moveTo(locationOf(p.st.galaxy, *best)));
            orders.push_back(warpThrough(p.st, *best));
        } else if (unexplored && !frontier.empty()) {
            Order o;
            o.kind = OrderKind::Explore;
            orders.push_back(o);
        } else if (at.system != p.home && p.home.valid()) {
            orders.push_back(moveTo(p.homeLocation));
        }
        if (!orders.empty()) p.setOrders(id, std::move(orders));
    }
}

void planColonization(Planner& p) {
    for (VehicleId id : p.ownVehicles()) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || p.busy.contains(id) || v->status == VehicleStatus::Mothballed) continue;
        const DesignInfo& di = p.info(v->design);
        if (di.role != Role::Colonizer) continue;
        if (!v->orders.empty() && v->orders.back().kind == OrderKind::Colonize) {
            const ObjectId target = v->orders.back().object;
            if (target.valid() && target.index() < p.st.galaxy.objects.size() &&
                colonizable(p.r, p.st, p.emp(), p.st.galaxy.object(target)) && p.mayEnter(p.st.galaxy.object(target).system)) {
                p.busy.insert(id);
                continue;
            }
        }
        if (!v->orders.empty() && (v->orders.back().kind == OrderKind::Resupply || v->orders.back().kind == OrderKind::Repair)) continue;
        std::optional<ColonyTarget> best;
        for (std::string_view surface : {"Rock", "Ice", "Gas"}) {
            if (!canColonizeSurface(di.stats, surface)) continue;
            // A copy of the planet list is rebuilt per ship: orders given to
            // earlier ships this turn already claim their targets.
            const auto targets = colonyTargets(p, surface, v->location.system);
            if (!targets.empty() && (!best || targets.front().value > best->value)) best = targets.front();
        }
        if (!best) continue;
        Order o;
        o.kind = OrderKind::Colonize;
        o.object = best->planet;
        o.location = locationOf(p.st.galaxy, best->planet);
        if (p.setOrders(id, {o})) p.reservedPlanets.insert(best->planet);
    }
}

} // namespace opense4::game::ai::detail
