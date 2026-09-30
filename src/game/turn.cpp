#include "game/turn.hpp"

#include "game/ai.hpp"
#include "game/combat.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/events.hpp"
#include "game/intel.hpp"
#include "game/movement.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <format>

namespace opense4::game {

namespace {

// Log entries are kept for this many turns (the log window shows recent turns).
constexpr uint32_t kLogTurnsKept = 20;

} // namespace

void applyOrders(const Rules& r, GameState& s, const EmpireOrders& orders, std::vector<std::pair<EmpireId, std::string>>& rejected) {
    for (const Command& c : orders.commands) {
        const CommandResult res = apply(r, s, orders.empire, c);
        if (!res.ok) rejected.emplace_back(orders.empire, std::format("{}: {}", commandName(c), res.error));
    }
}

TurnResult processTurn(const Rules& r, GameState& s, std::span<const EmpireOrders> orders, const TurnOptions& options) {
    TurnContext ctx{r, s, {}, {}, {}};
    if (s.gameOver) return {};
    s.combats.clear();
    for (Empire& e : s.empires) std::erase_if(e.log, [&](const LogEntry& l) { return l.turn + kLogTurnsKept < s.turn; });

    // ---- 1. Orders: players in empire order, then computers, then ministers.
    std::vector<const EmpireOrders*> byEmpire(s.empires.size(), nullptr);
    for (const EmpireOrders& o : orders) {
        if (!o.empire.valid() || o.empire.index() >= s.empires.size()) continue;
        if (o.turn != s.turn) {
            ctx.rejected.emplace_back(o.empire, std::format("Orders for turn {} ignored on turn {}", o.turn, s.turn));
            continue;
        }
        byEmpire[o.empire.index()] = &o;
    }
    for (size_t i = 0; i < s.empires.size(); ++i) {
        Empire& e = s.empires[i];
        if (!e.alive) continue;
        if (byEmpire[i] && e.kind == PlayerKind::Human) {
            applyOrders(r, s, *byEmpire[i], ctx.rejected);
        } else if (e.kind != PlayerKind::Human || options.aiForMissing) {
            const bool minimal = e.kind == PlayerKind::Human && e.aiMinimalChanges;
            EmpireOrders ai{e.id, s.turn, game::ai::planTurn(r, s, e.id, minimal)};
            applyOrders(r, s, ai, ctx.rejected);
        }
    }
    for (Empire& e : s.empires) {
        if (!e.alive || e.kind != PlayerKind::Human) continue;
        EmpireOrders m{e.id, s.turn, game::ai::ministerCommands(r, s, e.id)};
        applyOrders(r, s, m, ctx.rejected);
    }

    // ---- 2. Diplomacy: messages sent last turn arrive.
    diplomacy::deliverMessages(ctx);

    // ---- 3. Movement and space combat.
    movement::startTurn(ctx);
    movement::runMovementAndCombat(ctx);
    s.removeDeadVehicles();

    // ---- 4. Ground combat and capture, then colonization.
    combat::runGroundCombat(ctx);
    movement::runColonization(ctx);

    // ---- 5-8. Economy, research, intelligence, population.
    economy::runEconomy(ctx);
    research::runResearch(ctx);
    intel::runIntel(ctx);
    economy::runPopulation(ctx);

    // ---- 9-10. Events, then supply and repair.
    events::runEvents(ctx);
    movement::runUpkeep(ctx);
    s.removeDeadVehicles();

    // ---- 11-12. Contact and trade, AI anger.
    sight::updateKnowledge(r, s);
    diplomacy::updateContacts(ctx);
    diplomacy::advanceTrade(ctx);
    game::ai::updateAnger(ctx);

    // ---- 13. End of turn.
    for (Empire& e : s.empires)
        for (Relation& rel : e.relations) rel.messageSentThisTurn = false;
    score::endOfTurn(ctx);
    ++s.turn;
    economy::updateReports(r, s);

    return TurnResult{std::move(ctx.rejected)};
}

} // namespace opense4::game
