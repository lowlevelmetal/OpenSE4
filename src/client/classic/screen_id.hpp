#pragma once

// The ids of the classic client's windows: the ScreenId enum and the window
// ids that lessons and manual links use (docs/LEARNING.md).

#include <cstdint>
#include <optional>
#include <string_view>

namespace opense4::client::classic {

// Every window of the classic client. The main window is not a Screen.
enum class ScreenId {
    // Command buttons (docs/spec/06 §1.2).
    GameMenu, Designs, CreateDesign, Planets, Colonies, Ships, Queues, SetQueue, Research, TechTree, Empires, Log,
    EmpireStatus, Help, GalaxyMap,
    // Empire Status sub-windows.
    EmpireOptions, Ministers, SystemsToAvoid, Waypoints, Strategies, RepairPriorities,
    // Order dialogs (§1.3).
    FleetTransfer, CargoTransfer, LaunchRecover, Scrap, ViewOrders, SelectWaypoint, StellarManipulation, Rename,
    // Diplomacy and comparisons (§1.5).
    Communicate, Intelligence, TreatyGrid, Scores, Comparisons, History, RaceReport, VictoryConditions,
    // Combat (§1.6).
    CombatReplay, TacticalCombat, TacticalOrders, TacticalOptions, CombatSimulator, StrategicCombat, GroundCombat,
    // Files.
    SaveGame, LoadGame,
    // Graphics, controls and sound.
    Settings,
    // Learning to play (docs/LEARNING.md): tutorials, training games and the manual.
    Learn, Manual,
    Count
};

// The window id lessons and manual links use: the kebab-case of the ScreenId
// name ("create-design"; learn/ids.hpp lists the same ids), and back.
std::string_view windowId(ScreenId id);
std::optional<ScreenId> screenFromWindowId(std::string_view id);

} // namespace opense4::client::classic
