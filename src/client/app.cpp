#include "client/app.hpp"

#include "client/audio.hpp"

#include "client/classic/classic_mode.hpp"
#include "client/script/items.hpp"
#include "client/script/sdl_input.hpp"
#include "client/ui/theme.hpp"
#include "core/log.hpp"
#include "ruleset/ruleset.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <format>

namespace opense4::client {

namespace {

std::filesystem::path findDir(const std::string& override, const char* name, const char* marker) {
    namespace fs = std::filesystem;
    if (!override.empty()) return override;
    std::vector<fs::path> candidates;
    if (const char* base = SDL_GetBasePath()) {
        const fs::path b(base);
        candidates = {b / name, b / ".." / name, b / ".." / ".." / name, b / ".." / ".." / ".." / name};
    }
#ifdef OPENSE4_SOURCE_DIR
    candidates.push_back(fs::path(OPENSE4_SOURCE_DIR) / name);
#endif
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / marker, ec)) return fs::weakly_canonical(c, ec);
    }
    // Not found: the fonts built into the executable are used instead
    // (core/embedded.hpp), so a missing directory is not an error.
    return candidates.empty() ? fs::path(name) : candidates.front();
}

void fatal(const std::string& message) {
    log::error("{}", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenSE4", message.c_str(), nullptr);
}

// Neither renderer started: what the game needs, what to do about it, and why each
// one failed. Older systems (Windows 7 among them) seldom have a Vulkan 1.3
// driver, so there the OpenGL 3.3 of the graphics card maker's driver is what counts.
std::string noRendererMessage(const std::string& vulkanError, const std::string& openglError) {
    std::string text =
        "OpenSE4 found no graphics driver to draw with. It needs Vulkan 1.3 or OpenGL 3.3.\n\n"
        "Install the current driver for your graphics card from its maker (NVIDIA, AMD or Intel). "
        "The basic display driver that comes with Windows has no OpenGL 3.3, and a Remote "
        "Desktop session may have none either.\n";
    if (!vulkanError.empty()) text += "\nVulkan: " + vulkanError;
    text += "\nOpenGL: " + openglError;
    return text;
}

} // namespace

int App::run(const AppOptions& options) {
    options_ = options;
    // The identifier is the Wayland app ID and X11 window class, which desktops
    // match to packaging/linux/io.github.lowlevelmetal.OpenSE4.desktop.
    SDL_SetAppMetadata("OpenSE4", OPENSE4_CLIENT_VERSION, "io.github.lowlevelmetal.OpenSE4");
    // The game is the player's own installed copy: without one there is nothing
    // to play. Checked before any window or renderer exists.
    const auto dataDir = ruleset::findInstalledDataDir(options.installDir);
    if (!dataDir) {
        fatal(missingInstallMessage(options.installDir));
        SDL_Quit();
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fatal(std::format("SDL_Init failed: {}", SDL_GetError()));
        return 1;
    }
    assetsDir_ = findDir(options.assetsDir, "assets", "fonts");

    if (!createWindowAndDevice()) {
        shutdown();
        return 1;
    }
    initImGui();
    const bool scripted = options.inputScript || !options.recordInput.empty();
    if (scripted || options.lessonAudit) script::collectItems(true);   // the widgets scripts name (client/script/items.hpp)

    const Platform platform{window_, device_.get(), &fonts_, assetsDir_, rendererInfo_, this};
    // Saved display settings (a screenshot or script run keeps the plain window it asked for).
    if (options.screenshotPath.empty() && !options.inputScript) applyGraphics();
    std::string error;
    ClassicOptions co;
    co.installDir = dataDir->string();
    co.seed = options.seed;
    co.seedGiven = options.seedGiven;
    co.systemCount = options.systemCount;
    co.empireCount = options.empireCount;
    co.quadrantType = options.quadrantType;
    co.skipIntro = options.quickStart || (!options.screenshotPath.empty() && !options.inputScript);
    co.race = options.race;
    co.autoTurns = options.autoTurns;
    co.select = options.select;
    co.openWindow = options.openWindow;
    co.turnBased = options.turnBased;
    co.pbemFile = options.pbemFile;
    co.pbemEmpire = options.pbemEmpire;
    co.pbemPassword = options.pbemPassword;
    co.pbemOrdersDir = options.pbemOrdersDir;
    co.pbemEndTurn = options.pbemEndTurn;
    co.pbemExit = options.pbemEndTurn && options.screenshotPath.empty();
    co.tutorial = options.tutorial;
    co.training = options.training;
    co.manual = options.manual;
    co.learnDir = options.learnDir;
    co.lessonCheck = options.lessonCheck;
    co.lessonAudit = options.lessonAudit;
    co.lessonCheckQuits = options.screenshotPath.empty();
    co.scripted = scripted;
    mode_ = ClassicMode::create(platform, co, error);
    if (!mode_) {
        fatal(error);
        shutdown();
        return 1;
    }
    // Sound needs a real session: not with --no-audio, and not for screenshots or
    // scripts unless --audio asks for it (to check the audio itself).
    if (!options.noAudio && (options.audio || (options.screenshotPath.empty() && !options.inputScript))) audio().open();
    if (options.inputScript) {
        std::filesystem::path out = options.scriptOutput;
        if (out.empty()) {
            std::error_code ec;
            out = std::filesystem::temp_directory_path(ec) / "opense4-scripts";
        }
        std::error_code ec;
        std::filesystem::create_directories(out, ec);
        player_ = std::make_unique<script::Player>(*options.inputScript, out);
        log::info("Input script {} ({} steps)", options.inputScript->file, options.inputScript->steps.size());
    }
    if (!options.recordInput.empty()) recorder_ = std::make_unique<script::Recorder>(options.recordInput, options.recordOptions);

    SDL_ShowWindow(window_);
    lastTicks_ = SDL_GetTicksNS();
    while (frame()) {
    }
    int code = mode_->exitCode();
    if (player_) {
        // The script's verdict: every step done, or where it stopped.
        const script::Script& sc = player_->script();
        if (player_->finished() && !player_->failed()) {
            std::printf("input-script %s: passed (%zu steps, %llu frames)\n", sc.file.c_str(), sc.steps.size(),
                        static_cast<unsigned long long>(player_->frame()));
        } else {
            const std::string why = player_->failed() ? player_->failure() : "the game quit before the script ended";
            std::printf("input-script %s: FAILED\n%s\n", sc.file.c_str(), why.c_str());
            if (player_->failed()) std::printf("  picture: %s\n", player_->failureShot().string().c_str());
            code = 1;
        }
        std::fflush(stdout);
    }
    if (recorder_) {
        std::string problem;
        if (recorder_->save(problem)) log::info("Recorded the session as {}", recorder_->file().string());
        else log::error("{}", problem);
    }
    mode_.reset();
    shutdown();
    return code;
}

bool App::createWindowAndDevice() {
    const SDL_WindowFlags common = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN |
                                   (options_.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
    gfx::DeviceOptions deviceOptions{options_.vsync, options_.validation};
    std::string error;
    std::string vulkanError;  // why Vulkan did not start, for the message if OpenGL does not either

    if (options_.renderer != AppOptions::Renderer::OpenGL) {
        window_ = SDL_CreateWindow("OpenSE4", options_.width, options_.height, common | SDL_WINDOW_VULKAN);
        if (window_) {
            device_ = gfx::createVulkanDevice(window_, deviceOptions, error);
            if (!device_) {
                SDL_DestroyWindow(window_);
                window_ = nullptr;
            }
        } else {
            error = SDL_GetError();
        }
        if (!device_) {
            if (options_.renderer == AppOptions::Renderer::Vulkan) {
                fatal("Vulkan renderer unavailable: " + error);
                return false;
            }
            log::warn("Vulkan unavailable ({}); falling back to OpenGL", error);
            vulkanError = error;
        }
    }

    if (!device_) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG |
                                                      (options_.validation ? SDL_GL_CONTEXT_DEBUG_FLAG : 0));
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
        window_ = SDL_CreateWindow("OpenSE4", options_.width, options_.height, common | SDL_WINDOW_OPENGL);
        if (!window_) {
            fatal(std::format("Could not create a window: {}", SDL_GetError()));
            return false;
        }
        device_ = gfx::createOpenGLDevice(window_, deviceOptions, error);
        if (!device_) {
            fatal(noRendererMessage(vulkanError, error));
            return false;
        }
    }

    rendererInfo_ = std::format("{}: {}", gfx::backendName(device_->backend()), device_->deviceName());
    log::info("Renderer: {}", rendererInfo_);
    SDL_SetWindowMinimumSize(window_, 800, 600);  // the classic 800x600 layout fits (docs/spec/06 §2.1.1)
    renderer_ = std::make_unique<gfx::Renderer2D>(*device_);
    return true;
}

void App::minimize() { SDL_MinimizeWindow(window_); }

void App::applyGraphics() {
    const GraphicsSettings& g = appSettings().graphics;
    device_->setVSync(g.vsync);
    switch (g.displayMode) {
        case DisplayMode::Windowed:
            SDL_SetWindowFullscreen(window_, false);
            SDL_SetWindowSize(window_, g.windowWidth, g.windowHeight);
            break;
        case DisplayMode::Borderless:
            SDL_SetWindowFullscreenMode(window_, nullptr);  // the desktop mode
            SDL_SetWindowFullscreen(window_, true);
            break;
        case DisplayMode::Fullscreen: {
            SDL_DisplayMode mode{};
            const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
            const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
            const int w = g.fullscreenWidth > 0 ? g.fullscreenWidth : desktop ? desktop->w : g.windowWidth;
            const int h = g.fullscreenHeight > 0 ? g.fullscreenHeight : desktop ? desktop->h : g.windowHeight;
            if (SDL_GetClosestFullscreenDisplayMode(display, w, h, g.fullscreenRefresh, true, &mode))
                SDL_SetWindowFullscreenMode(window_, &mode);
            else
                SDL_SetWindowFullscreenMode(window_, nullptr);
            SDL_SetWindowFullscreen(window_, true);
            break;
        }
    }
    SDL_SyncWindow(window_);
}

std::vector<DisplayModeInfo> App::displayModes() const {
    std::vector<DisplayModeInfo> out;
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window_), &count);
    if (!modes) return out;
    for (int i = 0; i < count; ++i) out.push_back({modes[i]->w, modes[i]->h, modes[i]->refresh_rate});
    SDL_free(modes);
    return out;
}

void App::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed layout for now
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplSDL3_InitForOther(window_);
    fonts_ = loadFonts(assetsDir_);
    updateUiScale();
    imguiRenderer_ = std::make_unique<gfx::ImGuiRenderer>(*device_);
    imguiReady_ = true;
}

void App::updateUiScale() {
    const float density = SDL_GetWindowPixelDensity(window_);
    const float display = SDL_GetWindowDisplayScale(window_);
    uiScale_ = (density > 0.0f && display > 0.0f) ? std::clamp(display / density, 0.5f, 4.0f) : 1.0f;
    applyTheme(uiScale_);
    if (mode_) mode_->restyle();
}

EventVerdict App::handleEvent(SDL_Event& event, bool& running) {
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) event.key.mod = altGrAsAlt(event.key.mod);
    // The mode may hold input back (a tutorial's input lock) before ImGui sees it.
    const EventVerdict verdict = mode_->filterEvent(event);
    switch (verdict) {
        case EventVerdict::Pass: ImGui_ImplSDL3_ProcessEvent(&event); break;
        case EventVerdict::Drop: break;
        case EventVerdict::PointerAway: ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX); break;
    }
    if (event.type == SDL_EVENT_QUIT) running = false;
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window_)) running = false;
    if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) updateUiScale();
    return verdict;
}

bool App::frame() {
    bool running = true;
    // A screenshot or script run (automation) must give the same picture on
    // every machine: a fixed frame time instead of the wall clock, and no
    // pointer over the window but the script's (the desktop's pointer would
    // hover over whatever is under it).
    const bool automation = !options_.screenshotPath.empty() || player_;
    audio().update();
    // An input script: this frame's events, from what the frame drawn last
    // showed (docs/BUILDING.md "Input scripts").
    const script::Probe* probe = mode_->probe();
    script::FrameOutput scripted;
    if (player_ && !probe) {
        log::error("This mode cannot play input scripts");
        running = false;
    } else if (player_) {
        scripted = player_->tick(*probe);
        for (const std::string& m : scripted.messages) {
            if (m.starts_with("FAILED")) log::error("{}", m);
            else log::info("script{}", m.starts_with("  ") ? m.substr(1) : ": " + m);
        }
        captures_ = std::move(scripted.captures);
    }
    if (recorder_ && probe) recorder_->frame(*probe, time_);
    const SDL_WindowID window = SDL_GetWindowID(window_);
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (player_ && script::isUserInput(event)) continue;   // a script plays alone
        if (recorder_ && probe && script::isUserInput(event)) recorder_->event(event, *probe, time_);
        handleEvent(event, running);
        // The window going into the background or coming back: a player's own
        // run follows the system; automation keeps its sound unless its script
        // says otherwise (a window-event step, below).
        if (!automation) presence_.follow(event, window);
    }
    // The script's events go the same way as a player's.
    if (player_) {
        std::vector<script::Verdict> verdicts;
        for (const script::InputEvent& e : scripted.events) {
            SDL_Event ev = script::toSdlEvent(e, window);
            if (recorder_ && probe) recorder_->event(ev, *probe, time_);   // recording a script run tests the recorder
            const EventVerdict verdict = handleEvent(ev, running);
            presence_.follow(ev, window);
            // Where Dear ImGui has the pointer now: the event's place, or nowhere when the lock said so.
            const bool pointer = e.kind == script::InputEvent::Kind::Motion || e.kind == script::InputEvent::Kind::ButtonDown ||
                                 e.kind == script::InputEvent::Kind::ButtonUp || e.kind == script::InputEvent::Kind::Wheel;
            if (pointer && verdict == EventVerdict::PointerAway) scriptPointer_ = ImVec2(-FLT_MAX, -FLT_MAX);
            else if (pointer && verdict == EventVerdict::Pass) scriptPointer_ = e.pos;
            switch (verdict) {
                case EventVerdict::Pass: verdicts.push_back(script::Verdict::Pass); break;
                case EventVerdict::Drop: verdicts.push_back(script::Verdict::Drop); break;
                case EventVerdict::PointerAway: verdicts.push_back(script::Verdict::PointerAway); break;
            }
        }
        player_->verdicts(verdicts);
        // Done or failed: this frame is the last (with its picture).
        if (player_->finished() || player_->failed()) {
            if (player_->finished() && !options_.screenshotPath.empty()) captures_.emplace_back(options_.screenshotPath);
            running = false;
        }
    }
    // Once per frame, after all of its events: a window covered and uncovered
    // in one go (as Wayland does while resizing) does not dip the sound.
    audio().setBackground(presence_.background());
    if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
        // Windows reports minimizing (Wayland mostly does not): keep the game's
        // network traffic going, or the host would drop us.
        mode_->background();
        SDL_Delay(50);
        lastTicks_ = SDL_GetTicksNS();
        return running;
    }

    const uint64_t now = SDL_GetTicksNS();
    const float dt = automation ? 1.0f / 60.0f : std::min(static_cast<float>(now - lastTicks_) * 1e-9f, 0.1f);
    lastTicks_ = now;
    time_ += dt;

    ImGui_ImplSDL3_NewFrame();
    if (automation) {
        ImGui::GetIO().DeltaTime = dt;
        // The pointer is the script's, or none: while the window has the focus and
        // no button is down, the SDL backend puts the desktop's pointer in on the
        // video drivers that report it (Windows, X11, macOS), which would move
        // the script's pointer to wherever the real mouse is.
        const ImVec2 p = player_ ? scriptPointer_ : ImVec2(-FLT_MAX, -FLT_MAX);
        ImGui::GetIO().AddMousePosEvent(p.x, p.y);
    }
    ImGui::NewFrame();
    const AppSettings& prefs = appSettings();
    ImGui::GetIO().MouseDoubleClickTime = prefs.controls.doubleClickSeconds;
    // The fullscreen toggle (Alt+Enter by default) switches between the window and borderless fullscreen.
    if (prefs.controls.bindings.pressed(Action::ToggleFullscreen))
        SDL_SetWindowFullscreen(window_, (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) == 0);
    // Frame rate, for the optional counter.
    ++fpsFrames_;
    if (now - fpsWindowStart_ >= 500'000'000ull) {
        fps_ = static_cast<float>(fpsFrames_) * 1e9f / static_cast<float>(now - fpsWindowStart_);
        fpsFrames_ = 0;
        fpsWindowStart_ = now;
    }
    if (prefs.graphics.showFps)
        ImGui::GetForegroundDrawList()->AddText(ImVec2(6, 4), IM_COL32(255, 230, 120, 230), std::format("{:.0f} fps", fps_).c_str());
    int ww = 0, wh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    FrameState fs{gfx::FrameInfo{static_cast<uint32_t>(pw), static_cast<uint32_t>(ph)},
                  ww > 0 ? static_cast<float>(pw) / static_cast<float>(ww) : 1.0f, uiScale_, time_, dt};

    if (!mode_->update(fs)) running = false;
    script::endItemFrame();
    ImGui::Render();

    // Frame limit when vsync is off.
    if (const int limit = prefs.graphics.frameLimit; limit > 0 && !prefs.graphics.vsync) {
        const uint64_t period = 1'000'000'000ull / static_cast<uint64_t>(limit);
        const uint64_t t = SDL_GetTicksNS();
        if (nextFrameNs_ > t) SDL_DelayPrecise(nextFrameNs_ - t);
        nextFrameNs_ = std::max(nextFrameNs_ + period, SDL_GetTicksNS());
    }

    if (auto frameInfo = device_->beginFrame(mode_->clearColor())) {
        fs.frame = *frameInfo;
        mode_->render(*renderer_, fs);
        imguiRenderer_->render(ImGui::GetDrawData());

        ++frameCount_;
        // --screenshot: frame N; with a script, the pictures it asks for (and the last frame).
        const bool screenshotFrame = !player_ && !options_.screenshotPath.empty() && frameCount_ == options_.screenshotFrames;
        if (screenshotFrame) captures_.emplace_back(options_.screenshotPath);
        const bool capture = !captures_.empty();
        if (capture) device_->requestCapture();
        device_->endFrame();
        if (auto image = device_->takeCapture()) {
            for (const std::filesystem::path& file : captures_) {
                if (gfx::writePng(file.string(), *image)) log::info("Saved screenshot {} ({}x{})", file.string(), image->width, image->height);
                else log::error("Could not write {}", file.string());
            }
            if (screenshotFrame) running = false;
        } else if (capture) {
            log::error("Screenshot capture is not supported by this backend/surface");
            if (screenshotFrame) running = false;
        }
        captures_.clear();
    }
    return running;
}

void App::shutdown() {
    if (device_) device_->waitIdle();
    mode_.reset();
    audio().close();
    if (imguiReady_) {
        imguiRenderer_.reset();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    renderer_.reset();
    device_.reset();
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
}

} // namespace opense4::client
