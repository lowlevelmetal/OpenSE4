#include "test_support.hpp"

#include "sim/pathfinding.hpp"
#include "sim/rules.hpp"

#include <set>

using namespace opense4;
using namespace opense4::sim;

TEST_CASE("generated galaxies are connected, planar and well-formed") {
    for (int shapeIndex = 0; shapeIndex < static_cast<int>(GalaxyShape::Count); ++shapeIndex) {
        for (uint64_t seed : {1ull, 7ull, 12345ull}) {
            const auto shape = static_cast<GalaxyShape>(shapeIndex);
            INFO("shape " << displayName(shape) << " seed " << seed);
            const auto game = test::newGame(seed, 45, 4, false, shape);
            const GameState& s = game.state;
            REQUIRE(s.systems.size() == 45);

            // Connected: every system reachable by warp from system 0.
            const auto hops = warpHopDistances(s, SystemId{0u});
            CHECK(std::none_of(hops.begin(), hops.end(), [](int h) { return h < 0; }));

            // Warp points are linked in pairs, in bounds, and never share a sector.
            for (const WarpPoint& wp : s.warpPoints) {
                const WarpPoint& exit = s.warpPoint(wp.exit);
                CHECK(exit.exit == wp.id);
                CHECK(exit.system != wp.system);
                CHECK(s.inBounds(wp.sector));
            }
            for (const StarSystem& sys : s.systems) {
                std::set<SectorPos> used{SectorPos{}};
                for (WarpPointId w : sys.warpPoints) CHECK(used.insert(s.warpPoint(w).sector).second);
                for (PlanetId p : sys.planets) {
                    CHECK(s.planet(p).system == sys.id);
                    CHECK(used.insert(s.planet(p).sector).second);
                }
            }

            // Lanes don't cross each other on the galaxy map.
            std::vector<std::pair<SystemId, SystemId>> lanes;
            for (const WarpPoint& wp : s.warpPoints)
                if (wp.id < wp.exit) lanes.emplace_back(wp.system, s.warpPoint(wp.exit).system);
            for (size_t i = 0; i < lanes.size(); ++i)
                for (size_t j = i + 1; j < lanes.size(); ++j) {
                    const auto [a, b] = lanes[i];
                    const auto [c, d] = lanes[j];
                    if (a == c || a == d || b == c || b == d) continue;
                    CHECK_FALSE(segmentsCross(s.system(a).position, s.system(b).position, s.system(c).position,
                                              s.system(d).position));
                }
        }
    }
}

TEST_CASE("each empire starts with a suitable homeworld and fleet") {
    const auto game = test::newGame(42, 30, 4);
    const GameState& s = game.state;
    const Content& c = test::content();
    REQUIRE(s.empires.size() == 4);
    CHECK(game.player == EmpireId{0u});
    CHECK_FALSE(s.empire(game.player).ai);

    std::set<SystemId> homeSystems;
    for (const Empire& e : s.empires) {
        const Planet& hw = s.planet(e.homeworld);
        REQUIRE(hw.colony);
        CHECK(hw.colony->owner == e.id);
        CHECK(hw.surface == e.nativeSurface);
        CHECK(canBreathe(e, hw));
        CHECK(hasSpaceYard(c, *hw.colony));
        CHECK(e.hasExplored(hw.system));
        CHECK(homeSystems.insert(hw.system).second);
        CHECK(shipCount(s, e.id) == 4);
    }
}

TEST_CASE("setup handles cramped galaxies and rejects invalid settings") {
    // Smallest supported systems with the most empires: every home system gets a homeworld.
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        GameSetup setup;
        setup.galaxy.seed = seed;
        setup.galaxy.systemCount = 12;
        setup.galaxy.sectorRadius = kMinSectorRadius;
        setup.empireCount = 6;
        const auto game = createGame(test::content(), setup);
        REQUIRE_MESSAGE(game.has_value(), "seed " << seed << ": " << (game ? "" : game.error()));
        for (const Empire& e : game->state.empires) CHECK(game->state.planet(e.homeworld).colony.has_value());
    }

    GameSetup bad;
    bad.galaxy.sectorRadius = kMinSectorRadius - 1;
    CHECK_FALSE(createGame(test::content(), bad).has_value());
    bad.galaxy.sectorRadius = 6;
    bad.galaxy.systemCount = kMaxSystems + 1;
    CHECK_FALSE(createGame(test::content(), bad).has_value());
}

TEST_CASE("the same seed always generates the same galaxy") {
    const auto a = test::newGame(99, 40, 5);
    const auto b = test::newGame(99, 40, 5);
    CHECK(stateChecksum(a.state) == stateChecksum(b.state));
    CHECK(a.state.systems[7].name == b.state.systems[7].name);
    const auto other = test::newGame(100, 40, 5);
    CHECK(stateChecksum(a.state) != stateChecksum(other.state));
}
