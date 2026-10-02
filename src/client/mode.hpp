#pragma once

// A Mode is the "program" running inside the app shell: the classic client
// (ClassicMode, client/classic/). The shell owns the window, the render
// device and Dear ImGui; the mode owns everything else.

#include "client/fonts.hpp"
#include "core/math.hpp"
#include "gfx/device.hpp"
#include "gfx/renderer2d.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace opense4::client {

struct DisplayModeInfo {
    int width = 0;
    int height = 0;
    float refresh = 0.0f;
};

// What windows may ask of the application shell (the Settings window).
class AppControl {
public:
    virtual ~AppControl() = default;
    // Applies the display settings of appSettings() (window mode and size, vsync, limits).
    virtual void applyGraphics() = 0;
    // Exclusive fullscreen modes of the window's display, best first.
    virtual std::vector<DisplayModeInfo> displayModes() const = 0;
    virtual std::string rendererInfo() const = 0;
    virtual gfx::Backend backend() const = 0;
    virtual float fps() const = 0;
    // The status bar's minimize button (docs/spec/06 §2.2).
    virtual void minimize() {}
};

// Shell services available to a mode.
struct Platform {
    SDL_Window* window = nullptr;
    gfx::Device* device = nullptr;
    const Fonts* fonts = nullptr;
    std::filesystem::path assetsDir;  // our own assets (fonts)
    std::string rendererInfo;
    AppControl* app = nullptr;
};

struct FrameState {
    gfx::FrameInfo frame;  // framebuffer size in pixels
    float fbScale = 1.0f;  // framebuffer pixels per ImGui unit
    float uiScale = 1.0f;
    double time = 0.0;
    float dt = 0.0f;
};

// What the shell does with an input event (Mode::filterEvent).
enum class EventVerdict : uint8_t {
    Pass,         // to Dear ImGui as it is
    Drop,         // nowhere
    PointerAway,  // tell Dear ImGui the pointer is nowhere (nothing hovered)
};

class Mode {
public:
    virtual ~Mode() = default;
    // Called for every input event before Dear ImGui sees it (the tutorial
    // input lock, docs/LEARNING.md).
    virtual EventVerdict filterEvent(const SDL_Event& event) {
        (void)event;
        return EventVerdict::Pass;
    }
    // The program's exit code once update() asked to quit (0: success).
    virtual int exitCode() const { return 0; }
    // Called between ImGui::NewFrame() and ImGui::Render(): input, UI, labels.
    // Returns false when the app should quit.
    virtual bool update(const FrameState& fs) = 0;
    // Draws the scene with the 2D renderer; the ImGui overlay is drawn afterwards.
    virtual void render(gfx::Renderer2D& renderer, const FrameState& fs) = 0;
    // Called instead of update() while the window is minimized (no frames are
    // drawn then): what must go on regardless, such as a network game's
    // traffic (a host drops a client that sends nothing for a minute).
    virtual void background() {}
    // The app reset ImGui's style (the window moved to a display with another
    // scale): a mode with its own look applies it again.
    virtual void restyle() {}
    virtual Color clearColor() const { return Color::hex(0x05070d); }
};

} // namespace opense4::client
