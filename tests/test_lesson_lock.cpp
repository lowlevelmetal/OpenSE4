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
    // A right click in the main window gives orders: only where the step lists it (rightAreas).
    CHECK(lock.mouseButton({20, 20}, 3, true) == InputVerdict::Drop);
    CHECK(lock.mouseButton({20, 20}, 3, false) == InputVerdict::Drop);
    CHECK(lock.takeRefused());
    s.rightAreas = {box(10, 10, 40, 40)};
    lock.set(s);
    CHECK(lock.mouseButton({20, 20}, 3, true) == InputVerdict::Pass);
    CHECK(lock.mouseButton({20, 20}, 3, false) == InputVerdict::Pass);
    CHECK(lock.mouseButton({20, 20}, 2, true) == InputVerdict::Pass);   // the middle button too
    CHECK(lock.mouseButton({20, 20}, 2, false) == InputVerdict::Pass);
}

TEST_CASE("lesson lock: right clicks pass in windows where the pointer may point, and in the main window where the step lists them") {
    const Bindings keys;
    // Research's areas (allowed), its queue (shown) and the rest of the window (locked).
    std::vector<TaggedArea> tags = frameTags();
    tags.push_back({"research:queue", box(130, 450, 700, 580)});
    learn::Step s = step({"research:areas"}, true);
    s.show = {"research:queue"};
    LockState st = makeLockState(s, tags, {"research"}, {}, false, keys);
    CHECK(st.allowsButton({300, 300}, 3));    // an allowed list: a right click shows a report
    CHECK(st.allowsButton({300, 500}, 3));    // a part shown: the same
    CHECK_FALSE(st.allowsButton({800, 200}, 3));   // the window's other parts: no
    CHECK_FALSE(st.allowsButton({300, 500}, 1));   // and a left click on the part shown: no
    CHECK_FALSE(st.allowsButton({50, 650}, 3));    // the system view: a right click is a Move To
    // A window the step says nothing about is the player's, right clicks too.
    st = makeLockState(step({"command:research"}, true), tags, {"log"}, {}, false, keys);
    CHECK(st.allowsButton({200, 200}, 3));
    // An outline listed for right clicks only (the galaxy panel opens the Galaxy Map with one).
    tags.push_back({"panel:galaxy", box(650, 650, 1000, 760)});
    learn::Step g = step({"panel:galaxy"}, true);
    g.rightClick = {"panel:galaxy"};
    st = makeLockState(g, tags, {}, {}, false, keys);
    CHECK(st.allowsButton({660, 700}, 3));
    CHECK_FALSE(st.allowsButton({660, 700}, 1));   // a left click would show another system
    CHECK(st.lit({660, 700}));                     // still clear of the spotlight
    // Listed in `allow` too: both buttons.
    g.allow = {"panel:galaxy"};
    st = makeLockState(g, tags, {}, {}, false, keys);
    CHECK(st.allowsButton({660, 700}, 1));
    CHECK(st.allowsButton({660, 700}, 3));
}

TEST_CASE("lesson lock: a Close button brings Esc and Enter only while its window is open and in front") {
    const Bindings keys;
    std::vector<TaggedArea> tags = frameTags();
    tags.push_back({"log:close", box(110, 270, 290, 295)});
    tags.push_back({"window:designs", box(300, 100, 800, 600)});
    tags.push_back({"designs:close", box(600, 560, 780, 590)});
    auto escape = [](const LockState& st) { return hasKey(st, KeyChord{ImGuiKey_Escape}) || hasKey(st, KeyChord{ImGuiKey_Enter}); };
    // The Log is in front: Esc and Enter close it.
    LockState st = makeLockState(step({"command:research"}, true, {"log:close"}), tags, {"log"}, {}, false, keys);
    CHECK(escape(st));
    // Once it is closed they would end the turn (Enter) or clear the selection (Esc): no.
    st = makeLockState(step({"command:research"}, true, {"log:close"}), tags, {}, {}, false, keys);
    CHECK_FALSE(escape(st));
    // A window left open behind the one in front: its Close button, never the keys (they would close the front one).
    st = makeLockState(step({"research:areas"}, true), tags, {"designs", "research"}, {}, false, keys, {"designs", "research"});
    CHECK_FALSE(escape(st));
    CHECK_FALSE(st.windowKeys);
    st = makeLockState(step({"research:areas"}, true), tags, {"research", "designs"}, {}, false, keys, {"research", "designs"});
    CHECK(escape(st));   // Designs in front, left open: its Close and keys
}

TEST_CASE("lesson lock: a command button the step names names its window") {
    const Bindings keys;
    std::vector<TaggedArea> tags = frameTags();
    tags.push_back({"log:close", box(110, 270, 290, 295)});
    // The Log opened with the step's own command:log: only the Log's parts the step names.
    learn::Step s = step({"command:log"}, false, {"command:log", "log:close"});
    LockState st = makeLockState(s, tags, {"log"}, {}, false, keys);
    REQUIRE(st.windows.size() == 1);
    CHECK(st.windows[0].constrained);
    CHECK_FALSE(st.allows({200, 150}));
    CHECK(st.allows({200, 280}));
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

    // Tab and the arrows move nothing outside a text field.
    CHECK(lock.key(KeyChord{ImGuiKey_Tab}, true) == InputVerdict::Drop);
    CHECK(lock.key(KeyChord{ImGuiKey_DownArrow}, true) == InputVerdict::Drop);
    CHECK(lock.key(KeyChord{ImGuiKey_Space}, true) == InputVerdict::Drop);

    // A text field that has the keyboard takes every key (Tab moves between the
    // window's fields), but Ctrl+Tab, which would bring another window to the front.
    s.typing = true;
    lock.set(s);
    CHECK(lock.key(KeyChord{ImGuiKey_A}, true) == InputVerdict::Pass);
    CHECK(lock.key(KeyChord{ImGuiKey_Tab}, true) == InputVerdict::Pass);
    CHECK(lock.key(KeyChord{ImGuiKey_LeftArrow}, true) == InputVerdict::Pass);
    CHECK(lock.key(KeyChord{ImGuiKey_Tab, true}, true) == InputVerdict::Drop);
    CHECK(lock.key(KeyChord{ImGuiKey_Tab, true, true}, true) == InputVerdict::Drop);
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

    // The queue window closed over Construction Queues: its list (free) opens it
    // again; the Build Queue order under Construction Queues is not the way back.
    const std::vector<TaggedArea> queues{
        {"panel:orders", box(0, 0, 400, 60)},
        {"order:build-queue", box(300, 20, 330, 50)},
        {"window:queues", box(200, 10, 900, 600)},
        {"queues:list", box(220, 100, 880, 500)},
        {"queues:close", box(780, 550, 880, 590)},
    };
    r = findRecovery(step({"set-queue:queue"}, false), queues, {"queues"});
    CHECK(r.kind == Recovery::Kind::Reopen);
    CHECK(r.press == "queues:list");
    // With no free way back, closing the window over one is.
    std::vector<TaggedArea> orderOnly(queues.begin(), queues.begin() + 3);
    r = findRecovery(step({"set-queue:queue"}, false), orderOnly, {"queues"});
    CHECK(r.kind == Recovery::Kind::Uncover);
    CHECK(r.window == "queues");
}

TEST_CASE("lesson pager: an outlined order on another page of the order strip") {
    // 800x600: Move To on the page shown; Explore and Sentry on the next page, their tags on the
    // right arrow; View Orders on the page before, on the left arrow.
    const Bindings keys;
    std::vector<TaggedArea> tags = frameTags();
    tags.push_back({"order:move-to", box(250, 36, 284, 70)});
    tags.push_back({"order:explore", box(420, 44, 434, 94), true});
    tags.push_back({"order:sentry", box(420, 44, 434, 94), true});
    tags.push_back({"order:view-orders", box(230, 44, 244, 94), true});

    CHECK(pagedTargets(step({"order:explore"}, true), tags) == std::vector<std::string>{"order:explore"});
    CHECK(pagedTargets(step({"order:move-to", "order:sentry", "cycle:ship"}, true), tags) == std::vector<std::string>{"order:sentry"});
    // Allowed, not outlined: the arrow is not outlined either, so nothing to say.
    CHECK(pagedTargets(step({"button:end-turn"}, true, {"order:explore"}), tags).empty());
    CHECK(pagerHint(step({"order:move-to"}, true), tags).empty());
    CHECK(pagerHint(step({"order:explore"}, true), tags) == "Press the outlined arrow to show more order buttons: **Explore** is on another page.");
    CHECK(pagerHint(step({"order:sentry", "order:explore"}, false), tags) ==
          "Press the outlined arrow to show more order buttons: **Sentry** and **Explore** are on another page.");
    CHECK(pagerHint(step({"order:explore", "order:view-orders"}, true), tags) ==
          "Press an outlined arrow to show more order buttons: **Explore** and **View Orders** are on other pages.");

    // An action step: the arrow is the outlined order's stand-in, and turns the page.
    LockState st = makeLockState(step({"order:explore"}, true), tags, {}, {}, false, keys);
    CHECK(allows(st, {427, 60}));
    CHECK_FALSE(allows(st, {237, 60}));    // the other arrow leads elsewhere
    CHECK(hasKey(st, KeyChord{ImGuiKey_E}));   // the order's key works on any page
    // An explanation step: outlines are to look at, but a page arrow standing in for one only
    // turns the page, so it responds.
    st = makeLockState(step({"order:move-to", "order:sentry"}, false), tags, {}, {}, false, keys);
    CHECK(allows(st, {427, 60}));
    CHECK_FALSE(allows(st, {260, 50}));    // Move To itself: look only
    CHECK(looks(st, {260, 50}));

    // Once the page is turned, the order's own button is tagged: nothing to say.
    tags.push_back({"order:explore", box(284, 70, 318, 104)});
    CHECK(pagedTargets(step({"order:explore"}, true), tags).empty());
    CHECK(pagerHint(step({"order:explore"}, true), tags).empty());
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

// ---- Choices, parts shown, windows left open (docs/LEARNING.md "Choices") ----------------------

namespace {

// Designs with its Create button, the Select Vehicle Type picker open over it
// (a popup: above every window), and a list whose rows are options.
std::vector<TaggedArea> pickerTags() {
    return {
        {"window:designs", box(100, 100, 900, 700)},
        {"designs:create", box(700, 200, 880, 230)},
        {"designs:list", box(110, 150, 350, 650)},
        {"designs:details", box(360, 150, 690, 650)},
        {"designs:close", box(700, 660, 880, 690)},
        {"lesson:panel", box(10, 500, 90, 760)},
        // The picker's rows, drawn in a popup.
        {"designs:create:ship", box(400, 300, 600, 320), false, true},
        {"designs:create:base", box(400, 320, 600, 340), false, true},
        {"designs:create:fighter", box(400, 340, 600, 360), false, true},
    };
}

} // namespace

TEST_CASE("lesson lock: a step that names an option lets only that option of the chooser through") {
    const Bindings keys;
    learn::Step s = step({"designs:create", "designs:create:ship"}, true);
    const LockArea picker = box(390, 290, 610, 400);
    const LockState st = makeLockState(s, pickerTags(), {"designs"}, {picker}, false, keys);
    // The picker lies above the window: its chosen option passes, the others are refused
    // (pointing and scrolling still work there: the list scrolls).
    CHECK(st.allows({500, 310}));
    CHECK_FALSE(st.allows({500, 330}));
    CHECK_FALSE(st.allows({500, 350}));
    CHECK(st.looks({500, 330}));
    CHECK(st.access({500, 330}) == LockState::Access::Refused);
    // The rest of the picker (its Cancel) is the prompt's, as always.
    CHECK(st.allows({500, 390}));
    // The spotlight dims the refused options and leaves the chosen one clear.
    CHECK(st.lit({500, 310}));
    CHECK_FALSE(st.lit({500, 330}));
    // Grown for the spotlight, the chosen option wins over its refused neighbours.
    const LockState g = st.grown(6);
    CHECK(g.lit({500, 322}));
    CHECK_FALSE(g.lit({500, 335}));
    // A click on a refused option is refused, with its release.
    InputLock lock;
    lock.set(st);
    CHECK(lock.mouseButton({500, 330}, 1, true) == InputVerdict::Drop);
    CHECK(lock.mouseButton({500, 330}, 1, false) == InputVerdict::Drop);
    CHECK(lock.takeRefused());
    CHECK(lock.wheel({500, 330}) == InputVerdict::Pass);
    CHECK(lock.mouseButton({500, 310}, 1, true) == InputVerdict::Pass);
    CHECK(lock.mouseButton({500, 310}, 1, false) == InputVerdict::Pass);

    // "*": every option, by the step's choice.
    s = step({"designs:create"}, true, {"designs:create:*"});
    const LockState any = makeLockState(s, pickerTags(), {"designs"}, {picker}, false, keys);
    CHECK(any.allows({500, 310}));
    CHECK(any.allows({500, 330}));
    // A step that names no option leaves the picker as it is: every option passes.
    s = step({"designs:create"}, true);
    const LockState none = makeLockState(s, pickerTags(), {"designs"}, {picker}, false, keys);
    CHECK(none.allows({500, 330}));
}

TEST_CASE("lesson lock: an open chooser's options from the data set are refused too") {
    const Bindings keys;
    std::vector<TaggedArea> tags = {
        {"window:create-design", box(100, 100, 900, 700)},
        {"create-design:type", box(200, 150, 400, 170)},
        {"create-design:type:attack-ship", box(400, 170, 600, 190), false, true},
        {"create-design:type:pop-transport", box(400, 190, 600, 210), false, true},
    };
    const learn::Step s = step({"create-design:type", "create-design:type:attack-ship"}, true);
    const LockState st = makeLockState(s, tags, {"create-design"}, {box(390, 160, 610, 220)}, false, keys);
    CHECK(st.allows({500, 180}));
    CHECK_FALSE(st.allows({500, 200}));
}

TEST_CASE("lesson lock: options inside an allowed list") {
    const Bindings keys;
    std::vector<TaggedArea> tags = {
        {"window:set-queue", box(100, 100, 900, 700)},
        {"set-queue:available", box(110, 150, 400, 600)},
        {"set-queue:available:up", box(380, 150, 400, 170)},
        {"set-queue:available:other", box(110, 170, 380, 190)},
        {"set-queue:available:named", box(110, 190, 380, 210)},
        {"set-queue:available:other", box(110, 210, 380, 230)},
    };
    const learn::Step s = step({"set-queue:available"}, true, {"set-queue:available:named"});
    const LockState st = makeLockState(s, tags, {"set-queue"}, {}, false, keys);
    CHECK(st.allows({200, 200}));                                  // the row the step names
    CHECK_FALSE(st.allows({200, 180}));                            // the others: refused
    CHECK_FALSE(st.allows({200, 220}));
    CHECK(st.looks({200, 220}));                                   // but they scroll
    CHECK(st.allows({390, 160}));                                  // the list's arrows
    CHECK(st.allows({200, 500}));                                  // below the rows: the list (nothing to click)
    REQUIRE(st.windows.size() == 1);
    CHECK(st.windows[0].choices.refused.size() == 2);
    CHECK(st.windows[0].choices.chosen.size() == 1);
}

TEST_CASE("lesson lock: what a step shows is clear of the spotlight, never clicked") {
    const Bindings keys;
    learn::Step s = step({"designs:list"}, false, {"designs:list"});
    s.show = {"designs:details"};
    const LockState st = makeLockState(s, pickerTags(), {"designs"}, {}, false, keys);
    CHECK(st.allows({200, 300}));                                  // the list (allowed)
    CHECK_FALSE(st.allows({500, 300}));                            // the details: shown, not clicked
    CHECK(st.looks({500, 300}));
    CHECK(st.lit({500, 300}));
    CHECK_FALSE(st.lit({790, 215}));                               // Create: dimmed
    CHECK_FALSE(st.allows({790, 215}));
    // A window named only by what a step shows is the step's: its other parts are locked.
    learn::Step only = step({"command:research"}, true);
    only.show = {"designs:details"};
    const LockState o = makeLockState(only, pickerTags(), {"designs"}, {}, false, keys);
    REQUIRE(o.windows.size() == 1);
    CHECK(o.windows[0].constrained);
    CHECK_FALSE(o.allows({790, 215}));
    CHECK(o.lit({500, 300}));
}

TEST_CASE("lesson lock: a window an earlier step left open can only be closed") {
    const Bindings keys;
    const learn::Step s = step({"command:research"}, true);
    std::vector<TaggedArea> tags = frameTags();
    tags.push_back({"log:close", box(110, 270, 290, 295)});
    // The Log opened during this step (a new turn): the player's, all of it.
    LockState st = makeLockState(s, tags, {"log"}, {}, false, keys);
    REQUIRE(st.windows.size() == 1);
    CHECK_FALSE(st.windows[0].constrained);
    CHECK(st.allows({200, 150}));
    CHECK(st.windowKeys);
    // Left open by an earlier step: only its Close button, and Esc and Enter.
    st = makeLockState(s, tags, {"log"}, {}, false, keys, {"log"});
    REQUIRE(st.windows.size() == 1);
    CHECK(st.windows[0].constrained);
    CHECK_FALSE(st.allows({200, 150}));
    CHECK(st.allows({200, 280}));
    CHECK_FALSE(st.windowKeys);
    CHECK(hasKey(st, KeyChord{ImGuiKey_Escape}));
    CHECK(hasKey(st, KeyChord{ImGuiKey_Enter}));
    // A window the step names is the step's, left open or not.
    const learn::Step logStep = step({"log:close"}, true, {"window:log"});
    st = makeLockState(logStep, tags, {"log"}, {}, false, keys, {"log"});
    CHECK(st.allows({200, 150}));
    // A Close button of a window that is not open asks for no way to open it.
    st = makeLockState(step({"log:close"}, true), tags, {}, {}, false, keys);
    CHECK_FALSE(st.allows({25, 25}));   // command:research is not the Log's opener anyway
    CHECK(std::none_of(st.keys.begin(), st.keys.end(), [&](const KeyChord& c) {
        return std::find(keys.chords(Action::Log).begin(), keys.chords(Action::Log).end(), c) != keys.chords(Action::Log).end();
    }));
}

// ---- The lesson audit (client/classic/lesson_audit.hpp) -----------------------------------------

#include "client/classic/lesson_audit.hpp"
#include "learn/markdown.hpp"

namespace {

std::vector<learn::Block> text(std::string_view markdown) { return learn::parseMarkdown(markdown, {}, false).blocks; }

bool hasLine(const AuditReport& r, std::string_view part) {
    return std::any_of(r.lines.begin(), r.lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

} // namespace

TEST_CASE("lesson audit: the things a step's text names") {
    const std::vector<std::string> refs = textReferences(
        text("**Press Create, and pick Ship** when the game asks. The **Design Detail** on the right shows its **Movement**. Press **Next**."));
    auto has = [&](std::string_view r) { return std::find(refs.begin(), refs.end(), r) != refs.end(); };
    CHECK(has("Create"));   // a long bold instruction gives its capitalised names
    CHECK(has("Ship"));
    CHECK(has("Design Detail"));
    CHECK(has("Movement"));
    CHECK(has("on the right"));
    // Keys are no names on the screen; a lower-case bold idea is none either.
    const std::vector<std::string> keys = textReferences(text("**Close the Log** (`Esc`). Each system becomes **explored**."));
    CHECK(std::find(keys.begin(), keys.end(), "Esc") == keys.end());
    CHECK(std::find(keys.begin(), keys.end(), "explored") == keys.end());
}

TEST_CASE("lesson audit: a chooser the step does not narrow, and what the text names under the spotlight") {
    const Bindings keys;
    // Create, with its picker open: every vehicle type passes.
    learn::Step s = step({"designs:create"}, true);
    s.text = text("**Press Create, and pick Ship.** The **Design Detail** shows the figures.");
    std::vector<TaggedArea> tags = pickerTags();
    tags.push_back({"designs:details", box(360, 150, 690, 650)});
    const LockArea picker = box(390, 290, 610, 400);
    AuditInput in;
    in.step = &s;
    in.text = s.text;
    in.tags = tags;
    in.openWindows = {"designs"};
    in.lock = makeLockState(s, tags, in.openWindows, {picker}, false, keys);
    in.spotlight = in.lock.grown(6);
    in.items = {{"Create", "designs", box(700, 200, 880, 230)},
                {"Ship", "designs", box(400, 300, 600, 320)},
                {"Base", "designs", box(400, 320, 600, 340)},
                {"text:Design Detail", "designs", box(370, 130, 470, 148)}};
    in.display = ImVec2(1280, 800);
    AuditReport r = auditStep(in);
    CHECK(hasLine(r, "A chooser designs:create: every option passes"));
    CHECK(hasLine(r, "B ref \"Design Detail\""));
    CHECK(r.flags >= 2);   // the chooser, and the details dimmed

    // Narrowed to Ship, and the details shown: nothing to flag.
    s.highlight.push_back("designs:create:ship");
    s.show = {"designs:details"};
    in.lock = makeLockState(s, tags, in.openWindows, {picker}, false, keys);
    in.spotlight = in.lock.grown(6);
    r = auditStep(in);
    CHECK(hasLine(r, "A chooser designs:create: only ship"));
    CHECK_FALSE(hasLine(r, "FLAG"));
    CHECK(r.flags == 0);
}
