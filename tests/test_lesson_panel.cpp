// The lesson panel's layout (client/classic/lesson_panel.hpp), its keys and
// the input lock, and resuming a tutorial (learn/resume.hpp, the client's
// settings): docs/LEARNING.md "The lesson panel", "Resuming a lesson".

#include "client/classic/lesson_lock.hpp"
#include "client/classic/lesson_panel.hpp"
#include "client/classic/settings.hpp"
#include "learn/resume.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace opense4;
using namespace opense4::client;
using namespace opense4::client::classic;

namespace {

panel::Box box(float x0, float y0, float x1, float y1) { return {ImVec2(x0, y0), ImVec2(x1, y1)}; }

bool hasKey(const LockState& s, KeyChord c) { return std::find(s.keys.begin(), s.keys.end(), c) != s.keys.end(); }

learn::Step step(std::vector<std::string> highlight, std::vector<std::string> allow = {}) {
    learn::Step s;
    s.title = "A step";
    s.highlight = std::move(highlight);
    s.allow = std::move(allow);
    return s;
}

learn::Condition windowOpen(std::string id) {
    learn::Condition c;
    c.op = learn::Condition::Op::Fact;
    c.fact = learn::Fact::Window;
    c.text = std::move(id);
    return c;
}

// A tutorial like "Designing ships": the main window, then Designs, then the designer.
learn::Lesson designLesson() {
    learn::Lesson l;
    l.slug = "designing";
    l.title = "Designing";
    l.steps.push_back(step({"lesson:panel"}));                                     // 0: explains
    l.steps.push_back(step({"command:designs"}));                                  // 1: opens Designs
    l.steps.back().done = windowOpen("designs");
    l.steps.push_back(step({"designs:list"}));                                     // 2: in Designs
    l.steps.push_back(step({"designs:create"}));                                   // 3: opens the designer
    l.steps.back().done = windowOpen("create-design");
    l.steps.push_back(step({"create-design:hull"}));                               // 4: in the designer
    l.steps.push_back(step({"create-design:warnings"}, {"create-design:components"}));   // 5
    l.steps.push_back(step({"designs:close"}, {"designs:list"}));                  // 6: back in Designs
    l.steps.push_back(step({"cycle:colony", "order:build-queue"}));                // 7: the main window
    return l;
}

} // namespace

TEST_CASE("lesson panel: buttons flow into rows and never overlap") {
    // Three labels that fit: equal widths over the whole row.
    const std::vector<float> three{60, 60, 90};
    auto slots = panel::flowButtons(three, 300, 4);
    REQUIRE(slots.size() == 3);
    CHECK(panel::rowCount(slots) == 1);
    CHECK(slots[0].w == doctest::Approx(slots[2].w));
    CHECK(slots[2].x + slots[2].w <= 300.0f);
    // At a large text size the labels no longer fit in one row: two rows, each
    // button at least as wide as its label, none overlapping.
    const std::vector<float> wide{110, 110, 160};
    slots = panel::flowButtons(wide, 300, 4);
    CHECK(panel::rowCount(slots) == 2);
    for (size_t i = 0; i < slots.size(); ++i) {
        CHECK(slots[i].w >= wide[i]);
        CHECK(slots[i].x + slots[i].w <= 300.0f);
        for (size_t j = i + 1; j < slots.size(); ++j)
            if (slots[i].row == slots[j].row) CHECK(slots[i].x + slots[i].w <= slots[j].x);
    }
    // Labels of different widths that fit, but not in equal shares: each grows by its share.
    slots = panel::flowButtons(std::vector<float>{50, 50, 180}, 300, 4);
    CHECK(panel::rowCount(slots) == 1);
    CHECK(slots[2].w >= 180.0f);
    CHECK(slots[2].x + slots[2].w <= 300.0f);
    // One label wider than the row gets the row to itself.
    slots = panel::flowButtons(std::vector<float>{60, 400, 60}, 300, 4);
    CHECK(panel::rowCount(slots) == 3);
    CHECK(slots[1].w == doctest::Approx(300.0f));
}

TEST_CASE("lesson panel: its place hides prompts as little as the step's targets") {
    const std::vector<panel::Spot> spots{{3, box(0, 300, 330, 580)}, {8, box(470, 300, 800, 580)}};
    panel::Avoid avoid;
    // Nothing to avoid: the first place.
    CHECK(panel::bestSpot(spots, avoid, std::nullopt) == 0);
    // A prompt over the first place counts as much as a target there.
    avoid.prompts.push_back(box(100, 400, 300, 500));
    CHECK(panel::bestSpot(spots, avoid, std::nullopt) == 1);
    avoid.prompts.clear();
    avoid.targets.push_back(box(100, 400, 300, 500));
    CHECK(panel::bestSpot(spots, avoid, std::nullopt) == 1);
    // An allowed part counts half: a whole target outweighs it.
    avoid.targets = {box(500, 400, 600, 450)};
    avoid.allowed = {box(100, 400, 300, 500)};
    CHECK(panel::bestSpot(spots, avoid, std::nullopt) == 0);
    // The place taken last frame stays while it is as good.
    avoid = {};
    CHECK(panel::bestSpot(spots, avoid, 8) == 1);
    // But not when it hides more.
    avoid.targets.push_back(box(500, 400, 600, 450));
    CHECK(panel::bestSpot(spots, avoid, 8) == 0);
    // Places are kept on the screen.
    const panel::Box kept = panel::keepInside(box(700, 500, 1030, 780), box(0, 0, 800, 600));
    CHECK(kept.max.x == doctest::Approx(800.0f));
    CHECK(kept.max.y == doctest::Approx(600.0f));
    CHECK(kept.width() == doctest::Approx(330.0f));
}

TEST_CASE("lesson panel: its keys and Shift+F1 pass the tutorial lock") {
    const Bindings keys;
    // Defaults: Alt and the button's letter, used by nothing else.
    CHECK(keys.chords(Action::LessonNext)[0] == KeyChord{ImGuiKey_N, false, false, true});
    CHECK(keys.chords(Action::LessonBack)[0] == KeyChord{ImGuiKey_B, false, false, true});
    CHECK(keys.chords(Action::LessonSkip)[0] == KeyChord{ImGuiKey_K, false, false, true});
    CHECK(keys.chords(Action::LessonReadMore)[0] == KeyChord{ImGuiKey_R, false, false, true});
    for (const Action a : {Action::LessonNext, Action::LessonBack, Action::LessonSkip, Action::LessonReadMore})
        CHECK_FALSE(keys.boundTo(keys.chords(a)[0], a).has_value());
    CHECK(actionInfo(Action::LessonNext).group == std::string("Lesson panel"));

    learn::Step st = step({"command:research"});
    st.done = windowOpen("research");
    const LockState lock = makeLockState(st, {{"command:research", {ImVec2(10, 10), ImVec2(40, 40)}}}, {}, {}, false, keys);
    InputLock input;
    input.set(lock);
    for (const Action a : {Action::LessonNext, Action::LessonBack, Action::LessonSkip, Action::LessonReadMore, Action::ContextHelp,
                           Action::LessonText}) {
        CHECK(hasKey(lock, keys.chords(a)[0]));
        CHECK(input.key(keys.chords(a)[0], true) == InputVerdict::Pass);
    }
    // Shift+F1 is the manual page of the window in front, always.
    CHECK(input.key(KeyChord{ImGuiKey_F1, false, true}, true) == InputVerdict::Pass);
    // Plain F1 (Help) is still the step's to allow.
    CHECK(input.key(KeyChord{ImGuiKey_F1}, true) == InputVerdict::Drop);
}

TEST_CASE("resume: a step in a window goes back to where the work in it began") {
    const learn::Lesson l = designLesson();
    CHECK(learn::stepWindows(l.steps[5]) == std::vector<std::string_view>{"create-design"});
    CHECK(learn::stepWindows(l.steps[7]).empty());
    // Main-window steps resume as they are.
    CHECK(learn::resumeStep(l, 0) == 0);
    CHECK(learn::resumeStep(l, 1) == 1);
    CHECK(learn::resumeStep(l, 7) == 7);
    // In the designer: back through Designs to the step that opened it.
    CHECK(learn::resumeStep(l, 5) == 1);
    CHECK(learn::resumeStep(l, 4) == 1);
    CHECK(learn::resumeStep(l, 2) == 1);
    // Designs again once the designer's work is done (and saved with the
    // game): a run of its own, which a closed window does not hold up.
    CHECK(learn::resumeStep(l, 6) == 6);
    // Past the end: the last step's.
    CHECK(learn::resumeStep(l, 99) == 7);
}

TEST_CASE("resume: the fingerprint follows the steps, not their wording") {
    const learn::Lesson l = designLesson();
    learn::Lesson reworded = l;
    reworded.steps[3].title = "Press Create";
    reworded.title = "Ship design";
    CHECK(learn::lessonFingerprint(reworded) == learn::lessonFingerprint(l));
    learn::Lesson moreSteps = l;
    moreSteps.steps.push_back(step({"command:help"}));
    CHECK(learn::lessonFingerprint(moreSteps) != learn::lessonFingerprint(l));
    learn::Lesson otherTags = l;
    otherTags.steps[2].highlight = {"designs:create"};
    CHECK(learn::lessonFingerprint(otherTags) != learn::lessonFingerprint(l));
    learn::Lesson otherCondition = l;
    otherCondition.steps[1].done = windowOpen("planets");
    CHECK(learn::lessonFingerprint(otherCondition) != learn::lessonFingerprint(l));
    learn::Lesson reordered = l;
    std::swap(reordered.steps[1], reordered.steps[2]);
    CHECK(learn::lessonFingerprint(reordered) != learn::lessonFingerprint(l));
}

TEST_CASE("resume: places and the first-lesson flag are kept with the client settings") {
    ClassicSettings s;
    s.learnStarted = true;
    s.learnResume = {{"tutorial:first-steps", 6, 4, "00ff00ff00ff00ff"}, {"tutorial:combat", 2, 1, "0123456789abcdef"}};
    const std::string text = settingsToToml(s);
    // Step numbers from 1 in the file, as the panel shows them.
    CHECK(text.find("left_at = 7") != std::string::npos);
    CHECK(text.find("resume_at = 5") != std::string::npos);
    const ClassicSettings back = settingsFromToml(text);
    CHECK(back.learnStarted);
    CHECK(back.learnResume == s.learnResume);
    const ClassicSettings empty = settingsFromToml("");
    CHECK_FALSE(empty.learnStarted);
    CHECK(empty.learnResume.empty());
    // Records that make no sense are dropped.
    const ClassicSettings odd = settingsFromToml("[[learn.resume]]\nlesson = \"tutorial:x\"\nleft_at = 2\nresume_at = 5\n"
                                                 "[[learn.resume]]\nleft_at = 2\nresume_at = 1\n");
    CHECK(odd.learnResume.empty());
}
