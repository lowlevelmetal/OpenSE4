#pragma once

// The tutorial input lock (docs/LEARNING.md "The input lock"): while a
// tutorial step is active the player can use only what the step allows. The
// mode makes a LockState at the end of each frame from that frame's UI tags,
// and every input event of the next frame passes through the lock before
// Dear ImGui and the main window see it: clicks, drags and the wheel only
// over the allowed areas, keys only when the step allows them.
//
// Windows overlap: the lock knows the open classic windows in the order they
// are shown, and where the pointer is over one, the front-most window there
// decides, as it is the one that gets the click.
//
// The same geometry tells the lesson how a player who closed or covered the
// step's window gets back to it (findRecovery, docs/LEARNING.md "Getting
// back").

#include "client/input.hpp"
#include "learn/lesson.hpp"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::client::classic {

struct LockArea {
    ImVec2 min, max;
    bool contains(ImVec2 p) const { return p.x >= min.x && p.y >= min.y && p.x < max.x && p.y < max.y; }
    ImVec2 centre() const { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
    LockArea grown(float by) const { return {ImVec2(min.x - by, min.y - by), ImVec2(max.x + by, max.y + by)}; }
};

// An open classic window as the lock sees it.
struct LockWindow {
    std::string id;                  // its window id ("designs")
    LockArea area;
    // The step names the window (one of its tags): only its allowed and look
    // parts respond. A window the step says nothing about is the player's: all
    // of it responds.
    bool constrained = false;
    std::vector<LockArea> areas;      // where the pointer may act
    std::vector<LockArea> lookAreas;  // where it may only point and scroll
};

struct LockState {
    bool active = false;             // a tutorial step locks the input
    // Above every window and always usable: the game's prompts and Dear
    // ImGui's popups, the lesson panel and the T button.
    std::vector<LockArea> top;
    std::vector<LockWindow> windows; // the open classic windows, front first
    std::vector<LockArea> areas;     // the main window's parts (under every window) where the pointer may act
    std::vector<LockArea> lookAreas; // where it may only point and scroll (an explanation step's outlines)
    std::vector<KeyChord> keys;      // chords that pass
    bool typing = false;             // a text field has the keyboard: every key passes
    bool prompt = false;             // a prompt or popup is open: its answer keys pass (Y, N, T, S, Enter, Esc)
    bool windowKeys = false;         // the window in front is not locked: Esc and Enter (close it) pass

    // Whether the pointer may act at `p`: a top area, else the front-most
    // window there (all of it, or its allowed parts), else the main window's
    // allowed parts.
    bool allows(ImVec2 p) const;
    // Whether it may only point and scroll there.
    bool looks(ImVec2 p) const;
    // The same lock with every part grown by `by` on each side (windows and
    // prompts keep their size): the spotlight's clear areas reach round the
    // outlines, which lie just outside their parts.
    LockState grown(float by) const;
    // Every rectangle it holds: windows, parts, prompts (to cut the screen at).
    std::vector<LockArea> rects() const;
    // How many parts respond (for reports).
    size_t parts() const;
};

// A UI tag of the frame drawn last, and where it is. `pager`: the tag of an
// order on another page of the order strip, on the page arrow that leads
// there (UiTag::pager).
struct TaggedArea {
    std::string_view name;
    LockArea area;
    bool pager = false;
};

// The lock for a step, from the frame drawn last:
// - the areas of the step's allowed tags, and of its highlighted ones when it
//   waits for an action (a step with `done`); an explanation step's outlines
//   can be pointed at and scrolled, not clicked, except a page arrow that
//   stands in for an outlined order (it only turns the page);
// - the lesson panel and the T button; when a window the step works in is
//   closed, the tags that open it (so a step never waits behind a closed
//   window); when another window covers an outlined part, that window's Close
//   button;
// - every open window the step says nothing about (the game opened it, or an
//   allowed click did), and the game's prompts and ImGui popups;
// - the step's keys, the hotkeys of its tags (a command button's F-key, End
//   Turn's F12, an order's letter, Esc and Enter for a `:close` tag), the
//   panel's keys (Ctrl+H, Next, Back, Skip, Read More) and Shift+F1.
// `openWindows` are the open windows' ids back to front (the last one is in
// front); each window's rectangle is its `window:<id>` tag.
LockState makeLockState(const learn::Step& step, const std::vector<TaggedArea>& tags, const std::vector<std::string>& openWindows,
                        const std::vector<LockArea>& prompts, bool typing, const Bindings& bindings);
// The actions whose keys do what a click on the tag does.
std::vector<Action> tagActions(std::string_view tag);
// The window a tag is in ("research:areas", "window:research"), if any.
std::optional<std::string_view> tagWindowId(std::string_view tag);

// ---- Getting back (docs/LEARNING.md "Getting back") ----------------------------------------

// The open window that covers the middle of the tag's part: the front-most
// one in front of the window the tag is in (any window, for the main
// window's parts). `openWindows` back to front, as for makeLockState.
std::optional<std::string> coveringWindow(std::string_view tag, const std::vector<TaggedArea>& tags,
                                          const std::vector<std::string>& openWindows);

// How the player gets back to a step whose outlined parts cannot be used.
struct Recovery {
    enum class Kind : uint8_t {
        None,     // an outlined part is on screen and nothing covers it (or nothing can be done)
        Reopen,   // the window the part is in was closed: `press` opens it again (then `then`)
        Uncover,  // another window covers the part: close `window` first
    };
    Kind kind = Kind::None;
    std::string target;   // the outlined part it is about
    std::string window;   // Reopen: the window that was closed; Uncover: the window in the way
    std::string press;    // what to outline: the opener on screen, or the covering window's Close button (may be empty)
    std::string then;     // Reopen two windows deep: the opener in the window `press` opens ("designs:create")
    bool operator==(const Recovery&) const = default;
};
Recovery findRecovery(const learn::Step& step, const std::vector<TaggedArea>& tags, const std::vector<std::string>& openWindows);

// The step's outlined orders that are on another page of the order strip
// (800x600): their tags lie on a page arrow (TaggedArea::pager), which the
// lesson outlines in their place. In the step's order.
std::vector<std::string> pagedTargets(const learn::Step& step, const std::vector<TaggedArea>& tags);
// What the panel says about them (Markdown): "Press the outlined arrow to
// show more order buttons: **Explore** is on another page." Empty for none.
std::string pagerHint(const learn::Step& step, const std::vector<TaggedArea>& tags);

// Whether a step's `done` waits on the game rather than on a click: turns,
// the empire's counts that grow with them (systems explored, colonies,
// empires met, treaties, ...) and battles that play out. Such a step is
// never offered Skip for taking long.
bool waitsOnGame(const learn::Condition& done);

enum class InputVerdict : uint8_t {
    Pass,         // as it is
    Drop,         // not at all
    PointerAway,  // the pointer is over a locked area: tell ImGui it is nowhere (no hover)
};

class InputLock {
public:
    void set(LockState s) { state_ = std::move(s); }
    const LockState& state() const { return state_; }
    bool active() const { return state_.active; }

    InputVerdict mouseMove(ImVec2 p) const;
    InputVerdict mouseButton(ImVec2 p, int button, bool down);
    InputVerdict wheel(ImVec2 p) const;
    // A key going down or up, with the modifiers held.
    InputVerdict key(const KeyChord& chord, bool down) const;
    InputVerdict text() const;

    // Where the last press the lock refused was, once.
    std::optional<ImVec2> takeRefused() {
        const auto r = refused_;
        refused_.reset();
        return r;
    }

private:
    bool anyHeld() const;

    LockState state_;
    std::array<uint8_t, 8> held_{};       // buttons pressed over an allowed area: their drags and releases pass
    std::array<uint8_t, 8> swallowed_{};  // presses refused: their releases are refused too
    std::optional<ImVec2> refused_;
};

} // namespace opense4::client::classic
