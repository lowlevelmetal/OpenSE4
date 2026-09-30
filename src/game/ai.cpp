#include "game/ai.hpp"

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/generate.hpp"
#include "game/query.hpp"
#include "game/score.hpp"
#include "game/setup.hpp"
#include "game/sight.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <format>
#include <tuple>

namespace opense4::game::ai {

namespace detail {

using datafile::keysEqual;

namespace {

uint64_t mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

bool designHas(const Rules& r, const Design& d, AbilityKind k) {
    for (const auto& en : d.entries)
        if (hasAbility(r.componentAbilities(en.component), k)) return true;
    return false;
}

// Which empires own any object in each system, seen or not (spec 05 §7.5
// colonization danger, confirmed: binary). Index: empire × systems + system.
std::vector<uint8_t> presence(const GameState& s) {
    const size_t nSys = s.galaxy.systems.size();
    std::vector<uint8_t> present(s.empires.size() * nSys, 0);
    for (const auto& c : s.colonies)
        if (c && c->owner.valid() && c->owner.index() < s.empires.size()) present[c->owner.index() * nSys + s.galaxy.object(c->planet).system.index()] = 1;
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.owner.valid() && v.owner.index() < s.empires.size() && v.location.system.index() < nSys)
            present[v.owner.index() * nSys + v.location.system.index()] = 1;
    return present;
}

int sizeRank(const Rules& r, const SpaceObject& planet) {
    static constexpr std::array<std::string_view, 5> kSizes{"Tiny", "Small", "Medium", "Large", "Huge"};
    std::string_view name = planet.size;
    if (const ruleset::PlanetSize* ps = planetSize(r, planet)) {
        if (ps->constructed) return 5;
        name = ps->stellarSize;
    }
    for (size_t i = 0; i < kSizes.size(); ++i)
        if (keysEqual(name, kSizes[i])) return static_cast<int>(i);
    return 2;
}

bool hasRuins(const SpaceObject& planet) {
    for (const auto& a : planet.abilities)
        if (auto k = parseAbilityKind(a.type); k == AbilityKind::AncientRuins || k == AbilityKind::AncientRuinsUnique) return true;
    return false;
}

int64_t bestDamage(const Rules& r, const DesignEntry& en) {
    int best = 0;
    for (int range = 1; range <= std::max(1, weaponMaxRange(r, en)); ++range) best = std::max(best, weaponDamageAtRange(r, en, range));
    return best;
}

// The planet's defence in an attack candidate's value (spec 05 §7.2,
// confirmed: binary), in tenths: its colony's Planet - Shield Generation
// total / 5, its population in millions div 100, and for every unit stack in
// its cargo count × that design's summed best weapon damage.
int64_t planetDefence(const Rules& r, const GameState& s, const Colony& c) {
    int64_t tenths = sumValue1(colonyAbilities(r, s, c), AbilityKind::PlanetShieldGeneration) * kStrengthScale / 5;
    tenths += c.totalPopulation() / 100 * kStrengthScale;
    for (const UnitStack& u : c.cargo.units)
        if (u.count > 0 && u.design.index() < s.designs.size()) tenths += int64_t{u.count} * designWeaponDamage(r, s.design(u.design)) * kStrengthScale;
    return tenths;
}

// The ratings (without the + 1) of every object in a sector that is not ours, whoever owns it.
int64_t foreignRatingsAt(const Rules& r, const GameState& s, EmpireId us, Location where) {
    int64_t sum = 0;
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.owner != us && v.location == where) sum += vehicleRating(r, s, v);
    return sum;
}

} // namespace

// ---- Situation ---------------------------------------------------------------------------------

bool hostileTo(const Empire& e, EmpireId other) {
    if (!other.valid() || other == e.id || other.index() >= e.relations.size()) return false;
    const Relation& rel = e.relation(other);
    return !rel.contact || treatyIsHostile(rel.treaty);
}

SystemId homeSystem(const GameState& s, EmpireId e) {
    SystemId first;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        if (c->homeworld) return s.galaxy.object(c->planet).system;
        if (!first.valid()) first = s.galaxy.object(c->planet).system;
    }
    return first;
}

bool notices(const GameState& s, EmpireId e, uint64_t object) {
    if (difficultyOf(s, e) != kDifficultyLow) return true;
    const uint64_t h = mix(mix(mix(s.seed ^ 0x4E6F74696365ull) ^ s.turn) ^ (uint64_t{e.value} << 32) ^ object);
    return h % 100 < 90;
}

int64_t designWeaponDamage(const Rules& r, const Design& d) {
    int64_t n = 0;
    for (const DesignEntry& en : d.entries)
        if (en.component < r.data().components.size() && r.component(en.component).isWeapon()) n += bestDamage(r, en);
    return n;
}

// Spec 05 §7.2 (confirmed: binary): W is the best damage at any range summed
// over the undamaged weapon parts, B the Boarding Attack ability; when W + B
// > 0 the rating is W + B + (Shield Generation + Phased Shield Generation +
// Planet - Shield Generation) / 2, the half kept; then every fighter stack in
// the cargo adds count × the fighter design's summed best weapon damage, even
// when W + B is 0. The abilities are the vehicle's ability list (undamaged
// parts, mounts applied; none while mothballed).
int64_t vehicleRating(const Rules& r, const GameState& s, const Vehicle& v) {
    if (isUnitType(vehicleType(r, s, v))) return kStrengthScale * std::max(0, v.count);  // a unit group: its number of units
    const Design& d = s.design(v.design);
    int64_t weapons = 0;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i) && r.component(d.entries[i].component).isWeapon()) weapons += bestDamage(r, d.entries[i]);
    const std::vector<ParsedAbility> ab = vehicleAbilities(r, s, v);
    const int64_t boarding = sumValue1(ab, AbilityKind::BoardingAttack);
    const int64_t shields = sumValue1(ab, AbilityKind::ShieldGeneration) + sumValue1(ab, AbilityKind::PhasedShieldGeneration) +
                            sumValue1(ab, AbilityKind::PlanetShieldGeneration);
    int64_t rating = 0;
    if (weapons + boarding > 0) rating = (weapons + boarding) * kStrengthScale + shields * kStrengthScale / 2;
    for (const UnitStack& u : v.cargo.units)
        if (u.count > 0 && u.design.index() < s.designs.size() && r.hull(s.design(u.design).hull).type == ruleset::VehicleType::Fighter)
            rating += int64_t{u.count} * designWeaponDamage(r, s.design(u.design)) * kStrengthScale;
    return rating;
}

std::vector<int> jumpsOver(const GameState& s, SystemId from) {
    std::vector<int> dist(s.galaxy.systems.size(), kUnreachable);
    if (!from.valid() || from.index() >= dist.size()) return dist;
    std::deque<SystemId> queue{from};
    dist[from.index()] = 0;
    while (!queue.empty()) {
        const SystemId at = queue.front();
        queue.pop_front();
        for (SystemId to : s.galaxy.neighbors(at)) {
            if (to.index() >= dist.size() || dist[to.index()] != kUnreachable) continue;
            dist[to.index()] = dist[at.index()] + 1;
            queue.push_back(to);
        }
    }
    return dist;
}

std::vector<SystemId> computeTerritory(const GameState& s, EmpireId id) {
    const Empire& e = s.empire(id);
    const size_t nSys = s.galaxy.systems.size();
    std::vector<uint8_t> mine(nSys, 0);
    if (e.kind == PlayerKind::Human) {
        for (SystemId sys : e.claimedSystems)
            if (sys.index() < nSys) mine[sys.index()] = 1;
        if (const SystemId home = homeSystem(s, id); home.valid()) mine[home.index()] = 1;
    } else {
        std::vector<uint8_t> colonySys(nSys, 0);
        for (const auto& c : s.colonies)
            if (c && c->owner == id) colonySys[s.galaxy.object(c->planet).system.index()] = 1;
        for (size_t i = 0; i < nSys; ++i) {
            if (!colonySys[i]) continue;
            mine[i] = 1;
            if (e.kind == PlayerKind::Neutral) continue;
            // One warp jump over every link of the map, known or not (spec 05 §7.2).
            for (SystemId nb : s.galaxy.neighbors(SystemId{i}))
                if (nb.index() < nSys) mine[nb.index()] = 1;
        }
        // Not another computer player's home system, nor a system we agreed to leave.
        for (const Empire& other : s.empires)
            if (other.id != id && other.alive && other.kind != PlayerKind::Human)
                if (const SystemId home = homeSystem(s, other.id); home.valid()) mine[home.index()] = 0;
        for (SystemId sys : e.aiMemory.avoid)
            if (sys.index() < nSys) mine[sys.index()] = 0;
    }
    std::vector<SystemId> out;
    for (size_t i = 0; i < nSys; ++i)
        if (mine[i]) out.push_back(SystemId{i});
    return out;
}

void sortDefendEntries(std::vector<DefendEntry>& entries, bool weakestFirst) {
    std::stable_sort(entries.begin(), entries.end(), [&](const DefendEntry& a, const DefendEntry& b) {
        if (a.jumps != b.jumps) return a.jumps < b.jumps;
        if (a.ourMaxPopulation != b.ourMaxPopulation) return a.ourMaxPopulation > b.ourMaxPopulation;
        if (a.planetSector != b.planetSector) return a.planetSector;
        if (a.planetSector && a.threat != b.threat) return a.threat < b.threat;  // the weaker threat first among planet sectors
        if (a.threat != b.threat) return weakestFirst ? a.threat < b.threat : a.threat > b.threat;
        return false;
    });
}

Situation assess(const Rules& r, const GameState& s, EmpireId id, const AiProfile& prof) {
    const Empire& e = s.empire(id);
    const size_t nSys = s.galaxy.systems.size();
    const size_t nEmp = s.empires.size();
    const bool neutral = e.kind == PlayerKind::Neutral;
    Situation sit;
    sit.home = homeSystem(s, id);
    if (!sit.home.valid())
        for (const Vehicle& v : s.vehicles)
            if (v.owner == id) {
                sit.home = v.location.system;
                break;
            }
    auto considered = [&](SystemId sys) { return !neutral || sys == sit.home; };

    sit.territory.assign(nSys, 0);
    for (SystemId sys : computeTerritory(s, id)) sit.territory[sys.index()] = 1;

    // Strength of every empire per system, counting everything it owns, in
    // tenths: each object its rating + 1, planets rating 0 (spec 05 §7.2).
    std::vector<int64_t> strength(nEmp * nSys, 0);
    for (const Vehicle& v : s.vehicles) {
        if (!v.owner.valid() || v.owner.index() >= nEmp || v.location.system.index() >= nSys || v.count <= 0) continue;
        strength[v.owner.index() * nSys + v.location.system.index()] += vehicleRating(r, s, v) + kStrengthScale;
    }
    for (const auto& c : s.colonies)
        if (c && c->owner.valid() && c->owner.index() < nEmp)
            strength[c->owner.index() * nSys + s.galaxy.object(c->planet).system.index()] += kStrengthScale;
    sit.ours.assign(nSys, 0);
    sit.hostile.assign(nSys, 0);
    for (size_t i = 0; i < nSys; ++i) sit.ours[i] = strength[id.index() * nSys + i];
    for (const Empire& x : s.empires) {
        if (x.id == id || !x.alive || !e.relation(x.id).contact || !treatyIsHostile(e.relation(x.id).treaty)) continue;
        for (size_t i = 0; i < nSys; ++i) sit.hostile[i] += strength[x.id.index() * nSys + i];
    }

    // Jumps from home over every link, known or not (spec 05 §7.2).
    sit.homeJumps = jumpsOver(s, sit.home);
    auto jumps = [&](SystemId sys) { return sit.homeJumps[sys.index()]; };

    // Enemy in territory and enemy nearby.
    for (VehicleId vid : e.knowledge.visibleVehicles) {
        const Vehicle* v = s.vehicle(vid);
        if (!v || v->count <= 0 || !hostileTo(e, v->owner)) continue;
        const SystemId sys = v->location.system;
        if (sys.index() >= nSys || !sit.territory[sys.index()] || !considered(sys) || !notices(s, id, vehicleKey(vid))) continue;
        Threat t{sys, v->owner, vid, {}};
        if (vehicleType(r, s, *v) == ruleset::VehicleType::Mine) sit.enemyNearby.push_back(t);
        else sit.enemyInTerritory.push_back(t);
    }
    for (const auto& c : s.colonies) {
        if (!c || !hostileTo(e, c->owner) || c->totalPopulation() <= 0) continue;
        const SystemId sys = s.galaxy.object(c->planet).system;
        if (!sit.territory[sys.index()] || !e.hasExplored(sys) || !considered(sys) || !notices(s, id, planetKey(c->planet))) continue;
        sit.enemyInTerritory.push_back({sys, c->owner, {}, c->planet});
    }

    // Attack candidates: other empires' planets in systems we have explored,
    // valued by the foreign ratings in the planet's sector plus its defence.
    for (const auto& c : s.colonies) {
        if (!c || c->owner == id || !c->owner.valid() || c->owner.index() >= nEmp) continue;
        const SystemId sys = s.galaxy.object(c->planet).system;
        if (!e.hasExplored(sys) || !considered(sys)) continue;
        if (hostileTo(e, c->owner) && !notices(s, id, planetKey(c->planet))) continue;
        const int64_t value = foreignRatingsAt(r, s, id, locationOf(s.galaxy, c->planet)) + planetDefence(r, s, *c);
        sit.candidates.push_back({c->planet, sys, c->owner, jumps(sys), e.relation(c->owner).anger, value});
    }
    std::sort(sit.candidates.begin(), sit.candidates.end(), [](const Candidate& a, const Candidate& b) {
        return std::tuple(a.jumps, -a.anger, -a.value, a.planet) < std::tuple(b.jumps, -b.anger, -b.value, b.planet);
    });

    // The exploration frontier: warp points in explored systems into unexplored space.
    std::set<ObjectId> headed;
    std::set<Location> headedTo;
    auto note = [&](const std::vector<Order>& orders) {
        for (const Order& o : orders) {
            if ((o.kind == OrderKind::Warp || o.kind == OrderKind::Explore) && o.object.valid()) headed.insert(o.object);
            if (o.kind == OrderKind::MoveTo) headedTo.insert(o.location);
        }
    };
    for (const Vehicle& v : s.vehicles)
        if (v.owner == id) note(v.orders);
    for (const Fleet& f : s.fleets)
        if (f.owner == id) note(f.orders);
    for (size_t i = 0; i < nSys && !neutral; ++i) {
        if (!e.hasExplored(SystemId{i})) continue;
        for (ObjectId wp : s.galaxy.system(SystemId{i}).objects) {
            const SpaceObject& o = s.galaxy.object(wp);
            if (o.kind != ObjectKind::WarpPoint || !o.destination.valid()) continue;
            const bool leadsOut = !sight::knowsWarpLink(s, id, wp) || !e.hasExplored(s.galaxy.object(o.destination).system);
            if (!leadsOut) continue;
            sit.frontier.push_back(wp);
            if (sit.territory[i]) sit.bordersUnexplored = true;
            if (!headed.contains(wp) && !headedTo.contains(locationOf(s.galaxy, wp))) sit.freeFrontier.push_back(wp);
        }
    }

    // The defend list (spec 05 §7.2, confirmed: binary): the enemy-in-territory
    // entries one per (system, sector, owner), each with its threat.
    for (const Threat& t : sit.enemyInTerritory) {
        const Vehicle* v = t.vehicle.valid() ? s.vehicle(t.vehicle) : nullptr;
        const Location where = v ? v->location : locationOf(s.galaxy, t.planet);
        auto it = std::find_if(sit.defendEntries.begin(), sit.defendEntries.end(),
                               [&](const DefendEntry& d) { return d.where == where && d.owner == t.owner; });
        if (it == sit.defendEntries.end()) {
            DefendEntry d;
            d.where = where;
            d.owner = t.owner;
            d.jumps = jumps(where.system);
            for (ObjectId o : s.galaxy.system(where.system).objects) {
                const SpaceObject& obj = s.galaxy.object(o);
                if (obj.sector != where.sector || obj.kind != ObjectKind::Planet) continue;
                d.planetSector = true;
                if (const Colony* c = s.colony(o); c && c->owner == id) d.ourMaxPopulation += maxPopulation(r, s, *c);
            }
            sit.defendEntries.push_back(d);
            it = sit.defendEntries.end() - 1;
        }
        // Each noticed object adds its rating + 1; a populated colony (rating 0)
        // also adds the ratings of every object in its sector that is not ours.
        it->threat += (v ? vehicleRating(r, s, *v) : 0) + kStrengthScale;
        if (!v) it->threat += foreignRatingsAt(r, s, id, where);
        it->latest = t;
    }
    sortDefendEntries(sit.defendEntries, false);
    const size_t maxDefend = static_cast<size_t>(std::max(0, prof.settings.maxSystemsToDefend));
    for (const DefendEntry& d : sit.defendEntries)
        if (sit.defend.size() < maxDefend && std::find(sit.defend.begin(), sit.defend.end(), d.where.system) == sit.defend.end())
            sit.defend.push_back(d.where.system);

    for (const Empire& x : s.empires) sit.contact = sit.contact || (x.id != id && x.alive && e.relation(x.id).contact);

    // Colonization targets (spec 05 §7.5, confirmed: binary): uncolonized
    // planets in systems we know, plus hostile empires' colonies without
    // population (a Colonize order at such a planet fails on arrival).
    const std::vector<uint8_t> present = presence(s);
    auto nonFriendlyPresent = [&](SystemId sys) {
        int n = 0;
        for (const Empire& x : s.empires)
            if (x.id != id && hostileTo(e, x.id) && present[x.id.index() * nSys + sys.index()]) ++n;
        return n;
    };
    // The friendly-empire exclusion looks at colonies only.
    std::vector<uint8_t> ourColony(nSys, 0), friendlyColony(nSys, 0);
    for (const auto& c : s.colonies) {
        if (!c || !c->owner.valid() || c->owner.index() >= nEmp) continue;
        const size_t sys = s.galaxy.object(c->planet).system.index();
        if (c->owner == id) ourColony[sys] = 1;
        else if (!hostileTo(e, c->owner)) friendlyColony[sys] = 1;
    }
    std::set<ObjectId> targeted;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == id)
            for (const Order& o : v.orders)
                if (o.kind == OrderKind::Colonize) targeted.insert(o.object);
    for (const Fleet& f : s.fleets)
        if (f.owner == id)
            for (const Order& o : f.orders)
                if (o.kind == OrderKind::Colonize) targeted.insert(o.object);
    std::vector<int> danger(nSys, -1);
    for (size_t i = 0; i < nSys; ++i) {
        const SystemId sys{i};
        if (!e.hasExplored(sys) || !considered(sys)) continue;
        if (friendlyColony[i] && !ourColony[i]) continue;
        for (ObjectId o : s.galaxy.system(sys).objects) {
            const SpaceObject& planet = s.galaxy.object(o);
            if (planet.kind != ObjectKind::Planet || targeted.contains(o)) continue;
            const Colony* taken = s.colony(o);
            if (taken && (taken->owner == id || taken->totalPopulation() > 0 || !hostileTo(e, taken->owner))) continue;
            if (!notices(s, id, planetKey(o))) continue;
            if (danger[i] < 0) {
                // 5 per non-friendly empire present here, 1 per warp point leading
                // to a system where such an empire is present.
                danger[i] = 5 * nonFriendlyPresent(sys);
                for (const SystemId nb : s.galaxy.neighbors(sys)) danger[i] += nonFriendlyPresent(nb) > 0 ? 1 : 0;
            }
            ColonyTarget t;
            t.planet = o;
            t.system = sys;
            t.danger = danger[i];
            t.jumps = jumps(sys);
            t.ruins = hasRuins(planet);
            t.breathable = keysEqual(planet.atmosphere, e.race.atmosphere);
            t.size = sizeRank(r, planet);
            t.value = int64_t{planet.value[0]} + planet.value[1] + planet.value[2];
            t.settleable = canSettle(r, s, e, planet);
            t.colonized = taken != nullptr;
            sit.colonyTargets.push_back(t);
        }
    }
    std::sort(sit.colonyTargets.begin(), sit.colonyTargets.end(), [](const ColonyTarget& a, const ColonyTarget& b) {
        return std::tuple(a.danger, a.jumps, !a.ruins, !a.breathable, -a.size, -a.value, a.planet) <
               std::tuple(b.danger, b.jumps, !b.ruins, !b.breathable, -b.size, -b.value, b.planet);
    });

    // The Not Connected test: the reachability counts the systems other than
    // home that home reaches over every link (spec 05 §7.2).
    int settleable = 0;  // uncolonized planets we could settle and have not targeted
    for (const ColonyTarget& t : sit.colonyTargets) settleable += t.settleable && !t.colonized;
    int64_t reachable = 0;
    for (size_t i = 0; i < nSys; ++i) reachable += SystemId{i} != sit.home && sit.homeJumps[i] != kUnreachable;
    const int64_t others = static_cast<int64_t>(nSys) - (sit.home.valid() ? 1 : 0);
    sit.notConnected = settleable <= 10 && sit.freeFrontier.empty() && reachable * 100 <= int64_t{60} * others;
    return sit;
}

// ---- Planner core -----------------------------------------------------------------------------

Planner::Planner(const Rules& rules, const GameState& s, EmpireId e, Mode m, uint64_t salt)
    : r(rules),
      st(s),
      id(e),
      mode(m),
      prof(profileFor(rules, s.empire(e))),
      state(stateOf(s.empire(e))),
      rng(mix(mix(mix(s.seed) ^ s.turn) ^ (uint64_t{e.value} << 20) ^ salt)) {
    difficulty = difficultyOf(s, e);
    date = aiDate(s);
    neutral = emp().kind == PlayerKind::Neutral;

    const size_t nSys = st.galaxy.systems.size();
    links.resize(nSys);
    for (size_t i = 0; i < nSys; ++i)
        for (ObjectId wp : st.galaxy.system(SystemId{i}).objects) {
            const SpaceObject& o = st.galaxy.object(wp);
            if (o.kind != ObjectKind::WarpPoint || !o.destination.valid()) continue;
            links[i].push_back({wp, st.galaxy.object(o.destination).system});
        }

    scores = politicalScores(r, st);
    sit = assess(r, st, id, prof);
    for (const auto& c : st.colonies)
        if (c && c->owner == id && (c->homeworld || !homeLocation.system.valid())) {
            homeLocation = locationOf(st.galaxy, c->planet);
            if (c->homeworld) break;
        }
    if (!homeLocation.system.valid())
        for (const Vehicle& v : st.vehicles)
            if (v.owner == id) {
                homeLocation = v.location;
                break;
            }
}

bool Planner::emit(Command c) {
    const CommandResult res = apply(r, st, id, c);
    if (!res.ok) {
        dropped.push_back(std::format("{}: {}", commandName(c), res.error));
        return false;
    }
    out.push_back(std::move(c));
    return true;
}

bool Planner::on(Minister m) const { return mode == Mode::Computer || ministerOn(emp(), m); }

bool Planner::controlsColony(const Colony& c, Minister m) const {
    if (c.owner != id || !on(m)) return false;
    return mode == Mode::Computer || emp().ministerAll || isGlobalMinister(m) || c.minister;
}

bool Planner::controlsVehicle(const Vehicle& v, Minister m) const {
    if (v.owner != id || !on(m)) return false;
    if (mode == Mode::Computer || emp().ministerAll || isGlobalMinister(m) || v.minister) return true;
    if (const Fleet* f = st.fleet(v.fleet)) return f->minister;
    return false;
}

bool Planner::controlsFleet(const Fleet& f, Minister m) const {
    if (f.owner != id || !on(m)) return false;
    if (mode == Mode::Computer || emp().ministerAll || isGlobalMinister(m) || f.minister) return true;
    for (VehicleId member : f.members)
        if (const Vehicle* v = st.vehicle(member); !v || !v->minister) return false;
    return !f.members.empty();
}

std::vector<int> Planner::jumpsFrom(SystemId from) const {
    std::vector<int> dist(st.galaxy.systems.size(), kUnreachable);
    if (!from.valid() || from.index() >= dist.size()) return dist;
    std::deque<SystemId> queue{from};
    dist[from.index()] = 0;
    while (!queue.empty()) {
        const SystemId sys = queue.front();
        queue.pop_front();
        for (const Link& l : links[sys.index()]) {
            if (dist[l.to.index()] != kUnreachable) continue;
            dist[l.to.index()] = dist[sys.index()] + 1;
            queue.push_back(l.to);
        }
    }
    return dist;
}

const DesignInfo& Planner::info(DesignId d) {
    if (infos_.size() < st.designs.size()) infos_.resize(st.designs.size());
    DesignInfo& di = infos_[d.index()];
    if (!di.ready) {
        const Design& design = st.design(d);
        const Empire* owner = design.owner == id ? &emp() : nullptr;
        di.stats = computeDesignStats(r, owner, design);
        di.aiType = aiTypeOf(r, design, di.stats);
        di.role = roleOf(di.aiType, di.stats);
        di.ready = true;
    }
    return di;
}

namespace {

// The newest of the designs `accept` takes, by creation turn; the first
// listed on a tie (spec 05 §7.5).
template <class Accept>
std::optional<DesignId> newestOf(const GameState& s, const Empire& e, Accept&& accept) {
    std::optional<DesignId> best;
    for (DesignId d : e.designs) {
        if (!accept(d)) continue;
        if (!best || s.design(d).createdTurn > s.design(*best).createdTurn) best = d;
    }
    return best;
}

} // namespace

std::optional<DesignId> Planner::newestDesign(std::string_view aiType, bool anyMark) {
    return newestOf(st, emp(), [&](DesignId d) {
        if (!anyMark && st.design(d).obsolete) return false;
        const DesignInfo& di = info(d);
        return di.stats.problems.empty() && keysEqual(di.aiType, aiType);
    });
}

std::optional<DesignId> Planner::newestFromTemplate(std::string_view name) {
    return newestOf(st, emp(), [&](DesignId d) {
        const Design& design = st.design(d);
        return !design.templateName.empty() && keysEqual(design.templateName, name) && info(d).stats.problems.empty();
    });
}

bool Planner::atWarWith(EmpireId o) const {
    return o.valid() && o != id && o.index() < st.empires.size() && emp().relation(o).treaty == Treaty::War;
}

int Planner::colonyCount() const {
    int n = 0;
    for (const auto& c : st.colonies) n += c && c->owner == id;
    return n;
}

int64_t Planner::strengthOf(const Vehicle& v) { return vehicleRating(r, st, v) + kStrengthScale; }

std::vector<VehicleId> Planner::ownVehicles(Minister m) const {
    std::vector<VehicleId> ids;
    for (const Vehicle& v : st.vehicles)
        if (v.count > 0 && controlsVehicle(v, m)) ids.push_back(v.id);
    return ids;
}

bool Planner::idle(const Vehicle& v) const {
    if (busy.contains(v.id) || v.status == VehicleStatus::Mothballed || !v.orders.empty()) return false;
    if (const Fleet* f = st.fleet(v.fleet); f && !f->orders.empty()) return false;
    return true;
}

// Spec 05 §7.5 (confirmed: binary): production × the computer-player income
// factor, plus income from other empires (trade and tariffs received). The
// tariffs the empire pays are not subtracted.
Resources Planner::revenue() const {
    const EconomyReport& eco = emp().economy;
    // The report's otherIncome holds the bonus on what was left after tariffs:
    // (production - tariffs) x (factor - 1). Production x factor is the
    // report's income plus tariffs x (factor - 1).
    const int64_t extra = incomeBonusFactor(st, id) - 1;
    const Resources& t = eco.tariffsOut;
    return eco.colonies + eco.remoteMining + eco.otherIncome + Resources{t.v[0] * extra, t.v[1] * extra, t.v[2] * extra} + eco.trade +
           eco.tariffsIn;
}

Resources Planner::netIncome() const { return revenue() - emp().economy.maintenance; }

bool Planner::overCap(int extraPercent) const {
    const Resources rev = revenue();
    const Resources& upkeep = emp().economy.maintenance;
    const int64_t m = prof.settings.maxMaintenancePercent + extraPercent;
    for (Resource k : kResources)
        if (xmath::Ext(upkeep[k]) > xmath::Ext(rev[k]) * xmath::percent(m)) return true;
    return false;
}

bool Planner::setOrders(VehicleId vid, std::vector<Order> orders, bool repeat) {
    const Vehicle* v = st.vehicle(vid);
    if (!v) return false;
    busy.insert(vid);
    if (v->orders == orders && v->repeatOrders == repeat) return true;
    return emit(cmd::SetOrders{vid, {}, std::move(orders), repeat});
}

bool Planner::setFleetOrders(FleetId fid, std::vector<Order> orders) {
    const Fleet* f = st.fleet(fid);
    if (!f) return false;
    busyFleets.insert(fid);
    for (VehicleId m : f->members) busy.insert(m);
    if (f->orders == orders && !f->repeatOrders) return true;
    return emit(cmd::SetOrders{{}, fid, std::move(orders), false});
}

void Planner::runOrders() {
    if (!emp().alive) return;
    if (on(Minister::Politics)) planPolitics(*this);
    planTroops(*this);
    planTransports(*this);
    planColonization(*this);
    planSpaceYardShips(*this);
    planCarriers(*this);
    planMinesSatellitesDrones(*this);
    planFleets(*this);
    planDefense(*this);
    planAttack(*this);
    planExploration(*this);
    planPatrol(*this);
    if (on(Minister::Resupply)) planRepairAndResupply(*this, false);
    if (on(Minister::Repair)) planRepairAndResupply(*this, true);
    if (on(Minister::Scrap)) planScrap(*this);
    if (on(Minister::Retrofit)) planRetrofit(*this);
    planStellarManipulation(*this);
}

void Planner::runEconomy() {
    if (!emp().alive) return;
    // New colonies already have their type: colonization calls
    // colonyTypeAtColonization for every empire (spec 05 §7.5).
    if (mode == Mode::Computer) planStrategies(*this);
    if (on(Minister::Design)) planDesigns(*this);
    if (on(Minister::Research)) planResearch(*this);
    if (on(Minister::Intelligence)) planIntel(*this);
    if (date % 5 != 0) planFacilities(*this, true);  // skipped on every fifth turn (spec 05 §7.1)
    if (on(Minister::ShipConstruction)) planShips(*this);
    planFacilities(*this, false);
}

// ---- Strategies ------------------------------------------------------------------------------

void planStrategies(Planner& p) {
    // The AI's combat strategies (AI_Strategies) join the empire's list once,
    // so design templates and fleets can refer to them by name.
    for (const auto& s : p.prof.strategies) {
        bool have = false;
        for (const auto& mine : p.emp().strategies) have = have || keysEqual(mine.name, s.name);
        if (!have) p.emit(cmd::SetStrategy{-1, s, false});
    }
}

// ---- Shared helpers ----------------------------------------------------------------------------

Order moveOrder(Location where) {
    Order o;
    o.kind = OrderKind::MoveTo;
    o.location = where;
    return o;
}

Order simpleOrder(OrderKind k) {
    Order o;
    o.kind = k;
    return o;
}

std::string_view surfaceKey(std::string_view surface) {
    if (keysEqual(surface, "Ice")) return "Ice";
    if (keysEqual(surface, "Rock")) return "Rock";
    return "Gas";
}

std::string colonyTypeName(std::string_view surface) { return std::format("Colony ({})", surfaceKey(surface)); }

// Spec 05 §7.5 "Design types of other designs" (confirmed: binary): a design
// whose type label is one of the 39 names has that type; any other is typed
// by what it carries, the first test that matches.
std::string aiTypeOf(const Rules& r, const Design& d, const DesignStats& st) {
    for (std::string_view t : aiDesignTypes())
        if (t == d.designType) return std::string(t);  // matched exactly
    using ruleset::VehicleType;
    auto has = [&](AbilityKind k) { return designHas(r, d, k); };
    // 1. Colony modules.
    if (has(AbilityKind::ColonizeRock)) return "Colony (Rock)";
    if (has(AbilityKind::ColonizeIce)) return "Colony (Ice)";
    if (has(AbilityKind::ColonizeGas)) return "Colony (Gas)";
    // 2. A base hull.
    if (st.vehicleType == VehicleType::Base) return has(AbilityKind::SpaceYard) ? "Base Space Yard" : "Defense Base";
    // 3. Launchers.
    if (has(AbilityKind::LaunchRecoverFighters)) return "Carrier";
    if (has(AbilityKind::LaunchRecoverSatellites)) return "Satellite Layer";
    if (has(AbilityKind::LaunchDrones)) return "Drone Carrier";
    // 4. Mines, boarding, yards.
    if (has(AbilityKind::LayMines)) return "Mine Layer";
    if (has(AbilityKind::MineSweeping)) return "Mine Sweeper";
    if (has(AbilityKind::BoardingAttack)) return "Boarding Ship";
    if (has(AbilityKind::SpaceYard)) return "Space Yard Ship";
    // 5. Stellar manipulation, each ability its own type.
    static constexpr std::array<std::pair<AbilityKind, std::string_view>, 12> kStellar{{
        {AbilityKind::OpenWarpPointDistance, "Open Warp Point"},
        {AbilityKind::CloseWarpPoint, "Close Warp Point"},
        {AbilityKind::CreatePlanetSize, "Create Planet"},
        {AbilityKind::DestroyPlanetSize, "Destroy Planet"},
        {AbilityKind::CreateStar, "Create Star"},
        {AbilityKind::DestroyStar, "Destroy Star"},
        {AbilityKind::CreateStorm, "Create Storm"},
        {AbilityKind::DestroyStorm, "Destroy Storm"},
        {AbilityKind::CreateBlackHole, "Create Black Hole"},
        {AbilityKind::DestroyBlackHole, "Destroy Black Hole"},
        {AbilityKind::CreateNebulae, "Create Nebulae"},
        {AbilityKind::DestroyNebulae, "Destroy Nebulae"},
    }};
    for (const auto& [k, name] : kStellar)
        if (has(k)) return std::string(name);
    // 6. By hull type.
    switch (st.vehicleType) {
        case VehicleType::Satellite: return has(AbilityKind::SensorLevel) ? "Recon Satellite" : "Satellite";
        case VehicleType::Drone: {
            for (const DesignEntry& en : d.entries) {
                if (en.component >= r.data().components.size()) continue;
                const auto& w = r.component(en.component).weapon;
                if (w.kind != ruleset::WeaponKind::Warhead) continue;
                for (const std::string& target : w.targets)
                    if (keysEqual(target, "Planets")) return "Anti-Planet Drone";
            }
            return "Anti-Ship Drone";
        }
        case VehicleType::Fighter: return "Fighter";
        case VehicleType::Mine: return "Mine";
        case VehicleType::Troop: return "Troop";
        case VehicleType::WeaponPlatform: return "Weapon Platform";
        default: break;
    }
    // 7. Cargo space makes a Population Transport; anything else, armed or
    // not, is an Attack Ship (so a premade scout is one).
    return st.cargoCapacity > 0 ? "Population Transport" : "Attack Ship";
}

Role roleOf(std::string_view t, const DesignStats& st) {
    if (t.empty()) return Role::Other;
    if (keysEqual(t, "Attack Ship")) return Role::Attack;
    if (keysEqual(t, "Defense Ship")) return Role::Defense;
    if (keysEqual(t, "Attack Base") || keysEqual(t, "Defense Base")) return st.vehicleType == ruleset::VehicleType::Ship ? Role::Defense : Role::Base;
    if (keysEqual(t, "Base Space Yard")) return Role::YardBase;
    if (t.starts_with("Colony")) return Role::Colonizer;
    if (keysEqual(t, "Population Transport") || keysEqual(t, "Cargo Transport")) return Role::Transport;
    if (keysEqual(t, "Troop Transport")) return Role::TroopTransport;
    if (keysEqual(t, "Carrier")) return Role::Carrier;
    if (keysEqual(t, "Drone Carrier")) return Role::DroneCarrier;
    if (keysEqual(t, "Mine Layer")) return Role::MineLayer;
    if (keysEqual(t, "Satellite Layer")) return Role::SatelliteLayer;
    if (keysEqual(t, "Mine Sweeper")) return Role::Sweeper;
    if (keysEqual(t, "Boarding Ship")) return Role::Boarding;
    if (keysEqual(t, "Kamikaze Attack Ship")) return Role::Kamikaze;
    if (keysEqual(t, "Space Yard Ship")) return Role::YardShip;
    if (keysEqual(t, "Mine") || keysEqual(t, "Satellite") || keysEqual(t, "Weapon Platform") || keysEqual(t, "Troop") ||
        keysEqual(t, "Fighter") || keysEqual(t, "Recon Satellite") || keysEqual(t, "Anti-Ship Drone") || keysEqual(t, "Anti-Planet Drone"))
        return Role::Unit;
    return Role::Stellar;  // the warp, planet, star, storm, nebula and black hole types
}

bool combatRole(Role r) {
    return r == Role::Attack || r == Role::Defense || r == Role::Carrier || r == Role::DroneCarrier || r == Role::Boarding ||
           r == Role::Kamikaze || r == Role::TroopTransport;
}

bool hasColonyModule(const Rules& r, const Empire& e, std::string_view surface) {
    const std::string_view key = surfaceKey(surface);
    const AbilityKind k = key == "Rock" ? AbilityKind::ColonizeRock : key == "Ice" ? AbilityKind::ColonizeIce : AbilityKind::ColonizeGas;
    for (uint32_t c = 0; c < r.data().components.size(); ++c)
        if (r.componentAvailable(e, c) && hasAbility(r.componentAbilities(c), k)) return true;
    return false;
}

bool canSettle(const Rules& r, const GameState& s, const Empire& e, const SpaceObject& planet) {
    if (planet.kind != ObjectKind::Planet) return false;
    if (s.options.onlyBreathable && !keysEqual(planet.atmosphere, e.race.atmosphere)) return false;
    if (s.options.onlyHomeType && surfaceKey(planet.surface) != surfaceKey(e.race.nativeSurface)) return false;
    return hasColonyModule(r, e, planet.surface);
}

bool facilityHas(const Rules& r, uint32_t facility, std::string_view ability) {
    const auto kind = parseAbilityKind(ability);
    if (!kind) return false;
    for (const ParsedAbility& a : r.facilityAbilities(facility)) {
        if (*kind != AbilityKind::Unknown && *kind != AbilityKind::AITag && a.kind == *kind) return true;
        if (keysEqual(a.raw, ability)) return true;
    }
    return false;
}

std::optional<uint32_t> bestFacilityFor(const Rules& r, const Empire& e, std::string_view ability) {
    std::optional<uint32_t> best;
    for (uint32_t i = 0; i < r.data().facilities.size(); ++i) {
        if (!r.facilityAvailable(e, i) || !facilityHas(r, i, ability)) continue;
        if (!best || r.facility(i).romanNumeral >= r.facility(*best).romanNumeral) best = i;
    }
    return best;
}

} // namespace detail

// ---- Public entry points -------------------------------------------------------------------------

namespace {

constexpr uint64_t kSaltTurn = 1, kSaltOrders = 2, kSaltEconomy = 3, kSaltMinister = 4;

bool planFor(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size() && s.empire(e).alive; }

} // namespace

PlanReport planTurnReport(const Rules& r, const GameState& s, EmpireId e, bool minimal) {
    // An absent player who forbade AI changes: bookkeeping only (spec 05 §7.1).
    if (!planFor(s, e) || minimal) return {};
    detail::Planner p(r, s, e, detail::Mode::Computer, kSaltTurn);
    p.runOrders();
    p.runEconomy();
    return p.report();
}

std::vector<Command> planTurn(const Rules& r, const GameState& s, EmpireId e, bool minimal) {
    return planTurnReport(r, s, e, minimal).commands;
}

std::vector<Command> planOrders(const Rules& r, const GameState& s, EmpireId e) {
    if (!planFor(s, e)) return {};
    const detail::Mode mode = s.empire(e).kind == PlayerKind::Human ? detail::Mode::Minister : detail::Mode::Computer;
    detail::Planner p(r, s, e, mode, kSaltOrders);
    p.runOrders();
    return p.report().commands;
}

std::vector<Command> planEconomyStep(const Rules& r, const GameState& s, EmpireId e) {
    if (!planFor(s, e)) return {};
    const detail::Mode mode = s.empire(e).kind == PlayerKind::Human ? detail::Mode::Minister : detail::Mode::Computer;
    detail::Planner p(r, s, e, mode, kSaltEconomy);
    p.runEconomy();
    return p.report().commands;
}

bool ministersActive(const GameState& s, EmpireId e) {
    if (!planFor(s, e)) return false;
    const Empire& emp = s.empire(e);
    if (emp.kind != PlayerKind::Human) return false;
    bool any = emp.ministerAll;
    for (size_t m = 0; m < kMinisters && !any; ++m)
        if (isGlobalMinister(static_cast<Minister>(m)) && (emp.ministers & ministerBit(static_cast<Minister>(m)))) any = true;
    for (const auto& c : s.colonies) any = any || (c && c->owner == e && c->minister);
    for (const Vehicle& v : s.vehicles) any = any || (v.owner == e && v.minister);
    for (const Fleet& f : s.fleets) any = any || (f.owner == e && f.minister);
    return any;
}

std::vector<Command> ministerCommands(const Rules& r, const GameState& s, EmpireId e) {
    if (!ministersActive(s, e)) return {};
    detail::Planner p(r, s, e, detail::Mode::Minister, kSaltMinister);
    p.runOrders();
    p.runEconomy();
    return p.report().commands;
}

uint32_t aiDate(const GameState& s) { return s.options.simultaneous ? s.turn + 1 : s.turn; }

bool ministerOn(const Empire& e, Minister m) {
    if (e.kind != PlayerKind::Human || e.ministerAll) return true;
    return (e.ministers & ministerBit(m)) != 0;
}

std::string_view ministerStyleOf(const Empire& e) { return e.useRaceMinisterStyle ? std::string_view{} : std::string_view{e.ministerStyle}; }

MinisterSettings standIn(Empire& e) {
    const MinisterSettings saved{e.ministerAll, e.ministers};
    e.ministerAll = true;
    e.ministers = kAllMinisters;
    return saved;
}

void restoreMinisters(Empire& e, const MinisterSettings& saved) {
    e.ministerAll = saved.all;
    e.ministers = saved.areas;
}

std::string_view moodLabel(int anger) {
    // Spec 05 §7.3 (confirmed: binary).
    if (anger < 10) return "Brotherly";
    if (anger < 20) return "Amiable";
    if (anger < 30) return "Receptive";
    if (anger < 40) return "Warm";
    if (anger < 60) return "Moderate";
    if (anger < 70) return "Cool";
    if (anger < 80) return "Displeased";
    if (anger < 90) return "Angry";
    return "Murderous";
}

int difficultyOf(const GameState& s, EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size()) return kDifficultyMedium;
    const Empire& emp = s.empire(e);
    if (emp.kind == PlayerKind::Human) return kDifficultyMedium;  // a human's ministers
    if (emp.aiDifficulty >= 0) return std::clamp(emp.aiDifficulty, kDifficultyLow, kDifficultyHigh);
    const auto& random = s.options.randomAiPlayers;
    if (e.index() < random.size() && random[e.index()]) return std::clamp(s.options.aiDifficulty, kDifficultyLow, kDifficultyHigh);
    return kDifficultyMedium;
}

int rebelDifficulty(const GameState& s) {
    int best = -1;
    for (const Empire& e : s.empires)
        if (e.kind != PlayerKind::Human && e.aiDifficulty >= 0) best = std::max(best, std::min(e.aiDifficulty, kDifficultyHigh));
    return best < 0 ? kDifficultyMedium : best;
}

// ---- Random players ------------------------------------------------------------------------------

const ruleset::RacePreset* pickRandomRace(const Rules& r, Rng& rng, bool neutral, const std::vector<std::string>& used) {
    const auto& presets = r.racePresets();
    auto isUsed = [&](const ruleset::RacePreset& p) {
        return std::any_of(used.begin(), used.end(), [&](const std::string& u) { return datafile::keysEqual(u, p.folder); });
    };
    auto draw = [&](auto&& accept) -> const ruleset::RacePreset* {
        std::vector<const ruleset::RacePreset*> pool;
        for (const auto& p : presets)
            if (p.neutral == neutral && !isUsed(p) && accept(p)) pool.push_back(&p);
        return pool.empty() ? nullptr : pool[static_cast<size_t>(rng.below(pool.size()))];
    };
    if (!neutral) {
        // The personality group furthest below its target share (spec 05 §7.1).
        const int64_t groups = std::min<int64_t>(10, r.setting("Random Player Personality Groups", 0));
        const int64_t total = static_cast<int64_t>(used.size()) + 1;
        int64_t pick = 0, pickShare = 0;
        for (int64_t g = 1; g <= groups; ++g) {
            int64_t inGroup = 0;
            for (const std::string& u : used)
                if (const ruleset::RacePreset* p = findPreset(r, u); p && p->personalityGroup == g) ++inGroup;
            const int64_t share = xmath::divRoundHalfEven(100 * inGroup, total);
            if (share >= r.setting(std::format("Random Player Personality Group {} Percent", g), 0)) continue;
            if (pick == 0 || share < pickShare) {
                pick = g;
                pickShare = share;
            }
        }
        if (pick > 0)
            if (const auto* p = draw([&](const ruleset::RacePreset& x) { return x.personalityGroup == pick; })) return p;
    }
    return draw([](const ruleset::RacePreset&) { return true; });
}

namespace {

// Spec 05 §7.1 (confirmed: binary): every planet type and atmosphere pair
// is allowed except a Gas Giant with no atmosphere; the data set is not
// consulted. While the pair is not allowed, the atmosphere is redrawn
// uniformly among the five and the type among the three, with no limit.
void settleEnvironment(Race& race, Rng& rng) {
    static constexpr std::array<std::string_view, 5> kAtmospheres{"None", "Methane", "Oxygen", "Hydrogen", "Carbon Dioxide"};
    static constexpr std::array<std::string_view, 3> kSurfaces{"Rock", "Ice", "Gas Giant"};
    while (datafile::keysEqual(race.nativeSurface, "Gas Giant") && datafile::keysEqual(race.atmosphere, "None")) {
        race.atmosphere = kAtmospheres[static_cast<size_t>(rng.below(kAtmospheres.size()))];
        race.nativeSurface = kSurfaces[static_cast<size_t>(rng.below(kSurfaces.size()))];
    }
}

} // namespace

Race randomPlayerRace(const Rules& r, const ruleset::RacePreset& preset, int racialPoints, Rng& rng) {
    Race race = raceFromPreset(r, preset, 0);
    settleEnvironment(race, rng);
    race.characteristics.fill(100);
    race.traits.clear();
    // Race Opt 1, 2 and 3 for 2000, 3000 and 5000 points; none for 0 (spec 05 §7.1).
    const int level = racialPoints >= 5000 ? 3 : racialPoints >= 3000 ? 2 : racialPoints > 0 ? 1 : 0;
    if (level == 0 || preset.tiers.empty()) return race;
    const ruleset::RaceTier& tier = preset.tiers[std::min(static_cast<size_t>(level - 1), preset.tiers.size() - 1)];
    // Characteristics in the listed order, each only while the points spent
    // so far are below the budget; one that pushes the total over goes back
    // to 100 (confirmed: binary).
    for (const auto& [name, pct] : tier.characteristics) {
        Characteristic c;
        if (!parseCharacteristic(name, c)) continue;
        if (racialPointCost(r, race) >= racialPoints) continue;
        race.characteristics[static_cast<size_t>(c)] = pct;
        if (racialPointCost(r, race) > racialPoints) race.characteristics[static_cast<size_t>(c)] = 100;
    }
    // Then each trait whose cost fits the remaining points; one that does not
    // fit is skipped and the later ones are still tried (confirmed: binary).
    for (const std::string& name : tier.traits) {
        std::optional<uint32_t> trait;
        for (uint32_t i = 0; i < r.data().racialTraits.size() && !trait; ++i)
            if (datafile::keysEqual(r.data().racialTraits[i].name, name)) trait = i;
        if (!trait || std::find(race.traits.begin(), race.traits.end(), *trait) != race.traits.end()) continue;
        race.traits.push_back(*trait);
        if (racialPointCost(r, race) > racialPoints) race.traits.pop_back();
    }
    return race;
}

std::vector<std::string> randomComputerPresets(const Rules& r, int setting, bool neutral, Rng& rng) {
    static constexpr std::array<std::string_view, 3> kLevels{"Low", "Medium", "High"};
    static constexpr std::array<int64_t, 3> kMin{1, 3, 6}, kMax{3, 7, 10};  // our fallbacks
    const size_t level = static_cast<size_t>(std::clamp(setting, 0, 2));
    const std::string_view kind = neutral ? "Neutral" : "Computer";
    const int64_t lo = r.setting(std::format("Minimum {} Player {} Setting", kind, kLevels[level]), kMin[level]);
    const int64_t hi = r.setting(std::format("Maximum {} Player {} Setting", kind, kLevels[level]), kMax[level]);
    const int64_t count = rng.range(std::max<int64_t>(0, lo), std::max<int64_t>({0, lo, hi}));
    std::vector<std::string> out, used;
    for (int64_t n = 0; n < count; ++n) {
        const ruleset::RacePreset* p = pickRandomRace(r, rng, neutral, used);
        if (!p) break;
        used.push_back(p->folder);
        out.push_back(p->folder);
    }
    return out;
}

namespace {

// The "Computer Player Bonus" setting as 0 (None) .. 3 (High) for a computer
// empire, -1 for a human one. Settings above High count as High (inferred).
int bonusLevel(const GameState& s, EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size() || s.empire(e).kind == PlayerKind::Human) return -1;
    return std::clamp(s.options.aiBonus, 0, 3);
}

} // namespace

int incomeBonusFactor(const GameState& s, EmpireId e) {
    static constexpr std::array<int, 4> kFactor{1, 2, 3, 5};  // (confirmed: binary)
    const int level = bonusLevel(s, e);
    return level < 0 ? 1 : kFactor[static_cast<size_t>(level)];
}

int constructionBonusPercent(const GameState& s, EmpireId e) {
    static constexpr std::array<int, 4> kPercent{100, 150, 200, 300};  // (confirmed: binary)
    const int level = bonusLevel(s, e);
    return level < 0 ? 100 : kPercent[static_cast<size_t>(level)];
}

} // namespace opense4::game::ai
