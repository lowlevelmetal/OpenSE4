#pragma once

#include "gfx/renderer2d.hpp"

#include <vector>

namespace opense4::client {

// Screen-space parallax star background shared by the map views.
class Starfield {
public:
    explicit Starfield(uint64_t seed);
    void draw(gfx::Renderer2D& r, gfx::FrameInfo frame, Vec2 cameraCenter, float cameraZoom, double time) const;

private:
    struct Star {
        Vec2 pos;  // [0,1) in a screen-sized tile
        float size;
        float brightness;
        float depth;  // parallax factor
        float twinklePhase;
        Color tint;
    };
    std::vector<Star> stars_;
};

} // namespace opense4::client
