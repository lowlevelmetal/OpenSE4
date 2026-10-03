#include "client/classic/lesson_runner.hpp"

#include "client/audio.hpp"
#include "client/classic/learn_content.hpp"
#include "client/classic/screens/markdown_view.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"
#include "core/hash.hpp"
#include "learn/tokens.hpp"

#include <imgui_internal.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::client::classic {

namespace {

// The panel: a classic window, movable. Until the player moves it, it sits
// over the galaxy panel while only the main window shows, and at the bottom
// left of the system view while a window is open, where it hides the least
// of the classic windows (their buttons are on the right).
constexpr Vec2 kPanelSize{330, 280};
constexpr float kButtonsH = 66.0f;

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
    drawPanel(ui);
    drawOutlines(ui, lock);
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
    h.add(std::string_view(facts.designType)).add(facts.designNamed).add(facts.selectedVehicle.value);
    h.add(facts.battleBegun).add(facts.battleOrders.size()).add(facts.battleTurn);
    if (h.value() == seen_ || progress_.result() != learn::LessonProgress::Result::None) return;
    seen_ = h.value();
    const learn::LessonProgress::Changes ch = progress_.update(ui.rules(), ui.state(), session.player(), facts);
    // The active step's progress line ("Systems explored: 3 of 5").
    counters_.clear();
    for (const learn::Counter& c : progress_.counters(ui.rules(), ui.state(), session.player(), facts))
        counters_ += (counters_.empty() ? "" : "   ") + c.text();
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
    if (ImGuiWindow* panel = ImGui::FindWindowByName("##lessonpanel"); panel && panelOpen_) ImGui::BringWindowToDisplayFront(panel);
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
    // The progress line: what the active step counts while it waits (turns, systems, items).
    if (step == progress_.active() && !counters_.empty()) ImGui::TextColored(kGold, "%s", counters_.c_str());
    ImGui::Spacing();
    // While the game is locked to the step, a link cannot open a window around it.
    MarkdownOptions options;
    const bool locked = locking();
    options.canFollow = [locked](const learn::Link& link) { return !locked || link.kind != learn::Link::Kind::Window; };
    options.cannotFollow = "Use the outlined button: the lesson locks the game (Free Play unlocks it)";
    // {design:<type>} tokens show the player's own design names.
    const std::vector<learn::Block> text = learn::expandTokens(st.text, ui.state(), ui.session.player());
    if (auto clicked = drawMarkdown(ui.painter(), text, options)) followLink(ui, *clicked);
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
        if (auto clicked = drawMarkdown(p, learn::expandTokens(h.text, ui.state(), ui.session.player()), options)) followLink(ui, *clicked);
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
        if (auto clicked = drawMarkdown(p, learn::expandTokens(pg.text, ui.state(), ui.session.player()), options)) followLink(ui, *clicked);
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

void LessonRunner::drawPanel(UiContext& ui) {
    keepKeyboardFocus();
    if (!panelOpen_) return;
    const Painter p = ui.painter();
    if (!moved_) {
        // Its places, best first: over the galaxy panel while only the main
        // window shows, at the bottom left of the system view while a window
        // is open, at the system view's top left, then the corners of the
        // screen. The first one that hides the least of what the active step
        // outlines and allows wins: each tag counts by the share of it that
        // is hidden, so a small button weighs as much as a large map.
        const ImVec2 size = ui.size(kPanelSize);
        const float gap = ui.px(4);
        std::vector<ImVec2> spots;
        const UiTag* galaxy = findTag(ui, "panel:galaxy");
        const UiTag* system = findTag(ui, "panel:system");
        if (galaxy && !windowsOpen_) spots.emplace_back(galaxy->max.x - size.x, galaxy->max.y - size.y);
        if (system) {
            spots.emplace_back(system->min.x + gap, system->max.y - size.y - gap);
            spots.emplace_back(system->min.x + gap, system->min.y + ui.px(26));
        }
        if (galaxy && windowsOpen_) spots.emplace_back(galaxy->max.x - size.x, galaxy->max.y - size.y);
        if (spots.empty()) spots.push_back(ui.at({ui.map.left + 6, frameH() - kPanelSize.y - 6}));
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        spots.emplace_back(gap, display.y - size.y - gap);
        spots.emplace_back(display.x - size.x - gap, display.y - size.y - gap);
        spots.emplace_back(display.x - size.x - gap, ui.px(40));
        spots.emplace_back(gap, ui.px(40));
        auto hidden = [&](ImVec2 at) {
            float share = 0;
            auto add = [&](const std::vector<std::string>& tags, float weight) {
                for (const std::string& tag : tags) {
                    if (tag.starts_with("lesson:")) continue;
                    for (const UiTag& t : ui.tags) {
                        if (t.name != tag) continue;
                        const float w = std::min(t.max.x, at.x + size.x) - std::max(t.min.x, at.x);
                        const float h = std::min(t.max.y, at.y + size.y) - std::max(t.min.y, at.y);
                        const float all = (t.max.x - t.min.x) * (t.max.y - t.min.y);
                        if (w > 0 && h > 0 && all > 0) share += weight * w * h / all;
                    }
                }
            };
            if (const learn::Step* st = activeStep()) {
                add(st->highlight, 1.0f);
                add(st->allow, 0.5f);
            }
            return share;
        };
        ImVec2 best = spots.front();
        float bestHidden = hidden(best);
        for (const ImVec2& at : spots)
            if (const float h = hidden(at); h < bestHidden) {
                best = at;
                bestHidden = h;
            }
        ImGui::SetNextWindowPos(best, ImGuiCond_Always);
    }
    ImGui::SetNextWindowSize(ui.size(kPanelSize), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    const bool open = ImGui::Begin("##lessonpanel", nullptr, flags);
    ImGui::PopStyleVar(2);
    if (open) {
        // Above the classic windows, which take the focus when they open.
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGui::BringWindowToDisplayFront(window);
        // Once the player drags it, it stays where they put it.
        if (ImGui::GetCurrentContext()->MovingWindow == window) moved_ = true;
        const ImVec2 pos = ImGui::GetWindowPos();
        const Vec2 at = ui.map.fromFb(Vec2{pos.x, pos.y} * ui.fbScale);
        drawWindowFrame(p, ImGui::GetWindowDrawList(), Rect{at, at + kPanelSize}, lesson().title.c_str(), 0);
        ui.tag("lesson:panel", pos, ImVec2(pos.x + ui.px(kPanelSize.x), pos.y + ui.px(kPanelSize.y)));

        ImGui::SetCursorPos(ui.size({15, 36}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.size({4, 2}));
        ImGui::BeginChild("##body", ui.size({kPanelSize.x - 30, kPanelSize.y - 36 - kButtonsH - 6}), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        // OpenSE4's own panel: its own text font (docs/spec/06 §5.4).
        ImGui::PushFont(ui.fonts.readingFont(), ui.fontPx(kTextSize));
        if (lesson().kind == learn::LessonKind::Tutorial) tutorialBody(ui);
        else trainingBody(ui);
        ImGui::PopFont();
        ImGui::EndChild();

        // Two rows of buttons.
        const float y0 = kPanelSize.y - kButtonsH - 2;
        const float inner = kPanelSize.x - 30;
        auto button = [&](const char* label, float x, float w, float y, bool enabled) {
            ImGui::SetCursorPos(ui.size({15 + x, y}));
            const bool clicked = classicButton(p, label, {w, 26}, 0, false, enabled);
            if (clicked) audio().play("button");
            return clicked;
        };
        const float third = (inner - 8) / 3;
        const learn::Lesson& l = lesson();
        const bool over = progress_.result() != learn::LessonProgress::Result::None;
        if (l.kind == learn::LessonKind::Tutorial) {
            const size_t step = progress_.step();
            const learn::Step* st = step < l.steps.size() ? &l.steps[step] : nullptr;
            if (button("Back", 0, third, y0, progress_.canGoBack())) progress_.goBack();
            const bool last = step + 1 >= l.steps.size();
            // On an active step that looks impossible now, Next offers to skip it.
            const bool skip = step == progress_.active() && !progress_.canGoNext() && stuck(ui);
            const char* next = skip ? "Skip##next" : last ? "Finish##next" : "Next##next";
            if (button(next, third + 4, third, y0, progress_.canGoNext() || skip)) {
                if (skip) progress_.skip(ui.rules(), ui.state(), ui.session.player());
                else progress_.goNext(ui.rules(), ui.state(), ui.session.player());
                seen_ = 0;
                if (progress_.result() != learn::LessonProgress::Result::None) finished();
            }
            if (skip && ImGui::IsItemHovered()) ImGui::SetTooltip("Moves on without this step, for when it cannot be done any more");
            ui.tagItem("lesson:next");
            if (button("Read More", 2 * (third + 4), third, y0, st && !st->manual.empty())) {
                ScreenArgs a;
                a.text = st->manual;
                ui.open(ScreenId::Manual, a);
            }
            ui.tagItem("lesson:read-more");
        } else {
            // Previous and next browse the shown page's series.
            const std::vector<size_t> series = progress_.series();
            const auto shown = progress_.page() ? std::find(series.begin(), series.end(), *progress_.page()) : series.end();
            const bool hasPrev = shown != series.end() && shown != series.begin();
            const bool hasNext = shown != series.end() && shown + 1 != series.end();
            if (button("Previous", 0, third, y0, hasPrev)) progress_.showPage(*(shown - 1));
            if (button("Next##next", third + 4, third, y0, hasNext)) progress_.showPage(*(shown + 1));
            ui.tagItem("lesson:next");
            if (button("Close Page", 2 * (third + 4), third, y0, progress_.page().has_value())) progress_.showPage(std::nullopt);
        }
        // Hide, Free Play (tutorials only: training games are never locked), Leave.
        const bool tutorial = l.kind == learn::LessonKind::Tutorial;
        const float hideW = tutorial ? 78.0f : (inner - 4) / 2;
        const float freeW = tutorial ? inner - 2 * 78.0f - 8 : 0.0f;
        if (button("Hide", 0, hideW, y0 + 31, true)) panelOpen_ = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+H or the T button shows the panel again");
        if (tutorial) {
            ImGui::SetCursorPos(ui.size({15 + hideW + 4, y0 + 31}));
            if (classicButton(p, "Free Play", {freeW, 26}, 2, settings().learnFreePlay, true)) {
                audio().play("button");
                settings().learnFreePlay = !settings().learnFreePlay;
                saveSettings();
            }
            ui.tagItem("lesson:free-play");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Off: the lesson lets you use only what each step is about.\nOn: the whole game works while the lesson guides you.");
        }
        const char* leave = over ? "Learn" : tutorial ? "Leave" : "Leave Game";
        const float leaveX = tutorial ? hideW + freeW + 8 : hideW + 4;
        if (button(leave, leaveX, tutorial ? 78.0f : hideW, y0 + 31, true)) {
            if (over) request_ = Request::Leave;
            else leave_.open("Leave the lesson? Its game ends; anything not saved is lost.", "Leave Lesson");
        }
        if (!over && ImGui::IsItemHovered()) ImGui::SetTooltip("Leave the lesson");
        ui.tagItem("lesson:leave");
    }
    ImGui::End();

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
    const learn::Lesson& l = lesson();
    const Result result = progress_.result();
    const bool won = result == Result::Done || result == Result::Won;
    const bool tutorial = l.kind == learn::LessonKind::Tutorial;
    const char* title = result == Result::Done ? "Lesson complete" : result == Result::Won ? "Training game won" : "Training game lost";
    ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
    ImGui::TextColored(won ? kGood : kBad, "%s", title);
    ImGui::PopFont();
    ImGui::TextUnformatted(l.title.c_str());
    ImGui::Spacing();
    if (!progress_.why().empty()) ImGui::TextWrapped("%s", progress_.why().c_str());
    // The recap: what the lesson taught.
    if (won && !l.learned.empty()) {
        ImGui::TextColored(kLabelBlue, "%s", tutorial ? "What you learned" : "What you practised");
        ImGui::PushTextWrapPos(0.0f);
        for (const std::string& item : l.learned) {
            ImGui::Bullet();
            ImGui::TextUnformatted(learn::expandTokens(item, ui.state(), ui.session.player()).c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    if (won) dimWrapped("The Learn window marks it done. You can keep playing this game.");
    // What to play next: the lesson's suggestion, else the next one of its kind.
    const learn::Lesson* next = won && ui.learn ? ui.learn->library.following(l) : nullptr;
    if (next) {
        const bool training = next->kind == learn::LessonKind::Training;
        const std::string line = std::format("Next{}: {} ({} min)", training && tutorial ? ", a training game" : "", next->title, next->minutes);
        ImGui::TextColored(kGold, "%s", line.c_str());
        script::reportItem(line);   // input scripts check it by its text
        if (!next->summary.empty()) dimWrapped(next->summary.c_str());
    } else if (won) {
        dimWrapped(tutorial ? "That was the last lesson. The Training tab of the Learn window has practice games."
                            : "That was the last training game. Start a new game from the intro screen when you are ready.");
    }
    ImGui::Spacing();
    const ImVec2 size(ui.px(118), ui.px(26));
    if (next) {
        // "Next Lesson" only for a lesson; a training game is a game.
        const char* label = next->kind == learn::LessonKind::Tutorial ? "Next Lesson" : tutorial ? "Training Game" : "Next Game";
        if (ImGui::Button(label, size)) {
            ui.requests.startLesson = {next->kind, next->slug};
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
