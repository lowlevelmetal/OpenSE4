// Shared vehicle helpers: supply, damage outside combat, mines, cargo,
// unit launch and recovery, one-shot components (spec 03 §7, §11-13).

#include "datafile/datafile.hpp"
#include "game/combat.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/xmath.hpp"

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

std::vector<VehicleId> vehiclesInObjectOrder(const GameState& s) {
    std::vector<std::pair<uint32_t, VehicleId>> slots;
    slots.reserve(s.vehicles.size());
    for (const Vehicle& v : s.vehicles) slots.emplace_back(v.slot, v.id);
    std::sort(slots.begin(), slots.end());
    std::vector<VehicleId> out;
    out.reserve(slots.size());
    for (const auto& [slot, id] : slots) out.push_back(id);
    return out;
}

bool computerPlayer(const GameState& s, EmpireId e) {
    return e.valid() && e.index() < s.empires.size() && s.empire(e).kind != PlayerKind::Human;
}

// The chosen leader, else the first member in object order (spec 03 §9).
const Vehicle* fleetLeader(const GameState& s, const Fleet& f) {
    if (const Vehicle* v = s.vehicle(f.leader); v && alive(*v) && v->fleet == f.id) return v;
    const Vehicle* first = nullptr;
    for (VehicleId id : f.members)
        if (const Vehicle* v = s.vehicle(id); v && alive(*v) && (!first || std::pair(v->slot, v->id) < std::pair(first->slot, first->id)))
            first = v;
    return first;
}

// Fleet orders take precedence over a member's own while it is with the leader (inferred).
bool followsFleetOrders(const GameState& s, const Vehicle& v) {
    if (!v.fleet.valid()) return false;
    const Fleet* f = s.fleet(v.fleet);
    if (!f || f->orders.empty()) return false;
    const Vehicle* lead = fleetLeader(s, *f);
    return lead && lead->location == v.location && v.status != VehicleStatus::Mothballed;
}

bool pursuitOver(const GameState& s, EmpireId owner, const Order& o) {
    if (o.vehicle.valid()) {
        const Vehicle* t = s.vehicle(o.vehicle);
        return !t || !alive(*t) || t->owner == owner;
    }
    if (!o.object.valid() || !inSystem(s.galaxy, o.object)) return true;
    const SpaceObject& obj = s.galaxy.object(o.object);
    if (obj.kind != ObjectKind::Planet && obj.kind != ObjectKind::Asteroids) return false;
    const Colony* c = s.colony(o.object);
    return !c || c->owner == owner;
}

// ---- Supply ----------------------------------------------------------------------------------

int64_t scaledSupply(const Rules& r, const GameState& s, EmpireId owner, int64_t amount) {
    if (!owner.valid() || owner.index() >= s.empires.size()) return amount;
    const int64_t c = r.traitValue(s.empire(owner).race, "Supply Cost");
    if (c == 0) return amount;
    return std::max<int64_t>(0, xmath::pctRound(amount, 100 + c));  // to nearest, halves to even (§7, confirmed: binary)
}

void holdSupply(const Rules& r, const GameState& s, Vehicle& v) {
    if (vehicleHasUnlimitedSupply(r, s, v)) {
        v.supply = kUnlimitedSupply;
        return;
    }
    v.supply = std::clamp<int64_t>(v.supply, 0, vehicleSupplyCapacity(r, s, v));
}

void spendSupply(const Rules& r, const GameState& s, Vehicle& v, int64_t amount) {
    holdSupply(r, s, v);
    if (amount <= 0 || vehicleHasUnlimitedSupply(r, s, v)) return;
    v.supply = std::max<int64_t>(0, v.supply - amount);
}

void refillSupply(const Rules& r, const GameState& s, Vehicle& v) {
    if (vehicleType(r, s, v) == VehicleType::Drone) return;  // drones are never resupplied (§12)
    if (!vehicleUsesSupply(r, s, v)) return;
    v.supply = std::max(v.supply, initialSupply(r, s, v));
}

void poolSupply(const Rules& r, GameState& s, std::span<const VehicleId> members) {
    std::vector<Vehicle*> pool;
    for (VehicleId id : members)
        if (Vehicle* v = s.vehicle(id); v && alive(*v) && vehicleUsesSupply(r, s, *v)) pool.push_back(v);
    if (pool.size() < 2) return;
    // Equal shares, the remainder of the division lost; each member takes what
    // fits, and the overflow goes in member order to members with room (§7,
    // confirmed: binary). A member with unlimited supply brings its marker value.
    int64_t total = 0;
    std::vector<int64_t> caps;
    for (Vehicle* v : pool) {
        total += v->supply;
        caps.push_back(vehicleSupplyCapacity(r, s, *v));
    }
    const int64_t share = total / static_cast<int64_t>(pool.size());
    int64_t overflow = 0;
    for (size_t i = 0; i < pool.size(); ++i) {
        pool[i]->supply = std::min(share, caps[i]);
        overflow += share - pool[i]->supply;
    }
    for (size_t i = 0; i < pool.size() && overflow > 0; ++i) {
        const int64_t add = std::min(overflow, caps[i] - pool[i]->supply);
        if (add <= 0) continue;
        pool[i]->supply += add;
        overflow -= add;
    }
    for (Vehicle* v : pool)
        if (vehicleHasUnlimitedSupply(r, s, *v)) v->supply = kUnlimitedSupply;
}

// ---- Losses ------------------------------------------------------------------------------------

namespace {
// The log line and mood of a vehicle lost outside combat.
void announceLoss(TurnContext& ctx, const Vehicle& v, std::string_view cause) {
    ctx.log(v.owner, LogCategory::Misc, std::format("{} destroyed", v.name), std::string(cause), v.location);
    if (!isUnitType(vehicleType(ctx.rules, ctx.state, v))) {
        ctx.mood(v.owner, "Any Ship Lost");
        ctx.mood(v.owner, "Ship Lost in System", v.location.system);
    }
}
} // namespace

void vehicleLost(TurnContext& ctx, Vehicle& v, std::string_view cause) {
    if (!alive(v)) return;
    GameState& s = ctx.state;
    for (const UnitStack& st : groupStacks(v)) s.design(st.design).lost += st.count;  // every unit of a group
    announceLoss(ctx, v, cause);
    v.count = 0;
    v.mixed.clear();
}

bool hurt(TurnContext& ctx, VehicleId id, int amount, std::string_view cause) {
    Vehicle* v = ctx.state.vehicle(id);
    if (!v || !alive(*v) || amount <= 0) return false;
    if (isUnitType(vehicleType(ctx.rules, ctx.state, *v))) {
        // Whole units die; the design statistics count each (damageUnitGroup).
        const Vehicle before = *v;
        damageUnitGroup(ctx.rules, ctx.state, *v, amount, ctx.state.rng);
        if (alive(*v)) return false;
        announceLoss(ctx, before, cause);
        return true;
    }
    const int count = v->count;
    if (!damageVehicle(ctx.rules, ctx.state, *v, amount)) return false;
    v->count = count;  // restored so vehicleLost records it
    vehicleLost(ctx, *v, cause);
    return true;
}

// ---- Mines -------------------------------------------------------------------------------------

bool minefieldActs(const Rules& r, const GameState& s, Location where, std::span<const VehicleId> group) {
    for (const Vehicle& m : s.vehicles) {
        if (!alive(m) || m.location != where || !m.owner.valid() || vehicleType(r, s, m) != VehicleType::Mine) continue;
        bool friendly = false;
        for (VehicleId id : group)
            if (const Vehicle* v = s.vehicle(id); v && (v->owner == m.owner || !hostile(s, m.owner, v->owner))) friendly = true;
        if (!friendly) return true;
    }
    return false;
}

void decloakSweepers(const Rules& r, GameState& s, Location where, std::span<const VehicleId> group) {
    if (group.empty()) return;
    const Vehicle* first = s.vehicle(group.front());
    if (!first) return;
    const auto& tagged = s.empire(first->owner).taggedMinefields;
    if (std::find(tagged.begin(), tagged.end(), where) == tagged.end()) return;
    const bool sweeper = std::any_of(group.begin(), group.end(), [&](VehicleId id) {
        const Vehicle* v = s.vehicle(id);
        return v && alive(*v) && vehicleAbilityTotal(r, s, *v, AbilityKind::MineSweeping) > 0;
    });
    if (!sweeper || !minefieldActs(r, s, where, group)) return;
    for (VehicleId id : group)
        if (Vehicle* v = s.vehicle(id); v && v->status == VehicleStatus::Cloaked) v->status = VehicleStatus::Normal;
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

std::vector<ObjectId> ownColoniesHere(const GameState& s, EmpireId owner, Location where) {
    std::vector<ObjectId> out;
    for (ObjectId o : planetsAt(s, where))
        if (const Colony* c = s.colony(o); c && c->owner == owner) out.push_back(o);
    return out;
}

// Own ships and bases in the sector (cargo holders), in creation order, except `group`.
std::vector<VehicleId> ownHoldersHere(const Rules& r, const GameState& s, EmpireId owner, Location where, VehicleId self,
                                      std::span<const VehicleId> group) {
    std::vector<VehicleId> out;
    for (const Vehicle& v : s.vehicles) {
        if (!alive(v) || v.id == self || v.owner != owner || v.location != where || !isShipOrBase(vehicleType(r, s, v))) continue;
        if (std::find(group.begin(), group.end(), v.id) != group.end()) continue;
        out.push_back(v.id);
    }
    return out;
}

} // namespace

void trimCargo(const Rules& r, const GameState& s, Vehicle& v) {
    const int64_t mass = std::max<int64_t>(1, r.setting("Population Mass", 5));
    int64_t over = cargoSpaceUsed(r, s, v.cargo) - vehicleCargoCapacity(r, s, v);
    while (over > 0 && !v.cargo.population.empty()) {
        PopulationGroup& p = v.cargo.population.front();
        --p.millions;
        over -= mass;
        if (p.millions <= 0) v.cargo.population.erase(v.cargo.population.begin());
    }
    while (over > 0 && !v.cargo.units.empty()) {
        UnitStack& u = v.cargo.units.front();
        --u.count;
        over -= unitTons(r, s, u.design);
        if (u.count <= 0) v.cargo.units.erase(v.cargo.units.begin());
    }
}

int64_t loadCargo(TurnContext& ctx, VehicleId id, DesignId unit, int64_t amount, std::span<const VehicleId> group) {
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

    // Own planets first (none quarantined by plague), then own vehicles (§8, §11).
    for (ObjectId o : ownColoniesHere(s, owner, where)) {
        Colony* c = s.colony(o);
        if (want <= 0 || c->plagueLevel > 0) continue;
        if (unit.valid()) {
            fromCargo(c->cargo);
            continue;
        }
        // A colony keeps at least 1M people (spec 03 §11).
        for (EmpireId race : racesOf(c->population, owner)) {
            v = s.vehicle(id);
            const int64_t spare = std::max<int64_t>(0, c->totalPopulation() - 1);
            const int64_t n = movePopulation(c->population, v->cargo.population, race, std::min({want, spare, freeCargo(r, s, *v) / mass}));
            want -= n;
            moved += n;
        }
    }
    for (VehicleId b : ownHoldersHere(r, s, owner, where, id, group)) {
        if (want <= 0) break;
        fromCargo(s.vehicle(b)->cargo);
    }
    return moved;
}

int64_t dropCargo(TurnContext& ctx, VehicleId id, DesignId unit, int64_t amount, std::span<const VehicleId> group) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle* v = s.vehicle(id);
    if (!v || !alive(*v)) return 0;
    const EmpireId owner = v->owner;
    const Location where = v->location;
    int64_t want = amount < 0 ? std::numeric_limits<int64_t>::max() : amount;
    int64_t moved = 0;
    const int64_t mass = std::max<int64_t>(1, r.setting("Population Mass", 5));

    for (ObjectId o : ownColoniesHere(s, owner, where)) {
        if (want <= 0) break;
        Colony* c = s.colony(o);
        v = s.vehicle(id);
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
    for (VehicleId b : ownHoldersHere(r, s, owner, where, id, group)) {
        if (want <= 0) break;
        Vehicle* holder = s.vehicle(b);
        v = s.vehicle(id);
        if (unit.valid()) {
            const int64_t n = moveUnits(v->cargo, holder->cargo, unit, std::min(want, freeCargo(r, s, *holder) / unitTons(r, s, unit)));
            want -= n;
            moved += n;
        } else {
            for (EmpireId race : racesOf(v->cargo.population, owner)) {
                const int64_t n = movePopulation(v->cargo.population, holder->cargo.population, race, std::min(want, freeCargo(r, s, *holder) / mass));
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
    if (ownColoniesHere(ctx.state, v->owner, v->location).empty()) return 0;
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

// A colonized planet launches up to this many units of each kind per game turn (§12).
constexpr int64_t kPlanetLaunchesPerTurn = 1000;

int64_t unitsOfTypeHere(const Rules& r, const GameState& s, EmpireId owner, Location where, VehicleType t) {
    int64_t n = 0;
    for (const Vehicle& g : s.vehicles)
        if (alive(g) && g.owner == owner && g.location == where && vehicleType(r, s, g) == t) n += g.count;
    return n;
}

struct Holder {
    Cargo* cargo = nullptr;
    EmpireId owner;
    Location where;
    int64_t perTurn = 0;       // launches per unit kind this game turn
    bool canRecover = false;
    int64_t freeSpace = 0;
};

Holder holderOf(const Rules& r, GameState& s, Launcher l, AbilityKind k) {
    Holder h;
    if (l.vehicle.valid()) {
        Vehicle* v = s.vehicle(l.vehicle);
        if (!v || !alive(*v) || !isShipOrBase(vehicleType(r, s, *v))) return h;
        const std::vector<ParsedAbility> abilities = vehicleAbilities(r, s, *v);
        h.cargo = &v->cargo;
        h.owner = v->owner;
        h.where = v->location;
        h.perTurn = abilitySum(abilities, k, true);  // Σ Val 2, no fallback to Val 1 (confirmed: binary)
        h.canRecover = hasAbility(abilities, k);     // recovery needs the matching bay too (§12, confirmed: binary)
        h.freeSpace = freeCargo(r, s, *v);
        return h;
    }
    Colony* c = s.colony(l.planet);
    if (!c) return h;
    h.cargo = &c->cargo;
    h.owner = c->owner;
    h.where = locationOf(s.galaxy, c->planet);
    h.perTurn = kPlanetLaunchesPerTurn;  // no ability needed (confirmed: binary)
    h.canRecover = true;
    h.freeSpace = colonyFreeCargo(r, s, *c);
    return h;
}

} // namespace

int64_t launchUnits(TurnContext& ctx, UnitBudget& budget, Launcher from, const Order& o) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!o.design.valid() || o.design.index() >= s.designs.size()) return 0;
    const DesignId unit = o.design;
    const VehicleType type = r.hull(s.design(unit).hull).type;
    const AbilityKind k = launcherFor(type);
    if (k == AbilityKind::Unknown) return 0;  // troops and weapon platforms are never launched into space
    Holder h = holderOf(r, s, from, k);
    if (!h.cargo) return 0;
    const auto key = std::make_tuple(from.vehicle, from.planet, k);
    int64_t n = std::min<int64_t>(h.cargo->unitCount(unit), h.perTurn - budget.launched[key]);
    if (o.amount >= 0) n = std::min<int64_t>(n, o.amount);
    // Mines and satellites per sector: refused at the cap, otherwise cut to the room left.
    const char* capKey = type == VehicleType::Mine ? "Maximum Mines Per Player Per Sector"
                         : type == VehicleType::Satellite ? "Maximum Satellites Per Player Per Sector"
                                                          : nullptr;
    if (capKey) {
        const int64_t room = r.setting(capKey, 100) - unitsOfTypeHere(r, s, h.owner, h.where, type);
        if (room <= 0) return 0;
        n = std::min(n, room);
    }
    // Units in space: the empire must be below the cap before the stack; the
    // launch is not cut to fit (confirmed: binary).
    if (unitsInSpace(r, s, h.owner) >= s.options.maxUnitsPerPlayer) return 0;
    if (n <= 0) return 0;

    Cargo taken;
    n = moveUnits(*h.cargo, taken, unit, n);
    budget.launched[key] += n;
    const EmpireId owner = h.owner;
    const Location where = h.where;
    const Design& d = s.design(unit);

    // A new group starts full, with 0 movement; a turn-based launch gives it
    // its full movement, so it can move and be recovered in the same turn
    // (§12, confirmed: binary). A drone gets no target and no order.
    auto newGroup = [&](int count) {
        Vehicle g;
        g.owner = owner;
        g.design = unit;
        g.name = d.name;
        g.location = where;
        g.count = count;
        g.damage.assign(d.entries.size(), 0);
        g.movement = 0;
        g.builtTurn = s.turn;
        Vehicle& added = s.addVehicle(std::move(g));
        added.supply = initialSupply(r, s, added);
        if (budget.turnBased) added.movement = turnMovement(r, s, added);
    };
    if (type == VehicleType::Drone) {
        for (int64_t i = 0; i < n; ++i) newGroup(1);  // every drone is its own group and never merges (confirmed: binary)
        return n;
    }
    // The units join the last group of their kind and owner in the sector's
    // object order, whatever its designs, fleet, orders or cloak, and only that
    // group is refilled to its new maximum (§12, confirmed: binary).
    Vehicle* last = nullptr;
    for (VehicleId id : vehiclesInObjectOrder(s))
        if (Vehicle* g = s.vehicle(id); g && alive(*g) && g->owner == owner && g->location == where && vehicleType(r, s, *g) == type) last = g;
    if (!last) {
        newGroup(static_cast<int>(n));
        return n;
    }
    addGroupUnits(s, *last, unit, static_cast<int>(n));
    last->supply = initialSupply(r, s, *last);
    return n;
}

int64_t recoverUnits(TurnContext& ctx, Launcher into, const Order& o) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!o.design.valid() || o.design.index() >= s.designs.size()) return 0;
    const VehicleType type = r.hull(s.design(o.design).hull).type;
    if (type != VehicleType::Fighter && type != VehicleType::Satellite) return 0;  // only fighters and satellites come back
    const Holder h = holderOf(r, s, into, launcherFor(type));
    if (!h.cargo || !h.canRecover) return 0;
    Cargo* cargo = into.vehicle.valid() ? &s.vehicle(into.vehicle)->cargo : &s.colony(into.planet)->cargo;
    int64_t room = h.freeSpace;  // no per-turn limit: free cargo space is the only limit (confirmed: binary)
    int64_t moved = 0;
    // Units of one design leave the group into the cargo, as far as room allows.
    auto take = [&](Vehicle& g, DesignId unit, int64_t wanted) {
        const int64_t fit = std::min(wanted, room / unitTons(r, s, unit));
        if (fit <= 0) return int64_t{0};
        const int n = removeGroupUnits(s, g, unit, static_cast<int>(std::min<int64_t>(fit, std::numeric_limits<int>::max())));
        // (inferred) the supply left over stays with the group, up to what the rest can hold.
        if (g.count > 0) g.supply = std::min(g.supply, vehicleSupplyCapacity(r, s, g));
        room -= int64_t{n} * unitTons(r, s, unit);
        auto it = std::find_if(cargo->units.begin(), cargo->units.end(), [&](const UnitStack& u) { return u.design == unit; });
        if (it == cargo->units.end()) cargo->units.push_back({unit, n});
        else it->count += n;
        return int64_t{n};
    };
    auto ofKindHere = [&](const Vehicle& g) {
        return alive(g) && g.id != into.vehicle && g.owner == h.owner && g.location == h.where && vehicleType(r, s, g) == type;
    };
    // In turn-based games a fighter group comes back only with its full movement (§12, confirmed: binary).
    auto recoverable = [&](const Vehicle& g) {
        return ofKindHere(g) && (s.options.simultaneous || type != VehicleType::Fighter || g.movement >= turnMovement(r, s, g));
    };
    if (o.vehicle.valid()) {
        // The Launch/Recover window names one group and one design of it (OpenSE4's
        // stand-in for the turn-based window in simultaneous games, inferred).
        Vehicle* g = s.vehicle(o.vehicle);
        if (!g || !recoverable(*g)) return 0;
        const int64_t wanted = o.amount >= 0 ? o.amount : groupUnits(*g, o.design);
        return take(*g, o.design, std::min<int64_t>(wanted, groupUnits(*g, o.design)));
    }
    // Recover Units names a unit kind: each own group of that kind in the sector,
    // in object order, gives the units of every design as far as cargo room
    // allows; the next group comes only while the previous one gave at least
    // one unit (spec 03 §8, confirmed: binary).
    for (VehicleId id : vehiclesInObjectOrder(s)) {
        Vehicle* g = s.vehicle(id);
        if (!g || !ofKindHere(*g)) continue;
        int64_t gave = 0;
        if (recoverable(*g))
            for (const UnitStack& st : groupStacks(*g)) {
                if (!alive(*g)) break;
                gave += take(*g, st.design, st.count);
            }
        moved += gave;
        if (gave <= 0) break;
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
    const auto e = static_cast<size_t>(entry);
    const auto abilities = r.componentAbilities(d.entries[e].component);
    if (hasAbility(abilities, AbilityKind::SelfDestruct)) {  // spec 03 §15: no yard needed
        vehicleLost(ctx, *v, "It self-destructed.");
        return 0;
    }
    const int64_t energy = abilitySum(abilities, AbilityKind::EmergencyEnergy);
    const int64_t resupply = abilitySum(abilities, AbilityKind::EmergencyResupply);
    if (energy <= 0 && resupply <= 0) return -1;
    // A part destroyed on use goes first; no supply is charged (§8, confirmed: binary).
    if (hasAbility(abilities, AbilityKind::ComponentDestroyedOnUse)) {
        if (v->damage.size() < d.entries.size()) v->damage.resize(d.entries.size(), 0);
        v->damage[e] = entryStructure(r, d, e);
        fitToCapacity(r, s, *v);  // storage the part held goes with it (§7, §11)
    }
    if (resupply > 0 && !vehicleHasUnlimitedSupply(r, s, *v))
        v->supply = std::max(v->supply, std::min(v->supply + resupply, vehicleSupplyCapacity(r, s, *v)));
    ctx.log(v->owner, LogCategory::Misc, std::format("{} used {}", v->name, r.component(d.entries[e].component).name), {}, v->location);
    // Emergency energy only helps a vehicle that can move at all.
    return energy > 0 && vehicleMaxMovement(r, s, *v) > 0 ? static_cast<int>(std::min<int64_t>(energy, 1000)) : 0;
}

// ---- Cloaking ------------------------------------------------------------------------------------------

bool canCloak(const Rules& r, const GameState& s, const Vehicle& v) {
    const std::vector<ParsedAbility> list = vehicleAbilities(r, s, v);
    for (size_t t = 0; t < kSightTypes; ++t)
        if (abilityPerSightType(list, AbilityKind::CloakLevel, static_cast<SightType>(t)) >= 2) return true;
    return false;
}

int64_t cloakSupply(const Rules& r, const GameState& s, const Vehicle& v) {
    if (v.status == VehicleStatus::Mothballed) return 0;
    const Design& d = s.design(v.design);
    int64_t cost = 0;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i) && hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::CloakLevel))
            cost += mounted(r, d.entries[i]).supplyUsed;
    return cost;
}

} // namespace detail

// ---- Public helpers ------------------------------------------------------------------------------------

using detail::alive;
using ruleset::VehicleType;

int unitsInSpace(const Rules& r, const GameState& s, EmpireId owner) {
    int64_t n = 0;
    for (const Vehicle& g : s.vehicles)
        if (alive(g) && g.owner == owner && isUnitType(vehicleType(r, s, g))) n += g.count;
    return static_cast<int>(std::min<int64_t>(n, std::numeric_limits<int>::max()));
}

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
        if (!c) continue;  // a colonized planet; no population is needed (confirmed: binary)
        const bool usable = c->owner == empire || (empire.valid() && c->owner.valid() && empire.index() < s.empires.size() &&
                                                    treatyAllowsResupply(s.empire(empire).relation(c->owner).treaty));
        if (usable && hasAbility(colonyAbilities(r, s, *c), AbilityKind::SupplyGeneration)) return true;
    }
    return false;
}

int64_t repairPoolAt(const Rules& r, const GameState& s, EmpireId empire, Location where) {
    // Every own colonized planet (its abilities and facilities, no population
    // needed), ship, base and unit group in the sector (§13, confirmed: binary).
    int64_t pool = 0;
    for (ObjectId o : planetsAt(s, where))
        if (const Colony* c = s.colony(o); c && c->owner == empire) pool += abilitySum(colonyAbilities(r, s, *c), AbilityKind::ComponentRepair);
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == empire && v.location == where) pool += vehicleAbilityTotal(r, s, v, AbilityKind::ComponentRepair);
    return std::min(pool, kAbilitySumCap);
}

int64_t repairModifier(const Rules& r, const GameState& s, EmpireId empire) {
    if (!empire.valid() || empire.index() >= s.empires.size()) return 0;
    const Race& race = s.empire(empire).race;
    const ruleset::Culture* culture = r.culture(race);
    return r.traitValue(race, "Repair") + (race.characteristic(Characteristic::RepairAptitude) - 100) + (culture ? culture->repair : 0);
}

int repairCapacityAt(const Rules& r, const GameState& s, EmpireId empire, Location where) {
    const int64_t pool = repairPoolAt(r, s, empire, where);
    if (pool <= 0) return 0;
    // One truncation of pool × (100 + R) % (§13, confirmed: binary).
    const int64_t points = xmath::pctTrunc(pool, 100 + repairModifier(r, s, empire));
    return static_cast<int>(std::clamp<int64_t>(points, 0, std::numeric_limits<int>::max()));
}

int64_t moveSupplyCost(const Rules& r, const GameState& s, const Vehicle& v) {
    if (vehicleHasUnlimitedSupply(r, s, v)) return 0;
    // S = the mounted supply use of every working component with Standard Ship
    // Movement, Movement Bonus or Extra Movement Generation above 0 (§7).
    const Design& d = s.design(v.design);
    int64_t cost = 0;
    for (size_t i = 0; i < d.entries.size(); ++i) {
        if (!entryIntact(r, s, v, i)) continue;
        const auto ab = r.componentAbilities(d.entries[i].component);
        auto positive = [&](AbilityKind k) {
            return std::any_of(ab.begin(), ab.end(), [&](const ParsedAbility& a) { return a.kind == k && a.value1 > 0; });
        };
        if (positive(AbilityKind::StandardShipMovement) || positive(AbilityKind::MovementBonus) || positive(AbilityKind::ExtraMovementGeneration))
            cost += mounted(r, d.entries[i]).supplyUsed;
    }
    // A unit group pays for every unit: Σ over its designs of the design's cost
    // × its count, then the racial percentage (§12).
    if (isUnitType(vehicleType(r, s, v))) {
        if (!v.mixed.empty()) {
            int64_t total = 0;
            for (const UnitStack& st : v.mixed) {
                Vehicle probe = stackProbe(s, v, UnitStack{st.design, 1});   // one unit of the design
                probe.owner = {};  // the racial percentage comes once, below
                total += moveSupplyCost(r, s, probe) * st.count;
            }
            return detail::scaledSupply(r, s, v.owner, total);
        }
        cost *= std::max(1, v.count);
    }
    return detail::scaledSupply(r, s, v.owner, cost);
}

int64_t damageUnitGroup(const Rules& r, GameState& s, Vehicle& v, int64_t amount, Rng& rng) {
    if (amount <= 0 || !alive(v)) return 0;
    // Spec 04 §9.4 with both pools at 0 and nothing carried over (spec 03 §6.2,
    // confirmed: binary): the damage (at most 50,000) is the pool; up to 20
    // times one of the group's designs is drawn, all equally likely, a design
    // with no units left wasting the draw; a unit dies when the pool covers its
    // hit points, which then leave the pool. The rest is lost.
    std::vector<UnitStack> stacks = groupStacks(v);
    int64_t pool = std::min<int64_t>(amount, combat::kMaxShotDamage);
    int64_t used = 0;
    for (int draw = 0; draw < 20; ++draw) {
        if (std::none_of(stacks.begin(), stacks.end(), [](const UnitStack& st) { return st.count > 0; })) break;
        UnitStack& st = stacks[stacks.size() == 1 ? 0 : rng.below(stacks.size())];
        if (st.count <= 0) continue;
        const int64_t hp = std::max<int64_t>(1, combat::detail::unitHitPoints(r, s.design(st.design), combat::DamageType::Normal));
        if (pool < hp) continue;
        pool -= hp;
        used += hp;
        --st.count;
        ++s.design(st.design).lost;
    }
    setGroupStacks(s, v, std::move(stacks));
    if (alive(v)) fitToCapacity(r, s, v);
    return used;
}

bool damageVehicle(const Rules& r, GameState& s, Vehicle& v, int amount) {
    if (amount <= 0 || !alive(v)) return false;
    if (isUnitType(vehicleType(r, s, v))) {
        damageUnitGroup(r, s, v, amount, s.rng);   // records the units lost
        return !alive(v);
    }
    // Whole components in the random, structure-weighted order with all armor
    // first, until the damage cannot cover the next one; the rest is lost
    // (spec 03 §6.2, spec 04 §9.1a, confirmed: binary).
    combat::detail::destroyComponents(r, s, v, amount, combat::DamageType::Normal, s.rng);
    if (!vehicleDestroyed(r, s, v)) {
        fitToCapacity(r, s, v);
        return false;
    }
    v.count = 0;
    return true;
}

bool canSelfDestruct(const Rules& r, const GameState& s, const Vehicle& v) {
    if (!alive(v)) return false;
    switch (vehicleType(r, s, v)) {
        case VehicleType::Ship:
        case VehicleType::Base: return hasAbility(vehicleAbilities(r, s, v), AbilityKind::SelfDestruct);  // empty when mothballed
        case VehicleType::Satellite:
        case VehicleType::Mine:
        case VehicleType::Drone: return true;
        default: return false;  // fighter groups never
    }
}

void fitToCapacity(const Rules& r, const GameState& s, Vehicle& v) {
    if (v.supply != kUnlimitedSupply && vehicleUsesSupply(r, s, v) && !vehicleHasUnlimitedSupply(r, s, v))
        v.supply = std::clamp<int64_t>(v.supply, 0, vehicleSupplyCapacity(r, s, v));
    detail::trimCargo(r, s, v);
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
    v.supply = initialSupply(r, s, v);  // full; bases and reactor ships unlimited (§7)
    const Empire& e = s.empire(owner);
    if (autoWaypoint >= 0 && static_cast<size_t>(autoWaypoint) < e.waypoints.size() && e.waypoints[static_cast<size_t>(autoWaypoint)].set)
        v.orders.push_back(Order{OrderKind::MoveTo, e.waypoints[static_cast<size_t>(autoWaypoint)].location});
    return s.addVehicle(std::move(v));
}

} // namespace opense4::game::movement
