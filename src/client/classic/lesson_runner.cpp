#include "client/classic/lesson_runner.hpp"

#include "client/audio.hpp"
#include "client/classic/learn_content.hpp"
#include "client/classic/screens/markdown_view.hpp"
#include "client/classic/widgets.hpp"
#include "core/hash.hpp"

#include <imgui_internal.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <format>

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

} // namespace

LessonRunner::LessonRunner(learn::Lesson lesson, const ClassicSession& session)
    : progress_(std::move(lesson), session.rules(), session.state(), session.player()) {}

void LessonRunner::frame(UiContext& ui, const learn::ClientFacts& facts) {
    windowsOpen_ = !facts.openWindows.empty();
    evaluate(ui, facts);
    drawPanel(ui);
    drawOutlines(ui);
    drawResult(ui);
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
    if (h.value() == seen_ || progress_.result() != learn::LessonProgress::Result::None) return;
    seen_ = h.value();
    const learn::LessonProgress::Changes ch = progress_.update(ui.rules(), ui.state(), session.player(), facts);
    if (ch.stepChanged) audio().play("button");
    if (ch.stepChanged || ch.pageShown || ch.hintShown) panelOpen_ = true;
    if (ch.finished) finished();
}

void LessonRunner::drawOutlines(UiContext& ui) const {
    const learn::Lesson& l = lesson();
    const size_t step = progress_.step();
    if (l.kind != learn::LessonKind::Tutorial || progress_.result() != learn::LessonProgress::Result::None || step >= l.steps.size()) return;
    const learn::Step& st = l.steps[step];
    if (st.highlight.empty() || progress_.completed(step)) return;
    // The outlines go in a see-through window over the classic windows and
    // under the panel (the panel's own buttons are outlined over it).
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##lessonoutlines", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList* under = ImGui::GetWindowDrawList();
    ImGui::End();
    if (ImGuiWindow* panel = ImGui::FindWindowByName("##lessonpanel"); panel && panelOpen_) ImGui::BringWindowToDisplayFront(panel);
    const float pulse = 0.6f + 0.4f * std::sin(float(ui.time) * 5.0f);
    const ImU32 color = imColor(0xffd040, pulse);
    const float thick = std::max(2.0f, ui.px(2.5f));
    const float pad = ui.px(3);
    for (const std::string& tag : st.highlight) {
        ImDrawList* dl = tag.starts_with("lesson:") ? ImGui::GetForegroundDrawList() : under;
        for (const UiTag& t : ui.tags)
            if (t.name == tag) dl->AddRect(ImVec2(t.min.x - pad, t.min.y - pad), ImVec2(t.max.x + pad, t.max.y + pad), color, 0.0f, thick);
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
    heading(ui, st.title.c_str());
    ImGui::Spacing();
    MarkdownOptions options;
    if (auto clicked = drawMarkdown(ui.painter(), st.text, options)) followLink(ui, *clicked);
    ImGui::Spacing();
    if (progress_.result() == learn::LessonProgress::Result::Done) ImGui::TextColored(kGood, "Lesson complete.");
    else if (st.done && progress_.completed(step)) ImGui::TextColored(kGood, "Done.");
    else if (st.done) dimWrapped("Next lights up once you have done this.");
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

void LessonRunner::drawPanel(UiContext& ui) {
    if (!panelOpen_) return;
    const Painter p = ui.painter();
    if (!moved_) {
        const ImVec2 size = ui.size(kPanelSize);
        const float gap = ui.px(4);
        ImVec2 at = ui.at({ui.map.left + 6, frameH() - kPanelSize.y - 6});
        if (const UiTag* galaxy = findTag(ui, "panel:galaxy"); galaxy && !windowsOpen_)
            at = ImVec2(galaxy->max.x - size.x, galaxy->max.y - size.y);
        else if (const UiTag* system = findTag(ui, "panel:system"))
            at = ImVec2(system->min.x + gap, system->max.y - size.y - gap);
        ImGui::SetNextWindowPos(at, ImGuiCond_Always);
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
            if (button(last ? "Finish##next" : "Next##next", third + 4, third, y0, progress_.canGoNext())) {
                progress_.goNext(ui.rules(), ui.state(), ui.session.player());
                seen_ = 0;
                if (progress_.result() != learn::LessonProgress::Result::None) finished();
            }
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
        const float half = (inner - 4) / 2;
        if (button("Hide", 0, half, y0 + 31, true)) panelOpen_ = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+H or the T button shows the panel again");
        const char* leave = over ? "Learn" : l.kind == learn::LessonKind::Tutorial ? "Leave Lesson" : "Leave Game";
        if (button(leave, half + 4, half, y0 + 31, true)) {
            if (over) request_ = Request::Leave;
            else leave_.open("Leave the lesson? Its game ends; anything not saved is lost.", "Leave Lesson");
        }
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
