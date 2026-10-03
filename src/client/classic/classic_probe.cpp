// What input scripts see of the classic client (script::Probe; docs/BUILDING.md
// "Input scripts"): the UI tags and widgets of the frame drawn last, the main
// window's sectors and systems, the windows, the lesson and the game.

#include "client/classic/classic_mode.hpp"

#include <format>

namespace opense4::client {

using namespace classic;

void ClassicMode::trackForScripts() {
    if (!session_ || session_->revision() == trackedRevision_) return;
    trackedRevision_ = session_->revision();
    scriptTracker_.observe(session_->state(), session_->player());
}

std::vector<script::Box> ClassicMode::tagBoxes(std::string_view name) const {
    std::vector<script::Box> out;
    if (!ui_) return out;
    for (const UiTag& t : ui_->tags)
        if (t.name == name) out.push_back({t.min, t.max});
    return out;
}

std::vector<std::string> ClassicMode::tagNames() const {
    std::vector<std::string> out;
    if (!ui_) return out;
    for (const UiTag& t : ui_->tags)
        if (std::find(out.begin(), out.end(), t.name) == out.end()) out.push_back(t.name);
    return out;
}

ImVec2 ClassicMode::framePoint(float x, float y) const {
    const Vec2 p = mapping_.toFb({x, y}) / fbScale_;
    return {p.x, p.y};
}

float ClassicMode::frameScale() const { return mapping_.scale / fbScale_; }

std::vector<script::Box> ClassicMode::sectors(const script::Target& t, std::string& error) const {
    std::vector<script::Box> out;
    if (!session_ || !ui_) {
        error = "no game is running";
        return out;
    }
    for (const Rect& r : main_.findSectors(*ui_, t.name, error)) out.push_back({framePoint(r.min.x, r.min.y), framePoint(r.max.x, r.max.y)});
    return out;
}

std::vector<script::Box> ClassicMode::systems(const script::Target& t, std::string& error) const {
    std::vector<script::Box> out;
    if (!session_ || !ui_) {
        error = "no game is running";
        return out;
    }
    const float half = std::max(1.0f, main_.galaxyCellSize() * 0.5f);
    for (const Vec2& c : main_.findSystems(*ui_, t.name, error))
        out.push_back({framePoint(c.x - half, c.y - half), framePoint(c.x + half, c.y + half)});
    return out;
}

std::string ClassicMode::targetAt(ImVec2 p) const {
    if (!session_ || !ui_ || !screens_.empty()) return {};
    const Vec2 f = mapping_.fromFb(Vec2{p.x, p.y} * fbScale_);
    if (const auto sec = main_.sectorAtFrame(f)) return std::format("sector:{},{}", sec->x, sec->y);
    if (const auto sys = main_.systemAtFrame(*ui_, f)) return std::format("system:{}", sys->index());
    return {};
}

std::vector<std::string> ClassicMode::openWindows() const {
    std::vector<std::string> out;
    for (const auto& [id, screen] : screens_) out.emplace_back(windowId(id));
    return out;
}

std::optional<script::LessonInfo> ClassicMode::lesson() const {
    if (!lesson_) return std::nullopt;
    script::LessonInfo l;
    const learn::Lesson& lesson = lesson_->lesson();
    l.slug = lesson.slug;
    l.tutorial = lesson.kind == learn::LessonKind::Tutorial;
    l.steps = lesson.steps.size();
    l.step = lesson_->progress().step() + 1;
    l.active = lesson_->progress().active() + 1;
    using Result = learn::LessonProgress::Result;
    switch (lesson_->progress().result()) {
        case Result::None: l.result = "none"; break;
        case Result::Done: l.result = "done"; break;
        case Result::Won: l.result = "won"; break;
        case Result::Lost: l.result = "lost"; break;
    }
    l.locked = lock_.active();
    return l;
}

std::optional<uint32_t> ClassicMode::turn() const {
    if (!session_) return std::nullopt;
    return session_->state().turn;
}

std::vector<std::string> ClassicMode::logLines() const {
    std::vector<std::string> out;
    if (!session_) return out;
    for (const game::LogEntry& e : session_->me().log) out.push_back(e.text.empty() ? e.title : e.title + ": " + e.text);
    return out;
}

bool ClassicMode::typing() const { return ImGui::GetIO().WantTextInput; }

std::string ClassicMode::lockDescription() const {
    const LockState& l = lock_.state();
    if (!l.active) return "off";
    std::string keys;
    for (const KeyChord& k : l.keys) keys += (keys.empty() ? "" : " ") + chordName(k);
    return std::format("on: {} areas, {} to look at; keys: {}{}{}{}", l.areas.size(), l.lookAreas.size(), keys.empty() ? "none" : keys,
                       l.windowKeys ? "; Esc and Enter for the window in front" : "", l.prompt ? "; a prompt's keys" : "", l.typing ? "; typing" : "");
}

std::optional<bool> ClassicMode::holds(const learn::Condition& c, const learn::Mark& since, std::string& error) const {
    if (!session_) {
        error = "no game is running";
        return std::nullopt;
    }
    const learn::EvalContext ctx{session_->rules(), session_->state(), session_->player(), lastFacts_, scriptTracker_, since};
    return learn::holds(c, ctx);
}

std::optional<int64_t> ClassicMode::factValue(learn::Fact f, const learn::Mark& since) const {
    if (!session_) return std::nullopt;
    const learn::EvalContext ctx{session_->rules(), session_->state(), session_->player(), lastFacts_, scriptTracker_, since};
    return learn::factValue(f, ctx);
}

learn::Mark ClassicMode::mark(bool gameStart) const {
    if (!session_) return {};
    if (gameStart) return gameMark_;
    return learn::markNow(session_->rules(), session_->state(), session_->player(), scriptTracker_, lastFacts_.selections,
                          lastFacts_.battleOrders.size());
}

} // namespace opense4::client
