// Movement lines (spec 06 §2.4 "Movement lines", confirmed: binary): the
// route the system panel shows for the viewer's vehicle or fleet, worked out
// with the movement rules but never touching the game's random numbers, and
// the marks the panel draws for it.

#include "movement_fixture.hpp"

#include "client/classic/movement_line.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::mvtest;
using client::classic::LineMark;
using client::classic::PixelPoint;

namespace {


Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }

Order moveTo(SystemId s, int x, int y) {
    Order o;
    o.kind = OrderKind::MoveTo;
    o.location = at(s, x, y);
    return o;
}

movement::PlannedRoute route(World& w, VehicleId id) {
    Rng display(1);
    return movement::planRoute(w.rules(), w.s, w.v(id), display);
}

// Sector centres of the 800x600 layout: (34 + 36c, 139 + 36r).
PixelPoint centre(Sector s) { return {34 + 36 * s.x, 139 + 36 * s.y}; }

} // namespace

TEST_CASE("movement line: in-system moves follow the greedy steps, diagonal first, square by square") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId id = w.spawn(w.ship(kA, "Runner", 3), at(a, 2, 2));
    w.order(id, moveTo(a, 6, 4));
    const movement::PlannedRoute r = route(w, id);
    CHECK(r.points == std::vector<Location>{at(a, 2, 2), at(a, 3, 3), at(a, 4, 4), at(a, 5, 4), at(a, 6, 4)});
    CHECK(r.movementLeft == w.v(id).movement);
    CHECK(r.movementPerTurn == vehicleMaxMovement(w.rules(), w.s, w.v(id)));
}

TEST_CASE("movement line: a route through a warp point gives one point for the jump") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);
    w.link(a, {9, 6}, b, {2, 6});
    w.exploreAll(kA);
    const VehicleId id = w.spawn(w.ship(kA, "Runner", 3), at(a, 6, 6));
    w.order(id, moveTo(b, 5, 6));
    CHECK(route(w, id).points ==
          std::vector<Location>{at(a, 6, 6), at(a, 7, 6), at(a, 8, 6), at(a, 9, 6), at(b, 2, 6), at(b, 3, 6), at(b, 4, 6), at(b, 5, 6)});
}

TEST_CASE("movement line: only Move To and Move To Waypoint add squares; the route goes on from the last one") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId id = w.spawn(w.ship(kA, "Runner", 3), at(a, 0, 0));
    w.s.empire(kA).waypoints[3].set = true;
    w.s.empire(kA).waypoints[3].location = at(a, 2, 4);
    Order sentry;
    sentry.kind = OrderKind::Sentry;
    Order waypoint;
    waypoint.kind = OrderKind::MoveToWaypoint;
    waypoint.amount = 3;
    Order unset = waypoint;
    unset.amount = 5;
    w.order(id, moveTo(a, 2, 0));
    w.order(id, sentry);
    w.order(id, unset);   // an unset waypoint adds nothing
    w.order(id, waypoint);
    CHECK(route(w, id).points == std::vector<Location>{at(a, 0, 0), at(a, 1, 0), at(a, 2, 0), at(a, 2, 1), at(a, 2, 2), at(a, 2, 3), at(a, 2, 4)});

    // Nothing that moves: just the start (its "0").
    World n;
    const SystemId na = n.system("A");
    const VehicleId still = n.spawn(n.ship(kA, "Still", 3), at(na, 4, 4));
    n.order(still, sentry);
    CHECK(route(n, still).points == std::vector<Location>{at(na, 4, 4)});
}

TEST_CASE("movement line: a step around a hostile draws from the display generator, never the game's") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId id = w.spawn(w.ship(kA, "Runner", 4), at(a, 0, 6));
    w.spawn(w.ship(kB, "Picket", 1), at(a, 1, 6));
    w.order(id, moveTo(a, 6, 6));
    const Rng before = w.s.rng;
    const movement::PlannedRoute r = route(w, id);
    CHECK(w.s.rng == before);
    REQUIRE(r.points.size() >= 2);
    CHECK(r.points[1] != at(a, 1, 6));                 // stepped around the picket
    CHECK(r.points.back() == at(a, 6, 6));
    // The same generator gives the same line on every machine and every frame.
    CHECK(route(w, id).points == r.points);
}

TEST_CASE("movement line: a fleet starts at its location, with the lowest movement among its members there") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId fast = w.spawn(w.ship(kA, "Fast", 4), at(a, 3, 3));
    const VehicleId slow = w.spawn(w.ship(kA, "Slow", 2), at(a, 3, 3));
    REQUIRE(apply(w.rules(), w.s, kA, cmd::CreateFleet{"Pair", {fast, slow}}).ok);
    const Fleet& f = w.s.fleets.front();
    REQUIRE(apply(w.rules(), w.s, kA, cmd::SetOrders{{}, f.id, {moveTo(a, 6, 3)}, false, {}}).ok);
    w.v(fast).movement = 4;
    w.v(slow).movement = 1;
    Rng display(1);
    const movement::PlannedRoute r = movement::planRoute(w.rules(), w.s, w.s.fleets.front(), display);
    CHECK(r.points == std::vector<Location>{at(a, 3, 3), at(a, 4, 3), at(a, 5, 3), at(a, 6, 3)});
    CHECK(r.movementLeft == 1);
    CHECK(r.movementPerTurn == std::min(vehicleMaxMovement(w.rules(), w.s, w.v(fast)), vehicleMaxMovement(w.rules(), w.s, w.v(slow))));
}

TEST_CASE("movement line: turn numbers - 0 within the movement left, then one per full turn") {
    movement::PlannedRoute r;
    r.points.resize(10);
    r.movementLeft = 2;
    r.movementPerTurn = 3;
    const int expected[] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3};
    for (size_t i = 0; i < r.points.size(); ++i) CHECK(r.turnOf(i) == expected[i]);
    r.movementPerTurn = 0;  // mothballed: everything beyond A reads 0
    CHECK(r.turnOf(9) == 0);
}

TEST_CASE("movement line: the marks - rings, lines from the last drawn centre, numbers after their line") {
    const SystemId a{0u}, b{1u};
    movement::PlannedRoute r;
    r.points = {at(a, 1, 1), at(a, 2, 1), at(a, 3, 1), at(b, 0, 0), at(b, 1, 0)};
    r.movementLeft = 1;
    r.movementPerTurn = 1;
    const auto marks = client::classic::movementLineMarks(r, a, centre);
    using K = LineMark::Kind;
    REQUIRE(marks.size() == 7);
    CHECK((marks[0].kind == K::Ring && marks[0].at == centre({2, 1})));
    CHECK((marks[1].kind == K::Segment && marks[1].at == centre({1, 1}) && marks[1].to == centre({2, 1})));
    CHECK((marks[2].kind == K::Number && marks[2].at == centre({1, 1}) && marks[2].number == 0));
    CHECK((marks[3].kind == K::Ring && marks[3].at == centre({3, 1})));
    CHECK((marks[4].kind == K::Segment && marks[4].at == centre({2, 1})));
    CHECK((marks[5].kind == K::Number && marks[5].at == centre({2, 1}) && marks[5].number == 0));
    // The last point in this system: the next lies elsewhere, so its own number.
    CHECK((marks[6].kind == K::Number && marks[6].at == centre({3, 1}) && marks[6].number == 1));

    // In the other system: no line into its first point, a ring on each.
    const auto there = client::classic::movementLineMarks(r, b, centre);
    REQUIRE(there.size() == 5);
    CHECK(there[0].kind == K::Ring);
    CHECK(there[1].kind == K::Ring);
    CHECK((there[2].kind == K::Segment && there[2].at == centre({0, 0})));
    CHECK((there[3].kind == K::Number && there[3].number == 2));
    CHECK((there[4].kind == K::Number && there[4].number == 3));

    // A route that leaves and comes back is broken there: no square gets two numbers.
    movement::PlannedRoute loop;
    loop.points = {at(a, 1, 1), at(b, 0, 0), at(a, 5, 5)};
    int numbers = 0, segments = 0;
    for (const LineMark& m : client::classic::movementLineMarks(loop, a, centre)) {
        numbers += m.kind == K::Number;
        segments += m.kind == K::Segment;
    }
    CHECK(numbers == 2);
    CHECK(segments == 0);
}

TEST_CASE("movement line: Windows-style pixels - the end point is not set, rings are 8 px across") {
    const auto h = client::classic::linePixels({10, 10}, {14, 10});
    CHECK(h == std::vector<PixelPoint>{{10, 10}, {11, 10}, {12, 10}, {13, 10}});
    const auto d = client::classic::linePixels({10, 10}, {7, 13});
    CHECK(d == std::vector<PixelPoint>{{10, 10}, {9, 11}, {8, 12}});
    CHECK(client::classic::linePixels({3, 3}, {3, 3}).empty());
    int minX = 99, maxX = -99, minY = 99, maxY = -99;
    for (const PixelPoint p : client::classic::ringOffsets()) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    CHECK(minX == -4);
    CHECK(maxX == 3);
    CHECK(minY == -4);
    CHECK(maxY == 3);
}

TEST_CASE("movement line: whose line - the viewer's ship, base, fighter or drone group, or fleet, with orders") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId mine = w.spawn(w.ship(kA, "Mine", 3), at(a, 2, 2));
    const VehicleId theirs = w.spawn(w.ship(kB, "Theirs", 3), at(a, 4, 4));
    const Rules& r = w.rules();
    using client::classic::movementLineSubject;
    CHECK_FALSE(movementLineSubject(r, w.s, kA, mine, std::nullopt).has_value());   // no orders yet
    w.order(mine, moveTo(a, 5, 5));
    w.order(theirs, moveTo(a, 0, 0));
    REQUIRE(movementLineSubject(r, w.s, kA, mine, std::nullopt).has_value());
    CHECK(movementLineSubject(r, w.s, kA, mine, std::nullopt)->vehicle == mine);
    CHECK_FALSE(movementLineSubject(r, w.s, kA, theirs, std::nullopt).has_value());  // another empire's
    CHECK_FALSE(movementLineSubject(r, w.s, kA, std::nullopt, std::nullopt).has_value());
}
