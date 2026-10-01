#pragma once

// Immediate-mode batched 2D drawing on top of gfx::Device. Shapes are
// antialiased analytically in the shader (SDF discs, rings, lines and glows),
// so they stay crisp at every zoom level. Thicknesses given in pixels are
// screen-space and independent of the current zoom.

#include "gfx/device.hpp"

#include <span>
#include <vector>

namespace opense4::gfx {

class Renderer2D {
public:
    explicit Renderer2D(Device& device) : device_(device) {}

    // `transform` maps draw coordinates to clip space; `pixelsPerUnit` is the
    // zoom (framebuffer pixels per draw unit) used for pixel-sized widths.
    void begin(const Mat4& transform, FrameInfo frame, float pixelsPerUnit);
    void flush();

    float unitsPerPixel() const { return unitsPerPixel_; }

    void rect(const Rect& r, Color c);
    void rectOutline(const Rect& r, float thicknessPx, Color c);
    void sprite(TextureId tex, const Rect& dst, const Rect& uv, Color tint = {});
    // A sprite on any quad (corners clockwise from the picture's top left), e.g. turned.
    void spriteQuad(TextureId tex, const std::array<Vec2, 4>& corners, const Rect& uv, Color tint = {});
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color color);
    void convexPolygon(std::span<const Vec2> points, Color c);

    void disc(Vec2 center, float radius, Color c);
    void ring(Vec2 center, float radius, float thicknessPx, Color c);
    void glow(Vec2 center, float radius, Color c, float falloff = 2.0f);
    void line(Vec2 a, Vec2 b, float thicknessPx, Color c);
    void dashedLine(Vec2 a, Vec2 b, float thicknessPx, float dashPx, float gapPx, Color c);

private:
    void useTexture(TextureId tex);
    void quad(const std::array<Vec2, 4>& pos, const std::array<Vec2, 4>& uv, uint32_t color, ShapeMode mode, float param);

    Device& device_;
    Mat4 transform_;
    FrameInfo frame_;
    float unitsPerPixel_ = 1.0f;
    TextureId currentTexture_;
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<DrawCommand> commands_;
};

} // namespace opense4::gfx
