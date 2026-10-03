#include "client/classic/lesson_runner.hpp"

#include "client/audio.hpp"
#include "client/classic/learn_content.hpp"
#include "client/classic/screens/markdown_view.hpp"
#include "client/app_settings.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "core/hash.hpp"

#include <imgui_internal.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace opense4::client::classic {

namespace {

// The panel: a classic window, movable, in frame pixels at text size 1. It
// grows with the Text size setting and with its text (docs/LEARNING.md "The
// lesson panel"): as wide as kPanelW times the text size, at most
// kMaxWidthShare of the screen; as tall as its text needs, at most
// kMaxHeightShare of the screen, past which the text scrolls.
constexpr float kPanelW = 330.0f;
constexpr float kTitleH = 36.0f;          // the title strip and the gap below it
constexpr float kSide = 15.0f;            // left and right of the text and the buttons
constexpr float kBottom = 6.0f;           // below the buttons
constexpr float kButtonGap = 4.0f;        // between buttons in a row
constexpr float kRowGap = 5.0f;           // between rows of buttons
constexpr float kMaxWidthShare = 0.45f;
constexpr float kMaxHeightShare = 0.55f;
// A compact panel shows about this many lines of the step.
constexpr float kCompactLines = 4.5f;
// Below this frame height (the 800x600 layout) a tutorial's action step shows
// a compact panel.
constexpr float kSmallFrameH = 700.0f;

const UiTag* findTag(const UiContext& ui, std::string_view name) {
    for (const UiTag& t : ui.tags)
        if (t.name == name) return &t;
    return nullptr;
}

const ImVec4 kGood{0.45f, 0.9f, 0.45f, 1.0f};
const ImVec4 kBad{1.0f, 0.5f, 0.42f, 1.0f};
const ImVec4 kGold{1.0f, 0.85f, 0.45f, 1.0f};

void dimWrapped(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(imColorV(palette::kSecondary), "%s", text);
    ImGui::PopTextWrapPos();
}

// How long a step's targets may be missing from the screen, and how long a
// step may last, before Next offers to skip it (seconds of play).
constexpr double kTargetsGoneSkip = 3.0;
constexpr double kStepSkip = 120.0;
// How long the hint after a refused click shows.
constexpr double kRefusedHint = 2.5;

// Dims everything but `areas` (the spotlight of the input lock): the screen
// is cut into a grid at the areas' edges, and every cell outside them is
// filled, row by row.
void spotlight(ImDrawList* dl, const std::vector<LockArea>& areas, ImVec2 size, ImU32 color) {
    std::vector<float> xs{0.0f, size.x}, ys{0.0f, size.y};
    for (const LockArea& a : areas) {
        xs.push_back(std::clamp(a.min.x, 0.0f, size.x));
        xs.push_back(std::clamp(a.max.x, 0.0f, size.x));
        ys.push_back(std::clamp(a.min.y, 0.0f, size.y));
        ys.push_back(std::clamp(a.max.y, 0.0f, size.y));
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    auto open = [&](float x, float y) { return std::any_of(areas.begin(), areas.end(), [&](const LockArea& a) { return a.contains({x, y}); }); };
    for (size_t j = 0; j + 1 < ys.size(); ++j) {
        const float cy = (ys[j] + ys[j + 1]) * 0.5f;
        size_t i = 0;
        while (i + 1 < xs.size()) {
            if (open((xs[i] + xs[i + 1]) * 0.5f, cy)) {
                ++i;
                continue;
            }
            size_t k = i + 1;   // a run of dim cells
            while (k + 1 < xs.size() && !open((xs[k] + xs[k + 1]) * 0.5f, cy)) ++k;
            dl->AddRectFilled(ImVec2(xs[i], ys[j]), ImVec2(xs[k], ys[j + 1]), color);
            i = k;
        }
    }
}

} // namespace

LessonRunner::LessonRunner(learn::Lesson lesson, const ClassicSession& session)
    : progress_(std::move(lesson), session.rules(), session.state(), session.player()) {}

void LessonRunner::frame(UiContext& ui, const learn::ClientFacts& facts, const LockState& lock) {
    windowsOpen_ = !facts.openWindows.empty();
    evaluate(ui, facts);
    // When the active step began, and whether its targets are on screen.
    if (activeSeen_ != progress_.active()) {
        activeSeen_ = progress_.active();
        activeSince_ = targetsSeen_ = ui.time;
    }
    if (const learn::Step* st = activeStep())
        for (const std::string& tag : st->highlight)
            if (!tag.starts_with("lesson:") && findTag(ui, tag)) targetsSeen_ = ui.time;
    const Prompts prompts = findPrompts(ui);
    drawPanel(ui, prompts);
    drawOutlines(ui, lock);
    raisePanel(prompts);
    drawResult(ui);
}

bool LessonRunner::locking() const {
    return lesson().kind == learn::LessonKind::Tutorial && !settings().learnFreePlay &&
           progress_.result() == learn::LessonProgress::Result::None && activeStep() != nullptr;
}

const learn::Step* LessonRunner::activeStep() const {
    const auto& steps = lesson().steps;
    return progress_.active() < steps.size() ? &steps[progress_.active()] : nullptr;
}

void LessonRunner::refused(ImVec2 where, double time) {
    refusedAt_ = where;
    refusedTime_ = time;
}

bool LessonRunner::stuck(const UiContext& ui) const {
    const learn::Step* st = activeStep();
    if (!st || !st->done || progress_.completed(progress_.active())) return false;
    const bool targets = std::any_of(st->highlight.begin(), st->highlight.end(), [](const std::string& t) { return !t.starts_with("lesson:"); });
    return (targets && ui.time - targetsSeen_ > kTargetsGoneSkip) || ui.time - activeSince_ > kStepSkip;
}

void LessonRunner::jumpTo(const UiContext& ui, size_t step) {
    progress_.jumpTo(step, ui.rules(), ui.state(), ui.session.player());
    seen_ = 0;
}

void LessonRunner::finished() {
    showResult_ = true;
    panelOpen_ = true;
    const learn::LessonProgress::Result r = progress_.result();
    if (r == learn::LessonProgress::Result::Done || r == learn::LessonProgress::Result::Won) markLessonDone(lesson().kind, lesson().slug);
}

void LessonRunner::evaluate(UiContext& ui, const learn::ClientFacts& facts) {
    // The conditions read the game, the commands and the client facts: check
    // them again only when one of these changed.
    const ClassicSession& session = ui.session;
    Hasher h;
    h.add(session.revision()).add(progress_.tracker().commands().size()).add(progress_.step());
    for (const std::string& w : facts.openWindows) h.add(std::string_view(w));
    h.add(std::string_view("|"));
    for (const std::string& k : facts.selected) h.add(std::string_view(k));
    h.add(facts.selections);
    for (const std::string& t : facts.tabs) h.add(std::string_view(t));
    h.add(facts.designComponents.value_or(-1)).add(facts.designHullChosen).add(facts.simulatorOwners).add(facts.simulatorItems);
    h.add(facts.battleBegun).add(facts.battleOrders.size());
    if (h.value() == seen_ || progress_.result() != learn::LessonProgress::Result::None) return;
    seen_ = h.value();
    const learn::LessonProgress::Changes ch = progress_.update(ui.rules(), ui.state(), session.player(), facts);
    if (ch.stepChanged) audio().play("button");
    if (ch.stepChanged || ch.pageShown || ch.hintShown) panelOpen_ = true;
    if (ch.finished) finished();
}

void LessonRunner::drawOutlines(UiContext& ui, const LockState& lock) const {
    const learn::Step* st = lesson().kind == learn::LessonKind::Tutorial ? activeStep() : nullptr;
    if (!st || progress_.result() != learn::LessonProgress::Result::None) return;
    const bool outline = !st->highlight.empty() && !progress_.completed(progress_.active());
    if (!outline && !lock.active) return;
    // The spotlight goes in a see-through window over the classic windows and
    // under the panel. Each outline goes in the layer of the window its part
    // is drawn in, so a prompt, a menu or another window above that window
    // covers the outline as it covers the part.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::Begin("##lessonoutlines", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* under = ImGui::GetWindowDrawList();
    ImGui::End();
    // A refused click makes the outlines flash white for a moment.
    const double sinceRefused = ui.time - refusedTime_;
    const bool flash = sinceRefused >= 0 && sinceRefused < 1.0 && std::fmod(sinceRefused, 0.25) < 0.125;
    const float thick = std::max(2.0f, ui.px(flash ? 4.0f : 2.5f));
    const float pad = ui.px(3);
    // Everything the step does not let the player use is dimmed. The clear
    // areas reach round the outlines, which lie just outside their parts.
    if (lock.active) {
        std::vector<LockArea> open = lock.areas;
        open.insert(open.end(), lock.lookAreas.begin(), lock.lookAreas.end());
        const float ring = pad + std::max(2.0f, ui.px(4.0f));
        for (LockArea& a : open) a = {ImVec2(a.min.x - ring, a.min.y - ring), ImVec2(a.max.x + ring, a.max.y + ring)};
        spotlight(under, open, display, IM_COL32(0, 0, 0, 140));
    }
    if (outline) {
        const float pulse = 0.6f + 0.4f * std::sin(float(ui.time) * 5.0f);
        const ImU32 color = flash ? IM_COL32_WHITE : imColor(0xffd040, pulse);
        for (const std::string& tag : st->highlight)
            for (const UiTag& t : ui.tags) {
                if (t.name != tag) continue;
                // A part drawn outside every window lies on the map, under all of them.
                ImDrawList* dl = t.window ? t.window->DrawList : ImGui::GetBackgroundDrawList();
                dl->PushClipRect(ImVec2(0, 0), display, false);
                dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
                dl->AddRect(ImVec2(t.min.x - pad, t.min.y - pad), ImVec2(t.max.x + pad, t.max.y + pad), color, 0.0f, thick);
                dl->PopTexture();
                dl->PopClipRect();
            }
    }
    // And a word where the player clicked (outlined or not).
    if (refusedAt_ && sinceRefused >= 0 && sinceRefused < kRefusedHint) {
        const char* kHint = st->done ? "The lesson is waiting for the outlined part.\nFree Play in the lesson panel unlocks the game."
                                     : "This step explains: press Next in the lesson panel.\nFree Play in the lesson panel unlocks the game.";
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImVec2 textSize = ImGui::CalcTextSize(kHint);
        const ImVec2 pad2(ui.px(6), ui.px(4));
        ImVec2 at(refusedAt_->x + ui.px(14), refusedAt_->y + ui.px(10));
        at.x = std::min(at.x, display.x - textSize.x - 2 * pad2.x);
        at.y = std::min(at.y, display.y - textSize.y - 2 * pad2.y);
        const float alpha = float(std::min(1.0, (kRefusedHint - sinceRefused) * 2.0));
        fg->AddRectFilled(at, ImVec2(at.x + textSize.x + 2 * pad2.x, at.y + textSize.y + 2 * pad2.y), imColor(0x101c40, 0.95f * alpha));
        fg->AddRect(at, ImVec2(at.x + textSize.x + 2 * pad2.x, at.y + textSize.y + 2 * pad2.y), imColor(0xffd040, alpha));
        fg->AddText(ImVec2(at.x + pad2.x, at.y + pad2.y), imColor(0xffffff, alpha), kHint);
    }
}

void LessonRunner::followLink(UiContext& ui, const std::string& target) {
    const learn::Link l = learn::parseLink(target);
    ScreenArgs a;
    switch (l.kind) {
        case learn::Link::Kind::Page:
            a.text = target;
            ui.open(ScreenId::Manual, a);
            break;
        case learn::Link::Kind::Window:
            if (const auto id = screenFromWindowId(l.target)) ui.open(*id);
            break;
        case learn::Link::Kind::Help:
            a.text = l.target;
            ui.open(ScreenId::Help, a);
            break;
        case learn::Link::Kind::External: SDL_OpenURL(l.target.c_str()); break;
        case learn::Link::Kind::Invalid: break;
    }
}

void LessonRunner::tutorialBody(UiContext& ui) {
    const learn::Lesson& l = lesson();
    const size_t n = l.steps.size(), step = progress_.step();
    if (step >= n) return;
    const learn::Step& st = l.steps[step];
    ImGui::TextColored(kLabelBlue, "Step %zu of %zu", step + 1, n);
    if (step < progress_.active()) {
        ImGui::SameLine();
        ImGui::TextColored(kGold, "(reading back: Next returns to step %zu)", progress_.active() + 1);
    }
    heading(ui, st.title.c_str());
    ImGui::Spacing();
    // While the game is locked to the step, a link cannot open a window around it.
    MarkdownOptions options;
    const bool locked = locking();
    options.canFollow = [locked](const learn::Link& link) { return !locked || link.kind != learn::Link::Kind::Window; };
    options.cannotFollow = "Use the outlined button: the lesson locks the game (Free Play unlocks it)";
    if (auto clicked = drawMarkdown(ui.painter(), st.text, options)) followLink(ui, *clicked);
    ImGui::Spacing();
    if (progress_.result() == learn::LessonProgress::Result::Done) ImGui::TextColored(kGood, "Lesson complete.");
    else if (st.done && progress_.completed(step)) ImGui::TextColored(kGood, "Done.");
    else if (st.done && stuck(ui)) dimWrapped("If this cannot be done any more, Skip moves on.");
    else if (st.done) dimWrapped("The lesson moves on by itself once you have done this.");
}

void LessonRunner::trainingBody(UiContext& ui) {
    const learn::Lesson& l = lesson();
    const Painter p = ui.painter();
    if (const auto hint = progress_.hint()) {
        const learn::Hint& h = l.hints[*hint];
        ImGui::TextColored(kGold, "%s", h.title.c_str());
        MarkdownOptions options;
        if (auto clicked = drawMarkdown(p, h.text, options)) followLink(ui, *clicked);
        if (ImGui::SmallButton("OK")) progress_.dismissHint();
        ImGui::Separator();
    }
    ImGui::TextColored(kLabelBlue, "Objectives");
    for (size_t i = 0; i < l.objectives.size(); ++i) {
        const learn::Objective& o = l.objectives[i];
        ImGui::PushID(int(i));
        lamp(ui, progress_.objectiveDone(i));
        ImGui::SameLine();
        const std::string text = o.byTurn ? std::format("{} (by {})", o.text, formatDate(*o.byTurn)) : o.text;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(progress_.objectiveDone(i) ? kGood : progress_.objectiveFailed(i) ? kBad : ImVec4(1, 1, 1, 1), "%s", text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }
    if (progress_.result() == learn::LessonProgress::Result::Won) ImGui::TextColored(kGood, "Training game won.");
    else if (progress_.result() == learn::LessonProgress::Result::Lost) ImGui::TextColored(kBad, "Training game lost.");
    if (const auto page = progress_.page()) {
        const learn::BriefingPage& pg = l.pages[*page];
        ImGui::Separator();
        const std::vector<size_t> series = progress_.series();
        const size_t index = size_t(std::find(series.begin(), series.end(), *page) - series.begin());
        heading(ui, pg.title.c_str());
        if (series.size() > 1) ImGui::TextColored(imColorV(palette::kSecondary), "%zu of %zu", index + 1, series.size());
        ImGui::Spacing();
        MarkdownOptions options;
        if (auto clicked = drawMarkdown(p, pg.text, options)) followLink(ui, *clicked);
    }
}

void LessonRunner::keepKeyboardFocus() {
    // A click on the panel (Next, Back) gives it Dear ImGui's focus, and the
    // classic window in front would no longer close with Esc or Enter
    // (Dialog::close): once the click is over, the window that had the focus
    // gets it back. Dragging the panel keeps it until the drag ends.
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* panel = ImGui::FindWindowByName("##lessonpanel");
    ImGuiWindow* nav = g.NavWindow ? g.NavWindow->RootWindow : nullptr;
    if (!nav) return;
    if (nav != panel) {
        focusBefore_ = nav->ID;
        return;
    }
    if (g.MovingWindow == panel || g.ActiveId != 0 || ImGui::IsAnyMouseDown() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    if (ImGuiWindow* back = ImGui::FindWindowByID(focusBefore_); back && back != panel && back->WasActive) {
        ImGui::FocusWindow(back);
        return;
    }
    // That window has closed (Dear ImGui then focuses the one before it in
    // focus order, which can be the panel): the front-most other window.
    for (int i = g.WindowsFocusOrder.Size - 1; i >= 0; --i) {
        ImGuiWindow* w = g.WindowsFocusOrder[i];
        if (w == panel || !w->WasActive ||
            (w->Flags & (ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoNavFocus)) != 0)
            continue;
        ImGui::FocusWindow(w);
        return;
    }
}

LessonRunner::Prompts LessonRunner::findPrompts(const UiContext& ui) {
    Prompts out;
    for (const auto& [a, b] : ui.promptAreas) out.boxes.push_back({a, b});
    const ImGuiWindow* panel = ImGui::FindWindowByName("##lessonpanel");
    auto listed = [&](const ImGuiWindow* w) {
        return std::any_of(ui.promptAreas.begin(), ui.promptAreas.end(), [&](const auto& area) {
            return std::abs(area.first.x - w->Pos.x) < 1.0f && std::abs(area.first.y - w->Pos.y) < 1.0f;
        });
    };
    // Back to front. A prompt is a pop-up (the lesson's own Leave question and
    // result, combo lists, the windows' Yes/No boxes) or a window drawn with
    // kPromptFlags; a window that takes no input at all (the outlines' layer) is none.
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if (w == panel || w->RootWindow != w || !(w->Active || w->WasActive)) continue;
        const ImGuiWindowFlags f = w->Flags;
        if ((f & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip)) != 0) continue;
        const bool popup = (f & ImGuiWindowFlags_Popup) != 0;
        const bool prompt = (f & ImGuiWindowFlags_NoNavInputs) != 0 && (f & ImGuiWindowFlags_NoMouseInputs) == 0;
        const bool known = listed(w);
        if (!popup && !prompt && !known) continue;
        if (!out.lowest) out.lowest = w;
        if (!known) out.boxes.push_back({w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y)});
    }
    return out;
}

void LessonRunner::raisePanel(const Prompts& prompts) const {
    // Above the classic windows, which take the focus (and the front) when
    // they open; under every prompt and pop-up, so that their buttons are
    // never under the panel (it used to be raised over them every frame).
    ImGuiWindow* panel = ImGui::FindWindowByName("##lessonpanel");
    if (!panel || !panelOpen_ || !panel->Active) return;
    ImGui::BringWindowToDisplayFront(panel);
    if (prompts.lowest) ImGui::BringWindowToDisplayBehind(panel, prompts.lowest);
}

bool LessonRunner::compact() const {
    if (lesson().kind != learn::LessonKind::Tutorial || frameH() >= kSmallFrameH || progress_.result() != learn::LessonProgress::Result::None)
        return false;
    const size_t step = progress_.step();
    if (compactChoice_ && compactChoice_->first == step) return compactChoice_->second;
    // An action step the lesson is at: the window it is about matters more
    // than the reading. Explanation steps and steps read again show whole.
    const learn::Step* st = activeStep();
    return st && step == progress_.active() && st->done && !progress_.completed(step);
}

void LessonRunner::press(UiContext& ui, Button b) {
    const learn::Lesson& l = lesson();
    switch (b) {
        case Button::Back: progress_.goBack(); break;
        case Button::Next:
        case Button::Skip:
            if (b == Button::Skip) progress_.skip(ui.rules(), ui.state(), ui.session.player());
            else progress_.goNext(ui.rules(), ui.state(), ui.session.player());
            seen_ = 0;
            if (progress_.result() != learn::LessonProgress::Result::None) finished();
            break;
        case Button::ReadMore:
            if (const size_t step = progress_.step(); step < l.steps.size() && !l.steps[step].manual.empty()) {
                ScreenArgs a;
                a.text = l.steps[step].manual;
                ui.open(ScreenId::Manual, a);
            }
            break;
        case Button::Previous:
        case Button::PageNext: {
            // Previous and next browse the shown page's series.
            const std::vector<size_t> series = progress_.series();
            const auto shown = progress_.page() ? std::find(series.begin(), series.end(), *progress_.page()) : series.end();
            if (shown == series.end()) break;
            if (b == Button::Previous && shown != series.begin()) progress_.showPage(*(shown - 1));
            if (b == Button::PageNext && shown + 1 != series.end()) progress_.showPage(*(shown + 1));
            break;
        }
        case Button::ClosePage: progress_.showPage(std::nullopt); break;
        case Button::More: compactChoice_ = std::pair{progress_.step(), !compact()}; break;
        case Button::Hide: panelOpen_ = false; break;
        case Button::FreePlay:
            settings().learnFreePlay = !settings().learnFreePlay;
            saveSettings();
            break;
        case Button::Leave:
            if (progress_.result() != learn::LessonProgress::Result::None) request_ = Request::Leave;
            else leave_.open("Leave the lesson? Its game ends; anything not saved is lost.", "Leave Lesson");
            break;
    }
}

void LessonRunner::drawPanel(UiContext& ui, const Prompts& prompts) {
    keepKeyboardFocus();
    if (!panelOpen_) return;
    const Painter p = ui.painter();
    const learn::Lesson& l = lesson();
    const bool tutorial = l.kind == learn::LessonKind::Tutorial;
    const bool over = progress_.result() != learn::LessonProgress::Result::None;
    const bool smallScreen = tutorial && !over && frameH() < kSmallFrameH;   // More and Less switch the compact panel
    const bool tight = compact();
    const Bindings& keys = appSettings().controls.bindings;
    auto keyName = [&](Action a) {
        const KeyChord& c = keys.chords(a)[0];
        return c.empty() ? std::string{} : std::format(" ({})", chordName(c));
    };
    // One line for the lesson's recovery hint above the buttons
    // (LessonRunner::recoveryHint(), wired in at the merge).
    const std::string hint;

    // The buttons, in two groups: moving through the lesson, then the panel
    // itself (a compact panel leaves the second group to More).
    struct Spec {
        Button what;
        std::string label;
        bool enabled = true;
        const char* tag = nullptr;
        std::string tip;
        int style = 0;
        bool on = false;
    };
    std::vector<Spec> moving, own;
    bool skip = false;
    if (tutorial) {
        const size_t step = progress_.step();
        const learn::Step* st = step < l.steps.size() ? &l.steps[step] : nullptr;
        const bool last = step + 1 >= l.steps.size();
        // On an active step that looks impossible now, Next offers to skip it.
        skip = step == progress_.active() && !progress_.canGoNext() && stuck(ui);
        moving.push_back({Button::Back, "Back", progress_.canGoBack(), "lesson:back", "The step before, to read it again" + keyName(Action::LessonBack)});
        if (skip)
            moving.push_back({Button::Skip, "Skip##next", true, "lesson:next",
                              "Moves on without this step, for when it cannot be done any more" + keyName(Action::LessonSkip)});
        else
            moving.push_back({Button::Next, last ? "Finish##next" : "Next##next", progress_.canGoNext(), "lesson:next",
                              std::string(last ? "Ends the lesson" : "On to the next step") + keyName(Action::LessonNext)});
        moving.push_back({Button::ReadMore, "Read More", st && !st->manual.empty(), "lesson:read-more",
                          "This step's page in the manual" + keyName(Action::LessonReadMore)});
    } else {
        const std::vector<size_t> series = progress_.series();
        const auto shown = progress_.page() ? std::find(series.begin(), series.end(), *progress_.page()) : series.end();
        const bool hasPrev = shown != series.end() && shown != series.begin();
        const bool hasNext = shown != series.end() && shown + 1 != series.end();
        moving.push_back({Button::Previous, "Previous", hasPrev, nullptr, "The page before" + keyName(Action::LessonBack)});
        moving.push_back({Button::PageNext, "Next##next", hasNext, "lesson:next", "The next page" + keyName(Action::LessonNext)});
        moving.push_back({Button::ClosePage, "Close Page", progress_.page().has_value()});
    }
    if (!tight) {
        // Hide, Free Play (tutorials only: training games are never locked), Leave.
        own.push_back({Button::Hide, "Hide", true, "lesson:hide", std::format("{} or the T button shows the panel again", chordName(keys.chords(Action::LessonText)[0]))});
        if (tutorial)
            own.push_back({Button::FreePlay, "Free Play", true, "lesson:free-play",
                           "Off: the lesson lets you use only what each step is about.\nOn: the whole game works while the lesson guides you.", 2,
                           settings().learnFreePlay});
        own.push_back({Button::Leave, over ? "Learn" : tutorial ? "Leave" : "Leave Game", true, "lesson:leave", over ? "" : "Leave the lesson"});
    }

    // Sizes from the text: the buttons' labels, the reading font's lines.
    const float k = ui.k();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Vec2 screen{display.x / k, display.y / k};   // in frame pixels
    ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
    std::vector<float> movingW, ownW;
    for (const Spec& s : moving) movingW.push_back(std::ceil(ImGui::CalcTextSize(s.label.c_str(), nullptr, true).x / k + 16.0f));
    for (const Spec& s : own) ownW.push_back(std::ceil(ImGui::CalcTextSize(s.label.c_str(), nullptr, true).x / k + (s.style == 2 ? 32.0f : 16.0f)));
    const float rowH = std::max(26.0f, std::ceil(ImGui::GetFontSize() / k + 6.0f));
    ImGui::PopFont();
    ImGui::PushFont(ui.fonts.readingFont(), ui.fontPx(kTextSize));
    const float line = ImGui::GetTextLineHeightWithSpacing() / k;
    auto hintHeight = [&](float inner) { return hint.empty() ? 0.0f : std::ceil(ImGui::CalcTextSize(hint.c_str(), nullptr, false, ui.px(inner - 8)).y / k) + 4.0f; };

    struct Layout {
        float w = 0, h = 0, body = 0, hint = 0, buttons = 0;
        std::vector<panel::Slot> moving, own;
    };
    auto layoutFor = [&](float w, float maxH) {
        Layout L;
        L.w = w;
        const float inner = w - 2 * kSide;
        L.moving = panel::flowButtons(movingW, inner, kButtonGap);
        L.own = panel::flowButtons(ownW, inner, kButtonGap);
        const int rows = panel::rowCount(L.moving) + panel::rowCount(L.own);
        L.buttons = float(rows) * rowH + float(std::max(0, rows - 1)) * kRowGap;
        L.hint = hintHeight(inner);
        const float fixed = kTitleH + L.hint + 6.0f + L.buttons + kBottom;
        const float bodyMax = tight ? std::ceil(kCompactLines * line) + 4.0f : std::max(std::ceil(3 * line) + 4.0f, maxH - fixed);
        const float bodyMin = std::min(bodyMax, std::ceil(3 * line) + 4.0f);
        // The height the text needed last frame, rewrapped to this width.
        const float need = bodyNeed_ > 0 ? bodyNeed_ * std::max(1.0f, bodyWidth_ - 8.0f) / std::max(1.0f, inner - 8.0f) : bodyMax;
        L.body = std::clamp(std::ceil(need), bodyMin, bodyMax);
        L.h = fixed + L.body;
        return L;
    };
    const float width = std::max(std::min(kPanelW, screen.x - 8.0f), std::min(std::round(kPanelW * std::max(1.0f, ui.textScale)), std::floor(screen.x * kMaxWidthShare)));
    const Layout standard = layoutFor(width, std::floor(screen.y * kMaxHeightShare));

    // What the panel should not hide: what the active step outlines and
    // allows, and the prompts that are open.
    panel::Avoid avoid;
    if (const learn::Step* st = tutorial ? activeStep() : nullptr) {
        auto add = [&](const std::vector<std::string>& tags, std::vector<panel::Box>& into) {
            for (const std::string& tag : tags) {
                if (tag.starts_with("lesson:")) continue;
                for (const UiTag& t : ui.tags)
                    if (t.name == tag) into.push_back({t.min, t.max});
            }
        };
        add(st->highlight, avoid.targets);
        add(st->allow, avoid.allowed);
    }
    avoid.prompts = prompts.boxes;

    // Dragged: it keeps its place, until a later step would have more than
    // half of what it outlines under the panel.
    ImGuiWindow* existing = ImGui::FindWindowByName("##lessonpanel");
    if (moved_ && existing && movedOn_ != progress_.active() && !avoid.targets.empty()) {
        const panel::Box now{existing->Pos, ImVec2(existing->Pos.x + existing->Size.x, existing->Pos.y + existing->Size.y)};
        if (panel::hiddenShare(now, avoid.targets) > 0.5f * float(avoid.targets.size())) {
            moved_ = false;
            movedOn_.reset();
        }
    }

    const panel::Box screenBox{ImVec2(0, 0), display};
    Layout chosen = standard;
    if (!moved_) {
        // Its places, best first: over the galaxy panel while only the main
        // window shows; while a window is open, in a free column beside it
        // (wide screens), at the bottom left of the system view (where it
        // hides the least of the classic windows: their buttons are on the
        // right), at the system view's top left; then the corners of the
        // screen. The first that hides the least of the step's tags and of
        // the prompts wins (panel::bestSpot).
        const ImVec2 size = ui.size({standard.w, standard.h});
        const float gap = ui.px(4);
        std::vector<panel::Spot> spots;
        std::vector<Layout> layouts;
        auto spot = [&](int id, ImVec2 at, const Layout& L) {
            const ImVec2 sz = ui.size({L.w, L.h});
            spots.push_back({id, panel::keepInside({at, ImVec2(at.x + sz.x, at.y + sz.y)}, screenBox)});
            layouts.push_back(L);
        };
        const UiTag* galaxy = findTag(ui, "panel:galaxy");
        const UiTag* system = findTag(ui, "panel:system");
        if (galaxy && !windowsOpen_) spot(0, {galaxy->max.x - size.x, galaxy->max.y - size.y}, standard);
        if (windowsOpen_) {
            // The open windows and the main window's bars across the top.
            float left = display.x, right = 0, top = 0;
            for (const UiTag& t : ui.tags) {
                if (t.name.starts_with("window:")) {
                    left = std::min(left, t.min.x);
                    right = std::max(right, t.max.x);
                } else if (t.name.starts_with("status:") || t.name == "panel:commands" || t.name == "panel:orders") {
                    top = std::max(top, t.max.y);
                }
            }
            const float minW = std::max(240.0f, 0.8f * width);
            const float maxH = (display.y - top) / k - 8.0f;
            for (const auto& [id, x0, x1] : {std::tuple{1, 0.0f, left}, std::tuple{2, right, display.x}}) {
                const float room = (x1 - x0) / k - 8.0f;
                if (x1 <= x0 || room < minW) continue;
                const Layout column = layoutFor(std::floor(std::min(room, width)), maxH);
                spot(id, {(x0 + x1 - ui.px(column.w)) * 0.5f, display.y - ui.px(column.h) - gap}, column);
            }
        }
        if (system) {
            spot(3, {system->min.x + gap, system->max.y - size.y - gap}, standard);
            spot(4, {system->min.x + gap, system->min.y + ui.px(26)}, standard);
        }
        if (galaxy && windowsOpen_) spot(5, {galaxy->max.x - size.x, galaxy->max.y - size.y}, standard);
        if (!galaxy && !system) spot(6, ui.at({ui.map.left + 6, frameH() - standard.h - 6}), standard);
        spot(7, {gap, display.y - size.y - gap}, standard);
        spot(8, {display.x - size.x - gap, display.y - size.y - gap}, standard);
        spot(9, {display.x - size.x - gap, ui.px(40)}, standard);
        spot(10, {gap, ui.px(40)}, standard);
        const size_t best = panel::bestSpot(spots, avoid, spot_);
        spot_ = spots[best].id;
        chosen = layouts[best];
        ImGui::SetNextWindowPos(spots[best].box.min, ImGuiCond_Always);
    } else if (existing && ImGui::GetCurrentContext()->MovingWindow != existing) {
        // Where the player put it, kept on the screen as its size changes.
        const ImVec2 sz = ui.size({chosen.w, chosen.h});
        const panel::Box kept = panel::keepInside({existing->Pos, ImVec2(existing->Pos.x + sz.x, existing->Pos.y + sz.y)}, screenBox);
        if (kept.min.x != existing->Pos.x || kept.min.y != existing->Pos.y) ImGui::SetNextWindowPos(kept.min, ImGuiCond_Always);
    }
    ImGui::PopFont();
    const Layout& L = chosen;

    ImGui::SetNextWindowSize(ui.size({L.w, L.h}), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    const bool open = ImGui::Begin("##lessonpanel", nullptr, flags);
    ImGui::PopStyleVar(2);
    std::optional<Button> pressed;
    if (open) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        // Once the player drags it, it stays where they put it (above).
        if (ImGui::GetCurrentContext()->MovingWindow == window) {
            moved_ = true;
            movedOn_ = progress_.active();
        }
        const ImVec2 pos = ImGui::GetWindowPos();
        const Vec2 at = ui.map.fromFb(Vec2{pos.x, pos.y} * ui.fbScale);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // The frame with an empty title strip; the title is drawn below, at a
        // size that fits the strip at every text size.
        drawWindowFrame(p, dl, Rect{at, at + Vec2{L.w, L.h}}, "", 0);
        ui.tag("lesson:panel", pos, ImVec2(pos.x + ui.px(L.w), pos.y + ui.px(L.h)));
        float titleEnd = L.w - 12.0f;
        if (smallScreen) {
            // More shows the whole step and every button; Less the compact panel.
            Painter q = p;
            q.textScale = std::min(p.textScale, 1.2f);
            const char* label = tight ? "More" : "Less";
            ImGui::PushFont(q.fonts.bold, q.fontPx(kTitleSize));
            const float w = std::ceil(ImGui::CalcTextSize(label).x / k + 14.0f);
            ImGui::PopFont();
            ImGui::SetCursorPos(ui.size({L.w - 13.0f - w, 6}));
            if (classicButton(q, label, {w, 23})) {
                audio().play("button");
                pressed = Button::More;
            }
            ui.tagItem("lesson:more");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tight ? "Shows the whole step and every button of the panel" : "A smaller panel: the start of the step, Back and Next");
            titleEnd = L.w - 13.0f - w - 6.0f;
        }
        {
            const float size = std::min(kTitleSize * ui.textScale, 23.0f);
            const float y = std::max(5.0f, 11.0f - (size - kTitleSize) * 0.75f);
            ImGui::PushFont(ui.fonts.bold, ui.px(size));
            dl->PushClipRect(ui.at(at + Vec2{12, 4}), ui.at(at + Vec2{titleEnd, 31}), true);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ui.at(at + Vec2{17, y + kTitleLead}), IM_COL32_WHITE, l.title.c_str());
            dl->PopClipRect();
            ImGui::PopFont();
        }

        ImGui::SetCursorPos(ui.size({kSide, kTitleH}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.size({4, 2}));
        ImGui::BeginChild("##body", ui.size({L.w - 2 * kSide, L.body}), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        // A new step (or page) starts at the top of its text.
        const size_t scrollKey = tutorial ? progress_.step() : progress_.page().value_or(SIZE_MAX);
        if (scrolledFor_ != scrollKey) {
            ImGui::SetScrollY(0.0f);
            scrolledFor_ = scrollKey;
        }
        // OpenSE4's own panel: its own text font (docs/spec/06 §5.4).
        ImGui::PushFont(ui.fonts.readingFont(), ui.fontPx(kTextSize));
        if (tutorial) tutorialBody(ui);
        else trainingBody(ui);
        ImGui::PopFont();
        // How tall the text is, for the next frame's size.
        if (const ImGuiWindow* body = ImGui::GetCurrentWindow()) {
            bodyNeed_ = (body->DC.CursorMaxPos.y - body->DC.CursorStartPos.y) / k + 4.0f;
            bodyWidth_ = L.w - 2 * kSide;
        }
        ImGui::EndChild();

        if (!hint.empty()) {
            ImGui::SetCursorPos(ui.size({kSide + 4, kTitleH + L.body + 2}));
            ImGui::PushFont(ui.fonts.readingFont(), ui.fontPx(kTextSize));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui.px(L.w - 2 * kSide - 8));
            ImGui::TextColored(kGold, "%s", hint.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
        }

        // The buttons, row by row (panel::flowButtons): never overlapping, at any text size.
        const float y0 = L.h - kBottom - L.buttons;
        auto row = [&](const std::vector<Spec>& specs, const std::vector<panel::Slot>& slots, float top) {
            for (size_t i = 0; i < specs.size() && i < slots.size(); ++i) {
                const Spec& s = specs[i];
                ImGui::SetCursorPos(ui.size({kSide + slots[i].x, top + float(slots[i].row) * (rowH + kRowGap)}));
                if (classicButton(p, s.label.c_str(), {slots[i].w, rowH}, s.style, s.on, s.enabled)) {
                    audio().play("button");
                    pressed = s.what;
                }
                if (s.tag) ui.tagItem(s.tag);
                if (!s.tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.tip.c_str());
            }
        };
        row(moving, L.moving, y0);
        row(own, L.own, y0 + float(panel::rowCount(L.moving)) * (rowH + kRowGap));

        // The keys of the panel's buttons (Settings, Controls), while no
        // prompt is open: Next, Back, Skip when it is offered, Read More.
        if (!pressed && !prompts.lowest) {
            auto enabled = [&](Button b) {
                return std::any_of(moving.begin(), moving.end(), [&](const Spec& s) { return s.what == b && s.enabled; });
            };
            const std::pair<Action, Button> keyed[] = {
                {Action::LessonNext, tutorial ? Button::Next : Button::PageNext}, {Action::LessonSkip, Button::Skip},
                {Action::LessonBack, tutorial ? Button::Back : Button::Previous}, {Action::LessonReadMore, Button::ReadMore}};
            for (const auto& [action, button] : keyed)
                if (keys.pressed(action) && enabled(button)) {
                    audio().play("button");
                    pressed = button;
                    break;
                }
        }
    }
    ImGui::End();
    if (pressed) press(ui, *pressed);

    // A Yes/No message box: Y means Yes; N, Esc and Enter mean No (spec 06 §3.4).
    if (leave_.draw(ui)) request_ = Request::Leave;
}

void LessonRunner::drawResult(UiContext& ui) {
    constexpr const char* kPopup = "##lessonresult";
    if (showResult_) {
        ImGui::OpenPopup(kPopup);
        showResult_ = false;
    }
    ImGui::SetNextWindowSize(ui.size({400, 0}));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | kPromptFlags))
        return;
    using Result = learn::LessonProgress::Result;
    const Result result = progress_.result();
    const bool won = result == Result::Done || result == Result::Won;
    const char* title = result == Result::Done ? "Lesson complete" : result == Result::Won ? "Training game won" : "Training game lost";
    ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
    ImGui::TextColored(won ? kGood : kBad, "%s", title);
    ImGui::PopFont();
    ImGui::TextUnformatted(lesson().title.c_str());
    ImGui::Spacing();
    if (!progress_.why().empty()) ImGui::TextWrapped("%s", progress_.why().c_str());
    if (won) dimWrapped("The Learn window marks it done. You can keep playing this game.");
    ImGui::Spacing();
    const ImVec2 size(ui.px(118), ui.px(26));
    const learn::Lesson* next = ui.learn ? ui.learn->library.next(lesson().kind, lesson().slug) : nullptr;
    if (won && next) {
        if (ImGui::Button("Next Lesson", size)) {
            request_ = Request::Next;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    if (!won) {
        if (ImGui::Button("Try Again", size)) {
            request_ = Request::Restart;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    // A message box: Esc or Enter is its OK, Keep Playing (spec 06 §3.4).
    if (ImGui::Button("Keep Playing", size) || okKey()) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ImGui::Button("Learn", size)) {
        request_ = Request::Leave;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace opense4::client::classic
