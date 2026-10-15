#pragma once

// The classic-rules client: the engine in src/game driven by the player's
// installed classic data set and art, presented in the classic main-window
// layout (a fixed 1024×768 frame, scaled to fit). See docs/spec/06.

#include "client/audio.hpp"
#include "client/classic/classic_scenario.hpp"
#include "client/classic/command_line.hpp"
#include "client/classic/frontend.hpp"
#include "client/classic/learn_content.hpp"
#include "client/classic/lesson_audit.hpp"
#include "client/classic/lesson_runner.hpp"
#include "client/classic/main_window.hpp"
#include "client/classic/screens/type_picker.hpp"
#include "client/classic/turn_start.hpp"
#include "client/mode.hpp"
#include "client/script/player.hpp"
#include "mods/mod_set.hpp"

#include <chrono>
#include <deque>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace opense4::client {

struct ClassicOptions {
    std::string installDir;  // empty = auto-detect
    // Mods: these (paths, or ids in modsDir) when modsGiven, else the ones
    // the settings enable (docs/sdk/packages-and-data.md).
    std::vector<std::string> mods;
    bool modsGiven = false;
    std::string modsDir;     // empty: <user data>/Mods
    bool noBundledMods = false;  // without the mods that come with OpenSE4 (mods/ beside the program)
    // The program's fourth argument (AppOptions::positional): a mod folder,
    // relative to the game folder, played as a classic mod for this run and
    // read in Path.txt's place. Empty: none.
    std::string commandLineModFolder;
    // The original's command line (command_line.hpp, spec 05 §9.2) with a
    // player number: sign in to its game file as that player at once (a
    // turn file plays as with --pbem, with --pbem-orders and --pbem-end-turn).
    // The host's (no player number) runs before any window (client/app.cpp).
    std::optional<classic::CommandLineGame> commandLine;
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
    std::string aiPlayer;    // --ai: who plays a quick game's computer empires ("<mod id>:<player>"; empty: the built-in AI)
    // Play by e-mail (--pbem): open this game file for `pbemEmpire` (1-based;
    // 0: the only empire that can play now) and play its turn.
    // --load: open this saved game (OpenSE4's or the original's) at once.
    std::string loadFile;
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
    // On failure: why (empty when nothing is to be shown: a wrong password on
    // the command line, which the original quits on without a word; the log
    // says why) and the program's exit code.
    static std::unique_ptr<ClassicMode> create(const Platform& platform, const ClassicOptions& options, std::string& error, int& exitCode);
    ~ClassicMode() override;

    bool update(const FrameState& fs) override;
    void background() override;
    void restyle() override;
    void render(gfx::Renderer2D& renderer, const FrameState& fs) override;
    Color clearColor() const override { return Color::hex(0x000000); }
    // The tutorial input lock (lesson_lock.hpp).
    EventVerdict filterEvent(const SDL_Event& event) override;
    int exitCode() const override { return exitCode_; }
    bool closeRequested() override;
    const script::Probe* probe() const override { return options_.scripted ? this : nullptr; }

private:
    explicit ClassicMode(const Platform& platform) : platform_(platform) {}

    // The data set with its mods, and their pictures and sounds over the install's.
    struct DataSet {
        std::shared_ptr<const game::Rules> rules;
        assets::InstallFiles files;
        mods::ModSet mods;
        // The installed data's errors, when it loaded all the same (without
        // mods): shown in a window per data file, and the game goes on
        // (spec 06 §1.1 "Data File Errors", inferred).
        std::vector<std::string> errors;
    };
    // `modFolder`: the command line's mod folder, read in Path.txt's place (none: Path.txt's).
    static std::expected<DataSet, std::string> readDataSet(const std::filesystem::path& dataDir, const mods::ModChoice& choice,
                                                           const std::optional<std::string>& modFolder);
    mods::ModChoice modChoice(std::vector<std::string> ids) const;
    // The command line's mod folder as Path.txt would name it (relative to
    // the game folder; empty when it lies elsewhere), or none.
    std::optional<std::string> commandLineModFolder() const { return commandLineModDir_; }
    // Plays with this data set from now on: the art, fonts, pointers and music follow it.
    void useDataSet(DataSet d);
    // The data files' errors still to show, one window per file (DataSet::errors).
    struct DataFileErrors {
        std::string file;   // "Components.txt"
        std::filesystem::path path;
        std::vector<std::string> errors;
    };
    std::deque<DataFileErrors> dataErrors_;
    void drawDataErrors(const classic::MenuContext& ctx);
    // The Mods window's choice (in the front end only): the data set read
    // again with these mods, kept in the settings; why not, when it cannot be.
    std::optional<std::string> changeMods(const std::vector<std::string>& ids);
    std::filesystem::path dataDir_;
    std::vector<std::string> fontFiles_;   // where the classic fonts in use came from
    // Asked for by a front-end screen this frame (MenuContext::changeMods, goTo).
    std::optional<classic::MenuContext::ModsChange> pendingMods_;
    std::unique_ptr<classic::FrontScreen> nextFrontScreen_;

    void startGame(std::unique_ptr<classic::ClassicSession> session);
    // Plays a play-by-e-mail turn just opened (--pbem, or a turn file on the
    // command line): ended at once with --pbem-end-turn, --open's window
    // over it. On failure returns why.
    std::optional<std::string> playPbemTurn(classic::PbemGame game, classic::PbemTurn turn);
    // The command line's sign-in as its player (ClassicOptions::commandLine).
    // On failure sets why (empty for a wrong password) and the exit code.
    bool signInFromCommandLine(const classic::CommandLineGame& line, std::string& error, int& exitCode);
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
    // Clicks and keys the player gave while a long engine call kept the
    // frame busy (End Turn's processing, the rest of a turn after a battle's
    // window) are dropped: they were aimed at a screen that has changed since
    // (a second click on End Turn would end the next turn).
    void dropInputMadeWhileBusy();
    void updateAudio();
    // Switches the music for a cue (docs/spec/06 §5.5), when music is on.
    void cueMusic(MusicCue cue);
    bool updateFrame(const FrameState& fs);
    // The screen layout of this frame (docs/spec/06 §2.1.1): the setting, else the desktop's.
    void applyLayout();
    classic::ScreenLayout desktopLayout_ = classic::ScreenLayout::Large;

    Platform platform_;
    Fonts fonts_;  // the classic game's fonts, else the app's
    std::optional<std::string> commandLineModDir_;   // commandLineModFolder()
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
    // A new turn began and its moves in the shown system are still being
    // shown: the main window waits, and a click or a key shows them at once
    // (classic::turnStartGate).
    bool turnStarting_ = false;
    // The windows open when the moves being shown began (holdMoves).
    size_t windowsUnderMoves_ = 0;
    bool movesWereShowing_ = false;
    // What the start of a turn lets show now (turn_start.hpp), from the game
    // and the windows as they are; and the windows it lets open: the next
    // battle to watch, the endings due and, last, the Log.
    classic::TurnStartGate turnStartGate(classic::UiContext& ui);
    void openTurnStartWindows(classic::UiContext& ui, const classic::TurnStartGate& gate);
    bool confirmEndTurn_ = false;
    bool modalOpen_ = false;   // a window or a question is open this frame: the main window takes no input
    // A game of one of the original's scenarios (docs/spec/06 §5.6): its
    // pages, read from the scenario's stem when the game starts or loads
    // (none: not such a game, or its files could not be read).
    std::optional<classic::ScenarioFiles> scenarioFiles_;
    // The page of the turn may come up: the scenario has just started, a
    // human player's turn has started here, or a hand-over (Next Player)
    // came first; once the start of the turn lets the Log open, the first
    // page for the turn and the player opens (a plain load shows none).
    bool scenarioCheckDue_ = false;
    void openTurnPage(classic::UiContext& ui);
    // Ctrl+H and the T buttons: the page shown last.
    void openLastScenarioPage(classic::UiContext& ui);
    // The tutorial or training game being played, if any.
    std::unique_ptr<classic::LessonRunner> lesson_;
    std::string lessonError_;   // a lesson that could not start
    std::vector<std::string> importNotes_;   // a saved game of the original was imported: what it approximated
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
    void drawImportNotes(classic::UiContext& ui);
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
    classic::TypeListPicker colonyTypePicker_;
    std::optional<game::ObjectId> colonyTypeAsked_;   // the colony the picker was set up for
    // Turn-based games: the combat resolution prompt, Tactical or Strategic for
    // each human side of the battle that waits (spec 06 §1.6, spec 04 §3).
    void drawBattleQuestion(classic::UiContext& ui);
    size_t battleChoiceKey_ = SIZE_MAX;    // the battle question on screen
    bool battleNotice_ = false;            // the notice before it (a computer empire's turn)
    std::deque<size_t> strategicQueue_;   // battles (GameState::combats) waiting for the Strategic Combat window
    // Turn-based games: message boxes the player's own orders raised (a failed
    // Colonize, spec 03 §8), shown one after another with OK.
    std::deque<game::PlayerMessage> messageBoxes_;
    void drawMessageBox(classic::UiContext& ui);

    // Before a player plays, the map hidden (spec 06 §1.1, §1.2.1): the Next
    // Player window at each human player's turn when more than one empire is
    // human-controlled (destroyed ones included), and when a turn-based
    // game's autosave is loaded; Login To Game when a simultaneous game is
    // loaded (confirmed: binary).
    enum class SignInWindow { None, NextPlayer, Login };
    SignInWindow signIn_ = SignInWindow::None;
    struct TurnKey {
        uint32_t turn = 0;
        game::EmpireId player;
        bool operator==(const TurnKey&) const = default;
    };
    // The human turn the Next Player window last came for (or that needs none: a game loaded).
    std::optional<TurnKey> signInKey_;
    std::string signInPassword_;
    std::string signInError_;
    int signInFailures_ = 0;       // wrong passwords in a row in the Next Player window
    int loginRow_ = -1;            // Login To Game: the row picked (the empires', then Game Master)
    std::optional<TurnKey> currentTurnKey() const;
    // Whether the Next Player window is due now (and remembers it came).
    bool nextPlayerDue();
    // A game just started or loaded: what it asks first (ClassicSession::takeSignIn).
    void beginSignIn(classic::ClassicSession::SignIn ask, bool loaded);
    void drawNextPlayer(classic::UiContext& ui);
    void drawLogin(classic::UiContext& ui);
    // Leaves the game for the intro: the original exits the program at its
    // endings (Game Completed, Human Dead, No to playing on after the
    // conquest, a fourth wrong password in Next Player); OpenSE4 returns to
    // the intro instead (spec 06 §1.7).
    void leaveToFront(classic::FrontId where);

    // A turn-based game whose last turn every player has played (spec 05 §6):
    // the "Game Completed" box, then the intro.
    bool gameCompleted() const;
    void drawGameCompleted(classic::UiContext& ui);
    // Closing the window during a game asks first (spec 06 §4.3, confirmed: binary).
    bool quitAsked_ = false;
    void drawQuitQuestion(classic::UiContext& ui);
    // The Progress window while a turn is processed in the background (spec 06 §1.7).
    void drawProgress(classic::UiContext& ui);
};

} // namespace opense4::client
