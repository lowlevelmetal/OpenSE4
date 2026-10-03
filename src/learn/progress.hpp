#pragma once

// Where the player is in a lesson or training game, and the rules that move
// it on (docs/LEARNING.md): tutorial steps that finish when their condition
// holds (or on Next), training objectives with deadlines, hints shown once,
// briefing pages at the start of their turn, the fail rule and the result.
// Headless: the client's lesson panel draws it and feeds it the game state,
// the client facts and the player's commands.

#include "learn/condition.hpp"
#include "learn/lesson.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace opense4::learn {

class LessonProgress {
public:
    enum class Result : uint8_t { None, Done, Won, Lost };

    // Starts counting from the game as it is now.
    LessonProgress(Lesson lesson, const game::Rules& rules, const game::GameState& state, game::EmpireId empire);

    const Lesson& lesson() const { return lesson_; }
    const Tracker& tracker() const { return tracker_; }

    // A command the player issued successfully.
    void issued(const game::Command& c) { tracker_.issued(c); }

    // What an update changed, for the panel.
    struct Changes {
        bool stepChanged = false;   // a step's condition held: the next step is shown (or the lesson is done)
        bool pageShown = false;     // a briefing page became due
        bool hintShown = false;
        bool finished = false;
    };
    // Checks the conditions against the game and the client facts. Call it
    // whenever one of them may have changed.
    Changes update(const game::Rules& rules, const game::GameState& state, game::EmpireId empire, const ClientFacts& client);

    // ---- Tutorials ----
    // The step shown, and the active step: the furthest one reached, whose
    // condition is checked and whose inputs the lock allows. Back shows an
    // earlier step for reading; the active one stays where it is.
    size_t step() const { return step_; }
    size_t active() const { return frontier_; }
    bool completed(size_t step) const { return step < completed_.size() && completed_[step] != 0; }
    // Next is open on an earlier step (back to the active one), and on the
    // active step when it has no condition or its condition held.
    bool canGoNext() const;
    // Moves on; on the last step this finishes the lesson. Returns false if Next is not open.
    bool goNext(const game::Rules& rules, const game::GameState& state, game::EmpireId empire);
    bool canGoBack() const { return step_ > 0; }
    void goBack();
    // Gives up on the active step (its condition cannot be met any more) and
    // moves on as if it were done.
    void skip(const game::Rules& rules, const game::GameState& state, game::EmpireId empire);
    // Shows a step as if the ones before it were done (checking content).
    void jumpTo(size_t step, const game::Rules& rules, const game::GameState& state, game::EmpireId empire);
    // How far the active step has come, while it waits for something that
    // can be counted (systems explored, turns, items): its condition's
    // counters (learn::counters), then its `progress` facts once they are
    // above 0. Empty for a step without a condition, one that is done, and
    // while an earlier step is shown.
    std::vector<Counter> counters(const game::Rules& rules, const game::GameState& state, game::EmpireId empire,
                                  const ClientFacts& client) const;

    // ---- Training games ----
    bool objectiveDone(size_t i) const { return i < objectiveDone_.size() && objectiveDone_[i] != 0; }
    bool objectiveFailed(size_t i) const { return i < objectiveFailed_.size() && objectiveFailed_[i] != 0; }
    // The oldest hint not yet dismissed.
    std::optional<size_t> hint() const { return hints_.empty() ? std::nullopt : std::optional(hints_.front()); }
    void dismissHint() {
        if (!hints_.empty()) hints_.pop_front();
    }
    // The briefing page shown, and the pages of its series that have come
    // up so far (previous and next browse them).
    std::optional<size_t> page() const { return page_; }
    std::vector<size_t> series() const;
    void showPage(std::optional<size_t> page) { page_ = page; }

    Result result() const { return result_; }
    // Why it ended: the fail rule's text, the objective whose deadline passed, ...
    const std::string& why() const { return why_; }

private:
    void enter(size_t step, const game::Rules& rules, const game::GameState& state, game::EmpireId empire);
    void finish(Result r, std::string why);

    Lesson lesson_;
    Tracker tracker_;
    Mark gameMark_;

    size_t step_ = 0;
    size_t frontier_ = 0;
    std::vector<std::optional<Mark>> stepMarks_;
    std::vector<uint8_t> completed_;

    std::vector<uint8_t> objectiveDone_;
    std::vector<uint8_t> objectiveFailed_;
    std::vector<uint8_t> hintShown_;
    std::deque<size_t> hints_;
    std::vector<uint8_t> pageSeen_;
    std::optional<size_t> page_;
    std::optional<uint32_t> lastTurn_;
    uint64_t selections_ = 0;   // ClientFacts::selections at the last update (a new step's mark)
    size_t battleOrders_ = 0;   // and the size of ClientFacts::battleOrders

    Result result_ = Result::None;
    std::string why_;
};

} // namespace opense4::learn
