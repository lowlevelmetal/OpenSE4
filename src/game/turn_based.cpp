// The turn-based game (spec 05 §8 "Turn-based game", spec 03 §6.3
// "Turn-based", spec 04 §2): players take their turns one after another,
// their orders execute as they are given, and each player's end-of-turn
// processing runs when that player ends the turn. See turn.hpp.

#include "game/ai.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/events.hpp"
#include "game/movement.hpp"
#include "game/score.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"
#include "game/turn_internal.hpp"

#include <algorithm>
#include <format>
#include <optional>

namespace opense4::game {

using detail::Control;
using detail::living;
using detail::ministersPlan;

namespace {

// Pending mood events live in the state between calls (spec 02 §4).
class LiveContext {
public:
    LiveContext(const Rules& r, GameState& s) : ctx{r, s, {}, {}, {}} {
        ctx.moodEvents = std::move(s.pendingMood);
        s.pendingMood.clear();
    }
    ~LiveContext() { ctx.state.pendingMood = std::move(ctx.moodEvents); }
    LiveContext(const LiveContext&) = delete;
    LiveContext& operator=(const LiveContext&) = delete;

    TurnResult result() { return TurnResult{std::move(ctx.rejected), std::move(questions)}; }

    TurnContext ctx;
    std::vector<EntryQuestion> questions;
};

EmpireId firstLivingFrom(const GameState& s, size_t index) {
    for (size_t i = index; i < s.empires.size(); ++i)
        if (s.empires[i].alive) return EmpireId{i};
    return {};
}

bool anyLivingHuman(const GameState& s) {
    return std::any_of(s.empires.begin(), s.empires.end(), [](const Empire& e) { return e.alive && e.kind == PlayerKind::Human; });
}

// Interactive play: humans play their own turns, the computer the others.
Control interactiveControl(const GameState& s, EmpireId e) { return s.empire(e).kind == PlayerKind::Human ? Control::Player : Control::Computer; }

// ---- Carrying orders out ------------------------------------------------------------------------

void addQuestions(LiveContext& lc, const std::vector<EntryQuestion>& qs) {
    for (const EntryQuestion& q : qs)
        if (std::find(lc.questions.begin(), lc.questions.end(), q) == lc.questions.end()) lc.questions.push_back(q);
}

// The groups move and fight now; then colony ships at their planet found
// their colonies, and sight and first contact follow the new positions.
void carryOut(LiveContext& lc, const movement::LiveMove& move) {
    TurnContext& ctx = lc.ctx;
    GameState& s = ctx.state;
    addQuestions(lc, movement::runLive(ctx, move));
    movement::runColonization(ctx, move.empire);
    s.removeDeadVehicles();
    sight::updateKnowledge(ctx.rules, s);
    diplomacy::updateContacts(ctx);
}

// What a command sets in motion: the groups whose orders it set, and
// whether it sent a message.
struct Effects {
    movement::LiveMove move;
    bool messages = false;
    bool any() const { return !move.vehicles.empty() || !move.fleets.empty() || !move.planets.empty(); }
};

void noteEffects(Effects& fx, const Command& c) {
    if (const auto* o = std::get_if<cmd::SetOrders>(&c)) {
        if (o->planet.valid()) fx.move.planets.push_back(o->planet);
        else if (o->fleet.valid()) fx.move.fleets.push_back(o->fleet);
        else if (o->vehicle.valid()) fx.move.vehicles.push_back(o->vehicle);
    } else if (std::holds_alternative<cmd::SendMessage>(c) || std::holds_alternative<cmd::AnswerMessage>(c)) {
        fx.messages = true;
    }
}

void settle(LiveContext& lc, Effects& fx) {
    // Messages take effect the moment they are sent (spec 05 §3.4, confirmed: binary).
    if (fx.messages) diplomacy::deliverMessages(lc.ctx);
    if (fx.any()) carryOut(lc, fx.move);
}

// Commands given one after another, each carried out before the next.
void applyEach(LiveContext& lc, EmpireId e, std::span<const Command> commands, bool ask) {
    for (const Command& c : commands) {
        const CommandResult res = apply(lc.ctx.rules, lc.ctx.state, e, c);
        if (!res.ok) {
            lc.ctx.rejected.emplace_back(e, std::format("{}: {}", commandName(c), res.error));
            continue;
        }
        if (const auto* answer = std::get_if<cmd::EnterSector>(&c)) {
            if (!answer->enter) continue;
            Effects fx;
            fx.move.empire = e;
            fx.move.ask = ask;
            if (answer->fleet.valid()) fx.move.fleets.push_back(answer->fleet);
            else fx.move.vehicles.push_back(answer->vehicle);
            fx.move.allowed = EntryQuestion{answer->fleet.valid() ? VehicleId{} : answer->vehicle, answer->fleet, answer->where};
            settle(lc, fx);
            continue;
        }
        Effects fx;
        fx.move.empire = e;
        fx.move.ask = ask;
        noteEffects(fx, c);
        settle(lc, fx);
    }
}

// A computer player's (or a minister's) orders: given together, then
// carried out together (inferred: the planner hands them over at once).
void applyBatch(LiveContext& lc, EmpireId e, std::vector<Command> commands) {
    if (commands.empty()) return;
    Effects fx;
    fx.move.empire = e;
    for (const Command& c : commands) noteEffects(fx, c);
    detail::applyCommands(lc.ctx, e, std::move(commands));
    settle(lc, fx);
}

// ---- A player's turn ------------------------------------------------------------------------------

// The start of `e`'s turn. False when the empire is destroyed at its check.
bool startPlayerTurn(LiveContext& lc, EmpireId e, Control control) {
    TurnContext& ctx = lc.ctx;
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    s.playerTurn.empire = e;
    s.playerTurn.started = true;
    // The destruction check comes when the empire's turn comes up (spec 05 §6).
    score::checkDestruction(ctx, e);
    if (!living(s, e)) return false;
    // Movement is refilled and every group first carries on with its orders
    // (spec 03 §6.3); then the start-of-turn step (spec 05 §8) (inferred: in
    // this order).
    movement::startTurn(ctx, e);
    movement::LiveMove all;
    all.empire = e;
    all.ask = control == Control::Player;
    carryOut(lc, all);
    if (!living(s, e)) return true;  // it may have lost everything in battle; the check comes next turn
    ai::updateAiState(ctx, e);
    const std::optional<uint32_t> previousTurn = s.turn > 0 ? std::optional<uint32_t>(s.turn - 1) : std::nullopt;
    if (control != Control::Absent) ai::politicalStep(ctx, e, previousTurn);
    if (ministersPlan(s, e, control)) applyBatch(lc, e, ai::planOrders(r, s, e));
    ai::recordAiDecisions(ctx, e);
    return true;
}

// The once-per-game-turn steps after the last player (spec 05 §8 steps 7-10).
void endGameTurn(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    // The date advances; GameState::turn follows at the end, as in processTurn.
    const uint32_t date = s.turn + 1;
    if (date % 10 == 0) movement::purgeObsoleteDesigns(ctx);
    score::checkVictory(ctx, date);
    movement::runStellarHazards(ctx);
    {
        Rng rng = s.rng.fork();
        events::fireDueEvents(ctx, rng);
        events::rollNewEvent(ctx, date, rng);
    }
    s.removeDeadVehicles();
    for (Empire& e : s.empires)
        for (Relation& rel : e.relations) rel.messageSentThisTurn = false;
    sight::updateKnowledge(r, s);
    diplomacy::updateContacts(ctx);
    ai::rememberAiEvents(ctx);
    std::erase_if(ctx.moodEvents, [&](const MoodEvent& m) { return !living(s, m.empire); });
    ++s.turn;
    // The battles of the game turn just ended stay for the political steps of
    // the next one; older ones go.
    std::erase_if(s.combats, [&](const CombatRecord& c) { return c.turn + 1 < s.turn; });
    economy::updateReports(r, s);
    s.playerTurn = PlayerTurn{};
}

// The turn passes from `from` to the next living empire; after the last one
// the game turn ends.
void passTurn(TurnContext& ctx, EmpireId from) {
    GameState& s = ctx.state;
    const EmpireId next = firstLivingFrom(s, from.index() + 1);
    if (next.valid()) s.playerTurn = PlayerTurn{next, false, {}, {}};
    else endGameTurn(ctx);
}

// `e`'s end-of-turn processing, then the next player.
void finishPlayerTurn(LiveContext& lc, EmpireId e, Control control) {
    TurnContext& ctx = lc.ctx;
    GameState& s = ctx.state;
    empireEndOfTurn(ctx, e, ministersPlan(s, e, control));
    s.removeDeadVehicles();
    sight::updateKnowledge(ctx.rules, s);
    diplomacy::updateContacts(ctx);
    passTurn(ctx, e);
}

// Starts the next game turn when none is in progress. False when nobody is alive.
bool ensureRound(GameState& s) {
    if (s.playerTurn.empire.valid()) return true;
    const EmpireId first = firstLivingFrom(s, 0);
    if (!first.valid()) return false;
    s.playerTurn = PlayerTurn{first, false, {}, {}};
    return true;
}

void resume(LiveContext& lc) {
    GameState& s = lc.ctx.state;
    bool turnEnded = false;
    while (!s.gameOver) {
        if (!s.playerTurn.empire.valid()) {
            // An all-computer game plays one game turn per call.
            if (turnEnded && !anyLivingHuman(s)) return;
            if (!ensureRound(s)) return;
        }
        const EmpireId e = s.playerTurn.empire;
        if (!living(s, e)) {
            passTurn(lc.ctx, e);
            turnEnded = turnEnded || !s.playerTurn.empire.valid();
            continue;
        }
        const Control control = interactiveControl(s, e);
        if (!s.playerTurn.started && !startPlayerTurn(lc, e, control)) {
            passTurn(lc.ctx, e);
            turnEnded = turnEnded || !s.playerTurn.empire.valid();
            continue;
        }
        if (control == Control::Player) return;  // a human plays now
        finishPlayerTurn(lc, e, control);
        turnEnded = turnEnded || !s.playerTurn.empire.valid();
    }
}

TurnResult refused(EmpireId e, std::string why) {
    TurnResult out;
    out.rejected.emplace_back(e, std::move(why));
    return out;
}

} // namespace

EmpireId activePlayer(const GameState& s) {
    if (!turnBased(s) || s.gameOver) return {};
    if (s.playerTurn.empire.valid()) return s.playerTurn.empire;
    return firstLivingFrom(s, 0);
}

TurnResult resumeTurnBased(const Rules& r, GameState& s) {
    if (!turnBased(s) || s.gameOver) return {};
    LiveContext lc(r, s);
    resume(lc);
    return lc.result();
}

TurnResult applyLive(const Rules& r, GameState& s, EmpireId e, const Command& c) {
    if (!turnBased(s)) {
        const CommandResult res = apply(r, s, e, c);
        return res.ok ? TurnResult{} : refused(e, std::format("{}: {}", commandName(c), res.error));
    }
    if (s.gameOver) return refused(e, "The game is over.");
    if (s.playerTurn.empire != e || !s.playerTurn.started) return refused(e, "It is not your turn.");
    LiveContext lc(r, s);
    applyEach(lc, e, std::span<const Command>(&c, 1), interactiveControl(s, e) == Control::Player);
    return lc.result();
}

TurnResult endPlayerTurn(const Rules& r, GameState& s, EmpireId e) {
    if (!turnBased(s) || s.gameOver) return {};
    LiveContext lc(r, s);
    if (!s.playerTurn.started) resume(lc);  // the turn must have started before it can end
    if (s.gameOver) return lc.result();
    if (s.playerTurn.empire != e || !s.playerTurn.started) {
        lc.ctx.rejected.emplace_back(e, "It is not your turn.");
        return lc.result();
    }
    finishPlayerTurn(lc, e, interactiveControl(s, e));
    resume(lc);
    return lc.result();
}

namespace detail {

TurnResult playTurnBasedTurn(const Rules& r, GameState& s, std::span<const EmpireOrders> orders, const TurnOptions& options) {
    LiveContext lc(r, s);
    std::vector<const EmpireOrders*> byEmpire(s.empires.size(), nullptr);
    for (const EmpireOrders& o : orders) {
        if (!o.empire.valid() || o.empire.index() >= s.empires.size()) continue;
        if (o.turn != s.turn) {
            lc.ctx.rejected.emplace_back(o.empire, std::format("Orders for turn {} ignored on turn {}", o.turn, s.turn));
            continue;
        }
        byEmpire[o.empire.index()] = &o;
    }
    if (!ensureRound(s)) return lc.result();
    while (!s.gameOver && s.playerTurn.empire.valid()) {
        const EmpireId e = s.playerTurn.empire;
        if (!living(s, e)) {
            passTurn(lc.ctx, e);
            continue;
        }
        Empire& emp = s.empire(e);
        const EmpireOrders* given = e.index() < byEmpire.size() ? byEmpire[e.index()] : nullptr;
        // As in a simultaneous turn: a human whose orders are missing is played
        // by the computer for this turn (spec 05 §7.1, §9.2).
        Control control = Control::Computer;
        std::optional<ai::MinisterSettings> saved;
        if (emp.kind == PlayerKind::Human) {
            if (given || !options.aiForMissing) {
                control = Control::Player;
            } else if (emp.aiMinimalChanges) {
                control = Control::Absent;
            } else {
                control = Control::StandIn;
                saved = ai::standIn(emp);
            }
        }
        if (!s.playerTurn.started && !startPlayerTurn(lc, e, control)) {
            if (saved) ai::restoreMinisters(s.empire(e), *saved);
            passTurn(lc.ctx, e);
            continue;
        }
        if (given) applyEach(lc, e, given->commands, false);
        finishPlayerTurn(lc, e, control);
        if (saved) ai::restoreMinisters(s.empire(e), *saved);
    }
    return lc.result();
}

} // namespace detail

} // namespace opense4::game
