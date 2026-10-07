// The window without the system's title bar (client/window_hit.hpp): which
// parts of the window move it, resize it, or leave the press to the game.

#include "client/window_hit.hpp"

#include <doctest/doctest.h>

#include <imgui.h>

using namespace opense4::client;

namespace {

// A 1024 x 768 window with a 5-unit resize band, 16-unit corners and a title
// row 31 high, the minimize button's window over its right end.
WindowHitAreas classicWindow() {
    WindowHitAreas a;
    a.width = 1024;
    a.height = 768;
    a.border = 5;
    a.corner = 16;
    a.drag.push_back({0, 0, 1024, 31});
    a.holes.push_back({969, 7, 1013, 27});
    return a;
}

// A Dear ImGui context of its own, for frames without a renderer (as test_imgui_errors.cpp).
struct Context {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* ctx = ImGui::CreateContext();
    Context() {
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1024, 768);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();
    }
    ~Context() {
        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(previous);
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};

void window(const char* name, ImVec2 pos, ImVec2 size, ImGuiWindowFlags flags = 0) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | flags);
    ImGui::End();
}

} // namespace

TEST_CASE("window hit: the title row moves the window, what lies over it keeps its clicks") {
    const WindowHitAreas a = classicWindow();
    CHECK(windowHitAt(a, 500, 15) == WindowHit::Drag);      // the status bar's date
    CHECK(windowHitAt(a, 30, 20) == WindowHit::Drag);       // its flag
    CHECK(windowHitAt(a, 990, 15) == WindowHit::Normal);    // the minimize button
    CHECK(windowHitAt(a, 500, 31) == WindowHit::Normal);    // just below the row: the order strip
    CHECK(windowHitAt(a, 500, 400) == WindowHit::Normal);   // the map
    // Outside the window, nothing.
    CHECK(windowHitAt(a, -1, 15) == WindowHit::Normal);
    CHECK(windowHitAt(a, 1024, 15) == WindowHit::Normal);
}

TEST_CASE("window hit: the edges resize the window, the corners both ways, and the edge comes before the title row") {
    const WindowHitAreas a = classicWindow();
    CHECK(windowHitAt(a, 0, 400) == WindowHit::ResizeLeft);
    CHECK(windowHitAt(a, 4.9f, 400) == WindowHit::ResizeLeft);
    CHECK(windowHitAt(a, 5, 400) == WindowHit::Normal);
    CHECK(windowHitAt(a, 1023, 400) == WindowHit::ResizeRight);
    CHECK(windowHitAt(a, 500, 767) == WindowHit::ResizeBottom);
    CHECK(windowHitAt(a, 500, 0) == WindowHit::ResizeTop);   // over the title row: the edge wins
    CHECK(windowHitAt(a, 500, 5) == WindowHit::Drag);
    // A corner, from either of its edges as far as the corner length.
    CHECK(windowHitAt(a, 0, 0) == WindowHit::ResizeTopLeft);
    CHECK(windowHitAt(a, 15, 2) == WindowHit::ResizeTopLeft);
    CHECK(windowHitAt(a, 2, 15) == WindowHit::ResizeTopLeft);
    CHECK(windowHitAt(a, 16, 2) == WindowHit::ResizeTop);
    CHECK(windowHitAt(a, 1023, 0) == WindowHit::ResizeTopRight);
    CHECK(windowHitAt(a, 1020, 760) == WindowHit::ResizeBottomRight);
    CHECK(windowHitAt(a, 2, 760) == WindowHit::ResizeBottomLeft);
    // A corner shorter than the band is the band.
    WindowHitAreas small = a;
    small.corner = 0;
    CHECK(windowHitAt(small, 4, 4) == WindowHit::ResizeTopLeft);
    CHECK(windowHitAt(small, 6, 2) == WindowHit::ResizeTop);
}

TEST_CASE("window hit: a maximized window has no resize band, and with no areas every press goes to the game") {
    WindowHitAreas a = classicWindow();
    a.border = 0;
    CHECK(windowHitAt(a, 0, 400) == WindowHit::Normal);
    CHECK(windowHitAt(a, 500, 0) == WindowHit::Drag);
    CHECK(windowHitAt(a, 1023, 767) == WindowHit::Normal);
    const WindowHitAreas none;
    CHECK(windowHitAt(none, 0, 0) == WindowHit::Normal);
    CHECK(windowHitAt(none, 500, 15) == WindowHit::Normal);
}

TEST_CASE("window hit: names as scripts write them") {
    for (const WindowHit h : {WindowHit::Normal, WindowHit::Drag, WindowHit::ResizeTopLeft, WindowHit::ResizeTop, WindowHit::ResizeTopRight,
                              WindowHit::ResizeRight, WindowHit::ResizeBottomRight, WindowHit::ResizeBottom, WindowHit::ResizeBottomLeft,
                              WindowHit::ResizeLeft})
        CHECK(parseWindowHit(windowHitName(h)) == h);
    CHECK(windowHitName(WindowHit::ResizeBottomLeft) == "resize-bottom-left");
    CHECK_FALSE(parseWindowHit("move").has_value());
    // The band and corner grow with the desktop's scale.
    CHECK(windowResizeBorder(1.0f) == doctest::Approx(5.0));
    CHECK(windowResizeBorder(2.0f) == doctest::Approx(10.0));
    CHECK(windowResizeCorner(1.5f) == doctest::Approx(24.0));
}

TEST_CASE("window hit: a frame's title areas, Dear ImGui's windows over them as holes, published for the system") {
    Context c;
    ImGui::NewFrame();
    addWindowDragArea(ImVec2(0, 0), ImVec2(1024, 31));
    addWindowDragHole(ImVec2(300, 5), ImVec2(200, 25));   // a name cut short: corners in any order
    window("##buttons", ImVec2(969, 7), ImVec2(44, 20));
    window("##dialog", ImVec2(100, 20), ImVec2(400, 300));
    window("##elsewhere", ImVec2(100, 400), ImVec2(100, 100));   // not over the row: no hole
    ImGui::Render();
    const WindowHitAreas a = takeWindowHitAreas(1024, 768, 5, 16, true);
    CHECK(a.drag.size() == 1);
    CHECK(a.holes.size() == 3);
    CHECK(windowHitAt(a, 50, 15) == WindowHit::Drag);
    CHECK(windowHitAt(a, 250, 15) == WindowHit::Normal);   // the cut name
    CHECK(windowHitAt(a, 450, 25) == WindowHit::Normal);   // the dialog over the row
    CHECK(windowHitAt(a, 700, 15) == WindowHit::Drag);
    CHECK(windowHitAt(a, 990, 15) == WindowHit::Normal);   // the buttons' window
    // Published: what the system's question is answered from.
    publishWindowHitAreas(a);
    CHECK(windowHitNow(700, 15) == WindowHit::Drag);
    CHECK(windowHitNow(0, 400) == WindowHit::ResizeLeft);

    // The areas are the frame's: the next frame starts without them, and
    // with the title bar shown (or fullscreen) every press goes to the game.
    ImGui::NewFrame();
    addWindowDragArea(ImVec2(0, 0), ImVec2(1024, 31));
    ImGui::Render();
    const WindowHitAreas off = takeWindowHitAreas(1024, 768, 5, 16, false);
    CHECK(off.drag.empty());
    CHECK(windowHitAt(off, 700, 15) == WindowHit::Normal);
    ImGui::NewFrame();
    ImGui::Render();
    CHECK(takeWindowHitAreas(1024, 768, 5, 16, true).drag.empty());
    publishWindowHitAreas({});
    CHECK(windowHitNow(700, 15) == WindowHit::Normal);
}
