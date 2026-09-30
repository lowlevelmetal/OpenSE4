#pragma once

// The combat map shared by the Combat Replay and Tactical Combat windows
// (docs/spec/06 §1.6): the system background and grid, every piece with its
// picture, facing and owner frame, and the event being played (moves,
// beams and torpedoes, hits and misses, explosions, captures, launches).
// The pieces' squares come from a CombatPlayback over the battle's record,
// so both windows animate the same way.

#include "client/classic/replay.hpp"
#include "client/classic/ui.hpp"

#include <optional>
#include <string>

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
    // The event being animated, `t` of the way through.
    void event(ImDrawList* dl, const CombatView& v, const game::CombatEvent& e, float t) const;
    // Weapon and explosion sounds for the events played between two cursors.
    void sounds(size_t from, size_t to) const;

private:
    UiContext& ui_;
    const game::GameState& s_;
    const game::CombatRecord& record_;
    const CombatPlayback& playback_;
};

ImU32 withAlpha(ImU32 c, float a);

} // namespace opense4::client::classic
