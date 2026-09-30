// Scores, statistics history, eliminations and victory (docs/spec/05 §5-6).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/diplomacy.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

namespace {

const EmpireId kA{0u}, kB{1u}, kC{2u};

TurnContext context(GameState& s) { return turnContext(politicsRules(), s); }

void wipeOut(GameState& s, EmpireId e) {
    for (auto& c : s.colonies)
        if (c && c->owner == e) c.reset();
    for (Vehicle& v : s.vehicles)
        if (v.owner == e) v.count = 0;
    s.removeDeadVehicles();
}

// Makes `e` the clear leader by a large production figure.
void boost(GameState& s, EmpireId e, int64_t production) { s.empire(e).economy.colonies = {production, 0, 0}; }

} // namespace

TEST_CASE("score: a weighted sum of the statistics") {
    TurnStats t;
    t.production = {1000, 500, 500};
    t.research = 1000;
    t.intelligence = 500;
    t.techLevels = 20;
    t.systems = 2;
    t.planets = 3;
    t.population = 2500;
    t.units = 10;
    t.ships = 4;
    t.bases = 1;
    const score::Weights w;
    CHECK(score::scoreOf(t, w) == 200 + 100 + 50 + 2000 + 200 + 600 + 2500 + 50 + 200 + 50);
    t.research = -100;  // nonsense input never lowers a score
    CHECK(score::scoreOf(t, w) == 200 + 50 + 2000 + 200 + 600 + 2500 + 50 + 200 + 50);

    ruleset::Ruleset rs = buildPoliticsRuleset();
    rs.settings.set("Score Weight Planets", "1000000");
    const Rules custom{std::move(rs)};
    CHECK(score::weights(custom).planets == 1000000);
    CHECK(score::weights(custom).ships == w.ships);
}

TEST_CASE("score: current statistics") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    Empire& a = s.empire(kA);
    a.economy.colonies = {1000, 2000, 3000};
    a.economy.trade = {10, 0, 0};
    a.economy.tariffsIn = {0, 20, 0};
    a.economy.remoteMining = {};
    a.economy.otherIncome = {};
    a.economy.research = 700;
    a.economy.intelligence = 300;
    Colony& home = homeworld(s, kA);
    const Location here = locationOf(s.galaxy, home.planet);
    const DesignId station = addTestDesign(s, r, kA, "Keep", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters"});
    addTestVehicle(s, r, station, here);
    const DesignId wasp = addTestDesign(s, r, kA, "Wasp", "Test Fighter Hull", {"Test Fighter Gun", "Test Fighter Engine"});
    home.cargo.units.push_back({wasp, 5});
    for (Vehicle& v : s.vehicles)
        if (v.owner == kA) {
            v.status = VehicleStatus::Mothballed;  // not counted as a ship
            break;
        }

    const TurnStats t = score::currentStats(r, s, kA);
    CHECK(t.turn == s.turn);
    CHECK(t.production == Resources{1010, 2020, 3000});
    CHECK(t.research == 700);
    CHECK(t.intelligence == 300);
    CHECK(t.techLevels == research::totalLevels(a));
    CHECK(t.planets == 1);
    CHECK(t.systems == 1);
    CHECK(t.population == home.totalPopulation());
    CHECK(t.ships == 2);
    CHECK(t.bases == 1);
    CHECK(t.units == 5);
    CHECK(t.score == score::scoreOf(t, score::weights(r)));
    CHECK(score::empireScore(r, s, kA) == t.score);
}

TEST_CASE("score: ranking and score visibility") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    boost(s, kB, 1000000);
    boost(s, kC, 500000);
    CHECK(score::ranking(r, s) == std::vector<EmpireId>{kB, kC, kA});
    s.empire(kB).alive = false;
    CHECK(score::ranking(r, s) == std::vector<EmpireId>{kC, kA});

    CHECK(score::scoreVisible(s, kA, kA));
    CHECK_FALSE(score::scoreVisible(s, kA, kC));
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    diplomacy::setTreaty(ctx, kA, kC, Treaty::MilitaryAlliance);
    CHECK(score::scoreVisible(s, kA, kC));
    s.options.showAllScores = true;
    CHECK(score::scoreVisible(s, kA, kB));
}

TEST_CASE("score: end of turn records history and eliminates beaten empires") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    TurnContext ctx = context(s);
    score::endOfTurn(ctx);
    for (const Empire& e : s.empires) {
        REQUIRE(e.history.size() == 1);
        CHECK(e.history[0].turn == 0);
        CHECK(e.history[0].score == score::empireScore(r, s, e.id));
    }

    // C keeps only an empty colony and some units: that is defeat.
    Colony& homeC = homeworld(s, kC);
    homeC.population.clear();
    const DesignId wasp = addTestDesign(s, r, kC, "Wasp", "Test Fighter Hull", {"Test Fighter Gun", "Test Fighter Engine"});
    for (Vehicle& v : s.vehicles)
        if (v.owner == kC) v.count = 0;
    s.removeDeadVehicles();
    addTestVehicle(s, r, wasp, locationOf(s.galaxy, homeC.planet));
    CHECK(score::defeated(r, s, kC));
    CHECK_FALSE(score::defeated(r, s, kA));
    const ObjectId planetC = homeC.planet;
    s.turn = 1;
    score::endOfTurn(ctx);
    CHECK_FALSE(s.empire(kC).alive);
    CHECK(s.empire(kC).history.size() == 2);  // its last turn is still recorded...
    CHECK(s.colony(planetC) == nullptr);
    for (const Vehicle& v : s.vehicles) CHECK(v.owner != kC);
    CHECK(hasLog(s, kA, "Empire Destroyed"));
    CHECK(hasLog(s, kC, "Empire Destroyed"));
    CHECK_FALSE(s.gameOver);  // two empires remain
    s.turn = 2;
    score::endOfTurn(ctx);
    CHECK(s.empire(kC).history.size() == 2);  // ...but nothing after its death
    CHECK(s.empire(kA).history.size() == 3);
}

TEST_CASE("score: the last empire standing wins") {
    GameState s = newPoliticsGame();
    TurnContext ctx = context(s);
    wipeOut(s, kB);
    wipeOut(s, kC);
    score::endOfTurn(ctx);
    CHECK(s.gameOver);
    CHECK(s.winner == kA);
    CHECK(hasLog(s, kA, "Game Over"));

    // A game that started with one empire does not end by itself.
    GameState solo = newPoliticsGame(5, 1, 6);
    TurnContext soloCtx = context(solo);
    score::endOfTurn(soloCtx);
    CHECK_FALSE(solo.gameOver);
}

TEST_CASE("score: victory conditions") {
    const Rules& r = politicsRules();
    auto fresh = [] {
        GameState s = newPoliticsGame();
        boost(s, kB, 1000000);
        return s;
    };
    auto end = [](GameState& s) {
        TurnContext ctx = context(s);
        score::endOfTurn(ctx);
    };

    SUBCASE("score threshold") {
        GameState s = fresh();
        s.options.victory.score = true;
        s.options.victory.scoreValue = score::empireScore(r, s, kB) + 1;
        end(s);
        CHECK_FALSE(s.gameOver);
        boost(s, kB, 2000000);
        end(s);
        CHECK(s.gameOver);
        CHECK(s.winner == kB);
    }
    SUBCASE("percent of the second place") {
        GameState s = fresh();
        s.options.victory.percentOfSecond = true;
        s.options.victory.percentOfSecondValue = 200;
        boost(s, kC, 900000);
        end(s);
        CHECK_FALSE(s.gameOver);
        boost(s, kC, 0);
        end(s);
        CHECK(s.gameOver);
        CHECK(s.winner == kB);
    }
    SUBCASE("technology percentage") {
        GameState s = fresh();
        s.options.victory.techPercent = true;
        s.options.victory.techPercentValue = 75;
        end(s);
        CHECK_FALSE(s.gameOver);
        for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) s.empire(kC).techLevels[i] = r.data().techAreas[i].maxLevel;
        end(s);
        CHECK(s.winner == kC);
    }
    SUBCASE("years elapsed") {
        GameState s = fresh();
        s.options.victory.years = true;
        s.options.victory.yearsValue = 2;
        s.turn = 18;
        end(s);
        CHECK_FALSE(s.gameOver);
        s.turn = 19;  // the 20th turn: two years
        end(s);
        CHECK(s.gameOver);
        CHECK(s.winner == kB);
    }
    SUBCASE("peace") {
        GameState s = fresh();
        s.options.victory.peace = true;
        s.options.victory.peaceYears = 1;
        setContact(s, kA, kC);
        TurnContext ctx = context(s);
        diplomacy::setTreaty(ctx, kA, kC, Treaty::War);
        for (int i = 0; i < 12; ++i) end(s);
        CHECK(s.peacefulTurns == 0);
        CHECK_FALSE(s.gameOver);
        diplomacy::setTreaty(ctx, kA, kC, Treaty::None);
        for (int i = 0; i < 9; ++i) end(s);
        CHECK(s.peacefulTurns == 9);
        CHECK_FALSE(s.gameOver);
        end(s);
        CHECK(s.gameOver);
        CHECK(s.winner == kB);
    }
    SUBCASE("delay suppresses the checks") {
        GameState s = fresh();
        s.options.victory.score = true;
        s.options.victory.scoreValue = 1;
        s.options.victory.delay = true;
        s.options.victory.delayYears = 1;
        s.turn = 8;
        end(s);
        CHECK_FALSE(s.gameOver);
        s.turn = 9;
        end(s);
        CHECK(s.gameOver);
        CHECK(s.winner == kB);
    }
}

TEST_CASE("score: the turn pipeline keeps history and stops at game over") {
    const Rules& r = politicsRules();
    auto play = [&] {
        GameState s = newPoliticsGame(8);
        std::vector<EmpireOrders> none;
        for (int t = 0; t < 4; ++t) processTurn(r, s, none);
        return s;
    };
    GameState a = play();
    GameState b = play();
    CHECK(a.turn == 4);
    for (size_t i = 0; i < a.empires.size(); ++i) {
        REQUIRE(a.empires[i].history.size() == 4);
        CHECK(a.empires[i].history.back().turn == 3);
        for (size_t t = 0; t < 4; ++t) CHECK(a.empires[i].history[t].score == b.empires[i].history[t].score);
    }
    CHECK(a.rng == b.rng);

    a.gameOver = true;
    std::vector<EmpireOrders> none;
    processTurn(r, a, none);
    CHECK(a.turn == 4);
}
