// Shared vehicle helpers: supply, damage outside combat, mines, cargo,
// unit launch and recovery, one-shot components (spec 03 §7, §11-13).

#include "datafile/datafile.hpp"
#include "game/hooks.hpp"
#include "game/log_picture.hpp"
#include "game/combat.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
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

bool computerPlayer(const GameState& s, EmpireId e) {
    return e.valid() && e.index() < s.empires.size() && s.empire(e).kind != PlayerKind::Human;
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
    if (amount <= 0 || vehicleHasUnlimitedSupply(r, s, v) || v.supply <= 0) return;
    v.supply = std::max<int64_t>(0, v.supply - amount);
    // Supply running out drops the maximum to 1 (§6.1): the movement left is capped at once.
    if (v.supply == 0) capMovement(r, s, v);
}

void refillSupply(const Rules& r, const GameState& s, Vehicle& v) {
    if (vehicleType(r, s, v) == VehicleType::Drone) return;  // drones are never resupplied (§12)
    if (!vehicleUsesSupply(r, s, v)) return;
    v.supply = std::max(v.supply, initialSupply(r, s, v));
    // A resupply recomputes the maximum, which caps the movement left: a
    // turn-based game's unspent Emergency Energy above it is lost (§6.1).
    capMovement(r, s, v);
}

void depotCheck(TurnContext& ctx, Vehicle& v) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!alive(v)) return;
    if (v.status != VehicleStatus::Mothballed && isShipOrBase(vehicleType(r, s, v))) {
        const int64_t medical = bestValue1(vehicleAbilities(r, s, v), AbilityKind::MedicalBay);
        if (medical > 0)
            for (ObjectId o : planetsAt(s, v.location))
                if (Colony* c = s.colony(o); c && c->plagueLevel > 0 && medical >= c->plagueLevel && allied(s, v.owner, c->owner))
                    economy::curePlague(ctx, *c);
    }
    if (resupplyDepotAt(r, s, v.owner, v.location)) refillSupply(r, s, v);
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
    ctx.log(v.owner, LogCategory::Misc, std::format("{} destroyed", v.name), std::string(cause), v.location, "ShipDamaged");
    if (!isUnitType(vehicleType(ctx.rules, ctx.state, v))) {
        ctx.mood(v.owner, "Any Ship Lost");
        ctx.mood(v.owner, "Ship Lost in System", v.location.system);
    }
}
} // namespace

void vehicleLost(TurnContext& ctx, Vehicle& v, std::string_view cause) {
    if (!alive(v)) return;
    noteVehicleLost(ctx, v, "hazard");
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
        noteVehicleLost(ctx, before, "hazard");
        announceLoss(ctx, before, cause);
        return true;
    }
    const int count = v->count;
    if (!damageVehicle(ctx.rules, ctx.state, *v, amount)) return false;
    v->count = count;  // restored so vehicleLost records it
    // A ship or base destroyed by damage loses the units it carries too: each
    // stack's full count is lost for its design (spec 04 §15, confirmed: binary).
    for (const UnitStack& st : v->cargo.units) ctx.state.design(st.design).lost += st.count;
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

bool decloakSweepers(const Rules& r, GameState& s, Location where, std::span<const VehicleId> group) {
    if (group.empty()) return false;
    const Vehicle* first = s.vehicle(group.front());
    if (!first) return false;
    const auto& tagged = s.empire(first->owner).taggedMinefields;
    if (std::find(tagged.begin(), tagged.end(), where) == tagged.end()) return false;
    const bool sweeper = std::any_of(group.begin(), group.end(), [&](VehicleId id) {
        const Vehicle* v = s.vehicle(id);
        return v && alive(*v) && vehicleAbilityTotal(r, s, *v, AbilityKind::MineSweeping) > 0;
    });
    if (!sweeper || !minefieldActs(r, s, where, group)) return false;
    bool any = false;
    for (VehicleId id : group)
        if (Vehicle* v = s.vehicle(id); v && v->status == VehicleStatus::Cloaked) {
            v->status = VehicleStatus::Normal;
            capMovement(r, s, *v);  // the maximum is recomputed (spec 03 §6.1)
            any = true;
        }
    return any;
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

CargoWhat cargoWhat(const Order& o) {
    CargoWhat w;
    if (o.cargo != CargoKind::None) {
        w.type = unitTypeOf(o.cargo);   // Population: neither a type nor a design
        return w;
    }
    w.design = o.design;
    return w;
}

bool CargoWhat::takes(const Rules& r, const GameState& s, DesignId d) const {
    if (!d.valid() || d.index() >= s.designs.size()) return false;
    if (type) return r.hull(s.design(d).hull).type == *type;
    return d == design;
}

std::vector<DesignId> designsIn(const Rules& r, const GameState& s, const Cargo& c, const CargoWhat& what) {
    std::vector<DesignId> out;
    for (const UnitStack& u : c.units)
        if (u.count > 0 && what.takes(r, s, u.design) && std::find(out.begin(), out.end(), u.design) == out.end()) out.push_back(u.design);
    return out;
}

int64_t loadCargo(TurnContext& ctx, VehicleId id, const CargoWhat& what, int64_t amount, bool* noRoom) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle* v = s.vehicle(id);
    if (noRoom) *noRoom = false;
    if (!v || !alive(*v)) return 0;
    const EmpireId owner = v->owner;
    const Location where = v->location;
    int64_t want = amount < 0 ? std::numeric_limits<int64_t>::max() : amount;
    int64_t moved = 0;
    const int64_t mass = std::max<int64_t>(1, r.setting("Population Mass", 5));

    bool offered = false;   // a source held something of the kind (for noRoom)
    auto fromCargo = [&](Cargo& src) {
        if (!what.population()) {
            if (!designsIn(r, s, src, what).empty()) offered = true;
            // Every design it takes, stack by stack in the source's cargo order.
            for (DesignId unit : designsIn(r, s, src, what)) {
                if (want <= 0) break;
                v = s.vehicle(id);
                const int64_t n = moveUnits(src, v->cargo, unit, std::min(want, freeCargo(r, s, *v) / unitTons(r, s, unit)));
                want -= n;
                moved += n;
            }
            return;
        }
        if (src.totalPopulation() > 0) offered = true;
        for (EmpireId race : racesOf(src.population, owner)) {
            v = s.vehicle(id);
            const int64_t n = movePopulation(src.population, v->cargo.population, race, std::min(want, freeCargo(r, s, *v) / mass));
            want -= n;
            moved += n;
        }
    };

    // Each member loads for itself in two passes (spec 03 §8 Load Cargo,
    // confirmed: binary): first from the own colonies in the sector, in the
    // system's object order, a plague making no difference (only the Cargo
    // Transfer window refuses a plagued planet, §11); then, while it still
    // has room, from the cargo of the empire's own bases there that are not
    // cloaked, in object order. Ships are never sources: moving cargo from
    // ship to ship is what Cargo Transfer is for.
    for (ObjectId o : ownColoniesHere(s, owner, where)) {
        Colony* c = s.colony(o);
        if (want <= 0) break;
        if (!what.population()) {
            fromCargo(c->cargo);
            continue;
        }
        // A colony keeps at least 1M people (spec 03 §11).
        if (c->totalPopulation() > 1) offered = true;
        for (EmpireId race : racesOf(c->population, owner)) {
            v = s.vehicle(id);
            const int64_t spare = std::max<int64_t>(0, c->totalPopulation() - 1);
            const int64_t n = movePopulation(c->population, v->cargo.population, race, std::min({want, spare, freeCargo(r, s, *v) / mass}));
            want -= n;
            moved += n;
        }
    }
    for (VehicleId b : vehiclesInObjectOrder(s)) {
        if (want <= 0) break;
        const Vehicle* base = s.vehicle(b);
        if (b == id || !alive(*base) || base->owner != owner || base->location != where || base->status == VehicleStatus::Cloaked ||
            vehicleType(r, s, *base) != VehicleType::Base)
            continue;
        fromCargo(s.vehicle(b)->cargo);
    }
    // Nothing came aboard although a source held some: the hold had no room.
    if (noRoom) *noRoom = moved == 0 && offered;
    return moved;
}

int64_t dropCargo(TurnContext& ctx, VehicleId id, const CargoWhat& what, int64_t amount, std::span<const VehicleId> group, ObjectId colony,
                  VehicleId into, bool* noRoom) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle* v = s.vehicle(id);
    if (noRoom) *noRoom = false;
    if (!v || !alive(*v)) return 0;
    // Something of the kind aboard, and a place that could take it (for noRoom).
    const bool aboard = what.population() ? v->cargo.totalPopulation() > 0 : !designsIn(r, s, v->cargo, what).empty();
    bool place = false;
    const EmpireId owner = v->owner;
    const Location where = v->location;
    int64_t want = amount < 0 ? std::numeric_limits<int64_t>::max() : amount;
    int64_t moved = 0;
    const int64_t mass = std::max<int64_t>(1, r.setting("Population Mass", 5));

    // Into a holder's cargo: every design it takes, stack by stack in the ship's cargo order.
    auto unitsInto = [&](Cargo& to, auto room) {
        for (DesignId unit : designsIn(r, s, s.vehicle(id)->cargo, what)) {
            if (want <= 0) break;
            const int64_t n = moveUnits(s.vehicle(id)->cargo, to, unit, std::min(want, room() / unitTons(r, s, unit)));
            want -= n;
            moved += n;
        }
    };
    const bool named = colony.valid() || into.valid();
    for (ObjectId o : ownColoniesHere(s, owner, where)) {
        if (want <= 0) break;
        if (named && o != colony) continue;
        Colony* c = s.colony(o);
        place = true;
        v = s.vehicle(id);
        if (!what.population()) {
            unitsInto(c->cargo, [&] { return colonyFreeCargo(r, s, *c); });
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
    if (!what.population() && !named)
        for (DesignId unit : designsIn(r, s, s.vehicle(id)->cargo, what)) {
            if (!combat::isTroopDesign(r, s, unit)) continue;
            for (ObjectId o : planetsAt(s, where)) {
                const Colony* c = s.colony(o);
                if (want <= 0 || !c || c->owner == owner || !hostile(s, owner, c->owner)) continue;
                place = true;
                const int n = combat::landTroops(r, s, id, o, unit, static_cast<int>(std::min<int64_t>(want, s.vehicle(id)->cargo.unitCount(unit))));
                want -= n;
                moved += n;
            }
        }
    for (VehicleId b : ownHoldersHere(r, s, owner, where, id, group)) {
        if (want <= 0) break;
        if (named && b != into) continue;
        Vehicle* holder = s.vehicle(b);
        place = true;
        v = s.vehicle(id);
        if (!what.population()) {
            unitsInto(holder->cargo, [&] { return freeCargo(r, s, *holder); });
        } else {
            for (EmpireId race : racesOf(v->cargo.population, owner)) {
                const int64_t n = movePopulation(v->cargo.population, holder->cargo.population, race, std::min(want, freeCargo(r, s, *holder) / mass));
                want -= n;
                moved += n;
            }
        }
    }
    // Nothing left the ship although it held some and had a place to drop
    // it: the place had no room.
    if (noRoom) *noRoom = moved == 0 && aboard && place;
    return moved;
}

int64_t loadColonists(TurnContext& ctx, VehicleId id) {
    const Vehicle* v = ctx.state.vehicle(id);
    if (!v || !alive(*v) || v->cargo.totalPopulation() > 0) return 0;
    if (ownColoniesHere(ctx.state, v->owner, v->location).empty()) return 0;
    return loadCargo(ctx, id, CargoWhat{}, -1);
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

// "fighters", "mines", ...: a unit type in a warning's words.
std::string unitWords(VehicleType t) {
    switch (t) {
        case VehicleType::Fighter: return "fighters";
        case VehicleType::Satellite: return "satellites";
        case VehicleType::Mine: return "mines";
        case VehicleType::Drone: return "drones";
        case VehicleType::Troop: return "troops";
        case VehicleType::WeaponPlatform: return "weapon platforms";
        default: return "ships";
    }
}

// A launcher or recoverer: an own ship or base, or a colonized planet.
struct Holder {
    const Cargo* cargo = nullptr;
    EmpireId owner;
    Location where;
    std::string name;
    bool planet = false;
    std::vector<ParsedAbility> abilities;   // a vehicle's
    int64_t freeSpace = 0;
};

Holder holderOf(const Rules& r, const GameState& s, Launcher l) {
    Holder h;
    if (l.vehicle.valid()) {
        const Vehicle* v = s.vehicle(l.vehicle);
        if (!v || !alive(*v) || !isShipOrBase(vehicleType(r, s, *v))) return h;
        h.cargo = &v->cargo;
        h.owner = v->owner;
        h.where = v->location;
        h.name = v->name;
        h.abilities = vehicleAbilities(r, s, *v);
        h.freeSpace = freeCargo(r, s, *v);
        return h;
    }
    const Colony* c = s.colony(l.planet);
    if (!c) return h;
    h.cargo = &c->cargo;
    h.owner = c->owner;
    h.where = locationOf(s.galaxy, c->planet);
    h.name = s.galaxy.object(c->planet).name;
    h.planet = true;
    h.freeSpace = colonyFreeCargo(r, s, *c);
    return h;
}

// Launches per unit kind this game turn: Σ Val 2 of the matching ability, no
// fallback to Val 1 (confirmed: binary); a planet's 1000, no ability needed.
int64_t perTurn(const Holder& h, AbilityKind k) { return h.planet ? kPlanetLaunchesPerTurn : abilitySum(h.abilities, k, true); }

// Recovery needs the matching bay too; a planet none (§12, confirmed: binary).
bool canRecover(const Holder& h, AbilityKind k) { return h.planet || hasAbility(h.abilities, k); }

void warn(UnitTransfer& t, std::string text) {
    if (std::find(t.warnings.begin(), t.warnings.end(), text) == t.warnings.end()) t.warnings.push_back(std::move(text));
}

// One stack of a launch or recovery as planned: `count` units of `design`
// (from or into the unit group `group`, for a recovery).
struct Step {
    DesignId design;
    int64_t count = 0;
    VehicleId group;
};
struct Plan {
    UnitTransfer result;
    std::vector<Step> steps;
};

// The launch rules of §12 (confirmed: binary), stack by stack in cargo order:
// the per-turn budget per launcher and kind, the order's amount, the mines
// and satellites per sector (refused at the cap, otherwise cut to the room
// left), and the units in space, which must be below the cap before each
// stack (the launch is not cut to fit). Warnings (§12 "Warnings"): a kind
// that is never launched, no launcher of the kind, the units-in-space cap and
// the per-sector cap; the per-turn budget and an empty hold give none.
Plan launchPlan(const Rules& r, const GameState& s, const UnitBudget& budget, Launcher from, const CargoWhat& what, int64_t amount) {
    Plan p;
    const Holder h = holderOf(r, s, from);
    if (!h.cargo) return p;
    std::optional<VehicleType> type = what.type;
    if (!type && what.design.valid() && what.design.index() < s.designs.size()) type = r.hull(s.design(what.design).hull).type;
    const AbilityKind k = type ? launcherFor(*type) : AbilityKind::Unknown;
    if (k == AbilityKind::Unknown) {
        // Troops and weapon platforms are never launched into space, nor population.
        warn(p.result, std::format("Only fighters, satellites, mines and drones can be launched; {} cannot launch {}.", h.name,
                                   type ? unitWords(*type) : std::string("population")));
        return p;
    }
    if (!h.planet && !hasAbility(h.abilities, k)) {
        warn(p.result, std::format("{} has nothing that can launch {}.", h.name, unitWords(*type)));
        return p;
    }
    const auto it = budget.launched.find(std::make_tuple(from.vehicle, from.planet, k));
    int64_t left = perTurn(h, k) - (it == budget.launched.end() ? 0 : it->second);
    int64_t want = amount < 0 ? std::numeric_limits<int64_t>::max() : amount;
    const char* capKey = *type == VehicleType::Mine ? "Maximum Mines Per Player Per Sector"
                         : *type == VehicleType::Satellite ? "Maximum Satellites Per Player Per Sector"
                                                           : nullptr;
    const int64_t cap = capKey ? r.setting(capKey, 100) : 0;
    int64_t present = capKey ? unitsOfTypeHere(r, s, h.owner, h.where, *type) : 0;
    int64_t inSpace = unitsInSpace(r, s, h.owner);
    for (DesignId unit : designsIn(r, s, *h.cargo, what)) {
        int64_t n = std::min({int64_t{h.cargo->unitCount(unit)}, left, want});
        if (n <= 0) continue;
        if (capKey) {
            const int64_t room = cap - present;
            if (room < n) warn(p.result, std::format("At most {} of our {} may be in one sector, so {} could launch {}.", cap, unitWords(*type),
                                                     h.name, room <= 0 ? std::string("none") : std::format("only {}", room)));
            if (room <= 0) continue;
            n = std::min(n, room);
        }
        if (inSpace >= s.options.maxUnitsPerPlayer) {
            warn(p.result, std::format("{} cannot launch more units: the empire already has {} units in space, the most this game allows.",
                                       h.name, s.options.maxUnitsPerPlayer));
            continue;
        }
        p.steps.push_back(Step{unit, n, {}});
        p.result.moved += n;
        left -= n;
        want -= n;
        present += n;
        inSpace += n;
    }
    return p;
}

// The recovery rules of §12 (confirmed: binary): only fighters and
// satellites; a ship or base needs the matching bay; free cargo space is the
// only limit; in a turn-based game a fighter group comes back only with its
// full movement. A named group (the window) gives up to `amount` of the
// design `what` takes, or of every design; otherwise each own group of the
// type in the sector, in object order, gives the units of every design as far
// as room allows, whatever the amount, and the next group comes only while
// the previous one gave at least one unit.
Plan recoverPlan(const Rules& r, const GameState& s, bool turnBased, Launcher into, const CargoWhat& what, int64_t amount, VehicleId named) {
    Plan p;
    const Holder h = holderOf(r, s, into);
    if (!h.cargo) return p;
    const Vehicle* only = named.valid() ? s.vehicle(named) : nullptr;
    if (named.valid() && (!only || !alive(*only))) return p;
    std::optional<VehicleType> type = what.type;
    if (!type && what.design.valid() && what.design.index() < s.designs.size()) type = r.hull(s.design(what.design).hull).type;
    if (!type && only) type = vehicleType(r, s, *only);
    if (!type || (*type != VehicleType::Fighter && *type != VehicleType::Satellite)) {
        warn(p.result, "Only fighters and satellites can be recovered.");
        return p;
    }
    if (!canRecover(h, launcherFor(*type))) {
        warn(p.result, std::format("{} has nothing that can recover {}.", h.name, unitWords(*type)));
        return p;
    }
    int64_t room = h.freeSpace;
    // Recover Units takes what room allows; only the window's named group has an amount.
    int64_t want = amount < 0 || !only ? std::numeric_limits<int64_t>::max() : amount;
    auto ofKindHere = [&](const Vehicle& g) {
        return alive(g) && g.id != into.vehicle && g.owner == h.owner && g.location == h.where && vehicleType(r, s, g) == *type;
    };
    // In turn-based games a fighter group comes back only with its full movement.
    auto recoverable = [&](const Vehicle& g) {
        if (!turnBased || *type != VehicleType::Fighter || g.movement >= turnMovement(r, s, g)) return true;
        warn(p.result, std::format("{} has moved this turn: fighters come back aboard only with their full movement.", g.name));
        return false;
    };
    // The units of one group, design by design, as far as room allows.
    auto take = [&](const Vehicle& g, bool everyDesign) {
        int64_t gave = 0;
        for (const UnitStack& st : groupStacks(g)) {
            if (want <= 0) break;
            if (!everyDesign && st.design != what.design) continue;
            const int64_t n = std::min({int64_t{st.count}, want, room / unitTons(r, s, st.design)});
            if (n <= 0) continue;
            p.steps.push_back(Step{st.design, n, g.id});
            room -= n * unitTons(r, s, st.design);
            want -= n;
            gave += n;
        }
        p.result.moved += gave;
        return gave;
    };
    if (only) {
        if (ofKindHere(*only) && recoverable(*only)) take(*only, !what.design.valid() || what.type.has_value());
        return p;
    }
    for (VehicleId id : vehiclesInObjectOrder(s)) {
        const Vehicle* g = s.vehicle(id);
        if (!g || !ofKindHere(*g)) continue;
        const int64_t gave = recoverable(*g) ? take(*g, true) : 0;
        if (gave <= 0) break;
    }
    return p;
}

} // namespace

UnitTransfer planLaunch(const Rules& r, const GameState& s, const UnitBudget& budget, Launcher from, const CargoWhat& what, int64_t amount) {
    return launchPlan(r, s, budget, from, what, amount).result;
}

UnitTransfer planRecover(const Rules& r, const GameState& s, bool turnBased, Launcher into, const CargoWhat& what, int64_t amount,
                         VehicleId group) {
    return recoverPlan(r, s, turnBased, into, what, amount, group).result;
}

UnitTransfer launchUnits(TurnContext& ctx, UnitBudget& budget, Launcher from, const CargoWhat& what, int64_t amount) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Plan plan = launchPlan(r, s, budget, from, what, amount);
    if (plan.steps.empty()) return plan.result;
    const Holder h = holderOf(r, s, from);
    const EmpireId owner = h.owner;
    const Location where = h.where;
    for (const Step& st : plan.steps) {
        const DesignId unit = st.design;
        const VehicleType type = r.hull(s.design(unit).hull).type;
        // Taken each time: a new group may move the vehicles in memory.
        Cargo& cargo = from.vehicle.valid() ? s.vehicle(from.vehicle)->cargo : s.colony(from.planet)->cargo;
        Cargo taken;
        const int64_t n = moveUnits(cargo, taken, unit, st.count);
        budget.launched[std::make_tuple(from.vehicle, from.planet, launcherFor(type))] += n;
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
            continue;
        }
        // The units join the last group of their kind and owner in the sector's
        // object order, whatever its designs, fleet, orders or cloak, and only that
        // group is refilled to its new maximum (§12, confirmed: binary).
        Vehicle* last = nullptr;
        for (VehicleId id : vehiclesInObjectOrder(s))
            if (Vehicle* g = s.vehicle(id); g && alive(*g) && g->owner == owner && g->location == where && vehicleType(r, s, *g) == type) last = g;
        if (!last) {
            newGroup(static_cast<int>(n));
            continue;
        }
        addGroupUnits(s, *last, unit, static_cast<int>(n));
        last->supply = initialSupply(r, s, *last);
    }
    return plan.result;
}

UnitTransfer recoverUnits(TurnContext& ctx, Launcher into, const CargoWhat& what, int64_t amount, VehicleId group) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Plan plan = recoverPlan(r, s, !s.options.simultaneous, into, what, amount, group);
    Cargo* cargo = into.vehicle.valid() ? (s.vehicle(into.vehicle) ? &s.vehicle(into.vehicle)->cargo : nullptr)
                                        : (s.colony(into.planet) ? &s.colony(into.planet)->cargo : nullptr);
    if (!cargo) return plan.result;
    // Units of one design leave the group into the cargo.
    for (const Step& st : plan.steps) {
        Vehicle* g = s.vehicle(st.group);
        if (!g) continue;
        const int n = removeGroupUnits(s, *g, st.design, static_cast<int>(std::min<int64_t>(st.count, std::numeric_limits<int>::max())));
        // (inferred) the supply left over stays with the group, up to what the rest can hold.
        if (g->count > 0) g->supply = std::min(g->supply, vehicleSupplyCapacity(r, s, *g));
        auto it = std::find_if(cargo->units.begin(), cargo->units.end(), [&](const UnitStack& u) { return u.design == st.design; });
        if (it == cargo->units.end()) cargo->units.push_back({st.design, n});
        else it->count += n;
    }
    return plan.result;
}

} // namespace detail

namespace {

// The turn's launch budget of a turn-based player's turn (GameState::playerTurn).
detail::UnitBudget turnBudget(const GameState& s) {
    detail::UnitBudget b;
    b.turnBased = !s.options.simultaneous;
    for (const TurnLaunches& l : s.playerTurn.launched) b.launched[{l.vehicle, l.planet, static_cast<AbilityKind>(l.kind)}] = l.count;
    return b;
}

detail::CargoWhat oneDesign(DesignId d) {
    detail::CargoWhat w;
    w.design = d;
    return w;
}

} // namespace

UnitTransfer checkLaunch(const Rules& r, const GameState& s, VehicleId vehicle, ObjectId planet, DesignId design, int64_t amount) {
    return detail::planLaunch(r, s, turnBudget(s), detail::Launcher{vehicle, vehicle.valid() ? ObjectId{} : planet}, oneDesign(design), amount);
}

UnitTransfer checkRecover(const Rules& r, const GameState& s, VehicleId vehicle, ObjectId planet, VehicleId group, DesignId design,
                          int64_t amount) {
    return detail::planRecover(r, s, !s.options.simultaneous, detail::Launcher{vehicle, vehicle.valid() ? ObjectId{} : planet}, oneDesign(design),
                               amount, group);
}

UnitTransfer launchNow(TurnContext& ctx, VehicleId vehicle, ObjectId planet, DesignId design, int64_t amount) {
    GameState& s = ctx.state;
    detail::UnitBudget budget = turnBudget(s);
    const UnitTransfer t =
        detail::launchUnits(ctx, budget, detail::Launcher{vehicle, vehicle.valid() ? ObjectId{} : planet}, oneDesign(design), amount);
    s.playerTurn.launched.clear();
    for (const auto& [key, n] : budget.launched) {
        const auto& [v, p, kind] = key;
        s.playerTurn.launched.push_back({v, p, static_cast<uint16_t>(kind), n});
    }
    return t;
}

LaunchCapacity launchCapacity(const Rules& r, const GameState& s, VehicleId vehicle, ObjectId planet) {
    LaunchCapacity out;
    const detail::Launcher l{vehicle, vehicle.valid() ? ObjectId{} : planet};
    const detail::UnitBudget budget = turnBudget(s);
    using ruleset::VehicleType;
    const std::array<VehicleType, 4> kinds{VehicleType::Fighter, VehicleType::Satellite, VehicleType::Mine, VehicleType::Drone};
    const detail::Holder h = detail::holderOf(r, s, l);
    for (size_t k = 0; k < kinds.size(); ++k) {
        const AbilityKind a = detail::launcherFor(kinds[k]);
        if (!h.cargo || (!h.planet && !hasAbility(h.abilities, a))) {
            out.rate[k] = -1;
            continue;
        }
        out.rate[k] = detail::perTurn(h, a);
        const auto it = budget.launched.find(std::make_tuple(l.vehicle, l.planet, a));
        out.left[k] = std::max<int64_t>(0, out.rate[k] - (it == budget.launched.end() ? 0 : it->second));
    }
    return out;
}

UnitTransfer recoverNow(TurnContext& ctx, VehicleId vehicle, ObjectId planet, VehicleId group, DesignId design, int64_t amount) {
    return detail::recoverUnits(ctx, detail::Launcher{vehicle, vehicle.valid() ? ObjectId{} : planet}, oneDesign(design), amount, group);
}

namespace detail {

// ---- One-shot components -----------------------------------------------------------------------------

int useComponent(TurnContext& ctx, VehicleId id, int entry) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle* v = s.vehicle(id);
    if (!v || !alive(*v)) return 0;
    const Design& d = s.design(v->design);
    if (entry < 0 || static_cast<size_t>(entry) >= d.entries.size()) return 0;
    const auto e = static_cast<size_t>(entry);
    const auto abilities = r.componentAbilities(d.entries[e].component);
    // A part destroyed on use goes first, even one with neither ability; no
    // supply is charged and nothing is logged (§8, confirmed: binary).
    if (hasAbility(abilities, AbilityKind::ComponentDestroyedOnUse)) {
        if (v->damage.size() < d.entries.size()) v->damage.resize(d.entries.size(), 0);
        v->damage[e] = entryStructure(r, d, e);
        fitToCapacity(r, s, *v);  // storage the part held goes with it (§7, §11)
    }
    const int64_t energy = abilitySum(abilities, AbilityKind::EmergencyEnergy);
    const int64_t resupply = abilitySum(abilities, AbilityKind::EmergencyResupply);
    // Emergency Resupply: V1 more supply, capped at the maximum; nothing for
    // unlimited supply or a mothballed vehicle.
    if (resupply > 0 && v->status != VehicleStatus::Mothballed && !vehicleHasUnlimitedSupply(r, s, *v)) {
        v->supply = std::max(v->supply, std::min(v->supply + resupply, vehicleSupplyCapacity(r, s, *v)));
        capMovement(r, s, *v);  // supply set: the maximum is recomputed (§6.1)
    }
    // Emergency Energy only for a vehicle whose maximum movement is above 0.
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

std::string detail::colonizerProblem(const Rules& r, const GameState& s, const Vehicle& v, const SpaceObject& planet) {
    AbilityKind k = AbilityKind::ColonizeRock;
    if (datafile::keysEqual(planet.surface, "Ice")) k = AbilityKind::ColonizeIce;
    else if (datafile::keysEqual(planet.surface, "Gas Giant") || datafile::keysEqual(planet.surface, "Gas")) k = AbilityKind::ColonizeGas;
    if (!hasAbility(vehicleAbilities(r, s, v), k)) return std::format("{} cannot colonize {} planets", v.name, planet.surface);
    const Race& race = s.empire(v.owner).race;
    if (s.options.onlyBreathable && !datafile::keysEqual(planet.atmosphere, race.atmosphere))
        return "Only planets with a breathable atmosphere may be colonized in this game";
    if (s.options.onlyHomeType && !datafile::keysEqual(planet.surface, race.nativeSurface))
        return "Only planets of the home planet type may be colonized in this game";
    return {};
}

std::string colonizeProblem(const Rules& r, const GameState& s, const Vehicle& v, ObjectId planet) {
    if (!planet.valid() || planet.index() >= s.galaxy.objects.size()) return "No such planet";
    if (!detail::inSystem(s.galaxy, planet)) return "That planet no longer exists";
    const SpaceObject& obj = s.galaxy.object(planet);
    if (obj.kind != ObjectKind::Planet) return "Only planets can be colonized";
    // "Seen" (spec 03 §8, §19 Q80, confirmed: binary): the detection rule of
    // spec 01 §6.3 for the owner, applied to the planet whether or not it has
    // a colony, with the sensors of the moment. A planet that a storm, a
    // nebula or a colony's cloak hides from them leaves no planet to colonize,
    // as far as they know.
    if (!sight::canSeeColony(r, s, v.owner, planet)) return "There is no planet here to colonize";
    if (s.colony(planet)) return "The planet is already colonized";
    return detail::colonizerProblem(r, s, v, obj);
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
        UnitStack& st = stacks[stacks.size() == 1 ? 0 : rng.index(stacks.size())];
        if (st.count <= 0) continue;
        const int64_t hp = std::max<int64_t>(1, combat::detail::unitHitPoints(r, s.design(st.design)));
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
    capMovement(r, s, v);
}

void capMovement(const Rules& r, const GameState& s, Vehicle& v) {
    // Only ships and bases: a unit group's recompute sets its maximum (the
    // slowest unit design's speed, or 1 at 0 supply) and leaves the movement
    // left alone (spec 03 §6.1, §19 Q85, confirmed: binary).
    if (alive(v) && detail::isShipOrBase(vehicleType(r, s, v))) v.movement = std::min(v.movement, std::max(0, detail::turnMovement(r, s, v)));
}

Vehicle& spawnVehicle(const Rules& r, GameState& s, EmpireId owner, DesignId design, Location where, int autoWaypoint) {
    Design& d = s.design(design);
    Vehicle v;
    v.owner = owner;
    v.design = design;
    v.name = nextVehicleName(s, d);
    ++d.built;
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
