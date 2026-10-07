#pragma once

// The game's window without the system's title bar (Settings → Graphics,
// "Hide the window's title bar"; GraphicsSettings::hideTitleBar). The system
// asks the game, through SDL's window hit test, what a press at each point of
// the window does:
//
// - along the window's edges and in its corners it resizes the window
//   (unless the window is maximized);
// - on the game's own title areas it moves the window: the main window's top
//   row (the status bar, from its flag to its minimize button) and the band
//   along the top of the title screens and of a hotseat game's Next Player
//   screen;
// - everywhere else, and wherever a Dear ImGui window or a part of the game
//   that answers clicks lies over a title area, the press goes to the game.
//
// The answer comes from what the frame drawn last showed: during a frame the
// classic screens say where their title areas are (addWindowDragArea) and what
// over them keeps its clicks (addWindowDragHole); at the frame's end the app
// collects them with Dear ImGui's windows (takeWindowHitAreas) and publishes
// them for the system's questions (publishWindowHitAreas, windowHitNow).
// Input scripts check the answers with assert-window-hit (docs/BUILDING.md).
// Coordinates are the window's, which are Dear ImGui's units.

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace opense4::client {

enum class WindowHit : uint8_t {
    Normal,   // the game gets the press
    Drag,     // moves the window
    ResizeTopLeft,
    ResizeTop,
    ResizeTopRight,
    ResizeRight,
    ResizeBottomRight,
    ResizeBottom,
    ResizeBottomLeft,
    ResizeLeft,
};
// "normal", "drag", "resize-top-left", "resize-top", ... (input scripts).
std::string_view windowHitName(WindowHit h);
std::optional<WindowHit> parseWindowHit(std::string_view name);

struct HitBox {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

struct WindowHitAreas {
    float width = 0, height = 0;   // the window's size
    float border = 0;              // the band along the edges that resizes; 0: none
    float corner = 0;              // along an edge, the part next to a corner that resizes both ways
    std::vector<HitBox> drag;      // the title areas
    std::vector<HitBox> holes;     // over them: what keeps its clicks
};

// What a press at (x, y) does (pure; tests/test_window_hit.cpp).
WindowHit windowHitAt(const WindowHitAreas& areas, float x, float y);

// The resize band and corner for a window at the desktop's `scale` (window
// units per design pixel): 5 and 16 design pixels.
float windowResizeBorder(float scale);
float windowResizeCorner(float scale);

// During a frame: a title area of the game's picture (drawn behind every
// window), and a part over one that answers clicks itself.
void addWindowDragArea(ImVec2 min, ImVec2 max);
void addWindowDragHole(ImVec2 min, ImVec2 max);

// At a frame's end (after ImGui::Render): the frame's title areas, with every
// Dear ImGui window shown over them as a hole, for a window of that size and
// resize band; `active` false (a window with its title bar, or fullscreen)
// gives none, so every press goes to the game. Clears the frame's areas.
WindowHitAreas takeWindowHitAreas(float width, float height, float border, float corner, bool active);

// The areas the system's questions are answered from, until the next frame
// publishes others; and the answer for a point. Safe from any thread.
void publishWindowHitAreas(WindowHitAreas areas);
WindowHit windowHitNow(float x, float y);

} // namespace opense4::client
