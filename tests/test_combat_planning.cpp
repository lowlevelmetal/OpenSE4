// The computer's targets and moves, combat groups, start positions, launches
// and the smaller rules settled in docs/spec/04 §19.2 (questions 57 to 77) and
// spec 03 §19 Q60 and Q68. Most tests set up a battle and put its pieces on
// chosen squares, then ask the battle what the strategies would do. All
// content is invented for the tests.

#include "combat_fixture.hpp"

#include "game/combat.hpp"
#include "game/combat_battle.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"
#include "game/tactical.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <optional>
#include <tuple>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::ctest;
using opense4::test::addTestDesign;
using opense4::test::addTestVehicle;
using opense4::test::homeworld;
namespace combat = opense4::game::combat;
using combat::detail::Battle;
using combat::TacticalOrder;
using OK = combat::TacticalOrder::Kind;
using ruleset::WeaponKind;

namespace {

// A battle set up on an arena, to put pieces on chosen squares and ask what
// the strategies would do.
struct Bench {
    const Rules& r;
    Arena ar;
    TurnContext ctx;
    Rng rng;
    std::optional<Battle> b;

    explicit Bench(const Rules& rules, Arena arena) : r(rules), ar(std::move(arena)), ctx(context(ar.s, rules)), rng(ar.s.rng.fork()) {}
    explicit Bench(Arena arena) : Bench(combatRules(), std::move(arena)) {}

    Battle& start(std::vector<EmpireId> players = {}, combat::BattleCheck check = {}) {
        b.emplace(ctx, ar.loc, rng);
        b->setCheck(std::move(check));
        REQUIRE(b->setup());
        if (!players.empty()) b->setPlayers(std::move(players));
        return *b;
    }
    int at(VehicleId v) const {
        for (size_t i = 0; i < b->pieces().size(); ++i)
            if (b->pieces()[i].source == v) return static_cast<int>(i);
        FAIL("no piece for vehicle " << v.value);
        return -1;
    }
    int planet(ObjectId o) const {
        for (size_t i = 0; i < b->pieces().size(); ++i)
            if (b->pieces()[i].object == o) return static_cast<int>(i);
        FAIL("no piece for planet " << o.value);
        return -1;
    }
    // Every listed piece to its square (out of the way first, so no two meet).
    void arrange(std::initializer_list<std::tuple<int, int, int>> where) {
        int k = 0;
        for (const auto& [i, x, y] : where) b->placeAt(i, 1 + 2 * k++, 1);
        for (const auto& [i, x, y] : where) b->placeAt(i, x, y);
    }
    int targetsOn(const combat::detail::Targeting& t, int piece) const {
        return static_cast<int>(std::count_if(t.arms.begin(), t.arms.end(), [&](const combat::detail::Arm& a) { return a.target == piece; }));
    }
};

int cheb(int x0, int y0, int x1, int y1) { return std::max(std::abs(x0 - x1), std::abs(y0 - y1)); }

// A vehicle on the given rules (spawn uses the shared combat rules).
VehicleId spawnOn(const Rules& rules, GameState& s, DesignId d, Location where, int count = 1) {
    Vehicle& v = addTestVehicle(s, rules, d, where);
    v.count = count;
    return v.id;
}

void addStrategy(GameState& s, EmpireId e, std::string name, std::vector<std::pair<std::string, std::string>> settings) {
    s.empire(e).strategies.push_back(ruleset::CombatStrategy{std::move(name), std::move(settings)});
}

} // namespace

// ---- Giving out targets (spec 04 §16, §19.2 Q60) ---------------------------------------------------------

TEST_CASE("planning: weapons go to the first B candidates in rounds, each up to its overkill limit") {
    ruleset::Ruleset rs = buildCombatRuleset();
    part(rs, "CT Tracker", 10, {ab(AbilityKind::MultiplexTracking, 2)});
    rs.reindex();
    const Rules rules{std::move(rs)};
    auto run = [&](bool tracker, bool damageBarge, bool bargeFirst) {
        Bench k(rules, makeArena(rules));
        GameState& s = k.ar.s;
        const VehicleId gunboat = spawnOn(rules, s, tracker ? frigate(s, k.ar.a, "Gunboat", 1, {"CT Gun", "CT Gun", "CT Gun", "CT Gun", "CT Gun", "CT Gun", "CT Tracker"}, rules)
                                                   : frigate(s, k.ar.a, "Gunboat", 1, {"CT Gun", "CT Gun", "CT Gun", "CT Gun", "CT Gun", "CT Gun"}, rules),
                                         k.ar.loc);
        const VehicleId dinghy = addTestVehicle(s, rules, addTestDesign(s, rules, k.ar.b, "Dinghy", "Test Frigate", {"Test Bridge"}), k.ar.loc).id;
        const VehicleId barge =
            addTestVehicle(s, rules, addTestDesign(s, rules, k.ar.b, "Barge", "Test Frigate", {"Test Bridge", "CT Big Armor"}), k.ar.loc).id;
        if (damageBarge) combat::detail::destroyEntry(rules, s.design(s.vehicle(barge)->design), *s.vehicle(barge), 1);
        Battle& b = k.start();
        const int g = k.at(gunboat), d = k.at(dinghy), w = k.at(barge);
        k.arrange({{g, 20, 20}, {d, 22, 20}, {w, 24, 20}});
        if (bargeFirst) k.arrange({{g, 20, 20}, {w, 22, 20}, {d, 24, 20}});
        return std::tuple{b.targetsFor(g, false), d, w, k.b->targetsFor(g, true)};
    };
    {
        // Budget 1: only the nearest candidate takes weapons, until 1.5 × (its
        // shields + full hit points) = 30 is reached: three 10-point guns.
        const auto [t, d, w, firing] = run(false, false, false);
        CHECK(t.main == d);
        CHECK(std::count_if(t.arms.begin(), t.arms.end(), [&](const auto& a) { return a.target == d; }) == 3);
        CHECK(std::count_if(t.arms.begin(), t.arms.end(), [&](const auto& a) { return a.target == w; }) == 0);
    }
    {
        // Budget 2: the second candidate takes the rest.
        const auto [t, d, w, firing] = run(true, false, false);
        CHECK(std::count_if(t.arms.begin(), t.arms.end(), [&](const auto& a) { return a.target == d; }) == 3);
        CHECK(std::count_if(t.arms.begin(), t.arms.end(), [&](const auto& a) { return a.target == w; }) == 3);
    }
    {
        // The limit uses full hit points, not what is left: a barge that lost
        // its armor still takes all six guns.
        const auto [t, d, w, firing] = run(false, true, true);
        CHECK(t.main == w);
        CHECK(std::count_if(t.arms.begin(), t.arms.end(), [&](const auto& a) { return a.target == w; }) == 6);
    }
}

TEST_CASE("planning: firing counts only candidates within the longest ready range") {
    Bench k(makeArena());
    GameState& s = k.ar.s;
    const VehicleId gunboat = spawn(s, frigate(s, k.ar.a, "Gunboat", 1, {"CT Gun"}), k.ar.loc);   // reach 5
    const VehicleId far = spawn(s, design(s, k.ar.b, "Far", "Test Frigate", {"Test Bridge"}), k.ar.loc);
    const VehicleId near = spawn(s, design(s, k.ar.b, "Near", "Test Frigate", {"Test Bridge", "CT Big Armor"}), k.ar.loc);
    useStrategy(s, k.ar.a, {{"Targeting Priority 1", "Farthest"}});
    Battle& b = k.start();
    const int g = k.at(gunboat), f = k.at(far), n = k.at(near);
    k.arrange({{g, 20, 20}, {f, 28, 20}, {n, 23, 20}});
    CHECK(b.targetsFor(g, false).main == f);   // planning: no distance check
    const combat::detail::Targeting firing = b.targetsFor(g, true);
    CHECK(firing.main == n);
    CHECK(k.targetsOn(firing, n) == 1);
}

TEST_CASE("planning: push weapons spread, point-defense and warheads get targets, a fighter group takes one") {
    ruleset::Ruleset rs = buildCombatRuleset();
    part(rs, "CT Tracker", 10, {ab(AbilityKind::MultiplexTracking, 2)});
    rs.reindex();
    const Rules rules{std::move(rs)};
    {
        // A push closes its candidate for the round (history 1.73).
        Bench k(rules, makeArena(rules));
        GameState& s = k.ar.s;
        const VehicleId tug = spawnOn(rules, s, frigate(s, k.ar.a, "Tug", 1, {"CT Tractor", "CT Tractor", "CT Tracker"}, rules), k.ar.loc);
        const DesignId hulk = addTestDesign(s, rules, k.ar.b, "Hulk", "Test Frigate", {"Test Bridge", "CT Big Armor"});
        const VehicleId h1 = addTestVehicle(s, rules, hulk, k.ar.loc).id, h2 = addTestVehicle(s, rules, hulk, k.ar.loc).id;
        Battle& b = k.start();
        k.arrange({{k.at(tug), 20, 20}, {k.at(h1), 21, 20}, {k.at(h2), 23, 20}});
        const combat::detail::Targeting t = b.targetsFor(k.at(tug), false);
        CHECK(k.targetsOn(t, k.at(h1)) == 1);
        CHECK(k.targetsOn(t, k.at(h2)) == 1);
    }
    {
        Bench k(rules, makeArena(rules));
        GameState& s = k.ar.s;
        const VehicleId picket = spawnOn(rules, s, frigate(s, k.ar.a, "Picket", 1, {"CT PD", "Test Warhead"}, rules), k.ar.loc);
        const DesignId wasp = addTestDesign(s, rules, k.ar.b, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Short Gun"});
        const VehicleId bees = addTestVehicle(s, rules, wasp, k.ar.loc).id;
        s.vehicle(bees)->count = 3;
        const VehicleId barge = addTestVehicle(s, rules, addTestDesign(s, rules, k.ar.a, "Barge", "Test Frigate", {"Test Bridge", "CT Big Armor"}), k.ar.loc).id;
        const VehicleId other = addTestVehicle(s, rules, addTestDesign(s, rules, k.ar.a, "Other", "Test Frigate", {"Test Bridge", "CT Big Armor"}), k.ar.loc).id;
        Battle& b = k.start();
        const int p = k.at(picket), f = k.at(bees);
        k.arrange({{p, 20, 20}, {f, 21, 20}, {k.at(barge), 21, 22}, {k.at(other), 23, 20}});
        const combat::detail::Targeting t = b.targetsFor(p, false);
        int pd = 0, warhead = 0;
        for (const combat::detail::Arm& a : t.arms) {
            if (a.target != f) continue;
            if (a.warhead) ++warhead;
            else ++pd;
        }
        CHECK(pd == 1);
        CHECK(warhead == 1);
        // The fighter group gives all its weapons the first candidate its first ready weapon can hit.
        const combat::detail::Targeting ft = b.targetsFor(f, false);
        REQUIRE(ft.arms.size() == 2);
        CHECK(ft.arms[0].target >= 0);
        CHECK(ft.arms[0].target == ft.arms[1].target);
    }
}

// ---- The attack map and the square (spec 04 §16.1, §19.2 Q61) --------------------------------------------

TEST_CASE("planning: the attack map - point-defense only when nothing else adds, the border over-count, the weighted emissive cut") {
    ruleset::Ruleset rs = buildCombatRuleset();
    gun(rs, "CT Engine Cutter", WeaponKind::DirectFire, {100, 100, 100}, "Only Engines");
    gun(rs, "CT Small Cutter", WeaponKind::DirectFire, {50, 50, 50}, "Only Engines");
    rs.reindex();
    const Rules rules{std::move(rs)};
    {
        Bench k(rules, makeArena(rules));
        GameState& s = k.ar.s;
        const VehicleId duo = spawnOn(rules, s, frigate(s, k.ar.a, "Duo", 1, {"CT Gun", "CT PD"}, rules), k.ar.loc);
        const VehicleId solo = spawnOn(rules, s, frigate(s, k.ar.a, "Solo", 1, {"CT Gun"}, rules), k.ar.loc);
        const VehicleId guard = spawnOn(rules, s, frigate(s, k.ar.a, "Guard", 1, {"CT PD"}, rules), k.ar.loc);
        const VehicleId bees = addTestVehicle(s, rules, addTestDesign(s, rules, k.ar.b, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"}), k.ar.loc).id;
        Battle& b = k.start();
        const int f = k.at(bees);
        k.arrange({{k.at(duo), 20, 20}, {k.at(solo), 20, 24}, {k.at(guard), 20, 28}, {f, 3, 30}});
        const std::vector<int64_t> withPd = b.attackMapFor(k.at(duo));
        CHECK(withPd == b.attackMapFor(k.at(solo)));
        const std::vector<int64_t> pdOnly = b.attackMapFor(k.at(guard));
        CHECK(std::any_of(pdOnly.begin(), pdOnly.end(), [](int64_t v) { return v > 0; }));
        // Rings cut at the border collapse onto it: (0, 30) is counted at distances 3, 4 and 5.
        auto at = [](const std::vector<int64_t>& m, int x, int y) { return m[static_cast<size_t>(y * combat::kCombatMapWidth + x)]; };
        CHECK(at(withPd, 1, 30) == 10);
        CHECK(at(withPd, 0, 30) == 30);
    }
    {
        // Against a ship whose shields are no more than its regeneration, a
        // weighted value below its emissive armor (15) counts 0, weapon by
        // weapon: a fifth of 100 counts, a fifth of 50 does not.
        Bench k(rules, makeArena(rules));
        GameState& s = k.ar.s;
        const VehicleId cutter = spawnOn(rules, s, frigate(s, k.ar.a, "Cutter", 1, {"CT Engine Cutter", "CT Small Cutter"}, rules), k.ar.loc);
        const VehicleId glow = spawnOn(rules, s, frigate(s, k.ar.b, "Glow", 1, {"CT Emissive Armor"}, rules), k.ar.loc);
        Battle& b = k.start();
        const int t = k.at(glow);
        k.arrange({{k.at(cutter), 20, 20}, {t, 30, 30}});
        const std::vector<int64_t> map = b.attackMapFor(k.at(cutter));
        CHECK(map[static_cast<size_t>(30 * combat::kCombatMapWidth + 31)] == 20);
    }
}

TEST_CASE("planning: the range strategies never keep their own square; Optimal's third case takes the most damage") {
    ruleset::Ruleset rs = buildCombatRuleset();
    gun(rs, "CT Tickler", WeaponKind::DirectFire, {4, 1}, "Normal");
    gun(rs, "CT Brutal", WeaponKind::DirectFire, {100, 20}, "Normal");
    rs.reindex();
    const Rules rules{std::move(rs)};
    {
        // The best square is the mover's own: it goes to the best square left.
        Bench k(rules, makeArena(rules));
        GameState& s = k.ar.s;
        useStrategy(s, k.ar.a, {{"Primary Movement Strategy", "Optimal Weapons Range"}});
        const VehicleId mover = spawnOn(rules, s, frigate(s, k.ar.a, "Mover", 2, {"CT Gun"}, rules), k.ar.loc);
        const DesignId hulk = addTestDesign(s, rules, k.ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor"});
        std::vector<VehicleId> hulks;
        for (int n = 0; n < 8; ++n) hulks.push_back(addTestVehicle(s, rules, hulk, k.ar.loc).id);
        Battle& b = k.start();
        const int m = k.at(mover);
        // The target at (10, 10); the mover and seven hulks fill the squares around it.
        k.arrange({{m, 11, 10}, {k.at(hulks[0]), 10, 10}, {k.at(hulks[1]), 9, 9}, {k.at(hulks[2]), 10, 9}, {k.at(hulks[3]), 11, 9},
                   {k.at(hulks[4]), 9, 10}, {k.at(hulks[5]), 9, 11}, {k.at(hulks[6]), 10, 11}, {k.at(hulks[7]), 11, 11}});
        const combat::detail::MovePlan mv = b.planFor(m);
        CHECK(mv.dest != std::pair{11, 10});
        CHECK(cheb(mv.dest.first, mv.dest.second, 10, 10) == 2);
    }
    {
        // Every square where it can deal damage has 1000 × danger ÷ damage of
        // 9999 or more: it takes the most damage (range 1), not the lowest ratio (range 2).
        Bench k(rules, makeArena(rules));
        GameState& s = k.ar.s;
        useStrategy(s, k.ar.a, {{"Primary Movement Strategy", "Optimal Weapons Range"}});
        const VehicleId mover = spawnOn(rules, s, frigate(s, k.ar.a, "Mover", 2, {"CT Tickler"}, rules), k.ar.loc);
        const VehicleId fort = addTestVehicle(s, rules, addTestDesign(s, rules, k.ar.b, "Fort", "Test Station", {"Test Bridge", "CT Brutal", "CT Big Armor"}), k.ar.loc).id;
        Battle& b = k.start();
        const int m = k.at(mover);
        k.arrange({{m, 30, 20}, {k.at(fort), 20, 20}});
        const combat::detail::MovePlan mv = b.planFor(m);
        CHECK(cheb(mv.dest.first, mv.dest.second, 20, 20) == 1);
    }
}

TEST_CASE("planning: Maximum Weapons Range counts point-defense as a ready weapon") {
    Bench k(makeArena());
    GameState& s = k.ar.s;
    useStrategy(s, k.ar.a, {{"Primary Movement Strategy", "Maximum Weapons Range"}});
    const VehicleId guard = spawn(s, frigate(s, k.ar.a, "Guard", 2, {"CT PD"}), k.ar.loc);
    const VehicleId target = spawn(s, frigate(s, k.ar.b, "Target", 1, {"CT Gun"}), k.ar.loc);
    Battle& b = k.start();
    const int g = k.at(guard), t = k.at(target);
    k.arrange({{g, 30, 20}, {t, 20, 20}});
    // It has a ready weapon and can deal damage nowhere: the fallback, not the
    // ring at the target's range + movement + 2 that a piece without ready weapons heads for.
    const int ring = 5 + b.maxMovement(t) + 2;
    const combat::detail::MovePlan mv = b.planFor(g);
    CHECK(cheb(mv.dest.first, mv.dest.second, 20, 20) != ring);
}

// ---- Surrounded pieces (spec 04 §16.1, spec 03 §10) -------------------------------------------------------

TEST_CASE("planning: a surrounded leader's group dissolves before it plans; it does not move but fires") {
    Bench k(makeArena());
    GameState& s = k.ar.s;
    const DesignId liner = frigate(s, k.ar.a, "Liner", 2, {"CT Gun"});
    const VehicleId lead = spawn(s, liner, k.ar.loc), wing = spawn(s, liner, k.ar.loc);
    Fleet f;
    f.owner = k.ar.a;
    f.members = {lead, wing};
    f.leader = lead;
    f.formation = 0;
    const FleetId fid = s.addFleet(f).id;
    for (VehicleId v : {lead, wing}) s.vehicle(v)->fleet = fid;
    const DesignId hulk = design(s, k.ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor"});
    std::vector<VehicleId> hulks;
    for (int n = 0; n < 7; ++n) {
        hulks.push_back(spawn(s, hulk, k.ar.loc));
        warpIn(s, hulks.back());   // B is the attacker: A acts first
    }
    Battle& b = k.start({k.ar.b});
    const int l = k.at(lead), w = k.at(wing);
    k.arrange({{l, 20, 20}, {w, 21, 21}, {k.at(hulks[0]), 19, 19}, {k.at(hulks[1]), 20, 19}, {k.at(hulks[2]), 21, 19},
               {k.at(hulks[3]), 19, 20}, {k.at(hulks[4]), 21, 20}, {k.at(hulks[5]), 19, 21}, {k.at(hulks[6]), 20, 21}});
    REQUIRE(b.pieces()[static_cast<size_t>(l)].isLeader);
    REQUIRE(b.pieces()[static_cast<size_t>(w)].group == b.pieces()[static_cast<size_t>(l)].group);
    b.advance();   // A's phase; it stops at B's
    REQUIRE(b.phaseEmpire() == k.ar.b);
    const combat::detail::Piece& L = b.pieces()[static_cast<size_t>(l)];
    CHECK_FALSE(L.isLeader);
    CHECK(b.pieces()[static_cast<size_t>(w)].group == -1);
    CHECK(std::pair{L.x, L.y} == std::pair{20, 20});
    CHECK(L.fired);
}

// ---- Launches (spec 04 §16.1, §10.7, §19.2 Q62) -----------------------------------------------------------

TEST_CASE("planning: a carrier reached again launches again in the same phase; Anti-Planet Drones are not batched") {
    auto launches = [](bool antiPlanet, std::string_view perTarget) {
        Bench k(makeArena());
        GameState& s = k.ar.s;
        useStrategy(s, k.ar.a, {{"Primary Movement Strategy", "Don't Get Hurt"}, {"Drones Per Target", std::string(perTarget)}});
        addStrategy(s, k.ar.a, "Drone Attack", {{"Primary Movement Strategy", "Ram"}});
        const DesignId dart = design(s, k.ar.a, "Dart", "Test Drone Hull", {"Test Engine", "Test Engine", "Test Engine", "Test Engine", "CT Combat Thruster", "Test Warhead"});
        s.design(dart).strategy = 1;
        if (antiPlanet) s.design(dart).designType = "Anti-Planet Drone";
        const VehicleId mother = spawn(s, design(s, k.ar.a, "Mother", "Test Station", {"Test Bridge", "CT Drone Bay", "CT Big Armor"}), k.ar.loc);
        s.vehicle(mother)->cargo.units.push_back({dart, 4});
        const VehicleId hulk = spawn(s, design(s, k.ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor"}), k.ar.loc);
        Battle& b = k.start();
        const int carrier = k.at(mother);
        k.arrange({{carrier, 20, 20}, {k.at(hulk), 22, 20}});
        b.run();
        int first = 0;
        for (const CombatEvent& e : b.record().events)
            if (e.kind == CombatEvent::Kind::Launch && static_cast<int>(e.target) == carrier && e.round == 1) first += e.amount;
        return first;
    };
    // Drones Per Target 1 and one hostile: one drone at a time. It rams and dies,
    // and the carrier, reached again, launches the next within its rate of 2.
    CHECK(launches(false, "1") == 2);
    CHECK(launches(true, "1") == 0);
    CHECK(launches(true, "0") == 2);   // an option of 0 launches every drone
}

// ---- Board, Ram and Drop Troops (spec 04 §16.1, §19.2 Q69) ------------------------------------------------

TEST_CASE("planning: boarding picks a ship it can take, the latest hull first; ramming never picks planets or unit groups") {
    {
        Bench k(makeArena());
        GameState& s = k.ar.s;
        useStrategy(s, k.ar.a, {{"Primary Movement Strategy", "Board Enemy Ships"}});
        const VehicleId boarder = spawn(s, frigate(s, k.ar.a, "Boarder", 2, {"Test Boarding Party", "Test Boarding Party"}), k.ar.loc);   // 40
        const VehicleId skiff = spawn(s, design(s, k.ar.b, "Skiff", "Test Frigate", {"Test Bridge"}), k.ar.loc);
        const VehicleId galleon = spawn(s, design(s, k.ar.b, "Galleon", "CT Giant Hull", {"Test Bridge", "Test Crew Quarters"}), k.ar.loc);
        const VehicleId fortress = spawn(s, design(s, k.ar.b, "Fortress", "Test Frigate", {"Test Bridge", "Test Security Station", "Test Security Station"}), k.ar.loc);
        Battle& b = k.start();
        const int bo = k.at(boarder), sk = k.at(skiff), ga = k.at(galleon), fo = k.at(fortress);
        k.arrange({{bo, 20, 20}, {fo, 21, 20}, {sk, 23, 20}, {ga, 27, 20}});
        // The fortress defends with 40: not below 40. The galleon's hull comes later than the skiff's.
        CHECK(b.planFor(bo).target == ga);
        b.piece(ga).sh.current = 5;   // shields up: not a candidate
        CHECK(b.planFor(bo).target == sk);
        b.piece(sk).sh.current = 5;
        CHECK(b.planFor(bo).target == -1);
    }
    {
        Bench k(makeArena());
        GameState& s = k.ar.s;
        useStrategy(s, k.ar.a, {{"Primary Movement Strategy", "Ram"}});
        const VehicleId rammer = spawn(s, frigate(s, k.ar.a, "Rammer", 4, {"CT Gun"}), k.ar.loc);
        const VehicleId bees = spawn(s, design(s, k.ar.b, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"}), k.ar.loc, 3);
        const VehicleId skiff = spawn(s, design(s, k.ar.b, "Skiff", "Test Frigate", {"Test Bridge"}), k.ar.loc);
        const VehicleId giant = spawn(s, design(s, k.ar.b, "Giant", "CT Giant Hull", {"Test Bridge", "CT Big Armor"}), k.ar.loc);
        Battle& b = k.start();
        const int ra = k.at(rammer), gi = k.at(giant);
        k.arrange({{ra, 20, 20}, {k.at(bees), 21, 20}, {k.at(skiff), 22, 22}, {gi, 25, 20}});
        const combat::detail::MovePlan mv = b.planFor(ra);
        CHECK(mv.target == gi);
        // It approaches through the free square nearest to it around the
        // target's top-left square, the first found (column by column) on ties.
        CHECK(mv.dest == std::pair{24, 19});
    }
}

TEST_CASE("planning: Drop Troops takes a colony where another empire's troops fight too") {
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    Colony& hw = homeworld(s, ar.b);
    const Location there = locationOf(s.galaxy, hw.planet);
    const DesignId trooper = design(s, ar.c, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    hw.landedTroops = {{trooper, 2}};
    hw.invader = ar.c;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Drop Troops"}});
    const DesignId mine = design(s, ar.a, "Marine", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transport = spawn(s, frigate(s, ar.a, "Transport", 2, {"Test Cargo Bay", "Test Cargo Bay"}), there);
    s.vehicle(transport)->cargo.units.push_back({mine, 4});
    setTreaty(s, ar.a, ar.c, Treaty::Partnership);
    ar.loc = there;
    Bench k(std::move(ar));
    Battle& b = k.start();
    CHECK(b.planFor(k.at(transport)).target == k.planet(hw.planet));
}

// ---- Combat groups (spec 04 §3 steps 5 and 8, §5, §19.2 Q64) ----------------------------------------------

TEST_CASE("groups: member numbers are given out again, and places follow the current leader's formation") {
    ruleset::Ruleset rs = buildCombatRuleset();
    ruleset::Formation column;
    column.name = "Test Column";
    column.positions = {{0, -1, "Any"}, {0, -2, "Any"}, {0, -3, "Any"}};
    rs.formations.push_back(column);
    rs.reindex();
    const Rules rules{std::move(rs)};
    Bench k(rules, makeArena(rules));
    GameState& s = k.ar.s;
    const DesignId liner = frigate(s, k.ar.a, "Liner", 6, {"CT Gun"}, rules);   // 3 movement points
    std::vector<VehicleId> ships;
    for (int n = 0; n < 5; ++n) ships.push_back(spawnOn(rules, s, liner, k.ar.loc));
    warpIn(s, spawnOn(rules, s, frigate(s, k.ar.b, "Target", 1, {}, rules), k.ar.loc));
    Battle& b = k.start({k.ar.a});
    b.advance();
    REQUIRE(b.phaseEmpire() == k.ar.a);
    std::vector<int> p;
    for (VehicleId v : ships) p.push_back(k.at(v));
    k.arrange({{p[0], 20, 20}, {p[1], 25, 25}, {p[2], 26, 25}, {p[3], 27, 25}, {p[4], 28, 25}});
    auto order = [&](OK kind, int piece, int group, int formation = -1) {
        TacticalOrder o{kind, k.ar.a, piece};
        o.group = group;
        o.formation = formation;
        return b.submit(o);
    };
    REQUIRE(order(OK::SetLeader, p[0], 3, 0).empty());
    REQUIRE(order(OK::SetMember, p[1], 3).empty());
    REQUIRE(order(OK::SetMember, p[2], 3).empty());
    CHECK(b.pieces()[static_cast<size_t>(p[2])].member == 2);
    REQUIRE(order(OK::ClearGroup, p[2], -1).empty());
    REQUIRE(order(OK::SetMember, p[3], 3).empty());
    CHECK(b.pieces()[static_cast<size_t>(p[3])].member == 2);   // the freed highest number again
    // A new leader of the number with another formation: every member takes
    // the position with its number in it.
    REQUIRE(order(OK::ClearGroup, p[0], -1).empty());
    CHECK(b.leaderOf(p[1]) == -1);
    CHECK(b.pieces()[static_cast<size_t>(p[1])].group == 3);
    REQUIRE(order(OK::SetLeader, p[4], 3, 1).empty());
    CHECK(b.leaderOf(p[1]) == p[4]);
    CHECK(b.leaderOf(p[3]) == p[4]);
    TacticalOrder mv{OK::Move, k.ar.a, p[4]};
    mv.path = {combat::Square{28, 26}};   // facing 2 (down): the column's (0, -n) turns to (0, +n)
    REQUIRE(b.submit(mv).empty());
    CHECK(std::pair{b.pieces()[static_cast<size_t>(p[1])].x, b.pieces()[static_cast<size_t>(p[1])].y} == std::pair{28, 27});
    CHECK(std::pair{b.pieces()[static_cast<size_t>(p[3])].x, b.pieces()[static_cast<size_t>(p[3])].y} == std::pair{28, 28});
}

TEST_CASE("groups: fleet groups are numbered with the tactical groups; a fleet ship in any group uses the fleet's strategy") {
    Bench k(makeArena());
    GameState& s = k.ar.s;
    useStrategy(s, k.ar.a, {{"Primary Movement Strategy", "Optimal Weapons Range"}});
    addStrategy(s, k.ar.a, "Fleet Plan", {{"Primary Movement Strategy", "Point Blank"}});
    const DesignId liner = frigate(s, k.ar.a, "Liner", 2, {"CT Gun"});
    std::vector<VehicleId> ships;
    for (int n = 0; n < 5; ++n) ships.push_back(spawn(s, liner, k.ar.loc));
    for (int fl = 0; fl < 2; ++fl) {
        Fleet f;
        f.owner = k.ar.a;
        f.members = {ships[static_cast<size_t>(2 * fl)], ships[static_cast<size_t>(2 * fl + 1)]};
        f.leader = f.members.front();
        f.formation = 0;
        f.strategy = 1;
        const FleetId fid = s.addFleet(f).id;
        for (VehicleId v : f.members) s.vehicle(v)->fleet = fid;
    }
    warpIn(s, spawn(s, frigate(s, k.ar.b, "Target", 1, {}), k.ar.loc));
    Battle& b = k.start({k.ar.a});
    b.advance();
    REQUIRE(b.phaseEmpire() == k.ar.a);
    const int l1 = k.at(ships[0]), m1 = k.at(ships[1]), l2 = k.at(ships[2]), m2 = k.at(ships[3]), free = k.at(ships[4]);
    CHECK(b.pieces()[static_cast<size_t>(l1)].group == 1);
    CHECK(b.pieces()[static_cast<size_t>(l2)].group == 2);
    CHECK(b.pieces()[static_cast<size_t>(m1)].member == 1);
    CHECK(b.leaderOf(m2) == l2);
    TacticalOrder lead{OK::SetLeader, k.ar.a, free};
    lead.group = 1;
    lead.formation = 0;
    CHECK(b.check(lead) == "Group 1 already has a leader.");
    // Clearing the fleet's leader clears only it; its member follows whoever leads 1 next.
    REQUIRE(b.submit(TacticalOrder{OK::ClearGroup, k.ar.a, l1}).empty());
    CHECK(b.leaderOf(m1) == -1);
    REQUIRE(b.submit(lead).empty());
    CHECK(b.leaderOf(m1) == free);
    // The fleet's strategy in any group, the design's outside every group.
    CHECK(b.strategyIndexOf(m1) == 1);
    CHECK(b.strategyIndexOf(l1) == 0);
    TacticalOrder own{OK::SetLeader, k.ar.a, l1};
    own.group = 7;
    own.formation = 0;
    REQUIRE(b.submit(own).empty());
    CHECK(b.strategyIndexOf(l1) == 1);
    CHECK(b.strategyIndexOf(free) == 0);   // in a group, but in no fleet
}

TEST_CASE("groups: an automated side's leader with no movement left dissolves its group when hit") {
    // Spec 03 §10, §19 Q60 (confirmed: binary): after a hit it survives, even
    // one its shields absorb completely.
    Bench k(makeArena());
    GameState& s = k.ar.s;
    useStrategy(s, k.ar.a, {{"Primary Movement Strategy", "Point Blank"}});
    const DesignId liner = frigate(s, k.ar.a, "Liner", 2, {"CT Gun", "CT Big Armor"});
    const VehicleId lead = spawn(s, liner, k.ar.loc), wing = spawn(s, liner, k.ar.loc);
    Fleet f;
    f.owner = k.ar.a;
    f.members = {lead, wing};
    f.leader = lead;
    f.formation = 0;
    const FleetId fid = s.addFleet(f).id;
    for (VehicleId v : {lead, wing}) s.vehicle(v)->fleet = fid;
    const VehicleId shooter = spawn(s, frigate(s, k.ar.b, "Shooter", 1, {"CT Gun", "CT Always Hit", "CT Big Armor"}), k.ar.loc);
    warpIn(s, shooter);
    Battle& b = k.start({k.ar.b});
    const int l = k.at(lead), w = k.at(wing), sh = k.at(shooter);
    k.arrange({{l, 30, 30}, {w, 29, 30}, {sh, 32, 30}});
    b.advance();   // A, the computer, moves: its leader has no movement left
    REQUIRE(b.phaseEmpire() == k.ar.b);
    REQUIRE(b.pieces()[static_cast<size_t>(l)].alive);
    REQUIRE(b.pieces()[static_cast<size_t>(l)].mp == 0);
    REQUIRE(b.pieces()[static_cast<size_t>(w)].group >= 0);
    TacticalOrder fire{OK::Fire, k.ar.b, sh, l};
    REQUIRE(b.check(fire).empty());
    REQUIRE(b.submit(fire).empty());
    CHECK_FALSE(b.pieces()[static_cast<size_t>(l)].isLeader);
    CHECK(b.pieces()[static_cast<size_t>(w)].group == -1);
}

// ---- Start positions (spec 04 §3 step 4, §19.2 Q57, Q58) --------------------------------------------------

TEST_CASE("placement: empires in the middle are numbered by their first piece in the sector, in object order") {
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    // A's homeworld is elsewhere in this system; it plays no part.
    const VehicleId a = spawn(s, frigate(s, ar.a, "A", 1, {}), ar.loc);
    const VehicleId b = spawn(s, frigate(s, ar.b, "B", 1, {}), ar.loc);
    const VehicleId c = spawn(s, frigate(s, ar.c, "C", 1, {}), ar.loc);
    // Slots, not ages: C took the first slot, then B, then A (spec 03 §19 Q62).
    std::swap(s.vehicle(a)->slot, s.vehicle(c)->slot);
    REQUIRE(s.vehicle(c)->slot < s.vehicle(b)->slot);
    REQUIRE(s.vehicle(b)->slot < s.vehicle(a)->slot);
    Bench k(std::move(ar));
    Battle& bt = k.start();
    auto inBox = [&](VehicleId v, int x0, int y0) {
        const combat::detail::Piece& p = bt.pieces()[static_cast<size_t>(k.at(v))];
        return p.x >= x0 && p.x <= x0 + 6 && p.y >= y0 && p.y <= y0 + 6;
    };
    CHECK(inBox(c, 36 - 12, 31 - 12));   // 1: up-left
    CHECK(inBox(b, 36 + 12, 31 + 12));   // 2: down-right
    CHECK(inBox(a, 36 + 12, 31 - 12));   // 3: up-right
}

TEST_CASE("placement: planets and vehicles share one object order when the middle empires are numbered") {
    // Spec 04 §3 step 4, §19.2 Q57; spec 03 §19 Q62: stellar objects and
    // vehicles hold slots of one list, so a planet made during play comes
    // after the ships already there.
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    const VehicleId a = spawn(s, frigate(s, ar.a, "A", 1, {}), ar.loc);
    SpaceObject made = s.galaxy.object(homeworld(s, ar.b).planet);
    made.sector = ar.loc.sector;
    made.name = "Made";
    const ObjectId planet = s.addObject(made, ar.loc.system);
    Colony colony = homeworld(s, ar.b);
    colony.planet = planet;
    colony.homeworld = false;
    colony.cargo = {};
    s.colonies[planet.index()] = colony;
    const VehicleId c = spawn(s, frigate(s, ar.c, "C", 1, {}), ar.loc);
    REQUIRE(s.vehicle(a)->slot < s.galaxy.object(planet).slot);
    REQUIRE(s.galaxy.object(planet).slot < s.vehicle(c)->slot);
    Bench k(std::move(ar));
    Battle& bt = k.start();
    auto inBox = [&](VehicleId v, int x0, int y0) {
        const combat::detail::Piece& p = bt.pieces()[static_cast<size_t>(k.at(v))];
        return p.x >= x0 && p.x <= x0 + 6 && p.y >= y0 && p.y <= y0 + 6;
    };
    // A's ship comes first (1: up-left), B's colony second, and B, owner of
    // the last colonised planet in that order, keeps the centre; C is third.
    CHECK(inBox(a, 36 - 12, 31 - 12));   // 1: up-left
    CHECK(inBox(c, 36 + 12, 31 - 12));   // 3: up-right
}

TEST_CASE("placement: hops stay on the map, and footprint squares off the map do not count") {
    Bench k(makeArena());
    GameState& s = k.ar.s;
    const VehicleId corner = spawn(s, frigate(s, k.ar.a, "Corner", 1, {}), k.ar.loc);
    spawn(s, frigate(s, k.ar.b, "Other", 1, {}), k.ar.loc);
    Battle& b = k.start();
    b.placeAt(k.at(corner), 71, 62);
    for (int n = 0; n < 20; ++n) {
        const Battle::Settled at = b.settleFor(71, 62, 1);
        CHECK(at.found);
        CHECK(at.x <= 70);
        CHECK(at.y <= 61);
    }
    const Battle::Settled big = b.settleFor(70, 30, 4);
    CHECK(big.found);
    CHECK(std::pair{big.x, big.y} == std::pair{70, 30});
}

// ---- Supply, cloaking, seekers, the end of an unseen battle -----------------------------------------------

TEST_CASE("unit groups: a fighter group has one supply pool, filled at every launch into it, emptied by the weapons fired") {
    // Spec 04 §6, §19.1 Q56 (confirmed: binary).
    Bench k(makeArena());
    GameState& s = k.ar.s;
    const DesignId wasp = design(s, k.ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    const VehicleId carrier = spawn(s, frigate(s, k.ar.a, "Carrier", 1, {"Test Fighter Bay", "Test Fighter Bay"}), k.ar.loc);
    s.vehicle(carrier)->cargo.units = {{wasp, 4}};
    const VehicleId target = spawn(s, frigate(s, k.ar.b, "Target", 1, {"CT Big Armor"}), k.ar.loc);
    warpIn(s, target);
    Battle& b = k.start({k.ar.a});
    b.advance();
    REQUIRE(b.phaseEmpire() == k.ar.a);
    const int c = k.at(carrier), t = k.at(target);
    k.arrange({{c, 20, 20}, {t, 24, 20}});
    TacticalOrder launch{OK::Launch, k.ar.a, c};
    launch.design = wasp;
    launch.count = 2;
    launch.group = 0;
    REQUIRE(b.submit(launch).empty());
    const int group = static_cast<int>(b.pieces().size()) - 1;
    CHECK(b.pieces()[static_cast<size_t>(group)].unit.supply == 200);   // 2 × 100
    b.piece(group).unit.supply = 150;
    // Another launch from the same window joins the group and fills its pool to 3 × 100.
    launch.count = 1;
    REQUIRE(b.submit(launch).empty());
    CHECK(b.pieces()[static_cast<size_t>(group)].unit.count == 3);
    CHECK(b.pieces()[static_cast<size_t>(group)].unit.supply == 300);
    // A volley takes the supply use times the weapons fired together: 3 × 5.
    b.placeAt(group, 23, 20);
    TacticalOrder fire{OK::Fire, k.ar.a, group, t};
    REQUIRE(b.check(fire).empty());
    REQUIRE(b.submit(fire).empty());
    CHECK(b.pieces()[static_cast<size_t>(group)].unit.supply == 285);
}

TEST_CASE("combat: pieces cloaked when the battle began cloak again afterwards if they still can") {
    // Spec 04 §2 (confirmed: binary).
    Arena ar = makeArena();
    GameState& s = ar.s;
    for (EmpireId e : {ar.a, ar.b}) useStrategy(s, e, {{"Primary Movement Strategy", "Don't Get Hurt"}});   // nobody rams
    const VehicleId shade = spawn(s, frigate(s, ar.a, "Shade", 1, {"CT Cloak"}), ar.loc);
    const VehicleId ghost = spawn(s, frigate(s, ar.a, "Ghost", 1, {"CT Cloak"}), ar.loc);
    const VehicleId lure = spawn(s, frigate(s, ar.a, "Lure", 1, {"CT Cloak"}), ar.loc);
    const VehicleId buoys = spawn(s, design(s, ar.a, "Buoy", "Test Satellite Hull", {"Test Satellite Gun"}), ar.loc, 2);
    for (VehicleId v : {shade, ghost, buoys}) s.vehicle(v)->status = VehicleStatus::Cloaked;
    s.vehicle(ghost)->supply = 0;   // no supplies: it cannot cloak again
    spawn(s, frigate(s, ar.b, "Watcher", 1, {}), ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.combats.size() == 1);
    CHECK(s.vehicle(shade)->status == VehicleStatus::Cloaked);
    CHECK(s.vehicle(ghost)->status == VehicleStatus::Normal);
    CHECK(s.vehicle(lure)->status == VehicleStatus::Normal);   // it was not cloaked
    CHECK(s.vehicle(buoys)->status == VehicleStatus::Cloaked);
}

TEST_CASE("combat: Crew Conversion counts as damage against a seeker") {
    // Spec 04 §10.1, §19.2 Q66 (confirmed: binary): every type but Shields Only
    // and the reload types (modded data only).
    ruleset::Ruleset rs = buildCombatRuleset();
    gun(rs, "CT Soul Net", WeaponKind::PointDefense, {100, 100, 100, 100}, "Crew Conversion", {"Seekers"});
    rs.reindex();
    const Rules rules{std::move(rs)};
    Arena ar = makeArena(rules);
    GameState& s = ar.s;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Don't Get Hurt"}});
    const VehicleId net = spawnOn(rules, s, frigate(s, ar.a, "Net", 1, {"CT Soul Net", "CT Always Hit", "CT Big Armor"}, rules), ar.loc);
    const VehicleId launcher = spawnOn(rules, s, frigate(s, ar.b, "Launcher", 1, {"CT Torpedo", "CT Big Armor"}, rules), ar.loc);
    warpIn(s, launcher);   // the middle of the map, beside the net
    TurnContext ctx = context(s, rules);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    const int netPiece = pieceOf(rec, net);
    int seekers = 0, shotDown = 0;
    for (size_t i = 0; i < rec.pieces.size(); ++i) seekers += rec.pieces[i].kind == CombatPiece::Kind::Seeker ? 1 : 0;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Destroyed && rec.pieces[e.piece].kind == CombatPiece::Kind::Seeker && static_cast<int>(e.target) == netPiece)
            ++shotDown;
    REQUIRE(seekers > 0);
    CHECK(shotDown > 0);
    (void)launcher;
}

TEST_CASE("combat: an unseen battle runs the next turn's upkeep before it ends") {
    // Spec 04 §4, §19.2 Q70 (confirmed: binary): one more organic armor restore
    // before the end-of-battle restore of up to 10000.
    ruleset::Ruleset rs = buildCombatRuleset();
    part(rs, "CT Big Organic", 6000, {ab(AbilityKind::Armor), ab(AbilityKind::ArmorRegeneration, 6000)});
    gun(rs, "CT Cannon", WeaponKind::DirectFire, std::vector<int>(20, 18005), "Normal");
    gun(rs, "CT Killer", WeaponKind::DirectFire, std::vector<int>(20, 50000), "Normal");
    rs.reindex();
    const Rules rules{std::move(rs)};
    Arena ar = makeArena(rules);
    GameState& s = ar.s;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Don't Get Hurt"}});
    useStrategy(s, ar.b, {{"Primary Movement Strategy", "Don't Get Hurt"}});
    spawnOn(rules, s, frigate(s, ar.a, "Shooter", 1, {"CT Cannon", "CT Always Hit"}, rules), ar.loc);
    const VehicleId tank =
        spawnOn(rules, s, frigate(s, ar.b, "Tank", 1, {"CT Big Organic", "CT Big Organic", "CT Big Organic", "CT Big Organic", "CT Killer", "CT Always Hit"}, rules), ar.loc);
    warpIn(s, tank);   // B arrives: A fires first
    TurnContext ctx = context(s, rules);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.vehicle(tank) != nullptr);
    int destroyedOrganic = 0;
    const Design& d = s.design(s.vehicle(tank)->design);
    for (size_t e = 0; e < d.entries.size(); ++e)
        if (rules.component(d.entries[e].component).name == "CT Big Organic" && !entryIntact(rules, s, *s.vehicle(tank), e)) ++destroyedOrganic;
    // Three of four were destroyed by the cannon; the upkeep restores one with
    // the surviving part's 6000, the end of the battle one more.
    CHECK(destroyedOrganic == 1);
}

// ---- Drones (spec 03 §19 Q68, spec 04 §10.7) --------------------------------------------------------------

TEST_CASE("combat: a drone group's battle target is what its Attack pursuit names, of any kind") {
    auto run = [](bool named) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        useStrategy(s, ar.a, {{"Primary Movement Strategy", "Ram"}});
        const DesignId dart = design(s, ar.a, "Dart", "Test Drone Hull", {"Test Engine", "Test Engine", "Test Engine", "Test Engine", "CT Combat Thruster", "Test Warhead"});
        const VehicleId drone = spawn(s, dart, ar.loc);
        const VehicleId bees = spawn(s, design(s, ar.b, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "CT Big Armor"}), ar.loc, 2);
        const VehicleId hulk = spawn(s, design(s, ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc);
        if (named) {
            Order o;
            o.kind = OrderKind::Attack;
            o.vehicle = bees;
            s.vehicle(drone)->orders.push_back(o);
        }
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const CombatRecord& rec = s.combats.front();
        const int d = pieceOf(rec, drone);
        int onBees = 0, onHulk = 0;
        for (const CombatEvent& e : rec.events) {
            if (e.kind != CombatEvent::Kind::Hit || static_cast<int>(e.piece) != d) continue;
            if (static_cast<int>(e.target) == pieceOf(rec, bees)) ++onBees;
            if (static_cast<int>(e.target) == pieceOf(rec, hulk)) ++onHulk;
        }
        return std::pair{onBees, onHulk};
    };
    // Named: the fighter group, which drones never pick on their own.
    const auto [namedBees, namedHulk] = run(true);
    CHECK(namedBees > 0);
    CHECK(namedHulk == 0);
    const auto [ownBees, ownHulk] = run(false);
    CHECK(ownBees == 0);
    CHECK(ownHulk > 0);
}

// ---- Cargo trimmed in a battle (spec 02 §2, §13 Q54) ------------------------------------------------------

TEST_CASE("economy: a cargo trim drops every troop unit first when the colony has no population; the battle's dead take space") {
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId trooper = design(s, ar.b, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const DesignId sat = design(s, ar.b, "Buoy", "Test Satellite Hull", {"Test Satellite Gun"});
    const int64_t satTons = r.hull(s.design(sat).hull).tonnage;
    // Over capacity: with population, units go one at a time from the first stack.
    Colony populated = homeworld(s, ar.b);
    const int fit = static_cast<int>(colonyCargoCapacity(r, s, populated) / satTons);
    populated.cargo.units = {{sat, fit + 1}, {trooper, 2}};
    economy::trimCargoToCapacity(r, s, populated);
    CHECK(populated.cargo.unitCount(trooper) == 2);
    CHECK(populated.cargo.unitCount(sat) < fit + 1);
    // With no population left, every troop unit goes first (spec 02 §2, §13 Q54).
    Colony empty = homeworld(s, ar.b);
    empty.population.clear();
    const int fitEmpty = static_cast<int>(colonyCargoCapacity(r, s, empty) / satTons);
    empty.cargo.units = {{sat, fitEmpty + 1}, {trooper, 2}};
    economy::trimCargoToCapacity(r, s, empty);
    CHECK(empty.cargo.unitCount(trooper) == 0);
    CHECK(empty.cargo.unitCount(sat) == fitEmpty);
    // Units killed earlier in the battle still take space.
    Colony battle = homeworld(s, ar.b);
    battle.cargo.units = {{sat, fit}};
    economy::trimCargoToCapacity(r, s, battle, 2 * satTons, true);
    CHECK(battle.cargo.unitCount(sat) == fit - 2);
}

// ---- Drone targets, the overkill totals, the firing range and the verdict (spec 04 §19.3 Q80, Q84, Q86) ----

TEST_CASE("planning: drones choosing one after another spread over the targets; an ordinary choice clears the first totals") {
    const Rules& r = combatRules();
    Bench k(makeArena());
    GameState& s = k.ar.s;
    useStrategy(s, k.ar.a, {{"Primary Movement Strategy", "Ram"}});
    const DesignId dart = design(s, k.ar.a, "Dart", "Test Drone Hull", {"Test Engine", "Test Engine", "Test Engine", "Test Engine", "Test Warhead"});
    const VehicleId d1 = spawn(s, dart, k.ar.loc), d2 = spawn(s, dart, k.ar.loc);
    const VehicleId scout = spawn(s, design(s, k.ar.a, "Scout", "Test Frigate", {"Test Bridge"}), k.ar.loc);
    const DesignId skiff = design(s, k.ar.b, "Skiff", "Test Frigate", {"Test Bridge"});
    const VehicleId s1 = spawn(s, skiff, k.ar.loc), s2 = spawn(s, skiff, k.ar.loc);
    // One drone's warhead (60) reaches a skiff's limit: 1.5 × (no shields + its full structure).
    REQUIRE(60 * 2 >= 3 * combat::detail::designStructure(r, s.design(skiff)));
    Battle& b = k.start();
    const int a = k.at(d1), c = k.at(d2), x = k.at(s1), y = k.at(s2), sc = k.at(scout);
    // At set-up every drone group chooses, in piece order: the second does not take the first's target.
    CHECK(b.piece(a).droneTarget >= 0);
    CHECK(b.piece(c).droneTarget >= 0);
    CHECK(b.piece(a).droneTarget != b.piece(c).droneTarget);
    k.arrange({{a, 20, 20}, {c, 20, 22}, {x, 23, 21}, {y, 30, 21}, {sc, 5, 5}});
    // An ordinary choice takes both skiffs as candidates: their first totals go back to 0.
    b.targetsFor(sc, false);
    CHECK(b.totalFor(x).all == 0);
    CHECK(b.totalFor(y).all == 0);
    b.droneTargetFor(a);
    CHECK(b.piece(a).droneTarget == x);   // the nearest
    CHECK(b.totalFor(x).all == 60);       // grown by the drone's warhead damage
    b.droneTargetFor(c);
    CHECK(b.piece(c).droneTarget == y);   // the nearest is at its limit: the next one
    CHECK(b.totalFor(y).all == 60);
    // Drone choices clear nothing; an ordinary choice does.
    b.droneTargetFor(c);
    CHECK(b.piece(c).droneTarget == x);   // every candidate at its limit: the first sorted one
    b.targetsFor(sc, false);
    b.droneTargetFor(c);
    CHECK(b.piece(c).droneTarget == x);
    CHECK(b.totalFor(x).all == 60);
}

TEST_CASE("planning: a warhead-only drone takes planets or ships by its design type; never a fighter group while another is there") {
    auto target = [](std::string_view type, bool fighters, bool ship) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        Colony& hw = homeworld(s, ar.b);
        ar.loc = locationOf(s.galaxy, hw.planet);
        useStrategy(s, ar.a, {{"Primary Movement Strategy", "Ram"}});
        const DesignId dart = design(s, ar.a, "Dart", "Test Drone Hull", {"Test Engine", "Test Engine", "Test Engine", "Test Engine", "Test Warhead"});
        s.design(dart).designType = std::string(type);
        const VehicleId drone = spawn(s, dart, ar.loc);
        VehicleId bees, skiff;
        if (fighters) bees = spawn(s, design(s, ar.b, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"}), ar.loc, 2);
        if (ship) skiff = spawn(s, design(s, ar.b, "Skiff", "Test Frigate", {"Test Bridge"}), ar.loc);
        Bench k(std::move(ar));
        Battle& b = k.start();
        const int d = k.at(drone), p = k.planet(hw.planet);
        std::vector<std::tuple<int, int, int>> where{{d, 20, 20}, {p, 30, 18}};
        if (fighters) where.emplace_back(k.at(bees), 22, 20);
        if (ship) where.emplace_back(k.at(skiff), 24, 20);
        for (const auto& [i, x, y] : where) b.placeAt(i, x, y);
        b.targetsFor(d, false);   // an ordinary choice: the totals from the set-up go
        b.droneTargetFor(d);
        const int t = b.piece(d).droneTarget;
        if (t == p) return std::string("planet");
        if (fighters && t == k.at(bees)) return std::string("fighters");
        if (ship && t == k.at(skiff)) return std::string("ship");
        return std::string("none");
    };
    CHECK(target("Anti-Planet Drone", false, true) == "planet");
    CHECK(target("Anti-Ship Drone", false, true) == "ship");
    CHECK(target("Drone", false, true) == "ship");                // the nearest
    CHECK(target("Anti-Ship Drone", true, true) == "ship");       // never the fighter group while another is there
    CHECK(target("Anti-Ship Drone", true, false) == "fighters");  // else the first sorted candidate
}

TEST_CASE("planning: the firing choice never reaches past 20 squares, even with a mount that does damage further") {
    const Rules& r = combatRules();
    Bench k(makeArena());
    GameState& s = k.ar.s;
    const DesignId gunboat = frigate(s, k.ar.a, "Gunboat", 1, {"CT Twenty Gun"});
    for (DesignEntry& e : s.design(gunboat).entries)
        if (r.component(e.component).name == "CT Twenty Gun") e.mount = mountIndex(r, "CT Long Mount");
    const VehicleId g = spawn(s, gunboat, k.ar.loc);
    const VehicleId hulk = spawn(s, design(s, k.ar.b, "Hulk", "Test Frigate", {"Test Bridge", "CT Big Armor"}), k.ar.loc);
    Battle& b = k.start();
    const int gi = k.at(g), hi = k.at(hulk);
    k.arrange({{gi, 10, 20}, {hi, 35, 20}});
    // The weapon does damage at 25 squares (its table index is clamped to 20), but the choice stops at 20.
    for (const DesignEntry& e : s.design(gunboat).entries)
        if (r.component(e.component).name == "CT Twenty Gun") REQUIRE(combat::weaponDamage(r, e, 25) > 0);
    const combat::detail::Targeting far = b.targetsFor(gi, true);
    CHECK(far.main == -1);
    CHECK(k.targetsOn(far, hi) == 0);
    CHECK(k.targetsOn(b.targetsFor(gi, false), hi) == 1);   // planning has no distance check
    k.arrange({{gi, 10, 20}, {hi, 30, 20}});
    CHECK(k.targetsOn(b.targetsFor(gi, true), hi) == 1);    // 20 squares: within
}

TEST_CASE("combat: the verdict counts every other empire's survivors, whatever the treaty") {
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    setTreaty(s, ar.a, ar.c, Treaty::Partnership);
    setTreaty(s, ar.b, ar.c, Treaty::Partnership);
    const VehicleId hunter = spawn(s, frigate(s, ar.a, "Hunter", 2, {"CT Big Gun", "CT Big Gun", "CT Always Hit"}), ar.loc);
    const VehicleId prey = spawn(s, design(s, ar.b, "Prey", "Test Frigate", {"Test Bridge"}), ar.loc);
    const VehicleId bystander = spawn(s, design(s, ar.c, "Bystander", "Test Frigate", {"Test Bridge"}), ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.vehicle(prey)->count == 0);
    REQUIRE(s.vehicle(hunter)->count == 1);
    REQUIRE(s.vehicle(bystander)->count == 1);
    // A's only enemy is gone, but C, at peace with everyone, still has a survivor: a stalemate.
    CHECK(moodCount(ctx, ar.a, "Battle in System - Stalemate") == 1);
    CHECK(moodCount(ctx, ar.a, "Battle in System - Win") == 0);
    CHECK(moodCount(ctx, ar.b, "Battle in System - Loss") == 1);
    const auto& summary = s.combats.back().summary;
    CHECK(std::find(summary.begin(), summary.end(), std::format("{}: stalemate", s.empire(ar.c).name)) != summary.end());
}

// ---- Drop Troops without treaty checks (spec 04 §11, §13, §16.1; spec 06 §1.10.2, §7 Q37) ----------------

namespace {

// A second colony in the sector of B's homeworld, for empire `owner`: another
// planet of that system moved there.
ObjectId colonyBeside(GameState& s, ObjectId home, EmpireId owner, int64_t millions) {
    const SpaceObject& h = s.galaxy.object(home);
    for (SpaceObject& o : s.galaxy.objects) {
        if (o.kind != ObjectKind::Planet || o.system != h.system || o.id == home || s.colony(o.id)) continue;
        o.sector = h.sector;
        Colony c;
        c.planet = o.id;
        c.owner = owner;
        c.colonyType = "Balanced";
        c.population.push_back({owner, millions});
        s.colonies[o.id.index()] = c;
        return o.id;
    }
    FAIL("no free planet in the home system");
    return {};
}

// Plays the battle's computer phases until the player's side may give orders.
void untilPlayer(Battle& b, EmpireId player) {
    b.advance();
    REQUIRE(b.phaseEmpire() == player);
}

} // namespace

TEST_CASE("drop troops: the order takes the last adjacent colony of another empire, whatever the treaty, and the fight takes it") {
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    Colony& hw = homeworld(s, ar.b);
    hw.cargo = {};
    ar.loc = locationOf(s.galaxy, hw.planet);
    // C, an ally of A, has a small colony beside B's homeworld; C is at war with B, so there is a battle.
    const ObjectId ally = colonyBeside(s, hw.planet, ar.c, 10);   // below 20M: no militia
    setTreaty(s, ar.a, ar.b, Treaty::Partnership);
    setTreaty(s, ar.a, ar.c, Treaty::Partnership);
    const VehicleId skiff = spawn(s, design(s, ar.c, "Skiff", "Test Frigate", {"Test Bridge"}), ar.loc);
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transport = spawn(s, frigate(s, ar.a, "Transport", 1, {"Test Cargo Bay"}), ar.loc);
    Bench k(std::move(ar));
    Battle& b = k.start({k.ar.a});
    const int t = k.at(transport), home = k.planet(hw.planet), mine = k.planet(ally);
    REQUIRE(home < mine);   // C's colony comes later in piece order
    k.arrange({{home, 30, 30}, {mine, 35, 30}, {t, 34, 31}, {k.at(skiff), 50, 50}});
    untilPlayer(b, k.ar.a);
    TacticalOrder drop{OK::DropTroops, k.ar.a, t, home};   // the target the order names is ignored
    CHECK(b.check(drop) == "It carries no troops.");
    b.piece(t).unit.cargo.units.push_back({trooper, 4});
    CHECK(b.check(drop).empty());
    REQUIRE(b.submit(drop).empty());
    // The landing went to C's colony, the last in piece order, though C is an ally; four troops took it at once.
    REQUIRE(b.record().grounds.size() == 1);
    const GroundCombat& g = b.record().grounds.front();
    CHECK(static_cast<int>(g.planetPiece) == mine);
    CHECK(g.attacker == k.ar.a);
    CHECK(g.defender == k.ar.c);
    CHECK(g.captured);
    CHECK(b.pieces()[static_cast<size_t>(mine)].owner == k.ar.a);
    CHECK(b.pieces()[static_cast<size_t>(home)].landed.empty());
    CHECK(b.record().events[g.event].kind == CombatEvent::Kind::Launch);
    // Both empires' log entries, titled with the system.
    for (EmpireId e : {k.ar.a, k.ar.c}) {
        const auto& log = k.ar.s.empire(e).log;
        CHECK(std::any_of(log.begin(), log.end(), [](const LogEntry& l) { return l.title.starts_with("Ground combat at ") && l.text.ends_with("taken."); }));
    }
}

TEST_CASE("drop troops: refused where a third empire's troops are landed; every landing resets the planet's piece") {
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    Colony& hw = homeworld(s, ar.b);
    ar.loc = locationOf(s.galaxy, hw.planet);
    // B's homeworld: planet shields, a weapon platform, and enough troops to throw one invader back.
    for (size_t f = 0; f < combatRules().data().facilities.size(); ++f)
        if (combatRules().facility(static_cast<uint32_t>(f)).name == "Test Planet Shield") hw.facilities.push_back(static_cast<uint32_t>(f));
    const DesignId platform = design(s, ar.b, "Bastion", "CT Platform Hull", {"CT Platform Gun", "CT Platform Core"});
    const DesignId guard = design(s, ar.b, "Guard", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    hw.cargo.units = {{platform, 1}, {guard, 20}};
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const DesignId other = design(s, ar.c, "Other", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transport = spawn(s, frigate(s, ar.a, "Transport", 1, {"Test Cargo Bay"}), ar.loc);
    s.vehicle(transport)->cargo.units.push_back({trooper, 1});
    Bench k(std::move(ar));
    Battle& b = k.start({k.ar.a});
    const int t = k.at(transport), home = k.planet(hw.planet);
    k.arrange({{home, 30, 30}, {t, 34, 31}});
    untilPlayer(b, k.ar.a);
    const TacticalOrder drop{OK::DropTroops, k.ar.a, t};
    // C's troops already fight there: refused.
    b.piece(home).landed = {{other, 2}};
    b.piece(home).invader = k.ar.c;
    CHECK(b.check(drop) == "Another empire's troops are already there.");
    b.piece(home).landed.clear();
    b.piece(home).invader = {};
    // The planet has fired and lost its shields; the landing (which fails) resets the piece all the same.
    combat::detail::Piece& p = b.piece(home);
    REQUIRE(p.sh.max > 0);
    REQUIRE_FALSE(p.weapons.empty());
    p.sh.current = 0;
    for (auto& w : p.weapons) std::fill(w.reload.begin(), w.reload.end(), 2);
    p.engaged = {t};
    REQUIRE(b.submit(drop).empty());
    REQUIRE(b.record().grounds.size() == 1);
    CHECK_FALSE(b.record().grounds.front().captured);
    const combat::detail::Piece& q = b.pieces()[static_cast<size_t>(home)];
    CHECK(q.owner == k.ar.b);
    CHECK(q.sh.current == q.sh.max);
    CHECK(q.engaged.empty());
    for (const auto& w : q.weapons)
        for (int c : w.reload) CHECK(c == 0);
}

TEST_CASE("drop troops: a computer carrier that waits lands after its move on whichever foreign colony is adjacent") {
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    Colony& hw = homeworld(s, ar.b);
    hw.cargo = {};
    hw.population = {{ar.b, 10}};
    ar.loc = locationOf(s.galaxy, hw.planet);
    setTreaty(s, ar.a, ar.b, Treaty::Partnership);   // B is A's friend: no hostile colony to head for
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Drop Troops"}, {"Secondary Movement Strategy", "Don't Get Hurt"}});
    const VehicleId skiff = spawn(s, design(s, ar.c, "Skiff", "Test Frigate", {"Test Bridge"}), ar.loc);
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transport = spawn(s, frigate(s, ar.a, "Transport", 1, {"Test Cargo Bay"}), ar.loc);
    s.vehicle(transport)->cargo.units.push_back({trooper, 4});
    Bench k(std::move(ar));
    Battle& b = k.start();
    const int t = k.at(transport), home = k.planet(hw.planet);
    k.arrange({{home, 30, 30}, {t, 34, 31}, {k.at(skiff), 60, 55}});
    CHECK(b.planFor(t).target == -1);   // no hostile colony: it waits (Don't Get Hurt)
    b.piece(t).mp = 0;                  // it stays where it is
    b.run();
    REQUIRE_FALSE(b.record().grounds.empty());
    const GroundCombat& g = b.record().grounds.front();
    CHECK(static_cast<int>(g.planetPiece) == home);
    CHECK(g.attacker == k.ar.a);
    CHECK(g.captured);
    CHECK(g.round == 1);
}

TEST_CASE("ground combat: the record keeps every stack's count after every round") {
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& hw = homeworld(s, ar.b);
    hw.population = {{ar.b, 100}};   // five militia
    const DesignId guard = design(s, ar.b, "Guard", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    hw.cargo.units = {{guard, 6}};
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    hw.landedTroops = {{trooper, 9}};
    hw.invader = ar.a;
    hw.militia = -1;
    const combat::CombatSettings cs = combat::loadSettings(r);
    GroundCombat rec;
    combat::detail::GroundFight f;
    f.attacker = ar.a;
    f.defender = ar.b;
    f.invaders = &hw.landedTroops;
    f.cargo = &hw.cargo;
    f.population = &hw.population;
    f.militia = &hw.militia;
    f.record = &rec;
    Rng rng(11);
    const combat::detail::GroundOutcome o = combat::detail::fightGround(r, s, cs, f, rng);
    CHECK(rec.rounds == o.rounds);
    REQUIRE(rec.perRound.size() == static_cast<size_t>(o.rounds));
    REQUIRE(o.rounds >= 1);
    CHECK(rec.attackers == std::vector<UnitStack>{{trooper, 9}});
    CHECK(rec.defenders == std::vector<UnitStack>{{guard, 6}});
    CHECK(rec.militia == 5);
    int prevA = 9, prevD = 6, prevM = 5;
    for (const GroundRound& round : rec.perRound) {
        REQUIRE(round.attackers.size() == 1);
        REQUIRE(round.defenders.size() == 1);
        CHECK(round.attackers[0] <= prevA);
        CHECK(round.defenders[0] <= prevD);
        CHECK(round.militia <= prevM);
        prevA = round.attackers[0];
        prevD = round.defenders[0];
        prevM = round.militia;
    }
    CHECK(rec.attackersLeft.front().count == prevA);
    CHECK(rec.defendersLeft.front().count == prevD);
    CHECK(rec.militiaLeft == prevM);
    CHECK(hw.landedTroops.front().count == prevA);
    CHECK(hw.cargo.units.front().count == prevD);
    CHECK(hw.militia == prevM);
    CHECK(rec.captured == o.captured);
}
