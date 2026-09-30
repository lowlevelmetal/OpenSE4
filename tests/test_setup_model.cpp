// The New Game front end's model (src/client/classic/screens/setup_model.*):
// setup pages -> game::GameSetup, racial points, trait rules, empire files.
// Race presets are invented for these tests and written to a temporary folder.

#include "engine_fixture.hpp"

#include "client/classic/screens/setup_model.hpp"
#include "datafile/datafile.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace opense4;
using namespace opense4::client::classic;
namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// A preset folder: <root>/Pictures/<Races|RaceNeutral>/<folder>/<folder>_AI_General.txt (+ _AI_Settings.txt).
void writePreset(const fs::path& root, bool neutral, const std::string& folder, const std::string& name, const std::string& surface,
                 const std::string& atmosphere, int group, const std::string& tiers) {
    const fs::path dir = root / "Pictures" / (neutral ? "RaceNeutral" : "Races") / folder;
    writeFile(dir / (folder + "_AI_General.txt"), std::format(R"(Invented race preset for opense4 tests.
*BEGIN*
Name                   := {0}
Empire Name            := {0} League
Empire Type            := League
Emperor Name           := Tester
Emperor Title          := Chair
Demeanor               := Calm
Culture                := Steady
Happiness Type         := Test Mood
Planet Type            := {1}
Atmosphere             := {2}
Design Name File       := Tests.txt
{3}
*END*
)",
                                                              name, surface, atmosphere, tiers));
    writeFile(dir / (folder + "_AI_Settings.txt"), std::format("Invented.\n*BEGIN*\nPersonality Group := {}\n*END*\n", group));
}

struct SetupRules {
    fs::path root;
    std::unique_ptr<game::Rules> rules;
};

const game::Rules& setupRules() {
    static const SetupRules r = [] {
        SetupRules out;
        out.root = fs::temp_directory_path() / "opense4_setup_model_test";
        fs::remove_all(out.root);
        // Tier 1: Intelligence 110 (250 points); tier 2: + Night Eyes (750); tier 3: Intelligence 150,
        // Cunning 115, Night Eyes (5200).
        const std::string tiers = R"(Race Opt 1 Num Characteristics := 1
Race Opt 1 Characteristic 1 Type := Intelligence
Race Opt 1 Characteristic 1 Amount := 110
Race Opt 1 Num Advanced Traits := 0
Race Opt 2 Num Characteristics := 1
Race Opt 2 Characteristic 1 Type := Intelligence
Race Opt 2 Characteristic 1 Amount := 110
Race Opt 2 Num Advanced Traits := 1
Race Opt 2 Adv Trait 1 := Night Eyes
Race Opt 3 Num Characteristics := 2
Race Opt 3 Characteristic 1 Type := Intelligence
Race Opt 3 Characteristic 1 Amount := 150
Race Opt 3 Characteristic 2 Type := Cunning
Race Opt 3 Characteristic 2 Amount := 115
Race Opt 3 Num Advanced Traits := 1
Race Opt 3 Adv Trait 1 := Night Eyes)";
        writePreset(out.root, false, "Alpha", "Alphan", "Rock", "Oxygen", 1, tiers);
        writePreset(out.root, false, "Beta", "Betan", "Ice", "Methane", 1, tiers);
        writePreset(out.root, false, "Gamma", "Gamman", "Rock", "Oxygen", 2, tiers);
        writePreset(out.root, false, "Delta", "Deltan", "Rock", "Oxygen", 2, tiers);
        writePreset(out.root, true, "Neutral1", "Quietfolk", "Rock", "Oxygen", 0, tiers);
        writePreset(out.root, true, "Neutral2", "Stillfolk", "Rock", "Oxygen", 0, tiers);
        writeFile(out.root / "Dsgnname" / "TESTS.TXT", "Arrow\nBolt\n");

        ruleset::Ruleset rs = test::buildEngineRuleset();
        auto set = [&](std::string k, std::string v) { rs.settings.set(std::move(k), std::move(v)); };
        set("Maximum Number Of Systems", "60");
        set("Default Number Of Ships Per Player", "150");
        set("Number of Quick Start Styles", "1");
        set("Quick Start Style 1", "Beta");
        set("Characteristic Intelligence Pct Cost", "25");
        set("Characteristic Intelligence Threshold", "20");
        set("Characteristic Intelligence Threshhold Pct Cost Pos", "100");
        set("Characteristic Intelligence Threshhold Pct Cost Neg", "10");
        set("Characteristic Intelligence Min Pct", "50");
        set("Characteristic Intelligence Max Pct", "150");
        set("Characteristic Cunning Pct Cost", "20");
        set("Characteristic Cunning Threshold", "10");
        set("Characteristic Cunning Threshhold Pct Cost Pos", "200");
        set("Characteristic Cunning Threshhold Pct Cost Neg", "50");
        set("Minimum Computer Player Low Setting", "1");
        set("Maximum Computer Player Low Setting", "2");
        set("Minimum Computer Player Medium Setting", "2");
        set("Maximum Computer Player Medium Setting", "3");
        set("Minimum Neutral Player Low Setting", "1");
        set("Maximum Neutral Player Low Setting", "1");
        set("Random Player Personality Groups", "2");
        set("Random Player Personality Group 1 Percent", "50");
        set("Random Player Personality Group 2 Percent", "50");
        ruleset::RacialTrait keen;
        keen.name = "Keen Eyes";
        keen.cost = 300;
        keen.traitType = "Luck";
        keen.requiredTraits = {"Night Eyes"};
        keen.restrictedTraits = {"None"};
        rs.racialTraits.push_back(keen);
        ruleset::Culture bold;
        bold.name = "Bold";
        bold.spaceCombat = 10;
        rs.cultures.push_back(bold);
        rs.happinessModels.front().name = "Test Mood";
        out.rules = std::make_unique<game::Rules>(std::move(rs), out.root);
        return out;
    }();
    return *r.rules;
}

uint32_t traitIndex(const game::Rules& r, std::string_view name) {
    for (uint32_t i = 0; i < r.data().racialTraits.size(); ++i)
        if (r.data().racialTraits[i].name == name) return i;
    FAIL("no trait " << name);
    return 0;
}

} // namespace

TEST_CASE("setup model: defaults start one human empire from the first Quick Start style") {
    const game::Rules& r = setupRules();
    REQUIRE(r.racePresets().size() == 6);
    const setup::NewGameSettings s = setup::defaultSettings(r, 42);
    CHECK(s.seed == 42);
    CHECK(s.options.quadrantType == r.data().quadrantTypes.front().name);
    CHECK(s.options.maxShipsPerPlayer == 150);
    REQUIRE(s.players.size() == 1);
    CHECK(s.players[0].kind == game::PlayerKind::Human);
    CHECK(s.players[0].preset == "Beta");
    CHECK(s.players[0].name == "Betan League");
    CHECK_FALSE(s.players[0].customRace.has_value());
    CHECK(s.players[0].presetTier == 1);  // the costliest tier within the default 2000 points
    CHECK(setup::maxSystems(r) == 60);
    // The original's defaults (spec 01 §2.2, spec 02 §9).
    CHECK(s.options.systemCount == 0);  // rolled from the Quadrant Size
    CHECK(s.options.quadrantSize == 1);  // Medium
    CHECK(s.options.allWarpPointsConnected);
    CHECK(s.options.allPlanetsSameSize);
    CHECK_FALSE(s.options.sameSystemAllowed);
    CHECK(s.options.evenlyDistributed);
    CHECK(s.options.startingResources == game::Resources{20000, 20000, 20000});
    CHECK(s.options.racialPoints == 2000);
    CHECK(s.options.homePlanetValue == 1);
    CHECK(s.options.startingPlanets == 1);
    CHECK(s.options.startTechLevel == 0);
    CHECK(s.options.eventFrequency == 1);  // Low
    CHECK(setup::kStartingResources == std::array<int64_t, 3>{5000, 20000, 100000});
    CHECK(setup::kRacialPoints == std::array<int, 4>{0, 2000, 3000, 5000});
    CHECK(setup::kStartingPlanets == std::array<int, 4>{1, 3, 5, 10});
    // Maximum Number Of Systems 60: q = 12.
    CHECK(setup::quadrantSizeRange(r, 0) == std::pair{12, 23});
    CHECK(setup::quadrantSizeRange(r, 1) == std::pair{24, 47});
    CHECK(setup::quadrantSizeRange(r, 2) == std::pair{48, 59});
}

TEST_CASE("setup model: options, seed and players map into the game setup") {
    const game::Rules& r = setupRules();
    setup::NewGameSettings s = setup::defaultSettings(r, 7);
    s.options.systemCount = 20;
    s.options.quadrantSize = 2;
    s.options.allPlanetsSameSize = false;
    s.options.eventFrequency = 3;
    s.options.maxEventSeverity = 1;
    s.options.startTechLevel = 1;
    s.options.techCost = 2;
    s.options.racialPoints = 3000;
    s.options.homePlanetValue = 2;
    s.options.victory.score = true;
    s.options.victory.scoreValue = 1234;
    s.options.victory.percentOfSecond = true;
    s.options.victory.percentOfSecondValue = 50;  // below the minimum of 100
    s.options.allowIntel = false;
    s.options.aiDifficulty = 2;
    s.options.aiBonus = 1;
    s.options.scoreDisplay = 2;
    s.computers = {true, 1};
    s.neutrals = {true, 0};

    SUBCASE("every option is carried over") {
        auto g = setup::buildGameSetup(r, s);
        REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
        CHECK(g->seed == 7);
        CHECK(g->options.systemCount == 20);
        CHECK(g->options.quadrantSize == 2);
        CHECK_FALSE(g->options.allPlanetsSameSize);
        CHECK(g->options.eventFrequency == 3);
        CHECK(g->options.maxEventSeverity == 1);
        CHECK(g->options.startTechLevel == 1);
        CHECK(g->options.techCost == 2);
        CHECK(g->options.racialPoints == 3000);
        CHECK(g->options.homePlanetValue == 2);
        CHECK(g->options.victory.score);
        CHECK(g->options.victory.scoreValue == 1234);
        CHECK(g->options.victory.percentOfSecondValue == 100);
        CHECK_FALSE(g->options.allowIntel);
        CHECK(g->options.aiDifficulty == 2);
        CHECK(g->options.aiBonus == 1);
        CHECK(g->options.scoreDisplay == 2);
        CHECK(g->options.techAreasAllowed.empty());
    }

    SUBCASE("random computer and neutral players are rolled from the seed") {
        auto g = setup::buildGameSetup(r, s);
        REQUIRE(g.has_value());
        int computers = 0, neutrals = 0;
        std::vector<std::string> names;
        for (size_t i = 0; i < g->empires.size(); ++i) {
            const game::EmpireSetup& e = g->empires[i];
            const auto* p = setup::presetOf(r, e);
            REQUIRE(p != nullptr);
            if (e.kind == game::PlayerKind::Computer) {
                ++computers;
                CHECK_FALSE(p->neutral);
                // The preset's Race Opt 2 for 3000 points (spec 05 §7.1), and the chosen difficulty.
                REQUIRE(e.customRace.has_value());
                CHECK(game::racialPointCost(r, *e.customRace) <= 3000);
                REQUIRE(i < g->options.randomAiPlayers.size());
                CHECK(g->options.randomAiPlayers[i] == 1);
            }
            if (e.kind == game::PlayerKind::Neutral) {
                ++neutrals;
                CHECK(p->neutral);
            }
            CHECK(std::find(names.begin(), names.end(), e.name) == names.end());
            names.push_back(e.name);
        }
        CHECK(g->empires[0].kind == game::PlayerKind::Human);
        CHECK(computers >= 2);
        CHECK(computers <= 3);
        CHECK(neutrals == 1);
        // Same seed, same players.
        auto again = setup::buildGameSetup(r, s);
        REQUIRE(again.has_value());
        REQUIRE(again->empires.size() == g->empires.size());
        for (size_t i = 0; i < g->empires.size(); ++i) CHECK(again->empires[i].preset == g->empires[i].preset);
        // And the game starts.
        auto state = game::createGame(r, *g);
        REQUIRE_MESSAGE(state.has_value(), (state ? std::string{} : state.error()));
        CHECK(state->empires.size() == g->empires.size());
    }

    SUBCASE("technology areas allowed") {
        s.options.techAreasAllowed.assign(r.data().techAreas.size(), 1);
        auto all = setup::buildGameSetup(r, s);
        REQUIRE(all.has_value());
        CHECK(all->options.techAreasAllowed.empty());  // nothing removed: no list
        s.options.techAreasAllowed[1] = 0;
        auto some = setup::buildGameSetup(r, s);
        REQUIRE(some.has_value());
        REQUIRE(some->options.techAreasAllowed.size() == r.data().techAreas.size());
        CHECK(some->options.techAreasAllowed[1] == 0);
        CHECK(some->options.techAreasAllowed[0] == 1);
    }

    SUBCASE("several humans make a hotseat game") {
        s.computers.enabled = false;
        s.neutrals.enabled = false;
        const setup::EmpireDraft d = setup::draftFromPreset(r, *game::findPreset(r, "Gamma"), 0);
        auto second = setup::finishDraft(r, d, s.options.racialPoints);
        REQUIRE(second.has_value());
        s.players.push_back(*second);
        auto g = setup::buildGameSetup(r, s);
        REQUIRE(g.has_value());
        REQUIRE(g->empires.size() == 2);
        CHECK(g->empires[0].kind == game::PlayerKind::Human);
        CHECK(g->empires[1].kind == game::PlayerKind::Human);
    }

    SUBCASE("setup errors") {
        s.players[0].kind = game::PlayerKind::Computer;
        CHECK_FALSE(setup::buildGameSetup(r, s).has_value());
        s.players[0].kind = game::PlayerKind::Human;
        s.players.push_back(s.players[0]);
        CHECK_FALSE(setup::buildGameSetup(r, s).has_value());  // two empires with one name
        s.players.pop_back();
        s.options.racialPoints = 100;  // the chosen tier costs more
        auto g = setup::buildGameSetup(r, s);
        REQUIRE_FALSE(g.has_value());
        CHECK(g.error().find("racial points") != std::string::npos);
    }
}

TEST_CASE("setup model: the preview is the map the game starts with") {
    const game::Rules& r = setupRules();
    setup::NewGameSettings s = setup::defaultSettings(r, 99);
    s.options.quadrantSize = 0;  // Small: 12 to 23 systems
    s.computers.enabled = false;
    auto preview = setup::previewQuadrant(r, s.seed, s.options);
    REQUIRE_MESSAGE(preview.has_value(), (preview ? std::string{} : preview.error()));
    auto g = setup::buildGameSetup(r, s);
    REQUIRE(g.has_value());
    auto state = game::createGame(r, *g);
    REQUIRE(state.has_value());
    REQUIRE(state->galaxy.systems.size() == preview->galaxy.systems.size());
    CHECK(preview->galaxy.systems.size() >= 12);
    CHECK(preview->galaxy.systems.size() <= 23);
    for (size_t i = 0; i < preview->galaxy.systems.size(); ++i) {
        CHECK(state->galaxy.systems[i].name == preview->galaxy.systems[i].name);
        CHECK(state->galaxy.systems[i].position == preview->galaxy.systems[i].position);
    }
    // A different seed rerolls the map.
    auto other = setup::previewQuadrant(r, s.seed + 1, s.options);
    REQUIRE(other.has_value());
    bool differs = false;
    for (size_t i = 0; i < other->galaxy.systems.size() && i < preview->galaxy.systems.size(); ++i)
        differs = differs || other->galaxy.systems[i].position != preview->galaxy.systems[i].position;
    CHECK(differs);
}

TEST_CASE("setup model: racial point accounting") {
    const game::Rules& r = setupRules();
    using game::Characteristic;
    // Intelligence: 25 per point up to +20, then P = 100 points per point beyond;
    // refunds 25 per point down to -20, then N = 10 per point (spec 02 §8.1).
    CHECK(setup::characteristicCost(r, Characteristic::Intelligence, 100) == 0);
    CHECK(setup::characteristicCost(r, Characteristic::Intelligence, 110) == 250);
    CHECK(setup::characteristicCost(r, Characteristic::Intelligence, 120) == 500);
    CHECK(setup::characteristicCost(r, Characteristic::Intelligence, 130) == 25 * 20 + 100 * 10);
    CHECK(setup::characteristicCost(r, Characteristic::Intelligence, 70) == -(25 * 20 + 10 * 10));
    CHECK(setup::characteristicCost(r, Characteristic::Intelligence, 40) == -(25 * 20 + 10 * 30));  // clamped to Min Pct 50
    // Cunning: 20 per point, beyond +10 at 200 per point; refunds 50 per point beyond -10.
    CHECK(setup::characteristicCost(r, Characteristic::Cunning, 115) == 20 * 10 + 200 * 5);
    CHECK(setup::characteristicCost(r, Characteristic::Cunning, 80) == -(20 * 10 + 50 * 10));
    const setup::CharacteristicLimits l = setup::characteristicLimits(r, Characteristic::Intelligence);
    CHECK(l.min == 50);
    CHECK(l.max == 150);

    const ruleset::RacePreset& alpha = *game::findPreset(r, "Alpha");
    CHECK(setup::tierCosts(r, alpha) == std::vector<int>{250, 750, (25 * 20 + 100 * 30) + (20 * 10 + 200 * 5) + 500});
    CHECK(setup::bestTierWithin(r, alpha, 2000) == 1);
    CHECK(setup::bestTierWithin(r, alpha, 100) == 0);

    setup::EmpireDraft d = setup::draftFromPreset(r, alpha, 0);
    CHECK(setup::racialPointsLeft(r, d.race, 2000) == 1750);
    // The sum over characteristics and traits is what the engine charges.
    d.race.characteristics[static_cast<size_t>(Characteristic::Cunning)] = 115;
    REQUIRE(setup::addTrait(r, d.race, traitIndex(r, "Day Eyes")));
    const int sum = setup::characteristicCost(r, Characteristic::Intelligence, 110) + setup::characteristicCost(r, Characteristic::Cunning, 115) +
                    r.data().racialTraits[traitIndex(r, "Day Eyes")].cost;
    CHECK(game::racialPointCost(r, d.race) == sum);
    CHECK(setup::racialPointsLeft(r, d.race, 2000) == 2000 - sum);

    // Create Empire is refused while points are negative.
    CHECK_FALSE(setup::finishDraft(r, d, sum - 1).has_value());
    auto custom = setup::finishDraft(r, d, sum);
    REQUIRE(custom.has_value());
    REQUIRE(custom->customRace.has_value());
    CHECK(custom->preset == "Alpha");
    CHECK(setup::sameRace(*custom->customRace, d.race));

    // An unmodified preset tier stays a preset (no custom race).
    const setup::EmpireDraft plain = setup::draftFromPreset(r, alpha, 1);
    auto preset = setup::finishDraft(r, plain, 2000);
    REQUIRE(preset.has_value());
    CHECK_FALSE(preset->customRace.has_value());
    CHECK(preset->presetTier == 1);
    // Editing back to a tier's values finds that tier.
    setup::EmpireDraft back = setup::draftFromPreset(r, alpha, 0);
    REQUIRE(setup::addTrait(r, back.race, traitIndex(r, "Night Eyes")));
    auto found = setup::finishDraft(r, back, 2000);
    REQUIRE(found.has_value());
    CHECK_FALSE(found->customRace.has_value());
    CHECK(found->presetTier == 1);
}

TEST_CASE("setup model: advanced trait requirements and restrictions") {
    const game::Rules& r = setupRules();
    const uint32_t night = traitIndex(r, "Night Eyes"), day = traitIndex(r, "Day Eyes"), keen = traitIndex(r, "Keen Eyes");
    game::Race race;
    CHECK_FALSE(setup::canAddTrait(r, race, keen).ok);  // needs Night Eyes
    CHECK(setup::canAddTrait(r, race, keen).reason.find("Night Eyes") != std::string::npos);
    REQUIRE(setup::addTrait(r, race, night));
    CHECK(setup::canAddTrait(r, race, keen).ok);
    CHECK_FALSE(setup::canAddTrait(r, race, day).ok);  // restricted by Night Eyes
    REQUIRE(setup::addTrait(r, race, keen));
    CHECK_FALSE(setup::addTrait(r, race, day));
    // Dropping the requirement drops what depends on it.
    setup::removeTrait(r, race, night);
    CHECK(race.traits.empty());
    CHECK(setup::addTrait(r, race, day));
    CHECK_FALSE(setup::canAddTrait(r, race, night).ok);  // restricted the other way round
}

TEST_CASE("setup model: empire files round-trip through TOML") {
    const game::Rules& r = setupRules();
    setup::EmpireDraft d = setup::draftFromPreset(r, *game::findPreset(r, "Gamma"), 0);
    d.setup.name = "Glass \"Choir\"";
    d.setup.leaderName = "Ada";
    d.setup.kind = game::PlayerKind::Computer;
    d.setup.passwordHash = setup::hashPassword("secret");
    d.race.characteristics[static_cast<size_t>(game::Characteristic::Cunning)] = 90;
    d.race.culture = 1;
    d.race.nativeSurface = "Ice";
    d.race.atmosphere = "Methane";
    d.race.biology = "Lives in cold halls.\nSings at night.";
    REQUIRE(setup::addTrait(r, d.race, traitIndex(r, "Night Eyes")));
    REQUIRE(setup::addTrait(r, d.race, traitIndex(r, "Keen Eyes")));
    auto e = setup::finishDraft(r, d, 5000);
    REQUIRE(e.has_value());

    const std::string text = setup::empireToToml(r, *e);
    auto loaded = setup::empireFromToml(r, text);
    REQUIRE_MESSAGE(loaded.has_value(), (loaded ? std::string{} : loaded.error()));
    CHECK(loaded->warnings.empty());
    const game::EmpireSetup& back = loaded->empire;
    CHECK(back.name == e->name);
    CHECK(back.leaderName == "Ada");
    CHECK(back.kind == game::PlayerKind::Computer);
    CHECK(back.preset == "Gamma");
    CHECK(back.passwordHash == setup::hashPassword("secret"));
    CHECK(back.passwordHash != setup::hashPassword("Secret"));
    REQUIRE(back.customRace.has_value());
    CHECK(setup::sameRace(*back.customRace, d.race));

    // Files: save, list, load.
    const fs::path dir = fs::temp_directory_path() / "opense4_setup_model_empires";
    fs::remove_all(dir);
    auto file = setup::saveEmpireFile(r, dir, *e);
    REQUIRE(file.has_value());
    const auto list = setup::listEmpireFiles(r, dir);
    REQUIRE(list.size() == 1);
    CHECK(list[0].name == e->name);
    CHECK(list[0].style == "Gamma");
    auto again = setup::loadEmpireFile(r, *file);
    REQUIRE(again.has_value());
    CHECK(setup::sameRace(*again->empire.customRace, d.race));
    fs::remove_all(dir);

    // Things this data set lacks come back as warnings, and limits are enforced.
    std::string edited = text;
    const auto pos = edited.find("Keen Eyes");
    REQUIRE(pos != std::string::npos);
    edited.replace(pos, 9, "Fake Eyes");
    const auto cpos = edited.find("Cunning = 90");
    REQUIRE(cpos != std::string::npos);
    edited.replace(cpos, 12, "Cunning = 10");
    auto warned = setup::empireFromToml(r, edited);
    REQUIRE(warned.has_value());
    CHECK(warned->warnings.size() == 2);
    REQUIRE(warned->empire.customRace.has_value());
    CHECK(warned->empire.customRace->characteristic(game::Characteristic::Cunning) == 50);

    CHECK_FALSE(setup::empireFromToml(r, "name = [").has_value());
    CHECK_FALSE(setup::empireFromToml(r, "format = 99\nname = \"x\"").has_value());
}

TEST_CASE("setup model: choice lists come from the data set") {
    const game::Rules& r = setupRules();
    const auto surfaces = setup::planetSurfaces(r);
    CHECK(std::find(surfaces.begin(), surfaces.end(), "Rock") != surfaces.end());
    CHECK(std::find(surfaces.begin(), surfaces.end(), "Gas Giant") != surfaces.end());
    const auto atmospheres = setup::atmospheres(r);
    CHECK(std::find(atmospheres.begin(), atmospheres.end(), "Oxygen") != atmospheres.end());
    CHECK(setup::planetPicture(r, "Ice", "Methane").has_value());
    CHECK(setup::designNameFiles(r) == std::vector<std::string>{"TESTS.TXT"});
    const auto [lo, hi] = setup::randomPlayerRange(r, false, 1);
    CHECK(lo == 2);
    CHECK(hi == 3);
}

TEST_CASE("setup model: installed data set: defaults, presets and empire files (opt-in)") {
    const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
    if (!env) return;
    const auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? fs::path{} : fs::path(env));
    if (!dir) return;
    auto loaded = ruleset::loadRuleset(*dir);
    REQUIRE(loaded.ruleset.has_value());
    const game::Rules r(std::move(*loaded.ruleset), dir->parent_path());

    setup::NewGameSettings s = setup::defaultSettings(r, 7);
    REQUIRE(s.players.size() == 1);
    s.computers = {true, 2};
    s.neutrals = {true, 0};
    auto g = setup::buildGameSetup(r, s);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    CHECK(g->empires.size() >= 2);
    auto state = game::createGame(r, *g);
    REQUIRE_MESSAGE(state.has_value(), (state ? std::string{} : state.error()));
    auto preview = setup::previewQuadrant(r, s.seed, s.options);
    REQUIRE(preview.has_value());
    REQUIRE(preview->galaxy.systems.size() == state->galaxy.systems.size());
    for (size_t i = 0; i < preview->galaxy.systems.size(); ++i) CHECK(preview->galaxy.systems[i].name == state->galaxy.systems[i].name);

    // Every preset tier stays a preset and survives an empire file unchanged.
    for (const auto& p : r.racePresets()) {
        for (int tier = 0; tier < static_cast<int>(p.tiers.size()); ++tier) {
            INFO(p.folder << " tier " << tier);
            const setup::EmpireDraft d = setup::draftFromPreset(r, p, tier);
            auto e = setup::finishDraft(r, d, 1'000'000);
            REQUIRE(e.has_value());
            CHECK_FALSE(e->customRace.has_value());
            auto back = setup::empireFromToml(r, setup::empireToToml(r, *e));
            REQUIRE(back.has_value());
            CHECK(back->warnings.empty());
            CHECK(setup::sameRace(setup::raceOf(r, back->empire), d.race));
        }
    }
}

TEST_CASE("setup model: autosave every N turns rotates through ten slots") {
    // None (the default) never saves (spec 01 §2.2).
    CHECK(game::GameOptions{}.autosaveTurns == 0);
    CHECK_FALSE(setup::autosaveName(0, 1).has_value());
    CHECK_FALSE(setup::autosaveName(0, 30).has_value());
    CHECK(setup::kAutosaveTurns == std::array<int, 6>{0, 1, 2, 3, 5, 10});
    // Every turn: slots 1..10, then round again.
    CHECK(setup::autosaveName(1, 1) == "Autosave 1");
    CHECK(setup::autosaveName(1, 10) == "Autosave 10");
    CHECK(setup::autosaveName(1, 11) == "Autosave 1");
    // Every 5 turns: only turns 5, 10, ... and the eleventh save reuses slot 1.
    CHECK_FALSE(setup::autosaveName(5, 4).has_value());
    CHECK(setup::autosaveName(5, 5) == "Autosave 1");
    CHECK(setup::autosaveName(5, 15) == "Autosave 3");
    CHECK(setup::autosaveName(5, 55) == "Autosave 1");
    CHECK_FALSE(setup::autosaveName(3, 0).has_value());
}
