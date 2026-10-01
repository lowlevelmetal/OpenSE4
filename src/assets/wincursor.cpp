#include "assets/wincursor.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>

namespace opense4::assets {

namespace {

struct Reader {
    std::span<const uint8_t> d;
    bool ok(size_t at, size_t n) const { return at <= d.size() && n <= d.size() - at; }
    uint8_t u8(size_t at) const { return ok(at, 1) ? d[at] : 0; }
    uint16_t u16(size_t at) const { return ok(at, 2) ? uint16_t(d[at] | d[at + 1] << 8) : 0; }
    uint32_t u32(size_t at) const { return ok(at, 4) ? uint32_t(u16(at)) | uint32_t(u16(at + 2)) << 16 : 0; }
};

void fail(std::string* error, std::string text) {
    if (error && error->empty()) *error = std::move(text);
}

// Bytes in one bottom-up row of `bits` bits per pixel, padded to 4 bytes.
size_t stride(int width, int bits) { return (size_t(width) * size_t(bits) + 31) / 32 * 4; }

} // namespace

std::optional<CursorImage> parseCursor(std::span<const uint8_t> data, std::string* error) {
    const Reader r{data};
    // The directory: reserved 0, type (1 icon, 2 cursor), the number of images; then 16-byte entries.
    const uint16_t type = r.u16(2);
    if (!r.ok(0, 6) || r.u16(0) != 0 || (type != 1 && type != 2)) {
        fail(error, "not a cursor or icon file");
        return std::nullopt;
    }
    if (r.u16(4) == 0 || !r.ok(6, 16)) {
        fail(error, "the file holds no image");
        return std::nullopt;
    }
    const size_t entry = 6;
    const uint32_t offset = r.u32(entry + 12), length = r.u32(entry + 8);
    if (!r.ok(offset, 40)) {
        fail(error, "the image lies outside the file");
        return std::nullopt;
    }
    if (r.u8(offset) == 0x89 && r.u8(offset + 1) == 'P') {
        fail(error, "PNG-compressed cursors are not supported");
        return std::nullopt;
    }
    // BITMAPINFOHEADER.
    const size_t header = r.u32(offset);
    const int width = int(int32_t(r.u32(offset + 4)));
    const int doubled = int(int32_t(r.u32(offset + 8)));
    const int bits = r.u16(offset + 14);
    const uint32_t compression = r.u32(offset + 16);
    uint32_t colours = r.u32(offset + 32);
    if (header < 40 || width <= 0 || width > 256 || doubled <= 0 || doubled % 2 != 0 || doubled / 2 > 256) {
        fail(error, "bad image header");
        return std::nullopt;
    }
    if (compression != 0 || (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32)) {
        fail(error, std::format("unsupported image ({} bits per pixel, compression {})", bits, compression));
        return std::nullopt;
    }
    const int height = doubled / 2;
    if (bits <= 8 && colours == 0) colours = 1u << bits;
    if (bits > 8) colours = 0;
    const size_t palette = offset + header;
    const size_t xorAt = palette + size_t(colours) * 4;
    const size_t andAt = xorAt + stride(width, bits) * size_t(height);
    const size_t end = andAt + stride(width, 1) * size_t(height);
    if (colours > 256 || !r.ok(offset, end - offset) || (length != 0 && end - offset > length + 3)) {
        fail(error, "the image is truncated");
        return std::nullopt;
    }

    CursorImage out;
    out.width = width;
    out.height = height;
    // The hot spot lives in the directory entry of a cursor (an icon has none).
    if (type == 2) {
        out.hotX = std::min(int(r.u16(entry + 4)), width - 1);
        out.hotY = std::min(int(r.u16(entry + 6)), height - 1);
    }
    out.rgba.assign(size_t(width) * size_t(height) * 4, 0);
    bool anyAlpha = false;
    if (bits == 32)
        for (int y = 0; y < height && !anyAlpha; ++y)
            for (int x = 0; x < width && !anyAlpha; ++x) anyAlpha = r.u8(xorAt + stride(width, 32) * size_t(y) + size_t(x) * 4 + 3) != 0;

    for (int y = 0; y < height; ++y) {
        const size_t row = size_t(height - 1 - y);  // bottom-up
        const size_t xorRow = xorAt + stride(width, bits) * row, andRow = andAt + stride(width, 1) * row;
        for (int x = 0; x < width; ++x) {
            uint8_t b = 0, g = 0, rr = 0, a = 255;
            if (bits <= 8) {
                const size_t bit = size_t(x) * size_t(bits);
                const uint8_t byte = r.u8(xorRow + bit / 8);
                const unsigned index = (byte >> (8 - bits - bit % 8)) & ((1u << bits) - 1);
                const size_t c = palette + size_t(index) * 4;
                b = r.u8(c);
                g = r.u8(c + 1);
                rr = r.u8(c + 2);
            } else {
                const size_t c = xorRow + size_t(x) * size_t(bits / 8);
                b = r.u8(c);
                g = r.u8(c + 1);
                rr = r.u8(c + 2);
                if (anyAlpha) a = r.u8(c + 3);
            }
            const bool mask = (r.u8(andRow + size_t(x) / 8) >> (7 - x % 8)) & 1;
            if (!anyAlpha && mask) {
                // Screen-transparent over black; a screen-inverting pixel cannot be shown: opaque black.
                if (b == 0 && g == 0 && rr == 0) a = 0;
                else b = g = rr = 0;
            }
            uint8_t* p = &out.rgba[(size_t(y) * size_t(width) + size_t(x)) * 4];
            p[0] = rr;
            p[1] = g;
            p[2] = b;
            p[3] = a;
        }
    }
    return out;
}

std::optional<CursorImage> loadCursor(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail(error, "cannot open " + path.string());
        return std::nullopt;
    }
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parseCursor(bytes, error);
}

} // namespace opense4::assets
