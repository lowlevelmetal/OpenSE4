#pragma once

// The original's scenarios, its tutorial among them (docs/spec/06 §5.6,
// spec 01 §12): the scenario folder, the two text files of a scenario and
// the rules its pages follow, apart from drawing, for the tests. A game
// started from one keeps GameState::classicScenario (save format 10), apart
// from the mods' scenarios (GameState::scenario); its pages are not part of
// the game and are read again from the scenario's stem on every load. The
// window that shows them is screens/scenario_text.cpp.

#include "game/state.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {
class Rules;
}

namespace opense4::client::classic {

// One record of <stem>_Text.txt. The pages keep the file's order; their ID
// is read but nothing uses it (confirmed: binary).
struct ScenarioPage {
    int series = 0;
    int segment = 0;            // its place in the series: Previous is dim from 1 down
    uint32_t turn = 0;          // absolute: (date - 2400.0) x 10, as GameState::turn counts
    std::vector<int> players;   // For Players: the player numbers (1-based) who get it
    std::string title;
    std::vector<std::string> paragraphs;
    std::string image;          // a picture in the scenario folder; empty for none ("None")
    bool operator==(const ScenarioPage&) const = default;
};

// A scenario's two files as read: <stem>_Settings.txt and <stem>_Text.txt.
struct ScenarioFiles {
    std::string stem;                         // the full path without "_Settings.txt"
    std::filesystem::path folder;             // the scenario folder its Starting Game and pictures are in
    std::filesystem::path settingsFile;       // the files read (or tried)
    std::filesystem::path textFile;
    std::string name, description;            // read, never shown (spec 01 §12)
    std::string startingGame;                 // Starting Game: a file in the scenario folder
    std::vector<ScenarioPage> pages;          // in file order
    std::vector<std::string> settingsErrors;  // what was wrong with each file
    std::vector<std::string> textErrors;

    bool ok() const { return settingsErrors.empty() && textErrors.empty(); }
    // The errors as the original lists them, a list per file under its title:
    // "Scenario Settings Errors: <file>", "Scenario Text Errors: <file>".
    std::string problem() const;
    // The Starting Game, always looked for in the scenario folder.
    std::filesystem::path startingGameFile() const;
};

// A folder of the original's files ("Scenarios", "Empires", "Maps"): the
// folder of that name of the last classic mod in use that has one (OpenSE4's
// classic mods stand for the original's Path.txt mod folder), else the
// install's. Empty for a data set without a game folder.
std::filesystem::path classicGameFolder(const game::Rules& r, std::string_view name);
// The scenario folder: the Scenarios folder of the last classic mod in use
// that has one (OpenSE4's classic mods stand for the original's Path.txt mod
// folder), else the install's. Empty for a data set without a game folder.
std::filesystem::path scenarioFolder(const game::Rules& r);
// The scenarios of a folder: every file whose name ends in "_settings.txt"
// in any case, by the rest of its name, sorted ignoring case.
std::vector<std::string> scenarioNames(const std::filesystem::path& folder);
// The last part of a stem, written with either separator ("C:\Se4\Scenarios\Tutorial": "Tutorial").
std::string stemName(std::string_view stem);
// The tutorial is the scenario whose stem's name is Tutorial (any case).
bool isTutorial(std::string_view stem);

// Reads both files of the scenario at `stem` (both are always read). A file
// that is not at the stem's path, a stem written on another computer, is
// looked for by its name in `folder` (confirmed: binary).
ScenarioFiles readScenarioFiles(std::string_view stem, const std::filesystem::path& folder);
// The same from the files' text (tests); `fileName` names the file in messages.
void parseScenarioSettings(ScenarioFiles& f, std::string_view text, const std::string& fileName);
void parseScenarioText(ScenarioFiles& f, std::string_view text, const std::string& fileName);

// The state a game started from the scenario at `stem` gets: a scenario game,
// the tutorial when its name is Tutorial, the stem as a full path.
game::ClassicScenarioState scenarioState(std::string_view stem);

// The page that comes up at the start of a turn (0-based index in the file):
// the first one in file order whose Turn is `turn` and whose For Players lists
// `player` (1-based). Our client differs: the original searches the list's
// text for the number, so player 1 also gets the pages of player 11.
std::optional<size_t> pageForTurn(std::span<const ScenarioPage> pages, uint32_t turn, int player);
// Previous and Next: the page before or after it in the file; none before a
// page whose Segment is 1 or less, after the file's last page, or when the
// next page belongs to another Series (confirmed: binary).
std::optional<size_t> previousPage(std::span<const ScenarioPage> pages, size_t at);
std::optional<size_t> nextPage(std::span<const ScenarioPage> pages, size_t at);
// "Page i of n": n the pages of its Series. Our client differs: i is its
// place among them in the file, where the original shows its place in the
// whole file.
struct SeriesPlace {
    size_t at = 0;   // 1-based
    size_t of = 0;
};
SeriesPlace placeInSeries(std::span<const ScenarioPage> pages, size_t at);
// The page Ctrl+H and the T buttons show again: InterfaceOptions::scenarioPage
// (1-based, a place in the file), when the file has it.
std::optional<size_t> lastShownPage(std::span<const ScenarioPage> pages, uint16_t scenarioPage);

} // namespace opense4::client::classic
