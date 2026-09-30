// Computer player: fleets, defence, attack, patrol and the logistics
// ministers (spec 05 §7.5 AI_Fleets, "Attack and defence", "Logistics
// ministers", "Mines, satellites and drones"; confirmed: binary unless marked).

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/query.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace opense4::game::ai::detail {

namespace {

using datafile::keysEqual;

Order attackVehicle(const Vehicle& target) {
    Order o;
    o.kind = OrderKind::Attack;
    o.vehicle = target.id;
    o.location = target.location;
    return o;
}

Order attackPlanet(const GameState& s, ObjectId planet) {
    Order o;
    o.kind = OrderKind::Attack;
    o.object = planet;
    o.location = locationOf(s.galaxy, planet);
    return o;
}

// A factor written as a decimal constant in the rules (0.3, 1.3): the double
// nearest to it, num / 2^shift exactly, as the original loads it.
xmath::Ext decimal(int64_t num, int shift) { return xmath::Ext(num) / xmath::Ext(int64_t{1} << shift); }
const xmath::Ext kPoint3 = decimal(5404319552844595, 54);   // 0.3
const xmath::Ext kPoint25 = decimal(1, 2);                   // 0.25
const xmath::Ext kOnePoint3 = decimal(5854679515581645, 52); // 1.3
const xmath::Ext kOnePoint5 = decimal(3, 1);                 // 1.5

int damagedComponents(const Rules& r, const GameState& s, const Vehicle& v) {
    int n = 0;
    const size_t entries = s.design(v.design).entries.size();
    for (size_t i = 0; i < entries; ++i) n += !entryIntact(r, s, v, i);
    return n;
}

bool loaded(const Vehicle& v) { return !v.cargo.units.empty(); }

// The best known spot in a system: our most populous colony, else the centre.
Location spotIn(const Planner& p, SystemId sys) {
    const Colony* best = nullptr;
    for (ObjectId o : p.st.galaxy.system(sys).objects)
        if (const Colony* c = p.st.colony(o); c && c->owner == p.id && (!best || c->totalPopulation() > best->totalPopulation())) best = c;
    if (best) return locationOf(p.st.galaxy, best->planet);
    return {sys, Sector{kSystemCenter, kSystemCenter}};
}

// Spec 05 §7.5: a ship unfit for fleet duty.
bool unfit(Planner& p, const Vehicle& v) {
    const DesignInfo& di = p.info(v.design);
    if (!combatRole(di.role)) return true;
    const int components = static_cast<int>(p.st.design(v.design).entries.size());
    // round(0.3 x the component count) for attack and defence ships, round(0.25 x) for other combat types.
    const int64_t allowed = ((di.role == Role::Attack || di.role == Role::Defense ? kPoint3 : kPoint25) * xmath::Ext(components)).round();
    if (damagedComponents(p.r, p.st, v) > allowed) return true;
    if (!vehicleHasControl(p.r, p.st, v) || vehicleMaxMovement(p.r, p.st, v) <= 0) return true;
    if (di.stats.armed() && vehicleRating(p.r, p.st, v) == 0) return true;
    return false;
}

// Ships an attack fleet takes (spec 05 §7.5).
bool attackMaterial(Planner& p, const Vehicle& v) {
    switch (p.info(v.design).role) {
        case Role::Attack:
        case Role::Kamikaze:
        case Role::Boarding: return true;
        case Role::Carrier:
        case Role::DroneCarrier:
        case Role::TroopTransport: return loaded(v);
        default: return false;
    }
}

// The goal of the attack fleets in the current state (spec 05 §7.5).
std::vector<Order> stateGoal(Planner& p) {
    const AiMemory& m = p.emp().aiMemory;
    switch (p.state) {
        case AiState::PrepareForAttack:
            if (m.staging.valid()) return {moveOrder(spotIn(p, m.staging))};
            return {};
        case AiState::Attack: {
            SystemId best;
            int most = 0;
            for (SystemId t : m.targets) {
                int n = 0;
                for (const Candidate& c : p.sit.candidates) n += c.system == t && hostileTo(p.emp(), c.owner);
                if (n > most) {
                    most = n;
                    best = t;
                }
            }
            for (const Candidate& c : p.sit.candidates)
                if (c.system == best && hostileTo(p.emp(), c.owner))
                    return {moveOrder(locationOf(p.st.galaxy, c.planet)), attackPlanet(p.st, c.planet)};
            return {};
        }
        case AiState::SecureHoldings:
            if (m.secured.valid()) return {moveOrder(spotIn(p, m.secured))};
            return {};
        default:
            if (!p.sit.candidates.empty() && p.atWarWith(p.sit.candidates.front().owner)) {
                const ObjectId planet = p.sit.candidates.front().planet;
                return {moveOrder(locationOf(p.st.galaxy, planet)), attackPlanet(p.st, planet)};
            }
            return {};
    }
}

// Orders that engage an enemy-in-territory entry: in a simultaneous game a
// ship is pursued, otherwise its spot is the goal (spec 05 §7.5).
std::vector<Order> engage(const Planner& p, const Threat& t) {
    if (t.vehicle.valid())
        if (const Vehicle* v = p.st.vehicle(t.vehicle)) return {p.st.options.simultaneous ? attackVehicle(*v) : moveOrder(v->location)};
    if (t.planet.valid()) return {moveOrder(locationOf(p.st.galaxy, t.planet)), attackPlanet(p.st, t.planet)};
    return {};
}

bool anyFleet(const Planner& p) {
    for (const Fleet& f : p.st.fleets)
        if (f.owner == p.id && !f.members.empty()) return true;
    return false;
}

uint32_t formationIndex(const Planner& p, std::string_view name) {
    const auto& list = p.r.data().formations;
    for (size_t i = 0; i < list.size(); ++i)
        if (keysEqual(list[i].name, name)) return static_cast<uint32_t>(i);
    return 0;
}

uint32_t strategyIndex(const Planner& p, std::string_view name) {
    const auto& list = p.emp().strategies;
    for (size_t i = 0; i < list.size(); ++i)
        if (keysEqual(list[i].name, name)) return static_cast<uint32_t>(i);
    return 0;
}

// The nearest own colony (by jumps from `from`) that passes `accept`.
std::optional<ObjectId> nearestColony(Planner& p, SystemId from, auto&& accept) {
    const std::vector<int> jumps = p.jumpsFrom(from);
    std::optional<ObjectId> best;
    int bestJ = 0;
    for (const auto& c : p.st.colonies) {
        if (!c || c->owner != p.id || !accept(*c)) continue;
        const int j = jumps[p.st.galaxy.object(c->planet).system.index()];
        if (j < 0) continue;
        if (!best || j < bestJ) {
            best = c->planet;
            bestJ = j;
        }
    }
    return best;
}

// A unit design of a kind held in a colony's cargo.
std::optional<DesignId> unitsHeld(const Planner& p, const Cargo& cargo, auto&& accept) {
    for (const UnitStack& u : cargo.units)
        if (u.count > 0 && accept(p.r.hull(p.st.design(u.design).hull).type)) return u.design;
    return std::nullopt;
}

// Empty carriers, drone carriers and troop transports reload at the nearest
// colony holding their kind of unit.
void reload(Planner& p, Minister m, std::initializer_list<Role> roles, auto&& unitKind) {
    for (VehicleId id : p.ownVehicles(m)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v) || loaded(*v)) continue;
        const Role role = p.info(v->design).role;
        if (std::find(roles.begin(), roles.end(), role) == roles.end()) continue;
        std::optional<DesignId> unit;
        const auto depot = nearestColony(p, v->location.system, [&](const Colony& c) {
            unit = unitsHeld(p, c.cargo, unitKind);
            return unit.has_value();
        });
        if (!depot) continue;
        const Colony& c = *p.st.colony(*depot);
        unit = unitsHeld(p, c.cargo, unitKind);
        Order load;
        load.kind = OrderKind::LoadCargo;
        load.location = locationOf(p.st.galaxy, *depot);
        load.design = *unit;
        load.amount = -1;
        std::vector<Order> orders;
        if (v->location != load.location) orders.push_back(moveOrder(load.location));
        orders.push_back(load);
        p.setOrders(id, std::move(orders));
    }
}

} // namespace

// ---- Fleets --------------------------------------------------------------------------------------

void planFleets(Planner& p) {
    if (!p.on(Minister::Fleets)) return;
    const FleetsTable& t = p.prof.fleets;
    int vehicles = 0;
    for (const Vehicle& v : p.st.vehicles) vehicles += v.owner == p.id && v.count > 0;
    const int planets = p.colonyCount();
    int wanted = 0;
    for (const FleetDivision& d : t.divisions) {
        if (d.maxShips > 0 ? d.maxShips >= vehicles : d.maxPlanets >= planets) {
            wanted = d.fleets;
            break;
        }
    }
    if (static_cast<int>(p.st.turn) < t.dontUseForTurns) wanted = 0;
    wanted = std::max(0, wanted);

    // Disband fleets beyond the wanted number, and those whose leader is gone or unfit.
    std::vector<FleetId> fleets;
    for (const Fleet& f : p.st.fleets)
        if (p.controlsFleet(f, Minister::Fleets)) fleets.push_back(f.id);
    std::vector<FleetId> keep;
    for (FleetId fid : fleets) {
        const Fleet* f = p.st.fleet(fid);
        const Vehicle* leader = f ? p.st.vehicle(f->leader) : nullptr;
        if (!f || static_cast<int>(keep.size()) >= wanted || !leader || unfit(p, *leader)) {
            if (f) p.emit(cmd::DisbandFleet{fid});
            continue;
        }
        keep.push_back(fid);
    }
    // At most one new fleet per turn, around the newest idle, fit ship outside fleets.
    if (static_cast<int>(keep.size()) < wanted) {
        std::optional<VehicleId> leader;
        for (VehicleId id : p.ownVehicles(Minister::Fleets)) {
            const Vehicle* v = p.st.vehicle(id);
            if (!v || v->fleet.valid() || !p.idle(*v) || unfit(p, *v)) continue;
            if (!leader || id > *leader) leader = id;
        }
        if (leader && p.emit(cmd::CreateFleet{{}, {*leader}})) {
            const FleetId fid = p.st.fleets.back().id;
            keep.push_back(fid);
            const uint32_t formation = formationIndex(p, t.defaultFormation);
            const uint32_t strategy = strategyIndex(p, t.defaultStrategy);
            if (formation != 0 || strategy != 0) p.emit(cmd::SetFleetOptions{fid, formation, strategy});
        }
    }
    if (keep.empty()) return;
    std::sort(keep.begin(), keep.end());

    const int n = static_cast<int>(keep.size());
    // trunc(vehicles x pct / 100 / n) members per fleet.
    const int64_t members =
        wanted > 0 ? (xmath::Ext(vehicles) * xmath::percent(t.percentInFleets) / xmath::Ext(wanted)).trunc() : 0;
    // Fleet i of n attacks when i is odd and (i + 1) / 2 < n x (100 - defence %) / 100.
    const xmath::Ext attackShare = xmath::Ext(wanted) * xmath::percent(100 - t.percentForDefense);
    std::vector<uint8_t> attack(keep.size(), 0);
    for (int i = 1; i <= n; ++i) attack[static_cast<size_t>(i - 1)] = (i % 2 == 1) && xmath::Ext((i + 1) / 2) < attackShare;

    // Recruits: idle ships outside fleets within 3 jumps.
    for (size_t k = 0; k < keep.size(); ++k) {
        const Fleet* f = p.st.fleet(keep[k]);
        const Vehicle* leader = f ? p.st.vehicle(f->leader) : nullptr;
        if (!leader) continue;
        const bool defenceLed = p.info(leader->design).role == Role::Defense;
        const Location at = leader->location;
        const std::vector<int> jumps = p.jumpsFrom(at.system);
        int64_t size = static_cast<int64_t>(f->members.size());
        for (VehicleId id : p.ownVehicles(Minister::Fleets)) {
            if (size >= members) break;
            const Vehicle* v = p.st.vehicle(id);
            if (!v || v->fleet.valid() || !p.idle(*v) || unfit(p, *v)) continue;
            if (defenceLed ? p.info(v->design).role != Role::Defense : !attackMaterial(p, *v)) continue;
            const int j = jumps[v->location.system.index()];
            if (j < 0 || j > 3) continue;
            if (v->location == at) {
                if (p.emit(cmd::JoinFleet{keep[k], id})) ++size;
            } else if (p.setOrders(id, {moveOrder(at)})) {
                ++size;  // ordered to join (it joins when it arrives)
            }
            f = p.st.fleet(keep[k]);
        }
    }

    // Orders go to idle fleets only.
    std::vector<size_t> idleFleets;
    for (size_t k = 0; k < keep.size(); ++k)
        if (const Fleet* f = p.st.fleet(keep[k]); f && f->orders.empty() && !f->members.empty()) idleFleets.push_back(k);
        else if (f) p.busyFleets.insert(keep[k]);
    std::vector<uint8_t> done(keep.size(), 0);
    if (p.state == AiState::DefendShortTerm) {
        for (const Threat& threat : p.sit.enemyInTerritory) {
            std::optional<size_t> best;
            int bestJ = 0;
            for (size_t k : idleFleets) {
                if (done[k] || attack[k]) continue;
                const Vehicle* leader = p.st.vehicle(p.st.fleet(keep[k])->leader);
                const int j = p.jumpsFrom(leader->location.system)[threat.system.index()];
                if (j < 0) continue;
                if (!best || j < bestJ) {
                    best = k;
                    bestJ = j;
                }
            }
            if (!best) continue;
            auto orders = engage(p, threat);
            if (!orders.empty() && p.setFleetOrders(keep[*best], std::move(orders))) done[*best] = 1;
        }
    } else {
        const std::vector<Order> goal = stateGoal(p);
        if (!goal.empty())
            for (size_t k : idleFleets)
                if (attack[k] && !done[k] && p.setFleetOrders(keep[k], goal)) done[k] = 1;
    }
    // Leftover fleets defend, then patrol (defence-led) or explore.
    size_t defendAt = 0;
    for (size_t k : idleFleets) {
        if (done[k]) continue;
        const Fleet* f = p.st.fleet(keep[k]);
        const Vehicle* leader = p.st.vehicle(f->leader);
        if (defendAt < p.sit.defend.size()) {
            const SystemId sys = p.sit.defend[defendAt++];
            for (const Threat& threat : p.sit.enemyInTerritory)
                if (threat.system == sys) {
                    if (auto orders = engage(p, threat); !orders.empty() && p.setFleetOrders(keep[k], std::move(orders))) done[k] = 1;
                    break;
                }
            if (done[k]) continue;
        }
        if (p.info(leader->design).role == Role::Defense) {
            // Patrol: our colony with the fewest of our ships (inferred for fleets).
            std::optional<ObjectId> best;
            int fewest = 0;
            for (const auto& c : p.st.colonies) {
                if (!c || c->owner != p.id) continue;
                int ships = 0;
                const Location at = locationOf(p.st.galaxy, c->planet);
                for (const Vehicle& v : p.st.vehicles) ships += v.owner == p.id && v.location == at;
                if (!best || ships < fewest) {
                    best = c->planet;
                    fewest = ships;
                }
            }
            if (best && leader->location != locationOf(p.st.galaxy, *best)) p.setFleetOrders(keep[k], {moveOrder(locationOf(p.st.galaxy, *best))});
        } else if (!p.sit.freeFrontier.empty() && !p.neutral) {
            const ObjectId wp = p.sit.freeFrontier.front();
            Order warp;
            warp.kind = OrderKind::Warp;
            warp.object = wp;
            warp.location = locationOf(p.st.galaxy, wp);
            p.setFleetOrders(keep[k], {moveOrder(warp.location), warp});
        }
    }
}

// ---- Defence and attack ------------------------------------------------------------------------

void planDefense(Planner& p) {
    if (!p.on(Minister::Defense) || anyFleet(p) || p.sit.defend.empty()) return;
    // Defenders keep going to a threat until they exceed round(1.3 x) it (Low) or round(1.5 x) it.
    const xmath::Ext factor = p.difficulty == kDifficultyLow ? kOnePoint3 : kOnePoint5;
    std::vector<VehicleId> defenders;
    for (VehicleId id : p.ownVehicles(Minister::Defense)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v)) continue;
        const Role role = p.info(v->design).role;
        if ((role == Role::Attack || role == Role::Defense) && p.info(v->design).stats.movement > 0) defenders.push_back(id);
    }
    for (SystemId sys : p.sit.defend) {
        // The threat: the strength of the listed enemies in that system (inferred reading).
        int64_t strength = 0;
        const Threat* threat = nullptr;
        for (const Threat& t : p.sit.enemyInTerritory) {
            if (t.system != sys) continue;
            if (!threat) threat = &t;
            const Vehicle* v = t.vehicle.valid() ? p.st.vehicle(t.vehicle) : nullptr;
            strength += v ? vehicleRating(p.r, p.st, *v) + 1 : 1;
        }
        if (!threat) continue;
        const int64_t need = (factor * xmath::Ext(strength)).round();
        int64_t have = 0;
        std::vector<std::pair<int, VehicleId>> byDistance;
        for (VehicleId id : defenders) {
            if (p.busy.contains(id)) continue;
            const int j = p.jumpsFrom(p.st.vehicle(id)->location.system)[sys.index()];
            if (j >= 0) byDistance.emplace_back(j, id);
        }
        std::sort(byDistance.begin(), byDistance.end());
        for (const auto& [j, id] : byDistance) {
            if (have > need) break;
            auto orders = engage(p, *threat);
            if (orders.empty() || !p.setOrders(id, std::move(orders))) continue;
            have += p.strengthOf(*p.st.vehicle(id));
        }
    }
}

void planAttack(Planner& p) {
    if (!p.on(Minister::Attack) || anyFleet(p)) return;
    // The target system of the state (spec 05 §7.5), as for the fleets.
    SystemId target;
    const AiMemory& m = p.emp().aiMemory;
    if (p.state == AiState::Attack && !m.targets.empty()) target = m.targets.front();
    else if (p.state == AiState::SecureHoldings) target = m.secured;
    else if (p.state != AiState::PrepareForAttack && !p.sit.candidates.empty() && p.atWarWith(p.sit.candidates.front().owner))
        target = p.sit.candidates.front().system;
    std::vector<const Candidate*> candidates;
    for (const Candidate& c : p.sit.candidates)
        if (c.system == target && hostileTo(p.emp(), c.owner)) candidates.push_back(&c);
    if (candidates.empty()) return;
    std::vector<int64_t> assigned(candidates.size(), 0);
    for (VehicleId id : p.ownVehicles(Minister::Attack)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v) || p.info(v->design).role != Role::Attack || p.info(v->design).stats.movement <= 0) continue;
        std::optional<size_t> pick;
        for (size_t i = 0; i < candidates.size() && !pick; ++i) {
            if (p.rng.percent(25)) continue;
            const EmpireId owner = candidates[i]->owner;
            // k = 1.5 when our score exceeds 1.5 times the owner's.
            const bool strong = p.scores[p.id.index()] * 2 > p.scores[owner.index()] * 3;
            const int64_t limit = strong ? (kOnePoint5 * xmath::Ext(candidates[i]->value)).round() : candidates[i]->value;
            if (assigned[i] <= limit) pick = i;
        }
        for (size_t i = 0; i < candidates.size() && !pick; ++i)
            if (p.rng.percent(75)) pick = i;
        if (!pick) continue;
        const Location at = locationOf(p.st.galaxy, candidates[*pick]->planet);
        const Order order = v->location == at ? attackPlanet(p.st, candidates[*pick]->planet) : moveOrder(at);
        if (p.setOrders(id, {order})) assigned[*pick] += p.strengthOf(*v);
    }
}

void planPatrol(Planner& p) {
    if (!p.on(Minister::Patrol)) return;
    std::map<Location, int> ships;
    for (const Vehicle& v : p.st.vehicles)
        if (v.owner == p.id) ++ships[v.location];
    for (VehicleId id : p.ownVehicles(Minister::Patrol)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v) || p.info(v->design).stats.movement <= 0) continue;
        const Role role = p.info(v->design).role;
        if (role != Role::Attack && role != Role::Defense) continue;
        // Our colony with the fewest of our ships present, the smallest population breaking ties.
        std::optional<ObjectId> best;
        std::pair<int, int64_t> bestKey{};
        for (const auto& c : p.st.colonies) {
            if (!c || c->owner != p.id || !p.mayEnter(p.st.galaxy.object(c->planet).system)) continue;
            const Location at = locationOf(p.st.galaxy, c->planet);
            const std::pair<int, int64_t> key{ships[at] - (v->location == at ? 1 : 0), c->totalPopulation()};
            if (!best || key < bestKey) {
                best = c->planet;
                bestKey = key;
            }
        }
        if (!best) continue;
        const Location at = locationOf(p.st.galaxy, *best);
        if (v->location == at) {
            p.busy.insert(id);  // already on station
            continue;
        }
        if (p.setOrders(id, {moveOrder(at)})) {
            --ships[v->location];
            ++ships[at];
        }
    }
}

// ---- Logistics --------------------------------------------------------------------------------------

void planTroops(Planner& p) {
    if (!p.on(Minister::Troops)) return;
    reload(p, Minister::Troops, {Role::TroopTransport}, [](ruleset::VehicleType t) { return t == ruleset::VehicleType::Troop; });
}

void planCarriers(Planner& p) {
    if (!p.on(Minister::Carriers)) return;
    reload(p, Minister::Carriers, {Role::Carrier}, [](ruleset::VehicleType t) { return t == ruleset::VehicleType::Fighter; });
    reload(p, Minister::Carriers, {Role::DroneCarrier}, [](ruleset::VehicleType t) { return t == ruleset::VehicleType::Drone; });
}

// Population transports (spec 02 §10, spec 05 §7.5).
void planTransports(Planner& p) {
    if (!p.on(Minister::Transports) || p.neutral) return;
    const int64_t popMass = std::max<int64_t>(1, p.r.setting("Population Mass", 5));
    std::set<ObjectId> destinations;
    for (const Vehicle& v : p.st.vehicles)
        if (v.owner == p.id)
            for (const Order& o : v.orders)
                if (o.kind == OrderKind::DropCargo) destinations.insert(o.object);
    auto safe = [&](const Colony& c) { return p.sit.hostile[p.st.galaxy.object(c.planet).system.index()] == 0; };
    auto hosts = [&](const Colony& c, EmpireId race) {
        return race.valid() && race.index() < p.st.empires.size() &&
               keysEqual(p.st.galaxy.object(c.planet).atmosphere, p.st.empire(race).race.atmosphere);
    };
    auto underPopulated = [&](const Colony& c, EmpireId race) {
        return c.totalPopulation() < maxPopulation(p.r, p.st, c) && safe(c) && hosts(c, race) && !destinations.contains(c.planet);
    };
    for (VehicleId id : p.ownVehicles(Minister::Transports)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v) || p.info(v->design).role != Role::Transport) continue;
        const int capacity = vehicleCargoCapacity(p.r, p.st, *v);
        if (capacity <= 0) continue;
        const int64_t carried = v->cargo.totalPopulation();
        if (carried * popMass * 2 > capacity) {
            // Deliver to the least-populated planet that can take the race.
            const EmpireId race = v->cargo.population.front().race;
            const Colony* to = nullptr;
            for (const auto& c : p.st.colonies)
                if (c && c->owner == p.id && underPopulated(*c, race) && (!to || c->totalPopulation() < to->totalPopulation())) to = &*c;
            if (!to) continue;
            Order drop;
            drop.kind = OrderKind::DropCargo;
            drop.location = locationOf(p.st.galaxy, to->planet);
            drop.object = to->planet;
            drop.amount = -1;
            destinations.insert(to->planet);
            p.setOrders(id, {moveOrder(drop.location), drop});
            continue;
        }
        // Load at the nearest safe planet with 1000M of a race some planet can take.
        const auto source = nearestColony(p, v->location.system, [&](const Colony& c) {
            if (!safe(c)) return false;
            for (const PopulationGroup& g : c.population) {
                if (g.millions < 1000) continue;
                for (const auto& other : p.st.colonies)
                    if (other && other->owner == p.id && other->planet != c.planet && underPopulated(*other, g.race)) return true;
            }
            return false;
        });
        if (!source) continue;
        Order load;
        load.kind = OrderKind::LoadCargo;
        load.location = locationOf(p.st.galaxy, *source);
        load.amount = -1;
        p.setOrders(id, {moveOrder(load.location), load});
    }
}

// Where Space Yard Ships go is open (spec 05 open questions): they stay put.
void planSpaceYardShips(Planner&) {}

// The Stellar Manipulation minister is open (spec 05 open questions): it does nothing.
void planStellarManipulation(Planner&) {}

namespace {

using ruleset::VehicleType;

// Units of one kind held by the empire: in space, and in the cargo of its
// planets and vehicles.
struct UnitTotals {
    int64_t all = 0;
    int64_t onPlanets = 0;
};
UnitTotals unitTotals(Planner& p, VehicleType kind) {
    UnitTotals t;
    auto ofKind = [&](DesignId d) { return p.info(d).stats.vehicleType == kind; };
    for (const Vehicle& v : p.st.vehicles) {
        if (v.owner != p.id) continue;
        if (v.count > 0 && ofKind(v.design)) t.all += v.count;
        for (const UnitStack& u : v.cargo.units)
            if (ofKind(u.design)) t.all += u.count;
    }
    for (const auto& c : p.st.colonies)
        if (c && c->owner == p.id)
            for (const UnitStack& u : c->cargo.units)
                if (ofKind(u.design)) {
                    t.all += u.count;
                    t.onPlanets += u.count;
                }
    return t;
}

// Launches above the kept share: trunc(total x kept % / 100) stay in planet
// cargo, counted over every unit of the kind the empire holds (inferred: the
// spec does not say whether "total" is per planet or for the empire).
int64_t launchQuota(const UnitTotals& t, int keptPercent) {
    const int64_t kept = t.all * std::clamp(keptPercent, 0, 100) / 100;
    return std::max<int64_t>(0, t.onPlanets - kept);
}

Order launchOrder(DesignId unit, int64_t amount) {
    Order o;
    o.kind = OrderKind::LaunchUnits;
    o.design = unit;
    o.amount = static_cast<int>(std::min<int64_t>(amount, INT32_MAX));
    return o;
}

// The drones of the planet launch: half after ships, half after planets of
// empires at War with us, each half capped at targets x drones per target.
// Each target in turn gets its share from the nearest planets within range,
// the half's own drone type first (inferred). `left` is what each planet
// still holds.
void launchDrones(Planner& p, int64_t drones, const std::vector<ObjectId>& planets, std::map<ObjectId, std::vector<Order>>& orders,
                  std::map<std::pair<ObjectId, DesignId>, int64_t>& left) {
    const SettingsTable& set = p.prof.settings;
    std::vector<std::pair<SystemId, Order>> shipTargets, planetTargets;
    for (const Threat& t : p.sit.enemyInTerritory) {
        const Vehicle* v = t.vehicle.valid() ? p.st.vehicle(t.vehicle) : nullptr;
        if (!v || !p.atWarWith(t.owner) || isUnitType(vehicleType(p.r, p.st, *v))) continue;
        shipTargets.emplace_back(t.system, attackVehicle(*v));
    }
    for (const Candidate& c : p.sit.candidates)
        if (p.atWarWith(c.owner)) planetTargets.emplace_back(c.system, attackPlanet(p.st, c.planet));
    std::map<SystemId, std::vector<int>> jumps;
    auto jumpsTo = [&](ObjectId planet, SystemId target) {
        const SystemId from = p.st.galaxy.object(planet).system;
        auto it = jumps.find(from);
        if (it == jumps.end()) it = jumps.emplace(from, p.jumpsFrom(from)).first;
        return it->second[target.index()];
    };
    auto sendHalf = [&](const std::vector<std::pair<SystemId, Order>>& targets, int64_t quota, int perTarget, int range,
                        std::string_view preferred) {
        quota = std::min<int64_t>(quota, static_cast<int64_t>(targets.size()) * std::max(0, perTarget));
        for (const auto& [system, attack] : targets) {
            int64_t want = std::min<int64_t>(perTarget, quota);
            // Planets within range, nearest first; the drone type named for this half first (inferred).
            std::vector<std::pair<int, ObjectId>> from;
            for (ObjectId planet : planets)
                if (const int j = jumpsTo(planet, system); j >= 0 && j <= range) from.emplace_back(j, planet);
            std::sort(from.begin(), from.end());
            for (int pass = 0; pass < 2 && want > 0; ++pass)
                for (const auto& [j, planet] : from) {
                    for (const UnitStack& u : p.st.colony(planet)->cargo.units) {
                        if (want <= 0) break;
                        const DesignInfo& di = p.info(u.design);
                        if (di.stats.vehicleType != VehicleType::Drone || keysEqual(di.aiType, preferred) != (pass == 0)) continue;
                        int64_t& have = left[{planet, u.design}];
                        const int64_t n = std::min(have, want);
                        if (n <= 0) continue;
                        Order o = launchOrder(u.design, n);
                        o.vehicle = attack.vehicle;
                        o.object = attack.object;
                        orders[planet].push_back(o);
                        have -= n;
                        want -= n;
                        quota -= n;
                    }
                    if (want <= 0) break;
                }
            if (quota <= 0) break;
        }
    };
    sendHalf(shipTargets, (drones + 1) / 2, set.antiShipDronesPerTarget, set.antiShipDroneRange, "Anti-Ship Drone");
    sendHalf(planetTargets, drones / 2, set.antiPlanetDronesPerTarget, set.antiPlanetDroneRange, "Anti-Planet Drone");
}

// Spec 05 §7.5: satellites and drones in planet cargo above the kept shares
// are launched, as planet orders (spec 03 §12).
void launchFromPlanets(Planner& p, bool roomForUnits) {
    std::vector<ObjectId> planets;
    for (const auto& c : p.st.colonies)
        if (c && !c->cargo.units.empty() && p.controlsColony(*c, Minister::MinesSatellitesDrones)) planets.push_back(c->planet);
    if (planets.empty() || !roomForUnits) return;
    const SettingsTable& set = p.prof.settings;
    std::map<ObjectId, std::vector<Order>> orders;
    std::map<std::pair<ObjectId, DesignId>, int64_t> left;  // units still in each planet's cargo
    for (ObjectId planet : planets)
        for (const UnitStack& u : p.st.colony(planet)->cargo.units) left[{planet, u.design}] += u.count;

    // Satellites, planet by planet, where each planet is (up to the sector cap).
    int64_t satellites = launchQuota(unitTotals(p, VehicleType::Satellite), set.satellitesKeptPercent);
    const int64_t perSector = p.r.setting("Maximum Satellites Per Player Per Sector", 100);
    for (ObjectId planet : planets) {
        if (satellites <= 0) break;
        const Location at = locationOf(p.st.galaxy, planet);
        int64_t room = perSector;
        for (const Vehicle& v : p.st.vehicles)
            if (v.owner == p.id && v.location == at && v.count > 0 && p.info(v.design).stats.vehicleType == VehicleType::Satellite) room -= v.count;
        for (const UnitStack& u : p.st.colony(planet)->cargo.units) {
            if (satellites <= 0 || room <= 0) break;
            if (u.count <= 0 || p.info(u.design).stats.vehicleType != VehicleType::Satellite) continue;
            const int64_t n = std::min({int64_t{u.count}, satellites, room});
            orders[planet].push_back(launchOrder(u.design, n));
            left[{planet, u.design}] -= n;
            satellites -= n;
            room -= n;
        }
    }

    // Drones: the quota split in two halves (the odd one goes after ships; inferred).
    if (const int64_t drones = launchQuota(unitTotals(p, VehicleType::Drone), set.dronesKeptPercent); drones > 0)
        launchDrones(p, drones, planets, orders, left);

    for (auto& [planet, list] : orders) {
        if (list.empty() || p.st.colony(planet)->orders == list) continue;
        p.emit(cmd::SetOrders{.orders = std::move(list), .planet = planet});
    }
}

} // namespace

void planMinesSatellitesDrones(Planner& p) {
    if (!p.on(Minister::MinesSatellitesDrones)) return;
    const bool roomForUnits = unitCount(p.r, p.st, p.id) < p.st.options.maxUnitsPerPlayer;
    launchFromPlanets(p, roomForUnits);
    for (VehicleId id : p.ownVehicles(Minister::MinesSatellitesDrones)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v)) continue;
        const DesignInfo& di = p.info(v->design);
        switch (di.role) {
            case Role::Sweeper:
                // Sweepers sweep the hostile mine fields in our territory.
                if (!p.sit.enemyNearby.empty()) {
                    const Vehicle* field = p.st.vehicle(p.sit.enemyNearby.front().vehicle);
                    if (field) p.setOrders(id, {moveOrder(field->location), simpleOrder(OrderKind::SweepMines)});
                }
                break;
            case Role::MineLayer:
            case Role::SatelliteLayer:
                // Layers work only below the unit limit; they lay what they carry where they are (inferred).
                if (roomForUnits && loaded(*v)) {
                    Order launch;
                    launch.kind = OrderKind::LaunchUnits;
                    launch.location = v->location;
                    launch.design = v->cargo.units.front().design;
                    launch.amount = -1;
                    p.setOrders(id, {launch});
                }
                break;
            case Role::Unit: {
                // Idle drones go after targets within range.
                if (di.stats.vehicleType != ruleset::VehicleType::Drone) break;
                const bool antiPlanet = keysEqual(di.aiType, "Anti-Planet Drone");
                const int range = antiPlanet ? p.prof.settings.antiPlanetDroneRange : p.prof.settings.antiShipDroneRange;
                const std::vector<int> jumps = p.jumpsFrom(v->location.system);
                if (antiPlanet) {
                    for (const Candidate& c : p.sit.candidates)
                        if (p.atWarWith(c.owner) && jumps[c.system.index()] >= 0 && jumps[c.system.index()] <= range) {
                            p.setOrders(id, {attackPlanet(p.st, c.planet)});
                            break;
                        }
                } else {
                    for (const Threat& t : p.sit.enemyInTerritory)
                        if (t.vehicle.valid() && p.atWarWith(t.owner) && jumps[t.system.index()] >= 0 && jumps[t.system.index()] <= range)
                            if (const Vehicle* target = p.st.vehicle(t.vehicle)) {
                                p.setOrders(id, {attackVehicle(*target)});
                                break;
                            }
                }
                break;
            }
            default: break;
        }
    }
}

// Damaged ships and ships low on supplies go to the nearest repair or supply
// point. The thresholds are open: 30 % structure lost, 20 % supply left (inferred).
void planRepairAndResupply(Planner& p, bool repair) {
    for (const Fleet& f : p.st.fleets) {
        if (!p.controlsFleet(f, repair ? Minister::Repair : Minister::Resupply) || f.members.empty()) continue;
        bool need = false;
        for (VehicleId m : f.members)
            if (const Vehicle* v = p.st.vehicle(m)) {
                if (repair) {
                    const int structure = vehicleStructure(p.r, p.st, *v);
                    need = need || (structure > 0 && int64_t{vehicleDamageTaken(p.st, *v)} * 100 > int64_t{structure} * 30);
                } else {
                    const int64_t cap = vehicleSupplyCapacity(p.r, p.st, *v);
                    need = need || (cap > 0 && v->supply * 100 < cap * 20 && !vehicleHasQuantumReactor(p.r, p.st, *v));
                }
            }
        const OrderKind k = repair ? OrderKind::Repair : OrderKind::Resupply;
        if (need && (f.orders.empty() || f.orders.front().kind != k)) p.setFleetOrders(f.id, {simpleOrder(k)});
    }
    for (VehicleId id : p.ownVehicles(repair ? Minister::Repair : Minister::Resupply)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || v->status == VehicleStatus::Mothballed || p.info(v->design).stats.movement <= 0) continue;
        if (isUnitType(p.info(v->design).stats.vehicleType)) continue;
        const OrderKind k = repair ? OrderKind::Repair : OrderKind::Resupply;
        if (!v->orders.empty() && (v->orders.front().kind == OrderKind::Repair || v->orders.front().kind == OrderKind::Resupply)) continue;
        bool need = false;
        if (repair) {
            const int structure = vehicleStructure(p.r, p.st, *v);
            need = structure > 0 && int64_t{vehicleDamageTaken(p.st, *v)} * 100 > int64_t{structure} * 30;
        } else {
            const int64_t cap = vehicleSupplyCapacity(p.r, p.st, *v);
            need = cap > 0 && v->supply * 100 < cap * 20 && !vehicleHasQuantumReactor(p.r, p.st, *v);
        }
        if (need) p.setOrders(id, {simpleOrder(k)});
    }
}

void planScrap(Planner& p) {
    // While over the soft cap: one non-colony ship per turn, of the oldest design.
    if (p.overCap(0)) {
        std::vector<std::tuple<uint32_t, VehicleId>> ships;
        for (VehicleId id : p.ownVehicles(Minister::Scrap)) {
            const Vehicle* v = p.st.vehicle(id);
            if (!v || isUnitType(p.info(v->design).stats.vehicleType) || p.info(v->design).role == Role::Colonizer) continue;
            ships.emplace_back(p.st.design(v->design).createdTurn, id);
        }
        std::sort(ships.begin(), ships.end());
        for (const auto& [turn, id] : ships)
            if (spaceYardAt(p.r, p.st, p.id, p.st.vehicle(id)->location) && p.emit(cmd::Scrap{id, {}, -1})) break;
    }
    // Every 10 turns: useless facilities.
    if (p.st.turn % 10 != 0) return;
    for (const auto& c : p.st.colonies) {
        if (!c || !p.controlsColony(*c, Minister::Scrap)) continue;
        const ObjectId planet = c->planet;
        for (size_t i = p.st.colony(planet)->facilities.size(); i-- > 0;) {
            const Colony& col = *p.st.colony(planet);
            const uint32_t f = col.facilities[i];
            const auto ab = p.r.facilityAbilities(f);
            bool useless = false;
            if (p.st.options.finiteResources) {
                const SpaceObject& obj = p.st.galaxy.object(planet);
                useless = (hasAbility(ab, AbilityKind::ResourceGenMinerals) && obj.value[0] == 0) ||
                          (hasAbility(ab, AbilityKind::ResourceGenOrganics) && obj.value[1] == 0) ||
                          (hasAbility(ab, AbilityKind::ResourceGenRadioactives) && obj.value[2] == 0);
            }
            if (hasAbility(ab, AbilityKind::PlanetChangeAtmosphere) && breathable(p.st, col)) useless = true;
            if (useless) p.emit(cmd::Scrap{{}, planet, static_cast<int32_t>(i)});
        }
    }
}

void planRetrofit(Planner& p) {
    if (p.overCap(0) || p.state == AiState::Attack || p.state == AiState::Incursion || p.state == AiState::DefendShortTerm) return;
    int started = 0;
    for (VehicleId id : p.ownVehicles(Minister::Retrofit)) {
        if (started >= 3) break;
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v) || !v->cargo.empty() || isUnitType(p.info(v->design).stats.vehicleType)) continue;
        if (p.sit.hostile[v->location.system.index()] > 0) continue;
        const Design& old = p.st.design(v->design);
        if (static_cast<int64_t>(p.st.turn) - old.createdTurn <= 20) continue;
        // The newest valid design with the same hull and design type.
        std::optional<DesignId> newer;
        for (DesignId d : p.emp().designs) {
            const Design& cand = p.st.design(d);
            if (d == v->design || cand.obsolete || cand.hull != old.hull || !keysEqual(cand.designType, old.designType)) continue;
            if (cand.createdTurn <= old.createdTurn || !p.info(d).stats.problems.empty()) continue;
            if (!newer || std::pair(cand.createdTurn, d) > std::pair(p.st.design(*newer).createdTurn, *newer)) newer = d;
        }
        if (!newer) continue;
        if (spaceYardAt(p.r, p.st, p.id, v->location)) {
            if (p.emit(cmd::Retrofit{id, *newer})) {
                p.busy.insert(id);
                ++started;
            }
            continue;
        }
        const auto yard = nearestColony(p, v->location.system, [&](const Colony& c) { return colonyHasSpaceYard(p.r, c); });
        if (yard && p.setOrders(id, {moveOrder(locationOf(p.st.galaxy, *yard))})) ++started;
    }
}

} // namespace opense4::game::ai::detail
