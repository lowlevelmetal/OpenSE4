// Movement, orders, fleets, supply, cargo, units, colonization, repair,
// hazards and stellar manipulation (docs/spec/03, spec 01 §7-9, spec 05 §9.3).

#include "movement_fixture.hpp"

#include "game/ai.hpp"
#include "game/combat_detail.hpp"
#include "game/commands.hpp"
#include "game/movement_internal.hpp"
#include "game/orders.hpp"
#include "game/serialize.hpp"
#include "game/xmath.hpp"

#include <doctest/doctest.h>

#include <format>
#include <map>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::mvtest;

namespace {

Order mk(OrderKind k, Location l = {}, ObjectId obj = {}, VehicleId veh = {}, DesignId d = {}, int amount = 0) {
    Order o;
    o.kind = k;
    o.location = l;
    o.object = obj;
    o.vehicle = veh;
    o.design = d;
    o.amount = amount;
    return o;
}
Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }
Order moveTo(SystemId s, int x, int y) { return mk(OrderKind::MoveTo, at(s, x, y)); }
Order stellar(StellarAction a, ObjectId obj = {}, Location l = {}) {
    return mk(OrderKind::StellarManipulation, l, obj, {}, {}, static_cast<int>(a));
}
// Gives the vehicle a huge fuel cell (a copy of its design with one more part,
// appended so entry indices stay the same) and fills it.
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
int totalDamage(const Vehicle& v) {
    int n = 0;
    for (int d : v.damage) n += d;
    return n;
}
bool hasMood(const std::vector<MoodEvent>& moods, EmpireId e, std::string_view trigger) {
    return std::any_of(moods.begin(), moods.end(), [&](const MoodEvent& m) { return m.empire == e && m.trigger == trigger; });
}
bool inSystemList(const GameState& s, ObjectId o) {
    const auto& list = s.galaxy.system(s.galaxy.object(o).system).objects;
    return std::find(list.begin(), list.end(), o) != list.end();
}
// Gives a fleet orders: copies in the lists of its members at its location,
// as cmd::SetOrders on the fleet leaves them (spec 03 §8, §19 Q65).
void fleetGets(World& w, FleetId f, const std::vector<Order>& list, bool repeat = false) {
    for (VehicleId id : fleetGroup(w.s, *w.s.fleet(f))) {
        w.v(id).orders = list;
        w.v(id).repeatOrders = repeat;
    }
}
const std::vector<Order>& fleetList(World& w, FleetId f) { return fleetOrders(w.s, *w.s.fleet(f)); }
size_t countKind(const GameState& s, SystemId sys, ObjectKind k) {
    size_t n = 0;
    for (ObjectId o : s.galaxy.system(sys).objects) n += s.galaxy.object(o).kind == k;
    return n;
}

} // namespace

// ---- Schedule and pathfinding ------------------------------------------------------------------

TEST_CASE("movement: the 30-day schedule moves the k-th step on day ceil(k*30/speed), one action a day") {
    CHECK(movement::movesByDay(5, 5) == 0);
    CHECK(movement::movesByDay(5, 6) == 1);
    CHECK(movement::movesByDay(5, 30) == 5);
    std::vector<int> days;
    for (int d = 1; d <= movement::kDaysPerTurn; ++d)
        if (movement::movesByDay(7, d) > movement::movesByDay(7, d - 1)) days.push_back(d);
    CHECK(days == std::vector<int>{5, 9, 13, 18, 22, 26, 30});
    CHECK(movement::movesByDay(45, 1) == 1);
    CHECK(movement::movesByDay(45, 2) == 2);  // at most one action a day (spec 03 §6.3)
    CHECK(movement::movesByDay(0, 30) == 0);
}

TEST_CASE("movement: the day counter, exact and as the original's stored double") {
    using movement::DayCounterMode;
    CHECK(movement::actionDays(5, DayCounterMode::Exact) == std::vector<int>{6, 12, 18, 24, 30});
    CHECK(movement::actionDays(1, DayCounterMode::Exact) == std::vector<int>{30});
    CHECK(movement::actionDays(30, DayCounterMode::Exact).size() == 30);
    for (int speed = 0; speed <= 30; ++speed) {
        const auto days = movement::actionDays(speed, DayCounterMode::Exact);
        CHECK(static_cast<int>(days.size()) == speed);
        for (size_t k = 0; k < days.size(); ++k) CHECK(movement::movesByDay(speed, days[k]) == static_cast<int>(k) + 1);
    }
    // Kept as a double with the x87's extended precision, as spec 03 §6.3
    // describes: speed 1 never moves and speed 5 moves on days 7, 13, 19, 25.
    CHECK(movement::actionDays(1, DayCounterMode::Double).empty());
    CHECK(movement::actionDays(5, DayCounterMode::Double) == std::vector<int>{7, 13, 19, 25});
    CHECK(movement::actionDays(30, DayCounterMode::Double).size() == 30);
    CHECK(movement::kDayCounterMode == DayCounterMode::Double);  // the original's counter (confirmed: binary)
}

TEST_CASE("movement: the day counter gives every speed the acting days of spec 03 §6.3") {
    using movement::DayCounterMode;
    auto allBut = [](std::vector<int> skipped) {
        std::vector<int> days;
        for (int d = 1; d <= movement::kDaysPerTurn; ++d)
            if (std::find(skipped.begin(), skipped.end(), d) == skipped.end()) days.push_back(d);
        return days;
    };
    const std::map<int, std::vector<int>> table = {
        {1, {}},
        {2, {16}},
        {3, {11, 21}},
        {4, {8, 16, 23}},
        {5, {7, 13, 19, 25}},
        {6, {6, 11, 16, 21, 26}},
        {7, {5, 9, 13, 18, 22, 26}},
        {9, {4, 7, 11, 14, 17, 21, 24, 27}},
        {11, {3, 6, 9, 11, 14, 17, 20, 22, 25, 28}},
        {16, allBut({1, 3, 5, 7, 9, 11, 13, 15, 18, 20, 22, 24, 26, 28, 30})},
        {17, allBut({1, 3, 5, 7, 10, 12, 14, 17, 19, 21, 24, 26, 28, 30})},
        {18, allBut({1, 3, 5, 8, 10, 13, 15, 18, 20, 23, 25, 28, 30})},
        {19, allBut({1, 3, 6, 9, 11, 14, 17, 20, 22, 25, 28, 30})},
        {20, allBut({1, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30})},
        {21, allBut({1, 4, 7, 10, 14, 17, 20, 24, 27, 30})},
        {22, allBut({1, 4, 8, 12, 15, 19, 23, 27, 30})},
        {23, allBut({1, 5, 9, 13, 18, 22, 26, 30})},
        {25, allBut({1, 6, 12, 18, 24, 30})},
        {27, allBut({1, 10, 20, 30})},
        {29, allBut({1, 30})},
    };
    for (const auto& [speed, days] : table) {
        CAPTURE(speed);
        CHECK(movement::actionDays(speed) == days);
        CHECK(static_cast<int>(days.size()) == speed - 1);  // one action fewer than its movement points
    }
    for (int speed : {8, 10, 12, 13, 14, 15, 24, 26, 28, 30}) {
        CAPTURE(speed);
        CHECK(movement::actionDays(speed) == movement::actionDays(speed, DayCounterMode::Exact));
    }
    for (int speed : {31, 45, 100}) CHECK(movement::actionDays(speed).size() == 30);  // at most one action a day

    // In a game: a speed-6 ship steps on days 6, 11, 16, 21 and 26, a speed-1 ship never.
    World w;
    const SystemId a = w.system("A");
    const VehicleId six = w.spawn(w.ship(kA, "Six", 6), at(a, 0, 6));
    const VehicleId one = w.spawn(w.ship(kA, "One", 1), at(a, 0, 8));
    fuel(w, six);
    fuel(w, one);
    w.order(six, moveTo(a, 12, 6));
    w.order(one, moveTo(a, 12, 8));
    CombatSpy spy;
    spy.fight = [](const GameState&, Location) { return false; };
    w.move(spy.hooks());
    CHECK(w.v(six).location == at(a, 5, 6));
    CHECK(w.v(one).location == at(a, 0, 8));
    CHECK(w.v(six).movement == 6);  // movement points are not spent in a simultaneous game
    std::vector<Location> sixSteps;
    for (const Location& l : spy.asked)
        if (l.sector.y == 6) sixSteps.push_back(l);
    CHECK(sixSteps == std::vector<Location>{at(a, 1, 6), at(a, 2, 6), at(a, 3, 6), at(a, 4, 6), at(a, 5, 6)});
}

TEST_CASE("movement: paths inside a system use king moves") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("Alpha");
    const auto p = movement::findPath(r, w.s, kA, at(a, 0, 0), at(a, 5, 3));
    REQUIRE(p);
    CHECK(p->length == 5);
    REQUIRE(p->steps.size() == 5);
    CHECK(p->steps.back() == at(a, 5, 3));
    Location prev = at(a, 0, 0);
    for (const Location& l : p->steps) {
        CHECK(l.system == a);
        CHECK(chebyshev(prev.sector, l.sector) == 1);
        prev = l;
    }
    const auto same = movement::findPath(r, w.s, kA, at(a, 4, 4), at(a, 4, 4));
    REQUIRE(same);
    CHECK(same->length == 0);
    CHECK_FALSE(movement::findPath(r, w.s, kA, at(a, 0, 0), Location{a, Sector{13, 0}}));
}

TEST_CASE("movement: routes use only explored systems and known warp links") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A", 0, 0), b = w.system("B", 10, 0), c = w.system("C", 20, 0);
    const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
    const auto [bc, cb] = w.link(b, {12, 6}, c, {0, 6});
    const Location from = at(a, 6, 6), to = at(c, 6, 6);

    const auto omni = movement::findPath(r, w.s, EmpireId{}, from, to);
    REQUIRE(omni);
    CHECK(omni->length == 6 + 1 + 12 + 1 + 6);
    CHECK(omni->steps[5] == at(a, 12, 6));
    CHECK(omni->steps[6] == at(b, 0, 6));  // the jump is one step

    Knowledge& k = w.s.empire(kA).knowledge;
    k.explored[a.index()] = 1;
    CHECK_FALSE(movement::findPath(r, w.s, kA, from, to));
    CHECK_FALSE(movement::findPath(r, w.s, kA, from, at(b, 3, 6)));

    // A known link into an unexplored system reaches its sectors, but no further.
    k.knownWarpLink[ab.index()] = 1;
    const auto intoB = movement::findPath(r, w.s, kA, from, at(b, 3, 6));
    REQUIRE(intoB);
    CHECK(intoB->length == 6 + 1 + 3);
    CHECK_FALSE(movement::findPath(r, w.s, kA, from, to));

    k.explored[b.index()] = 1;
    k.knownWarpLink[bc.index()] = 1;
    const auto full = movement::findPath(r, w.s, kA, from, to);
    REQUIRE(full);
    CHECK(full->length == 26);
    CHECK(*full == *omni);

    // Omnipresent games know every link.
    World o;
    const SystemId oa = o.system("A"), ob = o.system("B");
    o.link(oa, {12, 6}, ob, {0, 6});
    o.s.options.omnipresent = true;
    CHECK(movement::findPath(r, o.s, kA, at(oa, 6, 6), at(ob, 6, 6)));
    (void)ba;
    (void)cb;
}

TEST_CASE("movement: systems to avoid are never crossed") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A", 0, 0), b = w.system("B", 10, 0), c = w.system("C", 20, 0), d = w.system("D", 10, 10);
    w.link(a, {12, 6}, b, {0, 6});
    w.link(b, {1, 6}, c, {0, 6});
    w.link(a, {12, 12}, d, {0, 0});
    w.link(d, {12, 12}, c, {12, 12});
    w.exploreAll(kA);
    const Location from = at(a, 6, 6), to = at(c, 6, 6);
    auto crosses = [](const movement::Path& p, SystemId sys) {
        return std::any_of(p.steps.begin(), p.steps.end(), [&](const Location& l) { return l.system == sys; });
    };

    auto p = movement::findPath(r, w.s, kA, from, to);
    REQUIRE(p);
    CHECK(p->length == 15);
    CHECK(crosses(*p, b));

    w.s.empire(kA).systemsToAvoid = {b};
    p = movement::findPath(r, w.s, kA, from, to);
    REQUIRE(p);
    CHECK(p->length == 26);
    CHECK_FALSE(crosses(*p, b));
    // A destination inside an avoided system is fine.
    const auto inside = movement::findPath(r, w.s, kA, from, at(b, 1, 6));
    REQUIRE(inside);
    CHECK(inside->length == 6 + 1 + 1);

    // No route around: there is no route; the order would fail (spec 03 §6.2).
    w.s.empire(kA).knowledge.explored[d.index()] = 0;
    CHECK_FALSE(movement::findPath(r, w.s, kA, from, to));
    const VehicleId ship = w.spawn(w.ship(kA, "Avoider", 3), from);
    w.order(ship, Order{OrderKind::MoveTo, to}, true);
    w.move();
    CHECK(w.v(ship).location == from);
    CHECK(w.v(ship).orders.empty());
    CHECK_FALSE(w.v(ship).repeatOrders);  // a failed order switches Repeat off
}

TEST_CASE("movement: tagged minefields are never entered unless they are the destination; hazards lose ties") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    w.exploreAll(kA);
    w.s.empire(kA).taggedMinefields = {at(a, 1, 1)};
    const auto around = movement::findPath(r, w.s, kA, at(a, 0, 0), at(a, 2, 2));
    REQUIRE(around);
    CHECK(around->length == 3);
    CHECK(std::find(around->steps.begin(), around->steps.end(), at(a, 1, 1)) == around->steps.end());
    const auto into = movement::findPath(r, w.s, kA, at(a, 0, 0), at(a, 1, 1));
    REQUIRE(into);
    CHECK(into->length == 1);

    const auto straight = movement::findPath(r, w.s, kA, at(a, 3, 0), at(a, 5, 0));
    REQUIRE(straight);
    CHECK(straight->steps[0] == at(a, 4, 0));
    const ObjectId storm = w.object(a, ObjectKind::Storm, {4, 0});
    w.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorDamage, 5));
    const auto safe = movement::findPath(r, w.s, kA, at(a, 3, 0), at(a, 5, 0));
    REQUIRE(safe);
    CHECK(safe->length == 2);  // same length, around the storm
    CHECK(safe->steps[0] != at(a, 4, 0));
}

TEST_CASE("movement: the Ship Movement options decide what routes avoid; a mine sweeper may use tagged warp links") {
    const Rules& r = mvtest::rules();
    // New empires avoid both and clear orders on meeting an enemy (spec 03 §6.2,
    // §6.4, confirmed: binary); the command changes only the fields it names.
    {
        World w;
        CHECK(w.s.empire(kA).avoidTaggedMinefields);
        CHECK(w.s.empire(kA).avoidRestrictedSystems);
        CHECK(w.s.empire(kA).clearOrdersOnEncounter == EncounterClear::Enemy);
        REQUIRE(apply(r, w.s, kA, cmd::SetEncounterOptions{EncounterClear::Never}).ok);
        CHECK(w.s.empire(kA).clearOrdersOnEncounter == EncounterClear::Never);
        REQUIRE(apply(r, w.s, kA, cmd::SetEncounterOptions{EncounterClear::Enemy}).ok);
        REQUIRE(apply(r, w.s, kA, cmd::SetEncounterOptions{.avoidTaggedMinefields = false}).ok);
        CHECK_FALSE(w.s.empire(kA).avoidTaggedMinefields);
        CHECK(w.s.empire(kA).avoidRestrictedSystems);
        CHECK(w.s.empire(kA).clearOrdersOnEncounter == EncounterClear::Enemy);
        REQUIRE(apply(r, w.s, kA, cmd::SetEncounterOptions{.avoidRestrictedSystems = false}).ok);
        CHECK_FALSE(w.s.empire(kA).avoidRestrictedSystems);
        CHECK(w.s.empire(kA).clearOrdersOnEncounter == EncounterClear::Enemy);
    }

    // Systems to avoid: crossed again with the option off.
    {
        World w;
        const SystemId a = w.system("A", 0, 0), b = w.system("B", 10, 0), c = w.system("C", 20, 0), d = w.system("D", 10, 10);
        w.link(a, {12, 6}, b, {0, 6});
        w.link(b, {1, 6}, c, {0, 6});
        w.link(a, {12, 12}, d, {0, 0});
        w.link(d, {12, 12}, c, {12, 12});
        w.exploreAll(kA);
        w.s.empire(kA).systemsToAvoid = {b};
        const Location from = at(a, 6, 6), to = at(c, 6, 6);
        CHECK(movement::findPath(r, w.s, kA, from, to)->length == 26);
        REQUIRE(apply(r, w.s, kA, cmd::SetEncounterOptions{.avoidRestrictedSystems = false}).ok);
        CHECK(movement::findPath(r, w.s, kA, from, to)->length == 15);
    }

    // Tagged minefields in a system, and warp links with one on either side.
    {
        World w;
        const SystemId a = w.system("A"), b = w.system("B", 10, 0);
        w.link(a, {12, 6}, b, {0, 6});
        w.link(a, {12, 12}, b, {0, 12});
        w.exploreAll(kA);
        auto passes = [](const movement::Path& p, Location l) { return std::find(p.steps.begin(), p.steps.end(), l) != p.steps.end(); };
        const Location from = at(a, 6, 6), to = at(b, 3, 6);
        auto p = movement::findPath(r, w.s, kA, from, to);
        REQUIRE(p);
        CHECK(p->length == 6 + 1 + 3);
        // A tagged arrival square: the other link is used.
        w.s.empire(kA).taggedMinefields = {at(b, 0, 6)};
        p = movement::findPath(r, w.s, kA, from, to);
        REQUIRE(p);
        CHECK(p->length == 6 + 1 + 6);
        CHECK_FALSE(passes(*p, at(b, 0, 6)));
        // A tagged departure square too.
        w.s.empire(kA).taggedMinefields = {at(a, 12, 6)};
        p = movement::findPath(r, w.s, kA, from, to);
        REQUIRE(p);
        CHECK(p->length == 6 + 1 + 6);
        CHECK_FALSE(passes(*p, at(a, 12, 6)));
        // Option off: straight through.
        w.s.empire(kA).avoidTaggedMinefields = false;
        p = movement::findPath(r, w.s, kA, from, to);
        REQUIRE(p);
        CHECK(p->length == 6 + 1 + 3);
        // Option on, but led by a mine sweeper: through the tagged link as well.
        w.s.empire(kA).avoidTaggedMinefields = true;
        movement::RouteOptions sweeper;
        sweeper.sweeper = true;
        const Location goals[] = {to};
        const auto swept = movement::findPathToNearest(r, w.s, kA, from, goals, sweeper);
        REQUIRE(swept);
        CHECK(swept->path.length == 6 + 1 + 3);
        const DesignId sd = w.ship(kA, "Sweeper", 3);
        w.s.design(sd).designType = "Mine Sweeper";
        const VehicleId sv = w.spawn(sd, from);
        CHECK(movement::leadsSweeperGroup(w.s, w.v(sv)));
        CHECK_FALSE(movement::leadsSweeperGroup(w.s, w.v(w.spawn(w.ship(kA, "Plain", 3), from))));
        CHECK(movement::etaTurns(r, w.s, w.v(sv), to) == 5);  // 10 steps at speed 3: 2 a turn (days 11 and 21)
        // A tagged warp-point sector blocks a link even at the start (spec 03 §6.2).
        w.s.empire(kA).taggedMinefields = {at(a, 12, 6)};
        p = movement::findPath(r, w.s, kA, at(a, 12, 6), to);
        REQUIRE(p);
        CHECK(p->steps.front() != at(b, 0, 6));
    }

    // In-system greedy steps: with the option off tagged squares are entered; a
    // sweeper has no exemption inside a system (spec 03 §6.2, confirmed: binary).
    auto blocked = [&](bool option, bool sweeper) {
        World m;
        const SystemId ma = m.system("A");
        m.s.empire(kA).taggedMinefields = {at(ma, 0, 1), at(ma, 1, 1)};
        m.s.empire(kA).avoidTaggedMinefields = option;
        const DesignId d = m.ship(kA, "Walker", 3);
        if (sweeper) m.s.design(d).designType = "Mine Sweeper";
        const VehicleId ship = m.spawn(d, at(ma, 0, 0));
        fuel(m, ship);
        m.order(ship, moveTo(ma, 0, 12));
        m.move();
        return m.v(ship).location == at(ma, 0, 0);
    };
    CHECK(blocked(true, false));
    CHECK_FALSE(blocked(false, false));
    CHECK(blocked(true, true));
}

TEST_CASE("movement: etaTurns uses the speed and the known route") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    w.exploreAll(kA);
    const VehicleId ship = w.spawn(w.ship(kA, "Runner", 3), at(a, 0, 6));
    CHECK(movement::etaTurns(r, w.s, w.v(ship), at(a, 7, 6)) == 4);  // speed 3 makes 2 steps a turn (spec 03 §6.3)
    const VehicleId one = w.spawn(w.ship(kA, "Crawler", 1), at(a, 0, 6));
    CHECK(movement::etaTurns(r, w.s, w.v(one), at(a, 1, 6)) == -1);   // speed 1 never moves in a simultaneous game
    w.s.options.simultaneous = false;
    CHECK(movement::etaTurns(r, w.s, w.v(ship), at(a, 7, 6)) == 3);  // turn-based: its movement points
    CHECK(movement::etaTurns(r, w.s, w.v(one), at(a, 1, 6)) == 1);
    w.s.options.simultaneous = true;
    CHECK(movement::etaTurns(r, w.s, w.v(ship), at(a, 0, 6)) == 0);
    CHECK(movement::etaTurns(r, w.s, w.v(ship), at(b, 0, 6)) == -1);  // no link
    const VehicleId base = w.spawn(w.design(kA, "Post", "Test Station", {"Test Bridge"}), at(a, 1, 1));
    CHECK(movement::etaTurns(r, w.s, w.v(base), at(a, 2, 2)) == -1);
}

// ---- Moving and orders ---------------------------------------------------------------------------

TEST_CASE("movement: a move order runs over several turns at the ship's speed") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId ship = w.spawn(w.ship(kA, "Runner", 3), at(a, 0, 6));
    fuel(w, ship);
    w.order(ship, moveTo(a, 12, 6));
    w.move();
    // Speed 3 acts on days 11 and 21 (spec 03 §6.3).
    CHECK(w.v(ship).location == at(a, 2, 6));
    CHECK(w.v(ship).movement == 3);  // not spent in a simultaneous game
    CHECK(w.v(ship).supply == 1'000'000 - 2 * 30);  // 3 engines x 10 per step
    CHECK(w.v(ship).orders.size() == 1);
    for (int i = 0; i < 5; ++i) w.move();
    CHECK(w.v(ship).location == at(a, 12, 6));
    CHECK(w.v(ship).orders.empty());
}

TEST_CASE("movement: vehicles held in place by sabotage keep their orders and wait") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId ship = w.spawn(w.ship(kA, "Held", 3), at(a, 0, 6));
    fuel(w, ship);
    w.order(ship, moveTo(a, 6, 6));
    w.v(ship).immobileUntil = w.s.turn + 1;
    CHECK(movement::etaTurns(r, w.s, w.v(ship), at(a, 6, 6)) == 1 + 3);
    w.move();
    CHECK(w.v(ship).location == at(a, 0, 6));
    CHECK(w.v(ship).movement == 0);
    CHECK(w.v(ship).orders.size() == 1);
    ++w.s.turn;
    w.move();
    CHECK(w.v(ship).location == at(a, 2, 6));

    // A held member holds its whole fleet.
    const VehicleId free = w.spawn(w.ship(kA, "Free", 3, {"Test Quantum Reactor"}), at(a, 9, 9));
    const VehicleId stuck = w.spawn(w.ship(kA, "Stuck", 3, {"Test Quantum Reactor"}), at(a, 9, 9));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Anchor", {free, stuck}}).ok);
    fleetGets(w, w.s.fleets.back().id, {moveTo(a, 12, 12)});
    w.v(stuck).immobileUntil = w.s.turn + 1;
    w.move();
    CHECK(w.v(free).location == at(a, 9, 9));
    CHECK(w.v(stuck).location == at(a, 9, 9));
    CHECK(fleetList(w, w.s.fleets.back().id).size() == 1);
}

TEST_CASE("movement: a warp order jumps an unknown link, learns it and explores the far system") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    const auto [wa, wb] = w.link(a, {7, 6}, b, {0, 6});
    w.s.empire(kA).knowledge.explored[a.index()] = 1;
    const VehicleId ship = w.spawn(w.ship(kA, "Jumper", 3), at(a, 6, 6));
    fuel(w, ship);
    w.order(ship, mk(OrderKind::Warp, {}, wa));
    w.move();
    CHECK(w.v(ship).location == at(b, 0, 6));  // a step on day 11, the jump on day 21
    CHECK(w.v(ship).orders.empty());
    CHECK(w.v(ship).movement == 3);
    CHECK(sight::knowsWarpLink(w.s, kA, wa));
    CHECK(sight::knowsWarpLink(w.s, kA, wb));
    CHECK(w.s.empire(kA).hasExplored(b));
    CHECK_FALSE(w.s.empire(kB).hasExplored(b));
}

TEST_CASE("movement: explore picks the nearest unexplored warp point and skips claimed ones") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 10, 0), c = w.system("C", 0, 10);
    const ObjectId toB = w.link(a, {8, 6}, b, {0, 6}).first;
    const ObjectId toC = w.link(a, {6, 8}, c, {0, 6}).first;
    w.s.empire(kA).knowledge.explored[a.index()] = 1;
    const DesignId scout = w.ship(kA, "Scout", 3);
    const VehicleId s1 = w.spawn(scout, at(a, 6, 6));
    const VehicleId s2 = w.spawn(scout, at(a, 6, 6));
    for (VehicleId id : {s1, s2}) {
        fuel(w, id);
        w.give(id, {mk(OrderKind::Explore)});
    }
    // Explore is expanded when given: Move To plus Warp for the nearest warp point
    // into unexplored space; the second scout skips the one the first is bound for.
    REQUIRE(w.v(s1).orders.size() == 2);
    CHECK(w.v(s1).orders[0] == moveTo(a, 8, 6));
    CHECK(w.v(s1).orders[1].kind == OrderKind::Warp);
    CHECK(w.v(s1).orders[1].object == toB);
    REQUIRE(w.v(s2).orders.size() == 2);
    CHECK(w.v(s2).orders[0] == moveTo(a, 6, 8));
    CHECK(w.v(s2).orders[1].object == toC);
    w.move();
    CHECK(w.v(s1).location == at(a, 8, 6));  // two steps a turn at speed 3
    CHECK(w.v(s2).location == at(a, 6, 8));
    w.move();
    CHECK(w.v(s1).location == at(b, 0, 6));
    CHECK(w.v(s2).location == at(c, 0, 6));
    CHECK(w.v(s1).orders.empty());
    CHECK(w.v(s2).orders.empty());
    CHECK(w.s.empire(kA).hasExplored(b));
    CHECK(w.s.empire(kA).hasExplored(c));

    // Nothing left: nothing is added.
    const VehicleId s3 = w.spawn(scout, at(a, 6, 6));
    w.give(s3, {mk(OrderKind::Explore)});
    CHECK(w.v(s3).orders.empty());
    // An Explore that reached a list another way is expanded when it comes up; with nothing left it goes, with a note.
    w.order(s3, mk(OrderKind::Explore));
    w.move();
    CHECK(w.v(s3).orders.empty());
    CHECK(w.v(s3).location == at(a, 6, 6));
    CHECK(w.logged(kA, "nothing left to explore"));
}

TEST_CASE("movement: fleets move together at the slowest member's speed") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId fast = w.spawn(w.ship(kA, "Fast", 5, {"Test Quantum Reactor"}), at(a, 0, 6));
    const VehicleId slow = w.spawn(w.ship(kA, "Slow", 3, {"Test Quantum Reactor"}), at(a, 0, 6));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pack", {fast, slow}}).ok);
    const FleetId fleet = w.s.fleets.back().id;
    fleetGets(w, fleet, {moveTo(a, 10, 6)});
    CHECK(movement::fleetSpeed(r, w.s, *w.s.fleet(fleet)) == 3);
    CHECK(movement::etaTurns(r, w.s, w.v(fast), at(a, 10, 6)) == 5);  // 2 steps a turn at speed 3
    w.move();
    CHECK(w.v(fast).location == at(a, 2, 6));
    CHECK(w.v(slow).location == at(a, 2, 6));
    CHECK(w.v(fast).movement == 3);  // every member starts with the fleet's lowest maximum (spec 03 §6.3)
    for (int i = 0; i < 4; ++i) w.move();
    CHECK(w.v(fast).location == at(a, 10, 6));
    CHECK(w.v(slow).location == at(a, 10, 6));
    CHECK(fleetList(w, fleet).empty());
    CHECK(w.s.fleet(fleet)->location == at(a, 10, 6));  // the fleet's location follows its members (spec 03 §9)

    // A fleet has no list of its own: an order in one member's list is carried
    // out by every member at the fleet's location (spec 03 §8, §19 Q65).
    w.order(fast, moveTo(a, 12, 6));
    w.move();
    CHECK(w.v(fast).location == at(a, 12, 6));
    CHECK(w.v(slow).location == at(a, 12, 6));
    CHECK(w.v(fast).orders.empty());
}

TEST_CASE("movement: fleet supply is pooled in equal shares at the end of the turn") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId full = w.spawn(w.ship(kA, "Full", 1), at(a, 0, 6));
    const VehicleId empty = w.spawn(w.ship(kA, "Empty", 1), at(a, 0, 6));
    w.v(empty).supply = 0;
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {full, empty}}).ok);
    fleetGets(w, w.s.fleets.back().id, {moveTo(a, 5, 6)});
    w.move();
    // Speed 1 (and a member out of supply has 1 MP) never moves in a simultaneous game (spec 03 §6.3).
    CHECK(w.v(full).location == at(a, 0, 6));
    CHECK(w.v(full).supply == 100);  // no pooling while moving
    CHECK(w.v(empty).supply == 0);
    w.upkeep();
    CHECK(w.v(full).supply == 50);  // 100 shared by two
    CHECK(w.v(empty).supply == 50);

    // The remainder of the division is lost.
    w.v(full).supply = 100;
    w.v(empty).supply = 1;
    w.upkeep();
    CHECK(w.v(full).supply == 50);
    CHECK(w.v(empty).supply == 50);

    // A share that does not fit goes, in member order, to members with room.
    const VehicleId small = w.spawn(w.ship(kA, "Small", 1), at(a, 9, 9));
    const VehicleId big = w.spawn(w.ship(kA, "Big", 1), at(a, 9, 9));
    fuel(w, big);
    w.v(small).supply = 0;
    w.v(big).supply = 1000;
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Odd", {small, big}}).ok);
    w.upkeep();
    CHECK(w.v(small).supply == 100);
    CHECK(w.v(big).supply == 900);

    // A member with unlimited supply refills the others.
    const VehicleId reactor = w.spawn(w.ship(kA, "Reactor", 1, {"Test Quantum Reactor"}), at(a, 3, 3));
    const VehicleId tender = w.spawn(w.ship(kA, "Tender", 1), at(a, 3, 3));
    w.v(tender).supply = 0;
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Endless", {reactor, tender}}).ok);
    w.upkeep();
    CHECK(w.v(tender).supply == 100);
    CHECK(w.v(reactor).supply == kUnlimitedSupply);
}

TEST_CASE("movement: supply is spent per step; without supply a ship has 1 movement point") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId ship = w.spawn(w.ship(kA, "Thirsty", 2), at(a, 0, 6));
    CHECK(movement::moveSupplyCost(r, w.s, w.v(ship)) == 20);
    w.v(ship).supply = 50;
    w.order(ship, moveTo(a, 12, 6));
    w.move();  // speed 2: one step, on day 16
    CHECK(w.v(ship).location == at(a, 1, 6));
    CHECK(w.v(ship).supply == 30);
    w.move();
    CHECK(w.v(ship).location == at(a, 2, 6));
    CHECK(w.v(ship).supply == 10);
    w.move();  // the step empties the tanks: the maximum drops to 1
    CHECK(w.v(ship).location == at(a, 3, 6));
    CHECK(w.v(ship).supply == 0);
    CHECK(vehicleMaxMovement(r, w.s, w.v(ship)) == 1);
    w.move();  // speed 1 never moves in a simultaneous game (spec 03 §6.3)
    CHECK(w.v(ship).location == at(a, 3, 6));
    w.s.options.simultaneous = false;
    TurnContext ctx{r, w.s, {}, {}, {}};
    movement::startTurn(ctx, kA);
    movement::runLive(ctx, movement::LiveMove{kA});
    CHECK(w.v(ship).location == at(a, 4, 6));  // a turn-based game: one move a turn
    w.s.options.simultaneous = true;

    // A maximum that drops during the vehicle's own action stops it for the rest
    // of the turn (spec 03 §6.3 step 4): speed 6 acts on days 6, 11, 16, 21 and 26,
    // but its third step empties the tanks.
    const VehicleId racer = w.spawn(w.ship(kA, "Racer", 6), at(a, 0, 8));
    fuel(w, racer);
    w.v(racer).supply = 130;  // 60 a step
    w.order(racer, moveTo(a, 12, 8));
    w.move();
    CHECK(w.v(racer).location == at(a, 3, 8));
    CHECK(w.v(racer).supply == 0);
    CHECK(w.v(racer).movement == 0);

    // Racial Supply Cost -50 %.
    w.s.empire(kA).race.traits.push_back(traitIndex(r, "Mv Frugal"));
    CHECK(movement::moveSupplyCost(r, w.s, w.v(ship)) == 10);

    // A quantum reactor never runs down: supply is held at the unlimited marker (spec 03 §7).
    const VehicleId q = w.spawn(w.ship(kA, "Reactor", 2, {"Test Quantum Reactor"}), at(a, 0, 0));
    CHECK(movement::moveSupplyCost(r, w.s, w.v(q)) == 0);
    CHECK(w.v(q).supply == kUnlimitedSupply);
    w.order(q, moveTo(a, 0, 12));
    w.move();
    CHECK(w.v(q).location == at(a, 0, 1));
    CHECK(w.v(q).supply == kUnlimitedSupply);
    w.upkeep();
    CHECK(w.v(q).supply == kUnlimitedSupply);
    // Losing the reactor brings it back to a normal value on the next move.
    w.v(q).damage[6] = 1000;
    w.v(q).orders = {moveTo(a, 0, 3)};
    w.move();
    CHECK(w.v(q).location == at(a, 0, 2));
    CHECK(w.v(q).supply == 100 - 10);  // one step at the Frugal race's half cost
    // Bases never run out either.
    const VehicleId base = w.spawn(w.design(kA, "Post", "Test Station", {"Test Bridge"}), at(a, 5, 5));
    CHECK(w.v(base).supply == kUnlimitedSupply);
}

TEST_CASE("movement: resupply at own and allied depots, also when passing through") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId depot = w.planet(a, {3, 6});
    w.colony(depot, kA, 0, {"Test Depot"});  // depots need no population (spec 03 §7)
    w.exploreAll(kA);
    const VehicleId ship = w.spawn(w.ship(kA, "Tanker", 3), at(a, 0, 6));
    w.order(ship, mk(OrderKind::Resupply));
    w.move();
    w.move();
    CHECK(w.v(ship).location == at(a, 3, 6));
    CHECK(w.v(ship).supply == 100);  // would be 10 without the depot
    CHECK(w.v(ship).orders.empty());

    const VehicleId passer = w.spawn(w.ship(kA, "Passer", 3), at(a, 0, 6));
    w.order(passer, moveTo(a, 6, 6));
    w.move();
    w.move();
    CHECK(w.v(passer).location == at(a, 4, 6));
    CHECK(w.v(passer).supply == 100 - 30);  // refilled at the depot on the way

    // An ally's depot works from Military Alliance up.
    const ObjectId foreign = w.planet(a, {9, 9});
    w.colony(foreign, kB, 1000, {"Test Depot"});
    CHECK_FALSE(movement::resupplyDepotAt(r, w.s, kA, at(a, 9, 9)));
    w.setTreaty(kA, kB, Treaty::NonAggression);
    CHECK_FALSE(movement::resupplyDepotAt(r, w.s, kA, at(a, 9, 9)));
    w.setTreaty(kA, kB, Treaty::MilitaryAlliance);
    CHECK(movement::resupplyDepotAt(r, w.s, kA, at(a, 9, 9)));

    // No depot anywhere reachable: the order is dropped.
    World lone;
    const SystemId la = lone.system("L");
    const VehicleId stray = lone.spawn(lone.ship(kA, "Stray", 3), at(la, 0, 0));
    lone.order(stray, mk(OrderKind::Resupply));
    lone.move();
    CHECK(lone.v(stray).orders.empty());
    CHECK(lone.logged(kA, "resupply depot"));
}

TEST_CASE("movement: repair orders and repair by priority, aptitude and culture") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId yard = w.planet(a, {6, 6});
    w.colony(yard, kA, 1000, {"Test Repair Yard"});
    const VehicleId ship = w.spawn(w.ship(kA, "Wreck", 3, {"Mv Armor"}), at(a, 2, 6));
    fuel(w, ship);
    for (int& d : w.v(ship).damage) d = 1;
    w.order(ship, mk(OrderKind::Repair));
    w.move();
    w.move();
    CHECK(w.v(ship).location == at(a, 6, 6));
    CHECK(w.v(ship).orders.empty());
    CHECK(movement::repairCapacityAt(r, w.s, kA, at(a, 6, 6)) == xmath::pctTrunc(20, 110));  // culture +10 %
    w.upkeep();
    CHECK(totalDamage(w.v(ship)) == 0);

    // A repair bay with a clumsy race: truncate(5 x (100 - 40 + 10) %) = 3 components, by priority.
    World p;
    const SystemId pa = p.system("P");
    p.s.empire(kA).race.characteristics[static_cast<size_t>(Characteristic::RepairAptitude)] = 60;
    p.s.empire(kA).repairPriorities = {"Armor", "Engines"};
    p.spawn(p.design(kA, "Tender", "Test Frigate", {"Test Repair Bay"}), at(pa, 1, 1));
    const VehicleId hurt = p.spawn(p.ship(kA, "Hurt", 2, {"Mv Armor"}), at(pa, 1, 1));
    CHECK(movement::repairCapacityAt(r, p.s, kA, at(pa, 1, 1)) == 3);
    for (int& d : p.v(hurt).damage) d = 5;
    p.upkeep();
    const Vehicle& h = p.v(hurt);
    CHECK(h.damage[6] == 0);  // armor first
    CHECK(h.damage[4] == 0);  // then engines
    CHECK(h.damage[5] == 0);
    CHECK(h.damage[0] == 5);
    CHECK(h.damage[3] == 5);

    // Nowhere to repair.
    World lone;
    const SystemId la = lone.system("L");
    const VehicleId stray = lone.spawn(lone.ship(kA, "Stray", 3), at(la, 0, 0));
    lone.order(stray, mk(OrderKind::Repair));
    lone.move();
    CHECK(lone.v(stray).orders.empty());
}

TEST_CASE("movement: combat is offered once per sector and phase, only where orders were executed") {
    World w;
    const SystemId a = w.system("A");
    // Speed 4 steps on days 8, 16 and 23 (spec 03 §6.3).
    const VehicleId raider = w.spawn(w.ship(kA, "Raider", 4), at(a, 0, 6));
    const VehicleId guard = w.spawn(w.ship(kB, "Guard", 3), at(a, 3, 6));
    fuel(w, raider);
    w.order(raider, moveTo(a, 3, 6));
    w.order(raider, moveTo(a, 6, 6));
    CombatSpy spy;
    spy.fight = hostilesMeet;
    w.move(spy.hooks());
    CHECK(spy.asked == std::vector<Location>{at(a, 1, 6), at(a, 2, 6), at(a, 3, 6)});
    REQUIRE(spy.fought.size() == 1);
    CHECK(spy.fought[0].second == at(a, 3, 6));
    // Combat is told who stepped in that day (the mines' victims, spec 04 §10.6),
    // and each step records the sector left (attackers and start boxes, spec 04 §3).
    REQUIRE(spy.entered.size() == 1);
    CHECK(spy.entered[0] == std::vector<VehicleId>{raider});
    CHECK(w.v(raider).cameFrom == at(a, 2, 6));
    CHECK(w.v(raider).cameFromTurn == w.s.turn);
    CHECK_FALSE(w.v(guard).cameFrom.system.valid());
    // Fighting clears no orders (spec 03 §6.3): the next one is still there.
    REQUIRE(w.v(raider).orders.size() == 1);
    CHECK(w.v(raider).orders.front() == moveTo(a, 6, 6));
    CHECK(w.v(guard).location == at(a, 3, 6));

    // Idle hostiles sharing a sector are not offered again; the raider moves on.
    CombatSpy again;
    again.fight = hostilesMeet;
    w.move(again.hooks());
    CHECK(std::find(again.asked.begin(), again.asked.end(), at(a, 3, 6)) == again.asked.end());
    CHECK(w.v(raider).location == at(a, 6, 6));

    // Two ships entering the same sector on the same day: one offer.
    World t;
    const SystemId ta = t.system("T");
    const DesignId d = t.ship(kA, "Twin", 4);
    for (int i = 0; i < 2; ++i) {
        const VehicleId id = t.spawn(d, at(ta, 0, 0));
        fuel(t, id);
        t.order(id, moveTo(ta, 3, 0));
    }
    CombatSpy twin;
    t.move(twin.hooks());
    CHECK(twin.asked.size() == 3);
}

TEST_CASE("movement: simultaneous games check every sector where an order was carried out, a waiting Sentry included") {
    // Spec 04 §2 (confirmed: binary).
    World w;
    const SystemId a = w.system("A");
    const VehicleId watch = w.spawn(w.ship(kA, "Watch", 4), at(a, 5, 5));  // acts on days 8, 16 and 23
    fuel(w, watch);
    w.order(watch, mk(OrderKind::Sentry));
    const VehicleId idle = w.spawn(w.ship(kA, "Idle", 4), at(a, 7, 7));  // no orders: never checked
    fuel(w, idle);
    CombatSpy spy;
    w.move(spy.hooks());
    CHECK(spy.asked == std::vector<Location>(3, at(a, 5, 5)));
    for (const auto& group : spy.checkers) CHECK(group.empty());  // the day's check of the sector
    CHECK(w.v(watch).orders.size() == 1);
}

TEST_CASE("movement: turn-based games check for battle only on a step, an Attack, or a Seek at its target") {
    // Spec 04 §2, questions 18 and 48 (confirmed: binary).
    struct Live {
        World w;
        SystemId a;
        CombatSpy spy;
        Live() {
            w.s.options.simultaneous = false;
            a = w.system("A");
            w.exploreAll(kA);
            w.setTreaty(kA, kB, Treaty::War);
            spy.fight = hostilesMeet;
        }
        void run(bool refill = true) {
            TurnContext ctx{w.rules(), w.s, {}, {}, {}};
            if (refill) movement::startTurn(ctx, kA);
            movement::runLive(ctx, movement::LiveMove{kA}, spy.hooks());
            w.s.removeDeadVehicles();
        }
    };
    const auto picketAt = [](Live& l, int x, int y) { return l.w.spawn(l.w.ship(kB, "Picket", 1, {"Mv Armor", "Mv Armor"}), at(l.a, x, y)); };

    SUBCASE("orders carried out in place, Sentry included, never start one") {
        Live l;
        l.w.colony(l.w.planet(l.a, {5, 5}), kA, 1000);
        const VehicleId hauler = l.w.spawn(l.w.ship(kA, "Hauler", 3, {"Test Cargo Bay"}), at(l.a, 5, 5));
        fuel(l.w, hauler);
        picketAt(l, 5, 5);
        l.w.order(hauler, mk(OrderKind::LoadCargo, {}, {}, {}, {}, -1));
        l.w.order(hauler, mk(OrderKind::Decloak));
        l.w.order(hauler, mk(OrderKind::Sentry));
        l.run();
        CHECK(l.spy.asked.empty());
        CHECK(l.spy.fought.empty());
        CHECK(l.w.v(hauler).cargo.totalPopulation() > 0);
        CHECK(l.w.v(hauler).orders.empty());  // the Sentry ended at once: an enemy is in the system
    }
    SUBCASE("the Attack order runs one where the group stands and is used up; the rest of the list goes on") {
        Live l;
        const VehicleId gunboat = l.w.spawn(l.w.ship(kA, "Gunboat", 3, {"Test Laser"}), at(l.a, 5, 5));
        fuel(l.w, gunboat);
        const VehicleId picket = picketAt(l, 5, 5);
        l.w.order(gunboat, mk(OrderKind::Attack, {}, {}, picket));
        l.w.order(gunboat, moveTo(l.a, 5, 7));
        l.run();
        REQUIRE(l.spy.asked.size() == 3);
        CHECK(l.spy.asked[0] == at(l.a, 5, 5));
        CHECK(l.spy.checkers[0] == std::vector<VehicleId>{gunboat});
        REQUIRE(l.spy.fought.size() == 1);
        CHECK(l.spy.fought[0].second == at(l.a, 5, 5));
        // 1 movement point for the attack, then two steps of the Move To (each checked).
        CHECK(l.spy.asked[1] == at(l.a, 5, 6));
        CHECK(l.w.v(gunboat).location == at(l.a, 5, 7));
        CHECK(l.w.v(gunboat).orders.empty());
        CHECK(l.w.v(gunboat).movement == 0);
    }
    SUBCASE("without movement left the Attack is removed doing nothing") {
        Live l;
        const VehicleId gunboat = l.w.spawn(l.w.ship(kA, "Gunboat", 3, {"Test Laser"}), at(l.a, 5, 5));
        fuel(l.w, gunboat);
        const VehicleId picket = picketAt(l, 5, 5);
        l.w.order(gunboat, mk(OrderKind::Attack, {}, {}, picket));
        l.w.v(gunboat).movement = 0;
        l.run(false);
        CHECK(l.spy.asked.empty());
        CHECK(l.w.v(gunboat).orders.empty());
        CHECK(l.w.logged(kA, "no movement left to attack"));
    }
    SUBCASE("a step into the target's sector fights there and clears the whole list") {
        Live l;
        const VehicleId gunboat = l.w.spawn(l.w.ship(kA, "Gunboat", 3, {"Test Laser"}), at(l.a, 3, 5));
        fuel(l.w, gunboat);
        const VehicleId picket = picketAt(l, 5, 5);
        l.w.order(gunboat, mk(OrderKind::Attack, at(l.a, 5, 5), {}, picket));
        l.w.order(gunboat, moveTo(l.a, 0, 0));
        l.run();
        CHECK(l.spy.asked == std::vector<Location>{at(l.a, 4, 5), at(l.a, 5, 5)});
        REQUIRE(l.spy.fought.size() == 1);
        CHECK(l.w.v(gunboat).location == at(l.a, 5, 5));
        CHECK(l.w.v(gunboat).orders.empty());
        CHECK(l.w.logged(kA, "Combat on entering the sector."));
    }
    SUBCASE("the Attack goes to the sector its target was in when given and attacks there") {
        // Spec 03 §8: Move To the target's sector plus an Attack there.
        Live l;
        const VehicleId gunboat = l.w.spawn(l.w.ship(kA, "Gunboat", 3, {"Test Laser"}), at(l.a, 3, 5));
        fuel(l.w, gunboat);
        const VehicleId picket = picketAt(l, 5, 5);
        l.w.order(gunboat, mk(OrderKind::Attack, at(l.a, 5, 5), {}, picket));
        l.w.v(picket).location = at(l.a, 9, 9);  // it has moved on since
        l.run();
        CHECK(l.spy.asked == std::vector<Location>{at(l.a, 4, 5), at(l.a, 5, 5), at(l.a, 5, 5)});
        CHECK(l.spy.checkers.back() == std::vector<VehicleId>{gunboat});
        CHECK(l.spy.fought.empty());
        CHECK(l.w.v(gunboat).location == at(l.a, 5, 5));
        CHECK(l.w.v(gunboat).orders.empty());
        CHECK(l.w.v(gunboat).movement == 0);
    }
    SUBCASE("an Attack with no sector recorded attacks where the group stands") {
        // Spec 03 §8, §19 Q71: the stored Attack names no place; it does not follow its target.
        Live l;
        const VehicleId gunboat = l.w.spawn(l.w.ship(kA, "Gunboat", 3, {"Test Laser"}), at(l.a, 3, 5));
        fuel(l.w, gunboat);
        const VehicleId picket = picketAt(l, 5, 5);
        l.w.order(gunboat, mk(OrderKind::Attack, {}, {}, picket));
        l.run();
        CHECK(l.spy.asked == std::vector<Location>{at(l.a, 3, 5)});
        CHECK(l.w.v(gunboat).location == at(l.a, 3, 5));
        CHECK(l.w.v(gunboat).orders.empty());
        CHECK(l.w.v(gunboat).movement == 2);  // the attack's 1 movement point
    }
    SUBCASE("the Attack decloaks nobody; the Ship Cloaking minister's vehicles cloak again afterwards") {
        // Spec 03 §6.4, §8, §19 Q69 (confirmed: binary).
        for (const bool minister : {false, true}) {
            CAPTURE(minister);
            Live l;
            const VehicleId gunboat = l.w.spawn(l.w.ship(kA, "Gunboat", 3, {"Test Laser", "Mv Cloak"}), at(l.a, 5, 5));
            fuel(l.w, gunboat);
            l.w.v(gunboat).status = VehicleStatus::Cloaked;
            if (minister) {
                l.w.s.empire(kA).ministers |= ministerBit(Minister::ShipCloaking);
                l.w.v(gunboat).minister = true;
            }
            const VehicleId picket = picketAt(l, 5, 5);
            l.w.order(gunboat, mk(OrderKind::Attack, at(l.a, 5, 5), {}, picket));
            std::optional<VehicleStatus> during;
            l.spy.fight = [&](const GameState& s, Location) {
                during = s.vehicle(gunboat)->status;
                return false;
            };
            l.run();
            REQUIRE(during);
            CHECK(*during == (minister ? VehicleStatus::Normal : VehicleStatus::Cloaked));
            CHECK(l.w.v(gunboat).status == VehicleStatus::Cloaked);  // raised again, or never lowered
            CHECK(l.w.v(gunboat).orders.empty());
        }
    }
    SUBCASE("a Seek at its target attacks every time its list runs and stays") {
        Live l;
        const DesignId dart = l.w.design(kA, "Dart", "Test Drone Hull", {"Mv Engine", "Mv Engine", "Mv Engine", "Test Warhead", "Mv Drone Tank"});
        const VehicleId drone = l.w.spawn(dart, at(l.a, 5, 5));
        const VehicleId picket = picketAt(l, 5, 5);
        l.w.order(drone, mk(OrderKind::Attack, {}, {}, picket));
        l.run();
        REQUIRE(l.spy.fought.size() == 1);
        CHECK(l.spy.checkers[0] == std::vector<VehicleId>{drone});
        CHECK(l.w.v(drone).orders.size() == 1);
        l.run(false);  // the list runs again, as it does when orders are given
        CHECK(l.spy.fought.size() == 2);
        CHECK(l.w.v(drone).orders.size() == 1);
        // A Seek that steps into its target's sector and meets a battle there
        // only stops for this run of its list: its order and list are kept, and
        // it attacks at the next run (spec 04 §2, §19.2 Q76).
        const VehicleId late = l.w.spawn(dart, at(l.a, 3, 5));
        l.w.order(late, mk(OrderKind::Attack, {}, {}, picket));
        l.w.order(late, moveTo(l.a, 0, 0));
        l.w.v(drone).orders.clear();
        const size_t battles = l.spy.fought.size();
        l.run();
        CHECK(l.w.v(late).location == at(l.a, 5, 5));
        CHECK(l.spy.fought.size() == battles + 1);
        CHECK(l.w.v(late).orders.size() == 2);
        CHECK_FALSE(l.w.logged(kA, "Combat on entering the sector."));
        l.run(false);
        CHECK(l.spy.fought.size() == battles + 2);
        CHECK(l.spy.checkers.back() == std::vector<VehicleId>{late});
        CHECK(l.w.v(late).orders.size() == 2);
    }
}

TEST_CASE("movement: with the real combat module, meetings become battles and mines stop a ship") {
    const movement::CombatHooks real = movement::defaultCombatHooks();
    REQUIRE(real.possible);
    REQUIRE(real.resolve);

    World w;
    const SystemId a = w.system("A");
    const VehicleId runner = w.spawn(w.ship(kA, "Runner", 3), at(a, 0, 6));
    const VehicleId picket = w.spawn(w.ship(kB, "Picket", 1, {"Test Laser"}), at(a, 1, 6));
    fuel(w, runner);
    // Steps avoid a visible hostile's square unless it is where the ship is going (spec 03 §6.2).
    w.order(runner, moveTo(a, 1, 6));
    w.order(runner, moveTo(a, 6, 6));
    w.move(real);
    REQUIRE_FALSE(w.s.combats.empty());
    CHECK(w.s.combats.front().location == at(a, 1, 6));
    // Combat does not stop the runner or clear its order (spec 03 §6.3).
    if (const Vehicle* v = w.s.vehicle(runner); v && !v->orders.empty()) CHECK(v->orders.front() == moveTo(a, 6, 6));
    if (const Vehicle* v = w.s.vehicle(runner); v && vehicleMaxMovement(w.rules(), w.s, *v) > 1)
        CHECK(v->location != at(a, 1, 6));
    (void)picket;

    // Mines strike a ship entering their sector: it stops and its order fails (spec 03 §6.4).
    World m;
    const SystemId ma = m.system("A");
    const VehicleId field = m.spawn(m.design(kB, "Mine", "Mv Mine Hull", {"Test Warhead"}), at(ma, 1, 6));
    const VehicleId tough = m.spawn(m.ship(kA, "Tough", 3, {"Mv Armor", "Mv Armor", "Mv Armor"}), at(ma, 0, 6));
    fuel(m, tough);
    m.order(tough, moveTo(ma, 3, 6));
    m.order(tough, moveTo(ma, 5, 6));
    m.move(real);
    CHECK(m.s.vehicle(field) == nullptr);  // the mine was used up
    REQUIRE(m.s.vehicle(tough));
    CHECK(m.v(tough).location == at(ma, 1, 6));
    CHECK(m.v(tough).orders.empty());
    CHECK(totalDamage(m.v(tough)) > 0);
    CHECK(m.s.combats.empty());  // mines alone are no battle
    CHECK(m.logged(kA, "minefield"));
}

TEST_CASE("movement: combat neither stops a moving ship nor clears its orders") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId runner = w.spawn(w.ship(kA, "Runner", 4), at(a, 0, 6));
    w.spawn(w.ship(kB, "Picket", 1), at(a, 1, 6));
    fuel(w, runner);
    w.order(runner, moveTo(a, 1, 6));  // into the picket's square on purpose (spec 03 §6.2)
    w.order(runner, moveTo(a, 6, 6));
    CombatSpy spy;
    spy.fight = hostilesMeet;
    w.move(spy.hooks());
    CHECK(w.v(runner).location == at(a, 3, 6));
    CHECK(w.v(runner).orders.size() == 1);
    REQUIRE(spy.fought.size() == 1);
    CHECK(spy.fought[0].second == at(a, 1, 6));

    // At peace, the same ship passes by.
    World p;
    const SystemId pa = p.system("A");
    const VehicleId friendly = p.spawn(p.ship(kA, "Runner", 4), at(pa, 0, 6));
    p.spawn(p.ship(kB, "Picket", 1), at(pa, 1, 6));
    p.setTreaty(kA, kB, Treaty::NonAggression);
    fuel(p, friendly);
    p.order(friendly, moveTo(pa, 6, 6));
    CombatSpy calm;
    calm.fight = hostilesMeet;
    p.move(calm.hooks());
    CHECK(p.v(friendly).location == at(pa, 3, 6));
    CHECK(calm.fought.empty());
}

TEST_CASE("movement: mined sectors are offered to combat; sweep orders clear hostile mines") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId mines = w.spawn(w.design(kB, "Mine", "Mv Mine Hull", {"Test Warhead"}), at(a, 1, 6));
    w.v(mines).count = 5;
    const VehicleId sweeper = w.spawn(w.ship(kA, "Sweeper", 3, {"Mv Sweeper", "Mv Armor", "Mv Armor", "Mv Armor", "Mv Armor"}), at(a, 0, 6));
    fuel(w, sweeper);
    w.order(sweeper, moveTo(a, 2, 6));
    CombatSpy spy;
    w.move(spy.hooks());
    CHECK(w.v(mines).count == 5);  // entering: combat sweeps and detonates (combat.hpp)
    CHECK(std::find(spy.asked.begin(), spy.asked.end(), at(a, 1, 6)) != spy.asked.end());

    // Mines strike (combat is offered) but do not stop the ship.
    World m;
    const SystemId ma = m.system("A");
    const VehicleId field = m.spawn(m.design(kB, "Mine", "Mv Mine Hull", {"Test Warhead"}), at(ma, 1, 6));
    const VehicleId runner = m.spawn(m.ship(kA, "Runner", 4), at(ma, 0, 6));
    fuel(m, runner);
    m.order(runner, moveTo(ma, 3, 6));
    CombatSpy blast;
    blast.fight = [&](const GameState&, Location l) { return l == at(ma, 1, 6); };
    m.move(blast.hooks());
    REQUIRE(blast.fought.size() == 1);
    CHECK(m.v(runner).location == at(ma, 3, 6));
    (void)field;

    // A Sweep Mines order runs the whole encounter again in place (spec 03 §8,
    // §12): the sweeper clears 3 mines, then the 2 left strike it (60 damage
    // each, straight to the armor) and are used up. The Move To that arrives and
    // the sweep happen in one action.
    w.order(sweeper, moveTo(a, 1, 6));
    w.order(sweeper, mk(OrderKind::SweepMines));
    w.move();
    CHECK(w.s.vehicle(mines) == nullptr);
    REQUIRE(w.s.vehicle(sweeper));
    CHECK(w.v(sweeper).location == at(a, 1, 6));
    CHECK(w.v(sweeper).orders.empty());
    CHECK(totalDamage(w.v(sweeper)) >= 4 * 30);  // the four armor plates
    CHECK(w.logged(kA, "cleared 3 enemy mines"));

    // Mines of a treaty partner are left alone.
    World p;
    const SystemId pa = p.system("A");
    const VehicleId friendly = p.spawn(p.design(kB, "Mine", "Mv Mine Hull", {"Test Warhead"}), at(pa, 1, 6));
    p.v(friendly).count = 5;
    p.setTreaty(kA, kB, Treaty::NonAggression);
    const VehicleId sw = p.spawn(p.ship(kA, "Sweeper", 3, {"Mv Sweeper"}), at(pa, 1, 6));
    p.order(sw, mk(OrderKind::SweepMines));
    p.move();
    CHECK(p.v(friendly).count == 5);
}

TEST_CASE("movement: sentry orders end when an enemy is present or supplies run low") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    const auto [ab, ba] = w.link(a, {0, 6}, b, {12, 6});
    const VehicleId watch = w.spawn(w.ship(kA, "Watch", 4), at(a, 10, 10));  // acts on days 8, 16 and 23
    fuel(w, watch);
    w.order(watch, mk(OrderKind::Sentry));
    w.order(watch, moveTo(a, 10, 9));
    w.move();
    CHECK(w.v(watch).orders.size() == 2);  // nobody around

    // An enemy arrives through the warp point during the turn.
    w.s.empire(kB).knowledge.explored[b.index()] = 1;
    const VehicleId intruder = w.spawn(w.ship(kB, "Intruder", 3), at(b, 11, 6));
    fuel(w, intruder);
    w.order(intruder, mk(OrderKind::Warp, {}, ba));
    w.move();
    CHECK(w.v(intruder).location == at(a, 0, 6));  // on day 21
    // On day 23 the Sentry counts as done and the Move To after it runs in the
    // same action (spec 03 §6.3, §8).
    CHECK(w.v(watch).orders.empty());
    CHECK(w.v(watch).location == at(a, 10, 9));
    CHECK(w.logged(kA, "enemy sighted"));

    // Low supplies end it too.
    const VehicleId thirsty = w.spawn(w.ship(kA, "Thirsty", 2), at(b, 3, 3));
    w.order(thirsty, mk(OrderKind::Sentry));
    w.move();
    CHECK(w.v(thirsty).orders.empty());
    CHECK(w.logged(kA, "supplies low"));

    // Combat removes a Sentry order at the head of a participant's list, and nothing else.
    World f;
    const SystemId fa = f.system("A");
    const VehicleId guard = f.spawn(f.ship(kA, "Guard", 1), at(fa, 6, 6));
    fuel(f, guard);
    f.order(guard, mk(OrderKind::Sentry));
    f.order(guard, moveTo(fa, 6, 5));
    const VehicleId raider = f.spawn(f.ship(kB, "Raider", 2), at(fa, 6, 7));
    fuel(f, raider);
    f.order(raider, moveTo(fa, 6, 6));
    f.order(raider, moveTo(fa, 6, 8));
    f.setTreaty(kA, kB, Treaty::NonAggression);  // no alarm from the raider's presence alone
    int battles = 0;
    movement::CombatHooks hooks{[](const Rules&, const GameState& gs, Location l, const combat::BattleCheck&) { return hostilesMeet(gs, l); },
                                [&](TurnContext& ctx, Location l, std::span<const VehicleId>, const combat::BattleCheck&) {
                                    ++battles;
                                    CombatRecord rec;
                                    rec.location = l;
                                    for (VehicleId id : {guard, raider}) {
                                        CombatPiece p;
                                        p.vehicle = id;
                                        rec.pieces.push_back(p);
                                    }
                                    ctx.state.combats.push_back(rec);
                                }};
    f.setTreaty(kA, kB, Treaty::War);
    f.s.empire(kA).relation(kB).treaty = Treaty::NonAggression;  // A is at peace, B attacks
    f.move(hooks);
    CHECK(battles == 1);
    REQUIRE(f.v(guard).orders.size() == 1);
    CHECK(f.v(guard).orders.front() == moveTo(fa, 6, 5));
    REQUIRE(f.v(raider).orders.size() == 1);  // the raider's list is kept
    CHECK(f.v(raider).orders.front() == moveTo(fa, 6, 8));

    // A cloaked enemy goes unnoticed.
    World c;
    const SystemId ca = c.system("A");
    const VehicleId post = c.spawn(c.ship(kA, "Post", 2), at(ca, 10, 10));
    fuel(c, post);
    c.order(post, mk(OrderKind::Sentry));
    const VehicleId ghost = c.spawn(c.ship(kB, "Ghost", 1, {"Mv Cloak"}), at(ca, 0, 0));
    c.v(ghost).status = VehicleStatus::Cloaked;
    c.move();
    CHECK(c.v(post).orders.size() == 1);

    // With Repeat on, an ending Sentry stays in the list and execution moves past it (spec 03 §8).
    World rp;
    const SystemId ra = rp.system("A");
    const VehicleId rover = rp.spawn(rp.ship(kA, "Rover", 2), at(ra, 5, 5));
    rp.v(rover).supply = 10;  // low: the Sentry ends at once
    rp.order(rover, mk(OrderKind::Sentry), true);
    rp.order(rover, mk(OrderKind::Decloak), true);
    rp.move();
    REQUIRE(rp.v(rover).orders.size() == 2);
    CHECK(rp.v(rover).repeatOrders);

    // Only objects the owner sees count: an unseen enemy colony is no alarm (spec 03 §8).
    World uc;
    const SystemId ua = uc.system("A");
    const VehicleId lookout = uc.spawn(uc.ship(kA, "Lookout", 2), at(ua, 1, 1));
    fuel(uc, lookout);
    uc.order(lookout, mk(OrderKind::Sentry));
    const ObjectId hidden = uc.planet(ua, {9, 9});
    uc.colony(hidden, kB);
    const ObjectId storm = uc.object(ua, ObjectKind::Storm, {9, 9});
    uc.s.galaxy.object(storm).abilities = {mvtest::ab(AbilityKind::SectorSightObscuration, 5)};
    REQUIRE_FALSE(sight::canSeePlanet(uc.rules(), uc.s, kA, hidden));
    uc.move();
    CHECK(uc.v(lookout).orders.size() == 1);
    (void)ab;
}

TEST_CASE("movement: attack pursues a moving target and stays until it is gone") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId target = w.spawn(w.ship(kB, "Prey", 2), at(a, 6, 0));      // steps on day 16
    const VehicleId hunter = w.spawn(w.ship(kA, "Hunter", 4), at(a, 0, 0));    // on days 8, 16 and 23
    fuel(w, target);
    fuel(w, hunter);
    w.order(target, moveTo(a, 12, 0));
    w.order(hunter, mk(OrderKind::Attack, {}, {}, target));
    CombatSpy spy;
    spy.fight = hostilesMeet;
    for (int turn = 0; turn < 5 && spy.fought.empty(); ++turn) w.move(spy.hooks());
    REQUIRE_FALSE(spy.fought.empty());
    CHECK(spy.fought[0].second == at(a, 9, 0));
    // The pursuit goes on while the target lives (spec 03 §8): it follows the
    // target to its next sector and fights again there.
    CHECK(w.v(hunter).location == w.v(target).location);
    REQUIRE(w.v(hunter).orders.size() == 1);
    CHECK(w.v(hunter).orders.front().kind == OrderKind::Attack);

    // There is no visibility test: a cloaked target is still pursued (spec 03 §8).
    World l;
    const SystemId la = l.system("A");
    const VehicleId ghost = l.spawn(l.ship(kB, "Ghost", 1, {"Mv Cloak"}), at(la, 6, 0));
    l.v(ghost).status = VehicleStatus::Cloaked;
    const VehicleId seeker = l.spawn(l.ship(kA, "Seeker", 3), at(la, 0, 0));
    fuel(l, seeker);
    l.order(seeker, mk(OrderKind::Attack, {}, {}, ghost));
    l.move();
    CHECK(l.v(seeker).location == at(la, 2, 0));
    CHECK(l.v(seeker).orders.size() == 1);
    // A target that is gone ends the pursuit after the day (spec 03 §6.3 step 7).
    l.v(ghost).count = 0;
    l.s.removeDeadVehicles();
    l.move();
    CHECK(l.v(seeker).orders.empty());
    CHECK(l.v(seeker).location == at(la, 2, 0));
    // So does a target that became the attacker's own.
    const VehicleId turncoat = l.spawn(l.ship(kB, "Turncoat", 1), at(la, 12, 12));
    l.order(seeker, mk(OrderKind::Attack, {}, {}, turncoat));
    l.v(turncoat).owner = kA;
    l.move();
    CHECK(l.v(seeker).orders.empty());

    // A cloaked ship stays cloaked at its target: only drones decloak to attack (spec 03 §8).
    const VehicleId sneaky = l.spawn(l.ship(kA, "Sneaky", 3, {"Mv Cloak"}), at(la, 0, 0));
    const VehicleId plain = l.spawn(l.ship(kB, "Plain", 1), at(la, 3, 3));
    fuel(l, sneaky);
    l.v(sneaky).status = VehicleStatus::Cloaked;
    l.order(sneaky, mk(OrderKind::Attack, {}, {}, plain));
    l.move();
    l.move();
    CHECK(l.v(sneaky).location == at(la, 3, 3));
    CHECK(l.v(sneaky).status == VehicleStatus::Cloaked);
    CHECK(l.v(sneaky).orders.size() == 1);
    // A group with no drone does not attack at its target: it waits there and
    // spends neither movement nor supply (spec 03 §8, §19 Q69).
    const int64_t supply = l.v(sneaky).supply;
    l.move();
    CHECK(l.v(sneaky).supply == supply);
    CHECK(l.v(sneaky).location == at(la, 3, 3));
    CHECK(l.v(sneaky).orders.size() == 1);
}

TEST_CASE("movement: repeat orders cycle; a repeating list that never moves idles") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId patrol = w.spawn(w.ship(kA, "Patrol", 4), at(a, 0, 6));  // 3 steps a turn
    fuel(w, patrol);
    w.order(patrol, moveTo(a, 3, 6), true);
    w.order(patrol, moveTo(a, 0, 6), true);
    w.move();
    CHECK(w.v(patrol).location == at(a, 3, 6));
    REQUIRE(w.v(patrol).orders.size() == 2);
    CHECK(w.v(patrol).orders.front() == moveTo(a, 0, 6));
    w.move();
    CHECK(w.v(patrol).location == at(a, 0, 6));
    w.move();
    CHECK(w.v(patrol).location == at(a, 3, 6));

    const VehicleId idle = w.spawn(w.ship(kA, "Idle", 3), at(a, 9, 9));
    w.order(idle, moveTo(a, 9, 9), true);
    w.move();
    CHECK(w.v(idle).orders.size() == 1);
    CHECK(w.v(idle).location == at(a, 9, 9));
}

TEST_CASE("movement: unreachable destinations, immobile vehicles and waypoints") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    const VehicleId ship = w.spawn(w.ship(kA, "Lost", 3), at(a, 0, 0));
    w.order(ship, moveTo(b, 3, 3));
    w.move();
    CHECK(w.v(ship).orders.empty());
    CHECK(w.v(ship).location == at(a, 0, 0));
    CHECK(w.logged(kA, "No known route"));

    const VehicleId base = w.spawn(w.design(kA, "Post", "Test Station", {"Test Bridge"}), at(a, 5, 5));
    w.order(base, moveTo(a, 6, 6));
    w.move();
    CHECK(w.v(base).orders.empty());
    CHECK(w.v(base).location == at(a, 5, 5));

    w.s.empire(kA).waypoints[2] = Waypoint{"Rally", at(a, 2, 2), true};
    w.order(ship, mk(OrderKind::MoveToWaypoint, {}, {}, {}, {}, 2));
    w.order(ship, mk(OrderKind::MoveToWaypoint, {}, {}, {}, {}, 3));
    w.move();
    CHECK(w.v(ship).location == at(a, 2, 2));
    CHECK(w.v(ship).orders.empty());  // slot 3 is not set: dropped

    // New vehicles can get an automatic Move To waypoint.
    const Rules& r = w.rules();
    Vehicle& fresh = movement::spawnVehicle(r, w.s, kA, w.s.design(w.v(ship).design).id, at(a, 0, 0), 2);
    REQUIRE(fresh.orders.size() == 1);
    CHECK(fresh.orders[0].location == at(a, 2, 2));
}

// ---- Cargo and units --------------------------------------------------------------------------------

TEST_CASE("movement: load and drop cargo orders") {
    World w;
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {2, 6});
    const ObjectId outpost = w.planet(a, {8, 6});
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    w.colony(home, kA, 1000).cargo.units.push_back({fighter, 4});
    w.colony(outpost, kA, 100);

    const VehicleId hauler = w.spawn(w.ship(kA, "Hauler", 3, {"Test Cargo Bay"}), at(a, 2, 6));
    fuel(w, hauler);
    // Given as a player gives them: the Drop becomes Move To plus Drop (spec 03 §8).
    w.give(hauler, {mk(OrderKind::LoadCargo, {}, {}, {}, {}, -1), mk(OrderKind::DropCargo, at(a, 8, 6), {}, {}, {}, -1)});
    REQUIRE(w.v(hauler).orders.size() == 3);
    w.move();
    CHECK(w.v(hauler).cargo.totalPopulation() == 10);  // 50 kT at 5 kT per million
    CHECK(w.s.colony(home)->totalPopulation() == 990);
    // Speed 3 acts on days 11 and 21; the load chains into the first step (spec 03 §6.3).
    CHECK(w.v(hauler).location == at(a, 4, 6));
    w.move();
    w.move();  // the step that arrives and the drop come in one action
    CHECK(w.v(hauler).location == at(a, 8, 6));
    CHECK(w.v(hauler).cargo.empty());
    CHECK(w.s.colony(outpost)->totalPopulation() == 110);
    CHECK(w.v(hauler).orders.empty());

    const VehicleId carrier = w.spawn(w.ship(kA, "Carrier", 3, {"Mv Fighter Bay"}), at(a, 2, 6));
    fuel(w, carrier);
    w.give(carrier, {mk(OrderKind::LoadCargo, {}, {}, {}, fighter, 3), mk(OrderKind::DropCargo, at(a, 8, 6), {}, {}, fighter, -1)});
    w.move();
    CHECK(w.s.colony(home)->cargo.unitCount(fighter) == 1);
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 3);
    w.move();
    w.move();
    CHECK(w.s.colony(outpost)->cargo.unitCount(fighter) == 3);

    // Troops dropped on an enemy planet land there to fight.
    const DesignId troop = w.design(kA, "Trooper", "Test Troop Hull", {"Test Troop Rifle"});
    const ObjectId enemy = w.planet(a, {10, 6});
    w.colony(enemy, kB, 500);
    const VehicleId lander = w.spawn(w.ship(kA, "Lander", 3, {"Test Troop Bay"}), at(a, 10, 6));
    w.v(lander).cargo.units.push_back({troop, 2});
    w.order(lander, mk(OrderKind::DropCargo, {}, {}, {}, troop, -1));
    w.move();
    // Landed troops are kept apart from the colony's cargo and fight for the lander's owner (spec 04 §13).
    CHECK(w.s.colony(enemy)->landedTroops == std::vector<UnitStack>{{troop, 2}});
    CHECK(w.s.colony(enemy)->invader == kA);
    CHECK(w.v(lander).cargo.unitCount(troop) == 0);

    // Loading is always done, even when nothing loads; a drop with nowhere to go fails (spec 03 §8).
    w.order(carrier, mk(OrderKind::LoadCargo, {}, {}, {}, fighter, -1));
    w.move();
    CHECK(w.v(carrier).orders.empty());
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 3);  // took them back from the outpost
    w.order(carrier, mk(OrderKind::LoadCargo, {}, {}, {}, fighter, -1));
    w.order(carrier, moveTo(a, 8, 7));
    w.move();
    CHECK_FALSE(w.logged(kA, "Load Cargo order cancelled"));
    CHECK(w.v(carrier).location == at(a, 8, 7));
    w.order(carrier, mk(OrderKind::DropCargo, {}, {}, {}, fighter, -1));
    w.move();
    CHECK(w.logged(kA, "Drop Cargo order cancelled"));
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 3);
}

TEST_CASE("movement: launching and recovering units") {
    World w;
    const SystemId a = w.system("A");
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const VehicleId carrier = w.spawn(w.ship(kA, "Carrier", 3, {"Mv Fighter Bay"}), at(a, 5, 5));
    w.v(carrier).cargo.units.push_back({fighter, 5});
    w.order(carrier, mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, -1));
    w.move();
    auto group = [&]() -> Vehicle* {
        for (Vehicle& v : w.s.vehicles)
            if (v.design == fighter && v.count > 0) return &v;
        return nullptr;
    };
    REQUIRE(group());
    CHECK(group()->count == 3);  // Val 2 = 3 per game turn
    CHECK(group()->location == at(a, 5, 5));
    CHECK(group()->supply == 3 * 12);  // a group's supply is every unit's
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 2);
    group()->supply = 1;
    w.order(carrier, mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, -1));
    w.move();
    CHECK(group()->count == 5);  // joins the same group
    CHECK(group()->supply == 5 * 12);  // launching refills the whole group
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 0);
    // Recover Units names a unit kind: every design of each own group of it in the
    // sector; no per-turn limit, cargo space is the only limit (spec 03 §8, §12).
    w.order(carrier, mk(OrderKind::RecoverUnits, {}, {}, {}, fighter, 4));
    w.move();
    CHECK(group() == nullptr);
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 5);
    // Launched units join the last group of their kind in the sector's object
    // order, and only that group is refilled (spec 03 §12, confirmed: binary).
    const VehicleId first = w.spawn(fighter, at(a, 5, 5));
    const VehicleId second = w.spawn(fighter, at(a, 5, 5));
    REQUIRE(w.v(second).slot > w.v(first).slot);
    w.v(first).supply = 1;
    w.v(second).supply = 1;
    w.order(carrier, mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, 2));
    w.move();
    CHECK(w.v(first).count == 1);
    CHECK(w.v(first).supply == 1);
    CHECK(w.v(second).count == 3);
    CHECK(w.v(second).supply == 3 * 12);
    // The Launch/Recover window may name one group and one design of it (inferred).
    w.order(carrier, mk(OrderKind::RecoverUnits, {}, {}, second, fighter, 2));
    w.move();
    CHECK(w.v(second).count == 1);
    CHECK(w.v(first).count == 1);
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 5);

    // Mines: capped per player per sector; never recovered.
    const DesignId mine = w.design(kA, "Mine", "Mv Mine Hull", {"Test Warhead"});
    const VehicleId field = w.spawn(mine, at(a, 7, 7));
    w.v(field).count = 98;
    const VehicleId layer = w.spawn(w.ship(kA, "Layer", 3, {"Mv Mine Layer"}), at(a, 7, 7));
    w.v(layer).cargo.units.push_back({mine, 10});
    w.order(layer, mk(OrderKind::LaunchUnits, {}, {}, {}, mine, -1));
    w.move();
    CHECK(w.v(field).count == 100);
    CHECK(w.v(layer).cargo.unitCount(mine) == 8);
    w.order(layer, mk(OrderKind::RecoverUnits, {}, {}, {}, mine, -1));
    w.move();
    CHECK(w.v(layer).orders.empty());  // always done, even when nothing moves
    CHECK(w.v(field).count == 100);
    // At the cap a launch is refused outright.
    w.order(layer, mk(OrderKind::LaunchUnits, {}, {}, {}, mine, -1));
    w.move();
    CHECK(w.v(layer).cargo.unitCount(mine) == 8);
    CHECK(w.v(field).count == 100);

    // A drone launch takes no target and adds no order, every drone is its own
    // group; troops never go into space (spec 03 §12, confirmed: binary).
    const DesignId drone = w.design(kA, "Drone", "Test Drone Hull", {"Mv Engine", "Test Warhead", "Mv Drone Tank"});
    const DesignId troop = w.design(kA, "Troop", "Test Troop Hull", {"Test Troop Rifle"});
    const VehicleId rack = w.spawn(w.ship(kA, "Rack", 3, {"Mv Drone Rack"}), at(a, 9, 9));
    w.v(rack).cargo.units = {{drone, 2}, {troop, 2}};
    w.order(rack, mk(OrderKind::LaunchUnits, {}, {}, {}, drone, -1));
    w.order(rack, mk(OrderKind::LaunchUnits, {}, {}, {}, troop, -1));
    w.move();
    CHECK(w.v(rack).cargo.unitCount(drone) == 0);
    CHECK(w.v(rack).cargo.unitCount(troop) == 2);
    int drones = 0;
    for (const Vehicle& v : w.s.vehicles)
        if (v.design == drone) {
            ++drones;
            CHECK(v.count == 1);
            CHECK_FALSE(v.targetVehicle.valid());
            CHECK(v.orders.empty());
        }
    CHECK(drones == 2);

    // A turn-based launch gives the new group its full movement at once (spec 03 §12).
    World tb;
    const SystemId ta = tb.system("A");
    tb.s.options.simultaneous = false;
    const DesignId quick = tb.design(kA, "Quick", "Test Fighter Hull", {"Mv Fighter Engine", "Mv Fighter Engine", "Mv Fighter Tank"});
    const VehicleId deck = tb.spawn(tb.ship(kA, "Deck", 3, {"Mv Fighter Bay"}), at(ta, 5, 5));
    tb.v(deck).cargo.units.push_back({quick, 2});
    tb.order(deck, mk(OrderKind::LaunchUnits, {}, {}, {}, quick, -1));
    TurnContext ctx{tb.rules(), tb.s, {}, {}, {}};
    movement::startTurn(ctx, kA);
    movement::runLive(ctx, movement::LiveMove{kA});
    const Vehicle* launched = nullptr;
    for (const Vehicle& v : tb.s.vehicles)
        if (v.design == quick) launched = &v;
    REQUIRE(launched);
    CHECK(launched->movement == 2);
}

TEST_CASE("movement: drones move only by orders; an Attack order pursues its target") {
    World w;
    const SystemId a = w.system("A");
    const DesignId droneDesign = w.design(kA, "Drone", "Test Drone Hull", {"Mv Engine", "Mv Engine", "Mv Engine", "Test Warhead", "Mv Drone Tank"});
    const VehicleId prey = w.spawn(w.ship(kB, "Prey", 1), at(a, 6, 0));
    const VehicleId idle = w.spawn(droneDesign, at(a, 0, 3));
    w.v(idle).targetVehicle = prey;  // a battle target is not an order
    const VehicleId drone = w.spawn(droneDesign, at(a, 0, 0));
    w.order(drone, mk(OrderKind::Attack, {}, {}, prey));
    CombatSpy spy;
    spy.fight = hostilesMeet;
    w.move(spy.hooks());
    CHECK(w.v(drone).location == at(a, 2, 0));  // speed 3: days 11 and 21
    CHECK(w.v(idle).location == at(a, 0, 3));
    w.move(spy.hooks());
    w.move(spy.hooks());
    CHECK(w.v(drone).location == at(a, 6, 0));
    REQUIRE(spy.fought.size() == 1);
    CHECK(spy.fought[0].second == at(a, 6, 0));
    // At its target a cloaked drone decloaks and pays one move's supply; the
    // order stays (spec 03 §8). A battle reads the drone's target from that
    // order; nothing is stored (spec 03 §19 Q68).
    w.v(drone).status = VehicleStatus::Cloaked;
    const int64_t before = w.v(drone).supply;
    w.move(spy.hooks());
    CHECK(w.v(drone).status == VehicleStatus::Normal);
    CHECK_FALSE(w.v(drone).targetVehicle.valid());
    CHECK(w.v(drone).supply < before);
    REQUIRE(w.v(drone).orders.size() == 1);
    CHECK(w.v(drone).orders.front().vehicle == prey);

    // Out of supply after a step, a drone is destroyed at once (spec 03 §12).
    const VehicleId thirsty = w.spawn(droneDesign, at(a, 0, 9));
    w.v(thirsty).supply = movement::moveSupplyCost(w.rules(), w.s, w.v(thirsty));
    w.order(thirsty, moveTo(a, 5, 9));
    w.move(spy.hooks());
    CHECK(w.s.vehicle(thirsty) == nullptr);
    CHECK(w.logged(kA, "Ran out of supplies"));

    // A drone given a warp point as target gets Move To plus Warp (spec 03 §8).
    World g;
    const SystemId ga = g.system("A"), gb = g.system("B", 10, 0);
    const auto [wa, wb] = g.link(ga, {6, 0}, gb, {0, 0});
    g.exploreAll(kA);
    const DesignId dd = g.design(kA, "Drone", "Test Drone Hull", {"Mv Engine", "Test Warhead", "Mv Drone Tank"});
    const VehicleId d = g.spawn(dd, at(ga, 0, 0));
    g.give(d, {mk(OrderKind::Attack, {}, wa)});
    REQUIRE(g.v(d).orders.size() == 2);
    CHECK(g.v(d).orders[0] == moveTo(ga, 6, 0));
    CHECK(g.v(d).orders[1].kind == OrderKind::Warp);
    CHECK(g.v(d).orders[1].object == wa);
    (void)wb;
}

TEST_CASE("movement: fighters cannot use warp points") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    const auto [wa, wb] = w.link(a, {7, 6}, b, {0, 6});
    w.exploreAll(kA);
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Mv Fighter Engine", "Mv Fighter Engine", "Mv Fighter Engine", "Mv Fighter Tank"});
    const VehicleId group = w.spawn(fighter, at(a, 6, 6));
    // A Move To into another system travels to the warp point and fails there (spec 03 §6.2).
    w.order(group, moveTo(b, 1, 6));
    w.move();
    CHECK(w.v(group).orders.empty());
    CHECK(w.v(group).location == at(a, 7, 6));
    CHECK(w.logged(kA, "Fighters cannot use warp points"));
    // A Warp order fails at once.
    w.s.empire(kA).log.clear();
    w.order(group, mk(OrderKind::Warp, {}, wa));
    w.move();
    CHECK(w.v(group).orders.empty());
    CHECK(w.v(group).location == at(a, 7, 6));
    CHECK(w.logged(kA, "Fighters cannot use warp points"));
    (void)wb;
}

TEST_CASE("movement: units use supply every turn; drones are lost without it, fighters slow to 1") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const DesignId drone = w.design(kA, "Drone", "Test Drone Hull", {"Mv Engine", "Test Warhead", "Mv Drone Tank"});
    const VehicleId f = w.spawn(fighter, at(a, 1, 1));
    const VehicleId d = w.spawn(drone, at(a, 2, 2));
    w.v(f).count = 4;
    CHECK(vehicleSupplyCapacity(r, w.s, w.v(f)) == 4 * 12);
    w.v(f).supply = 48;
    CHECK(vehicleMaxMovement(r, w.s, w.v(f)) == 1);  // the design's speed
    w.upkeep();
    CHECK(w.v(f).supply == 48 - 4 * 5);  // count x the setting, no racial scaling
    CHECK(w.v(d).supply == 100);
    w.upkeep();
    CHECK(w.v(f).supply == 8);
    CHECK(w.s.vehicle(d) == nullptr);
    w.upkeep();
    REQUIRE(w.s.vehicle(f));  // fighters are not lost (spec 03 §12)
    CHECK(w.v(f).supply == 0);
    CHECK(vehicleMaxMovement(r, w.s, w.v(f)) == 1);
    CHECK(w.s.design(fighter).lost == 0);
    CHECK(w.s.design(drone).lost == 1);

    // A depot refuels fighters but never drones.
    const ObjectId depot = w.planet(a, {5, 5});
    w.colony(depot, kA, 1000, {"Test Depot"});
    const VehicleId f2 = w.spawn(fighter, at(a, 5, 5));
    const VehicleId d2 = w.spawn(drone, at(a, 5, 5));
    w.upkeep();
    CHECK(w.v(f2).supply == 12);
    CHECK(w.v(d2).supply == 100);

    // Satellites and mines have no supply at all.
    const DesignId sat = w.design(kA, "Sat", "Test Satellite Hull", {"Test Satellite Gun"});
    const VehicleId s1 = w.spawn(sat, at(a, 7, 7));
    CHECK(w.v(s1).supply == 0);
    CHECK(vehicleSupplyCapacity(r, w.s, w.v(s1)) == 0);
    w.upkeep();
    CHECK(w.s.vehicle(s1));
}

TEST_CASE("movement: cloak and decloak orders") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId ship = w.spawn(w.ship(kA, "Shade", 2, {"Mv Cloak"}), at(a, 1, 1));
    w.v(ship).queue.items.push_back(QueueItem{});
    w.order(ship, mk(OrderKind::Cloak));
    w.move();
    CHECK(w.v(ship).status == VehicleStatus::Cloaked);
    CHECK(w.v(ship).queue.items.empty());
    CHECK(w.v(ship).supply == 100);  // cloaking is free...
    w.upkeep();
    CHECK(w.v(ship).supply == 80);  // ...the parts' supply is paid every end of turn
    w.upkeep();
    CHECK(w.v(ship).supply == 60);
    w.order(ship, mk(OrderKind::Decloak));
    w.move();
    CHECK(w.v(ship).status == VehicleStatus::Normal);
    w.upkeep();
    CHECK(w.v(ship).supply == 60);

    // At 0 supply the cloak drops; a level-1 part cannot cloak at all.
    w.order(ship, mk(OrderKind::Cloak));
    w.move();
    w.v(ship).supply = 10;
    w.upkeep();
    CHECK(w.v(ship).supply == 0);
    CHECK(w.v(ship).status == VehicleStatus::Normal);
    const VehicleId weak = w.spawn(w.design(kA, "Weak", "Test Frigate", {"Test Bridge", "Test Cloak"}), at(a, 1, 1));
    w.order(weak, mk(OrderKind::Cloak));
    w.move();
    CHECK(w.v(weak).status == VehicleStatus::Normal);

    const VehicleId plain = w.spawn(w.ship(kA, "Plain", 2), at(a, 1, 1));
    w.order(plain, mk(OrderKind::Cloak));
    w.move();
    CHECK(w.v(plain).status == VehicleStatus::Normal);
    CHECK(w.logged(kA, "cloaking device"));
}

TEST_CASE("movement: every action gives the acting vehicle exactly 1 movement point, a stopped one too") {
    // Spec 03 §6.3 step 4, §19 Q63 (confirmed: binary).
    World w;
    const SystemId a = w.system("A");
    const VehicleId ship = w.spawn(w.ship(kA, "Dash", 5, {"Mv Energy Cell"}), at(a, 0, 6));
    w.v(ship).supply = 10;  // the first step empties the tank: the maximum falls from 5 to 1
    w.order(ship, mk(OrderKind::UseComponent, {}, {}, {}, {}, 9));
    w.order(ship, moveTo(a, 12, 6));
    w.move();
    // Speed 5 acts on day 7: the use chains into a step that runs the supply
    // out, so the ship keeps the 0 the action left and gains no more day
    // credit. The energy's 4 on the counter still give actions on days 8 to
    // 11, each with 1 movement point again: one step each.
    CHECK(w.v(ship).location == at(a, 5, 6));
    CHECK(w.v(ship).movement == 0);
    CHECK(w.v(ship).supply == 0);
}

TEST_CASE("movement: emergency energy and emergency resupply are one-shot components") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId ship = w.spawn(w.ship(kA, "Sprinter", 2, {"Mv Energy Cell"}), at(a, 0, 6));
    fuel(w, ship);
    w.order(ship, mk(OrderKind::UseComponent, {}, {}, {}, {}, 6));
    w.order(ship, moveTo(a, 12, 6));
    const int64_t supplyBefore = w.v(ship).supply;
    w.move();
    // Speed 2 acts on day 16: the use chains into a step, and the energy's 4 on
    // the day counter give 4 more actions on days 17 to 20 (spec 03 §6.3, §8).
    CHECK(w.v(ship).location == at(a, 5, 6));
    CHECK_FALSE(entryIntact(r, w.s, w.v(ship), 6));
    CHECK(w.v(ship).supply == supplyBefore - 5 * 20);  // the use itself costs nothing
    CHECK(w.v(ship).movement == 2);                    // movement points untouched
    w.move();
    CHECK(w.v(ship).location == at(a, 6, 6));

    // The second use chains into the same action. Nothing checks that the
    // part is still intact: it gives its 60 again, capped at the maximum.
    // Nothing is logged, and the order never fails (spec 03 §8, confirmed: binary).
    const VehicleId tanker = w.spawn(w.ship(kA, "Reserve", 2, {"Mv Spare Tank"}), at(a, 3, 3));
    w.v(tanker).supply = 10;
    w.order(tanker, mk(OrderKind::UseComponent, {}, {}, {}, {}, 6));
    w.order(tanker, mk(OrderKind::UseComponent, {}, {}, {}, {}, 6));
    w.move();
    CHECK(w.v(tanker).supply == 100);
    CHECK_FALSE(entryIntact(r, w.s, w.v(tanker), 6));
    CHECK(w.v(tanker).orders.empty());
    CHECK_FALSE(w.logged(kA, "Reserve"));

    // Emergency resupply is capped at the maximum and does nothing for unlimited supply.
    const VehicleId full = w.spawn(w.ship(kA, "Brimming", 2, {"Mv Spare Tank"}), at(a, 4, 4));
    w.v(full).supply = 90;
    w.order(full, mk(OrderKind::UseComponent, {}, {}, {}, {}, 6));
    w.move();
    CHECK(w.v(full).supply == 100);
}

TEST_CASE("movement: Jettison Cargo destroys exactly what the player moved, at once, and counts the units as lost (spec 03 §8)") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Mv Fighter Engine", "Mv Fighter Tank"});
    const DesignId spare = w.design(kA, "Spare Fighter", "Test Fighter Hull", {"Mv Fighter Engine"});
    const VehicleId carrier = w.spawn(w.ship(kA, "Carrier", 2, {"Test Cargo Bay"}), at(a, 2, 2));
    w.v(carrier).cargo.units = {{fighter, 10}, {spare, 5}};
    w.v(carrier).cargo.population = {{kA, 4}, {kB, 3}};
    w.order(carrier, moveTo(a, 5, 5), true);
    const Vehicle before = w.v(carrier);
    // Five of the first stack and all of the second: the original would take
    // ten from the first (its window's position fault); OpenSE4 takes what
    // was moved. Each race's population is its own entry.
    REQUIRE(apply(r, w.s, kA, cmd::JettisonCargo{carrier, {}, {{kB, 3}}, {{fighter, 5}, {spare, 5}}}).ok);
    CHECK(w.v(carrier).cargo.units == std::vector<UnitStack>{{fighter, 5}});
    CHECK(w.v(carrier).cargo.population == std::vector<PopulationGroup>{{kA, 4}});
    CHECK(w.s.design(fighter).lost == 5);
    CHECK(w.s.design(spare).lost == 5);
    // Not an order: no movement or supply, the list and Repeat untouched, no log.
    CHECK(w.v(carrier).orders == before.orders);
    CHECK(w.v(carrier).repeatOrders);
    CHECK(w.v(carrier).supply == before.supply);
    CHECK(w.v(carrier).movement == before.movement);
    CHECK(w.s.empire(kA).log.empty());
    // More than is aboard, nothing at all, or a race not aboard: refused, nothing changes.
    CHECK_FALSE(apply(r, w.s, kA, cmd::JettisonCargo{carrier, {}, {}, {{fighter, 6}}}).ok);
    CHECK_FALSE(apply(r, w.s, kA, cmd::JettisonCargo{carrier, {}, {}, {{fighter, 3}, {fighter, 3}}}).ok);
    CHECK_FALSE(apply(r, w.s, kA, cmd::JettisonCargo{carrier, {}, {{kB, 1}}, {}}).ok);
    CHECK_FALSE(apply(r, w.s, kA, cmd::JettisonCargo{carrier, {}, {}, {}}).ok);
    CHECK_FALSE(apply(r, w.s, kB, cmd::JettisonCargo{carrier, {}, {{kA, 1}}, {}}).ok);
    CHECK(w.v(carrier).cargo.units == std::vector<UnitStack>{{fighter, 5}});
    // Being cloaked or in a fleet makes no difference; being mothballed does.
    w.v(carrier).status = VehicleStatus::Cloaked;
    CHECK(apply(r, w.s, kA, cmd::JettisonCargo{carrier, {}, {{kA, 1}}, {}}).ok);
    CHECK(w.v(carrier).cargo.totalPopulation() == 3);
    w.v(carrier).status = VehicleStatus::Mothballed;
    CHECK_FALSE(apply(r, w.s, kA, cmd::JettisonCargo{carrier, {}, {{kA, 1}}, {}}).ok);
    w.v(carrier).status = VehicleStatus::Normal;
    // A colony jettisons its stored cargo; its own population is not cargo.
    const ObjectId home = w.planet(a, {6, 6});
    Colony& col = w.colony(home, kA, 1000);
    col.cargo.units = {{fighter, 2}};
    CHECK_FALSE(apply(r, w.s, kA, cmd::JettisonCargo{{}, home, {{kA, 10}}, {}}).ok);
    REQUIRE(apply(r, w.s, kA, cmd::JettisonCargo{{}, home, {}, {{fighter, 2}}}).ok);
    CHECK(w.s.colony(home)->cargo.empty());
    CHECK(w.s.colony(home)->totalPopulation() == 1000);
    CHECK(w.s.design(fighter).lost == 7);
    // Turn-based games carry it out at once too.
    w.s.options.simultaneous = false;
    CHECK(apply(r, w.s, kA, cmd::JettisonCargo{carrier, {}, {}, {{fighter, 5}}}).ok);
    CHECK(w.v(carrier).cargo.units.empty());
}

TEST_CASE("movement: Use Component is used by the group's first member only, with no intact or mothball check (spec 03 §8)") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    // A fleet: the first member at the fleet's location in object order uses
    // the part at the recorded position, whoever the order was given to.
    const VehicleId first = w.spawn(w.ship(kA, "First", 2, {"Mv Spare Tank"}), at(a, 2, 2));
    const VehicleId second = w.spawn(w.ship(kA, "Second", 2, {"Mv Spare Tank"}), at(a, 2, 2));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {second, first}}).ok);
    w.v(first).supply = w.v(second).supply = 10;
    Order use = mk(OrderKind::UseComponent, {}, {}, {}, {}, 6);
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{second, {}, {use}, false}).ok);
    CHECK(w.v(first).orders == std::vector<Order>{use});  // a simultaneous game appends it to each list
    w.move();
    CHECK(w.v(first).supply == 70);
    CHECK_FALSE(entryIntact(r, w.s, w.v(first), 6));
    CHECK(w.v(second).supply == 10);
    CHECK(entryIntact(r, w.s, w.v(second), 6));
    CHECK(w.v(first).orders.empty());
    CHECK(w.v(second).orders.empty());

    // A mothballed vehicle uses its part too: the destroyed-on-use part goes,
    // but supply does not come to a mothballed vehicle, and no movement to one
    // whose maximum is 0. The order completes.
    const VehicleId laidUp = w.spawn(w.ship(kA, "Laid Up", 2, {"Mv Spare Tank", "Mv Energy Cell"}), at(a, 5, 5));
    w.v(laidUp).status = VehicleStatus::Mothballed;
    w.v(laidUp).supply = 0;
    w.order(laidUp, mk(OrderKind::UseComponent, {}, {}, {}, {}, 6));
    w.order(laidUp, mk(OrderKind::UseComponent, {}, {}, {}, {}, 7));
    w.move();
    CHECK(w.v(laidUp).orders.empty());
    CHECK(w.v(laidUp).supply == 0);
    CHECK_FALSE(entryIntact(r, w.s, w.v(laidUp), 6));
    CHECK_FALSE(entryIntact(r, w.s, w.v(laidUp), 7));
    CHECK(w.v(laidUp).location == at(a, 5, 5));
    // A position past the design's parts gives nothing and completes too.
    const VehicleId odd = w.spawn(w.ship(kA, "Odd", 2), at(a, 6, 6));
    w.order(odd, mk(OrderKind::UseComponent, {}, {}, {}, {}, 40));
    w.move();
    CHECK(w.v(odd).orders.empty());
}

TEST_CASE("movement: Use Facility runs on day 1 in a colony's list and completes with no effect (spec 03 §8)") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {6, 6});
    w.colony(home, kA, 1000, {"Test Mine"});
    const Resources before = w.s.empire(kA).stockpile;
    Order use = mk(OrderKind::UseFacility, {}, {}, {}, {}, 3);  // no facility at that position: no matter
    cmd::SetOrders c;
    c.planet = home;
    c.orders = {use};
    REQUIRE(apply(r, w.s, kA, c).ok);
    CHECK(w.s.colony(home)->orders == std::vector<Order>{use});  // a simultaneous game keeps it for the movement phase
    w.move();
    CHECK(w.s.colony(home)->orders.empty());
    CHECK(w.s.empire(kA).stockpile == before);
    CHECK(w.s.empire(kA).log.empty());
}

TEST_CASE("movement: self-destruct: ships and bases need the ability, satellites, mines and drones nothing, fighters never") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    // Use Component never reaches Self-Destruct: a part with neither emergency
    // ability gives nothing, and the order completes (spec 03 §8, confirmed: binary).
    const DesignId design = w.ship(kA, "Bomb", 2, {"Test Self Destruct"});
    const VehicleId bomb = w.spawn(design, at(a, 2, 2));
    w.order(bomb, mk(OrderKind::UseComponent, {}, {}, {}, {}, 6));
    w.move();
    REQUIRE(w.s.vehicle(bomb) != nullptr);
    CHECK(w.v(bomb).orders.empty());
    CHECK(w.s.design(design).lost == 0);

    // The Self-Destruct order (spec 03 §8, §15).
    const VehicleId second = w.spawn(design, at(a, 2, 2));
    const VehicleId plain = w.spawn(w.ship(kA, "Plain", 2), at(a, 3, 3));
    const VehicleId sat = w.spawn(w.design(kA, "Sat", "Test Satellite Hull", {"Test Satellite Gun"}), at(a, 4, 4));
    const VehicleId mine = w.spawn(w.design(kA, "Mine", "Mv Mine Hull", {"Test Warhead"}), at(a, 5, 5));
    const VehicleId drone = w.spawn(w.design(kA, "Drone", "Test Drone Hull", {"Mv Engine", "Mv Engine", "Test Warhead", "Mv Drone Tank"}), at(a, 6, 6));
    const VehicleId fighter = w.spawn(w.design(kA, "Fighter", "Test Fighter Hull", {"Mv Fighter Engine", "Mv Fighter Engine", "Mv Fighter Tank"}), at(a, 7, 7));
    CHECK(movement::canSelfDestruct(r, w.s, w.v(second)));
    CHECK_FALSE(movement::canSelfDestruct(r, w.s, w.v(plain)));
    CHECK(movement::canSelfDestruct(r, w.s, w.v(sat)));
    CHECK(movement::canSelfDestruct(r, w.s, w.v(mine)));
    CHECK(movement::canSelfDestruct(r, w.s, w.v(drone)));
    CHECK_FALSE(movement::canSelfDestruct(r, w.s, w.v(fighter)));
    for (VehicleId id : {second, plain, sat, mine, drone, fighter}) w.order(id, mk(OrderKind::SelfDestruct));
    w.move();
    CHECK(w.s.vehicle(second) == nullptr);
    CHECK(w.s.vehicle(sat) == nullptr);   // satellites and mines act on day 1
    CHECK(w.s.vehicle(mine) == nullptr);
    CHECK(w.s.vehicle(drone) == nullptr);
    REQUIRE(w.s.vehicle(plain));
    REQUIRE(w.s.vehicle(fighter));
    CHECK(w.v(plain).orders.empty());   // failed
    CHECK(w.v(fighter).orders.empty());
    // A mothballed ship has no abilities, so no self-destruct.
    const VehicleId laidUp = w.spawn(design, at(a, 8, 8));
    w.v(laidUp).status = VehicleStatus::Mothballed;
    CHECK_FALSE(movement::canSelfDestruct(r, w.s, w.v(laidUp)));
}

TEST_CASE("movement: a fleet's orders are copies in its members' lists, carried out position by position") {
    // Spec 03 §8, §9, §19 Q65 (confirmed: binary): joining clears a vehicle's
    // list and gives it none of the fleet's orders, only those given later;
    // the first member in object order with orders acts for the members at
    // the fleet's location, and completing an order removes the head of each
    // of their lists, whichever order that is.
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId lead = w.spawn(w.ship(kA, "Lead", 4, {"Test Quantum Reactor"}), at(a, 0, 6));
    const VehicleId mate = w.spawn(w.ship(kA, "Mate", 4, {"Test Quantum Reactor"}), at(a, 0, 6));
    const VehicleId late = w.spawn(w.ship(kA, "Late", 4, {"Test Quantum Reactor"}), at(a, 0, 6));
    w.order(lead, moveTo(a, 0, 0));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {lead, mate}}).ok);
    const FleetId fid = w.s.fleets.back().id;
    CHECK(w.v(lead).orders.empty());  // joining clears the list
    CHECK(w.s.fleet(fid)->location == at(a, 0, 6));
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{{}, fid, {moveTo(a, 3, 6)}, false}).ok);
    CHECK(w.v(lead).orders == std::vector<Order>{moveTo(a, 3, 6)});
    CHECK(w.v(mate).orders == std::vector<Order>{moveTo(a, 3, 6)});
    w.order(late, moveTo(a, 9, 9));
    REQUIRE(apply(r, w.s, kA, cmd::JoinFleet{fid, late}).ok);
    CHECK(w.v(late).orders.empty());  // none of the fleet's orders
    // An order given to any member is appended to every member's list.
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{late, {}, {moveTo(a, 5, 6)}, false}).ok);
    CHECK(w.v(lead).orders == std::vector<Order>{moveTo(a, 3, 6), moveTo(a, 5, 6)});
    CHECK(w.v(late).orders == std::vector<Order>{moveTo(a, 5, 6)});
    CHECK(fleetOrders(w.s, *w.s.fleet(fid)) == w.v(lead).orders);
    w.move();
    // Speed 4: three steps; the arrival removes the head of every list, so
    // `late` loses its only order, the one still to come for the others.
    for (VehicleId id : {lead, mate, late}) CHECK(w.v(id).location == at(a, 3, 6));
    CHECK(w.v(lead).orders == std::vector<Order>{moveTo(a, 5, 6)});
    CHECK(w.v(late).orders.empty());
    w.move();
    for (VehicleId id : {lead, mate, late}) CHECK(w.v(id).location == at(a, 5, 6));
    CHECK(fleetOrders(w.s, *w.s.fleet(fid)).empty());

    // Clear Orders and Repeat apply to every copy; leaving by Fleet Transfer clears the list.
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{{}, fid, {moveTo(a, 6, 6), moveTo(a, 5, 6)}, true}).ok);
    for (VehicleId id : {lead, mate, late}) CHECK(w.v(id).repeatOrders);
    REQUIRE(apply(r, w.s, kA, cmd::LeaveFleet{mate}).ok);
    CHECK(w.v(mate).orders.empty());
    CHECK_FALSE(w.v(mate).repeatOrders);
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{{}, fid, {}, false}).ok);
    for (VehicleId id : {lead, late}) {
        CHECK(w.v(id).orders.empty());
        CHECK_FALSE(w.v(id).repeatOrders);
    }
    // "Remove All": every member leaves and loses its orders.
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{{}, fid, {moveTo(a, 7, 7)}, false}).ok);
    REQUIRE(apply(r, w.s, kA, cmd::DisbandFleet{fid}).ok);
    for (VehicleId id : {lead, late}) {
        CHECK_FALSE(w.v(id).fleet.valid());
        CHECK(w.v(id).orders.empty());
    }

    // Turn-based: orders given to one member move the fleet at once, through
    // its first member in object order with orders.
    w.s.options.simultaneous = false;
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Again", {late, lead}}).ok);
    const FleetId again = w.s.fleets.back().id;
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{late, {}, {moveTo(a, 5, 9)}, false}).ok);
    TurnContext ctx{r, w.s, {}, {}, {}};
    movement::startTurn(ctx, kA);
    movement::LiveMove m{kA};
    m.vehicles = {late};
    movement::runLive(ctx, m, {});
    CHECK(w.v(lead).location == at(a, 5, 9));
    CHECK(w.v(late).location == at(a, 5, 9));
    CHECK(fleetOrders(w.s, *w.s.fleet(again)).empty());
}

TEST_CASE("movement: a fleet member gains day credit at the fleet's speed wherever it is; a fleet with nobody at its location is disbanded") {
    // Spec 03 §6.3 step 2, §9, §19 Q61 (confirmed: binary).
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId fast = w.spawn(w.ship(kA, "Fast", 6, {"Test Quantum Reactor"}), at(a, 0, 6));
    const VehicleId slow = w.spawn(w.ship(kA, "Slow", 3, {"Test Quantum Reactor"}), at(a, 0, 6));
    const VehicleId third = w.spawn(w.ship(kA, "Third", 6, {"Test Quantum Reactor"}), at(a, 0, 6));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Trio", {fast, slow, third}}).ok);
    const FleetId fid = w.s.fleets.back().id;
    // The location follows whichever member moved last, by any means: here
    // the slow member is moved away alone, as an event might.
    w.v(slow).location = at(a, 9, 9);
    fleetMemberMoved(w.s, w.v(slow));
    CHECK(w.s.fleet(fid)->location == at(a, 9, 9));
    // The fleet's speed is the slow member's alone, the only one at its
    // location; the members elsewhere move only with it, never on their own.
    CHECK(movement::fleetSpeed(r, w.s, *w.s.fleet(fid)) == 3);
    w.order(fast, moveTo(a, 9, 12));
    w.move();
    // Speed 3 acts twice (days 11 and 21): the order of `fast`, away from the
    // fleet's location, is carried out by the slow member alone.
    CHECK(w.v(slow).location == at(a, 9, 11));
    CHECK(w.v(fast).location == at(a, 0, 6));
    CHECK(w.v(third).location == at(a, 0, 6));
    CHECK(w.s.fleet(fid)->location == at(a, 9, 11));
    // The slow member leaves: nobody is at the location, and the fleet is
    // disbanded at once; the others lose their orders.
    REQUIRE(apply(r, w.s, kA, cmd::LeaveFleet{slow}).ok);
    CHECK(w.s.fleet(fid) == nullptr);
    CHECK_FALSE(w.v(fast).fleet.valid());
    CHECK_FALSE(w.v(third).fleet.valid());
    CHECK(w.v(fast).orders.empty());
}

TEST_CASE("movement: a fleet member away from the fleet's location that acts keeps its own list; the members there lose theirs (spec 03 Q73)") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId away = w.spawn(w.ship(kA, "Away", 6, {"Test Quantum Reactor"}), at(a, 0, 6));
    const VehicleId held = w.spawn(w.ship(kA, "Held", 3, {"Test Quantum Reactor"}), at(a, 0, 6));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {away, held}}).ok);
    const FleetId fid = w.s.fleets.back().id;
    // `held` moves to (9,9) and takes the fleet's location with it; `away` is elsewhere.
    w.v(held).location = at(a, 9, 9);
    fleetMemberMoved(w.s, w.v(held));
    w.v(held).orders = {moveTo(a, 1, 1), moveTo(a, 2, 2), moveTo(a, 3, 3)};
    w.v(away).orders = {moveTo(a, 9, 9)};  // the group at (9,9) is already there
    w.move();
    // `away` acts first (lower slot): its Move To completes at once for the
    // members at the location, removing the head of their lists, and chains:
    // its own list never changes, so the same order runs again and again,
    // wiping `held`'s list (confirmed: binary).
    CHECK(w.v(held).orders.empty());
    CHECK(w.v(away).orders == std::vector<Order>{moveTo(a, 9, 9)});
    CHECK(w.v(held).location == at(a, 9, 9));
    CHECK(w.s.fleet(fid)->location == at(a, 9, 9));
}

TEST_CASE("movement: mothballed fleet members are full members: copies of the orders, part of the group, speed 0 (spec 03 Q74)") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    w.object(a, ObjectKind::Star, {6, 6});
    const ObjectId home = w.planet(a, {0, 6});
    w.colony(home, kA, 1000, {"Test Space Yard"});
    const VehicleId active = w.spawn(w.ship(kA, "Active", 3, {"Test Quantum Reactor", "Test Cargo Bay"}), at(a, 0, 6));
    const VehicleId laidUp = w.spawn(w.ship(kA, "Laid Up", 3), at(a, 0, 6));
    w.s.options.simultaneous = false;  // carried out at once (spec 03 §15)
    REQUIRE(apply(r, w.s, kA, cmd::Mothball{laidUp, true}).ok);
    w.s.options.simultaneous = true;
    // A mothballed ship can join a fleet: Fleet Transfer tests no status.
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Odd", {active, laidUp}}).ok);
    const FleetId fid = w.s.fleets.back().id;
    CHECK(fleetGroup(w.s, *w.s.fleet(fid)) == std::vector<VehicleId>{active, laidUp});
    CHECK(movement::fleetSpeed(r, w.s, *w.s.fleet(fid)) == 0);
    // Orders given to the fleet are copied into the mothballed member's list too.
    Order load = mk(OrderKind::LoadCargo, at(a, 0, 6));
    load.amount = -1;
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{{}, fid, {load, moveTo(a, 3, 6)}, false}).ok);
    CHECK(w.v(laidUp).orders.size() == 2);
    CHECK(w.v(active).orders.size() == 2);
    // The fleet acts once a turn, on day 1: the Load Cargo (no movement
    // needed) is carried out and leaves every list; the move waits at speed 0.
    w.move();
    for (VehicleId id : {active, laidUp}) {
        CHECK(w.v(id).location == at(a, 0, 6));
        CHECK(w.v(id).orders == std::vector<Order>{moveTo(a, 3, 6)});
    }
    // Clear Orders reaches it like any member.
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{{}, fid, {}, false}).ok);
    CHECK(w.v(laidUp).orders.empty());
    // A fleet member cannot be mothballed, unmothballed, scrapped or
    // retrofitted: the Scrap window does not list it (spec 03 §15).
    CHECK_FALSE(apply(r, w.s, kA, cmd::Mothball{laidUp, false}).ok);
    CHECK_FALSE(apply(r, w.s, kA, cmd::Mothball{active, true}).ok);
    CHECK_FALSE(apply(r, w.s, kA, cmd::Scrap{active, {}, -1}).ok);
    const DesignId refit = w.ship(kA, "Refit", 4, {"Test Quantum Reactor", "Test Cargo Bay"});
    const CommandResult retrofit = apply(r, w.s, kA, cmd::Retrofit{active, refit});
    CHECK_FALSE(retrofit.ok);
    CHECK(retrofit.error.find("fleet") != std::string::npos);
    // Out of the fleet, the same commands work again (here, a simultaneous
    // game: the action becomes the vehicle's only order).
    REQUIRE(apply(r, w.s, kA, cmd::LeaveFleet{laidUp}).ok);
    w.s.empire(kA).stockpile = {100000, 100000, 100000};
    CHECK(apply(r, w.s, kA, cmd::Mothball{laidUp, false}).ok);
    CHECK(w.v(laidUp).orders == std::vector<Order>{Order{OrderKind::Unmothball, at(a, 0, 6)}});
}

TEST_CASE("movement: a computer player's ad-hoc companion joins alone and keeps its list (spec 03 Q75)") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    w.s.options.simultaneous = false;
    w.s.empire(kB).kind = PlayerKind::Computer;
    const VehicleId actor = w.spawn(w.ship(kB, "Actor", 3, {"Test Quantum Reactor"}), at(a, 0, 6));
    const VehicleId mate = w.spawn(w.ship(kB, "Mate", 3, {"Test Quantum Reactor"}), at(a, 0, 6));
    const VehicleId other = w.spawn(w.ship(kB, "Other", 3, {"Test Quantum Reactor"}), at(a, 0, 6));
    REQUIRE(apply(r, w.s, kB, cmd::CreateFleet{"Pack", {mate, other}}).ok);
    const FleetId fid = w.s.fleets.back().id;
    // The fleet members' lists differ: only `mate`'s head matches the actor's.
    w.v(actor).orders = {moveTo(a, 3, 6)};
    w.v(mate).orders = {moveTo(a, 3, 6)};
    w.v(other).orders = {moveTo(a, 0, 0)};
    TurnContext ctx{r, w.s, {}, {}, {}};
    movement::startTurn(ctx, kB);
    movement::LiveMove m{kB};
    m.vehicles = {actor};
    movement::runLive(ctx, m, {});
    // The actor's group took `mate` alone, not its fleet: `other` stays.
    CHECK(w.v(actor).location == at(a, 3, 6));
    CHECK(w.v(mate).location == at(a, 3, 6));
    CHECK(w.v(other).location == at(a, 0, 6));
    // Only the actor's list changed; the companion keeps its order, which
    // completes at once when it next acts itself.
    CHECK(w.v(actor).orders.empty());
    CHECK(w.v(mate).orders == std::vector<Order>{moveTo(a, 3, 6)});
    CHECK(w.v(other).orders == std::vector<Order>{moveTo(a, 0, 0)});
    // The companion took its fleet's location with it.
    CHECK(w.s.fleet(fid)->location == at(a, 3, 6));
}

TEST_CASE("movement: orders, Clear and Repeat given to an away fleet member go to the members at the location only (spec 03 Q76)") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId x = w.spawn(w.ship(kA, "X", 3), at(a, 0, 6));
    const VehicleId y = w.spawn(w.ship(kA, "Y", 3), at(a, 0, 6));
    const VehicleId z = w.spawn(w.ship(kA, "Z", 3), at(a, 0, 6));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Trio", {x, y, z}}).ok);
    const FleetId fid = w.s.fleets.back().id;
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{{}, fid, {moveTo(a, 1, 1)}, false}).ok);
    w.v(z).location = at(a, 9, 9);  // away; the fleet's location stays (0,6)
    w.v(z).orders = {mk(OrderKind::Sentry)};
    // An order given to `z` is appended to the lists at the location; its own stays.
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{z, {}, {mk(OrderKind::Sentry), moveTo(a, 2, 2)}, false}).ok);
    for (VehicleId id : {x, y}) CHECK(w.v(id).orders == std::vector<Order>{moveTo(a, 1, 1), moveTo(a, 2, 2)});
    CHECK(w.v(z).orders == std::vector<Order>{mk(OrderKind::Sentry)});
    // Repeat sets each list's flag at the location; Clear empties each of them.
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{z, {}, {mk(OrderKind::Sentry)}, true}).ok);
    for (VehicleId id : {x, y}) CHECK(w.v(id).repeatOrders);
    CHECK_FALSE(w.v(z).repeatOrders);
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{z, {}, {}, false}).ok);
    for (VehicleId id : {x, y}) {
        CHECK(w.v(id).orders.empty());
        CHECK_FALSE(w.v(id).repeatOrders);
    }
    CHECK(w.v(z).orders == std::vector<Order>{mk(OrderKind::Sentry)});
}

// ---- Hazards ------------------------------------------------------------------------------------------

TEST_CASE("movement: pulls, drift and destructive centres act on everything once per turn") {
    World w;
    const SystemId b = w.system("B", 5, 0), c = w.system("C", 10, 0), d = w.system("D", 15, 0);
    w.s.galaxy.system(b).abilities.push_back(ab(AbilityKind::SystemDestructiveCenter, 600));
    w.s.galaxy.system(b).abilities.push_back(ab(AbilityKind::SystemDestructiveCenter, 400));  // several add up
    const DesignId doomedDesign = w.ship(kA, "Doomed", 1);
    const VehicleId doomed = w.spawn(doomedDesign, at(b, 6, 6));
    const VehicleId offCentre = w.spawn(doomedDesign, at(b, 6, 7));
    w.s.galaxy.system(c).abilities.push_back(ab(AbilityKind::SystemMovementTowardsCenter, 1));
    w.s.galaxy.system(c).abilities.push_back(ab(AbilityKind::SystemMovementTowardsCenter, 1));
    const VehicleId pulled = w.spawn(w.ship(kA, "Pulled", 1), at(c, 0, 0));
    const VehicleId anchored = w.spawn(w.design(kA, "Post", "Test Station", {"Test Bridge"}), at(c, 0, 1));
    const VehicleId mines = w.spawn(w.design(kB, "Mine", "Mv Mine Hull", {"Test Warhead"}), at(c, 12, 12));
    const VehicleId held = w.spawn(w.ship(kA, "Held", 1), at(c, 12, 0));
    w.v(held).immobileUntil = w.s.turn + 5;
    w.s.galaxy.system(d).abilities.push_back(ab(AbilityKind::SystemMovementRandom, 1));
    const VehicleId drifter = w.spawn(w.ship(kA, "Drifter", 1), at(d, 6, 6));
    const VehicleId drifter2 = w.spawn(w.ship(kA, "Drifter 2", 1), at(d, 6, 6));

    // Movement alone applies none of it.
    w.move();
    CHECK(w.v(pulled).location == at(c, 0, 0));
    REQUIRE(w.s.vehicle(doomed));

    w.hazards();
    CHECK(w.s.vehicle(doomed) == nullptr);
    CHECK(w.s.design(doomedDesign).lost == 1);
    CHECK(w.logged(kA, "Doomed 0001 destroyed"));  // new ships are named "<design> NNNN" (spec 06 §6)
    CHECK(hasMood(w.lastMoods, kA, "Any Ship Lost"));
    CHECK(totalDamage(w.v(offCentre)) == 0);
    // Pull 1 + 1 = 2 king steps toward the centre, for ships, bases and unit groups alike.
    CHECK(w.v(pulled).location == at(c, 2, 2));
    CHECK(w.v(anchored).location == at(c, 2, 3));
    CHECK(w.v(mines).location == at(c, 10, 10));
    CHECK(w.v(held).location == at(c, 10, 2));
    // Drift: one target for everyone, drawn from sectors 0..144.
    CHECK(chebyshev(w.v(drifter).location.sector, Sector{6, 6}) <= 1);
    CHECK(w.v(drifter).location == w.v(drifter2).location);
}

TEST_CASE("movement: the drift target is one of sectors 0 to 144 and every drifter heads for it") {
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        World w;
        const SystemId a = w.system("A"), b = w.system("B", 5, 0);
        for (SystemId sys : {a, b}) w.s.galaxy.system(sys).abilities.push_back(ab(AbilityKind::SystemMovementRandom, 20));
        const VehicleId x = w.spawn(w.ship(kA, "X", 1), at(a, 0, 0));
        const VehicleId y = w.spawn(w.ship(kA, "Y", 1), at(b, 12, 12));
        w.s.rng.reseed(seed);
        w.hazards();
        const Sector t = w.v(x).location.sector;
        CHECK(t == w.v(y).location.sector);  // 20 steps reach any target
        CHECK(t.y * kSystemSize + t.x <= 144);
    }
}

TEST_CASE("movement: a storm may hit a group stepping in, stopping it; it never hurts those that stay") {
    int hits = 0, misses = 0;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        World w;
        const SystemId a = w.system("A");
        const ObjectId storm = w.object(a, ObjectKind::Storm, {3, 6});
        w.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorDamage, 20));
        w.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorDamage, 10));  // they add up
        const VehicleId stays = w.spawn(w.ship(kA, "Stays", 1, {"Mv Armor"}), at(a, 3, 6));
        const VehicleId runner = w.spawn(w.ship(kA, "Runner", 4, {"Mv Armor"}), at(a, 2, 6));  // days 8, 16, 23
        fuel(w, runner);
        w.order(runner, moveTo(a, 3, 6));  // routes step around storms when they can: go into it
        w.order(runner, moveTo(a, 5, 6));
        w.s.rng.reseed(seed);
        w.move();
        CHECK(totalDamage(w.v(stays)) == 0);
        if (totalDamage(w.v(runner)) > 0) {
            ++hits;
            // 30 destroys the armor plate whole: the combat routine for Normal damage (spec 03 §6.2).
            CHECK(totalDamage(w.v(runner)) == 30);
            CHECK(w.v(runner).damage[8] == 30);
            CHECK(w.v(runner).location == at(a, 3, 6));  // stopped
            CHECK(w.v(runner).orders.empty());           // and the order failed
        } else {
            ++misses;
            CHECK(w.v(runner).location == at(a, 5, 6));
        }
    }
    CHECK(hits > 0);
    CHECK(misses > 0);
}

TEST_CASE("movement: a pursuit that meets a storm or mines only stops; its order and list are kept") {
    // Spec 03 §6.4, spec 04 §19.2 Q76 (confirmed: binary). In a simultaneous
    // game every Attack is a pursuit.
    int hits = 0;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        World w;
        const SystemId a = w.system("A");
        const ObjectId storm = w.object(a, ObjectKind::Storm, {3, 6});
        w.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorDamage, 30));
        const VehicleId prey = w.spawn(w.ship(kB, "Prey", 1, {"Mv Armor"}), at(a, 3, 6));
        const VehicleId hunter = w.spawn(w.ship(kA, "Hunter", 4, {"Mv Armor"}), at(a, 2, 6));
        fuel(w, hunter);
        w.order(hunter, mk(OrderKind::Attack, {}, {}, prey));
        w.order(hunter, moveTo(a, 0, 0));
        w.s.rng.reseed(seed);
        w.move();
        CHECK(w.v(hunter).location == at(a, 3, 6));
        if (totalDamage(w.v(hunter)) == 0) continue;
        ++hits;
        REQUIRE(w.v(hunter).orders.size() == 2);   // the storm stopped it; nothing was cleared
        CHECK(w.v(hunter).orders.front().kind == OrderKind::Attack);
        CHECK_FALSE(w.logged(kA, "order cancelled"));
    }
    CHECK(hits > 0);

    // Mines strike a pursuit stepping in: it keeps its orders (with the real combat module).
    World m;
    const SystemId ma = m.system("A");
    const VehicleId field = m.spawn(m.design(kB, "Mine", "Mv Mine Hull", {"Test Warhead"}), at(ma, 1, 6));
    const VehicleId far = m.spawn(m.ship(kB, "Far", 1, {"Mv Armor"}), at(ma, 6, 6));
    const VehicleId tough = m.spawn(m.ship(kA, "Tough", 3, {"Mv Armor", "Mv Armor", "Mv Armor"}), at(ma, 0, 6));
    fuel(m, tough);
    m.order(tough, mk(OrderKind::Attack, {}, {}, far));
    m.move(movement::defaultCombatHooks());
    CHECK(m.s.vehicle(field) == nullptr);   // the mine was used up
    REQUIRE(m.s.vehicle(tough));
    CHECK(totalDamage(m.v(tough)) > 0);
    REQUIRE(m.v(tough).orders.size() == 1);
    CHECK(m.v(tough).orders.front().kind == OrderKind::Attack);
    CHECK(m.logged(kA, "keeps its orders"));
}

TEST_CASE("movement: warp point turbulence hits half the transits and ends the move") {
    int hits = 0, misses = 0;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        World t;
        const SystemId ta = t.system("A"), tb = t.system("B", 5, 0);
        const auto [wa, wb] = t.link(ta, {7, 6}, tb, {0, 6});
        t.s.galaxy.object(wa).abilities.push_back(ab(AbilityKind::WarpPointTurbulence, 20));
        t.s.galaxy.object(wa).abilities.push_back(ab(AbilityKind::WarpPointTurbulence, 10));
        t.s.galaxy.object(wb).abilities.push_back(ab(AbilityKind::WarpPointTurbulence, 100));  // the far end does not count
        t.exploreAll(kA);
        const VehicleId jumper = t.spawn(t.ship(kA, "Jumper", 4, {"Mv Armor"}), at(ta, 7, 6));
        fuel(t, jumper);
        t.order(jumper, moveTo(tb, 2, 6));
        t.s.rng.reseed(seed);
        t.move();
        if (totalDamage(t.v(jumper)) > 0) {
            ++hits;
            CHECK(totalDamage(t.v(jumper)) == 30);
            CHECK(t.v(jumper).location == at(tb, 0, 6));  // arrives, then stops
            CHECK(t.v(jumper).orders.empty());
        } else {
            ++misses;
            CHECK(t.v(jumper).location == at(tb, 2, 6));
        }
    }
    CHECK(hits > 0);
    CHECK(misses > 0);
}

// ---- Colonization -----------------------------------------------------------------------------------

TEST_CASE("movement: founding a colony claims nothing, whatever the Politics option says") {
    // Empire Options, Politics (spec 06 §1.9): on for a new empire, stored
    // and shown but never read (spec 06 §7 Q47, confirmed: binary).
    for (const bool on : {true, false}) {
        World w;
        const Rules& r = w.rules();
        CHECK(w.s.empire(kA).interfaceOptions.autoClaimColonized);
        InterfaceOptions o = w.s.empire(kA).interfaceOptions;
        o.autoClaimColonized = on;
        REQUIRE(apply(r, w.s, kA, cmd::SetInterfaceOptions{o}).ok);
        const SystemId a = w.system("A");
        const SystemId b = w.system("B", 10, 0);
        const ObjectId target = w.planet(b, {5, 6});
        w.s.empire(kA).claimedSystems = {a};
        const VehicleId ship = w.spawn(w.ship(kA, "Settler", 4, {"Test Rock Pod"}), at(b, 5, 6));
        fuel(w, ship);
        w.v(ship).cargo.population.push_back({kA, 2});
        w.order(ship, mk(OrderKind::Colonize, {}, target));
        w.colonize();
        REQUIRE(w.s.colony(target));
        const auto& claimed = w.s.empire(kA).claimedSystems;
        CHECK(std::is_sorted(claimed.begin(), claimed.end()));
        CHECK(std::find(claimed.begin(), claimed.end(), b) == claimed.end());
        CHECK(claimed == std::vector<SystemId>{a});
    }
}

TEST_CASE("commands: an abandoned colony that keeps facilities stays with its owner") {
    // Spec 06 §7 Q47 (confirmed: binary): the people leave and the anger is
    // reset to 25; with facilities left, the planet stays the same empire's
    // colony, empty, and nobody can colonize it.
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId planet = w.planet(a, {5, 6});
    w.colony(planet, kA, 10, {"Mv Trainer", "Mv System Scanner"});
    const std::vector<uint32_t> facilities = w.s.colony(planet)->facilities;
    w.s.colonies[planet.index()]->anger = 70;
    REQUIRE(apply(r, w.s, kA, cmd::AbandonPlanet{planet}).ok);
    REQUIRE(w.s.colony(planet));
    CHECK(w.s.colony(planet)->owner == kA);
    CHECK(w.s.colony(planet)->totalPopulation() == 0);
    CHECK(w.s.colony(planet)->anger == 25);
    CHECK(w.s.colony(planet)->facilities == facilities);
    CHECK(w.s.leftFacilities.empty());
    // Another empire's colony ship cannot settle it.
    const VehicleId ship = w.spawn(w.ship(kB, "Settler", 4, {"Test Rock Pod"}), at(a, 5, 6));
    fuel(w, ship);
    w.v(ship).cargo.population.push_back({kB, 2});
    w.order(ship, mk(OrderKind::Colonize, {}, planet));
    w.colonize();
    REQUIRE(w.s.colony(planet));
    CHECK(w.s.colony(planet)->owner == kA);

    // Scrapped first: a refund now, and with no facility left the colony goes.
    const ObjectId other = w.planet(a, {8, 8});
    w.colony(other, kA, 10, {"Mv Trainer"});
    const Resources before = w.s.empire(kA).stockpile;
    REQUIRE(apply(r, w.s, kA, cmd::Scrap{{}, other, 0}).ok);
    CHECK(w.s.empire(kA).stockpile.total() > before.total());
    REQUIRE(apply(r, w.s, kA, cmd::AbandonPlanet{other}).ok);
    CHECK_FALSE(w.s.colony(other));
    CHECK(w.s.leftFacilities.empty());
    CHECK(validateState(w.s, &r).empty());
}

TEST_CASE("commands: the Empire Options are replaced as a whole and checked") {
    World w;
    const Rules& r = w.rules();
    InterfaceOptions o;
    o.confirmEndTurn = false;
    o.logFilter = uint8_t(LogCategory::Combat) + 1;
    o.facilityMarkers = 0x0801;
    REQUIRE(apply(r, w.s, kA, cmd::SetInterfaceOptions{o}).ok);
    CHECK(w.s.empire(kA).interfaceOptions == o);
    CHECK(w.s.empire(kB).interfaceOptions == InterfaceOptions{});
    InterfaceOptions bad = o;
    bad.logFilter = 99;
    CHECK_FALSE(apply(r, w.s, kA, cmd::SetInterfaceOptions{bad}).ok);
    bad = o;
    bad.facilityMarkers = 0x1000;
    CHECK_FALSE(apply(r, w.s, kA, cmd::SetInterfaceOptions{bad}).ok);
    bad = o;
    bad.queuesShown = 0x10;
    CHECK_FALSE(apply(r, w.s, kA, cmd::SetInterfaceOptions{bad}).ok);
    CHECK(w.s.empire(kA).interfaceOptions == o);
}

TEST_CASE("movement: colony ships load colonists, travel and found a colony") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {2, 6});
    const ObjectId target = w.planet(a, {5, 6});
    w.colony(home, kA, 1000);
    const VehicleId ship = w.spawn(w.ship(kA, "Settler", 4, {"Test Rock Pod"}), at(a, 2, 6));
    fuel(w, ship);
    w.order(ship, mk(OrderKind::Colonize, {}, target));
    w.move();
    CHECK(w.v(ship).location == at(a, 5, 6));
    CHECK(w.v(ship).orders.size() == 1);  // colonizes in phase 4
    CHECK(w.v(ship).cargo.totalPopulation() == 2);
    CHECK(w.s.colony(home)->totalPopulation() == 998);
    const std::string type = ai::colonyTypeAtColonization(r, w.s, kA, target);
    w.colonize();
    const Colony* c = w.s.colony(target);
    REQUIRE(c);
    CHECK(c->owner == kA);
    CHECK(c->colonyType == type);  // chosen at colonization for every empire (spec 05 §7.5)
    CHECK(c->totalPopulation() == 2);
    CHECK(c->foundedTurn == w.s.turn);
    CHECK(w.s.vehicle(ship) == nullptr);
    CHECK(hasMood(w.lastMoods, kA, "Any Planet Colonized"));
    CHECK(w.logged(kA, "colonized"));
    REQUIRE_FALSE(w.s.empire(kA).historyEvents.empty());
    CHECK(w.s.empire(kA).historyEvents.back().text == std::format("Colonized {}", w.s.galaxy.object(target).name));
    CHECK(w.s.empire(kA).historyEvents.back().empire == kA);
    CHECK(w.s.empire(kA).historyEvents.back().location == std::optional<Location>(locationOf(w.s.galaxy, target)));

    // Game options and planet types.
    const ObjectId ice = w.planet(a, {7, 7}, "Ice", "Oxygen");
    const ObjectId smog = w.planet(a, {8, 8}, "Rock", "Methane");
    const VehicleId rock = w.spawn(w.ship(kA, "Rocky", 3, {"Test Rock Pod"}), at(a, 0, 0));
    const VehicleId icy = w.spawn(w.ship(kA, "Icy", 3, {"Test Ice Pod"}), at(a, 0, 0));
    CHECK_FALSE(movement::colonizeProblem(r, w.s, w.v(rock), ice).empty());
    CHECK(movement::colonizeProblem(r, w.s, w.v(icy), ice).empty());
    CHECK(movement::colonizeProblem(r, w.s, w.v(rock), smog).empty());
    w.s.options.onlyBreathable = true;
    CHECK_FALSE(movement::colonizeProblem(r, w.s, w.v(rock), smog).empty());
    w.s.options.onlyBreathable = false;
    w.s.options.onlyHomeType = true;
    CHECK_FALSE(movement::colonizeProblem(r, w.s, w.v(icy), ice).empty());
    w.s.options.onlyHomeType = false;
    CHECK_FALSE(movement::colonizeProblem(r, w.s, w.v(rock), target).empty());  // taken
    CHECK_FALSE(movement::colonizeProblem(r, w.s, w.v(rock), w.object(a, ObjectKind::Asteroids, {9, 9})).empty());

    // The wrong module: the order is dropped during movement.
    w.order(rock, mk(OrderKind::Colonize, {}, ice));
    w.move();
    CHECK(w.v(rock).orders.empty());
    CHECK(w.logged(kA, "cannot colonize"));
}

TEST_CASE("movement: colony ships found colonies during the phases, on an acting day with movement left") {
    // Colonize is carried out like any order (spec 05 §8 step 5, spec 03 §8):
    // a ship that arrives before its last acting day founds the colony that
    // turn, one that arrives on its last acting day founds it next turn.
    World w;
    const SystemId a = w.system("A");
    const ObjectId near = w.planet(a, {3, 6});
    const ObjectId far = w.planet(a, {5, 6});
    const VehicleId early = w.spawn(w.ship(kA, "Early", 4, {"Test Rock Pod"}), at(a, 1, 6));
    const VehicleId late = w.spawn(w.ship(kA, "Late", 4, {"Test Rock Pod"}), at(a, 2, 6));
    REQUIRE(movement::actionDays(vehicleMaxMovement(w.rules(), w.s, w.v(early)), movement::kDayCounterMode) == std::vector<int>{8, 16, 23});
    fuel(w, early);
    fuel(w, late);
    w.v(early).cargo.population.push_back({kA, 1});
    w.v(late).cargo.population.push_back({kA, 1});
    w.order(early, mk(OrderKind::Colonize, {}, near));  // two steps, then colonizes on day 23
    w.order(late, mk(OrderKind::Colonize, {}, far));    // three steps: arrives on day 23
    w.move();
    REQUIRE(w.s.colony(near));
    CHECK(w.s.colony(near)->owner == kA);
    CHECK(w.s.vehicle(early) == nullptr);
    CHECK_FALSE(w.s.colony(far));
    REQUIRE(w.s.vehicle(late));
    CHECK(w.v(late).location == at(a, 5, 6));
    w.move();
    REQUIRE(w.s.colony(far));
    CHECK(w.s.vehicle(late) == nullptr);
}

TEST_CASE("movement: the first colony ship at a planet wins; fleets colonize with a member") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId target = w.planet(a, {5, 5});
    const VehicleId first = w.spawn(w.ship(kA, "First", 3, {"Test Rock Pod"}), at(a, 5, 5));
    const VehicleId second = w.spawn(w.ship(kB, "Second", 3, {"Test Rock Pod"}), at(a, 5, 5));
    w.order(first, mk(OrderKind::Colonize, {}, target));
    w.order(second, mk(OrderKind::Colonize, {}, target));
    w.colonize();
    REQUIRE(w.s.colony(target));
    CHECK(w.s.colony(target)->owner == kA);
    CHECK(w.s.vehicle(first) == nullptr);
    REQUIRE(w.s.vehicle(second));
    CHECK(w.v(second).orders.empty());
    CHECK(w.logged(kB, "colonization failed"));

    const ObjectId other = w.planet(a, {9, 9});
    const VehicleId escort = w.spawn(w.ship(kA, "Escort", 4), at(a, 6, 6));
    const VehicleId settler = w.spawn(w.ship(kA, "Settler", 4, {"Test Rock Pod"}), at(a, 6, 6));
    fuel(w, escort);
    fuel(w, settler);
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Convoy", {escort, settler}}).ok);
    const FleetId convoy = w.s.fleets.back().id;
    fleetGets(w, convoy, {mk(OrderKind::Colonize, {}, other)});
    w.move();
    CHECK(w.v(escort).location == at(a, 9, 9));
    w.colonize();
    REQUIRE(w.s.colony(other));
    CHECK(w.s.vehicle(settler) == nullptr);
    REQUIRE(w.s.vehicle(escort));
    REQUIRE(w.s.fleet(convoy));
    CHECK(fleetList(w, convoy).empty());
}

TEST_CASE("movement: ruins give technology; automatic colonists are added") {
    ruleset::Ruleset rs = buildRuleset();
    rs.settings.set("Automatic Colonization Population", "5");
    for (const char* name : {"Mv Relic Lore", "Mv Relic Craft"}) {
        ruleset::TechArea relic;
        relic.name = name;
        relic.maxLevel = 2;
        relic.levelCost = 1000;
        relic.uniqueArea = 7;
        rs.techAreas.push_back(relic);
    }
    rs.reindex();
    const Rules r{std::move(rs)};
    World w(r);
    const SystemId a = w.system("A");
    const ObjectId ruin = w.planet(a, {4, 4});
    w.s.galaxy.object(ruin).abilities = {ab(AbilityKind::AncientRuins, 2), ab(AbilityKind::AncientRuinsUnique, 7)};
    const VehicleId ship = w.spawn(w.ship(kA, "Digger", 3, {"Test Rock Pod"}), at(a, 4, 4));
    w.v(ship).cargo.population.push_back({kA, 1});
    w.order(ship, mk(OrderKind::Colonize, {}, ruin));
    int before = 0;
    for (int l : w.s.empire(kA).techLevels) before += l;
    w.colonize();
    int after = 0;
    for (int l : w.s.empire(kA).techLevels) after += l;
    CHECK(after == before + 2);
    // Plain ruins come first and are used up; the unique ruins are then neither
    // applied nor removed (spec 03 §3.3, confirmed: binary).
    const auto& unique = w.s.empire(kA).uniqueAreasUnlocked;
    CHECK(std::find(unique.begin(), unique.end(), 7) == unique.end());
    REQUIRE(w.s.galaxy.object(ruin).abilities.size() == 1);
    CHECK(parseAbilityKind(w.s.galaxy.object(ruin).abilities[0].type) == AbilityKind::AncientRuinsUnique);
    REQUIRE(w.s.colony(ruin));
    CHECK(w.s.colony(ruin)->totalPopulation() == 1 + 5);
    CHECK(w.logged(kA, "Ancient ruins"));

    // Unique ruins alone: the unique area is added and every area of that
    // unique id gains a level, requirements unchecked; then they are used up.
    const ObjectId relic = w.planet(a, {6, 6});
    w.s.galaxy.object(relic).abilities = {ab(AbilityKind::AncientRuinsUnique, 7)};
    const VehicleId second = w.spawn(w.ship(kA, "Digger", 3, {"Test Rock Pod"}), at(a, 6, 6));
    w.v(second).cargo.population.push_back({kA, 1});
    w.order(second, mk(OrderKind::Colonize, {}, relic));
    w.colonize();
    CHECK(std::find(unique.begin(), unique.end(), 7) != unique.end());
    CHECK(w.s.galaxy.object(relic).abilities.empty());
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i)
        if (r.data().techAreas[i].uniqueArea == 7) CHECK(w.s.empire(kA).techLevel(ruleset::TechAreaId{i}) == 1);
}

// ---- Upkeep -------------------------------------------------------------------------------------------

TEST_CASE("movement: training facilities raise ship and fleet experience up to their cap") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId camp = w.planet(a, {6, 6});
    w.colony(camp, kA, 1000, {"Mv Trainer", "Mv Fleet Trainer"});
    const VehicleId cadet = w.spawn(w.ship(kA, "Cadet", 1), at(a, 6, 6));
    const VehicleId away = w.spawn(w.ship(kA, "Away", 1), at(a, 1, 1));
    const VehicleId m1 = w.spawn(w.ship(kA, "M1", 1), at(a, 6, 6));
    const VehicleId m2 = w.spawn(w.ship(kA, "M2", 1), at(a, 6, 6));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Drill", {m1, m2}}).ok);
    const FleetId drill = w.s.fleets.back().id;
    for (int expected : {5, 10, 12, 12}) {
        w.upkeep();
        CHECK(w.v(cadet).experience == expected);
    }
    CHECK(w.v(away).experience == 0);
    CHECK(w.s.fleet(drill)->experience == 9);
}

// ---- Stellar manipulation ---------------------------------------------------------------------------

TEST_CASE("movement: stellar manipulation - planets from asteroids and back") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), bare = w.system("Bare", 5, 0);
    w.object(a, ObjectKind::Star, {6, 6});
    const ObjectId rocks = w.object(a, ObjectKind::Asteroids, {2, 2});  // a Small field
    w.s.galaxy.object(rocks).value = {70, 80, 90};
    const size_t objects = w.s.galaxy.objects.size();

    // A visible ship of an empire without a Non-Aggression treaty in the sector prevents it.
    const VehicleId maker = w.spawn(w.ship(kA, "Maker", 3, {"Mv Planet Maker"}), at(a, 2, 2));
    const VehicleId intruder = w.spawn(w.ship(kB, "Intruder", 1), at(a, 2, 2));
    w.order(maker, stellar(StellarAction::CreatePlanet, rocks));
    w.move();
    CHECK(w.logged(kA, "hostile"));
    CHECK(w.s.galaxy.object(rocks).kind == ObjectKind::Asteroids);
    w.v(intruder).count = 0;
    w.s.removeDeadVehicles();

    // The planet has exactly min(Val 1, the field's size): Small; it keeps the values.
    // It is a new object, added while the field holds its slot; then the
    // field is removed (spec 03 §19 Q72).
    const uint32_t fieldSlot = w.s.galaxy.object(rocks).slot;
    w.order(maker, stellar(StellarAction::CreatePlanet, rocks));
    w.move();
    const ObjectId planet = w.objectAt(a, {2, 2}, ObjectKind::Planet);
    REQUIRE(planet.valid());
    CHECK(planet != rocks);
    CHECK_FALSE(inSystemList(w.s, rocks));
    const SpaceObject& made = w.s.galaxy.object(planet);
    CHECK(made.kind == ObjectKind::Planet);
    CHECK(made.size == "Small");
    CHECK(made.value == std::array<int, 3>{70, 80, 90});
    CHECK(made.conditions >= Conditions::hundredths(50));
    CHECK(made.conditions <= kOptimalConditions);
    CHECK(made.name == "A I");                        // the next free numeral
    CHECK(made.slot != fieldSlot);                    // never the field's own slot
    CHECK(w.s.galaxy.objects.size() == objects + 1);  // a new record; the field's stays as a tombstone
    CHECK(w.s.freeSlot() == fieldSlot);               // the field's slot is empty now
    CHECK_FALSE(entryIntact(r, w.s, w.v(maker), 7));
    CHECK(w.v(maker).supply == 50);
    CHECK(w.v(maker).orders.empty());
    CHECK(w.logged(kA, "Planet Created"));

    // No star in the system: no planet.
    const ObjectId lonely = w.object(bare, ObjectKind::Asteroids, {3, 3});
    const VehicleId m2 = w.spawn(w.ship(kA, "M2", 3, {"Mv Planet Maker"}), at(bare, 3, 3));
    w.order(m2, stellar(StellarAction::CreatePlanet, lonely));
    w.move();
    CHECK(w.logged(kA, "needs a star"));
    CHECK(w.s.galaxy.object(lonely).kind == ObjectKind::Asteroids);

    // Destroy it again; the colony on it is lost. Colonies of empires at peace do not prevent it.
    w.s.empire(kA).relation(kB).contact = w.s.empire(kB).relation(kA).contact = true;
    w.setTreaty(kA, kB, Treaty::NonAggression);
    w.colony(planet, kB, 500);
    const VehicleId breaker = w.spawn(w.ship(kA, "Breaker", 3, {"Mv Planet Breaker"}), at(a, 2, 2));
    w.order(breaker, stellar(StellarAction::DestroyPlanet));
    const uint32_t lowestEmpty = w.s.freeSlot();
    w.move();
    // A new asteroid field takes the lowest empty slot, then the planet is
    // removed with its colony (Q72).
    const ObjectId rubble = w.objectAt(a, {2, 2}, ObjectKind::Asteroids);
    REQUIRE(rubble.valid());
    CHECK_FALSE(inSystemList(w.s, planet));
    CHECK(w.s.galaxy.object(rubble).slot == lowestEmpty);
    CHECK(w.s.freeSlot() == w.s.galaxy.object(planet).slot);
    CHECK(w.s.galaxy.object(rubble).name == "A I");  // keeps its name and values
    CHECK(w.s.galaxy.object(rubble).value == std::array<int, 3>{70, 80, 90});
    CHECK(w.s.colony(planet) == nullptr);
    CHECK(w.s.colony(rubble) == nullptr);
    CHECK(w.logged(kB, "lost"));
    CHECK(w.logged(kA, "Planet Destroyed"));
    CHECK(hasMood(w.lastMoods, kB, "Any Planet Lost"));

    // A guard in the planet's sector protects it (whoever owns it); one elsewhere does not.
    const ObjectId guarded = w.planet(a, {4, 4});
    w.colony(guarded, kA, 500, {"Mv Planet Guard"});  // the acting empire's own guard counts too
    const ObjectId open = w.planet(a, {9, 9});
    const ObjectId big = w.planet(a, {8, 8}, "Rock", "Oxygen", "Large");  // PlanetSize record 4 > Val 1 = 3
    const VehicleId b2 = w.spawn(w.ship(kA, "B2", 3, {"Mv Planet Breaker"}), at(a, 4, 4));
    const VehicleId b3 = w.spawn(w.ship(kA, "B3", 3, {"Mv Planet Breaker"}), at(a, 8, 8));
    const VehicleId b4 = w.spawn(w.ship(kA, "B4", 3, {"Mv Planet Breaker"}), at(a, 9, 9));
    w.order(b2, stellar(StellarAction::DestroyPlanet, guarded));
    w.order(b3, stellar(StellarAction::DestroyPlanet, big));
    w.order(b4, stellar(StellarAction::DestroyPlanet, open));
    w.move();
    CHECK(inSystemList(w.s, guarded));
    CHECK(inSystemList(w.s, big));
    CHECK_FALSE(inSystemList(w.s, open));
    CHECK(w.objectAt(a, {9, 9}, ObjectKind::Asteroids).valid());
    // The report the computer players' anger reads (spec 05 §7.3 term 2).
    CHECK(std::any_of(w.s.empire(kA).log.begin(), w.s.empire(kA).log.end(), [](const LogEntry& l) {
        return l.category == LogCategory::Events && movement::isDestructiveStellarReport(l.title);
    }));
    CHECK_FALSE(movement::isDestructiveStellarReport("Storm created in A"));
    CHECK(w.logged(kA, "protected"));
    CHECK(w.logged(kA, "too large"));
    CHECK(entryIntact(r, w.s, w.v(b2), 7));

    // No movement left: the order waits (spec 03 §8, simultaneous games).
    // Cloaked, or short of supply: no manipulation (spec 01 §9).
    const VehicleId stuck = w.spawn(w.design(kA, "Stuck", "Test Frigate", {"Test Bridge", "Mv Storm Maker"}), at(a, 1, 1));
    w.order(stuck, stellar(StellarAction::CreateStorm));
    const VehicleId hidden = w.spawn(w.ship(kA, "Hidden", 3, {"Mv Storm Maker", "Mv Cloak"}), at(a, 1, 2));
    w.v(hidden).status = VehicleStatus::Cloaked;
    w.order(hidden, stellar(StellarAction::CreateStorm));
    const VehicleId dry = w.spawn(w.ship(kA, "Dry", 3, {"Mv Planet Maker"}), at(bare, 3, 3));
    w.v(dry).supply = 10;
    w.order(dry, stellar(StellarAction::CreatePlanet, lonely));
    w.move();
    CHECK(w.v(stuck).orders.size() == 1);
    CHECK(w.logged(kA, "cloaked"));
    CHECK(w.logged(kA, "Not enough supply"));
    CHECK(countKind(w.s, a, ObjectKind::Storm) == 0);
}

TEST_CASE("movement: stellar manipulation - stars, nebulae and black holes") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 5, 0);
    const ObjectId star = w.object(a, ObjectKind::Star, {6, 6});
    const ObjectId planet = w.planet(a, {3, 3});
    w.s.galaxy.object(planet).value = {11, 22, 33};
    w.s.galaxy.object(planet).conditions = Conditions::hundredths(70);
    w.colony(planet, kA, 500);
    const ObjectId storm = w.object(a, ObjectKind::Storm, {9, 9});
    const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
    const VehicleId victim = w.spawn(w.ship(kB, "Victim", 1), at(a, 1, 1));

    // A star in a starless system, wherever the ship is.
    const VehicleId maker = w.spawn(w.ship(kA, "Maker", 3, {"Mv Star Maker"}), at(b, 2, 9));
    w.order(maker, stellar(StellarAction::CreateStar));
    const size_t before = w.s.galaxy.objects.size();
    w.move();
    REQUIRE(w.s.galaxy.objects.size() == before + 1);
    const ObjectId born{before};
    CHECK(w.s.galaxy.object(born).kind == ObjectKind::Star);
    CHECK(w.s.galaxy.object(born).sector == Sector{2, 9});
    CHECK(w.s.galaxy.object(born).name == "B Star");
    CHECK(inSystemList(w.s, born));
    CHECK(w.s.colonies.size() == w.s.galaxy.objects.size());
    CHECK(w.s.empire(kA).knowledge.knownWarpLink.size() == w.s.galaxy.objects.size());
    CHECK(w.logged(kA, "Star Created"));
    // Only one star per system (a destroyed star counts).
    const VehicleId again = w.spawn(w.ship(kA, "Again", 3, {"Mv Star Maker"}), at(b, 4, 4));
    w.order(again, stellar(StellarAction::CreateStar));
    w.move();
    CHECK(w.logged(kA, "already has a star"));

    // A guard on any owned object in the system protects the star, the acting empire's own included.
    w.colony(planet, kA, 500, {"Mv Star Guard"});
    const VehicleId nova = w.spawn(w.ship(kA, "Nova", 3, {"Mv Star Breaker"}), at(a, 6, 6));
    w.order(nova, stellar(StellarAction::DestroyStar, star));
    w.move();
    CHECK(w.s.galaxy.object(star).kind == ObjectKind::Star);
    CHECK(w.logged(kA, "protected"));

    // Unguarded: the shockwave. Planets become asteroid fields that keep their
    // name, values and conditions; everything else but warp points is gone.
    w.colony(planet, kA, 500);
    w.order(nova, stellar(StellarAction::DestroyStar, star));
    w.move();
    CHECK_FALSE(inSystemList(w.s, star));  // no destroyed star remains
    CHECK(countKind(w.s, a, ObjectKind::DestroyedStar) == 0);
    CHECK(w.s.vehicle(nova) == nullptr);
    CHECK(w.s.vehicle(victim) == nullptr);
    CHECK(w.s.colony(planet) == nullptr);
    CHECK_FALSE(inSystemList(w.s, planet));
    const ObjectId field = w.objectAt(a, {3, 3}, ObjectKind::Asteroids);
    REQUIRE(field.valid());
    CHECK(w.s.galaxy.object(field).value == std::array<int, 3>{11, 22, 33});
    CHECK(w.s.galaxy.object(field).conditions == Conditions::hundredths(70));
    CHECK_FALSE(inSystemList(w.s, storm));
    CHECK(inSystemList(w.s, ab));
    CHECK(w.logged(kA, "Star Destroyed"));
    CHECK(w.s.galaxy.system(a).physicalType == "Normal");

    // Nebulae: made from a star, with the shockwave; then removed.
    World n;
    const SystemId na = n.system("N");
    n.object(na, ObjectKind::Star, {6, 6});
    const ObjectId np = n.planet(na, {2, 2});
    const VehicleId gone = n.spawn(n.ship(kB, "Gone", 1), at(na, 0, 0));
    const VehicleId fog = n.spawn(n.ship(kA, "Fog", 3, {"Mv Nebula Maker"}), at(na, 6, 6));
    n.order(fog, stellar(StellarAction::CreateNebulae));
    n.move();
    CHECK(n.s.galaxy.system(na).physicalType == "Nebulae");
    REQUIRE(n.s.galaxy.system(na).abilities.size() == 1);
    CHECK(n.s.galaxy.system(na).abilities[0].number1() == 3);
    CHECK(n.rules().data().systemTypes[n.s.galaxy.system(na).type.index()].physicalType == "Nebulae");  // a nebula backdrop
    CHECK(countKind(n.s, na, ObjectKind::Star) == 0);
    CHECK_FALSE(inSystemList(n.s, np));
    CHECK(n.objectAt(na, {2, 2}, ObjectKind::Asteroids).valid());
    CHECK(n.s.vehicle(gone) == nullptr);
    CHECK(n.s.vehicle(fog) == nullptr);
    // No stars can be made in a nebula.
    const VehicleId sm = n.spawn(n.ship(kA, "Sm", 3, {"Mv Star Maker"}), at(na, 1, 1));
    n.order(sm, stellar(StellarAction::CreateStar));
    n.move();
    CHECK(countKind(n.s, na, ObjectKind::Star) == 0);
    const VehicleId clear = n.spawn(n.ship(kA, "Clear", 3, {"Mv Nebula Breaker"}), at(na, 1, 1));
    n.order(clear, stellar(StellarAction::DestroyNebulae));
    n.move();
    CHECK(n.s.galaxy.system(na).physicalType == "Normal");
    CHECK(n.s.galaxy.system(na).abilities.empty());
    CHECK(n.objectAt(na, {2, 2}, ObjectKind::Asteroids).valid());  // objects untouched

    // Black holes: pull 2, centre damage 5000, shield disruption 5000.
    World h;
    const SystemId ha = h.system("H");
    h.object(ha, ObjectKind::Star, {6, 6});
    const VehicleId hole = h.spawn(h.ship(kA, "Hole", 3, {"Mv Hole Maker"}), at(ha, 6, 6));
    h.order(hole, stellar(StellarAction::CreateBlackHole));
    h.move();
    const StarSystem& hs = h.s.galaxy.system(ha);
    CHECK(hs.physicalType == "Black Hole");
    REQUIRE(hs.abilities.size() == 3);
    CHECK(parseAbilityKind(hs.abilities[0].type) == AbilityKind::SystemMovementTowardsCenter);
    CHECK(hs.abilities[0].number1() == 2);
    CHECK(parseAbilityKind(hs.abilities[1].type) == AbilityKind::SystemDestructiveCenter);
    CHECK(hs.abilities[1].number1() == 5000);
    CHECK(parseAbilityKind(hs.abilities[2].type) == AbilityKind::SectorShieldDisruption);
    CHECK(hs.abilities[2].number1() == 5000);
    const VehicleId notNebula = h.spawn(h.ship(kA, "Wrong", 3, {"Mv Nebula Breaker"}), at(ha, 0, 0));
    h.order(notNebula, stellar(StellarAction::DestroyNebulae));
    const VehicleId fix = h.spawn(h.ship(kA, "Fix", 3, {"Mv Hole Breaker"}), at(ha, 0, 0));
    h.order(fix, stellar(StellarAction::DestroyBlackHole));
    h.move();
    CHECK(h.logged(kA, "not a nebula"));
    CHECK(h.s.galaxy.system(ha).physicalType == "Normal");
    CHECK(h.s.galaxy.system(ha).abilities.empty());
    (void)ba;
}

TEST_CASE("movement: stellar manipulation - opening and closing warp points") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A", 0, 0), b = w.system("B", 5, 0), c = w.system("C", 30, 0), d = w.system("D", 3, 4);
    const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
    w.exploreAll(kA);

    const VehicleId opener = w.spawn(w.ship(kA, "Opener", 3, {"Mv Warp Opener"}), at(a, 0, 0));
    w.order(opener, stellar(StellarAction::OpenWarpPoint, {}, at(d, 6, 6)));  // the target sector is not used
    const size_t before = w.s.galaxy.objects.size();
    w.move();
    REQUIRE(w.s.galaxy.objects.size() == before + 2);
    const ObjectId here{before}, there{before + 1};
    CHECK(w.s.galaxy.object(here).kind == ObjectKind::WarpPoint);
    CHECK(w.s.galaxy.object(here).sector == Sector{0, 0});
    CHECK(w.s.galaxy.object(here).destination == there);
    CHECK(w.s.galaxy.object(there).destination == here);
    CHECK(w.s.galaxy.object(there).system == d);
    // The far end: edge placement facing the origin.
    CHECK(w.s.galaxy.object(there).sector == warpEdgeSector(galaxyBearing({3, 4}, {0, 0}), {}));
    CHECK(w.s.galaxy.object(there).sector == Sector{4, 0});
    CHECK(w.s.galaxy.object(here).abilities.empty());
    CHECK_FALSE(r.data().sectorObjectTypes[w.s.galaxy.object(here).sectorType].unusual);
    CHECK(sight::knowsWarpLink(w.s, kA, here));
    CHECK(w.s.colonies.size() == w.s.galaxy.objects.size());
    CHECK(countKind(w.s, a, ObjectKind::WarpPoint) == 2);
    CHECK(w.logged(kA, "Warp Point Opened"));
    const auto shortcut = movement::findPath(r, w.s, kA, at(a, 0, 0), at(d, 4, 0));
    REQUIRE(shortcut);
    CHECK(shortcut->length == 1);

    // Already linked, out of range (distance 30 > 10), and blocked by a guard at the far end.
    const VehicleId twice = w.spawn(w.ship(kA, "Twice", 3, {"Mv Warp Opener"}), at(a, 3, 3));
    w.order(twice, stellar(StellarAction::OpenWarpPoint, {}, at(b, 0, 0)));
    const VehicleId far = w.spawn(w.ship(kA, "Far", 3, {"Mv Warp Opener"}), at(a, 1, 1));
    w.order(far, stellar(StellarAction::OpenWarpPoint, {}, at(c, 0, 0)));
    const SystemId e = w.system("E", 0, 6);
    w.colony(w.planet(e, {5, 5}), kA, 100, {"Mv Warp Guard"});
    const VehicleId blocked = w.spawn(w.ship(kA, "Blocked", 3, {"Mv Warp Opener"}), at(a, 2, 2));
    w.order(blocked, stellar(StellarAction::OpenWarpPoint, {}, at(e, 0, 0)));
    const size_t mid = w.s.galaxy.objects.size();
    w.move();
    CHECK(w.logged(kA, "already leads there"));
    CHECK(w.logged(kA, "out of range"));
    CHECK(w.logged(kA, "blocked"));
    CHECK(w.s.galaxy.objects.size() == mid);

    // A system never holds more than 10 warp points.
    World full;
    const SystemId fa = full.system("Hub", 20, 20);
    for (int i = 0; i < 10; ++i) {
        const SystemId spoke = full.system("Spoke " + std::to_string(i), 20 + (i % 5) * 3 - 6, 20 + (i / 5) * 12 - 6);
        full.link(fa, {12, i}, spoke, {0, 6});
    }
    const SystemId extra = full.system("Extra", 22, 22);
    const VehicleId crowd = full.spawn(full.ship(kA, "Crowd", 3, {"Mv Warp Opener"}), at(fa, 6, 6));
    full.order(crowd, stellar(StellarAction::OpenWarpPoint, {}, at(extra, 0, 0)));
    full.move();
    CHECK(full.logged(kA, "Too many warp points"));

    // Closing removes both ends; closing again is a harmless no-op; a guard in either system blocks it.
    World k;
    const SystemId ka = k.system("A"), kb = k.system("B", 5, 0), kc = k.system("C", 0, 5);
    const auto [kab, kba] = k.link(ka, {12, 6}, kb, {0, 6});
    const auto [kac, kca] = k.link(ka, {6, 12}, kc, {6, 0});
    k.colony(k.planet(kc, {2, 2}), kB, 100, {"Mv Warp Guard"});
    k.exploreAll(kA);
    const VehicleId closer = k.spawn(k.ship(kA, "Closer", 3, {"Mv Warp Closer"}), at(ka, 12, 6));
    const VehicleId late = k.spawn(k.ship(kA, "Late", 3, {"Mv Warp Closer"}), at(ka, 12, 6));
    const VehicleId guardedCloser = k.spawn(k.ship(kA, "Guarded", 3, {"Mv Warp Closer"}), at(ka, 6, 12));
    k.order(closer, stellar(StellarAction::CloseWarpPoint, kab));
    k.order(late, stellar(StellarAction::CloseWarpPoint, kab));
    k.order(guardedCloser, stellar(StellarAction::CloseWarpPoint, kac));
    k.move();
    CHECK_FALSE(inSystemList(k.s, kab));
    CHECK_FALSE(inSystemList(k.s, kba));
    CHECK_FALSE(k.s.galaxy.object(kab).destination.valid());
    CHECK_FALSE(movement::findPath(r, k.s, EmpireId{}, at(ka, 6, 6), at(kb, 6, 6)));
    CHECK_FALSE(entryIntact(r, k.s, k.v(closer), 7));
    CHECK(entryIntact(r, k.s, k.v(late), 7));
    CHECK(k.v(late).orders.empty());
    CHECK(inSystemList(k.s, kac));
    CHECK(k.logged(kA, "closure is blocked"));
    CHECK(k.logged(kA, "Warp Point Closed"));
    (void)ba;
    (void)kca;
}

TEST_CASE("movement: stellar manipulation - storms and constructed worlds") {
    World w;
    const SystemId a = w.system("A");
    const ObjectId star = w.object(a, ObjectKind::Star, {6, 6});
    const ObjectId storm = w.object(a, ObjectKind::Storm, {8, 8});

    const VehicleId maker = w.spawn(w.ship(kA, "Maker", 3, {"Mv Storm Maker"}), at(a, 10, 10));
    w.order(maker, stellar(StellarAction::CreateStorm));
    const VehicleId breaker = w.spawn(w.ship(kA, "Breaker", 3, {"Mv Storm Breaker"}), at(a, 8, 8));
    w.order(breaker, stellar(StellarAction::DestroyStorm));
    const size_t before = w.s.galaxy.objects.size();
    w.move();
    CHECK_FALSE(inSystemList(w.s, storm));
    REQUIRE(w.s.galaxy.objects.size() == before + 1);
    const SpaceObject& made = w.s.galaxy.objects.back();
    CHECK(made.kind == ObjectKind::Storm);
    CHECK(made.name == "Storm");
    CHECK(made.sector == Sector{10, 10});
    REQUIRE(made.abilities.size() == 1);
    // The value is the Created Storm Maximum setting itself (2, 7 and 9 in the test rules).
    const auto kind = parseAbilityKind(made.abilities[0].type);
    const int64_t value = made.abilities[0].number1();
    if (kind == AbilityKind::SectorSightObscuration) CHECK(value == 2);
    else if (kind == AbilityKind::SectorDamage) CHECK(value == 7);
    else CHECK((kind == AbilityKind::SectorShieldDisruption && value == 9));

    // Settings of 0 are redrawn: only damage is left.
    ruleset::Ruleset data = buildRuleset();
    data.settings.set("Created Storm Maximum Obscuration Level", "0");
    data.settings.set("Created Storm Maximum Shield Disruption", "0");
    const Rules onlyDamage{std::move(data)};
    World d(onlyDamage);
    const SystemId da = d.system("D");
    for (int i = 0; i < 4; ++i) {
        const VehicleId m = d.spawn(d.ship(kA, "M", 3, {"Mv Storm Maker"}), at(da, i, 0));
        d.order(m, stellar(StellarAction::CreateStorm));
    }
    d.move();
    for (const SpaceObject& o : d.s.galaxy.objects) {
        REQUIRE(o.abilities.size() == 1);
        CHECK(parseAbilityKind(o.abilities[0].type) == AbilityKind::SectorDamage);
    }

    // A ringworld needs 20 kT of girders on ships in the star's sector, whoever owns them.
    const VehicleId builder = w.spawn(w.design(kA, "Builder", "Test Frigate", {"Test Bridge", "Mv Tank", "Mv World Builder", "Mv Girder"}), at(a, 6, 6));
    w.order(builder, stellar(StellarAction::CreateConstructedPlanet, star));
    w.move();
    CHECK(w.logged(kA, "materials"));
    w.s.empire(kA).relation(kB).contact = w.s.empire(kB).relation(kA).contact = true;
    w.setTreaty(kA, kB, Treaty::TradeAlliance);
    const VehicleId hauler = w.spawn(w.ship(kB, "Girders", 3, {"Mv Girder"}), at(a, 6, 6));
    const VehicleId bystander = w.spawn(w.ship(kA, "Bystander", 3), at(a, 6, 6));
    w.order(builder, stellar(StellarAction::CreateConstructedPlanet, star));
    const size_t count = w.s.galaxy.objects.size();
    w.move();  // no movement needed: the builder has no engines
    REQUIRE(w.s.galaxy.objects.size() == count + 1);
    const SpaceObject& world = w.s.galaxy.objects.back();
    CHECK(world.kind == ObjectKind::Planet);
    CHECK(world.size == "Ringworld");
    CHECK(world.sector == Sector{6, 6});
    CHECK(world.value == std::array<int, 3>{160, 160, 160});  // Planet Value High Percent
    CHECK(world.conditions == kOptimalConditions);             // Optimal
    CHECK(world.surface == "Rock");                            // the builder's type and atmosphere
    CHECK(world.atmosphere == "Oxygen");
    CHECK_FALSE(inSystemList(w.s, star));  // the star is used up
    CHECK(w.s.vehicle(builder) == nullptr);   // the builder's ship with the device is destroyed
    CHECK(w.s.vehicle(hauler) != nullptr);    // another empire's material ships are not
    CHECK(w.s.vehicle(bystander) != nullptr);
    CHECK(w.logged(kA, "Planet Created"));
}

// ---- Orders, groups and logistics (spec 03 §8, §9, §12) ---------------------------------------------

TEST_CASE("movement: a failed order clears every fleet member's list and switches Repeat off") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    const VehicleId one = w.spawn(w.ship(kA, "One", 2, {"Test Quantum Reactor"}), at(a, 0, 0));
    const VehicleId two = w.spawn(w.ship(kA, "Two", 2, {"Test Quantum Reactor"}), at(a, 0, 0));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {one, two}}).ok);
    const FleetId fid = w.s.fleets.back().id;
    // B is not linked: the second order fails.
    fleetGets(w, fid, {moveTo(a, 1, 0), moveTo(b, 3, 3), moveTo(a, 2, 0)}, true);
    w.move();  // the step that arrives uses the action's movement point: the next order waits
    CHECK(w.v(one).orders.size() == 3);
    CHECK(w.v(two).orders.size() == 3);
    w.move();
    CHECK(fleetList(w, fid).empty());
    CHECK_FALSE(fleetRepeats(w.s, *w.s.fleet(fid)));
    for (VehicleId id : {one, two}) {
        CHECK(w.v(id).orders.empty());
        CHECK_FALSE(w.v(id).repeatOrders);
    }
    CHECK(w.v(one).location == at(a, 1, 0));
    CHECK(w.logged(kA, "No known route"));

    // A done order stays at the end with Repeat on (the list cycles).
    const VehicleId solo = w.spawn(w.ship(kA, "Solo", 2, {"Test Quantum Reactor"}), at(a, 6, 6));
    w.order(solo, moveTo(a, 7, 6), true);
    w.order(solo, mk(OrderKind::Sentry), true);
    w.move();
    REQUIRE(w.v(solo).orders.size() == 2);
    CHECK(w.v(solo).orders.front().kind == OrderKind::Sentry);  // Sentry waits: the move went to the end
    CHECK(w.v(solo).orders.back() == moveTo(a, 7, 6));
}

TEST_CASE("movement: done orders chain within one action; a vehicle with no movement acts on day 1 only") {
    World w;
    const SystemId a = w.system("A");
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    // A base (speed 0) acts on day 1 and runs all of its orders that need no movement then (spec 03 §6.3).
    const VehicleId base = w.spawn(w.design(kA, "Hangar", "Test Station", {"Test Bridge", "Mv Fighter Bay"}), at(a, 4, 4));
    w.v(base).cargo.units.push_back({fighter, 2});
    w.order(base, mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, 1));
    w.order(base, mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, 1));
    w.order(base, moveTo(a, 5, 5));  // needs movement: fails (a base cannot move)
    w.move();
    CHECK(w.v(base).orders.empty());
    CHECK(w.v(base).cargo.unitCount(fighter) == 0);
    CHECK(w.logged(kA, "It cannot move"));

    // A repeating list of orders that complete goes round at most 21 executions an action.
    const VehicleId looper = w.spawn(w.ship(kA, "Looper", 2), at(a, 8, 8));
    w.order(looper, mk(OrderKind::Decloak), true);
    w.order(looper, mk(OrderKind::Decloak), true);
    w.move();
    CHECK(w.v(looper).orders.size() == 2);
}

TEST_CASE("movement: planets launch and recover units by order, with no bay") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {5, 5});
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const DesignId mine = w.design(kA, "Mine", "Mv Mine Hull", {"Test Warhead"});
    w.colony(home, kA, 1000).cargo.units = {{fighter, 1200}, {mine, 30}};
    auto inSpace = [&](DesignId d) {
        int n = 0;
        for (const Vehicle& v : w.s.vehicles)
            if (v.design == d && v.count > 0 && v.location == at(a, 5, 5)) n += v.count;
        return n;
    };
    // Only launch and recover orders, and only for own planets.
    CHECK_FALSE(apply(r, w.s, kA, cmd::SetOrders{{}, {}, {moveTo(a, 1, 1)}, false, home}).ok);
    CHECK_FALSE(apply(r, w.s, kB, cmd::SetOrders{{}, {}, {mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, -1)}, false, home}).ok);
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{{}, {}, {mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, -1)}, false, home}).ok);
    w.move();
    CHECK(inSpace(fighter) == 1000);  // 1000 of each kind a turn
    CHECK(w.s.colony(home)->cargo.unitCount(fighter) == 200);
    CHECK(w.s.colony(home)->orders.empty());
    CHECK(w.logged(kA, "launched 1000"));
    // The units-in-space cap (1000) is reached: the next launch is refused.
    w.s.colony(home)->orders = {mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, -1)};
    w.move();
    CHECK(inSpace(fighter) == 1000);
    // Mines count against the cap too; below it, a launch may go past it.
    w.s.options.maxUnitsPerPlayer = 1001;
    w.s.colony(home)->orders = {mk(OrderKind::LaunchUnits, {}, {}, {}, mine, -1)};
    w.move();
    CHECK(inSpace(mine) == 30);
    // Recovery into the planet is limited only by its free cargo space.
    w.s.colony(home)->cargo.units.clear();
    w.s.colony(home)->orders = {mk(OrderKind::RecoverUnits, {}, {}, {}, fighter, -1)};
    w.move();
    CHECK(w.s.colony(home)->cargo.unitCount(fighter) == 500 / 20);
    CHECK(inSpace(fighter) == 1000 - 25);
}

TEST_CASE("movement: the units-in-space cap is checked at launch, not when building") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const VehicleId carrier = w.spawn(w.ship(kA, "Carrier", 2, {"Mv Fighter Bay"}), at(a, 5, 5));
    w.v(carrier).cargo.units.push_back({fighter, 9});
    w.s.options.maxUnitsPerPlayer = 5;
    const VehicleId group = w.spawn(fighter, at(a, 5, 5));
    w.v(group).count = 4;
    w.order(carrier, mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, -1));
    w.move();
    CHECK(w.v(group).count == 4 + 3);  // below the cap before the stack: not cut to fit
    w.order(carrier, mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, -1));
    w.move();
    CHECK(w.v(group).count == 7);
    CHECK(movement::unitsInSpace(r, w.s, kA) == 7);
    CHECK(unitCount(r, w.s, kA) == 7 + 6);  // cargo is not in space
}

TEST_CASE("movement: a ship whose space yard is building cannot move") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId yard = w.spawn(w.ship(kA, "Yard", 2, {"Test Yard Module"}), at(a, 2, 2));
    w.v(yard).queue.items.push_back(QueueItem{});
    w.order(yard, moveTo(a, 5, 5));
    w.move();
    CHECK(w.v(yard).location == at(a, 2, 2));
    CHECK(w.v(yard).orders.empty());
    CHECK(w.logged(kA, "space yard"));
    w.v(yard).queue.items.clear();
    w.order(yard, moveTo(a, 3, 3));
    w.move();
    CHECK(w.v(yard).location == at(a, 3, 3));
}

TEST_CASE("movement: a sector is fought over again only after a damaged survivor, a newcomer or a minefield") {
    // Spec 03 §6.3 step 6: the latest battle at a location this turn decides.
    auto run = [](bool damage, bool newcomer, bool minefield) {
        World w;
        const SystemId a = w.system("A");
        const VehicleId guard = w.spawn(w.ship(kB, "Guard", 1, {"Mv Armor"}), at(a, 3, 6));
        const VehicleId raider = w.spawn(w.ship(kA, "Raider", 4, {"Mv Sweeper"}), at(a, 2, 6));  // days 8, 16, 23
        fuel(w, raider);
        // Arrives on day 8, then sweeps in place every action: the sector is checked each time.
        w.order(raider, moveTo(a, 3, 6), true);
        w.order(raider, mk(OrderKind::SweepMines), true);
        if (newcomer) {
            const VehicleId late = w.spawn(w.ship(kA, "Late", 2), at(a, 3, 5));  // day 16
            fuel(w, late);
            w.order(late, moveTo(a, 3, 6));
        }
        if (minefield) w.spawn(w.design(kA, "Mine", "Mv Mine Hull", {"Test Warhead"}), at(a, 3, 6));
        CombatSpy spy;
        spy.fight = hostilesMeet;
        movement::CombatHooks hooks = spy.hooks();
        if (damage) {
            auto resolve = hooks.resolve;
            hooks.resolve = [resolve, guard](TurnContext& ctx, Location l, std::span<const VehicleId> in, const combat::BattleCheck& check) {
                resolve(ctx, l, in, check);
                // The guard loses its armor plate: a survivor below full structure.
                if (Vehicle* g = ctx.state.vehicle(guard)) g->damage.back() = 30;
            };
        }
        w.move(hooks);
        CHECK(std::count(spy.asked.begin(), spy.asked.end(), at(a, 3, 6)) == 3);
        return spy.fought.size();
    };
    CHECK(run(false, false, false) == 1);  // everyone there survived it undamaged
    CHECK(run(true, false, false) == 3);   // a damaged survivor forces a new battle
    CHECK(run(false, true, false) == 2);   // a newcomer on day 16 does
    CHECK(run(false, false, true) == 3);   // a minefield is never a combat piece: always a new battle
}

TEST_CASE("movement: warp links work both ways; only the first 10 warp points of a system are used; neutrals never warp") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 5, 0);
    const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
    (void)ab;  // every link works both ways (spec 01 §8)
    w.exploreAll(kA);
    CHECK(movement::findPath(r, w.s, kA, at(a, 6, 6), at(b, 6, 6)));
    CHECK(movement::findPath(r, w.s, kA, at(b, 6, 6), at(a, 6, 6)));

    World m;
    const SystemId hub = m.system("Hub");
    std::vector<SystemId> spokes;
    for (int i = 0; i < 11; ++i) {
        spokes.push_back(m.system("S" + std::to_string(i), 10 + i, 10));
        m.link(hub, {i, 0}, spokes.back(), {6, 6});
    }
    m.exploreAll(kA);
    CHECK(movement::findPath(r, m.s, kA, at(hub, 6, 6), at(spokes[9], 5, 5)));
    CHECK_FALSE(movement::findPath(r, m.s, kA, at(hub, 6, 6), at(spokes[10], 5, 5)));

    w.s.empire(kB).kind = PlayerKind::Neutral;
    const VehicleId drifter = w.spawn(w.ship(kB, "Stay Home", 2), at(a, 12, 6));
    w.order(drifter, mk(OrderKind::Warp, {}, ab));
    w.move();
    CHECK(w.v(drifter).location == at(a, 12, 6));
    CHECK(w.logged(kB, "Neutral empires cannot use warp points"));
    (void)ba;
}

TEST_CASE("movement: resupply skips guarded depots; repair prefers planets and bases to ships") {
    World w;
    const SystemId a = w.system("A");
    w.exploreAll(kA);
    const ObjectId near = w.planet(a, {2, 6});
    const ObjectId far = w.planet(a, {8, 6});
    w.colony(near, kA, 100, {"Test Depot"});
    w.colony(far, kA, 100, {"Test Depot"});
    w.spawn(w.ship(kB, "Picket", 1, {"Test Laser"}), at(a, 2, 6));
    const VehicleId tanker = w.spawn(w.ship(kA, "Tanker", 4), at(a, 1, 6));
    fuel(w, tanker);
    w.order(tanker, mk(OrderKind::Resupply));
    w.move();
    w.move();
    w.move();
    CHECK(w.v(tanker).location == at(a, 8, 6));
    CHECK(w.v(tanker).orders.empty());

    World p;
    const SystemId pa = p.system("A");
    p.exploreAll(kA);
    p.spawn(p.design(kA, "Tender", "Test Frigate", {"Test Bridge", "Test Repair Bay"}), at(pa, 2, 2));
    p.spawn(p.design(kA, "Dock", "Test Station", {"Test Bridge", "Test Repair Bay"}), at(pa, 9, 9));
    const VehicleId wreck = p.spawn(p.ship(kA, "Wreck", 3), at(pa, 1, 1));
    fuel(p, wreck);
    p.order(wreck, mk(OrderKind::Repair));
    for (int i = 0; i < 4; ++i) p.move();
    CHECK(p.v(wreck).location == at(pa, 9, 9));  // the base, though the ship is closer
}

TEST_CASE("movement: colonizing fails with a cloaked member; the last suitable member colonizes") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId target = w.planet(a, {5, 5});
    const VehicleId first = w.spawn(w.ship(kA, "First", 3, {"Test Rock Pod"}), at(a, 5, 5));
    const VehicleId last = w.spawn(w.ship(kA, "Last", 3, {"Test Rock Pod"}), at(a, 5, 5));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Settlers", {first, last}}).ok);
    fleetGets(w, w.s.fleets.back().id, {mk(OrderKind::Colonize, {}, target)});
    w.colonize();
    REQUIRE(w.s.colony(target));
    CHECK(w.s.vehicle(last) == nullptr);
    CHECK(w.s.vehicle(first) != nullptr);

    const ObjectId other = w.planet(a, {6, 6});
    const VehicleId shade = w.spawn(w.ship(kA, "Shade", 3, {"Test Rock Pod", "Mv Cloak"}), at(a, 6, 6));
    w.v(shade).status = VehicleStatus::Cloaked;
    w.order(shade, mk(OrderKind::Colonize, {}, other));
    w.move();
    CHECK(w.v(shade).orders.empty());
    CHECK(w.logged(kA, "cloaked ship cannot colonize"));
    CHECK_FALSE(w.s.colony(other));
}

TEST_CASE("movement: training sources stack; a cap of 0 trains nobody; fleets stop at 50") {
    ruleset::Ruleset rs = buildRuleset();
    ruleset::Facility camp;
    camp.name = "Mv Camp";
    camp.abilities = {ab(AbilityKind::ShipTraining, 4, 20)};
    rs.facilities.push_back(camp);
    ruleset::Facility broken;
    broken.name = "Mv Broken Camp";
    broken.abilities = {ab(AbilityKind::ShipTraining, 50, 0)};
    rs.facilities.push_back(broken);
    ruleset::Facility academy;
    academy.name = "Mv Academy";
    academy.abilities = {ab(AbilityKind::FleetTrainingSystem, 30, 100)};
    rs.facilities.push_back(academy);
    rs.reindex();
    const Rules r{std::move(rs)};
    World w(r);
    const SystemId a = w.system("A");
    w.colony(w.planet(a, {6, 6}), kA, 100, {"Mv Camp"});
    w.colony(w.planet(a, {6, 6}), kA, 100, {"Mv Camp"});
    w.colony(w.planet(a, {6, 6}), kA, 100, {"Mv Broken Camp"});
    w.colony(w.planet(a, {1, 1}), kA, 100, {"Mv Academy"});
    const VehicleId cadet = w.spawn(w.ship(kA, "Cadet", 1), at(a, 6, 6));
    w.colony(w.planet(a, {9, 9}), kA, 100, {"Mv Camp", "Mv Broken Camp"});
    const VehicleId rookie = w.spawn(w.ship(kA, "Rookie", 1), at(a, 9, 9));
    const VehicleId m1 = w.spawn(w.ship(kA, "M1", 1), at(a, 3, 3));
    const VehicleId m2 = w.spawn(w.ship(kA, "M2", 1), at(a, 3, 3));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Drill", {m1, m2}}).ok);
    w.s.empire(kA).knowledge.explored[a.index()] = 1;  // system-wide sources count in explored systems
    w.upkeep();
    CHECK(w.v(cadet).experience == 8);  // two sources of 4; the one with cap 0 gives nothing
    CHECK(w.v(rookie).experience == 20);  // one object is one source: its largest V1 (50) and V2 (20)
    w.upkeep();
    w.upkeep();
    CHECK(w.v(cadet).experience == 20);  // 16, then the smaller of 4 and (20 - 16), twice
    CHECK(w.s.fleets.back().experience == 50);  // 30, then 60 capped at 50
}

TEST_CASE("movement: repair needs known technology; emergency parts need an own yard") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    w.spawn(w.design(kA, "Tender", "Test Frigate", {"Test Bridge", "Test Repair Bay"}), at(a, 2, 2));
    const VehicleId hurt = w.spawn(w.ship(kA, "Hurt", 1, {"Mv Energy Cell", "Test Planet Maker"}), at(a, 2, 2));
    for (int& d : w.v(hurt).damage) d = 5;
    w.upkeep();
    const Vehicle& h = w.v(hurt);
    CHECK(h.damage[5] == 5);  // the energy cell waits for a yard
    CHECK(h.damage[6] == 5);  // Physics 5 is not known
    CHECK(totalDamage(h) == 10);
    w.spawn(w.design(kA, "Yard", "Test Station", {"Test Bridge", "Test Yard Module"}), at(a, 2, 2));
    w.upkeep();
    CHECK(w.v(hurt).damage[5] == 0);
    CHECK(w.v(hurt).damage[6] == 5);
    (void)r;
}

TEST_CASE("movement: obsolete designs go when nothing uses or knows them") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const DesignId used = w.ship(kA, "Used", 1);
    const DesignId queued = w.ship(kA, "Queued", 1);
    const DesignId seen = w.ship(kA, "Seen", 1);
    const DesignId gone = w.ship(kA, "Gone", 1);
    const DesignId kept = w.ship(kA, "Kept", 1);
    for (DesignId d : {used, queued, seen, gone}) w.s.design(d).obsolete = true;
    w.spawn(used, at(a, 1, 1));
    w.colony(w.planet(a, {2, 2}), kA, 100).queue.items.push_back(QueueItem{QueueItem::Kind::Vehicle, queued});
    seeDesign(w.s.empire(kB).knowledge, seen, 59 - 49);  // less than 50 turns ago: protected
    const DesignId stale = w.ship(kA, "Stale", 1);
    w.s.design(stale).obsolete = true;
    seeDesign(w.s.empire(kB).knowledge, stale, 59 - 50);  // exactly 50 turns old: no protection (spec 03 §4.1)
    w.s.turn = 59;  // the cleanup runs every 10th turn
    w.upkeep();
    const auto& list = w.s.empire(kA).designs;
    auto has = [&](DesignId d) { return std::find(list.begin(), list.end(), d) != list.end(); };
    CHECK(has(used));
    CHECK(has(queued));
    CHECK(has(seen));
    CHECK(has(kept));
    CHECK_FALSE(has(gone));
    CHECK_FALSE(has(stale));
    (void)r;
}

TEST_CASE("movement: cargo that no longer fits goes population first, then from the first unit stack") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const DesignId mine = w.design(kA, "Mine", "Mv Mine Hull", {"Test Warhead"});
    const DesignId mine2 = w.design(kA, "Mine B", "Mv Mine Hull", {"Test Warhead"});
    const VehicleId hauler = w.spawn(w.ship(kA, "Hauler", 1, {"Test Cargo Bay", "Test Cargo Bay"}), at(a, 1, 1));  // 100 kT
    Vehicle& v = w.v(hauler);
    v.cargo.population = {{kA, 4}, {kB, 4}};  // 40 kT
    v.cargo.units = {{mine, 3}, {mine2, 3}};  // 60 kT
    v.damage[6] = 1000;                       // one bay gone: 50 kT left
    // The owner's upkeep no longer cuts cargo: it is cut when the part is
    // destroyed, at once (spec 04 §9.4), as hazards do (fitToCapacity).
    const Cargo before = v.cargo;
    w.upkeep();
    CHECK(w.v(hauler).cargo.units == before.units);
    CHECK(w.v(hauler).cargo.population == before.population);
    movement::fitToCapacity(r, w.s, w.v(hauler));
    // 50 kT over: 8M of people (40 kT), then one mine of the first stack.
    CHECK(w.v(hauler).cargo.population.empty());
    REQUIRE(w.v(hauler).cargo.units.size() == 2);
    CHECK(w.v(hauler).cargo.units[0] == UnitStack{mine, 2});
    CHECK(w.v(hauler).cargo.units[1] == UnitStack{mine2, 3});
    CHECK(cargoSpaceUsed(r, w.s, w.v(hauler).cargo) <= vehicleCargoCapacity(r, w.s, w.v(hauler)));
}

TEST_CASE("movement: damage outside combat cuts supply and cargo back at once") {
    // Hazards strike in the event step, after the owner's end of turn: the
    // storage they wreck takes supply and cargo with it there and then (spec 03 §7, §11).
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        World w;
        w.s.rng = Rng(seed);
        const Rules& r = w.rules();
        const SystemId a = w.system("A");
        const VehicleId id = w.spawn(w.ship(kA, "Hauler", 1, {"Test Cargo Bay"}), at(a, 1, 1));
        Vehicle& v = w.v(id);
        const Design& d = w.s.design(v.design);
        REQUIRE(r.component(d.entries[3].component).name == "Mv Tank");
        REQUIRE(r.component(d.entries[5].component).name == "Test Cargo Bay");
        REQUIRE(entryStructure(r, d, 3) == entryStructure(r, d, 5));
        for (size_t i : {0, 1, 2, 4}) v.damage[i] = entryStructure(r, d, i);  // only the tank and the bay are left
        v.supply = vehicleSupplyCapacity(r, w.s, v);
        v.cargo.population = {{kA, vehicleCargoCapacity(r, w.s, v) / r.setting("Population Mass", 5)}};
        // One of the two is wrecked.
        CHECK_FALSE(movement::damageVehicle(r, w.s, v, entryStructure(r, d, 3)));
        CHECK(entryIntact(r, w.s, v, 3) != entryIntact(r, w.s, v, 5));
        CHECK(v.supply <= vehicleSupplyCapacity(r, w.s, v));
        CHECK(cargoSpaceUsed(r, w.s, v.cargo) <= vehicleCargoCapacity(r, w.s, v));
    }
}

TEST_CASE("movement: objects act in object order; a new object takes the first slot any removed object freed") {
    World w;
    const SystemId a = w.system("A");
    const DesignId d = w.ship(kA, "Hull", 2);
    const VehicleId first = w.spawn(d, at(a, 0, 0));
    const VehicleId second = w.spawn(d, at(a, 0, 0));
    const VehicleId third = w.spawn(d, at(a, 0, 0));
    CHECK(w.v(first).slot < w.v(second).slot);
    CHECK(w.v(second).slot < w.v(third).slot);
    const uint32_t freed = w.v(second).slot;
    w.v(second).count = 0;
    w.s.removeDeadVehicles();
    const VehicleId fourth = w.spawn(d, at(a, 0, 0));
    CHECK(w.v(fourth).slot == freed);  // before `third` in object order, though created after it
    CHECK(fourth > third);

    // Planets with orders act on day 1 where their slots put them among the
    // vehicles (spec 03 §6.3 step 5, §19 Q62): a colony whose slot comes first
    // launches before a base acting on day 1, which recovers the group at once.
    const ObjectId home = w.planet(a, {5, 5});
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    w.colony(home, kA, 100).cargo.units.push_back({fighter, 3});
    w.s.colony(home)->orders = {mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, -1)};
    const DesignId dockDesign = w.design(kA, "Dock", "Test Station", {"Test Bridge", "Mv Fighter Bay"});
    const VehicleId dock = w.spawn(dockDesign, at(a, 5, 5));
    REQUIRE(w.s.galaxy.object(home).slot < w.v(dock).slot);
    w.order(dock, mk(OrderKind::RecoverUnits, {}, {}, {}, fighter, -1));
    w.move();
    CHECK(w.v(dock).cargo.unitCount(fighter) == 3);
    CHECK(w.s.colony(home)->cargo.unitCount(fighter) == 0);

    // A base whose slot comes before the planet's acts first: it finds nothing
    // to recover, and the colony's launch follows.
    const VehicleId early = w.spawn(dockDesign, at(a, 6, 6));
    const ObjectId later = w.planet(a, {6, 6});
    REQUIRE(w.v(early).slot < w.s.galaxy.object(later).slot);
    w.colony(later, kA, 100).cargo.units.push_back({fighter, 2});
    w.s.colony(later)->orders = {mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, -1)};
    w.order(early, mk(OrderKind::RecoverUnits, {}, {}, {}, fighter, -1));
    w.move();
    CHECK(w.v(early).cargo.unitCount(fighter) == 0);
    CHECK(w.s.colony(later)->cargo.unitCount(fighter) == 0);  // launched into space

    // The order holds stars, planets, storms, warp points and vehicles alike: a
    // ship takes the slot a storm left, and a new storm the slot a ship left.
    const ObjectId storm = w.object(a, ObjectKind::Storm, {9, 9});
    const uint32_t stormSlot = w.s.galaxy.object(storm).slot;
    std::erase(w.s.galaxy.system(a).objects, storm);  // taken off the map: its slot is free
    const VehicleId heir = w.spawn(d, at(a, 0, 0));
    CHECK(w.v(heir).slot == stormSlot);
    const uint32_t shipSlot = w.v(third).slot;
    w.v(third).count = 0;
    w.s.removeDeadVehicles();
    SpaceObject made;
    made.kind = ObjectKind::Storm;
    made.sector = Sector{8, 8};
    const ObjectId newStorm = w.s.addObject(made, a);
    CHECK(w.s.galaxy.object(newStorm).slot == shipSlot);
    const std::vector<ObjectRef> order = objectOrder(w.s);
    for (size_t i = 1; i < order.size(); ++i) CHECK(order[i - 1].slot < order[i].slot);
    // The system lists its objects in object order.
    const auto& list = w.s.galaxy.system(a).objects;
    for (size_t i = 1; i < list.size(); ++i) CHECK(w.s.galaxy.object(list[i - 1]).slot < w.s.galaxy.object(list[i]).slot);
}

TEST_CASE("movement: the Attack Sector question: seen enemies on in-system steps, not for cloaked or drone groups, not on warp jumps") {
    // Turn-based games, a human player moving (spec 03 §6.2, confirmed: binary).
    auto asked = [](auto&& setup) {
        World w;
        w.s.options.simultaneous = false;
        const SystemId a = w.system("A"), b = w.system("B", 10, 0);
        w.link(a, {2, 6}, b, {0, 6});
        w.exploreAll(kA);
        w.setTreaty(kA, kB, Treaty::War);
        const VehicleId mover = setup(w, a, b);
        TurnContext ctx{w.rules(), w.s, {}, {}, {}};
        movement::startTurn(ctx, kA);
        movement::LiveMove m{kA};
        m.ask = true;
        CombatSpy spy;
        const auto qs = movement::runLive(ctx, m, spy.hooks());
        return std::pair{qs.size(), w.v(mover).location};
    };
    // A visible enemy ship in the next sector: asked, the group waits.
    auto plain = [](World& w, SystemId a, SystemId) {
        w.spawn(w.ship(kB, "Picket", 1), at(a, 2, 5));
        const VehicleId v = w.spawn(w.ship(kA, "Runner", 3), at(a, 0, 5));
        fuel(w, v);
        w.order(v, moveTo(a, 2, 5));
        return v;
    };
    CHECK(asked(plain).first == 1);
    CHECK(asked(plain).second == at(SystemId{0u}, 1, 5));
    // A colony the owner sees counts too.
    CHECK(asked([](World& w, SystemId a, SystemId) {
              w.colony(w.planet(a, {2, 5}), kB, 100);
              const VehicleId v = w.spawn(w.ship(kA, "Runner", 3), at(a, 0, 5));
              fuel(w, v);
              w.order(v, moveTo(a, 2, 5));
              return v;
          }).first == 1);
    // Every member cloaked: no question, the group goes in.
    const auto cloaked = asked([](World& w, SystemId a, SystemId) {
        w.spawn(w.ship(kB, "Picket", 1), at(a, 2, 5));
        const VehicleId v = w.spawn(w.ship(kA, "Shade", 3, {"Mv Cloak"}), at(a, 0, 5));
        fuel(w, v);
        w.v(v).status = VehicleStatus::Cloaked;
        w.order(v, moveTo(a, 2, 5));
        return v;
    });
    CHECK(cloaked.first == 0);
    CHECK(cloaked.second == at(SystemId{0u}, 2, 5));
    // A group made only of drones: no question.
    CHECK(asked([](World& w, SystemId a, SystemId) {
              w.spawn(w.ship(kB, "Picket", 1), at(a, 2, 5));
              const VehicleId v = w.spawn(w.design(kA, "Drone", "Test Drone Hull", {"Mv Engine", "Mv Engine", "Mv Engine", "Mv Drone Tank"}), at(a, 0, 5));
              w.order(v, moveTo(a, 2, 5));
              return v;
          }).first == 0);
    // A warp jump never asks, even onto an enemy.
    const auto jumped = asked([](World& w, SystemId a, SystemId b) {
        w.spawn(w.ship(kB, "Picket", 1), at(b, 0, 6));
        const VehicleId v = w.spawn(w.ship(kA, "Jumper", 3), at(a, 2, 6));
        fuel(w, v);
        w.order(v, mk(OrderKind::Warp, {}, w.s.galaxy.system(a).objects[0]));
        return v;
    });
    CHECK(jumped.first == 0);
    CHECK(jumped.second == at(SystemId{1u}, 0, 6));
}

TEST_CASE("movement: a warp arrival makes no storm roll") {
    for (uint64_t seed = 1; seed <= 16; ++seed) {
        World w;
        const SystemId a = w.system("A"), b = w.system("B", 10, 0);
        const auto [wa, wb] = w.link(a, {6, 6}, b, {0, 6});
        const ObjectId storm = w.object(b, ObjectKind::Storm, {0, 6});
        w.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorDamage, 30));
        w.exploreAll(kA);
        const VehicleId ship = w.spawn(w.ship(kA, "Jumper", 2, {"Mv Armor"}), at(a, 6, 6));
        fuel(w, ship);
        w.order(ship, mk(OrderKind::Warp, {}, wa));
        w.s.rng.reseed(seed);
        w.move();
        CHECK(w.v(ship).location == at(b, 0, 6));
        CHECK(totalDamage(w.v(ship)) == 0);
        (void)wb;
    }
}

TEST_CASE("movement: around a destructive centre steps follow its cost map and keep out of the zone") {
    // The cost map (spec 03 §6.2, confirmed: binary): the target costs 1;
    // entering a square costs 1 + (30 - round(distance to the centre)) + 1000
    // within the zone.
    const auto map = movement::detail::centreCostMap(Sector{12, 6}, 2);
    CHECK(map[6 * kSystemSize + 12] == 1);
    CHECK(map[6 * kSystemSize + 11] == 1 + 1 + (30 - 5));        // (5, 0) from the centre
    CHECK(map[6 * kSystemSize + 8] > 1000);                        // inside the zone: reached, never spread
    CHECK(map[6 * kSystemSize + 7] == -1);                        // behind the zone: nothing spreads there
    // The cheapest of the nine squares, the lowest sector number on a tie.
    CHECK(movement::detail::centreStep(map, Sector{11, 6}) == Sector{12, 6});
    CHECK_FALSE(movement::detail::centreStep(map, Sector{6, 6}).has_value());

    World w;
    const SystemId a = w.system("A");
    w.s.galaxy.system(a).abilities.push_back(ab(AbilityKind::SystemDestructiveCenter, 100));
    w.s.galaxy.system(a).abilities.push_back(ab(AbilityKind::SystemMovementTowardsCenter, 2));
    w.exploreAll(kA);
    // The route estimate follows the map as well: around the zone, never through it.
    const auto path = movement::findPath(w.rules(), w.s, kA, at(a, 0, 6), at(a, 12, 6));
    REQUIRE(path);
    for (const Location& l : path->steps) CHECK(std::max(std::abs(l.sector.x - 6), std::abs(l.sector.y - 6)) > 2);
    CHECK(path->steps.back() == at(a, 12, 6));
    const VehicleId ship = w.spawn(w.ship(kA, "Careful", 10), at(a, 0, 6));
    fuel(w, ship);
    w.order(ship, moveTo(a, 12, 6));
    CombatSpy spy;
    spy.fight = [](const GameState&, Location) { return false; };
    for (int turn = 0; turn < 3 && !w.v(ship).orders.empty(); ++turn) w.move(spy.hooks());
    CHECK(w.v(ship).location == at(a, 12, 6));
    for (const Location& l : spy.asked) CHECK(std::max(std::abs(l.sector.x - 6), std::abs(l.sector.y - 6)) > 2);
}

TEST_CASE("movement: training caps: ships land on V2, fleets and system sources keep their fraction, 50 at most") {
    ruleset::Ruleset rs = buildRuleset();
    for (auto [name, kind, v1, v2] : {std::tuple{"Mv Drill", AbilityKind::FleetTraining, 1, 10}, std::tuple{"Mv Gym", AbilityKind::ShipTraining, 3, 10},
                                      std::tuple{"Mv Great Hall", AbilityKind::ShipTrainingSystem, 100, 90}}) {
        ruleset::Facility f;
        f.name = name;
        f.abilities = {ab(kind, v1, v2)};
        rs.facilities.push_back(f);
    }
    rs.reindex();
    const Rules r{std::move(rs)};
    World w(r);
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    w.s.empire(kA).knowledge.explored[b.index()] = 1;
    w.colony(w.planet(a, {3, 3}), kA, 0, {"Mv Drill", "Mv Gym"});  // no population needed
    const VehicleId m1 = w.spawn(w.ship(kA, "M1", 1), at(a, 3, 3));
    const VehicleId m2 = w.spawn(w.ship(kA, "M2", 1), at(a, 3, 3));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Drill", {m1, m2}}).ok);
    Fleet& f = w.s.fleets.back();
    f.experience = 9;
    f.experienceTenths = 5;
    w.v(m1).experience = 8;
    w.v(m1).experienceTenths = 5;
    w.upkeep();
    // A fleet at 9.5 with V1 1 and V2 10: + (10 - truncate(9.5)) = 10.5.
    CHECK(w.s.fleets.back().experience == 10);
    CHECK(w.s.fleets.back().experienceTenths == 5);
    // A ship at 8.5 with V1 3 and V2 10 lands on 10 exactly.
    CHECK(w.v(m1).experience == 10);
    CHECK(w.v(m1).experienceTenths == 0);
    // The system-wide source: V2 - truncate(experience), and never above 50 for a ship.
    w.colony(w.planet(b, {1, 1}), kA, 0, {"Mv Great Hall"});
    const VehicleId vet = w.spawn(w.ship(kA, "Vet", 1), at(b, 5, 5));
    w.v(vet).experience = 40;
    w.v(vet).experienceTenths = 5;
    w.upkeep();
    CHECK(w.v(vet).experience == 50);
    CHECK(w.v(vet).experienceTenths == 0);
}

TEST_CASE("movement: the supply step: a cloak at 0 drops before the depot; solar collectors after drones are lost") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    w.colony(w.planet(a, {2, 2}), kA, 0, {"Test Depot"});
    const VehicleId shade = w.spawn(w.ship(kA, "Shade", 2, {"Mv Cloak"}), at(a, 2, 2));
    w.v(shade).status = VehicleStatus::Cloaked;
    w.v(shade).supply = 10;  // less than the cloak's 20
    w.upkeep();
    CHECK(w.v(shade).status == VehicleStatus::Normal);  // decloaked at 0, before the refill
    CHECK(w.v(shade).supply == 100);

    // A drone at 0 after its upkeep is lost before its collector could help.
    const VehicleId drone = w.spawn(w.design(kA, "Sunny", "Test Drone Hull", {"Mv Engine", "Mv Drone Tank", "Mv Drone Panel"}), at(a, 8, 8));
    w.v(drone).supply = 200;
    w.object(a, ObjectKind::Star, {6, 6});
    w.upkeep();
    CHECK(w.s.vehicle(drone) == nullptr);
    // A ship's collector fills it in the training step.
    const VehicleId panel = w.spawn(w.ship(kA, "Panel", 2, {"Test Solar Panel"}), at(a, 9, 9));
    w.v(panel).supply = 10;
    w.upkeep();
    CHECK(w.v(panel).supply == 60);
    (void)r;
}

TEST_CASE("movement: the drift target is drawn every turn; hazards hit unit groups whole, 20 draws at most") {
    World w;
    const SystemId a = w.system("A");
    Rng expected = w.s.rng;
    expected.below(145);
    w.hazards();  // no system drifts
    CHECK(w.s.rng.next() == expected.next());

    // 20 draws of whole units; the leftover is lost (spec 03 §6.2, spec 04 §9.4).
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const VehicleId swarm = w.spawn(fighter, at(a, 4, 4));
    w.v(swarm).count = 30;
    const int64_t hp = combat::detail::unitHitPoints(w.rules(), w.s.design(fighter));
    const int lostBefore = w.s.design(fighter).lost;
    movement::damageUnitGroup(w.rules(), w.s, w.v(swarm), hp * 25 + hp / 2, w.s.rng);
    CHECK(w.v(swarm).count == 10);  // one unit per draw, 20 draws
    CHECK(w.s.design(fighter).lost == lostBefore + 20);
    movement::damageUnitGroup(w.rules(), w.s, w.v(swarm), hp - 1, w.s.rng);
    CHECK(w.v(swarm).count == 10);  // too little for a unit: lost
}

TEST_CASE("movement: repair counts unpopulated colonies and unit groups") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    w.colony(w.planet(a, {3, 3}), kA, 0, {"Test Repair Yard"});
    const int64_t colony = movement::repairPoolAt(r, w.s, kA, at(a, 3, 3));
    CHECK(colony > 0);
    const DesignId sat = w.design(kA, "Mender", "Test Satellite Hull", {"Mv Repair Drone Bay"});
    const VehicleId group = w.spawn(sat, at(a, 3, 3));
    w.v(group).count = 3;
    CHECK(movement::repairPoolAt(r, w.s, kA, at(a, 3, 3)) == colony + 3 * 2);
}

TEST_CASE("movement: a human colonizing in a turn-based game picks the colony type") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId target = w.planet(a, {4, 4});
    w.s.options.simultaneous = false;
    const VehicleId ship = w.spawn(w.ship(kA, "Settler", 2, {"Test Rock Pod"}), at(a, 4, 4));
    w.v(ship).cargo.population.push_back({kA, 1});
    w.order(ship, mk(OrderKind::Colonize, {}, target));
    w.colonize();
    REQUIRE(w.s.colony(target));
    REQUIRE(w.s.empire(kA).colonyTypeChoices == std::vector<ObjectId>{target});
    REQUIRE(apply(r, w.s, kA, cmd::SetColonyType{target, "Mining"}).ok);
    CHECK(w.s.colony(target)->colonyType == "Mining");
    CHECK(w.s.empire(kA).colonyTypeChoices.empty());
    // With the option off (or in a simultaneous game) the type is chosen automatically.
    REQUIRE(apply(r, w.s, kA, cmd::SetEmpireOptions{.chooseColonyType = false}).ok);
    const ObjectId other = w.planet(a, {6, 6});
    const VehicleId second = w.spawn(w.ship(kA, "Settler", 2, {"Test Rock Pod"}), at(a, 6, 6));
    w.v(second).cargo.population.push_back({kA, 1});
    w.order(second, mk(OrderKind::Colonize, {}, other));
    w.colonize();
    REQUIRE(w.s.colony(other));
    CHECK(w.s.empire(kA).colonyTypeChoices.empty());
}

// ---- Determinism --------------------------------------------------------------------------------------

namespace {

std::string playScenario() {
    World w;
    const SystemId a = w.system("A", 0, 0), b = w.system("B", 8, 0), c = w.system("C", 0, 8), d = w.system("D", 8, 8);
    w.link(a, {12, 6}, b, {0, 6});
    w.link(a, {6, 12}, c, {6, 0});
    w.link(b, {6, 12}, d, {6, 0});
    w.s.galaxy.system(d).abilities.push_back(ab(AbilityKind::SystemMovementRandom, 2));
    const ObjectId storm = w.object(c, ObjectKind::Storm, {6, 3});
    w.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorDamage, 3));
    const ObjectId home = w.planet(a, {6, 6});
    w.colony(home, kA, 1000, {"Test Depot", "Test Repair Yard"});
    const ObjectId target = w.planet(b, {3, 3});
    w.s.empire(kA).knowledge.explored[a.index()] = 1;
    w.s.empire(kB).knowledge.explored[a.index()] = 1;
    const DesignId scout = w.ship(kA, "Scout", 3, {"Test Quantum Reactor"});
    for (int i = 0; i < 3; ++i) {
        const VehicleId id = w.spawn(scout, at(a, 6, 6));
        w.order(id, mk(OrderKind::Explore));
        w.order(id, mk(OrderKind::Explore));
    }
    const VehicleId settler = w.spawn(w.ship(kA, "Settler", 2, {"Test Rock Pod"}), at(a, 6, 6));
    w.order(settler, mk(OrderKind::Warp, {}, w.s.galaxy.system(a).objects[0]));
    w.order(settler, mk(OrderKind::Colonize, {}, target));
    const VehicleId rover = w.spawn(w.ship(kB, "Rover", 4, {"Test Quantum Reactor", "Mv Armor"}), at(a, 0, 0));
    w.order(rover, moveTo(a, 12, 12), true);
    w.order(rover, moveTo(a, 0, 12), true);
    for (int turn = 0; turn < 8; ++turn) w.fullTurn();

    std::string out = std::format("turn {} rng {} {} {} {}\n", w.s.turn, w.s.rng.rawState()[0], w.s.rng.rawState()[1], w.s.rng.rawState()[2],
                                  w.s.rng.rawState()[3]);
    for (const Vehicle& v : w.s.vehicles)
        out += std::format("v{} {} {},{} s{} m{} d{} o{} c{}\n", v.id.value, v.location.system.value, v.location.sector.x, v.location.sector.y,
                           v.supply, v.movement, totalDamage(v), v.orders.size(), v.count);
    for (const Empire& e : w.s.empires) {
        for (auto x : e.knowledge.explored) out += std::to_string(x);
        out += "|";
        for (auto x : e.knowledge.knownWarpLink) out += std::to_string(x);
        out += "|";
        for (VehicleId id : e.knowledge.visibleVehicles) out += std::to_string(id.value) + ",";
        out += "\n";
    }
    for (const auto& col : w.s.colonies)
        if (col) out += std::format("colony {} {} {}\n", col->planet.value, col->owner.value, col->totalPopulation());
    return out;
}

} // namespace

TEST_CASE("movement: the same state and orders give the same result") {
    const std::string first = playScenario();
    const std::string second = playScenario();
    CHECK(first == second);
    CHECK(first.find("colony") != std::string::npos);
}

// ---- Orders as given, ad-hoc groups, greedy steps and the Ship Orders options (spec 03 §6.2, §6.4, §8) ----

TEST_CASE("orders: composite orders are expanded into simple ones when they are given") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
    w.exploreAll(kA);
    const ObjectId home = w.planet(a, {2, 6});
    const ObjectId target = w.planet(a, {5, 6});
    w.colony(home, kA, 1000, {"Test Depot"});

    // Colonize: Load Cargo (population) where it is given, Move To the planet, Colonize.
    const VehicleId ship = w.spawn(w.ship(kA, "Settler", 3, {"Test Rock Pod"}), at(a, 2, 6));
    fuel(w, ship);
    w.give(ship, {mk(OrderKind::Colonize, {}, target)});
    {
        const auto& o = w.v(ship).orders;
        REQUIRE(o.size() == 3);
        CHECK(o[0].kind == OrderKind::LoadCargo);
        CHECK_FALSE(o[0].design.valid());
        CHECK(o[0].location == at(a, 2, 6));
        CHECK(o[0].amount == -1);
        CHECK(o[1] == moveTo(a, 5, 6));
        CHECK(o[2].kind == OrderKind::Colonize);
        CHECK(o[2].object == target);
        CHECK(o[2].amount == kColonizeExpanded);
    }
    // Orders added later leave the earlier ones alone: a Warp from the planet's sector.
    w.give(ship, {mk(OrderKind::Warp, {}, ab)});
    REQUIRE(w.v(ship).orders.size() == 5);
    CHECK(w.v(ship).orders[3] == moveTo(a, 12, 6));
    CHECK(w.v(ship).orders[4].kind == OrderKind::Warp);
    // Giving the same list again changes nothing.
    w.give(ship, {});
    CHECK(w.v(ship).orders.size() == 5);
    w.v(ship).orders.resize(3);
    // Loading takes the first action; the Move To then goes on (spec 03 §6.3 step 4).
    w.move();
    CHECK(w.v(ship).location == at(a, 4, 6));
    CHECK(w.v(ship).cargo.totalPopulation() == 2);
    CHECK(w.v(ship).orders.size() == 2);
    // It arrives on its first acting day and colonizes on the next, during the
    // movement phases (spec 05 §8 step 5).
    w.move();
    CHECK(w.s.vehicle(ship) == nullptr);
    REQUIRE(w.s.colony(target));
    CHECK(w.s.colony(target)->owner == kA);

    // A ship already carrying colonists gets no Load Cargo.
    const ObjectId other = w.planet(a, {9, 9});
    const VehicleId loaded = w.spawn(w.ship(kA, "Loaded", 3, {"Test Rock Pod"}), at(a, 0, 0));
    w.v(loaded).cargo.population.push_back({kA, 1});
    w.give(loaded, {mk(OrderKind::Colonize, {}, other)});
    REQUIRE(w.v(loaded).orders.size() == 2);
    CHECK(w.v(loaded).orders[0] == moveTo(a, 9, 9));

    // Cargo orders for another sector: Move To there first.
    const VehicleId hauler = w.spawn(w.ship(kA, "Hauler", 3), at(a, 0, 0));
    w.give(hauler, {mk(OrderKind::LoadCargo, at(a, 2, 6), {}, {}, {}, -1)});
    REQUIRE(w.v(hauler).orders.size() == 2);
    CHECK(w.v(hauler).orders[0] == moveTo(a, 2, 6));
    CHECK(w.v(hauler).orders[1].kind == OrderKind::LoadCargo);

    // Resupply: Move To the nearest depot; already at one, nothing is added.
    const VehicleId tanker = w.spawn(w.ship(kA, "Tanker", 3), at(a, 6, 12));
    w.give(tanker, {mk(OrderKind::Resupply)});
    REQUIRE(w.v(tanker).orders.size() == 1);
    CHECK(w.v(tanker).orders[0] == moveTo(a, 2, 6));
    const VehicleId docked = w.spawn(w.ship(kA, "Docked", 3), at(a, 2, 6));
    w.give(docked, {mk(OrderKind::Resupply)});
    CHECK(w.v(docked).orders.empty());

    // Fleets: expanded once from the fleet's location, into every member's copy.
    const VehicleId f1 = w.spawn(w.ship(kA, "Wing", 3), at(a, 12, 0));
    const VehicleId f2 = w.spawn(w.ship(kA, "Wing", 3), at(a, 12, 0));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Wing", {f1, f2}}).ok);
    const FleetId fid = w.v(f1).fleet;
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{{}, fid, {mk(OrderKind::Warp, {}, ab)}, false}).ok);
    REQUIRE(w.v(f1).orders.size() == 2);
    CHECK(w.v(f1).orders[0] == moveTo(a, 12, 6));
    CHECK(w.v(f2).orders == w.v(f1).orders);
    (void)ba;
}

TEST_CASE("movement: ad-hoc groups form at every execution: a computer player's vehicles with one head order, a human's drones") {
    // Spec 03 §8 (confirmed: binary). A human player's ships never group.
    World h;
    const SystemId ha = h.system("A");
    const VehicleId hFast = h.spawn(h.ship(kA, "Fast", 4), at(ha, 0, 6));
    const VehicleId hSlow = h.spawn(h.ship(kA, "Slow", 2), at(ha, 0, 6));
    for (VehicleId id : {hFast, hSlow}) {
        fuel(h, id);
        h.order(id, moveTo(ha, 4, 6));
    }
    h.move();
    CHECK(h.v(hFast).location == at(ha, 3, 6));
    CHECK(h.v(hSlow).location == at(ha, 1, 6));

    // A computer player's group acts when its first member is due and carries
    // the others along: the slow ship moves at the fast one's pace.
    World w;
    const SystemId a = w.system("A");
    w.s.empire(kA).kind = PlayerKind::Computer;
    const VehicleId fast = w.spawn(w.ship(kA, "Fast", 4), at(a, 0, 6));
    const VehicleId slow = w.spawn(w.ship(kA, "Slow", 2), at(a, 0, 6));
    const VehicleId apart = w.spawn(w.ship(kA, "Apart", 2), at(a, 0, 7));
    for (VehicleId id : {fast, slow, apart}) fuel(w, id);
    w.order(fast, moveTo(a, 4, 6));
    w.order(fast, moveTo(a, 4, 0));
    w.order(slow, moveTo(a, 4, 6));
    w.order(slow, moveTo(a, 4, 12));
    w.order(apart, moveTo(a, 4, 6));  // same order, another sector: on its own
    w.move();
    CHECK(w.v(fast).location == at(a, 3, 6));
    CHECK(w.v(slow).location == at(a, 3, 6));
    CHECK(w.v(apart).location == at(a, 1, 6));  // one diagonal step on day 16
    // The shared order is done for both on day 8; their next orders differ, so
    // each goes on alone from there, the same day's chain included.
    w.move();
    CHECK(w.v(fast).location == at(a, 4, 4));
    CHECK(w.v(slow).location == at(a, 4, 7));
    REQUIRE(w.v(fast).orders.size() == 1);
    REQUIRE(w.v(slow).orders.size() == 1);

    // A human player's drone groups outside fleets gather those with the same head order.
    World d;
    const SystemId da = d.system("A");
    const DesignId three = d.design(kA, "Dart", "Test Drone Hull", {"Mv Engine", "Mv Engine", "Mv Engine", "Test Warhead", "Mv Drone Tank"});
    const DesignId two = d.design(kA, "Bolt", "Test Drone Hull", {"Mv Engine", "Mv Engine", "Test Warhead", "Mv Drone Tank"});
    const VehicleId dart = d.spawn(three, at(da, 0, 0));   // days 11 and 21
    const VehicleId bolt = d.spawn(two, at(da, 0, 0));     // day 16
    d.order(dart, moveTo(da, 6, 0));
    d.order(bolt, moveTo(da, 6, 0));
    d.move();
    CHECK(d.v(dart).location == at(da, 3, 0));
    CHECK(d.v(bolt).location == at(da, 3, 0));
}

TEST_CASE("movement: in-system steps are greedy and re-chosen around hazards and hostiles") {
    // A visible hostile's square is stepped around, unless it is where the ship goes.
    World w;
    const SystemId a = w.system("A");
    const VehicleId runner = w.spawn(w.ship(kA, "Runner", 4), at(a, 0, 6));
    w.spawn(w.ship(kB, "Picket", 1), at(a, 1, 6));
    fuel(w, runner);
    w.order(runner, moveTo(a, 6, 6));
    CombatSpy spy;
    spy.fight = hostilesMeet;
    w.move(spy.hooks());
    CHECK(w.v(runner).location == at(a, 3, 6));
    CHECK(spy.fought.empty());
    CHECK(std::find(spy.asked.begin(), spy.asked.end(), at(a, 1, 6)) == spy.asked.end());

    // A storm square too; without anything in the way the step is diagonal first.
    World st;
    const SystemId sa = st.system("A");
    const ObjectId storm = st.object(sa, ObjectKind::Storm, {1, 1});
    st.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorDamage, 5));
    const VehicleId sailor = st.spawn(st.ship(kA, "Sailor", 2), at(sa, 0, 0));
    fuel(st, sailor);
    st.order(sailor, moveTo(sa, 4, 4));
    st.move();
    CHECK((st.v(sailor).location == at(sa, 1, 0) || st.v(sailor).location == at(sa, 0, 1)));
    CHECK(totalDamage(st.v(sailor)) == 0);
    const VehicleId straight = st.spawn(st.ship(kA, "Straight", 2), at(sa, 5, 5));
    fuel(st, straight);
    st.order(straight, moveTo(sa, 9, 9));
    st.move();
    CHECK(st.v(straight).location == at(sa, 6, 6));

    // With every square toward the target to be avoided, 10 tries fail: no step, and the order fails.
    World m;
    const SystemId ma = m.system("A");
    m.s.empire(kA).taggedMinefields = {at(ma, 0, 1), at(ma, 1, 1)};
    const VehicleId stuck = m.spawn(m.ship(kA, "Stuck", 3), at(ma, 0, 0));
    fuel(m, stuck);
    m.order(stuck, moveTo(ma, 0, 12));
    m.move();
    CHECK(m.v(stuck).location == at(ma, 0, 0));
    CHECK(m.v(stuck).orders.empty());
    CHECK(m.logged(kA, "blocked"));
}

TEST_CASE("movement: the Ship Orders options clear orders after a warp into another empire's system") {
    const Rules& r = mvtest::rules();
    auto run = [&](EncounterClear options, Treaty treaty, bool ownerHasColony, bool hidden = false, bool drone = false) {
        World w;
        const SystemId a = w.system("A"), b = w.system("B", 10, 0);
        const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
        w.exploreAll(kA);
        if (ownerHasColony) w.colony(w.planet(b, {8, 8}), kB, 1000);
        if (hidden) {
            const ObjectId storm = w.object(b, ObjectKind::Storm, {8, 8});
            w.s.galaxy.object(storm).abilities = {mvtest::ab(AbilityKind::SectorSightObscuration, 5)};
        }
        w.setTreaty(kA, kB, treaty);
        w.setTreaty(kB, kA, treaty);
        REQUIRE(apply(r, w.s, kA, cmd::SetEncounterOptions{options}).ok);
        // Speed 4: a step on day 8, the jump on day 16, a step on day 23.
        const DesignId d = drone ? w.design(kA, "Drone", "Test Drone Hull", {"Mv Engine", "Mv Engine", "Mv Engine", "Mv Engine", "Mv Drone Tank"})
                                 : w.ship(kA, "Scout", 4);
        const VehicleId ship = w.spawn(d, at(a, 11, 6));
        fuel(w, ship);
        w.order(ship, mk(OrderKind::Warp, {}, ab));
        w.order(ship, moveTo(b, 3, 6));
        w.move();
        (void)ba;
        return std::pair{w.v(ship).location, w.v(ship).orders.size()};
    };
    // Off: the ship warps and goes on. On for meeting an enemy is the default (spec 03 §6.4).
    CHECK(World{}.s.empire(kA).clearOrdersOnEncounter == EncounterClear::Enemy);
    CHECK(run(EncounterClear::Never, Treaty::War, true).first == Location{SystemId{1u}, Sector{1, 6}});
    // Meeting an enemy: the jump does not fail, the Warp just ends and the list is cleared.
    const auto enemy = run(EncounterClear::Enemy, Treaty::War, true);
    CHECK(enemy.first == Location{SystemId{1u}, Sector{0, 6}});
    CHECK(enemy.second == 0);
    // Nobody there: nothing happens.
    CHECK(run(EncounterClear::Enemy, Treaty::War, false).first == Location{SystemId{1u}, Sector{1, 6}});
    // A friend is no enemy, but counts for "any empire".
    CHECK(run(EncounterClear::Enemy, Treaty::NonAggression, true).first == Location{SystemId{1u}, Sector{1, 6}});
    const auto any = run(EncounterClear::Any, Treaty::NonAggression, true);
    CHECK(any.first == Location{SystemId{1u}, Sector{0, 6}});
    CHECK(any.second == 0);
    // Only objects the owner sees count: a colony hidden by a storm does not.
    CHECK(run(EncounterClear::Enemy, Treaty::War, true, true).first == Location{SystemId{1u}, Sector{1, 6}});
    // Groups made only of drones are exempt.
    CHECK(run(EncounterClear::Enemy, Treaty::War, true, false, true).first == Location{SystemId{1u}, Sector{1, 6}});
    // Unknown values are refused.
    World w;
    CHECK_FALSE(apply(r, w.s, kA, cmd::SetEncounterOptions{static_cast<EncounterClear>(3)}).ok);
    CHECK(apply(r, w.s, kA, cmd::SetEncounterOptions{EncounterClear::Any}).ok);
    CHECK(w.s.empire(kA).clearOrdersOnEncounter == EncounterClear::Any);
}
