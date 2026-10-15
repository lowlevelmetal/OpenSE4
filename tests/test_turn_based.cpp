// The turn-based game style (spec 05 §8 "Turn-based game", spec 03 §6.3
// "Turn-based", spec 04 §2): players in sequence, orders carried out as
// they are given, end-of-turn processing per player, the once-per-game-turn
// steps after the last player.

#include "movement_fixture.hpp"

#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/commands.hpp"
#include "game/events.hpp"
#include "game/intel.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::mvtest;

namespace {

Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }

Order moveTo(SystemId s, int x, int y) {
    Order o;
    o.kind = OrderKind::MoveTo;
    o.location = at(s, x, y);
    return o;
}

// Gives the vehicle a huge fuel cell (a copy of its design with one more part) and fills it.
void fuel(World& w, VehicleId id) {
    Design d = w.s.design(w.v(id).design);
    d.name += " (fuelled)";
    d.entries.push_back({test::componentIndex(w.rules(), "Mv Fuel Cell"), -1});
    const DesignId fuelled = addDesign(w.s, std::move(d));
    Vehicle& v = w.v(id);
    v.design = fuelled;
    v.damage.resize(w.s.design(fuelled).entries.size(), 0);
    v.supply = 1'000'000;
}

cmd::SetOrders ordersFor(VehicleId v, std::vector<Order> orders) {
    cmd::SetOrders c;
    c.vehicle = v;
    c.orders = std::move(orders);
    return c;
}

// Two human empires in one explored system; each keeps a ship far away so
// neither is destroyed when its turn comes up.
struct Duel {
    World w;
    SystemId a;
    VehicleId runner;  // A's: three engines

    Duel() {
        a = w.system("A");
        w.s.options.simultaneous = false;
        runner = w.spawn(w.ship(kA, "Runner", 3), at(a, 0, 6));
        fuel(w, runner);
        w.spawn(w.ship(kB, "Keeper", 1), at(a, 12, 12));
        w.exploreAll(kA);
        w.exploreAll(kB);
    }
    const Rules& r() const { return w.rules(); }
    GameState& s() { return w.s; }
};

bool hasRejection(const TurnResult& res) { return !res.rejected.empty(); }

} // namespace

TEST_CASE("turn-based: the first player's turn starts, and orders execute at once, spending movement points") {
    Duel d;
    CHECK(activePlayer(d.s()) == kA);
    resumeTurnBased(d.r(), d.s());
    REQUIRE(d.s().playerTurn.empire == kA);
    REQUIRE(d.s().playerTurn.started);
    CHECK(d.w.v(d.runner).movement == 3);

    const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 12, 6)}));
    CHECK_FALSE(hasRejection(res));
    CHECK(d.w.v(d.runner).location == at(d.a, 3, 6));
    CHECK(d.w.v(d.runner).movement == 0);
    CHECK(d.w.v(d.runner).orders.size() == 1);  // the rest waits for the next turn
    CHECK(d.s().turn == 0);

    // A second order in the same turn has no movement left to spend.
    applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 3, 0)}));
    CHECK(d.w.v(d.runner).location == at(d.a, 3, 6));

    // B cannot act in A's turn, nor end it.
    CHECK(hasRejection(applyLive(d.r(), d.s(), kB, cmd::SetOrders{})));
    CHECK(hasRejection(endPlayerTurn(d.r(), d.s(), kB)));

    // The turn passes to B; A's ship waits.
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK(activePlayer(d.s()) == kB);
    CHECK(d.s().playerTurn.started);
    CHECK(d.w.v(d.runner).location == at(d.a, 3, 6));
    // After B, the game turn ends; at the start of A's next turn the ship
    // regains its movement and first carries on with its orders (spec 03 §6.3).
    endPlayerTurn(d.r(), d.s(), kB);
    CHECK(d.s().turn == 1);
    CHECK(activePlayer(d.s()) == kA);
    CHECK(d.w.v(d.runner).location == at(d.a, 3, 3));
    CHECK(d.w.v(d.runner).movement == 0);
}

TEST_CASE("turn-based: each step is reported as it is made, by an order given and at a turn's start, and changes nothing") {
    // The client's movement animation (spec 06 §2.4 "Moves as they are made"):
    // every group step, in order, from where the members stand, with the
    // heading they take. The game comes out the same without the observer.
    Duel watched, plain;
    std::vector<MovementStep> steps;
    const MovementObserver observe = [&](const MovementStep& st) {
        for (const VehicleId id : st.members) CHECK(watched.w.v(id).location == st.from);
        steps.push_back(st);
    };
    LiveOptions options;
    options.movementStep = observe;
    resumeTurnBased(watched.r(), watched.s(), options);
    resumeTurnBased(plain.r(), plain.s());
    CHECK(steps.empty());   // nothing has orders yet

    applyLive(watched.r(), watched.s(), kA, ordersFor(watched.runner, {moveTo(watched.a, 12, 6)}), nullptr, observe);
    applyLive(plain.r(), plain.s(), kA, ordersFor(plain.runner, {moveTo(plain.a, 12, 6)}));
    REQUIRE(steps.size() == 3);
    for (size_t i = 0; i < steps.size(); ++i) {
        CHECK(steps[i].day == 0);
        CHECK(steps[i].owner == kA);
        CHECK(steps[i].members == std::vector<VehicleId>{watched.runner});
        CHECK(steps[i].from == at(watched.a, int(i), 6));
        CHECK(steps[i].to == at(watched.a, int(i) + 1, 6));
        CHECK(steps[i].heading == 2);   // east
        CHECK_FALSE(steps[i].warp.valid());
        CHECK(steps[i].liveSteps == 1);  // after the run's start (TurnResult::liveSteps)
        CHECK(steps[i].lead == watched.runner);
    }
    CHECK(stateChecksum(watched.s()) == stateChecksum(plain.s()));

    // A turns north; at the start of A's next turn the ship carries its orders on.
    applyLive(watched.r(), watched.s(), kA, ordersFor(watched.runner, {moveTo(watched.a, 3, 0)}), nullptr, observe);
    applyLive(plain.r(), plain.s(), kA, ordersFor(plain.runner, {moveTo(plain.a, 3, 0)}));
    steps.clear();
    endPlayerTurn(watched.r(), watched.s(), kA, options);
    endPlayerTurn(plain.r(), plain.s(), kA);
    CHECK(steps.empty());   // B has nothing to move
    endPlayerTurn(watched.r(), watched.s(), kB, options);
    endPlayerTurn(plain.r(), plain.s(), kB);
    REQUIRE(steps.size() == 3);
    CHECK(steps[0].from == at(watched.a, 3, 6));
    CHECK(steps[2].to == at(watched.a, 3, 3));
    CHECK(steps[0].heading == 0);   // up
    CHECK(stateChecksum(watched.s()) == stateChecksum(plain.s()));

}

TEST_CASE("turn-based: the combat replays keep the battles of the game turn just ended and the three before it") {
    // Spec 04 §17 (confirmed: binary; since 2026-10-14): at the end of each game
    // turn the battles whose turn is at least the turn just ended − 3 stay.
    Duel d;
    resumeTurnBased(d.r(), d.s());
    for (int turn = 0; turn < 6; ++turn) {
        REQUIRE(d.s().turn == uint32_t(turn));
        CombatRecord rec;
        rec.turn = uint32_t(turn);
        d.s().combats.push_back(rec);
        endPlayerTurn(d.r(), d.s(), kA);
        endPlayerTurn(d.r(), d.s(), kB);
        std::vector<uint32_t> kept;
        for (const CombatRecord& c : d.s().combats) kept.push_back(c.turn);
        std::vector<uint32_t> expected;
        for (int t = std::max(0, turn - 3); t <= turn; ++t) expected.push_back(uint32_t(t));
        CHECK(kept == expected);
    }
}

TEST_CASE("turn-based: a human is asked before entering a sector with enemies; entering fights at once and the order fails") {
    for (bool enter : {true, false}) {
        CAPTURE(enter);
        Duel d;
        const VehicleId picket = d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 1, 6));
        resumeTurnBased(d.r(), d.s());
        REQUIRE(d.s().playerTurn.empire == kA);

        // In-system steps go around a visible enemy's square unless it is where the
        // group is heading (spec 03 §6.2), so the question comes at the picket's square
        // when the first Move To ends there.
        const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 1, 6), moveTo(d.a, 6, 6)}));
        REQUIRE(res.questions.size() == 1);
        CHECK(res.questions[0] == EntryQuestion{d.runner, {}, at(d.a, 1, 6)});
        CHECK(d.w.v(d.runner).location == at(d.a, 0, 6));  // stopped before the sector
        CHECK(d.w.v(d.runner).movement == 3);
        CHECK(d.w.v(d.runner).orders.size() == 2);
        CHECK(d.s().combats.empty());

        const TurnResult answer = applyLive(d.r(), d.s(), kA, cmd::EnterSector{d.runner, {}, at(d.a, 1, 6), enter});
        CHECK_FALSE(hasRejection(answer));
        if (enter) {
            // The battle is fought at once and the move ends there (spec 03 §6.2, §6.4).
            REQUIRE(d.s().combats.size() == 1);
            CHECK(d.s().combats[0].location == at(d.a, 1, 6));
            CHECK(d.s().combats[0].turn == 0);
            if (const Vehicle* v = d.s().vehicle(d.runner)) {
                CHECK(v->location == at(d.a, 1, 6));
                CHECK(v->orders.empty());
                CHECK(d.w.logged(kA, "Combat on entering the sector."));
            }
        } else {
            CHECK(d.s().combats.empty());
            CHECK(d.w.v(d.runner).location == at(d.a, 0, 6));
            CHECK(d.w.v(d.runner).orders.empty());
            CHECK(d.w.logged(kA, "orders cancelled"));
        }
        (void)picket;
    }
}

TEST_CASE("turn-based: at a turn's start the pass waits for the Attack Sector answer; the groups after it move only then") {
    // Spec 03 §6.2, spec 06 §2.4: the original's pass asks as the group
    // reaches the sector, and the next objects act only once it is answered.
    // A call that can stop (local games) stops at the question as at a
    // battle; one that cannot (a network or e-mail game's host) leaves it
    // open and goes on.
    Duel d;
    d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 1, 6));
    const VehicleId scout = d.w.spawn(d.w.ship(kA, "Scout", 3), at(d.a, 0, 9));
    fuel(d.w, scout);
    resumeTurnBased(d.r(), d.s());
    REQUIRE(d.s().playerTurn.empire == kA);
    // Orders carried over to A's next turn: the runner into the picket's square, the scout east.
    d.w.v(d.runner).orders = {moveTo(d.a, 1, 6)};
    d.w.v(scout).orders = {moveTo(d.a, 3, 9)};
    endPlayerTurn(d.r(), d.s(), kA);
    REQUIRE(d.s().playerTurn.empire == kB);
    const GameState bTurn = d.s();

    struct Run {
        TurnResult res;
        std::vector<MovementStep> steps;
        GameState state;
    };
    auto run = [&](const std::vector<BattleAnswer>* answers) {
        Run out{{}, {}, bTurn};
        LiveOptions o;
        o.movementStep = [&](const MovementStep& st) { out.steps.push_back(st); };
        out.res = endPlayerTurn(d.r(), out.state, kB, o, answers);
        return out;
    };
    auto firstStepOf = [](const std::vector<MovementStep>& steps, VehicleId id) {
        for (size_t i = 0; i < steps.size(); ++i)
            if (std::find(steps[i].members.begin(), steps[i].members.end(), id) != steps[i].members.end()) return i;
        return steps.size();
    };

    // No answer yet: the call stops at the question, the game as it was, the scout not moved.
    const std::vector<BattleAnswer> none;
    const Run asked = run(&none);
    REQUIRE(asked.res.battle.has_value());
    CHECK(asked.res.battle->kind == BattleQuestion::Kind::Enter);
    REQUIRE(asked.res.battle->entry.has_value());
    CHECK(*asked.res.battle->entry == EntryQuestion{d.runner, {}, at(d.a, 1, 6)});
    CHECK(stateChecksum(asked.state) == stateChecksum(bTurn));
    CHECK(firstStepOf(asked.steps, scout) == asked.steps.size());
    REQUIRE_FALSE(asked.res.liveSteps.empty());   // the runner's run had started
    CHECK(asked.res.liveSteps.back().vehicle == d.runner);

    // Declined: the runner's order fails there, and the scout moves after.
    BattleAnswer no;
    no.enter = false;
    const std::vector<BattleAnswer> declined{no};
    const Run off = run(&declined);
    CHECK_FALSE(off.res.battle.has_value());
    CHECK(off.state.playerTurn.empire == kA);
    CHECK(off.state.vehicle(d.runner)->orders.empty());
    CHECK(off.state.vehicle(d.runner)->location == at(d.a, 0, 6));
    CHECK(off.state.vehicle(scout)->location == at(d.a, 3, 9));
    CHECK(off.state.playerTurn.questions.empty());

    // Entered: the battle comes next (fought by the strategies with its answer), then the scout's moves.
    BattleAnswer yes;
    yes.enter = true;
    const std::vector<BattleAnswer> entered{yes};
    const Run in = run(&entered);
    REQUIRE(in.res.battle.has_value());
    CHECK(in.res.battle->kind == BattleQuestion::Kind::Choose);
    CHECK(firstStepOf(in.steps, scout) == in.steps.size());
    const std::vector<BattleAnswer> fought{yes, BattleAnswer{}};
    const Run done = run(&fought);
    CHECK_FALSE(done.res.battle.has_value());
    REQUIRE_FALSE(done.state.combats.empty());
    CHECK(done.state.combats.back().location == at(d.a, 1, 6));
    const size_t intoBattle = firstStepOf(done.steps, d.runner);
    REQUIRE(intoBattle < done.steps.size());
    CHECK(firstStepOf(done.steps, scout) > intoBattle);
    CHECK(done.state.vehicle(scout)->location == at(d.a, 3, 9));

    // A call that cannot stop: the question stays open and the scout has moved already.
    const Run host = run(nullptr);
    CHECK_FALSE(host.res.battle.has_value());
    CHECK(host.state.playerTurn.questions == std::vector<EntryQuestion>{EntryQuestion{d.runner, {}, at(d.a, 1, 6)}});
    CHECK(host.state.vehicle(d.runner)->orders.size() == 1);
    CHECK(host.state.vehicle(scout)->location == at(d.a, 3, 9));
}

TEST_CASE("turn-based: live steps go around an enemy's square on the way, without a question") {
    Duel d;
    d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 1, 6));
    resumeTurnBased(d.r(), d.s());
    REQUIRE(d.s().playerTurn.empire == kA);
    // Greedy in-system steps re-choose a square holding a visible hostile object that is
    // not the destination (spec 03 §6.2), in turn-based games as in simultaneous ones.
    const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 6, 6)}));
    CHECK(res.questions.empty());
    CHECK(d.s().combats.empty());
    CHECK(d.w.v(d.runner).location != at(d.a, 1, 6));
    CHECK(d.w.v(d.runner).location.sector.x == 3);  // three steps taken
}

TEST_CASE("turn-based: orders given in advance enter without asking and fight on entry") {
    Duel d;
    d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 1, 6));
    // processTurn plays the whole game turn: A's orders are carried out in A's turn.
    // (The first Move To ends at the picket's square, which steps never go around.)
    const EmpireOrders orders{kA, 0, {ordersFor(d.runner, {moveTo(d.a, 1, 6), moveTo(d.a, 6, 6)})}};
    TurnOptions o;
    o.aiForMissing = false;
    const TurnResult res = processTurn(d.r(), d.s(), std::span<const EmpireOrders>(&orders, 1), o);
    CHECK(res.questions.empty());
    CHECK(d.s().turn == 1);
    REQUIRE_FALSE(d.s().combats.empty());
    CHECK(d.s().combats[0].location == at(d.a, 1, 6));
    if (const Vehicle* v = d.s().vehicle(d.runner)) {
        CHECK(v->location == at(d.a, 1, 6));
        CHECK(v->orders.empty());
    }
}

TEST_CASE("turn-based: a sector with enemies is not entered when they cannot be seen or are at peace") {
    Duel d;
    d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 1, 6));
    d.w.setTreaty(kA, kB, Treaty::NonAggression);
    resumeTurnBased(d.r(), d.s());
    const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 6, 6)}));
    CHECK(res.questions.empty());
    CHECK(d.s().combats.empty());
    CHECK(d.w.v(d.runner).location == at(d.a, 3, 6));  // passed by
}

TEST_CASE("turn-based: messages take effect when sent") {
    Duel d;
    for (auto [x, y] : {std::pair{kA, kB}, std::pair{kB, kA}}) d.s().empire(x).relation(y).contact = true;
    d.w.setTreaty(kA, kB, Treaty::NonAggression);
    resumeTurnBased(d.r(), d.s());

    DiplomaticMessage m;
    m.to = kB;
    m.type = MessageType::DeclareWar;
    const TurnResult res = applyLive(d.r(), d.s(), kA, cmd::SendMessage{m});
    CHECK_FALSE(hasRejection(res));
    CHECK(d.s().empire(kA).relation(kB).treaty == Treaty::War);
    CHECK(d.s().empire(kB).relation(kA).treaty == Treaty::War);

    // One message per recipient per turn; the flag clears when the game turn ends.
    DiplomaticMessage again;
    again.to = kB;
    again.type = MessageType::General;
    CHECK(hasRejection(applyLive(d.r(), d.s(), kA, cmd::SendMessage{again})));
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK(d.s().empire(kA).relation(kB).messageSentThisTurn);
    endPlayerTurn(d.r(), d.s(), kB);
    CHECK_FALSE(d.s().empire(kA).relation(kB).messageSentThisTurn);
}

TEST_CASE("turn-based: each player's end-of-turn processing runs when it ends its turn; the game turn's steps after the last") {
    Duel d;
    d.s().options.victory.years = true;
    d.s().options.victory.yearsTurns = 0;  // met at the first victory check
    resumeTurnBased(d.r(), d.s());
    REQUIRE(d.s().empire(kA).history.empty());
    REQUIRE(d.s().empire(kB).history.empty());

    endPlayerTurn(d.r(), d.s(), kA);
    // A's processing ran (its statistics row), B's has not; the date has not moved.
    CHECK(d.s().empire(kA).history.size() == 1);
    CHECK(d.s().empire(kB).history.empty());
    CHECK(d.s().turn == 0);
    CHECK_FALSE(d.s().gameOver);

    endPlayerTurn(d.r(), d.s(), kB);
    CHECK(d.s().empire(kB).history.size() == 1);
    // After the last player: the date advanced and the victory check ran. The
    // game is completed but goes on for one more game turn, its last, which
    // every player plays (spec 05 §6, confirmed: binary).
    CHECK(d.s().turn == 1);
    CHECK_FALSE(d.s().gameOver);
    CHECK(d.s().lastTurn);
    CHECK(activePlayer(d.s()) == kA);
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {}))));
    // Every score shows during the last turn (the Victory ending points to the Scores window).
    d.s().options.scoreDisplay = 0;
    CHECK(score::scoreVisible(d.s(), kA, kB));

    // A saved game of the last turn keeps it.
    auto loaded = deserializeState(serializeState(d.s()));
    REQUIRE(loaded.has_value());
    CHECK(loaded->lastTurn);

    endPlayerTurn(d.r(), d.s(), kA);
    CHECK(activePlayer(d.s()) == kB);
    CHECK_FALSE(d.s().gameOver);
    endPlayerTurn(d.r(), d.s(), kB);
    // The last turn's end-of-turn processing is skipped entirely (spec 05 §6,
    // confirmed: binary): no statistics row. When it wraps, the game is over
    // before anything of the next game turn: the date stays.
    CHECK(d.s().gameOver);
    CHECK(d.s().turn == 1);
    CHECK(d.s().empire(kA).history.size() == 1);
    CHECK(activePlayer(d.s()) == EmpireId{});
    CHECK(hasRejection(applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {}))));
}

TEST_CASE("turn-based: the last turn has no start- or end-of-turn steps but orders given live (spec 05 §6)") {
    Duel d;
    d.s().options.victory.years = true;
    d.s().options.victory.yearsTurns = 0;
    resumeTurnBased(d.r(), d.s());
    endPlayerTurn(d.r(), d.s(), kA);
    endPlayerTurn(d.r(), d.s(), kB);
    REQUIRE(d.s().lastTurn);
    REQUIRE_FALSE(d.s().gameOver);
    // A's turn of the last game turn has begun (resume): its runner got no
    // movement back, and the order it was given before ran nothing.
    Vehicle& runner = d.w.v(d.runner);
    runner.movement = 2;
    runner.orders = {moveTo(d.a, 12, 6)};
    d.s().playerTurn = PlayerTurn{};
    d.s().empire(kA).researchPool = 5000;
    const int64_t pool = d.s().empire(kA).researchPool;
    resumeTurnBased(d.r(), d.s());
    REQUIRE(activePlayer(d.s()) == kA);
    CHECK(d.w.v(d.runner).movement == 2);
    CHECK(d.w.v(d.runner).location == at(d.a, 0, 6));
    // The human still gives orders live, with the movement left.
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 12, 6)}))));
    CHECK(d.w.v(d.runner).movement == 0);
    CHECK(d.w.v(d.runner).location == at(d.a, 2, 6));
    // Its end-of-turn processing is skipped: the research pool is not spent.
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK(d.s().empire(kA).researchPool == pool);
    CHECK(d.s().empire(kA).history.size() == 1);
}

TEST_CASE("turn-based: a simultaneous game's victory marks it completed, and further turns skip every start- and end-of-turn step (spec 05 §6)") {
    Duel d;
    d.s().options.simultaneous = true;
    d.s().options.victory.years = true;
    d.s().options.victory.yearsTurns = 0;
    processTurn(d.r(), d.s(), {});
    // Nothing ends the game: the check only marks it completed.
    CHECK_FALSE(d.s().gameOver);
    CHECK(d.s().lastTurn);
    CHECK(gameDecided(d.s()));
    REQUIRE(d.s().turn == 1);
    REQUIRE(d.s().empire(kA).history.size() == 1);
    // The host may go on: the turn is processed, the date advances, the ships
    // move, but no end-of-turn processing runs.
    d.s().empire(kA).researchPool = 5000;
    Vehicle& runner = d.w.v(d.runner);
    runner.orders = {moveTo(d.a, 12, 6)};
    processTurn(d.r(), d.s(), {});
    CHECK(d.s().turn == 2);
    CHECK_FALSE(d.s().gameOver);
    CHECK(d.s().empire(kA).researchPool == 5000);
    CHECK(d.s().empire(kA).history.size() == 1);
    CHECK(d.w.v(d.runner).location != at(d.a, 0, 6));
    // The Game Over entry is not logged again; the log is no longer pruned
    // (that is end-of-turn processing too), so the first one stays.
    int overs = 0;
    for (const LogEntry& l : d.s().empire(kA).log) overs += l.title == "Game Over" ? 1 : 0;
    CHECK(overs == 1);
}

TEST_CASE("turn-based: a human with nothing left is destroyed at its turn start and plays that turn marked dead") {
    // Spec 05 §6, spec 06 §7 Q83, Q99 (confirmed: binary): the destruction
    // check comes at the end of a human's turn start; one found defeated is
    // destroyed at once (the others are told, its treaties end) and plays
    // that turn, its last, already marked dead; the Lose ending is due when
    // it owned no colony and no vehicle as the turn came up.
    Duel d;
    for (Vehicle& v : d.s().vehicles)
        if (v.owner == kB) v.count = 0;
    d.s().removeDeadVehicles();
    resumeTurnBased(d.r(), d.s());
    CHECK(d.s().empire(kB).alive);  // not before its own turn
    CHECK(ai::anyHumanLeft(d.s()));
    endPlayerTurn(d.r(), d.s(), kA);
    // B's last turn: destroyed, and it is its turn.
    CHECK_FALSE(d.s().empire(kB).alive);
    CHECK(activePlayer(d.s()) == kB);
    CHECK(d.s().playerTurn.started);
    CHECK(playsLastTurn(d.s(), kB));
    CHECK(d.s().playerTurn.loseEnding);
    CHECK(d.s().turn == 0);
    CHECK(d.w.logged(kA, "Last Empire Standing"));
    // A human still plays.
    CHECK(ai::anyHumanLeft(d.s()));
    // Its orders are refused: the empire is dead.
    CHECK(hasRejection(applyLive(d.r(), d.s(), kB, cmd::SetOrders{})));
    endPlayerTurn(d.r(), d.s(), kB);
    CHECK_FALSE(playsLastTurn(d.s(), kB));
    // The game turn ended and A plays again, the last empire standing.
    CHECK(d.s().turn == 1);
    CHECK(activePlayer(d.s()) == kA);
    CHECK_FALSE(d.s().playerTurn.loseEnding);
    // B's turn never comes again.
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK(d.s().turn == 2);
    CHECK(activePlayer(d.s()) == kA);
}

TEST_CASE("turn-based: a human who loses everything in its own turn lives on until its next turn start") {
    // Spec 06 §7 Q99 (confirmed: binary): no destruction check for a human at
    // the end of its own turn; it is alive while the others play.
    Duel d;
    resumeTurnBased(d.r(), d.s());
    endPlayerTurn(d.r(), d.s(), kA);
    REQUIRE(activePlayer(d.s()) == kB);
    for (Vehicle& v : d.s().vehicles)
        if (v.owner == kB) v.count = 0;
    d.s().removeDeadVehicles();
    endPlayerTurn(d.r(), d.s(), kB);
    CHECK(d.s().empire(kB).alive);
    CHECK(activePlayer(d.s()) == kA);
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK_FALSE(d.s().empire(kB).alive);
    CHECK(activePlayer(d.s()) == kB);
    CHECK(d.s().playerTurn.loseEnding);
}

TEST_CASE("turn-based: a human left with units only is destroyed without the Lose ending") {
    // The Lose ending's test (no colony, no vehicle) is narrower than the
    // destruction test (no populated colony, no ship or base): units count
    // for the first only (spec 06 §7 Q83).
    Duel d;
    for (Vehicle& v : d.s().vehicles)
        if (v.owner == kB) v.count = 0;
    d.s().removeDeadVehicles();
    d.w.spawn(d.w.design(kB, "Sat", "Test Satellite Hull", {"Test Satellite Gun"}), at(d.a, 6, 6));
    REQUIRE(score::defeated(d.r(), d.s(), kB));
    REQUIRE_FALSE(score::ownsNothing(d.s(), kB));
    resumeTurnBased(d.r(), d.s());
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK_FALSE(d.s().empire(kB).alive);
    CHECK(playsLastTurn(d.s(), kB));
    CHECK_FALSE(d.s().playerTurn.loseEnding);
}

TEST_CASE("turn-based: a failed order of the human whose turn it is is a message box; otherwise a log entry") {
    // Spec 03 §8 (confirmed: binary): the original says nothing about a
    // missing route. With OpenSE4's "Explain failed orders" a titled box comes
    // at once and nothing is logged; in a simultaneous game the minister's log
    // entry.
    {
        Duel d;
        const SystemId island = d.w.system("Island", 30, 30);   // no warp point leads there
        d.w.exploreAll(kA);
        resumeTurnBased(d.r(), d.s());
        Order o;
        o.kind = OrderKind::MoveTo;
        o.location = at(island, 1, 1);
        const TurnResult silent = applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {o}));
        CHECK_FALSE(hasRejection(silent));
        CHECK(silent.messages.empty());
        CHECK(d.w.v(d.runner).orders.empty());
        const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {o}), nullptr, {}, true);
        CHECK_FALSE(hasRejection(res));
        REQUIRE(res.messages.size() == 1);
        CHECK(res.messages.front().empire == kA);
        CHECK(res.messages.front().title == "Move");
        CHECK_FALSE(d.w.logged(kA, "Unable to Move"));
        CHECK(d.w.v(d.runner).orders.empty());
    }
    {
        Duel d;
        const SystemId island = d.w.system("Island", 30, 30);
        d.w.exploreAll(kA);
        d.s().options.simultaneous = true;
        Order o;
        o.kind = OrderKind::MoveTo;
        o.location = at(island, 1, 1);
        d.w.v(d.runner).orders = {o};
        const std::vector<EmpireOrders> given{EmpireOrders{kA, 0, {}}, EmpireOrders{kB, 0, {}}};
        TurnOptions explain;
        explain.explainFailedOrders = true;
        const TurnResult res = processTurn(d.r(), d.s(), given, explain);
        CHECK(res.messages.empty());
        CHECK(d.w.logged(kA, "Unable to Move"));
    }
}

TEST_CASE("turn-based: the Lose mark is saved with the turn") {
    // Save format 10: a game saved during the last turn of a human destroyed
    // at its turn start loads with the Lose ending still due.
    Duel d;
    for (Vehicle& v : d.s().vehicles)
        if (v.owner == kB) v.count = 0;
    d.s().removeDeadVehicles();
    resumeTurnBased(d.r(), d.s());
    endPlayerTurn(d.r(), d.s(), kA);
    REQUIRE(d.s().playerTurn.loseEnding);
    auto loaded = deserializeState(serializeState(d.s()));
    REQUIRE(loaded.has_value());
    CHECK(loaded->playerTurn.loseEnding);
    CHECK(playsLastTurn(*loaded, kB));
}

TEST_CASE("turn-based: a computer player with nothing left is destroyed when its turn comes up") {
    Duel d;
    d.s().empire(kB).kind = PlayerKind::Computer;
    for (Vehicle& v : d.s().vehicles)
        if (v.owner == kB) v.count = 0;
    d.s().removeDeadVehicles();
    resumeTurnBased(d.r(), d.s());
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK_FALSE(d.s().empire(kB).alive);
    CHECK(d.s().turn == 1);
    CHECK(activePlayer(d.s()) == kA);
}

TEST_CASE("turn-based: computer players take their turns in sequence") {
    const Rules& r = test::engineRules();
    GameState s = test::newEngineGame(11, 3, 12, false);
    s.options.simultaneous = false;
    const EmpireId human{0u}, cpu1{1u}, cpu2{2u};
    REQUIRE(s.empire(cpu1).kind == PlayerKind::Computer);

    resumeTurnBased(r, s);
    CHECK(activePlayer(s) == human);
    CHECK(s.empire(cpu1).history.empty());

    endPlayerTurn(r, s, human);
    // Both computer players played and ended their turns; the game turn ended
    // and the human's next turn has started.
    CHECK(s.turn == 1);
    CHECK(activePlayer(s) == human);
    CHECK(s.playerTurn.started);
    CHECK(s.empire(cpu1).history.size() == 1);
    CHECK(s.empire(cpu2).history.size() == 1);
    CHECK(s.empire(human).history.size() == 1);
}

TEST_CASE("turn-based: a game saved in the middle of a game turn goes on the same after loading") {
    Duel d;
    resumeTurnBased(d.r(), d.s());
    applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 12, 6)}));
    endPlayerTurn(d.r(), d.s(), kA);  // B's turn in progress
    REQUIRE(activePlayer(d.s()) == kB);

    auto loaded = deserializeState(serializeState(d.s()));
    REQUIRE(loaded.has_value());
    GameState& copy = *loaded;
    CHECK(copy.playerTurn.empire == kB);
    CHECK(copy.playerTurn.started);
    CHECK(stateChecksum(copy) == stateChecksum(d.s()));

    for (GameState* s : {&d.s(), &copy}) {
        endPlayerTurn(d.r(), *s, kB);
        applyLive(d.r(), *s, kA, ordersFor(d.runner, {moveTo(d.a, 12, 6), moveTo(d.a, 0, 0)}));
        endPlayerTurn(d.r(), *s, kA);
        endPlayerTurn(d.r(), *s, kB);
    }
    CHECK(copy.turn == 2);
    CHECK(stateChecksum(copy) == stateChecksum(d.s()));

    // Mid-turn movement survives too. Nothing counts the steps made in the
    // turn in progress (spec 03 §19 Q85): the movement left is all there is.
    applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 12, 12)}));
    auto again = deserializeState(serializeState(d.s()));
    REQUIRE(again.has_value());
    CHECK(again->vehicle(d.runner)->movement == d.w.v(d.runner).movement);
    CHECK(again->playerTurn.moves.empty());
    CHECK(stateChecksum(*again) == stateChecksum(d.s()));
}

TEST_CASE("turn-based: the same seed and the same inputs give the same game") {
    const Rules& r = test::engineRules();
    auto play = [&](bool viaProcessTurn) {
        GameState s = test::newEngineGame(5, 4, 16, false);
        s.options.simultaneous = false;
        for (int i = 0; i < 6; ++i) {
            if (viaProcessTurn) {
                processTurn(r, s, {});  // every player, the human too, played by the computer
            } else {
                resumeTurnBased(r, s);
                // The human sends every idle ship exploring, one command at a time.
                std::vector<VehicleId> idle;
                for (const Vehicle& v : s.vehicles)
                    if (v.owner == EmpireId{0u} && v.orders.empty() && !v.fleet.valid()) idle.push_back(v.id);
                Order explore;
                explore.kind = OrderKind::Explore;
                for (VehicleId id : idle) applyLive(r, s, EmpireId{0u}, ordersFor(id, {explore}));
                endPlayerTurn(r, s, EmpireId{0u});
            }
        }
        return s;
    };
    for (bool viaProcessTurn : {false, true}) {
        CAPTURE(viaProcessTurn);
        const GameState a = play(viaProcessTurn);
        const GameState b = play(viaProcessTurn);
        CHECK(a.turn == 6);
        CHECK(stateChecksum(a) == stateChecksum(b));
    }
}

TEST_CASE("turn-based: launches share the per-turn budget; a fighter group comes back only with its full movement") {
    World w;
    w.s.options.simultaneous = false;
    const SystemId a = w.system("A");
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const VehicleId carrier = w.spawn(w.ship(kA, "Carrier", 1, {"Mv Fighter Bay"}), at(a, 6, 6));
    fuel(w, carrier);
    w.v(carrier).cargo.units.push_back({fighter, 5});
    w.spawn(w.ship(kB, "Keeper", 1), at(a, 12, 12));
    resumeTurnBased(w.rules(), w.s);

    Order launch;
    launch.kind = OrderKind::LaunchUnits;
    launch.design = fighter;
    launch.amount = -1;
    auto group = [&]() -> Vehicle* {
        for (Vehicle& v : w.s.vehicles)
            if (v.design == fighter && v.count > 0) return &v;
        return nullptr;
    };
    // Launched at once, up to the bay's 3 per game turn (spec 03 §12)...
    applyLive(w.rules(), w.s, kA, ordersFor(carrier, {launch}));
    REQUIRE(group());
    CHECK(group()->count == 3);
    CHECK(w.v(carrier).orders.empty());
    // ... and a second launch in the same turn finds the budget spent.
    applyLive(w.rules(), w.s, kA, ordersFor(carrier, {launch}));
    CHECK(group()->count == 3);
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 2);

    // Next turn: the group has its full movement until it moves.
    endPlayerTurn(w.rules(), w.s, kA);
    endPlayerTurn(w.rules(), w.s, kB);
    REQUIRE(activePlayer(w.s) == kA);
    const VehicleId wasps = group()->id;
    REQUIRE(w.v(wasps).movement > 0);
    Order recover;
    recover.kind = OrderKind::RecoverUnits;
    recover.design = fighter;
    recover.amount = -1;
    w.v(wasps).movement -= 1;  // as if it had taken a step
    applyLive(w.rules(), w.s, kA, ordersFor(carrier, {recover}));
    REQUIRE(w.s.vehicle(wasps));
    CHECK(w.v(wasps).count == 3);
    w.v(wasps).movement += 1;
    applyLive(w.rules(), w.s, kA, ordersFor(carrier, {recover}));
    CHECK(w.s.vehicle(wasps) == nullptr);
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 5);
}

TEST_CASE("turn-based: a colony ship founds its colony when it reaches the planet with movement left") {
    Duel d;
    const ObjectId home = d.w.planet(d.a, {2, 6});
    const ObjectId near = d.w.planet(d.a, {4, 6});
    const ObjectId far = d.w.planet(d.a, {5, 8});
    d.w.colony(home, kA, 1000);
    const VehicleId settler = d.w.spawn(d.w.ship(kA, "Settler", 3, {"Test Rock Pod"}), at(d.a, 2, 6));
    const VehicleId late = d.w.spawn(d.w.ship(kA, "Late Settler", 3, {"Test Rock Pod"}), at(d.a, 2, 6));
    fuel(d.w, settler);
    fuel(d.w, late);
    resumeTurnBased(d.r(), d.s());

    Order colonize;
    colonize.kind = OrderKind::Colonize;
    colonize.object = near;
    applyLive(d.r(), d.s(), kA, ordersFor(settler, {colonize}));
    // Two steps, one movement point left: the colony is founded at once.
    REQUIRE(d.s().colony(near));
    CHECK(d.s().colony(near)->owner == kA);
    CHECK(d.s().vehicle(settler) == nullptr);

    // Three steps use every movement point: the ship waits for its next turn.
    colonize.object = far;
    applyLive(d.r(), d.s(), kA, ordersFor(late, {colonize}));
    CHECK(d.w.v(late).location == at(d.a, 5, 8));
    CHECK(d.s().colony(far) == nullptr);
    endPlayerTurn(d.r(), d.s(), kA);
    endPlayerTurn(d.r(), d.s(), kB);
    REQUIRE(activePlayer(d.s()) == kA);
    REQUIRE(d.s().colony(far));
    CHECK(d.s().colony(far)->owner == kA);
}

TEST_CASE("turn-based: orders that need no movement run one after another; a repeating list that never moves waits") {
    Duel d;
    const ObjectId home = d.w.planet(d.a, {0, 6});
    d.w.colony(home, kA, 1000);
    const VehicleId hauler = d.w.spawn(d.w.ship(kA, "Hauler", 1, {"Test Cargo Bay"}), at(d.a, 0, 6));
    fuel(d.w, hauler);
    resumeTurnBased(d.r(), d.s());

    // Load is always done, even when nothing more fits (spec 03 §8).
    Order load;
    load.kind = OrderKind::LoadCargo;
    load.amount = -1;
    // Four in-place orders, then a move: all carried out at once.
    applyLive(d.r(), d.s(), kA, ordersFor(hauler, {load, load, load, load, moveTo(d.a, 1, 6)}));
    CHECK(d.w.v(hauler).orders.empty());
    CHECK(d.w.v(hauler).location == at(d.a, 1, 6));
    CHECK(d.w.v(hauler).cargo.totalPopulation() > 0);

    // Loading again and again at one place: the list goes round until 21
    // orders are completed, then waits (spec 05 §8 "Turn-based game").
    cmd::SetOrders loop = ordersFor(hauler, {load, load});
    loop.repeat = true;
    const TurnResult res = applyLive(d.r(), d.s(), kA, loop);
    CHECK_FALSE(hasRejection(res));
    CHECK(d.w.v(hauler).orders.size() == 2);
    CHECK(d.w.v(hauler).repeatOrders);

    // A long list: at most 21 orders are completed; the rest waits.
    std::vector<Order> many(30, load);
    applyLive(d.r(), d.s(), kA, ordersFor(hauler, many));
    CHECK(d.w.v(hauler).orders.size() == 9);
    endPlayerTurn(d.r(), d.s(), kA);
    endPlayerTurn(d.r(), d.s(), kB);
    CHECK(d.w.v(hauler).orders.empty());  // carried on at the start of the next turn
}

TEST_CASE("turn-based: a vehicle counts as coming from elsewhere only until its owner's turn ends") {
    // Step 16 of the end-of-turn processing records each vehicle's current
    // sector as the one it comes from (spec 05 §8, spec 04 §3).
    Duel d;
    resumeTurnBased(d.r(), d.s());
    applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 2, 6)}));
    REQUIRE(d.w.v(d.runner).location == at(d.a, 2, 6));
    CHECK(d.w.v(d.runner).cameFrom == at(d.a, 1, 6));
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK(d.w.v(d.runner).cameFrom == at(d.a, 2, 6));
    CHECK(activePlayer(d.s()) == kB);
}

TEST_CASE("turn-based: the political step counts everything logged since the empire's previous one") {
    const Rules& r = test::engineRules();
    GameState s = test::newEngineGame(11, 3, 12, false);
    s.options.simultaneous = false;
    const EmpireId human{0u}, a{1u}, b{2u};
    for (auto [x, y] : {std::pair{a, b}, std::pair{b, a}}) s.empire(x).relation(y).contact = true;
    const auto& table = ai::builtinProfile().anger;
    const std::string culprit = effects::empireFullName(s.empire(b));
    auto report = [&](uint32_t turn) {
        s.empire(a).log.push_back(
            LogEntry{turn, LogCategory::Intelligence, "Sabotage", "A hostile intelligence operation struck us." + intel::suspectLine(culprit), std::nullopt, {}});
    };
    // Turn 5: one report of turn 4 was counted by a's step in turn 4; one came
    // after it, and one comes in turn 5 before a's turn.
    s.turn = 5;
    report(4);
    report(4);
    report(5);
    ai::PoliticalWindow w;
    w.turn = 4u;
    w.logs.assign(s.empires.size(), 0);
    w.logs[a.index()] = 1;
    w.andLater = true;
    TurnContext ctx{r, s, {}, {}, {}};
    s.empire(a).relation(b).anger = 50;
    ai::politicalStep(ctx, a, w);
    CHECK(s.empire(a).relation(b).anger == std::clamp(50 + 2 * table.intelligenceAgainstUs, 0, 100) + table.regularDecrease);

    // The turn-based game keeps the mark of each step.
    GameState g = test::newEngineGame(11, 3, 12, false);
    g.options.simultaneous = false;
    resumeTurnBased(r, g);
    REQUIRE(g.empire(human).politicsMark.set);
    CHECK(g.empire(human).politicsMark.turn == 0);
    CHECK_FALSE(g.empire(a).politicsMark.set);  // its turn has not come yet
    endPlayerTurn(r, g, human);
    for (EmpireId e : {a, b}) {
        const PoliticsMark& m = g.empire(e).politicsMark;
        REQUIRE(m.set);
        CHECK(m.turn == 0);
        CHECK(m.logs.size() == g.empires.size());
        CHECK(m.nextMessage <= g.nextMessageId);
    }
}

TEST_CASE("turn-based: the ministers plan before the vehicles regain their movement and carry out every order") {
    // Spec 05 §8 "Turn-based game": the start-of-turn step comes first, then
    // the movement refill and the orders, the ministers' new ones included.
    const Rules& r = test::engineRules();
    GameState s = test::newEngineGame(11, 3, 12, false);
    s.options.simultaneous = false;
    const EmpireId human{0u}, cpu{1u};
    resumeTurnBased(r, s);
    endPlayerTurn(r, s, human);
    // The computer players' groups carried out the orders their ministers gave
    // this turn: none waits with movement left and orders it could follow.
    for (const Vehicle& v : s.vehicles)
        if (v.owner == cpu && !v.orders.empty() && v.orders.front().kind == OrderKind::MoveTo && v.location != v.orders.front().location)
            CHECK(v.movement == 0);
}

TEST_CASE("turn-based: an Attack order's approach asks like any step; entering the target's sector fights there and ends the orders") {
    Duel d;
    const VehicleId gunboat = d.w.spawn(d.w.ship(kA, "Gunboat", 3, {"Test Laser", "Mv Armor", "Mv Armor"}), at(d.a, 0, 0));
    fuel(d.w, gunboat);
    const VehicleId target = d.w.spawn(d.w.ship(kB, "Target", 1, {"Mv Armor", "Mv Armor", "Mv Armor", "Mv Armor"}), at(d.a, 2, 0));
    resumeTurnBased(d.r(), d.s());
    Order attack;
    attack.kind = OrderKind::Attack;
    attack.location = at(d.a, 2, 0);
    attack.vehicle = target;
    // The step into the target's sector asks (spec 03 §6.2, confirmed: binary).
    const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(gunboat, {attack, moveTo(d.a, 0, 3)}));
    REQUIRE(res.questions.size() == 1);
    CHECK(res.questions[0].where == at(d.a, 2, 0));
    CHECK(d.s().combats.empty());
    const TurnResult in = applyLive(d.r(), d.s(), kA, cmd::EnterSector{gunboat, {}, at(d.a, 2, 0), true});
    CHECK_FALSE(hasRejection(in));
    // Every movement step runs a battle check: the battle is fought on
    // entering, and the group's whole list is cleared, the Attack with it
    // (spec 04 §2, questions 18 and 48).
    REQUIRE(d.s().combats.size() == 1);
    CHECK(d.s().combats[0].location == at(d.a, 2, 0));
    REQUIRE(d.s().vehicle(gunboat));
    CHECK(d.w.v(gunboat).location == at(d.a, 2, 0));
    CHECK(d.w.v(gunboat).orders.empty());
    CHECK_FALSE(d.w.logged(kA, "Combat on entering the sector."));   // the battle is the news (spec 03 §8)
}

TEST_CASE("turn-based: the Attack order where the target is fights at once and is used up; sitting together starts nothing") {
    Duel d;
    const VehicleId gunboat = d.w.spawn(d.w.ship(kA, "Gunboat", 3, {"Test Laser", "Mv Armor", "Mv Armor"}), at(d.a, 6, 0));
    fuel(d.w, gunboat);
    const VehicleId target = d.w.spawn(d.w.ship(kB, "Target", 1, {"Mv Armor", "Mv Armor", "Mv Armor", "Mv Armor"}), at(d.a, 6, 0));
    resumeTurnBased(d.r(), d.s());
    // Hostile ships sharing a sector fight only when an order runs a check (spec 04 §2).
    endPlayerTurn(d.r(), d.s(), kA);
    endPlayerTurn(d.r(), d.s(), kB);
    REQUIRE(activePlayer(d.s()) == kA);
    CHECK(d.s().combats.empty());
    Order attack;
    attack.kind = OrderKind::Attack;
    attack.vehicle = target;
    const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(gunboat, {attack}));
    CHECK_FALSE(hasRejection(res));
    REQUIRE(d.s().combats.size() == 1);
    CHECK(d.s().combats[0].location == at(d.a, 6, 0));
    REQUIRE(d.s().vehicle(gunboat));
    CHECK(d.w.v(gunboat).orders.empty());
    // The attack cost 1 movement point; the parts the battle took (its bridge
    // and two engines here) cap what is left at the new maximum (spec 03 §6.1).
    CHECK(d.w.v(gunboat).movement == std::min(2, vehicleMaxMovement(d.r(), d.s(), d.w.v(gunboat))));
}

TEST_CASE("turn-based: open Attack Sector questions stay in the game until answered or overtaken") {
    Duel d;
    d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 1, 6));
    resumeTurnBased(d.r(), d.s());
    const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 1, 6), moveTo(d.a, 6, 6)}));
    REQUIRE(res.questions.size() == 1);
    CHECK(d.s().playerTurn.questions == res.questions);

    // Saved with the game, and only in the view of the player whose turn it is.
    auto copy = deserializeState(serializeState(d.s()));
    REQUIRE(copy.has_value());
    CHECK(copy->playerTurn.questions == res.questions);
    CHECK(redactForEmpire(d.r(), d.s(), kA).playerTurn.questions == res.questions);
    CHECK(redactForEmpire(d.r(), d.s(), kB).playerTurn.questions.empty());
    CHECK(redactForEmpire(d.r(), d.s(), EmpireId{}).playerTurn.questions.empty());

    // New orders for the group replace the question.
    applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 0, 5)}));
    CHECK(d.s().playerTurn.questions.empty());
    CHECK(d.w.v(d.runner).location == at(d.a, 0, 5));

    // Asked again, then answered: the question goes.
    const TurnResult again = applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 1, 6)}));
    REQUIRE(again.questions.size() == 1);
    CHECK(d.s().playerTurn.questions.size() == 1);
    applyLive(d.r(), d.s(), kA, cmd::EnterSector{d.runner, {}, at(d.a, 1, 6), false});
    CHECK(d.s().playerTurn.questions.empty());

    // A question left open ends with the player's turn.
    applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 1, 6)}));
    REQUIRE(d.s().playerTurn.questions.size() == 1);
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK(d.s().playerTurn.questions.empty());
}

TEST_CASE("turn-based: the computer plays a human's turn, or the rest of it, as a stand-in") {
    const Rules& r = test::engineRules();
    GameState s = test::newEngineGame(11, 3, 12, true);
    s.options.simultaneous = false;
    const EmpireId a{0u}, b{1u}, c{2u};
    for (const Empire& e : s.empires) REQUIRE(e.kind == PlayerKind::Human);
    const bool bAll = s.empire(b).ministerAll;
    const bool cAll = s.empire(c).ministerAll;
    const uint32_t bAreas = s.empire(b).ministers;

    const LiveOptions awayB{{b}};
    resumeTurnBased(r, s, awayB);
    CHECK(activePlayer(s) == a);
    // A ends its turn; the computer plays B's; C's turn starts.
    endPlayerTurn(r, s, a, awayB);
    CHECK(activePlayer(s) == c);
    CHECK(s.playerTurn.started);
    CHECK(s.empire(b).history.size() == 1);  // B's end-of-turn processing ran
    CHECK(s.empire(b).ministerAll == bAll);  // and its own minister settings are back
    CHECK(s.empire(b).ministers == bAreas);
    CHECK(s.empire(c).history.empty());

    // C runs out of time: the computer plays the rest of its turn.
    endPlayerTurn(r, s, c, LiveOptions{{c}});
    CHECK(s.turn == 1);
    CHECK(s.empire(c).history.size() == 1);
    CHECK(s.empire(c).ministerAll == cAll);
    CHECK(activePlayer(s) == a);
    CHECK(s.playerTurn.started);

    // With every human played by the computer, one game turn per call, then
    // the game waits between game turns.
    const LiveOptions everyone{{a, b, c}};
    endPlayerTurn(r, s, a, everyone);
    CHECK(s.turn == 2);
    CHECK_FALSE(s.playerTurn.empire.valid());
    CHECK(activePlayer(s) == a);
    resumeTurnBased(r, s, everyone);
    CHECK(s.turn == 3);
    CHECK_FALSE(s.playerTurn.empire.valid());
    // A player back at the controls: its turn starts.
    resumeTurnBased(r, s);
    CHECK(s.turn == 3);
    CHECK(activePlayer(s) == a);
    CHECK(s.playerTurn.started);
}

TEST_CASE("turn-based: Use Component clears the list first and takes effect as it is given (spec 03 §8)") {
    Duel d;
    resumeTurnBased(d.r(), d.s());
    REQUIRE(d.s().playerTurn.started);
    const VehicleId dasher = d.w.spawn(d.w.ship(kA, "Dasher", 3, {"Mv Energy Cell"}), at(d.a, 0, 0));
    fuel(d.w, dasher);
    {
        TurnContext ctx{d.r(), d.s(), {}, {}, {}};
        movement::startTurn(ctx, kA);  // the new ship gets its movement
    }
    applyLive(d.r(), d.s(), kA, ordersFor(dasher, {moveTo(d.a, 12, 0)}));
    REQUIRE(d.w.v(dasher).location == at(d.a, 3, 0));
    REQUIRE(d.w.v(dasher).movement == 0);
    // The waiting Move To no longer holds it back: the list is cleared, then
    // the use runs at once (position 7: the energy cell).
    Order use;
    use.kind = OrderKind::UseComponent;
    use.amount = 7;
    const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(dasher, {moveTo(d.a, 12, 0), use}));
    CHECK_FALSE(hasRejection(res));
    CHECK(d.w.v(dasher).orders.empty());
    CHECK_FALSE(d.w.v(dasher).repeatOrders);
    CHECK(d.w.v(dasher).movement == 4);   // the energy's 4 this turn, without a cap
    CHECK_FALSE(entryIntact(d.r(), d.s(), d.w.v(dasher), 7));
    applyLive(d.r(), d.s(), kA, ordersFor(dasher, {moveTo(d.a, 12, 0)}));
    CHECK(d.w.v(dasher).location == at(d.a, 7, 0));
}

TEST_CASE("turn-based: Use Facility clears the colony's list and waits for the colony's next run (spec 03 §8)") {
    Duel d;
    const ObjectId home = d.w.planet(d.a, {6, 6});
    d.w.colony(home, kA, 1000);
    resumeTurnBased(d.r(), d.s());
    REQUIRE(d.s().playerTurn.empire == kA);
    // An order already in the colony's list (as a minister might leave it).
    Order launch;
    launch.kind = OrderKind::LaunchUnits;
    launch.amount = -1;
    d.s().colony(home)->orders = {launch};
    Order use;
    use.kind = OrderKind::UseFacility;
    use.amount = 0;
    cmd::SetOrders c;
    c.planet = home;
    c.orders = {launch, use};
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, c)));
    // Cleared first; the immediate run covers vehicle lists only, so it stays.
    CHECK(d.s().colony(home)->orders == std::vector<Order>{use});
    // At the start of the owner's next turn the colony's list runs: the order
    // completes with no effect.
    endPlayerTurn(d.r(), d.s(), kA);
    endPlayerTurn(d.r(), d.s(), kB);
    REQUIRE(d.s().playerTurn.empire == kA);
    CHECK(d.s().colony(home)->orders.empty());
    // A vehicle cannot be given it.
    const VehicleId ship = d.w.spawn(d.w.ship(kA, "Ship", 1), at(d.a, 6, 6));
    CHECK(hasRejection(applyLive(d.r(), d.s(), kA, ordersFor(ship, {use}))));
}

TEST_CASE("turn-based: Abandon Planet runs the colony's list at once, without a log entry (spec 02 §5)") {
    Duel d;
    const ObjectId home = d.w.planet(d.a, {6, 6});
    d.w.colony(home, kA, 10, {"Mv Trainer"});
    resumeTurnBased(d.r(), d.s());
    REQUIRE(d.s().playerTurn.empire == kA);
    d.s().empire(kA).log.clear();
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, cmd::AbandonPlanet{home})));
    // Facilities kept: the planet stays ours, empty, and its list is done.
    REQUIRE(d.s().colony(home));
    CHECK(d.s().colony(home)->totalPopulation() == 0);
    CHECK(d.s().colony(home)->orders.empty());
    CHECK_FALSE(d.w.logged(kA, "abandoned"));
    // Scrapped first: the colony goes at once.
    const ObjectId other = d.w.planet(d.a, {2, 2});
    d.w.colony(other, kA, 10, {"Mv Trainer"});
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, cmd::AbandonPlanet{other, true})));
    CHECK_FALSE(d.s().colony(other));
}

// ---- Tagged vehicles (spec 03 §8 "Tagged vehicles", §19 Q82) ----------------------------------------

namespace {

bool inBattle(const CombatRecord& c, VehicleId v) {
    return std::any_of(c.pieces.begin(), c.pieces.end(), [&](const CombatPiece& p) { return p.vehicle == v; });
}

} // namespace

TEST_CASE("turn-based: tagged vehicles of different speeds and a fleet move as one group, asked once, into one battle") {
    Duel d;
    // Two ships in no fleet, of speeds 4 and 3, and a fleet of two, all at (0, 6).
    const VehicleId fast = d.w.spawn(d.w.ship(kA, "Fast", 4), at(d.a, 0, 6));
    const VehicleId slow = d.w.spawn(d.w.ship(kA, "Slow", 3), at(d.a, 0, 6));
    const VehicleId lead = d.w.spawn(d.w.ship(kA, "Lead", 3), at(d.a, 0, 6));
    const VehicleId mate = d.w.spawn(d.w.ship(kA, "Mate", 3), at(d.a, 0, 6));
    for (VehicleId v : {fast, slow, lead, mate}) fuel(d.w, v);
    REQUIRE(apply(d.r(), d.s(), kA, cmd::CreateFleet{"Pack", {lead, mate}}).ok);
    const VehicleId picket = d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 3, 6));
    resumeTurnBased(d.r(), d.s());
    REQUIRE(d.s().playerTurn.empire == kA);

    // Tagged in this order: the fast ship first (it acts), the fleet by one member.
    Order attack;
    attack.kind = OrderKind::Attack;
    attack.location = at(d.a, 3, 6);
    attack.vehicle = picket;
    const TurnResult res = applyLive(d.r(), d.s(), kA, cmd::OrderTagged{{fast, mate, slow}, {attack}});
    CHECK_FALSE(hasRejection(res));
    // Every tagged list holds the order, the fleet's members each a copy.
    for (VehicleId v : {fast, slow, lead, mate}) CHECK(d.w.v(v).orders == std::vector<Order>{attack});
    // They step together and stop before the picket's sector: one question for the group.
    REQUIRE(res.questions.size() == 1);
    const std::vector<VehicleId> group{fast, lead, mate, slow};
    CHECK(res.questions[0] == EntryQuestion{{}, {}, at(d.a, 3, 6), group});
    for (VehicleId v : {fast, slow, lead, mate}) CHECK(d.w.v(v).location == at(d.a, 2, 6));
    CHECK(d.w.v(slow).movement == 1);
    CHECK(d.s().combats.empty());

    // Entering takes them all into one battle; the failed order clears every list.
    const TurnResult in = applyLive(d.r(), d.s(), kA, cmd::EnterSector{{}, {}, at(d.a, 3, 6), true, group});
    CHECK_FALSE(hasRejection(in));
    CHECK(in.questions.empty());
    CHECK(d.s().playerTurn.questions.empty());
    REQUIRE(d.s().combats.size() == 1);
    for (VehicleId v : group) {
        CAPTURE(v.value);
        CHECK(inBattle(d.s().combats[0], v));
        if (const Vehicle* x = d.s().vehicle(v)) {
            CHECK(x->location == at(d.a, 3, 6));
            CHECK(x->orders.empty());
        }
    }
}

TEST_CASE("turn-based: a tagged group waits for its slowest member; what is left runs vehicle by vehicle next turn") {
    Duel d;
    const VehicleId fast = d.w.spawn(d.w.ship(kA, "Fast", 4), at(d.a, 0, 0));
    const VehicleId slow = d.w.spawn(d.w.ship(kA, "Slow", 3), at(d.a, 0, 0));
    fuel(d.w, fast);
    fuel(d.w, slow);
    resumeTurnBased(d.r(), d.s());
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, cmd::OrderTagged{{slow, fast}, {moveTo(d.a, 9, 0)}})));
    // Together while both have movement: three steps.
    CHECK(d.w.v(fast).location == at(d.a, 3, 0));
    CHECK(d.w.v(slow).location == at(d.a, 3, 0));
    CHECK(d.w.v(fast).movement == 1);
    CHECK(d.w.v(slow).movement == 0);
    // A second order while one of them has no movement left: the group waits,
    // though the fast ship could still step.
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, cmd::OrderTagged{{fast, slow}, {moveTo(d.a, 3, 9)}})));
    CHECK(d.w.v(fast).location == at(d.a, 3, 0));
    CHECK(d.w.v(fast).orders.size() == 2);
    CHECK(d.w.v(slow).orders.size() == 2);
    // At the next turn's start each carries its list out alone, at its own speed.
    endPlayerTurn(d.r(), d.s(), kA);
    endPlayerTurn(d.r(), d.s(), kB);
    REQUIRE(d.s().playerTurn.empire == kA);
    CHECK(d.w.v(fast).location == at(d.a, 7, 0));
    CHECK(d.w.v(slow).location == at(d.a, 6, 0));
}

TEST_CASE("turn-based: a tagged group's completed order leaves every tagged list, and a failure clears them all") {
    Duel d;
    const SystemId far = d.w.system("Far", 20, 20);   // no link: no route there
    const VehicleId first = d.w.spawn(d.w.ship(kA, "First", 3), at(d.a, 0, 0));
    const VehicleId second = d.w.spawn(d.w.ship(kA, "Second", 3), at(d.a, 0, 0));
    fuel(d.w, first);
    fuel(d.w, second);
    resumeTurnBased(d.r(), d.s());
    // The second already has an order of its own; the group acts through the
    // first one tagged and carries out its list.
    d.w.v(second).orders = {moveTo(d.a, 0, 9)};
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, cmd::OrderTagged{{first, second}, {moveTo(d.a, 1, 1)}})));
    CHECK(d.w.v(first).location == at(d.a, 1, 1));
    CHECK(d.w.v(second).location == at(d.a, 1, 1));
    CHECK(d.w.v(first).orders.empty());
    // The completed order took the entry at the head of each list.
    CHECK(d.w.v(second).orders == std::vector<Order>{moveTo(d.a, 1, 1)});

    // A failure (no route) clears every tagged list, earlier orders included.
    d.w.v(second).orders = {moveTo(d.a, 0, 9)};
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, cmd::OrderTagged{{first, second}, {moveTo(far, 5, 5)}})));
    CHECK(d.w.v(first).orders.empty());
    CHECK(d.w.v(second).orders.empty());
    CHECK(d.w.v(first).location == at(d.a, 1, 1));
    CHECK(d.w.v(second).location == at(d.a, 1, 1));
}

TEST_CASE("turn-based: declining a tagged group's question clears every tagged list; tagged orders are refused across sectors") {
    Duel d;
    const VehicleId one = d.w.spawn(d.w.ship(kA, "One", 3), at(d.a, 0, 6));
    const VehicleId two = d.w.spawn(d.w.ship(kA, "Two", 3), at(d.a, 0, 6));
    const VehicleId away = d.w.spawn(d.w.ship(kA, "Away", 3), at(d.a, 0, 0));
    for (VehicleId v : {one, two, away}) fuel(d.w, v);
    d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 1, 6));
    resumeTurnBased(d.r(), d.s());
    CHECK(hasRejection(applyLive(d.r(), d.s(), kA, cmd::OrderTagged{{one, away}, {moveTo(d.a, 1, 6)}})));
    CHECK(d.w.v(one).orders.empty());
    CHECK(d.w.v(away).orders.empty());
    CHECK(hasRejection(applyLive(d.r(), d.s(), kB, cmd::OrderTagged{{one}, {moveTo(d.a, 1, 6)}})));

    const TurnResult res = applyLive(d.r(), d.s(), kA, cmd::OrderTagged{{one, two}, {moveTo(d.a, 1, 6), moveTo(d.a, 5, 6)}});
    REQUIRE(res.questions.size() == 1);
    CHECK(res.questions[0].tagged == std::vector<VehicleId>{one, two});
    // Saved with the game: a loaded game asks it again.
    auto copy = deserializeState(serializeState(d.s()));
    REQUIRE(copy.has_value());
    CHECK(copy->playerTurn.questions == res.questions);
    applyLive(d.r(), d.s(), kA, cmd::EnterSector{{}, {}, at(d.a, 1, 6), false, {one, two}});
    CHECK(d.s().playerTurn.questions.empty());
    CHECK(d.w.v(one).orders.empty());
    CHECK(d.w.v(two).orders.empty());
    CHECK(d.w.v(one).location == at(d.a, 0, 6));
    CHECK(d.s().combats.empty());
    CHECK(d.w.logged(kA, " and 1 other: orders cancelled"));
}

TEST_CASE("simultaneous: an order to tagged vehicles is only appended to each list") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId one = w.spawn(w.ship(kA, "One", 3), at(a, 0, 0));
    const VehicleId two = w.spawn(w.ship(kA, "Two", 2), at(a, 0, 0));
    w.v(two).orders = {moveTo(a, 0, 9)};
    const TurnResult res = applyLive(w.rules(), w.s, kA, cmd::OrderTagged{{one, two}, {moveTo(a, 5, 5)}});
    CHECK_FALSE(hasRejection(res));
    CHECK(w.v(one).orders == std::vector<Order>{moveTo(a, 5, 5)});
    CHECK(w.v(two).orders == std::vector<Order>{moveTo(a, 0, 9), moveTo(a, 5, 5)});
    CHECK(w.v(one).location == at(a, 0, 0));
    // Set Patrol's: Repeat on in the first tagged vehicle's list alone (spec 03 §8, confirmed: binary).
    CHECK_FALSE(hasRejection(applyLive(w.rules(), w.s, kA, cmd::OrderTagged{{one, two}, {moveTo(a, 1, 1), moveTo(a, 2, 2)}, true})));
    CHECK(w.v(one).repeatOrders);
    CHECK_FALSE(w.v(two).repeatOrders);
    CHECK(w.v(one).orders.size() == 3);
    CHECK(w.v(two).orders.size() == 4);
    // Repeat Orders with vehicles tagged switches the first one's alone, and touches no list.
    CHECK_FALSE(hasRejection(applyLive(w.rules(), w.s, kA, cmd::OrderTagged{{one, two}, {}, false, true})));
    CHECK_FALSE(w.v(one).repeatOrders);
    CHECK_FALSE(w.v(two).repeatOrders);
    CHECK(w.v(one).orders.size() == 3);
    CHECK(w.v(two).orders.size() == 4);
    CHECK_FALSE(hasRejection(applyLive(w.rules(), w.s, kA, cmd::OrderTagged{{two, one}, {}, false, true})));
    CHECK(w.v(two).repeatOrders);
    CHECK_FALSE(w.v(one).repeatOrders);
    // No orders and no Repeat to switch: nothing changes (in a turn-based game the group runs).
    CHECK_FALSE(hasRejection(applyLive(w.rules(), w.s, kA, cmd::OrderTagged{{one, two}, {}})));
    CHECK(w.v(one).orders.size() == 3);
}

TEST_CASE("turn-based: orders given held run only at the list's next run") {
    // Set Patrol's points while the route is picked (spec 03 §8, confirmed: binary).
    Duel d;
    resumeTurnBased(d.r(), d.s());
    const Location start = d.w.v(d.runner).location;
    cmd::SetOrders held{d.runner, {}, {moveTo(d.a, 9, 0)}, false};
    held.hold = true;
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, held)));
    CHECK(d.w.v(d.runner).location == start);
    REQUIRE(d.w.v(d.runner).orders.size() == 1);
    // The next order given runs the whole list.
    CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, cmd::SetOrders{d.runner, {}, {moveTo(d.a, 9, 0), moveTo(d.a, 9, 1)}, false})));
    CHECK(d.w.v(d.runner).location != start);
}

TEST_CASE("turn-based: tagged orders and a fleet's new leader replay from an order file as they were given") {
    auto play = [](bool fromFile) {
        Duel d;
        const VehicleId lead = d.w.spawn(d.w.ship(kA, "Lead", 3), at(d.a, 0, 0));
        const VehicleId mate = d.w.spawn(d.w.ship(kA, "Mate", 3), at(d.a, 0, 0));
        const VehicleId solo = d.w.spawn(d.w.ship(kA, "Solo", 4), at(d.a, 0, 0));
        for (VehicleId v : {lead, mate, solo}) fuel(d.w, v);
        REQUIRE(apply(d.r(), d.s(), kA, cmd::CreateFleet{"Pack", {lead, mate}}).ok);
        const FleetId fleet = d.s().fleets.back().id;
        resumeTurnBased(d.r(), d.s());
        EmpireOrders orders{kA, d.s().turn, {cmd::SetFleetLeader{fleet, mate}, cmd::OrderTagged{{solo, lead}, {moveTo(d.a, 5, 0)}}}};
        if (fromFile) {
            auto loaded = deserializeOrders(serializeOrders(orders));
            REQUIRE(loaded.has_value());
            orders = std::move(*loaded);
        }
        for (const Command& c : orders.commands) CHECK_FALSE(hasRejection(applyLive(d.r(), d.s(), kA, c)));
        CHECK(d.s().fleet(fleet)->leader == mate);
        for (VehicleId v : {lead, mate, solo}) CHECK(d.w.v(v).location == at(d.a, 3, 0));
        return stateChecksum(d.s());
    };
    CHECK(play(false) == play(true));
}

TEST_CASE("fleets: a fleet's leader is set by its owner, to one of its members at its location") {
    Duel d;
    const VehicleId lead = d.w.spawn(d.w.ship(kA, "Lead", 3), at(d.a, 0, 0));
    const VehicleId mate = d.w.spawn(d.w.ship(kA, "Mate", 3), at(d.a, 0, 0));
    const VehicleId loner = d.w.spawn(d.w.ship(kA, "Loner", 3), at(d.a, 0, 0));
    const VehicleId theirs = d.w.spawn(d.w.ship(kB, "Theirs", 3), at(d.a, 0, 0));
    REQUIRE(apply(d.r(), d.s(), kA, cmd::CreateFleet{"Pack", {lead, mate}}).ok);
    const FleetId fleet = d.s().fleets.back().id;
    CHECK(fleetLeader(d.s(), *d.s().fleet(fleet))->id == lead);
    CHECK(apply(d.r(), d.s(), kA, cmd::SetFleetLeader{fleet, mate}).ok);
    CHECK(fleetLeader(d.s(), *d.s().fleet(fleet))->id == mate);
    // Saved with the game.
    auto copy = deserializeState(serializeState(d.s()));
    REQUIRE(copy.has_value());
    CHECK(copy->fleet(fleet)->leader == mate);
    // Refused: a ship of no fleet, another empire's ship, another empire's fleet.
    CHECK_FALSE(apply(d.r(), d.s(), kA, cmd::SetFleetLeader{fleet, loner}).ok);
    CHECK_FALSE(apply(d.r(), d.s(), kA, cmd::SetFleetLeader{fleet, theirs}).ok);
    CHECK_FALSE(apply(d.r(), d.s(), kB, cmd::SetFleetLeader{fleet, lead}).ok);
    CHECK(d.s().fleet(fleet)->leader == mate);
    // The chosen leader leaving: the first member leads again (spec 03 §9).
    REQUIRE(apply(d.r(), d.s(), kA, cmd::LeaveFleet{mate}).ok);
    CHECK(fleetLeader(d.s(), *d.s().fleet(fleet))->id == lead);
}

// ---- Movement after a battle (GitHub issue 13) -------------------------------------------------

namespace {

// The movement fixture plus an engine cutter: a weapon that hits engines only,
// always hits, takes 4 engines a shot and reloads too slowly to fire twice in
// a battle. So a battle between two ships that carry one each takes 4 engines
// from each and nothing else.
const Rules& cutterRules() {
    static const Rules r{[] {
        ruleset::Ruleset rs = buildRuleset();
        ruleset::Component c;
        c.name = "Mv Engine Cutter";
        c.tonnage = 10;
        c.structure = 10;
        c.cost = {10, 0, 0};
        c.vehicles = ruleset::maskOf(VehicleType::Ship);
        c.abilities = {ab(AbilityKind::WeaponsAlwaysHit)};
        c.generalGroup = "Weapons";
        c.weapon.kind = ruleset::WeaponKind::DirectFire;
        c.weapon.damageAtRange.assign(20, 40);
        c.weapon.damageType = "Only Engines";
        c.weapon.reloadRate = 100;
        c.weapon.targets = {"Ships"};
        rs.components.push_back(std::move(c));
        rs.reindex();
        return rs;
    }()};
    return r;
}

// Two human empires in one system, turn-based: A's Raider and B's Picket, six
// engines and a cutter each, two squares apart; B keeps a ship far away.
struct Cutters {
    World w{cutterRules()};
    SystemId a;
    VehicleId raider, picket;

    Cutters() {
        a = w.system("A");
        w.s.options.simultaneous = false;
        raider = w.spawn(w.ship(kA, "Raider", 6, {"Mv Engine Cutter"}), at(a, 0, 6));
        picket = w.spawn(w.ship(kB, "Picket", 6, {"Mv Engine Cutter"}), at(a, 2, 6));
        fuel(w, raider);
        fuel(w, picket);
        w.spawn(w.ship(kB, "Keeper", 1), at(a, 12, 12));
        w.exploreAll(kA);
        w.exploreAll(kB);
    }
    const Rules& r() const { return w.rules(); }
    GameState& s() { return w.s; }
    int max(VehicleId id) { return vehicleMaxMovement(r(), s(), w.v(id)); }
    int enginesLost(VehicleId id) {
        int n = 0;
        const Design& d = s().design(w.v(id).design);
        for (size_t i = 0; i < d.entries.size(); ++i)
            if (r().component(d.entries[i].component).name == "Mv Engine" && !entryIntact(r(), s(), w.v(id), i)) ++n;
        return n;
    }
};

} // namespace

TEST_CASE("turn-based: engines lost in a battle cap the movement left at once; the steps made do not count (issue 13)") {
    // Spec 03 §6.1, §6.4, §19 Q85 (confirmed: binary): the movement left
    // becomes min(left, new maximum) as the engines go, for the ship that
    // moved in, the one that was there and a ship hit in another empire's
    // turn alike; the owner's next turn refills it to the damaged maximum.
    Cutters c;
    resumeTurnBased(c.r(), c.s());
    REQUIRE(c.s().playerTurn.empire == kA);
    REQUIRE(c.w.v(c.raider).movement == 6);
    c.w.v(c.picket).movement = 6;   // as B's turn left it: it never moved

    // Two steps, the second into the Picket's sector, asked first; the battle on
    // entry takes four engines from each ship.
    const TurnResult res = applyLive(c.r(), c.s(), kA, ordersFor(c.raider, {moveTo(c.a, 2, 6)}));
    REQUIRE(res.questions.size() == 1);
    applyLive(c.r(), c.s(), kA, cmd::EnterSector{c.raider, {}, at(c.a, 2, 6), true});
    REQUIRE(c.s().combats.size() == 1);
    REQUIRE(c.enginesLost(c.raider) == 4);
    REQUIRE(c.enginesLost(c.picket) == 4);
    CHECK(c.w.v(c.raider).location == at(c.a, 2, 6));
    CHECK(c.w.v(c.raider).orders.empty());   // the battle on entry fails the order (§6.4)
    CHECK(c.max(c.raider) == 2);
    CHECK(c.max(c.picket) == 2);
    // "4 / 2" and "6 / 2" before 2026-10-14; now the smaller of the two.
    CHECK(c.w.v(c.raider).movement == 2);
    CHECK(c.w.v(c.picket).movement == 2);

    // Ordered on at once, the Raider uses its 2 points now (it used to get 0
    // and wait: 2 steps already made against the new maximum of 2); the rest
    // of the way waits for its next turn.
    applyLive(c.r(), c.s(), kA, ordersFor(c.raider, {moveTo(c.a, 8, 6)}));
    CHECK(c.w.v(c.raider).location == at(c.a, 4, 6));
    CHECK(c.w.v(c.raider).movement == 0);
    CHECK(c.w.v(c.raider).orders.size() == 1);

    // B's turn: the Picket, hit in A's turn, starts it with its damaged maximum.
    endPlayerTurn(c.r(), c.s(), kA);
    REQUIRE(activePlayer(c.s()) == kB);
    CHECK(c.w.v(c.picket).movement == 2);
    applyLive(c.r(), c.s(), kB, ordersFor(c.picket, {moveTo(c.a, 2, 0)}));
    CHECK(c.w.v(c.picket).location == at(c.a, 2, 4));
    // A's next turn: the Raider gets 2 and goes on with its order.
    endPlayerTurn(c.r(), c.s(), kB);
    REQUIRE(activePlayer(c.s()) == kA);
    CHECK(c.w.v(c.raider).location == at(c.a, 6, 6));
    CHECK(c.w.v(c.raider).movement == 0);
}

TEST_CASE("turn-based: Emergency Energy above the maximum lasts until the maximum is recomputed (spec 03 §6.1)") {
    for (bool depot : {false, true}) {
        CAPTURE(depot);
        Duel d;
        resumeTurnBased(d.r(), d.s());
        const VehicleId dasher = d.w.spawn(d.w.ship(kA, "Dasher", 3, {"Mv Energy Cell"}), at(d.a, 0, 0));
        fuel(d.w, dasher);
        if (depot) d.w.colony(d.w.object(d.a, ObjectKind::Planet, Sector{1, 0}, "Depot World"), kA, 1000, {"Mv Depot"});
        {
            TurnContext ctx{d.r(), d.s(), {}, {}, {}};
            movement::startTurn(ctx, kA);  // the new ship gets its movement
        }
        Order use;
        use.kind = OrderKind::UseComponent;
        use.amount = 7;
        applyLive(d.r(), d.s(), kA, ordersFor(dasher, {use}));
        REQUIRE(d.w.v(dasher).movement == 7);   // 3 + 4, without a ceiling
        applyLive(d.r(), d.s(), kA, ordersFor(dasher, {moveTo(d.a, 12, 0)}));
        // Plain steps recompute nothing: all 7 are used. A step that resupplies
        // at a depot recomputes the maximum, and what was left above it (6 after
        // the first step) goes: 3 more steps.
        CHECK(d.w.v(dasher).location == at(d.a, depot ? 4 : 7, 0));
        CHECK(d.w.v(dasher).movement == 0);
    }
}
