#pragma once

// The facility letter markers of the system window (Empire Options, System
// Display, docs/spec/06 §1.9): twelve groups of letters, each switched on by
// its own row. The spec names the letters and the first two groups; which
// ability each other letter stands for is read from the letters (inferred,
// spec 06 §7).

#include "game/rules.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <string>

namespace opense4::client::classic {

// The markers of one colony for the groups switched on (bit i: group i,
// game::kFacilityMarkerGroups groups), separated by spaces in group order,
// such as "R Y Cc". Empty when no facility of the colony matches.
std::string facilityMarkers(const game::Rules& r, const game::Colony& c, uint16_t groups);

} // namespace opense4::client::classic
