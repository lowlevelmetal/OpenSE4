#pragma once

// Economy (docs/spec/02): planet output, trade income, storage, maintenance,
// construction queues, population growth, mood, riots and plague.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::economy {

// What a colony produces this turn, before delivery (spec 02 §5).
struct ColonyOutput {
    Resources production;
    int64_t research = 0;
    int64_t intelligence = 0;
    int64_t supply = 0;              // resupply depot output (0 = none)
    bool connected = true;           // spaceport rule satisfied (spec 02 §5.5)
    bool blockaded = false;
    int reproductionPercent = 0;     // per year
    Mood mood = Mood::Indifferent;
};
ColonyOutput colonyOutput(const Rules& r, const GameState& s, const Colony& c);

// Per-turn construction rate of a queue (spec 02 §6.2).
Resources constructionRate(const Rules& r, const GameState& s, EmpireId e, const cmd::QueueTarget& t);
// Full cost of a queue item (vehicle design cost × count, facility, upgrade).
Resources itemCost(const Rules& r, const GameState& s, EmpireId e, const cmd::QueueTarget& t, const QueueItem& item);
// Turns to finish `remaining` at `rate` (max over resources, ceil); -1 = never.
int turnsToComplete(const Resources& remaining, const Resources& rate);

Resources storageCapacity(const Rules& r, const GameState& s, EmpireId e);
Resources maintenanceCost(const Rules& r, const GameState& s, EmpireId e);

// Recomputes Empire::economy (projected income/expenses) without changing
// anything else. Called after setup and at the end of each turn for the UI.
void updateReports(const Rules& r, GameState& s);

// Turn phase 5: production, trade, tariffs, maintenance, then queue spending.
void runEconomy(TurnContext& ctx);
// Turn phase 8: growth, mood (applies TurnContext::moodEvents), riots, rebellion, plague.
void runPopulation(TurnContext& ctx);

} // namespace opense4::game::economy
