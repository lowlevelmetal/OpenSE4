#include "client/views/starfield.hpp"

#include "core/rng.hpp"

#include <cmath>

namespace opense4::client {

Starfield::Starfield(uint64_t seed) {
    Rng rng(seed);
    constexpr int kCount = 900;
    stars_.reserve(kCount);
    static constexpr Color kTints[] = {Color::hex(0xffffff), Color::hex(0xcfe0ff), Color::hex(0xffe8c8), Color::hex(0xc8d8ff)};
    for (int i = 0; i < kCount; ++i) {
        Star s;
        s.pos = {rng.unit(), rng.unit()};
        const float r = rng.unit();
        s.size = 0.5f + r * r * r * 1.4f;
        s.brightness = 0.25f + rng.unit() * 0.6f;
        s.depth = 0.01f + rng.unit() * 0.05f;
        s.twinklePhase = rng.unit() * kTau;
        s.tint = kTints[rng.below(4)];
        stars_.push_back(s);
    }
}

void Starfield::draw(gfx::Renderer2D& r, gfx::FrameInfo frame, Vec2 cameraCenter, float cameraZoom, double time) const {
    const float w = static_cast<float>(frame.width);
    const float h = static_cast<float>(frame.height);
    r.begin(Mat4::ortho2D(0.0f, w, 0.0f, h), frame, 1.0f);
    // Tile large enough to avoid visible repetition on big screens.
    const float tile = std::max({w, h, 1024.0f});
    for (const Star& s : stars_) {
        const Vec2 offset = cameraCenter * (cameraZoom * s.depth);
        float x = std::fmod(s.pos.x * tile - offset.x, tile);
        float y = std::fmod(s.pos.y * tile - offset.y, tile);
        if (x < 0) x += tile;
        if (y < 0) y += tile;
        if (x > w || y > h) continue;
        const float twinkle = 0.85f + 0.15f * std::sin(static_cast<float>(time) * 1.3f + s.twinklePhase);
        const Color c = s.tint.withAlpha(s.brightness * twinkle);
        r.disc({x, y}, s.size, c);
        if (s.size > 1.3f) r.glow({x, y}, s.size * 4.0f, s.tint.withAlpha(0.18f * twinkle), 2.5f);
    }
    r.flush();
}

} // namespace opense4::client
