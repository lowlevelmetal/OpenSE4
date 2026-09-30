// The turn-based game style (spec 05 §8 "Turn-based game", spec 03 §6.3
// "Turn-based", spec 04 §2): players in sequence, orders carried out as
// they are given, end-of-turn processing per player, the once-per-game-turn
// steps after the last player.

#include "movement_fixture.hpp"

#include "game/commands.hpp"
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

TEST_CASE("turn-based: a human is asked before entering a sector with enemies; entering fights at once and the order fails") {
    for (bool enter : {true, false}) {
        CAPTURE(enter);
        Duel d;
        const VehicleId picket = d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 1, 6));
        resumeTurnBased(d.r(), d.s());
        REQUIRE(d.s().playerTurn.empire == kA);

        const TurnResult res = applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 6, 6)}));
        REQUIRE(res.questions.size() == 1);
        CHECK(res.questions[0] == EntryQuestion{d.runner, {}, at(d.a, 1, 6)});
        CHECK(d.w.v(d.runner).location == at(d.a, 0, 6));  // stopped before the sector
        CHECK(d.w.v(d.runner).movement == 3);
        CHECK(d.w.v(d.runner).orders.size() == 1);
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

TEST_CASE("turn-based: orders given in advance enter without asking and fight on entry") {
    Duel d;
    d.w.spawn(d.w.ship(kB, "Picket", 1, {"Test Laser"}), at(d.a, 1, 6));
    // processTurn plays the whole game turn: A's orders are carried out in A's turn.
    const EmpireOrders orders{kA, 0, {ordersFor(d.runner, {moveTo(d.a, 6, 6)})}};
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
    d.s().options.victory.yearsValue = 0;  // met at the first victory check
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
    // After the last player: the date advanced and the victory check ran.
    CHECK(d.s().turn == 1);
    CHECK(d.s().gameOver);
    CHECK(activePlayer(d.s()) == EmpireId{});
    CHECK(hasRejection(applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {}))));
}

TEST_CASE("turn-based: an empire with nothing left is destroyed when its turn comes up, and the turn passes on") {
    Duel d;
    for (Vehicle& v : d.s().vehicles)
        if (v.owner == kB) v.count = 0;
    d.s().removeDeadVehicles();
    resumeTurnBased(d.r(), d.s());
    CHECK(d.s().empire(kB).alive);  // not before its own turn
    endPlayerTurn(d.r(), d.s(), kA);
    CHECK_FALSE(d.s().empire(kB).alive);
    // B had no turn: the game turn ended and A plays again, the last empire standing.
    CHECK(d.s().turn == 1);
    CHECK(activePlayer(d.s()) == kA);
    CHECK(d.w.logged(kA, "Last Empire Standing"));
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

    // Mid-turn records survive too: the steps a ship made in the turn in progress.
    applyLive(d.r(), d.s(), kA, ordersFor(d.runner, {moveTo(d.a, 12, 12)}));
    auto again = deserializeState(serializeState(d.s()));
    REQUIRE(again.has_value());
    CHECK(again->playerTurn.moves == d.s().playerTurn.moves);
    CHECK_FALSE(again->playerTurn.moves.empty());
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
                endPlayerTurn(r, s, EmpireId{0u});
            }
        }
        return s;
    };
    for (bool viaProcessTurn : {false, true}) {
        CAPTURE(viaProcessTurn);
        const GameState a = play(viaProcessTurn);
        const GameState b = play(viaProcessTurn);
        CHECK(a.turn == 6 + (viaProcessTurn ? 0 : 0));
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
