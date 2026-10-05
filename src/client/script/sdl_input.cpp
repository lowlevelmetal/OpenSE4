#include "client/script/sdl_input.hpp"

namespace opense4::client::script {

namespace {

// The SDL ids of the script's mouse and keyboard (any but the touch mouse id).
constexpr SDL_MouseID kScriptMouse = 1;
constexpr SDL_KeyboardID kScriptKeyboard = 1;

} // namespace

std::optional<SdlKey> sdlKey(ImGuiKey key) {
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
        const int i = key - ImGuiKey_A;
        return SdlKey{SDL_Keycode(SDLK_A + i), SDL_Scancode(SDL_SCANCODE_A + i)};
    }
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
        const int i = key - ImGuiKey_0;
        // SDL's scancodes run 1..9 then 0.
        return SdlKey{SDL_Keycode(SDLK_0 + i), i == 0 ? SDL_SCANCODE_0 : SDL_Scancode(SDL_SCANCODE_1 + i - 1)};
    }
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) {
        const int i = key - ImGuiKey_F1;
        return SdlKey{SDL_Keycode(SDLK_F1 + i), SDL_Scancode(SDL_SCANCODE_F1 + i)};
    }
    switch (key) {
        case ImGuiKey_Tab: return SdlKey{SDLK_TAB, SDL_SCANCODE_TAB};
        case ImGuiKey_LeftArrow: return SdlKey{SDLK_LEFT, SDL_SCANCODE_LEFT};
        case ImGuiKey_RightArrow: return SdlKey{SDLK_RIGHT, SDL_SCANCODE_RIGHT};
        case ImGuiKey_UpArrow: return SdlKey{SDLK_UP, SDL_SCANCODE_UP};
        case ImGuiKey_DownArrow: return SdlKey{SDLK_DOWN, SDL_SCANCODE_DOWN};
        case ImGuiKey_PageUp: return SdlKey{SDLK_PAGEUP, SDL_SCANCODE_PAGEUP};
        case ImGuiKey_PageDown: return SdlKey{SDLK_PAGEDOWN, SDL_SCANCODE_PAGEDOWN};
        case ImGuiKey_Home: return SdlKey{SDLK_HOME, SDL_SCANCODE_HOME};
        case ImGuiKey_End: return SdlKey{SDLK_END, SDL_SCANCODE_END};
        case ImGuiKey_Insert: return SdlKey{SDLK_INSERT, SDL_SCANCODE_INSERT};
        case ImGuiKey_Delete: return SdlKey{SDLK_DELETE, SDL_SCANCODE_DELETE};
        case ImGuiKey_Backspace: return SdlKey{SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE};
        case ImGuiKey_Space: return SdlKey{SDLK_SPACE, SDL_SCANCODE_SPACE};
        case ImGuiKey_Enter: return SdlKey{SDLK_RETURN, SDL_SCANCODE_RETURN};
        case ImGuiKey_Escape: return SdlKey{SDLK_ESCAPE, SDL_SCANCODE_ESCAPE};
        case ImGuiKey_KeypadEnter: return SdlKey{SDLK_KP_ENTER, SDL_SCANCODE_KP_ENTER};
        case ImGuiKey_Apostrophe: return SdlKey{SDLK_APOSTROPHE, SDL_SCANCODE_APOSTROPHE};
        case ImGuiKey_Comma: return SdlKey{SDLK_COMMA, SDL_SCANCODE_COMMA};
        case ImGuiKey_Minus: return SdlKey{SDLK_MINUS, SDL_SCANCODE_MINUS};
        case ImGuiKey_Period: return SdlKey{SDLK_PERIOD, SDL_SCANCODE_PERIOD};
        case ImGuiKey_Slash: return SdlKey{SDLK_SLASH, SDL_SCANCODE_SLASH};
        case ImGuiKey_Semicolon: return SdlKey{SDLK_SEMICOLON, SDL_SCANCODE_SEMICOLON};
        case ImGuiKey_Equal: return SdlKey{SDLK_EQUALS, SDL_SCANCODE_EQUALS};
        case ImGuiKey_LeftBracket: return SdlKey{SDLK_LEFTBRACKET, SDL_SCANCODE_LEFTBRACKET};
        case ImGuiKey_Backslash: return SdlKey{SDLK_BACKSLASH, SDL_SCANCODE_BACKSLASH};
        case ImGuiKey_RightBracket: return SdlKey{SDLK_RIGHTBRACKET, SDL_SCANCODE_RIGHTBRACKET};
        case ImGuiKey_GraveAccent: return SdlKey{SDLK_GRAVE, SDL_SCANCODE_GRAVE};
        case ImGuiKey_LeftCtrl: return SdlKey{SDLK_LCTRL, SDL_SCANCODE_LCTRL};
        case ImGuiKey_LeftShift: return SdlKey{SDLK_LSHIFT, SDL_SCANCODE_LSHIFT};
        case ImGuiKey_LeftAlt: return SdlKey{SDLK_LALT, SDL_SCANCODE_LALT};
        case ImGuiKey_RightCtrl: return SdlKey{SDLK_RCTRL, SDL_SCANCODE_RCTRL};
        case ImGuiKey_RightShift: return SdlKey{SDLK_RSHIFT, SDL_SCANCODE_RSHIFT};
        case ImGuiKey_RightAlt: return SdlKey{SDLK_RALT, SDL_SCANCODE_RALT};
        default: return std::nullopt;
    }
}

SDL_Event toSdlEvent(const InputEvent& e, SDL_WindowID window) {
    SDL_Event ev;
    SDL_zero(ev);
    switch (e.kind) {
        case InputEvent::Kind::Motion:
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.windowID = window;
            ev.motion.which = kScriptMouse;
            ev.motion.x = e.pos.x;
            ev.motion.y = e.pos.y;
            break;
        case InputEvent::Kind::ButtonDown:
        case InputEvent::Kind::ButtonUp:
            ev.type = e.kind == InputEvent::Kind::ButtonDown ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.windowID = window;
            ev.button.which = kScriptMouse;
            ev.button.button = static_cast<Uint8>(e.button);
            ev.button.down = e.kind == InputEvent::Kind::ButtonDown;
            ev.button.clicks = 1;
            ev.button.x = e.pos.x;
            ev.button.y = e.pos.y;
            break;
        case InputEvent::Kind::Wheel:
            ev.type = SDL_EVENT_MOUSE_WHEEL;
            ev.wheel.windowID = window;
            ev.wheel.which = kScriptMouse;
            ev.wheel.y = e.wheel;
            ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            ev.wheel.mouse_x = e.pos.x;
            ev.wheel.mouse_y = e.pos.y;
            break;
        case InputEvent::Kind::KeyDown:
        case InputEvent::Kind::KeyUp: {
            const SdlKey k = sdlKey(e.key).value_or(SdlKey{});
            ev.type = e.kind == InputEvent::Kind::KeyDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            ev.key.windowID = window;
            ev.key.which = kScriptKeyboard;
            ev.key.key = k.keycode;
            ev.key.scancode = k.scancode;
            ev.key.raw = static_cast<Uint16>(k.scancode);
            ev.key.mod = static_cast<SDL_Keymod>((e.ctrl ? SDL_KMOD_LCTRL : 0) | (e.shift ? SDL_KMOD_LSHIFT : 0) | (e.alt ? SDL_KMOD_LALT : 0));
            ev.key.down = e.kind == InputEvent::Kind::KeyDown;
            break;
        }
        case InputEvent::Kind::Text:
            ev.type = SDL_EVENT_TEXT_INPUT;
            ev.text.windowID = window;
            ev.text.text = e.text.c_str();
            break;
        case InputEvent::Kind::Window:
            switch (e.window) {
                case WindowChange::FocusLost: ev.type = SDL_EVENT_WINDOW_FOCUS_LOST; break;
                case WindowChange::FocusGained: ev.type = SDL_EVENT_WINDOW_FOCUS_GAINED; break;
                case WindowChange::Minimized: ev.type = SDL_EVENT_WINDOW_MINIMIZED; break;
                case WindowChange::Restored: ev.type = SDL_EVENT_WINDOW_RESTORED; break;
                case WindowChange::Hidden: ev.type = SDL_EVENT_WINDOW_HIDDEN; break;
                case WindowChange::Shown: ev.type = SDL_EVENT_WINDOW_SHOWN; break;
                case WindowChange::Occluded: ev.type = SDL_EVENT_WINDOW_OCCLUDED; break;
                case WindowChange::Exposed: ev.type = SDL_EVENT_WINDOW_EXPOSED; break;
            }
            ev.window.windowID = window;
            break;
    }
    return ev;
}

bool isUserInput(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING:
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST: return true;
        default: return false;
    }
}

} // namespace opense4::client::script
