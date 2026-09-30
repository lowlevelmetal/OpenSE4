// Shared vehicle helpers: supply, damage outside combat, mines, cargo,
// unit launch and recovery, one-shot components (spec 03 §7, §11-13).

#include "datafile/datafile.hpp"
#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>
#include <limits>

namespace opense4::game::movement {

namespace detail {

using ruleset::VehicleType;

bool inSystem(const Galaxy& g, ObjectId o) {
    if (!o.valid() || o.index() >= g.objects.size()) return false;
    const SpaceObject& obj = g.object(o);
    if (!obj.system.valid() || obj.system.index() >= g.systems.size()) return false;
    const auto& list = g.system(obj.system).objects;
    return std::find(list.begin(), list.end(), o) != list.end();
}

int64_t rawBest(const std::vector<ruleset::Ability>& list, AbilityKind k) {
    int64_t best = 0;
    for (const auto& a : list)
        if (parseAbilityKind(a.type) == k) best = std::max(best, a.number1());
    return best;
}

int64_t rawSum(const std::vector<ruleset::Ability>& list, AbilityKind k) {
    int64_t total = 0;
    for (const auto& a : list)
        if (parseAbilityKind(a.type) == k) total += a.number1();
    return total;
}

int turnMovement(const Rules& r, const GameState& s, const Vehicle& v) { return heldInPlace(s, v) ? 0 : vehicleMaxMovement(r, s, v); }

bool isShipOrBase(VehicleType t) { return t == VehicleType::Ship || t == VehicleType::Base; }
bool isMobileType(VehicleType t) { return t == VehicleType::Ship || t == VehicleType::Fighter || t == VehicleType::Drone; }

const Vehicle* fleetLeader(const GameState& s, const Fleet& f) {
    if (const Vehicle* v = s.vehicle(f.leader); v && alive(*v)) return v;
    for (VehicleId id : f.members)
        if (const Vehicle* v = s.vehicle(id); v && alive(*v)) return v;
    return nullptr;
}

// Fleet orders take precedence over a member's own while it is with the leader (inferred).
bool followsFleetOrders(const GameState& s, const Vehicle& v) {
    if (!v.fleet.valid()) return false;
    const Fleet* f = s.fleet(v.fleet);
    if (!f || f->orders.empty()) return false;
    const Vehicle* lead = fleetLeader(s, *f);
    return lead && lead->location == v.location && v.status != VehicleStatus::Mothballed;
}

// ---- Supply ----------------------------------------------------------------------------------

int64_t scaledSupply(const Rules& r, const GameState& s, EmpireId owner, int64_t amount) {
    if (!owner.valid() || owner.index() >= s.empires.size()) return amount;
    const int64_t pct = 100 + r.traitValue(s.empire(owner).race, "Supply Cost");
    return amount * std::max<int64_t>(0, pct) / 100;
}

void spendSupply(const Rules& r, const GameState& s, Vehicle& v, int64_t amount) {
    if (amount <= 0 || vehicleHasQuantumReactor(r, s, v)) return;
    v.supply = std::max<int64_t>(0, v.supply - amount);
}

void refillSupply(const Rules& r, const GameState& s, Vehicle& v) {
    if (vehicleType(r, s, v) == VehicleType::Drone) return;  // drones are never resupplied
    v.supply = std::max(v.supply, vehicleSupplyCapacity(r, s, v));
}

void poolSupply(const Rules& r, GameState& s, std::span<const VehicleId> members) {
    std::vector<Vehicle*> pool;
    int64_t total = 0, capacity = 0;
    for (VehicleId id : members) {
        Vehicle* v = s.vehicle(id);
        if (!v || !alive(*v) || vehicleHasQuantumReactor(r, s, *v)) continue;
        const int64_t cap = vehicleSupplyCapacity(r, s, *v);
        if (cap <= 0) continue;
        pool.push_back(v);
        total += std::min(v->supply, cap);
        capacity += cap;
    }
    if (pool.size() < 2 || capacity <= 0) return;
    // Proportional to capacity; the remainder goes one point at a time in member order (inferred).
    int64_t given = 0;
    std::vector<int64_t> caps;
    for (Vehicle* v : pool) {
        const int64_t cap = vehicleSupplyCapacity(r, s, *v);
        caps.push_back(cap);
        v->supply = total * cap / capacity;
        given += v->supply;
    }
    for (size_t i = 0; given < total; i = (i + 1) % pool.size())
        if (pool[i]->supply < caps[i]) {
            ++pool[i]->supply;
            ++given;
        }
}

// ---- Losses ------------------------------------------------------------------------------------

void vehicleLost(TurnContext& ctx, Vehicle& v, std::string_view cause) {
    if (!alive(v)) return;
    GameState& s = ctx.state;
    Design& d = s.design(v.design);
    d.lost += v.count;
    const bool unit = isUnitType(vehicleType(ctx.rules, s, v));
    ctx.log(v.owner, LogCategory::Misc, std::format("{} destroyed", v.name), std::string(cause), v.location);
    if (!unit) {
        ctx.mood(v.owner, "Any Ship Lost");
        ctx.mood(v.owner, "Ship Lost in System", v.location.system);
    }
    v.count = 0;
}

bool hurt(TurnContext& ctx, VehicleId id, int amount, std::string_view cause) {
    Vehicle* v = ctx.state.vehicle(id);
    if (!v || !alive(*v) || amount <= 0) return false;
    const int count = v->count;
    if (!damageVehicle(ctx.rules, ctx.state, *v, amount)) return false;
    v->count = count;  // restored so vehicleLost records every member
    vehicleLost(ctx, *v, cause);
    return true;
}

// ---- Mines -------------------------------------------------------------------------------------

int sweepMines(TurnContext& ctx, VehicleId sweeperId) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle* sweeper = s.vehicle(sweeperId);
    if (!sweeper || !alive(*sweeper)) return 0;
    int64_t capacity = sumValue1(vehicleAbilities(r, s, *sweeper), AbilityKind::MineSweeping);
    if (capacity <= 0) return 0;
    const EmpireId owner = sweeper->owner;
    const Location where = sweeper->location;
    int64_t swept = 0;
    for (Vehicle& m : s.vehicles) {
        if (capacity <= 0) break;
        if (!alive(m) || m.location != where || m.owner == owner || vehicleType(r, s, m) != VehicleType::Mine) continue;
        if (!treatyIsHostile(s.empire(owner).relation(m.owner).treaty)) continue;  // the mine rule: no treaty or worse
        const int64_t n = std::min<int64_t>(capacity, m.count);
        m.count -= static_cast<int>(n);
        capacity -= n;
        swept += n;
        s.design(m.design).lost += static_cast<int>(n);
    }
    if (swept > 0) {
        const Vehicle* sv = s.vehicle(sweeperId);
        ctx.log(owner, LogCategory::Combat, std::format("{} swept {} mines", sv->name, swept), {}, where);
    }
    return static_cast<int>(swept);
}

// ---- Cargo -------------------------------------------------------------------------------------

int64_t freeCargo(const Rules& r, const GameState& s, const Vehicle& v) {
    return std::max<int64_t>(0, vehicleCargoCapacity(r, s, v) - cargoSpaceUsed(r, s, v.cargo));
}

namespace {

int64_t colonyFreeCargo(const Rules& r, const GameState& s, const Colony& c) {
    return std::max<int64_t>(0, colonyCargoCapacity(r, s, c) - cargoSpaceUsed(r, s, c.cargo));
}

int64_t unitTons(const Rules& r, const GameState& s, DesignId unit) { return std::max(1, r.hull(s.design(unit).hull).tonnage); }

int64_t moveUnits(Cargo& from, Cargo& to, DesignId unit, int64_t n) {
    auto it = std::find_if(from.units.begin(), from.units.end(), [&](const UnitStack& u) { return u.design == unit; });
    if (it == from.units.end() || n <= 0) return 0;
    n = std::min<int64_t>(n, it->count);
    it->count -= static_cast<int>(n);
    if (it->count <= 0) from.units.erase(it);
    auto dst = std::find_if(to.units.begin(), to.units.end(), [&](const UnitStack& u) { return u.design == unit; });
    if (dst == to.units.end()) to.units.push_back({unit, static_cast<int>(n)});
    else dst->count += static_cast<int>(n);
    return n;
}

int64_t movePopulation(std::vector<PopulationGroup>& from, std::vector<PopulationGroup>& to, EmpireId race, int64_t n) {
    auto it = std::find_if(from.begin(), from.end(), [&](const PopulationGroup& p) { return p.race == race; });
    if (it == from.end() || n <= 0) return 0;
    n = std::min(n, it->millions);
    it->millions -= n;
    if (it->millions <= 0) from.erase(it);
    auto dst = std::find_if(to.begin(), to.end(), [&](const PopulationGroup& p) { return p.race == race; });
    if (dst == to.end()) to.push_back({race, n});
    else dst->millions += n;
    return n;
}

// The owner's race first, then the others by empire id.
std::vector<EmpireId> racesOf(const std::vector<PopulationGroup>& pop, EmpireId owner) {
    std::vector<EmpireId> out;
    for (const auto& p : pop)
        if (p.millions > 0) out.push_back(p.race);
    std::sort(out.begin(), out.end(), [&](EmpireId a, EmpireId b) { return (a == owner) != (b == owner) ? a == owner : a < b; });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

Colony* ownColonyHere(GameState& s, EmpireId owner, Location where) {
    for (ObjectId o : planetsAt(s, where))
        if (Colony* c = s.colony(o); c && c->owner == owner) return c;
    return nullptr;
}

std::vector<VehicleId> ownBasesHere(const Rules& r, const GameState& s, EmpireId owner, Location where, VehicleId except) {
    std::vector<VehicleId> out;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.id != except && v.owner == owner && v.location == where && vehicleType(r, s, v) == VehicleType::Base)
            out.push_back(v.id);
    return out;
}

// Units of the vehicle's own cargo that no longer fit are lost, last stack first, then population (inferred).
void trim(const Rules& r, const GameState& s, Vehicle& v) {
    const int64_t mass = r.setting("Population Mass", 5);
    int64_t over = cargoSpaceUsed(r, s, v.cargo) - vehicleCargoCapacity(r, s, v);
    while (over > 0 && !v.cargo.units.empty()) {
        UnitStack& u = v.cargo.units.back();
        --u.count;
        over -= unitTons(r, s, u.design);
        if (u.count <= 0) v.cargo.units.pop_back();
    }
    while (over > 0 && !v.cargo.population.empty()) {
        PopulationGroup& p = v.cargo.population.back();
        const int64_t n = std::min(p.millions, mass > 0 ? (over + mass - 1) / mass : p.millions);
        p.millions -= n;
        over -= n * mass;
        if (p.millions <= 0) v.cargo.population.pop_back();
        if (mass <= 0) break;
    }
}

} // namespace

void trimCargo(const Rules& r, const GameState& s, Vehicle& v) { trim(r, s, v); }

int64_t loadCargo(TurnContext& ctx, VehicleId id, DesignId unit, int64_t amount) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle* v = s.vehicle(id);
    if (!v || !alive(*v)) return 0;
    const EmpireId owner = v->owner;
    const Location where = v->location;
    int64_t want = amount < 0 ? std::numeric_limits<int64_t>::max() : amount;
    int64_t moved = 0;
    const int64_t mass = std::max<int64_t>(1, r.setting("Population Mass", 5));

    auto fromCargo = [&](Cargo& src) {
        v = s.vehicle(id);
        if (unit.valid()) {
            const int64_t n = moveUnits(src, v->cargo, unit, std::min(want, freeCargo(r, s, *v) / unitTons(r, s, unit)));
            want -= n;
            moved += n;
            return;
        }
        for (EmpireId race : racesOf(src.population, owner)) {
            const int64_t n = movePopulation(src.population, v->cargo.population, race, std::min(want, freeCargo(r, s, *v) / mass));
            want -= n;
            moved += n;
        }
    };

    if (Colony* c = ownColonyHere(s, owner, where)) {
        if (unit.valid()) {
            fromCargo(c->cargo);
        } else {
            // A colony keeps at least 1M people (spec 03 §11).
            for (EmpireId race : racesOf(c->population, owner)) {
                v = s.vehicle(id);
                const int64_t spare = std::max<int64_t>(0, c->totalPopulation() - 1);
                const int64_t n = movePopulation(c->population, v->cargo.population, race, std::min({want, spare, freeCargo(r, s, *v) / mass}));
                want -= n;
                moved += n;
            }
        }
    }
    for (VehicleId b : ownBasesHere(r, s, owner, where, id)) {
        if (want <= 0) break;
        fromCargo(s.vehicle(b)->cargo);
    }
    return moved;
}

int64_t dropCargo(TurnContext& ctx, VehicleId id, DesignId unit, int64_t amount) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle* v = s.vehicle(id);
    if (!v || !alive(*v)) return 0;
    const EmpireId owner = v->owner;
    const Location where = v->location;
    int64_t want = amount < 0 ? std::numeric_limits<int64_t>::max() : amount;
    int64_t moved = 0;
    const int64_t mass = std::max<int64_t>(1, r.setting("Population Mass", 5));

    if (Colony* c = ownColonyHere(s, owner, where)) {
        if (unit.valid()) {
            const int64_t n = moveUnits(v->cargo, c->cargo, unit, std::min(want, colonyFreeCargo(r, s, *c) / unitTons(r, s, unit)));
            want -= n;
            moved += n;
        } else {
            for (EmpireId race : racesOf(v->cargo.population, owner)) {
                const int64_t room = std::max<int64_t>(0, maxPopulation(r, s, *c) - c->totalPopulation());
                const int64_t n = movePopulation(v->cargo.population, c->population, race, std::min(want, room));
                want -= n;
                moved += n;
            }
        }
    }
    // Troops dropped on an enemy planet land there to fight (combat::landTroops).
    if (unit.valid() && combat::isTroopDesign(r, s, unit))
        for (ObjectId o : planetsAt(s, where)) {
            const Colony* c = s.colony(o);
            if (want <= 0 || !c || c->owner == owner || !hostile(s, owner, c->owner)) continue;
            const int n = combat::landTroops(r, s, id, o, unit, static_cast<int>(std::min<int64_t>(want, s.vehicle(id)->cargo.unitCount(unit))));
            want -= n;
            moved += n;
        }
    for (VehicleId b : ownBasesHere(r, s, owner, where, id)) {
        if (want <= 0) break;
        Vehicle* base = s.vehicle(b);
        v = s.vehicle(id);
        if (unit.valid()) {
            const int64_t n = moveUnits(v->cargo, base->cargo, unit, std::min(want, freeCargo(r, s, *base) / unitTons(r, s, unit)));
            want -= n;
            moved += n;
        } else {
            for (EmpireId race : racesOf(v->cargo.population, owner)) {
                const int64_t n = movePopulation(v->cargo.population, base->cargo.population, race, std::min(want, freeCargo(r, s, *base) / mass));
                want -= n;
                moved += n;
            }
        }
    }
    return moved;
}

int64_t loadColonists(TurnContext& ctx, VehicleId id) {
    const Vehicle* v = ctx.state.vehicle(id);
    if (!v || !alive(*v) || v->cargo.totalPopulation() > 0) return 0;
    if (!ownColonyHere(ctx.state, v->owner, v->location)) return 0;
    return loadCargo(ctx, id, {}, -1);
}

// ---- Units -------------------------------------------------------------------------------------

AbilityKind launcherFor(VehicleType t) {
    switch (t) {
        case VehicleType::Fighter: return AbilityKind::LaunchRecoverFighters;
        case VehicleType::Satellite: return AbilityKind::LaunchRecoverSatellites;
        case VehicleType::Mine: return AbilityKind::LayMines;
        case VehicleType::Drone: return AbilityKind::LaunchDrones;
        default: return AbilityKind::Unknown;
    }
}

namespace {

// Per-game-turn rate of a vehicle's intact launchers: Val 2, or Val 1 when a
// record leaves Val 2 at 0 (inferred).
int64_t turnRate(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k) {
    int64_t rate = 0;
    for (const ParsedAbility& a : vehicleAbilities(r, s, v))
        if (a.kind == k) rate += a.value2 > 0 ? a.value2 : a.value1;
    return rate;
}

int64_t groupsOfTypeHere(const Rules& r, const GameState& s, EmpireId owner, Location where, VehicleType t) {
    int64_t n = 0;
    for (const Vehicle& g : s.vehicles)
        if (alive(g) && g.owner == owner && g.location == where && vehicleType(r, s, g) == t) n += g.count;
    return n;
}

} // namespace

int64_t launchUnits(TurnContext& ctx, UnitBudget& budget, VehicleId id, const Order& o) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const Vehicle* v = s.vehicle(id);
    if (!v || !alive(*v) || !o.design.valid() || o.design.index() >= s.designs.size()) return 0;
    const DesignId unit = o.design;
    const VehicleType type = r.hull(s.design(unit).hull).type;
    const AbilityKind k = launcherFor(type);
    if (k == AbilityKind::Unknown) return 0;  // troops and weapon platforms are never launched into space
    const int64_t inCargo = v->cargo.unitCount(unit);
    int64_t n = std::min<int64_t>(inCargo, turnRate(r, s, *v, k) - budget.launched[{id, k}]);
    if (o.amount >= 0) n = std::min<int64_t>(n, o.amount);
    const EmpireId owner = v->owner;
    const Location where = v->location;
    if (type == VehicleType::Mine)
        n = std::min(n, r.setting("Maximum Mines Per Player Per Sector", 100) - groupsOfTypeHere(r, s, owner, where, type));
    if (type == VehicleType::Satellite)
        n = std::min(n, r.setting("Maximum Satellites Per Player Per Sector", 100) - groupsOfTypeHere(r, s, owner, where, type));
    VehicleId targetVehicle;
    ObjectId targetObject;
    if (type == VehicleType::Drone) {
        // A drone needs a ship or planet target at launch (spec 03 §12).
        targetVehicle = o.vehicle;
        targetObject = o.object;
        if (!targetVehicle.valid() && !targetObject.valid()) return 0;
    }
    if (n <= 0) return 0;

    Vehicle* launcher = s.vehicle(id);
    Cargo taken;
    n = moveUnits(launcher->cargo, taken, unit, n);
    budget.launched[{id, k}] += n;

    // One group per (owner, design, sector); drones also share a target.
    const Design& d = s.design(unit);
    const int64_t fullSupply = computeDesignStats(r, nullptr, d).supplyCapacity;
    for (Vehicle& g : s.vehicles) {
        if (!alive(g) || g.owner != owner || g.design != unit || g.location != where || g.fleet.valid()) continue;
        if (type == VehicleType::Drone && (g.targetVehicle != targetVehicle || g.targetObject != targetObject)) continue;
        g.supply = (g.supply * g.count + fullSupply * n) / (g.count + n);
        g.count += static_cast<int>(n);
        return n;
    }
    Vehicle g;
    g.owner = owner;
    g.design = unit;
    g.name = d.name;
    g.location = where;
    g.count = static_cast<int>(n);
    g.damage.assign(d.entries.size(), 0);
    g.supply = fullSupply;
    g.movement = 0;  // launched units act from the next turn (inferred)
    g.targetVehicle = targetVehicle;
    g.targetObject = targetObject;
    g.builtTurn = s.turn;
    s.addVehicle(std::move(g));
    return n;
}

int64_t recoverUnits(TurnContext& ctx, UnitBudget& budget, VehicleId id, const Order& o) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle* v = s.vehicle(id);
    if (!v || !alive(*v) || !o.design.valid() || o.design.index() >= s.designs.size()) return 0;
    const DesignId unit = o.design;
    const VehicleType type = r.hull(s.design(unit).hull).type;
    if (type != VehicleType::Fighter && type != VehicleType::Satellite) return 0;  // mines and drones never come back
    const AbilityKind k = launcherFor(type);
    int64_t n = std::min(turnRate(r, s, *v, k) - budget.recovered[{id, k}], freeCargo(r, s, *v) / unitTons(r, s, unit));
    if (o.amount >= 0) n = std::min<int64_t>(n, o.amount);
    int64_t moved = 0;
    for (Vehicle& g : s.vehicles) {
        if (n <= 0) break;
        if (!alive(g) || g.id == id || g.owner != v->owner || g.design != unit || g.location != v->location) continue;
        if (o.vehicle.valid() && g.id != o.vehicle) continue;  // a named group only
        const int64_t take = std::min<int64_t>(n, g.count);
        g.count -= static_cast<int>(take);
        n -= take;
        moved += take;
    }
    if (moved > 0) {
        v = s.vehicle(id);
        auto it = std::find_if(v->cargo.units.begin(), v->cargo.units.end(), [&](const UnitStack& u) { return u.design == unit; });
        if (it == v->cargo.units.end()) v->cargo.units.push_back({unit, static_cast<int>(moved)});
        else it->count += static_cast<int>(moved);
        budget.recovered[{id, k}] += moved;
    }
    return moved;
}

// ---- One-shot components -----------------------------------------------------------------------------

int useComponent(TurnContext& ctx, VehicleId id, int entry) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle* v = s.vehicle(id);
    if (!v || !alive(*v) || v->status == VehicleStatus::Mothballed) return -1;
    const Design& d = s.design(v->design);
    if (entry < 0 || static_cast<size_t>(entry) >= d.entries.size() || !entryIntact(r, s, *v, static_cast<size_t>(entry))) return -1;
    const auto abilities = r.componentAbilities(d.entries[static_cast<size_t>(entry)].component);
    if (hasAbility(abilities, AbilityKind::SelfDestruct)) {  // spec 03 §15: no yard needed
        vehicleLost(ctx, *v, "It self-destructed.");
        return 0;
    }
    const int64_t energy = sumValue1(abilities, AbilityKind::EmergencyEnergy);
    const int64_t resupply = sumValue1(abilities, AbilityKind::EmergencyResupply);
    if (energy <= 0 && resupply <= 0) return -1;
    spendSupply(r, s, *v, scaledSupply(r, s, v->owner, mounted(r, d.entries[static_cast<size_t>(entry)]).supplyUsed));
    if (resupply > 0) v->supply = std::min(v->supply + resupply, std::max(v->supply, vehicleSupplyCapacity(r, s, *v)));  // capped (inferred)
    if (hasAbility(abilities, AbilityKind::ComponentDestroyedOnUse)) {
        if (v->damage.size() < d.entries.size()) v->damage.resize(d.entries.size(), 0);
        v->damage[static_cast<size_t>(entry)] = entryStructure(r, d, static_cast<size_t>(entry));
    }
    ctx.log(v->owner, LogCategory::Misc, std::format("{} used {}", v->name, r.component(d.entries[static_cast<size_t>(entry)].component).name), {},
            v->location);
    return static_cast<int>(std::max<int64_t>(0, energy));
}

} // namespace detail

// ---- Public helpers ------------------------------------------------------------------------------------

using detail::alive;

std::string colonizeProblem(const Rules& r, const GameState& s, const Vehicle& v, ObjectId planet) {
    if (!planet.valid() || planet.index() >= s.galaxy.objects.size()) return "No such planet";
    if (!detail::inSystem(s.galaxy, planet)) return "That planet no longer exists";
    const SpaceObject& obj = s.galaxy.object(planet);
    if (obj.kind != ObjectKind::Planet) return "Only planets can be colonized";
    if (s.colony(planet)) return "The planet is already colonized";
    AbilityKind k = AbilityKind::ColonizeRock;
    if (datafile::keysEqual(obj.surface, "Ice")) k = AbilityKind::ColonizeIce;
    else if (datafile::keysEqual(obj.surface, "Gas Giant") || datafile::keysEqual(obj.surface, "Gas")) k = AbilityKind::ColonizeGas;
    if (!hasAbility(vehicleAbilities(r, s, v), k)) return std::format("{} cannot colonize {} planets", v.name, obj.surface);
    const Race& race = s.empire(v.owner).race;
    if (s.options.onlyBreathable && !datafile::keysEqual(obj.atmosphere, race.atmosphere))
        return "Only planets with a breathable atmosphere may be colonized in this game";
    if (s.options.onlyHomeType && !datafile::keysEqual(obj.surface, race.nativeSurface))
        return "Only planets of the home planet type may be colonized in this game";
    return {};
}

bool resupplyDepotAt(const Rules& r, const GameState& s, EmpireId empire, Location where) {
    for (ObjectId o : planetsAt(s, where)) {
        const Colony* c = s.colony(o);
        if (!c || c->totalPopulation() <= 0) continue;  // facilities need people (inferred)
        if (c->owner != empire && !allied(s, c->owner, empire)) continue;
        if (hasAbility(colonyAbilities(r, s, *c), AbilityKind::SupplyGeneration)) return true;
    }
    return false;
}

int repairCapacityAt(const Rules& r, const GameState& s, EmpireId empire, Location where) {
    int64_t total = 0;
    for (ObjectId o : planetsAt(s, where))
        if (const Colony* c = s.colony(o); c && c->owner == empire && c->totalPopulation() > 0)
            total += sumValue1(colonyAbilities(r, s, *c), AbilityKind::ComponentRepair);
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == empire && v.location == where) total += sumValue1(vehicleAbilities(r, s, v), AbilityKind::ComponentRepair);
    if (total <= 0) return 0;
    // Repair Aptitude and the culture's Repair percentage both scale it (inferred: multiplied).
    const Race& race = s.empire(empire).race;
    const ruleset::Culture* culture = r.culture(race);
    total = total * race.characteristic(Characteristic::RepairAptitude) / 100;
    total = total * (100 + (culture ? culture->repair : 0)) / 100;
    return static_cast<int>(std::max<int64_t>(0, total));
}

int64_t moveSupplyCost(const Rules& r, const GameState& s, const Vehicle& v) {
    if (vehicleHasQuantumReactor(r, s, v)) return 0;
    const Design& d = s.design(v.design);
    int64_t cost = 0;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i) && hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::StandardShipMovement))
            cost += mounted(r, d.entries[i]).supplyUsed;
    return detail::scaledSupply(r, s, v.owner, cost);
}

bool damageVehicle(const Rules& r, GameState& s, Vehicle& v, int amount) {
    if (amount <= 0 || !alive(v)) return false;
    const Design& d = s.design(v.design);
    if (d.entries.empty()) return false;
    if (v.damage.size() < d.entries.size()) v.damage.resize(d.entries.size(), 0);
    int left = amount;
    for (int layer = 0; layer < 2 && left > 0; ++layer) {
        const bool armorLayer = layer == 0;
        for (;;) {
            std::vector<size_t> intact;
            for (size_t i = 0; i < d.entries.size(); ++i)
                if (entryIntact(r, s, v, i) && hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::Armor) == armorLayer)
                    intact.push_back(i);
            if (intact.empty() || left <= 0) break;
            const size_t pick = intact[s.rng.below(intact.size())];
            const int room = entryStructure(r, d, pick) - v.damage[pick];
            const int take = std::min(left, std::max(1, room));
            v.damage[pick] += take;
            left -= take;
        }
    }
    if (!vehicleDestroyed(r, s, v)) return false;
    v.count = 0;
    return true;
}

Vehicle& spawnVehicle(const Rules& r, GameState& s, EmpireId owner, DesignId design, Location where, int autoWaypoint) {
    Design& d = s.design(design);
    Vehicle v;
    v.owner = owner;
    v.design = design;
    v.name = std::format("{} {}", d.name, ++d.built);
    v.location = where;
    v.damage.assign(d.entries.size(), 0);
    v.builtTurn = s.turn;
    v.supply = computeDesignStats(r, nullptr, d).supplyCapacity;
    const Empire& e = s.empire(owner);
    if (autoWaypoint >= 0 && static_cast<size_t>(autoWaypoint) < e.waypoints.size() && e.waypoints[static_cast<size_t>(autoWaypoint)].set)
        v.orders.push_back(Order{OrderKind::MoveTo, e.waypoints[static_cast<size_t>(autoWaypoint)].location});
    return s.addVehicle(std::move(v));
}

} // namespace opense4::game::movement
