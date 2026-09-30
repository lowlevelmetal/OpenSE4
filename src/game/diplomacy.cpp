#include "game/diplomacy.hpp"

#include "game/turn.hpp"

namespace opense4::game::diplomacy {

// Stub: implemented by the research/diplomacy work package (docs/spec/05 §3).

void deliverMessages(TurnContext&) {}
void updateContacts(TurnContext&) {}
void advanceTrade(TurnContext&) {}
void setTreaty(TurnContext& ctx, EmpireId a, EmpireId b, Treaty t, bool aDominant) {
    Relation& ra = ctx.state.empire(a).relation(b);
    Relation& rb = ctx.state.empire(b).relation(a);
    ra.treaty = rb.treaty = t;
    ra.dominant = aDominant;
    rb.dominant = false;
    ra.treatyTurn = rb.treatyTurn = ctx.state.turn;
}
Resources tradeIncome(const Rules&, const GameState&, EmpireId) { return {}; }
int64_t researchTradeIncome(const Rules&, const GameState&, EmpireId) { return 0; }
Resources tariffsPaid(const Rules&, const GameState&, EmpireId) { return {}; }

} // namespace opense4::game::diplomacy
