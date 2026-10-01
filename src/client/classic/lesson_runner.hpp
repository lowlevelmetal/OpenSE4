#pragma once

// Runs a tutorial or training game over the game it started
// (docs/LEARNING.md "The lesson panel"): a movable panel with the step (or
// the objectives and the briefing pages), outlines around the UI tags the
// step names, hints and the result. The rules that move the lesson on are
// learn::LessonProgress; this draws it. It only reads the game: everything
// the player changes still goes through ClassicSession::issue(), which
// reports each command here.

#include "client/classic/ui.hpp"
#include "learn/progress.hpp"

#include <string>

namespace opense4::client::classic {

class LessonRunner {
public:
    LessonRunner(learn::Lesson lesson, const ClassicSession& session);

    const learn::Lesson& lesson() const { return progress_.lesson(); }
    const learn::LessonProgress& progress() const { return progress_; }

    // A command the player issued successfully (ClassicSession::onIssued).
    void issued(const game::Command& c) { progress_.issued(c); }
    // Each frame, after the windows drew (so this frame's UI tags are known):
    // checks the conditions, draws the panel, the outlines and the result.
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

private:
    void evaluate(UiContext& ui, const learn::ClientFacts& facts);
    void finished();

    void drawOutlines(UiContext& ui) const;
    void drawPanel(UiContext& ui);
    void tutorialBody(UiContext& ui);
    void trainingBody(UiContext& ui);
    void drawResult(UiContext& ui);
    void followLink(UiContext& ui, const std::string& target);

    learn::LessonProgress progress_;
    uint64_t seen_ = 0;   // what the conditions read when they were last checked
    bool showResult_ = false;
    bool panelOpen_ = true;
    bool moved_ = false;        // the player moved the panel: it keeps its place
    bool windowsOpen_ = false;  // a classic window is open (the panel's default place)
    YesNoPrompt leave_;
    Request request_ = Request::None;
};

} // namespace opense4::client::classic
