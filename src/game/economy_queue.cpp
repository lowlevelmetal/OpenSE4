#include "game/economy.hpp"

#include "game/ai.hpp"
#include "game/design.hpp"
#include "game/economy_internal.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>

// Construction queues (docs/spec/02 §6): rates, costs, per-turn payment,
// completion of ships, units, facilities and upgrades, build modes.

namespace opense4::game::economy {

using namespace detail;
using xmath::pctTrunc;

namespace {

// Yard rates of a colony's space yard facilities, summed per resource (one
// yard facility per planet in practice).
bool colonyYardRates(const Rules& r, const Colony& c, Resources& out) {
    bool any = false;
    out = {};
    for (uint32_t f : c.facilities) {
        const auto ab = r.facilityAbilities(f);
        if (!hasAbility(ab, AbilityKind::SpaceYard)) continue;
        out += spaceYardRates(ab);
        any = true;
    }
    return any;
}

Resources vehicleYardRates(const Rules& r, const GameState& s, const Vehicle& v) {
    Resources out;
    if (v.status == VehicleStatus::Mothballed) return out;
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i)) out += spaceYardRates(r.componentAbilities(d.entries[i].component));
    return out;
}

bool rioting(const Rules& r, const GameState& s, const Colony& c) {
    return moodFromAnger(c.anger) == Mood::Rioting && !emotionless(r, s, c.owner);
}

// Why a queue cannot work this turn (empty = it can).
std::string_view queueBlocked(const Rules& r, const GameState& s, const cmd::QueueTarget& t) {
    if (t.vehicle.valid()) {
        const Vehicle* v = s.vehicle(t.vehicle);
        if (!v) return "gone";
        if (v->status == VehicleStatus::Cloaked) return "cloaked";  // spec 02 §6.1
        if (v->status == VehicleStatus::Mothballed || !vehicleHasSpaceYard(r, s, *v)) return "no yard";
        return {};
    }
    const Colony* c = s.colony(t.planet);
    if (!c) return "gone";
    if (c->totalPopulation() <= 0) return "no population";
    if (rioting(r, s, *c)) return "rioting";
    return {};
}

} // namespace

// ---- Rates and costs ---------------------------------------------------------------------------------

Resources constructionRate(const Rules& r, const GameState& s, EmpireId e, const cmd::QueueTarget& t) {
    if (!e.valid() || e.index() >= s.empires.size()) return {};
    const Empire& emp = s.empire(e);
    const int yard = racialEffect(r, emp.race, RacialEffect::ShipyardRate);
    Resources rate;
    const ConstructionQueue* q = nullptr;
    if (t.vehicle.valid()) {
        const Vehicle* v = s.vehicle(t.vehicle);
        if (!v || v->owner != e) return {};
        // Ship yards: aptitude, culture and SY Rate traits; no population, no Planetary SY Rate.
        const Resources base = vehicleYardRates(r, s, *v);
        for (size_t k = 0; k < 3; ++k) rate.v[k] = pctTrunc(base.v[k], 100 + yard);
        q = &v->queue;
    } else {
        const Colony* c = s.colony(t.planet);
        if (!c || c->owner != e) return {};
        if (c->totalPopulation() <= 0 || rioting(r, s, *c)) return {};
        const int popShipyard = populationModifier(r, c->totalPopulation()).shipyard - 100;
        Resources base;
        int64_t m = 0;
        if (colonyYardRates(r, *c, base)) {
            m = yard + r.traitValue(emp.race, "Planetary SY Rate") + popShipyard;
        } else {
            // No yard: the Settings base rate with only the population modifier (confirmed: binary).
            base = {r.setting("Empire Base Planet Mineral Usage Rate", 2000), r.setting("Empire Base Planet Organic Usage Rate", 2000),
                    r.setting("Empire Base Planet Radioactive Usage Rate", 2000)};
            m = popShipyard;
        }
        for (size_t k = 0; k < 3; ++k) rate.v[k] = m != 0 ? pctTrunc(base.v[k], std::max<int64_t>(0, 100 + m)) : base.v[k];
        q = &c->queue;
    }
    if (const int bonus = ai::constructionBonusPercent(s, e); bonus != 100)
        for (int64_t& v : rate.v) v = pctTrunc(v, bonus);
    for (int64_t& v : rate.v) v = std::max<int64_t>(0, v);
    if (q->emergency) {
        const int64_t pct = r.setting("Construction Queue Emergency Build Rate Percent", 150);
        for (int64_t& v : rate.v) v = pctTrunc(v, pct);
    } else if (q->slowTurns > 0) {
        const int64_t pct = r.setting("Construction Queue Slow Build Rate Percent", 25);
        for (int64_t& v : rate.v) v = pctTrunc(v, pct);
    }
    return rate;
}

int upgradeCount(const Rules& r, const Colony& c, uint32_t target) {
    if (target >= r.data().facilities.size()) return 0;
    const ruleset::Facility& t = r.facility(target);
    int n = 0;
    for (uint32_t f : c.facilities) n += f < r.data().facilities.size() && r.facility(f).family == t.family && r.facility(f).romanNumeral < t.romanNumeral;
    return n;
}

QueueItem upgradeItem(const Rules& r, const Colony& c, uint32_t target) {
    QueueItem item;
    item.kind = QueueItem::Kind::Upgrade;
    item.facility = target;
    item.count = upgradeCount(r, c, target);
    return item;
}

bool itemObsolete(const Rules& r, const GameState& s, const cmd::QueueTarget& t, const QueueItem& item) {
    const Vehicle* ship = t.vehicle.valid() ? s.vehicle(t.vehicle) : nullptr;
    const Colony* c = t.vehicle.valid() ? nullptr : s.colony(t.planet);
    if (!ship && !c) return false;  // the queue itself is gone
    // A ship's queue loses its facility items only: its ship and base items
    // stay, even while it is mothballed or its yard component is destroyed
    // (confirmed: binary, spec 02 §6.1, §13 Q53).
    if (ship) return item.kind != QueueItem::Kind::Vehicle;
    // A colony without a working space yard loses its ship and base items and
    // its upgrades with no lower-level facility left. Unit and facility items
    // stay, an upgrade with nothing left stays on a colony with a yard (§6.6),
    // and so does an item whose design no longer exists.
    if (colonyHasSpaceYard(r, *c)) return false;
    switch (item.kind) {
        case QueueItem::Kind::Vehicle:
            if (!item.design.valid() || item.design.index() >= s.designs.size()) return false;
            return isShipOrBase(r.hull(s.design(item.design).hull).type);
        case QueueItem::Kind::Facility: return false;
        case QueueItem::Kind::Upgrade: return item.facility < r.data().facilities.size() && upgradeCount(r, *c, item.facility) == 0;
    }
    return false;
}

Resources itemCost(const Rules& r, const GameState& s, EmpireId, const cmd::QueueTarget&, const QueueItem& item) {
    switch (item.kind) {
        case QueueItem::Kind::Vehicle: {
            if (!item.design.valid() || item.design.index() >= s.designs.size()) return {};
            const Resources unit = designCost(r, s.design(item.design));
            const int64_t count = std::max(1, item.count);
            return {unit.v[0] * count, unit.v[1] * count, unit.v[2] * count};
        }
        case QueueItem::Kind::Facility:
            if (item.facility >= r.data().facilities.size()) return {};
            return Resources::from(r.facility(item.facility).cost).percent(100 * std::max(1, item.count));
        case QueueItem::Kind::Upgrade: {
            // trunc(the target's cost × Upgrade %) per facility, times the count stored
            // when the item was queued (confirmed: binary).
            if (item.facility >= r.data().facilities.size()) return {};
            const int64_t n = std::max(0, item.count);
            const int64_t pct = r.setting("Upgrade Facility Cost Percent", 50);
            const Resources cost = Resources::from(r.facility(item.facility).cost);
            return {pctTrunc(cost.v[0], pct) * n, pctTrunc(cost.v[1], pct) * n, pctTrunc(cost.v[2], pct) * n};
        }
    }
    return {};
}

int turnsToComplete(const Resources& remaining, const Resources& rate) {
    int turns = 0;
    for (size_t i = 0; i < 3; ++i) {
        if (remaining.v[i] <= 0) continue;
        if (rate.v[i] <= 0) return -1;
        turns = std::max(turns, static_cast<int>((remaining.v[i] + rate.v[i] - 1) / rate.v[i]));
    }
    return turns;
}

// ---- Queue processing ----------------------------------------------------------------------------------

namespace {

// Processing class of a queue by its top item (spec 02 §6.3): facilities that
// deliver or produce go first, in this order; everything else after.
int queuePriority(const Rules& r, const ConstructionQueue& q) {
    static constexpr std::array<AbilityKind, 5> kFirst{AbilityKind::Spaceport, AbilityKind::ResourceGenMinerals,
                                                       AbilityKind::ResourceGenOrganics, AbilityKind::ResourceGenRadioactives,
                                                       AbilityKind::SupplyGeneration};
    if (q.items.empty() || q.items.front().kind != QueueItem::Kind::Facility || q.items.front().facility >= r.data().facilities.size())
        return static_cast<int>(kFirst.size());
    const auto ab = r.facilityAbilities(q.items.front().facility);
    for (size_t i = 0; i < kFirst.size(); ++i)
        if (hasAbility(ab, kFirst[i])) return static_cast<int>(i);
    return static_cast<int>(kFirst.size());
}

const ConstructionQueue* queueOf(const GameState& s, const QueueRef& q) {
    if (q.target.vehicle.valid()) {
        const Vehicle* v = s.vehicle(q.target.vehicle);
        return v ? &v->queue : nullptr;
    }
    const Colony* c = s.colony(q.target.planet);
    return c ? &c->queue : nullptr;
}

ConstructionQueue* liveQueue(GameState& s, const QueueRef& q) { return const_cast<ConstructionQueue*>(queueOf(s, q)); }

} // namespace

namespace detail {

std::vector<QueueRef> empireQueues(const Rules& r, const GameState& s, EmpireId e) {
    std::vector<QueueRef> out;
    for (const auto& c : s.colonies)
        if (c && c->owner == e) out.push_back({cmd::QueueTarget{c->planet, {}}, locationOf(s.galaxy, c->planet)});
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && !v.queue.items.empty()) out.push_back({cmd::QueueTarget{{}, v.id}, v.location});
    std::stable_sort(out.begin(), out.end(), [&](const QueueRef& a, const QueueRef& b) {
        return queuePriority(r, *queueOf(s, a)) < queuePriority(r, *queueOf(s, b));
    });
    return out;
}

Resources projectQueueUsage(const Rules& r, const GameState& s, EmpireId e, const QueueRef& q, const ConstructionQueue& queue,
                            Resources& treasury) {
    // The top item once the items the queue can no longer build are gone (spec 02 §6.1).
    const auto first = std::find_if(queue.items.begin(), queue.items.end(),
                                    [&](const QueueItem& item) { return !itemObsolete(r, s, q.target, item); });
    if (queue.onHold || first == queue.items.end() || !queueBlocked(r, s, q.target).empty()) return {};
    const QueueItem& top = *first;
    const Resources use = max(min(constructionRate(r, s, e, q.target), itemCost(r, s, e, q.target, top) - top.spent), Resources{});
    if (!treasury.covers(use)) return {};
    treasury -= use;
    return use;
}

} // namespace detail

namespace {

enum class Outcome { Done, Blocked, Invalid };

std::string placeName(const GameState& s, const QueueRef& q) {
    if (q.target.vehicle.valid()) {
        const Vehicle* v = s.vehicle(q.target.vehicle);
        return v ? v->name : std::string("a space yard ship");
    }
    return s.galaxy.object(q.target.planet).name;
}

// Some object in the sector, so that location mood triggers find the colonies there.
ObjectId anchorAt(const GameState& s, const QueueRef& q) {
    if (!q.target.vehicle.valid()) return q.target.planet;
    const auto here = planetsAt(s, q.location);
    return here.empty() ? ObjectId{} : here.front();
}

struct Holder {
    Cargo* cargo = nullptr;
    int64_t free = 0;
};

// Places one unit at a time: in the builder's cargo if it fits, else in another
// planet or ship of the empire in the same sector (spec 02 §6.5, spec 03 §1). A
// unit that finds no room is not built, with a "No Storage Available" message
// of its own. Returns whether the last unit was placed. The other holders come
// in the game's object order, planets and ships mixed (spec 02 §13 Q52).
bool placeUnits(TurnContext& ctx, EmpireId e, const QueueRef& q, DesignId design, int count) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const int64_t size = std::max<int64_t>(0, r.hull(s.design(design).hull).tonnage);
    std::vector<Holder> holders;
    Cargo* builder = nullptr;
    if (q.target.vehicle.valid()) {
        if (Vehicle* v = s.vehicle(q.target.vehicle)) {
            builder = &v->cargo;
            holders.push_back({builder, vehicleCargoCapacity(r, s, *v) - cargoSpaceUsed(r, s, v->cargo)});
        }
    } else if (Colony* c = s.colony(q.target.planet)) {
        builder = &c->cargo;
        holders.push_back({builder, colonyCargoCapacity(r, s, *c) - cargoSpaceUsed(r, s, c->cargo)});
    }
    // Then the empire's other planets and ships in the sector, in the game's
    // object order, planets and ships mixed: the slots of the one object list
    // (spec 02 §6.5, §13 Q52, confirmed: binary).
    for (const ObjectRef& ref : objectOrder(s)) {
        if (ref.object.valid()) {
            if (Colony* c = s.colony(ref.object); c && c->owner == e && &c->cargo != builder && locationOf(s.galaxy, ref.object) == q.location)
                holders.push_back({&c->cargo, colonyCargoCapacity(r, s, *c) - cargoSpaceUsed(r, s, c->cargo)});
            continue;
        }
        Vehicle& v = *s.vehicle(ref.vehicle);
        if (v.owner == e && v.location == q.location && v.count > 0 && &v.cargo != builder && isShipOrBase(vehicleType(r, s, v)))
            holders.push_back({&v.cargo, vehicleCargoCapacity(r, s, v) - cargoSpaceUsed(r, s, v.cargo)});
    }

    const std::string where = placeName(s, q);
    const std::string name = s.design(design).name;
    int placed = 0;
    bool last = false;
    for (int k = 0; k < count; ++k) {
        auto h = std::find_if(holders.begin(), holders.end(), [&](const Holder& x) { return x.free >= size; });
        last = h != holders.end();
        if (!last) {
            ctx.log(e, LogCategory::Construction, std::format("No Storage Available at {}", where),
                    std::format("A {} found no cargo space in the sector and was not built.", name), q.location);
            continue;
        }
        h->free -= size;
        auto it = std::find_if(h->cargo->units.begin(), h->cargo->units.end(), [&](const UnitStack& u) { return u.design == design; });
        if (it == h->cargo->units.end()) h->cargo->units.push_back({design, 1});
        else ++it->count;
        ++placed;
    }
    s.design(design).built += placed;
    if (placed > 0) s.design(design).everBuilt = true;  // built at least once (spec 05 §2.3)
    if (placed > 0)
        ctx.log(e, LogCategory::Construction, std::format("{} x {} completed", placed, name), std::format("Built at {}.", where), q.location);
    return last;
}

Outcome completeItem(TurnContext& ctx, EmpireId e, const QueueRef& q, const QueueItem& item, int autoWaypoint) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const std::string where = placeName(s, q);
    switch (item.kind) {
        case QueueItem::Kind::Vehicle: {
            if (!item.design.valid() || item.design.index() >= s.designs.size()) return Outcome::Invalid;
            const Design& d = s.design(item.design);
            const int count = std::max(1, item.count);
            if (isShipOrBase(r.hull(d.hull).type)) {
                // At the ship limit nothing is built; the progress is already spent (confirmed: binary).
                if (shipCount(r, s, e) >= s.options.maxShipsPerPlayer) {
                    ctx.log(e, LogCategory::Construction, std::format("{} cannot build {}", where, d.name),
                            "The empire has reached its limit on ships.", q.location);
                    return Outcome::Blocked;
                }
                const DesignId design = item.design;
                const int64_t tonnage = designTonnage(r, d);
                s.design(design).everBuilt = true;  // built at least once (spec 05 §2.3)
                for (int k = 0; k < count; ++k) {
                    const Vehicle& v = movement::spawnVehicle(r, s, e, design, q.location, autoWaypoint);
                    ctx.log(e, LogCategory::Construction, std::format("{} completed", v.name), std::format("Built at {}.", where), q.location);
                    ctx.mood(e, "Ship Constructed", q.location.system, anchorAt(s, q));
                    ctx.mood(e, "Any Ship Constructed");
                    gainExperience(s.empire(e), tonnage / 10);  // the hull's tonnage div 10 per ship (confirmed: binary)
                }
                return Outcome::Done;
            }
            // Units have no cap here: the units-per-player cap is checked at launch
            // (spec 03 §12). The item leaves the queue only if its last unit found
            // room; otherwise it stays with its full count (confirmed: binary).
            return placeUnits(ctx, e, q, item.design, count) ? Outcome::Done : Outcome::Blocked;
        }
        case QueueItem::Kind::Facility: {
            Colony* c = s.colony(q.target.planet);
            if (!c || item.facility >= r.data().facilities.size()) return Outcome::Invalid;
            const ruleset::Facility& f = r.facility(item.facility);
            // With a free slot the whole count is added, even past the slots; with
            // none nothing is built and nothing is said. A second space yard is
            // not checked for again here (confirmed: binary).
            if (static_cast<int>(c->facilities.size()) >= facilitySlots(r, s, *c)) return Outcome::Blocked;
            const int count = std::max(1, item.count);
            for (int k = 0; k < count; ++k) {
                c->facilities.push_back(item.facility);
                ctx.log(e, LogCategory::Construction, std::format("{} completed", f.name), std::format("Built on {}.", where), q.location);
                ctx.mood(e, "Facility Constructed", q.location.system, c->planet);
            }
            gainExperience(s.empire(e), count);  // each finished facility item adds its count (confirmed: binary)
            return Outcome::Done;
        }
        case QueueItem::Kind::Upgrade: {
            Colony* c = s.colony(q.target.planet);
            if (!c || item.facility >= r.data().facilities.size()) return Outcome::Invalid;
            // The colony's facility types in their stored order (the order in which
            // each type first appears); every lower level of the target's family
            // is converted, in that order, until the stored count is used up. With
            // fewer left, only those change: the full price was paid (confirmed: binary).
            const ruleset::Facility& target = r.facility(item.facility);
            std::vector<uint32_t> types;
            for (uint32_t f : c->facilities)
                if (std::find(types.begin(), types.end(), f) == types.end()) types.push_back(f);
            int left = std::max(0, item.count);
            int changed = 0;
            for (uint32_t type : types) {
                if (type >= r.data().facilities.size() || r.facility(type).family != target.family ||
                    r.facility(type).romanNumeral >= target.romanNumeral)
                    continue;
                for (uint32_t& f : c->facilities)
                    if (f == type && left > 0) {
                        f = item.facility;
                        --left;
                        ++changed;
                    }
            }
            ctx.log(e, LogCategory::Construction, std::format("{} upgraded", where),
                    std::format("{} facilit{} now {}.", changed, changed == 1 ? "y is" : "ies are", target.name), q.location);
            return Outcome::Done;
        }
    }
    return Outcome::Invalid;
}

// Repeat Build keeps the item only while it can still be built (spec 02 §6.3,
// confirmed: binary): a vehicle item always; a facility item while the colony
// has a free slot and, for a space yard facility, no space yard yet; never an
// upgrade.
bool stillBuildable(const Rules& r, const GameState& s, EmpireId e, const QueueRef& q, const QueueItem& item) {
    switch (item.kind) {
        case QueueItem::Kind::Vehicle: return item.design.valid() && item.design.index() < s.designs.size();
        case QueueItem::Kind::Facility: {
            const Colony* c = s.colony(q.target.planet);
            if (!c || item.facility >= r.data().facilities.size() || !r.facilityAvailable(s.empire(e), item.facility)) return false;
            if (static_cast<int>(c->facilities.size()) >= facilitySlots(r, s, *c)) return false;
            return !hasAbility(r.facilityAbilities(item.facility), AbilityKind::SpaceYard) || !colonyHasSpaceYard(r, *c);
        }
        case QueueItem::Kind::Upgrade: return false;
    }
    return false;
}

// One queue's turn (spec 02 §6.3): the top item only. If the treasury covers
// min(rate, cost - progress) of every resource, that is paid and the progress
// grows by the whole rate; otherwise nothing happens. At most one completion,
// and the overshoot is lost.
Resources runQueue(TurnContext& ctx, EmpireId e, const QueueRef& q) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    ConstructionQueue* queue = liveQueue(s, q);
    if (!queue) return {};
    // At the start of its turn a queue drops the items the removal pass takes
    // (spec 02 §6.1, §13 Q53, confirmed: binary), before the empty test, the
    // on-hold test and the rate, so also while it is on hold or cannot work
    // this turn. The processing order was fixed from every queue's top item
    // before (empireQueues).
    std::erase_if(queue->items, [&](const QueueItem& item) { return itemObsolete(r, s, q.target, item); });
    if (queue->items.empty() || queue->onHold || !queueBlocked(r, s, q.target).empty()) return {};
    const Resources rate = constructionRate(r, s, e, q.target);
    QueueItem& item = queue->items.front();
    const Resources cost = itemCost(r, s, e, q.target, item);
    const Resources use = max(min(rate, cost - item.spent), Resources{});
    Resources& bank = s.empire(e).stockpile;
    if (!bank.covers(use)) {
        std::string what;
        for (Resource res : kResources)
            if (use[res] > bank[res]) what += std::format("{}{}", what.empty() ? "" : ", ", displayName(res));
        ctx.log(e, LogCategory::Construction, std::format("Lack of Resources at {}", placeName(s, q)),
                std::format("Nothing was built this turn: the treasury is short of {}.", what), q.location);
        return {};
    }
    bank -= use;
    item.spent += rate;
    if (!item.spent.covers(cost)) return use;

    const QueueItem done = item;
    item.spent = {};  // cleared before the item is built
    const int waypoint = queue->autoWaypoint;
    const Outcome outcome = completeItem(ctx, e, q, done, waypoint);
    queue = liveQueue(s, q);  // spawning may move vehicles in memory
    if (!queue || queue->items.empty() || outcome == Outcome::Blocked) return use;  // blocked: stays, to be paid again
    if (outcome == Outcome::Done && queue->repeat && stillBuildable(r, s, e, q, done)) return use;
    queue->items.erase(queue->items.begin());
    return use;
}

// Emergency / slow build (spec 02 §6.4): one counter per queue, moved at the end
// of every turn, built or not. Emergency runs until the counter has reached the
// maximum (so maximum + 1 turns); slow mode then lasts as many turns.
void advanceQueueMode(ConstructionQueue& q, int maxTurns) {
    if (q.emergency) {
        if (q.emergencyTurns >= maxTurns) {
            q.emergency = false;
            q.slowTurns = q.emergencyTurns;
        } else {
            ++q.emergencyTurns;
        }
    } else if (q.slowTurns > 0) {
        --q.slowTurns;
    }
}

} // namespace

void runConstruction(TurnContext& ctx, EmpireId e) {
    GameState& s = ctx.state;
    if (!livingEmpire(s, e)) return;
    Resources spent;
    for (const QueueRef& q : empireQueues(ctx.rules, s, e)) spent += runQueue(ctx, e, q);
    s.empire(e).economy.construction = spent;
    const int maxTurns = static_cast<int>(std::max<int64_t>(0, ctx.rules.setting("Maximum Emergency Build Turns", 10)));
    for (auto& c : s.colonies)
        if (c && c->owner == e) advanceQueueMode(c->queue, maxTurns);
    for (Vehicle& v : s.vehicles)
        if (v.owner == e) advanceQueueMode(v.queue, maxTurns);
}

} // namespace opense4::game::economy
