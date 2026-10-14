// The original's scenarios and tutorial (docs/spec/06 §5.6,
// client/classic/classic_scenario.hpp): the two text files of a scenario
// and their errors, the scenario folder and its stems, the rules of the
// pages, and a game started from a scenario's Starting Game. Our own
// invented scenarios; the installed tutorial is opt-in (OPENSE4_CLASSIC_DATA).

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "client/classic/classic_scenario.hpp"
#include "client/classic/session.hpp"
#include "game/classic_save.hpp"
#include "game/serialize.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

using namespace opense4;
using namespace opense4::client::classic;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSettings = R"(Notes for whoever edits the file.
*BEGIN*
Name                := Harbour Watch
Description         := A test scenario of our own.
Starting Game       := Harbour.gam
*END*
)";

// Five pages: series 1 (three pages), series 2 (one page), series 1 again.
constexpr std::string_view kText = R"(Fields of the file, as its header describes them.
*BEGIN*
ID                          := 10
Series                      := 1
Segment                     := 1
Turn                        := 0
For Players                 := 1
Text Title                  := Arrival
Text Number of Paragraphs   := 2
Text Paragraph 1            := First paragraph.
Text Paragraph 2            := Second paragraph.
Image                       := Harbour_Start.bmp

ID                          := 11
Series                      := 1
Segment                     := 2
Turn                        := 0
For Players                 := 1, 2
Text Title                  := Second page
Text Number of Paragraphs   := 1
Text Paragraph 1            := More.
Image                       := None

ID                          := 12
Series                      := 1
Segment                     := 3
Turn                        := 3
For Players                 := 11
Text Title                  := For player eleven
Text Number of Paragraphs   := 0
Image                       :=

ID                          := 20
Series                      := 2
Segment                     := 1
Turn                        := 3
For Players                 := 2,1
Text Title                  := Another series
Text Number of Paragraphs   := 1
Text Paragraph 1            := Elsewhere.

ID                          := 13
Series                      := 1
Segment                     := 4
Turn                        := 5
For Players                 := 1
Text Title                  := Back in the first
Text Number of Paragraphs   := 1
Text Paragraph 1            := Late.
*END*
)";

void write(const fs::path& file, std::string_view text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

std::shared_ptr<const game::Rules> sharedRules() {
    return std::shared_ptr<const game::Rules>(&test::engineRules(), [](const game::Rules*) {});
}

} // namespace

TEST_CASE("classic scenario: the settings and text files are read in file order") {
    ScenarioFiles f;
    parseScenarioSettings(f, kSettings, "Harbour_Settings.txt");
    parseScenarioText(f, kText, "Harbour_Text.txt");
    REQUIRE(f.ok());
    CHECK(f.startingGame == "Harbour.gam");
    CHECK(f.name == "Harbour Watch");   // read, never shown
    REQUIRE(f.pages.size() == 5);
    const ScenarioPage& first = f.pages[0];
    CHECK(first.series == 1);
    CHECK(first.segment == 1);
    CHECK(first.turn == 0);
    CHECK(first.players == std::vector<int>{1});
    CHECK(first.title == "Arrival");
    CHECK(first.paragraphs == std::vector<std::string>{"First paragraph.", "Second paragraph."});
    CHECK(first.image == "Harbour_Start.bmp");
    CHECK(f.pages[1].players == std::vector<int>{1, 2});
    CHECK(f.pages[1].image.empty());   // None: no picture
    CHECK(f.pages[2].image.empty());   // empty: no picture
    CHECK(f.pages[2].paragraphs.empty());
    CHECK(f.pages[3].players == std::vector<int>{2, 1});
    CHECK(f.pages[3].image.empty());   // no Image field
    CHECK(f.problem().empty());
}

TEST_CASE("classic scenario: errors in either file are listed per file, and both are read") {
    ScenarioFiles f;
    parseScenarioSettings(f, "*BEGIN*\nName := No game\n*END*\n", "Broken_Settings.txt");
    parseScenarioText(f, "*BEGIN*\nID := 1\nSeries := one\nSegment := 1\nTurn := 0\nFor Players := 1, x\nText Title := T\n"
                         "Text Number of Paragraphs := 2\nText Paragraph 1 := Only one.\n*END*\n",
                      "Broken_Text.txt");
    CHECK_FALSE(f.ok());
    REQUIRE(f.settingsErrors.size() == 1);
    CHECK(f.settingsErrors[0].find("Starting Game") != std::string::npos);
    // A Series that is no number, a player that is none, a paragraph missing.
    REQUIRE(f.textErrors.size() == 3);
    CHECK(f.textErrors[0].find("Series") != std::string::npos);
    CHECK(f.textErrors[1].find("'x'") != std::string::npos);
    CHECK(f.textErrors[2].find("Text Paragraph 2") != std::string::npos);
    f.settingsFile = "/somewhere/Broken_Settings.txt";
    f.textFile = "/somewhere/Broken_Text.txt";
    const std::string problem = f.problem();
    CHECK(problem.starts_with("Scenario Settings Errors: Broken_Settings.txt\n"));
    CHECK(problem.find("\nScenario Text Errors: Broken_Text.txt\n") != std::string::npos);

    ScenarioFiles empty;
    parseScenarioSettings(empty, "Only notes, no record.\n", "Empty_Settings.txt");
    parseScenarioText(empty, "Only notes.\n", "Empty_Text.txt");
    CHECK(empty.settingsErrors.size() == 1);
    CHECK(empty.textErrors.size() == 1);
}

TEST_CASE("classic scenario: the folder's scenarios, stems from any computer and the tutorial") {
    test::TempDir dir("classic_scenario_folder");
    write(dir / "Harbour_Settings.txt", kSettings);
    write(dir / "Harbour_Text.txt", kText);
    write(dir / "TUTORIAL_SETTINGS.TXT", kSettings);
    write(dir / "notes.txt", "not a scenario");
    write(dir / "_Settings.txt", "a name with nothing before the suffix");
    CHECK(scenarioNames(dir.path()) == std::vector<std::string>{"Harbour", "TUTORIAL"});
    CHECK(scenarioNames(dir / "missing").empty());

    CHECK(stemName("C:\\Games\\se4\\Scenarios\\Tutorial") == "Tutorial");
    CHECK(stemName("/home/player/se4/Scenarios/Harbour") == "Harbour");
    CHECK(stemName("Harbour") == "Harbour");
    CHECK(isTutorial("C:\\se4\\Scenarios\\TUTORIAL"));
    CHECK(isTutorial("Tutorial"));
    CHECK_FALSE(isTutorial("Tutorials"));
    CHECK_FALSE(isTutorial("C:\\Tutorial\\Harbour"));

    // At the stem's own path.
    const ScenarioFiles here = readScenarioFiles((dir / "Harbour").string(), dir / "missing");
    CHECK(here.ok());
    CHECK(here.pages.size() == 5);
    CHECK(here.settingsFile == dir / "Harbour_Settings.txt");
    // A stem written on another computer: found by its name in the scenario folder.
    const ScenarioFiles moved = readScenarioFiles("C:\\Games\\se4\\Scenarios\\Harbour", dir.path());
    CHECK(moved.ok());
    CHECK(moved.textFile == dir / "Harbour_Text.txt");
    CHECK(moved.startingGameFile() == dir / "Harbour.gam");
    // Neither there nor in the folder: an error for each file, and nothing read.
    const ScenarioFiles lost = readScenarioFiles("C:\\Games\\se4\\Scenarios\\Lost", dir.path());
    CHECK(lost.settingsErrors.size() == 1);
    CHECK(lost.textErrors.size() == 1);
    CHECK(lost.pages.empty());
    // The scenario of a game: the tutorial's flag goes with the name Tutorial alone.
    const game::ClassicScenarioState tutorial = scenarioState((dir / "Tutorial").string());
    CHECK(tutorial.active);
    CHECK(tutorial.tutorial);
    CHECK(tutorial.stem == (dir / "Tutorial").string());
    const game::ClassicScenarioState other = scenarioState((dir / "Harbour").string());
    CHECK(other.active);
    CHECK_FALSE(other.tutorial);
}

TEST_CASE("classic scenario: the page of a turn, Previous and Next, and the page's place in its series") {
    ScenarioFiles f;
    parseScenarioText(f, kText, "Harbour_Text.txt");
    REQUIRE(f.ok());
    const auto& pages = f.pages;
    // The first page of the turn that lists the player.
    CHECK(pageForTurn(pages, 0, 1) == size_t{0});
    CHECK(pageForTurn(pages, 0, 2) == size_t{1});
    CHECK(pageForTurn(pages, 5, 1) == size_t{4});
    CHECK_FALSE(pageForTurn(pages, 1, 1).has_value());
    // Players are matched in the list, not in its text: player 1 does not
    // get player 11's page (our client differs from the original here).
    CHECK(pageForTurn(pages, 3, 1) == size_t{3});
    CHECK(pageForTurn(pages, 3, 11) == size_t{2});
    CHECK_FALSE(pageForTurn(pages, 3, 3).has_value());

    // Previous: none from a page whose Segment is 1, nor from the first page.
    CHECK_FALSE(previousPage(pages, 0).has_value());
    CHECK(previousPage(pages, 1) == size_t{0});
    CHECK(previousPage(pages, 2) == size_t{1});
    CHECK_FALSE(previousPage(pages, 3).has_value());
    // From segment 4, the file's previous page, of another series (as the original).
    CHECK(previousPage(pages, 4) == size_t{3});
    // Next: none when the next page is in another series, nor after the last.
    CHECK(nextPage(pages, 0) == size_t{1});
    CHECK(nextPage(pages, 1) == size_t{2});
    CHECK_FALSE(nextPage(pages, 2).has_value());
    CHECK_FALSE(nextPage(pages, 3).has_value());
    CHECK_FALSE(nextPage(pages, 4).has_value());

    // "Page i of n": i its place among its series' pages, n their number.
    CHECK(placeInSeries(pages, 0).at == 1);
    CHECK(placeInSeries(pages, 0).of == 4);
    CHECK(placeInSeries(pages, 2).at == 3);
    CHECK(placeInSeries(pages, 4).at == 4);
    CHECK(placeInSeries(pages, 3).at == 1);
    CHECK(placeInSeries(pages, 3).of == 1);

    // The page shown last: a place in the file from 1 (0 counts as 1).
    CHECK(lastShownPage(pages, 1) == size_t{0});
    CHECK(lastShownPage(pages, 0) == size_t{0});
    CHECK(lastShownPage(pages, 5) == size_t{4});
    CHECK_FALSE(lastShownPage(pages, 6).has_value());
    CHECK_FALSE(lastShownPage({}, 1).has_value());
}

TEST_CASE("classic scenario: a game started from a Starting Game becomes the scenario's, its pages from the first") {
    // Our own scenario: its Starting Game an OpenSE4 save of a test game.
    test::TempDir dir("classic_scenario_start");
    write(dir / "Harbour_Settings.txt", kSettings);
    write(dir / "Harbour_Text.txt", kText);
    game::GameState start = test::newEngineGame(7, 2, 12);
    start.empires[0].interfaceOptions.scenarioPage = 4;   // starts again at 1
    REQUIRE(game::saveGame(dir / "Harbour.gam", start, game::SaveInfo{}).has_value());

    const std::string stem = (dir / "Harbour").string();
    const ScenarioFiles files = readScenarioFiles(stem, dir.path());
    REQUIRE(files.ok());
    const game::ClassicScenarioState state = scenarioState(stem);
    auto session = ClassicSession::load(sharedRules(), files.startingGameFile(), &state);
    REQUIRE_MESSAGE(session.has_value(), (session ? std::string{} : session.error()));
    ClassicSession& s = **session;
    CHECK(s.state().classicScenario == state);
    for (const game::Empire& e : s.state().empires) CHECK(e.interfaceOptions.scenarioPage == 1);
    CHECK(s.takeScenarioStart());
    CHECK_FALSE(s.takeScenarioStart());   // once
    CHECK(s.takeImportNotes().empty());
    // Its first page: turn 0, the first human (player 1).
    CHECK(pageForTurn(files.pages, s.state().turn, int(s.player().value) + 1) == size_t{0});

    // Saved and loaded as any game: still the scenario's, without a start.
    REQUIRE(s.save(dir / "Saved.gam", "Harbour").has_value());
    auto loaded = ClassicSession::load(sharedRules(), dir / "Saved.gam");
    REQUIRE(loaded.has_value());
    CHECK((*loaded)->state().classicScenario == state);
    CHECK_FALSE((*loaded)->takeScenarioStart());
    // A plain game is no scenario's.
    auto plain = ClassicSession::load(sharedRules(), dir / "Harbour.gam");
    REQUIRE(plain.has_value());
    CHECK_FALSE((*plain)->state().classicScenario.active);
    CHECK((*plain)->state().empires[0].interfaceOptions.scenarioPage == 4);
}

// The installed tutorial (opt-in: OPENSE4_CLASSIC_DATA): both its files read
// without errors, and its Starting Game, a file of version 1.58, starts as
// the tutorial with a page for its first player at once.
TEST_CASE("classic scenario: the installed tutorial reads and starts (opt-in)") {
    static const std::shared_ptr<const game::Rules> rules = []() -> std::shared_ptr<const game::Rules> {
        const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
        if (!env) return nullptr;
        auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? fs::path{} : fs::path(env));
        if (!dir) return nullptr;
        auto loaded = ruleset::loadRuleset(*dir);
        if (!loaded.ruleset) return nullptr;
        return std::make_shared<const game::Rules>(std::move(*loaded.ruleset), dir->parent_path());
    }();
    if (!rules) return;
    const fs::path folder = scenarioFolder(*rules);
    const std::vector<std::string> names = scenarioNames(folder);
    const auto tutorial = std::find_if(names.begin(), names.end(), [](const std::string& n) { return isTutorial(n); });
    if (tutorial == names.end()) {
        MESSAGE("skipped: no tutorial in " << folder.string());
        return;
    }
    const std::string stem = (folder / *tutorial).string();
    const ScenarioFiles files = readScenarioFiles(stem, folder);
    REQUIRE_MESSAGE(files.ok(), files.problem());
    REQUIRE_FALSE(files.pages.empty());
    MESSAGE(files.pages.size() << " pages, starting game " << files.startingGame);
    const game::ClassicScenarioState state = scenarioState(stem);
    CHECK(state.tutorial);
    auto session = ClassicSession::load(rules, files.startingGameFile(), &state);
    REQUIRE_MESSAGE(session.has_value(), (session ? std::string{} : session.error()));
    const game::GameState& s = (*session)->state();
    CHECK(s.classicScenario == state);
    CHECK(pageForTurn(files.pages, s.turn, int((*session)->player().value) + 1).has_value());
    // Every page's picture is in the scenario folder.
    std::error_code ec;
    for (const ScenarioPage& p : files.pages)
        if (!p.image.empty()) CHECK_MESSAGE(fs::is_regular_file(ruleset::childIgnoringCase(folder, p.image), ec), p.image);
}
