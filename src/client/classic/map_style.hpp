#pragma once

// How the galaxy panel, the Galaxy Map window and the system panel draw
// their marks (docs/spec/06 §2.4, §2.6, confirmed: binary): colours, the
// galaxy grid and the symbol of each system under each overlay. Headless,
// tested in tests/test_client_logic.cpp.

#include "game/state.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace opense4::client::classic::map_style {

// Colours as 0xRRGGBB.
inline constexpr uint32_t kUnexplored = 0x7e7e7e;   // (126,126,126)
inline constexpr uint32_t kExplored = 0xeeeeee;     // (238,238,238)
inline constexpr uint32_t kGrid = 0x15203b;         // (21,32,59): galaxy and system grid lines
inline constexpr uint32_t kWarpLine = 0xa5b0b3;     // (165,176,179)
inline constexpr uint32_t kHover = 0x00ffff;        // the hovered system and Show Names
inline constexpr uint32_t kGreen = 0x008000;        // Spaceports / Resupply Depots: with the facility
inline constexpr uint32_t kYellow = 0xffff00;       // without it; several claimants
inline constexpr uint32_t kCloakNoColor = 0x008080; // a cloaked ship's ring when its empire has no colour
inline constexpr uint32_t kWaypoint = 0x00ffff;     // waypoint frames and numbers in the system panel

// The galaxy grid is always 68 × 47 cells, whatever the quadrant's size
// (systems sit at x 1..67, y 1..46); a cell is the box's size div 68 and div 47.
inline constexpr int kGridColumns = 68;
inline constexpr int kGridRows = 47;
struct GridCell {
    int w = 1, h = 1;
};
GridCell gridCell(int boxWidth, int boxHeight);

// The overlays of the Galaxy Map window (Presence is the galaxy panel's too).
enum class Overlay { Presence, Avoid, AllyClaimed, EnemyClaimed, Spaceports, ResupplyDepots };

enum class Shape { Ring, Triangle, Disc };
// One system's symbol: its shape and colour, either an empire's colour or a
// fixed one. `outerRing`: a second ring 2 px outside the cell, in the same colour.
struct Symbol {
    Shape shape = Shape::Ring;
    std::optional<game::EmpireId> empire;  // the colour is this empire's
    uint32_t rgb = kUnexplored;            // otherwise this
    bool outerRing = false;
};

// The neutral ring every overlay starts from.
Symbol baseSymbol(bool explored);
// Presence: nobody seen there, one empire, or several (a triangle in the
// viewer's colour when the viewer is among them, otherwise in the colour of
// the highest-numbered empire present).
Symbol presenceSymbol(bool explored, std::span<const game::EmpireId> present, game::EmpireId viewer);
// Avoid: an avoided explored system gets a ring in the viewer's colour.
Symbol avoidSymbol(bool explored, bool avoided, game::EmpireId viewer);
// Ally / Enemy Claimed: the claimants counted for the tab.
Symbol claimedSymbol(bool explored, std::span<const game::EmpireId> claimants);
// Spaceports / Resupply Depots: where the viewer has a colony, green with the
// facility, yellow without.
Symbol facilitySymbol(bool explored, bool colony, bool facility);

// The heading of a mini that moved from one sector to another in a system:
// 0..7, 45° steps clockwise from up, the bearing rounded to the nearest step
// (23–67° is 45°, 338–22° is up); the engine's movement::headingFor.
int headingStep(game::Sector from, game::Sector to);

// Where a hovered system's name goes: the first corner, of above-right,
// below-right, above-left and below-left, where a `textW` × `textH` box beside
// the `cellW` × `cellH` cell at (x, y) fits in a box of `boxW` × `boxH`.
// Returns the text's top-left corner, relative to the box.
struct Point {
    float x = 0, y = 0;
};
Point nameCorner(float x, float y, float cellW, float cellH, float textW, float textH, float boxW, float boxH);

} // namespace opense4::client::classic::map_style
