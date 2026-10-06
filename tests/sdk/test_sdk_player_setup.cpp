// Choosing script computer players (docs/sdk/ai-protocol.md §1): the players
// a game's mods offer, the headless setup model, the client's --ai, and the
// server's setup files. A script player must be one a mod of the game declares.

#include "mod_fixture.hpp"
#include "players_fixture.hpp"

#include "client/classic/screens/setup_model.hpp"
#include "game/players.hpp"
#include "server/setup_file.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;
namespace setupm = opense4::client::classic::setup;

namespace {

// The fixture data set with the fixture computer players' mod.
struct ModdedRules {
    test::GameFolder folder{"players"};
    mods::LoadedDataSet data;
    std::optional<Rules> rules;
    ModdedRules() {
        data = mods::loadDataSet(folder.root, folder.data(), test::modSet({aiFixtureMod()}));
        REQUIRE_MESSAGE(data.ruleset.has_value(), test::allErrors(data.diagnostics));
        rules.emplace(*data.ruleset);
    }
};

EmpireSetup empire(std::string name, PlayerKind kind) {
    EmpireSetup e;
    e.name = std::move(name);
    e.kind = kind;
    return e;
}

} // namespace

TEST_CASE("sdk player setup: the players a game's mods offer") {
    ModdedRules m;
    const std::vector<sdk::PlayerChoice> offered = sdk::availablePlayers(*m.rules);
    REQUIRE(offered.size() == 3);
    CHECK(offered[0].mod == kFixtureMod);
    CHECK(offered[0].name == "Steady");
    CHECK(offered[0].controller() == scriptPlayer("Steady"));
    CHECK(sdk::availablePlayers(test::engineRules()).empty());
    const auto choices = setupm::computerPlayerChoices(*m.rules);
    REQUIRE(choices.size() == 3);
    CHECK(choices[1].label == "test.ai-fixture:Probe");
    CHECK_FALSE(choices[1].description.empty());
    // A setup naming a player is checked against the game's mods.
    std::vector<EmpireSetup> empires{empire("A", PlayerKind::Computer)};
    empires[0].controller = scriptPlayer("Steady");
    CHECK(sdk::checkControllers(empires, sdk::gamePackages(*m.rules)).empty());
    empires[0].controller = scriptPlayer("Nobody");
    CHECK(sdk::checkControllers(empires, sdk::gamePackages(*m.rules)).front() == "A: the mod test.ai-fixture has no computer player named 'Nobody'");
    empires[0].controller = scriptPlayer("Steady");
    CHECK(sdk::checkControllers(empires, {}).front().find("needs the mod test.ai-fixture, which the game does not use") != std::string::npos);
}

TEST_CASE("sdk player setup: the setup model gives the computer empires the game's player, and --ai gives it to all") {
    ModdedRules m;
    const Rules& r = *m.rules;
    setupm::NewGameSettings s = setupm::defaultSettings(r, 3);
    s.computers.enabled = false;
    s.neutrals.enabled = false;
    s.players = {empire("Human", PlayerKind::Human), empire("First", PlayerKind::Computer), empire("Second", PlayerKind::Computer),
                 empire("Neutral", PlayerKind::Neutral)};
    s.players[2].controller = scriptPlayer("Probe");   // its own
    s.computerPlayer = scriptPlayer("Steady");
    auto g = setupm::buildGameSetup(r, s);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    CHECK(g->empires[0].controller == Controller{});
    CHECK(g->empires[1].controller == scriptPlayer("Steady"));
    CHECK(g->empires[2].controller == scriptPlayer("Probe"));
    CHECK(g->empires[3].controller == Controller{});   // neutral empires keep the built-in AI unless given one
    s.computerPlayer = scriptPlayer("Nobody");
    CHECK(setupm::buildGameSetup(r, s).error() == "First: the mod test.ai-fixture has no computer player named 'Nobody'");

    // --ai: every computer empire.
    GameSetup quick = *g;
    CHECK_FALSE(setupm::useComputerPlayer(r, quick, "test.ai-fixture:Idle"));
    CHECK(quick.empires[1].controller == scriptPlayer("Idle"));
    CHECK(quick.empires[2].controller == scriptPlayer("Idle"));
    CHECK(quick.empires[0].controller == Controller{});
    CHECK_FALSE(setupm::useComputerPlayer(r, quick, "builtin"));
    CHECK(quick.empires[1].controller == Controller{});
    CHECK(*setupm::useComputerPlayer(r, quick, "nonsense") == "'nonsense' is not a computer player: give \"builtin\" or \"<mod id>:<player>\"");
    CHECK(setupm::useComputerPlayer(test::engineRules(), quick, "test.ai-fixture:Idle")->find("which the game does not use") != std::string::npos);
    // Saved with the game (format 9).
    auto created = createGame(test::engineRules(), *g);
    REQUIRE(created.has_value());
    CHECK(created->empire(EmpireId{1u}).controller == scriptPlayer("Steady"));
}

TEST_CASE("sdk player setup: server setup files name the computer empires' players and the script options") {
    ModdedRules m;
    const std::string text = R"(
ai = "test.ai-fixture:Steady"

[options]
ai_sees_everything = true
ai_planning_budget = 1000000
ai_call_budget = 20000
ai_memory_limit = 4096

[[empire]]
kind = "human"
player = "alice"
password = "pw"

[[empire]]
kind = "computer"

[[empire]]
kind = "computer"
ai = "builtin"

[[empire]]
kind = "computer"
ai = "test.ai-fixture:Probe"
)";
    auto s = server::parseSetup(text, "game.toml", *m.rules);
    REQUIRE_MESSAGE(s.has_value(), (s ? std::string{} : s.error()));
    CHECK(s->ai == scriptPlayer("Steady"));
    CHECK(s->options.aiSeesEverything);
    CHECK(s->options.aiPlanningBudget == 1'000'000);
    CHECK(s->options.aiCallBudget == 20'000);
    CHECK(s->options.aiMemoryLimit == 4096);
    REQUIRE(s->empires.size() == 4);
    CHECK(s->empires[0].setup.controller == Controller{});
    CHECK(s->empires[1].setup.controller == scriptPlayer("Steady"));
    CHECK(s->empires[2].setup.controller == Controller{});
    CHECK(s->empires[3].setup.controller == scriptPlayer("Probe"));

    // Mistakes are named with their line.
    auto bad = server::parseSetup("ai = \"test.ai-fixture:Nobody\"\n[[empire]]\nkind = \"human\"\nai = \"test.ai-fixture:Steady\"\n[options]\nai_call_budget = 0\n",
                                  "bad.toml", *m.rules);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().find("bad.toml:1: the mod test.ai-fixture has no computer player named 'Nobody'") != std::string::npos);
    CHECK(bad.error().find("bad.toml:4: 'ai' is for computer and neutral empires") != std::string::npos);
    CHECK(bad.error().find("'ai_call_budget' must be between 1") != std::string::npos);
    auto unknown = server::parseSetup("ai = \"test.ai-fixture:Steady\"\n", "plain.toml", test::engineRules());
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().find("needs the mod test.ai-fixture, which the game does not use (add it to 'mods')") != std::string::npos);
    CHECK(server::parseSetup("ai = \"just words\"\n", "words.toml", *m.rules).error().find("must be \"builtin\" or") != std::string::npos);
}
