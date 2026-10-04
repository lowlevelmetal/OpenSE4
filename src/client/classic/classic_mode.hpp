#pragma once

// The classic-rules client: the engine in src/game driven by the player's
// installed classic data set and art, presented in the classic main-window
// layout (a fixed 1024×768 frame, scaled to fit). See docs/spec/06.

#include "client/audio.hpp"
#include "client/classic/frontend.hpp"
#include "client/classic/learn_content.hpp"
#include "client/classic/lesson_audit.hpp"
#include "client/classic/lesson_runner.hpp"
#include "client/classic/main_window.hpp"
#include "client/mode.hpp"
#include "client/script/player.hpp"

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace opense4::client {

struct ClassicOptions {
    std::string installDir;  // empty = auto-detect
    uint64_t seed = 1;
    bool seedGiven = false;  // the player gave the seed (--seed, or a script run)
    int systemCount = 0;     // 0 = default
    int empireCount = 0;  // a quick game's empires, the player's included; 0: Quick Start's random players
    std::string quadrantType;
    bool skipIntro = false;  // start a quick game at once (automation, screenshots)
    std::string race;        // quick start race preset (folder name); empty = first
    int autoTurns = 0;       // let the computer play every empire for N turns first
    std::string select;      // then select a vehicle: "moving", "fleet" or a vehicle id (automation)
    std::string openWindow;  // open this window at start (automation, screenshots)
    bool turnBased = true;   // quick game in the turn-based style (the default, spec 01 §14 Q39)
    // Play by e-mail (--pbem): open this game file for `pbemEmpire` (1-based;
    // 0: the only empire that can play now) and play its turn.
    std::string pbemFile;
    int pbemEmpire = 0;
    std::string pbemPassword;
    std::string pbemOrdersDir;
    bool pbemEndTurn = false;  // automation: end the turn at once, writing the .plr
    bool pbemExit = false;     // and quit then (no screenshot asked for)
    // Learning to play (docs/LEARNING.md): start a tutorial or training game
    // at once, or open the manual (at a page: "slug" or "slug#anchor").
    std::string tutorial;
    std::string training;
    std::optional<std::string> manual;
    std::string learnDir;      // read the learning content from this folder only (testing)
    // Checking content (--lesson-check, with --tutorial=<slug>:<step>): open
    // the windows the step works in, then report whether every tag the step
    // highlights or allows is on screen, and exit with 1 when one is missing
    // (at once, or after the screenshot when one is asked for).
    bool lessonCheck = false;
    bool lessonCheckQuits = true;
    // --lesson-audit: the check, then what the input lock lets through and
    // whether what the step's text names can be seen (lesson_audit.hpp).
    bool lessonAudit = false;
    // An input script plays or the session is recorded (docs/BUILDING.md
    // "Input scripts"): the mode tracks what scripts check (probe()).
    bool scripted = false;
};

// What to tell the player when no installed copy of the game is found:
// auto-detection failed (`installDir` empty) or `installDir` holds no data set.
std::string missingInstallMessage(const std::string& installDir);

class ClassicMode final : public Mode, private script::Probe {
public:
    static std::unique_ptr<ClassicMode> create(const Platform& platform, const ClassicOptions& options, std::string& error);
    ~ClassicMode() override;

    bool update(const FrameState& fs) override;
    void background() override;
    void restyle() override;
    void render(gfx::Renderer2D& renderer, const FrameState& fs) override;
    Color clearColor() const override { return Color::hex(0x000000); }
    // The tutorial input lock (lesson_lock.hpp).
    EventVerdict filterEvent(const SDL_Event& event) override;
    int exitCode() const override { return exitCode_; }
    const script::Probe* probe() const override { return options_.scripted ? this : nullptr; }

private:
    explicit ClassicMode(const Platform& platform) : platform_(platform) {}

    void startGame(std::unique_ptr<classic::ClassicSession> session);
    // Starts a lesson's game from its [setup] through the quick start; on
    // failure returns why. `chosen`: the player chose it (the Learn window,
    // the lesson panel), so leaving it keeps its place; false on the command
    // line, which checks content.
    std::optional<std::string> startLesson(learn::LessonKind kind, const std::string& slug, bool chosen = true);
    // Resumes a tutorial at the place the player left it (docs/LEARNING.md
    // "Resuming a lesson"); a place that cannot be resumed any more starts the
    // lesson afresh, with a note. On failure returns why.
    std::optional<std::string> resumeLesson(learn::LessonKind kind, const std::string& slug);
    // Before the lesson's game goes (Leave, another game, quitting): a
    // tutorial left before its end keeps its game and step to resume.
    void keepLessonPlace();
    // Back to the front end's Learn window (after a lesson).
    void quitToLearn(learn::LessonKind kind);
    void openScreen(classic::ScreenId id, classic::ScreenArgs args);
    // Automation (--open): a window, or a sample battle for the battle
    // windows, over the game just started; "none" keeps the Log closed.
    std::optional<std::string> openAutomationWindow(const std::string& name);
    // --select: a vehicle for the main window to select (automation).
    std::optional<std::string> selectForAutomation(const std::string& what);
    // The lesson panel and its requests, Ctrl+H and Shift+F1 (not while a
    // question waits for its answer: `prompted`).
    void updateLesson(classic::UiContext& ui, bool prompted);
    void contextHelp();
    void endTurn();
    void updateAudio();
    // Switches the music for a cue (docs/spec/06 §5.5), when music is on.
    void cueMusic(MusicCue cue);
    bool updateFrame(const FrameState& fs);
    // The screen layout of this frame (docs/spec/06 §2.1.1): the setting, else the desktop's.
    void applyLayout();
    classic::ScreenLayout desktopLayout_ = classic::ScreenLayout::Large;

    Platform platform_;
    Fonts fonts_;  // the classic game's fonts, else the app's
    ClassicOptions options_;
    std::shared_ptr<const game::Rules> rules_;
    std::unique_ptr<classic::Art> art_;
    std::unique_ptr<classic::LearnContent> learn_;
    classic::FrameMapping mapping_;
    Playlists playlists_;
    MusicDirector music_{static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    bool replayWasOpen_ = false;
    bool loadedFromIntro_ = false;   // a game loaded from the intro this frame (background music)
    size_t logSeen_ = 0;             // the player's log entries already heard (stellar manipulation sounds)

    // Before a game: the front end.
    std::unique_ptr<classic::FrontScreen> front_;
    std::optional<classic::FrontId> nextFront_;
    std::string frontError_;
    std::string pendingSelect_;   // --select after a front-end screen: tried every frame until it succeeds
    bool keepLogClosed_ = false;  // ... and the Log does not open by itself (screenshots of the main window)
    bool quit_ = false;
    std::optional<std::pair<learn::LessonKind, std::string>> pendingLesson_;   // chosen in the front end
    bool pendingResume_ = false;                                               // ... to resume

    // During a game.
    std::unique_ptr<classic::ClassicSession> session_;
    std::unique_ptr<classic::UiContext> ui_;
    classic::MainWindow main_;
    std::vector<std::pair<classic::ScreenId, std::unique_ptr<classic::Screen>>> screens_;
    // The window that was in front when each window opened (its parent): a
    // window that closes with its parent (Screen::closesWithParent) closes with it.
    std::unordered_map<const classic::Screen*, classic::ScreenId> parentOf_;
    // Closes the windows that belong to the windows closed this frame.
    void closeChildren(std::vector<classic::ScreenId> closed);
    std::vector<std::pair<classic::ScreenId, classic::ScreenArgs>> pendingOpen_;
    bool openLogOnTurn_ = false;
    bool confirmEndTurn_ = false;
    bool modalOpen_ = false;   // a window or a question is open this frame: the main window takes no input
    // The tutorial or training game being played, if any.
    std::unique_ptr<classic::LessonRunner> lesson_;
    std::string lessonError_;   // a lesson that could not start
    // The lesson was chosen by the player (startLesson): leaving it keeps its place.
    bool lessonResumable_ = false;
    // The tutorial input lock, made at the end of each frame for the next.
    classic::InputLock lock_;
    void updateLock(classic::UiContext& ui);
    // The frame's UI tags as the lock reads them, and the open windows back to front.
    std::vector<classic::TaggedArea> lockTags(const classic::UiContext& ui) const;
    std::vector<std::string> lockWindows(const classic::UiContext& ui) const;
    std::string refusedKey_;    // a key the lock refused since the last frame (its name)
    bool navKeyboardOff_ = false;   // Dear ImGui's keyboard navigation is off for the lock (updateLock)
    // The keyboard goes to the classic window in front (keepFocusOnFrontWindow).
    void keepFocusOnFrontWindow();
    ImGuiID frontWindow_ = 0;   // the Dear ImGui window of the window in front (0: none)
    std::vector<ImGuiID> classicWindows_;   // and of every classic window drawn this frame
    // Every window is modal: whether the Dear ImGui window (or its root) is held
    // up by the window in front (or, for the main window's panels, by any window
    // or question), as of the last frame; and taking the hover from such windows.
    bool heldUp(const ImGuiWindow* window) const;
    void holdUpHover();
    // --lesson-check: the windows the step works in, and its report.
    void prepareLessonCheck();
    void lessonCheckReport(classic::UiContext& ui);
    void lessonAuditReport(classic::UiContext& ui, const learn::Step& step);
    classic::AuditReport auditStep(const classic::UiContext& ui, const learn::Step& step) const;
    std::vector<std::string> lessonAudit() const override;
    int lessonCheckFrame_ = -1;
    int exitCode_ = 0;

    // Input scripts (script::Probe, classic_probe.cpp): what the frame drawn
    // last showed, and the game's counters since it began.
    std::vector<script::Box> tagBoxes(std::string_view name) const override;
    std::vector<std::string> tagNames() const override;
    ImVec2 framePoint(float x, float y) const override;
    float frameScale() const override;
    std::vector<script::Box> sectors(const script::Target& t, std::string& error) const override;
    std::vector<script::Box> systems(const script::Target& t, std::string& error) const override;
    std::string targetAt(ImVec2 p) const override;
    std::string screen() const override { return session_ ? "game" : "front"; }
    std::vector<std::string> openWindows() const override;
    std::optional<script::LessonInfo> lesson() const override;
    std::optional<uint32_t> turn() const override;
    std::vector<std::string> logLines() const override;
    bool typing() const override;
    std::string lockDescription() const override;
    std::optional<bool> holds(const learn::Condition& c, const learn::Mark& since, std::string& error) const override;
    learn::Mark mark(bool gameStart) const override;
    std::optional<int64_t> factValue(learn::Fact f, const learn::Mark& since) const override;
    // Each frame: the commands and battles since the game began.
    void trackForScripts();
    learn::ClientFacts lastFacts_;
    learn::Tracker scriptTracker_;
    learn::Mark gameMark_;
    uint64_t trackedRevision_ = UINT64_MAX;
    float fbScale_ = 1.0f;

    // Network games: status strip and chat.
    void drawNetwork(classic::UiContext& ui);
    // Play by e-mail: status strip (where End Turn saves the orders, or where it did).
    void drawPbem(classic::UiContext& ui);
    bool chatOpen_ = false;
    std::string chatInput_;
    // The in-game host's empire list with "Toggle Empire AI On/Off" (spec 05 §9.4).
    void drawHostEmpires(classic::UiContext& ui);
    bool hostEmpiresOpen_ = false;
    game::EmpireId toggleAsked_;

    // Turn-based games: the Attack Sector question.
    void drawEntryQuestion(classic::UiContext& ui);
    // Turn-based games: the colony type of a colony just founded (spec 03 §8).
    std::optional<game::ObjectId> colonyTypeChoice(const classic::UiContext& ui) const;
    void drawColonyTypeChoice(classic::UiContext& ui, game::ObjectId planet);
    // Turn-based games: the combat resolution prompt, Tactical or Strategic for
    // each human side of the battle that waits (spec 06 §1.6, spec 04 §3).
    void drawBattleQuestion(classic::UiContext& ui);
    size_t battleChoiceKey_ = SIZE_MAX;    // the battle question on screen
    bool battleNotice_ = false;            // the notice before it (a computer empire's turn)
    std::deque<size_t> strategicQueue_;   // battles (GameState::combats) waiting for the Strategic Combat window

    // Hotseat hand-over between human players.
    void drawHandoff(classic::UiContext& ui);
    game::EmpireId handoffPlayer_;
    bool handoff_ = false;
    std::string handoffPassword_;
    std::string handoffError_;
};

} // namespace opense4::client
