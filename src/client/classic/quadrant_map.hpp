#pragma once

// The quadrant map as drawn inside windows (Galaxy Map, Systems To Avoid,
// Waypoints): a faint grid, known warp links, stubs for warp points whose
// destination is unknown, and one symbol per system (docs/spec/06 §2.6).
// Drawn with ImGui into the current window; one grid square is 10 light-years.

#include "client/classic/ui.hpp"

#include <optional>
#include <string>
#include <vector>

namespace opense4::client::classic {

// What the symbols show.
enum class MapOverlay {
    Presence,        // unexplored grey, explored white, empire colours, triangle for several
    Avoid,           // our systems to avoid in yellow
    AllyClaimed,     // systems claimed by us or our allies, in the claimant's colour
    EnemyClaimed,    // systems claimed by empires we are hostile to
    Spaceports,      // our colonies: green with a spaceport, yellow without
    ResupplyDepots,  // our colonies: green with a resupply depot, yellow without
};

struct QuadrantMapOptions {
    MapOverlay overlay = MapOverlay::Presence;
    bool names = false;       // system names under the symbols
    bool distances = false;   // light-years from the hovered system
    std::optional<game::SystemId> current;         // double circle with a filled centre
    std::vector<game::SystemId> highlight;         // extra marker ring (e.g. waypoints)
    std::vector<std::pair<game::SystemId, std::string>> tags;  // short labels above a system
    bool avoidRings = false;  // also ring avoided systems in yellow on other overlays
};

struct QuadrantMapResult {
    std::optional<game::SystemId> hovered;
    std::optional<game::SystemId> clicked;
};

// Draws the map in a frameSize (frame pixels) box at the cursor and handles hover/clicks.
QuadrantMapResult quadrantMap(UiContext& ui, const char* id, Vec2 frameSize, const QuadrantMapOptions& options);

// Empires the local player sees in each system (own and visible vehicles, known colonies).
std::vector<std::vector<game::EmpireId>> systemPresence(const UiContext& ui);
// Straight-line distance between two systems in light-years (grid squares × 10, rounded).
int lightYears(const game::Galaxy& g, game::SystemId a, game::SystemId b);
// Systems the player has explored, sorted by name.
std::vector<game::SystemId> exploredSystems(const UiContext& ui);

} // namespace opense4::client::classic
