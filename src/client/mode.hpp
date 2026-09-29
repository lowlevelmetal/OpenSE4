#pragma once

// A Mode is one self-contained "program" running inside the app shell: the
// prototype game, or the classic-rules engine. The shell owns the window,
// the render device and Dear ImGui; the mode owns everything else.

#include "client/view_context.hpp"
#include "core/math.hpp"
#include "gfx/device.hpp"
#include "gfx/renderer2d.hpp"

#include <filesystem>
#include <string>

struct SDL_Window;

namespace opense4::client {

// Shell services available to a mode.
struct Platform {
    SDL_Window* window = nullptr;
    gfx::Device* device = nullptr;
    const Fonts* fonts = nullptr;
    std::filesystem::path assetsDir;  // our own assets (fonts)
    std::string rendererInfo;
};

struct FrameState {
    gfx::FrameInfo frame;  // framebuffer size in pixels
    float fbScale = 1.0f;  // framebuffer pixels per ImGui unit
    float uiScale = 1.0f;
    double time = 0.0;
    float dt = 0.0f;
};

class Mode {
public:
    virtual ~Mode() = default;
    // Called between ImGui::NewFrame() and ImGui::Render(): input, UI, labels.
    // Returns false when the app should quit.
    virtual bool update(const FrameState& fs) = 0;
    // Draws the scene with the 2D renderer; the ImGui overlay is drawn afterwards.
    virtual void render(gfx::Renderer2D& renderer, const FrameState& fs) = 0;
    virtual Color clearColor() const { return Color::hex(0x05070d); }
};

} // namespace opense4::client
