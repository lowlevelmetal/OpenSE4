#include "game/generate.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
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
    std::vector<bool> seen(g.systems.size(), false);
    std::vector<SystemId> stack{SystemId{0u}};
    seen[0] = true;
    while (!stack.empty()) {
        const SystemId s = stack.back();
        stack.pop_back();
        for (SystemId n : g.neighbors(s))
            if (!seen[n.index()]) {
                seen[n.index()] = true;
                stack.push_back(n);
            }
    }
    return std::all_of(seen.begin(), seen.end(), [](bool b) { return b; });
}

// Structural invariants every generated quadrant must satisfy.
void checkInvariants(const ruleset::Ruleset& rs, const Generated& gen, const QuadrantOptions& opt) {
    const Galaxy& g = gen.galaxy;
    REQUIRE(static_cast<int>(g.systems.size()) == opt.systemCount);
    std::set<std::string> names;
    for (const StarSystem& sys : g.systems) {
        CHECK(names.insert(sys.name).second);
        CHECK(sys.position.x >= 0);
        CHECK(sys.position.y >= 0);
        CHECK(sys.position.x < g.width);
        CHECK(sys.position.y < g.height);
        for (ObjectId id : sys.objects) {
            const SpaceObject& o = g.object(id);
            CHECK(o.system == sys.id);
            CHECK(o.sector.valid());
            CHECK(o.sectorType < rs.sectorObjectTypes.size());
            CHECK(parseObjectKind(rs.sectorObjectTypes[o.sectorType].physicalType) == o.kind);
            if (o.kind == ObjectKind::WarpPoint) {
                REQUIRE(o.destination.valid());
                const SpaceObject& other = g.object(o.destination);
                CHECK(other.kind == ObjectKind::WarpPoint);
                CHECK(other.destination == o.id);
                CHECK(other.system != o.system);
                if (!opt.warpPointsAnywhere) CHECK(chebyshev(o.sector, Sector{}) == kSystemCenter);  // outer edge
            }
        }
    }
    if (opt.allWarpPointsConnected && !opt.noWarpPoints) CHECK(connected(g));
}

} // namespace

TEST_CASE("quadrant generation follows the data set") {
    const auto& rs = fixture();
    QuadrantOptions opt;
    opt.systemCount = 30;
    const Generated gen = generate(rs, opt, 7);
    checkInvariants(rs, gen, opt);
    const Galaxy& g = gen.galaxy;
    CHECK(g.quadrantType == "Test Quadrant");

    // Min Dist Between Systems = 2 empty squares -> Chebyshev distance >= 3.
    for (size_t i = 0; i < g.systems.size(); ++i)
        for (size_t j = i + 1; j < g.systems.size(); ++j)
            CHECK(std::max(std::abs(g.systems[i].position.x - g.systems[j].position.x),
                           std::abs(g.systems[i].position.y - g.systems[j].position.y)) >= 3);

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
    CHECK(objs[1]->sector == Sector(2, 3));    // Coord 2,3
    CHECK(objs[1]->surface == "Gas Giant");    // Composition "Gas" matches "Gas Giant"
    CHECK(objs[1]->abilities.size() == 1);     // Old Ruins always rolls
    CHECK(objs[2]->sector == objs[1]->sector); // Same As 2
    CHECK(objs[2]->atmosphere == "Methane");
    CHECK(objs[3]->kind == ObjectKind::Asteroids);
    CHECK(std::lround(std::hypot(objs[3]->sector.x - 6.0, objs[3]->sector.y - 6.0)) == 4);  // Circle Radius 4
    CHECK(objs[3]->value[0] >= 100);  // asteroid value range
    CHECK(objs[4]->kind == ObjectKind::Storm);
    CHECK(chebyshev(objs[4]->sector, Sector{}) == 4);  // Ring 5
    CHECK(objs[4]->abilities.size() == 1);
    // "Huge Rock Oxygen" doesn't exist: constraints were relaxed (never to the constructed Ringworld).
    CHECK(objs[5]->size != "Ringworld");
    CHECK_FALSE(gen.warnings.empty());
    for (const SpaceObject& o : g.objects)
        if (o.kind == ObjectKind::Planet) {
            CHECK(o.value[0] >= 40);
            CHECK(o.value[0] <= 160);
        }
    // Warp points in the busy system all carry the turbulence ability.
    for (ObjectId id : g.warpPoints(sys.id)) CHECK(g.object(id).abilities.size() == 1);
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
    for (const SpaceObject& o : anywhere.galaxy.objects)
        for (const auto& ab : o.abilities) CHECK(ab.type != "Ancient Ruins");

    Rng rng(1);
    opt.quadrantType = "No Such Quadrant";
    CHECK_FALSE(generateQuadrant(rs, opt, rng).has_value());
    opt.quadrantType.clear();
    opt.systemCount = 65;  // Settings cap is 64
    CHECK_FALSE(generateQuadrant(rs, opt, rng).has_value());
}

TEST_CASE("homeworld placement") {
    const auto& rs = fixture();
    QuadrantOptions opt;
    opt.systemCount = 30;
    Generated gen = generate(rs, opt, 11);
    const std::vector<EmpireStart> empires{{"Ice Folk", "Ice", "Methane"}, {"Rock Folk", "Rock", "Oxygen"}, {"Gas Folk", "Gas Giant", "Hydrogen"}};
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
        CHECK(p.value == std::array<int, 3>{110, 110, 110});  // Plr Planet Value Medium Percent
        CHECK(rs.systemTypes[gen.galaxy.system(p.system).type.index()].empiresCanStartIn);
        CHECK(systems.insert(p.system).second);
    }
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
        for (int count : {20, 60, 100}) {
            QuadrantOptions opt;
            opt.quadrantType = q.name;
            opt.systemCount = count;
            INFO(q.name << " with " << count << " systems");
            const Generated gen = generate(rs, opt, uint64_t(count) * 31 + q.name.size());
            checkInvariants(rs, gen, opt);
            for (const auto& w : gen.warnings) MESSAGE(w);
        }
    }
}
