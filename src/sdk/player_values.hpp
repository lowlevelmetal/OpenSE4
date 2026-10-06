#pragma once

// Internal to the SDK's script players (sdk/players.cpp): the values the
// protocol carries that are not the view's or the commands' own
// (docs/sdk/ai-protocol.md), and the digests and costs the session needs.

#include "core/hash.hpp"
#include "game/players.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"
#include "script/value.hpp"

#include <cstddef>
#include <string>

namespace opense4::sdk::detail {

// Feeds a value to a hash, the same on every computer: kinds, numbers,
// texts and the order of lists and maps.
void hashValue(Hasher& h, const script::Value& v);
// How many values a tree holds: what the services' costs count.
size_t valueNodes(const script::Value& v);

// The battle as a side sees it at the start of its phase (§5).
script::Value battleValue(const game::Rules& r, const game::GameState& s, const game::BattleRound& b);

// What changed from one view to the next (the `apply` service, §6): for
// each list of records with ids, the records added or changed ("changed")
// and the ids gone ("removed"); any other part that differs, whole.
script::Value viewChanges(const script::Value& before, const script::Value& after);

// What is wrong with the shape of a player's response (§4); empty when
// nothing. `planning`: a planning call, whose responses may hold commands.
std::string responseProblem(const script::Value& response, bool planning);

} // namespace opense4::sdk::detail
