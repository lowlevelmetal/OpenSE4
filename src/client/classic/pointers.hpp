#pragma once

// The classic pointers on screen (docs/spec/06 §5.8): the twelve .cur files of
// the install's Pictures/Game folder (the active mod's first), decoded by our
// own reader (assets/wincursor.hpp) and shown as SDL colour cursors. Normal is
// the pointer of every classic window; Hourglass covers the program while it
// saves, loads or processes a turn; the tactical map asks for the others
// (pointer_rules.hpp). A missing file falls back to the system's pointer.

#include "assets/assets.hpp"
#include "assets/wincursor.hpp"
#include "client/classic/pointer_rules.hpp"

#include <array>
#include <optional>

struct SDL_Cursor;

namespace opense4::client::classic {

class PointerSet {
public:
    ~PointerSet();
    // Reads the files; the pointers are shown from the next apply().
    void load(const assets::InstallFiles& files);
    void release();
    bool loaded() const { return images_[0].has_value(); }
    bool has(Pointer p) const { return images_[static_cast<size_t>(p)].has_value(); }

    // The pointer for this frame (the last request wins; Normal by default).
    void request(Pointer p) { requested_ = p; }
    // Shows this frame's pointer, the art drawn `scale` times (whole
    // multiples, nearest neighbour, so it grows with the classic screens), and
    // goes back to Normal for the next frame.
    void apply(int scale);
    // The Hourglass at once, for work that blocks the frame loop.
    void showBusy();

private:
    SDL_Cursor* cursorFor(Pointer p);
    void show(Pointer p);

    std::array<std::optional<assets::CursorImage>, kPointerCount> images_;
    std::array<SDL_Cursor*, kPointerCount> cursors_{};
    std::array<SDL_Cursor*, kPointerCount> system_{};
    int scale_ = 1;
    Pointer requested_ = Pointer::Normal;
    std::optional<Pointer> shown_;
};

PointerSet& pointers();

// While it lives the whole program shows the Hourglass (saving, loading, a
// turn being processed); afterwards the pointer goes back to Normal.
class BusyPointer {
public:
    BusyPointer();
    ~BusyPointer();
    BusyPointer(const BusyPointer&) = delete;
    BusyPointer& operator=(const BusyPointer&) = delete;
};

} // namespace opense4::client::classic
