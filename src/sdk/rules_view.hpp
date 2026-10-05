#pragma once

// The data set as scripts see it (docs/sdk/view.md, "The rules view"): the
// tables of the loaded rules (components, facilities, hulls, mounts, tech
// areas, races and their traits, planets and systems, abilities, formations,
// combat strategies, intelligence projects, design and colony types) as a
// script::Value tree. Each table is a list whose position is the record's
// index, the number the game's state and the view use for it.
//
// The rules never change during a game, so this is built once per engine
// call, not per turn. Pure.

#include "game/rules.hpp"
#include "script/value.hpp"

namespace opense4::sdk {

script::Value buildRulesView(const game::Rules& r);

} // namespace opense4::sdk
