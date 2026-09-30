#include "game/orders.hpp"

#include "game/abilities.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>

namespace opense4::game {

namespace {

using ruleset::VehicleType;

bool validLocation(const GameState& s, Location l) {
    return l.system.valid() && l.system.index() < s.galaxy.systems.size() && l.sector.valid();
}

bool validObject(const GameState& s, ObjectId o) { return o.valid() && o.index() < s.galaxy.objects.size() && movement::detail::inSystem(s.galaxy, o); }

bool carriesPopulation(const GameState& s, std::span<const VehicleId> members) {
    return std::any_of(members.begin(), members.end(), [&](VehicleId id) {
        const Vehicle* v = s.vehicle(id);
        return v && v->cargo.totalPopulation() > 0;
    });
}

// Fighters and neutral empires never use warp points (spec 03 §6.2, spec 01 §8).
movement::RouteOptions routeOptions(const Rules& r, const GameState& s, const OrderContext& ctx) {
    movement::RouteOptions options;
    const bool fighters = std::any_of(ctx.members.begin(), ctx.members.end(), [&](VehicleId id) {
        const Vehicle* v = s.vehicle(id);
        return v && vehicleType(r, s, *v) == VehicleType::Fighter;
    });
    const bool neutral = ctx.owner.valid() && ctx.owner.index() < s.empires.size() && s.empire(ctx.owner).kind == PlayerKind::Neutral;
    options.allowWarp = !fighters && !neutral;
    return options;
}

// The nearest of `goals` by travel from ctx.at, if any is reachable.
std::optional<size_t> nearest(const Rules& r, const GameState& s, const OrderContext& ctx, std::span<const Location> goals) {
    if (goals.empty() || !validLocation(s, ctx.at)) return std::nullopt;
    const auto p = movement::findPathToNearest(r, s, ctx.owner, ctx.at, goals, routeOptions(r, s, ctx));
    if (!p) return std::nullopt;
    return p->goal;
}

void sortUnique(std::vector<Location>& list) {
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
}

bool member(const OrderContext& ctx, VehicleId id) { return std::find(ctx.members.begin(), ctx.members.end(), id) != ctx.members.end(); }

// Move To `where` when the group is not there yet, then `o`.
void travelThen(const GameState& s, OrderContext& ctx, Location where, const Order& o, std::vector<Order>& out) {
    if (validLocation(s, where) && where != ctx.at) {
        Order move;
        move.kind = OrderKind::MoveTo;
        move.location = where;
        out.push_back(move);
        advanceOrderContext(s, ctx, move);
    }
    out.push_back(o);
    advanceOrderContext(s, ctx, o);
}

void moveTo(const GameState& s, OrderContext& ctx, Location where, std::vector<Order>& out) {
    if (!validLocation(s, where) || where == ctx.at) return;  // already there: nothing to add (inferred)
    Order move;
    move.kind = OrderKind::MoveTo;
    move.location = where;
    out.push_back(move);
    advanceOrderContext(s, ctx, move);
}

// ---- Explore (spec 03 §8, confirmed: binary) ----------------------------------------------------------

// The warp points other own ships and fleets are bound for (a Warp order
// anywhere in their lists), and those already in this list.
std::vector<ObjectId> boundFor(const GameState& s, const OrderContext& ctx, std::span<const Order> sofar) {
    std::vector<ObjectId> out;
    auto note = [&](std::span<const Order> list) {
        for (const Order& o : list)
            if (o.kind == OrderKind::Warp && o.object.valid()) out.push_back(o.object);
    };
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.owner == ctx.owner && !member(ctx, v.id)) note(v.orders);
    for (const Fleet& f : s.fleets) {
        if (f.owner != ctx.owner) continue;
        const bool ours = std::any_of(f.members.begin(), f.members.end(), [&](VehicleId m) { return member(ctx, m); });
        if (!ours) note(f.orders);
    }
    note(sofar);
    std::sort(out.begin(), out.end());
    return out;
}

// A warp point in a system the empire has explored, leading to one it has not.
bool explorable(const GameState& s, EmpireId e, ObjectId w) {
    if (!validObject(s, w)) return false;
    const SpaceObject& wp = s.galaxy.object(w);
    if (wp.kind != ObjectKind::WarpPoint || !validObject(s, wp.destination)) return false;
    const Empire& emp = s.empire(e);
    if (!s.options.omnipresent && !emp.hasExplored(wp.system)) return false;
    return !emp.hasExplored(s.galaxy.object(wp.destination).system);
}

void explore(const Rules& r, const GameState& s, OrderContext& ctx, std::vector<Order>& out) {
    if (!routeOptions(r, s, ctx).allowWarp) return;
    const std::vector<ObjectId> bound = boundFor(s, ctx, out);
    std::vector<ObjectId> candidates;
    std::vector<Location> goals;
    for (const StarSystem& sys : s.galaxy.systems)
        for (ObjectId w : sys.objects) {
            if (!explorable(s, ctx.owner, w) || std::binary_search(bound.begin(), bound.end(), w)) continue;
            candidates.push_back(w);
            goals.push_back(locationOf(s.galaxy, w));
        }
    const auto pick = nearest(r, s, ctx, goals);
    if (!pick) return;  // nothing left: nothing is added
    Order warp;
    warp.kind = OrderKind::Warp;
    warp.object = candidates[*pick];
    warp.location = goals[*pick];
    travelThen(s, ctx, goals[*pick], warp, out);
}

// ---- Resupply and Repair (spec 03 §8, confirmed: binary) -----------------------------------------------------

// A visible, armed, non-mothballed hostile vehicle in the sector.
bool armedHostileAt(const Rules& r, const GameState& s, EmpireId e, Location l) {
    for (const Vehicle& v : s.vehicles) {
        if (v.count <= 0 || v.location != l || v.owner == e || !hostile(s, e, v.owner) || v.status == VehicleStatus::Mothballed) continue;
        if (!computeDesignStats(r, nullptr, s.design(v.design)).armed()) continue;
        if (sight::canSeeVehicle(r, s, e, v)) return true;
    }
    return false;
}

void resupply(const Rules& r, const GameState& s, OrderContext& ctx, std::vector<Order>& out) {
    std::vector<Location> depots;
    const Empire& e = s.empire(ctx.owner);
    for (const auto& c : s.colonies) {
        if (!c || !movement::detail::inSystem(s.galaxy, c->planet)) continue;
        const Location l = locationOf(s.galaxy, c->planet);
        if (!s.options.omnipresent && !e.hasExplored(l.system)) continue;
        if (movement::resupplyDepotAt(r, s, ctx.owner, l) && !armedHostileAt(r, s, ctx.owner, l)) depots.push_back(l);
    }
    sortUnique(depots);
    if (const auto pick = nearest(r, s, ctx, depots)) moveTo(s, ctx, depots[*pick], out);
}

// Own sources only: populated colonies with Component Repair and bases first;
// repair ships only when no such source can be reached.
void repair(const Rules& r, const GameState& s, OrderContext& ctx, std::vector<Order>& out) {
    std::vector<Location> fixed, ships;
    for (const auto& c : s.colonies)
        if (c && c->owner == ctx.owner && movement::detail::inSystem(s.galaxy, c->planet) && c->totalPopulation() > 0 &&
            abilitySum(colonyAbilities(r, s, *c), AbilityKind::ComponentRepair) > 0)
            fixed.push_back(locationOf(s.galaxy, c->planet));
    for (const Vehicle& v : s.vehicles) {
        if (v.count <= 0 || v.owner != ctx.owner || member(ctx, v.id)) continue;
        const VehicleType t = vehicleType(r, s, v);
        if ((t != VehicleType::Ship && t != VehicleType::Base) || abilitySum(vehicleAbilities(r, s, v), AbilityKind::ComponentRepair) <= 0) continue;
        (t == VehicleType::Base ? fixed : ships).push_back(v.location);
    }
    sortUnique(fixed);
    sortUnique(ships);
    if (const auto pick = nearest(r, s, ctx, fixed)) moveTo(s, ctx, fixed[*pick], out);
    else if (const auto ship = nearest(r, s, ctx, ships)) moveTo(s, ctx, ships[*ship], out);
}

} // namespace

OrderContext orderContextOf(const GameState&, const Vehicle& v) {
    OrderContext ctx;
    ctx.owner = v.owner;
    ctx.members = {v.id};
    ctx.at = v.location;
    ctx.carriesPopulation = v.cargo.totalPopulation() > 0;
    return ctx;
}

OrderContext orderContextOf(const GameState& s, const Fleet& f) {
    OrderContext ctx;
    ctx.owner = f.owner;
    const Vehicle* lead = movement::detail::fleetLeader(s, f);
    if (!lead) return ctx;
    ctx.at = lead->location;
    for (VehicleId id : f.members)
        if (const Vehicle* v = s.vehicle(id); v && v->count > 0 && v->location == ctx.at) ctx.members.push_back(id);
    ctx.carriesPopulation = carriesPopulation(s, ctx.members);
    return ctx;
}

void advanceOrderContext(const GameState& s, OrderContext& ctx, const Order& o) {
    switch (o.kind) {
        case OrderKind::MoveTo:
            if (validLocation(s, o.location)) ctx.at = o.location;
            break;
        case OrderKind::MoveToWaypoint: {
            const Empire& e = s.empire(ctx.owner);
            if (o.amount >= 0 && static_cast<size_t>(o.amount) < e.waypoints.size() && e.waypoints[static_cast<size_t>(o.amount)].set)
                ctx.at = e.waypoints[static_cast<size_t>(o.amount)].location;
            break;
        }
        case OrderKind::Warp:
            if (validObject(s, o.object) && validObject(s, s.galaxy.object(o.object).destination))
                ctx.at = locationOf(s.galaxy, s.galaxy.object(o.object).destination);
            break;
        case OrderKind::Colonize:
            if (validObject(s, o.object)) ctx.at = locationOf(s.galaxy, o.object);
            ctx.carriesPopulation = false;  // the ship is consumed
            break;
        case OrderKind::Attack:
            if (const Vehicle* t = s.vehicle(o.vehicle)) ctx.at = t->location;
            else if (validObject(s, o.object)) ctx.at = locationOf(s.galaxy, o.object);
            break;
        case OrderKind::LoadCargo:
        case OrderKind::DropCargo:
        case OrderKind::LaunchUnits:
        case OrderKind::RecoverUnits:
            if (validLocation(s, o.location)) ctx.at = o.location;
            if (!o.design.valid() && o.kind == OrderKind::LoadCargo) ctx.carriesPopulation = true;
            if (!o.design.valid() && o.kind == OrderKind::DropCargo) ctx.carriesPopulation = false;
            break;
        case OrderKind::StellarManipulation:
            if (const auto t = movement::detail::stellarTarget(s, o, ctx.at)) ctx.at = *t;
            break;
        default: break;
    }
}

void expandOrder(const Rules& r, const GameState& s, OrderContext& ctx, const Order& o, std::vector<Order>& out) {
    if (!ctx.owner.valid() || ctx.owner.index() >= s.empires.size()) {
        out.push_back(o);
        return;
    }
    switch (o.kind) {
        case OrderKind::Explore: explore(r, s, ctx, out); return;
        case OrderKind::Resupply: resupply(r, s, ctx, out); return;
        case OrderKind::Repair: repair(r, s, ctx, out); return;
        case OrderKind::Warp:
            travelThen(s, ctx, validObject(s, o.object) ? locationOf(s.galaxy, o.object) : Location{}, o, out);
            return;
        case OrderKind::Colonize: {
            // Colonists come aboard where the order is given, when the ship carries none.
            if (!ctx.carriesPopulation) {
                Order load;
                load.kind = OrderKind::LoadCargo;
                load.location = ctx.at;
                load.amount = -1;
                out.push_back(load);
                advanceOrderContext(s, ctx, load);
            }
            Order colonize = o;
            colonize.amount = kColonizeExpanded;
            travelThen(s, ctx, validObject(s, o.object) ? locationOf(s.galaxy, o.object) : Location{}, colonize, out);
            return;
        }
        case OrderKind::LoadCargo:
        case OrderKind::DropCargo:
        case OrderKind::LaunchUnits:
        case OrderKind::RecoverUnits: travelThen(s, ctx, o.location, o, out); return;
        default:
            out.push_back(o);
            advanceOrderContext(s, ctx, o);
            return;
    }
}

std::vector<Order> expandGivenOrders(const Rules& r, const GameState& s, OrderContext ctx, std::span<const Order> current,
                                     std::span<const Order> given) {
    std::vector<Order> out;
    size_t kept = 0;
    while (kept < given.size() && kept < current.size() && given[kept] == current[kept]) ++kept;
    for (size_t i = 0; i < kept; ++i) {
        out.push_back(given[i]);
        advanceOrderContext(s, ctx, given[i]);
    }
    for (size_t i = kept; i < given.size(); ++i) expandOrder(r, s, ctx, given[i], out);
    return out;
}

} // namespace opense4::game
