#include "client/input.hpp"

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
    {Action::NextIdleShip, "Selection", "Next ship without orders", "next_idle_ship"},
    {Action::NextShip, "Selection", "Next ship", "next_ship"},
    {Action::PreviousShip, "Selection", "Previous ship", "previous_ship"},
    {Action::NextFleet, "Selection", "Next fleet", "next_fleet"},
    {Action::PreviousFleet, "Selection", "Previous fleet", "previous_fleet"},
    {Action::NextColony, "Selection", "Next colony", "next_colony"},
    {Action::PreviousColony, "Selection", "Previous colony", "previous_colony"},
    {Action::MovementLines, "Display", "Show or hide movement lines", "movement_lines"},
    {Action::Cancel, "Display", "Cancel targeting / clear selection", "cancel"},
    {Action::ToggleFullscreen, "Display", "Toggle fullscreen", "toggle_fullscreen"},
}};

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
    def(Action::NextIdleShip, k(ImGuiKey_Space));
    def(Action::NextShip, k(ImGuiKey_N, true));
    def(Action::PreviousShip, k(ImGuiKey_B, true));
    def(Action::NextFleet, k(ImGuiKey_F, true));
    def(Action::PreviousFleet, k(ImGuiKey_D, true));
    def(Action::NextColony, k(ImGuiKey_C, true));
    def(Action::PreviousColony, k(ImGuiKey_X, true));
    def(Action::MovementLines, k(ImGuiKey_L, true));
    def(Action::Cancel, k(ImGuiKey_Escape));
    def(Action::ToggleFullscreen, k(ImGuiKey_Enter, false, false, true));
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

} // namespace opense4::client
