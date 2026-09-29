#include "gfx/renderer2d.hpp"

namespace opense4::gfx {

void Renderer2D::begin(const Mat4& transform, FrameInfo frame, float pixelsPerUnit) {
    transform_ = transform;
    frame_ = frame;
    unitsPerPixel_ = pixelsPerUnit > 0.0f ? 1.0f / pixelsPerUnit : 1.0f;
    currentTexture_ = TextureId{};
    vertices_.clear();
    indices_.clear();
    commands_.clear();
}

void Renderer2D::flush() {
    if (!indices_.empty()) device_.draw(DrawBatch{transform_, vertices_, indices_, commands_});
    vertices_.clear();
    indices_.clear();
    commands_.clear();
}

void Renderer2D::useTexture(TextureId tex) {
    if (!commands_.empty() && commands_.back().texture == tex) return;
    currentTexture_ = tex;
    if (!commands_.empty() && commands_.back().indexCount == 0) {
        commands_.back().texture = tex;
        return;
    }
    DrawCommand cmd;
    cmd.texture = tex;
    cmd.firstIndex = static_cast<uint32_t>(indices_.size());
    cmd.scissor = Scissor{0, 0, frame_.width, frame_.height};
    commands_.push_back(cmd);
}

void Renderer2D::quad(const std::array<Vec2, 4>& pos, const std::array<Vec2, 4>& uv, uint32_t color, ShapeMode mode,
                      float param) {
    if (commands_.empty()) useTexture(currentTexture_);
    const auto base = static_cast<uint32_t>(vertices_.size());
    const float m = static_cast<float>(mode);
    for (size_t i = 0; i < 4; ++i) vertices_.push_back(Vertex{pos[i].x, pos[i].y, uv[i].x, uv[i].y, color, m, param});
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) indices_.push_back(base + i);
    commands_.back().indexCount += 6;
}

void Renderer2D::rect(const Rect& r, Color c) {
    useTexture(TextureId{});  // invalid id = the device's built-in white texture
    quad({r.min, Vec2{r.max.x, r.min.y}, r.max, Vec2{r.min.x, r.max.y}}, {Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}},
         c.toRgba8(), ShapeMode::Textured, 0.0f);
}

void Renderer2D::rectOutline(const Rect& r, float thicknessPx, Color c) {
    const Vec2 a = r.min, b{r.max.x, r.min.y}, d = r.max, e{r.min.x, r.max.y};
    line(a, b, thicknessPx, c);
    line(b, d, thicknessPx, c);
    line(d, e, thicknessPx, c);
    line(e, a, thicknessPx, c);
}

void Renderer2D::sprite(TextureId tex, const Rect& dst, const Rect& uv, Color tint) {
    useTexture(tex);
    quad({dst.min, Vec2{dst.max.x, dst.min.y}, dst.max, Vec2{dst.min.x, dst.max.y}},
         {uv.min, Vec2{uv.max.x, uv.min.y}, uv.max, Vec2{uv.min.x, uv.max.y}}, tint.toRgba8(), ShapeMode::Textured, 0.0f);
}

void Renderer2D::triangle(Vec2 a, Vec2 b, Vec2 c, Color color) {
    const Vec2 pts[] = {a, b, c};
    convexPolygon(pts, color);
}

void Renderer2D::convexPolygon(std::span<const Vec2> points, Color c) {
    if (points.size() < 3) return;
    useTexture(TextureId{});
    const auto base = static_cast<uint32_t>(vertices_.size());
    const uint32_t color = c.toRgba8();
    for (Vec2 p : points) vertices_.push_back(Vertex{p.x, p.y, 0.5f, 0.5f, color, 0.0f, 0.0f});
    for (uint32_t i = 1; i + 1 < points.size(); ++i) {
        indices_.push_back(base);
        indices_.push_back(base + i);
        indices_.push_back(base + i + 1);
        commands_.back().indexCount += 3;
    }
}

void Renderer2D::disc(Vec2 center, float radius, Color c) {
    const Vec2 h{radius, radius};
    quad({center - h, Vec2{center.x + radius, center.y - radius}, center + h, Vec2{center.x - radius, center.y + radius}},
         {Vec2{-1, -1}, Vec2{1, -1}, Vec2{1, 1}, Vec2{-1, 1}}, c.toRgba8(), ShapeMode::Disc, 0.0f);
}

void Renderer2D::ring(Vec2 center, float radius, float thicknessPx, Color c) {
    // The shader's AA band is the last pixel inside each edge; offsetting both
    // edges by half a pixel centers the bands on the requested ring.
    const float half = thicknessPx * 0.5f * unitsPerPixel_;
    const float outer = radius + half + 0.5f * unitsPerPixel_;
    const float inner = std::max(0.0f, radius - half + 0.5f * unitsPerPixel_) / outer;
    const Vec2 h{outer, outer};
    quad({center - h, Vec2{center.x + outer, center.y - outer}, center + h, Vec2{center.x - outer, center.y + outer}},
         {Vec2{-1, -1}, Vec2{1, -1}, Vec2{1, 1}, Vec2{-1, 1}}, c.toRgba8(), ShapeMode::Disc, std::max(inner, 1e-3f));
}

void Renderer2D::glow(Vec2 center, float radius, Color c, float falloff) {
    const Vec2 h{radius, radius};
    quad({center - h, Vec2{center.x + radius, center.y - radius}, center + h, Vec2{center.x - radius, center.y + radius}},
         {Vec2{-1, -1}, Vec2{1, -1}, Vec2{1, 1}, Vec2{-1, 1}}, c.toRgba8(), ShapeMode::Glow, falloff);
}

void Renderer2D::line(Vec2 a, Vec2 b, float thicknessPx, Color c) {
    const Vec2 d = b - a;
    const float len = length(d);
    if (len <= 0.0f) return;
    const Vec2 dir = d / len;
    const Vec2 n = perp(dir) * ((thicknessPx * 0.5f + 0.75f) * unitsPerPixel_);  // + half the 1.5px AA band
    const Vec2 ext = dir * (0.5f * unitsPerPixel_);
    const Vec2 a2 = a - ext, b2 = b + ext;
    quad({a2 + n, b2 + n, b2 - n, a2 - n}, {Vec2{0, -1}, Vec2{1, -1}, Vec2{1, 1}, Vec2{0, 1}}, c.toRgba8(), ShapeMode::Line, 0.0f);
}

void Renderer2D::dashedLine(Vec2 a, Vec2 b, float thicknessPx, float dashPx, float gapPx, Color c) {
    const float len = distance(a, b);
    const float dash = dashPx * unitsPerPixel_;
    const float step = (dashPx + gapPx) * unitsPerPixel_;
    if (len <= 0.0f || step <= 0.0f) return;
    const Vec2 dir = (b - a) / len;
    for (float t = 0.0f; t < len; t += step) line(a + dir * t, a + dir * std::min(t + dash, len), thicknessPx, c);
}

} // namespace opense4::gfx
