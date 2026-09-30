#include "game/intel.hpp"

#include "game/turn.hpp"

namespace opense4::game::intel {

// Stub: implemented by the research/diplomacy work package (docs/spec/05 §2).

void runIntel(TurnContext&) {}
int64_t defensePoints(const Rules&, const GameState&, EmpireId) { return 0; }

} // namespace opense4::game::intel
