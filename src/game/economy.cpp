#include "game/economy.hpp"

#include "game/turn.hpp"

namespace opense4::game::economy {

// Stub: implemented by the economy work package (docs/spec/02).

ColonyOutput colonyOutput(const Rules&, const GameState&, const Colony&) { return {}; }
Resources constructionRate(const Rules&, const GameState&, EmpireId, const cmd::QueueTarget&) { return {}; }
Resources itemCost(const Rules&, const GameState&, EmpireId, const cmd::QueueTarget&, const QueueItem&) { return {}; }

int turnsToComplete(const Resources& remaining, const Resources& rate) {
    int turns = 0;
    for (size_t i = 0; i < 3; ++i) {
        if (remaining.v[i] <= 0) continue;
        if (rate.v[i] <= 0) return -1;
        turns = std::max(turns, static_cast<int>((remaining.v[i] + rate.v[i] - 1) / rate.v[i]));
    }
    return turns;
}

Resources storageCapacity(const Rules&, const GameState&, EmpireId) { return {}; }
Resources maintenanceCost(const Rules&, const GameState&, EmpireId) { return {}; }
void updateReports(const Rules&, GameState&) {}
void runEconomy(TurnContext&) {}
void runPopulation(TurnContext&) {}

} // namespace opense4::game::economy
