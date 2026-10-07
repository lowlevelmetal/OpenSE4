#include "client/window_hit.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <utility>

namespace opense4::client {

namespace {

constexpr std::array<std::pair<WindowHit, std::string_view>, 10> kNames{{
    {WindowHit::Normal, "normal"},
    {WindowHit::Drag, "drag"},
    {WindowHit::ResizeTopLeft, "resize-top-left"},
    {WindowHit::ResizeTop, "resize-top"},
    {WindowHit::ResizeTopRight, "resize-top-right"},
    {WindowHit::ResizeRight, "resize-right"},
    {WindowHit::ResizeBottomRight, "resize-bottom-right"},
    {WindowHit::ResizeBottom, "resize-bottom"},
    {WindowHit::ResizeBottomLeft, "resize-bottom-left"},
    {WindowHit::ResizeLeft, "resize-left"},
}};

// This frame's title areas and holes, as the screens report them (the main thread's).
struct FrameAreas {
    std::vector<HitBox> drag, holes;
};
FrameAreas& frameAreas() {
    static FrameAreas f;
    return f;
}

HitBox box(ImVec2 min, ImVec2 max) { return {std::min(min.x, max.x), std::min(min.y, max.y), std::max(min.x, max.x), std::max(min.y, max.y)}; }

bool overlaps(const HitBox& a, const HitBox& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

// The published areas: the system asks while events are pumped, which SDL does
// on the main thread everywhere, but a lock costs nothing here.
std::mutex& publishedLock() {
    static std::mutex m;
    return m;
}
WindowHitAreas& published() {
    static WindowHitAreas a;
    return a;
}

} // namespace

std::string_view windowHitName(WindowHit h) {
    for (const auto& [hit, name] : kNames)
        if (hit == h) return name;
    return "?";
}

std::optional<WindowHit> parseWindowHit(std::string_view name) {
    for (const auto& [hit, text] : kNames)
        if (text == name) return hit;
    return std::nullopt;
}

WindowHit windowHitAt(const WindowHitAreas& a, float x, float y) {
    if (x < 0 || y < 0 || x >= a.width || y >= a.height) return WindowHit::Normal;
    if (a.border > 0) {
        const bool left = x < a.border, right = x >= a.width - a.border;
        const bool top = y < a.border, bottom = y >= a.height - a.border;
        if (left || right || top || bottom) {
            // Near a corner, along either of its edges: both ways.
            const float c = std::max(a.corner, a.border);
            const bool nearLeft = x < c, nearRight = x >= a.width - c, nearTop = y < c, nearBottom = y >= a.height - c;
            if ((top && nearLeft) || (left && nearTop)) return WindowHit::ResizeTopLeft;
            if ((top && nearRight) || (right && nearTop)) return WindowHit::ResizeTopRight;
            if ((bottom && nearLeft) || (left && nearBottom)) return WindowHit::ResizeBottomLeft;
            if ((bottom && nearRight) || (right && nearBottom)) return WindowHit::ResizeBottomRight;
            if (top) return WindowHit::ResizeTop;
            if (bottom) return WindowHit::ResizeBottom;
            return left ? WindowHit::ResizeLeft : WindowHit::ResizeRight;
        }
    }
    for (const HitBox& h : a.holes)
        if (h.contains(x, y)) return WindowHit::Normal;
    for (const HitBox& d : a.drag)
        if (d.contains(x, y)) return WindowHit::Drag;
    return WindowHit::Normal;
}

float windowResizeBorder(float scale) { return 5.0f * std::max(scale, 0.5f); }
float windowResizeCorner(float scale) { return 16.0f * std::max(scale, 0.5f); }

void addWindowDragArea(ImVec2 min, ImVec2 max) { frameAreas().drag.push_back(box(min, max)); }
void addWindowDragHole(ImVec2 min, ImVec2 max) { frameAreas().holes.push_back(box(min, max)); }

WindowHitAreas takeWindowHitAreas(float width, float height, float border, float corner, bool active) {
    FrameAreas& f = frameAreas();
    WindowHitAreas out;
    if (active) {
        out.width = width;
        out.height = height;
        out.border = border;
        out.corner = corner;
        out.drag = std::move(f.drag);
        out.holes = std::move(f.holes);
        // Every window shown over a title area takes its own clicks there: a
        // dialog, a button row, a pop-up (tooltips aside, which come and go
        // under the pointer). Child windows lie inside their parents.
        if (ImGuiContext* g = ImGui::GetCurrentContext(); g && !out.drag.empty())
            for (ImGuiWindow* w : g->Windows) {
                if (!w->Active || w->Hidden || (w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip))) continue;
                const HitBox r = box(w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y));
                if (std::any_of(out.drag.begin(), out.drag.end(), [&](const HitBox& d) { return overlaps(d, r); })) out.holes.push_back(r);
            }
    }
    f.drag.clear();
    f.holes.clear();
    return out;
}

void publishWindowHitAreas(WindowHitAreas areas) {
    const std::lock_guard lock(publishedLock());
    published() = std::move(areas);
}

WindowHit windowHitNow(float x, float y) {
    const std::lock_guard lock(publishedLock());
    return windowHitAt(published(), x, y);
}

} // namespace opense4::client
