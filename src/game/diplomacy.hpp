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

// Turn phase 2: delivers the messages sent this turn (they appear in the
// recipient's log next turn) and applies replies: treaty accepts, trades,
// gifts and tributes, plus the immediate messages (break treaty, declare war,
// surrender, grant independence). Expired messages are removed.
void deliverMessages(TurnContext& ctx);
// Turn phase 11: first contact where empires see each other's vehicles or
// colonies, contact loss when no warp path links their planets, and the
// knowledge Partnership and Subjugation share.
void updateContacts(TurnContext& ctx);
// Turn phase 11: trade percentage growth toward `Maximum Trade Percentage`.
void advanceTrade(TurnContext& ctx);

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

// ---- Trade and tariffs (the economy calls these in phase 5) -------------------------------------

// What an empire generates this turn and trade is based on: the output of
// its colonies that reaches the treasury (economy::colonyOutput) (inferred).
struct Generated {
    Resources resources;
    int64_t research = 0;
    int64_t intelligence = 0;
};
Generated generated(const Rules& r, const GameState& s, EmpireId e);
// One partner's contribution: base × tradePercent / 100, scaled by Political
// Savvy and the culture's Trade percentage (both additive, inferred).
int64_t tradeShare(int64_t partnerBase, int tradePercent, int politicalSavvy, int cultureTrade);

// Resources `e` receives this turn from resource trade plus the tariffs of
// its subjects and protectorates.
Resources tradeIncome(const Rules& r, const GameState& s, EmpireId e);
// The tariff part of tradeIncome (EconomyReport::tariffsIn).
Resources tariffsReceived(const Rules& r, const GameState& s, EmpireId e);
// Research points from Trade & Research Alliances and better.
int64_t researchTradeIncome(const Rules& r, const GameState& s, EmpireId e);
// Intelligence points from Partnerships (the economy adds them to its IP).
int64_t intelTradeIncome(const Rules& r, const GameState& s, EmpireId e);
// What `e` owes its master this turn (`Treaty Subjugated/Protectorate
// Resource Percentage` of its generated resources); the economy caps it at
// what the payer holds.
Resources tariffsPaid(const Rules& r, const GameState& s, EmpireId e);

} // namespace opense4::game::diplomacy
