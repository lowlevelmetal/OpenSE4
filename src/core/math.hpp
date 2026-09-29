#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace opense4 {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2&) const = default;
};

constexpr Vec2 operator*(float s, Vec2 v) { return v * s; }
constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
constexpr float lengthSq(Vec2 v) { return dot(v, v); }
inline float length(Vec2 v) { return std::sqrt(lengthSq(v)); }
inline float distance(Vec2 a, Vec2 b) { return length(b - a); }
inline Vec2 normalize(Vec2 v) {
    const float len = length(v);
    return len > 0.0f ? v / len : Vec2{};
}
constexpr Vec2 perp(Vec2 v) { return {-v.y, v.x}; }
constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline Vec2 fromAngle(float radians) { return {std::cos(radians), std::sin(radians)}; }

// Distance from point p to segment ab.
inline float distanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float lenSq = lengthSq(ab);
    const float t = lenSq > 0.0f ? std::clamp(dot(p - a, ab) / lenSq, 0.0f, 1.0f) : 0.0f;
    return distance(p, a + ab * t);
}

// True if segments ab and cd properly intersect (shared endpoints do not count).
inline bool segmentsCross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const float d1 = cross(b - a, c - a);
    const float d2 = cross(b - a, d - a);
    const float d3 = cross(d - c, a - c);
    const float d4 = cross(d - c, b - c);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) &&
           d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

struct Rect {
    Vec2 min;
    Vec2 max;

    static constexpr Rect fromPosSize(Vec2 pos, Vec2 size) { return {pos, pos + size}; }
    static constexpr Rect fromCenter(Vec2 c, Vec2 halfExtent) { return {c - halfExtent, c + halfExtent}; }
    constexpr Vec2 size() const { return max - min; }
    constexpr Vec2 center() const { return (min + max) * 0.5f; }
    constexpr bool contains(Vec2 p) const { return p.x >= min.x && p.y >= min.y && p.x < max.x && p.y < max.y; }
    constexpr bool overlaps(const Rect& o) const {
        return min.x < o.max.x && o.min.x < max.x && min.y < o.max.y && o.min.y < max.y;
    }
    constexpr Rect expanded(float by) const { return {min - Vec2{by, by}, max + Vec2{by, by}}; }
};

// Linear (non-sRGB-converted) RGBA color with float channels in [0, 1].
struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr Color fromRgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }
    static constexpr Color hex(uint32_t rgb, float alpha = 1.0f) {
        return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
    }

    constexpr Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
    constexpr Color scaled(float s) const { return {r * s, g * s, b * s, a}; }
    constexpr bool operator==(const Color&) const = default;

    // Packs to bytes R,G,B,A in memory order (matches Dear ImGui's IM_COL32 on little-endian).
    uint32_t toRgba8() const {
        auto c = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
    }
};

constexpr Color lerp(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// Column-major 4x4 matrix (GLSL layout).
struct Mat4 {
    std::array<float, 16> m{};

    static constexpr Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    // Maps the rectangle [left,right] x [top,bottom] to clip space with
    // Vulkan conventions: x -1..1 left-to-right, y -1..1 top-to-bottom.
    static constexpr Mat4 ortho2D(float left, float right, float top, float bottom) {
        Mat4 r = identity();
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (bottom - top);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(bottom + top) / (bottom - top);
        return r;
    }

    constexpr float& at(int col, int row) { return m[static_cast<size_t>(col * 4 + row)]; }
    constexpr float at(int col, int row) const { return m[static_cast<size_t>(col * 4 + row)]; }

    friend constexpr Mat4 operator*(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += a.at(k, row) * b.at(c, k);
                r.at(c, row) = sum;
            }
        return r;
    }
};

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kTau = 2.0f * kPi;

} // namespace opense4
