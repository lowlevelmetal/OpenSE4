#pragma once

#include "core/math.hpp"

#include <cmath>
#include <optional>

namespace opense4::client {

// 2D camera. Screen space is framebuffer pixels, origin top-left, y down.
// Zoom is animated; the world point under the cursor stays fixed while zooming.
class Camera2D {
public:
    Vec2 center;
    float zoom = 1.0f;  // pixels per world unit
    Vec2 viewport{1.0f, 1.0f};
    float minZoom = 0.02f;
    float maxZoom = 50.0f;

    Vec2 worldToScreen(Vec2 w) const { return (w - center) * zoom + viewport * 0.5f; }
    Vec2 screenToWorld(Vec2 s) const { return (s - viewport * 0.5f) / zoom + center; }

    Mat4 transform() const {
        const Vec2 half = viewport * (0.5f / zoom);
        return Mat4::ortho2D(center.x - half.x, center.x + half.x, center.y - half.y, center.y + half.y);
    }

    void pan(Vec2 screenDelta) {
        center -= screenDelta / zoom;
        anchored_ = false;
        flyTarget_.reset();
    }

    void zoomAt(Vec2 screen, float factor) {
        targetZoom_ = std::clamp(targetZoom_ * factor, minZoom, maxZoom);
        anchorScreen_ = screen;
        anchorWorld_ = screenToWorld(screen);
        anchored_ = true;
        flyTarget_.reset();
    }

    void flyTo(Vec2 world, std::optional<float> newZoom = std::nullopt) {
        flyTarget_ = world;
        if (newZoom) targetZoom_ = std::clamp(*newZoom, minZoom, maxZoom);
        anchored_ = false;
    }

    // Frames `bounds` in the viewport immediately (no animation).
    void fit(const Rect& bounds, float marginPx) {
        const Vec2 size = bounds.size();
        const float zx = (viewport.x - 2.0f * marginPx) / std::max(size.x, 1.0f);
        const float zy = (viewport.y - 2.0f * marginPx) / std::max(size.y, 1.0f);
        zoom = targetZoom_ = std::clamp(std::min(zx, zy), minZoom, maxZoom);
        center = bounds.center();
        anchored_ = false;
        flyTarget_.reset();
    }

    void update(float dt) {
        const float t = 1.0f - std::exp(-dt * 14.0f);
        zoom = std::exp(std::log(zoom) + (std::log(targetZoom_) - std::log(zoom)) * t);
        if (anchored_) {
            center = anchorWorld_ - (anchorScreen_ - viewport * 0.5f) / zoom;
            if (std::abs(std::log(zoom / targetZoom_)) < 1e-3f) anchored_ = false;
        } else if (flyTarget_) {
            center = lerp(center, *flyTarget_, t);
            if (distance(center, *flyTarget_) * zoom < 0.25f) flyTarget_.reset();
        }
    }

    float targetZoom() const { return targetZoom_; }

private:
    float targetZoom_ = 1.0f;
    bool anchored_ = false;
    Vec2 anchorScreen_;
    Vec2 anchorWorld_;
    std::optional<Vec2> flyTarget_;
};

} // namespace opense4::client
