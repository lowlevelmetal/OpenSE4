#include "learn/progress.hpp"

#include <algorithm>
#include <format>

namespace opense4::learn {

namespace {

// formatDate of the client: the game date of a turn ("2403.0").
std::string dateOf(uint32_t turn) { return std::format("{}.{}", 2400 + turn / 10, turn % 10); }

// The text of blocks on one line.
std::string oneLine(const std::vector<Block>& blocks) {
    std::string out = plainText(blocks);
    for (char& c : out)
        if (c == '\n') c = ' ';
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

} // namespace

LessonProgress::LessonProgress(Lesson lesson, const game::Rules& rules, const game::GameState& state, game::EmpireId empire)
    : lesson_(std::move(lesson)) {
    tracker_.observe(state, empire);   // who owns what, and the battles already fought, are not the lesson's
    gameMark_ = markNow(rules, state, empire, tracker_);
    stepMarks_.resize(lesson_.steps.size());
    completed_.assign(lesson_.steps.size(), 0);
    if (!stepMarks_.empty()) stepMarks_.front() = gameMark_;
    objectiveDone_.assign(lesson_.objectives.size(), 0);
    objectiveFailed_.assign(lesson_.objectives.size(), 0);
    hintShown_.assign(lesson_.hints.size(), 0);
    pageSeen_.assign(lesson_.pages.size(), 0);
}

void LessonProgress::enter(size_t step, const game::Rules& rules, const game::GameState& state, game::EmpireId empire) {
    step_ = step;
    // A step counts commands, turns and research from the first time it is shown.
    if (!stepMarks_[step]) stepMarks_[step] = markNow(rules, state, empire, tracker_, selections_);
}

void LessonProgress::finish(Result r, std::string why) {
    result_ = r;
    why_ = std::move(why);
}

bool LessonProgress::canGoNext() const {
    if (step_ >= lesson_.steps.size() || result_ != Result::None) return false;
    return !lesson_.steps[step_].done || completed_[step_];
}

bool LessonProgress::goNext(const game::Rules& rules, const game::GameState& state, game::EmpireId empire) {
    if (!canGoNext()) return false;
    completed_[step_] = 1;
    if (step_ + 1 < lesson_.steps.size()) enter(step_ + 1, rules, state, empire);
    else finish(Result::Done, {});
    return true;
}

void LessonProgress::goBack() {
    if (step_ > 0) --step_;
}

void LessonProgress::jumpTo(size_t step, const game::Rules& rules, const game::GameState& state, game::EmpireId empire) {
    if (lesson_.steps.empty()) return;
    step = std::min(step, lesson_.steps.size() - 1);
    for (size_t i = 0; i < step; ++i) completed_[i] = 1;
    enter(step, rules, state, empire);
}

std::vector<size_t> LessonProgress::series() const {
    std::vector<size_t> out;
    if (!page_) return out;
    for (size_t i = 0; i < lesson_.pages.size(); ++i)
        if (pageSeen_[i] && lesson_.pages[i].series == lesson_.pages[*page_].series) out.push_back(i);
    return out;
}

LessonProgress::Changes LessonProgress::update(const game::Rules& rules, const game::GameState& state, game::EmpireId empire,
                                               const ClientFacts& client) {
    Changes ch;
    tracker_.observe(state, empire);
    selections_ = client.selections;
    if (result_ != Result::None) return ch;

    if (lesson_.kind == LessonKind::Tutorial) {
        if (step_ >= lesson_.steps.size()) return ch;
        const Step& st = lesson_.steps[step_];
        if (completed_[step_] || !st.done) return ch;
        if (!holds(*st.done, EvalContext{rules, state, empire, client, tracker_, *stepMarks_[step_]})) return ch;
        // Done: on to the next step (or the end) at once.
        completed_[step_] = 1;
        ch.stepChanged = true;
        if (step_ + 1 < lesson_.steps.size()) {
            enter(step_ + 1, rules, state, empire);
        } else {
            finish(Result::Done, {});
            ch.finished = true;
        }
        return ch;
    }

    const EvalContext ctx{rules, state, empire, client, tracker_, gameMark_};
    const uint32_t turn = state.turn;
    // Briefing pages come up at the start of their turn (the first of them is shown).
    if (!lastTurn_ || *lastTurn_ != turn) {
        lastTurn_ = turn;
        for (size_t i = 0; i < lesson_.pages.size(); ++i) {
            if (pageSeen_[i] || lesson_.pages[i].turn > turn) continue;
            pageSeen_[i] = 1;
            if (!ch.pageShown) {
                page_ = i;
                ch.pageShown = true;
            }
        }
    }
    // An objective counts once it held; one that has not by its turn fails.
    for (size_t i = 0; i < lesson_.objectives.size(); ++i) {
        if (objectiveDone_[i] || objectiveFailed_[i]) continue;
        const Objective& o = lesson_.objectives[i];
        if (holds(o.when, ctx)) objectiveDone_[i] = 1;
        else if (o.byTurn && turn > *o.byTurn) objectiveFailed_[i] = 1;
    }
    for (size_t i = 0; i < lesson_.hints.size(); ++i)
        if (!hintShown_[i] && holds(lesson_.hints[i].when, ctx)) {
            hintShown_[i] = 1;
            hints_.push_back(i);
            ch.hintShown = true;
        }
    if (lesson_.fail && holds(lesson_.fail->when, ctx)) {
        finish(Result::Lost, oneLine(lesson_.fail->text));
    } else if (const auto failed = std::find(objectiveFailed_.begin(), objectiveFailed_.end(), uint8_t{1}); failed != objectiveFailed_.end()) {
        const Objective& o = lesson_.objectives[static_cast<size_t>(failed - objectiveFailed_.begin())];
        finish(Result::Lost, std::format("\"{}\" was not done by {}.", o.text, dateOf(*o.byTurn)));
    } else if (std::all_of(objectiveDone_.begin(), objectiveDone_.end(), [](uint8_t d) { return d != 0; })) {
        finish(Result::Won, std::format("Every objective was met by {}.", dateOf(turn)));
    }
    ch.finished = result_ != Result::None;
    return ch;
}

} // namespace opense4::learn
