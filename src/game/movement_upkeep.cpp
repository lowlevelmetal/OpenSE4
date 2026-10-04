// Colonization (founding a colony; runColonization for colony ships left
// waiting at their planet), and each empire's repair,
// supply and training steps (spec 05 §8): spec 02 §2, spec 03 §7, §8, §13,
// spec 01 §5.3 (ruins).

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/movement_internal.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <set>

namespace opense4::game::movement {

using namespace detail;

namespace {

using ruleset::VehicleType;

// Ancient ruins on a newly colonized planet (spec 03 §3.3, confirmed:
// binary). Plain ruins first: N = the planet's largest `Ancient Ruins` V1
// random advances, then that ability is used up and `Ancient Ruins Unique` is
// neither applied nor removed. Without plain ruins, `Ancient Ruins Unique`
// adds its unique area (the largest V1) to the empire's and raises every
// research area of that unique id below its maximum by one level
// (requirements are not checked); then it is used up.
void grantRuins(TurnContext& ctx, EmpireId owner, ObjectId planet) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    SpaceObject& obj = s.galaxy.object(planet);
    auto remove = [&](AbilityKind k) {
        std::erase_if(obj.abilities, [&](const ruleset::Ability& a) { return parseAbilityKind(a.type) == k; });
    };
    auto announce = [&]() {
        // Goto shows the planet, like the original's ruins entry (spec 06 §7 Q41).
        logGoto(ctx.log(owner, LogCategory::Research, std::format("Ancient ruins found on {}", obj.name), {}, locationOf(s.galaxy, planet)),
                LogGoto::Location);
        addHistory(s, owner, owner, std::format("Found ancient ruins on {}", obj.name), locationOf(s.galaxy, planet));
    };
    const int64_t advances = rawBest(obj.abilities, AbilityKind::AncientRuins);
    if (advances > 0) {
        announce();
        research::grantRandomAdvances(ctx, owner, static_cast<int>(std::min<int64_t>(advances, 1000)), s.rng, "ruins");
        remove(AbilityKind::AncientRuins);
        return;
    }
    const bool unique = std::any_of(obj.abilities.begin(), obj.abilities.end(),
                                    [](const auto& a) { return parseAbilityKind(a.type) == AbilityKind::AncientRuinsUnique; });
    if (!unique) return;
    const int area = static_cast<int>(rawBest(obj.abilities, AbilityKind::AncientRuinsUnique));
    announce();
    if (area > 0) {
        auto& unlocked = s.empire(owner).uniqueAreasUnlocked;
        if (std::find(unlocked.begin(), unlocked.end(), area) == unlocked.end()) unlocked.push_back(area);
        const auto& areas = r.data().techAreas;
        for (size_t i = 0; i < areas.size(); ++i) {
            if (areas[i].uniqueArea != area) continue;
            const ruleset::TechAreaId id{static_cast<uint32_t>(i)};
            const int level = s.empire(owner).techLevel(id);
            if (level < areas[i].maxLevel) research::grantLevel(ctx, owner, id, level + 1, "ruins");
        }
    }
    remove(AbilityKind::AncientRuinsUnique);
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
    // The colony type is chosen as a computer player chooses it (spec 05 §7.5
    // "at colonization"), unless a human player colonizes in a turn-based game
    // with the empire's option on: then the player picks it in a dialog
    // (Empire::colonyTypeChoices), and this pick stands until then (spec 03 §8,
    // confirmed: binary).
    c.colonyType = ai::colonyTypeAtColonization(r, s, owner, planet);
    if (Empire& e = s.empire(owner); !s.options.simultaneous && e.kind == PlayerKind::Human && e.chooseColonyType &&
                                     std::find(e.colonyTypeChoices.begin(), e.colonyTypeChoices.end(), planet) == e.colonyTypeChoices.end())
        e.colonyTypeChoices.push_back(planet);
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
    sight::recalculateColony(r, col);  // a new colony's cloak and sensor levels (spec 01 §6.9)
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
    addHistory(s, owner, owner, std::format("Colonized {}", s.galaxy.object(planet).name), locationOf(s.galaxy, planet));
    ctx.mood(owner, "Any Planet Colonized", sys, planet);
    // Founding a colony claims nothing: the Empire Options' "claim every
    // system we colonize" switch is stored and shown but never read; the
    // Politics minister's territory pass makes the claims (spec 06 §7 Q47,
    // confirmed: binary).
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
    for (ObjectId o : s.galaxy.system(sys).objects) n += isStarKind(s.galaxy.object(o).kind);  // destroyed stars count (spec 01 §5.4)
    return n;
}

// A training source: the largest V1 and largest V2 of a training ability
// (each taken on its own, spec 03 §3.2).
struct Training {
    int64_t perTurn = 0;
    int64_t cap = 0;
};

Training trainingOf(std::span<const ParsedAbility> list, AbilityKind k) {
    return {abilityLargest(list, k), abilityLargest(list, k, true)};
}

// Experience in tenths of a point (Vehicle/Fleet::experience + tenths).
int64_t tenthsOf(int whole, int tenths) { return int64_t{whole} * 10 + tenths; }
void setTenths(int& whole, int& tenths, int64_t value) {
    whole = static_cast<int>(value / 10);
    tenths = static_cast<int>(value % 10);
}

// One source's gain (spec 03 §3.3, §9, confirmed: binary). Below the cap V2 the
// value gains V1; when V1 would pass V2 it gains V2 − its experience (a ship:
// it lands on V2) or V2 − truncate(its experience) (a fleet, and the
// system-wide abilities). A V1 or V2 of 0 trains nobody. At most 50.
void train(int& whole, int& tenths, Training t, bool truncatedCap) {
    if (t.perTurn <= 0 || t.cap <= 0) return;
    const int64_t value = tenthsOf(whole, tenths);
    const int64_t cap = t.cap * 10;
    if (value >= cap) return;
    int64_t gain = t.perTurn * 10;
    if (value + gain > cap) gain = truncatedCap ? cap - (value / 10) * 10 : cap - value;
    setTenths(whole, tenths, std::min<int64_t>(value + gain, int64_t{combat::kMaxCombatExperience} * 10));
}

// Solar collectors on one vehicle: V1 per star in its system, capped at the
// maximum (spec 03 §7, confirmed: binary); a group adds every unit's.
void collectSolar(const Rules& r, const GameState& s, Vehicle& v) {
    if (!alive(v) || vehicleHasUnlimitedSupply(r, s, v) || !vehicleUsesSupply(r, s, v)) return;
    const int64_t solar = vehicleAbilityTotal(r, s, v, AbilityKind::SolarSupplyGeneration);
    const int64_t capacity = vehicleSupplyCapacity(r, s, v);
    if (solar > 0 && v.supply < capacity) v.supply = std::min(capacity, v.supply + solar * starsIn(s, v.location.system));
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

// Supply at the end of the turn (spec 03 §7, §12, confirmed: binary):
// 1. upkeep for every own object in object order (cloaked ships and bases
//    their cloak parts, fighter groups per unit or their cloak parts while
//    cloaked, drone groups their upkeep plus cloak); one that reaches 0
//    decloaks at once;
// 2. the depot check for every object;
// 3. fleet pooling;
// 4. drone groups at 0 supply are destroyed.
// Solar collectors come later, in the training step.
void supplyEmpire(TurnContext& ctx, EmpireId e) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const int64_t fighterUse = r.setting("Fighter Supply Usage Per Turn", 5);
    const int64_t droneUse = r.setting("Drone Supply Usage Per Turn", 200);
    auto decloak = [&](Vehicle& v) {
        v.status = VehicleStatus::Normal;
        ctx.log(e, LogCategory::Misc, std::format("{} decloaked", v.name), "Its cloak could no longer be kept up.", v.location);
        // Any decloak runs the first-contact check in its system (spec 05 §3.1).
        diplomacy::firstContactIn(ctx, v.location.system);
    };
    // Units pay every turn, without racial scaling.
    for (VehicleId id : vehiclesInObjectOrder(s)) {
        Vehicle& v = *s.vehicle(id);
        if (!alive(v) || v.owner != e || !vehicleUsesSupply(r, s, v)) continue;
        const VehicleType t = vehicleType(r, s, v);
        const bool cloaked = v.status == VehicleStatus::Cloaked;
        const int64_t cloak = cloaked ? cloakSupply(r, s, v) : 0;
        int64_t cost = cloak;
        if (t == VehicleType::Fighter) {
            cost = int64_t{std::max(1, v.count)} * fighterUse;
            if (cloaked) {
                // Each unit pays its own design's cloak parts instead.
                cost = 0;
                for (const UnitStack& st : groupStacks(v)) cost += int64_t{std::max(0, st.count)} * cloakSupply(r, s, stackProbe(s, v, st));
            }
        } else if (t == VehicleType::Drone) {
            cost = int64_t{std::max(1, v.count)} * (droneUse + cloak);
        }
        spendSupply(r, s, v, cost);
        // A cloak drops at 0 supply, before any depot, or when it can no longer work (§8).
        if (v.status == VehicleStatus::Cloaked && ((v.supply <= 0 && !vehicleHasUnlimitedSupply(r, s, v)) || !canCloak(r, s, v))) decloak(v);
    }
    for (Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == e && v.status == VehicleStatus::Cloaked && !canCloak(r, s, v)) decloak(v);
    for (Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == e && resupplyDepotAt(r, s, e, v.location)) refillSupply(r, s, v);
    // Fleet pooling among the members at the fleet's location, at the end of the turn only.
    for (const Fleet& f : s.fleets)
        if (f.owner == e) poolSupply(r, s, fleetMembersAt(s, f));
    for (Vehicle& v : s.vehicles) {
        if (!alive(v) || v.owner != e) continue;
        // Capacity lost to damage takes supply with it. Cargo is cut when the
        // part is destroyed, at once (fitToCapacity; spec 03 §19 Q57, spec 04
        // §9.4: in combat, by mines and hazards), never here.
        if (vehicleUsesSupply(r, s, v)) holdSupply(r, s, v);
        // Drones at 0 are lost; fighters are not, they drop to 1 MP (§12, confirmed: binary).
        if (vehicleType(r, s, v) == VehicleType::Drone && v.supply <= 0) vehicleLost(ctx, v, "Ran out of supplies.");
    }
}

// The training step (spec 03 §3.3, §7, §9, confirmed: binary): every own
// object, in object order, is a source for its own sector (a colonized planet
// through its facilities, no population needed; a ship, base or unit group
// through its abilities), and its solar collectors act; then the system-wide
// abilities give one source per explored system.
void trainEmpire(TurnContext& ctx, EmpireId e) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    auto trainSector = [&](Location where, std::span<const ParsedAbility> list) {
        const Training ship = trainingOf(list, AbilityKind::ShipTraining);
        if (ship.perTurn > 0)
            for (Vehicle& v : s.vehicles)
                if (alive(v) && v.owner == e && v.location == where && v.status != VehicleStatus::Mothballed && isShipOrBase(vehicleType(r, s, v)))
                    train(v.experience, v.experienceTenths, ship, false);
        const Training fleet = trainingOf(list, AbilityKind::FleetTraining);
        if (fleet.perTurn > 0)
            for (Fleet& f : s.fleets)
                if (f.owner == e && f.location == where && !fleetMembersAt(s, f).empty()) train(f.experience, f.experienceTenths, fleet, true);
    };
    // Sector sources, in object order: colonized planets and vehicles share
    // the object list (spec 03 §19 Q44, Q62).
    for (const ObjectRef& ref : objectOrder(s)) {
        if (ref.object.valid()) {
            if (const Colony* c = s.colony(ref.object); c && c->owner == e) trainSector(locationOf(s.galaxy, ref.object), colonyAbilities(r, s, *c));
            continue;
        }
        Vehicle* v = s.vehicle(ref.vehicle);
        if (!v || !alive(*v) || v->owner != e) continue;
        trainSector(v->location, vehicleAbilities(r, s, *v));
        collectSolar(r, s, *v);
    }
    // System-wide sources: per explored system, the largest V1 and V2 over all
    // the empire's objects there.
    const Empire& emp = s.empire(e);
    for (const StarSystem& sys : s.galaxy.systems) {
        if (!emp.hasExplored(sys.id) && !s.options.omnipresent) continue;
        Training ship, fleet;
        auto take = [&](std::span<const ParsedAbility> list) {
            const Training st = trainingOf(list, AbilityKind::ShipTrainingSystem), ft = trainingOf(list, AbilityKind::FleetTrainingSystem);
            ship = {std::max(ship.perTurn, st.perTurn), std::max(ship.cap, st.cap)};
            fleet = {std::max(fleet.perTurn, ft.perTurn), std::max(fleet.cap, ft.cap)};
        };
        for (ObjectId o : sys.objects)
            if (const Colony* c = s.colony(o); c && c->owner == e) take(colonyAbilities(r, s, *c));
        for (const Vehicle& v : s.vehicles)
            if (alive(v) && v.owner == e && v.location.system == sys.id) take(vehicleAbilities(r, s, v));
        if (ship.perTurn > 0)
            for (Vehicle& v : s.vehicles)
                if (alive(v) && v.owner == e && v.location.system == sys.id && v.status != VehicleStatus::Mothballed &&
                    isShipOrBase(vehicleType(r, s, v)))
                    train(v.experience, v.experienceTenths, ship, true);
        if (fleet.perTurn > 0)
            for (Fleet& f : s.fleets)
                if (f.owner == e && f.location.system == sys.id && !fleetMembersAt(s, f).empty()) train(f.experience, f.experienceTenths, fleet, true);
    }
}

// An obsolete design goes, every 10th turn, once no vehicle or unit of it
// exists, no queue of its owner holds it and no other living empire saw it
// less than 50 turns ago: a sighting exactly 50 turns old no longer protects
// it (spec 03 §4.1, confirmed: binary).
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
        if (alive(v))
            for (const UnitStack& st : groupStacks(v)) inUse.insert(st.design);   // every design of a unit group
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
            for (const Empire& other : s.empires) {
                if (other.id == owner.id || !other.alive) continue;
                const std::optional<uint32_t> seen = designSeenTurn(other.knowledge, id);
                if (seen && (*seen >= s.turn || s.turn - *seen < kDesignMemoryTurns)) return false;
            }
            return true;
        });
}

namespace {

// A failed Colonize, told as the movement phases tell it (spec 03 §8): a
// message box for a human in a turn-based game, nothing for a computer
// player there, and the Colonization Minister's log entry in a simultaneous game.
void colonizationFailed(TurnContext& ctx, EmpireId owner, const std::string& name, Location where, const std::string& why) {
    GameState& s = ctx.state;
    if (!s.options.simultaneous) {
        if (s.empire(owner).kind == PlayerKind::Human) ctx.messages.push_back(PlayerMessage{owner, "Colonize", why});
        return;
    }
    ctx.log(owner, LogCategory::Misc, "Unable to Colonize",
            std::format("The Colonization Minister reports that {} could not found a colony in the {} system. {}", name,
                        s.galaxy.system(where.system).name, why),
            where, "OrdersNotCompleted");
}

// Every empire's colony ships, or one empire's that have movement left (turn-based games).
void colonizeWaiting(TurnContext& ctx, std::optional<EmpireId> only) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    // Fleets: the group at the fleet's location carries out the order of the
    // member that acts for it (spec 03 §8, §19 Q65).
    std::vector<FleetId> fleets;
    for (const Fleet& f : s.fleets) fleets.push_back(f.id);
    for (FleetId fid : fleets) {
        const Fleet* f = s.fleet(fid);
        if (!f || (only && f->owner != *only)) continue;
        const Vehicle* holder = fleetOrderHolder(s, *f);
        if (!holder || !colonizeAt(s, holder->orders.front(), f->location)) continue;
        const Order o = holder->orders.front();
        const std::vector<VehicleId> group = fleetGroup(s, *f);
        if (only && std::any_of(group.begin(), group.end(), [&](VehicleId id) { return s.vehicle(id)->movement <= 0; })) continue;
        const EmpireId owner = f->owner;
        const std::string name = f->name;
        const Location where = f->location;
        // The colonizer is the last suitable member in group order; a cloaked member stops it (§8, confirmed: binary).
        VehicleId colonizer;
        std::string why;
        for (VehicleId id : group) {
            const Vehicle& v = *s.vehicle(id);
            if (v.status == VehicleStatus::Cloaked) {
                why = "A cloaked ship cannot colonize.";
                colonizer = {};
                break;
            }
            const std::string p = colonizeProblem(r, s, v, o.object);
            if (p.empty()) colonizer = id;
            else if (why.empty()) why = p;
        }
        if (colonizer.valid()) {
            colonize(ctx, colonizer, o.object);
            for (VehicleId id : group)
                if (Vehicle* v = s.vehicle(id); v && alive(*v)) popFront(v->orders, v->repeatOrders);
        } else {
            // A failed order clears the lists and switches Repeat off (§8).
            colonizationFailed(ctx, owner, name, where, why);
            for (VehicleId id : group)
                if (Vehicle* v = s.vehicle(id)) {
                    v->orders.clear();
                    v->repeatOrders = false;
                }
        }
    }
    // Single ships, in id order (the first to arrive at a planet wins).
    for (size_t i = 0; i < s.vehicles.size(); ++i) {
        Vehicle& v = s.vehicles[i];
        if (!alive(v) || v.orders.empty() || v.fleet.valid()) continue;
        if (only && (v.owner != *only || v.movement <= 0)) continue;
        const Order o = v.orders.front();
        if (!colonizeAt(s, o, v.location)) continue;
        std::string why = colonizeProblem(r, s, v, o.object);
        if (why.empty() && v.status == VehicleStatus::Cloaked) why = "A cloaked ship cannot colonize.";
        if (!why.empty()) {
            // When two ships target the same planet the first processed wins and the other's order fails (§8).
            colonizationFailed(ctx, v.owner, v.name, v.location, why);
            v.orders.clear();
            v.repeatOrders = false;
            continue;
        }
        colonize(ctx, v.id, o.object);
    }
    s.removeDeadVehicles();
}

} // namespace

void detail::foundColony(TurnContext& ctx, VehicleId colonizer, ObjectId planet) { colonize(ctx, colonizer, planet); }

void runColonization(TurnContext& ctx) { colonizeWaiting(ctx, std::nullopt); }

void runColonization(TurnContext& ctx, EmpireId empire) { colonizeWaiting(ctx, empire); }

} // namespace opense4::game::movement
