// Map files and map starting points (docs/spec/01 §3.6, §12; the format is
// ours, docs/MAPS.md).

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "client/classic/screens/setup_model.hpp"
#include "game/generate.hpp"
#include "game/map_file.hpp"
#include "game/query.hpp"
#include "game/setup.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

using namespace opense4;
using namespace opense4::game;
namespace fs = std::filesystem;

namespace {

const ruleset::Ruleset& fixture() {
    static const ruleset::Ruleset rs = [] {
        auto r = ruleset::loadRuleset(fs::path(OPENSE4_FIXTURE_DIR) / "minimal_dataset");
        REQUIRE(r.ruleset);
        REQUIRE(r.diagnostics.errors.empty());
        return std::move(*r.ruleset);
    }();
    return rs;
}

// SectType records of the fixture.
constexpr uint32_t kIceMethaneSmall = 2, kRockOxygenLarge = 4, kWarpType = 8;

void sameAbilities(const std::vector<ruleset::Ability>& a, const std::vector<ruleset::Ability>& b) {
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].type == b[i].type);
        CHECK(a[i].value1 == b[i].value1);
        CHECK(a[i].value2 == b[i].value2);
        CHECK(a[i].description == b[i].description);
    }
}

void sameGalaxy(const Galaxy& a, const Galaxy& b) {
    CHECK(a.width == b.width);
    CHECK(a.height == b.height);
    CHECK(a.quadrantType == b.quadrantType);
    REQUIRE(a.systems.size() == b.systems.size());
    REQUIRE(a.objects.size() == b.objects.size());
    for (size_t i = 0; i < a.systems.size(); ++i) {
        const StarSystem& x = a.systems[i];
        const StarSystem& y = b.systems[i];
        CHECK(x.id == y.id);
        CHECK(x.name == y.name);
        CHECK(x.position == y.position);
        CHECK(x.type == y.type);
        CHECK(x.physicalType == y.physicalType);
        CHECK(x.objects == y.objects);
        sameAbilities(x.abilities, y.abilities);
    }
    for (size_t i = 0; i < a.objects.size(); ++i) {
        const SpaceObject& x = a.objects[i];
        const SpaceObject& y = b.objects[i];
        INFO("object " << i << " " << x.name);
        CHECK(x.id == y.id);
        CHECK(x.kind == y.kind);
        CHECK(x.system == y.system);
        CHECK(x.sector == y.sector);
        CHECK(x.sectorType == y.sectorType);
        CHECK(x.name == y.name);
        CHECK(x.size == y.size);
        CHECK(x.surface == y.surface);
        CHECK(x.atmosphere == y.atmosphere);
        CHECK(x.conditions == y.conditions);
        CHECK(x.value == y.value);
        CHECK(x.starAge == y.starAge);
        CHECK(x.starColor == y.starColor);
        CHECK(x.starLuminosity == y.starLuminosity);
        CHECK(x.destination == y.destination);
        sameAbilities(x.abilities, y.abilities);
    }
}

// A chain of start-eligible systems linked by warp points; system i has its
// planets at the given SectType records on sector (3, 3).
Galaxy chain(const ruleset::Ruleset& rs, std::vector<std::optional<uint32_t>> planets) {
    Galaxy g;
    g.width = kQuadrantWidth;
    g.height = kQuadrantHeight;
    const auto single = *rs.findSystemType("Test Single Star");
    for (uint32_t i = 0; i < planets.size(); ++i) {
        StarSystem sys;
        sys.id = SystemId{i};
        sys.name = std::format("Link {}", i);
        sys.position = {static_cast<int>(i) * 4, 7};
        sys.type = single;
        sys.physicalType = "Normal";
        g.systems.push_back(sys);
    }
    auto add = [&](uint32_t sys, ObjectKind kind, uint32_t type, Sector at, std::string name) {
        SpaceObject o;
        o.id = ObjectId{g.objects.size()};
        o.kind = kind;
        o.system = SystemId{sys};
        o.sector = at;
        o.name = std::move(name);
        applySectorType(rs, o, type);
        g.systems[sys].objects.push_back(o.id);
        g.objects.push_back(o);
        return o.id;
    };
    for (uint32_t i = 0; i < planets.size(); ++i)
        if (planets[i]) add(i, ObjectKind::Planet, *planets[i], Sector{3, 3}, std::format("Link {} I", i));
    for (uint32_t i = 0; i + 1 < planets.size(); ++i) {
        const ObjectId a = add(i, ObjectKind::WarpPoint, kWarpType, Sector{12, 6}, "Warp Point");
        const ObjectId b = add(i + 1, ObjectKind::WarpPoint, kWarpType, Sector{0, 6}, "Warp Point");
        g.objects[a.index()].destination = b;
        g.objects[b.index()].destination = a;
    }
    return g;
}

struct TempDir {
    test::TempDir dir;
    fs::path path;
    explicit TempDir(std::string_view tag) : dir(std::format("maps_{}", tag)), path(dir.path()) {}
};

} // namespace

TEST_CASE("maps: a generated quadrant survives a map file unchanged") {
    const auto& rs = fixture();
    QuadrantOptions opt;
    opt.systemCount = 25;
    Rng rng(19);
    auto gen = generateQuadrant(rs, opt, rng);
    REQUIRE(gen.has_value());
    QuadrantMap m;
    m.name = "Round \"trip\" \\ test";
    m.galaxy = gen->galaxy;
    // Abilities with odd text survive too.
    m.galaxy.systems[0].abilities.push_back({"System - Movement Random", "Drifts\tabout", "2", ""});
    m.galaxy.objects[0].abilities.push_back({"Sector - Damage", "50", "", "A note"});
    m.startingPoints = {{SystemId{0u}, Sector{3, 4}, 0}, {SystemId{2u}, Sector{5, 5}, kCommonStart}, {SystemId{1u}, Sector{0, 12}, 3}};

    const std::string text = mapToText(rs, m);
    CHECK(text.starts_with("# An OpenSE4 map"));
    auto back = mapFromText(rs, text);
    REQUIRE_MESSAGE(back.has_value(), (back ? std::string{} : back.error()));
    CHECK(back->warnings.empty());
    CHECK(back->map.name == m.name);
    sameGalaxy(m.galaxy, back->map.galaxy);
    CHECK(back->map.startingPoints == m.startingPoints);
    // Writing it again gives the same text.
    CHECK(mapToText(rs, back->map) == text);

    // Through a file under a data folder of ours.
    TempDir dir("roundtrip");
    const fs::path file = dir.path / "maps" / ("Round trip" + std::string(kMapExtension));
    REQUIRE(saveMapFile(file, rs, m).has_value());
    auto loaded = loadMapFile(file, rs);
    REQUIRE(loaded.has_value());
    sameGalaxy(m.galaxy, loaded->map.galaxy);
    CHECK(mapFileStem("  My: map/1 ") == "My map1");
    CHECK(mapFileStem("???") == "Map");
}

TEST_CASE("maps: removed objects are left out and warp links stay paired") {
    const auto& rs = fixture();
    Galaxy g = chain(rs, {kRockOxygenLarge, std::nullopt, kIceMethaneSmall});
    // Stellar manipulation leaves tombstones: the first planet is gone from its system.
    const ObjectId gone = g.systems[0].objects.front();
    std::erase(g.systems[0].objects, gone);
    QuadrantMap m;
    m.galaxy = g;
    auto back = mapFromText(rs, mapToText(rs, m));
    REQUIRE(back.has_value());
    const Galaxy& b = back->map.galaxy;
    CHECK(b.objects.size() == g.objects.size() - 1);
    for (const SpaceObject& o : b.objects) {
        CHECK(o.name != "Link 0 I");
        if (o.kind != ObjectKind::WarpPoint) continue;
        REQUIRE(o.destination.valid());
        CHECK(b.object(o.destination).destination == o.id);
        CHECK(b.object(o.destination).system != o.system);
    }
    const std::vector<int> jumps = warpJumps(b, SystemId{0u});
    CHECK(std::none_of(jumps.begin(), jumps.end(), [](int j) { return j < 0; }));
}

TEST_CASE("maps: broken map files are refused with a reason") {
    const auto& rs = fixture();
    auto error = [&](std::string_view text) {
        auto m = mapFromText(rs, text);
        return m ? std::string{} : m.error();
    };
    const std::string head = "format = \"opense4-map\"\nversion = 1\n";
    const std::string system = "[[systems]]\nname = \"A\"\nx = 1\ny = 1\ntype = \"Test Single Star\"\n";
    CHECK(error("format = \"something\"\nversion = 1\n").find("Not an OpenSE4 map") != std::string::npos);
    CHECK(error("format = = 3").find("line 1") != std::string::npos);
    CHECK(error("format = \"opense4-map\"\nversion = 9\n").find("newer") != std::string::npos);
    CHECK(error(head).find("no systems") != std::string::npos);
    CHECK(error(head + "[[systems]]\nname = \"A\"\nx = 99\ny = 1\n").find("on the map") != std::string::npos);
    CHECK(error(head + system + "[[systems.objects]]\nkind = \"Planet\"\nsect_type = 4\nx = 13\ny = 0\n").find("sectors") !=
          std::string::npos);
    CHECK(error(head + system + "[[systems.objects]]\nkind = \"Nothing\"\nx = 1\ny = 0\n").find("unknown kind") != std::string::npos);
    const std::string twoObjects = head + system + "[[systems.objects]]\nkind = \"Planet\"\nsect_type = 4\nx = 3\ny = 3\n" +
                                   "[[systems.objects]]\nkind = \"Warp Point\"\nsect_type = 8\nx = 0\ny = 6\nwarp_to = [0, 0]\n";
    CHECK(error(twoObjects).find("another warp point") != std::string::npos);
    CHECK(error(head + system + "[[starts]]\nsystem = 4\nx = 1\ny = 1\n").find("no such system") != std::string::npos);

    // A system type or SectType record this data set lacks is replaced, with a warning.
    auto odd = mapFromText(rs, head + "[[systems]]\nname = \"A\"\nx = 1\ny = 1\ntype = \"Nowhere\"\n" +
                                   "[[systems.objects]]\nkind = \"Planet\"\nsect_type = 1\nsize = \"Large\"\nsurface = \"Rock\"\n"
                                   "atmosphere = \"Oxygen\"\nx = 2\ny = 2\n");
    REQUIRE(odd.has_value());
    CHECK(odd->warnings.size() == 2);
    const SpaceObject& planet = odd->map.galaxy.objects.front();
    CHECK(planet.sectorType == kRockOxygenLarge);  // record 1 is a star; the Large Rock/Oxygen planet matches
    CHECK(planet.conditions == 100);                // defaults when the file gives none
    CHECK(planet.value == std::array<int, 3>{100, 100, 100});
}

TEST_CASE("maps: starting points place empires first, converting or creating planets") {
    const auto& rs = fixture();
    // System 0: a Small Ice/Methane planet; system 1: a Large Rock/Oxygen planet; systems 2-5: none.
    const Galaxy g = chain(rs, {kIceMethaneSmall, kRockOxygenLarge, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    const std::vector<EmpireStart> four{{"A", "Rock", "Oxygen"}, {"B", "Rock", "Oxygen"}, {"C", "Rock", "Oxygen"}, {"D", "Rock", "Oxygen"}};
    PlacementOptions po;
    po.startingPoints = {
        {SystemId{1u}, Sector{3, 3}, kCommonStart},
        {SystemId{0u}, Sector{3, 3}, 0},       // player 1's own point, on the Ice/Methane planet
        {SystemId{2u}, Sector{5, 5}, kCommonStart},
        {SystemId{3u}, Sector{1, 1}, 9},       // no tenth player: ignored
    };
    std::set<std::pair<uint32_t, uint32_t>> orders;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        Galaxy copy = g;
        Rng rng(seed);
        auto homes = placeHomeworlds(copy, rs, four, po, rng);
        REQUIRE(homes.has_value());
        REQUIRE(homes->size() == 4);
        // Player 1 on its specific point: the planet there becomes Rock/Oxygen at the same size.
        const SpaceObject& first = copy.object((*homes)[0]);
        CHECK(first.id == g.systems[0].objects.front());
        CHECK(first.atmosphere == "Oxygen");
        CHECK(first.surface == "Rock");
        CHECK(first.size == "Small");
        CHECK(first.sectorType == kRockOxygenLarge);  // the only natural Rock/Oxygen record
        CHECK(first.name == "Link 0 I");
        // Players 2 and 3 take the two common points in a random order.
        const SpaceObject& second = copy.object((*homes)[1]);
        const SpaceObject& third = copy.object((*homes)[2]);
        std::set<uint32_t> commonSystems{second.system.value, third.system.value};
        CHECK(commonSystems == std::set<uint32_t>{1u, 2u});
        orders.insert({second.system.value, third.system.value});
        // The planet already there is used as it is; on the empty sector one is created.
        const SpaceObject& onPlanet = second.system.value == 1 ? second : third;
        const SpaceObject& created = second.system.value == 1 ? third : second;
        CHECK(onPlanet.id == g.systems[1].objects.front());
        CHECK(onPlanet.sectorType == kRockOxygenLarge);
        CHECK(created.id.index() >= g.objects.size());
        CHECK(created.sector == Sector{5, 5});
        CHECK(created.atmosphere == "Oxygen");
        // Player 4 has no point left: random placement, never on another home.
        const ObjectId fourth = (*homes)[3];
        CHECK(std::count(homes->begin(), homes->end(), fourth) == 1);
        CHECK(copy.object(fourth).atmosphere == "Oxygen");
    }
    CHECK(orders.size() == 2);  // both orders happen

    // Without starting points nothing changes: plain random placement.
    PlacementOptions plain;
    Galaxy a = g, b = g;
    Rng r1(5), r2(5);
    auto x = placeHomeworlds(a, rs, four, plain, r1);
    PlacementOptions none;
    none.startingPoints = {{SystemId{3u}, Sector{1, 1}, 9}};  // points for absent players only
    auto y = placeHomeworlds(b, rs, four, none, r2);
    REQUIRE(x.has_value());
    REQUIRE(y.has_value());
    CHECK(*x == *y);
}

TEST_CASE("maps: a game starts on a loaded map and Save Map records the capitals") {
    const Rules& r = test::engineRules();
    // A quadrant of the engine's data set, saved and loaded as a map.
    QuadrantOptions qo;
    qo.systemCount = 12;
    Rng rng(23);
    auto gen = generateQuadrant(r.data(), qo, rng);
    REQUIRE(gen.has_value());
    QuadrantMap m;
    m.name = "Engine test map";
    m.galaxy = gen->galaxy;
    // Player 2's own point on system 3, and a common one on system 5, both at empty sectors.
    const Sector free3 = emptySectors(m.galaxy, SystemId{3u}).front();
    const Sector free5 = emptySectors(m.galaxy, SystemId{5u}).front();
    m.startingPoints = {{SystemId{5u}, free5, kCommonStart}, {SystemId{3u}, free3, 1}};
    auto loaded = mapFromText(r.data(), mapToText(r.data(), m));
    REQUIRE(loaded.has_value());

    GameSetup setup;
    setup.seed = 4;
    setup.options.systemCount = 40;  // ignored: the map decides
    for (int i = 0; i < 2; ++i) {
        EmpireSetup e;
        e.name = std::format("Mapper {}", i + 1);
        setup.empires.push_back(e);
    }
    setup.map = loaded->map;
    auto s = createGame(r, setup);
    REQUIRE_MESSAGE(s.has_value(), (s ? std::string{} : s.error()));
    CHECK(s->galaxy.systems.size() == m.galaxy.systems.size());
    for (size_t i = 0; i < m.galaxy.systems.size(); ++i) CHECK(s->galaxy.systems[i].name == m.galaxy.systems[i].name);
    auto capitalOf = [&](EmpireId e) -> const SpaceObject* {
        for (const auto& c : s->colonies)
            if (c && c->owner == e && c->homeworld) return &s->galaxy.object(c->planet);
        return nullptr;
    };
    const SpaceObject* first = capitalOf(EmpireId{0u});
    const SpaceObject* second = capitalOf(EmpireId{1u});
    REQUIRE(first);
    REQUIRE(second);
    // Player 1 has no specific point and takes the common one; player 2 its own.
    CHECK(Location{first->system, first->sector} == Location{SystemId{5u}, free5});
    CHECK(Location{second->system, second->sector} == Location{SystemId{3u}, free3});
    CHECK(first->atmosphere == s->empires[0].race.atmosphere);
    CHECK(second->atmosphere == s->empires[1].race.atmosphere);

    // Save Map during the game: the quadrant with each capital as its player's point.
    const QuadrantMap saved = mapOfGame(*s, "Later");
    CHECK(saved.galaxy.systems.size() == s->galaxy.systems.size());
    REQUIRE(saved.startingPoints.size() == 2);
    CHECK(saved.startingPoints[0] == StartingPoint{SystemId{5u}, free5, 0});
    CHECK(saved.startingPoints[1] == StartingPoint{SystemId{3u}, free3, 1});
}

TEST_CASE("maps: Game Setup loads and saves maps; loading clears earlier starting points") {
    namespace setup = client::classic::setup;
    const Rules& r = test::engineRules();
    TempDir dir("setup");
    setup::NewGameSettings s = setup::defaultSettings(r, 3);
    s.players.clear();
    game::EmpireSetup player;
    player.name = "Cartographers";
    s.players.push_back(player);
    s.computers.enabled = false;
    auto preview = setup::previewQuadrant(r, s.seed, s.options);
    REQUIRE(preview.has_value());

    // Save Map without a loaded map writes the preview, with no starting points.
    const QuadrantMap generated = setup::mapToSave(s, preview->galaxy, "Plain");
    CHECK(generated.startingPoints.empty());
    REQUIRE(saveMapFile(setup::mapFilePath(dir.path, "Plain"), r.data(), generated).has_value());
    QuadrantMap withStarts = generated;
    withStarts.name = "Starts";
    withStarts.startingPoints = {{SystemId{0u}, Sector{1, 1}, 0}, {SystemId{1u}, Sector{2, 2}, kCommonStart}};
    REQUIRE(saveMapFile(setup::mapFilePath(dir.path, "Starts"), r.data(), withStarts).has_value());

    const auto files = setup::listMapFiles(r, dir.path);
    REQUIRE(files.size() == 2);
    CHECK(files[0].name == "Plain");
    CHECK(files[0].startingPoints == 0);
    CHECK(files[1].name == "Starts");
    CHECK(files[1].startingPoints == 2);
    CHECK(files[1].systems == static_cast<int>(preview->galaxy.systems.size()));

    auto a = loadMapFile(files[1].path, r.data());
    REQUIRE(a.has_value());
    setup::useMap(s, a->map);
    REQUIRE(s.map);
    CHECK(s.map->startingPoints.size() == 2);
    auto g = setup::buildGameSetup(r, s);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    REQUIRE(g->map);
    CHECK(g->map->startingPoints.size() == 2);
    // Loading another map replaces the starting points with its own (none here).
    auto b = loadMapFile(files[0].path, r.data());
    REQUIRE(b.has_value());
    setup::useMap(s, b->map);
    CHECK(s.map->startingPoints.empty());
    CHECK(setup::mapToSave(s, preview->galaxy, "Again").name == "Again");
    // Generate Map Now returns to a generated quadrant.
    setup::clearMap(s);
    CHECK_FALSE(s.map.has_value());
    auto plain = setup::buildGameSetup(r, s);
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->map.has_value());
}

TEST_CASE("maps: the example in docs/MAPS.md loads") {
    const fs::path doc = fs::path(OPENSE4_FIXTURE_DIR).parent_path().parent_path() / "docs" / "MAPS.md";
    std::ifstream in(doc);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    const size_t begin = text.find("```toml\n");
    REQUIRE(begin != std::string::npos);
    const size_t end = text.find("```", begin + 8);
    REQUIRE(end != std::string::npos);
    auto m = mapFromText(fixture(), text.substr(begin + 8, end - begin - 8));
    REQUIRE_MESSAGE(m.has_value(), (m ? std::string{} : m.error()));
    CHECK(m->warnings.empty());
    const Galaxy& g = m->map.galaxy;
    REQUIRE(g.systems.size() == 2);
    REQUIRE(g.objects.size() == 3);
    CHECK(g.objects[0].conditions == 90);
    CHECK(g.objects[1].destination == ObjectId{2u});
    CHECK(g.objects[2].destination == ObjectId{1u});
    REQUIRE(m->map.startingPoints.size() == 2);
    CHECK(m->map.startingPoints[0].player == 0);
    CHECK(m->map.startingPoints[1].player == kCommonStart);
}
