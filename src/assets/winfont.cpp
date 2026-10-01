#include "assets/winfont.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>

namespace opense4::assets {

namespace {

// Little-endian reads with bounds checks.
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

// FNT header offsets (the published Windows 3.0 font format). Not all are read.
[[maybe_unused]] constexpr size_t kVersion = 0, kSize = 2, kType = 66, kPoints = 68, kAscent = 74, kInternalLeading = 76, kItalic = 80, kWeight = 83,
                 kPixHeight = 88, kFirstChar = 95, kLastChar = 96, kDefaultChar = 97, kFace = 105;
constexpr size_t kHeaderV2 = 118, kHeaderV3 = 148;

constexpr uint16_t kRtFont = 0x8008;  // RT_FONT with the "integer id" bit

} // namespace

const BitmapGlyph* BitmapFont::glyph(uint8_t ch) const {
    if (glyphs.empty()) return nullptr;
    if (ch >= firstChar && ch <= lastChar) return &glyphs[size_t(ch - firstChar)];
    const size_t fallback = defaultChar;
    return fallback < glyphs.size() ? &glyphs[fallback] : &glyphs.front();
}

uint8_t unicodeToCp1252(char32_t c) {
    if (c < 0x80 || (c >= 0xa0 && c <= 0xff)) return uint8_t(c);
    static constexpr std::array<std::pair<char32_t, uint8_t>, 27> kHigh{{
        {0x20ac, 0x80}, {0x201a, 0x82}, {0x0192, 0x83}, {0x201e, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87},
        {0x02c6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8a}, {0x2039, 0x8b}, {0x0152, 0x8c}, {0x017d, 0x8e}, {0x2018, 0x91},
        {0x2019, 0x92}, {0x201c, 0x93}, {0x201d, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97}, {0x02dc, 0x98},
        {0x2122, 0x99}, {0x0161, 0x9a}, {0x203a, 0x9b}, {0x0153, 0x9c}, {0x017e, 0x9e}, {0x0178, 0x9f},
    }};
    for (const auto& [u, b] : kHigh)
        if (u == c) return b;
    return 0;
}

std::optional<BitmapFont> parseFnt(std::span<const uint8_t> data, std::string* error) {
    const Reader r{data};
    const uint16_t version = r.u16(kVersion);
    if (version != 0x0200 && version != 0x0300) {
        fail(error, std::format("unsupported font version 0x{:04x}", version));
        return std::nullopt;
    }
    if (!r.ok(0, version == 0x0300 ? kHeaderV3 : kHeaderV2)) {
        fail(error, "font header is truncated");
        return std::nullopt;
    }
    if (r.u16(kType) & 1) {
        fail(error, "vector fonts are not supported");
        return std::nullopt;
    }
    BitmapFont f;
    f.points = r.u16(kPoints);
    f.ascent = r.u16(kAscent);
    f.internalLeading = r.u16(kInternalLeading);
    f.italic = r.u8(kItalic) != 0;
    f.weight = r.u16(kWeight);
    f.pixelHeight = r.u16(kPixHeight);
    f.firstChar = r.u8(kFirstChar);
    f.lastChar = r.u8(kLastChar);
    f.defaultChar = r.u8(kDefaultChar);
    if (f.pixelHeight <= 0 || f.lastChar < f.firstChar) {
        fail(error, "font has no glyphs");
        return std::nullopt;
    }
    for (size_t at = r.u32(kFace); at < data.size() && data[at] != 0 && f.face.size() < 64; ++at) f.face += char(data[at]);

    const size_t table = version == 0x0300 ? kHeaderV3 : kHeaderV2;
    const size_t entry = version == 0x0300 ? 6 : 4;
    const size_t count = size_t(f.lastChar - f.firstChar) + 1;
    const size_t h = size_t(f.pixelHeight);
    f.glyphs.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t e = table + i * entry;
        if (!r.ok(e, entry)) {
            fail(error, "character table is truncated");
            return std::nullopt;
        }
        const int width = r.u16(e);
        const size_t offset = version == 0x0300 ? r.u32(e + 2) : r.u16(e + 2);
        const size_t columns = size_t(width + 7) / 8;
        if (width > 256 || !r.ok(offset, columns * h)) {
            fail(error, std::format("glyph {} lies outside the font", int(f.firstChar) + int(i)));
            return std::nullopt;
        }
        BitmapGlyph& g = f.glyphs[i];
        g.width = width;
        g.bits.assign(size_t(width) * h, 0);
        // Bitmaps are stored as 8-pixel-wide byte columns, each pixelHeight bytes tall; the top bit is the leftmost pixel.
        for (int x = 0; x < width; ++x)
            for (size_t y = 0; y < h; ++y) {
                const uint8_t b = data[offset + size_t(x / 8) * h + y];
                g.bits[y * size_t(width) + size_t(x)] = uint8_t((b >> (7 - x % 8)) & 1);
            }
    }
    return f;
}

std::vector<BitmapFont> parseFon(std::span<const uint8_t> data, std::string* error) {
    std::vector<BitmapFont> out;
    const Reader r{data};
    if (r.u16(0) == 0x0200 || r.u16(0) == 0x0300) {
        if (auto f = parseFnt(data, error)) out.push_back(std::move(*f));
        return out;
    }
    if (r.u16(0) != 0x5a4d) {  // "MZ"
        fail(error, "not a font file");
        return out;
    }
    const size_t ne = r.u32(0x3c);
    if (r.u16(ne) != 0x454e) {  // "NE"
        fail(error, "not a 16-bit Windows font module");
        return out;
    }
    // Resource table: an alignment shift, then type blocks (id, count, 4 reserved bytes, count × 12-byte entries), ending with id 0.
    size_t p = ne + r.u16(ne + 0x24);
    const unsigned shift = r.u16(p);
    if (shift > 16) {
        fail(error, "bad resource alignment");
        return out;
    }
    p += 2;
    for (int guard = 0; guard < 64 && r.ok(p, 2); ++guard) {
        const uint16_t type = r.u16(p);
        if (type == 0) break;
        const uint16_t n = r.u16(p + 2);
        p += 8;
        for (uint16_t i = 0; i < n; ++i, p += 12) {
            if (type != kRtFont) continue;
            const size_t offset = size_t(r.u16(p)) << shift, length = size_t(r.u16(p + 2)) << shift;
            if (!r.ok(offset, 1)) continue;
            const size_t avail = std::min(length, data.size() - offset);
            if (auto f = parseFnt(data.subspan(offset, avail), error)) out.push_back(std::move(*f));
        }
    }
    if (out.empty()) fail(error, "no raster fonts in the file");
    return out;
}

std::vector<BitmapFont> loadFon(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail(error, "cannot open " + path.string());
        return {};
    }
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parseFon(bytes, error);
}

} // namespace opense4::assets
