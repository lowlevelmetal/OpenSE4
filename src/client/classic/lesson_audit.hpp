#pragma once

// The lesson audit (docs/LEARNING.md "Checking the lessons"): for a tutorial
// step set up as --lesson-check sets it up, what the input lock lets the
// player do and whether what the step's text names can be seen.
//
// - (A) Every clickable widget and tag the lock lets through, by label and
//   tag, and why (a tag of the step, a window the step leaves free, a prompt,
//   the way to a closed window); flagged when the step does not ask for it and
//   it could change the game or the lesson's path: the options of a chooser
//   the step does not narrow (Create's vehicle types, a list's rows), another
//   tab, another button, a whole window.
// - (B) Every on-screen thing the step's text names (bold terms, names of
//   buttons, boxes, lists and windows, phrases such as "the report"), mapped
//   to the widgets, texts and tags of the frame by their labels and ids, and
//   whether each is on the screen, clear of the spotlight or dimmed, and
//   under the lesson panel. What could not be matched is listed for checking
//   by hand.
//
// The logic is plain (tests/test_lesson_lock.cpp); ClassicMode gathers the
// frame and prints the report (classic_mode.cpp lessonCheckReport).

#include "client/classic/lesson_lock.hpp"
#include "learn/lesson.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// A labelled widget or a text of the frame drawn last (script::lastItems).
struct AuditItem {
    std::string label;   // as written ("Create", "Name##col"; a drawn text "text:Design Detail")
    std::string scope;   // the window id it was drawn by, "main", "lesson", ...
    LockArea area;
    bool disabled = false;
};

struct AuditInput {
    const learn::Step* step = nullptr;
    std::vector<learn::Block> text;            // the step's text, its tokens filled in
    std::vector<TaggedArea> tags;              // the frame's UI tags
    std::vector<std::string> openWindows;      // back to front
    LockState lock;                            // the lock for the step
    LockState spotlight;                       // the same, grown as the spotlight draws it
    std::vector<AuditItem> items;
    std::optional<LockArea> panel;             // the lesson panel
    ImVec2 display;
};

struct AuditReport {
    std::vector<std::string> lines;   // "A ...", "B ..." lines, flagged ones with " FLAG: <why>"
    int flags = 0;                    // lines flagged
    int unmatched = 0;                // references matched to nothing (to check by hand)
};
AuditReport auditStep(const AuditInput& in);

// The on-screen things a step's text names: its bold terms, the capitalised
// names in it (buttons, boxes, lists, windows) and a few phrases ("the
// report", "the system view", "the tabs"), each once, in the order written.
std::vector<std::string> textReferences(const std::vector<learn::Block>& text);

} // namespace opense4::client::classic
