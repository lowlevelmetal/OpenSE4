// The movement phase (spec 03 §6.3, §6.4, §8; spec 01 §7).
//
// Simultaneous games: a month of 30 days. Every ship, base and unit group has
// a day counter that gains its current movement points / 30 each day (the
// original's double, DayCounter); at 1 or more it acts and loses 1. Within a
// day objects act in object order (Vehicle::slot; planets first). The group
// that acts is rebuilt at every order execution: a fleet's members at its
// location, and ad-hoc companions with an identical head order (spec 03 §8).
// An action runs the order list with exactly 1 movement point; orders that
// complete chain into the next. After every day, each sector where something
// acted is offered to combat.
//
// Turn-based games (runLive): the groups a player sets in motion carry out
// their orders at once, spending movement points.

#include "game/movement.hpp"

#include "game/combat.hpp"
#include "game/combat_detail.hpp"
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
    bool due() const {
        if constexpr (kDayCounterMode == DayCounterMode::Exact) return exact_ >= kDaysPerTurn;
        else return value_ >= xmath::Ext(1);
    }
    // At 1 or more the vehicle acts, and 1 is taken off.
    bool take() {
        if (!due()) return false;
        if constexpr (kDayCounterMode == DayCounterMode::Exact) exact_ -= kDaysPerTurn;
        else value_ = (value_ - xmath::Ext(1)).roundedTo(xmath::kDoubleBits);
        return true;
    }
    // Emergency Energy adds V1, the same way (spec 03 §6.3, §8).
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

// Orders executed per vehicle in one action (spec 03 §6.3, §8, confirmed: binary).
constexpr int kChainLimit = 21;

enum class Exec {
    Done,       // completed without acting here: removed (kept at the end with Repeat); the chain goes on
    Acted,      // completed by acting here (the sector is offered to combat); the chain goes on
    ActedStay,  // acted here; the order stays and the action ends (a pursuit at its target)
    Removed,    // this order alone is removed, even with Repeat; the chain goes on
    Fail,       // failed: the whole list is cleared and Repeat switched off (§8)
    Moved,      // took a step; the order continues; the action ends
    MovedDone,  // took a step that completed the order (arrival, warp jump); the chain goes on
    Wait,       // the order stays; the action ends
    Cleared,    // the Ship Orders options cleared the lists (§6.4); the action ends
    Gone,       // the group is gone
};

// Arrived: already there (no step). Reached: a step that arrived.
// Asked: turn-based games only, the group stopped before a sector with
// enemies for the player's answer (spec 03 §6.2); the order waits.
// Encounter: a warp transit met another empire and the owner's Ship Orders
// option cleared the lists (§6.4). FighterWarp: the route needs a warp point
// and the group has fighters (§6.2).
enum class Travel { Arrived, Moved, Reached, Wait, Unreachable, Immobile, Busy, Stopped, Blocked, Encounter, Asked, FighterWarp };

bool validLocation(const GameState& s, Location l) {
    return l.system.valid() && l.system.index() < s.galaxy.systems.size() && l.sector.valid();
}

// What starts an action: a vehicle (alone, with its fleet or with ad-hoc
// companions) or a planet's own orders.
struct ActorRef {
    VehicleId vehicle;
    ObjectId planet;
};

// The group that carries out one order execution (spec 03 §8).
struct Group {
    std::vector<VehicleId> members;   // group order: a fleet's in member order, then ad-hoc companions
    ObjectId planet;                  // a planet's own orders
    EmpireId owner;
    VehicleId lead;                   // the acting vehicle (the first member left when it is gone)
    FleetId fleet;                    // the acting vehicle carries out this fleet's orders
    std::vector<FleetId> fleets;      // fleets whose lists the group carries out
    std::vector<VehicleId> own;       // members that carry out their own lists
    bool stopped = false;             // gone, or stopped by a hazard
    bool encountered = false;         // the last warp transit cleared the lists (§6.4)
};

class Mover {
public:
    Mover(TurnContext& ctx, const CombatHooks& hooks) : ctx_(ctx), r_(ctx.rules), s_(ctx.state), hooks_(hooks) {}

    // ---- Simultaneous games (spec 03 §6.3) ---------------------------------------------------------

    void run() {
        for (int day = 1; day <= kDaysPerTurn; ++day) {
            newDay();
            objectOrder_ = vehiclesInObjectOrder(s_);
            const std::vector<VehicleId> order = objectOrder_;
            // Colonized planets with orders act on day 1, where their slots are:
            // before every vehicle (inferred, Vehicle::slot).
            if (day == 1)
                for (const auto& c : s_.colonies)
                    if (c && !c->orders.empty() && inSystem(s_.galaxy, c->planet)) action(ActorRef{{}, c->planet});
            for (VehicleId id : order) {
                const Vehicle* v = s_.vehicle(id);
                if (!v || !alive(*v) || acted_.contains(id)) continue;
                if (dayOneOnly(*v)) {
                    // A vehicle with no movement (minefields, satellite groups, bases)
                    // that has orders acts on day 1 only (confirmed: binary).
                    if (day != 1) continue;
                } else if (!counters_[id].take()) {
                    continue;
                }
                acted_.insert(id);
                const std::vector<Order>* list = listOf(*v);
                if (!list || list->empty()) continue;
                action(ActorRef{id, {}});
            }
            resolveCombat();
            endPursuits();
        }
    }

    // ---- Turn-based games (spec 03 §6.3 "Turn-based", spec 04 §2) ---------------------------------

    // The selected groups of one empire carry out their orders now, action
    // after action, until each has spent its movement, waits, fails or has
    // nothing left. A group that steps into a sector where combat is possible
    // fights there at once and its order fails; one that carries out an order
    // in a sector offers it to combat without failing. The per-turn records
    // (steps, emergency movement, launches) live in GameState::playerTurn.
    void runLive(const LiveMove& m) {
        live_ = &m;
        budget_.turnBased = true;
        loadPlayerTurn();
        const bool all = m.vehicles.empty() && m.fleets.empty() && m.planets.empty();
        auto has = [](const auto& list, auto id) { return std::find(list.begin(), list.end(), id) != list.end(); };
        for (const auto& c : s_.colonies)
            if (c && c->owner == m.empire && !c->orders.empty() && inSystem(s_.galaxy, c->planet) && (all || has(m.planets, c->planet)))
                liveActor(ActorRef{{}, c->planet});
        std::set<FleetId> fleetsDone;
        objectOrder_ = vehiclesInObjectOrder(s_);
        const std::vector<VehicleId> order = objectOrder_;
        for (VehicleId id : order) {
            const Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v) || v->owner != m.empire) continue;
            if (followsFleetOrders(s_, *v)) {
                if (fleetsDone.contains(v->fleet) || !(all || has(m.fleets, v->fleet))) continue;
                fleetsDone.insert(v->fleet);
            } else if (!all && !has(m.vehicles, id)) {
                continue;
            }
            const std::vector<Order>* list = listOf(*v);
            if (!list || list->empty()) continue;
            liveActor(ActorRef{id, {}});
        }
        savePlayerTurn();
    }

    std::vector<EntryQuestion> questions() const { return questions_; }

private:
    // ---- Days and actions --------------------------------------------------------------------------

    // Before anyone acts: a maximum lowered between actions (combat damage) caps
    // the movement points; then every ship, base and unit group with movement
    // left adds its speed / 30 (spec 03 §6.1, §6.3).
    void newDay() {
        acted_.clear();
        for (Vehicle& v : s_.vehicles)
            if (alive(v)) v.movement = std::min(v.movement, turnMovement(r_, s_, v));
        std::vector<std::pair<VehicleId, int>> gains;
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.movement > 0) gains.emplace_back(v.id, daySpeed(v));
        for (const auto& [id, speed] : gains) counters_[id].newDay(speed);
    }

    // The speed of the day: the current movement points; for a fleet member in
    // the fleet's sector, the lowest among the members there (spec 03 §6.3).
    // A member elsewhere uses its own (inferred).
    int daySpeed(const Vehicle& v) const {
        if (const Fleet* f = v.fleet.valid() ? s_.fleet(v.fleet) : nullptr)
            if (const Vehicle* lead = fleetLeader(s_, *f); lead && lead->location == v.location) {
                int lowest = INT_MAX;
                for (VehicleId id : f->members)
                    if (const Vehicle* m = s_.vehicle(id); m && alive(*m) && m->location == v.location) lowest = std::min(lowest, m->movement);
                return lowest == INT_MAX ? v.movement : lowest;
            }
        return v.movement;
    }

    bool dayOneOnly(const Vehicle& v) const { return turnMovement(r_, s_, v) <= 0; }

    // One action of a simultaneous game: the chain of executions, then movement
    // points come back (spec 03 §6.3 step 4).
    void action(ActorRef ref) {
        Group last;
        act(ref, last);
        for (const auto& [id, before] : actionMovement_) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            // A maximum that fell below the points during the action keeps what
            // the action left, which stops the vehicle for the turn (§6.3, §6.4).
            const int max = turnMovement(r_, s_, *v);
            v->movement = max < before ? std::min(v->movement, max) : before;
        }
        actionMovement_.clear();
    }

    // A vehicle takes part in the action: exactly 1 movement point (none when it
    // has none left), and it does not act again that day; one that was due
    // loses 1 from its counter (spec 03 §6.3 steps 4-5).
    void join(VehicleId id) {
        participants_.insert(id);
        if (live_ || actionMovement_.contains(id)) return;
        Vehicle* v = s_.vehicle(id);
        if (!v) return;
        actionMovement_[id] = v->movement;
        v->movement = v->movement > 0 ? 1 : 0;
        if (acted_.insert(id).second) counters_[id].take();
    }

    // ---- Groups ------------------------------------------------------------------------------------

    const std::vector<Order>* listOf(const Vehicle& v) const {
        if (followsFleetOrders(s_, v))
            if (const Fleet* f = s_.fleet(v.fleet)) return &f->orders;
        return &v.orders;
    }

    // The group of one execution (spec 03 §8, confirmed: binary): the fleet's
    // members at its location, or the vehicle alone. A computer player's group
    // also takes every own vehicle in the sector whose head order is identical
    // (ships, bases, unit groups, fleet members, cloaked ones); a human
    // player's ships never group, only a drone group outside fleets gathers
    // the other drone groups there with the same head order. In a turn-based
    // game the vehicles the player moves together form the group.
    Group build(ActorRef ref) const {
        Group g;
        if (ref.planet.valid()) {
            const Colony* c = s_.colony(ref.planet);
            g.planet = ref.planet;
            g.owner = c ? c->owner : EmpireId{};
            g.stopped = !c || !inSystem(s_.galaxy, ref.planet);
            return g;
        }
        const Vehicle* v = s_.vehicle(ref.vehicle);
        if (!v || !alive(*v)) {
            g.stopped = true;
            return g;
        }
        g.owner = v->owner;
        g.lead = v->id;
        auto addFleet = [&](const Fleet& f) {
            if (std::find(g.fleets.begin(), g.fleets.end(), f.id) != g.fleets.end()) return;
            g.fleets.push_back(f.id);
            for (VehicleId id : f.members)
                if (const Vehicle* m = s_.vehicle(id); m && alive(*m) && followsFleetOrders(s_, *m) &&
                                                       std::find(g.members.begin(), g.members.end(), id) == g.members.end())
                    g.members.push_back(id);
        };
        auto addOwn = [&](VehicleId id) {
            g.members.push_back(id);
            g.own.push_back(id);
        };
        const bool fleetOrders = followsFleetOrders(s_, *v);
        if (fleetOrders) {
            g.fleet = v->fleet;
            addFleet(*s_.fleet(v->fleet));  // in fleet member order; the acting member is `lead`
        } else {
            addOwn(v->id);
        }
        const std::vector<Order>* list = listOf(*v);
        if (!list || list->empty()) return g;
        const Order head = list->front();
        auto joins = [&](const Vehicle& w) {
            return alive(w) && w.owner == v->owner && w.location == v->location && std::find(g.members.begin(), g.members.end(), w.id) == g.members.end();
        };
        if (live_ && !live_->vehicles.empty() && !computerPlayer(s_, v->owner)) {
            // Turn-based: the vehicles the player moves together.
            if (fleetOrders) return g;
            for (VehicleId id : live_->vehicles)
                if (const Vehicle* w = s_.vehicle(id); w && joins(*w) && !followsFleetOrders(s_, *w) && !w->orders.empty() && w->orders.front() == head)
                    addOwn(id);
            return g;
        }
        if (computerPlayer(s_, v->owner)) {
            for (VehicleId id : objectOrder_) {
                const Vehicle* w = s_.vehicle(id);
                if (!w || !joins(*w)) continue;
                if (followsFleetOrders(s_, *w)) {
                    const Fleet* f = s_.fleet(w->fleet);
                    if (f && !f->orders.empty() && f->orders.front() == head) addFleet(*f);
                } else if (!w->orders.empty() && w->orders.front() == head) {
                    addOwn(id);
                }
            }
        } else if (!fleetOrders && !v->fleet.valid() && vehicleType(r_, s_, *v) == VehicleType::Drone) {
            for (VehicleId id : objectOrder_) {
                const Vehicle* w = s_.vehicle(id);
                if (w && joins(*w) && !w->fleet.valid() && vehicleType(r_, s_, *w) == VehicleType::Drone && !w->orders.empty() &&
                    w->orders.front() == head)
                    addOwn(id);
            }
        }
        return g;
    }

    void prune(Group& g) {
        if (g.planet.valid()) {
            const Colony* c = s_.colony(g.planet);
            if (!c || c->owner != g.owner) g.stopped = true;
            return;
        }
        std::erase_if(g.members, [&](VehicleId id) {
            const Vehicle* v = s_.vehicle(id);
            return !v || !alive(*v) || v->owner != g.owner;
        });
        if (g.members.empty()) {
            g.stopped = true;
            return;
        }
        if (std::find(g.members.begin(), g.members.end(), g.lead) == g.members.end()) g.lead = g.members.front();
    }

    Location where(const Group& g) const {
        if (g.planet.valid()) return locationOf(s_.galaxy, g.planet);
        return s_.vehicle(g.lead)->location;
    }

    std::string name(const Group& g) const {
        if (g.planet.valid()) return s_.galaxy.object(g.planet).name;
        if (g.fleet.valid())
            if (const Fleet* f = s_.fleet(g.fleet)) return f->name;
        return s_.vehicle(g.lead)->name;
    }

    // Movement points left: the lowest among the members.
    int remaining(const Group& g) const {
        int left = INT_MAX;
        for (VehicleId id : g.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v)) left = std::min(left, v->movement);
        return left == INT_MAX ? 0 : left;
    }

    bool any(const Group& g, auto&& pred) const {
        for (VehicleId id : g.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v) && pred(*v)) return true;
        return false;
    }
    bool all(const Group& g, auto&& pred) const {
        for (VehicleId id : g.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v) && !pred(*v)) return false;
        return true;
    }
    bool held(const Group& g) const {
        return any(g, [&](const Vehicle& v) { return heldInPlace(s_, v); });
    }
    bool hasFighter(const Group& g) const {
        return any(g, [&](const Vehicle& v) { return vehicleType(r_, s_, v) == VehicleType::Fighter; });
    }
    bool onlyDrones(const Group& g) const {
        return all(g, [&](const Vehicle& v) { return vehicleType(r_, s_, v) == VehicleType::Drone; });
    }
    // A member with no movement at all freezes the group.
    bool immobile(const Group& g) const {
        return g.planet.valid() || any(g, [&](const Vehicle& v) { return turnMovement(r_, s_, v) + (live_ ? bonus(v.id) : 0) <= 0; });
    }
    // A ship whose space yard has items queued cannot move or warp (§6.2).
    bool yardBusy(const Group& g) const {
        return any(g, [&](const Vehicle& v) { return !v.queue.items.empty() && vehicleHasSpaceYard(r_, s_, v); });
    }
    bool neutral(const Group& g) const {
        return g.owner.valid() && g.owner.index() < s_.empires.size() && s_.empire(g.owner).kind == PlayerKind::Neutral;
    }
    // The Mine Sweeper exemption is tested on the group's first member: for a
    // fleet, the first one at its location in object order (spec 03 §6.2).
    bool sweeperLed(const Group& g) const {
        if (g.planet.valid() || g.members.empty()) return false;
        return leadsSweeperGroup(s_, sweeperOf(s_, *s_.vehicle(g.lead)));
    }
    int bonus(VehicleId id) const {
        const auto it = bonus_.find(id);
        return it == bonus_.end() ? 0 : it->second;
    }

    // The list the group executes: the planet's, the fleet's, or the acting vehicle's.
    std::vector<Order>* orders(const Group& g) {
        if (g.planet.valid()) {
            Colony* c = s_.colony(g.planet);
            return c ? &c->orders : nullptr;
        }
        if (g.fleet.valid()) {
            Fleet* f = s_.fleet(g.fleet);
            return f ? &f->orders : nullptr;
        }
        Vehicle* v = s_.vehicle(g.lead);
        return v ? &v->orders : nullptr;
    }
    // Every list a change to the head applies to, with its Repeat flag: each
    // member's (fleet copies included, spec 03 §8).
    template <class Fn>
    void forEachList(const Group& g, Fn&& fn) {
        if (g.planet.valid()) {
            if (Colony* c = s_.colony(g.planet)) fn(c->orders, false);
            return;
        }
        for (FleetId id : g.fleets)
            if (Fleet* f = s_.fleet(id)) fn(f->orders, f->repeatOrders);
        for (VehicleId id : g.own)
            if (Vehicle* v = s_.vehicle(id)) fn(v->orders, v->repeatOrders);
    }
    // Replaces every list of the group and switches Repeat off. A fleet
    // member's own list is cleared: in the original each member's list holds
    // the fleet's orders (spec 03 §8, §9), so clearing them all clears it too.
    void setLists(const Group& g, const std::vector<Order>& list) {
        routes_.erase(routeKey(g));
        if (g.planet.valid()) {
            if (Colony* c = s_.colony(g.planet)) c->orders = list;
            return;
        }
        for (FleetId id : g.fleets)
            if (Fleet* f = s_.fleet(id)) {
                f->orders = list;
                f->repeatOrders = false;
            }
        for (VehicleId id : g.members)
            if (Vehicle* v = s_.vehicle(id)) {
                const bool own = std::find(g.own.begin(), g.own.end(), id) != g.own.end();
                v->orders = own ? list : std::vector<Order>{};
                v->repeatOrders = false;
            }
    }
    // Clears the lists these vehicles execute (their fleets' when they follow them).
    void clearListsOf(std::span<const VehicleId> ids) {
        for (VehicleId id : ids) {
            Vehicle* v = s_.vehicle(id);
            if (!v) continue;
            if (followsFleetOrders(s_, *v))
                if (Fleet* f = s_.fleet(v->fleet)) {
                    f->orders.clear();
                    f->repeatOrders = false;
                    continue;
                }
            v->orders.clear();
            v->repeatOrders = false;
        }
    }

    // ---- The order loop ----------------------------------------------------------------------------

    static bool chains(Exec e) { return e == Exec::Done || e == Exec::Acted || e == Exec::Removed || e == Exec::MovedDone; }

    // One action: the head order is executed, and every order that completes
    // chains into the next, up to 21 executions, until one waits or fails
    // (spec 03 §6.3, §8, confirmed: binary). The group is rebuilt for every
    // execution. Turn-based games stop the chain after a step or an order that
    // acted, for the checks that follow them (runLive), and count the orders
    // completed into `completed`, stopping at 21 a run (kLiveOrderLimit).
    // `last` receives the last group.
    std::optional<Exec> act(ActorRef ref, Group& last, int* completed = nullptr) {
        std::optional<Exec> result;
        participants_.clear();
        for (int n = 0; n < kChainLimit; ++n) {
            Group g = build(ref);
            if (g.stopped) break;
            for (VehicleId id : g.members) join(id);
            std::vector<Order>* list = orders(g);
            if (!list || list->empty()) {
                last = g;
                break;
            }
            Order o = list->front();
            const Exec e = execute(g, o);
            prune(g);
            last = g;
            result = e;
            if (!g.stopped || e == Exec::Fail) settle(g, e, o);
            if (completed && chains(e) && ++*completed >= kLiveOrderLimit) break;
            if (g.stopped || !chains(e)) break;
            if (live_ && e != Exec::Done && e != Exec::Removed) break;
        }
        afterAction();
        return result;
    }

    void settle(Group& g, Exec e, const Order& o) {
        switch (e) {
            case Exec::Moved:
            case Exec::Wait: writeBack(g, o); break;
            case Exec::MovedDone:
            case Exec::Done: complete(g); break;
            case Exec::Acted:
                touched_.push_back(where(g));
                complete(g);
                break;
            case Exec::ActedStay:
                touched_.push_back(where(g));
                writeBack(g, o);
                break;
            case Exec::Removed: removeFront(g); break;
            case Exec::Fail: setLists(g, {}); break;
            case Exec::Cleared:
            case Exec::Gone: break;
        }
    }

    // After every daily action: the depot check (§7), and a cloak drops at 0
    // supply or when it can no longer work (§8).
    void afterAction() {
        for (VehicleId id : participants_) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            if (resupplyDepotAt(r_, s_, v->owner, v->location)) refillSupply(r_, s_, *v);
            if (v->status == VehicleStatus::Cloaked &&
                ((v->supply <= 0 && !vehicleHasUnlimitedSupply(r_, s_, *v)) || !canCloak(r_, s_, *v)))
                v->status = VehicleStatus::Normal;
        }
        participants_.clear();
    }

    // Orders that need no travel from where the vehicle is now.
    void writeBack(const Group& g, const Order& o) {
        forEachList(g, [&](std::vector<Order>& list, bool) {
            if (!list.empty()) list.front() = o;
        });
    }

    // Done: the order leaves the head; with Repeat on it goes to the end (§8).
    void complete(Group& g) {
        routes_.erase(routeKey(g));
        forEachList(g, [&](std::vector<Order>& list, bool repeat) {
            if (list.empty()) return;
            const Order done = list.front();
            list.erase(list.begin());
            if (repeat) list.push_back(done);
        });
    }

    void removeFront(Group& g) {
        routes_.erase(routeKey(g));
        forEachList(g, [&](std::vector<Order>& list, bool) {
            if (!list.empty()) list.erase(list.begin());
        });
    }

    Exec fail(const Group& g, const Order& o, std::string_view why) {
        ctx_.log(g.owner, LogCategory::Misc, std::format("{}: {} order cancelled", name(g), displayName(o.kind)), std::string(why), where(g));
        return Exec::Fail;
    }

    Exec afterTravel(Group& g, const Order& o, Travel t) {
        switch (t) {
            case Travel::Moved:
            case Travel::Reached: return Exec::Moved;
            case Travel::Wait: return Exec::Wait;
            case Travel::Unreachable: return fail(g, o, "No known route to the destination.");
            case Travel::Immobile: return fail(g, o, "It cannot move.");
            case Travel::Busy: return fail(g, o, "Its space yard is building; it cannot move.");
            case Travel::Stopped: return fail(g, o, "Movement stopped by a hazard.");
            case Travel::Blocked: return fail(g, o, "The way is blocked.");
            case Travel::FighterWarp: return fail(g, o, "Fighters cannot use warp points.");
            case Travel::Encounter: return encounter(g, o, false);
            case Travel::Asked: return Exec::Wait;
            case Travel::Arrived: break;
        }
        return Exec::Done;
    }

    // The owner's Ship Orders option cleared the group's lists after a warp
    // transit (spec 03 §6.4, confirmed: binary). The jump itself does not
    // fail: in a turn-based game a Move To in progress goes on stepping to
    // its end while movement lasts; otherwise the action ends there.
    Exec encounter(Group& g, const Order& o, bool arrived) {
        ctx_.log(g.owner, LogCategory::Misc, std::format("{}: orders cleared", name(g)),
                 "Another empire is in the system; the orders were cleared (empire options).", where(g));
        if (live_ && (o.kind == OrderKind::MoveTo || o.kind == OrderKind::MoveToWaypoint) && !arrived) {
            setLists(g, {o});
            return Exec::Moved;
        }
        setLists(g, {});
        return Exec::Cleared;
    }

    // ---- Moving ------------------------------------------------------------------------------------

    using RouteKey = std::pair<int, uint32_t>;
    struct Route {
        Location goal{}, at{};
        RouteOptions options;
        std::vector<Location> steps;
        size_t pos = 0;
    };

    static RouteKey routeKey(const Group& g) {
        if (g.planet.valid()) return {2, g.planet.value};
        return g.fleet.valid() ? RouteKey{0, g.fleet.value} : RouteKey{1, g.lead.value};
    }

    RouteOptions routeOptions(const Group& g) const {
        RouteOptions options;
        // Fighters travel to the warp point and fail there (spec 03 §6.2); neutral empires never warp.
        options.allowWarp = !neutral(g);
        options.sweeper = sweeperLed(g);
        return options;
    }

    Route* ensureRoute(const Group& g, Location goal) {
        const Location here = where(g);
        const RouteOptions options = routeOptions(g);
        Route& rt = routes_[routeKey(g)];
        if (rt.goal == goal && rt.at == here && rt.options == options && rt.pos < rt.steps.size()) return &rt;
        const Location goals[] = {goal};
        auto p = findPathToNearest(r_, s_, g.owner, here, goals, options);
        if (!p || p->path.steps.empty()) {
            routes_.erase(routeKey(g));
            return nullptr;
        }
        rt = Route{goal, here, options, std::move(p->path.steps), 0};
        return &rt;
    }

    // Can the group take a step now? (Arrival and routes are checked by the callers.)
    std::optional<Travel> readyToStep(Group& g) {
        // Turn-based: a maximum that dropped (engines lost in a battle) caps the movement left at once (§6.1).
        if (live_)
            for (VehicleId id : g.members) capLive(id);
        if (immobile(g)) return held(g) ? Travel::Wait : Travel::Immobile;
        if (yardBusy(g)) return Travel::Busy;
        if (remaining(g) <= 0) return Travel::Wait;
        return std::nullopt;
    }

    // One step toward `goal`.
    Travel travel(Group& g, Location goal) {
        g.encountered = false;
        if (!validLocation(s_, goal)) return Travel::Unreachable;
        if (where(g) == goal) return Travel::Arrived;
        if (auto t = readyToStep(g)) return *t;
        Route* rt = ensureRoute(g, goal);
        if (!rt) return Travel::Unreachable;
        Step st = step(g, *rt);
        if (st == Step::Stale) {
            rt = ensureRoute(g, goal);
            st = rt ? step(g, *rt) : Step::Stale;
        }
        if (st == Step::Stale) return Travel::Unreachable;
        if (st == Step::Blocked) return Travel::Blocked;
        if (st == Step::Asked) return Travel::Asked;
        if (st == Step::NoWarp) return Travel::FighterWarp;
        prune(g);
        if (g.stopped) return Travel::Stopped;
        if (g.encountered) return Travel::Encounter;
        return where(g) == goal ? Travel::Reached : Travel::Moved;
    }

    // A Move To is done on the step that arrives (§8).
    Exec moveTo(Group& g, const Order& o, Location goal) {
        const Travel t = travel(g, goal);
        if (t == Travel::Reached) return Exec::MovedDone;
        if (t == Travel::Encounter) return encounter(g, o, where(g) == goal);
        return afterTravel(g, o, t);
    }

    // Asked: turn-based games, the player is asked before the group enters (spec 03 §6.2).
    enum class Step { Moved, Stale, Blocked, Asked, NoWarp };

    // A square an in-system step avoids (spec 03 §6.2, confirmed: binary): a
    // tagged minefield (the owner's option on; no Mine Sweeper exemption inside
    // a system), a sector whose objects have `Sector - Damage` (not the
    // system's own value), or one holding an object of an empire the mover is
    // hostile to that the mover sees: a ship, base, unit group (a mine only if
    // seen) or colony (only if seen).
    bool badSquare(const Group& g, Location l) const {
        const Empire& e = s_.empire(g.owner);
        if (e.avoidTaggedMinefields && std::find(e.taggedMinefields.begin(), e.taggedMinefields.end(), l) != e.taggedMinefields.end())
            return true;
        for (ObjectId o : s_.galaxy.system(l.system).objects) {
            const SpaceObject& obj = s_.galaxy.object(o);
            if (obj.sector != l.sector) continue;
            if (rawSum(obj.abilities, AbilityKind::SectorDamage) > 0) return true;
            if (const Colony* c = s_.colony(o); c && hostile(s_, g.owner, c->owner) && sight::canSeePlanet(r_, s_, g.owner, o)) return true;
        }
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location == l && hostile(s_, g.owner, v.owner) && sight::canSeeVehicle(r_, s_, g.owner, v)) return true;
        return false;
    }

    // In-system steps are greedy: one square toward `target`, diagonal first.
    // Unless it is the target it is tested; a bad square is replaced by a
    // random one drawn from the current square (after a diagonal first step,
    // one of the two straight steps; after a straight one, the forward square
    // or either square beside it), coordinates clamped to the grid, and every
    // replacement is tested, the target too. After the 10th bad test the group
    // stays and the order fails (spec 03 §6.2, confirmed: binary).
    std::optional<Sector> greedyStep(const Group& g, Location here, Sector target) {
        const int x = here.sector.x, y = here.sector.y;
        const int dx = (target.x > x) - (target.x < x), dy = (target.y > y) - (target.y < y);
        auto clamp = [](int c) { return std::clamp(c, 0, kSystemSize - 1); };
        Sector pick{x + dx, y + dy};
        if (pick == target) return pick;
        for (int bad = 0;;) {
            if (!badSquare(g, {here.system, pick})) return pick;
            if (++bad >= 10) return std::nullopt;
            if (dx != 0 && dy != 0) pick = s_.rng.below(2) == 0 ? Sector{x + dx, y} : Sector{x, y + dy};
            else if (dx != 0) pick = Sector{x + dx, clamp(y + static_cast<int>(s_.rng.below(3)) - 1)};
            else pick = Sector{clamp(x + static_cast<int>(s_.rng.below(3)) - 1), y + dy};
        }
    }

    // The square an in-system step goes to: around a destructive centre the
    // cost map chooses, falling back to the greedy step when no square around
    // the group was reached (spec 03 §6.2, confirmed: binary).
    std::optional<Sector> inSystemStep(const Group& g, Location here, Sector target) {
        if (destructiveCentre(s_, here.system) > 0)
            if (const auto s = centreStep(centreCostMap(target, centreZone(s_, here.system)), here.sector); s && *s != here.sector) return s;
        return greedyStep(g, here, target);
    }

    Step step(Group& g, Route& rt) {
        const Location here = where(g);
        const Location next = rt.steps[rt.pos];
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
                routes_.erase(routeKey(g));  // the link is gone
                return Step::Stale;
            }
            // A group with fighters travels to the warp point and fails there (§6.2).
            if (hasFighter(g)) return Step::NoWarp;
            ++rt.pos;
            rt.at = next;
            moveMembers(g, next, via);
            return Step::Moved;
        }
        // The route picks the warp points; inside a system the group heads for
        // the last square of this system on it (a warp point or the goal).
        size_t last = rt.pos;
        while (last + 1 < rt.steps.size() && rt.steps[last + 1].system == here.system) ++last;
        const auto chosen = inSystemStep(g, here, rt.steps[last].sector);
        if (!chosen) return Step::Blocked;
        const Location to{here.system, *chosen};
        if (asks(g, to)) return Step::Asked;
        if (to == next) {
            ++rt.pos;
            rt.at = to;
        } else {
            rt.steps.clear();  // off the planned squares: plan again from there
        }
        moveMembers(g, to, {});
        return Step::Moved;
    }

    // An explicit jump through a warp point, known link or not (Warp). Warp
    // jumps never ask (spec 03 §6.2).
    Travel jump(Group& g, ObjectId w) {
        g.encountered = false;
        const SpaceObject& wp = s_.galaxy.object(w);
        if (!wp.destination.valid() || !inSystem(s_.galaxy, wp.destination)) return Travel::Unreachable;
        if (auto t = readyToStep(g)) return *t;
        routes_.erase(routeKey(g));
        moveMembers(g, locationOf(s_.galaxy, wp.destination), w);
        prune(g);
        if (g.stopped) return Travel::Stopped;
        return g.encountered ? Travel::Encounter : Travel::Moved;
    }

    // The owner's Ship Orders options (spec 03 §6.4, confirmed: binary): after
    // a warp transit into a system where the owner sees an object of an enemy
    // empire (treaty below Non-Aggression), or with the second option of any
    // other empire: ships, bases, unit groups and colonies, each only if seen.
    bool encounterClearsOrders(EmpireId e, SystemId sys) const {
        const EncounterClear option = s_.empire(e).clearOrdersOnEncounter;
        if (option == EncounterClear::Never) return false;
        auto applies = [&](EmpireId other) {
            if (!other.valid() || other == e) return false;
            return option == EncounterClear::Any || hostile(s_, e, other);
        };
        for (ObjectId o : s_.galaxy.system(sys).objects)
            if (const Colony* c = s_.colony(o); c && applies(c->owner) && sight::canSeePlanet(r_, s_, e, o)) return true;
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

    // Damages every member; the group stops and its order fails.
    void hazardHit(Group& g, int64_t damage, std::string_view cause) {
        for (VehicleId id : std::vector<VehicleId>(g.members)) hurt(ctx_, id, static_cast<int>(std::min<int64_t>(damage, INT_MAX)), cause);
        for (VehicleId id : g.members)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v))
                ctx_.log(g.owner, LogCategory::Events, std::format("{} damaged", v->name), std::string(cause), v->location);
        g.stopped = true;
    }

    void moveMembers(Group& g, Location next, ObjectId via) {
        for (VehicleId id : g.members) {
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
        entered_.push_back(Entry{g.members, g.owner, next, name(g)});
        // A drone out of supply is destroyed after each step or warp (§12, confirmed: binary).
        for (VehicleId id : g.members)
            if (Vehicle* v = s_.vehicle(id); v && alive(*v) && v->supply <= 0 && vehicleType(r_, s_, *v) == VehicleType::Drone)
                vehicleLost(ctx_, *v, "Ran out of supplies.");
        prune(g);
        if (g.members.empty()) return;
        // A sweeper group entering a tagged minefield where mines act decloaks first (§12).
        decloakSweepers(r_, s_, next, g.members);
        if (via.valid()) {
            for (size_t i = 0; i < g.members.size(); ++i) sight::learnWarpLink(s_, g.owner, via);
            // Turbulence: a 50 % chance per transit that every member takes the
            // total of the warp point it leaves; the group arrives but its order fails (confirmed: binary).
            const int64_t turbulence = rawSum(s_.galaxy.object(via).abilities, AbilityKind::WarpPointTurbulence);
            const bool shaken = turbulence > 0 && s_.rng.percent(50);
            if (shaken) hazardHit(g, turbulence, "Damaged by warp point turbulence.");
            prune(g);
            if (g.members.empty()) return;
            // The Ship Orders options apply only after a transit that met no other
            // trouble (turbulence, mines, turn-based combat), never to drone-only
            // groups (§6.4, confirmed: binary).
            const bool trouble = shaken || combat::detail::minesCanStrike(r_, s_, next, g.members) || (live_ && combatHere(next));
            if (!trouble && !onlyDrones(g) && encounterClearsOrders(g.owner, next.system)) g.encountered = true;
        } else if (const int64_t storm = stormDamageAt(next); storm > 0 && s_.rng.percent(50)) {
            // A storm: a 50 % chance for a group stepping in; it stops. A warp
            // arrival makes no storm roll (confirmed: binary).
            hazardHit(g, storm, "Damaged by a storm.");
            prune(g);
            if (g.members.empty()) return;
        }
        arrive(g);
    }

    void arrive(Group& g) {
        const Location here = where(g);
        sight::markExplored(s_, g.owner, here.system);
        Knowledge& k = s_.empire(g.owner).knowledge;
        if (here.system.index() < k.present.size()) k.present[here.system.index()] = 1;
        for (VehicleId id : g.members) {
            Vehicle* v = s_.vehicle(id);
            if (v && alive(*v) && resupplyDepotAt(r_, s_, v->owner, here)) refillSupply(r_, s_, *v);  // passing a depot refuels
        }
        if (live_)
            for (VehicleId id : g.members) capLive(id);
        touched_.push_back(here);
    }

    // Turn-based games: engines lost or supply exhausted mid-turn cap the
    // remaining movement at the new maximum (with the turn's emergency
    // movement), less the steps made (spec 03 §6.1, §6.4).
    void capLive(VehicleId id) {
        Vehicle* v = s_.vehicle(id);
        if (!v || !alive(*v)) return;
        const int max = turnMovement(r_, s_, *v) + (heldInPlace(s_, *v) ? 0 : bonus(id));
        v->movement = std::min(v->movement, std::max(0, max - stepsOf(id)));
    }

    // ---- Orders ---------------------------------------------------------------------------------------

    Exec execute(Group& g, Order& o) {
        if (g.planet.valid()) {
            if (o.kind == OrderKind::LaunchUnits || o.kind == OrderKind::RecoverUnits) return cargo(g, o);
            return fail(g, o, "Planets cannot carry out that order.");
        }
        switch (o.kind) {
            case OrderKind::MoveTo: return moveTo(g, o, o.location);
            case OrderKind::MoveToWaypoint: {
                const Empire& e = s_.empire(g.owner);
                if (o.amount < 0 || static_cast<size_t>(o.amount) >= e.waypoints.size() || !e.waypoints[static_cast<size_t>(o.amount)].set)
                    return fail(g, o, "The waypoint is not set.");
                return moveTo(g, o, e.waypoints[static_cast<size_t>(o.amount)].location);
            }
            case OrderKind::Warp: return warp(g, o);
            case OrderKind::Attack: return attack(g, o);
            case OrderKind::Resupply:
            case OrderKind::Repair:
            case OrderKind::Explore: return expandHead(g, o);
            case OrderKind::Colonize: return colonize(g, o);
            case OrderKind::Sentry: return sentry(g);
            case OrderKind::LoadCargo:
            case OrderKind::DropCargo:
            case OrderKind::LaunchUnits:
            case OrderKind::RecoverUnits: return cargo(g, o);
            case OrderKind::Cloak:
            case OrderKind::Decloak: return cloak(g, o);
            case OrderKind::SweepMines: return sweep(g);
            case OrderKind::UseComponent: return useComponentOrder(g, o);
            case OrderKind::StellarManipulation: return stellar(g, o);
            case OrderKind::SelfDestruct: return selfDestruct(g, o);
            case OrderKind::Count: break;
        }
        return fail(g, o, "Unknown order.");
    }

    // A vehicle or colony of an empire whose treaty with `e` is below
    // Non-Aggression, anywhere in the system, that `e` sees: ships, bases,
    // unit groups (mines only if seen) and colonies (only if seen) (spec 03 §8
    // Sentry, confirmed: binary).
    bool hostilePresentInSystem(EmpireId e, SystemId sys) const {
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location.system == sys && hostile(s_, e, v.owner) && sight::canSeeVehicle(r_, s_, e, v)) return true;
        for (ObjectId o : s_.galaxy.system(sys).objects)
            if (const Colony* c = s_.colony(o); c && hostile(s_, e, c->owner) && sight::canSeePlanet(r_, s_, e, o)) return true;
        return false;
    }

    // Sentry stays at the head at no cost until an enemy is present in the
    // system or a member's supply is low; then it counts as done: removed, or
    // with Repeat on passed over (§8, confirmed: binary).
    Exec sentry(Group& g) {
        const int64_t low = r_.setting("Supply Amount for Low Supply Warning", 1000);
        const bool lowSupply = any(g, [&](const Vehicle& v) {
            if (!vehicleUsesSupply(r_, s_, v) || vehicleHasUnlimitedSupply(r_, s_, v)) return false;
            const int64_t threshold = vehicleType(r_, s_, v) == VehicleType::Fighter ? low / 10 : low;
            return v.supply < threshold;
        });
        const bool enemy = hostilePresentInSystem(g.owner, where(g).system);
        if (!lowSupply && !enemy) return Exec::Wait;
        ctx_.log(g.owner, LogCategory::Combat, std::format("{}: {}", name(g), enemy ? "enemy sighted" : "supplies low"), "Sentry duty ended.",
                 where(g));
        return Exec::Done;
    }

    Exec warp(Group& g, Order& o) {
        if (hasFighter(g)) return fail(g, o, "Fighters cannot use warp points.");
        if (neutral(g)) return fail(g, o, "Neutral empires cannot use warp points.");
        if (!o.object.valid() || o.object.index() >= s_.galaxy.objects.size() || s_.galaxy.object(o.object).kind != ObjectKind::WarpPoint ||
            !inSystem(s_.galaxy, o.object))
            return fail(g, o, "The warp point no longer exists.");
        const SpaceObject& wp = s_.galaxy.object(o.object);
        if (!wp.destination.valid() || !inSystem(s_.galaxy, wp.destination)) return fail(g, o, "The warp point leads nowhere.");
        const Travel t = travel(g, locationOf(s_.galaxy, o.object));
        if (t != Travel::Arrived) return afterTravel(g, o, t);
        const Travel j = jump(g, o.object);
        if (j == Travel::Moved) return Exec::MovedDone;
        if (j == Travel::Encounter) {
            // A Warp order just ends with the cleared lists (§6.4).
            setLists(g, {});
            ctx_.log(g.owner, LogCategory::Misc, std::format("{}: orders cleared", name(g)),
                     "Another empire is in the system; the orders were cleared (empire options).", where(g));
            return Exec::Cleared;
        }
        return afterTravel(g, o, j);
    }

    // Attack (§8, confirmed: binary): a pursuit that moves toward the target's
    // current sector, warping as needed, and attacks once there (1 movement
    // point and one move's supply; cloaked drones decloak first); the battle
    // comes from the day's combat check and the order stays. It is done when
    // the target is gone, the attacker's owner's, or a planet without colony.
    // In a turn-based game a group that is not all drones attacks at once
    // where it stands, decloaking (§6.4).
    Exec attack(Group& g, Order& o) {
        if (pursuitOver(s_, g.owner, o)) {
            ctx_.log(g.owner, LogCategory::Combat, std::format("{}: target gone", name(g)), {}, where(g));
            return Exec::Done;
        }
        // A drone sent at a warp point goes through it (an order given before
        // the expansion, orders.hpp).
        if (o.object.valid() && s_.galaxy.object(o.object).kind == ObjectKind::WarpPoint) return warp(g, o);
        const Location goal = o.vehicle.valid() ? s_.vehicle(o.vehicle)->location : locationOf(s_.galaxy, o.object);
        const Travel t = travel(g, goal);
        if (t == Travel::Reached) return Exec::Moved;  // the attack needs the next action's movement
        if (t != Travel::Arrived) return afterTravel(g, o, t);
        if (remaining(g) <= 0 || immobile(g)) return Exec::Wait;
        const bool pursuit = !live_ || onlyDrones(g);
        for (VehicleId id : g.members) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            const bool drone = vehicleType(r_, s_, *v) == VehicleType::Drone;
            if ((drone || !pursuit) && v->status == VehicleStatus::Cloaked) v->status = VehicleStatus::Normal;
            if (drone) {
                // Its target in the battle (spec 04 §10.7).
                v->targetVehicle = o.vehicle;
                v->targetObject = o.object;
            }
            v->movement = std::max(0, v->movement - 1);
            spendSupply(r_, s_, *v, moveSupplyCost(r_, s_, *v));
        }
        return Exec::ActedStay;
    }

    // Explore, Resupply and Repair are expanded when they are given (orders.hpp);
    // one that reached a list another way is expanded when it comes up, and the
    // first of the orders it stands for runs at once. With nothing to go to it is
    // removed.
    Exec expandHead(Group& g, Order& o) {
        OrderContext ctx;
        ctx.owner = g.owner;
        ctx.members = g.members;
        ctx.lead = g.lead;
        ctx.at = where(g);
        ctx.carriesPopulation = any(g, [](const Vehicle& v) { return v.cargo.totalPopulation() > 0; });
        std::vector<Order> expanded;
        expandOrder(r_, s_, ctx, o, expanded);
        if (expanded.empty()) {
            const char* why = o.kind == OrderKind::Explore    ? "nothing left to explore"
                              : o.kind == OrderKind::Resupply ? "no reachable resupply depot"
                                                              : "no reachable repair facility";
            ctx_.log(g.owner, LogCategory::Misc, std::format("{}: {}", name(g), why), {}, where(g));
            return Exec::Removed;
        }
        forEachList(g, [&](std::vector<Order>& list, bool) {
            if (list.empty()) return;
            list.erase(list.begin());
            list.insert(list.begin(), expanded.begin(), expanded.end());
        });
        o = expanded.front();
        return execute(g, o);
    }

    // Colonize (§8): fails while a member is cloaked or when nobody can colonize
    // the planet; the colonizer is the last suitable member (confirmed: binary).
    Exec colonize(Group& g, Order& o) {
        if (any(g, [](const Vehicle& v) { return v.status == VehicleStatus::Cloaked; })) return fail(g, o, "A cloaked ship cannot colonize.");
        VehicleId colonizer;
        std::string why;
        for (VehicleId id : g.members) {
            const std::string p = colonizeProblem(r_, s_, *s_.vehicle(id), o.object);
            if (p.empty()) colonizer = id;
            else if (why.empty()) why = p;
        }
        if (!colonizer.valid()) return fail(g, o, why);
        if (o.amount == 0) {
            loadColonists(ctx_, colonizer);  // colonists come aboard where the order starts (spec 03 §8)
            o.amount = 1;
        }
        const Travel t = travel(g, locationOf(s_.galaxy, o.object));
        if (t != Travel::Arrived) return afterTravel(g, o, t);
        // Carried out like any order, on an acting day with movement left, so
        // the colony exists during the later phases; without movement it
        // waits (spec 05 §8 step 5, open question 24; spec 03 §8).
        if (remaining(g) <= 0) return Exec::Wait;
        foundColony(ctx_, colonizer, o.object);
        return Exec::Done;
    }

    // Load, Launch and Recover are always done; Drop can fail (§8, confirmed: binary).
    Exec cargo(Group& g, Order& o) {
        if (!g.planet.valid() && o.location.system.valid()) {
            const Travel t = travel(g, o.location);
            if (t != Travel::Arrived) return afterTravel(g, o, t);
        }
        int64_t moved = 0;
        auto part = [&]() {
            Order p = o;
            if (o.amount >= 0) p.amount = static_cast<int>(std::max<int64_t>(0, o.amount - moved));
            return p;
        };
        if (g.planet.valid()) {
            const Launcher from{{}, g.planet};
            moved = o.kind == OrderKind::LaunchUnits ? launchUnits(ctx_, budget_, from, o) : recoverUnits(ctx_, from, o);
        } else {
            for (VehicleId id : std::vector<VehicleId>(g.members)) {
                const Order p = part();
                if (o.amount >= 0 && p.amount <= 0) break;
                switch (o.kind) {
                    case OrderKind::LoadCargo: moved += loadCargo(ctx_, id, o.design, p.amount, g.members); break;
                    case OrderKind::DropCargo: moved += dropCargo(ctx_, id, o.design, p.amount, g.members); break;
                    case OrderKind::LaunchUnits: moved += launchUnits(ctx_, budget_, Launcher{id, {}}, p); break;
                    case OrderKind::RecoverUnits: moved += recoverUnits(ctx_, Launcher{id, {}}, p); break;
                    default: break;
                }
            }
        }
        if (o.kind == OrderKind::DropCargo && moved <= 0) return fail(g, o, "Nothing could be dropped here.");
        if (moved > 0 && (o.kind == OrderKind::LaunchUnits || o.kind == OrderKind::RecoverUnits)) {
            const bool byKind = o.kind == OrderKind::RecoverUnits && !o.vehicle.valid();
            const std::string what = byKind ? std::string("units") : s_.design(o.design).name;
            ctx_.log(g.owner, LogCategory::Misc, std::format("{} {} {} {}", name(g), o.kind == OrderKind::LaunchUnits ? "launched" : "recovered", moved, what),
                     {}, where(g));
        }
        return Exec::Acted;
    }

    // Cloaking needs a working part of level 2 or more and supply above 0; it
    // costs nothing now (the parts' supply is charged every end of turn) (§8).
    Exec cloak(Group& g, Order& o) {
        bool changed = false;
        for (VehicleId id : g.members) {
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
        return changed ? Exec::Acted : fail(g, o, "No working cloaking device, or no supplies.");
    }

    // Sweep Mines (§8, §12, confirmed: binary): the whole mine encounter of the
    // sector runs again for the group (a sweeper group in a tagged minefield
    // decloaks, its uncloaked members sweep, the remaining hostile mines
    // strike); then, if the group survived, 1 movement point (if any is left)
    // and one move's supply. Always done.
    Exec sweep(Group& g) {
        const Location here = where(g);
        decloakSweepers(r_, s_, here, g.members);
        combat::detail::resolveMines(ctx_, here, g.members, s_.rng);
        prune(g);
        for (VehicleId id : g.members)
            if (Vehicle* v = s_.vehicle(id); v && alive(*v)) {
                v->movement = std::max(0, v->movement - 1);
                spendSupply(r_, s_, *v, moveSupplyCost(r_, s_, *v));
                if (live_) capLive(id);
            }
        return g.stopped ? Exec::Gone : Exec::Acted;
    }

    Exec useComponentOrder(Group& g, Order& o) {
        int gained = -1;
        for (VehicleId id : std::vector<VehicleId>(g.members)) {
            const int n = useComponent(ctx_, id, o.amount);
            if (n < 0) continue;
            gained = std::max(gained, n);
            if (n <= 0 || heldInPlace(s_, *s_.vehicle(id))) continue;
            // Emergency energy (spec 03 §8, confirmed: binary): in a turn-based
            // game V1 more movement this turn, without a cap; in a simultaneous
            // one V1 on the day counter, up to V1 more actions at one a day.
            if (live_) {
                s_.vehicle(id)->movement += n;
                bonus_[id] += n;
            } else {
                counters_[id].add(n);
            }
        }
        if (gained < 0) return fail(g, o, "No usable component.");
        prune(g);
        return g.stopped ? Exec::Gone : Exec::Acted;
    }

    // Self-Destruct (spec 03 §8, §15): every member that can is destroyed.
    Exec selfDestruct(Group& g, Order& o) {
        bool any = false;
        for (VehicleId id : std::vector<VehicleId>(g.members)) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v) || !canSelfDestruct(r_, s_, *v)) continue;
            vehicleLost(ctx_, *v, "It self-destructed.");
            any = true;
        }
        if (!any) return fail(g, o, "It cannot self-destruct.");
        prune(g);
        return g.stopped ? Exec::Gone : Exec::Acted;
    }

    Exec stellar(Group& g, Order& o) {
        const auto target = stellarTarget(s_, o, where(g));
        if (!target) return fail(g, o, "The target no longer exists.");
        const Travel t = travel(g, *target);
        if (t != Travel::Arrived) return afterTravel(g, o, t);
        // Movement is needed but not spent; a refusal for lack of it makes the
        // order wait (spec 03 §8, simultaneous games). Construct needs none.
        const auto action = static_cast<StellarAction>(std::clamp(o.amount, 0, static_cast<int>(StellarAction::Count)));
        if (action != StellarAction::CreateConstructedPlanet && action != StellarAction::Count) {
            const AbilityKind k = stellarAbility(action);
            bool capable = false, ready = false;
            for (VehicleId id : g.members) {
                const Vehicle* v = s_.vehicle(id);
                if (!v || !hasAbility(vehicleAbilities(r_, s_, *v), k)) continue;
                capable = true;
                ready = ready || v->movement > 0;
            }
            if (capable && !ready) return Exec::Wait;
        }
        bool consumed = false;
        const std::string why = stellarManipulation(ctx_, g.members, o, consumed);
        if (!why.empty()) return fail(g, o, why);
        routes_.clear();  // the map may have changed
        prune(g);
        return g.stopped ? Exec::Gone : Exec::Acted;
    }

    static AbilityKind stellarAbility(StellarAction a) {
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

    // After each day, pursuits whose target is gone are removed (spec 03 §6.3 step 7).
    void endPursuits() {
        auto finished = [&](EmpireId owner, const std::vector<Order>& list) {
            return !list.empty() && list.front().kind == OrderKind::Attack && pursuitOver(s_, owner, list.front());
        };
        for (Vehicle& v : s_.vehicles)
            if (alive(v) && finished(v.owner, v.orders)) v.orders.erase(v.orders.begin());
        for (Fleet& f : s_.fleets)
            if (finished(f.owner, f.orders)) f.orders.erase(f.orders.begin());
    }

    // ---- Combat ---------------------------------------------------------------------------------------

    // One step into a sector: the group's members as they entered.
    struct Entry {
        std::vector<VehicleId> members;
        EmpireId owner;
        Location where;
        std::string name;
    };

    // The latest battle at a location this turn: its survivors by owner and
    // name, and whether one of them was below full structure (spec 03 §6.3 step 6).
    struct BattleMemo {
        bool damaged = false;
        std::map<EmpireId, std::set<std::string>> survivors;
    };

    void rememberBattle(Location where, size_t recordsBefore) {
        const CombatRecord* rec = nullptr;
        for (size_t i = recordsBefore; i < s_.combats.size(); ++i)
            if (s_.combats[i].location == where) rec = &s_.combats[i];
        if (!rec) return;
        BattleMemo memo;
        for (const CombatPiece& p : rec->pieces) {
            if (p.vehicle.valid()) {
                const Vehicle* v = s_.vehicle(p.vehicle);
                if (!v || !alive(*v) || v->location != where) continue;
                memo.survivors[v->owner].insert(v->name);
                if (vehicleDamageTaken(s_, *v) > 0) memo.damaged = true;  // damage from before the battle counts
            } else if (p.planet.valid()) {
                if (const Colony* c = s_.colony(p.planet)) memo.survivors[c->owner].insert(s_.galaxy.object(p.planet).name);
            }
        }
        lastBattle_[where] = std::move(memo);
    }

    // A sector that already had a battle this turn is fought again only when a
    // survivor of the latest one was damaged, or an object owned by a player
    // there (ships, bases, unit groups including minefields, colonies) is not
    // on its owner's list of that battle's survivors, by name (confirmed: binary).
    bool freshBattle(Location where) const {
        const auto it = lastBattle_.find(where);
        if (it == lastBattle_.end()) return true;
        const BattleMemo& memo = it->second;
        if (memo.damaged) return true;
        auto listed = [&](EmpireId owner, const std::string& name) {
            const auto o = memo.survivors.find(owner);
            return o != memo.survivors.end() && o->second.contains(name);
        };
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location == where && v.owner.valid() && !listed(v.owner, v.name)) return true;
        for (ObjectId o : planetsAt(s_, where))
            if (const Colony* c = s_.colony(o); c && c->owner.valid() && !listed(c->owner, s_.galaxy.object(o).name)) return true;
        return false;
    }

    // The members of the groups that stepped into `where` today and are still
    // there: the groups hostile mines strike (spec 04 §10.6).
    std::vector<VehicleId> entering(Location where) const {
        std::vector<VehicleId> out;
        for (const Entry& e : entered_) {
            if (e.where != where) continue;
            for (VehicleId id : e.members)
                if (const Vehicle* v = s_.vehicle(id);
                    v && alive(*v) && v->location == where && std::find(out.begin(), out.end(), id) == out.end())
                    out.push_back(id);
        }
        return out;
    }

    // Per member of the groups that stepped into `where` today: count and damage.
    std::map<VehicleId, std::pair<int, std::vector<int>>> marks(Location where) const {
        std::map<VehicleId, std::pair<int, std::vector<int>>> out;
        for (const Entry& e : entered_) {
            if (e.where != where) continue;
            for (VehicleId id : e.members)
                if (const Vehicle* v = s_.vehicle(id)) out[id] = {v->count, v->damage};
        }
        return out;
    }

    void resolveCombat() {
        std::vector<Location> sites = std::move(touched_);
        touched_.clear();
        std::sort(sites.begin(), sites.end());
        sites.erase(std::unique(sites.begin(), sites.end()), sites.end());
        if (hooks_.possible && hooks_.resolve)
            for (const Location& where : sites) {
                if (!hooks_.possible(r_, s_, where) || !freshBattle(where)) continue;
                const auto before = marks(where);
                const size_t records = s_.combats.size();
                hooks_.resolve(ctx_, where, entering(where));
                afterBattle(where, records, before);
                rememberBattle(where, records);
            }
        entered_.clear();
        s_.removeDeadVehicles();
    }

    // Combat neither stops movement nor clears orders: lists are kept, and only
    // a Sentry order at the head of a participant's list is removed, even with
    // Repeat on (§6.3, §6.4, confirmed: binary). A group that met a minefield
    // stops and its order fails.
    void afterBattle(Location where, size_t recordsBefore, const std::map<VehicleId, std::pair<int, std::vector<int>>>& before) {
        std::set<VehicleId> fought;
        for (size_t i = recordsBefore; i < s_.combats.size(); ++i)
            if (s_.combats[i].location == where)
                for (const CombatPiece& p : s_.combats[i].pieces)
                    if (p.vehicle.valid()) fought.insert(p.vehicle);
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
        for (const Entry& e : entered_) {
            if (e.where != where) continue;
            bool struck = false;
            for (VehicleId id : e.members) {
                const auto mark = before.find(id);
                if (mark == before.end() || fought.contains(id)) continue;
                const Vehicle* v = s_.vehicle(id);
                if (!v || v->count < mark->second.first || v->damage != mark->second.second) struck = true;
            }
            if (!struck) continue;
            ctx_.log(e.owner, LogCategory::Combat, std::format("{} stopped by a minefield", e.name), "Its orders were cancelled.", where);
            clearListsOf(e.members);
        }
    }

    // ---- Turn-based games ------------------------------------------------------------------------

    // Actions one group may take in one run: movement bounds every loop but a
    // repeating list of orders that give movement back (inferred safeguard).
    static constexpr int kLiveActionLimit = 1000;

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

    // A group completes at most this many orders in one run (spec 05 §8
    // "Turn-based game" step 3, confirmed: binary); an order that leaves the
    // head of the list counts (inferred, spec 05 open question 45).
    static constexpr int kLiveOrderLimit = 21;

    void liveActor(ActorRef ref) {
        int completed = 0;  // orders that left the head of the list, chained ones included
        for (int n = 0; n < kLiveActionLimit; ++n) {
            const size_t steps = entered_.size();
            Group g;
            const std::optional<Exec> e = act(ref, g, &completed);
            if (!e) return;
            bool fought = false;
            if (entered_.size() > steps) fought = entryCombat(g);
            else if (*e == Exec::Acted || *e == Exec::ActedStay) placeCombat(g);
            entered_.clear();
            touched_.clear();
            if (fought || g.stopped) return;
            // A step, or a completed order, goes on until 21 orders are
            // completed, so a repeating list stops too; anything else ends
            // the run.
            switch (*e) {
                case Exec::Moved: break;
                case Exec::MovedDone:
                case Exec::Done:
                case Exec::Acted:
                case Exec::Removed:
                    if (completed >= kLiveOrderLimit) return;
                    break;
                case Exec::ActedStay:
                case Exec::Wait:
                case Exec::Fail:
                case Exec::Cleared:
                case Exec::Gone: return;
            }
        }
    }

    // Where the group's Attack order at the head of its list means to fight.
    std::optional<Location> attackGoal(const Group& g) {
        const std::vector<Order>* list = orders(g);
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
    bool entryCombat(Group& g) {
        prune(g);
        if (g.stopped) return true;
        const Location here = where(g);
        if (attackGoal(g) == here || !combatHere(here)) return false;
        fight(here, entering(here));
        prune(g);
        if (!g.stopped)
            if (const std::vector<Order>* list = orders(g); list && !list->empty()) {
                fail(g, list->front(), "Combat on entering the sector.");
                setLists(g, {});
            }
        g.stopped = true;
        return true;
    }

    // An order carried out in a sector offers it to combat (spec 04 §2).
    void placeCombat(Group& g) {
        prune(g);
        if (g.stopped) return;
        const Location here = where(g);
        if (combatHere(here)) fight(here, {});
    }

    // Objects of an empire `e` is hostile to in a sector, that `e` sees: ships,
    // bases, unit groups (mines included) and colonies (spec 03 §6.2).
    bool enemiesAt(EmpireId e, Location l) const {
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location == l && hostile(s_, e, v.owner) && sight::canSeeVehicle(r_, s_, e, v)) return true;
        for (ObjectId o : planetsAt(s_, l))
            if (const Colony* c = s_.colony(o); c && hostile(s_, e, c->owner) && sight::canSeePlanet(r_, s_, e, o)) return true;
        return false;
    }

    // Turn-based games: a human player's group stops before an in-system step
    // into a sector with enemies it sees, and the player is asked (spec 03
    // §6.2, confirmed: binary). Groups of drones only, and groups whose members
    // are all cloaked, always enter; the approach steps of an Attack ask too.
    bool asks(const Group& g, Location next) {
        if (!live_ || !live_->ask || g.planet.valid() || onlyDrones(g)) return false;
        if (all(g, [](const Vehicle& v) { return v.status == VehicleStatus::Cloaked; })) return false;
        const EntryQuestion q{g.fleet.valid() ? VehicleId{} : g.lead, g.fleet, next};
        if (live_->allowed && *live_->allowed == q) return false;
        if (!enemiesAt(g.owner, next)) return false;
        if (std::find(questions_.begin(), questions_.end(), q) == questions_.end()) questions_.push_back(q);
        return true;
    }

    TurnContext& ctx_;
    const Rules& r_;
    GameState& s_;
    const CombatHooks& hooks_;
    const LiveMove* live_ = nullptr;                    // turn-based: the move being carried out
    std::map<VehicleId, int> steps_;                    // turn-based: steps made this player turn
    std::map<VehicleId, int> bonus_;                    // turn-based: emergency energy gained this turn
    std::vector<EntryQuestion> questions_;
    std::map<VehicleId, DayCounter> counters_;          // simultaneous: the day counters
    std::set<VehicleId> acted_;                         // simultaneous: acted (or were carried along) today
    std::map<VehicleId, int> actionMovement_;           // simultaneous: movement points before the action
    std::set<VehicleId> participants_;                  // who took part in the action
    std::map<RouteKey, Route> routes_;
    std::vector<VehicleId> objectOrder_;                // the vehicles in object order (refreshed each day)
    std::vector<Location> touched_;                     // sectors where something acted today
    std::vector<Entry> entered_;                        // steps made today
    std::map<Location, BattleMemo> lastBattle_;         // the latest battle per location this phase
    UnitBudget budget_;
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

int movesPerTurn(const GameState& s, int speed) {
    if (speed <= 0) return 0;
    if (!s.options.simultaneous) return speed;
    return static_cast<int>(actionDays(std::min(speed, kDaysPerTurn + 1)).size());
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
    // R[0,144], so rows 0-10 and the first two squares of row 11. It is drawn
    // every turn, first thing, even when no system drifts (confirmed: binary).
    const int drawn = static_cast<int>(s.rng.below(145));
    const Sector driftTarget{drawn % kSystemSize, drawn / kSystemSize};
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
            if (drift > 0) v.location.sector = stepToward(v.location.sector, driftTarget, drift);
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
