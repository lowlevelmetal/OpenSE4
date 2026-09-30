// Quadrant generation and empire placement (docs/spec/01 §2.2, §3-§5).

#include "engine_fixture.hpp"

#include "game/generate.hpp"
#include "game/query.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <numbers>
#include <set>

using namespace opense4;
using namespace opense4::game;

namespace {

const ruleset::Ruleset& fixture() {
    static const ruleset::Ruleset rs = [] {
        auto r = ruleset::loadRuleset(std::filesystem::path(OPENSE4_FIXTURE_DIR) / "minimal_dataset");
        REQUIRE(r.ruleset);
        REQUIRE(r.diagnostics.errors.empty());
        return std::move(*r.ruleset);
    }();
    return rs;
}

Generated generate(const ruleset::Ruleset& rs, QuadrantOptions opt, uint64_t seed) {
    Rng rng(seed);
    auto g = generateQuadrant(rs, opt, rng);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    return std::move(*g);
}

bool connected(const Galaxy& g) {
    const std::vector<int> jumps = warpJumps(g, SystemId{0u});
    return std::none_of(jumps.begin(), jumps.end(), [](int j) { return j < 0; });
}

// Structural invariants every generated quadrant must satisfy.
void checkInvariants(const ruleset::Ruleset& rs, const Generated& gen, const QuadrantOptions& opt) {
    const Galaxy& g = gen.galaxy;
    if (opt.systemCount > 0) CHECK(static_cast<int>(g.systems.size()) <= opt.systemCount);
    CHECK(g.width == kQuadrantWidth);
    CHECK(g.height == kQuadrantHeight);
    std::set<std::string> names;
    for (const StarSystem& sys : g.systems) {
        CHECK(names.insert(sys.name).second);
        CHECK(sys.position.x >= 0);
        CHECK(sys.position.y >= 0);
        CHECK(sys.position.x < g.width);
        CHECK(sys.position.y < g.height);
        CHECK(static_cast<int>(g.warpPoints(sys.id).size()) <= kMaxWarpPoints);
        for (ObjectId id : sys.objects) {
            const SpaceObject& o = g.object(id);
            CHECK(o.system == sys.id);
            CHECK(o.sector.valid());
            CHECK(o.kind != ObjectKind::Comet);  // generation never makes comets
            CHECK(o.sectorType < rs.sectorObjectTypes.size());
            CHECK(parseObjectKind(rs.sectorObjectTypes[o.sectorType].physicalType) == o.kind);
            if (o.kind == ObjectKind::Planet) {
                CHECK(o.conditions.inHundredths() >= 50);
                CHECK(o.conditions.inHundredths() <= 150);
                CHECK(o.conditions.inHundredths() % 10 == 0);
            }
            if (o.kind == ObjectKind::Asteroids) {
                CHECK(o.conditions.inHundredths() >= 25);
                CHECK(o.conditions.inHundredths() <= 75);
                CHECK(o.conditions.inHundredths() % 5 == 0);
            }
            if (o.kind == ObjectKind::WarpPoint) {
                REQUIRE(o.destination.valid());
                const SpaceObject& other = g.object(o.destination);
                CHECK(other.kind == ObjectKind::WarpPoint);
                CHECK(other.destination == o.id);
                CHECK(other.system != o.system);
                // Both ends share one record and one rolled ability.
                CHECK(other.sectorType == o.sectorType);
                CHECK(other.abilities.size() == o.abilities.size());
                if (!opt.warpPointsAnywhere) CHECK(chebyshev(o.sector, Sector{}) == kSystemCenter);  // outer edge
            }
        }
    }
    if (opt.allWarpPointsConnected && !opt.noWarpPoints && !g.systems.empty()) CHECK(connected(g));
}

ruleset::SystemObjectTemplate obj(std::string kind, std::string position, std::string size = "Any", std::string atmosphere = "Any",
                                  std::string composition = "Any") {
    ruleset::SystemObjectTemplate t;
    t.physicalType = std::move(kind);
    t.position = std::move(position);
    t.stellarAbilityType = "None";
    t.size = std::move(size);
    t.atmosphere = std::move(atmosphere);
    t.composition = std::move(composition);
    t.age = t.color = t.luminosity = "Any";
    return t;
}

// A quadrant type with one system type made of `objects`.
ruleset::Ruleset withSystem(std::vector<ruleset::SystemObjectTemplate> objects, bool startable = true) {
    ruleset::Ruleset rs = fixture();
    ruleset::SystemType t = rs.systemTypes.front();
    t.name = "Test Custom";
    t.objects = std::move(objects);
    t.empiresCanStartIn = startable;
    t.warpPointStellarAbilityType = "None";
    rs.systemTypes.push_back(t);
    ruleset::QuadrantType q = rs.quadrantTypes.front();
    q.name = "Test Custom Quadrant";
    q.systemTypeChances = {{ruleset::SystemTypeId{rs.systemTypes.size() - 1}, 1000}};
    rs.quadrantTypes.push_back(q);
    rs.reindex();
    return rs;
}

double bearingDegrees(int dx, int dy) {  // reference in double: dx right, dy down
    const double a = std::atan2(double(dx), double(-dy)) * 180.0 / std::numbers::pi;
    return a < 0 ? a + 360.0 : a;
}

} // namespace

// ---- Geometry (spec 01 §3.2, §3.5) --------------------------------------------------------------------------

TEST_CASE("galaxy geometry: distance, bearing and angle difference") {
    CHECK(galaxyDistance({0, 0}, {3, 4}) == 5);
    CHECK(galaxyDistance({0, 0}, {1, 1}) == 1);   // 1.41
    CHECK(galaxyDistance({0, 0}, {1, 2}) == 2);   // 2.24
    CHECK(galaxyDistance({0, 0}, {2, 2}) == 3);   // 2.83
    CHECK(galaxyDistance({5, 5}, {5, 5}) == 0);
    CHECK(galaxyDistance({0, 0}, {66, 45}) == 80);  // 79.88

    const GalaxyPos c{30, 20};
    CHECK(galaxyBearing(c, {30, 10}) == 0);    // straight up the map
    CHECK(galaxyBearing(c, {40, 20}) == 90);
    CHECK(galaxyBearing(c, {30, 30}) == 180);
    CHECK(galaxyBearing(c, {20, 20}) == 270);
    CHECK(galaxyBearing(c, {40, 10}) == 45);
    CHECK(galaxyBearing(c, {40, 30}) == 135);
    CHECK(galaxyBearing(c, {20, 30}) == 225);
    CHECK(galaxyBearing(c, {20, 10}) == 315);
    CHECK(galaxyBearing(c, c) == 0);
    CHECK(galaxyBearing(c, {31, 0}) == 3);     // atan(1/20) = 2.86°
    CHECK(galaxyBearing(c, {29, 0}) == 357);

    CHECK(bearingDifference(10, 40) == 30);
    CHECK(bearingDifference(350, 10) == 20);   // across north
    CHECK(bearingDifference(260, 10) == 250);  // not across north: 260 is not above 270
    CHECK(bearingDifference(275, 85) == 170);
}

TEST_CASE("galaxy geometry: bearings match a floating-point reference with a safe margin") {
    // Every offset the quadrant allows. Our extended-precision arctangent must
    // give the same whole degree as the exact value, which is never within a
    // hair of a half degree.
    double closest = 1.0;
    for (int dx = -66; dx <= 66; ++dx)
        for (int dy = -45; dy <= 45; ++dy) {
            if (dx == 0 && dy == 0) continue;
            const double ref = bearingDegrees(dx, dy);
            if (dy != 0 && std::abs(dx) != std::abs(dy) && dx != 0) closest = std::min(closest, std::abs(std::fmod(ref, 1.0) - 0.5));
            int want = static_cast<int>(std::lround(ref)) % 360;
            // The original rounds the angle measured from the vertical axis, so reproduce that.
            const double base = std::atan(double(std::abs(dx)) / double(std::abs(dy))) * 180.0 / std::numbers::pi;
            if (dy != 0) {
                int a = static_cast<int>(std::nearbyint(base));
                if (dx >= 0 && dy > 0) a = 180 - a;
                else if (dx < 0 && dy > 0) a += 180;
                else if (dx < 0 && dy < 0) a = 360 - a;
                want = a % 360;
            }
            INFO("dx " << dx << " dy " << dy);
            CHECK(galaxyBearing({0, 0}, {dx, dy}) == want);
        }
    CHECK(closest > 1e-6);
}

TEST_CASE("galaxy geometry: the square outline and warp point placement") {
    // r = 6.5: the edge of the 13 x 13 system grid. Straight up gives the top edge's centre.
    CHECK(squareOutline(0, 13) == OutlinePoint{6, 0});
    CHECK(squareOutline(90, 13) == OutlinePoint{12, 6});
    CHECK(squareOutline(180, 13) == OutlinePoint{6, 12});
    CHECK(squareOutline(270, 13) == OutlinePoint{0, 6});
    CHECK(squareOutline(360, 13) == OutlinePoint{6, 0});
    // t(u) = atan(u π / 180): 45 degrees reach only atan(π/4) = 0.666 of the half side.
    CHECK(squareOutline(44, 13) == OutlinePoint{11, 0});
    CHECK(squareOutline(45, 13) == OutlinePoint{12, 2});
    // Every bearing lands on the outer ring; the corners are never reached.
    for (int a = 0; a <= 360; ++a) {
        const OutlinePoint p = squareOutline(a, 13);
        CHECK(chebyshev(Sector{p.x, p.y}, Sector{}) == kSystemCenter);
        CHECK_FALSE((p.x % 12 == 0 && p.y % 12 == 0));
        // Reference in double: margins from the rounding boundary.
        const auto t = [](int u) { return std::atan(u * std::numbers::pi / 180.0); };
        const int u = a < 45 ? a : a < 90 ? 90 - a : a < 135 ? a - 90 : a < 180 ? 180 - a : a < 225 ? a - 180 : a < 270 ? 270 - a : a < 315 ? a - 270 : 360 - a;
        const double v = 6.5 * t(u);
        if (u != 0) CHECK(std::abs(std::fmod(6.5 + v, 1.0) - 0.5) > 1e-6);
    }
    // Spiral sizes: integer r.
    CHECK(squareOutline(0, 12) == OutlinePoint{6, 0});
    CHECK(squareOutline(90, 12) == OutlinePoint{11, 6});

    // Edge placement: the along-edge coordinate is nudged half a square and rounded half to even.
    CHECK(warpEdgeSector(0, {}) == Sector{6, 0});    // 6.5 -> 6
    CHECK(warpEdgeSector(90, {}) == Sector{12, 6});  // y 6 + 0.5 -> 6
    const Sector odd = warpEdgeSector(44, {});      // x 11 + 0.5 -> 12
    CHECK(odd == Sector{12, 0});
    CHECK(warpEdgeSector(200, {}) == Sector{4, 12});  // x = round(6.5 - 6.5 t(20)) = 4, even: stays
    CHECK(warpEdgeSector(175, {}) == Sector{6, 12});  // x = round(6.5 + 6.5 t(5)) = 7, odd: 7 - 0.5 -> 6
    CHECK(warpEdgeSector(170, {}) == Sector{8, 12});  // x = 8, even: stays
    // Reversed when a warp point stands on the outline sector and the bearing is smaller than its bearing.
    const std::vector<PlacedWarpPoint> there{{Sector{7, 12}, 178}};
    CHECK(warpEdgeSector(175, there) == Sector{8, 12});
    const std::vector<PlacedWarpPoint> smaller{{Sector{7, 12}, 160}};
    CHECK(warpEdgeSector(175, smaller) == Sector{6, 12});
    // Anywhere: straight inward from the outline point.
    CHECK(warpInwardSector(0, 3) == Sector{6, 3});
    CHECK(warpInwardSector(90, 4) == Sector{8, 6});
    CHECK(warpInwardSector(180, 2) == Sector{6, 10});
    CHECK(warpInwardSector(270, 1) == Sector{1, 6});
}

// ---- Quadrant generation ---------------------------------------------------------------------------------------

TEST_CASE("quadrant generation follows the data set") {
    const auto& rs = fixture();
    QuadrantOptions opt;
    opt.systemCount = 30;
    const Generated gen = generate(rs, opt, 7);
    checkInvariants(rs, gen, opt);
    const Galaxy& g = gen.galaxy;
    CHECK(g.quadrantType == "Test Quadrant");
    REQUIRE(g.systems.size() == 30);

    // Min Dist Between Systems = 2: no earlier system within 2 squares on both axes.
    for (size_t i = 0; i < g.systems.size(); ++i)
        for (size_t j = i + 1; j < g.systems.size(); ++j)
            CHECK(std::max(std::abs(g.systems[i].position.x - g.systems[j].position.x),
                           std::abs(g.systems[i].position.y - g.systems[j].position.y)) >= 3);
    // Random placement: R(67) and R(46) clamped into 1..67 / 1..46, so the last column and row stay empty.
    for (const StarSystem& sys : g.systems) {
        CHECK(sys.position.x <= kQuadrantWidth - 2);
        CHECK(sys.position.y <= kQuadrantHeight - 2);
    }

    // Every system got its name from SystemNames.txt while names lasted, then a fallback.
    CHECK(std::count_if(g.systems.begin(), g.systems.end(), [](const StarSystem& s) { return s.name.starts_with("System "); }) == 14);

    const auto busy = rs.findSystemType("Test Busy System");
    const auto it = std::find_if(g.systems.begin(), g.systems.end(), [&](const StarSystem& s) { return s.type == *busy; });
    REQUIRE(it != g.systems.end());
    const StarSystem& sys = *it;
    CHECK(sys.abilities.size() == 1);  // system-wide nebula-like ability, always present
    std::vector<const SpaceObject*> objs;
    for (ObjectId id : sys.objects) objs.push_back(&g.object(id));
    REQUIRE(objs.size() >= 6);
    CHECK(objs[0]->kind == ObjectKind::Star);  // "Sun" alias
    CHECK(objs[0]->sector == Sector(6, 6));    // Ring 1 = center
    CHECK(objs[0]->name == sys.name + " Star");
    CHECK(objs[1]->sector == Sector(2, 3));    // Coord 2,3
    CHECK(objs[1]->surface == "Gas Giant");    // Composition "Gas" matches "Gas Giant"
    CHECK(objs[1]->abilities.size() == 1);     // Old Ruins always rolls
    CHECK(objs[1]->name == sys.name + " I");
    CHECK(objs[2]->sector == objs[1]->sector); // Same As 2
    CHECK(objs[2]->atmosphere == "Methane");
    CHECK(objs[2]->name == sys.name + " I A");  // a moon: the first object's name and a letter
    CHECK(objs[3]->kind == ObjectKind::Asteroids);
    const int d2 = (objs[3]->sector.x - 6) * (objs[3]->sector.x - 6) + (objs[3]->sector.y - 6) * (objs[3]->sector.y - 6);
    CHECK(d2 >= 16);  // Circle Radius 4: the distance, truncated, is 4
    CHECK(d2 < 25);
    CHECK(objs[3]->name == sys.name + " Asteroid Belt II");
    CHECK(objs[3]->value[0] >= 100);  // asteroid value range
    CHECK(objs[4]->kind == ObjectKind::Storm);
    CHECK(chebyshev(objs[4]->sector, Sector{}) == 4);  // Ring 5
    CHECK(objs[4]->abilities.size() == 1);
    CHECK(objs[4]->name == "Storm");
    // "Huge Rock Oxygen" doesn't exist: constraints were relaxed (never to the constructed Ringworld).
    CHECK(objs[5]->size != "Ringworld");
    CHECK(objs[5]->name == sys.name + " III");
    CHECK_FALSE(gen.warnings.empty());
    for (const SpaceObject& o : g.objects)
        if (o.kind == ObjectKind::Planet) {
            CHECK(o.value[0] >= 40);
            CHECK(o.value[0] <= 160);
        }

    // Warp points: a link's first end rolls its system type's warp ability, and the
    // other end copies it. Links made first in a busy system carry turbulence.
    int rough = 0;
    for (const StarSystem& s : g.systems)
        for (ObjectId id : g.warpPoints(s.id)) {
            const SpaceObject& wp = g.object(id);
            const SpaceObject& far = g.object(wp.destination);
            const SystemId first = std::min(s.id, far.system);
            const bool busyFirst = g.system(first).type == *busy;
            CHECK(wp.abilities.size() == (busyFirst ? 1u : 0u));
            CHECK(rs.sectorObjectTypes[wp.sectorType].unusual == busyFirst);
            CHECK(wp.name == "Warp Point");
            rough += busyFirst ? 1 : 0;
        }
    CHECK(rough > 0);
}

TEST_CASE("quadrant generation: the warp network") {
    const auto& rs = fixture();
    for (uint64_t seed : {3ull, 17ull, 99ull}) {
        QuadrantOptions opt;
        opt.systemCount = 40;
        const Generated gen = generate(rs, opt, seed);
        checkInvariants(rs, gen, opt);
        const Galaxy& g = gen.galaxy;
        // Warp points appear in the order the links were made, on the edge the bearing faces.
        for (const StarSystem& sys : g.systems) {
            std::vector<PlacedWarpPoint> before;
            for (ObjectId id : g.warpPoints(sys.id)) {
                const SpaceObject& wp = g.object(id);
                const int b = galaxyBearing(sys.position, g.system(g.object(wp.destination).system).position);
                CHECK(wp.sector == warpEdgeSector(b, before));
                before.push_back({wp.sector, b});
            }
        }
        // Min Angle (30) holds between the links of a system, except for the connectivity pass.
        int violations = 0, links = 0;
        for (const StarSystem& sys : g.systems) {
            const auto nb = g.neighbors(sys.id);
            for (size_t i = 0; i < nb.size(); ++i)
                for (size_t j = i + 1; j < nb.size(); ++j) {
                    ++links;
                    if (bearingDifference(galaxyBearing(sys.position, g.system(nb[i]).position),
                                          galaxyBearing(sys.position, g.system(nb[j]).position)) < 30)
                        ++violations;
                }
        }
        CHECK(violations * 4 < links);
    }

    // Without "all connected" only half as many neighbours are considered, and islands may remain.
    QuadrantOptions loose;
    loose.systemCount = 40;
    loose.allWarpPointsConnected = false;
    const Generated a = generate(rs, loose, 3);
    checkInvariants(rs, a, loose);
    size_t wpLoose = 0, wpTight = 0;
    for (const SpaceObject& o : a.galaxy.objects) wpLoose += o.kind == ObjectKind::WarpPoint;
    loose.allWarpPointsConnected = true;
    const Generated b = generate(rs, loose, 3);
    for (const SpaceObject& o : b.galaxy.objects) wpTight += o.kind == ObjectKind::WarpPoint;
    CHECK(wpLoose < wpTight);

    // A crowded, highly linked quadrant never exceeds 10 warp points per system.
    ruleset::Ruleset dense = fixture();
    dense.quadrantTypes.front().maxWarpPointsPerSystem = 30;
    dense.quadrantTypes.front().minAngleBetweenWarpPoints = 0;
    dense.quadrantTypes.front().minDistanceBetweenSystems = 0;
    QuadrantOptions many;
    many.systemCount = 64;
    const Generated d = generate(dense, many, 5);
    checkInvariants(dense, d, many);
    int full = 0;
    for (const StarSystem& sys : d.galaxy.systems) full += static_cast<int>(d.galaxy.warpPoints(sys.id).size()) == kMaxWarpPoints;
    CHECK(full > 0);
}

TEST_CASE("quadrant generation: quadrant sizes, placements and options") {
    const auto& rs = fixture();  // Maximum Number Of Systems 64: q = 12
    CHECK(maxSystemCount(rs) == 64);
    CHECK(systemCountRange(rs, QuadrantSize::Small) == std::pair{12, 23});
    CHECK(systemCountRange(rs, QuadrantSize::Medium) == std::pair{24, 47});
    CHECK(systemCountRange(rs, QuadrantSize::Large) == std::pair{48, 59});
    for (QuadrantSize size : {QuadrantSize::Small, QuadrantSize::Medium, QuadrantSize::Large})
        for (uint64_t seed = 1; seed <= 5; ++seed) {
            QuadrantOptions opt;
            opt.quadrantType = "Test Spiral";
            opt.size = size;
            const Generated gen = generate(rs, opt, seed);
            const auto [lo, hi] = systemCountRange(rs, size);
            CHECK(static_cast<int>(gen.galaxy.systems.size()) <= hi);
            CHECK(static_cast<int>(gen.galaxy.systems.size()) >= std::min(lo, 20));  // placement may run out of room
            checkInvariants(rs, gen, opt);
        }

    auto withPlacement = [&](std::string how, int minDist) {
        ruleset::Ruleset r = fixture();
        r.quadrantTypes.front().systemPlacement = std::move(how);
        r.quadrantTypes.front().minDistanceBetweenSystems = minDist;
        return r;
    };
    SUBCASE("grid") {
        const auto r = withPlacement("Grid", 1);
        QuadrantOptions opt;
        opt.systemCount = 40;
        const Generated gen = generate(r, opt, 8);
        checkInvariants(r, gen, opt);
        for (const StarSystem& sys : gen.galaxy.systems) {
            CHECK((sys.position.x + 1 - 2) % 5 == 0);  // x = 5 R(13) + 2
            CHECK((sys.position.y + 1 - 2) % 5 == 0);
        }
    }
    SUBCASE("clusters") {
        const auto r = withPlacement("Clusters", 0);
        QuadrantOptions opt;
        opt.systemCount = 40;  // 4 x 3 cells of 15 squares, 4 systems per cluster
        const Generated gen = generate(r, opt, 8);
        checkInvariants(r, gen, opt);
        for (size_t i = 0; i < gen.galaxy.systems.size(); ++i) {
            const int k = static_cast<int>(i + 1) / 4, column = k % 4, row = k / 4;
            const GalaxyPos p = gen.galaxy.systems[i].position;
            CHECK(p.x + 1 >= 4 + 15 * column + 4);
            CHECK(p.x + 1 <= 6 + 4 + 15 * column + 4);
            CHECK(p.y + 1 >= 4 + 15 * row + 1);
            CHECK(p.y + 1 <= 6 + 4 + 15 * row + 1);
        }
    }
    SUBCASE("spiral") {
        const auto r = withPlacement("Spiral", 0);
        QuadrantOptions opt;
        opt.systemCount = 30;
        const Generated gen = generate(r, opt, 8);
        checkInvariants(r, gen, opt);
        for (size_t i = 0; i < gen.galaxy.systems.size(); ++i) {
            const int radius = 6 + 3 * (static_cast<int>(i + 1) / 10);
            const GalaxyPos p = gen.galaxy.systems[i].position;
            // On the outline of the square of side 2r whose corner is (34 - r, 23 - r).
            const int ox = p.x + 1 - (34 - radius), oy = p.y + 1 - (23 - radius);
            CHECK((ox == 0 || oy == 0 || ox == 2 * radius - 1 || oy == 2 * radius - 1));
        }
    }
    SUBCASE("diffuse") {
        const auto r = withPlacement("Diffuse", 0);
        QuadrantOptions opt;
        opt.systemCount = 30;
        const Generated gen = generate(r, opt, 8);
        checkInvariants(r, gen, opt);
        const auto& sys = gen.galaxy.systems;
        for (size_t i = 0; i < sys.size(); ++i)
            for (size_t j = i + 1; j < sys.size(); ++j)
                CHECK(std::max(std::abs(sys[i].position.x - sys[j].position.x), std::abs(sys[i].position.y - sys[j].position.y)) >= 3);
    }
    SUBCASE("running out of room keeps the systems placed so far") {
        const auto r = withPlacement("Grid", 5);  // one system per lattice point at most
        QuadrantOptions opt;
        opt.systemCount = 60;
        const Generated gen = generate(r, opt, 8);
        CHECK(gen.galaxy.systems.size() < 60);
        CHECK_FALSE(gen.warnings.empty());
        checkInvariants(r, gen, opt);
    }
}

TEST_CASE("quadrant generation options and determinism") {
    const auto& rs = fixture();
    QuadrantOptions opt;
    opt.systemCount = 25;
    opt.quadrantType = "test spiral";
    const Generated a = generate(rs, opt, 99);
    const Generated b = generate(rs, opt, 99);
    checkInvariants(rs, a, opt);
    CHECK(a.galaxy.systems.size() == b.galaxy.systems.size());
    for (size_t i = 0; i < a.galaxy.systems.size(); ++i) {
        CHECK(a.galaxy.systems[i].position == b.galaxy.systems[i].position);
        CHECK(a.galaxy.systems[i].name == b.galaxy.systems[i].name);
    }
    CHECK(a.galaxy.objects.size() == b.galaxy.objects.size());

    opt.noWarpPoints = true;
    const Generated none = generate(rs, opt, 5);
    CHECK(std::none_of(none.galaxy.objects.begin(), none.galaxy.objects.end(),
                       [](const SpaceObject& o) { return o.kind == ObjectKind::WarpPoint; }));

    opt.noWarpPoints = false;
    opt.warpPointsAnywhere = true;
    opt.quadrantType = "Test Quadrant";
    opt.noRuins = true;
    const Generated anywhere = generate(rs, opt, 5);
    checkInvariants(rs, anywhere, opt);
    for (const SpaceObject& o : anywhere.galaxy.objects) {
        for (const auto& ab : o.abilities) CHECK(ab.type != "Ancient Ruins");
        if (o.kind != ObjectKind::WarpPoint) continue;
        // At most 4 squares in from the edge point that faces the destination.
        const StarSystem& sys = anywhere.galaxy.system(o.system);
        const int bearing = galaxyBearing(sys.position, anywhere.galaxy.system(anywhere.galaxy.object(o.destination).system).position);
        bool found = false;
        for (int k = 0; k <= 4; ++k) found = found || warpInwardSector(bearing, k) == o.sector;
        CHECK(found);
    }

    Rng rng(1);
    opt.quadrantType = "No Such Quadrant";
    CHECK_FALSE(generateQuadrant(rs, opt, rng).has_value());
    opt.quadrantType.clear();
    opt.systemCount = 65;  // Settings cap is 64
    CHECK_FALSE(generateQuadrant(rs, opt, rng).has_value());
}

TEST_CASE("quadrant generation: system type roll") {
    // Chances that sum to less than 1000 repeat the list; the last record with a name wins.
    ruleset::Ruleset rs = fixture();
    ruleset::SystemType twin = rs.systemTypes.front();
    twin.description = "The later record of the same name.";
    rs.systemTypes.push_back(twin);
    auto& chances = rs.quadrantTypes.front().systemTypeChances;
    chances[0].second = 300;  // Test Single Star
    chances[1].second = 100;  // Test Busy System
    rs.reindex();
    QuadrantOptions opt;
    opt.systemCount = 60;
    const Generated gen = generate(rs, opt, 4);
    int single = 0, busy = 0;
    for (const StarSystem& sys : gen.galaxy.systems) {
        CHECK(sys.type.index() != 0);  // never the first "Test Single Star" record
        if (sys.type.index() == rs.systemTypes.size() - 1) ++single;
        else ++busy;
    }
    CHECK(single > busy);

    // All chances 0: the first SystemTypes record.
    chances[0].second = 0;
    chances[1].second = 0;
    const Generated zero = generate(rs, opt, 4);
    for (const StarSystem& sys : zero.galaxy.systems) CHECK(sys.type.index() == 0);
}

TEST_CASE("quadrant generation: position specifiers, comets and names") {
    // Comets create nothing but keep their template index.
    const ruleset::Ruleset rs = withSystem({
        obj("Star", "Ring 1"),
        obj("Comet", "Ring 6"),
        obj("Planet", "Same As 2"),          // the comet was never placed: (0, 0)
        obj("Planet", "Ring 3", "Medium", "None", "Rock"),
        obj("Planet", "Same As 4", "Small", "Methane", "Ice"),
        obj("Planet", "Same As 4", "Small", "Methane", "Ice"),
        obj("Asteroids", "Circle Radius 6"),
        obj("Planet", "Coord 12,12", "Large", "Hydrogen", "Gas"),
        obj("Planet", "Somewhere"),          // unrecognised: random, with a warning
        obj("Planet", "Same As 1"),          // the star's sector
    });
    QuadrantOptions opt;
    opt.quadrantType = "Test Custom Quadrant";
    opt.systemCount = 3;
    const Generated gen = generate(rs, opt, 12);
    checkInvariants(rs, gen, opt);
    const Galaxy& g = gen.galaxy;
    const StarSystem& sys = g.systems.front();
    std::vector<const SpaceObject*> o;
    for (ObjectId id : sys.objects)
        if (g.object(id).kind != ObjectKind::WarpPoint) o.push_back(&g.object(id));
    REQUIRE(o.size() == 9);
    CHECK(o[0]->kind == ObjectKind::Star);
    CHECK(o[1]->sector == Sector(0, 0));
    CHECK(o[1]->name == sys.name + " I");
    CHECK(chebyshev(o[2]->sector, Sector{}) == 2);  // Ring 3
    CHECK(o[2]->name == sys.name + " II");
    CHECK(o[3]->sector == o[2]->sector);
    CHECK(o[3]->name == sys.name + " II A");
    CHECK(o[4]->name == sys.name + " II B");
    CHECK(o[5]->kind == ObjectKind::Asteroids);
    const int d2 = (o[5]->sector.x - 6) * (o[5]->sector.x - 6) + (o[5]->sector.y - 6) * (o[5]->sector.y - 6);
    CHECK(d2 >= 36);  // trunc(distance) == 6
    CHECK(d2 < 49);
    CHECK(o[5]->name == sys.name + " Asteroid Belt III");
    CHECK(o[6]->sector == Sector(12, 12));
    CHECK(o[6]->surface == "Gas Giant");
    CHECK(o[8]->sector == Sector(6, 6));
    CHECK(o[8]->name == sys.name + " Star A");  // a planet in the star's sector
    CHECK(std::any_of(gen.warnings.begin(), gen.warnings.end(), [](const std::string& w) { return w.find("Somewhere") != std::string::npos; }));

    // A ring draws on its own square ring and avoids occupied sectors while it can.
    const ruleset::Ruleset ringRules = withSystem({obj("Planet", "Ring 2"), obj("Planet", "Ring 2"), obj("Planet", "Ring 2"),
                                                   obj("Planet", "Ring 2"), obj("Planet", "Ring 2"), obj("Planet", "Ring 2"),
                                                   obj("Planet", "Ring 2"), obj("Planet", "Ring 2")});
    const Generated ring = generate(ringRules, opt, 3);
    std::set<Sector> seen;
    for (ObjectId id : ring.galaxy.systems.front().objects) {
        const SpaceObject& p = ring.galaxy.object(id);
        if (p.kind == ObjectKind::WarpPoint) continue;
        CHECK(chebyshev(p.sector, Sector{}) == 1);
        seen.insert(p.sector);
    }
    CHECK(seen.size() == 8);  // 101 draws find every free sector of the ring
}

TEST_CASE("quadrant generation: planet values and conditions") {
    const auto& rs = fixture();
    QuadrantOptions opt;
    opt.systemCount = 60;
    const Generated gen = generate(rs, opt, 21);
    std::set<uint64_t> planetConditions, asteroidConditions;
    for (const SpaceObject& o : gen.galaxy.objects) {
        if (o.kind == ObjectKind::Planet) planetConditions.insert(o.conditions.bits);
        if (o.kind == ObjectKind::Asteroids) asteroidConditions.insert(o.conditions.bits);
    }
    // 0.5, 0.6 ... 1.5 as doubles (R / 10 + 0.5), all eleven values; asteroid
    // fields half of that. Non-negative doubles order like their bit patterns.
    CHECK(planetConditions.size() == 11);
    CHECK(Conditions{*planetConditions.begin()} == Conditions::hundredths(50));
    CHECK(Conditions{*planetConditions.rbegin()} == kOptimalConditions);
    for (uint64_t b : planetConditions) {
        const Conditions c{b};
        CHECK(c.inHundredths() % 10 == 0);
        CHECK(c == Conditions::of(xmath::Ext(c.inHundredths() / 10 - 5) / xmath::Ext(10) + xmath::Ext(1) / xmath::Ext(2)));
    }
    for (uint64_t b : asteroidConditions) {
        const Conditions c{b};
        CHECK(c.inHundredths() % 5 == 0);
        CHECK(Conditions::of(c.value() * xmath::Ext(2)).inHundredths() % 10 == 0);
    }
}

// ---- Empire placement (spec 01 §3.6) -------------------------------------------------------------------------

TEST_CASE("homeworld placement") {
    const auto& rs = fixture();
    QuadrantOptions opt;
    opt.systemCount = 30;
    Generated gen = generate(rs, opt, 11);
    const std::vector<EmpireStart> empires{{"Ice Folk", "Ice", "Methane"}, {"Rock Folk", "Rock", "Oxygen"}, {"Gas Folk", "Gas Giant", "Hydrogen"}};
    const size_t before = gen.galaxy.objects.size();
    Rng rng(3);
    auto homes = placeHomeworlds(gen.galaxy, rs, empires, PlacementOptions{}, rng);
    REQUIRE(homes.has_value());
    REQUIRE(homes->size() == 3);
    std::set<SystemId> systems;
    for (size_t i = 0; i < homes->size(); ++i) {
        const SpaceObject& p = gen.galaxy.object((*homes)[i]);
        CHECK(p.kind == ObjectKind::Planet);
        CHECK(p.surface == empires[i].surface);
        CHECK(p.atmosphere == empires[i].atmosphere);
        CHECK(rs.systemTypes[gen.galaxy.system(p.system).type.index()].empiresCanStartIn);
        CHECK(systems.insert(p.system).second);
    }
    CHECK(homePlanetSize(rs, HomeValue::Medium, "Ice", "Methane") == 3);  // only Small exists: Medium is kept
    CHECK(homePlanetSize(rs, HomeValue::Medium, "Rock", "Oxygen") == 4);  // only Large natural Rock/Oxygen
    CHECK(homePlanetSize(rs, HomeValue::Low, "Gas Giant", "Hydrogen") == 4);
    // Natural planets of the home size are used as they are: never converted.
    for (size_t i = 1; i < homes->size(); ++i) {
        const SpaceObject& p = gen.galaxy.object((*homes)[i]);
        CHECK(p.id.index() < before);
        CHECK(stellarSizeOf(rs, p) == 4);
        CHECK(rs.sectorObjectTypes[p.sectorType].planetAtmosphere == empires[i].atmosphere);
    }
    // No Medium Ice/Methane planet exists anywhere: that homeworld was created,
    // in a start-eligible system, with natural conditions and a numeral name.
    const SpaceObject& ice = gen.galaxy.object((*homes)[0]);
    CHECK(ice.id.index() >= before);
    CHECK(ice.conditions >= Conditions::hundredths(50));
    CHECK(ice.name.starts_with(gen.galaxy.system(ice.system).name + " "));
}

TEST_CASE("homeworld placement: jump distance tiers and sizes") {
    // A chain of ten start-eligible systems, each with one Large Rock/Oxygen planet.
    const auto& rs = fixture();
    const uint32_t rockOxygen = 4, warpType = 8;  // SectType records of the fixture
    REQUIRE(rs.sectorObjectTypes[rockOxygen].planetAtmosphere == "Oxygen");
    Galaxy g;
    const auto single = *rs.findSystemType("Test Single Star");
    for (uint32_t i = 0; i < 10; ++i) {
        StarSystem sys;
        sys.id = SystemId{i};
        sys.name = std::format("Chain {}", i);
        sys.position = {static_cast<int>(i) * 3, 5};
        sys.type = single;
        g.systems.push_back(sys);
    }
    auto add = [&](uint32_t sys, ObjectKind kind, uint32_t type, Sector at) {
        SpaceObject o;
        o.id = ObjectId{g.objects.size()};
        o.kind = kind;
        o.system = SystemId{sys};
        o.sector = at;
        applySectorType(rs, o, type);
        g.systems[sys].objects.push_back(o.id);
        g.objects.push_back(o);
        return o.id;
    };
    for (uint32_t i = 0; i < 10; ++i) add(i, ObjectKind::Planet, rockOxygen, Sector{3, 3});
    for (uint32_t i = 0; i + 1 < 10; ++i) {
        const ObjectId a = add(i, ObjectKind::WarpPoint, warpType, Sector{12, 6});
        const ObjectId b = add(i + 1, ObjectKind::WarpPoint, warpType, Sector{0, 6});
        g.objects[a.index()].destination = b;
        g.objects[b.index()].destination = a;
    }
    // S = 10, P = 2: the second home is more than trunc(0.8 x 5) = 4 jumps from the first.
    const std::vector<EmpireStart> two{{"A", "Rock", "Oxygen"}, {"B", "Rock", "Oxygen"}};
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Galaxy copy = g;
        Rng rng(seed);
        auto homes = placeHomeworlds(copy, rs, two, PlacementOptions{}, rng);
        REQUIRE(homes.has_value());
        const int first = static_cast<int>(copy.object((*homes)[0]).system.index());
        const int second = static_cast<int>(copy.object((*homes)[1]).system.index());
        // Some system lies more than 4 jumps away wherever the first home is, so attempt 1 succeeds.
        CHECK(std::abs(second - first) > 4);
        CHECK(copy.objects.size() == g.objects.size());  // nothing created
    }
    // Not evenly distributed: anywhere but the first home's system.
    PlacementOptions loose;
    loose.evenlyDistributed = false;
    Galaxy copy = g;
    Rng rng(4);
    auto homes = placeHomeworlds(copy, rs, two, loose, rng);
    REQUIRE(homes.has_value());
    CHECK(copy.object((*homes)[0]).system != copy.object((*homes)[1]).system);

    // Good homes want Large, Bad ones Small (raised to Large here: no smaller Rock/Oxygen exists).
    CHECK(homePlanetSize(rs, HomeValue::High, "Rock", "Oxygen") == 4);
    CHECK(homePlanetSize(rs, HomeValue::Low, "Rock", "Oxygen") == 4);
    CHECK(homePlanetSize(rs, HomeValue::Low, "Rock", "None") == 3);  // Medium Rock/None is the smallest
    // With every player planet the same size and none of that size, a planet is created.
    PlacementOptions strict;
    strict.homeValue = HomeValue::Low;
    Galaxy small = g;
    Rng rng3(5);
    const std::vector<EmpireStart> bare{{"C", "Rock", "None"}};
    auto made = placeHomeworlds(small, rs, bare, strict, rng3);
    REQUIRE(made.has_value());
    CHECK(made->front().index() >= g.objects.size());
    CHECK(small.object(made->front()).atmosphere == "None");
    CHECK(stellarSizeOf(rs, small.object(made->front())) == 3);
}

// ---- Starting planets (spec 01 §3.6, spec 02 §9) ------------------------------------------------------------

namespace {

game::GameState startGame(const game::Rules& r, int planets, int systems, uint64_t seed, bool withNeutral) {
    game::GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = systems;
    setup.options.startingPlanets = planets;
    for (int i = 0; i < 2; ++i) {
        game::EmpireSetup e;
        e.name = "Empire " + std::to_string(i + 1);
        setup.empires.push_back(e);
    }
    if (withNeutral) {
        game::EmpireSetup n;
        n.name = "Neutral";
        n.kind = game::PlayerKind::Neutral;
        setup.empires.push_back(n);
    }
    auto g = game::createGame(r, setup);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    return std::move(*g);
}

std::vector<const game::Colony*> coloniesOf(const game::GameState& s, game::EmpireId e) {
    std::vector<const game::Colony*> out;
    for (const auto& c : s.colonies)
        if (c && c->owner == e) out.push_back(&*c);
    return out;
}

} // namespace

TEST_CASE("setup: every starting planet gets the homeworld setup") {
    // Planets of the starting systems always carry ruins here, to see them removed.
    ruleset::Ruleset data = test::buildEngineRuleset();
    for (auto& t : data.systemTypes)
        for (auto& o : t.objects)
            if (o.physicalType == "Planet") o.stellarAbilityType = "Old Ruins";
    const game::Rules r{std::move(data)};
    const int base = static_cast<int>(r.setting("Plr Planet Value Medium Percent", 100));
    for (uint64_t seed : {3ull, 8ull}) {
        game::GameState s = startGame(r, 3, 12, seed, true);
        for (const game::Empire& e : s.empires) {
            INFO(e.name << " seed " << seed);
            const auto colonies = coloniesOf(s, e.id);
            // A neutral empire always gets one starting planet.
            REQUIRE(colonies.size() == (e.kind == game::PlayerKind::Neutral ? 1u : 3u));
            const game::ObjectId home = test::homeworld(s, e.id).planet;
            const auto jumps = warpJumps(s.galaxy, s.galaxy.object(home).system);
            int capitals = 0;
            for (const game::Colony* c : colonies) {
                const game::SpaceObject& p = s.galaxy.object(c->planet);
                capitals += c->homeworld ? 1 : 0;
                CHECK(p.kind == game::ObjectKind::Planet);
                CHECK(p.atmosphere == e.race.atmosphere);
                CHECK(p.surface == e.race.nativeSurface);
                CHECK(c->totalPopulation() == game::maxPopulation(r, s, *c));
                CHECK(static_cast<int>(c->facilities.size()) == game::facilitySlots(r, s, *c));
                for (int v : p.value) {
                    CHECK(v >= base - 4);
                    CHECK(v <= base + 5);
                }
                CHECK(p.abilities.empty());  // ruins removed
                CHECK(e.hasExplored(p.system));
                // 12 systems are not over 60 % of 64: one warp jump at most.
                const int d = jumps[p.system.index()];
                CHECK(d >= 0);
                CHECK(d <= 1);
            }
            CHECK(capitals == 1);
        }
        // Planets that are nobody's starting planet keep their ruins.
        bool ruinsLeft = false;
        for (const game::SpaceObject& p : s.galaxy.objects)
            if (p.kind == game::ObjectKind::Planet && !s.colony(p.id) && !p.abilities.empty()) ruinsLeft = true;
        CHECK(ruinsLeft);
    }
    // Ten planets per empire are more than the neighbourhood holds: the rest are created,
    // on the inner 11 x 11 area. The quadrant alone (as createGame makes it) tells which.
    const game::GameState crowded = startGame(r, 10, 12, 5, false);
    Rng seeded(5);
    Rng galaxyRng = seeded.fork();
    QuadrantOptions qo;
    qo.systemCount = 12;
    const auto natural = generateQuadrant(r.data(), qo, galaxyRng);
    REQUIRE(natural.has_value());
    int created = 0;
    for (const game::Empire& e : crowded.empires) {
        const auto colonies = coloniesOf(crowded, e.id);
        CHECK(colonies.size() == 10u);
        for (const game::Colony* c : colonies) {
            if (c->homeworld || c->planet.index() < natural->galaxy.objects.size()) continue;
            ++created;
            const game::SpaceObject& p = crowded.galaxy.object(c->planet);
            CHECK(p.sector.x >= 1);
            CHECK(p.sector.x <= 11);
            CHECK(p.sector.y >= 1);
            CHECK(p.sector.y <= 11);
            CHECK(p.name.starts_with(crowded.galaxy.system(p.system).name + " "));
        }
    }
    CHECK(created > 0);
}

TEST_CASE("setup: starting facilities come in the original's order") {
    const game::Rules& r = test::engineRules();
    game::GameState s = startGame(r, 1, 12, 7, false);
    const game::Colony& home = test::homeworld(s, game::EmpireId{0u});
    // Rock/Oxygen homeworlds are Large in the test data: 14 slots.
    REQUIRE(game::facilitySlots(r, s, home) == 14);
    std::vector<std::string> names;
    for (uint32_t f : home.facilities) names.push_back(r.facility(f).name);
    const std::vector<std::string> expected{"Test Spaceport", "Test Space Yard", "Test Depot", "Test Mine", "Test Refinery",
                                            "Test Farm", "Test Lab", "Test Mine", "Test Lab", "Test Mine",
                                            "Test Lab", "Test Mine", "Test Lab", "Test Mine"};
    CHECK(names == expected);

    // No Spaceports: the space yard comes first.
    ruleset::Ruleset data = test::buildEngineRuleset();
    ruleset::RacialTrait free;
    free.name = "Test Free Trade";
    free.traitType = "No Spaceports";
    data.racialTraits.push_back(free);
    const game::Rules noPorts{std::move(data)};
    game::GameSetup setup;
    setup.seed = 7;
    setup.options.systemCount = 12;
    game::EmpireSetup e;
    e.name = "Traders";
    game::Race race;
    race.traits.push_back(static_cast<uint32_t>(noPorts.data().racialTraits.size() - 1));
    e.customRace = race;
    setup.empires.push_back(e);
    auto g = game::createGame(noPorts, setup);
    REQUIRE(g.has_value());
    const game::Colony& traders = test::homeworld(*g, game::EmpireId{0u});
    REQUIRE_FALSE(traders.facilities.empty());
    CHECK(noPorts.facility(traders.facilities.front()).name == "Test Space Yard");
    for (uint32_t f : traders.facilities) CHECK(noPorts.facility(f).name != "Test Spaceport");
}

TEST_CASE("installed data set: every quadrant type generates cleanly (opt-in)") {
    const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
    if (!env) {
        MESSAGE("skipped: set OPENSE4_CLASSIC_DATA to test against an installed data set");
        return;
    }
    const auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
    REQUIRE(dir);
    auto loaded = ruleset::loadRuleset(*dir);
    REQUIRE(loaded.ruleset);
    const ruleset::Ruleset& rs = *loaded.ruleset;
    for (const auto& q : rs.quadrantTypes) {
        for (QuadrantSize size : {QuadrantSize::Small, QuadrantSize::Medium, QuadrantSize::Large}) {
            QuadrantOptions opt;
            opt.quadrantType = q.name;
            opt.size = size;
            INFO(q.name << " size " << static_cast<int>(size));
            const Generated gen = generate(rs, opt, static_cast<uint64_t>(size) * 31 + q.name.size());
            checkInvariants(rs, gen, opt);
            const auto [lo, hi] = systemCountRange(rs, size);
            CHECK(static_cast<int>(gen.galaxy.systems.size()) <= hi);
            for (const auto& w : gen.warnings) MESSAGE(w);
        }
    }
}
