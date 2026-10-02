// The Scrap window's actions in the engine (docs/spec/03 §15, confirmed:
// binary): Analyze, Fire On, and how all seven actions are carried out in
// each turn style (src/game/scrap.*, cmd::Scrap and the others, movement).

#include "engine_fixture.hpp"

#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/scrap.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

namespace {

const EmpireId kMe{0u};
const EmpireId kThem{1u};

// The engine fixture with parts and areas for Analyze: a "Test Beam Five"
// (Test Beams 5) and a second part of the same need, a "Test Probe" hull that
// needs Test Beams 4, an intelligence project unlocked at Test Beams 4, an
// area "Test Optics" that needs Test Beams 5, and a part that needs Test
// Cloaking 5 although that area stops at 3.
std::unique_ptr<Rules> analyzeRules() {
    ruleset::Ruleset rs = buildEngineRuleset();
    auto area = [&](std::string_view name) {
        for (uint32_t i = 0; i < rs.techAreas.size(); ++i)
            if (rs.techAreas[i].name == name) return ruleset::TechAreaId{i};
        FAIL("no tech area ", name);
        return ruleset::TechAreaId{};
    };
    auto part = [&](std::string name, std::vector<ruleset::TechRequirement> reqs) {
        ruleset::Component c;
        c.name = std::move(name);
        c.tonnage = 10;
        c.structure = 10;
        c.cost = {10, 0, 0};
        c.vehicles = ruleset::maskOf(ruleset::VehicleType::Ship) | ruleset::maskOf(ruleset::VehicleType::Base);
        c.requirements = std::move(reqs);
        rs.components.push_back(std::move(c));
    };
    part("Test Beam Five", {{area("Test Beams"), 5}});
    part("Test Beam Five Twin", {{area("Test Beams"), 5}});
    part("Test Deep Cloak", {{area("Test Cloaking"), 5}});
    part("Test Relic Part", {{area("Test Relics"), 1}});
    ruleset::VehicleSize probe;
    probe.name = probe.shortName = "Test Probe";
    probe.code = "TP";
    probe.type = ruleset::VehicleType::Ship;
    probe.tonnage = 200;
    probe.cost = {50, 0, 0};
    probe.requirements = {{area("Test Construction"), 1}, {area("Test Beams"), 4}};
    rs.vehicleSizes.push_back(std::move(probe));
    ruleset::IntelProject spy;
    spy.name = "Test Beam Spying";
    spy.type = "Research - Steal";
    spy.requirements = {{area("Test Beams"), 4}};
    rs.intelProjects.push_back(std::move(spy));
    ruleset::TechArea optics;
    optics.name = "Test Optics";
    optics.group = "Applied Science";
    optics.maxLevel = 5;
    optics.levelCost = 1000;
    optics.requirements = {{area("Test Beams"), 5}};
    rs.techAreas.push_back(std::move(optics));
    rs.reindex();
    return std::make_unique<Rules>(std::move(rs));
}

GameState newGame(const Rules& r, bool simultaneous) {
    GameSetup setup;
    setup.seed = 7;
    setup.options.systemCount = 12;
    setup.options.simultaneous = simultaneous;
    for (int i = 0; i < 2; ++i) {
        EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        setup.empires.push_back(std::move(e));
    }
    auto g = createGame(r, setup);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    GameState s = std::move(*g);
    // Only the vehicles a test makes.
    s.vehicles.clear();
    s.fleets.clear();
    return s;
}

Location homeOf(GameState& s, EmpireId e) { return locationOf(s.galaxy, homeworld(s, e).planet); }

DesignId design(GameState& s, const Rules& r, EmpireId owner, std::string_view name, std::string_view hull,
                std::initializer_list<std::string_view> extra) {
    Design d;
    d.owner = owner;
    d.name = std::string(name);
    d.hull = hullIndex(r, hull);
    for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"}) d.entries.push_back({componentIndex(r, c), -1});
    for (auto c : extra) d.entries.push_back({componentIndex(r, c), -1});
    return addDesign(s, std::move(d));
}

int entries(const GameState& s, EmpireId e, std::string_view title) {
    return static_cast<int>(std::count_if(s.empire(e).log.begin(), s.empire(e).log.end(), [&](const LogEntry& l) { return l.title == title; }));
}

std::vector<std::string> researchTitles(const GameState& s, EmpireId e) {
    std::vector<std::string> out;
    for (const LogEntry& l : s.empire(e).log)
        if (l.category == LogCategory::Research) out.push_back(l.title);
    return out;
}

void setLevel(const Rules& r, GameState& s, EmpireId e, std::string_view area, int level) {
    Empire& emp = s.empire(e);
    emp.techLevels.resize(r.data().techAreas.size(), 0);
    emp.techLevels[techArea(r, area).index()] = level;
}

// The simultaneous movement phase only.
void movementPhase(const Rules& r, GameState& s) {
    TurnContext ctx{r, s, {}, {}, {}};
    movement::startTurn(ctx);
    movement::runMovementAndCombat(ctx);
    s.removeDeadVehicles();
}

} // namespace

// ---- Analyze ---------------------------------------------------------------------------------------

TEST_CASE("scrap window: Analyze teaches one level per distinct requirement above the owner's level") {
    const auto rules = analyzeRules();
    const Rules& r = *rules;
    GameState s = newGame(r, false);
    const Location home = homeOf(s, kMe);
    setLevel(r, s, kMe, "Test Beams", 2);
    setLevel(r, s, kMe, "Test Construction", 1);
    // A ship of the other empire's design, captured by us: it keeps its design.
    const DesignId theirs =
        design(s, r, kThem, "Prize", "Test Probe", {"Test Laser II", "Test Beam Five", "Test Beam Five Twin", "Test Disruptor"});
    Vehicle& prize = addTestVehicle(s, r, theirs, home);
    prize.owner = kMe;
    const VehicleId id = prize.id;
    const ruleset::TechAreaId beams = techArea(r, "Test Beams");
    // Parts in design order (Laser II needs 3, the twins 5 once, the
    // Disruptor's 2 is ours already), then the hull (4).
    CHECK(analyzePairs(r, s, kMe, prize) == std::vector<TechPair>{{beams, 3}, {beams, 5}, {beams, 4}});
    CHECK(researchPotentialWord(analyzePairs(r, s, kMe, prize).size()) == "Sizable");
    // A destroyed part teaches nothing.
    const std::vector<int> intact = prize.damage;
    prize.damage[4] = entryStructure(r, s.design(theirs), 4);
    CHECK(analyzePairs(r, s, kMe, prize) == std::vector<TechPair>{{beams, 5}, {beams, 4}});
    prize.damage = intact;

    const Resources bank = s.empire(kMe).stockpile;
    const int64_t pool = s.empire(kMe).researchPool;
    s.empire(kMe).research = {{beams, 123}};
    REQUIRE(apply(r, s, kMe, cmd::Analyze{id}).ok);
    CHECK(s.vehicle(id) == nullptr);
    // Three levels, never above the highest required (5).
    CHECK(s.empire(kMe).techLevel(beams) == 5);
    // No refund, no research points; the queued project keeps its progress.
    CHECK(s.empire(kMe).stockpile == bank);
    CHECK(s.empire(kMe).researchPool == pool);
    REQUIRE(s.empire(kMe).research.size() == 1);
    CHECK(s.empire(kMe).research[0].progress == 123);
    // "Number Scrapped" on the builder's design record.
    CHECK(s.design(theirs).scrapped == 1);
    CHECK(s.design(theirs).lost == 0);
    // The entries of three levels gained: the project at 4, the new area at 5;
    // never "All Projects Completed", and nothing about the vehicle itself.
    CHECK(entries(s, kMe, "New Tech Level") == 3);
    CHECK(entries(s, kMe, "Test Beam Spying Developed") == 1);
    CHECK(entries(s, kMe, "Test Laser II Discovered") == 1);
    CHECK(entries(s, kMe, "New Tech Area Discovered") == 1);
    CHECK(entries(s, kMe, "All Projects Completed") == 0);
    const std::vector<std::string> titles = researchTitles(s, kMe);
    REQUIRE_FALSE(titles.empty());
    CHECK(titles.front() == "New Tech Level");
    CHECK(titles.back() == "New Tech Area Discovered");
    for (const LogEntry& l : s.empire(kMe).log) CHECK(l.title.find("Prize") == std::string::npos);
}

TEST_CASE("scrap window: Analyze checks neither the maximum level nor the area's requirements") {
    const auto rules = analyzeRules();
    const Rules& r = *rules;
    GameState s = newGame(r, false);
    const Location home = homeOf(s, kMe);
    const ruleset::TechAreaId cloaking = techArea(r, "Test Cloaking");  // stops at 3, needs Test Physics 3
    setLevel(r, s, kMe, "Test Physics", 1);
    const DesignId deep = design(s, r, kMe, "Deep", "Test Frigate", {"Test Deep Cloak", "Test Relic Part"});
    // Each analysis gives one level, whatever the level required: 1, 2, ... past the maximum.
    for (int expected = 1; expected <= 5; ++expected) {
        const VehicleId id = addTestVehicle(s, r, deep, home).id;
        CHECK(researchPotentialWord(analyzePairs(r, s, kMe, *s.vehicle(id)).size()) == "Moderate");
        REQUIRE(apply(r, s, kMe, cmd::Analyze{id}).ok);
        CHECK(s.empire(kMe).techLevel(cloaking) == expected);
    }
    // No more to learn there; a unique area never gains a level, though it counts in the potential.
    const VehicleId last = addTestVehicle(s, r, deep, home).id;
    CHECK(researchPotentialWord(analyzePairs(r, s, kMe, *s.vehicle(last)).size()) == "Minor");
    REQUIRE(apply(r, s, kMe, cmd::Analyze{last}).ok);
    CHECK(s.empire(kMe).techLevel(techArea(r, "Test Relics")) == 0);
    CHECK(s.design(deep).scrapped == 6);
    CHECK(researchPotentialWord(0) == "None");
    CHECK(researchPotentialWord(1) == "Minor");
    CHECK(researchPotentialWord(2) == "Moderate");
    CHECK(researchPotentialWord(4) == "Major");
    CHECK(researchPotentialWord(9) == "Major");
}

TEST_CASE("scrap window: Analyze needs a ship or base at an own working yard, uncloaked, in no fleet") {
    const Rules& r = engineRules();
    GameState s = newGame(r, false);
    const Location home = homeOf(s, kMe);
    const DesignId frigate = design(s, r, kMe, "F", "Test Frigate", {});
    const VehicleId ship = addTestVehicle(s, r, frigate, home).id;
    const VehicleId sat = addTestVehicle(s, r, addTestDesign(s, r, kMe, "S", "Test Satellite Hull", {"Test Satellite Gun"}), home).id;
    CHECK_FALSE(apply(r, s, kMe, cmd::Analyze{sat}).ok);  // never a unit group
    s.vehicle(ship)->status = VehicleStatus::Cloaked;
    CHECK_FALSE(apply(r, s, kMe, cmd::Analyze{ship}).ok);
    s.vehicle(ship)->status = VehicleStatus::Normal;
    REQUIRE(apply(r, s, kMe, cmd::CreateFleet{"Pair", {ship}}).ok);
    CHECK_FALSE(apply(r, s, kMe, cmd::Analyze{ship}).ok);
    REQUIRE(apply(r, s, kMe, cmd::LeaveFleet{ship}).ok);
    // A cloaked colony's yard does not work.
    homeworld(s, kMe).cloaked = true;
    CHECK_FALSE(scrapYardAt(r, s, kMe, home));
    CHECK_FALSE(apply(r, s, kMe, cmd::Analyze{ship}).ok);
    homeworld(s, kMe).cloaked = false;
    // A mothballed ship can be analyzed; mothballed, a yard ship is no yard.
    s.vehicle(ship)->status = VehicleStatus::Mothballed;
    CHECK(scrapActionProblem(r, s, kMe, *s.vehicle(ship), ScrapAction::Analyze).empty());
    CHECK(apply(r, s, kMe, cmd::Analyze{ship}).ok);
}

// ---- Fire On ---------------------------------------------------------------------------------------

TEST_CASE("scrap window: Fire On's armed test") {
    const Rules& r = engineRules();
    GameState s = newGame(r, false);
    const Location home = homeOf(s, kMe);
    auto spawn = [&](std::string_view name, std::string_view hull, std::initializer_list<std::string_view> parts) {
        return addTestVehicle(s, r, addTestDesign(s, r, kMe, name, hull, parts), home).id;
    };
    const VehicleId gunship = spawn("Gun", "Test Frigate", {"Test Bridge", "Test Engine", "Test Laser"});
    const VehicleId missiles = spawn("Missile", "Test Frigate", {"Test Bridge", "Test Engine", "Test Missile"});
    const VehicleId defense = spawn("Defense", "Test Frigate", {"Test Bridge", "Test Engine", "Test Point Defense"});
    const VehicleId fighters = spawn("Fighters", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    const VehicleId sats = spawn("Sats", "Test Satellite Hull", {"Test Satellite Gun"});
    const VehicleId mines = spawn("Mines", "Test Mine Hull", {"Test Warhead"});
    auto armed = [&](VehicleId id) { return armedForFireOn(r, s, *s.vehicle(id)); };
    CHECK(armed(gunship));
    CHECK(armed(missiles));
    CHECK_FALSE(armed(defense));  // point-defense does not arm a ship
    CHECK(armed(fighters));       // any weapon arms a fighter group
    CHECK_FALSE(armed(sats));     // satellites and minefields never count
    CHECK_FALSE(armed(mines));
    // A destroyed weapon still counts; a mothballed ship does not.
    s.vehicle(gunship)->damage.back() = entryStructure(r, s.design(s.vehicle(gunship)->design), 2);
    CHECK(armed(gunship));
    s.vehicle(gunship)->status = VehicleStatus::Mothballed;
    CHECK_FALSE(armed(gunship));
    s.vehicle(gunship)->status = VehicleStatus::Normal;
    // Another armed vehicle there is all it takes: it may be cloaked or in a fleet.
    CHECK(canBeFiredOn(r, s, *s.vehicle(sats)));
    CHECK(canBeFiredOn(r, s, *s.vehicle(gunship)));  // the missile ship is armed
    REQUIRE(apply(r, s, kMe, cmd::CreateFleet{"Hidden", {missiles}}).ok);
    s.vehicle(missiles)->status = VehicleStatus::Cloaked;
    s.vehicle(fighters)->count = 0;
    s.removeDeadVehicles();
    CHECK(canBeFiredOn(r, s, *s.vehicle(gunship)));
    // The last armed vehicle in the sector can never be fired on.
    REQUIRE(apply(r, s, kMe, cmd::LeaveFleet{missiles}).ok);
    s.vehicle(missiles)->count = 0;
    s.removeDeadVehicles();
    CHECK_FALSE(canBeFiredOn(r, s, *s.vehicle(gunship)));
    CHECK(canBeFiredOn(r, s, *s.vehicle(defense)));
}

TEST_CASE("scrap window: Fire On removes the vehicle with no battle; a turn-based game tests each vehicle again") {
    const Rules& r = engineRules();
    GameState s = newGame(r, false);
    const Location home = homeOf(s, kMe);
    const DesignId gun = addTestDesign(s, r, kMe, "Gun", "Test Frigate", {"Test Bridge", "Test Engine", "Test Laser"});
    const DesignId plain = addTestDesign(s, r, kMe, "Plain", "Test Frigate", {"Test Bridge", "Test Engine"});
    const DesignId fighterD = addTestDesign(s, r, kMe, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    const DesignId droneD = addTestDesign(s, r, kMe, "Drone", "Test Drone Hull", {"Test Engine", "Test Warhead"});
    const VehicleId x = addTestVehicle(s, r, gun, home).id;
    const VehicleId y = addTestVehicle(s, r, gun, home).id;
    const VehicleId z = addTestVehicle(s, r, plain, home).id;
    Vehicle& group = addTestVehicle(s, r, fighterD, home);
    group.count = 4;
    const VehicleId fighters = group.id;
    const VehicleId drone = addTestVehicle(s, r, droneD, home).id;
    const Resources bank = s.empire(kMe).stockpile;

    REQUIRE(apply(r, s, kMe, cmd::FireOn{z}).ok);
    CHECK(s.vehicle(z) == nullptr);
    CHECK(s.design(plain).lost == 1);
    CHECK(entries(s, kMe, "Vehicle Destroyed") == 1);
    const auto it = std::find_if(s.empire(kMe).log.begin(), s.empire(kMe).log.end(), [](const LogEntry& l) { return l.title == "Vehicle Destroyed"; });
    REQUIRE(it != s.empire(kMe).log.end());
    CHECK(it->category == LogCategory::Construction);
    CHECK(it->location == home);
    CHECK(it->target == LogGoto::Location);
    // A fighter group: every living unit is lost; a drone group records nothing.
    REQUIRE(apply(r, s, kMe, cmd::FireOn{fighters}).ok);
    CHECK(s.design(fighterD).lost == 4);
    CHECK(entries(s, kMe, "Group Destroyed") == 1);
    REQUIRE(apply(r, s, kMe, cmd::FireOn{drone}).ok);
    CHECK(s.design(droneD).lost == 0);
    CHECK(entries(s, kMe, "Vehicle Destroyed") == 2);
    // No refund, no experience, no kill: nobody fired.
    CHECK(s.empire(kMe).stockpile == bank);
    CHECK(s.design(gun).enemyTonnageDestroyed == 0);
    // Both gunships selected: the first goes, the second finds no armed companion left.
    REQUIRE(apply(r, s, kMe, cmd::FireOn{x}).ok);
    CHECK(s.vehicle(x) == nullptr);
    CHECK_FALSE(apply(r, s, kMe, cmd::FireOn{y}).ok);
    CHECK(s.vehicle(y) != nullptr);
    CHECK(s.design(gun).lost == 1);
}

// ---- Turn styles -----------------------------------------------------------------------------------

TEST_CASE("scrap window: in a simultaneous game each action becomes the vehicle's only order") {
    const Rules& r = engineRules();
    GameState s = newGame(r, true);
    const Location home = homeOf(s, kMe);
    const Location away{home.system, Sector{home.sector.x == 0 ? 1 : 0, home.sector.y}};
    // Crewed, three engines and supplies: each ship acts during the turn's movement.
    const DesignId plain = design(s, r, kMe, "Plain", "Test Frigate", {"Test Engine", "Test Engine", "Test Supply Pod"});
    const VehicleId scrapped = addTestVehicle(s, r, plain, home).id;
    REQUIRE(apply(r, s, kMe, cmd::SetOrders{scrapped, {}, {Order{OrderKind::MoveTo, away}}, true}).ok);
    const Resources bank = s.empire(kMe).stockpile;
    REQUIRE(apply(r, s, kMe, cmd::Scrap{scrapped}).ok);
    // Nothing happens yet: the list is cleared, Repeat goes off.
    REQUIRE(s.vehicle(scrapped));
    CHECK(s.vehicle(scrapped)->orders == std::vector<Order>{Order{OrderKind::Scrap, home}});
    CHECK_FALSE(s.vehicle(scrapped)->repeatOrders);
    CHECK(s.empire(kMe).stockpile == bank);
    CHECK(displayName(OrderKind::Scrap) == "Scrap / Analyze / Mothball");
    CHECK(displayName(OrderKind::Analyze) == "Deconstruct & Analyze");
    CHECK(displayName(OrderKind::FireOn) == "Fire On And Destroy");
    // Orders given afterwards go behind it; the window's orders come only from its commands.
    REQUIRE(apply(r, s, kMe, cmd::SetOrders{scrapped, {}, {Order{OrderKind::Scrap, home}, Order{OrderKind::MoveTo, away}}, false}).ok);
    CHECK_FALSE(apply(r, s, kMe, cmd::SetOrders{scrapped, {}, {Order{OrderKind::Scrap, home}, Order{OrderKind::Scrap, home}}, false}).ok);
    const VehicleId other = addTestVehicle(s, r, plain, home).id;
    CHECK_FALSE(apply(r, s, kMe, cmd::SetOrders{other, {}, {Order{OrderKind::Analyze, home}}, false}).ok);
    // A retrofit and an unmothballing (a mothballed vehicle acts on day 1).
    const DesignId armored = design(s, r, kMe, "Armored", "Test Frigate", {"Test Engine", "Test Engine", "Test Supply Pod", "Test Armor Plate"});
    const VehicleId refit = addTestVehicle(s, r, plain, home).id;
    REQUIRE(apply(r, s, kMe, cmd::Retrofit{refit, armored}).ok);
    CHECK(s.vehicle(refit)->orders.front().design == armored);
    CHECK(s.vehicle(refit)->design == plain);
    const VehicleId sleeper = addTestVehicle(s, r, plain, home).id;
    s.vehicle(sleeper)->status = VehicleStatus::Mothballed;
    s.vehicle(sleeper)->movement = 0;
    REQUIRE(apply(r, s, kMe, cmd::Mothball{sleeper, false}).ok);
    CHECK(s.vehicle(sleeper)->status == VehicleStatus::Mothballed);
    const Resources refund = scrapRefund(r, s, *s.vehicle(scrapped));
    const Resources unmothball = unmothballCharge(r, s, *s.vehicle(sleeper));
    Resources retrofit;
    REQUIRE(retrofitProblem(r, s, kMe, *s.vehicle(refit), armored, &retrofit).empty());

    movementPhase(r, s);
    CHECK(s.vehicle(scrapped) == nullptr);
    CHECK(s.design(plain).scrapped == 1);
    CHECK(s.empire(kMe).stockpile == bank + refund - retrofit - unmothball);
    CHECK(s.vehicle(refit)->design == armored);
    CHECK(s.vehicle(sleeper)->status == VehicleStatus::Normal);
}

TEST_CASE("scrap window: a simultaneous order is tested again when it acts; a failure clears the list") {
    const Rules& r = engineRules();
    GameState s = newGame(r, true);
    const Location home = homeOf(s, kMe);
    const DesignId gun = design(s, r, kMe, "Gun", "Test Frigate", {"Test Engine", "Test Engine", "Test Supply Pod", "Test Laser"});
    const DesignId plain = design(s, r, kMe, "Plain", "Test Frigate", {"Test Engine", "Test Engine", "Test Supply Pod"});
    // Two armed ships fired on: each saw the other when the orders were
    // given; whichever acts last finds no armed companion, and its order fails.
    const VehicleId x = addTestVehicle(s, r, gun, home).id;
    const VehicleId y = addTestVehicle(s, r, gun, home).id;
    REQUIRE(apply(r, s, kMe, cmd::FireOn{x}).ok);
    REQUIRE(apply(r, s, kMe, cmd::FireOn{y}).ok);
    // Analyze and Scrap at a yard that is gone by then.
    const VehicleId analyzed = addTestVehicle(s, r, plain, home).id;
    const VehicleId scrapped = addTestVehicle(s, r, plain, home).id;
    REQUIRE(apply(r, s, kMe, cmd::Analyze{analyzed}).ok);
    REQUIRE(apply(r, s, kMe, cmd::Scrap{scrapped}).ok);
    std::erase_if(homeworld(s, kMe).facilities, [&](uint32_t f) { return hasAbility(r.facilityAbilities(f), AbilityKind::SpaceYard); });
    s.empire(kMe).log.clear();
    movementPhase(r, s);
    const bool xLeft = s.vehicle(x) != nullptr, yLeft = s.vehicle(y) != nullptr;
    CHECK(xLeft != yLeft);
    const VehicleId survivor = xLeft ? x : y;
    CHECK(s.vehicle(survivor)->orders.empty());
    CHECK(s.design(gun).lost == 1);
    // Both failed and their lists are cleared; only the Scrap says why.
    REQUIRE(s.vehicle(analyzed));
    REQUIRE(s.vehicle(scrapped));
    CHECK(s.vehicle(analyzed)->orders.empty());
    CHECK(s.vehicle(scrapped)->orders.empty());
    int cancelled = 0;
    for (const LogEntry& l : s.empire(kMe).log)
        if (l.title.find("order cancelled") != std::string::npos) {
            ++cancelled;
            CHECK(l.title.find("Scrap") != std::string::npos);
        }
    CHECK(cancelled == 1);
}

TEST_CASE("scrap window: in a turn-based game each action is carried out at once, the order list untouched") {
    const Rules& r = engineRules();
    GameState s = newGame(r, false);
    const Location home = homeOf(s, kMe);
    const Location away{home.system, Sector{home.sector.x == 0 ? 1 : 0, home.sector.y}};
    const DesignId plain = addTestDesign(s, r, kMe, "Plain", "Test Frigate", {"Test Bridge", "Test Engine"});
    const DesignId armored = addTestDesign(s, r, kMe, "Armored", "Test Frigate", {"Test Bridge", "Test Engine", "Test Armor Plate"});
    const VehicleId id = addTestVehicle(s, r, plain, home).id;
    s.vehicle(id)->orders = {Order{OrderKind::MoveTo, away}};
    s.vehicle(id)->repeatOrders = true;
    REQUIRE(apply(r, s, kMe, cmd::Retrofit{id, armored}).ok);
    CHECK(s.vehicle(id)->design == armored);
    CHECK(s.vehicle(id)->orders == std::vector<Order>{Order{OrderKind::MoveTo, away}});
    CHECK(s.vehicle(id)->repeatOrders);
    REQUIRE(apply(r, s, kMe, cmd::Scrap{id}).ok);
    CHECK(s.vehicle(id) == nullptr);
    CHECK(s.design(armored).scrapped == 1);
}

TEST_CASE("scrap window: a self-destructed vehicle counts as scrapped, not lost") {
    const Rules& r = engineRules();
    for (const bool simultaneous : {false, true}) {
        CAPTURE(simultaneous);
        GameState s = newGame(r, simultaneous);
        const Location home = homeOf(s, kMe);
        const DesignId boom = design(s, r, kMe, "Boom", "Test Frigate", {"Test Engine", "Test Engine", "Test Supply Pod", "Test Self Destruct"});
        const DesignId plain = addTestDesign(s, r, kMe, "Plain", "Test Frigate", {"Test Bridge", "Test Engine"});
        const VehicleId id = addTestVehicle(s, r, boom, home).id;
        CHECK_FALSE(apply(r, s, kMe, cmd::SelfDestruct{addTestVehicle(s, r, plain, home).id}).ok);
        REQUIRE(apply(r, s, kMe, cmd::SelfDestruct{id}).ok);
        if (simultaneous) {
            CHECK(s.vehicle(id)->orders == std::vector<Order>{Order{OrderKind::SelfDestruct, home}});
            movementPhase(r, s);
        }
        CHECK(s.vehicle(id) == nullptr);
        CHECK(s.design(boom).scrapped == 1);
        CHECK(s.design(boom).lost == 0);
    }
}
