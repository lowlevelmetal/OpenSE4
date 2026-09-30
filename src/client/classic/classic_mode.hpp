#pragma once

// The classic-rules client: the engine in src/game driven by the player's
// installed classic data set and art, presented in the classic main-window
// layout (a fixed 1024×768 frame, scaled to fit). See docs/spec/06.

#include "client/audio.hpp"
#include "client/classic/frontend.hpp"
#include "client/classic/main_window.hpp"
#include "client/mode.hpp"

#include <deque>
#include <memory>
#include <vector>

namespace opense4::client {

struct ClassicOptions {
    std::string installDir;  // empty = auto-detect
    uint64_t seed = 1;
    int systemCount = 0;     // 0 = default
    int empireCount = 5;
    std::string quadrantType;
    bool skipIntro = false;  // start a quick game at once (automation, screenshots)
    std::string race;        // quick start race preset (folder name); empty = first
    int autoTurns = 0;       // let the computer play every empire for N turns first
    std::string openWindow;  // open this window at start (automation, screenshots)
    bool turnBased = false;  // quick game in the turn-based style
    // Play by e-mail (--pbem): open this game file for `pbemEmpire` (1-based;
    // 0: the only empire that can play now) and play its turn.
    std::string pbemFile;
    int pbemEmpire = 0;
    std::string pbemPassword;
    std::string pbemOrdersDir;
    bool pbemEndTurn = false;  // automation: end the turn at once, writing the .plr
    bool pbemExit = false;     // and quit then (no screenshot asked for)
};

class ClassicMode final : public Mode {
public:
    static std::unique_ptr<ClassicMode> create(const Platform& platform, const ClassicOptions& options, std::string& error);
    ~ClassicMode() override;

    bool update(const FrameState& fs) override;
    void render(gfx::Renderer2D& renderer, const FrameState& fs) override;
    Color clearColor() const override { return Color::hex(0x000000); }

private:
    explicit ClassicMode(const Platform& platform) : platform_(platform) {}

    void startGame(std::unique_ptr<classic::ClassicSession> session);
    void openScreen(classic::ScreenId id, classic::ScreenArgs args);
    void endTurn();
    void updateAudio();
    bool updateFrame(const FrameState& fs);

    Platform platform_;
    Fonts fonts_;  // the classic game's fonts, else the app's
    ClassicOptions options_;
    std::shared_ptr<const game::Rules> rules_;
    std::unique_ptr<classic::Art> art_;
    classic::FrameMapping mapping_;
    Playlists playlists_;

    // Before a game: the front end.
    std::unique_ptr<classic::FrontScreen> front_;
    std::optional<classic::FrontId> nextFront_;
    std::string frontError_;
    bool quit_ = false;

    // During a game.
    std::unique_ptr<classic::ClassicSession> session_;
    std::unique_ptr<classic::UiContext> ui_;
    classic::MainWindow main_;
    std::vector<std::pair<classic::ScreenId, std::unique_ptr<classic::Screen>>> screens_;
    std::vector<std::pair<classic::ScreenId, classic::ScreenArgs>> pendingOpen_;
    bool openLogOnTurn_ = false;
    bool confirmEndTurn_ = false;

    // Network games: status strip and chat.
    void drawNetwork(classic::UiContext& ui);
    // Play by e-mail: status strip (where End Turn saves the orders, or where it did).
    void drawPbem(classic::UiContext& ui);
    bool chatOpen_ = false;
    std::string chatInput_;

    // Turn-based games: the Attack Sector question.
    void drawEntryQuestion(classic::UiContext& ui);
    // Turn-based games: the combat resolution prompt, Tactical or Strategic for
    // each human side of the battle that waits (spec 06 §1.6, spec 04 §3).
    void drawBattleQuestion(classic::UiContext& ui);
    std::vector<uint8_t> battleChoices_;   // per human side: 1 tactical
    size_t battleChoiceKey_ = SIZE_MAX;
    std::deque<size_t> strategicQueue_;   // battles (GameState::combats) waiting for the Strategic Combat window

    // Hotseat hand-over between human players.
    void drawHandoff(classic::UiContext& ui);
    game::EmpireId handoffPlayer_;
    bool handoff_ = false;
    std::string handoffPassword_;
    std::string handoffError_;
};

} // namespace opense4::client
