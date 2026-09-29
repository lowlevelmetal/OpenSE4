#pragma once

#include "client/game_session.hpp"
#include "gfx/renderer2d.hpp"

#include <optional>

struct ImFont;

namespace opense4::client {

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* medium = nullptr;
    ImFont* bold = nullptr;
};

// Everything a view or panel needs for one frame.
struct ViewContext {
    GameSession& session;
    Selection& selection;
    const Fonts& fonts;
    gfx::FrameInfo frame;  // framebuffer size in pixels
    float fbScale = 1.0f;  // framebuffer pixels per UI (ImGui) unit
    float uiScale = 1.0f;  // user/OS UI scale applied on top of UI units (for text sizes)
    double time = 0.0;
    float dt = 0.0f;
};

// Requests raised by views and panels, handled by the App.
struct NavRequest {
    std::optional<sim::SystemId> openSystem;
    std::optional<sim::Location> focus;  // open the system and center on this sector
    bool toGalaxy = false;
    bool endTurn = false;
    bool nextIdleShip = false;
    std::optional<sim::GameSetup> newGame;
    bool quit = false;
};

} // namespace opense4::client
