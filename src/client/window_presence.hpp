#pragma once

// Whether the game's window is in the background, from the window events the
// system sends; the game mutes itself meanwhile (docs/SETUP.md "Sound and
// music"). In the background means: another window has the keyboard focus,
// or the game's window is minimized, hidden or covered. Systems report this
// differently, so any one of them counts:
//
// - Windows: the focus, and minimizing (Alt+Tab out of exclusive fullscreen
//   minimizes the window too);
// - X11: the focus, minimizing (reported as minimized and covered) and the
//   window manager's hidden state (covered);
// - Wayland: the focus, and covered for a window the compositor suspends
//   (minimized, on another workspace or fully covered); Wayland seldom
//   reports minimizing itself;
// - macOS: the focus (another application in front), minimizing, and covered
//   for a window on another Space or an application hidden with Cmd+H.
//
// It starts in the foreground, so a system that never reports the focus
// (SDL's offscreen driver) never mutes the game.

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

namespace opense4::client {

class WindowPresence {
public:
    // Follows one event; other events, and other windows' events, change nothing.
    void follow(const SDL_Event& e, SDL_WindowID window) {
        const bool windowEvent = e.type >= static_cast<Uint32>(SDL_EVENT_WINDOW_FIRST) && e.type <= static_cast<Uint32>(SDL_EVENT_WINDOW_LAST);
        if (!windowEvent || e.window.windowID != window) return;
        switch (e.type) {
            case SDL_EVENT_WINDOW_FOCUS_GAINED: focused_ = true; break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: focused_ = false; break;
            case SDL_EVENT_WINDOW_MINIMIZED: minimized_ = true; break;
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED: minimized_ = false; break;
            case SDL_EVENT_WINDOW_HIDDEN: hidden_ = true; break;
            case SDL_EVENT_WINDOW_SHOWN:  // as SDL itself: shown is no longer minimized either
                hidden_ = false;
                minimized_ = false;
                break;
            case SDL_EVENT_WINDOW_OCCLUDED: occluded_ = true; break;
            case SDL_EVENT_WINDOW_EXPOSED: occluded_ = false; break;  // as SDL itself: exposed is no longer covered
            default: break;
        }
    }
    bool background() const { return !focused_ || minimized_ || hidden_ || occluded_; }

private:
    bool focused_ = true, minimized_ = false, hidden_ = false, occluded_ = false;
};

} // namespace opense4::client
