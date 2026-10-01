#pragma once

// Runs a tutorial or training game over the game it started
// (docs/LEARNING.md "The lesson panel"): a movable panel with the step (or
// the objectives and the briefing pages), outlines around the UI tags the
// step names, hints, deadlines, the fail rule and the result. It only reads
// the game; everything the player changes still goes through
// ClassicSession::issue(), which reports each command here.

#include "client/classic/ui.hpp"
#include "learn/condition.hpp"
#include "learn/lesson.hpp"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace opense4::client::classic {

class LessonRunner {
public:
    LessonRunner(learn::Lesson lesson, const ClassicSession& session);

    const learn::Lesson& lesson() const { return lesson_; }

    // A command the player issued successfully (ClassicSession::onIssued).
    void issued(const game::Command& c) { tracker_.issued(c); }
    // Each frame, after the windows drew (so this frame's UI tags are known):
    // checks the conditions, draws the outlines, the panel and the result.
    void frame(UiContext& ui, const learn::ClientFacts& facts);

    // Tutorials: shows step `step` (0-based) as if the ones before were done
    // (--tutorial=<slug>:<step>, for checking content).
    void jumpTo(const UiContext& ui, size_t step);

    // Ctrl+H and the status bar's T button.
    void togglePanel() { panelOpen_ = !panelOpen_; }
    bool panelOpen() const { return panelOpen_; }

    // What the player chose in the panel or the result dialog.
    enum class Request : uint8_t { None, Leave, Restart, Next };
    Request takeRequest() {
        const Request r = request_;
        request_ = Request::None;
        return r;
    }

    enum class Result : uint8_t { None, Done, Won, Lost };
    Result result() const { return result_; }

    // Tutorials: the step shown (0-based) and how many there are.
    size_t step() const { return step_; }

private:
    learn::EvalContext context(const UiContext& ui, const learn::ClientFacts& facts, const learn::Mark& mark) const;
    void evaluate(UiContext& ui, const learn::ClientFacts& facts);
    void enterStep(const UiContext& ui, size_t step);
    void finish(Result r, std::string why = {});

    void drawOutlines(UiContext& ui) const;
    void drawPanel(UiContext& ui);
    void tutorialBody(UiContext& ui);
    void trainingBody(UiContext& ui);
    void drawResult(UiContext& ui);
    void followLink(UiContext& ui, const std::string& target);

    learn::Lesson lesson_;
    learn::Tracker tracker_;
    learn::Mark gameMark_;
    uint64_t seenRevision_ = 0;
    uint64_t seenSignature_ = 0;

    // Tutorials.
    size_t step_ = 0;
    std::vector<std::optional<learn::Mark>> stepMarks_;
    std::vector<uint8_t> completed_;

    // Training games.
    std::vector<uint8_t> objectiveDone_;
    std::vector<uint8_t> objectiveFailed_;
    std::vector<uint8_t> hintShown_;
    std::deque<size_t> hints_;            // hints to show, oldest first
    std::vector<uint8_t> pageSeen_;
    std::optional<size_t> page_;          // the briefing page shown
    std::optional<uint32_t> lastTurn_;

    Result result_ = Result::None;
    std::string resultWhy_;
    bool showResult_ = false;
    bool panelOpen_ = true;
    bool confirmLeave_ = false;
    Request request_ = Request::None;
};

} // namespace opense4::client::classic
