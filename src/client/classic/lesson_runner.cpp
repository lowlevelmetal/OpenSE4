#include "client/classic/lesson_runner.hpp"

#include "client/app_settings.hpp"
#include "client/audio.hpp"
#include "client/classic/learn_content.hpp"
#include "client/classic/screen_id.hpp"
#include "client/classic/screens/markdown_view.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"
#include "core/hash.hpp"

#include <imgui_internal.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
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

// How long a step's targets may be missing from the screen with no way back
// to them, and how long a step that waits on a click may last, before Next
// offers to skip it (seconds of play). A step that waits on the turns or a
// battle is never timed (waitsOnGame).
constexpr double kTargetsGoneSkip = 10.0;
constexpr double kStepSkip = 120.0;
// How long the note after a refused click or key shows, and how many
// refusals in how many seconds bring up Back, Skip and Free Play in it.
constexpr double kRefusedHint = 3.0;
constexpr double kRefusedHintLong = 5.0;
constexpr size_t kStuckRefusals = 3;
constexpr double kStuckWindow = 15.0;
// A refused click flashes the outlines white twice, at 2 Hz.
constexpr double kFlashTime = 1.0;
constexpr double kFlashPeriod = 0.5;
// The outlines: the step's pulse in amber, the way back dashed in cyan.
constexpr uint32_t kOutlineColor = 0xffd040;
constexpr uint32_t kRecoveryColor = 0x50d8ff;

// Dims everything the lock does not let the pointer use (the spotlight of
// the input lock): the screen is cut into a grid at the edges of every
// rectangle the lock knows, so each cell lies wholly inside or outside each of
// them, and every cell the lock refuses is filled, row by row. Windows stacked
// over each other dim as the lock decides: the front-most one under a cell.
void spotlight(ImDrawList* dl, const LockState& lock, ImVec2 size, ImU32 color) {
    std::vector<float> xs{0.0f, size.x}, ys{0.0f, size.y};
    for (const LockArea& a : lock.rects()) {
        xs.push_back(std::clamp(a.min.x, 0.0f, size.x));
        xs.push_back(std::clamp(a.max.x, 0.0f, size.x));
        ys.push_back(std::clamp(a.min.y, 0.0f, size.y));
        ys.push_back(std::clamp(a.max.y, 0.0f, size.y));
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    auto open = [&](float x, float y) { return lock.allows({x, y}) || lock.looks({x, y}); };
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

// A rectangle of dashes (the way back's outline). `extend` lengthens each
// dash at both ends (the dark edge drawn under it).
void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float thick, float dash, float gap, float extend) {
    const ImVec2 corners[5] = {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y), a};
    for (size_t side = 0; side < 4; ++side) {
        const ImVec2 p = corners[side], q = corners[side + 1];
        const float len = std::hypot(q.x - p.x, q.y - p.y);
        if (len <= 0.0f) continue;
        const ImVec2 d((q.x - p.x) / len, (q.y - p.y) / len);
        for (float at = 0.0f; at < len; at += dash + gap) {
            const float from = std::max(0.0f, at - extend), to = std::min(len, at + dash + extend);
            dl->AddLine(ImVec2(p.x + d.x * from, p.y + d.y * from), ImVec2(p.x + d.x * to, p.y + d.y * to), color, thick);
        }
    }
}

// A window's title as Dear ImGui names it ("Finale###finale" shows "Finale").
std::string shownTitle(const char* name) {
    std::string_view n(name ? name : "");
    if (const size_t hashes = n.find("##"); hashes != std::string_view::npos) n = n.substr(0, hashes);
    return std::string(n);
}

// "build-queue" -> "Build Queue".
std::string titleCase(std::string_view id) {
    std::string out;
    bool start = true;
    for (const char c : id) {
        if (c == '-') {
            out += ' ';
            start = true;
            continue;
        }
        out += start && c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
        start = false;
    }
    return out;
}

// The keys bound to an action, "F3" or "Ctrl+H or F9"; empty when none is.
std::string keysOf(Action a) {
    std::string out;
    for (const KeyChord& c : appSettings().controls.bindings.chords(a))
        if (!c.empty()) out += (out.empty() ? "" : " or ") + chordName(c);
    return out;
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
    updateRecovery(ui, facts);
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

void LessonRunner::refused(std::optional<ImVec2> where, double time, std::string key) {
    refusedAt_ = where;
    refusedKey_ = std::move(key);
    refusedTime_ = time;
    refusals_.push_back(time);
    while (!refusals_.empty() && time - refusals_.front() > kStuckWindow) refusals_.pop_front();
}

bool LessonRunner::stuck(const UiContext& ui) const {
    const learn::Step* st = activeStep();
    if (!st || !st->done || progress_.completed(progress_.active())) return false;
    // While there is a way back to the step's window, the lesson shows it instead.
    if (recovery_.kind != Recovery::Kind::None) return false;
    const bool targets = std::any_of(st->highlight.begin(), st->highlight.end(), [](const std::string& t) { return !t.starts_with("lesson:"); });
    if (targets && ui.time - targetsSeen_ > kTargetsGoneSkip) return true;
    // A step that waits on the turns or a battle is going as it should, however long it takes.
    return !waitsOnGame(*st->done) && ui.time - activeSince_ > kStepSkip;
}

std::vector<std::string> windowsBackToFront(const UiContext& ui, std::vector<std::string> open) {
    // Dear ImGui keeps its windows in display order, back to front.
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    auto place = [&](const std::string& id) {
        const std::string tag = "window:" + id;
        for (const UiTag& t : ui.tags)
            if (t.name == tag && t.window)
                for (int i = 0; i < g.Windows.Size; ++i)
                    if (g.Windows[i] == t.window->RootWindow) return i;
        return -1;   // not drawn with its tag: behind the others
    };
    std::vector<std::pair<int, std::string>> keyed;
    keyed.reserve(open.size());
    for (std::string& id : open) {
        const int at = place(id);
        keyed.emplace_back(at, std::move(id));
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    out.reserve(keyed.size());
    for (auto& [at, id] : keyed) out.push_back(std::move(id));
    return out;
}

void LessonRunner::updateRecovery(const UiContext& ui, const learn::ClientFacts& facts) {
    // The windows' titles as shown, for a hint about one once it is closed.
    for (const UiTag& t : ui.tags)
        if (t.window && t.name.starts_with("window:")) titles_[t.name.substr(7)] = shownTitle(t.window->Name);
    const learn::Step* st = lesson().kind == learn::LessonKind::Tutorial ? activeStep() : nullptr;
    Recovery r;
    std::vector<std::string> windows;
    if (st && !progress_.completed(progress_.active()) && progress_.result() == learn::LessonProgress::Result::None) {
        std::vector<TaggedArea> tags;
        tags.reserve(ui.tags.size());
        for (const UiTag& t : ui.tags) tags.push_back({t.name, {t.min, t.max}});
        windows = windowsBackToFront(ui, facts.openWindows);
        r = findRecovery(*st, tags, windows);
        // Its targets on screen, or a way back to them: Skip waits.
        bool seen = r.kind != Recovery::Kind::None;
        for (const std::string& tag : st->highlight)
            if (!tag.starts_with("lesson:") && findTag(ui, tag)) seen = true;
        if (seen) targetsSeen_ = ui.time;
    }
    recovery_ = std::move(r);
    std::string hint = describe(recovery_);
    if (recovery_.kind == Recovery::Kind::Uncover && !windows.empty() && windows.back() == recovery_.window) hint += " (Esc)";
    if (!hint.empty()) hint += ".";
    if (hint != recoveryHint_) {
        recoveryHint_ = std::move(hint);
        recoveryBlocks_ = recoveryHint_.empty() ? std::vector<learn::Block>{} : learn::parseMarkdown(recoveryHint_, {}, false).blocks;
    }
}

std::string LessonRunner::describe(const Recovery& r) const {
    auto title = [&](std::string_view id) {
        if (const auto it = titles_.find(std::string(id)); it != titles_.end() && !it->second.empty()) return it->second;
        if (const auto screen = screenFromWindowId(id)) return std::string(screenTitle(*screen));
        return std::string(id);
    };
    // What to press, as the player sees it: the verb, and the button ("the
    // **Designs** button (F3)", "**Create** in Designs", "the **Build Queue**
    // order"); `in` names the window a button inside a window is in.
    struct Press {
        std::string verb, what;
    };
    auto press = [&](std::string_view tag, bool in) -> Press {
        const size_t colon = tag.find(':');
        const std::string_view kind = tag.substr(0, colon), id = colon == std::string_view::npos ? std::string_view{} : tag.substr(colon + 1);
        if (kind == "command") {
            const std::vector<Action> actions = tagActions(tag);
            const std::string keys = actions.empty() ? std::string{} : keysOf(actions.front());
            return {"Press", std::format("the **{}** button{}", title(id), keys.empty() ? "" : " (" + keys + ")")};
        }
        if (kind == "order") return {"Press", std::format("the **{}** order", titleCase(id))};
        if (tag == "panel:galaxy") return {"Right-click", "the galaxy panel"};
        const std::string where = in ? " in " + title(kind) : std::string{};
        if (id == "list") return {"Click", "a line of the list" + where};
        return {"Press", std::format("**{}**{}", titleCase(id), where)};
    };
    switch (r.kind) {
        case Recovery::Kind::None: return {};
        case Recovery::Kind::Uncover: return std::format("Close the {} window first", title(r.window));
        case Recovery::Kind::Reopen: {
            const Press first = press(r.press, true);
            std::string how = first.verb + " " + first.what;
            if (!r.then.empty()) how += ", then " + press(r.then, false).what + ",";
            return std::format("The {} window was closed. {} to open it again", title(r.window), how);
        }
    }
    return {};
}

std::string LessonRunner::recoveryPlain() const {
    std::string text = learn::plainText(recoveryBlocks_);
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    return text;
}

void LessonRunner::drawRecoveryHint(UiContext& ui) {
    // The way back, marked with a bar in the colour of its outline.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::Indent(ui.px(9));
    MarkdownOptions options;
    if (auto clicked = drawMarkdown(ui.painter(), recoveryBlocks_, options)) followLink(ui, *clicked);
    ImGui::Unindent(ui.px(9));
    const float end = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;
    dl->AddRectFilled(start, ImVec2(start.x + ui.px(3), std::max(start.y + ui.px(4), end)), imColor(kRecoveryColor));
    if (script::collectingItems())   // input scripts: item:"hint:<the text>"
        script::reportItem("hint:" + recoveryPlain(), start, ImVec2(start.x + ImGui::GetContentRegionAvail().x, end));
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
    const bool recover = !recovery_.press.empty();
    if (!outline && !recover && !lock.active) return;
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
    if (ImGuiWindow* panel = ImGui::FindWindowByName("##lessonpanel"); panel && panelOpen_) ImGui::BringWindowToDisplayFront(panel);
    // A refused click or key makes the outlines flash white, twice (2 Hz).
    const double sinceRefused = ui.time - refusedTime_;
    const bool flash = sinceRefused >= 0 && sinceRefused < kFlashTime && std::fmod(sinceRefused, kFlashPeriod) < kFlashPeriod * 0.5;
    const float thick = std::max(2.0f, ui.px(flash ? 4.0f : 2.5f));
    const float edge = std::max(1.0f, ui.px(1.0f));   // the dark edge on either side of the line
    const float pad = ui.px(3);
    // Everything the step does not let the player use is dimmed. The clear
    // areas reach round the outlines, which lie just outside their parts.
    if (lock.active) {
        const float ring = pad + std::max(2.0f, ui.px(4.0f)) + edge;
        spotlight(under, lock.grown(ring), display, IM_COL32(0, 0, 0, 140));
    }
    const ImU32 dark = IM_COL32(0, 0, 0, 230);
    auto around = [&](const std::string& tag, auto&& draw) {
        for (const UiTag& t : ui.tags) {
            if (t.name != tag) continue;
            // A part drawn outside every window lies on the map, under all of them.
            ImDrawList* dl = t.window ? t.window->DrawList : ImGui::GetBackgroundDrawList();
            dl->PushClipRect(ImVec2(0, 0), display, false);
            dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
            draw(dl, ImVec2(t.min.x - pad, t.min.y - pad), ImVec2(t.max.x + pad, t.max.y + pad));
            dl->PopTexture();
            dl->PopClipRect();
        }
    };
    if (outline) {
        // The step's outlines pulse between 55 % and full strength, an amber
        // line with a thin dark edge either side that sets it apart from the
        // game's own amber selection frames.
        const float pulse = 0.775f + 0.225f * std::sin(float(ui.time) * 5.0f);
        const ImU32 color = flash ? IM_COL32_WHITE : imColor(kOutlineColor, pulse);
        for (const std::string& tag : st->highlight)
            around(tag, [&](ImDrawList* dl, ImVec2 a, ImVec2 b) {
                dl->AddRect(a, b, dark, 0.0f, thick + 2 * edge);
                dl->AddRect(a, b, color, 0.0f, thick);
            });
    }
    if (recover) {
        // The way back to the step's window: dashed, in cyan, and steady.
        const ImU32 color = flash ? IM_COL32_WHITE : imColor(kRecoveryColor);
        const float dash = ui.px(7), gap = ui.px(4);
        around(recovery_.press, [&](ImDrawList* dl, ImVec2 a, ImVec2 b) {
            dashedRect(dl, a, b, dark, thick + 2 * edge, dash, gap, edge);
            dashedRect(dl, a, b, color, thick, dash, gap, 0.0f);
            script::reportItem("recovery:" + recovery_.press, a, b);   // input scripts: item:recovery:<tag>
        });
    }
    drawRefusedNote(ui, *st, sinceRefused);
    ImGui::End();   // ##lessonoutlines (the items reported for scripts lie in it)
}

void LessonRunner::drawRefusedNote(UiContext& ui, const learn::Step& st, double since) const {
    // A word where the player clicked (or by the panel, for a key): what the
    // step waits for, the way back when there is one, how to show the panel
    // when it is hidden, and after a few refusals in a short while, the panel's
    // ways out.
    if (!refusedAt_ && refusedKey_.empty()) return;
    const bool stuckLines = refusals_.size() >= kStuckRefusals;
    const double shows = stuckLines ? kRefusedHintLong : kRefusedHint;
    if (since < 0 || since >= shows) return;
    std::string text;
    if (!refusedKey_.empty()) text = std::format("This step does not use {}. ", refusedKey_);
    if (!st.done) text += "This step explains: press Next in the lesson panel.";
    else if (!recoveryBlocks_.empty()) text += recoveryPlain();
    else text += "Click the pulsing yellow outline.";
    if (!panelOpen_) {
        const std::string keys = keysOf(Action::LessonText);
        text += std::format("\nThe lesson panel is hidden: {}the T button shows it.", keys.empty() ? "" : keys + " or ");
    }
    if (stuckLines)
        text += "\nStuck? In the lesson panel, Back shows the steps before, Skip appears when a step cannot be done any more, "
                "and Free Play unlocks the whole game.";
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float wrap = std::min(display.x * 0.5f, ui.px(380));
    const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
    const ImVec2 pad(ui.px(6), ui.px(4));
    const ImVec2 box(textSize.x + 2 * pad.x, textSize.y + 2 * pad.y);
    // By the click; for a key, just above the panel, or under the T button when the panel is hidden.
    ImVec2 at(display.x * 0.5f - box.x * 0.5f, display.y * 0.3f);
    if (refusedAt_) {
        at = ImVec2(refusedAt_->x + ui.px(14), refusedAt_->y + ui.px(10));
    } else if (const UiTag* panel = findTag(ui, "lesson:panel"); panel && panelOpen_) {
        at = ImVec2(panel->min.x, panel->min.y - box.y - ui.px(6));
    } else if (const UiTag* t = findTag(ui, "status:lesson")) {
        at = ImVec2(t->max.x - box.x, t->max.y + ui.px(8));
    }
    at.x = std::clamp(at.x, 0.0f, std::max(0.0f, display.x - box.x));
    at.y = std::clamp(at.y, 0.0f, std::max(0.0f, display.y - box.y));
    const float alpha = float(std::min(1.0, (shows - since) * 2.0));
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(at, ImVec2(at.x + box.x, at.y + box.y), imColor(0x101c40, 0.95f * alpha));
    fg->AddRect(at, ImVec2(at.x + box.x, at.y + box.y), imColor(!st.done || recoveryBlocks_.empty() ? kOutlineColor : kRecoveryColor, alpha));
    fg->AddText(font, size, ImVec2(at.x + pad.x, at.y + pad.y), imColor(0xffffff, alpha), text.c_str(), nullptr, wrap);
    if (script::collectingItems()) {   // input scripts: item:"note:<the text>" (its lines joined by spaces)
        std::string label = "note:" + text;
        std::replace(label.begin(), label.end(), '\n', ' ');
        script::reportItem(label, at, ImVec2(at.x + box.x, at.y + box.y));
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
    else if (step == progress_.active() && !recoveryHint_.empty()) drawRecoveryHint(ui);   // the way back (recoveryHint())
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
