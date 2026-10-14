// The original's empire files (.emp) and map files (.map), docs/spec/08 §13,
// §14: OpenSE4's empires and maps through them and back, the version gates,
// damaged files, Add Existing's checks, what a new game takes from an empire
// file, and (opt-in) every empire file of the install:
//
//   OPENSE4_CLASSIC_DATA=auto ./opense4_tests -tc="classic files*"
//
// No file of the original is a fixture: the opt-in test reads the player's own.

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "game/classic_files.hpp"
#include "game/classic_save.hpp"
#include "game/generate.hpp"
#include "game/map_file.hpp"
#include "game/serialize.hpp"
#include "game/serialize_io.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::game::classic;
namespace fs = std::filesystem;

namespace {

// Two empires of Game Setup are the same when they serialize the same.
bool sameSetup(const EmpireSetup& a, const EmpireSetup& b) { return serial::encode(a) == serial::encode(b); }

std::vector<uint8_t> encodeEmpireOrFail(const ClassicEmpireFile& f) {
    auto bytes = encodeClassicEmpire(f);
    REQUIRE_MESSAGE(bytes.has_value(), (bytes ? std::string{} : bytes.error()));
    return std::move(*bytes);
}

ClassicEmpireFile decodeEmpireOrFail(std::span<const uint8_t> bytes, size_t traits) {
    auto f = decodeClassicEmpire(bytes, traits);
    REQUIRE_MESSAGE(f.has_value(), (f ? std::string{} : f.error()));
    return std::move(*f);
}

// export -> encode -> decode -> import.
EmpireSetup throughFile(const Rules& r, const EmpireSetup& e, ConversionReport& report, ClassicEmpireFile* written = nullptr,
                        EmpireExportOptions options = {}) {
    auto f = exportClassicEmpire(r, e, report, options);
    REQUIRE_MESSAGE(f.has_value(), (f ? std::string{} : f.error()));
    const std::vector<uint8_t> bytes = encodeEmpireOrFail(*f);
    const ClassicEmpireFile back = decodeEmpireOrFail(bytes, r.data().racialTraits.size());
    CHECK(back == *f);
    // Encoded again with the same keys: the same bytes.
    CHECK(encodeEmpireOrFail(back) == bytes);
    if (written) *written = back;
    auto imported = importClassicEmpire(r, back, report);
    REQUIRE_MESSAGE(imported.has_value(), (imported ? std::string{} : imported.error()));
    return std::move(*imported);
}

// An empire of Game Setup with something in every field an empire file carries.
EmpireSetup fullEmpire(const Rules& r) {
    EmpireSetup e;
    e.name = "Glass Choir";
    e.empireType = "Assembly";
    e.leaderTitle = "Speaker";
    e.leaderName = "Ada";
    Race race;
    race.name = "Choir";
    race.style = "choir";
    race.atmosphere = "Methane";
    race.nativeSurface = "Ice";
    race.biology = "Lives in cold halls.";
    race.society = "Sings.";
    race.history = "Long.";
    race.demeanor = "Calm";
    race.designNameFile = "Tests.txt";
    race.culture = static_cast<uint32_t>(r.data().cultures.size() - 1);
    race.happinessModel = static_cast<uint32_t>(r.data().happinessModels.size() - 1);
    race.traits = {0};
    race.characteristics[static_cast<size_t>(Characteristic::Cunning)] = 90;
    race.characteristics[static_cast<size_t>(Characteristic::Reproduction)] = 120;
    e.preset = race.style;
    e.customRace = race;
    e.email = "ada@example.org";
    e.useRaceMinisterStyle = true;
    e.experience = 1234;
    e.strategies = r.data().combatStrategies;
    e.strategies.push_back({"Close In", {{"Primary Movement Strategy", "Point Blank"}, {"Drones Per Target", "4"}}});
    EmpireSettings s;
    s.ministers = ministerBit(Minister::Research) | ministerBit(Minister::Colonization);
    s.ministersForNewVehicles = true;
    s.aiMinimalChanges = true;
    s.clearOrdersOnEncounter = EncounterClear::Any;
    s.avoidTaggedMinefields = false;
    s.chooseColonyType = false;
    s.interfaceOptions.warpPointNames = false;
    s.interfaceOptions.facilityMarkers = 0b101;
    s.interfaceOptions.planetsTab = 2;
    s.interfaceOptions.shipsShown = 3;
    s.interfaceOptions.designToHit = true;
    s.interfaceOptions.logFilter = 3;
    e.settings = s;
    e.designTypes = {"Attack Ship", "Colony Ship", "Scout"};
    e.colonyTypes = {"Mining", "Research"};
    e.repairPriorities = {"Weapons", "Engines"};
    Design d;
    d.name = "Long Reach";
    d.designType = "Attack Ship";
    d.hull = test::hullIndex(r, "Test Frigate");
    for (const char* part : {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"})
        d.entries.push_back({test::componentIndex(r, part), -1});
    d.strategy = 1;
    d.createdTurn = 31;
    e.designs = {d};
    return e;
}

} // namespace

// ---- Empire files ----------------------------------------------------------------------------------------------

TEST_CASE("classic files: an empire of Game Setup goes through the original's empire file and back") {
    const Rules& r = test::engineRules();
    const EmpireSetup e = fullEmpire(r);
    ConversionReport report;
    ClassicEmpireFile file;
    EmpireExportOptions options;
    options.player = 3;
    options.keySeed = 17;
    const EmpireSetup back = throughFile(r, e, report, &file, options);

    // What Save Empire writes (§13.2): the game state cleared.
    const EmpireRecord& x = file.empire;
    CHECK(file.version == kVersion);
    CHECK(x.player == 3);
    CHECK_FALSE(x.computer);
    CHECK_FALSE(x.neutral);
    CHECK_FALSE(x.destroyed);
    CHECK(x.techLevels.size() == r.data().techAreas.size());
    CHECK(std::all_of(x.techLevels.begin(), x.techLevels.end(), [](uint16_t l) { return l == 0; }));
    CHECK(x.stored == std::array<int32_t, 3>{});
    CHECK(x.log.empty());
    CHECK(x.fleets.empty());
    CHECK(std::all_of(x.politics.begin(), x.politics.end(), [](const PoliticsEntry& p) { return p.treaty == 3; }));
    CHECK(std::all_of(x.anger.begin(), x.anger.end(), [](uint8_t a) { return a == 50; }));
    CHECK(std::all_of(x.turnsSinceWar.begin(), x.turnsSinceWar.end(), [](uint16_t t) { return t == 999; }));
    CHECK(x.unused7 == std::array<uint8_t, 7>{0, 0, 0, 1, 1, 1, 0});
    CHECK(x.raceFolder == "choir");
    CHECK(x.artFolder == "choir");
    CHECK(x.atmosphere == 2);   // Methane
    CHECK(x.surface == 2);      // Ice
    CHECK(x.password.empty());
    REQUIRE(file.designs.size() == 1);
    CHECK(file.designs[0].id == 0);
    CHECK(file.designs[0].owner == 3);
    CHECK_FALSE(file.designs[0].obsolete);
    CHECK(file.designs[0].speed > 0);   // computed as for a saved game (§7.2)

    // Everything the file carries comes back.
    CHECK(back.name == e.name);
    CHECK(back.empireType == e.empireType);
    CHECK(back.leaderTitle == e.leaderTitle);
    CHECK(back.leaderName == e.leaderName);
    CHECK(back.kind == PlayerKind::Human);
    REQUIRE(back.customRace.has_value());
    const Race& race = *back.customRace;
    CHECK(race.style == "choir");
    CHECK(race.atmosphere == "Methane");
    CHECK(race.nativeSurface == "Ice");
    CHECK(race.biology == e.customRace->biology);
    CHECK(race.demeanor == "Calm");
    CHECK(race.designNameFile == "Tests.txt");
    CHECK(race.culture == e.customRace->culture);
    CHECK(race.happinessModel == e.customRace->happinessModel);
    CHECK(race.traits == e.customRace->traits);
    CHECK(race.characteristics == e.customRace->characteristics);
    CHECK(back.email == e.email);
    CHECK(back.useRaceMinisterStyle);
    CHECK(back.experience == 1234);
    REQUIRE(back.strategies.size() == e.strategies.size());
    CHECK(back.strategies.back().name == "Close In");
    REQUIRE(back.settings.has_value());
    CHECK(*back.settings == *e.settings);
    CHECK(back.designTypes == e.designTypes);
    CHECK(back.colonyTypes == e.colonyTypes);
    CHECK(back.repairPriorities == e.repairPriorities);
    REQUIRE(back.designs.size() == 1);
    CHECK(back.designs[0].name == "Long Reach");
    CHECK(back.designs[0].designType == "Attack Ship");
    CHECK(back.designs[0].hull == e.designs[0].hull);
    CHECK(back.designs[0].entries == e.designs[0].entries);
    CHECK(back.designs[0].strategy == 1);
    CHECK(back.designs[0].createdTurn == 31);

    // A second round changes nothing.
    ConversionReport again;
    CHECK(sameSetup(throughFile(r, back, again, nullptr, options), back));

    // Without its designs (Save Designs: No): a count of 0.
    options.designs = false;
    ClassicEmpireFile bare;
    ConversionReport none;
    CHECK(throughFile(r, e, none, &bare, options).designs.empty());
    CHECK(bare.designs.empty());
}

TEST_CASE("classic files: what an empire file cannot carry, and an empire without lists of its own") {
    const Rules& r = test::engineRules();
    EmpireSetup e = fullEmpire(r);
    // A password is only a check in OpenSE4: written empty, with a note (§7.4).
    e.passwordHash = hashPassword("secret");
    e.kind = PlayerKind::Computer;
    // An empire of Empire Setup: no settings, lists or strategies of its own.
    e.settings.reset();
    e.designTypes.clear();
    e.colonyTypes.clear();
    e.repairPriorities.clear();
    e.strategies.clear();
    ConversionReport report;
    ClassicEmpireFile file;
    const EmpireSetup back = throughFile(r, e, report, &file);
    CHECK(file.empire.password.empty());
    CHECK_FALSE(file.empire.computer);   // computer control off (§13.2)
    CHECK(back.passwordHash.empty());
    CHECK(back.kind == PlayerKind::Human);
    CHECK(std::any_of(report.notes.begin(), report.notes.end(), [](const std::string& n) { return n.find("password") != std::string::npos; }));
    // The data set's lists and strategies, as the game would give them; a new empire's settings.
    CHECK(file.empire.designTypes == r.data().names.designTypes);
    CHECK(back.designTypes == r.data().names.designTypes);
    CHECK(back.strategies.size() == r.data().combatStrategies.size());
    REQUIRE(back.settings.has_value());
    CHECK(*back.settings == EmpireSettings{});

    // A password in the file: OpenSE4's check of its trimmed lower-case text (§3.6.1).
    file.empire.password = " Secret ";
    ConversionReport withPassword;
    auto imported = importClassicEmpire(r, file, withPassword);
    REQUIRE(imported.has_value());
    CHECK(imported->passwordHash == hashPassword("secret"));
    // A computer-controlled one (never one Save Empire wrote) keeps the control and takes the data set's strategies.
    file.empire.computer = true;
    imported = importClassicEmpire(r, file, withPassword);
    REQUIRE(imported.has_value());
    CHECK(imported->kind == PlayerKind::Computer);
    CHECK(imported->strategies.empty());
    // A design whose hull is past the data set's records is left out with a note.
    file.empire.computer = false;
    file.designs.push_back(file.designs.empty() ? DesignRecord{} : file.designs.front());
    file.designs.back().name = "Too Far";
    file.designs.back().hull = static_cast<uint16_t>(r.data().vehicleSizes.size() + 1);
    ConversionReport far;
    imported = importClassicEmpire(r, file, far);
    REQUIRE(imported.has_value());
    CHECK(std::none_of(imported->designs.begin(), imported->designs.end(), [](const Design& d) { return d.name == "Too Far"; }));
    CHECK_FALSE(far.notes.empty());
}

TEST_CASE("classic files: empire files of older versions leave out what later releases added (§13.1)") {
    const Rules& r = test::engineRules();
    EmpireSetup e = fullEmpire(r);
    ConversionReport report;
    auto f = exportClassicEmpire(r, e, report);
    REQUIRE(f.has_value());
    f->empire.networkName = "lobby";
    f->empire.droneNameCounter = 4;
    f->empire.options.facilityMarkers2to12[0] = true;
    for (StrategyRecord& t : f->empire.strategies) t.dronesPerTarget = 7;
    REQUIRE_FALSE(f->designs.empty());
    f->designs[0].templateName = "Template";
    ClassicEmpireFile old = *f;
    old.version = "1.50";
    const std::vector<uint8_t> bytes = encodeEmpireOrFail(old);
    CHECK(bytes.size() < encodeEmpireOrFail(*f).size());
    const ClassicEmpireFile back = decodeEmpireOrFail(bytes, r.data().racialTraits.size());
    CHECK(back.version == "1.50");
    CHECK(back.empire.networkName == "lobby");         // 1.46 has it
    CHECK(back.empire.droneNameCounter == 0);          // 1.51
    CHECK_FALSE(back.empire.options.facilityMarkers2to12[0]);   // 1.78
    for (const StrategyRecord& t : back.empire.strategies) CHECK(t.dronesPerTarget == 3);   // 1.81
    CHECK(back.designs[0].templateName.empty());       // 1.62
    // Reading an empire older than 1.51 appends the two drone design types (§5.2).
    auto imported = importClassicEmpire(r, back, report);
    REQUIRE(imported.has_value());
    CHECK(imported->designTypes.size() == e.designTypes.size() + kOldDesignTypesAdded.size());
    CHECK(imported->designTypes.back() == kOldDesignTypesAdded.back());
    // 1.45: no network name either.
    old.version = "1.45";
    CHECK(decodeEmpireOrFail(encodeEmpireOrFail(old), r.data().racialTraits.size()).empire.networkName.empty());
    // Versions outside 1.40..1.95 are refused both ways.
    old.version = "1.13";
    CHECK_FALSE(encodeClassicEmpire(old).has_value());
}

TEST_CASE("classic files: damaged empire files and other data sets give clear messages") {
    const Rules& r = test::engineRules();
    ConversionReport report;
    auto f = exportClassicEmpire(r, fullEmpire(r), report);
    REQUIRE(f.has_value());
    std::vector<uint8_t> bytes = encodeEmpireOrFail(*f);
    const size_t traits = r.data().racialTraits.size();
    // Another trait count reads to the wrong end (§3.6.5); the counts that fit say which.
    CHECK_FALSE(decodeClassicEmpire(bytes, traits + 1).has_value());
    const std::vector<size_t> fits = empireTraitCountsThatDecode(bytes);
    CHECK(std::find(fits.begin(), fits.end(), traits) != fits.end());
    // A byte too many, or too few.
    std::vector<uint8_t> longer = bytes;
    longer.push_back(0);
    auto extra = decodeClassicEmpire(longer, traits);
    REQUIRE_FALSE(extra.has_value());
    CHECK(extra.error().find("left after") != std::string::npos);
    std::vector<uint8_t> shorter(bytes.begin(), bytes.end() - 3);
    CHECK_FALSE(decodeClassicEmpire(shorter, traits).has_value());
    CHECK_FALSE(decodeClassicEmpire(std::vector<uint8_t>{'O', 'S', 'E', '4'}, traits).has_value());
    // A saved game is not an empire file.
    ExportOptions saveOptions;
    auto save = exportClassicSave(r, test::newEngineGame(3, 2, 8), report, saveOptions);
    REQUIRE(save.has_value());
    auto saveBytes = encodeClassicSave(*save);
    REQUIRE(saveBytes.has_value());
    CHECK_FALSE(decodeClassicEmpire(*saveBytes, traits).has_value());

    // On disk: a data set with another number of racial traits names the count.
    const test::TempDir tmp("classic_files_empires");
    const fs::path file = tmp / "glass choir.emp";
    REQUIRE(writeClassicEmpire(r, fullEmpire(r), file, report).has_value());
    ruleset::Ruleset more = test::buildEngineRuleset();
    more.racialTraits.push_back(more.racialTraits.front());
    more.racialTraits.back().name = "Extra Trait";
    const Rules other(std::move(more));
    auto loaded = readClassicEmpire(other, file, report);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().find(std::format("{} racial traits", traits)) != std::string::npos);
    auto good = readClassicEmpire(r, file, report);
    REQUIRE_MESSAGE(good.has_value(), (good ? std::string{} : good.error()));
    CHECK(good->empire.name == "Glass Choir");
}

TEST_CASE("classic files: Add Existing checks the empire's technology and racial points (§13.4)") {
    const Rules& r = test::engineRules();
    GameOptions o;
    o.racialPoints = 2000;
    EmpireSetup e = fullEmpire(r);
    const int cost = racialPointCost(r, *e.customRace);
    o.racialPoints = cost;
    // Save Empire's files have every level at 0: the check passes.
    const std::vector<uint16_t> zero(r.data().techAreas.size(), 0);
    CHECK_FALSE(addExistingProblem(r, o, e, zero).has_value());
    CHECK_FALSE(addExistingProblem(r, o, e, {}).has_value());
    // Levels beyond what a new player gets: refused.
    std::vector<uint16_t> high(r.data().techAreas.size(), 0);
    for (size_t i = 0; i < high.size(); ++i) high[i] = static_cast<uint16_t>(std::max(0, r.data().techAreas[i].maxLevel));
    o.startTechLevel = 0;
    REQUIRE(empireTechSum(r, high) > allowedTechSum(r, o));
    auto tech = addExistingProblem(r, o, e, high);
    REQUIRE(tech.has_value());
    CHECK(tech->find("technology") != std::string::npos);
    // At High every area's maximum is allowed; a level above it counts as the maximum.
    o.startTechLevel = 2;
    for (uint16_t& l : high) l = static_cast<uint16_t>(l + 5);
    CHECK(empireTechSum(r, high) == allowedTechSum(r, o));
    CHECK_FALSE(addExistingProblem(r, o, e, high).has_value());
    // Removed areas give nothing.
    o.techAreasAllowed.assign(r.data().techAreas.size(), 0);
    if (std::any_of(r.data().techAreas.begin(), r.data().techAreas.end(), [](const ruleset::TechArea& a) { return a.canBeRemoved && a.maxLevel > 0; }))
        CHECK(addExistingProblem(r, o, e, high).has_value());
    // Too many racial points: refused, and that message wins when both fail.
    o.racialPoints = cost - 1;
    o.startTechLevel = 0;
    auto points = addExistingProblem(r, o, e, high);
    REQUIRE(points.has_value());
    CHECK(points->find("racial points") != std::string::npos);
}

TEST_CASE("classic files: a new game gives an empire what its empire file brought") {
    const Rules& r = test::engineRules();
    GameSetup g;
    g.seed = 5;
    g.options.systemCount = 10;
    g.options.racialPoints = 1'000'000;
    EmpireSetup first = fullEmpire(r);
    first.customRace->characteristics = Race{}.characteristics;
    EmpireSetup plain;
    plain.name = "Plain";
    g.empires = {first, plain};
    auto s = createGame(r, g);
    REQUIRE_MESSAGE(s.has_value(), (s ? std::string{} : s.error()));
    const Empire& a = s->empires[0];
    CHECK(a.ministers == first.settings->ministers);
    CHECK(a.ministersForNewVehicles);
    CHECK(a.aiMinimalChanges);
    CHECK(a.clearOrdersOnEncounter == EncounterClear::Any);
    CHECK_FALSE(a.avoidTaggedMinefields);
    CHECK_FALSE(a.chooseColonyType);
    CHECK_FALSE(a.interfaceOptions.warpPointNames);
    CHECK(a.interfaceOptions.facilityMarkers == 0b101);
    CHECK(a.designTypes == first.designTypes);
    CHECK(a.colonyTypes == first.colonyTypes);
    CHECK(a.repairPriorities == first.repairPriorities);
    CHECK(a.email == first.email);
    // An empire of Empire Setup starts as before: a new empire's settings, the data set's lists.
    const Empire& b = s->empires[1];
    const Empire fresh;
    CHECK(b.ministers == fresh.ministers);
    CHECK(b.interfaceOptions == fresh.interfaceOptions);
    CHECK(b.designTypes == r.data().names.designTypes);
    CHECK(b.colonyTypes == r.data().names.colonyTypes);
    CHECK(b.repairPriorities == r.data().names.repairPriorities);
    // Save Empire keeps them, without this game's log place.
    Empire played = a;
    played.interfaceOptions.logPosition = 9;
    const EmpireSettings kept = settingsOf(played);
    CHECK(kept.interfaceOptions.logPosition == 0);
    CHECK(kept.ministers == a.ministers);
}

TEST_CASE("classic files: the file name Save for SE IV gives") {
    CHECK(classicFileName("Terran Confederation", kEmpireFileExtension) == "terran confederation.emp");
    CHECK(classicFileName("  Two Suns: II ", kMapFileExtension) == "two suns ii.map");
    CHECK(classicFileName("", kEmpireFileExtension) == "empire.emp");
    CHECK(classicFileName("///", kMapFileExtension) == "map.map");
}

// ---- Map files ----------------------------------------------------------------------------------------------

namespace {

QuadrantMap generatedMap(uint64_t seed) {
    const Rules& r = test::engineRules();
    Rng rng(seed);
    QuadrantOptions q;
    q.systemCount = 9;
    auto g = generateQuadrant(r.data(), q, rng);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    QuadrantMap m;
    m.name = "Test Map";
    m.galaxy = std::move(g->galaxy);
    m.galaxy.quadrantType.clear();   // not in the original's map file
    // A specific point for player 2, a common one, and that common one again (dropped on reading).
    m.startingPoints.push_back({SystemId{0u}, Sector{3, 4}, 1});
    m.startingPoints.push_back({SystemId{1u}, Sector{6, 6}, kCommonStart});
    return m;
}

QuadrantMap throughMapFile(const QuadrantMap& m, ClassicMapFile* written = nullptr, ConversionReport* notes = nullptr) {
    const Rules& r = test::engineRules();
    ConversionReport report;
    auto f = exportClassicMap(r.data(), m, report, 23);
    REQUIRE_MESSAGE(f.has_value(), (f ? std::string{} : f.error()));
    auto bytes = encodeClassicMap(*f);
    REQUIRE_MESSAGE(bytes.has_value(), (bytes ? std::string{} : bytes.error()));
    auto back = decodeClassicMap(*bytes);
    REQUIRE_MESSAGE(back.has_value(), (back ? std::string{} : back.error()));
    CHECK(*back == *f);
    auto again = encodeClassicMap(*back);
    REQUIRE(again.has_value());
    CHECK(*again == *bytes);
    if (written) *written = *back;
    auto map = importClassicMap(r.data(), *back, notes ? *notes : report, m.name);
    REQUIRE_MESSAGE(map.has_value(), (map ? std::string{} : map.error()));
    return std::move(*map);
}

} // namespace

TEST_CASE("classic files: a quadrant goes through the original's map file and back") {
    const Rules& r = test::engineRules();
    const QuadrantMap m = generatedMap(41);
    ClassicMapFile file;
    const QuadrantMap back = throughMapFile(m, &file);
    // The map form (§14.1): no knowledge, no colonies; every stellar object of the systems.
    for (const SystemRecord& y : file.systems) {
        CHECK(y.explored.capacity == 0);
        CHECK(y.notes[0].empty());
    }
    for (const ObjectRecord& o : file.objects) CHECK_FALSE(o.colony.has_value());
    CHECK(file.objects.size() == m.galaxy.objects.size());
    CHECK(file.specificStarts.size() == 1);
    CHECK(file.commonStarts.size() == 1);
    // OpenSE4's own map file of both is the same text: systems, objects, warp
    // links, starting points; the file holds ability values as numbers, so an
    // empty one comes back as 0.
    QuadrantMap numbers = m;
    auto zero = [](std::vector<ruleset::Ability>& list) {
        for (ruleset::Ability& a : list) {
            if (a.value1.empty()) a.value1 = "0";
            if (a.value2.empty()) a.value2 = "0";
        }
    };
    for (StarSystem& sys : numbers.galaxy.systems) zero(sys.abilities);
    for (SpaceObject& o : numbers.galaxy.objects) zero(o.abilities);
    CHECK(mapToText(r.data(), back) == mapToText(r.data(), numbers));
    CHECK(back.startingPoints == m.startingPoints);
    size_t links = 0;
    for (const SpaceObject& o : back.galaxy.objects)
        if (o.kind == ObjectKind::WarpPoint && o.destination.valid()) {
            ++links;
            CHECK(back.galaxy.object(o.destination).destination == o.id);
        }
    CHECK(links > 0);

    // On disk, beside OpenSE4's maps.
    const test::TempDir tmp("classic_files_maps");
    ConversionReport report;
    const fs::path path = tmp / classicFileName(m.name, kMapFileExtension);
    REQUIRE(writeClassicMap(r.data(), m, path, report).has_value());
    auto read = readClassicMap(r.data(), path, report);
    REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error()));
    CHECK(read->name == "test map");   // the file's name
    CHECK(read->galaxy.systems.size() == m.galaxy.systems.size());
}

TEST_CASE("classic files: reading a map file (§14.2)") {
    const Rules& r = test::engineRules();
    QuadrantMap m = generatedMap(43);
    ClassicMapFile file;
    throughMapFile(m, &file);
    // Exact duplicate starting points are dropped.
    file.commonStarts.push_back(file.commonStarts.front());
    file.specificStarts.push_back(file.specificStarts.front());
    // A blank slot and an object of another class are left out; the rest are numbered by position.
    ObjectRecord blank;
    blank.cls = blank.recordClass = static_cast<uint8_t>(ObjectClass::Storm);
    blank.id = 999;
    file.objects.insert(file.objects.begin(), blank);
    ObjectRecord ship;
    ship.cls = ship.recordClass = static_cast<uint8_t>(ObjectClass::Ship);
    ship.owner = 1;
    ship.design = 1;
    ship.system = 1;
    file.objects.push_back(ship);
    ConversionReport report;
    auto map = importClassicMap(r.data(), file, report, "Edited");
    REQUIRE_MESSAGE(map.has_value(), (map ? std::string{} : map.error()));
    CHECK(map->startingPoints.size() == 2);
    CHECK(map->galaxy.objects.size() == m.galaxy.objects.size());
    for (size_t i = 0; i < map->galaxy.objects.size(); ++i) CHECK(map->galaxy.objects[i].id.index() == i);
    CHECK_FALSE(report.notes.empty());
    // A record past the data set's SectType.txt names it.
    ClassicMapFile broken = file;
    broken.objects[1].sectorType = static_cast<uint16_t>(r.data().sectorObjectTypes.size() + 5);
    auto refused = importClassicMap(r.data(), broken, report);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().find("SectType.txt") != std::string::npos);
    // A game started on the imported map places player 2 on its point (spec 01 §12).
    GameSetup g;
    g.seed = 3;
    g.map = *map;
    EmpireSetup a, b;
    a.name = "First";
    b.name = "Second";
    g.empires = {a, b};
    auto game = createGame(r, g);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    CHECK(game->empires[1].homeSystem == SystemId{0u});
    CHECK(game->empires[1].homeSector == (Sector{3, 4}));
}

// ---- The install's empire files (opt-in) -------------------------------------------------------------------------

namespace {

const Rules* installRules() {
    static const std::unique_ptr<Rules> rules = []() -> std::unique_ptr<Rules> {
        const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
        if (!env) return nullptr;
        auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? fs::path{} : fs::path(env));
        if (!dir) return nullptr;
        auto loaded = ruleset::loadRuleset(*dir);
        if (!loaded.ruleset) return nullptr;
        return std::make_unique<Rules>(std::move(*loaded.ruleset), dir->parent_path());
    }();
    return rules.get();
}

std::vector<fs::path> empireFiles(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (e.is_regular_file(ec) && ext == kEmpireFileExtension) out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

// Every empire file of the install's Empires folder decodes to its end,
// encodes back to the same bytes, imports, and comes back the same from an
// export and an import (spec 08 §13.6); a game begins with some of them.
TEST_CASE("classic files: the installed empire files import and round-trip (opt-in)") {
    const Rules* r = installRules();
    if (!r || r->gameRoot().empty()) return;
    const std::vector<fs::path> files = empireFiles(ruleset::childIgnoringCase(r->gameRoot(), "Empires"));
    if (files.empty()) {
        MESSAGE("skipped: no Empires/*.emp in the install");
        return;
    }
    const size_t traits = r->data().racialTraits.size();
    std::vector<EmpireSetup> imported;
    for (const fs::path& path : files) {
        INFO(path.filename().string());
        auto bytes = readFileBytes(path);
        REQUIRE(bytes.has_value());
        auto file = decodeClassicEmpire(*bytes, traits);
        REQUIRE_MESSAGE(file.has_value(), (file ? std::string{} : file.error()));
        CHECK(readableVersion(file->version));
        CHECK(file->empire.techLevels.size() == r->data().techAreas.size());
        // The codec keeps every field: encoded with its own keys, the same bytes.
        auto again = encodeClassicEmpire(*file);
        REQUIRE(again.has_value());
        CHECK(*again == *bytes);
        ConversionReport report;
        auto empire = importClassicEmpire(*r, *file, report);
        REQUIRE_MESSAGE(empire.has_value(), (empire ? std::string{} : empire.error()));
        CHECK(empire->name == file->empire.name);
        // Export, then import: the same empire (a password cannot come back, §7.4).
        EmpireSetup want = *empire;
        want.passwordHash.clear();
        EmpireExportOptions options;
        options.player = file->empire.player;
        options.keySeed = 5;
        const EmpireSetup back = throughFile(*r, *empire, report, nullptr, options);
        CHECK(sameSetup(back, want));
        // Add Existing takes it at the default setup (its levels are all 0).
        GameOptions o;
        o.racialPoints = 5000;
        CHECK_FALSE(addExistingProblem(*r, o, *empire, file->empire.techLevels).has_value());
        imported.push_back(std::move(*empire));
    }
    MESSAGE(files.size() << " empire files");
    // A game of the first four: one human, three computer players.
    GameSetup g;
    g.seed = 11;
    g.options.racialPoints = 1'000'000;
    for (size_t i = 0; i < imported.size() && i < 4; ++i) {
        EmpireSetup e = imported[i];
        e.kind = i == 0 ? PlayerKind::Human : PlayerKind::Computer;
        g.empires.push_back(std::move(e));
    }
    auto game = createGame(*r, g);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    CHECK(validateState(*game, r).empty());
    CHECK(game->empires[0].designTypes == imported[0].designTypes);
}
