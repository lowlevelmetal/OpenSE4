// The tutorial input lock (client/classic/lesson_lock.hpp, docs/LEARNING.md
// "The input lock"): hit-testing against the tags of the frame drawn last,
// windows stacked over each other, keys, and the windows and prompts it never
// covers; and the way back to a closed or covered window ("Getting back").

#include "client/classic/lesson_lock.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace opense4;
using namespace opense4::client;
using namespace opense4::client::classic;

namespace {

LockArea box(float x0, float y0, float x1, float y1) { return {ImVec2(x0, y0), ImVec2(x1, y1)}; }

bool hasKey(const LockState& s, KeyChord c) { return std::find(s.keys.begin(), s.keys.end(), c) != s.keys.end(); }

bool allows(const LockState& s, ImVec2 p) { return s.allows(p); }
bool looks(const LockState& s, ImVec2 p) { return s.looks(p); }

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
    CHECK(allows(st, {20, 20}));          // the outlined command button
    CHECK(allows(st, {60, 20}));          // End Turn, allowed
    CHECK(allows(st, {800, 600}));        // the lesson panel
    CHECK(allows(st, {1000, 10}));        // the T button
    CHECK_FALSE(allows(st, {300, 300}));  // the system view
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
    CHECK_FALSE(allows(st, {20, 20}));
    CHECK(looks(st, {20, 20}));
    CHECK(looks(st, {300, 300}));
    CHECK(allows(st, {800, 600}));        // Next, in the panel
    CHECK_FALSE(hasKey(st, keys.chords(Action::Research)[0]));
}

TEST_CASE("lesson lock: windows, closed and open, and the game's prompts") {
    const Bindings keys;
    const learn::Step s = step({"research:areas"}, true);
    // Research closed: its command button opens it.
    LockState st = makeLockState(s, frameTags(), {}, {}, false, keys);
    CHECK(allows(st, {20, 20}));
    CHECK(hasKey(st, keys.chords(Action::Research)[0]));
    // Research open: only the areas list in it; the rest of it is locked.
    st = makeLockState(s, frameTags(), {"research"}, {}, false, keys);
    CHECK(allows(st, {200, 300}));
    CHECK_FALSE(allows(st, {800, 300}));  // elsewhere in the window
    CHECK_FALSE(allows(st, {300, 590}));  // its Close button: the step is not about closing it
    CHECK_FALSE(allows(st, {50, 650}));   // the system view behind it
    CHECK_FALSE(allows(st, {20, 20}));    // its opener is not needed now
    CHECK_FALSE(st.windowKeys);
    // A window the step says nothing about (the Log the turn opened) is the player's, Esc and Enter included.
    st = makeLockState(s, frameTags(), {"research", "log"}, {}, false, keys);
    CHECK(allows(st, {150, 150}));
    CHECK(st.windowKeys);
    // The game's prompts and popups are never locked, and answer with their keys.
    st = makeLockState(s, frameTags(), {"research"}, {box(400, 300, 600, 400)}, false, keys);
    CHECK(allows(st, {500, 350}));
    CHECK(st.prompt);
    // A Close tag brings Esc and Enter.
    st = makeLockState(step({"research:close"}, true), frameTags(), {"research"}, {}, false, keys);
    CHECK(allows(st, {300, 590}));
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

namespace {

// Designs with the designer over it, as T4 has them: the designer covers most
// of Designs, whose Create and Simulator buttons it leaves free.
std::vector<TaggedArea> stackedTags() {
    return {
        {"command:designs", box(10, 10, 40, 40)},
        {"button:end-turn", box(50, 10, 80, 40)},
        {"panel:system", box(0, 100, 600, 700)},
        {"lesson:panel", box(700, 500, 1000, 760)},
        {"status:lesson", box(990, 2, 1010, 20)},
        {"window:designs", box(100, 60, 900, 700)},
        {"designs:create", box(750, 100, 890, 130)},
        {"designs:simulator", box(750, 140, 890, 170)},
        {"designs:close", box(750, 650, 890, 690)},
        {"window:create-design", box(120, 80, 700, 680)},
        {"create-design:components", box(140, 300, 400, 600)},
        {"create-design:hull", box(140, 100, 400, 140)},
        {"create-design:close", box(500, 640, 690, 670)},   // its Cancel
    };
}

} // namespace

TEST_CASE("lesson lock: the front-most window under the pointer decides") {
    const Bindings keys;
    const learn::Step s = step({"create-design:components"}, true);
    // Designs behind, the designer in front: the step names the designer only.
    const LockState st = makeLockState(s, stackedTags(), {"designs", "create-design"}, {}, false, keys);
    REQUIRE(st.windows.size() == 2);
    CHECK(st.windows[0].id == "create-design");   // front first
    CHECK(st.windows[0].constrained);
    CHECK_FALSE(st.windows[1].constrained);
    CHECK(allows(st, {200, 400}));              // the components list
    CHECK_FALSE(allows(st, {600, 655}));        // the designer's Cancel, over Designs
    CHECK_FALSE(allows(st, {300, 120}));        // its hull list: another step's
    CHECK_FALSE(allows(st, {450, 400}));        // the rest of the designer, though Designs lies under it
    CHECK(allows(st, {800, 115}));              // Designs where the designer leaves it free: the player's
    CHECK(allows(st, {800, 670}));              // Designs' own Close
    CHECK_FALSE(allows(st, {50, 650}));         // the system view, outside both windows
    CHECK(allows(st, {800, 600}));              // the lesson panel lies above every window
    CHECK_FALSE(st.windowKeys);                 // the designer in front is the step's: no Esc
    CHECK(st.parts() > 0);

    // The player brought Designs to the front: all of it is theirs, the designer's parts under it too.
    const LockState back = makeLockState(s, stackedTags(), {"create-design", "designs"}, {}, false, keys);
    CHECK(back.windows[0].id == "designs");
    CHECK(allows(back, {600, 655}));
    CHECK(allows(back, {450, 400}));
    CHECK(back.windowKeys);

    // A prompt over the designer passes, its keys too.
    const LockState asked = makeLockState(s, stackedTags(), {"designs", "create-design"}, {box(400, 300, 600, 400)}, false, keys);
    CHECK(allows(asked, {500, 350}));
    CHECK(asked.prompt);
}

TEST_CASE("lesson lock: the main window's parts under windows") {
    const Bindings keys;
    // End Turn is allowed; a window the step names lies over part of it, another one the step leaves alone over the rest.
    std::vector<TaggedArea> tags = frameTags();
    tags.push_back({"window:queues", box(45, 5, 65, 45)});
    tags.push_back({"queues:list", box(46, 6, 50, 10)});
    const learn::Step s = step({"button:end-turn"}, true, {"queues:list"});
    LockState st = makeLockState(s, tags, {"queues"}, {}, false, keys);
    CHECK_FALSE(allows(st, {55, 20}));   // under Construction Queues, which the step names
    CHECK(allows(st, {70, 20}));         // the rest of End Turn
    CHECK(allows(st, {48, 8}));          // the window's allowed part
    // A window the step says nothing about over the button: the window's, so the click is.
    st = makeLockState(step({"button:end-turn"}, true), tags, {"queues"}, {}, false, keys);
    CHECK(allows(st, {55, 20}));
    // An explanation step's outline under a window the step names: neither click nor look.
    st = makeLockState(step({"button:end-turn"}, false, {"queues:list"}), tags, {"queues"}, {}, false, keys);
    CHECK_FALSE(looks(st, {55, 20}));
    CHECK(looks(st, {70, 20}));
    CHECK_FALSE(allows(st, {70, 20}));
}

TEST_CASE("lesson lock: a window that covers an outline") {
    const Bindings keys;
    std::vector<TaggedArea> tags = stackedTags();
    // Designs alone, over the command button of another window.
    tags.push_back({"command:empire-status", box(760, 600, 780, 620)});
    CHECK(coveringWindow("command:empire-status", tags, {"designs"}) == "designs");
    CHECK_FALSE(coveringWindow("command:designs", tags, {"designs"}));            // outside it
    CHECK_FALSE(coveringWindow("command:empire-status", tags, {}));
    // A window's own parts are covered only by windows in front of it.
    CHECK(coveringWindow("designs:create", tags, {"designs", "create-design"}) == std::nullopt);   // the designer leaves it free
    CHECK(coveringWindow("create-design:components", tags, {"create-design", "designs"}) == "designs");
    CHECK_FALSE(coveringWindow("create-design:components", tags, {"designs", "create-design"}));
    CHECK_FALSE(coveringWindow("lesson:panel", tags, {"designs"}));

    // A window the step names covers its outline: its Close button (and Esc) become usable.
    const learn::Step s = step({"command:empire-status"}, true, {"designs:list"});
    const LockState st = makeLockState(s, tags, {"designs"}, {}, false, keys);
    CHECK(allows(st, {800, 670}));
    CHECK(hasKey(st, KeyChord{ImGuiKey_Escape}));
    CHECK_FALSE(allows(st, {800, 115}));   // nothing else of it
}

TEST_CASE("lesson recovery: the way back to a closed or covered window") {
    std::vector<TaggedArea> tags = stackedTags();
    const learn::Step components = step({"create-design:components"}, true);
    // The designer is open: nothing to do.
    CHECK(findRecovery(components, tags, {"designs", "create-design"}).kind == Recovery::Kind::None);

    // The designer was closed (its tags gone): Create in Designs opens it again.
    std::vector<TaggedArea> noDesigner;
    for (const TaggedArea& t : tags)
        if (!t.name.starts_with("create-design:") && t.name != "window:create-design") noDesigner.push_back(t);
    Recovery r = findRecovery(components, noDesigner, {"designs"});
    CHECK(r.kind == Recovery::Kind::Reopen);
    CHECK(r.target == "create-design:components");
    CHECK(r.window == "create-design");
    CHECK(r.press == "designs:create");
    CHECK(r.then.empty());

    // Designs was closed too: its command button first, then Create.
    std::vector<TaggedArea> mainOnly;
    for (const TaggedArea& t : noDesigner)
        if (!t.name.starts_with("designs:") && t.name != "window:designs") mainOnly.push_back(t);
    r = findRecovery(components, mainOnly, {});
    CHECK(r.kind == Recovery::Kind::Reopen);
    CHECK(r.press == "command:designs");
    CHECK(r.then == "designs:create");

    // An outlined main-window button under Designs: close Designs first.
    tags.push_back({"command:empire-status", box(760, 600, 780, 620)});
    r = findRecovery(step({"command:empire-status"}, true), tags, {"designs"});
    CHECK(r.kind == Recovery::Kind::Uncover);
    CHECK(r.window == "designs");
    CHECK(r.press == "designs:close");
    // Not covered: nothing to do. An explanation step's outlines count as well.
    CHECK(findRecovery(step({"command:designs"}, false), tags, {"designs"}).kind == Recovery::Kind::None);
    // No way back on screen: nothing to show (Skip comes later).
    CHECK(findRecovery(step({"tactical-combat:map"}, true), mainOnly, {}).kind == Recovery::Kind::None);
    // Only the lesson's own tags: nothing to recover.
    CHECK(findRecovery(step({"lesson:next"}, false), mainOnly, {}).kind == Recovery::Kind::None);
}

TEST_CASE("lesson recovery: steps that wait on the game are never timed out") {
    std::vector<learn::Diagnostic> problems;
    auto waits = [&](std::string_view text) {
        const auto c = learn::parseCondition(text, "test", 1, problems);
        REQUIRE(c);
        return waitsOnGame(*c);
    };
    CHECK(waits("{ turns_passed = 1 }"));
    CHECK(waits("{ systems_explored = 5 }"));
    CHECK(waits("{ empires_met = 1 }"));
    CHECK(waits("{ techs_researched = 1 }"));
    CHECK(waits("{ colonies = 2 }"));
    CHECK(waits("{ any = [{ treaty = \"non-aggression\" }, { treaties = 1 }, { turns_passed = 3 }] }"));
    CHECK(waits("{ battle_order = \"fire\" }"));
    CHECK(waits("{ not = { window = \"tactical-combat\" } }"));       // the battle plays out
    CHECK_FALSE(waits("{ window = \"tactical-combat\" }"));             // Begin in the simulator opens it
    CHECK_FALSE(waits("{ window = \"research\" }"));
    CHECK_FALSE(waits("{ not = { window = \"galaxy-map\" } }"));
    CHECK_FALSE(waits("{ selected = \"ship\" }"));
    CHECK_FALSE(waits("{ order = \"explore\" }"));
    CHECK_FALSE(waits("{ command = \"QueueAdd\" }"));
    CHECK_FALSE(waits("{ design_components = 5 }"));
    CHECK_FALSE(waits("{ research_queued = 3 }"));
    CHECK_FALSE(waits("{ all = [{ simulator_owners = 2 }, { simulator_items = 3 }] }"));
    CHECK(problems.empty());
}
