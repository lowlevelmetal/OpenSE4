#include "game/score.hpp"

#include "game/turn.hpp"

namespace opense4::game::score {

// Stub: implemented by the research/diplomacy work package (docs/spec/05 §5-6).

int64_t empireScore(const Rules&, const GameState&, EmpireId) { return 0; }
TurnStats currentStats(const Rules&, const GameState& s, EmpireId) {
    TurnStats t;
    t.turn = s.turn;
    return t;
}
void endOfTurn(TurnContext&) {}

} // namespace opense4::game::score
