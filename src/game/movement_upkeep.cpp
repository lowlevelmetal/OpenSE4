// Colonization at the end of the movement phase, and each empire's repair,
// supply and training steps (spec 05 §8): spec 02 §2, spec 03 §7, §8, §13,
// spec 01 §5.3 (ruins).

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/design.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/research.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <set>

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
    if (s.colonies.size() < s.galaxy.objects.size()) s.colonies.resize(s.galaxy.objects.size());

    Colony c;
    c.planet = planet;
    c.owner = owner;
    // Every empire's new colony gets the computer's pick (spec 05 §7.5 "at
    // colonization"; OpenSE4 never asks the player).
    c.colonyType = ai::colonyTypeAtColonization(r, s, owner, planet);
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

int starsIn(const GameState& s, SystemId sys) {
    int n = 0;
    for (ObjectId o : s.galaxy.system(sys).objects) n += s.galaxy.object(o).kind == ObjectKind::Star;
    return n;
}

// A training source: one own object's largest V1 and largest V2 of a training ability.
struct Training {
    int64_t perTurn = 0;
    int64_t cap = 0;
};

// Every own object in the system is a source for the sector-level ability
// when it is in `where`'s sector and for the system-wide one anywhere in the
// system: populated colonies first, then vehicles (inferred order).
std::vector<Training> trainingSources(const Rules& r, const GameState& s, EmpireId owner, Location where, AbilityKind sector,
                                      AbilityKind system) {
    std::vector<Training> out;
    auto add = [&](std::span<const ParsedAbility> list, bool here) {
        for (AbilityKind k : {sector, system}) {
            if (k == sector && !here) continue;
            if (!hasAbility(list, k)) continue;
            out.push_back({abilityLargest(list, k), abilityLargest(list, k, true)});
        }
    };
    for (ObjectId o : s.galaxy.system(where.system).objects) {
        const Colony* c = s.colony(o);
        if (!c || c->owner != owner || c->totalPopulation() <= 0) continue;  // (inferred) facilities need people
        add(colonyAbilities(r, s, *c), s.galaxy.object(o).sector == where.sector);
    }
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == owner && v.location.system == where.system)
            add(vehicleAbilities(r, s, v), v.location.sector == where.sector);
    return out;
}

// From each source in turn, a value below the cap gains the smaller of V1 and
// (cap − value); a cap of 0 gives nothing (spec 03 §3.3, confirmed: binary).
void train(int& experience, const std::vector<Training>& sources, int limit = std::numeric_limits<int>::max()) {
    for (const Training& t : sources) {
        if (t.perTurn <= 0 || t.cap <= 0 || experience >= t.cap) continue;
        experience = static_cast<int>(std::min<int64_t>(limit, experience + std::min(t.perTurn, t.cap - experience)));
    }
}

} // namespace

// Repair (spec 03 §13, confirmed: binary): each (empire, sector) pool is
// shared by the empire's damaged ships, bases and unit groups there in order
// of creation; each point restores one component, by the empire's repair
// priorities, then in design order.
void repairEmpire(TurnContext& ctx, EmpireId e) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const Empire& emp = s.empire(e);
    std::map<Location, std::vector<VehicleId>> damaged;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == e && std::any_of(v.damage.begin(), v.damage.end(), [](int d) { return d > 0; }))
            damaged[v.location].push_back(v.id);
    for (const auto& [where, ids] : damaged) {
        int points = repairCapacityAt(r, s, e, where);
        const bool yard = spaceYardAt(r, s, e, where);
        for (VehicleId id : ids) {
            if (points <= 0) break;
            Vehicle& v = *s.vehicle(id);
            const Design& d = s.design(v.design);
            // Parts of unknown technology never; emergency parts only with an own yard here.
            auto repairable = [&](size_t i) {
                if (i >= v.damage.size() || v.damage[i] <= 0) return false;
                if (!r.componentAvailable(emp, d.entries[i].component)) return false;
                const auto ab = r.componentAbilities(d.entries[i].component);
                return yard || (!hasAbility(ab, AbilityKind::EmergencyEnergy) && !hasAbility(ab, AbilityKind::EmergencyResupply));
            };
            std::vector<size_t> order;
            for (size_t i = 0; i < d.entries.size(); ++i)
                if (repairable(i)) order.push_back(i);
            // The priority groups in list order, then everything else, each in design order.
            std::stable_sort(order.begin(), order.end(),
                             [&](size_t a, size_t b) { return repairRank(r, emp, d.entries[a]) < repairRank(r, emp, d.entries[b]); });
            for (size_t i : order) {
                if (points <= 0) break;
                v.damage[i] = 0;  // the engine keeps partial damage; a damaged part costs one point like a destroyed one
                --points;
            }
        }
    }
}

// Supply at the end of the turn (spec 03 §7, §12). The order of the steps
// within it is (inferred): upkeep and cloaks, depots, fleet pooling, solar
// collectors, then limits.
void supplyEmpire(TurnContext& ctx, EmpireId e) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const int64_t fighterUse = r.setting("Fighter Supply Usage Per Turn", 5);
    const int64_t droneUse = r.setting("Drone Supply Usage Per Turn", 200);
    // Units pay every turn, without racial scaling: fighters count × the
    // setting (or the cloak parts per unit while cloaked), drones the setting
    // plus the cloak parts. Cloaked ships and bases pay their cloak parts.
    for (Vehicle& v : s.vehicles) {
        if (!alive(v) || v.owner != e || !vehicleUsesSupply(r, s, v)) continue;
        const VehicleType t = vehicleType(r, s, v);
        const bool cloaked = v.status == VehicleStatus::Cloaked;
        const int64_t cloak = cloaked ? cloakSupply(r, s, v) : 0;
        int64_t cost = cloak;
        if (t == VehicleType::Fighter) cost = int64_t{std::max(1, v.count)} * (cloaked ? cloak : fighterUse);
        else if (t == VehicleType::Drone) cost = int64_t{std::max(1, v.count)} * (droneUse + cloak);
        spendSupply(r, s, v, cost);
    }
    for (Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == e && resupplyDepotAt(r, s, e, v.location)) refillSupply(r, s, v);
    // Fleet pooling among the members in the fleet's sector, at the end of the turn only.
    for (const Fleet& f : s.fleets) {
        if (f.owner != e) continue;
        const Vehicle* lead = fleetLeader(s, f);
        if (!lead) continue;
        std::vector<VehicleId> together;
        for (VehicleId id : f.members)
            if (const Vehicle* v = s.vehicle(id); v && alive(*v) && v->location == lead->location) together.push_back(id);
        poolSupply(r, s, together);
    }
    // Solar collectors: V1 per star in the system, capped at the maximum.
    for (Vehicle& v : s.vehicles) {
        if (!alive(v) || v.owner != e || vehicleHasUnlimitedSupply(r, s, v) || !vehicleUsesSupply(r, s, v)) continue;
        const int64_t solar = abilitySum(vehicleAbilities(r, s, v), AbilityKind::SolarSupplyGeneration) *
                              (isShipOrBase(vehicleType(r, s, v)) ? 1 : std::max(1, v.count));  // a group adds every unit's
        const int64_t capacity = vehicleSupplyCapacity(r, s, v);
        if (solar > 0 && v.supply < capacity) v.supply = std::min(capacity, v.supply + solar * starsIn(s, v.location.system));
    }
    for (Vehicle& v : s.vehicles) {
        if (!alive(v) || v.owner != e) continue;
        // Capacity lost to damage takes supply and cargo with it.
        if (vehicleUsesSupply(r, s, v)) holdSupply(r, s, v);
        trimCargo(r, s, v);
        const VehicleType t = vehicleType(r, s, v);
        // Drones at 0 are lost; fighters are not, they drop to 1 MP (§12, confirmed: binary).
        if (t == VehicleType::Drone && v.supply <= 0) {
            vehicleLost(ctx, v, "Ran out of supplies.");
            continue;
        }
        // A cloak drops at 0 supply or when it can no longer work (§8).
        if (v.status == VehicleStatus::Cloaked &&
            ((v.supply <= 0 && !vehicleHasUnlimitedSupply(r, s, v)) || !canCloak(r, s, v))) {
            v.status = VehicleStatus::Normal;
            ctx.log(e, LogCategory::Misc, std::format("{} decloaked", v.name), "Its cloak could no longer be kept up.", v.location);
        }
    }
}

// Ship and fleet training (spec 03 §3.3): every own object is a source.
void trainEmpire(TurnContext& ctx, EmpireId e) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    std::vector<std::pair<VehicleId, std::vector<Training>>> ships;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == e && v.status != VehicleStatus::Mothballed && isShipOrBase(vehicleType(r, s, v)))
            ships.emplace_back(v.id, trainingSources(r, s, e, v.location, AbilityKind::ShipTraining, AbilityKind::ShipTrainingSystem));
    for (auto& [id, sources] : ships) train(s.vehicle(id)->experience, sources);
    // Fleet experience is capped at 50 (spec 03 §9).
    for (Fleet& f : s.fleets)
        if (f.owner == e)
            if (const Vehicle* lead = fleetLeader(s, f))
                train(f.experience, trainingSources(r, s, e, lead->location, AbilityKind::FleetTraining, AbilityKind::FleetTrainingSystem), 50);
}

// An obsolete design goes once no vehicle or unit of it exists, no queue of
// its owner holds it and no other living empire knows it (spec 03 §4.1;
// inferred: a design an empire still lists among its seen designs counts as
// seen within 50 turns).
void purgeObsoleteDesigns(TurnContext& ctx) {
    GameState& s = ctx.state;
    std::set<DesignId> inUse;
    auto cargo = [&](const Cargo& c) {
        for (const UnitStack& u : c.units) inUse.insert(u.design);
    };
    auto queue = [&](const ConstructionQueue& q) {
        for (const QueueItem& item : q.items)
            if (item.kind == QueueItem::Kind::Vehicle) inUse.insert(item.design);
    };
    for (const Vehicle& v : s.vehicles) {
        if (alive(v)) inUse.insert(v.design);
        cargo(v.cargo);
        queue(v.queue);
    }
    for (const auto& c : s.colonies)
        if (c) {
            cargo(c->cargo);
            queue(c->queue);
        }
    for (Empire& owner : s.empires)
        std::erase_if(owner.designs, [&](DesignId id) {
            if (id.index() >= s.designs.size() || !s.design(id).obsolete || inUse.contains(id)) return false;
            for (const Empire& other : s.empires)
                if (other.id != owner.id && other.alive &&
                    std::binary_search(other.knowledge.seenDesigns.begin(), other.knowledge.seenDesigns.end(), id))
                    return false;
            return true;
        });
}

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
        // The colonizer is the last suitable member in group order; a cloaked member stops it (§8, confirmed: binary).
        VehicleId colonizer;
        std::string why;
        for (VehicleId id : f.members) {
            const Vehicle* v = s.vehicle(id);
            if (!v || !alive(*v) || !followsFleetOrders(s, *v)) continue;
            if (v->status == VehicleStatus::Cloaked) {
                why = "A cloaked ship cannot colonize.";
                colonizer = {};
                break;
            }
            const std::string p = colonizeProblem(r, s, *v, o.object);
            if (p.empty()) colonizer = id;
            else if (why.empty()) why = p;
        }
        if (colonizer.valid()) {
            colonize(ctx, colonizer, o.object);
            popFront(s.fleets[fi].orders, s.fleets[fi].repeatOrders);
        } else {
            // A failed order clears the list and switches Repeat off (§8).
            ctx.log(f.owner, LogCategory::Misc, std::format("{}: colonization failed", f.name), why, lead->location);
            s.fleets[fi].orders.clear();
            s.fleets[fi].repeatOrders = false;
        }
    }
    // Single ships, in id order (the first to arrive at a planet wins).
    for (size_t i = 0; i < s.vehicles.size(); ++i) {
        Vehicle& v = s.vehicles[i];
        if (!alive(v) || v.orders.empty() || followsFleetOrders(s, v)) continue;
        const Order o = v.orders.front();
        if (!colonizeAt(s, o, v.location)) continue;
        std::string why = colonizeProblem(r, s, v, o.object);
        if (why.empty() && v.status == VehicleStatus::Cloaked) why = "A cloaked ship cannot colonize.";
        if (!why.empty()) {
            // When two ships target the same planet the first processed wins and the other's order fails (§8).
            ctx.log(v.owner, LogCategory::Misc, std::format("{}: colonization failed", v.name), why, v.location);
            v.orders.clear();
            v.repeatOrders = false;
            continue;
        }
        colonize(ctx, v.id, o.object);
    }
    s.removeDeadVehicles();
}

} // namespace opense4::game::movement
