// The simultaneous movement phase (spec 05 §9.3, spec 03 §6-8).
//
// Each vehicle, or each fleet moving together, is an "actor" with an order
// list. A month has 30 days; an actor of speed S makes its k-th move on day
// ceil(k*30/S). Within a day actors act in a fixed order (faster first, then
// empire id, then vehicle id). Orders that need no movement run as soon as
// they reach the head of the list. After every day, each sector where an
// order was executed is offered to combat once.

#include "game/movement.hpp"

#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <climits>
#include <format>
#include <set>

namespace opense4::game::movement {

using namespace detail;

CombatHooks defaultCombatHooks() {
    return {[](const Rules& r, const GameState& s, Location where) { return combat::combatPossible(r, s, where); },
            [](TurnContext& ctx, Location where) { combat::resolveSpaceCombat(ctx, where); }};
}

namespace {

using ruleset::VehicleType;

enum class Exec {
    Done,       // completed without doing anything here: next order
    Acted,      // completed by acting here (counts as executing an order in this sector)
    Fail,       // impossible: the order is removed
    Moved,      // took a step; the order continues
    MovedDone,  // took a step that completed the order (warp jumps)
    Wait,       // nothing more today
    Stop,       // the order list was replaced (sentry alert)
};

enum class Travel { Arrived, Moved, Wait, Unreachable, Immobile };

bool validLocation(const GameState& s, Location l) {
    return l.system.valid() && l.system.index() < s.galaxy.systems.size() && l.sector.valid();
}

// A visible vehicle of an empire hostile to `e` in the system.
bool enemyInSystem(const Rules& r, const GameState& s, EmpireId e, SystemId sys) {
    std::optional<sight::SightVector> sensors;  // computed once, when a candidate shows up
    for (const Vehicle& v : s.vehicles) {
        if (!alive(v) || v.location.system != sys || v.owner == e || !hostile(s, e, v.owner)) continue;
        if (!sensors) sensors = sight::sensorLevels(r, s, e, sys);
        if (sight::detects(*sensors, sight::obscuration(r, s, v))) return true;
    }
    return false;
}

struct Actor {
    std::vector<VehicleId> members;  // in fleet member order
    FleetId fleet;                   // valid: the members execute the fleet's orders together
    EmpireId owner;
    VehicleId lead;
    int used = 0;                    // moves made this turn
    int initialSpeed = 0;
    bool stopped = false;            // intercepted, or everyone is gone
    bool pinned = false;             // a fleet member's own orders: in-place actions only
    // Route cache.
    bool routeValid = false;
    Location routeGoal{}, routeAt{};
    std::vector<Location> route;
    size_t routePos = 0;
};

class Mover {
public:
    Mover(TurnContext& ctx, const CombatHooks& hooks) : ctx_(ctx), r_(ctx.rules), s_(ctx.state), hooks_(hooks) {}

    void run() {
        buildActors();
        for (int day = 1; day <= kDaysPerTurn; ++day) {
            for (Actor& a : actors_) process(a, day);
            resolveCombat();
        }
        hazards();
        resolveCombat();
    }

private:
    // ---- Actors ----------------------------------------------------------------------------------

    void buildActors() {
        std::set<VehicleId> assigned;
        for (const Fleet& f : s_.fleets) {
            if (f.orders.empty()) continue;
            const Vehicle* lead = fleetLeader(s_, f);
            if (!lead) continue;
            Actor a;
            a.fleet = f.id;
            a.owner = f.owner;
            a.lead = lead->id;
            for (VehicleId id : f.members)
                if (const Vehicle* v = s_.vehicle(id); v && followsFleetOrders(s_, *v) && alive(*v)) {
                    a.members.push_back(id);
                    assigned.insert(id);
                }
            if (std::find(a.members.begin(), a.members.end(), a.lead) == a.members.end()) continue;
            actors_.push_back(std::move(a));
        }
        for (const Vehicle& v : s_.vehicles) {
            if (!alive(v)) continue;
            if (v.orders.empty() && !autoDrone(v)) continue;
            Actor a;
            a.members = {v.id};
            a.owner = v.owner;
            a.lead = v.id;
            // A member moving with its fleet still carries out its own orders that act
            // where it stands: self-destruct, launches, cloaking... (inferred)
            a.pinned = assigned.contains(v.id);
            actors_.push_back(std::move(a));
        }
        for (Actor& a : actors_) a.initialSpeed = speed(a);
        // Faster first, then empire, then vehicle id (spec 05 §9.3 tie-break).
        std::stable_sort(actors_.begin(), actors_.end(), [](const Actor& x, const Actor& y) {
            if (x.initialSpeed != y.initialSpeed) return x.initialSpeed > y.initialSpeed;
            if (x.owner != y.owner) return x.owner < y.owner;
            return x.lead < y.lead;
        });
    }

    bool autoDrone(const Vehicle& v) const {
        return vehicleType(r_, s_, v) == VehicleType::Drone && (v.targetVehicle.valid() || v.targetObject.valid());
    }

    void prune(Actor& a) {
        std::erase_if(a.members, [&](VehicleId id) {
            const Vehicle* v = s_.vehicle(id);
            return !v || !alive(*v);
        });
        if (a.members.empty()) {
            a.stopped = true;
            return;
        }
        if (std::find(a.members.begin(), a.members.end(), a.lead) == a.members.end()) a.lead = a.members.front();
    }

    Location where(const Actor& a) const { return s_.vehicle(a.lead)->location; }

    std::string name(const Actor& a) const {
        if (a.fleet.valid())
            if (const Fleet* f = s_.fleet(a.fleet)) return f->name;
        return s_.vehicle(a.lead)->name;
    }

    // Speed this turn: the slowest member's moves made plus moves left.
    int speed(const Actor& a) const {
        int sp = INT_MAX;
        for (VehicleId id : a.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v)) sp = std::min(sp, a.used + v->movement);
        return sp == INT_MAX ? 0 : sp;
    }
    int remaining(const Actor& a) const {
        int left = INT_MAX;
        for (VehicleId id : a.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v)) left = std::min(left, v->movement);
        return left == INT_MAX ? 0 : left;
    }
    bool canStep(const Actor& a, int day) const { return remaining(a) > 0 && movesByDay(speed(a), day) > a.used; }

    bool any(const Actor& a, auto&& pred) const {
        for (VehicleId id : a.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v) && pred(*v)) return true;
        return false;
    }
    bool held(const Actor& a) const {
        return any(a, [&](const Vehicle& v) { return heldInPlace(s_, v); });
    }
    bool hasFighter(const Actor& a) const {
        return any(a, [&](const Vehicle& v) { return vehicleType(r_, s_, v) == VehicleType::Fighter; });
    }

    std::vector<Order>* orders(const Actor& a) {
        if (a.fleet.valid()) {
            Fleet* f = s_.fleet(a.fleet);
            return f ? &f->orders : nullptr;
        }
        Vehicle* v = s_.vehicle(a.lead);
        return v ? &v->orders : nullptr;
    }
    bool repeat(const Actor& a) const {
        if (a.fleet.valid()) {
            const Fleet* f = s_.fleet(a.fleet);
            return f && f->repeatOrders;
        }
        const Vehicle* v = s_.vehicle(a.lead);
        return v && v->repeatOrders;
    }

    // ---- The order loop ----------------------------------------------------------------------------

    void process(Actor& a, int day) {
        if (a.stopped) return;
        prune(a);
        if (a.stopped) return;
        int idle = 0;
        for (int guard = 0; guard < 4096 && !a.stopped; ++guard) {
            std::vector<Order>* list = orders(a);
            if (!list) return;
            if (list->empty()) {
                if (a.members.size() == 1 && autoDrone(*s_.vehicle(a.lead))) droneStep(a, day);
                return;
            }
            const size_t size = list->size();
            Order o = list->front();
            if (a.pinned && !inPlace(a, o)) return;  // waits until the member acts alone
            const Exec e = execute(a, o, day);
            prune(a);
            if (a.stopped) return;
            switch (e) {
                case Exec::Moved:
                    idle = 0;
                    writeBack(a, o);
                    continue;
                case Exec::MovedDone:
                    idle = 0;
                    complete(a);
                    continue;
                case Exec::Acted:
                    touched_.push_back(where(a));
                    complete(a);
                    break;
                case Exec::Done: complete(a); break;
                case Exec::Fail: removeFront(a); break;
                case Exec::Wait: writeBack(a, o); return;
                case Exec::Stop: return;
            }
            // A repeating list whose orders all complete without moving idles (spec 03 §8).
            if (++idle > static_cast<int>(size) + 1) return;
        }
    }

    // Orders that need no travel from where the vehicle is now.
    bool inPlace(const Actor& a, const Order& o) const {
        const Location here = where(a);
        switch (o.kind) {
            case OrderKind::UseComponent:
            case OrderKind::Cloak:
            case OrderKind::Decloak:
            case OrderKind::SweepMines: return true;
            case OrderKind::LoadCargo:
            case OrderKind::DropCargo:
            case OrderKind::LaunchUnits:
            case OrderKind::RecoverUnits: return !o.location.system.valid() || o.location == here;
            case OrderKind::StellarManipulation: return stellarTarget(s_, o, here) == here;
            default: return false;
        }
    }

    void writeBack(const Actor& a, const Order& o) {
        if (std::vector<Order>* list = orders(a); list && !list->empty()) list->front() = o;
    }

    static Order fresh(Order o) {
        // Choices made while executing are made again on the next pass of a repeating list.
        switch (o.kind) {
            case OrderKind::Explore: o.object = {}; o.location = {}; break;
            case OrderKind::Resupply:
            case OrderKind::Repair: o.location = {}; break;
            case OrderKind::Colonize: o.amount = 0; break;
            default: break;
        }
        return o;
    }

    void complete(Actor& a) {
        a.routeValid = false;
        std::vector<Order>* list = orders(a);
        if (!list || list->empty()) return;
        const Order done = list->front();
        list->erase(list->begin());
        if (repeat(a)) list->push_back(fresh(done));
    }

    void removeFront(Actor& a) {
        a.routeValid = false;
        if (std::vector<Order>* list = orders(a); list && !list->empty()) list->erase(list->begin());
    }

    Exec fail(const Actor& a, const Order& o, std::string_view why) {
        ctx_.log(a.owner, LogCategory::Misc, std::format("{}: {} order cancelled", name(a), displayName(o.kind)), std::string(why), where(a));
        return Exec::Fail;
    }

    Exec afterTravel(const Actor& a, const Order& o, Travel t) {
        switch (t) {
            case Travel::Moved: return Exec::Moved;
            case Travel::Wait: return Exec::Wait;
            case Travel::Unreachable: return fail(a, o, "No known route to the destination.");
            case Travel::Immobile: return fail(a, o, "It cannot move.");
            case Travel::Arrived: break;
        }
        return Exec::Done;
    }

    // ---- Moving ------------------------------------------------------------------------------------

    bool ensureRoute(Actor& a, Location goal) {
        const Location here = where(a);
        if (a.routeValid && a.routeGoal == goal && a.routeAt == here && a.routePos < a.route.size()) return true;
        RouteOptions options;
        options.allowWarp = !hasFighter(a);  // fighters cannot use warp points
        const Location goals[] = {goal};
        auto p = findPathToNearest(r_, s_, a.owner, here, goals, options);
        a.routeValid = p && !p->path.steps.empty();
        if (!a.routeValid) return false;
        a.route = std::move(p->path.steps);
        a.routePos = 0;
        a.routeGoal = goal;
        a.routeAt = here;
        return true;
    }

    Travel travel(Actor& a, Location goal, int day) {
        if (!validLocation(s_, goal)) return Travel::Unreachable;
        if (where(a) == goal) return Travel::Arrived;
        if (speed(a) <= 0) return held(a) ? Travel::Wait : Travel::Immobile;
        if (!ensureRoute(a, goal)) return Travel::Unreachable;
        if (!canStep(a, day)) return Travel::Wait;
        if (!step(a) && (!ensureRoute(a, goal) || !step(a))) return Travel::Unreachable;
        return Travel::Moved;
    }

    bool step(Actor& a) {
        const Location here = where(a);
        const Location next = a.route[a.routePos];
        ObjectId via;
        if (next.system != here.system) {
            for (ObjectId w : s_.galaxy.system(here.system).objects) {
                const SpaceObject& wp = s_.galaxy.object(w);
                if (wp.kind == ObjectKind::WarpPoint && wp.sector == here.sector && wp.destination.valid() &&
                    inSystem(s_.galaxy, wp.destination) && locationOf(s_.galaxy, wp.destination) == next) {
                    via = w;
                    break;
                }
            }
            if (!via.valid()) {
                a.routeValid = false;  // the link is gone
                return false;
            }
        }
        ++a.routePos;
        a.routeAt = next;
        moveMembers(a, next, via);
        return true;
    }

    // An explicit jump through a warp point, known link or not (Warp, Explore).
    Travel jump(Actor& a, ObjectId w, int day) {
        const SpaceObject& wp = s_.galaxy.object(w);
        if (!wp.destination.valid() || !inSystem(s_.galaxy, wp.destination)) return Travel::Unreachable;
        if (speed(a) <= 0) return held(a) ? Travel::Wait : Travel::Immobile;
        if (!canStep(a, day)) return Travel::Wait;
        a.routeValid = false;
        moveMembers(a, locationOf(s_.galaxy, wp.destination), w);
        return Travel::Moved;
    }

    void moveMembers(Actor& a, Location next, ObjectId via) {
        for (VehicleId id : a.members) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            v->location = next;
            v->movement = std::max(0, v->movement - 1);
            spendSupply(r_, s_, *v, moveSupplyCost(r_, s_, *v));
        }
        ++a.used;
        if (via.valid()) {
            knowledgeChanged(a.owner);
            // Turbulence hits objects leaving through the warp point (inferred: departures only).
            const int64_t turbulence = rawBest(s_.galaxy.object(via).abilities, AbilityKind::WarpPointTurbulence);
            for (VehicleId id : a.members) {
                sight::learnWarpLink(s_, a.owner, via);
                if (turbulence > 0) hurt(ctx_, id, static_cast<int>(turbulence), "Damaged by warp point turbulence.");
            }
            prune(a);
            if (a.stopped) return;
        }
        arrive(a);
    }

    void arrive(Actor& a) {
        const Location here = where(a);
        sight::markExplored(s_, a.owner, here.system);
        Knowledge& k = s_.empire(a.owner).knowledge;
        if (here.system.index() < k.present.size()) k.present[here.system.index()] = 1;
        for (VehicleId id : a.members) {
            Vehicle* v = s_.vehicle(id);
            if (v && alive(*v) && resupplyDepotAt(r_, s_, v->owner, here)) refillSupply(r_, s_, *v);  // passing a depot refuels
        }
        if (a.fleet.valid()) poolSupply(r_, s_, a.members);
        for (VehicleId id : a.members) capMovement(a, id);
        touched_.push_back(here);
    }

    // Engines lost or supply exhausted mid-turn: the remaining movement is
    // capped at the new maximum (spec 03 §6.1, inferred).
    void capMovement(const Actor& a, VehicleId id) {
        Vehicle* v = s_.vehicle(id);
        if (!v || !alive(*v)) return;
        const int max = turnMovement(r_, s_, *v) + (heldInPlace(s_, *v) ? 0 : bonus_[id]);
        v->movement = std::min(v->movement, std::max(0, max - a.used));
    }

    void droneStep(Actor& a, int day) {
        Vehicle* d = s_.vehicle(a.lead);
        std::optional<Location> goal;
        if (const Vehicle* t = s_.vehicle(d->targetVehicle); t && alive(*t) && sight::canSeeVehicle(r_, s_, d->owner, *t)) goal = t->location;
        else if (d->targetObject.valid() && inSystem(s_.galaxy, d->targetObject)) goal = locationOf(s_.galaxy, d->targetObject);
        if (!goal) {
            d->targetVehicle = {};  // the target is lost; the drone waits for a new one (inferred)
            d->targetObject = {};
            return;
        }
        while (!a.stopped && travel(a, *goal, day) == Travel::Moved) {
        }
    }

    // ---- Orders ---------------------------------------------------------------------------------------

    Exec execute(Actor& a, Order& o, int day) {
        switch (o.kind) {
            case OrderKind::MoveTo: return afterTravel(a, o, travel(a, o.location, day));
            case OrderKind::MoveToWaypoint: {
                const Empire& e = s_.empire(a.owner);
                if (o.amount < 0 || static_cast<size_t>(o.amount) >= e.waypoints.size() || !e.waypoints[static_cast<size_t>(o.amount)].set)
                    return fail(a, o, "The waypoint is not set.");
                return afterTravel(a, o, travel(a, e.waypoints[static_cast<size_t>(o.amount)].location, day));
            }
            case OrderKind::Warp: return warp(a, o, day);
            case OrderKind::Attack: return attack(a, o, day);
            case OrderKind::Resupply: return resupply(a, o, day);
            case OrderKind::Repair: return repair(a, o, day);
            case OrderKind::Explore: return explore(a, o, day);
            case OrderKind::Colonize: return colonize(a, o, day);
            case OrderKind::Sentry:
                if (enemyInSystem(r_, s_, a.owner, where(a).system)) {
                    if (std::vector<Order>* list = orders(a)) list->clear();
                    ctx_.log(a.owner, LogCategory::Combat, std::format("{}: enemy sighted", name(a)), "Sentry duty ended.", where(a));
                    return Exec::Stop;
                }
                return Exec::Wait;
            case OrderKind::LoadCargo:
            case OrderKind::DropCargo:
            case OrderKind::LaunchUnits:
            case OrderKind::RecoverUnits: return cargo(a, o, day);
            case OrderKind::Cloak:
            case OrderKind::Decloak: return cloak(a, o);
            case OrderKind::SweepMines: {
                if (!any(a, [&](const Vehicle& v) { return hasAbility(vehicleAbilities(r_, s_, v), AbilityKind::MineSweeping); }))
                    return fail(a, o, "No working mine sweeper.");
                for (VehicleId id : std::vector<VehicleId>(a.members)) sweepMines(ctx_, id);
                return Exec::Acted;
            }
            case OrderKind::UseComponent: {
                int gained = -1;
                for (VehicleId id : std::vector<VehicleId>(a.members)) {
                    const int g = useComponent(ctx_, id, o.amount);
                    if (g < 0) continue;
                    gained = std::max(gained, g);
                    if (g > 0 && !heldInPlace(s_, *s_.vehicle(id))) {
                        s_.vehicle(id)->movement += g;
                        bonus_[id] += g;
                    }
                }
                return gained < 0 ? fail(a, o, "No usable component.") : Exec::Acted;
            }
            case OrderKind::StellarManipulation: {
                const auto target = stellarTarget(s_, o, where(a));
                if (!target) return fail(a, o, "The target no longer exists.");
                const Travel t = travel(a, *target, day);
                if (t != Travel::Arrived) return afterTravel(a, o, t);
                bool consumed = false;
                const std::string why = stellarManipulation(ctx_, a.members, o, consumed);
                if (!why.empty()) return fail(a, o, why);
                for (Actor& other : actors_) other.routeValid = false;  // the map may have changed
                mapChanged();
                prune(a);
                return a.stopped ? Exec::Stop : Exec::Acted;
            }
            case OrderKind::Count: break;
        }
        return fail(a, o, "Unknown order.");
    }

    Exec warp(Actor& a, Order& o, int day) {
        if (hasFighter(a)) return fail(a, o, "Fighters cannot use warp points.");
        if (!o.object.valid() || o.object.index() >= s_.galaxy.objects.size() || s_.galaxy.object(o.object).kind != ObjectKind::WarpPoint ||
            !inSystem(s_.galaxy, o.object))
            return fail(a, o, "The warp point no longer exists.");
        const SpaceObject& wp = s_.galaxy.object(o.object);
        if (!wp.destination.valid() || !inSystem(s_.galaxy, wp.destination)) return fail(a, o, "The warp point leads nowhere.");
        if (!wp.oneWay && s_.galaxy.object(wp.destination).oneWay) return fail(a, o, "That warp point cannot be entered from this side.");
        const Travel t = travel(a, locationOf(s_.galaxy, o.object), day);
        if (t != Travel::Arrived) return afterTravel(a, o, t);
        const Travel j = jump(a, o.object, day);
        return j == Travel::Moved ? Exec::MovedDone : afterTravel(a, o, j);
    }

    Exec attack(Actor& a, Order& o, int day) {
        if (any(a, [](const Vehicle& v) { return v.status == VehicleStatus::Cloaked; }))
            return fail(a, o, "A cloaked ship must decloak before it can attack.");  // (inferred) the order is dropped
        Location goal;
        if (o.vehicle.valid()) {
            const Vehicle* t = s_.vehicle(o.vehicle);
            if (!t || !alive(*t) || !sight::canSeeVehicle(r_, s_, a.owner, *t)) {
                ctx_.log(a.owner, LogCategory::Combat, std::format("{}: target lost", name(a)), {}, where(a));
                return Exec::Done;
            }
            goal = t->location;  // pursuit: the route is recomputed whenever the target moves
        } else if (o.object.valid()) {
            if (o.object.index() >= s_.galaxy.objects.size() || !inSystem(s_.galaxy, o.object)) return fail(a, o, "The target no longer exists.");
            goal = locationOf(s_.galaxy, o.object);
        } else {
            return fail(a, o, "No target.");
        }
        const Travel t = travel(a, goal, day);
        return t == Travel::Arrived ? Exec::Acted : afterTravel(a, o, t);
    }

    // Picks the nearest of `goals` and stores it in the order.
    bool chooseNearest(Actor& a, Order& o, const std::vector<Location>& goals) {
        if (goals.empty()) return false;
        RouteOptions options;
        options.allowWarp = !hasFighter(a);
        const auto p = findPathToNearest(r_, s_, a.owner, where(a), goals, options);
        if (!p) return false;
        o.location = goals[p->goal];
        return true;
    }

    Exec resupply(Actor& a, Order& o, int day) {
        if (!validLocation(s_, o.location) || !resupplyDepotAt(r_, s_, a.owner, o.location)) {
            std::vector<Location> depots;
            for (const auto& c : s_.colonies)
                if (c && inSystem(s_.galaxy, c->planet) && resupplyDepotAt(r_, s_, a.owner, locationOf(s_.galaxy, c->planet)))
                    depots.push_back(locationOf(s_.galaxy, c->planet));
            std::sort(depots.begin(), depots.end());
            depots.erase(std::unique(depots.begin(), depots.end()), depots.end());
            if (!chooseNearest(a, o, depots)) return fail(a, o, "No reachable resupply depot.");
        }
        const Travel t = travel(a, o.location, day);
        if (t != Travel::Arrived) return afterTravel(a, o, t);
        for (VehicleId id : a.members) refillSupply(r_, s_, *s_.vehicle(id));
        return Exec::Acted;
    }

    Exec repair(Actor& a, Order& o, int day) {
        if (repairCapacityAt(r_, s_, a.owner, where(a)) > 0) return Exec::Acted;
        if (!validLocation(s_, o.location) || repairCapacityAt(r_, s_, a.owner, o.location) <= 0) {
            std::vector<Location> yards;
            for (const auto& c : s_.colonies)
                if (c && c->owner == a.owner && inSystem(s_.galaxy, c->planet) &&
                    repairCapacityAt(r_, s_, a.owner, locationOf(s_.galaxy, c->planet)) > 0)
                    yards.push_back(locationOf(s_.galaxy, c->planet));
            for (const Vehicle& v : s_.vehicles)
                if (alive(v) && v.owner == a.owner && std::find(a.members.begin(), a.members.end(), v.id) == a.members.end() &&
                    hasAbility(vehicleAbilities(r_, s_, v), AbilityKind::ComponentRepair))
                    yards.push_back(v.location);
            std::sort(yards.begin(), yards.end());
            yards.erase(std::unique(yards.begin(), yards.end()), yards.end());
            if (!chooseNearest(a, o, yards)) return fail(a, o, "No reachable repair facility.");
        }
        const Travel t = travel(a, o.location, day);
        return t == Travel::Arrived ? Exec::Acted : afterTravel(a, o, t);
    }

    // A warp point worth exploring: in a known system, leading somewhere unexplored or unknown.
    bool explorable(EmpireId e, ObjectId w) const {
        if (!w.valid() || w.index() >= s_.galaxy.objects.size()) return false;
        const SpaceObject& wp = s_.galaxy.object(w);
        if (wp.kind != ObjectKind::WarpPoint || !inSystem(s_.galaxy, w) || !wp.destination.valid() || !inSystem(s_.galaxy, wp.destination))
            return false;
        const SpaceObject& far = s_.galaxy.object(wp.destination);
        if (!wp.oneWay && far.oneWay) return false;
        const Empire& emp = s_.empire(e);
        if (!s_.options.omnipresent && !emp.hasExplored(wp.system)) return false;
        return !sight::knowsWarpLink(s_, e, w) || !emp.hasExplored(far.system);
    }

    // Explore targets of one empire: its explorable warp points and what its
    // ships already head for. Rebuilt when the empire's map knowledge changes.
    struct ExploreBoard {
        uint64_t version = UINT64_MAX;
        std::vector<ObjectId> candidates;
        std::set<ObjectId> claimedWarps;
        std::set<SystemId> claimedSystems;
    };

    void claim(ExploreBoard& b, EmpireId e, ObjectId w) {
        b.claimedWarps.insert(w);
        const ObjectId far = s_.galaxy.object(w).destination;
        if (far.valid() && sight::knowsWarpLink(s_, e, w)) b.claimedSystems.insert(s_.galaxy.object(far).system);
    }

    ExploreBoard& board(EmpireId e) {
        ExploreBoard& b = boards_[e];
        if (b.version == version_[e]) return b;
        b = ExploreBoard{};
        b.version = version_[e];
        auto head = [&](const std::vector<Order>& list) {
            if (list.empty()) return;
            const Order& h = list.front();
            if ((h.kind == OrderKind::Explore || h.kind == OrderKind::Warp) && h.object.valid() && h.object.index() < s_.galaxy.objects.size())
                claim(b, e, h.object);
            if (h.kind == OrderKind::MoveTo && h.location.system.valid()) b.claimedSystems.insert(h.location.system);
        };
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.owner == e && !followsFleetOrders(s_, v)) head(v.orders);
        for (const Fleet& f : s_.fleets)
            if (f.owner == e) head(f.orders);
        for (const StarSystem& sys : s_.galaxy.systems)
            for (ObjectId w : sys.objects)
                if (explorable(e, w)) b.candidates.push_back(w);
        return b;
    }

    // The map changed for `e` (a jump), or for everyone (stellar manipulation).
    void knowledgeChanged(EmpireId e) { ++version_[e]; }
    void mapChanged() {
        for (const Empire& e : s_.empires) knowledgeChanged(e.id);
    }

    Exec explore(Actor& a, Order& o, int day) {
        if (hasFighter(a)) return fail(a, o, "Fighters cannot use warp points.");
        if (!explorable(a.owner, o.object)) {
            o.object = {};
            // Nothing was left the last time and nothing changed since: done at once.
            if (auto it = exploreIdle_.find(a.lead); it != exploreIdle_.end() && it->second == version_[a.owner]) return Exec::Done;
            ExploreBoard& b = board(a.owner);
            // Warp points and systems other own ships are already heading for are skipped.
            std::vector<ObjectId> candidates;
            std::vector<Location> goals;
            for (ObjectId w : b.candidates) {
                if (b.claimedWarps.contains(w) || !explorable(a.owner, w)) continue;
                const ObjectId far = s_.galaxy.object(w).destination;
                if (sight::knowsWarpLink(s_, a.owner, w) && b.claimedSystems.contains(s_.galaxy.object(far).system)) continue;
                candidates.push_back(w);
                goals.push_back(locationOf(s_.galaxy, w));
            }
            RouteOptions options;
            const auto p = goals.empty() ? std::nullopt : findPathToNearest(r_, s_, a.owner, where(a), goals, options);
            if (!p) {  // nothing left: the order completes (inferred)
                exploreIdle_[a.lead] = version_[a.owner];
                if (exploreLogged_.insert(a.lead).second)
                    ctx_.log(a.owner, LogCategory::Misc, std::format("{}: nothing left to explore", name(a)), {}, where(a));
                return Exec::Done;
            }
            // Several warp points can share a sector: the first of them at the chosen goal.
            o.object = candidates[p->goal];
            o.location = goals[p->goal];
            claim(b, a.owner, o.object);
        }
        const Travel t = travel(a, locationOf(s_.galaxy, o.object), day);
        if (t != Travel::Arrived) return afterTravel(a, o, t);
        const Travel j = jump(a, o.object, day);
        return j == Travel::Moved ? Exec::MovedDone : afterTravel(a, o, j);
    }

    Exec colonize(Actor& a, Order& o, int day) {
        VehicleId colonizer;
        std::string why;
        for (VehicleId id : a.members) {
            const std::string p = colonizeProblem(r_, s_, *s_.vehicle(id), o.object);
            if (p.empty()) {
                colonizer = id;
                break;
            }
            if (why.empty()) why = p;
        }
        if (!colonizer.valid()) return fail(a, o, why);
        if (o.amount == 0) {
            loadColonists(ctx_, colonizer);  // colonists come aboard where the order starts (spec 03 §8)
            o.amount = 1;
        }
        const Travel t = travel(a, locationOf(s_.galaxy, o.object), day);
        return t == Travel::Arrived ? Exec::Wait : afterTravel(a, o, t);  // colonized in turn phase 4
    }

    Exec cargo(Actor& a, Order& o, int day) {
        if (o.location.system.valid()) {
            const Travel t = travel(a, o.location, day);
            if (t != Travel::Arrived) return afterTravel(a, o, t);
        }
        int64_t moved = 0;
        for (VehicleId id : std::vector<VehicleId>(a.members)) {
            Order part = o;
            if (o.amount >= 0) {
                part.amount = static_cast<int>(o.amount - moved);
                if (part.amount <= 0) break;
            }
            switch (o.kind) {
                case OrderKind::LoadCargo: moved += loadCargo(ctx_, id, o.design, part.amount); break;
                case OrderKind::DropCargo: moved += dropCargo(ctx_, id, o.design, part.amount); break;
                case OrderKind::LaunchUnits: moved += launchUnits(ctx_, budget_, id, part); break;
                case OrderKind::RecoverUnits: moved += recoverUnits(ctx_, budget_, id, part); break;
                default: break;
            }
        }
        if (moved <= 0) return fail(a, o, "Nothing could be moved.");
        if (o.kind == OrderKind::LaunchUnits || o.kind == OrderKind::RecoverUnits)
            ctx_.log(a.owner, LogCategory::Misc,
                     std::format("{} {} {} {}", name(a), o.kind == OrderKind::LaunchUnits ? "launched" : "recovered", moved, s_.design(o.design).name),
                     {}, where(a));
        return Exec::Acted;
    }

    Exec cloak(Actor& a, Order& o) {
        bool changed = false;
        for (VehicleId id : a.members) {
            Vehicle* v = s_.vehicle(id);
            if (o.kind == OrderKind::Decloak) {
                if (v->status == VehicleStatus::Cloaked) v->status = VehicleStatus::Normal;
                changed = true;
                continue;
            }
            if (v->status != VehicleStatus::Normal) continue;
            const Design& d = s_.design(v->design);
            int64_t cost = 0;
            bool device = false;
            for (size_t i = 0; i < d.entries.size(); ++i)
                if (entryIntact(r_, s_, *v, i) && hasAbility(r_.componentAbilities(d.entries[i].component), AbilityKind::CloakLevel)) {
                    device = true;
                    cost += mounted(r_, d.entries[i]).supplyUsed;
                }
            if (!device) continue;
            v->status = VehicleStatus::Cloaked;
            v->queue.items.clear();  // cloaked ships cannot build (spec 01 §6.4)
            spendSupply(r_, s_, *v, scaledSupply(r_, s_, v->owner, cost));  // once, when cloaking (inferred)
            changed = true;
        }
        return changed ? Exec::Acted : fail(a, o, "No working cloaking device.");
    }

    // ---- Combat ---------------------------------------------------------------------------------------

    // Combat sweeps mines, lets them strike and fights the battle; it clears the
    // orders of every vehicle that fought and records it in GameState::combats.
    void resolveCombat() {
        std::vector<Location> sites = std::move(touched_);
        touched_.clear();
        std::sort(sites.begin(), sites.end());
        sites.erase(std::unique(sites.begin(), sites.end()), sites.end());
        if (!hooks_.possible || !hooks_.resolve) return;
        bool resolved = false;
        for (const Location& where : sites) {
            if (!hooks_.possible(r_, s_, where)) continue;
            const size_t records = s_.combats.size();
            hooks_.resolve(ctx_, where);
            afterBattle(where, records);
            resolved = true;
        }
        if (resolved) s_.removeDeadVehicles();
    }

    // The vehicles of the battles recorded here stop for the rest of the turn and
    // lose their orders (spec 03 §6.4, spec 04 §2; inferred for simultaneous
    // games, spec 03 §19 Q8). Mines alone record no battle: a ship that only met
    // mines keeps going.
    void afterBattle(Location where, size_t recordsBefore) {
        std::set<VehicleId> fought;
        for (size_t i = recordsBefore; i < s_.combats.size(); ++i)
            if (s_.combats[i].location == where)
                for (const CombatPiece& p : s_.combats[i].pieces)
                    if (p.vehicle.valid()) fought.insert(p.vehicle);
        if (fought.empty()) return;
        if (std::find(ctx_.battleSites.begin(), ctx_.battleSites.end(), where) == ctx_.battleSites.end()) ctx_.battleSites.push_back(where);
        for (VehicleId id : fought) {
            Vehicle* v = s_.vehicle(id);
            if (!v) continue;
            if (followsFleetOrders(s_, *v))
                if (Fleet* f = s_.fleet(v->fleet)) f->orders.clear();
            v->orders.clear();
        }
        for (Actor& a : actors_)
            if (std::any_of(a.members.begin(), a.members.end(), [&](VehicleId id) { return fought.contains(id); })) a.stopped = true;
    }

    // ---- Hazards (spec 01 §7) ---------------------------------------------------------------------------

    void hazards() {
        std::vector<VehicleId> ids;
        for (const Vehicle& v : s_.vehicles)
            if (alive(v)) ids.push_back(v.id);
        static constexpr int kDirs[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
        // Order (inferred): displacement, centre damage, then sector damage, in vehicle id order.
        // 1. Displacement: black-hole pull, then random drift (ships, fighters and drones).
        for (VehicleId id : ids) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v) || heldInPlace(s_, *v) || !isMobileType(vehicleType(r_, s_, *v))) continue;
            const StarSystem& sys = s_.galaxy.system(v->location.system);
            const int64_t pull = rawBest(sys.abilities, AbilityKind::SystemMovementTowardsCenter);
            const int64_t drift = rawBest(sys.abilities, AbilityKind::SystemMovementRandom);
            const Location start = v->location;
            Sector at = v->location.sector;
            for (int64_t k = 0; k < pull; ++k)
                at = Sector{at.x + (kSystemCenter > at.x) - (kSystemCenter < at.x), at.y + (kSystemCenter > at.y) - (kSystemCenter < at.y)};
            if (drift > 0) {
                const auto& d = kDirs[s_.rng.below(8)];
                for (int64_t k = 0; k < drift; ++k) {
                    const Sector next{at.x + d[0], at.y + d[1]};
                    if (next.valid()) at = next;  // the grid edge stops the drift (inferred)
                }
            }
            v->location.sector = at;
            if (v->location != start) touched_.push_back(v->location);
        }
        // 2. Damage at a destructive centre.
        for (VehicleId id : ids) {
            const Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v) || v->location.sector != Sector{kSystemCenter, kSystemCenter}) continue;
            const int64_t dmg = rawBest(s_.galaxy.system(v->location.system).abilities, AbilityKind::SystemDestructiveCenter);
            if (dmg > 0) hurt(ctx_, id, static_cast<int>(dmg), "Torn apart at the centre of the system.");
        }
        // 3. Sector damage from storms and system-wide effects (summed, inferred).
        for (VehicleId id : ids) {
            const Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            const StarSystem& sys = s_.galaxy.system(v->location.system);
            int64_t dmg = rawSum(sys.abilities, AbilityKind::SectorDamage) + rawSum(sys.abilities, AbilityKind::SystemDamage);
            for (ObjectId o : sys.objects)
                if (s_.galaxy.object(o).sector == v->location.sector) dmg += rawSum(s_.galaxy.object(o).abilities, AbilityKind::SectorDamage);
            if (dmg > 0) hurt(ctx_, id, static_cast<int>(dmg), "Damaged by a hazard in its sector.");
        }
    }

    TurnContext& ctx_;
    const Rules& r_;
    GameState& s_;
    const CombatHooks& hooks_;
    std::vector<Actor> actors_;
    std::vector<Location> touched_;       // sectors where an order was executed this phase
    std::map<VehicleId, int> bonus_;      // emergency energy gained this turn
    UnitBudget budget_;
    std::map<EmpireId, uint64_t> version_;       // bumped when an empire's map knowledge changes
    std::map<EmpireId, ExploreBoard> boards_;
    std::map<VehicleId, uint64_t> exploreIdle_;  // actor -> version at which nothing was left to explore
    std::set<VehicleId> exploreLogged_;
};

} // namespace

void startTurn(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    for (Vehicle& v : s.vehicles)
        if (alive(v)) v.movement = turnMovement(r, s, v);  // 0 while held by sabotage or an event
    // Sentry: an enemy in the system at the start of an order step clears the list (spec 03 §8).
    for (Vehicle& v : s.vehicles) {
        if (!alive(v) || v.orders.empty() || v.orders.front().kind != OrderKind::Sentry || followsFleetOrders(s, v)) continue;
        if (!enemyInSystem(r, s, v.owner, v.location.system)) continue;
        v.orders.clear();
        ctx.log(v.owner, LogCategory::Combat, std::format("{}: enemy sighted", v.name), "Sentry duty ended.", v.location);
    }
    for (Fleet& f : s.fleets) {
        if (f.orders.empty() || f.orders.front().kind != OrderKind::Sentry) continue;
        const Vehicle* lead = fleetLeader(s, f);
        if (!lead || !enemyInSystem(r, s, f.owner, lead->location.system)) continue;
        f.orders.clear();
        ctx.log(f.owner, LogCategory::Combat, std::format("{}: enemy sighted", f.name), "Sentry duty ended.", lead->location);
    }
}

void runMovementAndCombat(TurnContext& ctx) { runMovementAndCombat(ctx, defaultCombatHooks()); }

void runMovementAndCombat(TurnContext& ctx, const CombatHooks& hooks) {
    Mover mover(ctx, hooks);
    mover.run();
}

} // namespace opense4::game::movement
