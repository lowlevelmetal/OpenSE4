// Movement, orders, fleets, supply, cargo, units, colonization, repair,
// hazards and stellar manipulation (docs/spec/03, spec 01 §7-9, spec 05 §9.3).

#include "movement_fixture.hpp"

#include "game/ai.hpp"
#include "game/commands.hpp"
#include "game/xmath.hpp"

#include <doctest/doctest.h>

#include <format>

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
    CHECK(movement::kDayCounterMode == DayCounterMode::Exact);  // the spec's recommendation until observed
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

TEST_CASE("movement: etaTurns uses the speed and the known route") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    w.exploreAll(kA);
    const VehicleId ship = w.spawn(w.ship(kA, "Runner", 3), at(a, 0, 6));
    CHECK(movement::etaTurns(r, w.s, w.v(ship), at(a, 7, 6)) == 3);
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
    CHECK(w.v(ship).location == at(a, 3, 6));
    CHECK(w.v(ship).movement == 0);
    CHECK(w.v(ship).supply == 1'000'000 - 3 * 30);  // 3 engines x 10 per step
    CHECK(w.v(ship).orders.size() == 1);
    for (int i = 0; i < 3; ++i) w.move();
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
    CHECK(movement::etaTurns(r, w.s, w.v(ship), at(a, 6, 6)) == 1 + 2);
    w.move();
    CHECK(w.v(ship).location == at(a, 0, 6));
    CHECK(w.v(ship).movement == 0);
    CHECK(w.v(ship).orders.size() == 1);
    ++w.s.turn;
    w.move();
    CHECK(w.v(ship).location == at(a, 3, 6));

    // A held member holds its whole fleet.
    const VehicleId free = w.spawn(w.ship(kA, "Free", 3, {"Test Quantum Reactor"}), at(a, 9, 9));
    const VehicleId stuck = w.spawn(w.ship(kA, "Stuck", 3, {"Test Quantum Reactor"}), at(a, 9, 9));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Anchor", {free, stuck}}).ok);
    w.s.fleets.back().orders = {moveTo(a, 12, 12)};
    w.v(stuck).immobileUntil = w.s.turn + 1;
    w.move();
    CHECK(w.v(free).location == at(a, 9, 9));
    CHECK(w.v(stuck).location == at(a, 9, 9));
    CHECK(w.s.fleets.back().orders.size() == 1);
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
    CHECK(w.v(ship).location == at(b, 0, 6));
    CHECK(w.v(ship).orders.empty());
    CHECK(w.v(ship).movement == 1);
    CHECK(sight::knowsWarpLink(w.s, kA, wa));
    CHECK(sight::knowsWarpLink(w.s, kA, wb));
    CHECK(w.s.empire(kA).hasExplored(b));
    CHECK_FALSE(w.s.empire(kB).hasExplored(b));
}

TEST_CASE("movement: explore picks the nearest unexplored warp point and skips claimed ones") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 10, 0), c = w.system("C", 0, 10);
    w.link(a, {8, 6}, b, {0, 6});
    w.link(a, {6, 8}, c, {0, 6});
    w.s.empire(kA).knowledge.explored[a.index()] = 1;
    const DesignId scout = w.ship(kA, "Scout", 3);
    const VehicleId s1 = w.spawn(scout, at(a, 6, 6));
    const VehicleId s2 = w.spawn(scout, at(a, 6, 6));
    for (VehicleId id : {s1, s2}) {
        fuel(w, id);
        w.order(id, mk(OrderKind::Explore));
    }
    w.move();
    CHECK(w.v(s1).location == at(b, 0, 6));
    CHECK(w.v(s2).location == at(c, 0, 6));
    CHECK(w.v(s1).orders.empty());
    CHECK(w.v(s2).orders.empty());
    CHECK(w.s.empire(kA).hasExplored(b));
    CHECK(w.s.empire(kA).hasExplored(c));

    // Nothing left: the order ends with a note.
    const VehicleId s3 = w.spawn(scout, at(a, 6, 6));
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
    const VehicleId slow = w.spawn(w.ship(kA, "Slow", 2, {"Test Quantum Reactor"}), at(a, 0, 6));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pack", {fast, slow}}).ok);
    const FleetId fleet = w.s.fleets.back().id;
    w.s.fleet(fleet)->orders = {moveTo(a, 10, 6)};
    CHECK(movement::fleetSpeed(r, w.s, *w.s.fleet(fleet)) == 2);
    CHECK(movement::etaTurns(r, w.s, w.v(fast), at(a, 10, 6)) == 5);
    w.move();
    CHECK(w.v(fast).location == at(a, 2, 6));
    CHECK(w.v(slow).location == at(a, 2, 6));
    CHECK(w.v(fast).movement == 0);  // every member starts with the fleet's lowest maximum (spec 03 §6.3)
    for (int i = 0; i < 4; ++i) w.move();
    CHECK(w.v(fast).location == at(a, 10, 6));
    CHECK(w.v(slow).location == at(a, 10, 6));
    CHECK(w.s.fleet(fleet)->orders.empty());

    // Without fleet orders, a member with its own orders moves alone.
    w.order(fast, moveTo(a, 12, 6));
    w.move();
    CHECK(w.v(fast).location == at(a, 12, 6));
    CHECK(w.v(slow).location == at(a, 10, 6));
}

TEST_CASE("movement: fleet supply is pooled in equal shares at the end of the turn") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId full = w.spawn(w.ship(kA, "Full", 1), at(a, 0, 6));
    const VehicleId empty = w.spawn(w.ship(kA, "Empty", 1), at(a, 0, 6));
    w.v(empty).supply = 0;
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {full, empty}}).ok);
    w.s.fleets.back().orders = {moveTo(a, 5, 6)};
    w.move();
    CHECK(w.v(full).location == at(a, 1, 6));
    CHECK(w.v(full).supply == 90);  // no pooling while moving
    CHECK(w.v(empty).supply == 0);
    w.upkeep();
    CHECK(w.v(full).supply == 45);  // 90 shared by two
    CHECK(w.v(empty).supply == 45);

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

TEST_CASE("movement: supply is spent per step; without supply a ship makes one move per turn") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId ship = w.spawn(w.ship(kA, "Thirsty", 2), at(a, 0, 6));
    CHECK(movement::moveSupplyCost(r, w.s, w.v(ship)) == 20);
    w.v(ship).supply = 50;
    w.order(ship, moveTo(a, 12, 6));
    w.move();
    CHECK(w.v(ship).location == at(a, 2, 6));
    CHECK(w.v(ship).supply == 10);
    w.move();  // the first step empties the tanks: movement is capped at 1 for the turn
    CHECK(w.v(ship).location == at(a, 3, 6));
    CHECK(w.v(ship).supply == 0);
    w.move();
    CHECK(w.v(ship).location == at(a, 4, 6));
    CHECK(vehicleMaxMovement(r, w.s, w.v(ship)) == 1);

    // Racial Supply Cost -50 %.
    w.s.empire(kA).race.traits.push_back(traitIndex(r, "Mv Frugal"));
    CHECK(movement::moveSupplyCost(r, w.s, w.v(ship)) == 10);

    // A quantum reactor never runs down: supply is held at the unlimited marker (spec 03 §7).
    const VehicleId q = w.spawn(w.ship(kA, "Reactor", 2, {"Test Quantum Reactor"}), at(a, 0, 0));
    CHECK(movement::moveSupplyCost(r, w.s, w.v(q)) == 0);
    CHECK(w.v(q).supply == kUnlimitedSupply);
    w.order(q, moveTo(a, 0, 12));
    w.move();
    CHECK(w.v(q).location == at(a, 0, 2));
    CHECK(w.v(q).supply == kUnlimitedSupply);
    w.upkeep();
    CHECK(w.v(q).supply == kUnlimitedSupply);
    // Losing the reactor brings it back to a normal value on the next move.
    w.v(q).damage[6] = 1000;
    w.v(q).orders = {moveTo(a, 0, 3)};
    w.move();
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
    CHECK(w.v(ship).location == at(a, 3, 6));
    CHECK(w.v(ship).supply == 100);  // would be 10 without the depot
    CHECK(w.v(ship).orders.empty());

    const VehicleId passer = w.spawn(w.ship(kA, "Passer", 3), at(a, 0, 6));
    w.order(passer, moveTo(a, 6, 6));
    w.move();
    CHECK(w.v(passer).location == at(a, 3, 6));
    CHECK(w.v(passer).supply == 100);

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
    const VehicleId raider = w.spawn(w.ship(kA, "Raider", 3), at(a, 0, 6));
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
    const DesignId d = t.ship(kA, "Twin", 3);
    for (int i = 0; i < 2; ++i) {
        const VehicleId id = t.spawn(d, at(ta, 0, 0));
        fuel(t, id);
        t.order(id, moveTo(ta, 3, 0));
    }
    CombatSpy twin;
    t.move(twin.hooks());
    CHECK(twin.asked.size() == 3);
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
    const VehicleId runner = w.spawn(w.ship(kA, "Runner", 3), at(a, 0, 6));
    w.spawn(w.ship(kB, "Picket", 1), at(a, 1, 6));
    fuel(w, runner);
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
    const VehicleId friendly = p.spawn(p.ship(kA, "Runner", 3), at(pa, 0, 6));
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
    const VehicleId sweeper = w.spawn(w.ship(kA, "Sweeper", 3, {"Mv Sweeper"}), at(a, 0, 6));
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
    const VehicleId runner = m.spawn(m.ship(kA, "Runner", 3), at(ma, 0, 6));
    fuel(m, runner);
    m.order(runner, moveTo(ma, 3, 6));
    CombatSpy blast;
    blast.fight = [&](const GameState&, Location l) { return l == at(ma, 1, 6); };
    m.move(blast.hooks());
    REQUIRE(blast.fought.size() == 1);
    CHECK(m.v(runner).location == at(ma, 3, 6));
    (void)field;

    // A sweep order sweeps in place.
    w.order(sweeper, moveTo(a, 1, 6));
    w.order(sweeper, mk(OrderKind::SweepMines));
    w.move();
    CHECK(w.v(mines).count == 2);
    CHECK(w.logged(kA, "swept 3 mines"));
    w.order(sweeper, mk(OrderKind::SweepMines));
    w.move();
    CHECK(w.s.vehicle(mines) == nullptr);

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
    const VehicleId watch = w.spawn(w.ship(kA, "Watch", 1), at(a, 10, 10));
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
    CHECK(w.v(intruder).location == at(a, 0, 6));
    // Only the Sentry order goes; the following orders run (spec 03 §8).
    REQUIRE(w.v(watch).orders.size() == 1);
    CHECK(w.v(watch).orders.front() == moveTo(a, 10, 9));
    CHECK(w.logged(kA, "enemy sighted"));

    // Low supplies end it too.
    const VehicleId thirsty = w.spawn(w.ship(kA, "Thirsty", 1), at(b, 3, 3));
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
    const VehicleId raider = f.spawn(f.ship(kB, "Raider", 1), at(fa, 6, 7));
    fuel(f, raider);
    f.order(raider, moveTo(fa, 6, 6));
    f.order(raider, moveTo(fa, 6, 8));
    f.setTreaty(kA, kB, Treaty::NonAggression);  // no alarm from the raider's presence alone
    int battles = 0;
    movement::CombatHooks hooks{[](const Rules&, const GameState& gs, Location l) { return hostilesMeet(gs, l); },
                                [&](TurnContext& ctx, Location l, std::span<const VehicleId>) {
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
    const VehicleId post = c.spawn(c.ship(kA, "Post", 1), at(ca, 10, 10));
    fuel(c, post);
    c.order(post, mk(OrderKind::Sentry));
    const VehicleId ghost = c.spawn(c.ship(kB, "Ghost", 1, {"Mv Cloak"}), at(ca, 0, 0));
    c.v(ghost).status = VehicleStatus::Cloaked;
    c.move();
    CHECK(c.v(post).orders.size() == 1);
    (void)ab;
}

TEST_CASE("movement: attack pursues a moving target and stays until it is gone") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId target = w.spawn(w.ship(kB, "Prey", 1), at(a, 6, 0));
    const VehicleId hunter = w.spawn(w.ship(kA, "Hunter", 3), at(a, 0, 0));
    fuel(w, target);
    fuel(w, hunter);
    w.order(target, moveTo(a, 12, 0));
    w.order(hunter, mk(OrderKind::Attack, {}, {}, target));
    CombatSpy spy;
    spy.fight = hostilesMeet;
    for (int turn = 0; turn < 5 && spy.fought.empty(); ++turn) w.move(spy.hooks());
    REQUIRE_FALSE(spy.fought.empty());
    CHECK(spy.fought[0].second == at(a, 8, 0));
    // The pursuit goes on while the target lives (spec 03 §8): it follows the
    // target to its next sector and fights again there.
    CHECK(w.v(hunter).location == w.v(target).location);
    REQUIRE(w.v(hunter).orders.size() == 1);
    CHECK(w.v(hunter).orders.front().kind == OrderKind::Attack);

    // A target that cannot be seen is lost.
    World l;
    const SystemId la = l.system("A");
    const VehicleId ghost = l.spawn(l.ship(kB, "Ghost", 1, {"Mv Cloak"}), at(la, 6, 0));
    l.v(ghost).status = VehicleStatus::Cloaked;
    const VehicleId seeker = l.spawn(l.ship(kA, "Seeker", 3), at(la, 0, 0));
    l.order(seeker, mk(OrderKind::Attack, {}, {}, ghost));
    l.move();
    CHECK(l.v(seeker).orders.empty());
    CHECK(l.v(seeker).location == at(la, 0, 0));
    CHECK(l.logged(kA, "target lost"));

    // A cloaked attacker decloaks on reaching its target.
    const VehicleId sneaky = l.spawn(l.ship(kA, "Sneaky", 3, {"Mv Cloak"}), at(la, 0, 0));
    const VehicleId plain = l.spawn(l.ship(kB, "Plain", 1), at(la, 3, 3));
    l.v(sneaky).status = VehicleStatus::Cloaked;
    l.order(sneaky, mk(OrderKind::Attack, {}, {}, plain));
    l.move();
    CHECK(l.v(sneaky).location == at(la, 3, 3));
    CHECK(l.v(sneaky).status == VehicleStatus::Normal);
    CHECK(l.v(sneaky).orders.size() == 1);
}

TEST_CASE("movement: repeat orders cycle; a repeating list that never moves idles") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId patrol = w.spawn(w.ship(kA, "Patrol", 3), at(a, 0, 6));
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
    w.order(hauler, mk(OrderKind::LoadCargo, {}, {}, {}, {}, -1));
    w.order(hauler, mk(OrderKind::DropCargo, at(a, 8, 6), {}, {}, {}, -1));
    w.move();
    CHECK(w.v(hauler).cargo.totalPopulation() == 10);  // 50 kT at 5 kT per million
    CHECK(w.s.colony(home)->totalPopulation() == 990);
    CHECK(w.v(hauler).location == at(a, 4, 6));  // loading took the first action of the turn
    w.move();
    w.move();  // six steps, then the drop on the next action
    CHECK(w.v(hauler).location == at(a, 8, 6));
    CHECK(w.v(hauler).cargo.empty());
    CHECK(w.s.colony(outpost)->totalPopulation() == 110);
    CHECK(w.v(hauler).orders.empty());

    const VehicleId carrier = w.spawn(w.ship(kA, "Carrier", 3, {"Mv Fighter Bay"}), at(a, 2, 6));
    fuel(w, carrier);
    w.order(carrier, mk(OrderKind::LoadCargo, {}, {}, {}, fighter, 3));
    w.order(carrier, mk(OrderKind::DropCargo, at(a, 8, 6), {}, {}, fighter, -1));
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
    CHECK(w.s.colony(enemy)->cargo.unitCount(troop) == 2);
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
    // Recovery has no per-turn limit: cargo space is the only limit (spec 03 §12).
    w.order(carrier, mk(OrderKind::RecoverUnits, {}, {}, {}, fighter, 4));
    w.move();
    CHECK(group()->count == 1);
    CHECK(w.v(carrier).cargo.unitCount(fighter) == 4);
    // Recovering from a named group leaves other groups alone.
    const VehicleId other = w.spawn(fighter, at(a, 5, 5));
    w.order(carrier, mk(OrderKind::RecoverUnits, {}, {}, other, fighter, -1));
    w.move();
    CHECK(w.s.vehicle(other) == nullptr);
    CHECK(group()->count == 1);
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

    // Drones need a target; troops never go into space.
    const DesignId drone = w.design(kA, "Drone", "Test Drone Hull", {"Mv Engine", "Test Warhead", "Mv Drone Tank"});
    const DesignId troop = w.design(kA, "Troop", "Test Troop Hull", {"Test Troop Rifle"});
    const VehicleId rack = w.spawn(w.ship(kA, "Rack", 3, {"Mv Drone Rack"}), at(a, 9, 9));
    w.v(rack).cargo.units = {{drone, 2}, {troop, 2}};
    w.order(rack, mk(OrderKind::LaunchUnits, {}, {}, {}, drone, -1));
    w.move();
    CHECK(w.v(rack).cargo.unitCount(drone) == 2);
    const VehicleId prey = w.spawn(w.ship(kB, "Prey", 1), at(a, 12, 12));
    w.order(rack, mk(OrderKind::LaunchUnits, {}, {}, prey, drone, 2));
    w.order(rack, mk(OrderKind::LaunchUnits, {}, {}, {}, troop, -1));
    w.move();
    CHECK(w.v(rack).cargo.unitCount(drone) == 0);
    CHECK(w.v(rack).cargo.unitCount(troop) == 2);
    int drones = 0;
    for (const Vehicle& v : w.s.vehicles)
        if (v.design == drone) {
            ++drones;
            CHECK(v.count == 1);  // every drone is its own group
            CHECK(v.targetVehicle == prey);
        }
    CHECK(drones == 2);
}

TEST_CASE("movement: drones chase their target on their own") {
    World w;
    const SystemId a = w.system("A");
    const DesignId droneDesign = w.design(kA, "Drone", "Test Drone Hull", {"Mv Engine", "Mv Engine", "Mv Engine", "Test Warhead", "Mv Drone Tank"});
    const VehicleId prey = w.spawn(w.ship(kB, "Prey", 1), at(a, 6, 0));
    const VehicleId drone = w.spawn(droneDesign, at(a, 0, 0));
    w.v(drone).targetVehicle = prey;
    CombatSpy spy;
    spy.fight = hostilesMeet;
    w.move(spy.hooks());
    CHECK(w.v(drone).location == at(a, 3, 0));
    w.move(spy.hooks());
    CHECK(w.v(drone).location == at(a, 6, 0));
    REQUIRE(spy.fought.size() == 1);
    CHECK(spy.fought[0].second == at(a, 6, 0));
}

TEST_CASE("movement: fighters cannot use warp points") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    const auto [wa, wb] = w.link(a, {7, 6}, b, {0, 6});
    w.exploreAll(kA);
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const VehicleId group = w.spawn(fighter, at(a, 6, 6));
    w.order(group, moveTo(b, 1, 6));
    w.move();
    CHECK(w.v(group).orders.empty());
    CHECK(w.v(group).location == at(a, 6, 6));
    w.order(group, mk(OrderKind::Warp, {}, wa));
    w.move();
    CHECK(w.v(group).orders.empty());
    CHECK(w.logged(kA, "Fighters cannot use warp points"));
    w.order(group, moveTo(a, 7, 6));
    w.move();
    CHECK(w.v(group).location == at(a, 7, 6));
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
    const VehicleId ship = w.spawn(w.ship(kA, "Shade", 1, {"Mv Cloak"}), at(a, 1, 1));
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

    const VehicleId plain = w.spawn(w.ship(kA, "Plain", 1), at(a, 1, 1));
    w.order(plain, mk(OrderKind::Cloak));
    w.move();
    CHECK(w.v(plain).status == VehicleStatus::Normal);
    CHECK(w.logged(kA, "cloaking device"));
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
    // The use takes one of the 2 actions; the energy adds 4 more (a day each).
    CHECK(w.v(ship).location == at(a, 5, 6));
    CHECK_FALSE(entryIntact(r, w.s, w.v(ship), 6));
    CHECK(w.v(ship).supply == supplyBefore - 5 * 20);  // the use itself costs nothing
    w.move();
    CHECK(w.v(ship).location == at(a, 7, 6));

    const VehicleId tanker = w.spawn(w.ship(kA, "Reserve", 1, {"Mv Spare Tank"}), at(a, 3, 3));
    w.v(tanker).supply = 10;
    w.order(tanker, mk(OrderKind::UseComponent, {}, {}, {}, {}, 5));
    w.order(tanker, mk(OrderKind::UseComponent, {}, {}, {}, {}, 5));
    w.move();  // speed 1: one action a turn
    CHECK(w.v(tanker).supply == 70);
    CHECK_FALSE(entryIntact(r, w.s, w.v(tanker), 5));
    CHECK(w.v(tanker).orders.size() == 1);
    w.move();
    CHECK(w.v(tanker).orders.empty());
    CHECK(w.logged(kA, "No usable component"));

    // Emergency resupply is capped at the maximum and does nothing for unlimited supply.
    const VehicleId full = w.spawn(w.ship(kA, "Brimming", 1, {"Mv Spare Tank"}), at(a, 4, 4));
    w.v(full).supply = 90;
    w.order(full, mk(OrderKind::UseComponent, {}, {}, {}, {}, 5));
    w.move();
    CHECK(w.v(full).supply == 100);
}

TEST_CASE("movement: self-destruct is a one-shot component order") {
    World w;
    const SystemId a = w.system("A");
    const DesignId design = w.ship(kA, "Bomb", 1, {"Test Self Destruct"});
    const VehicleId bomb = w.spawn(design, at(a, 2, 2));
    w.order(bomb, mk(OrderKind::UseComponent, {}, {}, {}, {}, 5));
    w.move();
    CHECK(w.s.vehicle(bomb) == nullptr);
    CHECK(w.s.design(design).lost == 1);
    CHECK(w.logged(kA, "self-destructed"));
}

TEST_CASE("movement: fleet members still carry out their own orders that act in place") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId lead = w.spawn(w.ship(kA, "Lead", 3, {"Test Quantum Reactor"}), at(a, 0, 6));
    const VehicleId shade = w.spawn(w.ship(kA, "Shade", 3, {"Test Quantum Reactor", "Mv Cloak"}), at(a, 0, 6));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {lead, shade}}).ok);
    w.s.fleets.back().orders = {moveTo(a, 6, 6)};
    w.order(shade, mk(OrderKind::Cloak));
    w.order(shade, moveTo(a, 0, 0));  // needs travel: waits while the fleet moves it
    w.move();
    CHECK(w.v(shade).status == VehicleStatus::Cloaked);
    CHECK(w.v(shade).location == at(a, 3, 6));
    CHECK(w.v(lead).location == at(a, 3, 6));
    REQUIRE(w.v(shade).orders.size() == 1);
    CHECK(w.v(shade).orders[0].kind == OrderKind::MoveTo);
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
    CHECK(w.logged(kA, "Doomed 1 destroyed"));
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
        w.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorDamage, 3));
        w.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorDamage, 2));  // they add up
        const VehicleId stays = w.spawn(w.ship(kA, "Stays", 1, {"Mv Armor"}), at(a, 3, 6));
        const VehicleId runner = w.spawn(w.ship(kA, "Runner", 3, {"Mv Armor"}), at(a, 2, 6));
        fuel(w, runner);
        w.order(runner, moveTo(a, 3, 6));  // routes step around storms when they can: go into it
        w.order(runner, moveTo(a, 5, 6));
        w.s.rng.reseed(seed);
        w.move();
        CHECK(totalDamage(w.v(stays)) == 0);
        if (totalDamage(w.v(runner)) > 0) {
            ++hits;
            CHECK(totalDamage(w.v(runner)) == 5);
            CHECK(w.v(runner).damage[7] == 5);  // armor takes it first
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

TEST_CASE("movement: warp point turbulence hits half the transits and ends the move") {
    int hits = 0, misses = 0;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        World t;
        const SystemId ta = t.system("A"), tb = t.system("B", 5, 0);
        const auto [wa, wb] = t.link(ta, {7, 6}, tb, {0, 6});
        t.s.galaxy.object(wa).abilities.push_back(ab(AbilityKind::WarpPointTurbulence, 4));
        t.s.galaxy.object(wa).abilities.push_back(ab(AbilityKind::WarpPointTurbulence, 3));
        t.s.galaxy.object(wb).abilities.push_back(ab(AbilityKind::WarpPointTurbulence, 100));  // the far end does not count
        t.exploreAll(kA);
        const VehicleId jumper = t.spawn(t.ship(kA, "Jumper", 3, {"Mv Armor"}), at(ta, 7, 6));
        fuel(t, jumper);
        t.order(jumper, moveTo(tb, 2, 6));
        t.s.rng.reseed(seed);
        t.move();
        if (totalDamage(t.v(jumper)) > 0) {
            ++hits;
            CHECK(totalDamage(t.v(jumper)) == 7);
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

TEST_CASE("movement: colony ships load colonists, travel and found a colony") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {2, 6});
    const ObjectId target = w.planet(a, {5, 6});
    w.colony(home, kA, 1000);
    const VehicleId ship = w.spawn(w.ship(kA, "Settler", 3, {"Test Rock Pod"}), at(a, 2, 6));
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
    const VehicleId escort = w.spawn(w.ship(kA, "Escort", 3), at(a, 6, 6));
    const VehicleId settler = w.spawn(w.ship(kA, "Settler", 3, {"Test Rock Pod"}), at(a, 6, 6));
    fuel(w, escort);
    fuel(w, settler);
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Convoy", {escort, settler}}).ok);
    const FleetId convoy = w.s.fleets.back().id;
    w.s.fleet(convoy)->orders = {mk(OrderKind::Colonize, {}, other)};
    w.move();
    CHECK(w.v(escort).location == at(a, 9, 9));
    w.colonize();
    REQUIRE(w.s.colony(other));
    CHECK(w.s.vehicle(settler) == nullptr);
    REQUIRE(w.s.vehicle(escort));
    REQUIRE(w.s.fleet(convoy));
    CHECK(w.s.fleet(convoy)->orders.empty());
}

TEST_CASE("movement: ruins give technology; automatic colonists are added") {
    ruleset::Ruleset rs = buildRuleset();
    rs.settings.set("Automatic Colonization Population", "5");
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
    const auto& unique = w.s.empire(kA).uniqueAreasUnlocked;
    CHECK(std::find(unique.begin(), unique.end(), 7) != unique.end());
    CHECK(w.s.galaxy.object(ruin).abilities.empty());
    REQUIRE(w.s.colony(ruin));
    CHECK(w.s.colony(ruin)->totalPopulation() == 1 + 5);
    CHECK(w.logged(kA, "Ancient ruins"));
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
    const SystemId a = w.system("A");
    w.object(a, ObjectKind::Star, {6, 6});
    const ObjectId rocks = w.object(a, ObjectKind::Asteroids, {2, 2});
    const size_t objects = w.s.galaxy.objects.size();

    const VehicleId maker = w.spawn(w.ship(kA, "Maker", 3, {"Mv Planet Maker"}), at(a, 2, 2));
    w.order(maker, stellar(StellarAction::CreatePlanet, rocks));
    w.move();
    const SpaceObject& made = w.s.galaxy.object(rocks);
    CHECK(made.kind == ObjectKind::Planet);
    CHECK((made.size == "Medium" || made.size == "Small"));
    CHECK(w.s.galaxy.objects.size() == objects);  // converted in place
    CHECK_FALSE(entryIntact(r, w.s, w.v(maker), 7));
    CHECK(w.v(maker).supply == 50);
    CHECK(w.v(maker).orders.empty());
    CHECK(w.logged(kA, "Planet created"));

    // Destroy it again; the colony on it is lost.
    w.colony(rocks, kB, 500);
    const VehicleId breaker = w.spawn(w.ship(kA, "Breaker", 3, {"Mv Planet Breaker"}), at(a, 2, 2));
    w.order(breaker, stellar(StellarAction::DestroyPlanet));
    w.move();
    CHECK(w.s.galaxy.object(rocks).kind == ObjectKind::Asteroids);
    CHECK(w.s.colony(rocks) == nullptr);
    CHECK(w.logged(kB, "lost"));
    CHECK(hasMood(w.lastMoods, kB, "Any Planet Lost"));

    // Protected and oversized planets survive.
    const ObjectId guarded = w.planet(a, {4, 4});
    w.colony(guarded, kB, 500, {"Mv Planet Guard"});
    const ObjectId big = w.planet(a, {8, 8}, "Rock", "Oxygen", "Large");
    const VehicleId b2 = w.spawn(w.ship(kA, "B2", 3, {"Mv Planet Breaker"}), at(a, 4, 4));
    const VehicleId b3 = w.spawn(w.ship(kA, "B3", 3, {"Mv Planet Breaker"}), at(a, 8, 8));
    w.order(b2, stellar(StellarAction::DestroyPlanet, guarded));
    w.order(b3, stellar(StellarAction::DestroyPlanet, big));
    w.move();
    CHECK(w.s.galaxy.object(guarded).kind == ObjectKind::Planet);
    CHECK(w.s.galaxy.object(big).kind == ObjectKind::Planet);
    CHECK(w.logged(kA, "protected"));
    CHECK(w.logged(kA, "too large"));
    CHECK(entryIntact(r, w.s, w.v(b2), 7));

    // No movement left, no manipulation: the order waits (spec 03 §8, simultaneous games).
    const VehicleId stuck = w.spawn(w.design(kA, "Stuck", "Test Frigate", {"Test Bridge", "Mv Storm Maker"}), at(a, 1, 1));
    w.order(stuck, stellar(StellarAction::CreateStorm));
    w.move();
    CHECK(w.v(stuck).orders.size() == 1);
    CHECK(countKind(w.s, a, ObjectKind::Storm) == 0);
}

TEST_CASE("movement: stellar manipulation - stars, nebulae and black holes") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 5, 0);
    const ObjectId star = w.object(a, ObjectKind::Star, {6, 6});
    const ObjectId planet = w.planet(a, {3, 3});
    w.colony(planet, kB, 500);
    const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
    const VehicleId victim = w.spawn(w.ship(kB, "Victim", 1), at(a, 1, 1));

    const VehicleId maker = w.spawn(w.ship(kA, "Maker", 3, {"Mv Star Maker"}), at(a, 2, 9));
    w.order(maker, stellar(StellarAction::CreateStar));
    const size_t before = w.s.galaxy.objects.size();
    w.move();
    REQUIRE(w.s.galaxy.objects.size() == before + 1);
    const ObjectId born{before};
    CHECK(w.s.galaxy.object(born).kind == ObjectKind::Star);
    CHECK(w.s.galaxy.object(born).sector == Sector{2, 9});
    CHECK(inSystemList(w.s, born));
    CHECK(w.s.colonies.size() == w.s.galaxy.objects.size());
    CHECK(w.s.empire(kA).knowledge.knownWarpLink.size() == w.s.galaxy.objects.size());

    // A guarded star cannot be destroyed.
    w.colony(planet, kB, 500, {"Mv Star Guard"});
    const VehicleId nova = w.spawn(w.ship(kA, "Nova", 3, {"Mv Star Breaker"}), at(a, 6, 6));
    w.order(nova, stellar(StellarAction::DestroyStar, star));
    w.move();
    CHECK(w.s.galaxy.object(star).kind == ObjectKind::Star);
    CHECK(w.logged(kA, "protected"));

    // Unguarded: the whole system goes, warp points excepted.
    w.colony(planet, kB, 500);
    w.order(nova, stellar(StellarAction::DestroyStar, star));
    w.move();
    CHECK(w.s.galaxy.object(star).kind == ObjectKind::DestroyedStar);
    CHECK(w.s.vehicle(nova) == nullptr);
    CHECK(w.s.vehicle(victim) == nullptr);
    CHECK(w.s.vehicle(maker) == nullptr);
    CHECK(w.s.colony(planet) == nullptr);
    CHECK_FALSE(inSystemList(w.s, planet));
    CHECK_FALSE(inSystemList(w.s, born));
    CHECK(inSystemList(w.s, ab));

    // Nebulae: made from a star, wiping the system; then removed.
    World n;
    const SystemId na = n.system("N");
    n.object(na, ObjectKind::Star, {6, 6});
    const VehicleId gone = n.spawn(n.ship(kB, "Gone", 1), at(na, 0, 0));
    const VehicleId fog = n.spawn(n.ship(kA, "Fog", 3, {"Mv Nebula Maker"}), at(na, 6, 6));
    n.order(fog, stellar(StellarAction::CreateNebulae));
    n.move();
    CHECK(n.s.galaxy.system(na).physicalType == "Nebulae");
    REQUIRE(n.s.galaxy.system(na).abilities.size() == 1);
    CHECK(countKind(n.s, na, ObjectKind::Star) == 0);
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

    // Black holes.
    World h;
    const SystemId ha = h.system("H");
    h.object(ha, ObjectKind::Star, {6, 6});
    const VehicleId hole = h.spawn(h.ship(kA, "Hole", 3, {"Mv Hole Maker"}), at(ha, 6, 6));
    h.order(hole, stellar(StellarAction::CreateBlackHole));
    h.move();
    CHECK(h.s.galaxy.system(ha).physicalType == "Black Hole");
    CHECK(h.s.galaxy.system(ha).abilities.size() == 2);
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
    const SystemId a = w.system("A", 0, 0), b = w.system("B", 5, 0), c = w.system("C", 30, 0);
    const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
    w.exploreAll(kA);

    const VehicleId opener = w.spawn(w.ship(kA, "Opener", 3, {"Mv Warp Opener"}), at(a, 0, 0));
    w.order(opener, stellar(StellarAction::OpenWarpPoint, {}, at(b, 6, 6)));  // not an edge sector
    const size_t before = w.s.galaxy.objects.size();
    w.move();
    REQUIRE(w.s.galaxy.objects.size() == before + 2);
    const ObjectId here{before}, there{before + 1};
    CHECK(w.s.galaxy.object(here).kind == ObjectKind::WarpPoint);
    CHECK(w.s.galaxy.object(here).sector == Sector{0, 0});
    CHECK(w.s.galaxy.object(here).destination == there);
    CHECK(w.s.galaxy.object(there).destination == here);
    CHECK(w.s.galaxy.object(there).system == b);
    CHECK(w.s.galaxy.object(there).sector == Sector{0, 5});  // the free edge sector facing A
    CHECK(sight::knowsWarpLink(w.s, kA, here));
    CHECK(w.s.colonies.size() == w.s.galaxy.objects.size());
    CHECK(countKind(w.s, a, ObjectKind::WarpPoint) == 2);
    const auto shortcut = movement::findPath(r, w.s, kA, at(a, 0, 0), at(b, 0, 5));
    REQUIRE(shortcut);
    CHECK(shortcut->length == 1);

    // Out of range, and blocked by a guard at the far end.
    const VehicleId far = w.spawn(w.ship(kA, "Far", 3, {"Mv Warp Opener"}), at(a, 1, 1));
    w.order(far, stellar(StellarAction::OpenWarpPoint, {}, at(c, 0, 0)));
    w.colony(w.planet(b, {5, 5}), kB, 100, {"Mv Warp Guard"});
    const VehicleId blocked = w.spawn(w.ship(kA, "Blocked", 3, {"Mv Warp Opener"}), at(a, 2, 2));
    w.order(blocked, stellar(StellarAction::OpenWarpPoint, {}, at(b, 0, 0)));
    w.move();
    CHECK(w.logged(kA, "out of range"));
    CHECK(w.logged(kA, "blocked"));
    CHECK(w.s.galaxy.objects.size() == before + 3);  // only the guard's planet was added

    // A sector picked on the target's edge is used for the far end.
    World e;
    const SystemId ea = e.system("A", 0, 0), eb = e.system("B", 5, 0);
    const VehicleId picker = e.spawn(e.ship(kA, "Picker", 3, {"Mv Warp Opener"}), at(ea, 3, 3));
    e.order(picker, stellar(StellarAction::OpenWarpPoint, {}, at(eb, 12, 12)));
    e.move();
    REQUIRE(e.s.galaxy.objects.size() == 2);
    CHECK(e.s.galaxy.objects[1].system == eb);
    CHECK(e.s.galaxy.objects[1].sector == Sector{12, 12});

    // Closing removes both ends; closing again is a harmless no-op.
    World k;
    const SystemId ka = k.system("A"), kb = k.system("B", 5, 0);
    const auto [kab, kba] = k.link(ka, {12, 6}, kb, {0, 6});
    k.exploreAll(kA);
    const VehicleId closer = k.spawn(k.ship(kA, "Closer", 3, {"Mv Warp Closer"}), at(ka, 12, 6));
    const VehicleId late = k.spawn(k.ship(kA, "Late", 3, {"Mv Warp Closer"}), at(ka, 12, 6));
    k.order(closer, stellar(StellarAction::CloseWarpPoint, kab));
    k.order(late, stellar(StellarAction::CloseWarpPoint, kab));
    k.move();
    CHECK_FALSE(inSystemList(k.s, kab));
    CHECK_FALSE(inSystemList(k.s, kba));
    CHECK_FALSE(k.s.galaxy.object(kab).destination.valid());
    CHECK_FALSE(movement::findPath(r, k.s, EmpireId{}, at(ka, 6, 6), at(kb, 6, 6)));
    CHECK_FALSE(entryIntact(r, k.s, k.v(closer), 7));
    CHECK(entryIntact(r, k.s, k.v(late), 7));
    CHECK(k.v(late).orders.empty());
    (void)ba;
}

TEST_CASE("movement: stellar manipulation - storms and constructed worlds") {
    World w;
    const Rules& r = w.rules();
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
    CHECK(made.sector == Sector{10, 10});
    REQUIRE(made.abilities.size() == 1);
    const auto kind = parseAbilityKind(made.abilities[0].type);
    const int64_t value = made.abilities[0].number1();
    CHECK(value >= 1);
    if (kind == AbilityKind::SectorSightObscuration) CHECK(value <= 2);
    else if (kind == AbilityKind::SectorDamage) CHECK(value <= 7);
    else CHECK((kind == AbilityKind::SectorShieldDisruption && value <= 9));

    // A ringworld needs 20 kT of girders at the star.
    const VehicleId builder = w.spawn(w.ship(kA, "Builder", 3, {"Mv World Builder", "Mv Girder"}), at(a, 6, 6));
    w.order(builder, stellar(StellarAction::CreateConstructedPlanet, star));
    w.move();
    CHECK(w.logged(kA, "materials"));
    const VehicleId hauler = w.spawn(w.ship(kA, "Girders", 3, {"Mv Girder"}), at(a, 6, 6));
    w.order(builder, stellar(StellarAction::CreateConstructedPlanet, star));
    const size_t count = w.s.galaxy.objects.size();
    w.move();
    REQUIRE(w.s.galaxy.objects.size() == count + 1);
    const SpaceObject& world = w.s.galaxy.objects.back();
    CHECK(world.kind == ObjectKind::Planet);
    CHECK(world.size == "Ringworld");
    CHECK(world.sector == Sector{6, 6});
    CHECK_FALSE(entryIntact(r, w.s, w.v(builder), 7));  // the builder is used up
    CHECK_FALSE(entryIntact(r, w.s, w.v(builder), 8));  // and the girders
    CHECK_FALSE(entryIntact(r, w.s, w.v(hauler), 7));
    CHECK(w.logged(kA, "Planet constructed"));
}

// ---- Orders, groups and logistics (spec 03 §8, §9, §12) ---------------------------------------------

TEST_CASE("movement: a failed order clears every fleet member's list and switches Repeat off") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    const VehicleId one = w.spawn(w.ship(kA, "One", 2, {"Test Quantum Reactor"}), at(a, 0, 0));
    const VehicleId two = w.spawn(w.ship(kA, "Two", 2, {"Test Quantum Reactor"}), at(a, 0, 0));
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {one, two}}).ok);
    Fleet& f = w.s.fleets.back();
    f.orders = {moveTo(a, 1, 0), moveTo(b, 3, 3), moveTo(a, 2, 0)};  // B is not linked: the second order fails
    f.repeatOrders = true;
    w.v(one).orders = {moveTo(a, 5, 5)};  // a member's own list goes too
    w.v(one).repeatOrders = true;
    w.move();
    CHECK(w.s.fleets.back().orders.empty());
    CHECK_FALSE(w.s.fleets.back().repeatOrders);
    CHECK(w.v(one).orders.empty());
    CHECK_FALSE(w.v(one).repeatOrders);
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

TEST_CASE("movement: one order execution per action; a ship with no movement acts once, on day 1") {
    World w;
    const SystemId a = w.system("A");
    const DesignId fighter = w.design(kA, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    // A base (speed 0) with two in-place orders carries out one per turn.
    const VehicleId base = w.spawn(w.design(kA, "Hangar", "Test Station", {"Test Bridge", "Mv Fighter Bay"}), at(a, 4, 4));
    w.v(base).cargo.units.push_back({fighter, 2});
    w.order(base, mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, 1));
    w.order(base, mk(OrderKind::LaunchUnits, {}, {}, {}, fighter, 1));
    w.move();
    CHECK(w.v(base).orders.size() == 1);
    CHECK(w.v(base).cargo.unitCount(fighter) == 1);
    w.move();
    CHECK(w.v(base).orders.empty());
    CHECK(w.v(base).cargo.unitCount(fighter) == 0);
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
    const VehicleId carrier = w.spawn(w.ship(kA, "Carrier", 1, {"Mv Fighter Bay"}), at(a, 5, 5));
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

TEST_CASE("movement: a sector is not fought over again once everything there has fought this turn") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId guard = w.spawn(w.ship(kB, "Guard", 1), at(a, 3, 6));
    const VehicleId raider = w.spawn(w.ship(kA, "Raider", 3, {"Mv Sweeper"}), at(a, 2, 6));
    fuel(w, raider);
    w.order(raider, moveTo(a, 3, 6));
    w.order(raider, mk(OrderKind::SweepMines));  // acts in place: the sector is checked again
    w.order(raider, mk(OrderKind::SweepMines));
    CombatSpy spy;
    spy.fight = hostilesMeet;
    w.move(spy.hooks());
    CHECK(spy.fought.size() == 1);
    CHECK(std::count(spy.asked.begin(), spy.asked.end(), at(a, 3, 6)) == 3);
    // A newcomer brings a new battle.
    const VehicleId second = w.spawn(w.ship(kA, "Second", 1), at(a, 3, 5));
    fuel(w, second);
    w.order(second, moveTo(a, 3, 6));
    CombatSpy again;
    again.fight = hostilesMeet;
    w.move(again.hooks());
    CHECK(again.fought.size() == 1);
    (void)guard;
}

TEST_CASE("movement: warp links work both ways; only the first 10 warp points of a system are used; neutrals never warp") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 5, 0);
    const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
    w.s.galaxy.object(ab).oneWay = true;  // the flag is never read (spec 01 §8)
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
    const VehicleId tanker = w.spawn(w.ship(kA, "Tanker", 3), at(a, 1, 6));
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
    w.s.fleets.back().orders = {mk(OrderKind::Colonize, {}, target)};
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
    w.s.empire(kB).knowledge.seenDesigns = {seen};
    w.s.turn = 9;  // the cleanup runs when a new year starts
    w.upkeep();
    const auto& list = w.s.empire(kA).designs;
    auto has = [&](DesignId d) { return std::find(list.begin(), list.end(), d) != list.end(); };
    CHECK(has(used));
    CHECK(has(queued));
    CHECK(has(seen));
    CHECK(has(kept));
    CHECK_FALSE(has(gone));
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
    w.upkeep();
    // 50 kT over: 8M of people (40 kT), then one mine of the first stack.
    CHECK(w.v(hauler).cargo.population.empty());
    REQUIRE(w.v(hauler).cargo.units.size() == 2);
    CHECK(w.v(hauler).cargo.units[0] == UnitStack{mine, 2});
    CHECK(w.v(hauler).cargo.units[1] == UnitStack{mine2, 3});
    CHECK(cargoSpaceUsed(r, w.s, w.v(hauler).cargo) <= vehicleCargoCapacity(r, w.s, w.v(hauler)));
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
