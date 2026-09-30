#include "game/economy.hpp"

#include "game/design.hpp"
#include "game/economy_internal.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>

// Construction queues (docs/spec/02 §6): rates, costs, per-turn spending,
// completion of ships, units, facilities and upgrades, build modes.

namespace opense4::game::economy {

using namespace detail;

namespace {

// Yard rates of a colony's working space yard facility (one per planet; the
// best per resource if data ever allows more).
bool colonyYardRates(const Rules& r, const Colony& c, Resources& out) {
    bool any = false;
    if (!facilitiesWork(c)) return false;
    for (uint32_t f : c.facilities) {
        const auto ab = r.facilityAbilities(f);
        if (!hasAbility(ab, AbilityKind::SpaceYard)) continue;
        out = any ? max(out, spaceYardRates(ab)) : spaceYardRates(ab);
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
    if (moodFromAnger(c->anger) == Mood::Rioting) return "rioting";
    return {};
}

} // namespace

// ---- Rates and costs ---------------------------------------------------------------------------------

Resources constructionRate(const Rules& r, const GameState& s, EmpireId e, const cmd::QueueTarget& t) {
    if (!e.valid() || e.index() >= s.empires.size()) return {};
    const Empire& emp = s.empire(e);
    const ruleset::Culture* culture = r.culture(emp.race);
    const int common = charBonus(emp.race, Characteristic::ConstructionAptitude) + (culture ? culture->shipyardRate : 0);
    Resources base;
    int64_t pct = 0;
    const ConstructionQueue* q = nullptr;
    if (t.vehicle.valid()) {
        const Vehicle* v = s.vehicle(t.vehicle);
        if (!v || v->owner != e) return {};
        base = vehicleYardRates(r, s, *v);
        pct = 100 + common;  // no population on a ship; aptitude applies (inferred, spec 02 §13 Q4)
        q = &v->queue;
    } else {
        const Colony* c = s.colony(t.planet);
        if (!c || c->owner != e) return {};
        if (!colonyYardRates(r, *c, base))  // planets without a yard use the empire base rate (inferred)
            base = {r.setting("Empire Base Planet Mineral Usage Rate", 2000), r.setting("Empire Base Planet Organic Usage Rate", 2000),
                    r.setting("Empire Base Planet Radioactive Usage Rate", 2000)};
        pct = populationModifier(r, c->totalPopulation()).shipyard + common + r.traitValue(emp.race, "Planetary SY Rate");
        q = &c->queue;
    }
    pct = std::max<int64_t>(pct, 0);
    const int64_t mode = q->emergency         ? r.setting("Construction Queue Emergency Build Rate Percent", 150)
                         : q->slowTurns > 0 ? r.setting("Construction Queue Slow Build Rate Percent", 25)
                                            : 100;
    Resources rate;
    for (size_t k = 0; k < 3; ++k) rate.v[k] = base.v[k] * pct / 100 * mode / 100;
    return rate;
}

int upgradeableCount(const Rules& r, const GameState& s, EmpireId e, const Colony& c, uint32_t facility) {
    if (facility >= r.data().facilities.size()) return 0;
    const int family = r.facility(facility).family;
    const auto latest = r.latestFacilityOfFamily(s.empire(e), family);
    if (!latest) return 0;
    const int newest = r.facility(*latest).romanNumeral;
    int n = 0;
    for (uint32_t f : c.facilities) n += r.facility(f).family == family && r.facility(f).romanNumeral < newest;
    return n;
}

Resources itemCost(const Rules& r, const GameState& s, EmpireId e, const cmd::QueueTarget& t, const QueueItem& item) {
    switch (item.kind) {
        case QueueItem::Kind::Vehicle:
            if (!item.design.valid() || item.design.index() >= s.designs.size()) return {};
            return designCost(r, s.design(item.design)).percent(100 * std::max(1, item.count));
        case QueueItem::Kind::Facility:
            if (item.facility >= r.data().facilities.size()) return {};
            return Resources::from(r.facility(item.facility).cost);
        case QueueItem::Kind::Upgrade: {
            // Upgrade % of the newest level's cost, per facility changed (spec 02 §6.6).
            const Colony* c = s.colony(t.planet);
            if (!c || item.facility >= r.data().facilities.size()) return {};
            const auto latest = r.latestFacilityOfFamily(s.empire(e), r.facility(item.facility).family);
            if (!latest) return {};
            const int n = upgradeableCount(r, s, e, *c, item.facility);
            return Resources::from(r.facility(*latest).cost).percent(r.setting("Upgrade Facility Cost Percent", 50) * n);
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

namespace detail {

std::vector<QueueRef> empireQueues(const GameState& s, EmpireId e) {
    std::vector<QueueRef> out;
    for (const auto& c : s.colonies)
        if (c && c->owner == e) out.push_back({cmd::QueueTarget{c->planet, {}}, locationOf(s.galaxy, c->planet)});
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && !v.queue.items.empty()) out.push_back({cmd::QueueTarget{{}, v.id}, v.location});
    return out;
}

Resources projectQueueUsage(const Rules& r, const GameState& s, EmpireId e, const QueueRef& q, const ConstructionQueue& queue,
                            Resources& treasury) {
    Resources used;
    if (queue.onHold || queue.items.empty() || !queueBlocked(r, s, q.target).empty()) return used;
    Resources budget = constructionRate(r, s, e, q.target);
    for (const QueueItem& item : queue.items) {
        const Resources remaining = max(itemCost(r, s, e, q.target, item) - item.spent, Resources{});
        const Resources pay = min(min(budget, remaining), max(treasury, Resources{}));
        used += pay;
        treasury -= pay;
        budget -= pay;
        if (!(remaining - pay).isZero() || queue.repeat) break;
    }
    return used;
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

// Units go into the builder's cargo, then into any other cargo space the
// empire owns (spec 02 §6.5): colonies by planet id, then ships by id.
bool placeUnits(const Rules& r, GameState& s, EmpireId e, const QueueRef& q, DesignId design, int count) {
    const int64_t size = r.hull(s.design(design).hull).tonnage;
    std::vector<Holder> holders;
    Cargo* builder = nullptr;
    if (q.target.vehicle.valid()) {
        Vehicle* v = s.vehicle(q.target.vehicle);
        if (v) {
            builder = &v->cargo;
            holders.push_back({builder, vehicleCargoCapacity(r, s, *v) - cargoSpaceUsed(r, s, v->cargo)});
        }
    } else if (Colony* c = s.colony(q.target.planet)) {
        builder = &c->cargo;
        holders.push_back({builder, colonyCargoCapacity(r, s, *c) - cargoSpaceUsed(r, s, c->cargo)});
    }
    for (auto& c : s.colonies)
        if (c && c->owner == e && &c->cargo != builder)
            holders.push_back({&c->cargo, colonyCargoCapacity(r, s, *c) - cargoSpaceUsed(r, s, c->cargo)});
    for (Vehicle& v : s.vehicles)
        if (v.owner == e && &v.cargo != builder && isShipOrBase(vehicleType(r, s, v)))
            holders.push_back({&v.cargo, vehicleCargoCapacity(r, s, v) - cargoSpaceUsed(r, s, v.cargo)});
    if (holders.empty()) return false;

    int64_t room = 0;
    for (const Holder& h : holders) room += size > 0 ? std::max<int64_t>(0, h.free) / size : count;
    if (room < count) return false;
    int left = count;
    for (Holder& h : holders) {
        if (left == 0) break;
        const int64_t fits = size > 0 ? std::max<int64_t>(0, h.free) / size : left;
        const int n = static_cast<int>(std::min<int64_t>(fits, left));
        if (n <= 0) continue;
        auto it = std::find_if(h.cargo->units.begin(), h.cargo->units.end(), [&](const UnitStack& u) { return u.design == design; });
        if (it == h.cargo->units.end()) h.cargo->units.push_back({design, n});
        else it->count += n;
        left -= n;
    }
    return true;
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
                if (!q.target.vehicle.valid()) {
                    const Colony* c = s.colony(q.target.planet);
                    if (!c || !colonyHasSpaceYard(r, *c)) {
                        ctx.log(e, LogCategory::Construction, std::format("{} cannot build {}", where, d.name), "Ships need a space yard.",
                                q.location);
                        return Outcome::Blocked;
                    }
                }
                if (shipCount(r, s, e) + count > s.options.maxShipsPerPlayer) {
                    ctx.log(e, LogCategory::Construction, std::format("{} cannot build {}", where, d.name),
                            "The empire has reached its limit on ships.", q.location);
                    return Outcome::Blocked;
                }
                const DesignId design = item.design;
                for (int k = 0; k < count; ++k) {
                    const Vehicle& v = movement::spawnVehicle(r, s, e, design, q.location, autoWaypoint);
                    ctx.log(e, LogCategory::Construction, std::format("{} completed", v.name), std::format("Built at {}.", where), q.location);
                    ctx.mood(e, "Ship Constructed", q.location.system, anchorAt(s, q));
                    ctx.mood(e, "Any Ship Constructed");
                }
                return Outcome::Done;
            }
            if (unitCount(r, s, e) + count > s.options.maxUnitsPerPlayer) {
                ctx.log(e, LogCategory::Construction, std::format("{} cannot build {}", where, d.name),
                        "The empire has reached its limit on units.", q.location);
                return Outcome::Blocked;
            }
            if (!placeUnits(r, s, e, q, item.design, count)) {
                ctx.log(e, LogCategory::Construction, std::format("No room for {} built at {}", d.name, where),
                        "There is no free cargo space for the new units, so they were not built.", q.location);
                return Outcome::Blocked;
            }
            s.design(item.design).built += count;
            ctx.log(e, LogCategory::Construction, std::format("{} x {} completed", count, d.name), std::format("Built at {}.", where), q.location);
            return Outcome::Done;
        }
        case QueueItem::Kind::Facility: {
            Colony* c = s.colony(q.target.planet);
            if (!c || item.facility >= r.data().facilities.size()) return Outcome::Invalid;
            const ruleset::Facility& f = r.facility(item.facility);
            if (hasAbility(r.facilityAbilities(item.facility), AbilityKind::SpaceYard) && colonyHasSpaceYard(r, *c)) {
                ctx.log(e, LogCategory::Construction, std::format("{} dropped at {}", f.name, where), "A planet can have only one space yard.",
                        q.location);
                return Outcome::Invalid;
            }
            if (static_cast<int>(c->facilities.size()) >= facilitySlots(r, s, *c)) {
                ctx.log(e, LogCategory::Construction, std::format("{} cannot build {}", where, f.name), "No facility slot is free.", q.location);
                return Outcome::Blocked;
            }
            c->facilities.push_back(item.facility);
            ctx.log(e, LogCategory::Construction, std::format("{} completed", f.name), std::format("Built on {}.", where), q.location);
            ctx.mood(e, "Facility Constructed", q.location.system, c->planet);
            return Outcome::Done;
        }
        case QueueItem::Kind::Upgrade: {
            Colony* c = s.colony(q.target.planet);
            if (!c || item.facility >= r.data().facilities.size()) return Outcome::Invalid;
            const int family = r.facility(item.facility).family;
            const auto latest = r.latestFacilityOfFamily(s.empire(e), family);
            if (!latest) return Outcome::Invalid;
            const int newest = r.facility(*latest).romanNumeral;
            int changed = 0;
            for (uint32_t& f : c->facilities)
                if (r.facility(f).family == family && r.facility(f).romanNumeral < newest) {
                    f = *latest;
                    ++changed;
                }
            if (changed == 0) return Outcome::Invalid;
            ctx.log(e, LogCategory::Construction, std::format("{} upgraded", where),
                    std::format("{} facilities now {}.", changed, r.facility(*latest).name), q.location);
            return Outcome::Done;
        }
    }
    return Outcome::Invalid;
}

ConstructionQueue* liveQueue(GameState& s, const QueueRef& q) {
    if (q.target.vehicle.valid()) {
        Vehicle* v = s.vehicle(q.target.vehicle);
        return v ? &v->queue : nullptr;
    }
    Colony* c = s.colony(q.target.planet);
    return c ? &c->queue : nullptr;
}

// One queue's turn: spend up to the rate on the top item, finish it, and let
// what is left of the rate go on to the next item (inferred, spec 02 §13 Q5).
Resources runQueue(TurnContext& ctx, EmpireId e, const QueueRef& q) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Resources spent;
    ConstructionQueue* queue = liveQueue(s, q);
    if (!queue || queue->items.empty() || queue->onHold || !queueBlocked(r, s, q.target).empty()) return spent;
    Resources budget = constructionRate(r, s, e, q.target);
    constexpr int kMaxCompletions = 1000;  // guards against free items on repeat
    for (int completions = 0; completions < kMaxCompletions; ++completions) {
        queue = liveQueue(s, q);
        if (!queue || queue->items.empty()) break;
        QueueItem& item = queue->items.front();
        const Resources remaining = max(itemCost(r, s, e, q.target, item) - item.spent, Resources{});
        Resources& bank = s.empire(e).stockpile;
        const Resources wanted = min(budget, remaining);
        const Resources pay = min(wanted, max(bank, Resources{}));
        item.spent += pay;
        bank -= pay;
        budget -= pay;
        spent += pay;
        if (!(remaining - pay).isZero()) {
            if (!(wanted - pay).isZero()) {
                std::string what;
                for (Resource res : kResources)
                    if (wanted[res] > pay[res]) what += std::format("{}{}", what.empty() ? "" : ", ", displayName(res));
                ctx.log(e, LogCategory::Construction, std::format("Construction slowed at {}", placeName(s, q)),
                        std::format("The treasury is short of {}.", what), q.location);
            }
            break;
        }
        const QueueItem done = item;
        const int waypoint = queue->autoWaypoint;
        const Outcome outcome = completeItem(ctx, e, q, done, waypoint);
        queue = liveQueue(s, q);  // spawning may move vehicles in memory
        if (!queue || outcome == Outcome::Blocked) break;
        if (!queue->items.empty()) queue->items.erase(queue->items.begin());
        if (outcome == Outcome::Done && queue->repeat) {
            QueueItem again = done;
            again.spent = {};
            queue->items.insert(queue->items.begin(), again);
            break;  // a repeated item is built at most once per turn (inferred)
        }
    }
    return spent;
}

} // namespace

namespace detail {

Resources runConstruction(TurnContext& ctx, EmpireId e) {
    Resources spent;
    for (const QueueRef& q : empireQueues(ctx.state, e)) spent += runQueue(ctx, e, q);
    return spent;
}

void advanceQueueModes(const Rules& r, GameState& s) {
    const int maxTurns = static_cast<int>(std::max<int64_t>(1, r.setting("Maximum Emergency Build Turns", 10)));
    auto step = [&](ConstructionQueue& q) {
        if (q.emergency) {
            if (++q.emergencyTurns >= maxTurns) {
                q.emergency = false;
                q.slowTurns = std::max(1, q.emergencyTurns);
            }
        } else if (q.slowTurns > 0) {
            --q.slowTurns;
        }
    };
    for (auto& c : s.colonies)
        if (c) step(c->queue);
    for (Vehicle& v : s.vehicles) step(v.queue);
}

} // namespace detail

} // namespace opense4::game::economy
