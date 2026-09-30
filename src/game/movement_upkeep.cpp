// Turn phase 4 (colonization) and phase 10 (repair, training, supply):
// spec 02 §2, spec 03 §7, §8, §13, spec 01 §5.3 (ruins).

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/research.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::movement {

using namespace detail;

namespace {

using ruleset::VehicleType;

void grantRuins(TurnContext& ctx, EmpireId owner, ObjectId planet) {
    GameState& s = ctx.state;
    SpaceObject& obj = s.galaxy.object(planet);
    // Val 1 random advances of one level each (spec 01 §5.3).
    const int64_t advances = rawSum(obj.abilities, AbilityKind::AncientRuins);
    if (advances > 0) research::grantRandomAdvances(ctx, owner, static_cast<int>(advances), s.rng, "ruins");
    for (const auto& a : obj.abilities) {
        if (parseAbilityKind(a.type) != AbilityKind::AncientRuinsUnique) continue;
        auto& unlocked = s.empire(owner).uniqueAreasUnlocked;
        const int area = static_cast<int>(a.number1());
        if (std::find(unlocked.begin(), unlocked.end(), area) == unlocked.end()) unlocked.push_back(area);
    }
    const bool unique = std::any_of(obj.abilities.begin(), obj.abilities.end(),
                                    [](const auto& a) { return parseAbilityKind(a.type) == AbilityKind::AncientRuinsUnique; });
    if (advances > 0 || unique) ctx.log(owner, LogCategory::Research, std::format("Ancient ruins found on {}", obj.name), {}, locationOf(s.galaxy, planet));
    // The ruins are used up (inferred).
    std::erase_if(obj.abilities, [](const ruleset::Ability& a) {
        const auto k = parseAbilityKind(a.type);
        return k == AbilityKind::AncientRuins || k == AbilityKind::AncientRuinsUnique;
    });
}

// The ship is broken up; its population and cargo land on the new colony.
void colonize(TurnContext& ctx, VehicleId id, ObjectId planet) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Vehicle& v = *s.vehicle(id);
    const EmpireId owner = v.owner;
    const Empire& emp = s.empire(owner);
    if (s.colonies.size() < s.galaxy.objects.size()) s.colonies.resize(s.galaxy.objects.size());

    Colony c;
    c.planet = planet;
    c.owner = owner;
    c.colonyType = emp.colonyTypes.empty() ? "Balanced" : emp.colonyTypes.front();  // (inferred)
    c.foundedTurn = s.turn;
    c.population = v.cargo.population;
    const int64_t bonus = r.setting("Automatic Colonization Population", 0);
    if (bonus > 0) {
        auto it = std::find_if(c.population.begin(), c.population.end(), [&](const PopulationGroup& p) { return p.race == owner; });
        if (it == c.population.end()) c.population.push_back({owner, bonus});
        else it->millions += bonus;
    }
    c.cargo.units = v.cargo.units;
    s.colonies[planet.index()] = std::move(c);
    Colony& col = *s.colonies[planet.index()];
    // More than the planet holds is lost (inferred).
    int64_t over = col.totalPopulation() - maxPopulation(r, s, col);
    for (auto it = col.population.rbegin(); over > 0 && it != col.population.rend(); ++it) {
        const int64_t n = std::min(over, it->millions);
        it->millions -= n;
        over -= n;
    }
    std::erase_if(col.population, [](const PopulationGroup& p) { return p.millions <= 0; });
    while (!col.cargo.units.empty() && cargoSpaceUsed(r, s, col.cargo) > colonyCargoCapacity(r, s, col)) {
        UnitStack& u = col.cargo.units.back();
        if (--u.count <= 0) col.cargo.units.pop_back();
    }

    const SystemId sys = s.galaxy.object(planet).system;
    ctx.log(owner, LogCategory::Misc, std::format("{} colonized", s.galaxy.object(planet).name), std::format("{} founded the colony.", v.name),
            locationOf(s.galaxy, planet));
    ctx.mood(owner, "Any Planet Colonized", sys, planet);
    s.vehicle(id)->count = 0;  // the colony ship is consumed
    grantRuins(ctx, owner, planet);
}

bool colonizeAt(const GameState& s, const Order& o, Location where) {
    return o.kind == OrderKind::Colonize && o.object.valid() && o.object.index() < s.galaxy.objects.size() &&
           locationOf(s.galaxy, o.object) == where;
}

void popFront(std::vector<Order>& list, bool repeat) {
    if (list.empty()) return;
    Order done = list.front();
    list.erase(list.begin());
    if (repeat) {
        done.amount = 0;
        list.push_back(done);
    }
}

// ---- Upkeep ---------------------------------------------------------------------------------------

size_t repairRank(const Rules& r, const Empire& e, const DesignEntry& entry) {
    const std::string& group = r.component(entry.component).generalGroup;
    for (size_t i = 0; i < e.repairPriorities.size(); ++i)
        if (datafile::keysEqual(e.repairPriorities[i], group)) return i;
    return e.repairPriorities.size();
}

// Repair (spec 03 §13): a pool per (empire, sector) shared by the own damaged
// vehicles there in id order (the spec's recommendation), free of charge
// (inferred); components by the empire's repair priorities.
void repair(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    std::map<std::pair<EmpireId, Location>, std::vector<VehicleId>> damaged;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && std::any_of(v.damage.begin(), v.damage.end(), [](int d) { return d > 0; })) damaged[{v.owner, v.location}].push_back(v.id);
    for (const auto& [key, ids] : damaged) {
        int capacity = repairCapacityAt(r, s, key.first, key.second);
        const Empire& e = s.empire(key.first);
        for (VehicleId id : ids) {
            if (capacity <= 0) break;
            Vehicle& v = *s.vehicle(id);
            const Design& d = s.design(v.design);
            std::vector<size_t> order;
            for (size_t i = 0; i < v.damage.size() && i < d.entries.size(); ++i)
                if (v.damage[i] > 0) order.push_back(i);
            std::stable_sort(order.begin(), order.end(),
                             [&](size_t a, size_t b) { return repairRank(r, e, d.entries[a]) < repairRank(r, e, d.entries[b]); });
            for (size_t i : order) {
                if (capacity <= 0) break;
                v.damage[i] = 0;
                --capacity;
            }
        }
    }
}

struct Training {
    int64_t perTurn = 0;
    int64_t cap = 0;
};

// The best training facility of `owner` for this sector (only one counts, spec 03 §3.2).
Training trainingAt(const Rules& r, const GameState& s, EmpireId owner, Location where, AbilityKind sector, AbilityKind system) {
    Training best;
    for (ObjectId o : s.galaxy.system(where.system).objects) {
        const Colony* c = s.colony(o);
        if (!c || c->owner != owner || c->totalPopulation() <= 0) continue;
        const bool here = s.galaxy.object(o).sector == where.sector;
        for (const ParsedAbility& a : colonyAbilities(r, s, *c)) {
            if (!(a.kind == system || (here && a.kind == sector))) continue;
            if (a.value1 > best.perTurn || (a.value1 == best.perTurn && a.value2 > best.cap)) best = {a.value1, a.value2};
        }
    }
    return best;
}

void train(int& experience, const Training& t) {
    if (t.perTurn <= 0) return;
    if (t.cap <= 0) {  // no cap given (inferred)
        experience += static_cast<int>(t.perTurn);
        return;
    }
    if (experience < t.cap) experience = static_cast<int>(std::min<int64_t>(t.cap, experience + t.perTurn));
}

void training(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    for (Vehicle& v : s.vehicles)
        if (alive(v) && isShipOrBase(vehicleType(r, s, v)))
            train(v.experience, trainingAt(r, s, v.owner, v.location, AbilityKind::ShipTraining, AbilityKind::ShipTrainingSystem));
    for (Fleet& f : s.fleets)
        if (const Vehicle* lead = fleetLeader(s, f))
            train(f.experience, trainingAt(r, s, f.owner, lead->location, AbilityKind::FleetTraining, AbilityKind::FleetTrainingSystem));
}

int starsIn(const GameState& s, SystemId sys) {
    int n = 0;
    for (ObjectId o : s.galaxy.system(sys).objects) n += s.galaxy.object(o).kind == ObjectKind::Star;
    return n;
}

void supply(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const int64_t fighterUse = r.setting("Fighter Supply Usage Per Turn", 5);
    const int64_t droneUse = r.setting("Drone Supply Usage Per Turn", 200);
    // Units in space use supply every turn, even when idle.
    for (Vehicle& v : s.vehicles) {
        if (!alive(v)) continue;
        const VehicleType t = vehicleType(r, s, v);
        if (t == VehicleType::Fighter) spendSupply(r, s, v, scaledSupply(r, s, v.owner, fighterUse));
        if (t == VehicleType::Drone) spendSupply(r, s, v, scaledSupply(r, s, v.owner, droneUse));
    }
    // Depots, solar collectors, quantum reactors.
    for (Vehicle& v : s.vehicles) {
        if (!alive(v)) continue;
        const int64_t capacity = vehicleSupplyCapacity(r, s, v);
        if (vehicleHasQuantumReactor(r, s, v)) {
            v.supply = std::max(v.supply, capacity);
            continue;
        }
        if (resupplyDepotAt(r, s, v.owner, v.location)) {
            refillSupply(r, s, v);
            continue;
        }
        const int64_t solar = sumValue1(vehicleAbilities(r, s, v), AbilityKind::SolarSupplyGeneration);
        if (solar > 0 && v.supply < capacity) v.supply = std::min(capacity, v.supply + solar * starsIn(s, v.location.system));
    }
    // Fighters and drones without supply are lost.
    for (Vehicle& v : s.vehicles) {
        if (!alive(v) || v.supply > 0) continue;
        const VehicleType t = vehicleType(r, s, v);
        if (t == VehicleType::Fighter || t == VehicleType::Drone) vehicleLost(ctx, v, "Ran out of supplies.");
    }
    // Fleet pooling among the members with the leader.
    for (const Fleet& f : s.fleets) {
        const Vehicle* lead = fleetLeader(s, f);
        if (!lead) continue;
        std::vector<VehicleId> together;
        for (VehicleId id : f.members)
            if (const Vehicle* v = s.vehicle(id); v && alive(*v) && v->location == lead->location) together.push_back(id);
        poolSupply(r, s, together);
    }
    // Capacity lost to damage takes supply and cargo with it.
    for (Vehicle& v : s.vehicles) {
        if (!alive(v)) continue;
        v.supply = std::min(v.supply, vehicleSupplyCapacity(r, s, v));
        trimCargo(r, s, v);
    }
}

} // namespace

void runColonization(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    // Fleets: the first member able to colonize founds the colony.
    for (size_t fi = 0; fi < s.fleets.size(); ++fi) {
        Fleet& f = s.fleets[fi];
        if (f.orders.empty() || f.orders.front().kind != OrderKind::Colonize) continue;
        const Vehicle* lead = fleetLeader(s, f);
        const Order o = f.orders.front();
        if (!lead || !colonizeAt(s, o, lead->location)) continue;
        VehicleId colonizer;
        std::string why;
        for (VehicleId id : f.members) {
            const Vehicle* v = s.vehicle(id);
            if (!v || !alive(*v) || !followsFleetOrders(s, *v)) continue;
            const std::string p = colonizeProblem(r, s, *v, o.object);
            if (p.empty()) {
                colonizer = id;
                break;
            }
            if (why.empty()) why = p;
        }
        if (colonizer.valid()) colonize(ctx, colonizer, o.object);
        else ctx.log(f.owner, LogCategory::Misc, std::format("{}: colonization failed", f.name), why, lead->location);
        popFront(s.fleets[fi].orders, s.fleets[fi].repeatOrders);
    }
    // Single ships, in id order (the first to arrive at a planet wins).
    for (size_t i = 0; i < s.vehicles.size(); ++i) {
        Vehicle& v = s.vehicles[i];
        if (!alive(v) || v.orders.empty() || followsFleetOrders(s, v)) continue;
        const Order o = v.orders.front();
        if (!colonizeAt(s, o, v.location)) continue;
        if (const std::string why = colonizeProblem(r, s, v, o.object); !why.empty()) {
            ctx.log(v.owner, LogCategory::Misc, std::format("{}: colonization failed", v.name), why, v.location);
            popFront(v.orders, v.repeatOrders);
            continue;
        }
        colonize(ctx, v.id, o.object);
    }
    s.removeDeadVehicles();
}

void runUpkeep(TurnContext& ctx) {
    repair(ctx);
    training(ctx);
    supply(ctx);
}

} // namespace opense4::game::movement
