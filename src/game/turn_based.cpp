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
    LiveContext(const Rules& r, GameState& s, TurnContext::Battles* battles = nullptr) : ctx{r, s, {}, {}, {}} {
        ctx.moodEvents = std::move(s.pendingMood);
        s.pendingMood.clear();
        ctx.battles = battles;
    }
    ~LiveContext() { ctx.state.pendingMood = std::move(ctx.moodEvents); }
    LiveContext(const LiveContext&) = delete;
    LiveContext& operator=(const LiveContext&) = delete;

    TurnResult result() { return TurnResult{std::move(ctx.rejected), std::move(questions), {}, std::move(ctx.records)}; }

    TurnContext ctx;
    std::vector<EntryQuestion> questions;
};

EmpireId firstLivingFrom(const GameState& s, size_t index) {
    for (size_t i = index; i < s.empires.size(); ++i)
        if (s.empires[i].alive) return EmpireId{i};
    return {};
}

// A living human who plays its own turns.
bool anyHumanToPlay(const GameState& s, const LiveOptions& options) {
    return std::any_of(s.empires.begin(), s.empires.end(),
                       [&](const Empire& e) { return e.alive && e.kind == PlayerKind::Human && !options.computerPlaysFor(e.id); });
}

// Interactive play: humans play their own turns, the computer the others and
// the humans it plays for now, as a stand-in (spec 05 §7.1).
Control liveControl(const GameState& s, EmpireId e, const LiveOptions& options) {
    const Empire& emp = s.empire(e);
    if (emp.kind != PlayerKind::Human) return Control::Computer;
    if (!options.computerPlaysFor(e)) return Control::Player;
    return emp.aiMinimalChanges ? Control::Absent : Control::StandIn;
}

// ---- Attack Sector questions --------------------------------------------------------------------

bool sameGroup(const EntryQuestion& q, VehicleId vehicle, FleetId fleet) {
    return fleet.valid() ? q.fleet == fleet : !q.fleet.valid() && q.vehicle == vehicle;
}

void dropQuestions(GameState& s, VehicleId vehicle, FleetId fleet) {
    std::erase_if(s.playerTurn.questions, [&](const EntryQuestion& q) { return sameGroup(q, vehicle, fleet); });
}

// A question stays while its group exists and still has orders to go on with.
void pruneQuestions(GameState& s) {
    std::erase_if(s.playerTurn.questions, [&](const EntryQuestion& q) {
        if (q.fleet.valid()) {
            const Fleet* f = s.fleet(q.fleet);
            return !f || fleetOrders(s, *f).empty();
        }
        const Vehicle* v = s.vehicle(q.vehicle);
        return !v || v->orders.empty();
    });
}

// ---- Carrying orders out ------------------------------------------------------------------------

void addQuestions(LiveContext& lc, const std::vector<EntryQuestion>& qs) {
    std::vector<EntryQuestion>& open = lc.ctx.state.playerTurn.questions;
    for (const EntryQuestion& q : qs) {
        if (std::find(lc.questions.begin(), lc.questions.end(), q) == lc.questions.end()) lc.questions.push_back(q);
        if (std::find(open.begin(), open.end(), q) == open.end()) open.push_back(q);
    }
}

// The groups move, fight and colonize now; then sight and first contact
// follow the new positions.
void carryOut(LiveContext& lc, const movement::LiveMove& move) {
    TurnContext& ctx = lc.ctx;
    GameState& s = ctx.state;
    addQuestions(lc, movement::runLive(ctx, move));
    s.removeDeadVehicles();
    pruneQuestions(s);
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
        // A colony's list runs at once when the player gives it an order,
        // except Use Facility: the immediate run covers vehicle lists only,
        // so that order waits for the colony's next run (spec 03 §8).
        if (o->planet.valid()) {
            if (o->orders.empty() || o->orders.back().kind != OrderKind::UseFacility) fx.move.planets.push_back(o->planet);
        } else if (o->fleet.valid()) {
            fx.move.fleets.push_back(o->fleet);
        } else if (o->vehicle.valid()) {
            fx.move.vehicles.push_back(o->vehicle);
        }
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
        // An answer settles its question, even one the rules refuse.
        const auto* answer = std::get_if<cmd::EnterSector>(&c);
        if (answer) dropQuestions(lc.ctx.state, answer->vehicle, answer->fleet);
        if (!res.ok) {
            lc.ctx.rejected.emplace_back(e, std::format("{}: {}", commandName(c), res.error));
            continue;
        }
        if (answer) {
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
        // New orders replace the ones a question was about.
        if (const auto* o = std::get_if<cmd::SetOrders>(&c); o && !o->planet.valid()) dropQuestions(lc.ctx.state, o->vehicle, o->fleet);
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

// Orders given at the start of a turn: they take effect (messages at once),
// and the vehicles carry them out afterwards with the rest (spec 05 §8
// "Turn-based game" step 3).
void giveOrders(LiveContext& lc, EmpireId e, std::vector<Command> commands) {
    if (commands.empty()) return;
    const bool messages = std::any_of(commands.begin(), commands.end(), [](const Command& c) {
        return std::holds_alternative<cmd::SendMessage>(c) || std::holds_alternative<cmd::AnswerMessage>(c);
    });
    detail::applyCommands(lc.ctx, e, std::move(commands));
    if (messages) diplomacy::deliverMessages(lc.ctx);
}

// ---- The political step (spec 05 §7.3 "What it counts") ----------------------------------------

// Everything logged since the empire's previous political step, among what
// is dated this game turn or the one before: the rest of its own last turn,
// the turns of the players after it and those of the players before it in
// this game turn (confirmed: binary). The marks are OpenSE4's way of knowing
// what was counted (spec 05 open question 44).
ai::PoliticalWindow politicalWindow(const GameState& s, EmpireId e) {
    ai::PoliticalWindow w;
    w.andLater = true;
    const PoliticsMark& mark = s.empire(e).politicsMark;
    const uint32_t earliest = s.turn > 0 ? s.turn - 1 : 0;
    if (mark.set && mark.turn >= earliest && mark.turn <= s.turn) {
        w.turn = mark.turn;
        w.battles = mark.battles;
        w.logs = mark.logs;
        w.firstMessage = mark.nextMessage;
    } else {
        w.turn = earliest;
    }
    return w;
}

// After the step: everything logged so far counts as counted.
void markPoliticalStep(GameState& s, EmpireId e) {
    PoliticsMark mark;
    mark.set = true;
    mark.turn = s.turn;
    for (const CombatRecord& c : s.combats) mark.battles += c.turn == s.turn ? 1 : 0;
    for (const Empire& x : s.empires)
        mark.logs.push_back(static_cast<uint32_t>(std::count_if(x.log.begin(), x.log.end(), [&](const LogEntry& l) { return l.turn == s.turn; })));
    mark.nextMessage = s.nextMessageId;
    s.empire(e).politicsMark = std::move(mark);
}

// ---- A player's turn ------------------------------------------------------------------------------

// The start of `e`'s turn (spec 05 §8 "Turn-based game", confirmed: binary):
// a human's destruction check; the start-of-turn step (AI state, political
// step, the Politics minister and then the other ministers give their orders,
// messages taking effect when sent); the vehicles get their movement back and
// every group carries out its orders, the ministers' new ones included; a
// computer player's destruction check. False when the empire is destroyed.
bool startPlayerTurn(LiveContext& lc, EmpireId e, Control control) {
    TurnContext& ctx = lc.ctx;
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    s.playerTurn.empire = e;
    s.playerTurn.started = true;
    s.playerTurn.questions.clear();
    const bool human = s.empire(e).kind == PlayerKind::Human;
    // 1. A human's turn starts with the destruction check (spec 05 §6).
    if (human) {
        score::checkDestruction(ctx, e);
        if (!living(s, e)) return false;
    }
    // 2. The start-of-turn step.
    ai::updateAiState(ctx, e);
    if (control != Control::Absent) {
        ai::politicalStep(ctx, e, politicalWindow(s, e));
        markPoliticalStep(s, e);
    }
    if (ministersPlan(s, e, control)) {
        giveOrders(lc, e, ai::planPoliticsOrders(r, s, e));
        giveOrders(lc, e, ai::planOrdersAfterPolitics(r, s, e));
    }
    ai::recordAiDecisions(ctx, e);
    // 3. Movement is refilled, and every group carries out its orders.
    movement::startTurn(ctx, e);
    movement::LiveMove all;
    all.empire = e;
    all.ask = control == Control::Player;
    carryOut(lc, all);
    // 4. A computer player's destruction check comes now; its turn then ends.
    if (!human) {
        score::checkDestruction(ctx, e);
        if (!living(s, e)) return false;
    }
    return true;
}

// The once-per-game-turn steps after the last player (spec 05 §8 steps 7-10):
// the design cleanup, the contact check, the victory check and the event step.
void endGameTurn(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    // The date advances; GameState::turn follows at the end, as in processTurn.
    const uint32_t date = s.turn + 1;
    if (date % 10 == 0) movement::purgeObsoleteDesigns(ctx);
    diplomacy::checkContacts(ctx);
    score::checkVictory(ctx, date);
    movement::runStellarHazards(ctx);
    {
        Rng rng = s.rng.fork();
        events::fireDueEvents(ctx, rng);
        events::rollNewEvent(ctx, date, rng);
    }
    s.removeDeadVehicles();
    // Per-turn flags clear once per game turn, not at each player's turn (inferred).
    for (Empire& e : s.empires)
        for (Relation& rel : e.relations) rel.messageSentThisTurn = false;
    sight::updateKnowledge(r, s);
    diplomacy::updateContacts(ctx);
    ai::rememberAiEvents(ctx);
    std::erase_if(ctx.moodEvents, [&](const MoodEvent& m) { return !living(s, m.empire); });
    ++s.turn;
    // The battles of the game turn just ended stay for the political steps of
    // the next one, which count those fought since each empire's previous
    // step (spec 05 §7.3); older ones go.
    std::erase_if(s.combats, [&](const CombatRecord& c) { return c.turn + 1 < s.turn; });
    economy::updateReports(r, s);
    s.playerTurn = PlayerTurn{};
}

// The turn passes from `from` to the next living empire; after the last one
// the game turn ends. An empire founded during the game turn (a rebel colony)
// plays when its number comes up, possibly in the same game turn (inferred).
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

// The computer plays `e`'s turn, or the rest of it when it has started (a
// human's turn it takes over). A human's stand-in has all ministers on for
// the turn, and the player's own settings come back after it.
void computerTurn(LiveContext& lc, EmpireId e, Control control) {
    TurnContext& ctx = lc.ctx;
    GameState& s = ctx.state;
    std::optional<ai::MinisterSettings> saved;
    if (control == Control::StandIn) saved = ai::standIn(s.empire(e));
    if (!s.playerTurn.started) {
        if (!startPlayerTurn(lc, e, control)) {
            if (saved) ai::restoreMinisters(s.empire(e), *saved);
            passTurn(ctx, e);
            return;
        }
    } else if (control != Control::Computer && ministersPlan(s, e, control)) {
        // Taking over a human's turn in progress: the ministers plan the rest
        // of it now, the Politics minister first, as at a start of turn, and
        // their orders are carried out at once (inferred). The turn's counters
        // and decisions were recorded when it started.
        applyBatch(lc, e, ai::planPoliticsOrders(ctx.rules, s, e));
        applyBatch(lc, e, ai::planOrdersAfterPolitics(ctx.rules, s, e));
    }
    s.playerTurn.questions.clear();
    finishPlayerTurn(lc, e, control);
    if (saved) ai::restoreMinisters(s.empire(e), *saved);
}

void resume(LiveContext& lc, const LiveOptions& options) {
    GameState& s = lc.ctx.state;
    bool turnEnded = false;
    while (!s.gameOver) {
        if (!s.playerTurn.empire.valid()) {
            // An all-computer game (or one whose humans the computer plays
            // for now) plays one game turn per call.
            if (turnEnded && !anyHumanToPlay(s, options)) return;
            if (!ensureRound(s)) return;
        }
        const EmpireId e = s.playerTurn.empire;
        if (!living(s, e)) {
            passTurn(lc.ctx, e);
            turnEnded = turnEnded || !s.playerTurn.empire.valid();
            continue;
        }
        const Control control = liveControl(s, e, options);
        if (control == Control::Player) {
            if (!s.playerTurn.started && !startPlayerTurn(lc, e, control)) {
                passTurn(lc.ctx, e);
                turnEnded = turnEnded || !s.playerTurn.empire.valid();
                continue;
            }
            return;  // a human plays now
        }
        computerTurn(lc, e, control);
        turnEnded = turnEnded || !s.playerTurn.empire.valid();
    }
}

TurnResult refused(EmpireId e, std::string why) {
    TurnResult out;
    out.rejected.emplace_back(e, std::move(why));
    return out;
}

// Runs a turn-based call with the answers of its battles (turn.hpp). A
// battle whose answer is missing stops the call: the state goes back to
// what it was before, and the result asks the question.
template <class Body>
TurnResult withBattles(GameState& s, const std::vector<BattleAnswer>* answers, Body&& body) {
    if (!answers || !tacticalOffered(s)) return body(nullptr);
    GameState before = s;
    TurnContext::Battles battles{answers, 0};
    try {
        return body(&battles);
    } catch (detail::BattleQuestionRaised& raised) {
        s = std::move(before);
        TurnResult out;
        out.battle = std::move(raised.question);
        return out;
    }
}

} // namespace

bool LiveOptions::computerPlaysFor(EmpireId e) const { return std::find(computerPlays.begin(), computerPlays.end(), e) != computerPlays.end(); }
bool tacticalOffered(const GameState& s) { return turnBased(s) && !s.options.noTacticalCombat; }

EmpireId activePlayer(const GameState& s) {
    if (!turnBased(s) || s.gameOver) return {};
    if (s.playerTurn.empire.valid()) return s.playerTurn.empire;
    return firstLivingFrom(s, 0);
}

TurnResult resumeTurnBased(const Rules& r, GameState& s, const LiveOptions& options, const std::vector<BattleAnswer>* battles) {
    if (!turnBased(s) || s.gameOver) return {};
    return withBattles(s, battles, [&](TurnContext::Battles* b) {
        LiveContext lc(r, s, b);
        resume(lc, options);
        return lc.result();
    });
}

TurnResult applyLive(const Rules& r, GameState& s, EmpireId e, const Command& c, const std::vector<BattleAnswer>* battles) {
    if (!turnBased(s)) {
        const CommandResult res = apply(r, s, e, c);
        return res.ok ? TurnResult{} : refused(e, std::format("{}: {}", commandName(c), res.error));
    }
    if (s.gameOver) return refused(e, "The game is over.");
    if (s.playerTurn.empire != e || !s.playerTurn.started) return refused(e, "It is not your turn.");
    return withBattles(s, battles, [&](TurnContext::Battles* b) {
        LiveContext lc(r, s, b);
        applyEach(lc, e, std::span<const Command>(&c, 1), s.empire(e).kind == PlayerKind::Human);
        return lc.result();
    });
}

TurnResult endPlayerTurn(const Rules& r, GameState& s, EmpireId e, const LiveOptions& options, const std::vector<BattleAnswer>* battles) {
    if (!turnBased(s) || s.gameOver) return {};
    return withBattles(s, battles, [&](TurnContext::Battles* b) {
        LiveContext lc(r, s, b);
        if (!s.playerTurn.started) resume(lc, options);  // the turn must have started before it can end
        if (s.gameOver) return lc.result();
        if (s.playerTurn.empire != e || !s.playerTurn.started) {
            lc.ctx.rejected.emplace_back(e, "It is not your turn.");
            return lc.result();
        }
        const Control control = liveControl(s, e, options);
        if (control == Control::Player) finishPlayerTurn(lc, e, control);
        else computerTurn(lc, e, control);
        resume(lc, options);
        return lc.result();
    });
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
