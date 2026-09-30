#pragma once

// Research (docs/spec/05 §1).

#include "game/rules.hpp"
#include "game/state.hpp"

#include <string>
#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::research {

// RP to reach `level` in an area under this game's cost growth.
int64_t levelCost(const Rules& r, const GameState& s, ruleset::TechAreaId area, int level);
// Areas the empire may add to its queue now.
std::vector<ruleset::TechAreaId> researchable(const Rules& r, const GameState& s, const Empire& e);
// Turns to finish the queue entry at the current allocation (-1 = never).
int etaTurns(const Rules& r, const GameState& s, const Empire& e, size_t queueIndex);

// Raises an area (research, gifts, theft, ruins) and logs what became available.
void grantLevel(TurnContext& ctx, EmpireId e, ruleset::TechAreaId area, int newLevel, std::string_view source);
// Names of items (components, facilities, hulls, mounts) that a level unlocks.
std::vector<std::string> unlockedBy(const Rules& r, ruleset::TechAreaId area, int level);

// Turn phase 6: spends Empire::economy.research on the queue.
void runResearch(TurnContext& ctx);

} // namespace opense4::game::research
