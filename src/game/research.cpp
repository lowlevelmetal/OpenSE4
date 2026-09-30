#include "game/research.hpp"

#include "game/turn.hpp"

namespace opense4::game::research {

// Stub: implemented by the research/diplomacy work package (docs/spec/05 §1).

int64_t levelCost(const Rules& r, const GameState& s, ruleset::TechAreaId area, int level) {
    return r.techLevelCost(area, level, s.options.techCostGrowth);
}
std::vector<ruleset::TechAreaId> researchable(const Rules&, const GameState&, const Empire&) { return {}; }
int etaTurns(const Rules&, const GameState&, const Empire&, size_t) { return -1; }
void grantLevel(TurnContext& ctx, EmpireId e, ruleset::TechAreaId area, int newLevel, std::string_view) {
    Empire& emp = ctx.state.empire(e);
    if (emp.techLevels.size() <= area.index()) emp.techLevels.resize(area.index() + 1, 0);
    emp.techLevels[area.index()] = std::max(emp.techLevels[area.index()], newLevel);
}
std::vector<std::string> unlockedBy(const Rules&, ruleset::TechAreaId, int) { return {}; }
void runResearch(TurnContext&) {}

} // namespace opense4::game::research
