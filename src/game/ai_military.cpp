// Computer player: fleets, defence, attack, patrol and the logistics
// ministers (spec 05 §7.5 AI_Fleets, "Attack and defence", "Logistics
// ministers", "Mines, satellites and drones"; confirmed: binary unless marked).

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/movement.hpp"
#include "game/orders.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <deque>
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

// It lacks a part it needs to operate (spec 05 §7.5 Repair, confirmed:
// binary): no working source of `Ship Bridge`, none of `Ship Auxiliary
// Control` and none of `Master Computer`, the hull's own abilities included.
// Engines, life support and crew quarters are not checked. A mothballed
// vehicle has no working abilities, so it always lacks one.
bool lacksOperatingPart(const Planner& p, const Vehicle& v) {
    const std::vector<ParsedAbility> working = vehicleAbilities(p.r, p.st, v);
    return !hasAbility(working, AbilityKind::ShipBridge) && !hasAbility(working, AbilityKind::ShipAuxiliaryControl) &&
           !hasAbility(working, AbilityKind::MasterComputer);
}

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
    if (lacksOperatingPart(p, v)) return true;
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

// The nearest own colony (by jumps from `from`, over every link) that passes `accept`.
std::optional<ObjectId> nearestColony(Planner& p, SystemId from, auto&& accept) {
    const std::vector<int> jumps = p.jumpsFrom(from);
    std::optional<ObjectId> best;
    int bestJ = 0;
    for (const auto& c : p.st.colonies) {
        if (!c || c->owner != p.id || !accept(*c)) continue;
        const int j = jumps[p.st.galaxy.object(c->planet).system.index()];
        if (j == kUnreachable) continue;
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
    if (static_cast<int64_t>(p.date) < t.dontUseForTurns) wanted = 0;  // the date the ministers see
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
            if (j > 3) continue;
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
    // Leftover fleets defend (the defend list's entries, strongest threat
    // first), then patrol (defence-led) or explore.
    std::vector<const DefendEntry*> toDefend;
    for (const DefendEntry& d : p.sit.defendEntries)
        if (std::find(p.sit.defend.begin(), p.sit.defend.end(), d.where.system) != p.sit.defend.end()) toDefend.push_back(&d);
    size_t defendAt = 0;
    for (size_t k : idleFleets) {
        if (done[k]) continue;
        const Fleet* f = p.st.fleet(keep[k]);
        const Vehicle* leader = p.st.vehicle(f->leader);
        if (defendAt < toDefend.size()) {
            if (auto orders = engage(p, toDefend[defendAt++]->latest); !orders.empty() && p.setFleetOrders(keep[k], std::move(orders))) done[k] = 1;
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

// Spec 05 §7.5 "Defence" (confirmed: binary): only in Defend (Short Term).
// Each defender in turn goes to the first entry, in the defend-list systems
// and the Defense minister's order (§7.2: the weaker threat first), whose
// assigned strength is at most round(its threat × 1.3) at Low difficulty or
// round(its threat × 1.5) at Medium and High. The order is Attack when the
// defender is already in that sector, otherwise a move there, or in a
// simultaneous game a pursuit of the entry's latest object. The entry's
// assigned strength then grows by the defender's rating, without the + 1.
void planDefense(Planner& p) {
    if (!p.on(Minister::Defense) || anyFleet(p) || p.state != AiState::DefendShortTerm || p.sit.defend.empty()) return;
    const xmath::Ext factor = p.difficulty == kDifficultyLow ? kOnePoint3 : kOnePoint5;
    std::vector<DefendEntry> entries;
    for (const DefendEntry& d : p.sit.defendEntries)
        if (std::find(p.sit.defend.begin(), p.sit.defend.end(), d.where.system) != p.sit.defend.end()) entries.push_back(d);
    sortDefendEntries(entries, true);  // ships outside fleets: the weakest threat first
    std::vector<int64_t> assigned(entries.size(), 0);  // tenths
    for (VehicleId id : p.ownVehicles(Minister::Defense)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v)) continue;
        const Role role = p.info(v->design).role;
        if ((role != Role::Attack && role != Role::Defense) || p.info(v->design).stats.movement <= 0) continue;
        for (size_t i = 0; i < entries.size(); ++i) {
            const int64_t limit = (factor * xmath::Ext(entries[i].threat) / xmath::Ext(kStrengthScale)).round() * kStrengthScale;
            if (assigned[i] > limit) continue;
            // Attack there (a pursuit in a simultaneous game), else a move there.
            const Threat& latest = entries[i].latest;
            const Vehicle* target = latest.vehicle.valid() ? p.st.vehicle(latest.vehicle) : nullptr;
            const Order order = v->location == entries[i].where || p.st.options.simultaneous
                                    ? (target ? attackVehicle(*target) : attackPlanet(p.st, latest.planet))
                                    : moveOrder(entries[i].where);
            if (p.setOrders(id, {order})) assigned[i] += vehicleRating(p.r, p.st, *p.st.vehicle(id));
            break;
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
    std::vector<int64_t> assigned(candidates.size(), 0);  // tenths
    for (VehicleId id : p.ownVehicles(Minister::Attack)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v) || p.info(v->design).role != Role::Attack || p.info(v->design).stats.movement <= 0) continue;
        std::optional<size_t> pick;
        for (size_t i = 0; i < candidates.size() && !pick; ++i) {
            if (p.rng.percent(25)) continue;
            const EmpireId owner = candidates[i]->owner;
            // At most round(value × k): k = 1.5 when our score exceeds 1.5 times the owner's.
            const bool strong = p.scores[p.id.index()] * 2 > p.scores[owner.index()] * 3;
            const xmath::Ext value = xmath::Ext(candidates[i]->value) / xmath::Ext(kStrengthScale);
            const int64_t limit = (strong ? (kOnePoint5 * value).round() : value.round()) * kStrengthScale;
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

namespace {

// The travel distance and target of the nearest of `goals` (movement's routes).
std::optional<movement::NearestPath> nearestByTravel(const Planner& p, Location from, const std::vector<Location>& goals) {
    if (goals.empty()) return std::nullopt;
    return movement::findPathToNearest(p.r, p.st, p.id, from, goals);
}

// A visible, armed, non-mothballed hostile vehicle in the sector.
bool armedHostileSeenAt(Planner& p, Location where) {
    for (VehicleId vid : p.emp().knowledge.visibleVehicles) {
        const Vehicle* v = p.st.vehicle(vid);
        if (!v || v->count <= 0 || v->location != where || !hostileTo(p.emp(), v->owner) || v->status == VehicleStatus::Mothballed) continue;
        if (p.info(v->design).stats.armed()) return true;
    }
    return false;
}

// The Resupply minister's orders for a group at `from` (spec 05 §7.5,
// confirmed: binary): a Move To the nearest own or allied depot, avoiding
// sectors with a visible armed hostile (the Resupply order of spec 03 §8);
// with no depot, a move to our nearest colony, or home when we have none.
// Already there: no order.
std::vector<Order> resupplyOrders(Planner& p, Location from) {
    auto goTo = [&](Location where) { return where == from ? std::vector<Order>{} : std::vector<Order>{moveOrder(where)}; };
    std::vector<Location> depots, colonies;
    for (const auto& c : p.st.colonies) {
        if (!c) continue;
        const Location at = locationOf(p.st.galaxy, c->planet);
        if (c->owner == p.id) colonies.push_back(at);
        if (!p.st.options.omnipresent && !p.explored(at.system)) continue;
        if (movement::resupplyDepotAt(p.r, p.st, p.id, at) && !armedHostileSeenAt(p, at)) depots.push_back(at);
    }
    if (const auto near = nearestByTravel(p, from, depots)) return goTo(depots[near->goal]);
    if (const auto near = nearestByTravel(p, from, colonies)) return goTo(colonies[near->goal]);
    return p.homeLocation.system.valid() ? goTo(p.homeLocation) : std::vector<Order>{};
}

} // namespace

// Population transports (spec 02 §10, spec 05 §7.5 "Logistics ministers",
// confirmed: binary). An idle transport that carries people and whose used
// cargo space is more than half its capacity delivers; otherwise it runs the
// load step. A failed delivery never falls back to loading. After the load
// step the delivery is planned in the same turn only when the source is in
// the transport's own sector, by the races already aboard. A transport left
// without orders gets the resupply orders.
void planTransports(Planner& p) {
    if (!p.on(Minister::Transports) || p.neutral) return;
    std::set<ObjectId> dropTargets;  // other transports' drop targets: only a Drop order reserves one
    for (const Vehicle& v : p.st.vehicles)
        if (v.owner == p.id)
            for (const Order& o : v.orders)
                if (o.kind == OrderKind::DropCargo && o.object.valid()) dropTargets.insert(o.object);
    auto safe = [&](const Colony& c) { return p.sit.hostile[p.st.galaxy.object(c.planet).system.index()] == 0; };
    auto atmosphereOf = [&](EmpireId race) -> std::string_view {
        return race.valid() && race.index() < p.st.empires.size() ? std::string_view{p.st.empire(race).race.atmosphere} : std::string_view{};
    };
    auto underPopulated = [&](const Colony& c) { return c.totalPopulation() < maxPopulation(p.r, p.st, c); };  // the domed maximum when domed
    // The one atmosphere the whole population breathes, if there is one.
    auto sharedAtmosphere = [&](const Colony& c) -> std::optional<std::string_view> {
        std::optional<std::string_view> one;
        for (const PopulationGroup& g : c.population) {
            if (g.millions <= 0) continue;
            const std::string_view a = atmosphereOf(g.race);
            if (one && !keysEqual(*one, a)) return std::nullopt;
            one = a;
        }
        return one;
    };
    for (VehicleId id : p.ownVehicles(Minister::Transports)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v) || p.info(v->design).aiType != "Population Transport") continue;
        // A ship with a Medical Bay gets no order: the routine picks a planet and then drops it.
        if (hasAbility(vehicleAbilities(p.r, p.st, *v), AbilityKind::MedicalBay)) continue;
        const int capacity = vehicleCargoCapacity(p.r, p.st, *v);
        if (capacity <= 0) continue;
        const Location here = v->location;
        std::vector<EmpireId> carried;
        for (const PopulationGroup& g : v->cargo.population)
            if (g.millions > 0) carried.push_back(g.race);
        std::vector<Order> orders;
        // The orders for a job at `where`: in another system only a move to
        // its sector (the job is planned again on arrival); in this system a
        // move, then the job; in this sector the job at once.
        auto job = [&](Location where, const Order& act) {
            if (where.system != here.system) {
                orders.push_back(moveOrder(where));
                return false;
            }
            if (where != here) orders.push_back(moveOrder(where));
            orders.push_back(act);
            return true;
        };
        // Deliver: the own colony with the lowest population below its
        // maximum, safe, nobody's drop target, where every carried race
        // breathes, or already domed and hosting one of them. Nothing aboard:
        // no delivery.
        auto deliver = [&]() {
            if (carried.empty()) return;
            const Colony* to = nullptr;
            for (const auto& c : p.st.colonies) {
                if (!c || c->owner != p.id || !underPopulated(*c) || !safe(*c) || dropTargets.contains(c->planet)) continue;
                const std::string_view atmosphere = p.st.galaxy.object(c->planet).atmosphere;
                const bool everyone = std::all_of(carried.begin(), carried.end(), [&](EmpireId r) { return keysEqual(atmosphereOf(r), atmosphere); });
                bool hosted = false;
                if (!breathable(p.st, *c))
                    for (const PopulationGroup& g : c->population)
                        hosted = hosted || (g.millions > 0 && std::find(carried.begin(), carried.end(), g.race) != carried.end());
                if (!everyone && !hosted) continue;
                if (!to || c->totalPopulation() < to->totalPopulation()) to = &*c;
            }
            if (!to) return;
            Order drop;
            drop.kind = OrderKind::DropCargo;
            drop.location = locationOf(p.st.galaxy, to->planet);
            drop.object = to->planet;
            drop.amount = -1;
            if (job(drop.location, drop)) dropTargets.insert(to->planet);
        };
        // Load: the nearest safe own colony with at least 1000M whose own
        // atmosphere is wanted and whose whole population breathes one same
        // atmosphere. An atmosphere is wanted when it is that of an
        // under-populated safe colony or, for a domed one, the one all its
        // races breathe. The test reads the source planet's atmosphere.
        // Returns whether the source is in the transport's own sector.
        auto load = [&]() {
            std::vector<std::string_view> wanted;
            for (const auto& c : p.st.colonies) {
                if (!c || c->owner != p.id || !underPopulated(*c) || !safe(*c)) continue;
                if (breathable(p.st, *c)) wanted.push_back(p.st.galaxy.object(c->planet).atmosphere);
                else if (const auto a = sharedAtmosphere(*c)) wanted.push_back(*a);
            }
            const auto source = nearestColony(p, here.system, [&](const Colony& c) {
                if (!safe(c) || c.totalPopulation() < 1000 || !sharedAtmosphere(c)) return false;
                const std::string_view own = p.st.galaxy.object(c.planet).atmosphere;
                return std::any_of(wanted.begin(), wanted.end(), [&](std::string_view a) { return keysEqual(a, own); });
            });
            if (!source) return false;
            Order take;
            take.kind = OrderKind::LoadCargo;
            take.location = locationOf(p.st.galaxy, *source);
            take.amount = -1;
            job(take.location, take);
            return take.location == here;
        };
        if (!carried.empty() && cargoSpaceUsed(p.r, p.st, v->cargo) * 2 > capacity) deliver();
        else if (load()) deliver();
        if (orders.empty()) orders = resupplyOrders(p, here);
        p.setOrders(id, std::move(orders));
    }
}


// Space Yard Ships (spec 05 §7.5, confirmed: binary): own yard ships with
// normal status, movement left (what the last movement left them when the
// ministers act) and no orders, in a fleet or not. One without a working yard
// part gets the resupply orders. Otherwise it seeks the nearest own vehicle
// that has a destroyed part, a maximum movement of at most 2 and no own yard
// in its sector, and waits when already there; with no such vehicle it gets
// the resupply orders. An own yard is a colony with a Space Yard facility or
// an uncloaked ship with a working yard, the yard ship itself included, so a
// vehicle it has reached is no longer a target.
void planSpaceYardShips(Planner& p) {
    if (!p.on(Minister::SpaceYardShips)) return;
    auto yardAt = [&](Location where) {
        for (const auto& c : p.st.colonies)
            if (c && c->owner == p.id && locationOf(p.st.galaxy, c->planet) == where && colonyHasSpaceYard(p.r, *c)) return true;
        for (const Vehicle& o : p.st.vehicles)
            if (o.owner == p.id && o.count > 0 && o.location == where && o.status != VehicleStatus::Cloaked && vehicleHasSpaceYard(p.r, p.st, o))
                return true;
        return false;
    };
    for (VehicleId id : p.ownVehicles(Minister::SpaceYardShips)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || p.busy.contains(id) || v->status != VehicleStatus::Normal || v->movement <= 0 || !v->orders.empty()) continue;
        if (p.info(v->design).role != Role::YardShip) continue;
        if (!vehicleHasSpaceYard(p.r, p.st, *v)) {
            p.setOrders(id, resupplyOrders(p, v->location));
            continue;
        }
        std::vector<Location> goals;
        for (const Vehicle& o : p.st.vehicles) {
            if (o.id == id || o.owner != p.id || o.count <= 0 || isUnitType(vehicleType(p.r, p.st, o))) continue;
            if (damagedComponents(p.r, p.st, o) == 0 || vehicleMaxMovement(p.r, p.st, o) > 2 || yardAt(o.location)) continue;
            goals.push_back(o.location);
        }
        const auto near = nearestByTravel(p, v->location, goals);
        if (!near) {
            p.setOrders(id, resupplyOrders(p, v->location));
            continue;
        }
        const Location at = goals[near->goal];
        p.setOrders(id, at == v->location ? std::vector<Order>{} : std::vector<Order>{moveOrder(at)});
    }
}

namespace {

Order stellarOrder(StellarAction action, ObjectId object, Location where) {
    Order o;
    o.kind = OrderKind::StellarManipulation;
    o.amount = static_cast<int>(action);
    o.object = object;
    o.location = where;
    return o;
}

bool hasStar(const GameState& s, SystemId sys) {
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (s.galaxy.object(o).kind == ObjectKind::Star || s.galaxy.object(o).kind == ObjectKind::DestroyedStar) return true;
    return false;
}

// A planet's PlanetSize record number: its position in PlanetSize.txt, from 1
// (spec 01 §9 Destroy Planet).
int64_t sizeRecordNumber(const Rules& r, const SpaceObject& obj) {
    const auto& sizes = r.data().planetSizes;
    for (size_t i = 0; i < sizes.size(); ++i)
        if (keysEqual(sizes[i].physicalType, "Planet") && keysEqual(sizes[i].name, obj.size)) return static_cast<int64_t>(i) + 1;
    for (size_t i = 0; i < sizes.size(); ++i)
        if (keysEqual(sizes[i].name, obj.size)) return static_cast<int64_t>(i) + 1;
    return 0;
}

bool isEdge(Sector s) { return s.x == 0 || s.y == 0 || s.x == kSystemSize - 1 || s.y == kSystemSize - 1; }

// Any space object in the sector.
bool objectAt(const GameState& s, Location where) {
    for (ObjectId o : s.galaxy.system(where.system).objects)
        if (s.galaxy.object(o).sector == where.sector) return true;
    return false;
}

} // namespace

// Stellar Manipulation (spec 05 §7.5, confirmed: binary): idle own ships of
// the matching design type that are not in a fleet. "Nearest" means nearest
// to our home system, by jumps over every link.
void planStellarManipulation(Planner& p) {
    if (!p.on(Minister::StellarManipulation)) return;
    const Empire& e = p.emp();
    const auto& jumps = p.sit.homeJumps;
    auto nearer = [&](SystemId a, SystemId b) { return jumps[a.index()] < jumps[b.index()]; };
    std::vector<uint8_t> ourColony(p.st.galaxy.systems.size(), 0), hostileColony(p.st.galaxy.systems.size(), 0);
    for (const auto& c : p.st.colonies) {
        if (!c) continue;
        const size_t sys = p.st.galaxy.object(c->planet).system.index();
        if (c->owner == p.id) ourColony[sys] = 1;
        else if (c->owner.valid() && c->owner.index() < p.st.empires.size() && p.st.empire(c->owner).alive && hostileTo(e, c->owner)) hostileColony[sys] = 1;
    }
    auto armedHostileStrengthAt = [&](Location where) {
        for (const Vehicle& o : p.st.vehicles)
            if (o.count > 0 && o.location == where && hostileTo(e, o.owner) && vehicleRating(p.r, p.st, o) > 0) return true;
        return false;
    };
    auto stopsPlanetDestroyer = [&](Location where) {
        for (ObjectId o : planetsAt(p.st, where))
            if (const Colony* c = p.st.colony(o); c && hasAbility(colonyAbilities(p.r, p.st, *c), AbilityKind::StopPlanetDestroyer)) return true;
        for (const Vehicle& o : p.st.vehicles)
            if (o.count > 0 && o.location == where && hasAbility(vehicleAbilities(p.r, p.st, o), AbilityKind::StopPlanetDestroyer)) return true;
        return false;
    };
    std::set<ObjectId> headedFor;  // our ships' manipulation targets
    for (const Vehicle& v : p.st.vehicles)
        if (v.owner == p.id)
            for (const Order& o : v.orders)
                if (o.kind == OrderKind::StellarManipulation && o.object.valid()) headedFor.insert(o.object);

    for (VehicleId id : p.ownVehicles(Minister::StellarManipulation)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v) || p.info(v->design).role != Role::Stellar) continue;
        const std::string type = p.info(v->design).aiType;
        const Location here = v->location;
        auto goAndDo = [&](Location where, const Order& act) {
            std::vector<Order> orders;
            if (where != here) orders.push_back(moveOrder(where));
            orders.push_back(act);
            p.setOrders(id, std::move(orders));
        };
        // The nearest object of a kind in explored systems that passes `accept`.
        auto nearestObject = [&](ObjectKind kind, auto&& accept) -> std::optional<ObjectId> {
            std::optional<ObjectId> best;
            for (size_t i = 0; i < p.st.galaxy.systems.size(); ++i) {
                if (!p.explored(SystemId{i})) continue;
                for (ObjectId o : p.st.galaxy.system(SystemId{i}).objects) {
                    if (p.st.galaxy.object(o).kind != kind || !accept(o)) continue;
                    if (!best || nearer(SystemId{i}, p.st.galaxy.object(*best).system)) best = o;
                }
            }
            return best;
        };
        if (type == "Open Warp Point") {
            // Only when no free exploration frontier point is left.
            if (!p.sit.freeFrontier.empty()) continue;
            const int64_t range = bestValue1(vehicleAbilities(p.r, p.st, *v), AbilityKind::OpenWarpPointDistance);
            std::optional<std::pair<SystemId, SystemId>> pick;  // source, target
            std::tuple<int, int> pickKey{};
            for (size_t i = 0; i < p.st.galaxy.systems.size(); ++i) {
                const SystemId from{i};
                if (!p.explored(from) || static_cast<int>(p.st.galaxy.warpPoints(from).size()) >= kMaxWarpPoints) continue;
                const std::vector<SystemId> linked = p.st.galaxy.neighbors(from);
                std::vector<SystemId> targets;
                for (size_t j = 0; j < p.st.galaxy.systems.size(); ++j) {
                    const SystemId to{j};
                    if (to == from || p.explored(to) || static_cast<int>(p.st.galaxy.warpPoints(to).size()) >= kMaxWarpPoints) continue;
                    if (std::find(linked.begin(), linked.end(), to) != linked.end()) continue;
                    if (galaxyDistance(p.st.galaxy.system(from).position, p.st.galaxy.system(to).position) > range) continue;
                    targets.push_back(to);
                }
                if (targets.empty()) continue;
                const SystemId to = targets[static_cast<size_t>(p.rng.below(targets.size()))];  // one random target per source
                int colonies = 0;
                for (const auto& c : p.st.colonies) colonies += c && c->owner == p.id && p.st.galaxy.object(c->planet).system == from;
                const std::tuple<int, int> key{colonies, jumps[i]};
                if (!pick || key < pickKey) {
                    pick = std::pair{from, to};
                    pickKey = key;
                }
            }
            if (!pick) {
                p.setOrders(id, resupplyOrders(p, here));
                continue;
            }
            // A random empty edge sector of the source (up to 100 tries; the last draw otherwise, inferred).
            std::vector<Sector> edges;
            for (int y = 0; y < kSystemSize; ++y)
                for (int x = 0; x < kSystemSize; ++x)
                    if (isEdge(Sector{x, y})) edges.push_back(Sector{x, y});
            Sector sector;
            for (int tries = 0; tries < 100; ++tries) {
                sector = edges[static_cast<size_t>(p.rng.below(edges.size()))];
                if (!objectAt(p.st, {pick->first, sector})) break;
            }
            goAndDo({pick->first, sector}, stellarOrder(StellarAction::OpenWarpPoint, {}, {pick->second, Sector{kSystemCenter, kSystemCenter}}));
        } else if (type == "Close Warp Point") {
            // A random warp point from a system with one of our colonies into a
            // system where we have no ship, base or colony but see a hostile empire
            // (one of its vehicles we see, or its colony in an explored system; inferred).
            std::vector<ObjectId> points;
            for (size_t i = 0; i < p.st.galaxy.systems.size(); ++i) {
                if (!ourColony[i]) continue;
                for (ObjectId wp : p.st.galaxy.warpPoints(SystemId{i})) {
                    const SpaceObject& obj = p.st.galaxy.object(wp);
                    if (!obj.destination.valid()) continue;
                    const SystemId far = p.st.galaxy.object(obj.destination).system;
                    if (ourColony[far.index()]) continue;
                    bool ours = false, seen = false;
                    for (const Vehicle& o : p.st.vehicles)
                        if (o.count > 0 && o.location.system == far && o.owner == p.id && !isUnitType(vehicleType(p.r, p.st, o))) ours = true;
                    for (VehicleId vid : e.knowledge.visibleVehicles)
                        if (const Vehicle* o = p.st.vehicle(vid); o && o->location.system == far && hostileTo(e, o->owner)) seen = true;
                    if (p.explored(far) && hostileColony[far.index()]) seen = true;
                    if (!ours && seen) points.push_back(wp);
                }
            }
            if (points.empty()) continue;
            const ObjectId wp = points[static_cast<size_t>(p.rng.below(points.size()))];
            goAndDo(locationOf(p.st.galaxy, wp), stellarOrder(StellarAction::CloseWarpPoint, wp, locationOf(p.st.galaxy, wp)));
        } else if (type == "Create Planet") {
            // The uncolonized asteroid field nearest home, in an explored system
            // with a star, that no other ship of ours is heading for; ties random.
            std::vector<ObjectId> best;
            for (size_t i = 0; i < p.st.galaxy.systems.size(); ++i) {
                if (!p.explored(SystemId{i}) || !hasStar(p.st, SystemId{i})) continue;
                for (ObjectId o : p.st.galaxy.system(SystemId{i}).objects) {
                    if (p.st.galaxy.object(o).kind != ObjectKind::Asteroids || p.st.colony(o) || headedFor.contains(o)) continue;
                    if (!best.empty() && jumps[i] > jumps[p.st.galaxy.object(best.front()).system.index()]) continue;
                    if (!best.empty() && jumps[i] < jumps[p.st.galaxy.object(best.front()).system.index()]) best.clear();
                    best.push_back(o);
                }
            }
            if (best.empty()) continue;
            const ObjectId field = best[static_cast<size_t>(p.rng.below(best.size()))];
            headedFor.insert(field);
            goAndDo(locationOf(p.st.galaxy, field), stellarOrder(StellarAction::CreatePlanet, field, locationOf(p.st.galaxy, field)));
        } else if (type == "Destroy Planet") {
            // The nearest planet colonized by a hostile empire, within the ship's
            // size limit, with no armed hostile strength in its sector and no Stop Planet Destroyer.
            const int64_t limit = bestValue1(vehicleAbilities(p.r, p.st, *v), AbilityKind::DestroyPlanetSize);
            const auto target = nearestObject(ObjectKind::Planet, [&](ObjectId o) {
                const Colony* c = p.st.colony(o);
                if (!c || !c->owner.valid() || c->owner == p.id || !hostileTo(e, c->owner)) return false;
                const Location at = locationOf(p.st.galaxy, o);
                return sizeRecordNumber(p.r, p.st.galaxy.object(o)) <= limit && !armedHostileStrengthAt(at) && !stopsPlanetDestroyer(at);
            });
            if (target) goAndDo(locationOf(p.st.galaxy, *target), stellarOrder(StellarAction::DestroyPlanet, *target, locationOf(p.st.galaxy, *target)));
        } else if (type == "Create Star") {
            // The nearest explored system without a star whose centre sector
            // holds no visible, armed, non-mothballed hostile.
            std::optional<SystemId> best;
            for (size_t i = 0; i < p.st.galaxy.systems.size(); ++i) {
                const SystemId sys{i};
                if (!p.explored(sys) || hasStar(p.st, sys) || armedHostileSeenAt(p, {sys, Sector{kSystemCenter, kSystemCenter}})) continue;
                if (!best || nearer(sys, *best)) best = sys;
            }
            if (best) {
                const Location centre{*best, Sector{kSystemCenter, kSystemCenter}};
                goAndDo(centre, stellarOrder(StellarAction::CreateStar, {}, centre));
            }
        } else if (type == "Destroy Star" || type == "Create Black Hole" || type == "Create Nebulae") {
            // The nearest star in an explored system where we have no colony and a living hostile empire has one.
            const StellarAction action = type == "Destroy Star" ? StellarAction::DestroyStar
                                         : type == "Create Black Hole" ? StellarAction::CreateBlackHole
                                                                       : StellarAction::CreateNebulae;
            const auto star = nearestObject(ObjectKind::Star, [&](ObjectId o) {
                const size_t sys = p.st.galaxy.object(o).system.index();
                return !ourColony[sys] && hostileColony[sys];
            });
            if (star) goAndDo(locationOf(p.st.galaxy, *star), stellarOrder(action, *star, locationOf(p.st.galaxy, *star)));
        } else if (type == "Destroy Storm") {
            // The nearest storm whose sector holds no visible armed hostile.
            const auto storm = nearestObject(ObjectKind::Storm, [&](ObjectId o) { return !armedHostileSeenAt(p, locationOf(p.st.galaxy, o)); });
            if (storm) goAndDo(locationOf(p.st.galaxy, *storm), stellarOrder(StellarAction::DestroyStorm, *storm, locationOf(p.st.galaxy, *storm)));
        } else if (type == "Destroy Black Hole" || type == "Destroy Nebulae") {
            // The explored system of that kind nearest by jumps: the order at once
            // when the ship is inside, otherwise a move to a fixed sector of it
            // (the top-left corner, inferred).
            const bool hole = type == "Destroy Black Hole";
            std::optional<SystemId> best;
            for (size_t i = 0; i < p.st.galaxy.systems.size(); ++i) {
                const SystemId sys{i};
                if (!p.explored(sys) || !keysEqual(p.st.galaxy.system(sys).physicalType, hole ? "Black Hole" : "Nebulae")) continue;
                if (!best || nearer(sys, *best)) best = sys;
            }
            if (!best) continue;
            if (here.system == *best)
                p.setOrders(id, {stellarOrder(hole ? StellarAction::DestroyBlackHole : StellarAction::DestroyNebulae, {}, here)});
            else
                p.setOrders(id, {moveOrder({*best, Sector{0, 0}})});
        }
        // Create Storm is never used.
    }
}

namespace {

using ruleset::VehicleType;

// Units of one kind the empire holds (spec 05 §7.5): everywhere, in space and
// in every planet's and vehicle's cargo, and those stored in any cargo.
struct UnitTotals {
    int64_t all = 0;
    int64_t stored = 0;
};
UnitTotals unitTotals(Planner& p, VehicleType kind) {
    UnitTotals t;
    auto ofKind = [&](DesignId d) { return d.index() < p.st.designs.size() && p.info(d).stats.vehicleType == kind; };
    for (const Vehicle& v : p.st.vehicles) {
        if (v.owner != p.id) continue;
        for (const UnitStack& u : groupStacks(v))
            if (ofKind(u.design)) t.all += u.count;
        for (const UnitStack& u : v.cargo.units)
            if (ofKind(u.design)) {
                t.all += u.count;
                t.stored += u.count;
            }
    }
    for (const auto& c : p.st.colonies)
        if (c && c->owner == p.id)
            for (const UnitStack& u : c->cargo.units)
                if (ofKind(u.design)) {
                    t.all += u.count;
                    t.stored += u.count;
                }
    return t;
}

// The excess above the kept share: when total > 0 and stored / total × 100
// exceeds the kept percent, trunc((stored / total × 100 − kept %) / 100 × total).
int64_t excessOf(const UnitTotals& t, int keptPercent) {
    if (t.all <= 0) return 0;
    const xmath::Ext share = xmath::Ext(t.stored) / xmath::Ext(t.all) * xmath::Ext(100);
    const xmath::Ext kept(int64_t{keptPercent});
    if (!(share > kept)) return 0;
    return ((share - kept) / xmath::Ext(100) * xmath::Ext(t.all)).trunc();
}

// Launch Units Remotely orders for every stack of a kind in a cargo: each
// launches all it may, with no target (spec 03 §12, spec 05 §7.5).
int64_t launchAll(Planner& p, const Cargo& cargo, VehicleType kind, std::vector<Order>& orders) {
    int64_t n = 0;
    for (const UnitStack& u : cargo.units) {
        if (u.count <= 0 || p.info(u.design).stats.vehicleType != kind) continue;
        n += u.count;
        const bool already = std::any_of(orders.begin(), orders.end(), [&](const Order& o) { return o.kind == OrderKind::LaunchUnits && o.design == u.design; });
        if (already) continue;
        Order o;
        o.kind = OrderKind::LaunchUnits;
        o.design = u.design;
        o.amount = -1;
        orders.push_back(o);
    }
    return n;
}

bool holds(Planner& p, const Cargo& cargo, auto&& accept) {
    return std::any_of(cargo.units.begin(), cargo.units.end(), [&](const UnitStack& u) { return u.count > 0 && accept(p.info(u.design)); });
}

// The targets of the drones (spec 05 §7.5): ships inside our territory of
// empires at War with us, and those empires' planets among the attack candidates.
std::vector<Order> antiShipTargets(const Planner& p) {
    std::vector<Order> out;
    for (const Threat& t : p.sit.enemyInTerritory) {
        const Vehicle* v = t.vehicle.valid() ? p.st.vehicle(t.vehicle) : nullptr;
        if (v && p.atWarWith(t.owner) && !isUnitType(vehicleType(p.r, p.st, *v))) out.push_back(attackVehicle(*v));
    }
    return out;
}
std::vector<Order> antiPlanetTargets(const Planner& p) {
    std::vector<Order> out;
    for (const Candidate& c : p.sit.candidates)
        if (p.atWarWith(c.owner)) out.push_back(attackPlanet(p.st, c.planet));
    return out;
}

// Spec 05 §7.5 (confirmed: binary): satellites and drones in planet cargo
// above the empire's kept shares are launched, colony by colony in planet
// order, each colony launching its whole stock (planet orders, spec 03 §12).
void launchFromPlanets(Planner& p) {
    std::vector<ObjectId> planets;
    for (const auto& c : p.st.colonies)
        if (c && !c->cargo.units.empty() && p.controlsColony(*c, Minister::MinesSatellitesDrones)) planets.push_back(c->planet);
    if (planets.empty()) return;
    const SettingsTable& set = p.prof.settings;
    std::map<ObjectId, std::vector<Order>> orders;

    // Satellites: while the excess is at least 1 each colony holding them
    // launches all, the excess dropping by its whole satellite cargo; once it
    // is used up, colonies holding a Recon Satellite still launch.
    if (int64_t excess = excessOf(unitTotals(p, VehicleType::Satellite), set.satellitesKeptPercent); excess > 0)
        for (ObjectId planet : planets) {
            const Cargo& cargo = p.st.colony(planet)->cargo;
            if (!holds(p, cargo, [](const DesignInfo& di) { return di.stats.vehicleType == VehicleType::Satellite; })) continue;
            const bool recon = holds(p, cargo, [](const DesignInfo& di) { return di.aiType == "Recon Satellite"; });
            if (excess < 1 && !recon) continue;
            excess -= launchAll(p, cargo, VehicleType::Satellite, orders[planet]);
        }

    // Drones: each half gets excess div 2, so an odd one stays; the ship half
    // is capped at the ship targets × drones per target, the planet half at
    // the planet targets × drones per target.
    const int64_t half = excessOf(unitTotals(p, VehicleType::Drone), set.dronesKeptPercent) / 2;
    int64_t shipHalf = std::min<int64_t>(half, static_cast<int64_t>(antiShipTargets(p).size()) * std::max(0, set.antiShipDronesPerTarget));
    int64_t planetHalf = std::min<int64_t>(half, static_cast<int64_t>(antiPlanetTargets(p).size()) * std::max(0, set.antiPlanetDronesPerTarget));
    for (ObjectId planet : planets) {
        if (shipHalf <= 0) break;
        const Cargo& cargo = p.st.colony(planet)->cargo;
        if (holds(p, cargo, [](const DesignInfo& di) { return di.aiType == "Anti-Ship Drone"; }))
            shipHalf -= launchAll(p, cargo, VehicleType::Drone, orders[planet]);
    }
    for (ObjectId planet : planets) {
        if (planetHalf <= 0) break;
        const Cargo& cargo = p.st.colony(planet)->cargo;
        if (holds(p, cargo, [](const DesignInfo& di) { return di.aiType == "Anti-Planet Drone"; }))
            planetHalf -= launchAll(p, cargo, VehicleType::Drone, orders[planet]);
    }

    for (auto& [planet, list] : orders) {
        if (list.empty() || p.st.colony(planet)->orders == list) continue;
        p.emit(cmd::SetOrders{.orders = std::move(list), .planet = planet});
    }
}

// Idle drones in space within the target distance are sent after the
// targets, per-target times (spec 05 §7.5).
void sendDrones(Planner& p) {
    const SettingsTable& set = p.prof.settings;
    std::map<SystemId, std::vector<int>> jumps;
    auto jumpsTo = [&](SystemId from, SystemId to) {
        auto it = jumps.find(from);
        if (it == jumps.end()) it = jumps.emplace(from, p.jumpsFrom(from)).first;
        return it->second[to.index()];
    };
    for (const bool antiPlanet : {false, true}) {
        std::vector<VehicleId> drones;
        for (VehicleId id : p.ownVehicles(Minister::MinesSatellitesDrones)) {
            const Vehicle* v = p.st.vehicle(id);
            if (v && !v->fleet.valid() && p.idle(*v) && p.info(v->design).aiType == (antiPlanet ? "Anti-Planet Drone" : "Anti-Ship Drone"))
                drones.push_back(id);
        }
        const std::vector<Order> targets = antiPlanet ? antiPlanetTargets(p) : antiShipTargets(p);
        const int perTarget = antiPlanet ? set.antiPlanetDronesPerTarget : set.antiShipDronesPerTarget;
        const int range = antiPlanet ? set.antiPlanetDroneRange : set.antiShipDroneRange;
        for (const Order& target : targets) {
            int sent = 0;
            for (VehicleId id : drones) {
                if (sent >= perTarget) break;
                const Vehicle* v = p.st.vehicle(id);
                if (!v || p.busy.contains(id) || jumpsTo(v->location.system, target.location.system) > range) continue;
                if (p.setOrders(id, {target})) ++sent;
            }
        }
    }
}

// The warp-point sector a loaded mine or satellite layer lays at (spec 05
// §7.5, confirmed: binary).
std::optional<Location> layingSite(Planner& p, bool mines) {
    const Empire& e = p.emp();
    const size_t nSys = p.st.galaxy.systems.size();
    // Every other empire owning any object in each system.
    std::vector<std::vector<EmpireId>> others(nSys);
    auto note = [&](SystemId sys, EmpireId owner) {
        if (owner == p.id || !owner.valid() || sys.index() >= nSys) return;
        auto& list = others[sys.index()];
        if (std::find(list.begin(), list.end(), owner) == list.end()) list.push_back(owner);
    };
    for (const auto& c : p.st.colonies)
        if (c) note(p.st.galaxy.object(c->planet).system, c->owner);
    for (const Vehicle& v : p.st.vehicles)
        if (v.count > 0) note(v.location.system, v.owner);
    std::vector<uint8_t> colonySystem(nSys, 0);
    for (const auto& c : p.st.colonies)
        if (c && c->owner == p.id) colonySystem[p.st.galaxy.object(c->planet).system.index()] = 1;
    const int64_t cap = p.r.setting("Maximum Mines Per Player Per Sector", 100);  // the mine limit, for satellites too
    // The cap counts every unit group of ours at that sector (inferred).
    auto ourUnitsAt = [&](Location where) {
        int64_t n = 0;
        for (const Vehicle& v : p.st.vehicles)
            if (v.owner == p.id && v.count > 0 && v.location == where && isUnitType(vehicleType(p.r, p.st, v))) n += v.count;
        return n;
    };
    // Star-destroying designs among the enemy designs we have seen: those with Destroy Star (inferred).
    bool starDestroyers = false;
    for (const SeenDesign& seen : e.knowledge.seenDesigns)
        if (seen.design.index() < p.st.designs.size())
            for (const DesignEntry& en : p.st.design(seen.design).entries)
                starDestroyers = starDestroyers || hasAbility(p.r.componentAbilities(en.component), AbilityKind::DestroyStar);

    std::vector<std::pair<Location, int>> weighted;
    if (!(mines && starDestroyers))
        for (size_t i = 0; i < nSys; ++i) {
            if (!colonySystem[i]) continue;
            for (ObjectId wp : p.st.galaxy.warpPoints(SystemId{i})) {
                const SpaceObject& obj = p.st.galaxy.object(wp);
                if (!obj.destination.valid()) continue;
                const SystemId far = p.st.galaxy.object(obj.destination).system;
                if (others[far.index()].empty() || ourUnitsAt({far, obj.sector}) >= cap) continue;
                int weight = 1;  // the largest weight among the empires there (inferred)
                for (EmpireId x : others[far.index()]) {
                    if (mines) weight = std::max(weight, p.atWarWith(x) ? 7 : hostileTo(e, x) ? 4 : 1);
                    else weight = std::max(weight, hostileTo(e, x) ? 2 : 1);
                }
                weighted.emplace_back(obj.system == SystemId{i} ? Location{obj.system, obj.sector} : locationOf(p.st.galaxy, wp), weight);
            }
        }
    if (!weighted.empty()) {
        int64_t total = 0;
        for (const auto& [where, w] : weighted) total += w;
        int64_t roll = static_cast<int64_t>(p.rng.below(static_cast<uint64_t>(total)));
        for (const auto& [where, w] : weighted) {
            if (roll < w) return where;
            roll -= w;
        }
    }
    // Mine layers facing star destroyers: half the time a random star's
    // sector of our colony systems (inferred which stars).
    if (mines && starDestroyers && p.rng.percent(50)) {
        std::vector<Location> stars;
        for (size_t i = 0; i < nSys; ++i)
            if (colonySystem[i])
                for (ObjectId o : p.st.galaxy.system(SystemId{i}).objects)
                    if (p.st.galaxy.object(o).kind == ObjectKind::Star) stars.push_back(locationOf(p.st.galaxy, o));
        if (!stars.empty()) return stars[static_cast<size_t>(p.rng.below(stars.size()))];
    }
    // A random warp-point sector of a colony system where no other empire has any object.
    std::vector<Location> fallback;
    for (size_t i = 0; i < nSys; ++i)
        if (colonySystem[i] && others[i].empty())
            for (ObjectId wp : p.st.galaxy.warpPoints(SystemId{i})) fallback.push_back(locationOf(p.st.galaxy, wp));
    if (fallback.empty()) return std::nullopt;
    return fallback[static_cast<size_t>(p.rng.below(fallback.size()))];
}

} // namespace

void planMinesSatellitesDrones(Planner& p) {
    if (!p.on(Minister::MinesSatellitesDrones)) return;
    // Layers, launches and the orders to idle drones work only while the
    // empire has fewer placed units than the game's unit limit.
    const bool roomForUnits = movement::unitsInSpace(p.r, p.st, p.id) < p.st.options.maxUnitsPerPlayer;
    if (roomForUnits) launchFromPlanets(p);
    for (VehicleId id : p.ownVehicles(Minister::MinesSatellitesDrones)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || !p.idle(*v)) continue;
        const Role role = p.info(v->design).role;
        if (role == Role::Sweeper) {
            // Sweepers sweep the hostile mine fields in our territory.
            if (!p.sit.enemyNearby.empty())
                if (const Vehicle* field = p.st.vehicle(p.sit.enemyNearby.front().vehicle))
                    p.setOrders(id, {moveOrder(field->location), simpleOrder(OrderKind::SweepMines)});
            continue;
        }
        if ((role != Role::MineLayer && role != Role::SatelliteLayer) || !roomForUnits) continue;
        const bool mines = role == Role::MineLayer;
        const VehicleType kind = mines ? VehicleType::Mine : VehicleType::Satellite;
        const auto ofKind = [&](const DesignInfo& di) { return di.stats.vehicleType == kind; };
        if (!holds(p, v->cargo, ofKind)) {
            // Empty: load at the nearest own colony holding such units.
            std::optional<DesignId> unit;
            const auto depot = nearestColony(p, v->location.system, [&](const Colony& c) { return holds(p, c.cargo, ofKind); });
            if (!depot) continue;
            for (const UnitStack& u : p.st.colony(*depot)->cargo.units)
                if (!unit && u.count > 0 && ofKind(p.info(u.design))) unit = u.design;
            Order take;
            take.kind = OrderKind::LoadCargo;
            take.location = locationOf(p.st.galaxy, *depot);
            take.design = *unit;
            take.amount = -1;
            std::vector<Order> orders;
            if (v->location != take.location) orders.push_back(moveOrder(take.location));
            orders.push_back(take);
            p.setOrders(id, std::move(orders));
            continue;
        }
        const std::optional<Location> site = layingSite(p, mines);
        if (!site) continue;
        std::vector<Order> launches;
        launchAll(p, v->cargo, kind, launches);
        std::vector<Order> orders;
        if (v->location != *site) orders.push_back(moveOrder(*site));
        for (Order& o : launches) {
            o.location = *site;
            orders.push_back(o);
        }
        p.setOrders(id, std::move(orders));
    }
    if (roomForUnits) sendDrones(p);
}

namespace {

// Spec 05 §7.5 "Repair" (confirmed: binary): only a vehicle with a destroyed
// part, and then by design type.
bool needsRepair(Planner& p, const Vehicle& v) {
    const int destroyed = damagedComponents(p.r, p.st, v);
    if (destroyed == 0) return false;
    const std::string& type = p.info(v.design).aiType;
    const xmath::Ext parts(static_cast<int64_t>(p.st.design(v.design).entries.size()));
    if (type == "Attack Ship" || type == "Defense Ship") {
        if (vehicleRating(p.r, p.st, v) == 0 || destroyed > (kPoint3 * parts).round() || lacksOperatingPart(p, v)) return true;
        if (const Fleet* f = p.st.fleet(v.fleet)) {
            const int speed = vehicleMaxMovement(p.r, p.st, v);
            int fastest = 0;
            for (VehicleId m : f->members)
                if (const Vehicle* o = p.st.vehicle(m); o && o->count > 0) fastest = std::max(fastest, vehicleMaxMovement(p.r, p.st, *o));
            if (speed < 6 && speed < fastest - 1) return true;
        }
        return false;
    }
    static constexpr std::array<std::string_view, 6> kAlways{"Population Transport", "Mine Layer", "Mine Sweeper", "Boarding Ship",
                                                             "Space Yard Ship", "Cargo Transport"};
    if (p.info(v.design).role == Role::Stellar) return true;  // the warp-point and stellar-manipulation types
    for (std::string_view t : kAlways)
        if (type == t) return true;
    return destroyed > (kPoint25 * parts).round() || lacksOperatingPart(p, v);
}

// Every turn a vehicle that needs repair loses all its orders (Colonize
// included) and leaves its fleet. If it can move, it seeks the nearest of the
// empire's space yards by travel (a colony with a Space Yard facility, or an
// uncloaked ship with a working yard) and waits when already there or when a
// yard ship is within that ship's own speed; with no yard it keeps no orders.
// Details (confirmed: binary): mothballed vehicles are not skipped (their
// maximum movement is 0, so they get no destination); the yards are visited
// by system number, then in object order within the system, and on equal
// travel distance the first one found wins; a yard more than 9,998 away is
// never chosen; the vehicle itself is a yard when it has a working one, and
// then stays where it is.
void planRepair(Planner& p) {
    // The yards in visiting order: by system, then in object order (the
    // engine's object slots put planets before vehicles, spec 02 §13 Q52).
    struct Yard {
        Location at;
        VehicleId ship;   // invalid: a colony
        uint32_t order = 0;
    };
    std::vector<Yard> yards;
    for (const auto& c : p.st.colonies)
        if (c && c->owner == p.id && colonyHasSpaceYard(p.r, *c)) yards.push_back({locationOf(p.st.galaxy, c->planet), {}, c->planet.value});
    for (const Vehicle& o : p.st.vehicles)
        if (o.owner == p.id && o.count > 0 && o.status != VehicleStatus::Cloaked && vehicleHasSpaceYard(p.r, p.st, o))
            yards.push_back({o.location, o.id, static_cast<uint32_t>(p.st.galaxy.objects.size()) + o.slot});
    std::stable_sort(yards.begin(), yards.end(), [](const Yard& a, const Yard& b) {
        return std::tuple{a.at.system.value, a.order} < std::tuple{b.at.system.value, b.order};
    });
    for (VehicleId id : p.ownVehicles(Minister::Repair)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || isUnitType(p.info(v->design).stats.vehicleType) || !needsRepair(p, *v)) continue;
        if (v->fleet.valid()) p.emit(cmd::LeaveFleet{id});
        v = p.st.vehicle(id);
        std::vector<Order> orders;
        // A vehicle with a working yard of its own is its own nearest yard.
        if (vehicleMaxMovement(p.r, p.st, *v) > 0 && !vehicleHasSpaceYard(p.r, p.st, *v)) {
            std::vector<Location> goals;
            std::vector<int> yardShipSpeed;  // -1 for a colony
            for (const Yard& y : yards) {
                if (y.ship == id) continue;
                goals.push_back(y.at);
                const Vehicle* ship = y.ship.valid() ? p.st.vehicle(y.ship) : nullptr;
                yardShipSpeed.push_back(ship ? vehicleMaxMovement(p.r, p.st, *ship) : -1);
            }
            // Equal routes go to the earlier goal (findPathToNearest).
            if (const auto near = nearestByTravel(p, v->location, goals); near && near->path.length <= 9998) {
                const Location at = goals[near->goal];
                const int speed = yardShipSpeed[near->goal];
                const bool wait = at == v->location || (speed >= 0 && near->path.length <= speed);
                if (!wait) orders.push_back(moveOrder(at));
            }
        }
        p.setOrders(id, std::move(orders));
    }
}

// Spec 05 §7.5 "Resupply" (confirmed: binary): each system's supply distance
// is 13 × (1 + the warp jumps, over every link, to the nearest system holding
// one of our colonies with Supply Generation), or 999,999 when there is none.
void planResupply(Planner& p) {
    const size_t nSys = p.st.galaxy.systems.size();
    std::vector<int64_t> distance(nSys, 999'999);
    {
        std::vector<int> jumps(nSys, kUnreachable);
        std::deque<SystemId> queue;
        for (const auto& c : p.st.colonies) {
            if (!c || c->owner != p.id || !hasAbility(colonyAbilities(p.r, p.st, *c), AbilityKind::SupplyGeneration)) continue;
            const SystemId sys = p.st.galaxy.object(c->planet).system;
            if (jumps[sys.index()] == 0) continue;
            jumps[sys.index()] = 0;
            queue.push_back(sys);
        }
        while (!queue.empty()) {
            const SystemId at = queue.front();
            queue.pop_front();
            for (const Link& l : p.links[at.index()])
                if (jumps[l.to.index()] == kUnreachable) {
                    jumps[l.to.index()] = jumps[at.index()] + 1;
                    queue.push_back(l.to);
                }
        }
        for (size_t i = 0; i < nSys; ++i)
            if (jumps[i] != kUnreachable) distance[i] = 13 * (1 + int64_t{jumps[i]});
    }
    // A fleet not on unlimited supply goes when its total supply ÷ its total
    // supply cost per move is below the distance where it is; it takes the
    // orders its leader would get.
    for (const Fleet& f : p.st.fleets) {
        if (!p.controlsFleet(f, Minister::Resupply) || f.members.empty()) continue;
        const Vehicle* leader = p.st.vehicle(f.leader);
        if (!leader) continue;
        // On unlimited supply when none of its ships lacks it; the totals
        // count only the members without unlimited supply (confirmed: binary).
        int64_t supply = 0, cost = 0;
        bool unlimited = true;
        for (VehicleId m : f.members)
            if (const Vehicle* v = p.st.vehicle(m); v && v->count > 0 && !vehicleHasUnlimitedSupply(p.r, p.st, *v)) {
                unlimited = false;
                supply += v->supply;
                cost += movement::moveSupplyCost(p.r, p.st, *v);
            }
        if (unlimited || cost <= 0 || supply >= distance[leader->location.system.index()] * cost) continue;
        p.setFleetOrders(f.id, resupplyOrders(p, leader->location));
    }
    // A ship outside fleets goes on the same test or when its supply is 0;
    // colony ships and Destroy Star ships are exempt.
    for (VehicleId id : p.ownVehicles(Minister::Resupply)) {
        const Vehicle* v = p.st.vehicle(id);
        if (!v || v->fleet.valid() || v->status == VehicleStatus::Mothballed || isUnitType(p.info(v->design).stats.vehicleType)) continue;
        const DesignInfo& di = p.info(v->design);
        if (di.role == Role::Colonizer || di.aiType == "Destroy Star" || vehicleHasUnlimitedSupply(p.r, p.st, *v)) continue;
        const int64_t cost = movement::moveSupplyCost(p.r, p.st, *v);
        const bool send = v->supply <= 0 || (cost > 0 && v->supply < distance[v->location.system.index()] * cost);
        if (send) p.setOrders(id, resupplyOrders(p, v->location));
    }
}

} // namespace

void planRepairAndResupply(Planner& p, bool repair) {
    if (repair) planRepair(p);
    else planResupply(p);
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
    if (p.date % 10 != 0) return;  // the date the ministers see
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
