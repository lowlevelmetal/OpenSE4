#pragma once

// Research (docs/spec/05 §1).
//
// Points and the pool (spec 05 §1.1, §8): an empire's research step spends
// Empire::researchPool on its queue and empties it; the income step of the
// same end-of-turn processing then refills the pool with the turn's
// production. So the points shown during a turn were produced at the end of
// the previous one and are spent at the end of this one.
//
// Entry points for the spec 05 §8 turn order:
// - researchStep(ctx, e): step 4 of one empire's end-of-turn processing
//   (after intel::intelStep).
// - addToPools(e, rp, ip): the income step adds the turn's research and
//   intelligence income (after tariffs) to the pools.
// - openingPools(r, s): when the game is created.
// runResearch() is the aggregate phase turn.cpp calls until then.

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
// Every point pool (resources, research, intelligence) is capped at this
// when income is added (spec 05 §8, confirmed: binary).
inline constexpr int64_t kPoolCap = 2'000'000'000;
// Ancient ruins: draws of a random tech area per advance (spec 05 §1.2).
inline constexpr int kRuinsDraws = 1000;

// RP to reach `level` in an area under this game's Technology Cost option.
int64_t levelCost(const Rules& r, const GameState& s, ruleset::TechAreaId area, int level);
// Whether an area may gain a level at all: allowed in this game and passing
// the racial and unique checks (spec 05 §1.2: gaining a level from any
// source checks only these).
bool canGainLevel(const Rules& r, const GameState& s, const Empire& e, ruleset::TechAreaId area);
// Whether an area is researchable now: allowed, racial and unique checks,
// requirements met, below its maximum (spec 05 §1.2).
bool isResearchable(const Rules& r, const GameState& s, const Empire& e, ruleset::TechAreaId area);
// Areas the empire may add to its queue now, in data order.
std::vector<ruleset::TechAreaId> researchable(const Rules& r, const GameState& s, const Empire& e);
// Turns to finish the queue entry (-1 = never): the queue is simulated with
// the pool for the first turn and this turn's production after that.
int etaTurns(const Rules& r, const GameState& s, const Empire& e, size_t queueIndex);

// The shares of a pool over projects that still need `need[i]` (spec 05
// §1.4 step 2, also used by intelligence): evenly, every project gets
// round(pool / n), not capped by what it needs; in order, walking down the
// list, each takes the smaller of what it needs and what is left.
std::vector<int64_t> allocate(int64_t pool, std::span<const int64_t> need, bool evenly);

// ---- Pools --------------------------------------------------------------------------------------

// The research points this turn's research step will spend: the pool, or
// before the first turn has been processed, the opening pool (Starting
// Resources plus one turn of production, which the economy's first-turn
// income holds while the game setup leaves the pool empty).
int64_t availablePoints(const GameState& s, const Empire& e);
// Adds income to the research and intelligence pools, capped at kPoolCap.
void addToPools(Empire& e, int64_t research, int64_t intelligence);
// The pools at game creation (spec 05 §1.1): research at Starting Resources
// plus one turn of production (Empire::economy.research after the setup's
// economy report), intelligence at 0. For createGame once the spec 05 §8
// order lands; the first research step does the same meanwhile.
void openingPools(const Rules& r, GameState& s);

// ---- Levels ---------------------------------------------------------------------------------------

// Raises an area to `newLevel` (research, gifts, trades, theft, ruins,
// surrender) when canGainLevel allows it, and logs "New Tech Level" and
// "<item> Discovered" for everything that became available. Technology never
// passes to a master (spec 05 §1.5). `source` is a short word for the log.
void grantLevel(TurnContext& ctx, EmpireId e, ruleset::TechAreaId area, int newLevel, std::string_view source);
// Ancient ruins (spec 05 §1.2): `count` advances; for each, up to kRuinsDraws
// uniform draws among all tech areas until one is researchable, which gains
// one level.
void grantRandomAdvances(TurnContext& ctx, EmpireId e, int count, Rng& rng, std::string_view source);
// Names of items (components, facilities, hulls, intel projects) that
// require exactly this level of the area.
std::vector<std::string> unlockedBy(const Rules& r, ruleset::TechAreaId area, int level);
// Names of every item available to the empire (same item kinds, data order).
std::vector<std::string> availableItems(const Rules& r, const Empire& e);

// ---- Totals (scores and victory, spec 05 §5–§6) ---------------------------------------------------

// Sum of the levels of all areas, each capped at its area's maximum.
int totalLevels(const Rules& r, const Empire& e);
// Sum of the maximum levels of the areas allowed in this game that pass the
// empire's racial and unique checks.
int maxLevels(const Rules& r, const GameState& s, const Empire& e);
// totalLevels as a whole percentage of maxLevels (rounded down; display).
int techPercent(const Rules& r, const GameState& s, const Empire& e);
// The empire has researched everything: totalLevels >= maxLevels.
bool researchedEverything(const Rules& r, const GameState& s, const Empire& e);

// ---- Turn steps -------------------------------------------------------------------------------------

// One empire's research step (spec 05 §1.4, confirmed: binary): projects of
// areas at their maximum leave the queue (and later entries of an area that
// is already queued, inferred); every share is computed from the pool; each
// project adds its share and completes when its progress reaches the cost of
// the next level (one level at most, the excess lost); Repeat Projects
// re-queues a completed area below its maximum at the end; the pool is
// emptied.
void researchStep(TurnContext& ctx, EmpireId e);

// Aggregate phase for turn.cpp (after the economy): for each living empire in
// order, the research step, then this turn's research income (the economy's
// Empire::economy.research less the tariff to a master) goes into the pool.
void runResearch(TurnContext& ctx);

} // namespace opense4::game::research
