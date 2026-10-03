// Scores, statistics history, destruction and victory (docs/spec/05 §5–§6,
// spec 01 §11).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/diplomacy.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "game/xmath.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

namespace {

const EmpireId kA{0u}, kB{1u}, kC{2u};

TurnContext context(GameState& s) { return turnContext(politicsRules(), s); }

// The score parts of the turn as processTurn runs them: each empire's
// statistics row (step 2 of its end-of-turn processing) and destruction check
// (right after its processing), then the victory check (spec 05 §8).
void scoreSteps(TurnContext& ctx) {
    GameState& s = ctx.state;
    for (size_t i = 0; i < s.empires.size(); ++i) {
        if (!s.empires[i].alive) continue;
        score::recordStatistics(ctx, EmpireId{i});
        score::checkDestruction(ctx, EmpireId{i});
    }
    score::checkVictory(ctx, s.turn + 1);
}

void wipeOut(GameState& s, EmpireId e) {
    for (auto& c : s.colonies)
        if (c && c->owner == e) c.reset();
    for (Vehicle& v : s.vehicles)
        if (v.owner == e) v.count = 0;
    s.removeDeadVehicles();
}

// Adds `n` bases of 500 kT at the empire's home: +5,000 score each.
void boost(GameState& s, EmpireId e, int n) {
    const Rules& r = politicsRules();
    const DesignId keep = addTestDesign(s, r, e, "Keep", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters"});
    for (int i = 0; i < n; ++i) addTestVehicle(s, r, keep, locationOf(s.galaxy, homeworld(s, e).planet));
}

// Every empire at peace with every other: contact and Non-Aggression.
void makePeace(GameState& s) {
    TurnContext ctx = context(s);
    for (size_t a = 0; a < s.empires.size(); ++a)
        for (size_t b = a + 1; b < s.empires.size(); ++b) {
            setContact(s, EmpireId{a}, EmpireId{b});
            diplomacy::setTreaty(ctx, EmpireId{a}, EmpireId{b}, Treaty::NonAggression);
        }
}

} // namespace

TEST_CASE("score: 10 × tonnage + production + 200 × tech levels + 50,000 for everything") {
    score::ScoreParts p;
    p.tonnage = 1200;
    p.production = 3456;
    p.techLevels = 20;
    CHECK(score::scoreOf(p) == 12000 + 3456 + 4000);
    p.everything = true;
    CHECK(score::scoreOf(p) == 12000 + 3456 + 4000 + 50000);
}

TEST_CASE("score: the parts come from the empire") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const Empire& a = s.empire(kA);
    Colony& home = homeworld(s, kA);
    const Location here = locationOf(s.galaxy, home.planet);
    const DesignId station = addTestDesign(s, r, kA, "Keep", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters"});
    addTestVehicle(s, r, station, here);
    const DesignId wasp = addTestDesign(s, r, kA, "Wasp", "Test Fighter Hull", {"Test Fighter Gun", "Test Fighter Engine"});
    home.cargo.units.push_back({wasp, 5});
    addTestVehicle(s, r, wasp, here);  // a unit group in space: no tonnage
    VehicleId mothballed;
    for (Vehicle& v : s.vehicles)
        if (v.owner == kA && vehicleType(r, s, v) == ruleset::VehicleType::Ship) {
            v.status = VehicleStatus::Mothballed;  // not counted
            mothballed = v.id;
            break;
        }
    REQUIRE(mothballed.valid());

    int64_t tonnage = 0;
    int ships = 0;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kA && v.status != VehicleStatus::Mothballed && !isUnitType(vehicleType(r, s, v))) {
            tonnage += r.hull(s.design(v.design).hull).tonnage;
            ++ships;
        }
    const score::ScoreParts p = score::scoreParts(r, s, kA);
    CHECK(p.tonnage == tonnage);
    const diplomacy::Generated g = diplomacy::generated(r, s, kA);
    CHECK(p.production == g.resources.total() + g.research + g.intelligence);
    CHECK(p.production > 0);
    CHECK(p.techLevels == research::totalLevels(r, a));
    CHECK_FALSE(p.everything);
    CHECK(score::empireScore(r, s, kA) == score::scoreOf(p));

    // Systems, planets, population and units are statistics, not score.
    const TurnStats t = score::currentStats(r, s, kA);
    CHECK(t.turn == s.turn);
    CHECK(t.production == g.resources);
    CHECK(t.research == g.research);
    CHECK(t.intelligence == g.intelligence);
    CHECK(t.techLevels == research::totalLevels(r, a));
    CHECK(t.planets == 1);
    CHECK(t.systems == 1);
    CHECK(t.population == home.totalPopulation());
    CHECK(t.ships + t.bases == ships);
    CHECK(t.bases == 1);
    CHECK(t.units == 6);
    CHECK(t.score == score::scoreOf(p));

    // Everything researched: the capped level sum reaches the maxima of the
    // areas allowed in the game that the race can see.
    Empire& e = s.empire(kA);
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i)
        if (r.data().techAreas[i].racialArea == 0 && r.data().techAreas[i].uniqueArea == 0) e.techLevels[i] = r.data().techAreas[i].maxLevel;
    CHECK(score::scoreParts(r, s, kA).everything);
    e.techLevels[techArea(r, "Test Beams").index()] = 50;  // levels are capped at the maximum
    CHECK(score::scoreParts(r, s, kA).techLevels == research::maxLevels(r, s, e));
}

TEST_CASE("score: ranking and the Score Display option") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    boost(s, kB, 20);
    boost(s, kC, 10);
    CHECK(score::ranking(r, s) == std::vector<EmpireId>{kB, kC, kA});

    // Default: own score plus the empires at Non-Aggression or better.
    CHECK(s.options.scoreDisplay == 1);
    CHECK(score::scoreVisible(s, kA, kA));
    CHECK_FALSE(score::scoreVisible(s, kA, kC));
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    CHECK_FALSE(score::scoreVisible(s, kA, kC));  // no treaty
    diplomacy::setTreaty(ctx, kA, kC, Treaty::NonAggression);
    CHECK(score::scoreVisible(s, kA, kC));
    diplomacy::setTreaty(ctx, kA, kC, Treaty::Subjugation, true);
    CHECK(score::scoreVisible(s, kA, kC));
    s.options.scoreDisplay = 0;
    CHECK(score::scoreVisible(s, kA, kA));
    CHECK_FALSE(score::scoreVisible(s, kA, kC));
    s.options.scoreDisplay = 2;
    CHECK(score::scoreVisible(s, kA, kB));
    // Once the game is over every score is visible; the destroyed are never shown.
    s.options.scoreDisplay = 0;
    s.gameOver = true;
    CHECK(score::scoreVisible(s, kA, kB));
    s.empire(kB).alive = false;
    CHECK_FALSE(score::scoreVisible(s, kA, kB));
    CHECK(score::ranking(r, s) == std::vector<EmpireId>{kC, kA});
}

TEST_CASE("score: history and the destruction of beaten empires") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    setContact(s, kA, kC);
    TurnContext ctx = context(s);
    diplomacy::setTreaty(ctx, kA, kC, Treaty::TradeAlliance);
    scoreSteps(ctx);
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
    s.empire(kB).intel.push_back({});
    s.empire(kB).intel.back().target = kC;
    CHECK(score::defeated(r, s, kC));
    CHECK_FALSE(score::defeated(r, s, kA));
    const ObjectId planetC = homeC.planet;
    s.turn = 1;
    scoreSteps(ctx);
    CHECK_FALSE(s.empire(kC).alive);
    CHECK(s.empire(kC).history.size() == 2);  // its row is written before the destruction check
    CHECK(s.colony(planetC) == nullptr);
    for (const Vehicle& v : s.vehicles) CHECK(v.owner != kC);
    CHECK(hasLog(s, kA, "Empire Destroyed"));  // in contact
    CHECK_FALSE(hasLog(s, kB, "Empire Destroyed"));  // never met it
    CHECK(hasLog(s, kC, "Empire Destroyed"));
    CHECK(s.empire(kB).intel.empty());  // projects aimed at it are removed
    CHECK_FALSE(s.empire(kA).relation(kC).contact);  // back to "no contact"
    CHECK(s.empire(kA).relation(kC).treaty == Treaty::None);
    CHECK_FALSE(s.gameOver);
    s.turn = 2;
    scoreSteps(ctx);
    CHECK(s.empire(kA).history.size() == 3);
    CHECK(s.empire(kC).history.size() == 2);  // nothing recorded after its death
}

TEST_CASE("score: no victory for the last empire standing, and none when nothing is enabled") {
    GameState s = newPoliticsGame();
    TurnContext ctx = context(s);
    wipeOut(s, kB);
    wipeOut(s, kC);
    scoreSteps(ctx);
    CHECK_FALSE(s.empire(kB).alive);
    CHECK_FALSE(s.gameOver);
    CHECK(hasLog(s, kA, "Last Empire Standing"));
    for (uint32_t t = 1; t < 300; ++t) {
        s.turn = t;
        scoreSteps(ctx);
    }
    CHECK_FALSE(s.gameOver);
}

TEST_CASE("score: victory conditions end the game without naming a winner") {
    const Rules& r = politicsRules();
    auto fresh = [] {
        GameState s = newPoliticsGame();
        boost(s, kB, 20);
        return s;
    };
    auto end = [](GameState& s) {
        TurnContext ctx = context(s);
        scoreSteps(ctx);
    };

    SUBCASE("score threshold") {
        GameState s = fresh();
        s.options.victory.score = true;
        s.options.victory.scoreValue = score::empireScore(r, s, kB) + 1;
        end(s);
        CHECK_FALSE(s.gameOver);
        boost(s, kB, 1);
        end(s);
        CHECK(s.gameOver);
        CHECK(s.winner == kB);  // the best score, for the game-over screen
        const LogEntry* over = findLog(s, kA, "Game Over");
        REQUIRE(over);
        CHECK(over->text.find("Realm 2") == std::string::npos);  // no winner named
        CHECK(over->text.find("Scores") != std::string::npos);
    }
    SUBCASE("percent of every other empire's score") {
        GameState s = fresh();
        s.options.victory.percentOfSecond = true;
        s.options.victory.percentOfSecondValue = 200;
        boost(s, kC, 18);
        end(s);
        CHECK_FALSE(s.gameOver);
        wipeOut(s, kC);
        s.empire(kC).alive = false;
        end(s);
        CHECK(s.gameOver);
        CHECK(s.winner == kB);
    }
    SUBCASE("percent of second place overrides the score and years tests") {
        GameState s = fresh();
        s.options.victory.percentOfSecond = true;
        s.options.victory.percentOfSecondValue = 1000;
        s.options.victory.score = true;
        s.options.victory.scoreValue = 1;
        s.options.victory.years = true;
        s.options.victory.yearsValue = 1;
        s.turn = 30;
        end(s);
        CHECK_FALSE(s.gameOver);  // score and years are met, but the lead is not
        s.options.victory.percentOfSecond = false;
        end(s);
        CHECK(s.gameOver);
    }
    SUBCASE("technology share, counting levels") {
        GameState s = fresh();
        s.options.victory.techPercent = true;
        s.options.victory.techPercentValue = 75;
        end(s);
        CHECK_FALSE(s.gameOver);
        Empire& c = s.empire(kC);
        const int total = research::maxLevels(r, s, c);
        for (uint32_t i = 0; i < r.data().techAreas.size() && research::totalLevels(r, c) * 100 < 75 * total; ++i)
            if (research::canGainLevel(r, s, c, ruleset::TechAreaId{i})) c.techLevels[i] = r.data().techAreas[i].maxLevel;
        end(s);
        CHECK(s.gameOver);
    }
    SUBCASE("years elapsed") {
        GameState s = fresh();
        s.options.victory.years = true;
        s.options.victory.yearsValue = 2;
        s.turn = 18;
        end(s);
        CHECK_FALSE(s.gameOver);
        s.turn = 19;  // the date reaches 2402.0 at the end of this turn
        end(s);
        CHECK(s.gameOver);
        CHECK(s.winner == kB);
    }
    SUBCASE("peace needs Non-Aggression or better between every pair") {
        GameState s = fresh();
        s.options.victory.peace = true;
        s.options.victory.peaceYears = 1;
        for (int i = 0; i < 12; ++i) end(s);
        CHECK(s.peacefulTurns == 0);  // no contact breaks the peace
        makePeace(s);
        TurnContext ctx = context(s);
        diplomacy::setTreaty(ctx, kA, kC, Treaty::None);
        for (int i = 0; i < 12; ++i) end(s);
        CHECK(s.peacefulTurns == 0);  // no treaty breaks it too
        CHECK_FALSE(s.gameOver);
        diplomacy::setTreaty(ctx, kA, kC, Treaty::TradeAlliance);
        for (int i = 0; i < 9; ++i) end(s);
        CHECK(s.peacefulTurns == 9);
        CHECK_FALSE(s.gameOver);
        end(s);
        CHECK(s.gameOver);
    }
    SUBCASE("before the After-X-years date nothing is checked and peace does not count") {
        GameState s = fresh();
        makePeace(s);
        s.options.victory.score = true;
        s.options.victory.scoreValue = 1;
        s.options.victory.delay = true;
        s.options.victory.delayYears = 1;
        s.turn = 8;
        end(s);
        CHECK_FALSE(s.gameOver);
        CHECK(s.peacefulTurns == 0);
        s.turn = 9;
        end(s);
        CHECK(s.gameOver);
        CHECK(s.peacefulTurns == 1);
    }
}

TEST_CASE("score: the victory comparisons are made in floating point") {
    // score >= (X / 100) × other: X / 100 is rounded, so an exact tie can
    // go either way, unlike an integer comparison (spec 05 §6).
    int differs = 0;
    for (int pct = 101; pct <= 400; ++pct) {
        const bool fp = xmath::Ext(pct) >= xmath::percent(pct) * xmath::Ext(100);
        CHECK(score::leadsBy(pct, 100, pct) == fp);
        differs += fp ? 0 : 1;
    }
    CHECK(differs > 0);  // some exact ties fail in floating point
    CHECK(score::leadsBy(301, 200, 150));
    CHECK_FALSE(score::leadsBy(299, 200, 150));
    CHECK(score::leadsBy(0, 0, 150));
    // levels >= maxLevels × X / 100.
    CHECK(score::techShareMet(15, 20, 75));
    CHECK_FALSE(score::techShareMet(14, 20, 75));
    CHECK(score::techShareMet(1, 3, 33));   // 0.99
    CHECK_FALSE(score::techShareMet(0, 3, 33));
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

TEST_CASE("score: a human player who lost everything plays a last turn before it is destroyed") {
    // Spec 06 §7 Q83 (confirmed: binary): the Lose ending comes at the start of
    // a last turn, and the empire is marked dead at that turn's end; a
    // computer player is destroyed at once (spec 05 §6).
    const Rules& r = engineRules();
    GameState s = newEngineGame(7, 3, 12, false);
    REQUIRE(s.empire(kA).kind == PlayerKind::Human);
    REQUIRE(s.empire(kB).kind == PlayerKind::Computer);
    wipeOut(s, kA);
    wipeOut(s, kB);
    {
        // Found defeated for the first time (during the turn that took
        // everything): the human is spared, the computer player is not.
        TurnContext ctx = turnContext(r, s);
        score::checkDestruction(ctx, kA, false);
        score::checkDestruction(ctx, kB, false);
        CHECK(s.empire(kA).alive);
        CHECK_FALSE(s.empire(kB).alive);
    }
    // The next turn starts with the human defeated: its last turn. At its end
    // the empire is marked dead.
    std::vector<EmpireOrders> orders{{kA, s.turn, {}}};
    processTurn(r, s, orders);
    CHECK_FALSE(s.empire(kA).alive);
    CHECK(hasLog(s, kA, "Empire Destroyed"));
}

TEST_CASE("score: a human player's statistics, history and log text files") {
    // Spec 05 §3.4, §5, §8 step 2 (open question 40): written for human
    // players at the start of their end-of-turn processing; the engine makes
    // the lines.
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    REQUIRE(s.options.simultaneous);
    s.empire(kC).kind = PlayerKind::Computer;
    TurnContext ctx = context(s);
    setContact(s, kA, kB);
    diplomacy::setTreaty(ctx, kA, kB, Treaty::NonAggression);

    // Statistics: a row per empire whose score the player may see; the empire
    // number in 5, the date in tenths (advanced in a simultaneous game) in 8,
    // then eleven columns of 12.
    CHECK(score::fileDate(s) == 1);
    score::PlayerRecords rec = score::playerRecords(r, s, kA);
    REQUIRE(rec.statistics.size() == 2);
    const TurnStats mine = score::currentStats(r, s, kA);
    CHECK(rec.statistics[0] == score::statisticsLine(kA, 1, mine));
    CHECK(rec.statistics[0].starts_with("    1   24001"));
    CHECK(rec.statistics[0].size() == 5 + 8 + 11 * 12);
    CHECK(rec.statistics[0].substr(13, 12) == std::format("{:>12}", mine.score));
    CHECK(rec.statistics[1].starts_with("    2"));
    s.options.scoreDisplay = 2;
    CHECK(score::playerRecords(r, s, kA).statistics.size() == 3);
    s.options.scoreDisplay = 0;
    CHECK(score::playerRecords(r, s, kA).statistics.size() == 1);
    // A turn-based game's processing sees the unadvanced date.
    s.options.simultaneous = false;
    CHECK(score::playerRecords(r, s, kA).statistics[0].starts_with("    1   24000"));
    s.options.simultaneous = true;

    // History: the entries dated the turn before (the processing sees 6 at
    // turn 5): accepted treaties, broken treaties, wars, first contacts,
    // contacts lost and destroyed empires, each naming the other empire.
    s.turn = 5;
    auto message = [&](EmpireId from, EmpireId to, MessageType type, uint32_t dated, Treaty t = Treaty::None, MessageId reply = {}) {
        DiplomaticMessage m;
        m.id = MessageId{s.nextMessageId++};
        m.from = from;
        m.to = to;
        m.type = type;
        m.sentTurn = dated;
        m.dated = dated;
        m.treaty = t;
        m.inReplyTo = reply;
        m.delivered = true;
        s.messages.push_back(m);
        return m.id;
    };
    const MessageId proposal = message(kA, kB, MessageType::ProposeTreaty, 4, Treaty::TradeAlliance);
    message(kB, kA, MessageType::AcceptTreaty, 5, Treaty::None, proposal);
    message(kC, kA, MessageType::DeclareWar, 5);
    message(kA, kC, MessageType::BreakTreaty, 6);  // dated now: next time
    message(kB, kC, MessageType::DeclareWar, 5);    // not ours
    message(kC, kA, MessageType::Surrender, 5);     // never recorded
    // Log entries of the turn processed before (engine turn 4, dated 5).
    s.empire(kA).log.push_back(LogEntry{4, LogCategory::Politics, "First Contact", diplomacy::firstContactText(s, kC), std::nullopt, {}});
    s.empire(kA).log.push_back(LogEntry{4, LogCategory::Politics, "Empire Destroyed", score::destroyedText(s, kB), std::nullopt, {}});
    s.empire(kA).log.push_back(LogEntry{5, LogCategory::Misc, "Too New", "Two\nlines", std::nullopt, {}});
    rec = score::playerRecords(r, s, kA);
    REQUIRE(rec.history.size() == 4);
    CHECK(rec.history[0] == score::historyLine(5, kB, "Trade Alliance established"));
    CHECK(rec.history[0] == "   24005    2    0    0 Trade Alliance established");
    CHECK(rec.history[1] == score::historyLine(5, kC, "War declared"));
    CHECK(rec.history[2].find("First contact with") != std::string::npos);
    CHECK(rec.history[2].starts_with("   24005    3    0    0 "));
    CHECK(rec.history[3].find("was destroyed") != std::string::npos);
    CHECK(score::historyLine(4, {}, "x") == "   24004    0    0    0 x");
    // The log copy: none while `Create Log Text Files for Players` is off,
    // which it is when the key is missing.
    CHECK(rec.log.empty());
    {
        ruleset::Ruleset rs = buildPoliticsRuleset();
        rs.settings.set("Create Log Text Files for Players", "TRUE");
        rs.reindex();
        const Rules withLog{std::move(rs)};
        const score::PlayerRecords copy = score::playerRecords(withLog, s, kA);
        // Two header lines, then the whole log as it stands.
        REQUIRE(copy.log.size() == 2 + s.empire(kA).log.size());
        const auto& log = s.empire(kA).log;
        const size_t first = static_cast<size_t>(std::find_if(log.begin(), log.end(), [](const LogEntry& l) { return l.title == "First Contact"; }) -
                                                 log.begin());
        REQUIRE(first < log.size());
        // Spec 06 §6.1, §7 Q55: "Date" at column 1, "Header" at 10, "Text" at 51, then 78 dashes.
        CHECK(copy.log[0] == "Date     Header                                   Text");
        CHECK(copy.log[0].find("Header") == 9);
        CHECK(copy.log[0].find("Text") == 50);
        CHECK(copy.log[1] == std::string(78, '-'));
        const std::string& line = copy.log[2 + first];
        CHECK(line == score::logLine(5, "First Contact", diplomacy::firstContactText(s, kC)));
        CHECK(line.starts_with("2400.5   First Contact"));
        CHECK(line.substr(9, 40) == std::format("{:<40}", "First Contact"));
        CHECK(line.substr(49, 1) == " ");   // one space before the text, which starts at column 51
        CHECK(copy.log.back() == std::format("{:<9}{:<40} {}", "2400.6", "Too New", "Two lines"));
    }
    {
        // Each CR LF pair (or our single LF) becomes one space; a long title is followed by one space only.
        CHECK(score::logLine(1, "T", "a\r\nb\nc") == std::format("{:<9}{:<40} {}", "2400.1", "T", "a b c"));
        const std::string longTitle(45, 'x');
        CHECK(score::logLine(1, longTitle, "text") == "2400.1   " + longTitle + " text");
    }

    // processTurn hands out a set of lines for each human player only.
    GameState g = newPoliticsGame();
    g.empire(kC).kind = PlayerKind::Computer;
    TurnOptions o;
    o.aiForMissing = false;
    const TurnResult result = processTurn(r, g, {}, o);
    REQUIRE(result.records.size() == 2);
    CHECK(result.records[0].empire == kA);
    CHECK(result.records[1].empire == kB);
    CHECK_FALSE(result.records[0].statistics.empty());
}
