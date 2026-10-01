#pragma once

struct ImFont;

namespace opense4::client {

// The UI fonts in use: our own (Noto Sans) from the app shell, or the classic
// game's raster fonts once the classic client has loaded them from the
// install (docs/spec/06 §5.4): `medium` and `regular` are Futurist Medium
// (the Body setting), `small` Futurist small, `bold` SE4 Text button (titles,
// buttons), and `tiny` OpenSE4's own small raster face standing in for the
// system's Small Fonts on the maps (assets/tiny_font.hpp). Each falls back to
// our own font when its file is missing.
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* medium = nullptr;
    ImFont* bold = nullptr;
    ImFont* small = nullptr;   // fine print (falls back to regular)
    ImFont* tiny = nullptr;    // map numbers and letters (falls back to small)
    bool bitmap = false;       // the classic game's own raster fonts are in use
    // OpenSE4's own fonts, for its own windows (Learn, the manual, the lesson
    // panel): the app shell's, whatever the classic client loads.
    ImFont* ownRegular = nullptr;
    ImFont* ownBold = nullptr;

    ImFont* readingFont() const { return ownRegular ? ownRegular : regular; }
    ImFont* readingBold() const { return ownBold ? ownBold : bold; }
};

} // namespace opense4::client
