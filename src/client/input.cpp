#include "client/input.hpp"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace opense4::client {

namespace {

constexpr std::array<ActionInfo, kActionCount> kActions{{
    {Action::Help, "Windows", "Help", "help"},
    {Action::GameMenu, "Windows", "Game menu", "game_menu"},
    {Action::Designs, "Windows", "Designs", "designs"},
    {Action::Planets, "Windows", "Planets", "planets"},
    {Action::Colonies, "Windows", "Colonies", "colonies"},
    {Action::Ships, "Windows", "Ships and units", "ships"},
    {Action::Queues, "Windows", "Construction queues", "queues"},
    {Action::Research, "Windows", "Research", "research"},
    {Action::Empires, "Windows", "Empires", "empires"},
    {Action::Log, "Windows", "Log", "log"},
    {Action::EmpireStatus, "Windows", "Empire status", "empire_status"},
    {Action::EndTurn, "Windows", "End turn", "end_turn"},
    {Action::Settings, "Windows", "Settings", "settings"},
    {Action::ContextHelp, "Windows", "Manual page of the window in front", "context_help"},
    {Action::LessonText, "Lesson panel", "Show or hide the lesson or training panel", "lesson_text"},
    {Action::LessonNext, "Lesson panel", "Next (or Finish)", "lesson_next"},
    {Action::LessonBack, "Lesson panel", "Back", "lesson_back"},
    {Action::LessonSkip, "Lesson panel", "Skip, when the panel offers it", "lesson_skip"},
    {Action::LessonReadMore, "Lesson panel", "Read More", "lesson_read_more"},
    {Action::MoveTo, "Orders", "Move to", "move_to"},
    {Action::Warp, "Orders", "Warp", "warp"},
    {Action::Attack, "Orders", "Attack", "attack"},
    {Action::Colonize, "Orders", "Colonize", "colonize"},
    {Action::Resupply, "Orders", "Resupply", "resupply"},
    {Action::Repair, "Orders", "Repair", "repair"},
    {Action::ClearOrders, "Orders", "Clear orders", "clear_orders"},
    {Action::FleetTransfer, "Orders", "Fleet transfer", "fleet_transfer"},
    {Action::BuildQueue, "Orders", "Construction queue", "build_queue"},
    {Action::CargoTransfer, "Orders", "Cargo transfer", "cargo_transfer"},
    {Action::LaunchRecover, "Orders", "Launch or recover units", "launch_recover"},
    {Action::LoadCargo, "Orders", "Load cargo", "load_cargo"},
    {Action::DropCargo, "Orders", "Drop cargo", "drop_cargo"},
    {Action::Sentry, "Orders", "Sentry", "sentry"},
    {Action::Explore, "Orders", "Explore", "explore"},
    {Action::Patrol, "Orders", "Set patrol", "patrol"},
    {Action::RepeatOrders, "Orders", "Repeat orders", "repeat_orders"},
    {Action::StellarManipulation, "Orders", "Stellar manipulation", "stellar"},
    {Action::ViewOrders, "Orders", "View orders", "view_orders"},
    {Action::Scrap, "Orders", "Scrap, analyze, mothball", "scrap"},
    {Action::Rename, "Orders", "Change name", "rename"},
    {Action::Cloak, "Orders", "Cloak", "cloak"},
    {Action::Decloak, "Orders", "Decloak", "decloak"},
    {Action::MoveToWaypoint, "Orders", "Move to a waypoint", "move_to_waypoint"},
    {Action::LaunchRemote, "Orders", "Launch units at a location", "launch_remote"},
    {Action::RecoverRemote, "Orders", "Recover units at a location", "recover_remote"},
    {Action::Strategy, "Orders", "Fleet formation and strategy", "strategy"},
    {Action::Jettison, "Orders", "Jettison cargo", "jettison"},
    {Action::SweepMines, "Orders", "Sweep mines", "sweep_mines"},
    {Action::TagMinefield, "Orders", "Tag the selected sector as a minefield", "tag_minefield"},
    {Action::UntagMinefield, "Orders", "Remove the selected sector's minefield tag", "untag_minefield"},
    {Action::AbandonPlanet, "Orders", "Abandon the planet", "abandon_planet"},
    {Action::ConvertResources, "Orders", "Convert resources", "convert_resources"},
    {Action::UseComponent, "Orders", "Use a component", "use_component"},
    {Action::UseFacility, "Orders", "Use a facility", "use_facility"},
    {Action::ScrapFacilities, "Orders", "Scrap facilities", "scrap_facilities"},
    {Action::Minister, "Orders", "Minister control on or off", "minister"},
    {Action::ReplayPlay, "Movement log", "Play the movement log", "replay_play"},
    {Action::ReplayRewind, "Movement log", "Rewind the movement log", "replay_rewind"},
    {Action::ReplayStep, "Movement log", "Step the movement log", "replay_step"},
    {Action::ReplayShip, "Movement log", "Play the movement log for every ship", "replay_ship"},
    {Action::NextIdleShip, "Selection", "Next ship without orders (turn-based: with movement left)", "next_idle_ship"},
    {Action::NextShip, "Selection", "Next ship", "next_ship"},
    {Action::PreviousShip, "Selection", "Previous ship", "previous_ship"},
    {Action::NextFleet, "Selection", "Next fleet", "next_fleet"},
    {Action::PreviousFleet, "Selection", "Previous fleet", "previous_fleet"},
    {Action::NextColony, "Selection", "Next colony", "next_colony"},
    {Action::PreviousColony, "Selection", "Previous colony", "previous_colony"},
    {Action::TagAll, "Selection", "Tag every own object in the list", "tag_all"},
    {Action::ClearTags, "Selection", "Clear the tags", "clear_tags"},
    {Action::MovementLines, "Display", "Show or hide movement lines", "movement_lines"},
    {Action::ToggleSound, "Display", "Sound effects on or off", "toggle_sound"},
    {Action::Cancel, "Display", "Cancel targeting / clear selection", "cancel"},
    {Action::ToggleFullscreen, "Display", "Toggle fullscreen", "toggle_fullscreen"},
    {Action::AiNotes, "Display", "Show or hide the computer players' notes", "ai_notes"},
}};

// actionInfo() indexes the table by the action.
constexpr bool inEnumOrder() {
    for (size_t i = 0; i < kActions.size(); ++i)
        if (static_cast<size_t>(kActions[i].action) != i) return false;
    return true;
}
static_assert(inEnumOrder(), "kActions lists the actions in the order of the Action enum");

KeyChord k(ImGuiKey key, bool ctrl = false, bool shift = false, bool alt = false) { return {key, ctrl, shift, alt}; }

bool modifiersDown(const KeyChord& c) {
    const ImGuiIO& io = ImGui::GetIO();
    return io.KeyCtrl == c.ctrl && io.KeyShift == c.shift && io.KeyAlt == c.alt;
}

bool isModifier(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_LeftCtrl:
        case ImGuiKey_RightCtrl:
        case ImGuiKey_LeftShift:
        case ImGuiKey_RightShift:
        case ImGuiKey_LeftAlt:
        case ImGuiKey_RightAlt:
        case ImGuiKey_LeftSuper:
        case ImGuiKey_RightSuper:
        case ImGuiMod_Ctrl:
        case ImGuiMod_Shift:
        case ImGuiMod_Alt:
        case ImGuiMod_Super: return true;
        default: return false;
    }
}

} // namespace

std::span<const ActionInfo> actionInfos() { return kActions; }

const ActionInfo& actionInfo(Action a) { return kActions[static_cast<size_t>(a)]; }

std::string chordName(const KeyChord& c) {
    if (c.empty()) return "(none)";
    std::string s;
    if (c.ctrl) s += "Ctrl+";
    if (c.shift) s += "Shift+";
    if (c.alt) s += "Alt+";
    return s + ImGui::GetKeyName(c.key);
}

std::optional<KeyChord> parseChord(std::string_view text) {
    KeyChord c;
    if (text.empty() || text == "(none)") return c;
    auto eat = [&](std::string_view prefix, bool& flag) {
        if (text.substr(0, prefix.size()) == prefix && text.size() > prefix.size()) {
            flag = true;
            text.remove_prefix(prefix.size());
            return true;
        }
        return false;
    };
    while (eat("Ctrl+", c.ctrl) || eat("Shift+", c.shift) || eat("Alt+", c.alt)) {
    }
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
        if (text == ImGui::GetKeyName(static_cast<ImGuiKey>(key))) {
            c.key = static_cast<ImGuiKey>(key);
            return c;
        }
    return std::nullopt;
}

Bindings::Bindings() { resetAll(); }

void Bindings::resetAll() {
    keys_ = {};
    auto def = [&](Action a, KeyChord primary, KeyChord secondary = {}) { keys_[static_cast<size_t>(a)] = {primary, secondary}; };
    // The classic hotkeys (docs/spec/06 §3).
    def(Action::Help, k(ImGuiKey_F1));
    def(Action::GameMenu, k(ImGuiKey_F2));
    def(Action::Designs, k(ImGuiKey_F3));
    def(Action::Planets, k(ImGuiKey_F4));
    def(Action::Colonies, k(ImGuiKey_F5));
    def(Action::Ships, k(ImGuiKey_F6));
    def(Action::Queues, k(ImGuiKey_F7));
    def(Action::Research, k(ImGuiKey_F8));
    def(Action::Empires, k(ImGuiKey_F9));
    def(Action::Log, k(ImGuiKey_F10));
    def(Action::EmpireStatus, k(ImGuiKey_F11));
    def(Action::EndTurn, k(ImGuiKey_F12), k(ImGuiKey_Enter));
    def(Action::Settings, k(ImGuiKey_Comma, true));
    def(Action::ContextHelp, k(ImGuiKey_F1, false, true));
    // OpenSE4's lesson panel: Alt and the button's letter, as in a set-up
    // wizard. No prompt answers to K, B or R, and N there means No.
    def(Action::LessonText, k(ImGuiKey_H, true));
    def(Action::LessonNext, k(ImGuiKey_N, false, false, true));
    def(Action::LessonBack, k(ImGuiKey_B, false, false, true));
    def(Action::LessonSkip, k(ImGuiKey_K, false, false, true));
    def(Action::LessonReadMore, k(ImGuiKey_R, false, false, true));
    def(Action::MoveTo, k(ImGuiKey_M));
    def(Action::Warp, k(ImGuiKey_W));
    def(Action::Attack, k(ImGuiKey_A));
    def(Action::Colonize, k(ImGuiKey_C));
    def(Action::Resupply, k(ImGuiKey_S));
    def(Action::Repair, k(ImGuiKey_R));
    def(Action::ClearOrders, k(ImGuiKey_Delete), k(ImGuiKey_Backspace));
    def(Action::FleetTransfer, k(ImGuiKey_F));
    def(Action::BuildQueue, k(ImGuiKey_Q));
    def(Action::CargoTransfer, k(ImGuiKey_T));
    def(Action::LaunchRecover, k(ImGuiKey_U));
    def(Action::LoadCargo, k(ImGuiKey_L));
    def(Action::DropCargo, k(ImGuiKey_D));
    def(Action::Sentry, k(ImGuiKey_Y));
    def(Action::Explore, k(ImGuiKey_E));
    def(Action::Patrol, k(ImGuiKey_P));
    def(Action::RepeatOrders, k(ImGuiKey_K));
    def(Action::StellarManipulation, k(ImGuiKey_B));
    def(Action::ViewOrders, k(ImGuiKey_V));
    def(Action::Scrap, k(ImGuiKey_G));
    def(Action::Rename, k(ImGuiKey_N));
    def(Action::Cloak, k(ImGuiKey_Z));
    def(Action::Decloak, k(ImGuiKey_X));
    def(Action::MoveToWaypoint, k(ImGuiKey_W, true));
    def(Action::LaunchRemote, k(ImGuiKey_I));
    def(Action::RecoverRemote, k(ImGuiKey_O));
    def(Action::Strategy, k(ImGuiKey_H));
    def(Action::Jettison, k(ImGuiKey_J));
    def(Action::SweepMines, k(ImGuiKey_M, true));
    def(Action::TagMinefield, k(ImGuiKey_T, true));
    def(Action::UntagMinefield, k(ImGuiKey_R, true));
    def(Action::AbandonPlanet, k(ImGuiKey_A, true));
    def(Action::ConvertResources, k(ImGuiKey_V, true));
    def(Action::UseComponent, k(ImGuiKey_Z, true));
    def(Action::UseFacility, k(ImGuiKey_J, true));
    def(Action::ScrapFacilities, k(ImGuiKey_K, true));
    def(Action::Minister, k(ImGuiKey_Y, true));
    def(Action::ReplayPlay, k(ImGuiKey_P, true));
    def(Action::ReplayRewind, k(ImGuiKey_O, true));
    def(Action::ReplayStep, k(ImGuiKey_I, true));
    def(Action::ReplayShip, k(ImGuiKey_U, true));
    def(Action::NextIdleShip, k(ImGuiKey_Space));
    def(Action::NextShip, k(ImGuiKey_N, true));
    def(Action::PreviousShip, k(ImGuiKey_B, true));
    def(Action::NextFleet, k(ImGuiKey_F, true));
    def(Action::PreviousFleet, k(ImGuiKey_D, true));
    def(Action::NextColony, k(ImGuiKey_C, true));
    def(Action::PreviousColony, k(ImGuiKey_X, true));
    def(Action::TagAll, k(ImGuiKey_A, false, true));
    def(Action::ClearTags, k(ImGuiKey_C, false, true));
    def(Action::MovementLines, k(ImGuiKey_L, true));
    def(Action::ToggleSound, k(ImGuiKey_S, true));
    def(Action::Cancel, k(ImGuiKey_Escape));
    def(Action::ToggleFullscreen, k(ImGuiKey_Enter, false, false, true));
    def(Action::AiNotes, k(ImGuiKey_N, true, true));
}

void Bindings::set(Action a, int slot, KeyChord c) {
    if (slot < 0 || slot > 1) return;
    keys_[static_cast<size_t>(a)][static_cast<size_t>(slot)] = c;
}

std::optional<Action> Bindings::boundTo(const KeyChord& c, Action except) const {
    if (c.empty()) return std::nullopt;
    for (size_t i = 0; i < kActionCount; ++i)
        if (static_cast<Action>(i) != except && (keys_[i][0] == c || keys_[i][1] == c)) return static_cast<Action>(i);
    return std::nullopt;
}

bool Bindings::pressed(Action a) const {
    if (ImGui::GetIO().WantTextInput) return false;
    for (const KeyChord& c : keys_[static_cast<size_t>(a)])
        if (!c.empty() && ImGui::IsKeyPressed(c.key, false) && modifiersDown(c)) return true;
    return false;
}

std::optional<KeyChord> capturePressedChord() {
    const ImGuiIO& io = ImGui::GetIO();
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
        const auto k2 = static_cast<ImGuiKey>(key);
        if (isModifier(k2) || (k2 >= ImGuiKey_MouseLeft && k2 <= ImGuiKey_MouseWheelY)) continue;
        if (ImGui::IsKeyPressed(k2, false)) return KeyChord{k2, io.KeyCtrl, io.KeyShift, io.KeyAlt};
    }
    return std::nullopt;
}

bool chordPressed(const KeyChord& c) {
    if (c.empty() || ImGui::GetIO().WantTextInput) return false;
    return ImGui::IsKeyPressed(c.key, false) && modifiersDown(c);
}

// ---- The mods' actions ---------------------------------------------------------------------------

std::vector<ModKeys> resolveModKeys(const Bindings& b, const ModKeyChoices& chosen, std::span<const ModAction> actions) {
    std::vector<ModKeys> out(actions.size());
    // The player's choices first: they are kept whatever a mod suggests.
    for (size_t i = 0; i < actions.size(); ++i)
        if (auto it = chosen.find(actions[i].id); it != chosen.end()) {
            out[i].chords = it->second;
            out[i].chosen = true;
        }
    auto taken = [&](const KeyChord& c, size_t self) -> std::string {
        if (auto a = b.boundTo(c, Action::Count)) return actionInfo(*a).label;
        for (size_t j = 0; j < actions.size(); ++j)
            if (j != self && (out[j].chords[0] == c || out[j].chords[1] == c)) return actions[j].label;
        return {};
    };
    for (size_t i = 0; i < actions.size(); ++i) {
        if (out[i].chosen || actions[i].suggested.empty()) continue;
        const std::optional<KeyChord> c = parseChord(actions[i].suggested);
        if (!c || c->empty()) {
            out[i].conflict = std::format("{} is not a key this game knows", actions[i].suggested);
            continue;
        }
        if (std::string user = taken(*c, i); !user.empty()) {
            out[i].conflict = std::format("{} is the key of {}", chordName(*c), user);
            continue;
        }
        out[i].chords[0] = *c;
    }
    return out;
}

std::string chordUser(const Bindings& b, const ModKeyChoices& chosen, std::span<const ModAction> actions, const KeyChord& c,
                      std::string_view exceptModAction, std::optional<Action> exceptAction) {
    if (c.empty()) return {};
    if (auto a = b.boundTo(c, exceptAction.value_or(Action::Count))) return actionInfo(*a).label;
    const std::vector<ModKeys> keys = resolveModKeys(b, chosen, actions);
    for (size_t i = 0; i < actions.size(); ++i)
        if (actions[i].id != exceptModAction && (keys[i].chords[0] == c || keys[i].chords[1] == c)) return actions[i].label;
    return {};
}

namespace {
std::vector<ModAction>& modActionList() {
    static std::vector<ModAction> list;
    return list;
}
} // namespace

void setModActions(std::vector<ModAction> actions) { modActionList() = std::move(actions); }
std::span<const ModAction> modActions() { return modActionList(); }

uint16_t altGrAsAlt(uint16_t sdlKeymod) {
    return (sdlKeymod & SDL_KMOD_MODE) != 0 ? static_cast<uint16_t>(sdlKeymod | SDL_KMOD_RALT) : sdlKeymod;
}

} // namespace opense4::client
