// The movement phase (spec 03 §6.3, §6.4, §8; spec 01 §7).
//
// Simultaneous games: a month of 30 days. Every ship, base and unit group has
// a day counter that gains its current movement points / 30 each day (the
// original's double, DayCounter); at 1 or more it acts and loses 1. Within a
// day objects act in object order (the slots of the game's one object list,
// planets and vehicles mixed: objectOrder, spec 03 §19 Q62). The group
// that acts is rebuilt at every order execution: a fleet's members at its
// location, and ad-hoc companions with an identical head order (spec 03 §8).
// An action gives the acting vehicle exactly 1 movement point and runs its
// order list; orders that complete chain into the next. After every day, each sector where an object
// carried out an order (any order, a waiting Sentry included) runs a battle
// check (spec 04 §2).
//
// Turn-based games (runLive): the groups a player sets in motion carry out
// their orders at once, spending movement points. Only a movement step (a
// warp jump included), the Attack order and a Seek order at its target run a
// battle check (spec 04 §2).

#include "game/log_picture.hpp"
#include "game/movement.hpp"

#include "game/combat.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/movement_internal.hpp"
#include "game/orders.hpp"
#include "game/query.hpp"
#include "game/scrap.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <map>
#include <climits>
#include <format>
#include <optional>
#include <set>

namespace opense4::game::movement {

using namespace detail;

CombatHooks defaultCombatHooks() {
    return {[](const Rules& r, const GameState& s, Location where, const combat::BattleCheck& check) {
                return combat::combatPossible(r, s, where, check);
            },
            [](TurnContext& ctx, Location where, std::span<const VehicleId> entering, const combat::BattleCheck& check) {
                combat::resolveSpaceCombat(ctx, where, entering, check);
            }};
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
    Acted,      // completed by acting here; the chain goes on
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

// A square an in-system step avoids (spec 03 §6.2, confirmed: binary): a
// tagged minefield (the owner's option on; no Mine Sweeper exemption inside
// a system), a sector whose objects have `Sector - Damage` (not the
// system's own value), or one holding an object of an empire the mover is
// hostile to that the mover sees: a ship, base, unit group (a mine only if
// seen) or colony (only if seen).
bool badStepSquare(const Rules& r, const GameState& s, EmpireId mover, Location l) {
    const Empire& e = s.empire(mover);
    if (e.avoidTaggedMinefields && std::find(e.taggedMinefields.begin(), e.taggedMinefields.end(), l) != e.taggedMinefields.end())
        return true;
    for (ObjectId o : s.galaxy.system(l.system).objects) {
        const SpaceObject& obj = s.galaxy.object(o);
        if (obj.sector != l.sector) continue;
        if (rawSum(obj.abilities, AbilityKind::SectorDamage) > 0) return true;
        if (const Colony* c = s.colony(o); c && hostile(s, mover, c->owner) && sight::canSeeColony(r, s, mover, o)) return true;
    }
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.location == l && hostile(s, mover, v.owner) && sight::canSeeVehicle(r, s, mover, v)) return true;
    return false;
}

// In-system steps are greedy: one square toward `target`, diagonal first.
// Unless it is the target it is tested; a bad square is replaced by a
// random one drawn from the current square (after a diagonal first step,
// one of the two straight steps; after a straight one, the forward square
// or either square beside it), coordinates clamped to the grid, and every
// replacement is tested, the target too. After the 10th bad test the group
// stays and the order fails (spec 03 §6.2, confirmed: binary).
std::optional<Sector> greedyStep(const Rules& r, const GameState& s, EmpireId mover, Location here, Sector target, Rng& rng) {
    const int x = here.sector.x, y = here.sector.y;
    const int dx = (target.x > x) - (target.x < x), dy = (target.y > y) - (target.y < y);
    auto clamp = [](int c) { return std::clamp(c, 0, kSystemSize - 1); };
    Sector pick{x + dx, y + dy};
    if (pick == target) return pick;
    for (int bad = 0;;) {
        if (!badStepSquare(r, s, mover, {here.system, pick})) return pick;
        if (++bad >= 10) return std::nullopt;
        if (dx != 0 && dy != 0) pick = rng.below(2) == 0 ? Sector{x + dx, y} : Sector{x, y + dy};
        else if (dx != 0) pick = Sector{x + dx, clamp(y + static_cast<int>(rng.below(3)) - 1)};
        else pick = Sector{clamp(x + static_cast<int>(rng.below(3)) - 1), y + dy};
    }
}

// What starts an action: a vehicle (alone, with its fleet or with ad-hoc
// companions) or a planet's own orders.
struct ActorRef {
    VehicleId vehicle;
    ObjectId planet;
};

// The group that carries out one order execution (spec 03 §8).
struct Group {
    std::vector<VehicleId> members;   // group order: a fleet's group in member order, then ad-hoc companions
    // The lists carrying the order out changes (spec 03 §8, §19 Q73, Q75,
    // confirmed: binary): the acting vehicle's, or its fleet's members' at
    // the fleet's location (an away actor's own list is not one of them), and
    // in a turn-based game the vehicles the player moves together. Ad-hoc
    // companions keep their lists.
    std::vector<VehicleId> holders;
    ObjectId planet;                  // a planet's own orders
    EmpireId owner;
    VehicleId actor;                  // the vehicle whose order the group carries out
    VehicleId lead;                   // the acting vehicle when it is a member, else the first member: the group's place and name
    FleetId fleet;                    // the actor's fleet: its members at the fleet's location are the group
    bool tagged = false;              // turn-based: a tagged group (LiveMove::tagged)
    bool stopped = false;             // gone, or stopped by a hazard
    bool encountered = false;         // the last warp transit cleared the lists (§6.4)
};

class Mover {
public:
    Mover(TurnContext& ctx, const CombatHooks& hooks) : ctx_(ctx), r_(ctx.rules), s_(ctx.state), hooks_(hooks) {}

    // ---- Simultaneous games (spec 03 §6.3) ---------------------------------------------------------

    void run() {
        for (int day = 1; day <= kDaysPerTurn; ++day) {
            day_ = day;
            newDay();
            const std::vector<ObjectRef> order = refreshObjectOrder();
            for (const ObjectRef& ref : order) {
                if (ref.object.valid()) {
                    // Colonized planets with orders act on day 1 only, where
                    // their slots put them among the vehicles (spec 03 §6.3
                    // steps 3 and 5, §19 Q62, confirmed: binary).
                    if (day == 1 && plannedPlanet(ref.object)) action(ActorRef{{}, ref.object});
                    continue;
                }
                const VehicleId id = ref.vehicle;
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
                // A fleet acts through its first member, in object order, that
                // is due and has orders (spec 03 §6.3 step 5, §19 Q65).
                if (v->orders.empty()) {
                    // A vehicle with movement acts on its counter's days with
                    // or without orders, so an idle one marks its sector for
                    // the day's battle check (spec 03 §6.3 steps 3 and 6,
                    // spec 04 §2, confirmed: binary). One with no movement
                    // acts only when it has orders.
                    if (!dayOneOnly(*v)) touched_.push_back(v->location);
                    continue;
                }
                action(ActorRef{id, {}});
            }
            resolveCombat();
            recloak();
            endPursuits();
            if (ctx_.movementDay) ctx_.movementDay(day, s_);
        }
        // The ministers' Seek orders last the movement phase (spec 05 §7.5).
        endSeeks([](const Vehicle&) { return true; });
    }

    // ---- Turn-based games (spec 03 §6.3 "Turn-based", spec 04 §2) ---------------------------------

    // The selected groups of one empire carry out their orders now, action
    // after action, until each has spent its movement, waits, fails or has
    // nothing left. A group's movement step runs a battle check: a battle is
    // fought there at once and the group's whole list is cleared. The Attack
    // order and a Seek order at its target run one where the group stands:
    // the Attack is used up, the Seek stays. No other order starts a battle
    // (spec 04 §2). The per-turn records (steps, emergency movement,
    // launches) live in GameState::playerTurn.
    void runLive(const LiveMove& m) {
        live_ = &m;
        budget_.turnBased = true;
        loadPlayerTurn();
        if (!m.tagged.empty()) {
            // A tagged group acts alone, through its first vehicle still there
            // (spec 03 §8 "Tagged vehicles").
            if (const auto actor = taggedActor()) liveActor(ActorRef{*actor, {}});
            endSeeks([&](const Vehicle& v) { return v.owner == m.empire; });
            savePlayerTurn();
            return;
        }
        const bool all = m.vehicles.empty() && m.fleets.empty() && m.planets.empty();
        auto has = [](const auto& list, auto id) { return std::find(list.begin(), list.end(), id) != list.end(); };
        std::set<FleetId> fleetsDone;
        // Every vehicle, fleet and planet with orders, in object order (spec 05
        // §8 "Turn-based game" step 3; planets and vehicles share the object
        // list, spec 03 §19 Q62).
        const std::vector<ObjectRef> order = refreshObjectOrder();
        for (const ObjectRef& ref : order) {
            if (ref.object.valid()) {
                const Colony* c = s_.colony(ref.object);
                if (c && c->owner == m.empire && plannedPlanet(ref.object) && (all || has(m.planets, ref.object)))
                    liveActor(ActorRef{{}, ref.object});
                continue;
            }
            const VehicleId id = ref.vehicle;
            const Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v) || v->owner != m.empire || v->orders.empty()) continue;
            if (const Fleet* f = v->fleet.valid() ? s_.fleet(v->fleet) : nullptr) {
                // A fleet runs once, through its first member in object order
                // that has orders (spec 03 §8, §19 Q65); naming any member
                // names the fleet.
                const bool named = all || has(m.fleets, f->id) ||
                                   std::any_of(f->members.begin(), f->members.end(), [&](VehicleId member) { return has(m.vehicles, member); });
                if (fleetsDone.contains(f->id) || !named) continue;
                fleetsDone.insert(f->id);
            } else if (!all && !has(m.vehicles, id)) {
                continue;
            }
            liveActor(ActorRef{id, {}});
        }
        // A Seek moves its group as far as it can in a turn-based game and is
        // then done (spec 05 §7.5): what is left of one after the run goes.
        endSeeks([&](const Vehicle& v) { return v.owner == m.empire; });
        savePlayerTurn();
    }

    std::vector<EntryQuestion> questions() const { return questions_; }

private:
    // The game's object list as it stands; objectOrder_ keeps its vehicles
    // for the ad-hoc groups (spec 03 §8).
    std::vector<ObjectRef> refreshObjectOrder() {
        std::vector<ObjectRef> order = objectOrder(s_);
        objectOrder_.clear();
        for (const ObjectRef& ref : order)
            if (ref.vehicle.valid()) objectOrder_.push_back(ref.vehicle);
        return order;
    }

    // A colonized planet still on the map with orders of its own (Launch and Recover Units).
    bool plannedPlanet(ObjectId planet) const {
        const Colony* c = s_.colony(planet);
        return c && !c->orders.empty() && inSystem(s_.galaxy, planet);
    }

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

    // The speed of the day: the current movement points; for a fleet member,
    // wherever it is, the lowest among the members at the fleet's location,
    // and 0 when none is there (spec 03 §6.3 step 2, §19 Q61, confirmed:
    // binary). The member's own points only decide whether it gains at all.
    int daySpeed(const Vehicle& v) const {
        if (const Fleet* f = v.fleet.valid() ? s_.fleet(v.fleet) : nullptr) {
            int lowest = INT_MAX;
            for (VehicleId id : fleetMembersAt(s_, *f)) lowest = std::min(lowest, s_.vehicle(id)->movement);
            return lowest == INT_MAX ? 0 : lowest;
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

    // A vehicle takes part in the action, and it does not act again that day;
    // one that was due loses 1 from its counter (spec 03 §6.3 steps 4-5). The
    // acting vehicle gets exactly 1 movement point, whatever it had, when its
    // maximum is at least 1, so a vehicle stopped earlier in the turn moves
    // again, one step per action; with a maximum of 0 it keeps 0. The other
    // members keep their own, so one with 0 still holds the group back (spec
    // 03 §6.3 step 4, §19 Q63, confirmed: binary).
    void join(VehicleId id, bool acting) {
        participants_.insert(id);
        if (live_ || actionMovement_.contains(id)) return;
        Vehicle* v = s_.vehicle(id);
        if (!v) return;
        actionMovement_[id] = v->movement;
        if (acting) v->movement = turnMovement(r_, s_, *v) >= 1 ? 1 : 0;
        if (acted_.insert(id).second) counters_[id].take();
    }

    // ---- Groups ------------------------------------------------------------------------------------

    // The group of one execution (spec 03 §8, §19 Q61, Q65, Q74, Q75,
    // confirmed: binary): a fleet member's order is carried out by the fleet's
    // members at its location, mothballed ones included (a member elsewhere is
    // not one of them, even when it is the one acting); any other vehicle acts
    // alone. A computer player's group also takes every own vehicle in the
    // sector whose head order is identical (ships, bases, unit groups, fleet
    // members, cloaked and mothballed ones), each alone, never its whole
    // fleet; a human player's ships never group, only a drone group outside
    // fleets gathers the other drone groups there with the same head order.
    // In a turn-based game a tagged group is every tagged vehicle (tagged()).
    // Only the holders' lists change (Group::holders).
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
        if (live_ && !live_->tagged.empty()) return tagged(*v);
        g.owner = v->owner;
        g.actor = v->id;
        auto has = [&](VehicleId id) { return std::find(g.members.begin(), g.members.end(), id) != g.members.end(); };
        const Fleet* own = v->fleet.valid() ? s_.fleet(v->fleet) : nullptr;
        if (own) {
            g.fleet = own->id;
            g.members = fleetGroup(s_, *own);
        } else {
            g.members.push_back(v->id);
        }
        g.holders = g.members;
        if (g.members.empty()) {
            g.stopped = true;  // no member of the fleet can act at its location
            return g;
        }
        g.lead = has(v->id) ? v->id : g.members.front();
        if (v->orders.empty()) return g;
        const Order head = v->orders.front();
        const Location here = s_.vehicle(g.lead)->location;
        auto joins = [&](const Vehicle& w) { return alive(w) && w.owner == v->owner && w.location == here && !has(w.id); };
        if (computerPlayer(s_, v->owner)) {
            // Each vehicle joins alone, a fleet member too (spec 03 §19 Q75).
            for (VehicleId id : objectOrder_)
                if (const Vehicle* w = s_.vehicle(id); w && joins(*w) && !w->orders.empty() && w->orders.front() == head) g.members.push_back(id);
        } else if (!own && vehicleType(r_, s_, *v) == VehicleType::Drone) {
            for (VehicleId id : objectOrder_) {
                const Vehicle* w = s_.vehicle(id);
                if (w && joins(*w) && !w->fleet.valid() && vehicleType(r_, s_, *w) == VehicleType::Drone && !w->orders.empty() &&
                    w->orders.front() == head)
                    g.members.push_back(id);
            }
        }
        return g;
    }

    // A tagged group (spec 03 §8 "Tagged vehicles", confirmed: binary): every
    // tagged vehicle still in the game and standing with the actor, in tag
    // order, whatever its fleet and whatever its own list holds; each of their
    // lists is a holder. The actor's list is the one carried out.
    Group tagged(const Vehicle& actor) const {
        Group g;
        g.owner = actor.owner;
        g.actor = actor.id;
        g.lead = actor.id;
        g.tagged = true;
        for (VehicleId id : live_->tagged)
            if (const Vehicle* w = s_.vehicle(id); w && alive(*w) && w->owner == actor.owner && w->location == actor.location)
                g.members.push_back(id);
        g.holders = g.members;
        return g;
    }

    // The tagged group's acting vehicle: the first one tagged that is still in the game.
    std::optional<VehicleId> taggedActor() const {
        for (VehicleId id : live_->tagged)
            if (const Vehicle* v = s_.vehicle(id); v && alive(*v) && v->owner == live_->empire) return id;
        return std::nullopt;
    }

    void prune(Group& g) {
        if (g.planet.valid()) {
            const Colony* c = s_.colony(g.planet);
            if (!c || c->owner != g.owner) g.stopped = true;
            return;
        }
        auto gone = [&](VehicleId id) {
            const Vehicle* v = s_.vehicle(id);
            return !v || !alive(*v) || v->owner != g.owner;
        };
        std::erase_if(g.members, gone);
        std::erase_if(g.holders, gone);
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
        return g.owner.valid() && g.owner.index() < s_.empires.size() && isNeutral(s_.empire(g.owner));
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

    // The list the group executes: the planet's, or the acting vehicle's (for
    // a fleet, its copy of the fleet's orders, spec 03 §8).
    std::vector<Order>* orders(const Group& g) {
        if (g.planet.valid()) {
            Colony* c = s_.colony(g.planet);
            return c ? &c->orders : nullptr;
        }
        Vehicle* v = s_.vehicle(g.actor);
        if (!v || !alive(*v)) v = s_.vehicle(g.lead);
        return v ? &v->orders : nullptr;
    }
    // Every list a change to the head applies to, with its Repeat flag: the
    // group's holders (spec 03 §8, §19 Q65, Q73, Q75). A fleet member away
    // from the fleet's location that acts keeps its own list, so a chained
    // run carries its unchanged head order out again (Q73).
    template <class Fn>
    void forEachList(const Group& g, Fn&& fn) {
        if (g.planet.valid()) {
            if (Colony* c = s_.colony(g.planet)) fn(c->orders, c->repeatOrders);
            return;
        }
        for (VehicleId id : g.holders)
            if (Vehicle* v = s_.vehicle(id)) fn(v->orders, v->repeatOrders);
    }
    // Replaces every list of the group and switches Repeat off.
    void setLists(const Group& g, const std::vector<Order>& list) {
        routes_.erase(routeKey(g));
        if (g.planet.valid()) {
            if (Colony* c = s_.colony(g.planet)) {
                c->orders = list;
                c->repeatOrders = false;
            }
            return;
        }
        for (VehicleId id : g.holders)
            if (Vehicle* v = s_.vehicle(id)) {
                v->orders = list;
                v->repeatOrders = false;
            }
    }
    // Clears the lists of these vehicles (a fleet's group clears each member's copy).
    void clearListsOf(std::span<const VehicleId> ids) {
        for (VehicleId id : ids)
            if (Vehicle* v = s_.vehicle(id)) {
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
            for (VehicleId id : g.members) join(id, id == g.actor);
            std::vector<Order>* list = orders(g);
            const bool carried = (!list || list->empty()) && carried_.has_value();
            if (!carried && (!list || list->empty())) {
                last = g;
                break;
            }
            Order o = carried ? *carried_ : list->front();
            const Order head = o;
            if (carried) carried_.reset();
            const Exec e = execute(g, o);
            if (carried && e == Exec::Moved && !carried_) carried_ = o;  // still on its way
            prune(g);
            last = g;
            result = e;
            // Simultaneous games: every sector where an object carried out an
            // order today, whatever the order (a Sentry that waits too), is
            // checked after the day (spec 04 §2, confirmed: binary).
            if (!live_ && !g.stopped) touched_.push_back(where(g));
            if (!g.stopped || e == Exec::Fail) settle(g, e, head, o);
            if (completed && chains(e) && ++*completed >= kLiveOrderLimit) break;
            if (g.stopped || !chains(e)) break;
            if (live_ && e != Exec::Done && e != Exec::Removed) break;
        }
        afterAction();
        return result;
    }

    void settle(Group& g, Exec e, const Order& head, const Order& o) {
        switch (e) {
            case Exec::Moved:
            case Exec::Wait: writeBack(g, head, o); break;
            case Exec::MovedDone:
            case Exec::Done: complete(g); break;
            case Exec::Acted: complete(g); break;
            case Exec::ActedStay: writeBack(g, head, o); break;
            case Exec::Removed: removeFront(g); break;
            case Exec::Fail: setLists(g, {}); break;
            case Exec::Cleared:
            case Exec::Gone: break;
        }
    }

    // After every daily action: the depot check (§7), and a cloak drops at 0
    // supply or when it can no longer work (§8).
    void afterAction() {
        std::vector<SystemId> decloakedIn;
        for (VehicleId id : participants_) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            if (resupplyDepotAt(r_, s_, v->owner, v->location)) refillSupply(r_, s_, *v);
            if (v->status == VehicleStatus::Cloaked &&
                ((v->supply <= 0 && !vehicleHasUnlimitedSupply(r_, s_, *v)) || !canCloak(r_, s_, *v))) {
                v->status = VehicleStatus::Normal;
                decloakedIn.push_back(v->location.system);
            }
        }
        participants_.clear();
        for (SystemId sys : decloakedIn) decloaked(sys);
    }

    // Any decloak of a ship, unit group or colony, whatever its cause, runs
    // the first-contact check in its system (spec 05 §3.1, confirmed: binary).
    void decloaked(SystemId sys) { diplomacy::firstContactIn(ctx_, sys); }

    // The order stays where it is, as the execution left it (a Colonize whose
    // colonists came aboard): in each list whose head it still is.
    void writeBack(const Group& g, const Order& head, const Order& o) {
        forEachList(g, [&](std::vector<Order>& list, bool) {
            if (!list.empty() && list.front() == head) list.front() = o;
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
        ctx_.log(g.owner, LogCategory::Misc, std::format("{}: {} order cancelled", name(g), displayName(o.kind)), std::string(why), where(g),
                 picture(g));
        return Exec::Fail;
    }

    // The acting object's own picture for the entries about it (spec 06
    // §4.1): a planet's, a fleet member's fleet portrait, a unit group's
    // group portrait, a ship's or base's hull portrait.
    std::string picture(const Group& g) const {
        if (g.planet.valid()) return logpicture::planet(g.planet);
        const Vehicle* v = s_.vehicle(g.lead);
        return v ? logpicture::vehicle(r_, s_, *v) : std::string{};
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

    // The owner's Ship Orders option cleared the lists after a warp transit
    // (spec 03 §6.4, §19 Q77, confirmed: binary): the list of every member of
    // the acting group, a computer player's ad-hoc companions and the
    // turn-based selection included, is emptied with Repeat off. The jump
    // itself does not fail: in a turn-based game a Move To in progress goes on
    // stepping toward its destination in this run while movement lasts
    // (carried_), but nothing is left of it for a later turn; otherwise the
    // action ends there.
    Exec encounter(Group& g, const Order& o, bool arrived) {
        ctx_.log(g.owner, LogCategory::Misc, std::format("{}: orders cleared", name(g)),
                 "Another empire is in the system; the orders were cleared (empire options).", where(g), picture(g));
        routes_.erase(routeKey(g));
        clearListsOf(g.members);
        clearListsOf(g.holders);
        if (live_ && (o.kind == OrderKind::MoveTo || o.kind == OrderKind::MoveToWaypoint) && !arrived) {
            carried_ = o;
            return Exec::Moved;
        }
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
        // A fleet with a member of maximum movement 0 (a mothballed ship, a
        // base) is frozen: its movement orders wait (spec 03 §9, §19 Q74).
        if (immobile(g)) return held(g) || g.fleet.valid() || g.tagged ? Travel::Wait : Travel::Immobile;
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
        const auto chosen = inSystemStep(r_, s_, g.owner, here, rt.steps[last].sector, s_.rng);
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
            if (const Colony* c = s_.colony(o); c && applies(c->owner) && sight::canSeeColony(r_, s_, e, o)) return true;
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
                ctx_.log(g.owner, LogCategory::Events, std::format("{} damaged", v->name), std::string(cause), v->location, "ShipDamaged");
        g.stopped = true;
    }

    void moveMembers(Group& g, Location next, ObjectId via) {
        for (VehicleId id : g.members) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            // The sector it leaves, for combat's attackers and start boxes (spec 04 §3).
            v->cameFrom = v->location;
            v->cameFromTurn = s_.turn;
            // A step within a system turns the vehicle to its bearing; a warp
            // keeps its heading (spec 06 §2.4, §7 Q62).
            if (!via.valid() && v->location.system == next.system)
                v->heading = static_cast<uint8_t>(headingFor(v->location.sector, next.sector));
            if (ctx_.movementStep && !live_) ctx_.movementStep(MovementStep{day_, id, v->location, next});
            if (next.system != v->location.system) s_.arrived(*v);  // last on the new system's list
            v->location = next;
            fleetMemberMoved(s_, *v);  // the fleet's location goes with it (spec 03 §9)
            v->movement = std::max(0, v->movement - 1);
            if (live_) ++steps_[id];
            // The depot check runs before the step's cost is taken (§7).
            if (resupplyDepotAt(r_, s_, v->owner, next)) refillSupply(r_, s_, *v);
            spendSupply(r_, s_, *v, moveSupplyCost(r_, s_, *v));
        }
        entered_.push_back(Entry{g.members, g.holders, g.owner, next, name(g), pursuing_});
        // A drone out of supply is destroyed after each step or warp (§12, confirmed: binary).
        for (VehicleId id : g.members)
            if (Vehicle* v = s_.vehicle(id); v && alive(*v) && v->supply <= 0 && vehicleType(r_, s_, *v) == VehicleType::Drone)
                vehicleLost(ctx_, *v, "Ran out of supplies.");
        prune(g);
        if (g.members.empty()) return;
        // A sweeper group entering a tagged minefield where mines act decloaks first (§12).
        if (decloakSweepers(r_, s_, next, g.members)) decloaked(next.system);
        if (via.valid()) {
            // A warp jump of a turn-based group: the view following it shows
            // the arrival system with the exit warp point current (spec 06 §2.7).
            if (live_) ctx_.liveSteps.push_back(LiveStep{g.owner, g.actor.valid() ? g.actor : g.lead, g.fleet, {}, next, true});
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
            const bool trouble = shaken || combat::detail::minesCanStrike(r_, s_, next, g.members) ||
                                 (live_ && combatHere(next, combat::BattleCheck{g.members}));
            if (!trouble && !onlyDrones(g) && encounterClearsOrders(g.owner, next.system)) g.encountered = true;
        } else if (const int64_t storm = stormDamageAt(next); storm > 0 && s_.rng.percent(50)) {
            // A storm: a 50 % chance for a group stepping in; it stops. A warp
            // arrival makes no storm roll (confirmed: binary).
            hazardHit(g, storm, "Damaged by a storm.");
            prune(g);
            if (g.members.empty()) return;
        }
        arrive(g);
        // A group arriving through a warp point, some member still there after
        // the passage, runs the first-contact check in the system it reached
        // (spec 05 §3.1, confirmed: binary); in-system steps never do.
        if (via.valid()) diplomacy::firstContactIn(ctx_, next.system);
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
            // A colony of a computer player, or a human's under minister
            // control with the Ship Cloaking minister on, decloaks before each
            // of its orders and cloaks again afterwards if it can, whether or
            // not it was cloaked before (spec 01 §6.9, confirmed: binary).
            Colony* c = s_.colony(g.planet);
            const bool minister = c && colonyUnderCloakingMinister(*c);
            if (minister && c->cloaked) {
                c->cloaked = false;
                decloaked(where(g).system);
            }
            const Exec e = colonyOrder(g, o);
            if (Colony* after = s_.colony(g.planet); minister && after && sight::colonyCanCloak(*after)) after->cloaked = true;
            return e;
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
            case OrderKind::Scrap:
            case OrderKind::Analyze:
            case OrderKind::Mothball:
            case OrderKind::Unmothball:
            case OrderKind::Retrofit:
            case OrderKind::FireOn: return scrapWindowAction(g, o);
            case OrderKind::Seek: return seek(g, o);
            case OrderKind::JoinFleet: return joinFleetOrder(g, o);
            case OrderKind::UseFacility:
            case OrderKind::ConvertResources: return fail(g, o, "Only colonies carry out that order.");
            case OrderKind::Count: break;
        }
        return fail(g, o, "Unknown order.");
    }

    Exec colonyOrder(Group& g, Order& o) {
        if (o.kind == OrderKind::LaunchUnits || o.kind == OrderKind::RecoverUnits) return cargo(g, o);
        // Use Facility looks up the facility at the recorded position and
        // completes with no effect, cost or message, whether or not that
        // facility still exists (spec 03 §8, confirmed: binary).
        if (o.kind == OrderKind::UseFacility) return Exec::Done;
        if (o.kind == OrderKind::ConvertResources) return convert(g, o);
        return fail(g, o, "Planets cannot carry out that order.");
    }

    // A vehicle or colony of an empire whose treaty with `e` is below
    // Non-Aggression, anywhere in the system, that `e` sees: ships, bases,
    // unit groups (mines only if seen) and colonies (only if seen) (spec 03 §8
    // Sentry, confirmed: binary).
    bool hostilePresentInSystem(EmpireId e, SystemId sys) const {
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location.system == sys && hostile(s_, e, v.owner) && sight::canSeeVehicle(r_, s_, e, v)) return true;
        for (ObjectId o : s_.galaxy.system(sys).objects)
            if (const Colony* c = s_.colony(o); c && hostile(s_, e, c->owner) && sight::canSeeColony(r_, s_, e, o)) return true;
        return false;
    }

    // Sentry stays at the head at no cost until an enemy is present in the
    // system or a member's supply is low; then it counts as done: removed, or
    // with Repeat on passed over (§8, confirmed: binary). A fighter or drone
    // group is low below a tenth of the warning level while it holds at least
    // one unit (spec 06 §4.4, §7 Q54, Q61, confirmed: binary).
    Exec sentry(Group& g) {
        const int64_t low = r_.setting("Supply Amount for Low Supply Warning", 1000);
        const bool lowSupply = any(g, [&](const Vehicle& v) {
            if (!vehicleUsesSupply(r_, s_, v) || vehicleHasUnlimitedSupply(r_, s_, v)) return false;
            const VehicleType t = vehicleType(r_, s_, v);
            if (t == VehicleType::Fighter || t == VehicleType::Drone) return v.count >= 1 && v.supply < low / 10;
            return v.supply < low;
        });
        const bool enemy = hostilePresentInSystem(g.owner, where(g).system);
        if (!lowSupply && !enemy) return Exec::Wait;
        ctx_.log(g.owner, LogCategory::Combat, std::format("{}: {}", name(g), enemy ? "enemy sighted" : "supplies low"), "Sentry duty ended.",
                 where(g), picture(g));
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
        if (j == Travel::Encounter) return encounter(g, o, true);  // a Warp order just ends with the cleared lists (§6.4)
        return afterTravel(g, o, j);
    }

    // Attack (§8, confirmed: binary): a pursuit that moves toward the target's
    // current sector, warping as needed. Once there, a group holding a drone
    // whose own first order pursues an object in that sector attacks: its
    // cloaked drones decloak, then 1 movement point and one move's supply; the
    // battle comes from the day's combat check (a turn-based game's check at
    // once) and the order stays. A group with no such drone just waits there,
    // spending neither (§19 Q69). A step of the pursuit stopped by storm
    // damage, turbulence, mines or a battle only ends this run of the list;
    // the order and the list are kept (spec 03 §6.4, spec 04 §19.2 Q76). The
    // pursuit is done when the target is gone, the attacker's owner's, or a
    // planet without colony. A drone group's
    // pursuit (a Seek) at its target attacks and runs a battle check every
    // time its list runs, and stays (spec 04 §2, confirmed: binary). In a
    // turn-based game a group that is not all drones carries out the Move To
    // plus Attack the order stands for (placeAttack).
    Exec attack(Group& g, Order& o) {
        // The stored Attack, naming no target and no place, which the
        // computer's ministers give a ship already on its target's sector:
        // carried out at once where the group stands, and done, in either
        // kind of game (spec 03 §8, spec 05 §7.5, confirmed: binary).
        const bool stored = !o.vehicle.valid() && !o.object.valid() && !validLocation(s_, o.location);
        if ((live_ && !onlyDrones(g)) || stored) return placeAttack(g, o);
        if (pursuitOver(s_, g.owner, o)) {
            ctx_.log(g.owner, LogCategory::Combat, std::format("{}: target gone", name(g)), {}, where(g), picture(g));
            return Exec::Done;
        }
        // A drone sent at a warp point goes through it (an order given before
        // the expansion, orders.hpp).
        if (o.object.valid() && o.object.index() < s_.galaxy.objects.size() && s_.galaxy.object(o.object).kind == ObjectKind::WarpPoint)
            return warp(g, o);
        const Location goal = o.vehicle.valid() ? s_.vehicle(o.vehicle)->location : locationOf(s_.galaxy, o.object);
        pursuing_ = true;
        const Travel t = travel(g, goal);
        pursuing_ = false;
        if (t == Travel::Reached) return Exec::Moved;  // the attack needs the next action's movement
        // A pursuit that meets storm damage or warp turbulence only stops moving
        // for this run of its list; its order and list are kept (spec 03 §6.4,
        // spec 04 §19.2 Q76, confirmed: binary).
        if (t == Travel::Stopped) return Exec::Wait;
        if (t != Travel::Arrived) return afterTravel(g, o, t);
        if (!droneSeeksHere(g, goal)) return Exec::Wait;
        if (remaining(g) <= 0 || immobile(g)) return Exec::Wait;
        bool lowered = false;
        for (VehicleId id : g.members) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            // Only drones decloak (§6.4, §19 Q69). A drone's target in a
            // battle is read from its first order when the battle starts
            // (spec 03 §19 Q68, spec 04 §10.7).
            if (vehicleType(r_, s_, *v) == VehicleType::Drone && v->status == VehicleStatus::Cloaked) {
                v->status = VehicleStatus::Normal;
                lowered = true;
            }
            v->movement = std::max(0, v->movement - 1);
            spendSupply(r_, s_, *v, moveSupplyCost(r_, s_, *v));
        }
        if (lowered) decloaked(goal.system);
        if (live_) checkHere_ = true;  // turn-based: the Seek runs a battle check (runLive)
        return Exec::ActedStay;
    }

    // A member of the group is a drone whose own first order is an Attack
    // pursuing an object now in `here` (spec 03 §8, §19 Q69).
    bool droneSeeksHere(const Group& g, Location here) const {
        return any(g, [&](const Vehicle& v) {
            if (vehicleType(r_, s_, v) != VehicleType::Drone || v.orders.empty() || v.orders.front().kind != OrderKind::Attack) return false;
            const Order& seek = v.orders.front();
            if (pursuitOver(s_, v.owner, seek)) return false;
            if (seek.vehicle.valid()) return s_.vehicle(seek.vehicle)->location == here;
            return locationOf(s_.galaxy, seek.object) == here;
        });
    }

    // The ministers' Seek (spec 05 §7.5 "How long the ministers' movement
    // orders last", confirmed: binary): toward a sector, or after a ship or
    // planet whose current sector is the goal. Its steps are a pursuit's: a
    // hazard or a battle on one only stops it for this run of the list (spec
    // 03 §6.4, spec 04 §19.2 Q76). In a simultaneous game it stays for the
    // whole movement phase, also once there, where it waits (so an order
    // behind it waits for the next phase), and run() removes it after the
    // phase. In a turn-based game it moves the group as far as it can and is
    // then done: on arrival the next order runs, and runLive() removes what
    // is left of it when the run ends. A pursued object that is gone, or no
    // longer anybody else's, ends it. Nothing attacks: a group with no drone
    // just waits at its target (spec 03 §8 Attack), and the day's battle
    // check covers the sector where it waits.
    Exec seek(Group& g, Order& o) {
        Location goal = o.location;
        if (o.vehicle.valid() || o.object.valid()) {
            if (pursuitOver(s_, g.owner, o)) return Exec::Done;
            goal = o.vehicle.valid() ? s_.vehicle(o.vehicle)->location : locationOf(s_.galaxy, o.object);
        }
        pursuing_ = true;
        const Travel t = travel(g, goal);
        pursuing_ = false;
        switch (t) {
            case Travel::Arrived: return live_ ? Exec::Done : Exec::Wait;
            case Travel::Reached: return live_ ? Exec::MovedDone : Exec::Moved;
            case Travel::Stopped: return Exec::Wait;
            default: return afterTravel(g, o, t);
        }
    }

    // The Fleets minister's recruits (spec 05 §7.5 AI_Fleets, confirmed:
    // binary): on each action the group steps toward the fleet's position at
    // that moment, wherever the fleet has gone, and the vehicle joins as soon
    // as it stands where the fleet stands, its list cleared as by Fleet
    // Transfer. It waits when it cannot step now (no movement left, a hazard,
    // a busy yard, a blocked way), and fails, clearing the list, only when no
    // route is left or the fleet is gone. In a computer player's ad-hoc group
    // only the acting vehicle's list holds the order; companions join when
    // their own order runs.
    Exec joinFleetOrder(Group& g, Order& o) {
        auto fleet = [&]() -> Fleet* {
            Fleet* f = o.amount >= 0 ? s_.fleet(FleetId{o.amount}) : nullptr;
            return f && f->owner == g.owner && !f->members.empty() ? f : nullptr;
        };
        // The holders standing where the fleet stands join; their lists are cleared.
        auto joinHere = [&]() {
            Fleet* f = fleet();
            bool joined = false;
            routes_.erase(routeKey(g));
            for (VehicleId id : std::vector<VehicleId>(g.holders))
                if (Vehicle* v = s_.vehicle(id); f && v && alive(*v) && !v->fleet.valid() && v->location == f->location &&
                                                 fleetJoinProblem(r_, s_, *v).empty()) {
                    joinFleet(*f, *v);
                    joined = true;
                }
            return joined ? Exec::Cleared : fail(g, o, "It cannot join that fleet.");
        };
        const Fleet* f = fleet();
        if (!f || g.planet.valid() || g.fleet.valid()) return fail(g, o, "The fleet is gone.");
        if (where(g) == f->location) return joinHere();
        const Travel t = travel(g, f->location);
        switch (t) {
            case Travel::Reached:
            case Travel::Moved: {
                const Fleet* now = fleet();
                return now && now->location == where(g) ? joinHere() : Exec::Moved;
            }
            case Travel::Arrived: return joinHere();
            case Travel::Wait:
            case Travel::Stopped:
            case Travel::Busy:
            case Travel::Blocked:
            case Travel::Immobile:
            case Travel::Asked: return Exec::Wait;
            default: return afterTravel(g, o, t);
        }
    }

    // Removes the ministers' Seek orders from the lists of the vehicles `which` takes.
    template <class Which>
    void endSeeks(Which&& which) {
        for (Vehicle& v : s_.vehicles)
            if (which(v)) std::erase_if(v.orders, [](const Order& o) { return o.kind == OrderKind::Seek; });
    }

    // The turn-based Attack of a group that is not all drones (§8, §19 Q69,
    // Q71, confirmed: binary): the Move To the sector the target was in when
    // the order was given (Order::location; done at once when the group is
    // already there), then an attack wherever the group stands, whatever became
    // of the target; with no sector recorded it attacks where it stands. The
    // attack costs 1 movement point and one move's supply per member and runs
    // a battle check at once, and the order is used up; without movement left
    // it is removed doing nothing. "Used up" is done, so with Repeat on it goes
    // to the end of the list (spec 04 §19.2 Q77, confirmed: binary). Nobody decloaks:
    // only the vehicles under the Ship Cloaking minister lower their cloaks for
    // it and raise them again afterwards if they can (recloak).
    Exec placeAttack(Group& g, Order& o) {
        if (validLocation(s_, o.location)) {
            const Travel t = travel(g, o.location);
            if (t == Travel::Reached) return Exec::Moved;  // the attack needs the next action's movement
            if (t != Travel::Arrived) return afterTravel(g, o, t);
        }
        if (remaining(g) <= 0 || immobile(g)) {
            ctx_.log(g.owner, LogCategory::Combat, std::format("{}: no movement left to attack", name(g)), "The Attack order was removed.", where(g),
                     picture(g));
            return Exec::Done;
        }
        bool lowered = false;
        for (VehicleId id : g.members) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v)) continue;
            if (v->status == VehicleStatus::Cloaked && underCloakingMinister(*v)) {
                v->status = VehicleStatus::Normal;
                recloak_.push_back(id);
                lowered = true;
            }
            v->movement = std::max(0, v->movement - 1);
            spendSupply(r_, s_, *v, moveSupplyCost(r_, s_, *v));
        }
        if (lowered) decloaked(where(g).system);
        checkHere_ = true;  // the attack runs a battle check (runLive)
        return Exec::Acted;
    }

    // The Ship Cloaking minister handles the vehicle or colony: every one of a
    // computer player, and a human's under minister control while that
    // minister is on (spec 03 §6.4, §19 Q69; spec 05 §7.1; spec 01 §6.9).
    bool underCloakingMinister(EmpireId owner, bool minister) const {
        if (!owner.valid() || owner.index() >= s_.empires.size()) return false;
        const Empire& e = s_.empire(owner);
        if (e.kind != PlayerKind::Human || e.ministerAll) return true;
        return (e.ministers & ministerBit(Minister::ShipCloaking)) != 0 && minister;
    }
    bool underCloakingMinister(const Vehicle& v) const { return underCloakingMinister(v.owner, v.minister); }
    bool colonyUnderCloakingMinister(const Colony& c) const { return underCloakingMinister(c.owner, c.minister); }

    // After the Attack's battle check, the vehicles the Ship Cloaking minister
    // decloaked for it cloak again when they still can (§8 Cloak: a working
    // cloak and supply above 0).
    void recloak() {
        for (VehicleId id : recloak_)
            if (Vehicle* v = s_.vehicle(id); v && alive(*v) && v->status == VehicleStatus::Normal && canCloak(r_, s_, *v) &&
                                             (v->supply > 0 || vehicleHasUnlimitedSupply(r_, s_, *v)))
                v->status = VehicleStatus::Cloaked;
        recloak_.clear();
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
            ctx_.log(g.owner, LogCategory::Misc, std::format("{}: {}", name(g), why), {}, where(g), picture(g));
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

    // Colonize (§8, confirmed: binary): given as Load Cargo, Move To and
    // Colonize, so nothing about the planet is checked before the group is in
    // its sector (spec 06 §7 Q100). There the reasons are tested in the
    // game's order, each time the Colonize heads the list: the planet gone,
    // unseen or elsewhere; an asteroid field; no movement left (the order
    // waits); a colony there, of any empire; a cloaked member; no member able
    // to colonize it. The colonizer is the last suitable member.
    Exec colonize(Group& g, Order& o) {
        const bool known = o.object.valid() && o.object.index() < s_.galaxy.objects.size();
        if (o.amount == 0) {
            // An order that reached a list without its Load Cargo (tools,
            // tests): the colonists come aboard where it starts, on the last
            // member whose module suits the planet, else the last member.
            VehicleId loader = g.members.empty() ? VehicleId{} : g.members.back();
            if (known)
                for (VehicleId id : g.members)
                    if (colonizerProblem(r_, s_, *s_.vehicle(id), s_.galaxy.object(o.object)).empty()) loader = id;
            if (loader.valid()) loadColonists(ctx_, loader);
            o.amount = 1;
        }
        if (known && locationOf(s_.galaxy, o.object) != where(g)) {
            // A Colonize away from its planet: a list set without the
            // expansion cmd::SetOrders makes (tools, tests); every order
            // given, the computer players' too, already is Load Cargo, Move
            // To and Colonize (spec 05 §7.5). It stands for that Move To:
            // nothing is checked before the planet's sector, even when the
            // planet has been taken, changed or destroyed (spec 05 §7 Q35,
            // Q78). The step that arrives completes the Move To, and the
            // Colonize is carried out in the same action; a turn-based game
            // runs it after the step's battle check, as it runs the next
            // order (runLive).
            const Travel t = travel(g, locationOf(s_.galaxy, o.object));
            if (t != Travel::Reached || live_) return afterTravel(g, o, t);
        }
        // "Seen" (§8, §19 Q80, confirmed: binary): the detection rule of spec
        // 01 §6.3 for the group's owner applied to the planet, colonized or
        // not, with the sensors of this moment, the group's own counted (sight
        // is live: it follows every step and warp). With the omnipresent view
        // the explored test drops and EM Active is at least 1 (spec 01 §6.5).
        // A planet that a storm, a nebula or an undetected colony's cloak
        // hides, like one that is gone or elsewhere, leaves no planet here;
        // the test comes before the movement test, so it fails at once.
        const bool seen = known && sight::canSeeColony(r_, s_, g.owner, o.object);
        if (!seen || locationOf(s_.galaxy, o.object) != where(g)) return colonizeFailed(g, "There is no planet here to colonize.");
        const SpaceObject& planet = s_.galaxy.object(o.object);
        if (planet.kind != ObjectKind::Planet) return colonizeFailed(g, std::format("{} cannot be colonized.", planet.name));
        // Carried out like any order, on an acting day with movement left, so
        // the colony exists during the later phases; without movement it
        // waits (spec 05 §8 step 5, open question 24; spec 03 §8).
        if (remaining(g) <= 0) return Exec::Wait;
        if (s_.colony(o.object)) return colonizeFailed(g, std::format("{} is already a colony.", planet.name));
        if (any(g, [](const Vehicle& v) { return v.status == VehicleStatus::Cloaked; })) return colonizeFailed(g, "A cloaked ship cannot colonize.");
        VehicleId colonizer;
        for (VehicleId id : g.members)
            if (colonizerProblem(r_, s_, *s_.vehicle(id), planet).empty()) colonizer = id;
        if (!colonizer.valid()) return colonizeFailed(g, std::format("Unable to colonize {} planets.", planet.surface));
        foundColony(ctx_, colonizer, o.object);
        return Exec::Done;
    }

    // A failed Colonize clears the whole list, as every failed order (§8).
    // Where the player reads why (spec 03 §8, confirmed: binary): in a
    // turn-based game the human whose turn it is gets a message box titled
    // "Colonize" at once and nothing is logged, a computer player nothing;
    // in a simultaneous game the owner, computer players too, gets one entry,
    // "Unable to Colonize", in the Colonization Minister's words, with the
    // picture OrdersNotCompleted (spec 06 §4.1).
    Exec colonizeFailed(const Group& g, std::string_view why) {
        if (live_) {
            if (live_->ask) ctx_.messages.push_back(PlayerMessage{g.owner, "Colonize", std::string(why)});
        } else {
            const Location here = where(g);
            ctx_.log(g.owner, LogCategory::Misc, "Unable to Colonize",
                     std::format("The Colonization Minister reports that {} could not found a colony in the {} system. {}", name(g),
                                 s_.galaxy.system(here.system).name, why),
                     here, "OrdersNotCompleted");
        }
        return Exec::Fail;
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
                    case OrderKind::DropCargo: moved += dropCargo(ctx_, id, o.design, p.amount, g.members, o.object, o.vehicle); break;
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
                     {}, where(g), picture(g));
        }
        return Exec::Acted;
    }

    // Cloaking needs a working part of level 2 or more and supply above 0; it
    // costs nothing now (the parts' supply is charged every end of turn) (§8).
    Exec cloak(Group& g, Order& o) {
        bool changed = false, lowered = false;
        for (VehicleId id : g.members) {
            Vehicle* v = s_.vehicle(id);
            if (o.kind == OrderKind::Decloak) {
                if (v->status == VehicleStatus::Cloaked) {
                    v->status = VehicleStatus::Normal;
                    lowered = true;
                }
                changed = true;
                continue;
            }
            if (v->status != VehicleStatus::Normal || !canCloak(r_, s_, *v)) continue;
            if (v->supply <= 0 && !vehicleHasUnlimitedSupply(r_, s_, *v)) continue;
            v->status = VehicleStatus::Cloaked;
            v->queue.items.clear();  // cloaked ships cannot build (spec 01 §6.4)
            changed = true;
        }
        if (lowered) decloaked(where(g).system);
        return changed ? Exec::Acted : fail(g, o, "No working cloaking device, or no supplies.");
    }

    // Sweep Mines (§8, §12, confirmed: binary): the whole mine encounter of the
    // sector runs again for the group (a sweeper group in a tagged minefield
    // decloaks, its uncloaked members sweep, the remaining hostile mines
    // strike); then, if the group survived, 1 movement point (if any is left)
    // and one move's supply. Always done.
    Exec sweep(Group& g) {
        const Location here = where(g);
        if (decloakSweepers(r_, s_, here, g.members)) decloaked(here.system);
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

    // Use Component (spec 03 §8, confirmed: binary): only the group's first
    // member uses the part at the recorded position, whatever part sits there
    // now: the acting vehicle itself, or for a fleet the first of its members
    // at the fleet's location in object order. Nothing checks that the part is
    // intact or the vehicle not mothballed; no supply is charged and nothing
    // is logged. Emergency energy gives, in a turn-based game, V1 more
    // movement this turn without a cap; in a simultaneous one V1 on the day
    // counter, up to V1 more actions at one a day. It needs no movement, so it
    // completes, never fails, and the next order runs in the same action.
    Exec useComponentOrder(Group& g, Order& o) {
        VehicleId user = g.actor;
        if (const Fleet* f = g.fleet.valid() ? s_.fleet(g.fleet) : nullptr) {
            user = {};
            for (VehicleId id : fleetGroup(s_, *f))
                if (!user.valid() || objectOrderKey(*s_.vehicle(id)) < objectOrderKey(*s_.vehicle(user))) user = id;
        }
        const int n = user.valid() ? useComponent(ctx_, user, o.amount) : 0;
        if (n > 0 && !heldInPlace(s_, *s_.vehicle(user))) {
            if (live_) {
                s_.vehicle(user)->movement += n;
                bonus_[user] += n;
            } else {
                counters_[user].add(n);
            }
        }
        return Exec::Done;
    }

    // Convert Resources (spec 02 §5.6, confirmed: binary): nothing happens
    // unless the source and the target are each minerals, organics or
    // radioactives. The amount is cut to the empire's stock of the source;
    // what is left above 0 goes from the source, and the target gains it less
    // the colony's loss, read now (economy::conversionGain), uncapped (the
    // storage cap comes later in the turn). It costs nothing else and never
    // fails. In a simultaneous game each order that converts something is
    // logged; a turn-based player gets no entry.
    Exec convert(Group& g, const Order& o) {
        const Colony* c = s_.colony(g.planet);
        if (!c || o.from >= kResources.size() || o.to >= kResources.size()) return Exec::Done;
        Resources& bank = s_.empire(g.owner).stockpile;
        const Resource from = kResources[o.from], to = kResources[o.to];
        const int64_t amount = std::min<int64_t>(o.amount, bank[from]);
        if (amount <= 0) return Exec::Done;
        const int64_t gain = economy::conversionGain(amount, economy::conversionLoss(r_, *c));
        bank[from] -= amount;
        bank[to] += gain;
        if (s_.options.simultaneous) {
            const SpaceObject& planet = s_.galaxy.object(g.planet);
            ctx_.log(g.owner, LogCategory::Misc, "Resources Converted",
                     std::format("The Resource Minister reports that {} in the {} system has converted {} {} into {} {}.", planet.name,
                                 s_.galaxy.system(planet.system).name, amount, displayName(from), gain, displayName(to)),
                     locationOf(s_.galaxy, g.planet), logpicture::planet(g.planet));
        }
        return Exec::Done;
    }

    // Self-Destruct (spec 03 §8, §15): every member that can is destroyed,
    // counting as scrapped, not lost (spec 04 §15).
    Exec selfDestruct(Group& g, Order& o) {
        bool any = false;
        for (VehicleId id : std::vector<VehicleId>(g.members)) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v) || !canSelfDestruct(r_, s_, *v)) continue;
            carryOutScrapAction(ctx_, *v, ScrapAction::SelfDestruct);
            any = true;
        }
        if (!any) return fail(g, o, "It cannot self-destruct.");
        prune(g);
        return g.stopped ? Exec::Gone : Exec::Acted;
    }

    // The Scrap window's actions given in a simultaneous game (spec 03 §15,
    // confirmed: binary): carried out at the vehicle's first action, by the
    // acting vehicle alone even when it heads an ad-hoc group, with no
    // movement needed or spent. The test is made again now: a failed one
    // fails the order and clears the list, with no message for Analyze and
    // Fire On.
    Exec scrapWindowAction(Group& g, Order& o) {
        const ScrapAction a = *scrapActionOf(o.kind);
        Vehicle* v = s_.vehicle(g.actor);
        if (!v || !alive(*v)) return Exec::Gone;
        if (const std::string why = scrapActionProblem(r_, s_, g.owner, *v, a, o.design); !why.empty())
            return a == ScrapAction::Analyze || a == ScrapAction::FireOn ? Exec::Fail : fail(g, o, why + ".");
        carryOutScrapAction(ctx_, *v, a, o.design);
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
    // The ministers' Seek after an object is such a pursuit; the stored
    // Attack, which names no target, is none.
    void endPursuits() {
        auto finished = [&](EmpireId owner, const std::vector<Order>& list) {
            if (list.empty()) return false;
            const Order& o = list.front();
            if (o.kind != OrderKind::Attack && o.kind != OrderKind::Seek) return false;
            if (!o.vehicle.valid() && !o.object.valid()) return false;
            return pursuitOver(s_, owner, o);
        };
        for (Vehicle& v : s_.vehicles)
            if (alive(v) && finished(v.owner, v.orders)) v.orders.erase(v.orders.begin());
    }

    // ---- Combat ---------------------------------------------------------------------------------------

    // One step into a sector: the group's members as they entered.
    struct Entry {
        std::vector<VehicleId> members;
        std::vector<VehicleId> holders;   // the lists a failure clears (Group::holders)
        EmpireId owner;
        Location where;
        std::string name;
        bool pursuit = false;   // a step of an Attack pursuit (spec 04 §19.2 Q76)
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
                if (!hooks_.possible(r_, s_, where, {}) || !freshBattle(where)) continue;
                const auto before = marks(where);
                const size_t records = s_.combats.size();
                hooks_.resolve(ctx_, where, entering(where), {});
                afterBattle(where, records, before);
                rememberBattle(where, records);
            }
        entered_.clear();
        s_.removeDeadVehicles();
    }

    // Combat neither stops movement nor clears orders: lists are kept, and only
    // a Sentry order at the head of a participant's list is removed, even with
    // Repeat on (§6.3, §6.4, confirmed: binary); in a turn-based game the
    // group whose order started the battle (`starter`) is not one of those
    // other participants (spec 04 §2). A group that met a minefield stops and
    // its order fails.
    void afterBattle(Location where, size_t recordsBefore, const std::map<VehicleId, std::pair<int, std::vector<int>>>& before,
                     std::span<const VehicleId> starter = {}) {
        std::set<VehicleId> fought;
        for (size_t i = recordsBefore; i < s_.combats.size(); ++i)
            if (s_.combats[i].location == where)
                for (const CombatPiece& p : s_.combats[i].pieces)
                    if (p.vehicle.valid()) fought.insert(p.vehicle);
        if (!fought.empty() && std::find(ctx_.battleSites.begin(), ctx_.battleSites.end(), where) == ctx_.battleSites.end())
            ctx_.battleSites.push_back(where);
        for (VehicleId id : fought) {
            Vehicle* v = s_.vehicle(id);
            if (!v || !alive(*v) || std::find(starter.begin(), starter.end(), id) != starter.end()) continue;
            std::vector<Order>& list = v->orders;  // a fleet member's copy too (spec 03 §8)
            if (!list.empty() && list.front().kind == OrderKind::Sentry) list.erase(list.begin());
        }
        // Minefields: a group that stepped in today, was hurt and fought no
        // battle (inferred detection). A pursuit only stops moving and keeps its
        // orders (spec 03 §6.4, spec 04 §19.2 Q76).
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
            ctx_.log(e.owner, LogCategory::Combat, std::format("{} stopped by a minefield", e.name),
                     e.pursuit ? "It keeps its orders." : "Its orders were cancelled.", where, "MineExplosion");
            if (!e.pursuit) clearListsOf(e.holders);
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
    // head of the list counts (confirmed: binary, spec 05 open question 45).
    static constexpr int kLiveOrderLimit = 21;

    void liveActor(ActorRef ref) {
        // The group's orders start to run here: the view of a human player
        // following its own moves selects it (spec 06 §2.7).
        if (const Vehicle* v = s_.vehicle(ref.vehicle)) ctx_.liveSteps.push_back(LiveStep{v->owner, v->id, v->fleet, {}, v->location, false});
        else if (const Colony* c = s_.colony(ref.planet))
            ctx_.liveSteps.push_back(LiveStep{c->owner, {}, {}, ref.planet, locationOf(s_.galaxy, ref.planet), false});
        int completed = 0;  // orders that left the head of the list, chained ones included
        carried_.reset();
        struct EndRun {
            std::optional<Order>& carried;
            ~EndRun() { carried.reset(); }  // a carried Move To lasts for this run only
        } endRun{carried_};
        for (int n = 0; n < kLiveActionLimit; ++n) {
            const size_t steps = entered_.size();
            checkHere_ = false;
            Group g;
            const std::optional<Exec> e = act(ref, g, &completed);
            if (!e) return;
            bool fought = false;
            if (entered_.size() > steps) fought = entryCombat(g);
            else if (checkHere_) orderCombat(g);
            recloak();
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

    // Fights the battle of `where` now (mines strike `entering` first) if the
    // check passes; `starter`: the group whose order started it.
    void fight(Location where, const std::vector<VehicleId>& entering, const combat::BattleCheck& check,
               std::span<const VehicleId> starter = {}) {
        const auto before = marks(where);
        const size_t records = s_.combats.size();
        hooks_.resolve(ctx_, where, entering, check);
        afterBattle(where, records, before, starter);
        s_.removeDeadVehicles();
    }

    bool combatHere(Location where, const combat::BattleCheck& check) const {
        return hooks_.possible && hooks_.resolve && hooks_.possible(r_, s_, where, check);
    }

    // Whether a battle was fought at `where` since the record count `before`.
    bool battleSince(Location where, size_t before) const {
        for (size_t i = before; i < s_.combats.size(); ++i)
            if (s_.combats[i].location == where) return true;
        return false;
    }

    // A movement step (a warp jump included) runs a battle check once the
    // mines have struck. A battle is fought at once; the group's order fails
    // and every member's list is cleared, an Attack at the head included
    // (spec 03 §6.4, spec 04 §2, confirmed: binary). A pursuit (a Seek) only
    // stops moving for this run of its list, after a battle or mines alike;
    // its order and list are kept (spec 04 §19.2 Q76, confirmed: binary). True
    // when the group's run ends here.
    bool entryCombat(Group& g) {
        prune(g);
        if (g.stopped) return true;
        const Location here = where(g);
        const bool pursuit = !entered_.empty() && entered_.back().pursuit;
        const combat::BattleCheck check{g.members};
        if (!combatHere(here, check)) return false;
        const size_t records = s_.combats.size();
        fight(here, entering(here), check);
        prune(g);
        if (!battleSince(here, records)) return g.stopped || pursuit;  // only mines struck
        if (!g.stopped && !pursuit)
            if (const std::vector<Order>* list = orders(g); list && !list->empty()) {
                fail(g, list->front(), "Combat on entering the sector.");
                setLists(g, {});
            }
        g.stopped = true;
        return true;
    }

    // The Attack order, or a Seek order at its target, runs a battle check
    // where the group stands; the group keeps the rest of its list (spec 04 §2).
    void orderCombat(Group& g) {
        prune(g);
        if (g.stopped) return;
        const Location here = where(g);
        const combat::BattleCheck check{g.members};
        if (combatHere(here, check)) fight(here, {}, check, g.members);
    }

    // Objects of an empire `e` is hostile to in a sector, that `e` sees: ships,
    // bases, unit groups (mines included) and colonies (spec 03 §6.2).
    bool enemiesAt(EmpireId e, Location l) const {
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.location == l && hostile(s_, e, v.owner) && sight::canSeeVehicle(r_, s_, e, v)) return true;
        for (ObjectId o : planetsAt(s_, l))
            if (const Colony* c = s_.colony(o); c && hostile(s_, e, c->owner) && sight::canSeeColony(r_, s_, e, o)) return true;
        return false;
    }

    // Turn-based games: a human player's group stops before an in-system step
    // into a sector with enemies it sees, and the player is asked (spec 03
    // §6.2, confirmed: binary). Groups of drones only, and groups whose members
    // are all cloaked, always enter; the approach steps of an Attack ask too.
    bool asks(const Group& g, Location next) {
        if (!live_ || !live_->ask || g.planet.valid() || onlyDrones(g)) return false;
        if (all(g, [](const Vehicle& v) { return v.status == VehicleStatus::Cloaked; })) return false;
        // A tagged group is asked once, for all its vehicles (spec 03 §8).
        const EntryQuestion q = g.tagged ? EntryQuestion{{}, {}, next, live_->tagged} : EntryQuestion{g.fleet.valid() ? VehicleId{} : g.lead, g.fleet, next, {}};
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
    std::vector<Location> touched_;                     // simultaneous: sectors where an object acted today
    std::vector<Entry> entered_;                        // steps made today
    std::map<Location, BattleMemo> lastBattle_;         // the latest battle per location this phase
    bool checkHere_ = false;                            // turn-based: the last action's Attack or Seek runs a battle check
    int day_ = 0;                                       // simultaneous games: the movement day being played
    // Turn-based: a Move To whose lists the Ship Orders options emptied after
    // a warp transit; it goes on stepping in this run only (spec 03 §6.4, §19 Q77).
    std::optional<Order> carried_;
    std::vector<VehicleId> recloak_;                    // turn-based: decloaked by the Ship Cloaking minister for an Attack
    bool pursuing_ = false;                             // the steps being made are an Attack pursuit's
    UnitBudget budget_;
};

// King steps from `at` toward `target`, stopping there.
Sector stepToward(Sector at, Sector target, int64_t steps) {
    for (int64_t k = 0; k < steps && at != target; ++k)
        at = Sector{at.x + (target.x > at.x) - (target.x < at.x), at.y + (target.y > at.y) - (target.y < at.y)};
    return at;
}

} // namespace

std::optional<Sector> inSystemStep(const Rules& r, const GameState& s, EmpireId mover, Location here, Sector target, Rng& rng) {
    // Around a destructive centre the cost map chooses, falling back to the
    // greedy step when no square around the group was reached (spec 03 §6.2,
    // confirmed: binary).
    if (destructiveCentre(s, here.system) > 0)
        if (const auto c = centreStep(centreCostMap(target, centreZone(s, here.system)), here.sector); c && *c != here.sector) return c;
    return greedyStep(r, s, mover, here, target, rng);
}

std::vector<int> actionDays(int speed, DayCounterMode mode) {
    return mode == DayCounterMode::Exact ? daysActed<DayCounterMode::Exact>(speed) : daysActed<DayCounterMode::Double>(speed);
}

int headingFor(Sector from, Sector to) {
    const int dx = to.x - from.x, dy = to.y - from.y;
    if (dx == 0 && dy == 0) return 0;
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    const int major = std::max(ax, ay), minor = std::min(ax, ay);
    // Along the major axis when minor / major < tan 22.5° = √2 − 1, that is
    // (minor + major)² < 2 · major²; never a tie with whole squares.
    const bool straight = (minor + major) * (minor + major) < 2 * major * major;
    if (straight) {
        if (ay >= ax) return dy < 0 ? 0 : 4;
        return dx > 0 ? 2 : 6;
    }
    if (dx > 0) return dy < 0 ? 1 : 3;
    return dy > 0 ? 5 : 7;
}

int movesPerTurn(const GameState& s, int speed) {
    if (speed <= 0) return 0;
    if (!s.options.simultaneous) return speed;
    return static_cast<int>(actionDays(std::min(speed, kDaysPerTurn + 1)).size());
}

namespace {

// Movement points back to the maximum: every vehicle, or one empire's.
// The movement each vehicle gets at a turn start (of every empire, or of `only`).
std::vector<std::pair<VehicleId, int>> refill(const Rules& r, const GameState& s, std::optional<EmpireId> only) {
    std::vector<std::pair<VehicleId, int>> out;
    std::map<VehicleId, int> given;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && (!only || v.owner == *only)) given[v.id] = turnMovement(r, s, v);  // 0 while held by sabotage or an event
    // Fleet members at the fleet's location get the lowest maximum among them (§6.3 step 1).
    for (const Fleet& f : s.fleets) {
        if (only && f.owner != *only) continue;
        const std::vector<VehicleId> here = fleetMembersAt(s, f);
        int lowest = INT_MAX;
        for (VehicleId id : here) lowest = std::min(lowest, given[id]);
        for (VehicleId id : here) given[id] = lowest;
    }
    out.assign(given.begin(), given.end());
    return out;
}

void refillMovement(TurnContext& ctx, std::optional<EmpireId> only) {
    for (const auto& [id, points] : refill(ctx.rules, ctx.state, only))
        if (Vehicle* v = ctx.state.vehicle(id)) v->movement = points;
}

} // namespace

void startTurn(TurnContext& ctx) { refillMovement(ctx, std::nullopt); }

std::vector<std::pair<VehicleId, int>> refilledMovement(const Rules& r, const GameState& s, EmpireId empire) { return refill(r, s, empire); }

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
            const Location before = v.location;
            if (pull > 0) v.location.sector = stepToward(v.location.sector, centre, pull);
            if (drift > 0) v.location.sector = stepToward(v.location.sector, driftTarget, drift);
            if (v.location != before) fleetMemberMoved(s, v);  // the fleet's location follows (spec 03 §9)
        }
        if (damage <= 0) continue;
        for (VehicleId id : here) {
            const Vehicle* v = s.vehicle(id);
            if (!v || !alive(*v) || v->location.sector != centre) continue;
            const std::string name = v->name;
            const EmpireId owner = v->owner;
            const Location where = v->location;
            if (!hurt(ctx, id, static_cast<int>(std::min<int64_t>(damage, INT_MAX)), "Torn apart at the centre of the system."))
                ctx.log(owner, LogCategory::Events, std::format("{} damaged", name), "Damaged at the centre of the system.", where, "ShipDamaged");
        }
    }
    s.removeDeadVehicles();
}

} // namespace opense4::game::movement
