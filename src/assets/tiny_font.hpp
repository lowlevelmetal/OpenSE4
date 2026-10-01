#pragma once

// OpenSE4's own small raster face for the map numbers and letters. The
// classic game draws them in the system's "Small Fonts" at 6 pt, which is not
// part of the install (docs/spec/06 §5.4); this face stands in for it. Every
// glyph here was drawn for OpenSE4: caps and digits 5 pixels tall, a cell of
// 8 pixels (ascent 6, descent 2), characters 32-126, one pixel between
// characters. It goes through the same path as the install's fonts
// (client/ui/bitmap_font.hpp), so it is drawn at its own pixels.

#include "assets/winfont.hpp"

namespace opense4::assets {

inline constexpr const char* kTinyFaceName = "OpenSE4 Tiny";

BitmapFont makeTinyFont();

} // namespace opense4::assets
