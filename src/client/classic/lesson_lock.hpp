#pragma once

// The tutorial input lock (docs/LEARNING.md "The input lock"): while a
// tutorial step is active the player can use only what the step allows. The
// mode makes a LockState at the end of each frame from that frame's UI tags,
// and every input event of the next frame passes through the lock before
// Dear ImGui and the main window see it: clicks, drags and the wheel only
// over the allowed areas, keys only when the step allows them.

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
};

struct LockState {
    bool active = false;             // a tutorial step locks the input
    std::vector<LockArea> areas;     // where the pointer may act (ImGui screen units)
    std::vector<LockArea> lookAreas; // where it may only point and scroll (an explanation step's outlines)
    std::vector<KeyChord> keys;      // chords that pass
    bool typing = false;             // a text field has the keyboard: every key passes
    bool prompt = false;             // a prompt or popup is open: its answer keys pass (Y, N, T, S, Enter, Esc)
    bool windowKeys = false;         // the window in front is not locked: Esc and Enter (close it) pass
};

// A UI tag of the frame drawn last, and where it is.
struct TaggedArea {
    std::string_view name;
    LockArea area;
};

// The lock for a step, from the frame drawn last:
// - the areas of the step's allowed tags, and of its highlighted ones when it
//   waits for an action (a step with `done`); an explanation step's outlines
//   can be pointed at and scrolled, not clicked;
// - the lesson panel and the T button; when a window the step works in is
//   closed, the tags that open it (so a step never waits behind a closed window);
// - every open window the step says nothing about (the game opened it, or an
//   allowed click did), and the game's prompts and ImGui popups;
// - the step's keys, the hotkeys of its tags (a command button's F-key, End
//   Turn's F12, an order's letter, Esc and Enter for a `:close` tag) and
//   Ctrl+H for the panel.
LockState makeLockState(const learn::Step& step, const std::vector<TaggedArea>& tags, const std::vector<std::string>& openWindows,
                        const std::vector<LockArea>& prompts, bool typing, const Bindings& bindings);
// The actions whose keys do what a click on the tag does.
std::vector<Action> tagActions(std::string_view tag);
// The window a tag is in ("research:areas", "window:research"), if any.
std::optional<std::string_view> tagWindowId(std::string_view tag);

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
    bool allowedAt(ImVec2 p) const;
    bool lookAt(ImVec2 p) const;
    bool anyHeld() const;

    LockState state_;
    std::array<uint8_t, 8> held_{};       // buttons pressed over an allowed area: their drags and releases pass
    std::array<uint8_t, 8> swallowed_{};  // presses refused: their releases are refused too
    std::optional<ImVec2> refused_;
};

} // namespace opense4::client::classic
