#pragma once

// Windows cursor files (.cur), as shipped in the classic game's Pictures/Game
// folder (docs/spec/06 §5.8). A .cur is the icon container of the published
// Windows format with a hot spot in each directory entry: a BITMAPINFOHEADER
// image whose height counts the colour (XOR) bitmap and the 1-bit AND mask
// together, bottom-up rows padded to 4 bytes. Read from the player's install
// at run time, like the pictures; nothing is copied.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opense4::assets {

struct CursorImage {
    int width = 0;
    int height = 0;
    int hotX = 0;                 // the hot spot, from the image's top-left corner
    int hotY = 0;
    std::vector<uint8_t> rgba;    // top row first, straight alpha

    bool empty() const { return width <= 0 || height <= 0; }
};

// Decodes the first image of a .cur (or .ico) file. A pixel whose mask bit is
// set is transparent when its colour is black; a set mask bit over another
// colour would invert the screen, which a picture cannot do: it comes out
// opaque black (inferred; the install's pointers have no such pixel). 1, 4,
// 8, 24 and 32 bits per pixel are read; 32-bit images use their alpha when
// they have one. PNG-compressed entries are not supported.
std::optional<CursorImage> parseCursor(std::span<const uint8_t> data, std::string* error = nullptr);
std::optional<CursorImage> loadCursor(const std::filesystem::path& path, std::string* error = nullptr);

} // namespace opense4::assets
