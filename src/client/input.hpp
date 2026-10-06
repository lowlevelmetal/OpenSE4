#pragma once

// Rebindable controls. Every hotkey of the classic main window (docs/spec/06
// §3) is an Action with a primary and an optional secondary key chord; the
// defaults are the classic keys. Stored in the app settings file.

#include <imgui.h>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client {

enum class Action : uint8_t {
    // Windows; ContextHelp opens the manual page of the window in front.
    Help, GameMenu, Designs, Planets, Colonies, Ships, Queues, Research, Empires, Log, EmpireStatus, EndTurn, Settings,
    ContextHelp,
    // The lesson panel (docs/LEARNING.md): LessonText shows or hides it; the
    // others press its Next, Back, Skip and Read More while it shows.
    LessonText, LessonNext, LessonBack, LessonSkip, LessonReadMore,
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
    // OpenSE4's AI notes view (computer players of mods).
    AiNotes,
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

// AltGr counts as Alt on every platform (applied to each SDL key event's
// modifiers before ImGui sees it). SDL reports AltGr as the right Alt on
// Windows, where it drops the left Ctrl that Windows adds, but on Linux as the
// mode key without Alt: a key typed with AltGr was a plain key there and fired
// the letter shortcuts, and on Windows it did not. Text input is unchanged
// (ImGui takes characters typed with Alt).
uint16_t altGrAsAlt(uint16_t sdlKeymod);

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
// The chord went down this frame with exactly its modifiers (never while ImGui types text).
bool chordPressed(const KeyChord& c);

// ---- The mods' actions (docs/sdk/interface.md "Key bindings") ----------------------------------
//
// A mod's interface may suggest keys for its orders, report panels and
// Empires pages. They join the rebindable keys (the Settings' Controls page,
// under the mod's name): the player's own choice wins; a suggestion is used
// only while no other binding (the game's or an earlier mod's) has that key,
// and is otherwise left unbound with the conflict said, never taking the
// other binding's place.
struct ModAction {
    std::string id;          // "<mod id>:order:<name>", "<mod id>:panel:<name>", "<mod id>:page:<name>"
    std::string group;       // the Controls page's heading: the mod's name
    std::string label;       // "Overcharge shields"
    std::string suggested;   // the chord the mod suggests ("Ctrl+Shift+O"); empty: none
};
// The player's choices, by action id (settings.toml [controls.mod_keys]).
using ModKeyChoices = std::map<std::string, std::array<KeyChord, 2>>;
struct ModKeys {
    std::array<KeyChord, 2> chords;
    bool chosen = false;     // the player's choice, not the suggestion
    std::string conflict;    // why the suggestion is not used ("Ctrl+O is the key of Move to"); empty: none
};
// The keys of `actions`, in their order (see above).
std::vector<ModKeys> resolveModKeys(const Bindings& b, const ModKeyChoices& chosen, std::span<const ModAction> actions);
// What uses a chord besides the action `except` among the game's bindings and
// the mods' keys: the game's action's label, or the mod action's; empty: nothing.
std::string chordUser(const Bindings& b, const ModKeyChoices& chosen, std::span<const ModAction> actions, const KeyChord& c,
                      std::string_view exceptModAction = {}, std::optional<Action> exceptAction = std::nullopt);

// The mods' actions of the data set in use (set when the mods load; empty without them).
void setModActions(std::vector<ModAction> actions);
std::span<const ModAction> modActions();

} // namespace opense4::client
