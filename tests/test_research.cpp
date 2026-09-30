// Research: costs, allocation, completion, unlocks, tech gifts (docs/spec/05 §1).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/commands.hpp"
#include "game/diplomacy.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

namespace {

TurnContext context(const Rules& r, GameState& s) { return turnContext(r, s); }

int logCount(const GameState& s, EmpireId e, std::string_view title) {
    int n = 0;
    for (const auto& l : s.empire(e).log)
        if (l.title == title) ++n;
    return n;
}

} // namespace

TEST_CASE("research: level cost follows the tech cost growth option") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const auto physics = techArea(r, "Test Physics");  // Level Cost 1000
    CHECK(research::levelCost(r, s, physics, 1) == 1000);
    CHECK(research::levelCost(r, s, physics, 2) == 2000);
    CHECK(research::levelCost(r, s, physics, 5) == 5000);
    s.options.techCostGrowth = 50;
    CHECK(research::levelCost(r, s, physics, 3) == 2000);
    s.options.techCostGrowth = 0;
    CHECK(research::levelCost(r, s, physics, 7) == 1000);
}

TEST_CASE("research: allocation evenly and in order, without carry-over") {
    const std::vector<int64_t> three{4000, 4000, 4000};
    CHECK(research::allocate(10000, three, false) == std::vector<int64_t>{4000, 4000, 2000});
    CHECK(research::allocate(10000, three, true) == std::vector<int64_t>{3333, 3333, 3333});
    // A project needing less than its share takes only what it needs; the rest is lost.
    const std::vector<int64_t> small{100, 5000};
    CHECK(research::allocate(1000, small, true) == std::vector<int64_t>{100, 500});
    CHECK(research::allocate(1000, small, false) == std::vector<int64_t>{100, 900});
    CHECK(research::allocate(0, small, true) == std::vector<int64_t>{0, 0});
    CHECK(research::allocate(-5, small, false) == std::vector<int64_t>{0, 0});
    CHECK(research::allocate(100, std::vector<int64_t>{}, true).empty());
}

TEST_CASE("research: researchable areas respect requirements, racial and unique areas") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Empire& e = s.empires[0];
    auto has = [&](std::string_view name) {
        const auto list = research::researchable(r, s, e);
        return std::find(list.begin(), list.end(), techArea(r, name)) != list.end();
    };
    CHECK(has("Test Physics"));
    CHECK_FALSE(has("Test Missiles"));  // needs Physics 2
    CHECK_FALSE(has("Test Psionics"));  // racial
    CHECK_FALSE(has("Test Relics"));    // unique
    CHECK_FALSE(has("Test Rock Colonies"));  // already at its maximum (1)
    e.techLevels[techArea(r, "Test Physics").index()] = 2;
    CHECK(has("Test Missiles"));
    e.uniqueAreasUnlocked.push_back(7);
    CHECK(has("Test Relics"));
}

TEST_CASE("research: a finished level logs the level, the discoveries and the empty queue") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    const auto beams = techArea(r, "Test Beams");
    REQUIRE(e.techLevel(beams) == 1);
    REQUIRE(apply(r, s, me, cmd::SetResearch{{{beams, 0}}, true, false}).ok);
    e.economy.research = 2000 + 700;  // level 2 costs 2000; the rest is lost
    TurnContext ctx = context(r, s);
    research::runResearch(ctx);
    CHECK(e.techLevel(beams) == 2);
    CHECK(e.research.empty());
    CHECK(hasLog(s, me, "New Tech Level"));
    CHECK(hasLog(s, me, "Test Disruptor Discovered"));  // needs Beams 2
    CHECK_FALSE(hasLog(s, me, "Test Laser II Discovered"));  // needs Beams 3
    CHECK(hasLog(s, me, "All Projects Completed"));
    const LogEntry* level = findLog(s, me, "New Tech Level");
    REQUIRE(level);
    CHECK(level->category == LogCategory::Research);
    CHECK(level->text.find("Test Beams") != std::string::npos);

    // No carry-over: a new project starts from zero next turn.
    REQUIRE(apply(r, s, me, cmd::SetResearch{{{beams, 0}}, true, false}).ok);
    e.economy.research = 1000;
    research::runResearch(ctx);
    CHECK(e.techLevel(beams) == 2);
    REQUIRE(e.research.size() == 1);
    CHECK(e.research[0].progress == 1000);
}

TEST_CASE("research: even split, in-order funding and partial progress") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    const auto beams = techArea(r, "Test Beams"), armor = techArea(r, "Test Armor"), econ = techArea(r, "Test Economics");
    TurnContext ctx = context(r, s);

    e.research = {{beams, 0}, {armor, 0}, {econ, 0}};  // each needs 2000 for level 2
    e.researchEvenly = true;
    e.economy.research = 3000;
    research::runResearch(ctx);
    for (const auto& p : e.research) CHECK(p.progress == 1000);

    e.research = {{beams, 0}, {armor, 0}, {econ, 0}};
    e.researchEvenly = false;
    e.economy.research = 5000;
    research::runResearch(ctx);
    CHECK(e.techLevel(beams) == 2);
    CHECK(e.techLevel(armor) == 2);
    REQUIRE(e.research.size() == 1);
    CHECK(e.research[0].area == econ);
    CHECK(e.research[0].progress == 1000);
}

TEST_CASE("research: repeat re-queues the next level until the maximum") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    const auto espionage = techArea(r, "Test Espionage");  // max 5, start 1
    e.research = {{espionage, 0}};
    e.repeatResearch = true;
    e.economy.research = 1000000;
    TurnContext ctx = context(r, s);
    for (int turn = 0; turn < 10; ++turn) research::runResearch(ctx);
    CHECK(e.techLevel(espionage) == 5);
    CHECK(e.research.empty());
    CHECK(logCount(s, me, "New Tech Level") == 4);  // one level per turn
    CHECK(logCount(s, me, "All Projects Completed") == 1);
}

TEST_CASE("research: repeated entries of one area are successive levels") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Empire& e = s.empires[0];
    const auto physics = techArea(r, "Test Physics");  // max 5, level 1
    e.research = {{physics, 0}, {physics, 0}, {physics, 0}, {physics, 0}, {physics, 0}};
    CHECK(research::targetLevels(e) == std::vector<int>{2, 3, 4, 5, 6});
    // The entry for the impossible level 6 is dropped; 3500 each completes levels 2 and 3.
    e.economy.research = 4 * 3500;
    e.researchEvenly = true;
    TurnContext ctx = context(r, s);
    research::runResearch(ctx);
    CHECK(e.techLevel(physics) == 3);
    REQUIRE(e.research.size() == 2);
    CHECK(research::targetLevels(e) == std::vector<int>{4, 5});
    CHECK(e.research[0].progress == 3500);
    CHECK(e.research[1].progress == 3500);
}

TEST_CASE("research: ETA simulates the queue") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Empire& e = s.empires[0];
    const auto beams = techArea(r, "Test Beams"), armor = techArea(r, "Test Armor");
    e.research = {{beams, 500}, {armor, 0}};  // need 1500 and 2000
    e.economy.research = 500;
    e.researchEvenly = false;
    CHECK(research::etaTurns(r, s, e, 0) == 3);
    CHECK(research::etaTurns(r, s, e, 1) == 7);  // after beams: 4 more turns
    e.researchEvenly = true;
    CHECK(research::etaTurns(r, s, e, 0) == 6);  // 250 per turn
    CHECK(research::etaTurns(r, s, e, 1) == 7);  // 250/turn until turn 6 (1500), then 500
    CHECK(research::etaTurns(r, s, e, 2) == -1);
    e.economy.research = 0;
    CHECK(research::etaTurns(r, s, e, 0) == -1);
}

TEST_CASE("research: unlockedBy and availableItems") {
    const Rules& r = engineRules();
    const auto beams = techArea(r, "Test Beams");
    const auto items = research::unlockedBy(r, beams, 3);
    CHECK(std::find(items.begin(), items.end(), "Test Laser II") != items.end());
    CHECK(std::find(items.begin(), items.end(), "Test Laser") == items.end());
    GameState s = newEngineGame();
    const auto have = research::availableItems(r, s.empires[0]);
    CHECK(std::find(have.begin(), have.end(), "Test Laser") != have.end());
    CHECK(std::find(have.begin(), have.end(), "Test Laser II") == have.end());
}

TEST_CASE("research: grantLevel ignores lower levels, caps at the maximum, and feeds the master") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const EmpireId master{0u}, subject{1u};
    const auto beams = techArea(r, "Test Beams");
    TurnContext ctx = context(r, s);
    setContact(s, master, subject);
    diplomacy::setTreaty(ctx, master, subject, Treaty::Subjugation, true);
    REQUIRE(diplomacy::masterOf(s, subject) == master);

    research::grantLevel(ctx, subject, beams, 4, "test");
    CHECK(s.empire(subject).techLevel(beams) == 4);
    CHECK(s.empire(master).techLevel(beams) == 4);  // the subject's discoveries pass up
    research::grantLevel(ctx, subject, beams, 2, "test");
    CHECK(s.empire(subject).techLevel(beams) == 4);
    research::grantLevel(ctx, master, beams, 99, "test");
    CHECK(s.empire(master).techLevel(beams) == 10);
    CHECK(s.empire(subject).techLevel(beams) == 4);  // not downward

    // A protectorate does not pass technology.
    const EmpireId third{2u};
    setContact(s, master, third);
    diplomacy::setTreaty(ctx, master, third, Treaty::Protectorate, true);
    research::grantLevel(ctx, third, techArea(r, "Test Armor"), 5, "test");
    CHECK(s.empire(master).techLevel(techArea(r, "Test Armor")) == 1);
}

TEST_CASE("research: random advances and the tech percentage") {
    const Rules& r = engineRules();
    GameState a = newEngineGame(9);
    GameState b = newEngineGame(9);
    Rng ra(4), rb(4);
    TurnContext ca = context(r, a), cb = context(r, b);
    const int before = research::totalLevels(a.empires[0]);
    research::grantRandomAdvances(ca, EmpireId{0u}, 3, ra, "ruins");
    research::grantRandomAdvances(cb, EmpireId{0u}, 3, rb, "ruins");
    CHECK(research::totalLevels(a.empires[0]) == before + 3);
    CHECK(a.empires[0].techLevels == b.empires[0].techLevels);

    GameState s = newEngineGame();
    Empire& e = s.empires[0];
    int total = 0;
    for (const auto& t : r.data().techAreas)
        if (t.racialArea == 0 && t.uniqueArea == 0) total += t.maxLevel;
    int owned = 0;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i)
        if (r.data().techAreas[i].racialArea == 0 && r.data().techAreas[i].uniqueArea == 0) owned += e.techLevels[i];
    CHECK(research::techPercent(r, s, e) == owned * 100 / total);
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) e.techLevels[i] = r.data().techAreas[i].maxLevel;
    CHECK(research::techPercent(r, s, e) == 100);
}

TEST_CASE("research: turns are deterministic") {
    const Rules& r = engineRules();
    auto run = [&] {
        GameState s = newEngineGame(21, 2, 10);
        for (Empire& e : s.empires) {
            e.research = {{techArea(r, "Test Physics"), 0}, {techArea(r, "Test Beams"), 0}};
            e.repeatResearch = true;
        }
        std::vector<std::vector<int>> levels;
        for (int t = 0; t < 8; ++t) {
            for (Empire& e : s.empires) e.economy.research = 1500;
            TurnContext ctx = context(r, s);
            research::runResearch(ctx);
            levels.push_back(s.empires[0].techLevels);
        }
        return levels;
    };
    CHECK(run() == run());
}
