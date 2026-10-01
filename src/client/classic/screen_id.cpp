#include "client/classic/screen_id.hpp"

#include <iterator>

namespace opense4::client::classic {

std::string_view windowId(ScreenId id) {
    // learn/ids.hpp lists the same ids in ScreenId order (a test checks it).
    static constexpr std::string_view kIds[] = {
        "game-menu", "designs", "create-design", "planets", "colonies", "ships", "queues", "set-queue", "research", "tech-tree",
        "empires", "log", "empire-status", "help", "galaxy-map", "empire-options", "ministers", "systems-to-avoid", "waypoints",
        "strategies", "repair-priorities", "fleet-transfer", "cargo-transfer", "launch-recover", "scrap", "view-orders",
        "select-waypoint", "stellar-manipulation", "rename", "abandon-planet", "jettison-cargo", "convert-resources",
        "communicate", "intelligence", "treaty-grid", "scores",
        "comparisons", "history", "race-report", "victory-conditions", "combat-replay", "tactical-combat", "tactical-orders", "tactical-options",
        "tactical-launch", "combat-piece-report", "combat-replay-options", "combat-simulator", "strategic-combat", "ground-combat",
        "save-game", "load-game", "options", "settings", "learn", "manual"};
    static_assert(std::size(kIds) == static_cast<size_t>(ScreenId::Count));
    const auto i = static_cast<size_t>(id);
    return i < std::size(kIds) ? kIds[i] : std::string_view{};
}

std::optional<ScreenId> screenFromWindowId(std::string_view id) {
    for (int i = 0; i < static_cast<int>(ScreenId::Count); ++i)
        if (windowId(static_cast<ScreenId>(i)) == id) return static_cast<ScreenId>(i);
    return std::nullopt;
}

} // namespace opense4::client::classic
