// Input scripts (docs/BUILDING.md "Input scripts"): the format, the player
// against a probe of our own, and the SDL events it becomes.

#include "client/script/items.hpp"
#include "client/script/player.hpp"
#include "client/script/recorder.hpp"
#include "client/script/script.hpp"
#include "client/script/sdl_input.hpp"

#include "temp_dir.hpp"

#include <doctest/doctest.h>
#include <imgui.h>

#include <fstream>
#include <iterator>
#include <map>

ImGuiKey ImGui_ImplSDL3_KeyEventToImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);

using namespace opense4;
using namespace opense4::client::script;

namespace {

Script parse(std::string_view text) {
    std::vector<std::string> errors;
    auto s = parseScript(text, "t.txt", errors);
    const std::string first = errors.empty() ? std::string{} : errors.front();
    INFO(first);
    REQUIRE(s);
    return *s;
}

std::vector<std::string> problems(std::string_view text) {
    std::vector<std::string> errors;
    CHECK_FALSE(parseScript(text, "t.txt", errors));
    return errors;
}

// A screen of our own: frame pixels are ImGui units, tags and items as set.
class FakeProbe final : public Probe {
public:
    std::map<std::string, std::vector<Box>> tags;
    std::vector<Item> itemList;
    std::vector<std::string> windows;
    std::optional<LessonInfo> lessonInfo;
    std::optional<uint32_t> gameTurn;
    std::vector<std::string> log;
    bool textField = false;
    bool conditionHolds = false;

    std::vector<Box> tagBoxes(std::string_view name) const override {
        const auto it = tags.find(std::string(name));
        return it == tags.end() ? std::vector<Box>{} : it->second;
    }
    std::vector<std::string> tagNames() const override {
        std::vector<std::string> out;
        for (const auto& [name, boxes] : tags) out.push_back(name);
        return out;
    }
    const std::vector<Item>& items() const override { return itemList; }
    std::vector<Item> textList;
    const std::vector<Item>& texts() const override { return textList; }
    ImVec2 framePoint(float x, float y) const override { return ImVec2(x, y); }
    float frameScale() const override { return 1.0f; }
    std::vector<Box> sectors(const Target& t, std::string& error) const override {
        if (t.numeric) return {Box{ImVec2(t.x * 50, t.y * 50), ImVec2(t.x * 50 + 50, t.y * 50 + 50)}};
        error = "no such sector";
        return {};
    }
    std::vector<Box> systems(const Target&, std::string&) const override { return {}; }
    std::string targetAt(ImVec2) const override { return {}; }
    std::string screen() const override { return gameTurn ? "game" : "front"; }
    std::vector<std::string> openWindows() const override { return windows; }
    std::optional<LessonInfo> lesson() const override { return lessonInfo; }
    std::optional<uint32_t> turn() const override { return gameTurn; }
    std::vector<std::string> logLines() const override { return log; }
    bool typing() const override { return textField; }
    std::optional<bool> holds(const learn::Condition&, const learn::Mark&, std::string&) const override { return conditionHolds; }
    learn::Mark mark(bool) const override { return {}; }
    std::optional<int64_t> factValue(learn::Fact, const learn::Mark&) const override { return 4; }
};

Item item(std::string label, std::string scope, ImVec2 min, ImVec2 max, bool disabled = false) {
    return Item{std::move(label), std::move(scope), "Window", min, max, disabled};
}

// Plays frames until the player is done or `most` frames went by, passing every event.
std::vector<InputEvent> play(Player& p, const Probe& probe, int most = 400, Verdict verdict = Verdict::Pass) {
    std::vector<InputEvent> all;
    for (int i = 0; i < most && !p.finished() && !p.failed(); ++i) {
        FrameOutput out = p.tick(probe);
        std::vector<Verdict> v(out.events.size(), verdict);
        for (InputEvent& e : out.events) all.push_back(e);
        p.verdicts(v);
    }
    return all;
}

} // namespace

TEST_CASE("input script: the format") {
    const Script s = parse(R"(# a comment
options --tutorial=first-steps --layout=1024x768
timeout 50
click tag:lesson:next            # Next
double-click item:"Keep Playing" in=lesson nth=2
right-click window:research@10,-5 shift ctrl
click item:"a@b"@50%,25%
drag tag:a to tag:b frames=3
wheel sector:3,4 -2
key Ctrl+H
key Escape refused
type "My \"first\" design"
wait 10
wait-until timeout=20 { turns_passed = 1 }
assert { all = [{ colonies = 2 }, { not = { window = "research" } }] }
wait-step 3
assert-log "colonized"
screenshot shot.png
echo Hello there
print colonies turn
)");
    CHECK(s.options == std::vector<std::string>{"--tutorial=first-steps", "--layout=1024x768"});
    REQUIRE(s.steps.size() == 17);
    CHECK(s.steps[0].op == Op::Click);
    CHECK(s.steps[0].target.kind == TargetKind::Tag);
    CHECK(s.steps[0].target.name == "lesson:next");
    CHECK(s.steps[0].timeout == 50);
    CHECK(s.steps[0].source == "click tag:lesson:next");
    CHECK(s.steps[1].op == Op::DoubleClick);
    CHECK(s.steps[1].target.kind == TargetKind::Item);
    CHECK(s.steps[1].target.name == "Keep Playing");
    CHECK(s.steps[1].target.scope == "lesson");
    CHECK(s.steps[1].target.nth == 2);
    CHECK(s.steps[2].op == Op::RightClick);
    CHECK(s.steps[2].target.kind == TargetKind::Window);
    CHECK(s.steps[2].target.offset.set);
    CHECK(s.steps[2].target.offset.x == 10);
    CHECK(s.steps[2].target.offset.y == -5);
    CHECK(s.steps[2].shift);
    CHECK(s.steps[2].ctrl);
    CHECK_FALSE(s.steps[2].alt);
    // An @ inside quotes belongs to the label.
    CHECK(s.steps[3].target.name == "a@b");
    CHECK(s.steps[3].target.offset.xPercent);
    CHECK(s.steps[3].target.offset.x == 50);
    CHECK(s.steps[4].op == Op::Drag);
    CHECK(s.steps[4].to.name == "b");
    CHECK(s.steps[4].dragFrames == 3);
    CHECK(s.steps[5].op == Op::Wheel);
    CHECK(s.steps[5].target.numeric);
    CHECK(s.steps[5].number == -2);
    CHECK(s.steps[6].chord.key == ImGuiKey_H);
    CHECK(s.steps[6].chord.ctrl);
    CHECK(s.steps[7].refused);
    CHECK(s.steps[8].text == "My \"first\" design");
    CHECK(s.steps[9].number == 10);
    CHECK(s.steps[10].op == Op::WaitUntil);
    CHECK(s.steps[10].timeout == 20);
    REQUIRE(s.steps[10].condition);
    CHECK(learn::describe(*s.steps[10].condition) == "turns_passed = 1");
    CHECK(s.steps[11].op == Op::Assert);
    CHECK(s.steps[12].number == 3);
    CHECK(s.steps[13].text == "colonized");
    CHECK(s.steps[14].text == "shot.png");
    CHECK(s.steps[15].text == "Hello there");
    CHECK(s.steps[16].facts == std::vector<std::string>{"colonies", "turn"});
}

TEST_CASE("input script: problems name the file and line") {
    auto e = problems("click tag:a\njump tag:b\n");
    REQUIRE(e.size() == 1);
    CHECK(e[0] == "t.txt:2: unknown step 'jump'");
    e = problems("click button:x\n");
    CHECK(e[0].starts_with("t.txt:1: unknown target kind 'button:'"));
    e = problems("type \"open\n");
    CHECK(e[0] == "t.txt:1: a quote is not closed");
    e = problems("wait-until { colonies = 0 }\n");
    CHECK(e[0].starts_with("t.txt:1: 'colonies = 0' always holds"));
    e = problems("assert { planets = 3 }\n");
    CHECK(e[0] == "t.txt:1: unknown condition key 'planets'");
    e = problems("click tag:a\noptions --seed=1\n");
    CHECK(e[0] == "t.txt:2: 'options' come before the first step");
    e = problems("key Hyper+Q\n");
    CHECK(e[0].starts_with("t.txt:1: unknown key 'Hyper+Q'"));
    e = problems("click tag:a@x,y\n");
    CHECK(e[0].find("an offset is @x,y") != std::string::npos);
    e = problems("click item:Next in=lesson@5,5\n");
    CHECK(e[0].find("an offset goes right after the target") != std::string::npos);
    e = problems("drag tag:a tag:b\n");
    CHECK(e[0].find("'to'") != std::string::npos);
    e = problems("# nothing\n");
    CHECK(e[0] == "t.txt: no steps");
}

TEST_CASE("input script: quoting and labels") {
    CHECK(quoteWord("Next") == "Next");
    CHECK(quoteWord("Keep Playing") == "\"Keep Playing\"");
    CHECK(quoteWord("a\"b") == "\"a\\\"b\"");
    CHECK(visibleLabel("Name##col") == "Name");
    CHECK(visibleLabel("##up").empty());
    CHECK(labelMatches("Next##next", "Next"));
    CHECK(labelMatches("Next##next", "##next"));
    CHECK(labelMatches("Title###id", "##id"));
    CHECK(labelMatches("##up", "##up"));
    CHECK_FALSE(labelMatches("##up", "up"));
    CHECK_FALSE(labelMatches("Next", "Nex"));
    CHECK(labelMatches("Keep Playing##x", "Keep*"));
    CHECK(labelMatches("Keep Playing", "*"));
    CHECK(labelMatches("Keep Playing", "*Play?ng"));
    CHECK_FALSE(labelMatches("##up", "*"));
    CHECK_FALSE(labelMatches("Keep Playing", "*Plays"));
    CHECK(labelMatches("window:Design Report###designreport", "window:Design Report"));
    CHECK(labelMatches("window:##itemreport", "window:##itemreport"));
    CHECK_FALSE(labelMatches("window:Research", "*"));
    CHECK(labelMatches("window:Research", "window:*"));
}

TEST_CASE("input script: a click is a move, a press and a release") {
    FakeProbe probe;
    probe.tags["lesson:next"] = {Box{ImVec2(100, 200), ImVec2(140, 220)}};
    Player p(parse("click tag:lesson:next\n"), "/tmp");
    const auto events = play(p, probe);
    CHECK(p.finished());
    CHECK_FALSE(p.failed());
    REQUIRE(events.size() == 3);
    CHECK(events[0].kind == InputEvent::Kind::Motion);
    CHECK(events[0].pos.x == 120);
    CHECK(events[0].pos.y == 210);
    CHECK(events[1].kind == InputEvent::Kind::ButtonDown);
    CHECK(events[1].decisive);
    CHECK(events[2].kind == InputEvent::Kind::ButtonUp);
    CHECK(events[2].button == 1);
}

TEST_CASE("input script: a target that moves before the press is aimed at again") {
    FakeProbe probe;
    probe.tags["link"] = {Box{ImVec2(100, 100), ImVec2(120, 110)}};
    Player p(parse("click tag:link\n"), "/tmp");
    std::vector<InputEvent> events;
    for (int i = 0; i < 50 && !p.finished(); ++i) {
        FrameOutput out = p.tick(probe);
        for (const InputEvent& e : out.events) events.push_back(e);
        p.verdicts(std::vector<Verdict>(out.events.size(), Verdict::Pass));
        if (i == 0) probe.tags["link"] = {Box{ImVec2(300, 50), ImVec2(320, 60)}};   // the layout settled
    }
    REQUIRE(p.finished());
    REQUIRE(events.size() == 4);
    CHECK(events[0].kind == InputEvent::Kind::Motion);
    CHECK(events[0].pos.x == 110);
    CHECK(events[1].kind == InputEvent::Kind::Motion);
    CHECK(events[1].pos.x == 310);
    CHECK(events[2].kind == InputEvent::Kind::ButtonDown);
    CHECK(events[2].pos.x == 310);
    CHECK(events[3].pos.y == 55);
}

TEST_CASE("input script: offsets, items, scopes and the n-th match") {
    FakeProbe probe;
    probe.tags["window:research"] = {Box{ImVec2(100, 100), ImVec2(300, 200)}};
    probe.itemList = {item("Back", "manual", ImVec2(0, 0), ImVec2(10, 10)), item("Back", "lesson", ImVec2(20, 0), ImVec2(30, 10)),
                      item("Back##2", "lesson", ImVec2(40, 0), ImVec2(50, 10))};
    Player p(parse("right-click window:research@10,-20\nclick item:Back in=lesson nth=2\nmove tag:window:research@50%,10%\n"), "/tmp");
    const auto events = play(p, probe);
    REQUIRE(p.finished());
    REQUIRE(events.size() == 7);
    CHECK(events[0].pos.x == 110);
    CHECK(events[0].pos.y == 180);
    CHECK(events[1].button == 3);
    CHECK(events[3].pos.x == 45);   // the second Back of the lesson scope
    CHECK(events[6].pos.x == 200);
    CHECK(events[6].pos.y == 110);
}

TEST_CASE("input script: widgets inside a UI tag") {
    FakeProbe probe;
    probe.tags["cycle:ship"] = {Box{ImVec2(0, 0), ImVec2(60, 20)}};
    probe.tags["cycle:fleet"] = {Box{ImVec2(0, 20), ImVec2(60, 40)}};
    probe.itemList = {item("prev", "main", ImVec2(0, 0), ImVec2(20, 20)), item("next", "main", ImVec2(40, 0), ImVec2(60, 20)),
                      item("prev", "main", ImVec2(0, 20), ImVec2(20, 40)), item("next", "main", ImVec2(40, 20), ImVec2(60, 40))};
    Player p(parse("click item:next in=tag:cycle:fleet\n"), "/tmp");
    const auto events = play(p, probe);
    REQUIRE(p.finished());
    CHECK(events[0].pos.x == 50);
    CHECK(events[0].pos.y == 30);
}

TEST_CASE("input script: a press the input lock refuses fails the step") {
    FakeProbe probe;
    probe.tags["command:help"] = {Box{ImVec2(0, 0), ImVec2(10, 10)}};
    probe.lessonInfo = LessonInfo{"first-steps", true, 3, 3, 15, "none", true};
    Player p(parse("click tag:command:help\n"), "/tmp/out");
    play(p, probe, 400, Verdict::Drop);
    REQUIRE(p.failed());
    CHECK(p.failure().find("t.txt:1: click tag:command:help") == 0);
    CHECK(p.failure().find("the tutorial input lock refused this") != std::string::npos);
    CHECK(p.failure().find("tutorial 'first-steps' at step 3 of 15 (input locked)") != std::string::npos);
    CHECK(p.failureShot() == std::filesystem::path("/tmp/out/t-failure.png"));

    // ... and one it should refuse but lets through fails too.
    Player q(parse("click tag:command:help refused\n"), "/tmp/out");
    play(q, probe, 400, Verdict::Pass);
    REQUIRE(q.failed());
    CHECK(q.failure().find("expected it to be refused") != std::string::npos);
    Player r(parse("click tag:command:help refused\n"), "/tmp/out");
    play(r, probe, 400, Verdict::Drop);
    CHECK(r.finished());
}

TEST_CASE("input script: waits time out with the reason") {
    FakeProbe probe;
    probe.lessonInfo = LessonInfo{"first-steps", true, 2, 2, 15, "none", true};
    Player p(parse("wait-step 4 timeout=5\n"), "/tmp");
    play(p, probe);
    REQUIRE(p.failed());
    CHECK(p.failure().find("timed out after 5 frames: the lesson is at step 2") != std::string::npos);

    Player q(parse("click tag:missing timeout=3\n"), "/tmp");
    probe.tags["missing-not"] = {Box{ImVec2(0, 0), ImVec2(1, 1)}};
    play(q, probe);
    REQUIRE(q.failed());
    CHECK(q.failure().find("no UI tag 'missing' on screen") != std::string::npos);

    // A dim item is waited for, then fails.
    probe.itemList = {item("Start Lesson", "front", ImVec2(0, 0), ImVec2(10, 10), true)};
    Player r(parse("click item:\"Start Lesson\" timeout=4\n"), "/tmp");
    play(r, probe);
    REQUIRE(r.failed());
    CHECK(r.failure().find("is dim (disabled)") != std::string::npos);
}

TEST_CASE("input script: an optional click is skipped when its target never comes") {
    FakeProbe probe;
    const Script s = parse("click tag:log:close optional\nclick tag:x optional timeout=30\n");
    CHECK(s.steps[0].optional);
    CHECK(s.steps[0].timeout == kOptionalTimeout);
    CHECK(s.steps[1].timeout == 30);
    Player p(s, "/tmp");
    const auto events = play(p, probe);
    CHECK(p.finished());
    CHECK(events.empty());
    CHECK(p.frame() >= 40);
}

TEST_CASE("input script: the wheel turns a notch a frame") {
    FakeProbe probe;
    Player p(parse("wheel sector:1,1 -3\n"), "/tmp");
    std::vector<uint64_t> frames;
    for (int i = 0; i < 100 && !p.finished(); ++i) {
        FrameOutput out = p.tick(probe);
        for (const InputEvent& e : out.events) {
            if (e.kind != InputEvent::Kind::Wheel) continue;
            CHECK(e.wheel == -1.0f);
            CHECK(e.pos.x == 75);
            frames.push_back(p.frame());
        }
        p.verdicts(std::vector<Verdict>(out.events.size(), Verdict::Pass));
    }
    REQUIRE(frames.size() == 3);
    CHECK(frames[1] == frames[0] + 1);
    CHECK(frames[2] == frames[1] + 1);
}

TEST_CASE("input script: where things are, and drags with other buttons") {
    FakeProbe probe;
    probe.tags["panel"] = {Box{ImVec2(0, 0), ImVec2(100, 100)}};
    probe.tags["lesson"] = {Box{ImVec2(20, 20), ImVec2(60, 60)}};
    Player p(parse("assert-inside tag:lesson tag:panel\nassert-inside tag:panel@-1,-1 tag:panel\n"), "/tmp");
    play(p, probe);
    CHECK(p.finished());
    Player q(parse("assert-inside tag:panel@99,99 tag:lesson\n"), "/tmp");
    play(q, probe);
    REQUIRE(q.failed());
    CHECK(q.failure().find("tag:panel@99,99 is not inside tag:lesson") != std::string::npos);
    Player d(parse("drag tag:lesson to tag:panel middle frames=2\n"), "/tmp");
    const auto events = play(d, probe);
    REQUIRE(d.finished());
    REQUIRE(events.size() == 5);
    CHECK(events[1].kind == InputEvent::Kind::ButtonDown);
    CHECK(events[1].button == 2);
    CHECK(events[4].kind == InputEvent::Kind::ButtonUp);
    CHECK(events[4].pos.x == 50);
}

TEST_CASE("input script: assert-fits") {
    FakeProbe probe;
    Item ok = item("Close", "designs", ImVec2(0, 0), ImVec2(50, 20));
    Item slack = item("Row", "designs", ImVec2(0, 20), ImVec2(50, 40));
    slack.clipped = true;
    slack.hidden = 3;   // a selectable's half item spacing past the edge
    Item scrolled = item("Far row", "designs", ImVec2(0, 40), ImVec2(50, 60));
    scrolled.clipped = true;
    scrolled.hidden = 30;
    scrolled.scrolls = true;
    probe.itemList = {ok, slack, scrolled};
    Item caption = item("Ship Designs", "designs", ImVec2(0, 0), ImVec2(40, 10));
    probe.textList = {caption};
    Player p(parse("assert-fits designs\n"), "/tmp");
    play(p, probe);
    CHECK(p.finished());

    Item cut = item("Make Current", "designs", ImVec2(0, 60), ImVec2(50, 80));
    cut.clipped = true;
    cut.hidden = 12;
    probe.itemList.push_back(cut);
    Player q(parse("assert-fits designs\n"), "/tmp");
    play(q, probe);
    REQUIRE(q.failed());
    CHECK(q.failure().find("item:\"Make Current\" is cut off by its window") != std::string::npos);
    // Another scope's widgets are not its business.
    Player r(parse("assert-fits planets\n"), "/tmp");
    play(r, probe);
    CHECK(r.finished());

    probe.itemList.pop_back();
    caption.overflow = true;
    probe.textList = {caption};
    Player t(parse("assert-fits designs\n"), "/tmp");
    play(t, probe);
    REQUIRE(t.failed());
    CHECK(t.failure().find("text \"Ship Designs\" runs out of its box") != std::string::npos);
    CHECK(problems("assert-fits\n").size() == 1);
}

TEST_CASE("input script: loops") {
    FakeProbe probe;
    probe.tags["a"] = {Box{ImVec2(0, 0), ImVec2(10, 10)}};
    // A plain repeat runs its body N times.
    Player p(parse("repeat 3\n  move tag:a\nend\necho done\n"), "/tmp");
    const auto events = play(p, probe);
    CHECK(p.finished());
    CHECK(events.size() == 3);
    // An until-condition leaves at once when it holds...
    probe.conditionHolds = true;
    Player q(parse("repeat 5 until { turns_passed = 1 }\n  move tag:a\nend\n"), "/tmp");
    CHECK(play(q, probe).empty());
    CHECK(q.finished());
    // ... and fails when it never does.
    probe.conditionHolds = false;
    Player r(parse("repeat 2 until { turns_passed = 1 }\n  move tag:a\nend\n"), "/tmp");
    CHECK(play(r, probe).size() == 2);
    REQUIRE(r.failed());
    CHECK(r.failure().find("still does not hold after 2 passes") != std::string::npos);
    // Nested loops.
    Player n(parse("repeat 2\n  repeat 3\n    move tag:a\n  end\nend\n"), "/tmp");
    CHECK(play(n, probe).size() == 6);
    CHECK(problems("repeat 2\nmove tag:a\n")[0] == "t.txt:1: this 'repeat' has no 'end'");
    CHECK(problems("end\n")[0] == "t.txt:1: 'end' without a 'repeat'");
    CHECK(problems("repeat 2 while { turn = 1 }\nend\n")[0].find("until") != std::string::npos);
}

TEST_CASE("input script: waits and checks pass when they hold") {
    FakeProbe probe;
    probe.gameTurn = 3;
    probe.windows = {"research"};
    probe.lessonInfo = LessonInfo{"combat", true, 5, 5, 11, "none", true};
    probe.log = {"Colony founded: a new colony on a world"};
    probe.conditionHolds = true;
    Player p(parse("wait 3\nwait-window research\nassert-window research\nassert-no-window log\nwait-turn 2\nassert-turn 3\n"
                   "assert-step 5\nassert-log \"colony FOUNDED\"\nassert-no-log Victory\nassert { colonies = 1 }\n"
                   "wait-until { turns_passed = 1 }\nassert-screen game\nassert-lesson combat\nassert-result none\n"),
             "/tmp");
    play(p, probe);
    CHECK(p.finished());
    CHECK_FALSE(p.failed());
    CHECK(p.frame() >= 4);
}

TEST_CASE("input script: keys with modifiers, and typing waits for a text field") {
    FakeProbe probe;
    Player p(parse("key Ctrl+Shift+F1\n"), "/tmp");
    const auto events = play(p, probe);
    REQUIRE(p.finished());
    REQUIRE(events.size() == 6);
    CHECK(events[0].key == ImGuiKey_LeftCtrl);
    CHECK(events[0].ctrl);
    CHECK(events[1].key == ImGuiKey_LeftShift);
    CHECK(events[2].kind == InputEvent::Kind::KeyDown);
    CHECK(events[2].key == ImGuiKey_F1);
    CHECK(events[2].ctrl);
    CHECK(events[2].shift);
    CHECK(events[2].decisive);
    CHECK(events[3].kind == InputEvent::Kind::KeyUp);
    CHECK(events[4].key == ImGuiKey_LeftShift);
    CHECK_FALSE(events[4].shift);
    CHECK(events[5].key == ImGuiKey_LeftCtrl);
    CHECK_FALSE(events[5].ctrl);

    Player q(parse("type \"Hé!\" timeout=5\n"), "/tmp");
    play(q, probe);
    CHECK(q.failed());
    probe.textField = true;
    Player r(parse("type \"Hé!\"\n"), "/tmp");
    const auto typed = play(r, probe);
    REQUIRE(r.finished());
    REQUIRE(typed.size() == 3);
    CHECK(typed[1].text == "é");
}

TEST_CASE("input script: clicks at one place are kept apart from a double click") {
    FakeProbe probe;
    probe.tags["lesson:next"] = {Box{ImVec2(0, 0), ImVec2(10, 10)}};
    Player p(parse("click tag:lesson:next\nclick tag:lesson:next\ndouble-click tag:lesson:next\n"), "/tmp");
    std::vector<uint64_t> presses;
    for (int i = 0; i < 400 && !p.finished(); ++i) {
        FrameOutput out = p.tick(probe);
        for (const InputEvent& e : out.events)
            if (e.kind == InputEvent::Kind::ButtonDown) presses.push_back(p.frame());
        p.verdicts(std::vector<Verdict>(out.events.size(), Verdict::Pass));
    }
    REQUIRE(presses.size() == 4);
    CHECK(presses[1] - presses[0] >= 24);
    CHECK(presses[2] - presses[1] >= 24);
    CHECK(presses[3] - presses[2] == 2);   // the double click's own presses are close
}

TEST_CASE("input script: SDL events as a keyboard and mouse send them") {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const auto key = static_cast<ImGuiKey>(k);
        const auto sdl = sdlKey(key);
        if (!sdl) continue;
        // The backend reads them back as the same key.
        CHECK(ImGui_ImplSDL3_KeyEventToImGuiKey(sdl->keycode, sdl->scancode) == key);
    }
    for (const ImGuiKey key : {ImGuiKey_A, ImGuiKey_Z, ImGuiKey_0, ImGuiKey_9, ImGuiKey_F1, ImGuiKey_F12, ImGuiKey_Escape, ImGuiKey_Enter,
                               ImGuiKey_Space, ImGuiKey_LeftArrow, ImGuiKey_Comma, ImGuiKey_LeftCtrl})
        CHECK(sdlKey(key).has_value());

    InputEvent e;
    e.kind = InputEvent::Kind::KeyDown;
    e.key = ImGuiKey_H;
    e.ctrl = true;
    SDL_Event ev = toSdlEvent(e, 7);
    CHECK(ev.type == SDL_EVENT_KEY_DOWN);
    CHECK(ev.key.windowID == 7);
    CHECK(ev.key.down);
    CHECK((ev.key.mod & SDL_KMOD_CTRL) != 0);
    CHECK(ev.key.key == SDLK_H);

    e = InputEvent{};
    e.kind = InputEvent::Kind::ButtonDown;
    e.button = 3;
    e.pos = ImVec2(12.5f, 40);
    ev = toSdlEvent(e, 7);
    CHECK(ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    CHECK(ev.button.button == SDL_BUTTON_RIGHT);
    CHECK(ev.button.x == 12.5f);
    CHECK(ev.button.which != SDL_TOUCH_MOUSEID);

    e = InputEvent{};
    e.kind = InputEvent::Kind::Text;
    e.text = "x";
    ev = toSdlEvent(e, 7);
    CHECK(ev.type == SDL_EVENT_TEXT_INPUT);
    CHECK(std::string(ev.text.text) == "x");

    CHECK(isUserInput(ev));
    SDL_Event quit;
    SDL_zero(quit);
    quit.type = SDL_EVENT_QUIT;
    CHECK_FALSE(isUserInput(quit));
}

namespace {

SDL_Event mouse(Uint32 type, float x, float y, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.button.x = x;
        e.button.y = y;
        e.button.button = button;
    }
    return e;
}

} // namespace

TEST_CASE("input script: the recorder names what was clicked and writes a script that parses") {
    FakeProbe probe;
    probe.tags["panel:report"] = {Box{ImVec2(600, 100), ImVec2(900, 400)}};
    probe.tags["lesson:next"] = {Box{ImVec2(700, 500), ImVec2(760, 520)}};
    probe.tags["window:research"] = {Box{ImVec2(100, 100), ImVec2(500, 400)}};
    probe.itemList = {item("Keep Playing", "lesson", ImVec2(300, 600), ImVec2(400, 620)), item("Close", "research", ImVec2(400, 350), ImVec2(480, 370)),
                      item("Close", "help", ImVec2(10, 10), ImVec2(20, 20)), item("Back", "main", ImVec2(150, 150), ImVec2(200, 170))};
    // The smallest name around the point: a widget, a tag (with an offset when the point is not
    // at its middle), else a point of the frame.
    CHECK(Recorder::targetFor(ImVec2(350, 610), probe) == "item:\"Keep Playing\"");
    CHECK(Recorder::targetFor(ImVec2(730, 510), probe) == "tag:lesson:next");
    CHECK(Recorder::targetFor(ImVec2(700, 200), probe) == "tag:panel:report@100,100");
    CHECK(Recorder::targetFor(ImVec2(440, 360), probe) == "item:Close in=research");
    CHECK(Recorder::targetFor(ImVec2(950, 700), probe) == "at:950,700");
    // A window in front hides what lies behind it: the main window's Back is not named there.
    probe.windows = {"research"};
    CHECK(Recorder::targetFor(ImVec2(175, 160), probe) == "window:research@75,60");

    test::TempDir dir("recorder");
    Recorder rec(dir.path() / "rec.script", {"--tutorial=first-steps"});
    double t = 1.0;
    auto send = [&](const SDL_Event& e) {
        rec.event(e, probe, t);
        t += 0.05;
    };
    // A click, the same again at once (a double click), a pause, a right-click, a drag.
    for (int i = 0; i < 2; ++i) {
        send(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, 730, 510));
        send(mouse(SDL_EVENT_MOUSE_BUTTON_UP, 730, 510));
    }
    t += 2.0;
    send(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, 350, 610, SDL_BUTTON_RIGHT));
    send(mouse(SDL_EVENT_MOUSE_BUTTON_UP, 350, 610, SDL_BUTTON_RIGHT));
    send(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, 730, 510));
    send(mouse(SDL_EVENT_MOUSE_MOTION, 600, 400));
    send(mouse(SDL_EVENT_MOUSE_BUTTON_UP, 350, 610));
    // The wheel, twice in a row the same way; a key; typing into a text field.
    SDL_Event w;
    SDL_zero(w);
    w.type = SDL_EVENT_MOUSE_WHEEL;
    w.wheel.y = -1;
    w.wheel.mouse_x = 730;
    w.wheel.mouse_y = 510;
    send(w);
    send(w);
    SDL_Event k;
    SDL_zero(k);
    k.type = SDL_EVENT_KEY_DOWN;
    k.key.key = SDLK_H;
    k.key.scancode = SDL_SCANCODE_H;
    k.key.mod = SDL_KMOD_LCTRL;
    send(k);
    probe.textField = true;
    SDL_Event txt;
    SDL_zero(txt);
    txt.type = SDL_EVENT_TEXT_INPUT;
    txt.text.text = "Hi there";
    send(txt);
    std::string error;
    REQUIRE(rec.save(error));

    std::ifstream in(dir.path() / "rec.script");
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    CHECK(text.find("options --tutorial=first-steps\n") != std::string::npos);
    CHECK(text.find("double-click tag:lesson:next\n") != std::string::npos);
    CHECK(text.find("wait 126\n") != std::string::npos);   // the right-click came 2.1 s after the double click
    CHECK(text.find("right-click item:\"Keep Playing\"\n") != std::string::npos);
    CHECK(text.find("drag tag:lesson:next to item:\"Keep Playing\"\n") != std::string::npos);
    CHECK(text.find("wheel tag:lesson:next -2\n") != std::string::npos);
    CHECK(text.find("key Ctrl+H\n") != std::string::npos);
    CHECK(text.find("type \"Hi there\"\n") != std::string::npos);
    std::vector<std::string> errors;
    CHECK(parseScript(text, "rec.script", errors).has_value());
}
