#pragma once

// The combat map shared by the Combat Replay, Tactical Combat and Strategic
// Combat windows (docs/spec/06 §1.6): the system background and grid, every piece with its
// picture, facing and owner frame, and the event being played (moves,
// beams and torpedoes, hits and misses, explosions, captures, launches).
// The pieces' squares come from a CombatPlayback over the battle's record,
// so both windows animate the same way.

#include "client/classic/replay.hpp"
#include "client/classic/ui.hpp"

#include <optional>
#include <string>
#include <vector>

namespace opense4::client::classic {

// Squares <-> screen pixels.
struct CombatView {
    ImVec2 center;         // screen position of square coordinates (cx, cy)
    float cell = 20;       // pixels per square
    float cx = 0, cy = 0;  // square coordinates at the centre
    ImVec2 at(float sx, float sy) const { return {center.x + (sx - cx) * cell, center.y + (sy - cy) * cell}; }
    // The square under a screen point.
    std::pair<int, int> square(ImVec2 p) const;
};

class CombatMapPainter {
public:
    // `state` names the record's designs, empires and planets (a sandbox for simulations).
    CombatMapPainter(UiContext& ui, const game::GameState& state, const game::CombatRecord& record, const CombatPlayback& playback)
        : ui_(ui), s_(state), record_(record), playback_(playback) {}

    // ---- Names and pictures -------------------------------------------------------------------
    std::string pieceName(uint32_t i) const;     // with its empire when another side has one of the same name
    std::string empireName(game::EmpireId e) const;
    const std::string& styleOf(game::EmpireId e) const;
    const ruleset::VehicleSize* hullOf(const game::CombatPiece& p) const;
    // The piece's picture and whether it turns to its heading (top-down ships do).
    Sprite pieceSprite(const game::CombatPiece& p, game::EmpireId owner, bool& directional) const;
    std::string weaponName(uint32_t component) const;
    std::string eventText(const game::CombatEvent& e) const;

    // ---- Drawing --------------------------------------------------------------------------------
    // Black, the system's background picture, and the square grid.
    void background(ImDrawList* dl, ImVec2 min, ImVec2 max, const CombatView& v, ImU32 gridColor) const;
    // Where piece i is drawn now (between squares while its move animates).
    ImVec2 piecePos(const CombatView& v, uint32_t i) const;
    float pieceHeading(uint32_t i) const;
    float pieceExtent(uint32_t i) const;
    // Every piece on the map; returns the one under `mouse` when `hover` is set.
    std::optional<uint32_t> pieces(ImDrawList* dl, const CombatView& v, bool hover, ImVec2 mouse) const;
    // Every piece as a square of its owner's colour (planets and obstacles 4x4,
    // neutral grey, seekers as dots), for small maps: the Strategic Combat
    // window's map and the Tactical Combat overview. Returns the piece under
    // `mouse` when `hover` is set. `minSize` is the smallest square in pixels.
    std::optional<uint32_t> squares(ImDrawList* dl, const CombatView& v, float minSize, bool hover = false, ImVec2 mouse = {}) const;
    // The event being animated, at its current frame (CombatPlayback::frame()).
    void event(ImDrawList* dl, const CombatView& v, const game::CombatEvent& e, const AnimationFrame& f) const;
    // Weapon and explosion sounds for the events played between two cursors.
    void sounds(size_t from, size_t to) const;

private:
    UiContext& ui_;
    const game::GameState& s_;
    const game::CombatRecord& record_;
    const CombatPlayback& playback_;
};

ImU32 withAlpha(ImU32 c, float a);

// ---- Combat simulator sides (spec 04 §17, spec 06 §1.10.4, confirmed: binary) ----------------------
// A simulation shows each side not by a flag but by a box in the side's fixed
// colour holding its number: 1 red, 2 blue, 3 green, 4 yellow, 5 purple, 6
// white, 7 aqua, 8 lime, 9 maroon, 10 olive; the number in white on the dark
// colours (1, 2, 3, 5, 9, 10), in black on the others. Wherever a real battle
// shows a flag, the battle windows of a simulation and the reports opened
// from them show the box.

// The simulation being fought: its sandbox (TacticalBattle::state()) and the
// virtual empire of each side, in side order (Simulation::sides). Set when
// the simulator starts a battle; replaced by the next one.
void setSimulationSides(const game::GameState* sandbox, std::vector<game::EmpireId> sides);
// The side number (1 to 10) of an empire in the simulation fought on `state`;
// 0 when `state` is not that sandbox or the empire is no side of it.
int simulationSide(const game::GameState& state, game::EmpireId e);
// A side's colour (1-based; 0 and out of range: grey) and its number's.
ImU32 sideBoxColor(int side);
ImU32 sideNumberColor(int side);
// The box over a rectangle of the draw list, and as an item at the cursor.
void drawSideBox(UiContext& ui, ImDrawList* dl, ImVec2 min, ImVec2 max, int side);
void sideBox(UiContext& ui, int side, Vec2 size);
// An owner's mark as an item at the cursor: the side's box in a simulation,
// else the empire's flag (`small`: the small flag). False when there is none.
bool ownerMark(UiContext& ui, const game::GameState& s, game::EmpireId e, Vec2 size, bool small = true);

// The pace of the combat windows' animations (spec 06 §1.10.3): Fast Tactical
// Combat and "animate ship movement", and which weapons are drawn as beams.
CombatPace combatPace(const game::Rules& r, bool fast, bool animateMoves);
// Whether square (x, y) lies wholly in the shown part of a map drawn with `v`
// over [mapMin, mapMax] (a move with a square outside it is not animated).
bool squareInView(const CombatView& v, ImVec2 mapMin, ImVec2 mapMax, int x, int y);

} // namespace opense4::client::classic
