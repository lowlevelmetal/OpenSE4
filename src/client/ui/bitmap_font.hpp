#pragma once

// Windows bitmap fonts (assets/winfont) as ImGui fonts. A custom ImGui font
// loader rasterizes each glyph at the size asked for: whole-number scales
// reproduce the original pixels exactly, other scales use area coverage so
// strokes keep an even weight.
//
// Size convention: font size `nominalSize(font)` (the pixel height without
// the internal leading) draws the font at its native pixels, with lines one
// cap-plus-descender apart like the classic screens.

#include "assets/winfont.hpp"

#include <imgui.h>

namespace opense4::client {

float bitmapFontNominalSize(const assets::BitmapFont& font);

// Adds the font to the atlas; the font data is kept alive for the process.
ImFont* addBitmapFont(ImFontAtlas* atlas, assets::BitmapFont font);

} // namespace opense4::client
