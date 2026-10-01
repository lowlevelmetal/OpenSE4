#pragma once

// The facility letter markers of the system window (Empire Options, System
// Display rows; docs/spec/06 §1.9, §2.4, §7 Q44, confirmed: binary): twelve
// rows, each switching on its letter groups. Every test reads the abilities
// of the colony's facilities, except Y: a working space yard (a `Space Yard`
// facility on a colony that is not cloaked). Headless, tested in
// tests/test_classic_options.cpp.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opense4::client::classic {

// The letter groups of one colony for the rows switched on (bit i: row i,
// game::kFacilityMarkerGroups rows), in the order of the Q44 table: "R", "S",
// "Y", "Ca", ..., "Slr". `yardWorks`: the colony's yard is not stopped by a
// cloak (our colonies cannot cloak yet, so callers pass true).
std::vector<std::string> facilityMarkerGroups(const game::Rules& r, const game::Colony& c, uint16_t rows, bool yardWorks = true);
// The groups joined with spaces (for lists and tests), such as "R Y Cc".
std::string facilityMarkers(const game::Rules& r, const game::Colony& c, uint16_t rows);

// Whose colonies show markers to `viewer`: its own, and those of empires with
// a Military Alliance or Partnership with it.
bool showsFacilityMarkers(const game::GameState& s, game::EmpireId viewer, game::EmpireId owner);

// Where each group goes (§2.4): packed with no spaces, right to left, the
// first group ending at the right edge of the planet's sprite square; a group
// that would start at or left of the square's left edge starts a new line one
// text height higher, again ending at the right edge. `x` is the group's left
// edge from the square's left edge, `line` counts lines upwards from the
// square's bottom (0: the bottom line).
struct MarkerPlace {
    int x = 0;
    int line = 0;
};
std::vector<MarkerPlace> packFacilityMarkers(const std::vector<int>& groupWidths, int squareWidth);

} // namespace opense4::client::classic
