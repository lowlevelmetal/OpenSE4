#pragma once

// The quadrant map as drawn inside windows (Galaxy Map, Systems To Avoid,
// Waypoints): the 68 × 47 grid, warp lines from explored systems (stubs to
// unexplored ones) and one symbol per system, in the colours of
// docs/spec/06 §2.6 (map_style.hpp). Drawn with ImGui into the current
// window; one grid square is 10 light-years.

#include "client/classic/map_style.hpp"
#include "client/classic/ui.hpp"

#include <optional>
#include <string>
#include <vector>

namespace opense4::client::classic {

// What the symbols show.
enum class MapOverlay {
    Presence,        // unexplored grey, explored white, an empire's colour, a triangle for several
    Avoid,           // our explored systems to avoid: a ring in our colour
    AllyClaimed,     // systems claimed by us or our allies: the claimant's colour, yellow for several
    EnemyClaimed,    // systems claimed by empires we are hostile to
    Spaceports,      // our colonies: a green ring with a spaceport, yellow without
    ResupplyDepots,  // our colonies: a green ring with a resupply depot, yellow without
};

struct QuadrantMapOptions {
    MapOverlay overlay = MapOverlay::Presence;
    bool names = false;       // system names under the symbols
    bool distances = false;   // light-years from the hovered system
    std::optional<game::SystemId> current;         // the symbol filled, with a ring outside the cell
    std::vector<game::SystemId> highlight;         // extra marker ring (e.g. waypoints)
    std::vector<std::pair<game::SystemId, std::string>> tags;  // short labels above a system
    bool avoidRings = false;  // also ring avoided systems (in our colour) on other overlays
};

struct QuadrantMapResult {
    std::optional<game::SystemId> hovered;
    std::optional<game::SystemId> clicked;
};

// Draws the map in a frameSize (frame pixels) box at the cursor and handles hover/clicks.
QuadrantMapResult quadrantMap(UiContext& ui, const char* id, Vec2 frameSize, const QuadrantMapOptions& options);

// A system's symbol under an overlay (map_style.hpp).
map_style::Symbol systemSymbol(const UiContext& ui, game::SystemId sys, MapOverlay overlay,
                               const std::vector<std::vector<game::EmpireId>>& presence);
// Empires the local player sees in each system (own and visible vehicles, known colonies).
std::vector<std::vector<game::EmpireId>> systemPresence(const UiContext& ui);
// Straight-line distance between two systems in light-years (grid squares × 10, rounded).
int lightYears(const game::Galaxy& g, game::SystemId a, game::SystemId b);
// Systems the player has explored, sorted by name.
std::vector<game::SystemId> exploredSystems(const UiContext& ui);

} // namespace opense4::client::classic
