#pragma once

// Rebindable controls. Every hotkey of the classic main window (docs/spec/06
// §3) is an Action with a primary and an optional secondary key chord; the
// defaults are the classic keys. Stored in the app settings file.

#include <imgui.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace opense4::client {

enum class Action : uint8_t {
    // Windows; LessonText re-opens the lesson panel, ContextHelp the manual page of the window in front.
    Help, GameMenu, Designs, Planets, Colonies, Ships, Queues, Research, Empires, Log, EmpireStatus, EndTurn, Settings,
    LessonText, ContextHelp,
    // Orders for the selection (each works only while its button is lit, docs/spec/06 §2.8).
    MoveTo, Warp, Attack, Colonize, Resupply, Repair, ClearOrders, FleetTransfer, BuildQueue, CargoTransfer, LaunchRecover,
    LoadCargo, DropCargo, Sentry, Explore, Patrol, RepeatOrders, StellarManipulation, ViewOrders, Scrap, Rename, Cloak, Decloak,
    MoveToWaypoint, LaunchRemote, RecoverRemote, Strategy, Jettison, SweepMines, TagMinefield, UntagMinefield, AbandonPlanet,
    ConvertResources, UseComponent, UseFacility, ScrapFacilities, Minister,
    // The movement log of a simultaneous game.
    ReplayPlay, ReplayRewind, ReplayStep, ReplayShip,
    // Selection.
    NextIdleShip, NextShip, PreviousShip, NextFleet, PreviousFleet, NextColony, PreviousColony, TagAll, ClearTags,
    // Display and interface.
    MovementLines, ToggleSound, Cancel, ToggleFullscreen,
    Count
};
inline constexpr size_t kActionCount = static_cast<size_t>(Action::Count);

struct KeyChord {
    ImGuiKey key = ImGuiKey_None;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool empty() const { return key == ImGuiKey_None; }
    bool operator==(const KeyChord&) const = default;
};

struct ActionInfo {
    Action action;
    const char* group;   // heading in the Controls tab
    const char* label;   // our own wording
    const char* key;     // settings file key
};
std::span<const ActionInfo> actionInfos();
const ActionInfo& actionInfo(Action a);

// "Ctrl+Shift+F5", "Delete", "(none)".
std::string chordName(const KeyChord& c);
// Parses what chordName writes; nullopt on nonsense.
std::optional<KeyChord> parseChord(std::string_view text);

class Bindings {
public:
    Bindings();  // the classic defaults

    const std::array<KeyChord, 2>& chords(Action a) const { return keys_[static_cast<size_t>(a)]; }
    void set(Action a, int slot, KeyChord c);
    void resetAll();
    // The action another binding already uses this chord for (conflict check).
    std::optional<Action> boundTo(const KeyChord& c, Action except) const;

    // True on the frame the action's key went down with exactly its modifiers
    // (never while ImGui is typing text).
    bool pressed(Action a) const;

private:
    std::array<std::array<KeyChord, 2>, kActionCount> keys_;
};

// The chord pressed this frame, if any (for "press a key" capture).
std::optional<KeyChord> capturePressedChord();

} // namespace opense4::client
