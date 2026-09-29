#include "client/app.hpp"

#include "client/classic/classic_mode.hpp"
#include "client/prototype_mode.hpp"
#include "client/ui/theme.hpp"
#include "core/log.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>

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
    candidates.push_back(fs::path(OPENSE4_SOURCE_DIR) / name);
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::exists(c / marker, ec)) return fs::weakly_canonical(c, ec);
    }
    return fs::path(OPENSE4_SOURCE_DIR) / name;
}

void fatal(const std::string& message) {
    log::error("{}", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenSE4", message.c_str(), nullptr);
}

} // namespace

int App::run(const AppOptions& options) {
    options_ = options;
    SDL_SetAppMetadata("OpenSE4", "0.1.0", "org.opense4.OpenSE4");
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

    const Platform platform{window_, device_.get(), &fonts_, assetsDir_, rendererInfo_};
    std::string error;
    if (options.classic) {
        ClassicOptions co;
        co.installDir = options.classicDir;
        co.seed = options.setup.galaxy.seed;
        co.systemCount = options.setup.galaxy.systemCount;
        co.empireCount = options.setup.empireCount;
        co.quadrantType = options.quadrantType;
        mode_ = ClassicMode::create(platform, co, error);
    } else {
        PrototypeOptions po;
        po.setup = options.setup;
        po.dataDir = findDir(options.dataDir, "data", "rules.toml");
        po.startInSystemView = options.startInSystemView;
        po.autoTurns = options.autoTurns;
        log::info("Data: {}  Assets: {}", po.dataDir.string(), assetsDir_.string());
        mode_ = PrototypeMode::create(platform, po, error);
    }
    if (!mode_) {
        fatal(error);
        shutdown();
        return 1;
    }

    SDL_ShowWindow(window_);
    lastTicks_ = SDL_GetTicksNS();
    while (frame()) {
    }
    mode_.reset();
    shutdown();
    return 0;
}

bool App::createWindowAndDevice() {
    const SDL_WindowFlags common = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN |
                                   (options_.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
    gfx::DeviceOptions deviceOptions{options_.vsync, options_.validation};
    std::string error;

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
            fatal("No usable renderer. Vulkan 1.3 or OpenGL 3.3 is required.\n\n" + error);
            return false;
        }
    }

    rendererInfo_ = std::format("{}: {}", gfx::backendName(device_->backend()), device_->deviceName());
    log::info("Renderer: {}", rendererInfo_);
    SDL_SetWindowMinimumSize(window_, 960, 600);
    renderer_ = std::make_unique<gfx::Renderer2D>(*device_);
    return true;
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
}

bool App::frame() {
    bool running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) running = false;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window_)) running = false;
        if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) updateUiScale();
        // Alt+Enter toggles fullscreen (function keys belong to the game's own hotkeys).
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT) && !event.key.repeat)
            SDL_SetWindowFullscreen(window_, (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) == 0);
    }
    if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(50);
        lastTicks_ = SDL_GetTicksNS();
        return running;
    }

    const uint64_t now = SDL_GetTicksNS();
    const float dt = std::min(static_cast<float>(now - lastTicks_) * 1e-9f, 0.1f);
    lastTicks_ = now;
    time_ += dt;

    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    int ww = 0, wh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    FrameState fs{gfx::FrameInfo{static_cast<uint32_t>(pw), static_cast<uint32_t>(ph)},
                  ww > 0 ? static_cast<float>(pw) / static_cast<float>(ww) : 1.0f, uiScale_, time_, dt};

    if (!mode_->update(fs)) running = false;
    ImGui::Render();

    if (auto frameInfo = device_->beginFrame(mode_->clearColor())) {
        fs.frame = *frameInfo;
        mode_->render(*renderer_, fs);
        imguiRenderer_->render(ImGui::GetDrawData());

        ++frameCount_;
        const bool screenshotFrame = !options_.screenshotPath.empty() && frameCount_ == options_.screenshotFrames;
        if (screenshotFrame) device_->requestCapture();
        device_->endFrame();
        if (auto image = device_->takeCapture()) {
            if (gfx::writePng(options_.screenshotPath, *image))
                log::info("Saved screenshot {} ({}x{})", options_.screenshotPath, image->width, image->height);
            else
                log::error("Could not write {}", options_.screenshotPath);
            running = false;
        } else if (screenshotFrame) {
            log::error("Screenshot capture is not supported by this backend/surface");
            running = false;
        }
    }
    return running;
}

void App::shutdown() {
    if (device_) device_->waitIdle();
    mode_.reset();
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
