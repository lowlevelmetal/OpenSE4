// Research: costs, the pool, shares, completion, unlocks, ruins and tech
// gains (docs/spec/05 §1).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/ai.hpp"
#include "game/commands.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"
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

// One research step with `points` in the pool.
void step(const Rules& r, GameState& s, EmpireId e, int64_t points) {
    s.empire(e).researchPool = points;
    TurnContext ctx = context(r, s);
    research::researchStep(ctx, e);
}

} // namespace

TEST_CASE("research: the three technology cost settings, capped") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const auto physics = techArea(r, "Test Physics");  // Level Cost 1000
    CHECK(s.options.techCost == 1);                     // Medium is the default
    // Medium: max(LC × L, trunc(LC × L² / 2)).
    CHECK(research::levelCost(r, s, physics, 1) == 1000);
    CHECK(research::levelCost(r, s, physics, 2) == 2000);
    CHECK(research::levelCost(r, s, physics, 3) == 4500);
    CHECK(research::levelCost(r, s, physics, 5) == 12500);
    CHECK(research::levelCost(r, s, physics, 10) == 50000);
    s.options.techCost = 0;  // Low: LC × L
    CHECK(research::levelCost(r, s, physics, 2) == 2000);
    CHECK(research::levelCost(r, s, physics, 5) == 5000);
    s.options.techCost = 2;  // High: LC × L²
    CHECK(research::levelCost(r, s, physics, 2) == 4000);
    CHECK(research::levelCost(r, s, physics, 10) == 100000);
    CHECK(research::levelCost(r, s, physics, 100000) == kMaxTechLevelCost);

    // The spec's worked table for Level Cost 5,000 (levels 2, 3, 5, 10).
    ruleset::Ruleset rs = buildEngineRuleset();
    rs.techAreas[physics.index()].levelCost = 5000;
    const Rules five{std::move(rs)};
    const std::array<std::array<int64_t, 4>, 3> table{{
        {10000, 15000, 25000, 50000},
        {10000, 22500, 62500, 250000},
        {20000, 45000, 125000, 500000},
    }};
    const std::array<int, 4> levels{2, 3, 5, 10};
    for (int setting = 0; setting < 3; ++setting)
        for (size_t i = 0; i < levels.size(); ++i) CHECK(five.techLevelCost(physics, levels[i], setting) == table[size_t(setting)][i]);
}

TEST_CASE("research: shares, evenly rounded and uncapped, or in order") {
    const std::vector<int64_t> three{4000, 4000, 4000};
    CHECK(research::allocate(10000, three, false) == std::vector<int64_t>{4000, 4000, 2000});
    CHECK(research::allocate(10000, three, true) == std::vector<int64_t>{3333, 3333, 3333});
    CHECK(research::allocate(10001, std::vector<int64_t>{1, 1}, true) == std::vector<int64_t>{5000, 5000});  // 5000.5: ties to even
    CHECK(research::allocate(10003, std::vector<int64_t>{1, 1}, true) == std::vector<int64_t>{5002, 5002});  // 5001.5
    CHECK(research::allocate(5, std::vector<int64_t>{9, 9, 9}, true) == std::vector<int64_t>{2, 2, 2});      // more than the pool
    // Even shares are not capped by what a project needs.
    const std::vector<int64_t> small{100, 5000};
    CHECK(research::allocate(1000, small, true) == std::vector<int64_t>{500, 500});
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
    // Gaining a level checks only the game's allowed areas and the racial and unique checks.
    CHECK(research::canGainLevel(r, s, e, techArea(r, "Test Cloaking")));  // requirements not met
    CHECK_FALSE(research::canGainLevel(r, s, e, techArea(r, "Test Psionics")));
}

TEST_CASE("research: a finished level logs the level, the discoveries and the empty queue") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    const auto beams = techArea(r, "Test Beams");
    REQUIRE(e.techLevel(beams) == 1);
    REQUIRE(apply(r, s, me, cmd::SetResearch{{{beams, 0}}, true, false}).ok);
    step(r, s, me, 2000 + 700);  // level 2 costs 2000; the rest is lost
    CHECK(e.techLevel(beams) == 2);
    CHECK(e.research.empty());
    CHECK(e.researchPool == 0);
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
    step(r, s, me, 1000);
    CHECK(e.techLevel(beams) == 2);
    REQUIRE(e.research.size() == 1);
    CHECK(e.research[0].progress == 1000);

    // An empty queue loses the pool too.
    e.research.clear();
    step(r, s, me, 5000);
    CHECK(e.researchPool == 0);
}

TEST_CASE("research: even split, in-order funding, one level at most, partial progress") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    const auto beams = techArea(r, "Test Beams"), armor = techArea(r, "Test Armor"), econ = techArea(r, "Test Economics");

    e.research = {{beams, 0}, {armor, 0}, {econ, 0}};  // each needs 2000 for level 2
    e.researchEvenly = true;
    step(r, s, me, 3000);
    for (const auto& p : e.research) CHECK(p.progress == 1000);

    e.research = {{beams, 0}, {armor, 0}, {econ, 0}};
    e.researchEvenly = false;
    step(r, s, me, 5000);
    CHECK(e.techLevel(beams) == 2);
    CHECK(e.techLevel(armor) == 2);
    REQUIRE(e.research.size() == 1);
    CHECK(e.research[0].area == econ);
    CHECK(e.research[0].progress == 1000);

    // A huge pool still completes one level per project per turn.
    e.research = {{econ, 0}};
    step(r, s, me, 1000000);
    CHECK(e.techLevel(econ) == 2);
}

TEST_CASE("research: an area is queued once; areas at their maximum leave first") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    const auto physics = techArea(r, "Test Physics");  // max 5, level 1
    const auto rock = techArea(r, "Test Rock Colonies");  // max 1, at its maximum
    e.research = {{rock, 0}, {physics, 100}, {physics, 900}, {physics, 0}};
    e.researchEvenly = false;
    step(r, s, me, 500);
    REQUIRE(e.research.size() == 1);
    CHECK(e.research[0].area == physics);
    CHECK(e.research[0].progress == 600);  // the first entry keeps its progress
}

TEST_CASE("research: repeat re-queues the area at the end until the maximum") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    Empire& e = s.empire(me);
    const auto espionage = techArea(r, "Test Espionage");  // max 5, start 1
    const auto econ = techArea(r, "Test Economics");
    e.research = {{espionage, 0}, {econ, 0}};
    e.repeatResearch = true;
    e.researchEvenly = false;
    step(r, s, me, 2000);  // espionage 2 costs 2000: done, requeued behind economics
    REQUIRE(e.research.size() == 2);
    CHECK(e.research[0].area == econ);
    CHECK(e.research[1].area == espionage);
    CHECK(e.research[1].progress == 0);
    e.research = {{espionage, 0}};
    for (int turn = 0; turn < 10; ++turn) step(r, s, me, 1000000);
    CHECK(e.techLevel(espionage) == 5);
    CHECK(e.research.empty());
    CHECK(logCount(s, me, "New Tech Level") == 4);  // one level per turn
    CHECK(logCount(s, me, "All Projects Completed") == 1);
}

TEST_CASE("research: an area already queued is not queued again") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const auto beams = techArea(r, "Test Beams"), armor = techArea(r, "Test Armor");
    REQUIRE(apply(r, s, me, cmd::SetResearch{{{beams, 0}, {armor, 0}, {beams, 0}}, true, false}).ok);
    REQUIRE(s.empire(me).research.size() == 2);  // spec 05 §1.4
    CHECK(s.empire(me).research[0].area == beams);
    CHECK(s.empire(me).research[1].area == armor);
}

TEST_CASE("research: points are spent the turn after they are produced") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const auto beams = techArea(r, "Test Beams");
    s.empire(me).research = {{beams, 0}};
    TurnContext ctx = context(r, s);

    // The opening pools (createGame): Starting Resources plus one turn of
    // research, and no intelligence (spec 05 §1.1).
    REQUIRE(s.turn == 0);
    const int64_t opening = s.options.startingResources[Resource::Minerals] + economy::empireProduction(r, s, me).research;
    CHECK(s.empire(me).researchPool == opening);
    CHECK(s.empire(me).intelPool == 0);
    CHECK(research::availablePoints(s, s.empire(me)) == opening);
    // The step spends the whole pool and empties it.
    research::researchStep(ctx, me);
    CHECK(s.empire(me).techLevel(beams) == 2);
    CHECK(s.empire(me).researchPool == 0);
    // The income step fills it for the next turn's step.
    research::addToPools(s.empire(me), 700, 0);

    ++s.turn;
    s.empire(me).research = {{beams, 0}};
    CHECK(research::availablePoints(s, s.empire(me)) == 700);
    research::researchStep(ctx, me);
    CHECK(s.empire(me).research[0].progress == 700);
    CHECK(s.empire(me).researchPool == 0);

    // Pools are capped.
    research::addToPools(s.empire(me), research::kPoolCap, research::kPoolCap);
    CHECK(s.empire(me).researchPool == research::kPoolCap);
    CHECK(s.empire(me).intelPool == research::kPoolCap);
}

TEST_CASE("research: the opening pools hold one turn of production only") {
    // Spec 02 §9, §13 Q37: no computer bonus, Generate Points, trade or
    // tariffs; a resource delivered at exactly 0 gets the Settings minimum.
    ruleset::Ruleset rs = buildPoliticsRuleset();
    ruleset::Facility mint;
    mint.name = "Test Mint";
    mint.group = "Test";
    mint.family = 9000;
    mint.romanNumeral = 1;
    for (auto [kind, value] : {std::pair{AbilityKind::GeneratePointsMinerals, 1000}, std::pair{AbilityKind::GeneratePointsResearch, 300}}) {
        ruleset::Ability a;
        a.type = std::string(identifier(kind));
        a.value1 = std::to_string(value);
        mint.abilities.push_back(a);
    }
    rs.facilities.push_back(mint);
    const Rules r{std::move(rs)};
    GameState s = newPoliticsGame();
    const EmpireId me{0u}, them{1u};
    s.empire(me).kind = PlayerKind::Computer;
    s.options.aiBonus = 3;
    homeworld(s, me).facilities.push_back(static_cast<uint32_t>(r.data().facilities.size() - 1));
    TurnContext ctx = context(r, s);
    setContact(s, me, them);
    diplomacy::setTreaty(ctx, me, them, Treaty::Partnership);
    for (int i = 0; i < 5; ++i) diplomacy::treatyStep(ctx, me);
    s.options.finiteResources = true;
    const auto values = s.galaxy.object(homeworld(s, me).planet).value;
    research::openingPools(r, s);
    const economy::Production made = economy::empireProduction(r, s, me);
    Resources opening = made.resources;
    for (int64_t& v : opening.v)
        if (v == 0) v = 200;
    CHECK(s.empire(me).stockpile == s.options.startingResources + opening);
    CHECK(s.empire(me).researchPool == s.options.startingResources[Resource::Minerals] + made.research);
    CHECK(s.empire(me).intelPool == 0);
    CHECK(s.galaxy.object(homeworld(s, me).planet).value == values);  // nothing drawn from finite stocks
}

TEST_CASE("research: a master's tariff takes part of the research income, and nobody gets it") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const EmpireId master{0u}, subject{1u};
    TurnContext ctx = context(r, s);
    setContact(s, master, subject);
    ++s.turn;  // past the opening pool
    // A computer subject with the Low bonus: its incomes are doubled after the tariff (spec 05 §8).
    s.empire(subject).kind = PlayerKind::Computer;
    s.options.aiBonus = 1;
    REQUIRE(ai::incomeBonusFactor(s, subject) == 2);
    REQUIRE(ai::incomeBonusFactor(s, master) == 1);
    auto income = [&] {
        economy::collectIncome(ctx, subject);
        economy::collectIncome(ctx, master);
        return std::pair{s.empire(subject).economy, s.empire(master).economy};
    };
    const auto [freeSubject, freeMaster] = income();

    diplomacy::setTreaty(ctx, master, subject, Treaty::Subjugation, true);
    const diplomacy::Generated due = diplomacy::tariffDue(r, s, subject);
    REQUIRE(due.research > 0);
    const auto [paying, receiving] = income();
    // The income step takes the tariff once, before the bonus, on research and intelligence too.
    CHECK(paying.research == freeSubject.research - 2 * due.research);
    CHECK(paying.intelligence == freeSubject.intelligence - 2 * due.intelligence);
    CHECK(receiving.research == freeMaster.research);  // nobody receives the research part
    CHECK(receiving.intelligence == freeMaster.intelligence);

    // The income step puts that net income into the pools as it is.
    s.empire(subject).researchPool = s.empire(master).researchPool = 0;
    s.empire(subject).intelPool = s.empire(master).intelPool = 0;
    economy::collectIncome(ctx, subject);
    economy::collectIncome(ctx, master);
    CHECK(s.empire(subject).researchPool == paying.research);
    CHECK(s.empire(subject).intelPool == paying.intelligence);
    CHECK(s.empire(master).researchPool == receiving.research);

    // The master receives the resource part at once, at its subject's
    // income step, not at its own (spec 05 §3.3, §8).
    REQUIRE(paying.tariffsOut.total() > 0);
    const Resources before = s.empire(master).stockpile;
    economy::collectIncome(ctx, subject);
    CHECK(s.empire(master).stockpile == before + s.empire(subject).economy.tariffsOut);
    // Its own trade step adds no tariffs a second time.
    const Resources beforeTrade = s.empire(master).stockpile;
    economy::collectTrade(ctx, master);
    CHECK(s.empire(master).stockpile == beforeTrade + s.empire(master).economy.trade);
}

TEST_CASE("research: ETA simulates the queue") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    ++s.turn;
    Empire& e = s.empires[0];
    const auto beams = techArea(r, "Test Beams"), armor = techArea(r, "Test Armor");
    e.research = {{beams, 500}, {armor, 0}};  // need 1500 and 2000
    e.researchPool = 500;
    e.economy.research = 500;
    e.researchEvenly = false;
    CHECK(research::etaTurns(r, s, e, 0) == 3);
    CHECK(research::etaTurns(r, s, e, 1) == 7);  // after beams: 4 more turns
    e.researchEvenly = true;
    CHECK(research::etaTurns(r, s, e, 0) == 6);  // 250 per turn
    CHECK(research::etaTurns(r, s, e, 1) == 7);  // 250/turn until turn 6 (1500), then 500
    CHECK(research::etaTurns(r, s, e, 2) == -1);
    e.researchPool = 1500;  // this turn's pool differs from next turns' production
    e.researchEvenly = false;
    CHECK(research::etaTurns(r, s, e, 0) == 1);
    e.economy.research = 0;
    e.researchPool = 0;
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

TEST_CASE("research: grantLevel ignores lower levels, caps at the maximum and never feeds a master") {
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
    CHECK(s.empire(master).techLevel(beams) == 1);  // no technology passes to the master
    research::grantLevel(ctx, subject, beams, 2, "test");
    CHECK(s.empire(subject).techLevel(beams) == 4);
    research::grantLevel(ctx, master, beams, 99, "test");
    CHECK(s.empire(master).techLevel(beams) == 10);

    // An area the race cannot see gains nothing; one short of its requirements can.
    research::grantLevel(ctx, master, techArea(r, "Test Psionics"), 2, "test");
    CHECK(s.empire(master).techLevel(techArea(r, "Test Psionics")) == 0);
    research::grantLevel(ctx, master, techArea(r, "Test Cloaking"), 1, "test");
    CHECK(s.empire(master).techLevel(techArea(r, "Test Cloaking")) == 1);
}

TEST_CASE("research: ruins give levels in random researchable areas; totals and the tech share") {
    const Rules& r = engineRules();
    GameState a = newEngineGame(9);
    GameState b = newEngineGame(9);
    Rng ra(4), rb(4);
    TurnContext ca = context(r, a), cb = context(r, b);
    const int before = research::totalLevels(r, a.empires[0]);
    research::grantRandomAdvances(ca, EmpireId{0u}, 3, ra, "ruins");
    research::grantRandomAdvances(cb, EmpireId{0u}, 3, rb, "ruins");
    CHECK(research::totalLevels(r, a.empires[0]) == before + 3);
    CHECK(a.empires[0].techLevels == b.empires[0].techLevels);
    // Only researchable areas: nothing beyond a maximum, nothing unseen.
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) CHECK(a.empires[0].techLevels[i] <= r.data().techAreas[i].maxLevel);
    CHECK(a.empires[0].techLevel(techArea(r, "Test Psionics")) == 0);

    GameState s = newEngineGame();
    Empire& e = s.empires[0];
    int total = 0, owned = 0;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
        const auto& t = r.data().techAreas[i];
        owned += std::min(e.techLevels[i], t.maxLevel);
        if (t.racialArea == 0 && t.uniqueArea == 0) total += t.maxLevel;  // the areas this race can see
    }
    CHECK(research::totalLevels(r, e) == owned);
    CHECK(research::maxLevels(r, s, e) == total);
    CHECK(research::techPercent(r, s, e) == owned * 100 / total);
    CHECK_FALSE(research::researchedEverything(r, s, e));
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i)
        if (r.data().techAreas[i].racialArea == 0 && r.data().techAreas[i].uniqueArea == 0) e.techLevels[i] = r.data().techAreas[i].maxLevel;
    CHECK(research::techPercent(r, s, e) == 100);
    CHECK(research::researchedEverything(r, s, e));
    // An area excluded from the game leaves the measure.
    s.options.techAreasAllowed.assign(r.data().techAreas.size(), 1);
    s.options.techAreasAllowed[techArea(r, "Test Physics").index()] = 0;
    CHECK(research::maxLevels(r, s, e) == total - 5);
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
            for (Empire& e : s.empires) e.researchPool = 1500;
            TurnContext ctx = context(r, s);
            for (size_t i = 0; i < s.empires.size(); ++i) research::researchStep(ctx, EmpireId{i});
            ++s.turn;
            levels.push_back(s.empires[0].techLevels);
        }
        return levels;
    };
    CHECK(run() == run());
}
