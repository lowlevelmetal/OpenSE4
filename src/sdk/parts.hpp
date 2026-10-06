#pragma once

// Internal to the SDK: the pieces the view and the queries share (abilities,
// design figures, queue and research estimates). Each is worked out with the
// engine's own functions; none changes the game.

#include "game/design.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"
#include "sdk/value_io.hpp"

#include <span>
#include <vector>

namespace opense4::sdk::detail {

// ---- Abilities ----------------------------------------------------------------------------------------

// A value as the data file writes it: a whole number, or the text itself
// when it is not one (a few abilities name a sight type).
Value abilityNumberOrText(const std::string& raw);
// One "Ability N" entry: {name, value1, value2, description}.
Value abilityEntry(const ruleset::Ability& a);
Value abilityEntries(std::span<const ruleset::Ability> list);
// The value of every ability type in the list, each read in its own mode
// (game::aggregationOf; an ability a mod declares as it declares: sum,
// largest or smallest), in order of first appearance: {name, aggregation,
// value, per_sight_type}.
Value abilityTotals(const game::Rules& r, std::span<const game::ParsedAbility> list);
// An ability type's name: the data's identifier ("Supply Storage"); AI tags
// and abilities the engine does not know keep their own text.
std::string_view abilityName(const game::ParsedAbility& a);

// ---- Designs ---------------------------------------------------------------------------------------------

// The designer's figures for a hull and component list (computeDesignStats
// and the helpers of design.hpp). `design`: an existing design, for its
// maintenance (null: none given).
Value designFigures(const game::Rules& r, const game::GameState& s, const game::DesignStats& st, uint32_t hull,
                    std::span<const game::DesignEntry> entries, const game::Design* maintenanceOf);

// ---- Construction queues -----------------------------------------------------------------------------------

// What each item of a queue costs and when it is done at the queue's rate
// now: an item takes ceil(what is left of each resource / its rate) turns,
// at least one, once it reaches the top; `doneIn` counts the items ahead.
// -1: never at this rate (a resource it needs is not produced, or the queue
// is on hold). The treasury is assumed to pay.
struct QueueEstimate {
    game::Resources cost;
    game::Resources remaining;
    int turns = -1;
    int doneIn = -1;
};
std::vector<QueueEstimate> estimateQueue(const game::Rules& r, const game::GameState& s, game::EmpireId owner,
                                         const game::cmd::QueueTarget& t, const game::ConstructionQueue& q, const game::Resources& rate);
// The queue with its rate and the items' estimates.
Value queueValue(const game::Rules& r, const game::GameState& s, game::EmpireId owner, const game::cmd::QueueTarget& t,
                 const game::ConstructionQueue& q);

// ---- Research ------------------------------------------------------------------------------------------------

// The levels between the empire's level in `area` and `level`, each with its
// cost and the turns it takes when the empire researches nothing else: the
// first draws on the points of this turn (Empire::researchPool) and the
// area's progress, the later ones on the research income of a turn; one
// level a turn at most, the excess lost (research::etaTurns). -1: never.
struct LevelEstimate {
    int level = 0;
    int64_t cost = 0;
    int turns = -1;
};
std::vector<LevelEstimate> estimateResearch(const game::Rules& r, const game::GameState& s, const game::Empire& e, ruleset::TechAreaId area,
                                            int level);

// ---- Reports -------------------------------------------------------------------------------------------------

Value economyValue(const game::EconomyReport& e);
Value statsValue(const game::TurnStats& t);
Value raceValue(const game::Race& race);

} // namespace opense4::sdk::detail
