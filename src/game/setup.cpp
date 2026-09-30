#include "game/setup.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/generate.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

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

// Facilities a homeworld starts with (spec 02 §9: the stock set is not
// documented). Calibrated against the observed Quick Start homeworld, which
// fills a medium planet's 15 slots (docs/spec/07, Calibration): a yard, a
// spaceport, a depot, 5 mineral, 1 organic and 1 radioactive producer and 5
// research facilities. Placed in this order until the slots run out, so
// smaller homeworlds keep a balanced mix. Each entry is an ability and how
// many facilities with it to place.
constexpr std::array<std::pair<AbilityKind, int>, 15> kHomeFacilities{{
    {AbilityKind::SpaceYard, 1},
    {AbilityKind::Spaceport, 1},
    {AbilityKind::SupplyGeneration, 1},
    {AbilityKind::ResourceGenMinerals, 1},
    {AbilityKind::ResourceGenOrganics, 1},
    {AbilityKind::ResourceGenRadioactives, 1},
    {AbilityKind::PointGenResearch, 1},
    {AbilityKind::ResourceGenMinerals, 1},
    {AbilityKind::PointGenResearch, 1},
    {AbilityKind::ResourceGenMinerals, 1},
    {AbilityKind::PointGenResearch, 1},
    {AbilityKind::ResourceGenMinerals, 1},
    {AbilityKind::PointGenResearch, 1},
    {AbilityKind::ResourceGenMinerals, 1},
    {AbilityKind::PointGenResearch, 1},
}};

Vehicle makeVehicle(const Rules& r, GameState& s, const Design& d, Location where, int number) {
    Vehicle v;
    v.owner = d.owner;
    v.design = d.id;
    v.name = std::format("{} {}", d.name, number);
    v.location = where;
    v.damage.assign(d.entries.size(), 0);
    v.builtTurn = s.turn;
    v.supply = computeDesignStats(r, nullptr, d).supplyCapacity;
    return v;
}

} // namespace

uint32_t defaultEmpireColor(size_t index) {
    static constexpr std::array<uint32_t, 20> kColors{
        0x3a7bd5, 0xd53a3a, 0x3ad56b, 0xd5c83a, 0xa03ad5, 0x3ad5cf, 0xd5823a, 0xd53aa6, 0x8fd53a, 0x6b6bd5,
        0x9a6b3a, 0xb0b0b0, 0x1f5f8f, 0x8f1f1f, 0x1f8f3f, 0x8f7f1f, 0x5f1f8f, 0x1f8f8a, 0x8f4f1f, 0x8f1f6a};
    return kColors[index % kColors.size()];
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
    int64_t total = 0;
    for (size_t i = 0; i < kCharacteristics; ++i) {
        const std::string name{displayName(static_cast<Characteristic>(i))};
        const int64_t c = r.setting(std::format("Characteristic {} Pct Cost", name), 0);
        const int64_t t = r.setting(std::format("Characteristic {} Threshold", name), 1000);
        const int64_t pos = r.setting(std::format("Characteristic {} Threshhold Pct Cost Pos", name), 100);
        const int64_t neg = r.setting(std::format("Characteristic {} Threshhold Pct Cost Neg", name), 100);
        const int64_t d = race.characteristics[i] - 100;
        if (d > 0) total += c * std::min(d, t) + c * pos / 100 * std::max<int64_t>(0, d - t);
        if (d < 0) total -= c * std::min(-d, t) + c * neg / 100 * std::max<int64_t>(0, -d - t);
    }
    for (uint32_t ti : race.traits) total += r.data().racialTraits[ti].cost;
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

    // ---- Quadrant.
    QuadrantOptions qo;
    qo.quadrantType = s.options.quadrantType;
    qo.systemCount = s.options.systemCount;
    qo.allWarpPointsConnected = s.options.allWarpPointsConnected;
    qo.noWarpPoints = s.options.noWarpPoints;
    qo.warpPointsAnywhere = s.options.warpPointsAnywhere;
    qo.noRuins = s.options.noRuins;
    qo.finiteResources = s.options.finiteResources;
    Rng galaxyRng = s.rng.fork();
    auto generated = generateQuadrant(r.data(), qo, galaxyRng);
    if (!generated) return std::unexpected(generated.error());
    s.galaxy = std::move(generated->galaxy);
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
        e.stockpile = s.options.startingResources;
        e.techLevels = startingTechLevels(r, s.options, e.race);
        e.relations.assign(n, Relation{});
        e.knowledge.explored.assign(s.galaxy.systems.size(), s.options.allSystemsSeen ? 1 : 0);
        e.knowledge.present.assign(s.galaxy.systems.size(), 0);
        e.knowledge.lastSeen.assign(s.galaxy.systems.size(), 0);
        e.knowledge.knownWarpLink.assign(s.galaxy.objects.size(), s.options.allSystemsSeen ? 1 : 0);
        e.knowledge.notes.assign(s.galaxy.systems.size(), {});
        e.strategies = r.data().combatStrategies;
        if (e.strategies.empty()) e.strategies.push_back({"Default", {}});
        e.designTypes = r.data().names.designTypes;
        e.colonyTypes = r.data().names.colonyTypes;
        e.repairPriorities = r.data().names.repairPriorities;
        s.empires.push_back(std::move(e));
        starts.push_back({s.empires.back().name, s.empires.back().race.nativeSurface, s.empires.back().race.atmosphere});
    }

    // ---- Homeworlds.
    PlacementOptions po;
    po.evenlyDistributed = s.options.evenlyDistributed;
    po.allowSameSystem = s.options.sameSystemAllowed;
    po.homeValue = s.options.homePlanetValue == 0 ? HomeValue::Low : s.options.homePlanetValue == 2 ? HomeValue::High : HomeValue::Medium;
    po.finiteResources = s.options.finiteResources;
    auto homes = placeHomeworlds(s.galaxy, r.data(), starts, po, s.rng);
    if (!homes) return std::unexpected(homes.error());
    // Placement may add a planet to a start system without one.
    s.colonies.resize(s.galaxy.objects.size());
    for (Empire& e : s.empires) e.knowledge.knownWarpLink.resize(s.galaxy.objects.size(), s.options.allSystemsSeen ? 1 : 0);

    for (size_t i = 0; i < n; ++i) {
        Empire& e = s.empires[i];
        const ObjectId home = (*homes)[i];
        Colony c;
        c.planet = home;
        c.owner = e.id;
        c.homeworld = true;
        c.colonyType = "Homeworld";
        c.foundedTurn = 0;
        s.colonies[home.index()] = c;
        Colony& col = *s.colonies[home.index()];
        col.population.push_back({e.id, 0});
        col.population.front().millions = maxPopulation(r, s, col);
        // Starting facilities.
        const int slots = facilitySlots(r, s, col);
        for (const auto& [kind, count] : kHomeFacilities)
            for (int k = 0; k < count && static_cast<int>(col.facilities.size()) < slots; ++k)
                if (auto f = r.bestFacilityWith(e, kind)) col.facilities.push_back(*f);
        const SystemId sys = s.galaxy.object(home).system;
        e.knowledge.explored[sys.index()] = 1;
        e.claimedSystems.push_back(sys);
    }

    // Extra starting planets: nearest uncolonized planets of the native type (spec 01 §3.6).
    for (int extra = 1; extra < s.options.startingPlanets; ++extra)
        for (size_t i = 0; i < n; ++i) {
            Empire& e = s.empires[i];
            const SystemId homeSys = s.galaxy.object((*homes)[i]).system;
            std::vector<SystemId> frontier{homeSys};
            std::vector<uint8_t> seen(s.galaxy.systems.size(), 0);
            seen[homeSys.index()] = 1;
            std::optional<ObjectId> pick;
            for (size_t f = 0; f < frontier.size() && !pick; ++f) {
                for (ObjectId o : s.galaxy.system(frontier[f]).objects) {
                    const SpaceObject& obj = s.galaxy.object(o);
                    if (obj.kind == ObjectKind::Planet && !s.colony(o) && keysEqual(obj.surface, e.race.nativeSurface)) {
                        pick = o;
                        break;
                    }
                }
                for (SystemId nb : s.galaxy.neighbors(frontier[f]))
                    if (!seen[nb.index()]) {
                        seen[nb.index()] = 1;
                        frontier.push_back(nb);
                    }
            }
            if (!pick) continue;
            Colony c;
            c.planet = *pick;
            c.owner = e.id;
            c.colonyType = "Balanced";
            c.population.push_back({e.id, 0});
            s.colonies[pick->index()] = c;
            Colony& col = *s.colonies[pick->index()];
            col.population.front().millions = std::max<int64_t>(1, maxPopulation(r, s, col) / 4);
            e.knowledge.explored[s.galaxy.object(*pick).system.index()] = 1;
        }

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
            d->name = st.name;
            d->designType = st.designType;
            d->owner = id;
            const DesignId did = addDesign(s, std::move(*d));
            for (int k = 0; k < st.ships; ++k) {
                Vehicle v = makeVehicle(r, s, s.design(did), home, k + 1);
                ++s.design(did).built;
                s.addVehicle(std::move(v));
            }
        }
    }

    sight::updateKnowledge(r, s);
    economy::updateReports(r, s);
    return s;
}

} // namespace opense4::game
