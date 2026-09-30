#pragma once

#include "client/mode.hpp"
#include "client/view_context.hpp"
#include "gfx/device.hpp"
#include "gfx/imgui_renderer.hpp"
#include "gfx/renderer2d.hpp"
#include "sim/setup.hpp"

#include <filesystem>
#include <memory>
#include <string>

struct SDL_Window;

namespace opense4::client {

struct AppOptions {
    enum class Renderer { Auto, Vulkan, OpenGL };
    Renderer renderer = Renderer::Auto;
    bool validation = false;
    bool vsync = true;
    bool fullscreen = false;
    int width = 1600;
    int height = 900;
    std::string dataDir;
    std::string assetsDir;
    sim::GameSetup setup;

    // Classic-rules engine (reads the player's installed classic data set).
    bool classic = false;
    std::string classicDir;  // install or Data directory; empty = auto-detect
    std::string quadrantType;
    bool classicQuickStart = false;  // skip the intro and start a quick game
    std::string classicRace;         // race preset for the quick game
    std::string classicWindow;       // window to open at start

    // Automation: render a few frames, save a PNG and exit.
    std::string screenshotPath;
    int screenshotFrames = 10;
    bool startInSystemView = false;
    int autoTurns = 0;
};

// The application shell: window, render device (Vulkan with OpenGL fallback),
// Dear ImGui, the frame loop and screenshots. The game itself is a Mode.
class App {
public:
    int run(const AppOptions& options);

private:
    bool createWindowAndDevice();
    void initImGui();
    void updateUiScale();
    bool frame();
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
    std::string rendererInfo_;

    std::unique_ptr<Mode> mode_;
    double time_ = 0.0;
    uint64_t lastTicks_ = 0;
    int frameCount_ = 0;
};

} // namespace opense4::client
