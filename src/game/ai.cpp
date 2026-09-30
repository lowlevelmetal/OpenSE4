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

// Is another empire (below Non-Aggression, or not met) present in a system:
// one of its colonies there, or one of its vehicles we can see.
std::vector<uint8_t> presence(const GameState& s, const Empire& e) {
    const size_t nSys = s.galaxy.systems.size();
    std::vector<uint8_t> present(s.empires.size() * nSys, 0);
    for (const auto& c : s.colonies)
        if (c && c->owner.valid() && c->owner.index() < s.empires.size()) {
            const SystemId sys = s.galaxy.object(c->planet).system;
            if (c->owner == e.id || e.hasExplored(sys)) present[c->owner.index() * nSys + sys.index()] = 1;
        }
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e.id && v.location.system.index() < nSys) present[v.owner.index() * nSys + v.location.system.index()] = 1;
    for (VehicleId vid : e.knowledge.visibleVehicles)
        if (const Vehicle* v = s.vehicle(vid); v && v->owner.valid() && v->owner.index() < s.empires.size() && v->location.system.index() < nSys)
            present[v->owner.index() * nSys + v->location.system.index()] = 1;
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

} // namespace

// ---- Situation ---------------------------------------------------------------------------------

bool hostileTo(const Empire& e, EmpireId other) {
    if (!other.valid() || other == e.id || other.index() >= e.relations.size()) return false;
    const Relation& rel = e.relation(other);
    return !rel.contact || treatyIsHostile(rel.treaty);
}

SystemId homeSystem(const GameState& s, EmpireId e) {
    if (e.valid() && e.index() < s.empires.size() && s.empire(e).homeSystem.index() < s.galaxy.systems.size()) return s.empire(e).homeSystem;  // recorded at creation
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

int64_t vehicleRating(const Rules& r, const GameState& s, const Vehicle& v) {
    if (isUnitType(vehicleType(r, s, v))) return std::max(0, v.count);  // a unit group: its number of units
    const Design& d = s.design(v.design);
    int64_t weapons = 0, bonus = 0, shields = 0;
    for (size_t i = 0; i < d.entries.size(); ++i) {
        if (!entryIntact(r, s, v, i)) continue;
        const DesignEntry& en = d.entries[i];
        if (r.component(en.component).isWeapon()) {
            int best = 0;
            for (int range = 1; range <= std::max(1, weaponMaxRange(r, en)); ++range) best = std::max(best, weaponDamageAtRange(r, en, range));
            weapons += best;
        }
        const auto ab = r.componentAbilities(en.component);
        bonus += sumValue1(ab, AbilityKind::CombatToHitOffensePlus);  // the combat bonus (inferred which ability)
        shields += sumValue1(ab, AbilityKind::ShieldGeneration) + sumValue1(ab, AbilityKind::PhasedShieldGeneration);
    }
    int64_t rating = weapons + bonus;
    if (rating > 0) rating += shields / 2;
    return rating;
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
            for (ObjectId wp : s.galaxy.system(SystemId{i}).objects) {
                const SpaceObject& o = s.galaxy.object(wp);
                if (o.kind != ObjectKind::WarpPoint || !o.destination.valid() || !sight::knowsWarpLink(s, id, wp)) continue;
                mine[s.galaxy.object(o.destination).system.index()] = 1;
            }
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

    // Strength of every empire per system, counting everything it owns (spec 05 §7.2).
    std::vector<int64_t> strength(nEmp * nSys, 0);
    for (const Vehicle& v : s.vehicles) {
        if (!v.owner.valid() || v.owner.index() >= nEmp || v.location.system.index() >= nSys || v.count <= 0) continue;
        strength[v.owner.index() * nSys + v.location.system.index()] += vehicleRating(r, s, v) + 1;
    }
    for (const auto& c : s.colonies)
        if (c && c->owner.valid() && c->owner.index() < nEmp) strength[c->owner.index() * nSys + s.galaxy.object(c->planet).system.index()] += 1;
    sit.ours.assign(nSys, 0);
    sit.hostile.assign(nSys, 0);
    for (size_t i = 0; i < nSys; ++i) sit.ours[i] = strength[id.index() * nSys + i];
    for (const Empire& x : s.empires) {
        if (x.id == id || !x.alive || !e.relation(x.id).contact || !treatyIsHostile(e.relation(x.id).treaty)) continue;
        for (size_t i = 0; i < nSys; ++i) sit.hostile[i] += strength[x.id.index() * nSys + i];
    }

    // Jumps from home over the links we know.
    sit.homeJumps.assign(nSys, -1);
    if (sit.home.valid()) {
        std::deque<SystemId> queue{sit.home};
        sit.homeJumps[sit.home.index()] = 0;
        while (!queue.empty()) {
            const SystemId at = queue.front();
            queue.pop_front();
            for (ObjectId wp : s.galaxy.system(at).objects) {
                const SpaceObject& o = s.galaxy.object(wp);
                if (o.kind != ObjectKind::WarpPoint || !o.destination.valid() || !sight::knowsWarpLink(s, id, wp)) continue;
                const SystemId to = s.galaxy.object(o.destination).system;
                if (sit.homeJumps[to.index()] >= 0) continue;
                sit.homeJumps[to.index()] = sit.homeJumps[at.index()] + 1;
                queue.push_back(to);
            }
        }
    }
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

    // Attack candidates: other empires' planets in systems we have explored.
    for (const auto& c : s.colonies) {
        if (!c || c->owner == id || !c->owner.valid() || c->owner.index() >= nEmp) continue;
        const SystemId sys = s.galaxy.object(c->planet).system;
        if (!e.hasExplored(sys) || !considered(sys) || jumps(sys) < 0) continue;
        if (hostileTo(e, c->owner) && !notices(s, id, planetKey(c->planet))) continue;
        int64_t defence = 0;  // the planet's defence: units kept in its cargo (inferred)
        for (const UnitStack& u : c->cargo.units) defence += u.count;
        const int64_t value = strength[c->owner.index() * nSys + sys.index()] + defence;
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

    // The defend list.
    std::vector<SystemId> threatened;
    for (const Threat& t : sit.enemyInTerritory)
        if (std::find(threatened.begin(), threatened.end(), t.system) == threatened.end()) threatened.push_back(t.system);
    std::vector<int64_t> atStake(nSys, 0);  // our colony value there: population plus 100 per facility (inferred)
    for (const auto& c : s.colonies)
        if (c && c->owner == id)
            atStake[s.galaxy.object(c->planet).system.index()] += c->totalPopulation() + 100 * static_cast<int64_t>(c->facilities.size());
    std::sort(threatened.begin(), threatened.end(), [&](SystemId a, SystemId b) {
        const int ja = jumps(a) < 0 ? INT32_MAX : jumps(a), jb = jumps(b) < 0 ? INT32_MAX : jumps(b);
        return std::tuple(ja, -atStake[a.index()], sit.hostile[a.index()], a) < std::tuple(jb, -atStake[b.index()], sit.hostile[b.index()], b);
    });
    const size_t maxDefend = static_cast<size_t>(std::max(0, prof.settings.maxSystemsToDefend));
    if (threatened.size() > maxDefend) threatened.resize(maxDefend);
    sit.defend = std::move(threatened);

    for (const Empire& x : s.empires) sit.contact = sit.contact || (x.id != id && x.alive && e.relation(x.id).contact);

    // Colonization targets (spec 05 §7.5).
    const std::vector<uint8_t> present = presence(s, e);
    auto nonFriendlyPresent = [&](SystemId sys) {
        int n = 0;
        for (const Empire& x : s.empires)
            if (x.id != id && x.alive && hostileTo(e, x.id) && present[x.id.index() * nSys + sys.index()]) ++n;
        return n;
    };
    auto friendlyPresent = [&](SystemId sys) {
        for (const Empire& x : s.empires)
            if (x.id != id && x.alive && !hostileTo(e, x.id) && present[x.id.index() * nSys + sys.index()]) return true;
        return false;
    };
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
        if (!e.hasExplored(sys) || !considered(sys) || jumps(sys) < 0) continue;
        if (friendlyPresent(sys) && !present[id.index() * nSys + i]) continue;
        for (ObjectId o : s.galaxy.system(sys).objects) {
            const SpaceObject& planet = s.galaxy.object(o);
            if (planet.kind != ObjectKind::Planet || s.colony(o) || targeted.contains(o)) continue;
            if (!notices(s, id, planetKey(o))) continue;
            if (danger[i] < 0) {
                danger[i] = 5 * nonFriendlyPresent(sys);
                for (SystemId nb : s.galaxy.neighbors(sys)) danger[i] += nonFriendlyPresent(nb);
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
            sit.colonyTargets.push_back(t);
        }
    }
    std::sort(sit.colonyTargets.begin(), sit.colonyTargets.end(), [](const ColonyTarget& a, const ColonyTarget& b) {
        return std::tuple(a.danger, a.jumps, !a.ruins, !a.breathable, -a.size, -a.value, a.planet) <
               std::tuple(b.danger, b.jumps, !b.ruins, !b.breathable, -b.size, -b.value, b.planet);
    });

    // The Not Connected test.
    int settleable = 0;
    for (const ColonyTarget& t : sit.colonyTargets) settleable += t.settleable;
    int reachable = 0;
    for (int j : sit.homeJumps) reachable += j >= 0;
    sit.notConnected = settleable <= 10 && sit.freeFrontier.empty() && int64_t{reachable} * 100 <= int64_t{60} * static_cast<int64_t>(nSys);
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

// The same knowledge movement plans with (sight::knowsWarpLink), so every
// route the computer picks is one its ships can actually fly.
bool Planner::knownLink(ObjectId wp) const { return sight::knowsWarpLink(st, id, wp); }

std::vector<int> Planner::jumpsFrom(SystemId from) const {
    std::vector<int> dist(st.galaxy.systems.size(), -1);
    if (!from.valid() || from.index() >= dist.size()) return dist;
    std::deque<SystemId> queue{from};
    dist[from.index()] = 0;
    while (!queue.empty()) {
        const SystemId sys = queue.front();
        queue.pop_front();
        for (const Link& l : links[sys.index()]) {
            if (dist[l.to.index()] >= 0 || !knownLink(l.warpPoint)) continue;
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
        Vehicle probe;
        probe.design = d;
        probe.damage.assign(design.entries.size(), 0);
        di.rating = vehicleRating(r, st, probe);
        di.ready = true;
    }
    return di;
}

std::optional<DesignId> Planner::newestDesign(std::string_view aiType) {
    std::optional<DesignId> best;
    for (DesignId d : emp().designs) {
        const Design& design = st.design(d);
        if (design.obsolete) continue;
        const DesignInfo& di = info(d);
        if (!di.stats.problems.empty() || !keysEqual(di.aiType, aiType)) continue;
        if (!best || std::pair(design.createdTurn, d) > std::pair(st.design(*best).createdTurn, *best)) best = d;
    }
    return best;
}

std::optional<DesignId> Planner::newestDesignMatching(std::string_view text) {
    const std::string want = datafile::normalizeKey(text);
    std::optional<DesignId> best;
    for (DesignId d : emp().designs) {
        const Design& design = st.design(d);
        if (design.obsolete || !info(d).stats.problems.empty()) continue;
        const bool match = datafile::normalizeKey(design.name).find(want) != std::string::npos ||
                           datafile::normalizeKey(design.designType).find(want) != std::string::npos;
        if (!match) continue;
        if (!best || std::pair(design.createdTurn, d) > std::pair(st.design(*best).createdTurn, *best)) best = d;
    }
    return best;
}

bool Planner::atWarWith(EmpireId o) const {
    return o.valid() && o != id && o.index() < st.empires.size() && emp().relation(o).treaty == Treaty::War;
}

int Planner::colonyCount() const {
    int n = 0;
    for (const auto& c : st.colonies) n += c && c->owner == id;
    return n;
}

int64_t Planner::strengthOf(const Vehicle& v) { return vehicleRating(r, st, v) + 1; }

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

// Spec 05 §7.5: production × the computer-player income factor, plus income
// from other empires. The economy report holds the income step's parts: its
// otherIncome includes the bonus, which multiplies the income left after
// tariffs (spec 05 §8), so production × factor is the report's income less the
// tariffs paid (inferred: the spec does not say whether "production" is taken
// before or after tariffs). This is exactly what the income step banks.
Resources Planner::revenue() const {
    const EconomyReport& eco = emp().economy;
    return eco.colonies + eco.remoteMining + eco.otherIncome - eco.tariffsOut + eco.trade + eco.tariffsIn;
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
    if (st.turn % 5 != 0) planFacilities(*this, true);
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

std::string aiTypeOf(const Rules& r, const Design& d, const DesignStats& st) {
    for (std::string_view t : aiDesignTypes())
        if (keysEqual(t, d.designType)) return std::string(t);
    // A design outside the fixed types (hand-made, premade): what it can do (inferred).
    using ruleset::VehicleType;
    if (st.canColonizeRock) return "Colony (Rock)";
    if (st.canColonizeIce) return "Colony (Ice)";
    if (st.canColonizeGas) return "Colony (Gas)";
    switch (st.vehicleType) {
        case VehicleType::Base: return st.spaceYard && !st.armed() ? "Base Space Yard" : "Defense Base";
        case VehicleType::Fighter: return "Fighter";
        case VehicleType::Satellite: return "Satellite";
        case VehicleType::Mine: return "Mine";
        case VehicleType::Troop: return "Troop";
        case VehicleType::WeaponPlatform: return "Weapon Platform";
        case VehicleType::Drone: return "Anti-Ship Drone";
        default: break;
    }
    if (st.spaceYard) return "Space Yard Ship";
    if (designHas(r, d, AbilityKind::LaunchRecoverFighters)) return "Carrier";
    if (designHas(r, d, AbilityKind::LaunchDrones)) return "Drone Carrier";
    if (designHas(r, d, AbilityKind::LayMines)) return "Mine Layer";
    if (designHas(r, d, AbilityKind::LaunchRecoverSatellites)) return "Satellite Layer";
    if (designHas(r, d, AbilityKind::MineSweeping)) return "Mine Sweeper";
    if (st.armed()) return "Attack Ship";
    if (st.cargoCapacity > 0) return "Population Transport";
    return {};
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

// Spec 05 §7.1: a planet type and atmosphere pair that is not allowed is
// replaced by random ones until it is. Allowed (inferred): some natural
// planet record of the data set has that type and atmosphere, and it is not a
// Gas Giant without atmosphere (spec 02 §2). The draws are uniform over the
// types and atmospheres the planet records use, at most 1000 times.
void settleEnvironment(const Rules& r, Race& race, Rng& rng) {
    std::vector<std::string> surfaces, atmospheres;
    auto addUnique = [](std::vector<std::string>& list, const std::string& v) {
        if (!v.empty() && !datafile::keysEqual(v, "None") && std::none_of(list.begin(), list.end(), [&](const std::string& x) { return datafile::keysEqual(x, v); }))
            list.push_back(v);
    };
    bool anyNone = false;
    for (uint32_t i : naturalSectorTypes(r.data(), ObjectKind::Planet)) {
        const ruleset::SectorObjectType& t = r.data().sectorObjectTypes[i];
        addUnique(surfaces, t.planetPhysicalType);
        if (datafile::keysEqual(t.planetAtmosphere, "None")) anyNone = true;
        else addUnique(atmospheres, t.planetAtmosphere);
    }
    if (anyNone) atmospheres.insert(atmospheres.begin(), "None");
    const bool fromData = !surfaces.empty() && !atmospheres.empty();
    if (!fromData) {
        surfaces = {"Rock", "Ice", "Gas Giant"};
        atmospheres = {"None", "Methane", "Oxygen", "Hydrogen", "Carbon Dioxide"};
    }
    auto has = [](const std::vector<std::string>& list, std::string_view v) {
        return std::any_of(list.begin(), list.end(), [&](const std::string& x) { return datafile::keysEqual(x, v); });
    };
    auto allowed = [&](const std::string& surface, const std::string& atmosphere) {
        if (detail::surfaceKey(surface) == "Gas" && datafile::keysEqual(atmosphere, "None")) return false;
        if (!fromData) return has(surfaces, surface) && has(atmospheres, atmosphere);
        return !naturalSectorTypes(r.data(), ObjectKind::Planet, 0, surface, atmosphere).empty();
    };
    for (int tries = 0; tries < 1000 && !allowed(race.nativeSurface, race.atmosphere); ++tries) {
        race.nativeSurface = surfaces[static_cast<size_t>(rng.below(surfaces.size()))];
        race.atmosphere = atmospheres[static_cast<size_t>(rng.below(atmospheres.size()))];
    }
}

} // namespace

Race randomPlayerRace(const Rules& r, const ruleset::RacePreset& preset, int racialPoints, Rng& rng) {
    Race race = raceFromPreset(r, preset, 0);
    settleEnvironment(r, race, rng);
    race.characteristics.fill(100);
    race.traits.clear();
    // Race Opt 1, 2 and 3 for 2000, 3000 and 5000 points; none for 0 (spec 05 §7.1).
    const int level = racialPoints >= 5000 ? 3 : racialPoints >= 3000 ? 2 : racialPoints > 0 ? 1 : 0;
    if (level == 0 || preset.tiers.empty()) return race;
    const ruleset::RaceTier& tier = preset.tiers[std::min(static_cast<size_t>(level - 1), preset.tiers.size() - 1)];
    for (const auto& [name, pct] : tier.characteristics) {
        Characteristic c;
        if (!parseCharacteristic(name, c)) continue;
        race.characteristics[static_cast<size_t>(c)] = pct;
        if (racialPointCost(r, race) > racialPoints) race.characteristics[static_cast<size_t>(c)] = 100;
    }
    for (const std::string& name : tier.traits) {
        std::optional<uint32_t> trait;
        for (uint32_t i = 0; i < r.data().racialTraits.size() && !trait; ++i)
            if (datafile::keysEqual(r.data().racialTraits[i].name, name)) trait = i;
        if (!trait || std::find(race.traits.begin(), race.traits.end(), *trait) != race.traits.end()) continue;
        race.traits.push_back(*trait);
        if (racialPointCost(r, race) > racialPoints) {
            race.traits.pop_back();
            break;  // traits are added while each one fits (inferred: the first misfit ends the list)
        }
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
