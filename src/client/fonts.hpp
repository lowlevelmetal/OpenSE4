#pragma once

struct ImFont;

namespace opense4::client {

// The UI fonts in use: our own (Noto Sans) from the app shell, or the classic
// game's bitmap fonts once the classic client has loaded them.
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* medium = nullptr;
    ImFont* bold = nullptr;
    ImFont* small = nullptr;   // fine print (falls back to regular)
    bool bitmap = false;       // the classic game's own bitmap fonts are in use
};

} // namespace opense4::client
