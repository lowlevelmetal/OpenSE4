// The simultaneous movement phase (spec 03 §6.3, §6.4, §8; spec 01 §7).
//
// Each vehicle, each fleet moving together and each planet with orders is an
// "actor" with an order list. A month has 30 days. Every day each actor's day
// counter gains speed/30 (kept exactly, in thirtieths of a movement point);
// when it reaches 1 the actor acts once and the counter loses 1. An action is
// exactly one order execution: one step for a moving order, or the whole of an
// order that needs no movement. Within a day actors act in the order their
// objects were created. After every day, each sector where something acted is
// offered to combat, unless every object there already fought there this turn.

#include "game/movement.hpp"

#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

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

// The day counter of spec 03 §6.3 (see DayCounterMode).
class DayCounter {
public:
    // Before anyone acts, each day adds speed / 30.
    void newDay(int speed) {
        if constexpr (kDayCounterMode == DayCounterMode::Exact) exact_ += speed;
        else value_ = (value_ + xmath::Ext(speed) / xmath::Ext(kDaysPerTurn)).roundedTo(xmath::kDoubleBits);
    }
    // At 1 or more the vehicle acts, and 1 is taken off.
    bool take() {
        if constexpr (kDayCounterMode == DayCounterMode::Exact) {
            if (exact_ < kDaysPerTurn) return false;
            exact_ -= kDaysPerTurn;
        } else {
            if (value_ < xmath::Ext(1)) return false;
            value_ = (value_ - xmath::Ext(1)).roundedTo(xmath::kDoubleBits);
        }
        return true;
    }
    // Emergency energy adds whole actions (spec 03 §8).
    void add(int actions) {
        if constexpr (kDayCounterMode == DayCounterMode::Exact) exact_ += actions * kDaysPerTurn;
        else value_ = (value_ + xmath::Ext(actions)).roundedTo(xmath::kDoubleBits);
    }

private:
    int exact_ = 0;       // in thirtieths
    xmath::Ext value_;    // DayCounterMode::Double
};

template <DayCounterMode Mode>
std::vector<int> daysActed(int speed) {
    std::vector<int> days;
    int exact = 0;
    xmath::Ext value;
    for (int day = 1; day <= kDaysPerTurn; ++day) {
        if constexpr (Mode == DayCounterMode::Exact) {
            exact += speed;
            if (exact >= kDaysPerTurn) {
                exact -= kDaysPerTurn;
                days.push_back(day);
            }
        } else {
            value = (value + xmath::Ext(speed) / xmath::Ext(kDaysPerTurn)).roundedTo(xmath::kDoubleBits);
            if (value >= xmath::Ext(1)) {
                value = (value - xmath::Ext(1)).roundedTo(xmath::kDoubleBits);
                days.push_back(day);
            }
        }
    }
    return days;
}

enum class Exec {
    Done,       // completed without acting here: removed (kept at the end with Repeat)
    Acted,      // completed by acting here (the sector is offered to combat)
    ActedStay,  // acted here; the order stays (a pursuit that reached its target)
    Removed,    // this order alone is removed, even with Repeat (a Sentry that ends)
    Fail,       // failed: the whole list is cleared and Repeat switched off (§8)
    Moved,      // took a step; the order continues
    MovedDone,  // took a step that completed the order (warp jumps)
    Wait,       // nothing more on this action; the order stays
    Gone,       // the actor is gone
};

// Arrived: already there (no step). Reached: a step that arrived. The composite
// orders (Warp, Colonize, cargo...) are a Move To plus an action in the
// original (§8): their action comes on the next action after Reached.
enum class Travel { Arrived, Moved, Reached, Wait, Unreachable, Immobile, Busy, Stopped };

bool validLocation(const GameState& s, Location l) {
    return l.system.valid() && l.system.index() < s.galaxy.systems.size() && l.sector.valid();
}

// A visible vehicle, or a colony, of an empire `e` is hostile to in the system
// (spec 03 §8 Sentry: "present"; inferred: vehicles must be seen).
bool hostilePresentInSystem(const Rules& r, const GameState& s, EmpireId e, SystemId sys) {
    std::optional<sight::SightVector> sensors;  // computed once, when a candidate shows up
    for (const Vehicle& v : s.vehicles) {
        if (!alive(v) || v.location.system != sys || v.owner == e || !hostile(s, e, v.owner)) continue;
        if (!sensors) sensors = sight::sensorLevels(r, s, e, sys);
        if (sight::detects(*sensors, sight::obscuration(r, s, v))) return true;
    }
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && c->owner != e && hostile(s, e, c->owner)) return true;
    return false;
}

AbilityKind stellarAbility(StellarAction a) {
    switch (a) {
        case StellarAction::CreatePlanet: return AbilityKind::CreatePlanetSize;
        case StellarAction::DestroyPlanet: return AbilityKind::DestroyPlanetSize;
        case StellarAction::CreateStar: return AbilityKind::CreateStar;
        case StellarAction::DestroyStar: return AbilityKind::DestroyStar;
        case StellarAction::OpenWarpPoint: return AbilityKind::OpenWarpPointDistance;
        case StellarAction::CloseWarpPoint: return AbilityKind::CloseWarpPoint;
        case StellarAction::CreateStorm: return AbilityKind::CreateStorm;
        case StellarAction::DestroyStorm: return AbilityKind::DestroyStorm;
        case StellarAction::CreateNebulae: return AbilityKind::CreateNebulae;
        case StellarAction::DestroyNebulae: return AbilityKind::DestroyNebulae;
        case StellarAction::CreateBlackHole: return AbilityKind::CreateBlackHole;
        case StellarAction::DestroyBlackHole: return AbilityKind::DestroyBlackHole;
        case StellarAction::CreateConstructedPlanet: return AbilityKind::CreateConstructedPlanet;
        case StellarAction::Count: break;
    }
    return AbilityKind::Unknown;
}

struct Actor {
    std::vector<VehicleId> members;  // in fleet member order
    FleetId fleet;                   // valid: the members execute the fleet's orders together
    ObjectId planet;                 // valid: a planet's own orders
    EmpireId owner;
    VehicleId lead;
    uint64_t created = 0;            // object creation order (the order actors act in)
    int speed = 0;                   // movement points at the start of the turn
    DayCounter counter;
    bool actedOnce = false;          // speed 0: one action on day 1
    int used = 0;                    // steps made this turn
    bool stopped = false;            // gone, or stopped for the turn by a hazard
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
            for (Actor& a : actors_)
                if (!a.stopped) a.counter.newDay(a.speed);
            for (size_t i = 0; i < actors_.size(); ++i) {
                Actor& a = actors_[i];
                if (a.stopped) continue;
                prune(a);
                if (a.stopped) continue;
                if (a.speed > 0) {
                    if (!a.counter.take()) continue;
                } else {
                    // A vehicle with no movement that has orders acts once, on day 1 (confirmed: binary).
                    if (day != 1 || a.actedOnce) continue;
                    a.actedOnce = true;
                }
                current_ = i;
                act(a);
            }
            resolveCombat();
        }
    }

private:
    // ---- Actors ----------------------------------------------------------------------------------

    static constexpr uint64_t kVehicleOrder = uint64_t{1} << 40;  // planets were created before any vehicle (inferred)

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
            a.created = kVehicleOrder + lead->id.value * 2;  // a fleet acts at its leader's place (inferred)
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
            a.created = kVehicleOrder + v.id.value * 2 + 1;
            // A member moving with its fleet still carries out its own orders that act
            // where it stands: self-destruct, launches, cloaking... (inferred)
            a.pinned = assigned.contains(v.id);
            actors_.push_back(std::move(a));
        }
        for (const auto& c : s_.colonies) {
            if (!c || c->orders.empty() || !inSystem(s_.galaxy, c->planet)) continue;
            Actor a;
            a.planet = c->planet;
            a.owner = c->owner;
            a.created = c->planet.value;
            actors_.push_back(std::move(a));
        }
        for (Actor& a : actors_) a.speed = a.planet.valid() ? 0 : remaining(a);
        std::stable_sort(actors_.begin(), actors_.end(), [](const Actor& x, const Actor& y) { return x.created < y.created; });
    }

    bool autoDrone(const Vehicle& v) const {
        return vehicleType(r_, s_, v) == VehicleType::Drone && (v.targetVehicle.valid() || v.targetObject.valid());
    }

    void prune(Actor& a) {
        if (a.planet.valid()) {
            const Colony* c = s_.colony(a.planet);
            if (!c || c->owner != a.owner) a.stopped = true;
            return;
        }
        std::erase_if(a.members, [&](VehicleId id) {
            const Vehicle* v = s_.vehicle(id);
            return !v || !alive(*v) || v->owner != a.owner;
        });
        if (a.members.empty()) {
            a.stopped = true;
            return;
        }
        if (std::find(a.members.begin(), a.members.end(), a.lead) == a.members.end()) a.lead = a.members.front();
    }

    Location where(const Actor& a) const {
        if (a.planet.valid()) return locationOf(s_.galaxy, a.planet);
        return s_.vehicle(a.lead)->location;
    }

    std::string name(const Actor& a) const {
        if (a.planet.valid()) return s_.galaxy.object(a.planet).name;
        if (a.fleet.valid())
            if (const Fleet* f = s_.fleet(a.fleet)) return f->name;
        return s_.vehicle(a.lead)->name;
    }

    // Movement points left: the lowest among the members.
    int remaining(const Actor& a) const {
        int left = INT_MAX;
        for (VehicleId id : a.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v)) left = std::min(left, v->movement);
        return left == INT_MAX ? 0 : left;
    }

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
    // A member with no movement at all freezes the group.
    bool immobile(const Actor& a) const {
        return a.planet.valid() || any(a, [&](const Vehicle& v) { return turnMovement(r_, s_, v) + bonus(v.id) <= 0; });
    }
    // A ship whose space yard has items queued cannot move or warp (§6.2).
    bool yardBusy(const Actor& a) const {
        return any(a, [&](const Vehicle& v) { return !v.queue.items.empty() && vehicleHasSpaceYard(r_, s_, v); });
    }
    bool neutral(const Actor& a) const {
        return a.owner.valid() && a.owner.index() < s_.empires.size() && s_.empire(a.owner).kind == PlayerKind::Neutral;
    }
    int bonus(VehicleId id) const {
        const auto it = bonus_.find(id);
        return it == bonus_.end() ? 0 : it->second;
    }

    std::vector<Order>* orders(const Actor& a) {
        if (a.planet.valid()) {
            Colony* c = s_.colony(a.planet);
            return c ? &c->orders : nullptr;
        }
        if (a.fleet.valid()) {
            Fleet* f = s_.fleet(a.fleet);
            return f ? &f->orders : nullptr;
        }
        Vehicle* v = s_.vehicle(a.lead);
        return v ? &v->orders : nullptr;
    }
    bool repeat(const Actor& a) const {
        if (a.planet.valid()) return false;
        if (a.fleet.valid()) {
            const Fleet* f = s_.fleet(a.fleet);
            return f && f->repeatOrders;
        }
        const Vehicle* v = s_.vehicle(a.lead);
        return v && v->repeatOrders;
    }

    // ---- The order loop ----------------------------------------------------------------------------

    void act(Actor& a) {
        std::vector<Order>* list = orders(a);
        if (!list) return;
        if (list->empty()) {
            if (!a.planet.valid() && a.members.size() == 1 && autoDrone(*s_.vehicle(a.lead))) droneStep(a);
            return;
        }
        Order o = list->front();
        if (a.pinned && !inPlace(a, o)) return;  // waits until the member acts alone
        const Exec e = execute(a, o);
        prune(a);
        afterAction(a);
        if (a.stopped && e != Exec::Fail) return;
        switch (e) {
            case Exec::Moved:
            case Exec::Wait: writeBack(a, o); break;
            case Exec::MovedDone:
            case Exec::Done: complete(a); break;
            case Exec::Acted:
                touched_.push_back(where(a));
                complete(a);
                break;
            case Exec::ActedStay:
                touched_.push_back(where(a));
                writeBack(a, o);
                break;
            case Exec::Removed: removeFront(a); break;
            case Exec::Fail: clearOrders(a); break;
            case Exec::Gone: break;
        }
    }

    // After every daily action: the depot check (§7), and a cloak drops at 0
    // supply or when it can no longer work (§8).
    void afterAction(Actor& a) {
        if (a.planet.valid() || a.members.empty()) return;
        for (VehicleId id : a.members) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            if (resupplyDepotAt(r_, s_, v->owner, v->location)) refillSupply(r_, s_, *v);
            if (v->status == VehicleStatus::Cloaked &&
                ((v->supply <= 0 && !vehicleHasUnlimitedSupply(r_, s_, *v)) || !canCloak(r_, s_, *v)))
                v->status = VehicleStatus::Normal;
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

    // Done: the order leaves the head; with Repeat on it goes to the end (§8).
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

    // Failed: the whole list is cleared and Repeat switched off, for every
    // member of the group (§8, confirmed: binary).
    void clearOrders(Actor& a) {
        a.routeValid = false;
        if (a.planet.valid()) {
            if (Colony* c = s_.colony(a.planet)) c->orders.clear();
            return;
        }
        if (a.fleet.valid())
            if (Fleet* f = s_.fleet(a.fleet)) {
                f->orders.clear();
                f->repeatOrders = false;
            }
        for (VehicleId id : a.members)
            if (Vehicle* v = s_.vehicle(id)) {
                v->orders.clear();
                v->repeatOrders = false;
            }
    }

    Exec fail(const Actor& a, const Order& o, std::string_view why) {
        ctx_.log(a.owner, LogCategory::Misc, std::format("{}: {} order cancelled", name(a), displayName(o.kind)), std::string(why), where(a));
        return Exec::Fail;
    }

    Exec afterTravel(Actor& a, const Order& o, Travel t) {
        switch (t) {
            case Travel::Moved:
            case Travel::Reached: return Exec::Moved;
            case Travel::Wait: return Exec::Wait;
            case Travel::Unreachable: return fail(a, o, "No known route to the destination.");
            case Travel::Immobile: return fail(a, o, "It cannot move.");
            case Travel::Busy: return fail(a, o, "Its space yard is building; it cannot move.");
            case Travel::Stopped: return fail(a, o, "Movement stopped by a hazard.");
            case Travel::Arrived: break;
        }
        return Exec::Done;
    }

    // ---- Moving ------------------------------------------------------------------------------------

    RouteOptions routeOptions(const Actor& a) const {
        RouteOptions options;
        options.allowWarp = !hasFighter(a) && !neutral(a);  // fighters and neutral empires never warp
        return options;
    }

    bool ensureRoute(Actor& a, Location goal) {
        const Location here = where(a);
        if (a.routeValid && a.routeGoal == goal && a.routeAt == here && a.routePos < a.route.size()) return true;
        const Location goals[] = {goal};
        auto p = findPathToNearest(r_, s_, a.owner, here, goals, routeOptions(a));
        a.routeValid = p && !p->path.steps.empty();
        if (!a.routeValid) return false;
        a.route = std::move(p->path.steps);
        a.routePos = 0;
        a.routeGoal = goal;
        a.routeAt = here;
        return true;
    }

    // Can the group take a step now? (Arrival and routes are checked by the callers.)
    std::optional<Travel> readyToStep(Actor& a) {
        // A maximum that dropped (engines lost in a battle) caps the movement left at once (§6.1).
        for (VehicleId id : a.members) capMovement(a, id);
        if (immobile(a)) return held(a) ? Travel::Wait : Travel::Immobile;
        if (yardBusy(a)) return Travel::Busy;
        if (remaining(a) <= 0) return Travel::Wait;
        return std::nullopt;
    }

    // One step toward `goal` (one action).
    Travel travel(Actor& a, Location goal) {
        if (!validLocation(s_, goal)) return Travel::Unreachable;
        if (where(a) == goal) return Travel::Arrived;
        if (auto t = readyToStep(a)) return *t;
        if (!ensureRoute(a, goal)) return Travel::Unreachable;
        if (!step(a) && (!ensureRoute(a, goal) || !step(a))) return Travel::Unreachable;
        if (a.stopped) return Travel::Stopped;
        return !a.members.empty() && where(a) == goal ? Travel::Reached : Travel::Moved;
    }

    // A Move To is done on the step that arrives (§8).
    Exec moveTo(Actor& a, const Order& o, Location goal) {
        const Travel t = travel(a, goal);
        return t == Travel::Reached ? Exec::MovedDone : afterTravel(a, o, t);
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
    Travel jump(Actor& a, ObjectId w) {
        const SpaceObject& wp = s_.galaxy.object(w);
        if (!wp.destination.valid() || !inSystem(s_.galaxy, wp.destination)) return Travel::Unreachable;
        if (auto t = readyToStep(a)) return *t;
        a.routeValid = false;
        moveMembers(a, locationOf(s_.galaxy, wp.destination), w);
        return a.stopped ? Travel::Stopped : Travel::Moved;
    }

    // `Sector - Damage` a group stepping into `l` risks: the objects there plus
    // the system's own value (spec 01 §7; several add up).
    int64_t stormDamageAt(Location l) const {
        const StarSystem& sys = s_.galaxy.system(l.system);
        int64_t total = rawSum(sys.abilities, AbilityKind::SectorDamage);
        for (ObjectId o : sys.objects)
            if (s_.galaxy.object(o).sector == l.sector) total += rawSum(s_.galaxy.object(o).abilities, AbilityKind::SectorDamage);
        return total;
    }

    // Damages every member; the group stops for the rest of the turn.
    void hazardHit(Actor& a, int64_t damage, std::string_view cause) {
        for (VehicleId id : std::vector<VehicleId>(a.members)) hurt(ctx_, id, static_cast<int>(std::min<int64_t>(damage, INT_MAX)), cause);
        for (VehicleId id : a.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v))
                ctx_.log(a.owner, LogCategory::Events, std::format("{} damaged", v->name), std::string(cause), v->location);
        a.stopped = true;
    }

    void moveMembers(Actor& a, Location next, ObjectId via) {
        for (VehicleId id : a.members) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            v->location = next;
            v->movement = std::max(0, v->movement - 1);
            // The depot check runs before the step's cost is taken (§7).
            if (resupplyDepotAt(r_, s_, v->owner, next)) refillSupply(r_, s_, *v);
            spendSupply(r_, s_, *v, moveSupplyCost(r_, s_, *v));
        }
        ++a.used;
        entered_.emplace_back(current_, next);
        if (via.valid()) {
            knowledgeChanged(a.owner);
            for (size_t i = 0; i < a.members.size(); ++i) sight::learnWarpLink(s_, a.owner, via);
            // Turbulence: a 50 % chance per transit that every member takes the
            // total of the warp point it leaves; the group arrives but stops (confirmed: binary).
            const int64_t turbulence = rawSum(s_.galaxy.object(via).abilities, AbilityKind::WarpPointTurbulence);
            if (turbulence > 0 && s_.rng.percent(50)) hazardHit(a, turbulence, "Damaged by warp point turbulence.");
            prune(a);
            if (a.members.empty()) return;
        }
        // A storm: a 50 % chance for a group stepping in; it stops (confirmed: binary).
        if (!a.stopped)
            if (const int64_t storm = stormDamageAt(next); storm > 0 && s_.rng.percent(50)) {
                hazardHit(a, storm, "Damaged by a storm.");
                prune(a);
                if (a.members.empty()) return;
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
        for (VehicleId id : a.members) capMovement(a, id);
        touched_.push_back(here);
    }

    // Engines lost or supply exhausted mid-turn: the remaining movement is
    // capped at the new maximum, less the steps made (spec 03 §6.1, §6.4).
    void capMovement(const Actor& a, VehicleId id) {
        Vehicle* v = s_.vehicle(id);
        if (!v || !alive(*v)) return;
        const int max = turnMovement(r_, s_, *v) + (heldInPlace(s_, *v) ? 0 : bonus(id));
        v->movement = std::min(v->movement, std::max(0, max - a.used));
    }

    void droneStep(Actor& a) {
        Vehicle* d = s_.vehicle(a.lead);
        std::optional<Location> goal;
        if (const Vehicle* t = s_.vehicle(d->targetVehicle); t && alive(*t) && sight::canSeeVehicle(r_, s_, d->owner, *t)) goal = t->location;
        else if (d->targetObject.valid() && inSystem(s_.galaxy, d->targetObject)) goal = locationOf(s_.galaxy, d->targetObject);
        if (!goal) {
            d->targetVehicle = {};  // the target is lost; the drone waits for a new one (inferred)
            d->targetObject = {};
            return;
        }
        if (travel(a, *goal) == Travel::Arrived) touched_.push_back(*goal);
    }

    // ---- Orders ---------------------------------------------------------------------------------------

    Exec execute(Actor& a, Order& o) {
        if (a.planet.valid()) {
            if (o.kind == OrderKind::LaunchUnits || o.kind == OrderKind::RecoverUnits) return cargo(a, o);
            return fail(a, o, "Planets cannot carry out that order.");
        }
        switch (o.kind) {
            case OrderKind::MoveTo: return moveTo(a, o, o.location);
            case OrderKind::MoveToWaypoint: {
                const Empire& e = s_.empire(a.owner);
                if (o.amount < 0 || static_cast<size_t>(o.amount) >= e.waypoints.size() || !e.waypoints[static_cast<size_t>(o.amount)].set)
                    return fail(a, o, "The waypoint is not set.");
                return moveTo(a, o, e.waypoints[static_cast<size_t>(o.amount)].location);
            }
            case OrderKind::Warp: return warp(a, o);
            case OrderKind::Attack: return attack(a, o);
            case OrderKind::Resupply: return resupply(a, o);
            case OrderKind::Repair: return repair(a, o);
            case OrderKind::Explore: return explore(a, o);
            case OrderKind::Colonize: return colonize(a, o);
            case OrderKind::Sentry: return sentry(a);
            case OrderKind::LoadCargo:
            case OrderKind::DropCargo:
            case OrderKind::LaunchUnits:
            case OrderKind::RecoverUnits: return cargo(a, o);
            case OrderKind::Cloak:
            case OrderKind::Decloak: return cloak(a, o);
            case OrderKind::SweepMines: return sweep(a);
            case OrderKind::UseComponent: return useComponentOrder(a, o);
            case OrderKind::StellarManipulation: return stellar(a, o);
            case OrderKind::Count: break;
        }
        return fail(a, o, "Unknown order.");
    }

    // Sentry stays at the head until an empire we are hostile to is present in
    // the system, or a member's supply is low; then only it is removed (§8).
    Exec sentry(Actor& a) {
        const int64_t low = r_.setting("Supply Amount for Low Supply Warning", 1000);
        const bool lowSupply = any(a, [&](const Vehicle& v) {
            if (!vehicleUsesSupply(r_, s_, v) || vehicleHasUnlimitedSupply(r_, s_, v)) return false;
            const int64_t threshold = vehicleType(r_, s_, v) == VehicleType::Fighter ? low / 10 : low;
            return v.supply < threshold;
        });
        const bool enemy = hostilePresentInSystem(r_, s_, a.owner, where(a).system);
        if (!lowSupply && !enemy) return Exec::Wait;
        ctx_.log(a.owner, LogCategory::Combat, std::format("{}: {}", name(a), enemy ? "enemy sighted" : "supplies low"), "Sentry duty ended.",
                 where(a));
        return Exec::Removed;
    }

    Exec warp(Actor& a, Order& o) {
        if (hasFighter(a)) return fail(a, o, "Fighters cannot use warp points.");
        if (neutral(a)) return fail(a, o, "Neutral empires cannot use warp points.");
        if (!o.object.valid() || o.object.index() >= s_.galaxy.objects.size() || s_.galaxy.object(o.object).kind != ObjectKind::WarpPoint ||
            !inSystem(s_.galaxy, o.object))
            return fail(a, o, "The warp point no longer exists.");
        const SpaceObject& wp = s_.galaxy.object(o.object);
        if (!wp.destination.valid() || !inSystem(s_.galaxy, wp.destination)) return fail(a, o, "The warp point leads nowhere.");
        const Travel t = travel(a, locationOf(s_.galaxy, o.object));
        if (t != Travel::Arrived) return afterTravel(a, o, t);
        const Travel j = jump(a, o.object);
        return j == Travel::Moved ? Exec::MovedDone : afterTravel(a, o, j);
    }

    // Attack: a pursuit that stays until the target is destroyed or no longer
    // seen; on reaching it the attackers decloak and fight (§8, confirmed: binary).
    Exec attack(Actor& a, Order& o) {
        Location goal;
        if (o.vehicle.valid()) {
            const Vehicle* t = s_.vehicle(o.vehicle);
            if (!t || !alive(*t) || !sight::canSeeVehicle(r_, s_, a.owner, *t)) {
                ctx_.log(a.owner, LogCategory::Combat, std::format("{}: target lost", name(a)), {}, where(a));
                return Exec::Done;
            }
            goal = t->location;  // pursuit: the route is recomputed whenever the target moves
        } else if (o.object.valid()) {
            if (o.object.index() >= s_.galaxy.objects.size() || !inSystem(s_.galaxy, o.object)) return Exec::Done;
            const Colony* c = s_.colony(o.object);
            if (!c || c->owner == a.owner) return Exec::Done;  // (inferred) a planet target is "destroyed" when its colony is gone
            goal = locationOf(s_.galaxy, o.object);
        } else {
            return fail(a, o, "No target.");
        }
        const Travel t = travel(a, goal);
        if (t != Travel::Arrived && t != Travel::Reached) return afterTravel(a, o, t);
        for (VehicleId id : a.members)
            if (Vehicle* v = s_.vehicle(id); v && v->status == VehicleStatus::Cloaked) v->status = VehicleStatus::Normal;
        return t == Travel::Reached ? Exec::Moved : Exec::ActedStay;
    }

    // Picks the nearest of `goals` and stores it in the order.
    bool chooseNearest(Actor& a, Order& o, const std::vector<Location>& goals) {
        if (goals.empty()) return false;
        const auto p = findPathToNearest(r_, s_, a.owner, where(a), goals, routeOptions(a));
        if (!p) return false;
        o.location = goals[p->goal];
        return true;
    }

    // A visible, armed, non-mothballed hostile vehicle in the sector.
    bool armedHostileAt(EmpireId e, Location l) const {
        for (const Vehicle& v : s_.vehicles) {
            if (!alive(v) || v.location != l || v.owner == e || !hostile(s_, e, v.owner) || v.status == VehicleStatus::Mothballed) continue;
            if (!computeDesignStats(r_, nullptr, s_.design(v.design)).armed()) continue;
            if (sight::canSeeVehicle(r_, s_, e, v)) return true;
        }
        return false;
    }

    // Resupply: to the nearest depot in an explored system, skipping sectors
    // with a visible armed hostile (§8, confirmed: binary).
    Exec resupply(Actor& a, Order& o) {
        if (!validLocation(s_, o.location) || !resupplyDepotAt(r_, s_, a.owner, o.location)) {
            std::vector<Location> depots;
            const Empire& e = s_.empire(a.owner);
            for (const auto& c : s_.colonies) {
                if (!c || !inSystem(s_.galaxy, c->planet)) continue;
                const Location l = locationOf(s_.galaxy, c->planet);
                if (!s_.options.omnipresent && !e.hasExplored(l.system)) continue;
                if (resupplyDepotAt(r_, s_, a.owner, l) && !armedHostileAt(a.owner, l)) depots.push_back(l);
            }
            std::sort(depots.begin(), depots.end());
            depots.erase(std::unique(depots.begin(), depots.end()), depots.end());
            if (!chooseNearest(a, o, depots)) return fail(a, o, "No reachable resupply depot.");
        }
        // Expanded into a Move To the depot: done on arrival; the depot check refills.
        const Travel t = travel(a, o.location);
        if (t == Travel::Reached) return Exec::MovedDone;
        if (t != Travel::Arrived) return afterTravel(a, o, t);
        for (VehicleId id : a.members) refillSupply(r_, s_, *s_.vehicle(id));
        return Exec::Acted;
    }

    // Repair: to the nearest own repair source, immobile ones (planets, bases)
    // first; repair ships only when there is none (§8, confirmed: binary).
    Exec repair(Actor& a, Order& o) {
        // Chosen once, as the original expands the order when it is given.
        if (!validLocation(s_, o.location) && repairCapacityAt(r_, s_, a.owner, where(a)) > 0) return Exec::Acted;
        if (!validLocation(s_, o.location) || repairCapacityAt(r_, s_, a.owner, o.location) <= 0) {
            std::vector<Location> fixed, ships;
            for (const auto& c : s_.colonies)
                if (c && c->owner == a.owner && inSystem(s_.galaxy, c->planet) && c->totalPopulation() > 0 &&
                    abilitySum(colonyAbilities(r_, s_, *c), AbilityKind::ComponentRepair) > 0)
                    fixed.push_back(locationOf(s_.galaxy, c->planet));
            for (const Vehicle& v : s_.vehicles) {
                if (!alive(v) || v.owner != a.owner || std::find(a.members.begin(), a.members.end(), v.id) != a.members.end()) continue;
                const VehicleType t = vehicleType(r_, s_, v);
                if (!isShipOrBase(t) || abilitySum(vehicleAbilities(r_, s_, v), AbilityKind::ComponentRepair) <= 0) continue;
                (t == VehicleType::Base ? fixed : ships).push_back(v.location);
            }
            for (auto* list : {&fixed, &ships}) {
                std::sort(list->begin(), list->end());
                list->erase(std::unique(list->begin(), list->end()), list->end());
            }
            if (!chooseNearest(a, o, fixed) && !chooseNearest(a, o, ships)) return fail(a, o, "No reachable repair facility.");
        }
        // Expanded into a Move To the source: done on arrival.
        const Travel t = travel(a, o.location);
        if (t == Travel::Reached) return Exec::MovedDone;
        return t == Travel::Arrived ? Exec::Acted : afterTravel(a, o, t);
    }

    // A warp point worth exploring: in a known system, leading somewhere unexplored or unknown.
    bool explorable(EmpireId e, ObjectId w) const {
        if (!w.valid() || w.index() >= s_.galaxy.objects.size()) return false;
        const SpaceObject& wp = s_.galaxy.object(w);
        if (wp.kind != ObjectKind::WarpPoint || !inSystem(s_.galaxy, w) || !wp.destination.valid() || !inSystem(s_.galaxy, wp.destination))
            return false;
        const SpaceObject& far = s_.galaxy.object(wp.destination);
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

    Exec explore(Actor& a, Order& o) {
        if (hasFighter(a)) return fail(a, o, "Fighters cannot use warp points.");
        if (neutral(a)) return fail(a, o, "Neutral empires cannot use warp points.");
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
            const auto p = goals.empty() ? std::nullopt : findPathToNearest(r_, s_, a.owner, where(a), goals, routeOptions(a));
            if (!p) {  // nothing left: the order completes (the original adds nothing, §8)
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
        const Travel t = travel(a, locationOf(s_.galaxy, o.object));
        if (t != Travel::Arrived) return afterTravel(a, o, t);
        const Travel j = jump(a, o.object);
        return j == Travel::Moved ? Exec::MovedDone : afterTravel(a, o, j);
    }

    // Colonize (§8): fails while a member is cloaked or when nobody can colonize
    // the planet; the colonizer is the last suitable member (confirmed: binary).
    Exec colonize(Actor& a, Order& o) {
        if (any(a, [](const Vehicle& v) { return v.status == VehicleStatus::Cloaked; })) return fail(a, o, "A cloaked ship cannot colonize.");
        VehicleId colonizer;
        std::string why;
        for (VehicleId id : a.members) {
            const std::string p = colonizeProblem(r_, s_, *s_.vehicle(id), o.object);
            if (p.empty()) colonizer = id;
            else if (why.empty()) why = p;
        }
        if (!colonizer.valid()) return fail(a, o, why);
        if (o.amount == 0) {
            loadColonists(ctx_, colonizer);  // colonists come aboard where the order starts (spec 03 §8)
            o.amount = 1;
        }
        const Travel t = travel(a, locationOf(s_.galaxy, o.object));
        return t == Travel::Arrived ? Exec::Wait : afterTravel(a, o, t);  // colonized in turn phase 4
    }

    // Load, Launch and Recover are always done; Drop can fail (§8, confirmed: binary).
    Exec cargo(Actor& a, Order& o) {
        if (!a.planet.valid() && o.location.system.valid()) {
            const Travel t = travel(a, o.location);
            if (t != Travel::Arrived) return afterTravel(a, o, t);
        }
        int64_t moved = 0;
        auto part = [&]() {
            Order p = o;
            if (o.amount >= 0) p.amount = static_cast<int>(std::max<int64_t>(0, o.amount - moved));
            return p;
        };
        if (a.planet.valid()) {
            const Launcher from{{}, a.planet};
            moved = o.kind == OrderKind::LaunchUnits ? launchUnits(ctx_, budget_, from, o) : recoverUnits(ctx_, from, o);
        } else {
            for (VehicleId id : std::vector<VehicleId>(a.members)) {
                const Order p = part();
                if (o.amount >= 0 && p.amount <= 0) break;
                switch (o.kind) {
                    case OrderKind::LoadCargo: moved += loadCargo(ctx_, id, o.design, p.amount, a.members); break;
                    case OrderKind::DropCargo: moved += dropCargo(ctx_, id, o.design, p.amount, a.members); break;
                    case OrderKind::LaunchUnits: moved += launchUnits(ctx_, budget_, Launcher{id, {}}, p); break;
                    case OrderKind::RecoverUnits: moved += recoverUnits(ctx_, Launcher{id, {}}, p); break;
                    default: break;
                }
            }
        }
        if (o.kind == OrderKind::DropCargo && moved <= 0) return fail(a, o, "Nothing could be dropped here.");
        if (moved > 0 && (o.kind == OrderKind::LaunchUnits || o.kind == OrderKind::RecoverUnits))
            ctx_.log(a.owner, LogCategory::Misc,
                     std::format("{} {} {} {}", name(a), o.kind == OrderKind::LaunchUnits ? "launched" : "recovered", moved, s_.design(o.design).name),
                     {}, where(a));
        return Exec::Acted;
    }

    // Cloaking needs a working part of level 2 or more and supply above 0; it
    // costs nothing now (the parts' supply is charged every end of turn) (§8).
    Exec cloak(Actor& a, Order& o) {
        bool changed = false;
        for (VehicleId id : a.members) {
            Vehicle* v = s_.vehicle(id);
            if (o.kind == OrderKind::Decloak) {
                if (v->status == VehicleStatus::Cloaked) v->status = VehicleStatus::Normal;
                changed = true;
                continue;
            }
            if (v->status != VehicleStatus::Normal || !canCloak(r_, s_, *v)) continue;
            if (v->supply <= 0 && !vehicleHasUnlimitedSupply(r_, s_, *v)) continue;
            v->status = VehicleStatus::Cloaked;
            v->queue.items.clear();  // cloaked ships cannot build (spec 01 §6.4)
            changed = true;
        }
        return changed ? Exec::Acted : fail(a, o, "No working cloaking device, or no supplies.");
    }

    // Sweep Mines: the minefield encounter of the sector runs again, then 1 MP
    // (if any is left) and one move's supply are paid. Always done (§8).
    Exec sweep(Actor& a) {
        for (VehicleId id : std::vector<VehicleId>(a.members)) sweepMines(ctx_, id);
        for (VehicleId id : a.members)
            if (Vehicle* v = s_.vehicle(id); v && alive(*v)) {
                v->movement = std::max(0, v->movement - 1);
                spendSupply(r_, s_, *v, moveSupplyCost(r_, s_, *v));
                capMovement(a, id);
            }
        return Exec::Acted;
    }

    Exec useComponentOrder(Actor& a, Order& o) {
        int gained = -1;
        for (VehicleId id : std::vector<VehicleId>(a.members)) {
            const int g = useComponent(ctx_, id, o.amount);
            if (g < 0) continue;
            gained = std::max(gained, g);
            if (g > 0 && !heldInPlace(s_, *s_.vehicle(id))) {
                // Emergency energy: V1 more actions at one a day (the day counter,
                // confirmed: binary), with the movement points to spend in them (inferred).
                s_.vehicle(id)->movement += g;
                bonus_[id] += g;
            }
        }
        if (gained < 0) return fail(a, o, "No usable component.");
        a.counter.add(gained);
        return Exec::Acted;
    }

    Exec stellar(Actor& a, Order& o) {
        const auto target = stellarTarget(s_, o, where(a));
        if (!target) return fail(a, o, "The target no longer exists.");
        const Travel t = travel(a, *target);
        if (t != Travel::Arrived) return afterTravel(a, o, t);
        // Movement is needed but not spent; a refusal for lack of it makes the
        // order wait (spec 03 §8, simultaneous games). Construct needs none.
        const auto action = static_cast<StellarAction>(std::clamp(o.amount, 0, static_cast<int>(StellarAction::Count)));
        if (action != StellarAction::CreateConstructedPlanet && action != StellarAction::Count) {
            const AbilityKind k = stellarAbility(action);
            bool capable = false, ready = false;
            for (VehicleId id : a.members) {
                const Vehicle* v = s_.vehicle(id);
                if (!v || !hasAbility(vehicleAbilities(r_, s_, *v), k)) continue;
                capable = true;
                ready = ready || v->movement > 0;
            }
            if (capable && !ready) return Exec::Wait;
        }
        bool consumed = false;
        const std::string why = stellarManipulation(ctx_, a.members, o, consumed);
        if (!why.empty()) return fail(a, o, why);
        for (Actor& other : actors_) other.routeValid = false;  // the map may have changed
        mapChanged();
        prune(a);
        return a.stopped ? Exec::Gone : Exec::Acted;
    }

    // ---- Combat ---------------------------------------------------------------------------------------

    struct OrderSnapshot {
        std::vector<std::tuple<VehicleId, EmpireId, std::vector<Order>, bool>> vehicles;
        std::vector<std::tuple<FleetId, std::vector<Order>, bool>> fleets;
    };

    // Everything in the sector that could fight already fought there this turn
    // (spec 03 §6.3). Counted (inferred): vehicles other than mines and
    // colonies whose owner is hostile to, or faced by, another owner present;
    // a cloaked vehicle that did not fight is taken as unseen.
    bool everyoneFought(Location where) const {
        std::vector<EmpireId> owners;
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location == where && v.owner.valid()) owners.push_back(v.owner);
        for (ObjectId o : planetsAt(s_, where))
            if (const Colony* c = s_.colony(o); c && c->owner.valid()) owners.push_back(c->owner);
        auto contested = [&](EmpireId e) {
            return std::any_of(owners.begin(), owners.end(), [&](EmpireId o) { return o != e && (hostile(s_, e, o) || hostile(s_, o, e)); });
        };
        for (const Vehicle& v : s_.vehicles) {
            if (!alive(v) || v.location != where || !v.owner.valid() || vehicleType(r_, s_, v) == VehicleType::Mine) continue;
            if (foughtVehicles_.contains({v.id, where}) || v.status == VehicleStatus::Cloaked || !contested(v.owner)) continue;
            return false;
        }
        for (ObjectId o : planetsAt(s_, where))
            if (const Colony* c = s_.colony(o); c && c->owner.valid() && contested(c->owner) && !foughtPlanets_.contains({o, where})) return false;
        return true;
    }

    OrderSnapshot snapshot(Location where) const {
        OrderSnapshot snap;
        std::set<FleetId> fleets;
        for (const Vehicle& v : s_.vehicles) {
            if (!alive(v) || v.location != where) continue;
            snap.vehicles.emplace_back(v.id, v.owner, v.orders, v.repeatOrders);
            if (v.fleet.valid()) fleets.insert(v.fleet);
        }
        for (FleetId id : fleets)
            if (const Fleet* f = s_.fleet(id)) snap.fleets.emplace_back(id, f->orders, f->repeatOrders);
        return snap;
    }

    // Per member of the actors that stepped into `where` today: count and damage.
    std::map<VehicleId, std::pair<int, std::vector<int>>> marks(Location where) const {
        std::map<VehicleId, std::pair<int, std::vector<int>>> out;
        for (const auto& [index, l] : entered_) {
            if (l != where) continue;
            for (VehicleId id : actors_[index].members)
                if (const Vehicle* v = s_.vehicle(id)) out[id] = {v->count, v->damage};
        }
        return out;
    }

    void resolveCombat() {
        std::vector<Location> sites = std::move(touched_);
        touched_.clear();
        std::sort(sites.begin(), sites.end());
        sites.erase(std::unique(sites.begin(), sites.end()), sites.end());
        bool resolved = false;
        if (hooks_.possible && hooks_.resolve)
            for (const Location& where : sites) {
                if (!hooks_.possible(r_, s_, where) || everyoneFought(where)) continue;
                const OrderSnapshot snap = snapshot(where);
                const auto before = marks(where);
                const size_t records = s_.combats.size();
                hooks_.resolve(ctx_, where);
                afterBattle(where, records, snap, before);
                resolved = true;
            }
        entered_.clear();
        if (resolved) s_.removeDeadVehicles();
    }

    // Combat neither stops movement nor clears orders: lists are kept, and only
    // a Sentry order at the head of a participant's list is removed (§6.3, §6.4,
    // confirmed: binary). A group that met a minefield stops and its order fails.
    void afterBattle(Location where, size_t recordsBefore, const OrderSnapshot& snap,
                     const std::map<VehicleId, std::pair<int, std::vector<int>>>& before) {
        std::set<VehicleId> fought;
        for (size_t i = recordsBefore; i < s_.combats.size(); ++i)
            if (s_.combats[i].location == where)
                for (const CombatPiece& p : s_.combats[i].pieces) {
                    if (p.vehicle.valid()) fought.insert(p.vehicle);
                    if (p.planet.valid()) foughtPlanets_.insert({p.planet, where});
                }
        for (VehicleId id : fought) foughtVehicles_.insert({id, where});
        if (!fought.empty() && std::find(ctx_.battleSites.begin(), ctx_.battleSites.end(), where) == ctx_.battleSites.end())
            ctx_.battleSites.push_back(where);
        // Keep what combat may have cleared (a captured vehicle keeps its new, empty list).
        for (const auto& [id, owner, list, rep] : snap.vehicles)
            if (Vehicle* v = s_.vehicle(id); v && v->owner == owner) {
                v->orders = list;
                v->repeatOrders = rep;
            }
        for (const auto& [id, list, rep] : snap.fleets)
            if (Fleet* f = s_.fleet(id)) {
                f->orders = list;
                f->repeatOrders = rep;
            }
        for (VehicleId id : fought) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            std::vector<Order>* list = &v->orders;
            if (followsFleetOrders(s_, *v))
                if (Fleet* f = s_.fleet(v->fleet)) list = &f->orders;
            if (!list->empty() && list->front().kind == OrderKind::Sentry) list->erase(list->begin());
        }
        // Minefields: a group that stepped in today, was hurt and fought no battle (inferred detection).
        for (const auto& [index, l] : entered_) {
            if (l != where) continue;
            Actor& a = actors_[index];
            bool struck = false;
            for (const auto& [id, mark] : before) {
                if (fought.contains(id) || std::find(a.members.begin(), a.members.end(), id) == a.members.end()) continue;
                const Vehicle* v = s_.vehicle(id);
                if (!v || v->count < mark.first || v->damage != mark.second) struck = true;
            }
            if (!struck) continue;
            ctx_.log(a.owner, LogCategory::Combat, std::format("{} stopped by a minefield", name(a)), "Its orders were cancelled.", where);
            prune(a);
            if (!a.stopped) clearOrders(a);
            a.stopped = true;
        }
    }

    TurnContext& ctx_;
    const Rules& r_;
    GameState& s_;
    const CombatHooks& hooks_;
    std::vector<Actor> actors_;
    size_t current_ = 0;                                // the actor acting now
    std::vector<Location> touched_;                     // sectors where something acted today
    std::vector<std::pair<size_t, Location>> entered_;  // (actor, sector) steps made today
    std::set<std::pair<VehicleId, Location>> foughtVehicles_;  // who fought where this turn
    std::set<std::pair<ObjectId, Location>> foughtPlanets_;
    std::map<VehicleId, int> bonus_;                    // emergency energy gained this turn
    UnitBudget budget_;
    std::map<EmpireId, uint64_t> version_;              // bumped when an empire's map knowledge changes
    std::map<EmpireId, ExploreBoard> boards_;
    std::map<VehicleId, uint64_t> exploreIdle_;         // actor -> version at which nothing was left to explore
    std::set<VehicleId> exploreLogged_;
};

// King steps from `at` toward `target`, stopping there.
Sector stepToward(Sector at, Sector target, int64_t steps) {
    for (int64_t k = 0; k < steps && at != target; ++k)
        at = Sector{at.x + (target.x > at.x) - (target.x < at.x), at.y + (target.y > at.y) - (target.y < at.y)};
    return at;
}

} // namespace

std::vector<int> actionDays(int speed, DayCounterMode mode) {
    return mode == DayCounterMode::Exact ? daysActed<DayCounterMode::Exact>(speed) : daysActed<DayCounterMode::Double>(speed);
}

void startTurn(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    for (Vehicle& v : s.vehicles)
        if (alive(v)) v.movement = turnMovement(r, s, v);  // 0 while held by sabotage or an event
    // Fleet members in the fleet's sector get the lowest maximum among them (§6.3).
    for (const Fleet& f : s.fleets) {
        const Vehicle* lead = fleetLeader(s, f);
        if (!lead) continue;
        const Location here = lead->location;
        int lowest = INT_MAX;
        for (VehicleId id : f.members)
            if (const Vehicle* v = s.vehicle(id); v && alive(*v) && v->location == here) lowest = std::min(lowest, v->movement);
        for (VehicleId id : f.members)
            if (Vehicle* v = s.vehicle(id); v && alive(*v) && v->location == here) v->movement = lowest;
    }
}

void runMovementAndCombat(TurnContext& ctx) { runMovementAndCombat(ctx, defaultCombatHooks()); }

void runMovementAndCombat(TurnContext& ctx, const CombatHooks& hooks) {
    Mover mover(ctx, hooks);
    mover.run();
}

void runStellarHazards(TurnContext& ctx) {
    GameState& s = ctx.state;
    // One random target sector per turn, shared by every system: sector number
    // R[0,144], so rows 0-10 and the first two squares of row 11 (confirmed:
    // binary). Drawn only when some system drifts (inferred).
    std::optional<Sector> driftTarget;
    for (const StarSystem& sys : s.galaxy.systems)
        if (rawSum(sys.abilities, AbilityKind::SystemMovementRandom) > 0) {
            const int n = static_cast<int>(s.rng.below(145));
            driftTarget = Sector{n % kSystemSize, n / kSystemSize};
            break;
        }
    const Sector centre{kSystemCenter, kSystemCenter};
    for (const StarSystem& sys : s.galaxy.systems) {
        // Several abilities of a kind add up (spec 01 §4.4).
        const int64_t pull = rawSum(sys.abilities, AbilityKind::SystemMovementTowardsCenter);
        const int64_t drift = rawSum(sys.abilities, AbilityKind::SystemMovementRandom);
        const int64_t damage = rawSum(sys.abilities, AbilityKind::SystemDestructiveCenter);
        if (pull <= 0 && drift <= 0 && damage <= 0) continue;
        std::vector<VehicleId> here;  // every ship, base and unit group in the system
        for (const Vehicle& v : s.vehicles)
            if (alive(v) && v.location.system == sys.id) here.push_back(v.id);
        for (VehicleId id : here) {
            Vehicle& v = *s.vehicle(id);
            if (pull > 0) v.location.sector = stepToward(v.location.sector, centre, pull);
            if (drift > 0 && driftTarget) v.location.sector = stepToward(v.location.sector, *driftTarget, drift);
        }
        if (damage <= 0) continue;
        for (VehicleId id : here) {
            const Vehicle* v = s.vehicle(id);
            if (!v || !alive(*v) || v->location.sector != centre) continue;
            const std::string name = v->name;
            const EmpireId owner = v->owner;
            const Location where = v->location;
            if (!hurt(ctx, id, static_cast<int>(std::min<int64_t>(damage, INT_MAX)), "Torn apart at the centre of the system."))
                ctx.log(owner, LogCategory::Events, std::format("{} damaged", name), "Damaged at the centre of the system.", where);
        }
    }
    s.removeDeadVehicles();
}

} // namespace opense4::game::movement
