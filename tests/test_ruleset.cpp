#include "datafile/datafile.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>

using namespace opense4;

namespace {

const std::filesystem::path kFixture = std::filesystem::path(OPENSE4_FIXTURE_DIR) / "minimal_dataset";

bool mentions(const std::vector<std::string>& messages, std::string_view needle) {
    return std::any_of(messages.begin(), messages.end(), [&](const std::string& m) { return m.find(needle) != std::string::npos; });
}

} // namespace

TEST_CASE("data files: header ignored, records split on the first key") {
    const auto f = datafile::parse("Docs mention Name := Fake here.\r\n*BEGIN*\r\n====\r\n"
                                   "Name := Alpha\r\nSize   :=   3  \r\nNote :=\r\n\r\n"
                                   "Name := Beta\r\nSize := 4\r\n*END*\r\nName := After End\r\n",
                                   "Test.txt");
    REQUIRE(f.hasDataSection);
    REQUIRE(f.records.size() == 2);
    CHECK(f.records[0].fields.size() == 3);
    CHECK(f.records[0].find("size")->value == "3");      // keys are case-insensitive, values trimmed
    CHECK(f.records[0].find("Note")->value.empty());     // empty values are allowed
    CHECK(f.records[1].find("Name")->value == "Beta");
    CHECK(f.records[1].line == 8);

    const auto list = datafile::parse("Alpha\n\n  Beta  \nGamma", "List.txt");
    CHECK_FALSE(list.hasDataSection);
    CHECK(list.entries == std::vector<std::string>{"Alpha", "Beta", "Gamma"});
}

TEST_CASE("data files: key normalization and Latin-1 text") {
    CHECK(datafile::keysEqual("Tech Area  Req 1", "tech area req 1"));
    CHECK(datafile::keysEqual("  Max Warp Points per Sys", "Max\tWarp Points Per Sys"));
    CHECK_FALSE(datafile::keysEqual("Cost Minerals", "Cost Mineral"));
    CHECK(datafile::normalizeKey("  Level   Cost ") == "level cost");
    CHECK(datafile::latin1ToUtf8("caf\xe9") == "caf\xc3\xa9");
}

TEST_CASE("ruleset: the fixture data set loads completely") {
    const auto result = ruleset::loadRuleset(kFixture);
    std::string all;
    for (const auto& e : result.diagnostics.errors) all += e + "\n";
    INFO(all);
    REQUIRE(result.ruleset);
    CHECK(result.diagnostics.errors.empty());
    CHECK(result.diagnostics.unreadFields.empty());
    const ruleset::Ruleset& rs = *result.ruleset;

    // A tech area may require one defined later in the file.
    REQUIRE(rs.techAreas.size() == 2);
    const auto physics = rs.findTechArea("applied physics");
    REQUIRE(physics);
    REQUIRE(rs.techAreas[0].requirements.size() == 1);
    CHECK(rs.techAreas[0].requirements[0].area == *physics);
    CHECK(rs.techAreas[0].requirements[0].level == 2);
    CHECK_FALSE(rs.techAreas[1].canBeRemoved);

    const ruleset::VehicleSize* hull = rs.findVehicleSize("Test Cutter");
    REQUIRE(hull);
    CHECK(hull->type == ruleset::VehicleType::Ship);
    CHECK(hull->mustHaveBridge);
    CHECK(hull->maxEngines == 4);

    const ruleset::Component* engine = rs.findComponent("Spark Drive");
    const ruleset::Component* beam = rs.findComponent("Needle Beam");
    REQUIRE(engine);
    REQUIRE(beam);
    CHECK_FALSE(engine->isWeapon());
    CHECK(engine->vehicles == (ruleset::maskOf(ruleset::VehicleType::Ship) | ruleset::maskOf(ruleset::VehicleType::Base) |
                               ruleset::maskOf(ruleset::VehicleType::Drone)));
    REQUIRE(engine->abilities.size() == 1);
    CHECK(engine->abilities[0].type == "Standard Ship Movement");
    CHECK(engine->abilities[0].number1() == 1);
    CHECK(engine->abilities[0].number2() == 0);  // blank value
    CHECK(beam->weapon.kind == ruleset::WeaponKind::DirectFire);
    CHECK(beam->weapon.damageAtRange == std::vector<int>{12, 12, 8, 0, 0});
    CHECK(beam->maxPerVehicle == 2);

    REQUIRE(rs.quadrantTypes.size() == 2);
    REQUIRE(rs.quadrantTypes[0].systemTypeChances.size() == 2);
    CHECK(rs.quadrantTypes[0].systemTypeChances[0].first == *rs.findSystemType("Test Single Star"));
    CHECK(rs.quadrantTypes[0].systemTypeChances[1].first == *rs.findSystemType("test busy system"));
    CHECK(rs.quadrantTypes[0].systemTypeChances[1].second == 400);
    CHECK(rs.systemTypes[0].objects.size() == 2);
    CHECK(rs.racialTraits[0].restrictedTraits == std::vector<std::string>{"Day Eyes"});
    CHECK(rs.happinessModels[0].triggers.size() == 2);
    CHECK(rs.intelProjects[0].targetMessages[0].title == "Spies!");
    CHECK(rs.formations[0].positions.size() == 2);

    CHECK(rs.settings.integer("number of space combat turns") == 30);
    CHECK_FALSE(rs.settings.boolean("Bases Can Join Fleets", true));
    CHECK(rs.names.designTypes == std::vector<std::string>{"Attack Ship", "Scout Ship"});
    CHECK(rs.names.empireNames.size() == 2);
}

TEST_CASE("ruleset: problems are reported with file, line and record") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "opense4_bad_classic_data";
    fs::remove_all(dir);
    fs::copy(kFixture, dir);
    {
        std::ofstream out(dir / "Components.txt", std::ios::trunc);
        out << "*BEGIN*\nName := Broken Gun\nTonnage Space Taken := ten\nTonnage Structure := 5\n"
               "Cost Minerals := 1\nCost Organics := 0\nCost Radioactives := 0\nVehicle Type := Ship\\Starbase\n"
               "Number of Tech Req := 1\nTech Area Req 1 := Warp Theory\nTech Level Req 1 := 1\n"
               "Weapon Type := Laser\nSpeeed := 9\n*END*\n";
    }
    const auto result = ruleset::loadRuleset(dir);
    fs::remove_all(dir);
    const auto& errors = result.diagnostics.errors;
    CHECK(mentions(errors, "Components.txt:3: 'Tonnage Space Taken' should be a whole number, not 'ten'"));
    CHECK(mentions(errors, "unknown vehicle type 'Starbase'"));
    CHECK(mentions(errors, "Components.txt:2 [Broken Gun]: unknown tech area 'Warp Theory'"));
    CHECK(mentions(errors, "unknown weapon type 'Laser'"));
    CHECK(result.diagnostics.unreadFields.contains("Components.txt: Speeed"));
}

TEST_CASE("ruleset: a player's installed classic data set (opt-in)") {
    // Set OPENSE4_CLASSIC_DATA to a Data directory, or "auto" to search Steam libraries.
    // The data belongs to the player and is never part of this repository.
    const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
    if (!env) {
        MESSAGE("skipped: set OPENSE4_CLASSIC_DATA to test against an installed data set");
        return;
    }
    const auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
    REQUIRE_MESSAGE(dir, "no data set found");
    const auto result = ruleset::loadRuleset(*dir);
    std::string all;
    for (const auto& e : result.diagnostics.errors) all += e + "\n";
    INFO(all);
    REQUIRE(result.ruleset);
    CHECK(result.diagnostics.errors.empty());
    CHECK(result.ruleset->components.size() > 100);
    CHECK(result.ruleset->techAreas.size() > 20);
}
