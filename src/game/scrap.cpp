#include "game/scrap.hpp"

#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>

namespace opense4::game {

namespace {

using ruleset::VehicleType;

bool alive(const Vehicle& v) { return v.count > 0; }

bool shipOrBase(VehicleType t) { return t == VehicleType::Ship || t == VehicleType::Base; }

// The own design a vehicle may be retrofitted to.
bool ownDesign(const GameState& s, EmpireId e, DesignId d) { return d.valid() && d.index() < s.designs.size() && s.design(d).owner == e; }

// The vehicle is gone: no units left (the caller removes it).
void removeVehicle(Vehicle& v) {
    v.count = 0;
    v.mixed.clear();
}

// Every unit of a unit group, per design; a ship or base counts 1.
template <class Fn>
void perDesign(const Rules& r, const GameState& s, const Vehicle& v, Fn&& fn) {
    if (!isUnitType(vehicleType(r, s, v))) {
        fn(v.design, 1);
        return;
    }
    for (const UnitStack& st : groupStacks(v)) fn(st.design, st.count);
}

std::string sectorText(const GameState& s, Location where) {
    const std::string system = where.system.valid() && where.system.index() < s.galaxy.systems.size() ? s.galaxy.system(where.system).name : "?";
    return std::format("the {} system, sector ({}, {})", system, where.sector.x, where.sector.y);
}

// A retrofit's pairing (spec 03 §14): each target component takes the first
// unpaired current entry of the same component and mount; unpaired target
// parts cost `Retrofit Cost Percent For Comps`, unpaired current parts `...
// For Comp Removal`, each truncated. The hull costs nothing.
struct RetrofitPlan {
    Resources cost;
    bool added = false;
    std::vector<int> pairOf;  // per target entry: the current entry it keeps, or -1
};

RetrofitPlan planRetrofit(const Rules& r, const Design& oldD, const Design& newD) {
    RetrofitPlan plan;
    std::vector<bool> paired(oldD.entries.size(), false);
    plan.pairOf.assign(newD.entries.size(), -1);
    const int64_t addPct = r.setting("Retrofit Cost Percent For Comps", 120);
    const int64_t removePct = r.setting("Retrofit Cost Percent For Comp Removal", 30);
    for (size_t i = 0; i < newD.entries.size(); ++i) {
        for (size_t j = 0; j < oldD.entries.size(); ++j)
            if (!paired[j] && oldD.entries[j] == newD.entries[i]) {
                paired[j] = true;
                plan.pairOf[i] = static_cast<int>(j);
                break;
            }
        if (plan.pairOf[i] >= 0) continue;
        plan.added = true;
        const Resources each = mounted(r, newD.entries[i]).cost;
        for (Resource res : kResources) plan.cost[res] += xmath::pctTrunc(each[res], addPct);
    }
    for (size_t j = 0; j < oldD.entries.size(); ++j) {
        if (paired[j]) continue;
        const Resources each = mounted(r, oldD.entries[j]).cost;
        for (Resource res : kResources) plan.cost[res] += xmath::pctTrunc(each[res], removePct);
    }
    return plan;
}

void scrapVehicle(TurnContext& ctx, Vehicle& v) {
    GameState& s = ctx.state;
    // Damage does not lower the value and cargo is lost (spec 03 §15).
    s.empire(v.owner).stockpile += scrapRefund(ctx.rules, s, v);
    perDesign(ctx.rules, s, v, [&](DesignId d, int n) { s.design(d).scrapped += n; });
    ctx.log(v.owner, LogCategory::Construction, std::format("{} scrapped", v.name), {}, v.location);
    removeVehicle(v);
}

void analyzeVehicle(TurnContext& ctx, Vehicle& v) {
    GameState& s = ctx.state;
    const EmpireId owner = v.owner;
    // The pairs are worked out once, before anything changes; each gives
    // exactly one level in its area, whatever its level (spec 03 §15).
    const std::vector<TechPair> pairs = analyzePairs(ctx.rules, s, owner, v);
    // Removed like a scrapped vehicle, with no refund and no entry of its own;
    // a captured ship counts on its builder's design record.
    ++s.design(v.design).scrapped;
    removeVehicle(v);
    for (const TechPair& p : pairs) research::analyzeLevel(ctx, owner, p.area);
}

void mothballVehicle(Vehicle& v) {
    v.status = VehicleStatus::Mothballed;
    v.orders.clear();
    v.repeatOrders = false;
    v.queue.items.clear();
    v.supply = 0;
    v.movement = 0;
}

void unmothballVehicle(TurnContext& ctx, Vehicle& v) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    s.empire(v.owner).stockpile -= unmothballCharge(r, s, v);
    v.status = VehicleStatus::Normal;
    // Unlimited supply comes back full; others only at a depot, else 0.
    if (vehicleHasUnlimitedSupply(r, s, v)) v.supply = kUnlimitedSupply;
    else if (movement::resupplyDepotAt(r, s, v.owner, v.location)) v.supply = vehicleSupplyCapacity(r, s, v);
    else v.supply = 0;
}

void selfDestructVehicle(TurnContext& ctx, Vehicle& v) {
    GameState& s = ctx.state;
    perDesign(ctx.rules, s, v, [&](DesignId d, int n) { s.design(d).scrapped += n; });
    ctx.log(v.owner, LogCategory::Misc, std::format("{} destroyed", v.name), "It self-destructed.", v.location);
    removeVehicle(v);
}

void fireOnVehicle(TurnContext& ctx, Vehicle& v) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const VehicleType type = vehicleType(r, s, v);
    // Nobody fires: no battle, damage, experience or kill statistic (spec 03 §15).
    if (shipOrBase(type)) ++s.design(v.design).lost;
    else if (type != VehicleType::Drone)
        for (const UnitStack& st : groupStacks(v)) s.design(st.design).lost += st.count;
    const bool group = type == VehicleType::Fighter || type == VehicleType::Satellite || type == VehicleType::Mine;
    // One Construction entry with Goto to the sector (spec 03 §15).
    ctx.log(v.owner, LogCategory::Construction, group ? "Group Destroyed" : "Vehicle Destroyed",
            std::format("The Demolition Minister reports that our own ships fired on {} in {} and destroyed it.", v.name, sectorText(s, v.location)),
            v.location);
    removeVehicle(v);
}

} // namespace

OrderKind scrapOrderKind(ScrapAction a) {
    switch (a) {
        case ScrapAction::Scrap: return OrderKind::Scrap;
        case ScrapAction::Analyze: return OrderKind::Analyze;
        case ScrapAction::Mothball: return OrderKind::Mothball;
        case ScrapAction::Unmothball: return OrderKind::Unmothball;
        case ScrapAction::Retrofit: return OrderKind::Retrofit;
        case ScrapAction::SelfDestruct: return OrderKind::SelfDestruct;
        case ScrapAction::FireOn: return OrderKind::FireOn;
    }
    return OrderKind::Scrap;
}

std::optional<ScrapAction> scrapActionOf(OrderKind k) {
    switch (k) {
        case OrderKind::Scrap: return ScrapAction::Scrap;
        case OrderKind::Analyze: return ScrapAction::Analyze;
        case OrderKind::Mothball: return ScrapAction::Mothball;
        case OrderKind::Unmothball: return ScrapAction::Unmothball;
        case OrderKind::Retrofit: return ScrapAction::Retrofit;
        case OrderKind::SelfDestruct: return ScrapAction::SelfDestruct;
        case OrderKind::FireOn: return ScrapAction::FireOn;
        default: return std::nullopt;
    }
}

bool scrapWindowOrder(OrderKind k) { return k != OrderKind::SelfDestruct && scrapActionOf(k).has_value(); }

bool scrapListed(const Vehicle& v, EmpireId e) {
    return alive(v) && v.owner == e && !v.fleet.valid() && v.status != VehicleStatus::Cloaked;
}

bool scrapYardAt(const Rules& r, const GameState& s, EmpireId e, Location where) {
    for (ObjectId o : planetsAt(s, where))
        if (const Colony* c = s.colony(o); c && c->owner == e && colonyHasWorkingYard(r, *c)) return true;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == e && v.location == where && v.status != VehicleStatus::Cloaked && vehicleHasSpaceYard(r, s, v)) return true;
    return false;
}

std::string scrapActionProblem(const Rules& r, const GameState& s, EmpireId e, const Vehicle& v, ScrapAction a, DesignId retrofitTo) {
    if (!alive(v) || v.owner != e) return "Not your vehicle";
    // The window lists no fleet member and no cloaked vehicle (spec 03 §15, §19 Q74).
    if (v.fleet.valid()) return "A vehicle in a fleet is not in the Scrap window";
    if (v.status == VehicleStatus::Cloaked) return "A cloaked vehicle is not in the Scrap window";
    const VehicleType type = vehicleType(r, s, v);
    switch (a) {
        case ScrapAction::Scrap:
            if (type == VehicleType::Drone || type == VehicleType::Mine) return "Drones and minefields cannot be scrapped";
            if (!scrapYardAt(r, s, e, v.location)) return "Scrapping needs a space yard in the sector";
            return {};
        case ScrapAction::Analyze:
            if (!shipOrBase(type)) return "Only ships and bases can be analyzed";
            if (!scrapYardAt(r, s, e, v.location)) return "Analyzing needs a space yard in the sector";
            return {};
        case ScrapAction::Mothball:
            if (!shipOrBase(type)) return "Units cannot be mothballed";
            if (v.status != VehicleStatus::Normal) return "Already mothballed";
            if (!scrapYardAt(r, s, e, v.location)) return "Mothballing needs a space yard in the sector";
            if (!v.cargo.empty()) return "Unload the cargo first";
            return {};
        case ScrapAction::Unmothball:
            if (v.status != VehicleStatus::Mothballed) return "Not mothballed";
            if (!s.empire(e).stockpile.covers(unmothballCharge(r, s, v))) return "Not enough resources to unmothball";
            return {};
        case ScrapAction::Retrofit: return retrofitProblem(r, s, e, v, retrofitTo);
        case ScrapAction::SelfDestruct: return movement::canSelfDestruct(r, s, v) ? std::string{} : std::string("It cannot self-destruct");
        case ScrapAction::FireOn: return canBeFiredOn(r, s, v) ? std::string{} : std::string("No other armed vehicle of ours is in the sector");
    }
    return "Unknown action";
}

void carryOutScrapAction(TurnContext& ctx, Vehicle& v, ScrapAction a, DesignId retrofitTo) {
    switch (a) {
        case ScrapAction::Scrap: scrapVehicle(ctx, v); break;
        case ScrapAction::Analyze: analyzeVehicle(ctx, v); break;
        case ScrapAction::Mothball: mothballVehicle(v); break;
        case ScrapAction::Unmothball: unmothballVehicle(ctx, v); break;
        case ScrapAction::Retrofit: retrofitVehicle(ctx, v, retrofitTo); break;
        case ScrapAction::SelfDestruct: selfDestructVehicle(ctx, v); break;
        case ScrapAction::FireOn: fireOnVehicle(ctx, v); break;
    }
}

// ---- Analyze ----------------------------------------------------------------------------------------

std::vector<TechPair> analyzePairs(const Rules& r, const GameState& s, EmpireId e, const Vehicle& v) {
    std::vector<TechPair> pairs;
    if (!e.valid() || e.index() >= s.empires.size() || !v.design.valid() || v.design.index() >= s.designs.size()) return pairs;
    const Empire& emp = s.empire(e);
    auto add = [&](std::span<const ruleset::TechRequirement> reqs) {
        for (const ruleset::TechRequirement& q : reqs) {
            const TechPair p{q.area, q.level};
            if (emp.techLevel(q.area) < q.level && std::find(pairs.begin(), pairs.end(), p) == pairs.end()) pairs.push_back(p);
        }
    };
    // Components in design order, destroyed ones skipped; then the hull.
    // Mounts, cargo and the rest of the design are not read.
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (d.entries[i].component < r.data().components.size() && entryIntact(r, s, v, i)) add(r.component(d.entries[i].component).requirements);
    if (d.hull < r.data().vehicleSizes.size()) add(r.hull(d.hull).requirements);
    return pairs;
}

std::string_view researchPotentialWord(size_t pairs) {
    switch (pairs) {
        case 0: return "None";
        case 1: return "Minor";
        case 2: return "Moderate";
        case 3: return "Sizable";
        default: return "Major";
    }
}

// ---- Fire On ----------------------------------------------------------------------------------------

bool armedForFireOn(const Rules& r, const GameState& s, const Vehicle& v) {
    if (!alive(v)) return false;
    auto weapon = [&](DesignId id, bool anyKind) {
        if (!id.valid() || id.index() >= s.designs.size()) return false;
        for (const DesignEntry& en : s.design(id).entries) {
            if (en.component >= r.data().components.size()) continue;
            const ruleset::WeaponKind k = r.component(en.component).weapon.kind;
            if (anyKind ? k != ruleset::WeaponKind::None : k == ruleset::WeaponKind::DirectFire || k == ruleset::WeaponKind::Seeking) return true;
        }
        return false;
    };
    switch (vehicleType(r, s, v)) {
        case VehicleType::Ship:
        case VehicleType::Base:
            // Destroyed components count; Point-Defense and Warhead do not.
            return v.status != VehicleStatus::Mothballed && weapon(v.design, false);
        case VehicleType::Fighter:
            for (const UnitStack& st : groupStacks(v))
                if (weapon(st.design, true)) return true;
            return false;
        default: return false;  // satellite groups, minefields and drone groups never
    }
}

bool canBeFiredOn(const Rules& r, const GameState& s, const Vehicle& v) {
    for (const Vehicle& other : s.vehicles)
        if (other.id != v.id && other.owner == v.owner && other.location == v.location && armedForFireOn(r, s, other)) return true;
    return false;
}

// ---- Retrofit ---------------------------------------------------------------------------------------

std::string retrofitProblem(const Rules& r, const GameState& s, EmpireId e, const Vehicle& v, DesignId to, Resources* cost) {
    if (!alive(v) || v.owner != e) return "Not your vehicle";
    if (!ownDesign(s, e, to)) return "Not your design";
    if (v.status == VehicleStatus::Cloaked) return "A cloaked vehicle cannot be retrofitted";
    if (v.fleet.valid()) return "A vehicle in a fleet cannot be retrofitted";  // spec 03 §15, §19 Q74
    const Design& oldD = s.design(v.design);
    const Design& newD = s.design(to);
    const RetrofitPlan plan = planRetrofit(r, oldD, newD);
    if (cost) *cost = plan.cost;
    // The checks in order; the first failure cancels it (spec 03 §14, confirmed: binary).
    // 1. Identical designs.
    if (plan.cost.total() == 0) return "The designs are the same";
    // 2. An own space yard in the sector (a ship's only while it is not cloaked).
    if (!scrapYardAt(r, s, e, v.location)) return "Retrofit needs a space yard in the sector";
    // 3. The hull.
    if (oldD.hull != newD.hull) return "A retrofit must keep the hull";
    // 4. Cargo.
    if (!v.cargo.empty()) return "Unload the cargo before a retrofit";
    // 5. Resources in stock.
    if (!s.empire(e).stockpile.covers(plan.cost)) return "Not enough resources for the retrofit";
    // 6. Space yards and colony modules cannot be added.
    auto has = [&](const Design& d, AbilityKind k) {
        return std::any_of(d.entries.begin(), d.entries.end(), [&](const DesignEntry& en) { return hasAbility(r.componentAbilities(en.component), k); });
    };
    auto colonizes = [&](const Design& d) {
        return has(d, AbilityKind::ColonizeRock) || has(d, AbilityKind::ColonizeIce) || has(d, AbilityKind::ColonizeGas);
    };
    if (r.settingFlag("No Retrofit Adding Of Spaceyards", true) && !has(oldD, AbilityKind::SpaceYard) && has(newD, AbilityKind::SpaceYard))
        return "Space yards cannot be added by retrofit";
    if (r.settingFlag("No Retrofit Adding Of Colony Module", true) && !colonizes(oldD) && colonizes(newD))
        return "Colony modules cannot be added by retrofit";
    // 7. Only an increase in total cost is limited, compared in floating point.
    const int64_t oldTotal = computeDesignStats(r, nullptr, oldD).cost.total();
    const int64_t newTotal = computeDesignStats(r, nullptr, newD).cost.total();
    const int64_t maxPct = r.setting("Retrofit Max Percent Difference in Cost", 50);
    if (xmath::Ext(newTotal) > xmath::Ext(oldTotal) * xmath::percent(100 + maxPct)) return "The new design costs too much more";
    return {};
}

void retrofitVehicle(TurnContext& ctx, Vehicle& v, DesignId to) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const Design& newD = s.design(to);
    const RetrofitPlan plan = planRetrofit(r, s.design(v.design), newD);
    // The cost is taken only when a component is added.
    if (plan.added) s.empire(v.owner).stockpile -= plan.cost;
    // Paired parts keep their state; added parts start destroyed and must be repaired.
    std::vector<int> damage;
    for (size_t i = 0; i < newD.entries.size(); ++i) {
        const int j = plan.pairOf[i];
        damage.push_back(j >= 0 && static_cast<size_t>(j) < v.damage.size() ? v.damage[static_cast<size_t>(j)]
                         : j >= 0                                            ? 0
                                                                             : entryStructure(r, newD, i));
    }
    const std::string name = newD.name;
    // A design a ship is retrofitted to is no longer a prototype (spec 03 §4.1).
    s.design(to).retrofitted = true;
    s.design(to).everBuilt = true;  // what design theft reads (spec 05 §2.3)
    v.design = to;
    v.damage = std::move(damage);
    // Movement and supply recomputed and clamped to the new maxima.
    v.movement = std::min(v.movement, vehicleMaxMovement(r, s, v));
    if (vehicleHasUnlimitedSupply(r, s, v)) v.supply = kUnlimitedSupply;
    else v.supply = std::clamp<int64_t>(v.supply, 0, vehicleSupplyCapacity(r, s, v));
    ctx.log(v.owner, LogCategory::Construction, std::format("{} retrofitted to {}", v.name, name), {}, v.location);
}

} // namespace opense4::game
