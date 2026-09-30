#pragma once

// Windows 3.x bitmap fonts (.fon), as shipped in the classic game's Fonts
// folder. A .fon file is a resource-only NE (16-bit) module; each RT_FONT
// resource holds one raster font in the documented FNT format (versions 2
// and 3). Loaded from the player's install at run time, like the pictures.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opense4::assets {

struct BitmapGlyph {
    int width = 0;               // advance and bitmap width in pixels
    std::vector<uint8_t> bits;   // width × pixelHeight, row-major, 1 = ink
};

struct BitmapFont {
    std::string face;
    int pixelHeight = 0;         // every glyph is this tall
    int ascent = 0;              // baseline, from the top of the glyph box
    int internalLeading = 0;     // accent space inside the ascent
    int points = 0;
    int weight = 400;
    bool italic = false;
    uint8_t firstChar = 0;
    uint8_t lastChar = 0;
    uint8_t defaultChar = 0;     // relative to firstChar, as in the file
    std::vector<BitmapGlyph> glyphs;  // firstChar..lastChar

    // The glyph for a Windows-1252 byte, or the default glyph.
    const BitmapGlyph* glyph(uint8_t ch) const;
    bool ink(const BitmapGlyph& g, int x, int y) const { return g.bits[size_t(y * g.width + x)] != 0; }
};

// Unicode code point -> Windows-1252 byte (0 if the code point has none).
uint8_t unicodeToCp1252(char32_t c);

// Parses one FNT resource.
std::optional<BitmapFont> parseFnt(std::span<const uint8_t> data, std::string* error = nullptr);
// Every font in a .fon (NE) file; also accepts a bare .fnt.
std::vector<BitmapFont> parseFon(std::span<const uint8_t> data, std::string* error = nullptr);
std::vector<BitmapFont> loadFon(const std::filesystem::path& path, std::string* error = nullptr);

} // namespace opense4::assets
