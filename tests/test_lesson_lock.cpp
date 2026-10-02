// The tutorial input lock (client/classic/lesson_lock.hpp, docs/LEARNING.md
// "The input lock"): hit-testing against the tags of the frame drawn last,
// keys, and the windows and prompts it never covers.

#include "client/classic/lesson_lock.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace opense4;
using namespace opense4::client;
using namespace opense4::client::classic;

namespace {

LockArea box(float x0, float y0, float x1, float y1) { return {ImVec2(x0, y0), ImVec2(x1, y1)}; }

bool hasKey(const LockState& s, KeyChord c) { return std::find(s.keys.begin(), s.keys.end(), c) != s.keys.end(); }

bool covers(const std::vector<LockArea>& areas, ImVec2 p) {
    return std::any_of(areas.begin(), areas.end(), [&](const LockArea& a) { return a.contains(p); });
}

// A main window and a Research window, as the client tags them.
std::vector<TaggedArea> frameTags() {
    return {
        {"command:research", box(10, 10, 40, 40)},
        {"button:end-turn", box(50, 10, 80, 40)},
        {"panel:system", box(0, 100, 600, 700)},
        {"lesson:panel", box(700, 500, 1000, 760)},
        {"status:lesson", box(990, 2, 1010, 20)},
        {"window:research", box(120, 140, 900, 620)},
        {"research:areas", box(130, 180, 700, 440)},
        {"research:close", box(200, 580, 380, 610)},
        {"window:log", box(100, 100, 300, 300)},
    };
}

learn::Step step(std::vector<std::string> highlight, bool action, std::vector<std::string> allow = {}, std::vector<std::string> keys = {}) {
    learn::Step s;
    s.highlight = std::move(highlight);
    s.allow = std::move(allow);
    s.keys = std::move(keys);
    if (action) s.done = learn::Condition{};
    return s;
}

} // namespace

TEST_CASE("lesson lock: clicks, drags and the wheel only over the allowed areas") {
    InputLock lock;
    // Off: everything passes.
    CHECK(lock.mouseButton({5, 5}, 1, true) == InputVerdict::Pass);
    CHECK(lock.mouseButton({5, 5}, 1, false) == InputVerdict::Pass);

    LockState s;
    s.active = true;
    s.areas = {box(10, 10, 40, 40)};
    s.lookAreas = {box(100, 100, 200, 200)};
    lock.set(s);
    CHECK(lock.mouseMove({20, 20}) == InputVerdict::Pass);
    CHECK(lock.mouseMove({150, 150}) == InputVerdict::Pass);           // look, point
    CHECK(lock.mouseMove({300, 300}) == InputVerdict::PointerAway);    // nothing hovers there
    CHECK(lock.wheel({20, 20}) == InputVerdict::Pass);
    CHECK(lock.wheel({150, 150}) == InputVerdict::Pass);               // look, scroll
    CHECK(lock.wheel({300, 300}) == InputVerdict::Drop);

    // A refused press: its release is refused too, and the lock says where.
    CHECK(lock.mouseButton({300, 300}, 1, true) == InputVerdict::Drop);
    CHECK(lock.mouseButton({20, 20}, 1, false) == InputVerdict::Drop);
    const auto refused = lock.takeRefused();
    REQUIRE(refused);
    CHECK(refused->x == 300);
    CHECK_FALSE(lock.takeRefused());
    // A look area cannot be clicked.
    CHECK(lock.mouseButton({150, 150}, 1, true) == InputVerdict::Drop);
    CHECK(lock.mouseButton({150, 150}, 1, false) == InputVerdict::Drop);
    CHECK(lock.takeRefused());

    // An allowed press: its drag may leave the area (dragging the panel), and its release passes.
    CHECK(lock.mouseButton({20, 20}, 1, true) == InputVerdict::Pass);
    CHECK(lock.mouseMove({300, 300}) == InputVerdict::Pass);
    CHECK(lock.mouseButton({300, 300}, 1, false) == InputVerdict::Pass);
    CHECK(lock.mouseMove({300, 300}) == InputVerdict::PointerAway);
    // Right clicks follow the same rule.
    CHECK(lock.mouseButton({20, 20}, 3, true) == InputVerdict::Pass);
    CHECK(lock.mouseButton({20, 20}, 3, false) == InputVerdict::Pass);
}

TEST_CASE("lesson lock: keys") {
    InputLock lock;
    LockState s;
    s.active = true;
    s.keys = {KeyChord{ImGuiKey_F12}, KeyChord{ImGuiKey_L, true}};
    lock.set(s);
    CHECK(lock.key(KeyChord{ImGuiKey_F12}, true) == InputVerdict::Pass);
    CHECK(lock.key(KeyChord{ImGuiKey_L, true}, true) == InputVerdict::Pass);
    CHECK(lock.key(KeyChord{ImGuiKey_L}, true) == InputVerdict::Drop);          // L alone is another key
    CHECK(lock.key(KeyChord{ImGuiKey_F8}, true) == InputVerdict::Drop);
    CHECK(lock.key(KeyChord{ImGuiKey_LeftCtrl, true}, true) == InputVerdict::Pass);   // modifiers make chords
    CHECK(lock.key(KeyChord{ImGuiKey_F8}, false) == InputVerdict::Pass);        // releases never matter
    CHECK(lock.text() == InputVerdict::Drop);
    CHECK(lock.key(KeyChord{ImGuiKey_Escape}, true) == InputVerdict::Drop);
    CHECK(lock.key(KeyChord{ImGuiKey_Y}, true) == InputVerdict::Drop);

    // A prompt answers with its keys.
    s.prompt = true;
    lock.set(s);
    CHECK(lock.key(KeyChord{ImGuiKey_Y}, true) == InputVerdict::Pass);
    CHECK(lock.key(KeyChord{ImGuiKey_Escape}, true) == InputVerdict::Pass);
    CHECK(lock.key(KeyChord{ImGuiKey_F8}, true) == InputVerdict::Drop);

    // A window in front that the step leaves alone closes with Esc or Enter.
    s.prompt = false;
    s.windowKeys = true;
    lock.set(s);
    CHECK(lock.key(KeyChord{ImGuiKey_Escape}, true) == InputVerdict::Pass);
    CHECK(lock.key(KeyChord{ImGuiKey_Enter}, true) == InputVerdict::Pass);
    CHECK(lock.key(KeyChord{ImGuiKey_Y}, true) == InputVerdict::Drop);

    // A text field that has the keyboard takes every key.
    s.typing = true;
    lock.set(s);
    CHECK(lock.key(KeyChord{ImGuiKey_A}, true) == InputVerdict::Pass);
    CHECK(lock.text() == InputVerdict::Pass);
}

TEST_CASE("lesson lock: an action step allows its outlines and their hotkeys") {
    const Bindings keys;
    const learn::Step s = step({"command:research"}, true, {"button:end-turn"}, {"Ctrl+L"});
    const LockState st = makeLockState(s, frameTags(), {}, {}, false, keys);
    CHECK(st.active);
    CHECK(covers(st.areas, {20, 20}));          // the outlined command button
    CHECK(covers(st.areas, {60, 20}));          // End Turn, allowed
    CHECK(covers(st.areas, {800, 600}));        // the lesson panel
    CHECK(covers(st.areas, {1000, 10}));        // the T button
    CHECK_FALSE(covers(st.areas, {300, 300}));  // the system view
    CHECK(hasKey(st, keys.chords(Action::Research)[0]));   // F8, as the button
    CHECK(hasKey(st, keys.chords(Action::EndTurn)[0]));    // F12
    CHECK(hasKey(st, keys.chords(Action::LessonText)[0])); // Ctrl+H, always
    CHECK(hasKey(st, KeyChord{ImGuiKey_L, true}));         // the step's own
    CHECK_FALSE(hasKey(st, keys.chords(Action::Help)[0]));
    CHECK_FALSE(st.prompt);
}

TEST_CASE("lesson lock: an explanation step's outlines are to look at") {
    const Bindings keys;
    const learn::Step s = step({"command:research", "panel:system"}, false);
    const LockState st = makeLockState(s, frameTags(), {}, {}, false, keys);
    CHECK_FALSE(covers(st.areas, {20, 20}));
    CHECK(covers(st.lookAreas, {20, 20}));
    CHECK(covers(st.lookAreas, {300, 300}));
    CHECK(covers(st.areas, {800, 600}));        // Next, in the panel
    CHECK_FALSE(hasKey(st, keys.chords(Action::Research)[0]));
}

TEST_CASE("lesson lock: windows, closed and open, and the game's prompts") {
    const Bindings keys;
    const learn::Step s = step({"research:areas"}, true);
    // Research closed: its command button opens it.
    LockState st = makeLockState(s, frameTags(), {}, {}, false, keys);
    CHECK(covers(st.areas, {20, 20}));
    CHECK(hasKey(st, keys.chords(Action::Research)[0]));
    // Research open: only the areas list in it; the rest of it is locked.
    st = makeLockState(s, frameTags(), {"research"}, {}, false, keys);
    CHECK(covers(st.areas, {200, 300}));
    CHECK_FALSE(covers(st.areas, {800, 300}));  // elsewhere in the window
    CHECK_FALSE(covers(st.areas, {300, 590}));  // its Close button: the step is not about closing it
    CHECK_FALSE(covers(st.areas, {50, 650}));   // the system view behind it
    CHECK_FALSE(covers(st.areas, {20, 20}));    // its opener is not needed now
    CHECK_FALSE(st.windowKeys);
    // A window the step says nothing about (the Log the turn opened) is the player's, Esc and Enter included.
    st = makeLockState(s, frameTags(), {"research", "log"}, {}, false, keys);
    CHECK(covers(st.areas, {150, 150}));
    CHECK(st.windowKeys);
    // The game's prompts and popups are never locked, and answer with their keys.
    st = makeLockState(s, frameTags(), {"research"}, {box(400, 300, 600, 400)}, false, keys);
    CHECK(covers(st.areas, {500, 350}));
    CHECK(st.prompt);
    // A Close tag brings Esc and Enter.
    st = makeLockState(step({"research:close"}, true), frameTags(), {"research"}, {}, false, keys);
    CHECK(covers(st.areas, {300, 590}));
    CHECK(hasKey(st, KeyChord{ImGuiKey_Escape}));
    CHECK(hasKey(st, KeyChord{ImGuiKey_Enter}));
    CHECK(st.typing == false);
    CHECK(makeLockState(s, frameTags(), {}, {}, true, keys).typing);
}

TEST_CASE("lesson lock: what a tag's keys do") {
    CHECK(tagActions("command:research") == std::vector<Action>{Action::Research});
    CHECK(tagActions("button:end-turn") == std::vector<Action>{Action::EndTurn});
    CHECK(tagActions("order:explore") == std::vector<Action>{Action::Explore});
    CHECK(tagActions("cycle:colony") == std::vector<Action>{Action::NextColony, Action::PreviousColony});
    CHECK(tagActions("panel:system").empty());
    CHECK(tagWindowId("research:areas") == "research");
    CHECK(tagWindowId("window:log") == "log");
    CHECK_FALSE(tagWindowId("panel:system"));
}
