#include "game/sight.hpp"

#include "game/abilities.hpp"
#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>

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

void raise(SightVector& a, const SightVector& b) {
    for (size_t t = 0; t < kSightTypes; ++t) a[t] = std::max(a[t], b[t]);
}

SightVector baseline() {
    SightVector v{};
    v[static_cast<size_t>(SightType::EMActive)] = 1;
    return v;
}

// Sensor Level / Cloak Level: Val 1 names the sight type, Val 2 is the level.
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

int64_t bestRaw(const std::vector<ruleset::Ability>& list, AbilityKind k) {
    int64_t best = 0;
    for (const auto& a : list)
        if (parseAbilityKind(a.type) == k) best = std::max(best, a.number1());
    return best;
}

int environmentObscuration(const GameState& s, Location where) {
    if (!where.system.valid() || where.system.index() >= s.galaxy.systems.size()) return 0;
    const StarSystem& sys = s.galaxy.system(where.system);
    int64_t level = bestRaw(sys.abilities, AbilityKind::SectorSightObscuration);
    for (ObjectId o : sys.objects) {
        const SpaceObject& obj = s.galaxy.object(o);
        if (obj.sector == where.sector) level = std::max(level, bestRaw(obj.abilities, AbilityKind::SectorSightObscuration));
    }
    return static_cast<int>(level);
}

SightVector vehicleSensors(const Rules& r, const GameState& s, const Vehicle& v) {
    SightVector out = baseline();
    addLevels(out, vehicleAbilities(r, s, v), AbilityKind::SensorLevel);
    return out;
}

// Facilities only work on a populated colony (inferred); the planet itself
// always carries the baseline.
SightVector colonySensors(const Rules& r, const GameState& s, const Colony& c) {
    SightVector out = baseline();
    if (c.totalPopulation() > 0) addLevels(out, colonyAbilities(r, s, c), AbilityKind::SensorLevel);
    return out;
}

std::vector<uint8_t> groupMask(const GameState& s, EmpireId viewer) {
    std::vector<uint8_t> mask(s.empires.size(), 0);
    for (EmpireId e : sightGroup(s, viewer)) mask[e.index()] = 1;
    return mask;
}

bool inGroup(const std::vector<uint8_t>& mask, EmpireId e) { return e.valid() && e.index() < mask.size() && mask[e.index()]; }

// Every vehicle (units included) and every colony, even an empty one, gives presence (inferred).
bool presenceFor(const GameState& s, const std::vector<uint8_t>& mask, SystemId sys) {
    if (s.options.omnipresent) return true;
    if (!sys.valid() || sys.index() >= s.galaxy.systems.size()) return false;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.location.system == sys && inGroup(mask, v.owner)) return true;
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && inGroup(mask, c->owner)) return true;
    return false;
}

SightVector sensorsFor(const Rules& r, const GameState& s, const std::vector<uint8_t>& mask, SystemId sys) {
    SightVector out{};
    if (!sys.valid() || sys.index() >= s.galaxy.systems.size()) return out;
    for (const Vehicle& v : s.vehicles)
        if (alive(v) && v.location.system == sys && inGroup(mask, v.owner)) raise(out, vehicleSensors(r, s, v));
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && inGroup(mask, c->owner)) raise(out, colonySensors(r, s, *c));
    if (s.options.omnipresent) raise(out, baseline());
    return out;
}

bool scannerJammed(const Rules& r, const GameState& s, const Vehicle& v) {
    return hasAbility(vehicleAbilities(r, s, v), AbilityKind::ScannerJammer);
}

void insertSorted(std::vector<DesignId>& list, DesignId d) {
    auto it = std::lower_bound(list.begin(), list.end(), d);
    if (it == list.end() || *it != d) list.insert(it, d);
}

} // namespace

std::vector<EmpireId> sightGroup(const GameState& s, EmpireId viewer) {
    std::vector<EmpireId> out;
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return out;
    std::vector<uint8_t> seen(s.empires.size(), 0);
    std::vector<EmpireId> queue{viewer};
    seen[viewer.index()] = 1;
    for (size_t i = 0; i < queue.size(); ++i) {
        const Empire& a = s.empire(queue[i]);
        for (size_t j = 0; j < s.empires.size(); ++j) {
            if (seen[j]) continue;
            const Empire& b = s.empires[j];
            const bool ab = j < a.relations.size() && treatySharesSight(a.relations[j].treaty);
            const bool ba = queue[i].index() < b.relations.size() && treatySharesSight(b.relations[queue[i].index()].treaty);
            if (ab || ba) {
                seen[j] = 1;
                queue.push_back(EmpireId{j});
            }
        }
    }
    std::sort(queue.begin(), queue.end());
    return queue;
}

SightVector sensorLevels(const Rules& r, const GameState& s, EmpireId viewer, SystemId sys) {
    if (!viewer.valid()) return {};
    return sensorsFor(r, s, groupMask(s, viewer), sys);
}

SightVector obscuration(const Rules& r, const GameState& s, const Vehicle& v) {
    SightVector o;
    o.fill(1);
    const Design& d = s.design(v.design);
    if (v.status != VehicleStatus::Mothballed) {
        addLevels(o, r.hullAbilities(d.hull), AbilityKind::CloakLevel);  // hull cloaks are always on (inferred)
        if (v.status == VehicleStatus::Cloaked)
            for (size_t i = 0; i < d.entries.size(); ++i)
                if (entryIntact(r, s, v, i)) addLevels(o, r.componentAbilities(d.entries[i].component), AbilityKind::CloakLevel);
    }
    // Storms and nebulae hide ships but not units (spec 01 §5.3, interpretation).
    if (!isUnitType(vehicleType(r, s, v))) {
        const int env = environmentObscuration(s, v.location);
        for (int& x : o) x = std::max(x, env);
    }
    return o;
}

SightVector planetObscuration(const GameState& s, ObjectId planet) {
    SightVector o;
    o.fill(1);
    const SpaceObject& obj = s.galaxy.object(planet);
    if (obj.kind == ObjectKind::Star || obj.kind == ObjectKind::DestroyedStar) return o;  // stars are never hidden
    const int env = environmentObscuration(s, {obj.system, obj.sector});
    for (int& x : o) x = std::max(x, env);
    return o;
}

bool hasPresence(const GameState& s, EmpireId viewer, SystemId sys) {
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return false;
    return presenceFor(s, groupMask(s, viewer), sys);
}

bool canSeeVehicle(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& v) {
    if (!alive(v) || !viewer.valid() || viewer.index() >= s.empires.size()) return false;
    if (v.owner == viewer) return true;
    const std::vector<uint8_t> mask = groupMask(s, viewer);
    if (inGroup(mask, v.owner)) return true;  // partners share everything they see
    if (!presenceFor(s, mask, v.location.system)) return false;
    return detects(sensorsFor(r, s, mask, v.location.system), obscuration(r, s, v));
}

bool canSeePlanet(const Rules& r, const GameState& s, EmpireId viewer, ObjectId planet) {
    if (!viewer.valid() || viewer.index() >= s.empires.size() || !planet.valid() || planet.index() >= s.galaxy.objects.size())
        return false;
    if (!inSystem(s.galaxy, planet)) return false;
    const std::vector<uint8_t> mask = groupMask(s, viewer);
    if (const Colony* c = s.colony(planet); c && inGroup(mask, c->owner)) return true;
    const SpaceObject& obj = s.galaxy.object(planet);
    if (!s.options.omnipresent && !s.empire(viewer).hasExplored(obj.system)) return false;
    const SightVector obsc = planetObscuration(s, planet);
    if (std::all_of(obsc.begin(), obsc.end(), [](int x) { return x <= 1; })) return true;
    // Hidden by a storm or nebula: only current sensors that pierce it reveal it.
    return presenceFor(s, mask, obj.system) && detects(sensorsFor(r, s, mask, obj.system), obsc);
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
    // Travelling a link also shows where its far end leads back to (inferred).
    const ObjectId back = s.galaxy.object(warpPoint).destination;
    if (back.valid() && back.index() < k.knownWarpLink.size()) k.knownWarpLink[back.index()] = 1;
}

bool knowsWarpLink(const GameState& s, EmpireId e, ObjectId warpPoint) {
    if (s.options.omnipresent || !e.valid()) return true;
    const Knowledge& k = s.empire(e).knowledge;
    return warpPoint.index() < k.knownWarpLink.size() && k.knownWarpLink[warpPoint.index()];
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

    // Each empire's own presence and sensors per system.
    std::vector<std::vector<uint8_t>> own(nEmp, std::vector<uint8_t>(nSys, 0));
    std::vector<std::vector<SightVector>> sensors(nEmp, std::vector<SightVector>(nSys, SightVector{}));
    for (const Vehicle& v : s.vehicles) {
        if (!alive(v) || !v.owner.valid() || v.owner.index() >= nEmp) continue;
        const size_t sys = v.location.system.index();
        own[v.owner.index()][sys] = 1;
        raise(sensors[v.owner.index()][sys], vehicleSensors(r, s, v));
    }
    for (size_t i = 0; i < s.colonies.size(); ++i) {
        const auto& c = s.colonies[i];
        if (!c || !c->owner.valid() || c->owner.index() >= nEmp || !inSystem(s.galaxy, c->planet)) continue;
        const size_t sys = s.galaxy.object(c->planet).system.index();
        own[c->owner.index()][sys] = 1;
        raise(sensors[c->owner.index()][sys], colonySensors(r, s, *c));
    }

    // Exploration by own presence, and the options that reveal everything.
    for (size_t ei = 0; ei < nEmp; ++ei) {
        Empire& e = s.empires[ei];
        const bool galaxySeen = s.options.allSystemsSeen || r.hasTrait(e.race, "Galaxy Seen");
        for (size_t sys = 0; sys < nSys; ++sys)
            if (own[ei][sys] || galaxySeen || s.options.omnipresent) e.knowledge.explored[sys] = 1;
        if (s.options.omnipresent || r.hasTrait(e.race, "Galaxy Seen")) std::fill(e.knowledge.knownWarpLink.begin(), e.knowledge.knownWarpLink.end(), 1);
    }

    // Partners share their live view: presence and sensors, through chains of
    // partnerships (spec 01 §6.1). Their maps and scanned designs are shared by
    // diplomacy::updateContacts.
    std::vector<std::vector<EmpireId>> groups(nEmp);
    for (size_t ei = 0; ei < nEmp; ++ei) groups[ei] = sightGroup(s, EmpireId{ei});
    std::vector<std::vector<SightVector>> groupSensors(nEmp, std::vector<SightVector>(nSys, SightVector{}));
    for (size_t ei = 0; ei < nEmp; ++ei) {
        Knowledge& k = s.empires[ei].knowledge;
        for (EmpireId member : groups[ei]) {
            const size_t mi = member.index();
            for (size_t sys = 0; sys < nSys; ++sys) {
                if (own[mi][sys]) k.present[sys] = 1;
                raise(groupSensors[ei][sys], sensors[mi][sys]);
            }
        }
        for (size_t sys = 0; sys < nSys; ++sys) {
            if (s.options.omnipresent) {
                k.present[sys] = 1;
                raise(groupSensors[ei][sys], baseline());
            }
            if (k.present[sys]) {
                k.explored[sys] = 1;
                k.lastSeen[sys] = s.turn;
            }
        }
    }

    // Visible foreign vehicles.
    std::vector<SightVector> obsc(s.vehicles.size());
    for (size_t vi = 0; vi < s.vehicles.size(); ++vi)
        if (alive(s.vehicles[vi])) obsc[vi] = obscuration(r, s, s.vehicles[vi]);
    for (size_t ei = 0; ei < nEmp; ++ei) {
        Empire& e = s.empires[ei];
        std::vector<uint8_t> mask(nEmp, 0);
        for (EmpireId m : groups[ei]) mask[m.index()] = 1;
        e.knowledge.visibleVehicles.clear();
        for (size_t vi = 0; vi < s.vehicles.size(); ++vi) {
            const Vehicle& v = s.vehicles[vi];
            if (!alive(v) || v.owner == e.id) continue;
            const size_t sys = v.location.system.index();
            if (inGroup(mask, v.owner) || (e.knowledge.present[sys] && detects(groupSensors[ei][sys], obsc[vi])))
                e.knowledge.visibleVehicles.push_back(v.id);
        }
    }

    // Long range scanning reveals designs (spec 01 §6.6).
    std::vector<std::vector<DesignId>> scanned(nEmp);
    auto visibleTo = [&](size_t ei, VehicleId id) {
        const auto& list = s.empires[ei].knowledge.visibleVehicles;
        return std::binary_search(list.begin(), list.end(), id);
    };
    for (const Vehicle& scanner : s.vehicles) {
        if (!alive(scanner) || !scanner.owner.valid() || scanner.owner.index() >= nEmp) continue;
        const int64_t range = bestValue1(vehicleAbilities(r, s, scanner), AbilityKind::LongRangeScanner);
        if (range <= 0) continue;
        for (const Vehicle& t : s.vehicles) {
            if (!alive(t) || t.owner == scanner.owner || t.location.system != scanner.location.system) continue;
            if (chebyshev(t.location.sector, scanner.location.sector) > range) continue;
            if (!visibleTo(scanner.owner.index(), t.id) || scannerJammed(r, s, t)) continue;
            insertSorted(scanned[scanner.owner.index()], t.design);
        }
    }
    for (size_t i = 0; i < s.colonies.size(); ++i) {
        const auto& c = s.colonies[i];
        if (!c || !c->owner.valid() || c->owner.index() >= nEmp || c->totalPopulation() <= 0 || !inSystem(s.galaxy, c->planet)) continue;
        if (!hasAbility(colonyAbilities(r, s, *c), AbilityKind::LongRangeScannerSystem)) continue;
        const SystemId sys = s.galaxy.object(c->planet).system;
        for (const Vehicle& t : s.vehicles)
            if (alive(t) && t.owner != c->owner && t.location.system == sys && visibleTo(c->owner.index(), t.id) && !scannerJammed(r, s, t))
                insertSorted(scanned[c->owner.index()], t.design);
    }
    for (size_t ei = 0; ei < nEmp; ++ei) {
        Empire& e = s.empires[ei];
        for (DesignId d : scanned[ei])
            if (d.valid() && d.index() < s.designs.size() && s.design(d).owner != e.id) insertSorted(e.knowledge.seenDesigns, d);
    }
}

} // namespace opense4::game::sight
