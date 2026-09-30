#pragma once

// Contact, treaties, messages, trade and tariffs (docs/spec/05 §3).

#include "game/rules.hpp"
#include "game/state.hpp"

#include <span>
#include <string_view>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::diplomacy {

// Messages are kept this many turns after they were sent, so replies can
// refer to them and the UI can show recent history (inferred).
inline constexpr uint32_t kMessageLifetime = 10;

// ---- Turn phases -------------------------------------------------------------------------------

// Delivers every message not yet delivered and applies replies: treaty
// accepts, trades, gifts and tributes, plus the immediate messages (break
// treaty, declare war, surrender, grant independence). Expired messages are
// removed. The turn calls it for the players' messages (spec 05 §8 step 2)
// and after each computer player's and minister's orders (step 4: their
// messages take effect as they are sent).
void deliverMessages(TurnContext& ctx);
// First contact between every pair of living empires that have not met and
// that each detect the other in one system (spec 05 §3.1, confirmed: binary),
// after sight::updateKnowledge. Contact is never lost; only the destruction
// of an empire (forgetEmpire) ends it.
void updateContacts(TurnContext& ctx);
// One empire's treaty step (spec 05 §8 end-of-turn step 6, §3.2–§3.3), in
// this order: the consistency check (two sides that record different
// treaties both fall to None), a master's view of its subject's designs, the
// trade income from every partner (economy::collectTrade), Partnership maps
// and designs, and the trade counters (+1 toward every other living empire).
void treatyStep(TurnContext& ctx, EmpireId e);

// ---- Treaties and contact ----------------------------------------------------------------------

// Sets a treaty on both sides: dominance for Subjugation/Protectorate
// (`aDominant`: a is the master), treaty turn, last war turn, trade reset
// below trade level, "New Treaty ..." mood events and logs. A subject's
// other treaties are broken.
void setTreaty(TurnContext& ctx, EmpireId a, EmpireId b, Treaty t, bool aDominant = false);
// Happiness.txt trigger for a new treaty, from `forEmpire`'s side.
std::string_view treatyTrigger(Treaty t, bool dominant);
bool inContact(const GameState& s, EmpireId a, EmpireId b);
// Establishes contact both ways; logs first contact.
void makeContact(TurnContext& ctx, EmpireId a, EmpireId b);
// Declare War (spec 05 §3.4): both sides are at War whatever the treaty was,
// and both are told. Also used by `Politics - Fake Messages` (§2.3).
void declareWar(TurnContext& ctx, EmpireId from, EmpireId to);
// A destroyed empire (spec 05 §6): every relation with it returns to "no
// contact" (no contact, treaty None, trade counter 0) on both sides.
void forgetEmpire(GameState& s, EmpireId gone);
// The empire that `e` pays tariffs to (Subjugation/Protectorate), if any.
EmpireId masterOf(const GameState& s, EmpireId e);
// Whether `viewer` may see the treaty between `a` and `b` in the treaty grid:
// its own treaties, and those of empires it is allied with (spec 05 §3.2).
bool treatyVisible(const GameState& s, EmpireId viewer, EmpireId a, EmpireId b);

// ---- Ownership transfers (packages, surrender, defection, rebellion) ---------------------------

void transferColony(GameState& s, ObjectId planet, EmpireId to);
void transferVehicle(GameState& s, VehicleId vehicle, EmpireId to);
// Moves package items from `giver` to `receiver`. Items that no longer exist
// are skipped and reported in the logs of both sides.
void executePackage(TurnContext& ctx, EmpireId giver, EmpireId receiver, std::span<const PackageItem> items);
// True if an item is an unfilled "Any" placeholder (no specific tech, planet, ...).
bool isPlaceholder(const PackageItem& item);
// The whole empire of `from` passes to `to`; `from` is eliminated.
void surrender(TurnContext& ctx, EmpireId from, EmpireId to);

// ---- Trade and tariffs (the economy's income and trade steps use these) ------------------------

// An empire's production of the five kinds this turn: the base of trade,
// tariffs and the score (spec 05 §3.3, §5). What its colonies deliver to the
// treasury (economy::empireProduction, spec 02 §5.5), without remote mining,
// Generate Points or the income floor (inferred, spec 05 open question 15).
struct Generated {
    Resources resources;
    int64_t research = 0;
    int64_t intelligence = 0;
};
Generated generated(const Rules& r, const GameState& s, EmpireId e);
// The trade percentage between two empires (spec 05 §3.3): with Trade
// Alliance or better, min(trade counter, `Maximum Trade Percentage`), else 0.
int tradePercent(const Rules& r, const GameState& s, EmpireId e, EmpireId partner);
// The receiver's trade factor F = 100 + (Political Savvy − 100) + its race's
// `Trade` trait values + its culture's Trade value (spec 05 §3.3).
int64_t tradeFactor(const Rules& r, const Empire& receiver);
// One partner's contribution (confirmed: binary):
// trunc(round(base × tradePercent / 100) × F / 100), never below 0.
int64_t tradeShare(int64_t partnerBase, int tradePercent, int64_t factor);

// Resources `e` receives this turn from resource trade plus the tariffs of
// its subjects and protectorates.
Resources tradeIncome(const Rules& r, const GameState& s, EmpireId e);
// The tariff part of tradeIncome (EconomyReport::tariffsIn).
Resources tariffsReceived(const Rules& r, const GameState& s, EmpireId e);
// Research points from Trade & Research Alliances and better.
int64_t researchTradeIncome(const Rules& r, const GameState& s, EmpireId e);
// Intelligence points from Partnerships (the economy adds them to its IP).
int64_t intelTradeIncome(const Rules& r, const GameState& s, EmpireId e);
// What `e` owes its master this turn on each of its five incomes (spec 05
// §3.3, confirmed: binary): round(income × pct / 100), never more than the
// income, with pct the treaty's `Treaty Subjugated/Protectorate Resource
// Percentage`. The master receives the resources; the research and
// intelligence parts are simply lost. The economy's income step takes it,
// once, before the computer bonus (economy::collectIncome).
Generated tariffDue(const Rules& r, const GameState& s, EmpireId e);
// The resource part of tariffDue; the economy caps it at what the payer holds.
Resources tariffsPaid(const Rules& r, const GameState& s, EmpireId e);

} // namespace opense4::game::diplomacy
