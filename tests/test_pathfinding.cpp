#include "test_support.hpp"

#include "sim/pathfinding.hpp"

using namespace opense4;
using namespace opense4::sim;

TEST_CASE("paths inside a system are Chebyshev-shortest") {
    const auto game = test::newGame(3);
    const GameState& s = game.state;
    const Location from{SystemId{0u}, SectorPos{-3, -2}};
    const Location to{SystemId{0u}, SectorPos{4, 1}};
    const auto path = findPath(s, from, to);
    REQUIRE(path);
    CHECK(static_cast<int>(path->size()) == sectorDistance(from.sector, to.sector));
    CHECK(path->back() == to);
    Location prev = from;
    for (const Location& step : *path) {
        CHECK(step.system == from.system);
        CHECK(sectorDistance(prev.sector, step.sector) == 1);
        prev = step;
    }
    CHECK(findPath(s, from, from)->empty());
}

TEST_CASE("paths cross warp points between systems") {
    const auto game = test::newGame(5);
    const GameState& s = game.state;
    const WarpPoint& wp = s.warpPoint(s.system(SystemId{0u}).warpPoints.front());
    const WarpPoint& exit = s.warpPoint(wp.exit);

    const Location start{wp.system, SectorPos{}};
    const Location goal{exit.system, SectorPos{}};
    const auto path = findPath(s, start, goal);
    REQUIRE(path);
    CHECK(path->back() == goal);

    // Every step is either a sector step within a system or a warp jump.
    Location prev = start;
    int jumps = 0;
    for (const Location& step : *path) {
        if (step.system == prev.system) {
            CHECK(sectorDistance(prev.sector, step.sector) == 1);
        } else {
            ++jumps;
            bool linked = false;
            for (WarpPointId w : s.system(prev.system).warpPoints) {
                const WarpPoint& from = s.warpPoint(w);
                const WarpPoint& to = s.warpPoint(from.exit);
                if (from.sector == prev.sector && to.system == step.system && to.sector == step.sector) linked = true;
            }
            CHECK(linked);
        }
        prev = step;
    }
    CHECK(jumps >= 1);
    // Direct neighbours: star -> warp point, jump, warp point -> star.
    CHECK(static_cast<int>(path->size()) <=
          sectorDistance({}, wp.sector) + 1 + sectorDistance(exit.sector, {}));
}

TEST_CASE("findPathTo reaches the nearest matching location") {
    const auto game = test::newGame(8);
    const GameState& s = game.state;
    const Location start{SystemId{0u}, SectorPos{}};
    const auto path = findPathTo(s, start, [&](Location l) { return l.system != start.system; });
    REQUIRE(path);
    REQUIRE_FALSE(path->empty());
    CHECK(path->back().system != start.system);
    // The first location outside the start system is the exit of a warp point.
    bool isWarpExit = false;
    for (const WarpPoint& wp : s.warpPoints)
        if (wp.system == path->back().system && wp.sector == path->back().sector) isWarpExit = true;
    CHECK(isWarpExit);
}
