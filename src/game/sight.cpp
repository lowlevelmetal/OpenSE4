#include "game/sight.hpp"

#include "game/abilities.hpp"
#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <optional>

namespace opense4::game::sight {

namespace {

bool alive(const Vehicle& v) { return v.count > 0; }

// Objects removed by stellar manipulation leave their system's object list.
bool inSystem(const Galaxy& g, ObjectId o) {
    const SpaceObject& obj = g.object(o);
    if (!obj.system.valid() || obj.system.index() >= g.systems.size()) return false;
    const auto& list = g.system(obj.system).objects;
    return std::find(list.begin(), list.end(), o) != list.end();
}

bool validSystem(const GameState& s, SystemId sys) { return sys.valid() && sys.index() < s.galaxy.systems.size(); }

void raise(SightVector& a, const SightVector& b) {
    for (size_t t = 0; t < kSightTypes; ++t) a[t] = std::max(a[t], b[t]);
}

SightVector baseline() {
    SightVector v{};
    v[static_cast<size_t>(SightType::EMActive)] = 1;
    return v;
}

bool any(const SightVector& v) {
    return std::any_of(v.begin(), v.end(), [](int x) { return x > 0; });
}

// Sensor Level / Cloak Level: Val 1 names the sight type, Val 2 is the level; the highest counts.
void addLevels(SightVector& out, std::span<const ParsedAbility> abilities, AbilityKind kind) {
    for (const ParsedAbility& a : abilities) {
        if (a.kind != kind) continue;
        SightType t;
        if (parseSightType(a.text1, t)) {
            const size_t i = static_cast<size_t>(t);
            out[i] = std::max(out[i], static_cast<int>(a.value2));
        }
    }
}

// Sensor or cloak levels of a vehicle's hull and intact components (what
// vehicleAbilities would list), without building the list: sight runs often.
void addVehicleLevels(SightVector& out, const Rules& r, const GameState& s, const Vehicle& v, AbilityKind kind) {
    if (v.status == VehicleStatus::Mothballed) return;
    // A group that mixes designs lists every design's abilities: the best level counts.
    for (size_t k = 1; k < v.mixed.size(); ++k) {
        const Design& d = s.design(v.mixed[k].design);
        addLevels(out, r.hullAbilities(d.hull), kind);
        for (const DesignEntry& e : d.entries) addLevels(out, r.componentAbilities(e.component), kind);
    }
    const Design& d = s.design(v.design);
    addLevels(out, r.hullAbilities(d.hull), kind);
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const auto ab = r.componentAbilities(d.entries[i].component);
        if (hasAbility(ab, kind) && entryIntact(r, s, v, i)) addLevels(out, ab, kind);
    }
}

// The best Value 1 of an ability over a vehicle's hull and intact components.
int64_t vehicleBest(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind kind) {
    if (v.status == VehicleStatus::Mothballed) return 0;
    const Design& d = s.design(v.design);
    int64_t best = bestValue1(r.hullAbilities(d.hull), kind);
    for (size_t k = 1; k < v.mixed.size(); ++k) {   // the other designs of a mixed group
        const Design& m = s.design(v.mixed[k].design);
        best = std::max(best, bestValue1(r.hullAbilities(m.hull), kind));
        for (const DesignEntry& e : m.entries) best = std::max(best, bestValue1(r.componentAbilities(e.component), kind));
    }
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const auto ab = r.componentAbilities(d.entries[i].component);
        if (hasAbility(ab, kind) && entryIntact(r, s, v, i)) best = std::max(best, bestValue1(ab, kind));
    }
    return best;
}

int64_t bestRaw(const std::vector<ruleset::Ability>& list, AbilityKind k) {
    int64_t best = 0;
    for (const auto& a : list)
        if (parseAbilityKind(a.type) == k) best = std::max(best, a.number1());
    return best;
}

// Storms and nebulae hide planets, asteroid fields and comets; never stars,
// storms or warp points (spec 01 §5.3, confirmed: binary).
bool hideable(ObjectKind k) { return k == ObjectKind::Planet || k == ObjectKind::Asteroids || k == ObjectKind::Comet; }

// The obscuration a ship or base spreads over its sector; unit groups never
// obscure a sector (spec 01 §6.2, confirmed: binary).
int64_t vehicleObscuring(const Rules& r, const GameState& s, const Vehicle& v) {
    if (!alive(v) || isUnitType(vehicleType(r, s, v))) return 0;
    return vehicleBest(r, s, v, AbilityKind::SectorSightObscuration);
}

// Stellar objects that obscure their sector with their own rolled abilities:
// storms, planets and asteroid fields. Stars, warp points and comets never do,
// whatever abilities they carry (spec 01 §6.2, §14 Q32, confirmed: binary).
bool obscuresSector(ObjectKind k) { return k == ObjectKind::Storm || k == ObjectKind::Planet || k == ObjectKind::Asteroids; }

// The system-wide value and the storms, planets and asteroid fields of a
// sector, by their own rolled abilities; a colony's facilities do not count.
int64_t placeObscuration(const GameState& s, Location where) {
    const StarSystem& sys = s.galaxy.system(where.system);
    int64_t level = std::max<int64_t>(1, bestRaw(sys.abilities, AbilityKind::SectorSightObscuration));
    for (ObjectId o : sys.objects) {
        const SpaceObject& obj = s.galaxy.object(o);
        if (obj.sector != where.sector || !obscuresSector(obj.kind)) continue;
        level = std::max(level, bestRaw(obj.abilities, AbilityKind::SectorSightObscuration));
    }
    return level;
}

// The environment's obscuration at a place: the largest `Sector - Sight
// Obscuration` among the storms, planets, asteroid fields, ships and bases in
// the sector, and the system-wide value; at least 1 (spec 01 §6.2, confirmed:
// binary).
int environmentObscuration(const Rules& r, const GameState& s, Location where) {
    if (!validSystem(s, where.system)) return 1;
    int64_t level = placeObscuration(s, where);
    for (const Vehicle& v : s.vehicles)
        if (v.location == where) level = std::max(level, vehicleObscuring(r, s, v));
    return static_cast<int>(level);
}

// The same for every place with vehicles at once (knowledge updates).
std::map<Location, int> environmentByPlace(const Rules& r, const GameState& s) {
    std::map<Location, int64_t> ships;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && validSystem(s, v.location.system)) {
            int64_t& level = ships[v.location];
            level = std::max(level, vehicleObscuring(r, s, v));
        }
    std::map<Location, int> out;
    for (const auto& [where, level] : ships) out[where] = static_cast<int>(std::max(level, placeObscuration(s, where)));
    return out;
}

// Cloak levels: a ship's only while it is cloaked; unit groups always use
// theirs (spec 01 §6.2, confirmed: binary). Mothballed vehicles have none.
SightVector cloakLevels(const Rules& r, const GameState& s, const Vehicle& v) {
    SightVector o;
    o.fill(1);
    if (v.status == VehicleStatus::Cloaked || isUnitType(vehicleType(r, s, v))) addVehicleLevels(o, r, s, v, AbilityKind::CloakLevel);
    return o;
}

std::optional<SightVector> vehicleSensors(const Rules& r, const GameState& s, const Vehicle& v) {
    if (!isSensorSource(vehicleType(r, s, v))) return std::nullopt;
    SightVector out = baseline();
    addVehicleLevels(out, r, s, v, AbilityKind::SensorLevel);
    return out;
}

// Every owned planet is a sensor source; the sensor levels its facilities
// give, stored with the colony (recalculateColony), count whether or not
// anyone lives there. The planet's own abilities are not read (spec 01 §6.1,
// §6.9, §14 Q26, confirmed: binary).
SightVector colonySensors(const Colony& c) { return c.sensorLevels; }

// reach[e][m]: empire e gets empire m's sensors. Each round raises every
// empire, in empire order, to the empires it holds a Partnership with; five
// rounds (spec 01 §6.1, confirmed: binary).
std::vector<std::vector<uint8_t>> sensorReach(const GameState& s) {
    const size_t n = s.empires.size();
    std::vector<std::vector<uint8_t>> reach(n, std::vector<uint8_t>(n, 0));
    for (size_t e = 0; e < n; ++e) reach[e][e] = 1;
    for (int round = 0; round < 5; ++round)
        for (size_t e = 0; e < n; ++e)
            for (size_t p = 0; p < n; ++p) {
                if (p == e || p >= s.empires[e].relations.size() || !treatySharesSight(s.empires[e].relations[p].treaty)) continue;
                for (size_t m = 0; m < n; ++m) reach[e][m] = reach[e][m] | reach[p][m];
            }
    return reach;
}

std::vector<uint8_t> reachOf(const GameState& s, EmpireId viewer) {
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return {};
    bool partners = false;
    for (const Empire& e : s.empires)
        for (const Relation& rel : e.relations) partners = partners || treatySharesSight(rel.treaty);
    if (!partners) {
        std::vector<uint8_t> self(s.empires.size(), 0);
        self[viewer.index()] = 1;
        return self;
    }
    return sensorReach(s)[viewer.index()];
}

bool inMask(const std::vector<uint8_t>& mask, EmpireId e) { return e.valid() && e.index() < mask.size() && mask[e.index()]; }

bool presenceFor(const Rules& r, const GameState& s, const std::vector<uint8_t>& mask, SystemId sys) {
    if (s.options.omnipresent) return true;
    if (!validSystem(s, sys)) return false;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.location.system == sys && inMask(mask, v.owner) && isSensorSource(vehicleType(r, s, v))) return true;
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && inMask(mask, c->owner)) return true;
    return false;
}

SightVector sensorsFor(const Rules& r, const GameState& s, const std::vector<uint8_t>& mask, SystemId sys) {
    SightVector out{};
    if (!validSystem(s, sys)) return out;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.location.system == sys && inMask(mask, v.owner))
            if (auto sensors = vehicleSensors(r, s, v)) raise(out, *sensors);
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && inMask(mask, c->owner)) raise(out, colonySensors(*c));
    if (s.options.omnipresent) raise(out, baseline());  // every system as if present (spec 01 §6.5)
    return out;
}

// The empire's own sensor source in a system: arriving there explored it.
bool ownSourceIn(const Rules& r, const GameState& s, EmpireId e, SystemId sys) {
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.owner == e && v.location.system == sys && isSensorSource(vehicleType(r, s, v))) return true;
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && c->owner == e) return true;
    return false;
}

// Only explored systems show anything (the explored test is dropped with omnipresence).
bool explored(const Rules& r, const GameState& s, EmpireId e, SystemId sys) {
    return s.options.omnipresent || s.empire(e).hasExplored(sys) || ownSourceIn(r, s, e, sys);
}

bool scannerJammed(const Rules& r, const GameState& s, const Vehicle& v) {
    return hasAbility(vehicleAbilities(r, s, v), AbilityKind::ScannerJammer);
}

void insertSorted(std::vector<DesignId>& list, DesignId d) {
    auto it = std::lower_bound(list.begin(), list.end(), d);
    if (it == list.end() || *it != d) list.insert(it, d);
}

} // namespace

bool isSensorSource(ruleset::VehicleType t) {
    using ruleset::VehicleType;
    return t == VehicleType::Ship || t == VehicleType::Base || t == VehicleType::Fighter || t == VehicleType::Satellite ||
           t == VehicleType::Drone;
}

std::vector<EmpireId> sightGroup(const GameState& s, EmpireId viewer) {
    std::vector<EmpireId> out;
    const std::vector<uint8_t> mask = reachOf(s, viewer);
    for (size_t i = 0; i < mask.size(); ++i)
        if (mask[i]) out.push_back(EmpireId{i});
    return out;
}

SightVector sensorLevels(const Rules& r, const GameState& s, EmpireId viewer, SystemId sys) {
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return {};
    return sensorsFor(r, s, reachOf(s, viewer), sys);
}

SightVector obscuration(const Rules& r, const GameState& s, const Vehicle& v) {
    SightVector o = cloakLevels(r, s, v);
    const int env = environmentObscuration(r, s, v.location);
    for (int& x : o) x = std::max(x, env);
    return o;
}

SightVector planetObscuration(const Rules& r, const GameState& s, ObjectId planet) {
    SightVector o;
    o.fill(1);
    const SpaceObject& obj = s.galaxy.object(planet);
    if (!hideable(obj.kind)) return o;
    // A cloaked colony's cloak levels replace the baseline (spec 01 §6.9).
    if (const Colony* c = s.colony(planet); c && c->cloaked) o = c->cloakLevels;
    const int env = environmentObscuration(r, s, {obj.system, obj.sector});
    for (int& x : o) x = std::max(x, env);
    return o;
}

bool hasPresence(const Rules& r, const GameState& s, EmpireId viewer, SystemId sys) {
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return false;
    return presenceFor(r, s, reachOf(s, viewer), sys);
}

bool canSeeVehicle(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& v) {
    if (!alive(v) || !viewer.valid() || viewer.index() >= s.empires.size()) return false;
    if (v.owner == viewer) return true;
    if (!validSystem(s, v.location.system) || !explored(r, s, viewer, v.location.system)) return false;
    return detects(sensorsFor(r, s, reachOf(s, viewer), v.location.system), obscuration(r, s, v));
}

bool canSeePlanet(const Rules& r, const GameState& s, EmpireId viewer, ObjectId planet) {
    if (!viewer.valid() || viewer.index() >= s.empires.size() || !planet.valid() || planet.index() >= s.galaxy.objects.size())
        return false;
    if (!inSystem(s.galaxy, planet)) return false;
    if (const Colony* c = s.colony(planet); c && c->owner == viewer) return true;
    const SpaceObject& obj = s.galaxy.object(planet);
    if (!explored(r, s, viewer, obj.system)) return false;
    const SightVector obsc = planetObscuration(r, s, planet);
    if (std::all_of(obsc.begin(), obsc.end(), [](int x) { return x <= 1; })) return true;  // remembered since exploration
    // Hidden by a storm or nebula: only current sensors that pierce it reveal it.
    return detects(sensorsFor(r, s, reachOf(s, viewer), obj.system), obsc);
}

bool canSeeColony(const Rules& r, const GameState& s, EmpireId viewer, ObjectId planet) {
    if (!viewer.valid() || viewer.index() >= s.empires.size() || !planet.valid() || planet.index() >= s.galaxy.objects.size())
        return false;
    if (!inSystem(s.galaxy, planet)) return false;
    if (const Colony* c = s.colony(planet); c && c->owner == viewer) return true;
    const SpaceObject& obj = s.galaxy.object(planet);
    if (!explored(r, s, viewer, obj.system)) return false;
    // The planet's obscuration holds the colony's cloak levels while it is
    // cloaked (spec 01 §6.9).
    return detects(sensorsFor(r, s, reachOf(s, viewer), obj.system), planetObscuration(r, s, planet));
}

void recalculateColony(const Rules& r, Colony& c) {
    SightVector cloak, sensors = baseline();
    cloak.fill(1);
    for (uint32_t f : c.facilities) {
        if (f >= r.data().facilities.size()) continue;
        addLevels(cloak, r.facilityAbilities(f), AbilityKind::CloakLevel);
        addLevels(sensors, r.facilityAbilities(f), AbilityKind::SensorLevel);
    }
    c.cloakLevels = cloak;
    c.sensorLevels = sensors;
    if (c.cloaked && !colonyCanCloak(c)) c.cloaked = false;
}

void recalculateColonies(const Rules& r, GameState& s) {
    for (auto& c : s.colonies)
        if (c) recalculateColony(r, *c);
}

bool colonyCanCloak(const Colony& c) {
    return std::any_of(c.cloakLevels.begin(), c.cloakLevels.end(), [](int level) { return level >= 2; });
}

bool colonyShown(const Rules& r, const GameState& s, EmpireId viewer, ObjectId planet) {
    const Colony* c = s.colony(planet);
    if (!c || c->owner == viewer || !c->cloaked) return true;
    return canSeeColony(r, s, viewer, planet);
}

size_t forgetOldDesigns(GameState& s, EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size()) return 0;
    // "More than 50 turns ago": a design seen at turn T is still known at
    // turn T + 50 and forgotten at T + 51. Both turns are GameState::turn
    // values, the number every record of the turn carries.
    return std::erase_if(s.empire(e).knowledge.seenDesigns,
                         [&](const SeenDesign& d) { return d.turn < s.turn && s.turn - d.turn > kDesignMemoryTurns; });
}

void markExplored(GameState& s, EmpireId e, SystemId sys) {
    if (!e.valid() || e.index() >= s.empires.size() || !sys.valid()) return;
    Knowledge& k = s.empire(e).knowledge;
    if (k.explored.size() < s.galaxy.systems.size()) k.explored.resize(s.galaxy.systems.size(), 0);
    k.explored[sys.index()] = 1;
}

void learnWarpLink(GameState& s, EmpireId e, ObjectId warpPoint) {
    if (!e.valid() || e.index() >= s.empires.size() || !warpPoint.valid()) return;
    Knowledge& k = s.empire(e).knowledge;
    if (k.knownWarpLink.size() < s.galaxy.objects.size()) k.knownWarpLink.resize(s.galaxy.objects.size(), 0);
    k.knownWarpLink[warpPoint.index()] = 1;
    // Travelling a link also shows where its far end leads back to (inferred, spec 01 §14 Q24).
    const ObjectId back = s.galaxy.object(warpPoint).destination;
    if (back.valid() && back.index() < k.knownWarpLink.size()) k.knownWarpLink[back.index()] = 1;
}

bool knowsWarpLink(const GameState& s, EmpireId e, ObjectId warpPoint) {
    if (s.options.omnipresent || !e.valid()) return true;
    const Knowledge& k = s.empire(e).knowledge;
    return warpPoint.index() < k.knownWarpLink.size() && k.knownWarpLink[warpPoint.index()];
}

std::string warpPointName(const GameState& s, EmpireId viewer, ObjectId warpPoint) {
    std::string name = "Warp Point";
    if (!warpPoint.valid() || warpPoint.index() >= s.galaxy.objects.size()) return name;
    const ObjectId far = s.galaxy.object(warpPoint).destination;
    if (!far.valid() || far.index() >= s.galaxy.objects.size()) return name;
    const SystemId dest = s.galaxy.object(far).system;
    const bool known = !viewer.valid() || viewer.index() >= s.empires.size() || s.options.omnipresent || s.empire(viewer).hasExplored(dest);
    if (known && validSystem(s, dest)) name += " " + s.galaxy.system(dest).name;
    return name;
}

void updateKnowledge(const Rules& r, GameState& s) {
    const size_t nSys = s.galaxy.systems.size();
    const size_t nObj = s.galaxy.objects.size();
    const size_t nEmp = s.empires.size();
    for (Empire& e : s.empires) {
        Knowledge& k = e.knowledge;
        k.present.assign(nSys, 0);
        k.explored.resize(nSys, 0);
        k.lastSeen.resize(nSys, 0);
        k.knownWarpLink.resize(nObj, 0);
        k.notes.resize(nSys);
    }

    // Each empire's own sensor sources and sensors per system.
    std::vector<std::vector<uint8_t>> own(nEmp, std::vector<uint8_t>(nSys, 0));
    std::vector<std::vector<SightVector>> sensors(nEmp, std::vector<SightVector>(nSys, SightVector{}));
    for (const Vehicle& v : s.vehicles) {
        if (!alive(v) || !v.owner.valid() || v.owner.index() >= nEmp || !validSystem(s, v.location.system)) continue;
        const auto vs = vehicleSensors(r, s, v);
        if (!vs) continue;  // mine fields give no presence
        const size_t sys = v.location.system.index();
        own[v.owner.index()][sys] = 1;
        raise(sensors[v.owner.index()][sys], *vs);
    }
    for (size_t i = 0; i < s.colonies.size(); ++i) {
        const auto& c = s.colonies[i];
        if (!c || !c->owner.valid() || c->owner.index() >= nEmp || !inSystem(s.galaxy, c->planet)) continue;
        const size_t sys = s.galaxy.object(c->planet).system.index();
        own[c->owner.index()][sys] = 1;
        raise(sensors[c->owner.index()][sys], colonySensors(*c));
    }

    // Exploration by the empire's own sources, and the options that reveal everything.
    for (size_t ei = 0; ei < nEmp; ++ei) {
        Empire& e = s.empires[ei];
        const bool galaxySeen = s.options.allSystemsSeen || r.hasTrait(e.race, "Galaxy Seen");
        for (size_t sys = 0; sys < nSys; ++sys)
            if (own[ei][sys] || galaxySeen || s.options.omnipresent) e.knowledge.explored[sys] = 1;
        if (s.options.omnipresent || r.hasTrait(e.race, "Galaxy Seen")) std::fill(e.knowledge.knownWarpLink.begin(), e.knowledge.knownWarpLink.end(), 1);
    }

    // Partners' sensors (one-way, through chains); omnipresence gives baseline sensors everywhere.
    const auto reach = sensorReach(s);
    std::vector<std::vector<SightVector>> shared(nEmp, std::vector<SightVector>(nSys, SightVector{}));
    for (size_t ei = 0; ei < nEmp; ++ei) {
        Knowledge& k = s.empires[ei].knowledge;
        for (size_t mi = 0; mi < nEmp; ++mi)
            if (reach[ei][mi])
                for (size_t sys = 0; sys < nSys; ++sys) raise(shared[ei][sys], sensors[mi][sys]);
        for (size_t sys = 0; sys < nSys; ++sys) {
            if (s.options.omnipresent) raise(shared[ei][sys], baseline());
            if (any(shared[ei][sys])) {
                k.present[sys] = 1;
                k.lastSeen[sys] = s.turn;
            }
        }
    }

    // Visible foreign vehicles: explored system, and a sight type that reaches the obscuration.
    std::vector<SightVector> obsc(s.vehicles.size());
    const std::map<Location, int> env = environmentByPlace(r, s);
    for (size_t vi = 0; vi < s.vehicles.size(); ++vi) {
        const Vehicle& v = s.vehicles[vi];
        if (!alive(v) || !validSystem(s, v.location.system)) continue;
        obsc[vi] = cloakLevels(r, s, v);
        const auto it = env.find(v.location);
        for (int& x : obsc[vi]) x = std::max(x, it == env.end() ? 1 : it->second);
    }
    for (size_t ei = 0; ei < nEmp; ++ei) {
        Empire& e = s.empires[ei];
        e.knowledge.visibleVehicles.clear();
        for (size_t vi = 0; vi < s.vehicles.size(); ++vi) {
            const Vehicle& v = s.vehicles[vi];
            if (!alive(v) || v.owner == e.id || !validSystem(s, v.location.system)) continue;
            const size_t sys = v.location.system.index();
            if ((e.knowledge.explored[sys] || s.options.omnipresent) && detects(shared[ei][sys], obsc[vi])) e.knowledge.visibleVehicles.push_back(v.id);
        }
    }

    // Long-range scanners teach nothing by themselves: a design is learned
    // only when a human player opens the report of a vehicle they reach
    // (learnFromReport, spec 05 §8 "Design knowledge", confirmed: binary).
}

bool scannerReaches(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& target) {
    if (!viewer.valid() || viewer.index() >= s.empires.size() || !alive(target) || target.owner == viewer) return false;
    const auto& visible = s.empire(viewer).knowledge.visibleVehicles;
    if (!std::binary_search(visible.begin(), visible.end(), target.id) || scannerJammed(r, s, target)) return false;
    // `Long Range Scanner - System` covers ships and bases only, never unit
    // groups (spec 03 §3.3, confirmed: binary).
    const bool shipOrBase = !isUnitType(vehicleType(r, s, target));
    const SystemId sys = target.location.system;
    // Every own object in the system: its own largest `Long Range Scanner`
    // reaches that many sectors; a system-wide scanner reaches the whole
    // system. Colonies count through their facilities, without population.
    auto reaches = [&](const std::vector<ParsedAbility>& abilities, Sector from) {
        if (shipOrBase && hasAbility(abilities, AbilityKind::LongRangeScannerSystem)) return true;
        const int64_t range = bestValue1(abilities, AbilityKind::LongRangeScanner);
        return range > 0 && chebyshev(target.location.sector, from) <= range;
    };
    for (const Vehicle& scanner : s.vehicles)
        if (alive(scanner) && scanner.owner == viewer && scanner.location.system == sys &&
            reaches(vehicleAbilities(r, s, scanner), scanner.location.sector))
            return true;
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && c->owner == viewer && reaches(colonyAbilities(r, s, *c), s.galaxy.object(o).sector))
            return true;
    return false;
}

std::vector<DesignId> reportDesigns(const Rules& r, const GameState& s, const Vehicle& v) {
    std::vector<DesignId> out;
    if (isUnitType(vehicleType(r, s, v)))
        for (const UnitStack& st : groupStacks(v)) insertSorted(out, st.design);
    else
        insertSorted(out, v.design);
    std::erase_if(out, [&](DesignId d) { return !d.valid() || d.index() >= s.designs.size(); });
    return out;
}

bool learnFromReport(const Rules& r, GameState& s, EmpireId viewer, VehicleId vehicle) {
    const Vehicle* v = s.vehicle(vehicle);
    if (!v || !viewer.valid() || viewer.index() >= s.empires.size() || s.empire(viewer).kind != PlayerKind::Human) return false;
    if (!scannerReaches(r, s, viewer, *v)) return false;
    if (s.options.simultaneous && isUnitType(vehicleType(r, s, *v))) return false;
    bool changed = false;
    for (DesignId d : reportDesigns(r, s, *v)) {
        if (s.design(d).owner == viewer) continue;
        changed = changed || designSeenTurn(s.empire(viewer).knowledge, d) != std::optional<uint32_t>(s.turn);
        seeDesign(s.empire(viewer).knowledge, d, s.turn);
    }
    return changed;
}

} // namespace opense4::game::sight
