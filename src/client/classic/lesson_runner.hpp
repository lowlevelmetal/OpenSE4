#pragma once

// Runs a tutorial or training game over the game it started
// (docs/LEARNING.md "The lesson panel"): a movable panel with the step (or
// the objectives and the briefing pages), outlines around the UI tags the
// step names, hints and the result. The rules that move the lesson on are
// learn::LessonProgress; this draws it. It only reads the game: everything
// the player changes still goes through ClassicSession::issue(), which
// reports each command here.

#include "client/classic/lesson_lock.hpp"
#include "client/classic/lesson_panel.hpp"
#include "client/classic/ui.hpp"
#include "learn/markdown.hpp"
#include "learn/progress.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct ImGuiWindow;

namespace opense4::client::classic {

// The open classic windows (ids, oldest first) in the order Dear ImGui shows
// them, back to front: a window the player clicks comes to the front. The
// input lock and the way back follow what the player sees.
std::vector<std::string> windowsBackToFront(const UiContext& ui, std::vector<std::string> open);

class LessonRunner {
public:
    LessonRunner(learn::Lesson lesson, const ClassicSession& session);

    const learn::Lesson& lesson() const { return progress_.lesson(); }
    const learn::LessonProgress& progress() const { return progress_; }

    // A command the player issued successfully (ClassicSession::onIssued).
    void issued(const game::Command& c) { progress_.issued(c); }
    // Each frame, after the windows drew (so this frame's UI tags are known):
    // checks the conditions, draws the panel, the outlines (and, with the
    // input lock on, the spotlight around `lock`'s areas) and the result.
    void frame(UiContext& ui, const learn::ClientFacts& facts, const LockState& lock);

    // The tutorial input lock (lesson_lock.hpp): whether the active step locks
    // the input (a tutorial, Free Play off, not over), and that step.
    bool locking() const;
    const learn::Step* activeStep() const;
    // A press the lock refused (`where`), or a key (none; `key` names it): a
    // note by the pointer or the panel, and the outlines flash.
    void refused(std::optional<ImVec2> where, double time, std::string key = {});
    // The way back when the active step's window was closed or another window
    // covers its outline (docs/LEARNING.md "Getting back"): Markdown for the
    // panel, under the step's text; empty when there is none. Its part is
    // outlined in the recovery style meanwhile.
    const std::string& recoveryHint() const { return recoveryHint_; }

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

    void drawOutlines(UiContext& ui, const LockState& lock) const;
    // The note by a refused click or key.
    void drawRefusedNote(UiContext& ui, const learn::Step& st, double since) const;
    // The active step's condition looks out of reach: Next offers Skip.
    bool stuck(const UiContext& ui) const;
    // The way back for the active step (lesson_lock.hpp findRecovery), and its hint.
    void updateRecovery(const UiContext& ui, const learn::ClientFacts& facts);
    std::string describe(const Recovery& r) const;
    std::string recoveryPlain() const;   // the hint without its Markdown
    void drawPanel(UiContext& ui);

    // The game's prompts and the pop-ups open this frame (and last frame's,
    // for those drawn after the panel): the panel stays under them, avoids
    // them, and its keys wait while one is open.
    struct Prompts {
        std::vector<panel::Box> boxes;
        ImGuiWindow* lowest = nullptr;   // the one furthest back in display order
    };
    static Prompts findPrompts(const UiContext& ui);
    // Above the classic windows, under every prompt and pop-up.
    void raisePanel(const Prompts& prompts) const;
    // The panel's buttons, and what pressing one (or its key) does.
    enum class Button : uint8_t { Back, Next, Skip, ReadMore, Previous, PageNext, ClosePage, More, Hide, FreePlay, Leave };
    void press(UiContext& ui, Button b);
    // Small screens show an action step's panel compact (its start and one row
    // of buttons) until the player asks for More.
    bool compact() const;
    void drawPanel(UiContext& ui, const Prompts& prompts);
    // Hands Dear ImGui's keyboard focus back to the window that had it before a click on the panel.
    void keepKeyboardFocus();
    void tutorialBody(UiContext& ui);
    void trainingBody(UiContext& ui);
    void drawResult(UiContext& ui);
    void followLink(UiContext& ui, const std::string& target);

    learn::LessonProgress progress_;
    uint64_t seen_ = 0;   // what the conditions read when they were last checked
    std::string counters_;   // the active step's progress line, made when the conditions were checked
    bool showResult_ = false;
    bool panelOpen_ = true;
    bool moved_ = false;        // the player moved the panel: it keeps its place
    std::optional<size_t> movedOn_;   // the active step then (a later step may place it again)
    bool windowsOpen_ = false;  // a classic window is open (the panel's default place)
    // The panel's layout: the height its text needs (frame pixels, measured
    // last frame at `bodyWidth_`; 0 until measured), the spot it took, what
    // its text was last scrolled to the top for, and the step whose compact
    // panel the player opened (More) or closed (Less).
    float bodyNeed_ = 0, bodyWidth_ = 0;
    std::optional<int> spot_;
    std::optional<size_t> scrolledFor_;
    std::optional<std::pair<size_t, bool>> compactChoice_;
    // The active step: since when, and when its targets were last on screen
    // (or a way back to them was).
    std::optional<size_t> activeSeen_;
    double activeSince_ = 0;
    double targetsSeen_ = 0;
    Recovery recovery_;
    std::string recoveryHint_;                 // Markdown
    std::vector<learn::Block> recoveryBlocks_;
    std::map<std::string, std::string> titles_;   // the windows' titles as last shown, by id
    // The last press or key the lock refused, and when the recent ones were.
    std::optional<ImVec2> refusedAt_;
    std::string refusedKey_;
    double refusedTime_ = -10;
    std::deque<double> refusals_;
    YesNoPrompt leave_;
    Request request_ = Request::None;
    ImGuiID focusBefore_ = 0;   // the window that had the keyboard focus before the panel took it
};

} // namespace opense4::client::classic
