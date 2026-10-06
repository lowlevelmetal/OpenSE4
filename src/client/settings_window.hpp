#pragma once

// The Settings pages shared by the intro's Settings screen and the in-game
// Settings window: Graphics (display, renderer, scaling, frame rate) and
// Controls (rebindable keys, mouse). Drawn into the current ImGui window;
// changes are applied and saved at once.

#include "client/input.hpp"
#include "client/mode.hpp"

#include <optional>
#include <string>
#include <utility>

namespace opense4::client {

struct SettingsPanelState {
    // Key capture: (action, slot) waiting for a key press.
    std::optional<std::pair<Action, int>> capturing;
    // ... or (mod action id, slot), for a mod's key (input.hpp ModAction).
    std::optional<std::pair<std::string, int>> capturingMod;
    bool capturingKey() const { return capturing.has_value() || capturingMod.has_value(); }
    std::string message;
    // Pending display choice until Apply.
    bool displayDirty = false;
};

// `px`: ImGui units per design pixel (1 in a plain window; the frame scale in the classic client).
void graphicsSettingsPage(SettingsPanelState& state, AppControl& app, float px);
void controlsSettingsPage(SettingsPanelState& state, float px);

} // namespace opense4::client
