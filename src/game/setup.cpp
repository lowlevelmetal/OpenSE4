#include "game/setup.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/generate.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"
#include "core/hash.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>

namespace opense4::game {

namespace {

using datafile::keysEqual;

template <class T>
uint32_t indexByName(const std::vector<T>& list, std::string_view name) {
    for (uint32_t i = 0; i < list.size(); ++i)
        if (keysEqual(list[i].name, name)) return i;
    return 0;
}

AbilityKind colonizeKind(std::string_view surface) {
    if (keysEqual(surface, "Ice")) return AbilityKind::ColonizeIce;
    if (keysEqual(surface, "Gas Giant") || keysEqual(surface, "Gas")) return AbilityKind::ColonizeGas;
    return AbilityKind::ColonizeRock;
}

// True when the empire has every technology it could research at its
// maximum level (the starting-planet facility rule of spec 02 §9).
bool knowsEverything(const Rules& r, const GameState& s, const Empire& e) {
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
        const ruleset::TechAreaId a{i};
        if (r.techVisible(s, e, a) && e.techLevel(a) < r.tech(a).maxLevel) return false;
    }
    return true;
}

// Facilities of a starting planet (spec 02 §9, confirmed: binary): the empire's
// best facility for each role, while slots are free: a spaceport (unless the
// race has No Spaceports), a space yard, a supply generator, one mineral, one
// radioactives and one organics producer, one research facility (unless every
// technology is known), then alternately a mineral producer and a research
// facility, starting with minerals (only minerals once everything is known).
void addStartingFacilities(const Rules& r, const GameState& s, const Empire& e, Colony& col) {
    const int slots = facilitySlots(r, s, col);
    auto add = [&](AbilityKind kind) {
        if (static_cast<int>(col.facilities.size()) >= slots) return false;
        const auto f = r.bestFacilityWith(e, kind);
        if (f) col.facilities.push_back(*f);
        return f.has_value();
    };
    if (!r.hasTrait(e.race, "No Spaceports")) add(AbilityKind::Spaceport);
    add(AbilityKind::SpaceYard);
    add(AbilityKind::SupplyGeneration);
    add(AbilityKind::ResourceGenMinerals);
    add(AbilityKind::ResourceGenRadioactives);
    add(AbilityKind::ResourceGenOrganics);
    const bool everything = knowsEverything(r, s, e);
    if (!everything) add(AbilityKind::PointGenResearch);
    const bool mines = r.bestFacilityWith(e, AbilityKind::ResourceGenMinerals).has_value();
    const bool labs = !everything && r.bestFacilityWith(e, AbilityKind::PointGenResearch).has_value();
    // A role the empire has no facility for is skipped (inferred).
    for (bool mineral = true; static_cast<int>(col.facilities.size()) < slots && (mines || labs); mineral = !mineral) {
        if (mineral ? !mines : !labs) continue;
        add(mineral ? AbilityKind::ResourceGenMinerals : AbilityKind::PointGenResearch);
    }
}

Vehicle makeVehicle(const Rules& r, GameState& s, const Design& d, Location where) {
    Vehicle v;
    v.owner = d.owner;
    v.design = d.id;
    v.name = nextVehicleName(s, d);
    v.location = where;
    v.damage.assign(d.entries.size(), 0);
    v.builtTurn = s.turn;
    v.supply = computeDesignStats(r, nullptr, d).supplyCapacity;
    return v;
}


HomeValue homeValueOf(const GameOptions& o) {
    return o.homePlanetValue == 0 ? HomeValue::Low : o.homePlanetValue == 2 ? HomeValue::High : HomeValue::Medium;
}

// Keeps per-object vectors in step after planets were created.
void objectsGrown(GameState& s) {
    s.colonies.resize(s.galaxy.objects.size());
    for (Empire& e : s.empires) e.knowledge.knownWarpLink.resize(s.galaxy.objects.size(), s.options.allSystemsSeen ? 1 : 0);
}

bool surfaceMatches(std::string_view a, std::string_view b) {
    auto norm = [](std::string_view x) { return keysEqual(x, "Gas") ? std::string_view("Gas Giant") : x; };
    return keysEqual(norm(a), norm(b));
}

// The planets beyond the homeworld (spec 01 §3.6, confirmed: binary).
//  1. Candidate systems: the home system, then one or two rounds (two when the
//     quadrant holds more than 60 % of Maximum Number Of Systems, compared
//     exactly) that go through the list as it stands and append, system by
//     system, the destinations of its warp points in the order of those warp
//     points, skipping systems already listed.
//  2. Systems that are not start-eligible are dropped, and unless empires may
//     share systems, so are systems where another empire has a colony and
//     other players' home systems (placed already, even when set up later).
//     The home system always stays (inferred for the second filter: it only
//     matters when a map puts two empires in one system, spec 01 §14 Q41).
//     Every system left is explored for the empire.
//  3. Sectors 0..168 of each candidate system in order; in each only the first
//     planet counts. It is taken when it has no colony, the empire's
//     atmosphere and planet type (and the home size when every player planet
//     has the same size) and is nobody's homeworld.
//  4. The rest are created in random candidate systems, on a sector of the
//     inner 11 × 11 area that holds no object.
std::vector<ObjectId> extraStartingPlanets(const Rules& r, GameState& s, const Empire& e, std::span<const ObjectId> homes, int count) {
    std::vector<ObjectId> out;
    if (count <= 0) return out;
    const auto& rs = r.data();
    const ObjectId home = homes[e.id.index()];
    const SystemId homeSys = s.galaxy.object(home).system;
    const int homeSize = homePlanetSize(rs, homeValueOf(s.options), e.race.nativeSurface, e.race.atmosphere);
    const int rounds = static_cast<int64_t>(s.galaxy.systems.size()) * 10 > int64_t{6} * maxSystemCount(rs) ? 2 : 1;

    std::vector<SystemId> systems{homeSys};
    auto listed = [&](SystemId sys) { return std::find(systems.begin(), systems.end(), sys) != systems.end(); };
    for (int round = 0; round < rounds; ++round) {
        const size_t asItStands = systems.size();
        for (size_t i = 0; i < asItStands; ++i)
            for (SystemId next : s.galaxy.neighbors(systems[i]))  // warp points in creation order
                if (!listed(next)) systems.push_back(next);
    }

    auto otherColony = [&](SystemId sys) {
        for (ObjectId o : s.galaxy.system(sys).objects)
            if (const Colony* c = s.colony(o); c && c->owner != e.id) return true;
        return false;
    };
    auto otherHome = [&](SystemId sys) {
        for (size_t i = 0; i < homes.size(); ++i)
            if (i != e.id.index() && homes[i].valid() && s.galaxy.object(homes[i]).system == sys) return true;
        return false;
    };
    std::erase_if(systems, [&](SystemId sys) {
        if (sys == homeSys) return false;
        const ruleset::SystemTypeId type = s.galaxy.system(sys).type;
        const bool eligible = type.index() < rs.systemTypes.size() && rs.systemTypes[type.index()].empiresCanStartIn;
        return !eligible || (!s.options.sameSystemAllowed && (otherColony(sys) || otherHome(sys)));
    });
    for (SystemId sys : systems) sight::markExplored(s, e.id, sys);

    auto taken = [&](ObjectId o) {
        return std::find(homes.begin(), homes.end(), o) != homes.end() || std::find(out.begin(), out.end(), o) != out.end();
    };
    for (SystemId sysId : systems)
        for (const auto& planet : firstPlanetPerSector(s.galaxy, sysId)) {
            if (static_cast<int>(out.size()) >= count) return out;
            if (!planet || s.colony(*planet) || taken(*planet)) continue;
            const SpaceObject& obj = s.galaxy.object(*planet);
            if (!keysEqual(obj.atmosphere, e.race.atmosphere) || !surfaceMatches(obj.surface, e.race.nativeSurface)) continue;
            if (s.options.allPlanetsSameSize && stellarSizeOf(rs, obj) != homeSize) continue;
            out.push_back(*planet);
        }
    while (static_cast<int>(out.size()) < count) {
        const SystemId target = systems[s.rng.below(systems.size())];
        std::vector<Sector> inner;
        for (Sector sct : emptySectors(s.galaxy, target))
            if (sct.x >= 1 && sct.x <= 11 && sct.y >= 1 && sct.y <= 11) inner.push_back(sct);
        // The original redraws without limit; with the inner area full we take any inner sector (OpenSE4 choice).
        const Sector where = inner.empty() ? Sector{s.rng.rangeInt(1, 11), s.rng.rangeInt(1, 11)} : inner[s.rng.below(inner.size())];
        out.push_back(createStartingPlanet(s.galaxy, rs, target, where, e.race.nativeSurface, e.race.atmosphere,
                                           s.options.allPlanetsSameSize ? homeSize : 0, s.options.finiteResources, s.rng));
        objectsGrown(s);
    }
    return out;
}

// Every starting planet, the homeworld and the extra ones alike, is set up the
// same way (spec 02 §9, confirmed: binary): its system explored, ruins
// removed, the Home Planet Value (plus R[1,10] − 5 per resource in a normal
// game), conditions unchanged, and a colony of the empire's race at maximum
// population that is a capital (anger at most 80) of colony type "Homeworld",
// with the starting facilities.
void setUpStartingPlanet(const Rules& r, GameState& s, Empire& e, ObjectId planet) {
    SpaceObject& obj = s.galaxy.object(planet);
    sight::markExplored(s, e.id, obj.system);
    std::erase_if(obj.abilities, [](const ruleset::Ability& a) {
        return keysEqual(a.type, "Ancient Ruins") || keysEqual(a.type, "Ancient Ruins Unique");
    });
    const char* level = s.options.homePlanetValue == 0 ? "Low" : s.options.homePlanetValue == 2 ? "High" : "Medium";
    if (s.options.finiteResources) {
        const int v = static_cast<int>(r.setting(std::format("Plr Planet Value {} Resources", level), 20000));
        obj.value = {v, v, v};
    } else {
        const int base = static_cast<int>(r.setting(std::format("Plr Planet Value {} Percent", level), 100));
        for (int& v : obj.value) v = base + s.rng.rangeInt(1, 10) - 5;
    }
    Colony c;
    c.planet = planet;
    c.owner = e.id;
    c.homeworld = true;
    c.colonyType = "Homeworld";
    c.foundedTurn = 0;
    c.population.push_back({e.id, 0});
    s.colonies[planet.index()] = std::move(c);
    Colony& col = *s.colonies[planet.index()];
    col.population.front().millions = maxPopulation(r, s, col);
    addStartingFacilities(r, s, e, col);
    sight::recalculateColony(r, col);  // the colony is founded (spec 01 §6.9)
}

} // namespace

uint32_t defaultEmpireColor(size_t index) {
    static constexpr std::array<uint32_t, 20> kColors{
        0x3a7bd5, 0xd53a3a, 0x3ad56b, 0xd5c83a, 0xa03ad5, 0x3ad5cf, 0xd5823a, 0xd53aa6, 0x8fd53a, 0x6b6bd5,
        0x9a6b3a, 0xb0b0b0, 0x1f5f8f, 0x8f1f1f, 0x1f8f3f, 0x8f7f1f, 0x5f1f8f, 0x1f8f8a, 0x8f4f1f, 0x8f1f6a};
    return kColors[index % kColors.size()];
}

std::string hashPassword(std::string_view password) {
    if (password.empty()) return {};
    Hasher h;
    h.add(std::string_view("opense4-empire-password"));
    h.add(password);
    return std::format("fnv1a64:{:016x}", h.value());
}

const ruleset::RacePreset* findPreset(const Rules& r, std::string_view folderOrName) {
    for (const auto& p : r.racePresets())
        if (keysEqual(p.folder, folderOrName) || keysEqual(p.name, folderOrName)) return &p;
    return nullptr;
}

Race raceFromPreset(const Rules& r, const ruleset::RacePreset& preset, int tier) {
    Race race;
    race.name = preset.name;
    race.style = preset.folder;
    race.biology = preset.biology;
    race.society = preset.society;
    race.history = preset.history;
    race.demeanor = preset.demeanor;
    race.designNameFile = preset.designNameFile;
    if (!preset.planetType.empty()) race.nativeSurface = preset.planetType;
    if (!preset.atmosphere.empty()) race.atmosphere = preset.atmosphere;
    race.culture = indexByName(r.data().cultures, preset.culture);
    race.happinessModel = indexByName(r.data().happinessModels, preset.happinessType);
    if (!preset.tiers.empty()) {
        const auto& t = preset.tiers[static_cast<size_t>(std::clamp(tier, 0, static_cast<int>(preset.tiers.size()) - 1))];
        for (const auto& [name, pct] : t.characteristics) {
            Characteristic c;
            if (parseCharacteristic(name, c)) race.characteristics[static_cast<size_t>(c)] = pct;
        }
        for (const auto& name : t.traits)
            for (uint32_t i = 0; i < r.data().racialTraits.size(); ++i)
                if (keysEqual(r.data().racialTraits[i].name, name) &&
                    std::find(race.traits.begin(), race.traits.end(), i) == race.traits.end())
                    race.traits.push_back(i);
    }
    return race;
}

int racialPointCost(const Rules& r, const Race& race) {
    // Spec 02 §8.1 (confirmed: binary): the characteristics' costs
    // (economy::characteristicPointCost, the one implementation) plus the traits'.
    int64_t total = 0;
    for (size_t i = 0; i < kCharacteristics; ++i)
        total += economy::characteristicPointCost(r, static_cast<Characteristic>(i), race.characteristics[i]);
    for (uint32_t ti : race.traits)
        if (ti < r.data().racialTraits.size()) total += r.data().racialTraits[ti].cost;
    return static_cast<int>(total);
}

std::vector<int> startingTechLevels(const Rules& r, const GameOptions& o, const Race& race) {
    const auto& areas = r.data().techAreas;
    std::vector<int> levels(areas.size(), 0);
    Empire probe;
    probe.race = race;
    GameState dummy;
    dummy.options = o;
    // Levels are set in dependency order: an area whose requirements are not
    // met stays at 0. Iterate until nothing changes.
    for (bool changed = true; changed;) {
        changed = false;
        probe.techLevels = levels;
        for (uint32_t i = 0; i < areas.size(); ++i) {
            const ruleset::TechAreaId id{i};
            if (!r.techVisible(dummy, probe, id)) continue;
            const auto& a = areas[i];
            int want = a.startLevel;
            if (o.startTechLevel == 1) want = std::max(a.startLevel, a.raiseLevel);
            if (o.startTechLevel >= 2) want = a.maxLevel;
            want = std::min(want, a.maxLevel);
            if (want > levels[i]) {
                levels[i] = want;
                changed = true;
            }
        }
    }
    // Colonization of the home planet type is always known (spec 05 §1.2).
    const AbilityKind k = colonizeKind(race.nativeSurface);
    std::optional<uint32_t> cheapest;
    for (uint32_t c = 0; c < r.data().components.size(); ++c) {
        if (!hasAbility(r.componentAbilities(c), k)) continue;
        if (!cheapest || r.component(c).requirements.size() < r.component(*cheapest).requirements.size() ||
            r.component(c).romanNumeral < r.component(*cheapest).romanNumeral)
            cheapest = c;
    }
    if (cheapest)
        for (const auto& req : r.component(*cheapest).requirements)
            if (req.area.index() < levels.size()) levels[req.area.index()] = std::max(levels[req.area.index()], req.level);
    return levels;
}

std::expected<GameState, std::string> createGame(const Rules& r, const GameSetup& setup) {
    if (setup.empires.empty()) return std::unexpected("A game needs at least one empire.");
    GameState s;
    s.seed = setup.seed;
    s.options = setup.options;
    s.rng.reseed(setup.seed);

    // ---- Quadrant (spec 01 §3.7 steps 1-6).
    QuadrantOptions qo;
    qo.quadrantType = s.options.quadrantType;
    qo.systemCount = s.options.systemCount;
    qo.size = static_cast<QuadrantSize>(std::clamp(s.options.quadrantSize, 0, 2));
    qo.allWarpPointsConnected = s.options.allWarpPointsConnected;
    qo.noWarpPoints = s.options.noWarpPoints;
    qo.warpPointsAnywhere = s.options.warpPointsAnywhere;
    qo.noRuins = s.options.noRuins;
    qo.finiteResources = s.options.finiteResources;
    Rng galaxyRng = s.rng.fork();
    if (setup.map) {
        // A loaded map replaces generation (spec 01 §12).
        if (setup.map->galaxy.systems.empty()) return std::unexpected("The map has no systems.");
        s.galaxy = setup.map->galaxy;
        if (!s.galaxy.quadrantType.empty()) s.options.quadrantType = s.galaxy.quadrantType;
    } else {
        auto generated = generateQuadrant(r.data(), qo, galaxyRng);
        if (!generated) return std::unexpected(generated.error());
        s.galaxy = std::move(generated->galaxy);
    }
    s.colonies.resize(s.galaxy.objects.size());

    // ---- Empires.
    const size_t n = setup.empires.size();
    std::vector<EmpireStart> starts;
    for (size_t i = 0; i < n; ++i) {
        const EmpireSetup& es = setup.empires[i];
        Empire e;
        e.id = EmpireId{i};
        e.kind = es.kind;
        e.passwordHash = es.passwordHash;
        // The Empire Setup minister style; it drives a Computer Controlled
        // empire too, unless the race's style is used (spec 02 §9, spec 05 §7.1).
        e.ministerStyle = es.ministerStyle;
        e.useRaceMinisterStyle = es.useRaceMinisterStyle;
        e.experience = std::clamp(es.experience, 0, economy::kMaxEmpireExperience);  // carried from the empire file (spec 02 §9)
        const ruleset::RacePreset* preset = es.preset.empty() ? nullptr : findPreset(r, es.preset);
        if (es.customRace) e.race = *es.customRace;
        else if (preset) e.race = raceFromPreset(r, *preset, es.presetTier);
        else if (!r.racePresets().empty()) {
            // No preset named: take them in order so empires differ.
            const auto& all = r.racePresets();
            preset = &all[i % all.size()];
            e.race = raceFromPreset(r, *preset, es.presetTier);
        } else {
            e.race.name = std::format("Race {}", i + 1);
        }
        e.name = !es.name.empty() ? es.name : preset && !preset->empireName.empty() ? preset->empireName : e.race.name;
        e.empireType = !es.empireType.empty() ? es.empireType : preset ? preset->empireType : std::string{};
        e.leaderTitle = !es.leaderTitle.empty() ? es.leaderTitle : preset ? preset->emperorTitle : std::string{};
        e.leaderName = !es.leaderName.empty() ? es.leaderName : preset ? preset->emperorName : std::string{};
        e.color = es.color ? es.color : defaultEmpireColor(i);
        e.racialPointsSpent = racialPointCost(r, e.race);
        if (e.kind == PlayerKind::Human && es.customRace && e.racialPointsSpent > s.options.racialPoints)
            return std::unexpected(std::format("{} spends {} racial points; the limit is {}.", e.name, e.racialPointsSpent,
                                               s.options.racialPoints));
        e.techLevels = startingTechLevels(r, s.options, e.race);
        e.relations.assign(n, Relation{});
        e.knowledge.explored.assign(s.galaxy.systems.size(), s.options.allSystemsSeen ? 1 : 0);
        e.knowledge.present.assign(s.galaxy.systems.size(), 0);
        e.knowledge.lastSeen.assign(s.galaxy.systems.size(), 0);
        e.knowledge.knownWarpLink.assign(s.galaxy.objects.size(), s.options.allSystemsSeen ? 1 : 0);
        e.knowledge.notes.assign(s.galaxy.systems.size(), {});
        // The strategies saved with the empire, else the data set's (spec 06 §7 Q72).
        e.strategies = es.strategies.empty() ? r.data().combatStrategies : es.strategies;
        if (e.strategies.empty()) e.strategies.push_back({"Default", {}});
        e.designTypes = r.data().names.designTypes;
        e.colonyTypes = r.data().names.colonyTypes;
        e.repairPriorities = r.data().names.repairPriorities;
        s.empires.push_back(std::move(e));
        starts.push_back({s.empires.back().name, s.empires.back().race.nativeSurface, s.empires.back().race.atmosphere});
    }

    // ---- Homeworlds (spec 01 §3.6, step 7).
    PlacementOptions po;
    po.evenlyDistributed = s.options.evenlyDistributed;
    po.allowSameSystem = s.options.sameSystemAllowed;
    po.homeValue = homeValueOf(s.options);
    po.finiteResources = s.options.finiteResources;
    po.allPlanetsSameSize = s.options.allPlanetsSameSize;
    if (setup.map) po.startingPoints = setup.map->startingPoints;
    auto homes = placeHomeworlds(s.galaxy, r.data(), starts, po, s.rng, &s.startingPoints);
    if (!homes) return std::unexpected(homes.error());
    objectsGrown(s);  // placement may have created planets
    // The home systems and sectors are recorded now and never move (spec 02
    // §2, spec 05 §4).
    for (size_t i = 0; i < n; ++i) {
        s.empires[i].homeSystem = s.galaxy.object((*homes)[i]).system;
        s.empires[i].homeSector = s.galaxy.object((*homes)[i]).sector;
    }

    // ---- Each empire's starting planets, in player order (step 8, spec 02 §9).
    for (size_t i = 0; i < n; ++i) {
        Empire& e = s.empires[i];
        const ObjectId home = (*homes)[i];
        // A neutral empire always gets one starting planet.
        const int extras = e.kind == PlayerKind::Neutral ? 0 : std::max(0, s.options.startingPlanets - 1);
        std::vector<ObjectId> planets{home};
        for (ObjectId o : extraStartingPlanets(r, s, e, *homes, extras)) planets.push_back(o);
        for (ObjectId p : planets) setUpStartingPlanet(r, s, e, p);
        e.claimedSystems.push_back(s.galaxy.object(home).system);
    }

    // The starting stockpile and the research and intelligence pools are set
    // at the end (research::openingPools, spec 02 §9).

    // ---- Starting designs and ships.
    for (size_t i = 0; i < n; ++i) {
        const EmpireId id{i};
        const Location home = locationOf(s.galaxy, (*homes)[i]);
        struct Start {
            std::string role;
            std::string name;
            std::string designType;
            int ships;
        };
        const std::string colonyRole = "colony:" + s.empires[i].race.nativeSurface;
        const std::array<Start, 4> roles{{
            {"scout", "Scout", "Scout", 2},
            {colonyRole, "Colonizer", "Colony Ship", 1},
            {"warship", "Escort", "Attack Ship", 0},
            {"base", "Defense Base", "Defense Base", 0},
        }};
        for (const Start& st : roles) {
            auto d = autoDesign(r, s.empires[i], st.role);
            if (!d) continue;
            // Design names are unique in the whole game (spec 03 §4.1): the
            // plain name when it is free, else prefixed with the empire's name,
            // else numbered (inferred).
            d->name = !designNameInUse(s, st.name) ? st.name : uniqueDesignName(s, std::format("{} {}", s.empires[i].name, st.name));
            d->designType = st.designType;
            d->owner = id;
            const DesignId did = addDesign(s, std::move(*d));
            for (int k = 0; k < st.ships; ++k) {
                Vehicle v = makeVehicle(r, s, s.design(did), home);
                ++s.design(did).built;
                s.addVehicle(std::move(v));
            }
        }
    }

    // ---- Designs an empire file brought (spec 06 §7 Q48, Q72, confirmed:
    // binary): they replace the empire's designs, in file order. Each must
    // pass the design rules but the technology test (spec 03 §4.2, without
    // rule 2; OpenSE4 also skips the mount technology test of rule 5, which
    // stock mounts never fail, inferred) or it is dropped without a message;
    // a design whose parts the data set lacks was already left out with a
    // warning when the file was read. Each comes back current: not obsolete,
    // never built (so a prototype, editable), with its saved strategy and
    // creation date. A name another design has is renamed (an OpenSE4
    // choice, spec 03 Q50). The starting designs that the starting ships
    // use stay with the empire until spec 01 §3.6 ("no starting assets")
    // takes effect (inferred).
    for (size_t i = 0; i < n; ++i) {
        const auto& saved = setup.empires[i].designs;
        if (saved.empty()) continue;
        const EmpireId id{i};
        std::erase_if(s.empires[i].designs, [&](DesignId d) {
            return std::none_of(s.vehicles.begin(), s.vehicles.end(), [&](const Vehicle& v) { return v.owner == id && v.design == d; });
        });
        for (const Design& file : saved) {
            const auto& data = r.data();
            const bool known = file.hull < data.vehicleSizes.size() &&
                               std::all_of(file.entries.begin(), file.entries.end(), [&](const DesignEntry& e) {
                                   return e.component < data.components.size() &&
                                          (e.mount < 0 || static_cast<size_t>(e.mount) < data.weaponMounts.size());
                               });
            if (!known || file.name.empty()) continue;
            if (!computeDesignStats(r, nullptr, file.hull, file.entries).problems.empty()) continue;
            Design d;
            d.owner = id;
            d.name = designNameInUse(s, file.name) ? uniqueDesignName(s, file.name) : file.name;
            d.designType = file.designType;
            d.hull = file.hull;
            d.entries = file.entries;
            d.strategy = file.strategy < s.empires[i].strategies.size() ? file.strategy : 0;
            d.createdTurn = file.createdTurn;
            addDesign(s, std::move(d));
        }
    }

    sight::updateKnowledge(r, s);
    // The first-contact check runs once in every system when the game is
    // created, after the empires are placed (spec 05 §3.1, confirmed: binary).
    {
        TurnContext contact{r, s, {}, {}, {}};
        diplomacy::firstContactEverywhere(contact);
    }
    // Starting Resources plus one turn of production for the stockpile and
    // research, intelligence at 0 (spec 02 §9, spec 05 §1.1, confirmed: binary).
    research::openingPools(r, s);
    economy::updateReports(r, s);
    return s;
}

} // namespace opense4::game
