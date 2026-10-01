#pragma once

// Lessons (tutorials) and training games: TOML files read into this model
// (docs/LEARNING.md "Lessons and training games"). Every problem names the
// file and line; an unknown key is an error, so a typo never silently turns
// into a step that cannot be finished.

#include "learn/condition.hpp"
#include "learn/markdown.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::learn {

enum class LessonKind : uint8_t { Tutorial, Training };
std::string_view kindName(LessonKind k);   // "tutorial", "training"

// How the lesson's game is created: a quick start for `race` with
// `computerPlayers` opponents, then these game options. Unset values keep
// the quick start's.
struct Setup {
    std::optional<uint64_t> seed;
    std::string race;                       // a race preset (folder or name); empty: the first one
    int computerPlayers = 0;
    std::optional<int> systems;             // exactly this many systems
    std::string quadrant;                   // a quadrant type of the data set
    std::optional<int> quadrantSize;        // 0 small, 1 medium, 2 large
    bool turnBased = true;
    std::optional<int> techLevel;           // starting technology: 0 low, 1 medium, 2 high
    std::optional<int> techCost;            // 0 low, 1 medium, 2 high
    std::optional<int64_t> startingResources;   // of each resource
    std::optional<int> startingPlanets;     // 1, 3, 5 or 10
    std::optional<int> events;              // event frequency: 0 none .. 3 high
    std::optional<int> aiDifficulty;        // 0 low, 1 medium, 2 high
    std::optional<bool> noTacticalCombat;
    std::optional<bool> allSystemsSeen;
    std::optional<bool> omnipresent;
    std::optional<bool> noRuins;
};
// Applies the options the setup sets (the quick start's race, seed and
// opponents are the client's to apply).
void applySetup(const Setup& setup, game::GameOptions& options);

// A tutorial step.
struct Step {
    int line = 0;
    std::string title;
    std::vector<Block> text;
    std::vector<std::string> highlight;     // UI tags to outline
    std::optional<Condition> done;          // none: the player presses Next
    std::string manual;                     // "slug#anchor" for Read more
};

// A training game's objective; it counts once its condition held (before the deadline).
struct Objective {
    int line = 0;
    std::string text;
    Condition when;
    std::optional<uint32_t> byTurn;         // must hold by this turn
};

// A briefing page shown at the start of a turn; previous and next browse a series.
struct BriefingPage {
    int line = 0;
    uint32_t turn = 0;
    std::string series;
    std::string title;
    std::vector<Block> text;
};

// Shown once, when its condition first holds.
struct Hint {
    int line = 0;
    std::string title;
    std::vector<Block> text;
    Condition when;
};

// The training game is lost when this holds.
struct FailRule {
    int line = 0;
    Condition when;
    std::vector<Block> text;
};

struct Lesson {
    LessonKind kind = LessonKind::Tutorial;
    std::string slug;
    std::string file;       // where it was read from (diagnostics)
    std::string origin;     // "built in", or the folder on disk
    std::string title;
    std::string summary;
    int minutes = 0;
    Setup setup;
    std::vector<Step> steps;               // tutorials
    std::vector<Objective> objectives;     // training games
    std::vector<BriefingPage> pages;
    std::vector<Hint> hints;
    std::optional<FailRule> fail;
};

// Reads a lesson or training game. Problems are appended to `problems`;
// returns nothing when the file cannot be used at all.
std::optional<Lesson> parseLesson(std::string_view text, std::string_view file, LessonKind kind, std::vector<Diagnostic>& problems);

// The slug of a content file: its name without the order number and the
// extension ("03-first-colony.toml" gives "first-colony").
std::string slugOf(std::string_view fileName);

} // namespace opense4::learn
