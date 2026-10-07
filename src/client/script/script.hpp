#pragma once

// Input scripts (docs/BUILDING.md "Input scripts"): scripted clicks, keys
// and checks played against the running client through its real input
// path. A script is a text file with one step per line:
//
//     options --tutorial=first-steps --layout=1024x768
//     click tag:lesson:next
//     click sector:!planet+!ship
//     wait-step 4
//     key F12
//     assert { turns_passed = 1 }
//     repeat 10 until { battle_order = "fire" }
//       ...
//     end
//
// This is the format and its parser (headless); player.hpp plays it.

#include "client/input.hpp"
#include "learn/condition.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::script {

// What a pointer step points at.
enum class TargetKind : uint8_t {
    Tag,     // tag:<name>: a UI tag (docs/LEARNING.md "UI tags")
    Item,    // item:<label>: a widget by its label (items.hpp), with in=<scope> and nth=<n>
    Window,  // window:<id>: a window (its tag, window:<id>)
    Sector,  // sector:<x>,<y> or sector:<query>: a sector of the system view
    System,  // system:<query>: a star system of the galaxy panel
    At,      // at:<x>,<y>: a point of the classic frame (frame pixels)
};

// A point inside a target's rectangle: frame pixels from its left or top
// edge, from its right or bottom edge when negative, or a percentage.
struct Offset {
    bool set = false;
    float x = 0, y = 0;
    bool xPercent = false, yPercent = false;
};

struct Target {
    TargetKind kind = TargetKind::Tag;
    std::string name;     // the tag, label, window id, or query
    std::string scope;    // items: in=<scope> (a window id, "main", "lesson", "front", a Dear ImGui window's name, or tag:<name>)
    int nth = 1;          // the n-th match (1-based)
    Offset offset;        // @x,y
    float x = 0, y = 0;   // at:x,y and sector:x,y
    bool numeric = false; // sector:x,y (not a query)
    std::string text;     // as written, for messages
};

// What a window-event step says happened to the game's window: the events the
// system sends when the player switches to another window, minimizes the game,
// and back (client/window_presence.hpp).
enum class WindowChange : uint8_t { FocusLost, FocusGained, Minimized, Restored, Hidden, Shown, Occluded, Exposed };
std::string_view windowChangeName(WindowChange c);   // "focus-lost", ...

enum class Op : uint8_t {
    // Pointer and keyboard, and the window.
    Click, DoubleClick, RightClick, MiddleClick, Drag, Move, Wheel, Key, Type, WindowEvent,
    // Waiting.
    Wait, WaitFor, WaitGone, WaitWindow, WaitClosed, WaitStep, WaitUntil, WaitTurn, WaitResult, WaitScreen, WaitLesson,
    // Checks.
    AssertPresent, AssertAbsent, AssertEnabled, AssertDisabled, AssertWindow, AssertNoWindow, AssertStep, Assert, AssertLog,
    AssertNoLog, AssertResult, AssertScreen, AssertLesson, AssertTurn, AssertInside, AssertFits, AssertWhole, AssertWindowHit,
    // Other.
    Screenshot, Echo, Print, Dump, Audit,
    // Loops: repeat N [until {condition}] ... end.
    Repeat, End,
};

struct Step {
    int line = 0;
    std::string source;     // the line as written (without its comment)
    Op op = Op::Wait;
    Target target;
    Target to;              // drag: where to
    int64_t number = 0;     // wait: frames; wait-step/assert-step: the step; wait-turn/assert-turn: the turn; wheel: notches
    std::string text;       // type: the text; key: the chord as written; windows, results, screens, lessons, log text, files;
                            // assert-window-hit: what a press there does (client/window_hit.hpp)
    std::vector<std::string> facts;   // print: condition keys
    KeyChord chord;         // key
    WindowChange window = WindowChange::FocusLost;   // window-event
    bool shift = false, ctrl = false, alt = false;   // held during a click, a drag or a wheel turn
    bool refused = false;   // the tutorial input lock must refuse the press or key
    int button = 0;         // drag: 2 middle, 3 right (default left)
    bool optional = false;  // a pointer step whose target may not come: skipped then (after kOptionalTimeout frames)
    std::optional<learn::Condition> condition;      // wait-until, assert
    int timeout = 0;        // frames a wait (or a pointer step's target) may take
    int dragFrames = 8;     // drag: the moves between press and release
    size_t jump = 0;        // repeat: the index of its end; end: the index of its repeat
};

struct Script {
    std::string file;
    std::vector<std::string> options;   // command-line options from `options` lines
    std::vector<Step> steps;
};

// The frames a wait takes at most unless the script says otherwise (`timeout N`,
// or timeout=N on a step): 30 seconds at the fixed 60 frames a second.
inline constexpr int kDefaultTimeout = 1800;
// How long an optional step waits for its target unless it says otherwise.
inline constexpr int kOptionalTimeout = 10;

// Parses a script. Every problem is "file:line: message" in `errors`; the
// script is returned only when there are none.
std::optional<Script> parseScript(std::string_view text, std::string_view file, std::vector<std::string>& errors);
std::optional<Script> loadScript(const std::filesystem::path& path, std::vector<std::string>& errors);

// One line of a script for a value: quoted when it needs to be.
std::string quoteWord(std::string_view text);

// "click tag:x" for messages: the step's verb.
std::string_view opName(Op op);

} // namespace opense4::client::script
