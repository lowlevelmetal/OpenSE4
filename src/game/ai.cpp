#include "game/ai.hpp"

#include "game/turn.hpp"

namespace opense4::game::ai {

// Stub: implemented by the AI work package (docs/spec/05 §7).

std::vector<Command> planTurn(const Rules&, const GameState&, EmpireId, bool) { return {}; }
std::vector<Command> ministerCommands(const Rules&, const GameState&, EmpireId) { return {}; }
void updateAnger(TurnContext&) {}

} // namespace opense4::game::ai
