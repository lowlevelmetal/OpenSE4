#pragma once

// Research (docs/spec/05 §1).

#include "game/rules.hpp"
#include "game/state.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::research {

inline constexpr size_t kMaxProjects = 12;

// RP to reach `level` in an area under this game's cost growth.
int64_t levelCost(const Rules& r, const GameState& s, ruleset::TechAreaId area, int level);
// Areas the empire may add to its queue now (visible and below the maximum).
std::vector<ruleset::TechAreaId> researchable(const Rules& r, const GameState& s, const Empire& e);
// The level each queue entry works toward. Repeated entries for one area
// are successive levels (inferred).
std::vector<int> targetLevels(const Empire& e);
// Turns to finish the queue entry at the current allocation (-1 = never),
// simulating the queue with this turn's research points.
int etaTurns(const Rules& r, const GameState& s, const Empire& e, size_t queueIndex);

// Splits `points` over projects that still need `need[i]` (spec 05 §1.4):
// evenly gives floor(points / n) to each, in order funds the list top down.
// Nobody receives more than it needs; the rest is lost (no carry-over).
std::vector<int64_t> allocate(int64_t points, std::span<const int64_t> need, bool evenly);

// Raises an area to `newLevel` (research, gifts, trades, theft, ruins,
// subjugation), logs "New Tech Level" and "<item> Discovered" for everything
// that became available, and passes the level on to the empire's master.
// `source` is a short word for the log ("research", "trade", ...).
void grantLevel(TurnContext& ctx, EmpireId e, ruleset::TechAreaId area, int newLevel, std::string_view source);
// `count` advances of one level each in random researchable areas (ruins).
void grantRandomAdvances(TurnContext& ctx, EmpireId e, int count, Rng& rng, std::string_view source);
// Names of items (components, facilities, hulls, intel projects) that
// require exactly this level of the area.
std::vector<std::string> unlockedBy(const Rules& r, ruleset::TechAreaId area, int level);
// Names of every item available to the empire (same item kinds, data order).
std::vector<std::string> availableItems(const Rules& r, const Empire& e);

// Total levels owned, and the tech-percent victory measure (levels ÷ Σ max
// levels over areas allowed in this game, racial and unique areas excluded).
int totalLevels(const Empire& e);
int techPercent(const Rules& r, const GameState& s, const Empire& e);

// Turn phase 6: spends Empire::economy.research on the queue.
void runResearch(TurnContext& ctx);

} // namespace opense4::game::research
