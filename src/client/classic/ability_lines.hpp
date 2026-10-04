#pragma once

// The lines of a report's Ability page (docs/spec/06 §1.4 "The Ability page",
// §7 Q106), headless for the tests; reports.cpp draws them.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <string>
#include <vector>

namespace opense4::client::classic {

// The Ability page's lines (spec 06 §1.4 "The Ability page", confirmed:
// binary): nothing combined, summed, sorted or de-duplicated; each ability
// entry gives one line, its `Descr` as written. A planet: its own abilities,
// then, for the viewer's own colony, the owner's racial trait descriptions,
// "Racial Trait:" and "Cultural Trait:" lines that concern planets and the
// population-level and mood lines; never its facilities. A ship or base: its
// hull's entries, then the owner's racial and culture lines that concern
// ships; never its components, mothballed or not. Without `racial` (the
// Combat Simulator's reports) the racial and culture lines are left out.
std::vector<std::string> planetAbilityLines(const game::Rules& r, const game::GameState& s, game::ObjectId planet, game::EmpireId viewer,
                                            bool racial = true);
std::vector<std::string> vehicleAbilityLines(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, bool racial = true);

} // namespace opense4::client::classic
