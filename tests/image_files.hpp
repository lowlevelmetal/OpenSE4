#pragma once

// Picture files written by tests: an uncompressed 24-bit BMP (as the classic
// art is) and a PNG with an alpha channel (as mods may add), from pixels the
// test makes. Our own content, made on the spot.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

namespace opense4::test {

// RGBA, top row first.
using PixelFn = std::function<std::array<uint8_t, 4>(int x, int y)>;

inline void writeBytes(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A bottom-up 24-bit BMP (alpha is dropped: the classic format has none).
inline std::vector<uint8_t> bmpBytes(int w, int h, const PixelFn& pixel) {
    const uint32_t row = static_cast<uint32_t>((w * 3 + 3) / 4 * 4);
    const uint32_t data = row * static_cast<uint32_t>(h);
    std::vector<uint8_t> b;
    auto u16 = [&](uint32_t v) {
        b.push_back(static_cast<uint8_t>(v));
        b.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto u32 = [&](uint32_t v) {
        u16(v & 0xffff);
        u16(v >> 16);
    };
    b.push_back('B');
    b.push_back('M');
    u32(54 + data);
    u32(0);
    u32(54);
    u32(40);
    u32(static_cast<uint32_t>(w));
    u32(static_cast<uint32_t>(h));
    u16(1);
    u16(24);
    u32(0);
    u32(data);
    u32(2835);
    u32(2835);
    u32(0);
    u32(0);
    for (int y = h - 1; y >= 0; --y) {
        const size_t start = b.size();
        for (int x = 0; x < w; ++x) {
            const auto p = pixel(x, y);
            b.push_back(p[2]);
            b.push_back(p[1]);
            b.push_back(p[0]);
        }
        while (b.size() - start < row) b.push_back(0);
    }
    return b;
}

// An RGBA PNG whose image data is stored, not compressed (a valid zlib stream of stored blocks).
inline std::vector<uint8_t> pngBytes(int w, int h, const PixelFn& pixel) {
    auto crc32 = [](const uint8_t* p, size_t n, uint32_t c = 0xffffffffu) {
        for (size_t i = 0; i < n; ++i) {
            c ^= p[i];
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
        }
        return c;
    };
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);   // no filter
        for (int x = 0; x < w; ++x) {
            const auto p = pixel(x, y);
            raw.insert(raw.end(), p.begin(), p.end());
        }
    }
    std::vector<uint8_t> z{0x78, 0x01};
    for (size_t at = 0; at < raw.size() || at == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
        if (last) break;
    }
    uint32_t a = 1, bsum = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        bsum = (bsum + a) % 65521;
    }
    const uint32_t adler = (bsum << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back(static_cast<uint8_t>(adler >> s));

    std::vector<uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    auto chunk = [&](const char* type, const std::vector<uint8_t>& body) {
        const auto n = static_cast<uint32_t>(body.size());
        for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(n >> s));
        std::vector<uint8_t> typed(type, type + 4);
        typed.insert(typed.end(), body.begin(), body.end());
        out.insert(out.end(), typed.begin(), typed.end());
        const uint32_t c = ~crc32(typed.data(), typed.size());
        for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(c >> s));
    };
    std::vector<uint8_t> ihdr;
    for (uint32_t v : {static_cast<uint32_t>(w), static_cast<uint32_t>(h)})
        for (int s = 24; s >= 0; s -= 8) ihdr.push_back(static_cast<uint8_t>(v >> s));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});   // 8 bits, RGBA
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return out;
}

inline void writeBmp(const std::filesystem::path& file, int w, int h, const PixelFn& pixel) { writeBytes(file, bmpBytes(w, h, pixel)); }
inline void writePng(const std::filesystem::path& file, int w, int h, const PixelFn& pixel) { writeBytes(file, pngBytes(w, h, pixel)); }

// One colour everywhere.
inline PixelFn solid(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return [=](int, int) { return std::array<uint8_t, 4>{r, g, b, a}; };
}

} // namespace opense4::test
