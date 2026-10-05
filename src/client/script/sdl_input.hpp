#pragma once

// Input scripts and SDL events: a script's input event as the SDL event a
// real mouse or keyboard would send (handled by the app exactly like one),
// and, for the recorder, the key chord of a real key event.

#include "client/input.hpp"
#include "client/script/player.hpp"

#include <SDL3/SDL.h>

#include <optional>

namespace opense4::client::script {

struct SdlKey {
    SDL_Keycode keycode = SDLK_UNKNOWN;
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
};
// The SDL key of a Dear ImGui key, as a US keyboard sends it; nothing for
// keys a script cannot press.
std::optional<SdlKey> sdlKey(ImGuiKey key);

// The SDL event for a script's event in the window `window` (a Window event:
// the window event the system would send). A Text event's text points into
// `e`, which must outlive the SDL event's handling.
SDL_Event toSdlEvent(const InputEvent& e, SDL_WindowID window);

// Whether an SDL event is the player's input (mouse, keyboard, text, and the
// pointer entering, leaving or the window losing focus), which a script run
// keeps away from the game.
bool isUserInput(const SDL_Event& e);

} // namespace opense4::client::script
