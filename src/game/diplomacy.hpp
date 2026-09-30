#pragma once

// Contact, treaties, messages, trade and tariffs (docs/spec/05 §3).

#include "game/rules.hpp"
#include "game/state.hpp"

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::diplomacy {

// Turn phase 2: delivers last turn's messages, applies accepted treaties,
// packages, war declarations and surrenders.
void deliverMessages(TurnContext& ctx);
// Turn phase 11: contact checks, then trade percentage growth.
void updateContacts(TurnContext& ctx);
void advanceTrade(TurnContext& ctx);

// Sets a treaty on both sides (mood events, logs).
void setTreaty(TurnContext& ctx, EmpireId a, EmpireId b, Treaty t, bool aDominant = false);
// Resources `e` receives from trade and tariffs this turn (economy calls this).
Resources tradeIncome(const Rules& r, const GameState& s, EmpireId e);
int64_t researchTradeIncome(const Rules& r, const GameState& s, EmpireId e);
Resources tariffsPaid(const Rules& r, const GameState& s, EmpireId e);

} // namespace opense4::game::diplomacy
