#include "game/turn.hpp"

#include "game/ai.hpp"
#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/events.hpp"
#include "game/intel.hpp"
#include "game/movement.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "game/sight.hpp"
#include "game/turn_internal.hpp"

#include <algorithm>
#include <format>
#include <optional>

namespace opense4::game {

namespace detail {

bool ministersPlan(const GameState& s, EmpireId e, Control c) {
    switch (c) {
        case Control::Computer: return true;
        case Control::Absent: return false;
        case Control::Player:
        case Control::StandIn: return ai::ministersActive(s, e);
    }
    return false;
}

void applyCommands(TurnContext& ctx, EmpireId e, std::vector<Command> commands) {
    if (commands.empty()) return;
    applyOrders(ctx.rules, ctx.state, EmpireOrders{e, ctx.state.turn, std::move(commands)}, ctx.rejected);
}

bool living(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size() && s.empire(e).alive; }

void resetCameFrom(const Rules& r, GameState& s, EmpireId e) {
    for (Vehicle& v : s.vehicles) {
        if (v.owner != e || v.count <= 0) continue;
        const ruleset::VehicleType type = vehicleType(r, s, v);
        if (type == ruleset::VehicleType::Ship || type == ruleset::VehicleType::Base || type == ruleset::VehicleType::Fighter ||
            type == ruleset::VehicleType::Drone)
            v.cameFrom = v.location;
    }
}

} // namespace detail

using detail::applyCommands;
using detail::Control;
using detail::living;
using detail::ministersPlan;
using detail::resetCameFrom;

void applyOrders(const Rules& r, GameState& s, const EmpireOrders& orders, std::vector<std::pair<EmpireId, std::string>>& rejected) {
    for (const Command& c : orders.commands) {
        const CommandResult res = apply(r, s, orders.empire, c);
        if (!res.ok) rejected.emplace_back(orders.empire, std::format("{}: {}", commandName(c), res.error));
    }
}

void empireEndOfTurn(TurnContext& ctx, EmpireId e, bool ministers) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!living(s, e)) return;

    // 1. The ministers' end-of-turn actions (spec 05 §7.1 group 2): Design,
    // Research, Intelligence and the construction ministers. The queues need
    // no refresh of their own: their rates are worked out when they run
    // (inferred).
    // The units reserve the vehicle list applies is what the nearest empire
    // before it left; in a turn-based game its own start-of-turn step has
    // just reset it to 0 (spec 05 §7.5 "Units file").
    if (ministers) {
        applyCommands(ctx, e, ai::planEconomyStep(r, s, e, s.options.simultaneous ? ctx.unitReserve : 0));
        if (living(s, e) && ai::ministerOn(s.empire(e), Minister::ShipConstruction)) ctx.unitReserve = ai::unitReserveLeft(r, s.empire(e));
    }
    // 2. The statistics row of the Scores and Comparisons windows (spec 05 §5;
    // OpenSE4 keeps every empire's in the save), and for a human player the
    // lines of its statistics, history and log text files (TurnResult::records).
    score::recordStatistics(ctx, e);
    // 3-4. Intelligence, then research: each spends the pool the previous
    // turn's income and trade filled, then empties it (spec 05 §1.1, §2.1).
    intel::intelStep(ctx, e);
    research::researchStep(ctx, e);
    // 5. Income: production, tariffs to a master (paid to it at once), the
    // computer bonus; research and intelligence go into the pools.
    economy::collectIncome(ctx, e);
    // 6. Treaties and trade: consistency, a master's view of designs, trade
    // income, Partnership maps and designs, the trade counters.
    diplomacy::treatyStep(ctx, e);
    // 7. Maintenance.
    economy::payMaintenance(ctx, e);
    // 8. Planets: growth, planet changes, plague.
    economy::processPlanets(ctx, e);
    // 9. Happiness, from the mood events raised since the last update.
    economy::updateHappiness(ctx, e);
    // 10. Construction.
    economy::runConstruction(ctx, e);
    // 11. Repair.
    movement::repairEmpire(ctx, e);
    // 12. Foreign designs last seen more than 50 turns ago are forgotten.
    sight::forgetOldDesigns(s, e);
    // 13. Supply, with the per-turn upkeep of unit groups and cloaks (the
    // per-object upkeep of step 16 is part of it, inferred).
    movement::supplyEmpire(ctx, e);
    // 14. Storage cap.
    economy::applyStorageCap(ctx, e);
    // 15. System-wide abilities and training.
    economy::applySystemAbilities(ctx, e);
    movement::trainEmpire(ctx, e);
    // 16. Each ship, base, fighter group and drone group records its current
    // sector as the one it comes from: in a turn-based game a vehicle that
    // moved in its owner's turn counts as an arrival only until now (spec 05
    // §8 step 16, spec 04 §3, confirmed: binary; which vehicles, open question 46).
    resetCameFrom(ctx.rules, s, e);
    // 17. Ground combat on the empire's colonies where landed troops still
    // fight (spec 04 §13: the colony owner's step).
    combat::runGroundCombat(ctx, e);
    s.removeDeadVehicles();
    // 18. The log keeps only this turn's entries (spec 05 §3.4).
    if (living(s, e)) std::erase_if(s.empire(e).log, [&](const LogEntry& l) { return l.turn < s.turn; });
}

TurnResult processTurn(const Rules& r, GameState& s, std::span<const EmpireOrders> orders, const TurnOptions& options) {
    if (s.gameOver) return {};
    if (!s.options.simultaneous) return detail::playTurnBasedTurn(r, s, orders, options);
    TurnContext ctx{r, s, {}, {}, {}};
    // Mood events raised after an empire's happiness update last turn (spec 02 §4).
    ctx.moodEvents = std::move(s.pendingMood);
    s.pendingMood.clear();

    // ---- 1. Orders, in player order. A human whose orders are missing is
    // played by the computer for this turn: every minister is switched on and
    // restored afterwards; a player who forbade AI changes only gets the
    // bookkeeping (spec 05 §7.1, §9.2).
    std::vector<const EmpireOrders*> byEmpire(s.empires.size(), nullptr);
    for (const EmpireOrders& o : orders) {
        if (!o.empire.valid() || o.empire.index() >= s.empires.size()) continue;
        if (o.turn != s.turn) {
            ctx.rejected.emplace_back(o.empire, std::format("Orders for turn {} ignored on turn {}", o.turn, s.turn));
            continue;
        }
        byEmpire[o.empire.index()] = &o;
    }
    std::vector<Control> control(s.empires.size(), Control::Computer);
    std::vector<std::pair<EmpireId, ai::MinisterSettings>> standIns;  // their own minister settings, restored at the end
    for (size_t i = 0; i < s.empires.size(); ++i) {
        Empire& e = s.empires[i];
        if (!e.alive || e.kind != PlayerKind::Human) continue;
        if (byEmpire[i]) {
            control[i] = Control::Player;
            applyOrders(r, s, *byEmpire[i], ctx.rejected);
        } else if (!options.aiForMissing) {
            control[i] = Control::Player;
        } else if (e.aiMinimalChanges) {
            control[i] = Control::Absent;
        } else {
            // Planned once below (planOrders, planEconomyStep), and its
            // political step runs with the Politics minister on (spec 05 §7.1).
            control[i] = Control::StandIn;
            standIns.emplace_back(e.id, ai::standIn(e));
        }
    }
    auto controlOf = [&](size_t i) { return i < control.size() ? control[i] : Control::Computer; };

    // ---- 2. Each player's messages, player by player.
    diplomacy::deliverMessages(ctx);

    // ---- 3. The date advances. GameState::turn stays the number the orders
    // were given for until the end of the turn (log entries and records carry
    // it); the steps below that depend on the date get `date`.
    const uint32_t date = s.turn + 1;

    // ---- 4. Start of turn, empire by empire: the AI state update, the
    // political step (counting the turn processed before: its battles are
    // still in GameState::combats), then the ministers that act while orders
    // are given. The Politics minister acts first and its messages take
    // effect as they are sent, so the other ministers already see the
    // treaties it changed (spec 05 §8 step 4, confirmed: binary).
    const std::optional<uint32_t> previousTurn = s.turn > 0 ? std::optional<uint32_t>(s.turn - 1) : std::nullopt;
    for (size_t i = 0; i < s.empires.size(); ++i) {
        const EmpireId id{i};
        if (!s.empire(id).alive) continue;
        ai::updateAiState(ctx, id);
        if (controlOf(i) != Control::Absent) ai::politicalStep(ctx, id, previousTurn);
        // Messages sent now carry the advanced date (spec 05 §7.4 "Answer window").
        if (ministersPlan(s, id, controlOf(i))) {
            applyCommands(ctx, id, ai::planPoliticsOrders(r, s, id));
            diplomacy::deliverMessages(ctx, date);
            applyCommands(ctx, id, ai::planOrdersAfterPolitics(r, s, id));
            diplomacy::deliverMessages(ctx, date);
        }
    }
    ai::recordAiDecisions(ctx);

    // ---- 5. Movement and space combat: 30 movement phases, each followed by
    // combat where it applies. Colonize orders found their colonies during the
    // phases, like any order (spec 05 §8 step 5). Sight and first contact
    // then follow the new positions.
    s.combats.clear();  // from here on: this turn's battles
    movement::startTurn(ctx);
    movement::runMovementAndCombat(ctx);
    s.removeDeadVehicles();
    sight::updateKnowledge(r, s);
    diplomacy::updateContacts(ctx);

    // ---- 6. End-of-turn processing, one empire at a time in empire order,
    // each followed by its destruction check. An empire founded during it
    // (a rebel colony) starts its own processing next turn (inferred).
    const size_t processed = s.empires.size();
    for (size_t i = 0; i < processed; ++i) {
        const EmpireId id{i};
        if (!s.empire(id).alive) continue;
        empireEndOfTurn(ctx, id, ministersPlan(s, id, controlOf(i)));
        score::checkDestruction(ctx, id);
    }

    // ---- 7. Design cleanup when a new year starts; then, every turn, the
    // contact check: empires that no warp path links any more lose contact
    // (spec 05 §3.1, confirmed: binary).
    if (date % 10 == 0) movement::purgeObsoleteDesigns(ctx);
    diplomacy::checkContacts(ctx);

    // ---- 8. Victory check.
    score::checkVictory(ctx, date);

    // ---- 9. Event step: hazard damage, the timed events that are due, then
    // one roll for a new event for the whole galaxy.
    movement::runStellarHazards(ctx);
    {
        Rng rng = s.rng.fork();
        events::fireDueEvents(ctx, rng);
        events::rollNewEvent(ctx, date, rng);
    }
    s.removeDeadVehicles();

    // ---- 10. Per-turn flags are cleared; sight and contact follow the
    // events; the AI remembers the turn's battles and spies; stand-ins get
    // their own ministers back; mood events still waiting carry over.
    for (Empire& e : s.empires)
        for (Relation& rel : e.relations) rel.messageSentThisTurn = false;
    sight::updateKnowledge(r, s);
    diplomacy::updateContacts(ctx);
    ai::rememberAiEvents(ctx);
    for (const auto& [id, saved] : standIns) ai::restoreMinisters(s.empire(id), saved);
    std::erase_if(ctx.moodEvents, [&](const MoodEvent& m) { return !living(s, m.empire); });
    s.pendingMood = std::move(ctx.moodEvents);
    ++s.turn;
    economy::updateReports(r, s);

    return TurnResult{std::move(ctx.rejected), {}, {}, std::move(ctx.records)};
}

} // namespace opense4::game
