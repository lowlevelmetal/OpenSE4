#pragma once

#include "client/app_settings.hpp"
#include "client/fonts.hpp"
#include "client/mode.hpp"
#include "client/script/player.hpp"
#include "client/script/recorder.hpp"
#include "gfx/device.hpp"
#include "gfx/imgui_renderer.hpp"
#include "gfx/renderer2d.hpp"

#include <imgui.h>

#include <cfloat>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace opense4::client {

struct AppOptions {
    enum class Renderer { Auto, Vulkan, OpenGL };
    Renderer renderer = Renderer::Auto;
    bool validation = false;
    bool vsync = true;
    bool noAudio = false;
    bool fullscreen = false;
    int width = 1600;
    int height = 900;
    std::string assetsDir;

    // The player's installed copy of the game (its data set and art).
    std::string installDir;          // game, se4 or Data directory; empty = auto-detect

    // New games.
    uint64_t seed = 0;               // 0 = from the clock
    bool seedGiven = false;          // --seed (or a script run): a network game hosted here uses it too
    int systemCount = 0;             // a quick game's star systems; 0 = the default
    int empireCount = 4;             // a quick game's empires, the player's included
    std::string quadrantType;
    bool quickStart = false;         // skip the intro and start a quick game
    std::string race;                // race preset for the quick game
    std::string openWindow;          // window to open at start
    bool turnBased = true;           // a quick game's turn style: turn-based, as a new game (spec 01 §2.2, §14 Q39; spec 05 §8)

    // Play by e-mail: open this game file and play the turn (docs/MULTIPLAYER.md).
    std::string pbemFile;
    int pbemEmpire = 0;              // 1-based; 0 = the only empire that can play now
    std::string pbemPassword;
    std::string pbemOrdersDir;       // empty = the game file's folder
    bool pbemEndTurn = false;        // automation: end the turn at once (writes the .plr)

    // Learning to play (docs/LEARNING.md): start a tutorial or training game,
    // or open the manual (empty value: its first page); content from another folder.
    std::string tutorial;
    std::string training;
    std::optional<std::string> manual;
    std::string learnDir;
    bool lessonCheck = false;        // --lesson-check: report whether a tutorial step's areas are on screen

    // Input scripts (docs/BUILDING.md "Input scripts"): play one (its
    // screenshots and failure picture go to scriptOutput), or record the
    // session as one (`recordOptions`: the command line, for its options line).
    std::optional<script::Script> inputScript;
    std::string scriptOutput;
    std::string recordInput;
    std::vector<std::string> recordOptions;

    // Automation: render a few frames, save a PNG and exit.
    std::string screenshotPath;
    int screenshotFrames = 10;
    int autoTurns = 0;
    std::string select;   // --select: what the main window selects afterwards
};

// The application shell: window, render device (Vulkan with OpenGL fallback),
// Dear ImGui, the frame loop and screenshots. The game itself is a Mode.
class App final : public AppControl {
public:
    int run(const AppOptions& options);

    // AppControl.
    void applyGraphics() override;
    std::vector<DisplayModeInfo> displayModes() const override;
    std::string rendererInfo() const override { return rendererInfo_; }
    gfx::Backend backend() const override { return device_->backend(); }
    float fps() const override { return fps_; }
    void minimize() override;

private:
    bool createWindowAndDevice();
    void initImGui();
    void updateUiScale();
    bool frame();
    // One input event: the mode's filter (a tutorial's input lock), then Dear ImGui.
    EventVerdict handleEvent(SDL_Event& event, bool& running);
    void shutdown();

    AppOptions options_;
    std::filesystem::path assetsDir_;

    SDL_Window* window_ = nullptr;
    std::unique_ptr<gfx::Device> device_;
    std::unique_ptr<gfx::Renderer2D> renderer_;
    std::unique_ptr<gfx::ImGuiRenderer> imguiRenderer_;
    bool imguiReady_ = false;
    Fonts fonts_;
    float uiScale_ = 1.0f;
    float fps_ = 0.0f;
    uint64_t fpsWindowStart_ = 0;
    int fpsFrames_ = 0;
    uint64_t nextFrameNs_ = 0;  // frame limiter
    std::string rendererInfo_;

    std::unique_ptr<Mode> mode_;
    std::unique_ptr<script::Player> player_;
    std::unique_ptr<script::Recorder> recorder_;
    std::vector<std::filesystem::path> captures_;   // this frame's picture goes to these files
    ImVec2 scriptPointer_{-FLT_MAX, -FLT_MAX};      // where the script has the pointer
    double time_ = 0.0;
    uint64_t lastTicks_ = 0;
    int frameCount_ = 0;
};

} // namespace opense4::client
