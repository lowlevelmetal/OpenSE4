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
#include "game/orders.hpp"
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
            [](TurnContext& ctx, Location where, std::span<const VehicleId> entering) { combat::resolveSpaceCombat(ctx, where, entering); }};
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
// Asked: turn-based games only, the group stopped before a sector with
// enemies for the player's answer (spec 03 §6.2); the order waits.
enum class Travel { Arrived, Moved, Reached, Wait, Unreachable, Immobile, Busy, Stopped, Blocked, Encounter, Asked };

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
    bool adhoc = false;              // ships outside fleets with identical head orders, each with its own list (§8)
    bool encountered = false;        // the last warp arrived where the owner's options clear orders (§6.4)
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
                act(actors_[i]);
            }
            resolveCombat();
            mergeSplitActors();
        }
    }

    // Turn-based games (spec 03 §6.3 "Turn-based", spec 04 §2): the selected
    // groups of one empire carry out their orders now, action after action,
    // until each has spent its movement, waits, fails or has nothing left.
    // A group that steps into a sector where combat is possible fights there
    // at once and its order fails; one that carries out an order in a sector
    // offers it to combat without failing. The per-turn records (steps,
    // emergency movement, launches) live in GameState::playerTurn.
    void runLive(const LiveMove& m) {
        live_ = &m;
        loadPlayerTurn();
        buildActors();
        // Ad-hoc groups (§8) form among the ships the player moves now (inferred).
        if (!m.vehicles.empty() || !m.fleets.empty() || !m.planets.empty())
            for (Actor& a : actors_) {
                if (!a.adhoc) continue;
                std::erase_if(a.members, [&](VehicleId id) { return std::find(m.vehicles.begin(), m.vehicles.end(), id) == m.vehicles.end(); });
                if (a.members.empty()) continue;
                a.lead = a.members.front();
                a.adhoc = a.members.size() > 1;
            }
        std::erase_if(actors_, [&](const Actor& a) { return a.owner != m.empire || (a.members.empty() && !a.planet.valid()) || !selected(a); });
        for (size_t i = 0; i < actors_.size(); ++i) {
            current_ = i;
            liveActor(actors_[i]);
            // Members that left an ad-hoc group go on as their own groups.
            for (Actor& b : split_) actors_.push_back(std::move(b));
            split_.clear();
        }
        savePlayerTurn();
    }

    std::vector<EntryQuestion> questions() const { return questions_; }

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
        // Ad-hoc groups (spec 03 §8): ships outside fleets in one sector whose head
        // orders are identical act as one group, at the pace of the slowest. They
        // form when movement starts and only lose members (inferred).
        using GroupKey = std::tuple<EmpireId, Location, uint8_t, Location, ObjectId, VehicleId, DesignId, int>;
        std::map<GroupKey, size_t> groups;
        for (const Vehicle& v : s_.vehicles) {
            if (!alive(v)) continue;
            startSpeed_[v.id] = v.movement;
            if (v.orders.empty() && !autoDrone(v)) continue;
            // A member moving with its fleet still carries out its own orders that act
            // where it stands: self-destruct, launches, cloaking... (inferred)
            const bool pinned = assigned.contains(v.id);
            if (!pinned && !v.orders.empty() && vehicleType(r_, s_, v) == VehicleType::Ship) {
                const Order& h = v.orders.front();
                const GroupKey key{v.owner, v.location, static_cast<uint8_t>(h.kind), h.location, h.object, h.vehicle, h.design, h.amount};
                if (const auto it = groups.find(key); it != groups.end()) {
                    Actor& g = actors_[it->second];
                    g.members.push_back(v.id);
                    g.adhoc = true;
                    continue;
                }
                groups.emplace(key, actors_.size());
            }
            Actor a;
            a.members = {v.id};
            a.owner = v.owner;
            a.lead = v.id;
            a.created = kVehicleOrder + v.id.value * 2 + 1;
            a.pinned = pinned;
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
    // Led by a mine sweeper: the owner's tagged minefields are no obstacle (spec 03 §6.2).
    bool sweeperLed(const Actor& a) const {
        const Vehicle* v = a.planet.valid() ? nullptr : s_.vehicle(a.lead);
        return v && leadsSweeperGroup(s_, *v);
    }
    int bonus(VehicleId id) const {
        const auto it = bonus_.find(id);
        return it == bonus_.end() ? 0 : it->second;
    }

    // The list the actor executes: the planet's, the fleet's, or its lead's (an
    // ad-hoc group's members hold identical head orders in their own lists).
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
    // The Repeat flag of the list the actor executes.
    bool repeat(const Actor& a) const {
        if (a.planet.valid()) return false;
        if (a.fleet.valid()) {
            const Fleet* f = s_.fleet(a.fleet);
            return f && f->repeatOrders;
        }
        const Vehicle* v = s_.vehicle(a.lead);
        return v && v->repeatOrders;
    }
    // Every list a change to the head applies to, with its Repeat flag.
    template <class Fn>
    void forEachList(const Actor& a, Fn&& fn) {
        if (a.planet.valid()) {
            if (Colony* c = s_.colony(a.planet)) fn(c->orders, false);
            return;
        }
        if (a.fleet.valid()) {
            if (Fleet* f = s_.fleet(a.fleet)) fn(f->orders, f->repeatOrders);
            return;
        }
        for (VehicleId id : a.adhoc ? a.members : std::vector<VehicleId>{a.lead})
            if (Vehicle* v = s_.vehicle(id)) fn(v->orders, v->repeatOrders);
    }

    // An ad-hoc group keeps only the members whose head order is still the
    // lead's; the others go on alone, or as groups of their own (inferred).
    void regroup(Actor& a) {
        if (!a.adhoc || a.stopped) return;
        const Vehicle* lead = s_.vehicle(a.lead);
        const std::optional<Order> head = lead && !lead->orders.empty() ? std::optional<Order>(lead->orders.front()) : std::nullopt;
        std::vector<VehicleId> stay;
        std::vector<std::pair<Order, std::vector<VehicleId>>> leaving;
        for (VehicleId id : a.members) {
            const Vehicle* v = s_.vehicle(id);
            if (!v || v->orders.empty()) continue;
            if (head && v->orders.front() == *head) {
                stay.push_back(id);
                continue;
            }
            auto it = std::find_if(leaving.begin(), leaving.end(), [&](const auto& g) { return g.first == v->orders.front(); });
            if (it == leaving.end()) leaving.push_back({v->orders.front(), {id}});
            else it->second.push_back(id);
        }
        for (auto& [order, ids] : leaving) {
            Actor b;
            b.members = ids;
            b.owner = a.owner;
            b.lead = ids.front();
            b.created = kVehicleOrder + ids.front().value * 2 + 1;
            b.adhoc = ids.size() > 1;
            b.counter = a.counter;
            b.used = a.used;
            b.speed = INT_MAX;
            for (VehicleId id : ids) b.speed = std::min(b.speed, startSpeed_[id]);
            split_.push_back(std::move(b));
        }
        a.members = std::move(stay);
        a.adhoc = a.members.size() > 1;
        if (a.members.empty()) a.stopped = true;
    }

    // Actors that left an ad-hoc group act from the next day, in creation order.
    void mergeSplitActors() {
        if (split_.empty()) return;
        for (Actor& b : split_) actors_.push_back(std::move(b));
        split_.clear();
        std::stable_sort(actors_.begin(), actors_.end(), [](const Actor& x, const Actor& y) { return x.created < y.created; });
    }

    // ---- The order loop ----------------------------------------------------------------------------

    // One action; what it did, or nothing when the actor had nothing to do.
    std::optional<Exec> act(Actor& a) {
        regroup(a);
        if (a.stopped) return std::nullopt;
        std::vector<Order>* list = orders(a);
        if (!list) return std::nullopt;
        if (list->empty()) {
            if (!a.planet.valid() && a.members.size() == 1 && autoDrone(*s_.vehicle(a.lead))) return droneStep(a);
            return std::nullopt;
        }
        Order o = list->front();
        if (a.pinned && !inPlace(a, o)) return std::nullopt;  // waits until the member acts alone
        const Exec e = execute(a, o);
        prune(a);
        afterAction(a);
        if (a.stopped && e != Exec::Fail) return e;
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
        regroup(a);
        return e;
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
        forEachList(a, [&](std::vector<Order>& list, bool) {
            if (!list.empty()) list.front() = o;
        });
    }

    // Done: the order leaves the head; with Repeat on it goes to the end (§8).
    void complete(Actor& a) {
        a.routeValid = false;
        forEachList(a, [&](std::vector<Order>& list, bool repeat) {
            if (list.empty()) return;
            const Order done = list.front();
            list.erase(list.begin());
            if (repeat) list.push_back(done);
        });
    }

    void removeFront(Actor& a) {
        a.routeValid = false;
        forEachList(a, [&](std::vector<Order>& list, bool) {
            if (!list.empty()) list.erase(list.begin());
        });
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
            case Travel::Blocked: return fail(a, o, "The way is blocked.");
            case Travel::Encounter: return fail(a, o, "Another empire is in the system; the orders were cleared (empire options).");
            case Travel::Asked: return Exec::Wait;
            case Travel::Arrived: break;
        }
        return Exec::Done;
    }

    // ---- Moving ------------------------------------------------------------------------------------

    RouteOptions routeOptions(const Actor& a) const {
        RouteOptions options;
        options.allowWarp = !hasFighter(a) && !neutral(a);  // fighters and neutral empires never warp
        options.sweeper = sweeperLed(a);
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
        a.encountered = false;
        if (!validLocation(s_, goal)) return Travel::Unreachable;
        if (where(a) == goal) return Travel::Arrived;
        if (auto t = readyToStep(a)) return *t;
        if (!ensureRoute(a, goal)) return Travel::Unreachable;
        Step st = step(a);
        if (st == Step::Stale) st = ensureRoute(a, goal) ? step(a) : Step::Stale;
        if (st == Step::Stale) return Travel::Unreachable;
        if (st == Step::Blocked) return Travel::Blocked;
        if (st == Step::Asked) return Travel::Asked;
        if (a.stopped) return Travel::Stopped;
        if (a.encountered) return Travel::Encounter;
        return !a.members.empty() && where(a) == goal ? Travel::Reached : Travel::Moved;
    }

    // A Move To is done on the step that arrives (§8).
    Exec moveTo(Actor& a, const Order& o, Location goal) {
        const Travel t = travel(a, goal);
        return t == Travel::Reached ? Exec::MovedDone : afterTravel(a, o, t);
    }

    // Asked: turn-based games, the player is asked before the group enters (spec 03 §6.2).
    enum class Step { Moved, Stale, Blocked, Asked };

    // A step onto `l` is re-chosen when it is not the square the group heads
    // for and it is a tagged minefield (with the owner's option on, unless a
    // mine sweeper leads), holds a storm with `Sector - Damage` or a visible
    // hostile object (spec 03 §6.2, confirmed: binary). Counted (inferred): the
    // damage of the sector's own objects, not a system-wide value no step
    // could avoid; hostile vehicles the owner sees and hostile colonies.
    bool avoidStep(const Actor& a, Location l) const {
        const Empire& e = s_.empire(a.owner);
        if (e.avoidTaggedMinefields && !sweeperLed(a) &&
            std::find(e.taggedMinefields.begin(), e.taggedMinefields.end(), l) != e.taggedMinefields.end())
            return true;
        for (ObjectId o : s_.galaxy.system(l.system).objects) {
            const SpaceObject& obj = s_.galaxy.object(o);
            if (obj.sector != l.sector) continue;
            if (rawSum(obj.abilities, AbilityKind::SectorDamage) > 0) return true;
            if (const Colony* c = s_.colony(o); c && hostile(s_, a.owner, c->owner)) return true;
        }
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location == l && hostile(s_, a.owner, v.owner) && sight::canSeeVehicle(r_, s_, a.owner, v)) return true;
        return false;
    }

    // In-system steps are greedy: one square toward `target`, diagonal first.
    // A step to avoid is re-chosen at random among the other steps that still
    // approach the target: 1 of the 2 straight steps when moving diagonally, 1
    // of the 3 forward squares when moving straight. After 10 failed tries the
    // group does not move (spec 03 §6.2, confirmed: binary; which squares count
    // as the 3 is inferred).
    std::optional<Sector> greedyStep(const Actor& a, Location here, Sector target) {
        const int x = here.sector.x, y = here.sector.y;
        const int dx = (target.x > x) - (target.x < x), dy = (target.y > y) - (target.y < y);
        auto usable = [&](Sector c) { return c.valid() && (c == target || !avoidStep(a, {here.system, c})); };
        const Sector first{x + dx, y + dy};
        if (usable(first)) return first;
        for (int tries = 0; tries < 10; ++tries) {
            Sector pick;
            if (dx != 0 && dy != 0) pick = s_.rng.below(2) == 0 ? Sector{x + dx, y} : Sector{x, y + dy};
            else if (dx != 0) pick = Sector{x + dx, y + static_cast<int>(s_.rng.below(3)) - 1};
            else pick = Sector{x + static_cast<int>(s_.rng.below(3)) - 1, y + dy};
            if (usable(pick)) return pick;
        }
        return std::nullopt;
    }

    Step step(Actor& a) {
        const Location here = where(a);
        const Location next = a.route[a.routePos];
        if (next.system != here.system) {
            ObjectId via;
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
                return Step::Stale;
            }
            if (asks(a, next)) return Step::Asked;
            ++a.routePos;
            a.routeAt = next;
            moveMembers(a, next, via);
            return Step::Moved;
        }
        // The route picks the warp points; inside a system the group heads for
        // the last square of this system on it (a warp point or the goal).
        size_t last = a.routePos;
        while (last + 1 < a.route.size() && a.route[last + 1].system == here.system) ++last;
        const auto chosen = greedyStep(a, here, a.route[last].sector);
        if (!chosen) return Step::Blocked;
        const Location to{here.system, *chosen};
        // The square actually stepped onto is the one asked about. Greedy steps
        // avoid visible hostile squares other than the leg's last one, so the
        // question only comes up there, before any random re-choice.
        if (asks(a, to)) return Step::Asked;
        if (to == next) {
            ++a.routePos;
            a.routeAt = to;
        } else {
            a.routeValid = false;  // off the planned squares: plan again from there
        }
        moveMembers(a, to, {});
        return Step::Moved;
    }

    // An explicit jump through a warp point, known link or not (Warp).
    Travel jump(Actor& a, ObjectId w) {
        a.encountered = false;
        const SpaceObject& wp = s_.galaxy.object(w);
        if (!wp.destination.valid() || !inSystem(s_.galaxy, wp.destination)) return Travel::Unreachable;
        if (auto t = readyToStep(a)) return *t;
        if (asks(a, locationOf(s_.galaxy, wp.destination))) return Travel::Asked;
        a.routeValid = false;
        moveMembers(a, locationOf(s_.galaxy, wp.destination), w);
        if (a.stopped) return Travel::Stopped;
        return a.encountered ? Travel::Encounter : Travel::Moved;
    }

    // The owner's Ship Orders options (spec 03 §6.4, confirmed: binary): after a
    // warp into a system where an enemy empire (or, with the second option, any
    // other empire) has objects, the order fails and the list is cleared.
    // Counted (inferred): that empire's colonies there and its vehicles the
    // owner sees.
    bool encounterClearsOrders(EmpireId e, SystemId sys) const {
        const EncounterClear option = s_.empire(e).clearOrdersOnEncounter;
        if (option == EncounterClear::Never) return false;
        auto applies = [&](EmpireId other) {
            if (!other.valid() || other == e) return false;
            return option == EncounterClear::Any || hostile(s_, e, other);
        };
        for (ObjectId o : s_.galaxy.system(sys).objects)
            if (const Colony* c = s_.colony(o); c && applies(c->owner)) return true;
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location.system == sys && applies(v.owner) && sight::canSeeVehicle(r_, s_, e, v)) return true;
        return false;
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
            // The sector it leaves, for combat's attackers and start boxes (spec 04 §3).
            v->cameFrom = v->location;
            v->cameFromTurn = s_.turn;
            v->location = next;
            v->movement = std::max(0, v->movement - 1);
            if (live_) ++steps_[id];
            // The depot check runs before the step's cost is taken (§7).
            if (resupplyDepotAt(r_, s_, v->owner, next)) refillSupply(r_, s_, *v);
            spendSupply(r_, s_, *v, moveSupplyCost(r_, s_, *v));
        }
        ++a.used;
        entered_.emplace_back(current_, next);
        if (via.valid()) {
            for (size_t i = 0; i < a.members.size(); ++i) sight::learnWarpLink(s_, a.owner, via);
            // Turbulence: a 50 % chance per transit that every member takes the
            // total of the warp point it leaves; the group arrives but stops (confirmed: binary).
            const int64_t turbulence = rawSum(s_.galaxy.object(via).abilities, AbilityKind::WarpPointTurbulence);
            if (turbulence > 0 && s_.rng.percent(50)) hazardHit(a, turbulence, "Damaged by warp point turbulence.");
            prune(a);
            if (a.members.empty()) return;
            if (encounterClearsOrders(a.owner, next.system)) a.encountered = true;
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
        // Turn-based games count the vehicle's own steps over the whole player turn.
        const int used = live_ ? stepsOf(id) : a.used;
        v->movement = std::min(v->movement, std::max(0, max - used));
    }

    // Moved: a step toward the target; ActedStay: at the target; Wait: nothing to do.
    Exec droneStep(Actor& a) {
        Vehicle* d = s_.vehicle(a.lead);
        std::optional<Location> goal;
        if (const Vehicle* t = s_.vehicle(d->targetVehicle); t && alive(*t) && sight::canSeeVehicle(r_, s_, d->owner, *t)) goal = t->location;
        else if (d->targetObject.valid() && inSystem(s_.galaxy, d->targetObject)) goal = locationOf(s_.galaxy, d->targetObject);
        if (!goal) {
            d->targetVehicle = {};  // the target is lost; the drone waits for a new one (inferred)
            d->targetObject = {};
            return Exec::Wait;
        }
        const Travel t = travel(a, *goal);
        if (t == Travel::Arrived) {
            touched_.push_back(*goal);
            return Exec::ActedStay;
        }
        return t == Travel::Moved || t == Travel::Reached ? Exec::Moved : Exec::Wait;
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
            case OrderKind::Resupply:
            case OrderKind::Repair:
            case OrderKind::Explore: return expandHead(a, o);
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

    // Explore, Resupply and Repair are expanded when they are given (orders.hpp);
    // one that reached a list another way is expanded when it comes up, and the
    // first of the orders it stands for runs at once. With nothing to go to it is
    // removed.
    Exec expandHead(Actor& a, Order& o) {
        OrderContext ctx;
        ctx.owner = a.owner;
        ctx.members = a.members;
        ctx.lead = a.lead;
        ctx.at = where(a);
        ctx.carriesPopulation = any(a, [](const Vehicle& v) { return v.cargo.totalPopulation() > 0; });
        std::vector<Order> expanded;
        expandOrder(r_, s_, ctx, o, expanded);
        if (expanded.empty()) {
            const char* why = o.kind == OrderKind::Explore    ? "nothing left to explore"
                              : o.kind == OrderKind::Resupply ? "no reachable resupply depot"
                                                              : "no reachable repair facility";
            ctx_.log(a.owner, LogCategory::Misc, std::format("{}: {}", name(a), why), {}, where(a));
            return Exec::Removed;
        }
        forEachList(a, [&](std::vector<Order>& list, bool) {
            if (list.empty()) return;
            list.erase(list.begin());
            list.insert(list.begin(), expanded.begin(), expanded.end());
        });
        o = expanded.front();
        return execute(a, o);
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
        return t == Travel::Arrived ? Exec::Wait : afterTravel(a, o, t);  // colonized when the movement phase ends (runColonization)
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
        prune(a);
        return a.stopped ? Exec::Gone : Exec::Acted;
    }

    // ---- Combat ---------------------------------------------------------------------------------------

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

    // The members of the actors that stepped into `where` today and are still
    // there: the groups hostile mines strike (spec 04 §10.6).
    std::vector<VehicleId> entering(Location where) const {
        std::vector<VehicleId> out;
        for (const auto& [index, l] : entered_) {
            if (l != where) continue;
            for (VehicleId id : actors_[index].members)
                if (const Vehicle* v = s_.vehicle(id); v && alive(*v) && v->location == where &&
                                                       std::find(out.begin(), out.end(), id) == out.end())
                    out.push_back(id);
        }
        return out;
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
                const auto before = marks(where);
                const size_t records = s_.combats.size();
                hooks_.resolve(ctx_, where, entering(where));
                afterBattle(where, records, before);
                resolved = true;
            }
        entered_.clear();
        if (resolved) s_.removeDeadVehicles();
    }

    // Combat neither stops movement nor clears orders: lists are kept, and only
    // a Sentry order at the head of a participant's list is removed (§6.3, §6.4,
    // confirmed: binary). A group that met a minefield stops and its order fails.
    void afterBattle(Location where, size_t recordsBefore, const std::map<VehicleId, std::pair<int, std::vector<int>>>& before) {
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

    // ---- Turn-based games ------------------------------------------------------------------------

    // Actions one group may take in one run: movement bounds every loop but a
    // repeating list of orders that give movement back (inferred safeguard).
    static constexpr int kLiveActionLimit = 1000;

    bool selected(const Actor& a) const {
        const LiveMove& m = *live_;
        if (m.vehicles.empty() && m.fleets.empty() && m.planets.empty()) return true;
        auto has = [](const auto& list, auto id) { return std::find(list.begin(), list.end(), id) != list.end(); };
        if (a.planet.valid()) return has(m.planets, a.planet);
        if (a.fleet.valid()) return has(m.fleets, a.fleet);
        return has(m.vehicles, a.lead);
    }

    void loadPlayerTurn() {
        for (const TurnMoves& m : s_.playerTurn.moves) {
            if (m.steps != 0) steps_[m.vehicle] = m.steps;
            if (m.bonus != 0) bonus_[m.vehicle] = m.bonus;
        }
        for (const TurnLaunches& l : s_.playerTurn.launched)
            budget_.launched[{l.vehicle, l.planet, static_cast<AbilityKind>(l.kind)}] = l.count;
    }

    void savePlayerTurn() {
        std::map<VehicleId, TurnMoves> moves;
        for (const auto& [id, n] : steps_) moves[id].steps = n;
        for (const auto& [id, n] : bonus_) moves[id].bonus = n;
        s_.playerTurn.moves.clear();
        for (auto& [id, m] : moves)
            if (s_.vehicle(id)) {
                m.vehicle = id;
                s_.playerTurn.moves.push_back(m);
            }
        s_.playerTurn.launched.clear();
        for (const auto& [key, n] : budget_.launched) {
            const auto& [vehicle, planet, kind] = key;
            s_.playerTurn.launched.push_back({vehicle, planet, static_cast<uint16_t>(kind), n});
        }
    }

    int stepsOf(VehicleId id) const {
        const auto it = steps_.find(id);
        return it == steps_.end() ? 0 : it->second;
    }

    size_t listLength(const Actor& a) {
        const std::vector<Order>* list = orders(a);
        return list ? list->size() : 0;
    }

    void liveActor(Actor& a) {
        size_t idle = 0;  // actions in a row that took no step
        for (int n = 0; n < kLiveActionLimit; ++n) {
            prune(a);
            if (a.stopped) return;
            const size_t steps = entered_.size();
            const std::optional<Exec> e = act(a);
            if (!e) return;
            bool fought = false;
            if (entered_.size() > steps) fought = entryCombat(a);
            else if (*e == Exec::Acted || *e == Exec::ActedStay) placeCombat(a);
            entered_.clear();
            touched_.clear();
            if (fought || a.stopped) return;
            switch (*e) {
                case Exec::Moved:
                case Exec::MovedDone: idle = 0; break;
                case Exec::Done:
                case Exec::Acted:
                case Exec::Removed:
                    // A repeating list that goes round without a step waits for the next
                    // turn (inferred); any other list gets shorter with each order.
                    if (repeat(a) && ++idle > listLength(a) + 1) return;
                    break;
                case Exec::ActedStay:
                case Exec::Wait:
                case Exec::Fail:
                case Exec::Gone: return;
            }
        }
    }

    // Where the group's Attack order at the head of its list means to fight.
    std::optional<Location> attackGoal(const Actor& a) {
        const std::vector<Order>* list = orders(a);
        if (!list || list->empty() || list->front().kind != OrderKind::Attack) return std::nullopt;
        const Order& o = list->front();
        if (o.vehicle.valid()) {
            if (const Vehicle* t = s_.vehicle(o.vehicle); t && alive(*t)) return t->location;
            return std::nullopt;
        }
        if (o.object.valid() && o.object.index() < s_.galaxy.objects.size() && inSystem(s_.galaxy, o.object))
            return locationOf(s_.galaxy, o.object);
        return std::nullopt;
    }

    // Fights the battle of `where` now (mines strike `entering` first).
    void fight(Location where, const std::vector<VehicleId>& entering) {
        const auto before = marks(where);
        const size_t records = s_.combats.size();
        hooks_.resolve(ctx_, where, entering);
        afterBattle(where, records, before);
        s_.removeDeadVehicles();
    }

    bool combatHere(Location where) const { return hooks_.possible && hooks_.resolve && hooks_.possible(r_, s_, where); }

    // A step into a sector where combat is possible: the battle is fought at
    // once and the order fails (spec 03 §6.2, §6.4; spec 04 §2). The sector of
    // an Attack order's target is fought by the order itself, after decloaking.
    bool entryCombat(Actor& a) {
        const Location here = where(a);
        if (attackGoal(a) == here || !combatHere(here)) return false;
        fight(here, entering(here));
        prune(a);
        if (!a.stopped)
            if (const std::vector<Order>* list = orders(a); list && !list->empty()) {
                fail(a, list->front(), "Combat on entering the sector.");
                clearOrders(a);
            }
        a.stopped = true;
        return true;
    }

    // An order carried out in a sector offers it to combat (spec 04 §2).
    void placeCombat(Actor& a) {
        prune(a);
        if (a.stopped) return;
        const Location here = where(a);
        if (combatHere(here)) fight(here, {});
    }

    bool onlyDrones(const Actor& a) const {
        for (VehicleId id : a.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v) && vehicleType(r_, s_, *v) != VehicleType::Drone) return false;
        return true;
    }

    // Visible enemy forces in a sector: vehicles other than mines, or colonies,
    // of an empire we are hostile to (inferred reading of "enemy ships").
    bool enemiesAt(EmpireId e, Location l) const {
        for (const Vehicle& v : s_.vehicles) {
            if (!alive(v) || v.location != l || v.owner == e || !hostile(s_, e, v.owner)) continue;
            if (vehicleType(r_, s_, v) == VehicleType::Mine) continue;
            if (sight::canSeeVehicle(r_, s_, e, v)) return true;
        }
        for (ObjectId o : planetsAt(s_, l))
            if (const Colony* c = s_.colony(o); c && c->owner != e && hostile(s_, e, c->owner)) return true;
        return false;
    }

    // Turn-based games: a human player's group stops before a sector with
    // enemy forces, and the player is asked (spec 03 §6.2). Groups of drones
    // always enter, and so does an Attack order into its target's sector.
    bool asks(Actor& a, Location next) {
        if (!live_ || !live_->ask || a.planet.valid() || onlyDrones(a)) return false;
        const EntryQuestion q{a.fleet.valid() ? VehicleId{} : a.lead, a.fleet, next};
        if (live_->allowed && *live_->allowed == q) return false;
        if (attackGoal(a) == next || !enemiesAt(a.owner, next)) return false;
        if (std::find(questions_.begin(), questions_.end(), q) == questions_.end()) questions_.push_back(q);
        return true;
    }

    TurnContext& ctx_;
    const Rules& r_;
    GameState& s_;
    const CombatHooks& hooks_;
    const LiveMove* live_ = nullptr;                    // turn-based: the move being carried out
    std::map<VehicleId, int> steps_;                    // turn-based: steps made this player turn
    std::vector<EntryQuestion> questions_;
    std::vector<Actor> actors_;
    size_t current_ = 0;                                // the actor acting now
    std::vector<Location> touched_;                     // sectors where something acted today
    std::vector<std::pair<size_t, Location>> entered_;  // (actor, sector) steps made today
    std::set<std::pair<VehicleId, Location>> foughtVehicles_;  // who fought where this turn
    std::set<std::pair<ObjectId, Location>> foughtPlanets_;
    std::map<VehicleId, int> bonus_;                    // emergency energy gained this turn
    UnitBudget budget_;
    std::map<VehicleId, int> startSpeed_;               // movement points when the phase began
    std::vector<Actor> split_;                          // actors that left an ad-hoc group today
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

namespace {

// Movement points back to the maximum: every vehicle, or one empire's.
void refillMovement(TurnContext& ctx, std::optional<EmpireId> only) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    for (Vehicle& v : s.vehicles)
        if (alive(v) && (!only || v.owner == *only)) v.movement = turnMovement(r, s, v);  // 0 while held by sabotage or an event
    // Fleet members in the fleet's sector get the lowest maximum among them (§6.3).
    for (const Fleet& f : s.fleets) {
        if (only && f.owner != *only) continue;
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

} // namespace

void startTurn(TurnContext& ctx) { refillMovement(ctx, std::nullopt); }

void startTurn(TurnContext& ctx, EmpireId empire) {
    refillMovement(ctx, empire);
    ctx.state.playerTurn.moves.clear();
    ctx.state.playerTurn.launched.clear();
}

std::vector<EntryQuestion> runLive(TurnContext& ctx, const LiveMove& move) { return runLive(ctx, move, defaultCombatHooks()); }

std::vector<EntryQuestion> runLive(TurnContext& ctx, const LiveMove& move, const CombatHooks& hooks) {
    Mover mover(ctx, hooks);
    mover.runLive(move);
    return mover.questions();
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
