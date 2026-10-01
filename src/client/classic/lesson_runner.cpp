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

// The panel: a classic window at the bottom left of the system view, where it
// hides the least of the classic windows (their buttons are on the right). Movable.
constexpr Vec2 kPanelSize{330, 300};
constexpr float kPanelTop = 462;
constexpr float kButtonsH = 66.0f;

const ImVec4 kGood{0.45f, 0.9f, 0.45f, 1.0f};
const ImVec4 kBad{1.0f, 0.5f, 0.42f, 1.0f};
const ImVec4 kGold{1.0f, 0.85f, 0.45f, 1.0f};

void dimWrapped(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(imColorV(palette::kSecondary), "%s", text);
    ImGui::PopTextWrapPos();
}

} // namespace

LessonRunner::LessonRunner(learn::Lesson lesson, const ClassicSession& session) : lesson_(std::move(lesson)) {
    gameMark_ = learn::markNow(session.rules(), session.state(), session.player(), tracker_);
    stepMarks_.resize(lesson_.steps.size());
    completed_.assign(lesson_.steps.size(), 0);
    if (!stepMarks_.empty()) stepMarks_.front() = gameMark_;
    objectiveDone_.assign(lesson_.objectives.size(), 0);
    objectiveFailed_.assign(lesson_.objectives.size(), 0);
    hintShown_.assign(lesson_.hints.size(), 0);
    pageSeen_.assign(lesson_.pages.size(), 0);
}

learn::EvalContext LessonRunner::context(const UiContext& ui, const learn::ClientFacts& facts, const learn::Mark& mark) const {
    return learn::EvalContext{ui.rules(), ui.state(), ui.session.player(), facts, tracker_, mark};
}

void LessonRunner::frame(UiContext& ui, const learn::ClientFacts& facts) {
    evaluate(ui, facts);
    drawPanel(ui);
    drawOutlines(ui);
    drawResult(ui);
}

void LessonRunner::enterStep(const UiContext& ui, size_t step) {
    step_ = step;
    if (!stepMarks_[step]) stepMarks_[step] = learn::markNow(ui.rules(), ui.state(), ui.session.player(), tracker_);
    panelOpen_ = true;
    seenSignature_ = 0;
}

void LessonRunner::jumpTo(const UiContext& ui, size_t step) {
    if (lesson_.steps.empty()) return;
    step = std::min(step, lesson_.steps.size() - 1);
    for (size_t i = 0; i < step; ++i) completed_[i] = 1;
    enterStep(ui, step);
}

void LessonRunner::finish(Result r, std::string why) {
    result_ = r;
    resultWhy_ = std::move(why);
    showResult_ = true;
    panelOpen_ = true;
    if (r == Result::Done || r == Result::Won) markLessonDone(lesson_.kind, lesson_.slug);
}

void LessonRunner::evaluate(UiContext& ui, const learn::ClientFacts& facts) {
    const ClassicSession& session = ui.session;
    if (session.revision() != seenRevision_) {
        tracker_.observe(session.state(), session.player());
        seenRevision_ = session.revision();
    }
    // The conditions read the game, the commands and the client facts: check
    // them again only when one of these changed.
    Hasher h;
    h.add(session.revision()).add(tracker_.commands().size()).add(step_);
    for (const std::string& w : facts.openWindows) h.add(std::string_view(w));
    h.add(std::string_view("|"));
    for (const std::string& k : facts.selected) h.add(std::string_view(k));
    if (h.value() == seenSignature_ || result_ != Result::None) return;
    seenSignature_ = h.value();

    if (lesson_.kind == learn::LessonKind::Tutorial) {
        if (step_ >= lesson_.steps.size()) return;
        const learn::Step& st = lesson_.steps[step_];
        if (completed_[step_] || !st.done || !learn::holds(*st.done, context(ui, facts, *stepMarks_[step_]))) return;
        // Done: on to the next step (or the end) at once.
        completed_[step_] = 1;
        audio().play("button");
        if (step_ + 1 < lesson_.steps.size()) enterStep(ui, step_ + 1);
        else finish(Result::Done);
        return;
    }

    const learn::EvalContext ctx = context(ui, facts, gameMark_);
    const uint32_t turn = ui.state().turn;
    // Briefing pages appear at the start of their turn.
    if (!lastTurn_ || *lastTurn_ != turn) {
        lastTurn_ = turn;
        bool shown = false;
        for (size_t i = 0; i < lesson_.pages.size(); ++i) {
            if (pageSeen_[i] || lesson_.pages[i].turn > turn) continue;
            pageSeen_[i] = 1;
            if (!shown) {
                page_ = i;
                panelOpen_ = true;
                shown = true;
            }
        }
    }
    for (size_t i = 0; i < lesson_.objectives.size(); ++i) {
        if (objectiveDone_[i] || objectiveFailed_[i]) continue;
        const learn::Objective& o = lesson_.objectives[i];
        if (learn::holds(o.when, ctx)) objectiveDone_[i] = 1;
        else if (o.byTurn && turn > *o.byTurn) objectiveFailed_[i] = 1;
    }
    for (size_t i = 0; i < lesson_.hints.size(); ++i)
        if (!hintShown_[i] && learn::holds(lesson_.hints[i].when, ctx)) {
            hintShown_[i] = 1;
            hints_.push_back(i);
            panelOpen_ = true;
        }
    if (lesson_.fail && learn::holds(lesson_.fail->when, ctx)) {
        finish(Result::Lost, learn::plainText(lesson_.fail->text));
        return;
    }
    for (size_t i = 0; i < lesson_.objectives.size(); ++i)
        if (objectiveFailed_[i]) {
            finish(Result::Lost, std::format("The deadline for \"{}\" has passed.", lesson_.objectives[i].text));
            return;
        }
    if (std::all_of(objectiveDone_.begin(), objectiveDone_.end(), [](uint8_t d) { return d != 0; }))
        finish(Result::Won, std::format("Every objective was met by {}.", formatDate(turn)));
}

void LessonRunner::drawOutlines(UiContext& ui) const {
    if (lesson_.kind != learn::LessonKind::Tutorial || result_ != Result::None || step_ >= lesson_.steps.size()) return;
    const learn::Step& st = lesson_.steps[step_];
    if (st.highlight.empty() || completed_[step_]) return;
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
    const size_t n = lesson_.steps.size();
    if (n == 0) return;
    const learn::Step& st = lesson_.steps[step_];
    ImGui::TextColored(kLabelBlue, "Step %zu of %zu", step_ + 1, n);
    heading(ui, st.title.c_str());
    ImGui::Spacing();
    MarkdownOptions options;
    if (auto clicked = drawMarkdown(ui.painter(), st.text, options)) followLink(ui, *clicked);
    ImGui::Spacing();
    if (result_ == Result::Done) ImGui::TextColored(kGood, "Lesson complete.");
    else if (st.done && completed_[step_]) ImGui::TextColored(kGood, "Done.");
    else if (st.done) dimWrapped("Next lights up once you have done this.");
}

void LessonRunner::trainingBody(UiContext& ui) {
    const Painter p = ui.painter();
    if (!hints_.empty()) {
        const learn::Hint& h = lesson_.hints[hints_.front()];
        ImGui::TextColored(kGold, "%s", h.title.c_str());
        MarkdownOptions options;
        if (auto clicked = drawMarkdown(p, h.text, options)) followLink(ui, *clicked);
        if (ImGui::SmallButton("OK")) hints_.pop_front();
        ImGui::Separator();
    }
    ImGui::TextColored(kLabelBlue, "Objectives");
    for (size_t i = 0; i < lesson_.objectives.size(); ++i) {
        const learn::Objective& o = lesson_.objectives[i];
        ImGui::PushID(int(i));
        lamp(ui, objectiveDone_[i] != 0);
        ImGui::SameLine();
        const std::string text = o.byTurn ? std::format("{} (by {})", o.text, formatDate(*o.byTurn)) : o.text;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(objectiveDone_[i] ? kGood : objectiveFailed_[i] ? kBad : ImVec4(1, 1, 1, 1), "%s", text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }
    if (result_ == Result::Won) ImGui::TextColored(kGood, "Training game won.");
    else if (result_ == Result::Lost) ImGui::TextColored(kBad, "Training game lost.");
    if (page_) {
        const learn::BriefingPage& pg = lesson_.pages[*page_];
        ImGui::Separator();
        // Where the page is in its series.
        size_t index = 0, count = 0;
        for (size_t i = 0; i < lesson_.pages.size(); ++i)
            if (lesson_.pages[i].series == pg.series && pageSeen_[i]) {
                ++count;
                if (i <= *page_) index = count;
            }
        heading(ui, pg.title.c_str());
        if (count > 1) ImGui::TextColored(imColorV(palette::kSecondary), "%zu of %zu", index, count);
        ImGui::Spacing();
        MarkdownOptions options;
        if (auto clicked = drawMarkdown(p, pg.text, options)) followLink(ui, *clicked);
    }
}

void LessonRunner::drawPanel(UiContext& ui) {
    if (!panelOpen_) return;
    const Painter p = ui.painter();
    ImGui::SetNextWindowPos(ui.at({ui.map.left + 6, kPanelTop}), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ui.size(kPanelSize), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool open = ImGui::Begin("##lessonpanel", nullptr,
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    if (open) {
        // Above the classic windows, which take the focus when they open.
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        const ImVec2 pos = ImGui::GetWindowPos();
        const Vec2 at = ui.map.fromFb(Vec2{pos.x, pos.y} * ui.fbScale);
        drawWindowFrame(p, ImGui::GetWindowDrawList(), Rect{at, at + kPanelSize}, lesson_.title.c_str(), 0);
        ui.tag("lesson:panel", pos, ImVec2(pos.x + ui.px(kPanelSize.x), pos.y + ui.px(kPanelSize.y)));

        ImGui::SetCursorPos(ui.size({15, 36}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.size({4, 2}));
        ImGui::BeginChild("##body", ui.size({kPanelSize.x - 30, kPanelSize.y - 36 - kButtonsH - 6}), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        if (lesson_.kind == learn::LessonKind::Tutorial) tutorialBody(ui);
        else trainingBody(ui);
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
        if (lesson_.kind == learn::LessonKind::Tutorial) {
            const learn::Step* st = step_ < lesson_.steps.size() ? &lesson_.steps[step_] : nullptr;
            if (button("Back", 0, third, y0, step_ > 0)) step_ -= 1;
            const bool last = step_ + 1 >= lesson_.steps.size();
            const bool canNext = st && (!st->done || completed_[step_]) && !(last && result_ != Result::None);
            if (button(last ? "Finish##next" : "Next##next", third + 4, third, y0, canNext)) {
                completed_[step_] = 1;
                if (!last) enterStep(ui, step_ + 1);
                else finish(Result::Done);
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
            std::vector<size_t> series;
            if (page_)
                for (size_t i = 0; i < lesson_.pages.size(); ++i)
                    if (pageSeen_[i] && lesson_.pages[i].series == lesson_.pages[*page_].series) series.push_back(i);
            const auto shown = page_ ? std::find(series.begin(), series.end(), *page_) : series.end();
            const bool hasPrev = shown != series.end() && shown != series.begin();
            const bool hasNext = shown != series.end() && shown + 1 != series.end();
            if (button("Previous", 0, third, y0, hasPrev)) page_ = *(shown - 1);
            if (button("Next##next", third + 4, third, y0, hasNext)) page_ = *(shown + 1);
            ui.tagItem("lesson:next");
            if (button("Close Page", 2 * (third + 4), third, y0, page_.has_value())) page_.reset();
        }
        const float half = (inner - 4) / 2;
        if (button("Hide", 0, half, y0 + 31, true)) panelOpen_ = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+H or the T button shows the panel again");
        const bool over = result_ != Result::None;
        const char* leave = over ? "Learn" : lesson_.kind == learn::LessonKind::Tutorial ? "Leave Lesson" : "Leave Game";
        if (button(leave, half + 4, half, y0 + 31, true)) {
            if (over) request_ = Request::Leave;
            else confirmLeave_ = true;
        }
    }
    ImGui::End();

    if (confirmLeave_) {
        ImGui::OpenPopup("Leave Lesson");
        confirmLeave_ = false;
    }
    ImGui::SetNextWindowSize(ui.size({380, 0}));
    if (ImGui::BeginPopupModal("Leave Lesson", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Leave the lesson? Its game ends; anything not saved is lost.");
        ImGui::Spacing();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Leave", ImVec2(w, ui.px(26)))) {
            request_ = Request::Leave;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stay", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void LessonRunner::drawResult(UiContext& ui) {
    constexpr const char* kPopup = "##lessonresult";
    if (showResult_) {
        ImGui::OpenPopup(kPopup);
        showResult_ = false;
    }
    ImGui::SetNextWindowSize(ui.size({400, 0}));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    const bool won = result_ == Result::Done || result_ == Result::Won;
    const char* title = result_ == Result::Done ? "Lesson complete" : result_ == Result::Won ? "Training game won" : "Training game lost";
    ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
    ImGui::TextColored(won ? kGood : kBad, "%s", title);
    ImGui::PopFont();
    ImGui::TextUnformatted(lesson_.title.c_str());
    ImGui::Spacing();
    if (!resultWhy_.empty()) ImGui::TextWrapped("%s", resultWhy_.c_str());
    if (won) dimWrapped("The Learn window marks it done. You can keep playing this game.");
    ImGui::Spacing();
    const ImVec2 size(ui.px(118), ui.px(26));
    const learn::Lesson* next = ui.learn ? ui.learn->library.next(lesson_.kind, lesson_.slug) : nullptr;
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
    if (ImGui::Button("Keep Playing", size) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ImGui::Button("Learn", size)) {
        request_ = Request::Leave;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace opense4::client::classic
