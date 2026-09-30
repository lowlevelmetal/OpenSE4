// Logic behind the classic Designs / Create Design / Weapons Report windows
// (src/client/classic/screens/design_tools.*), on the engine test rules.

#include "engine_fixture.hpp"

#include "client/classic/screens/design_tools.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
using namespace opense4::client::classic;

namespace {

// An empire that knows nothing, then the given tech levels.
Empire empireWith(const Rules& r, std::initializer_list<std::pair<std::string_view, int>> levels) {
    Empire e;
    e.id = EmpireId{0};
    e.techLevels.assign(r.data().techAreas.size(), 0);
    for (const auto& [area, level] : levels) e.techLevels[techArea(r, area).index()] = level;
    return e;
}

bool contains(const std::vector<uint32_t>& v, uint32_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// The test rules plus one heavy direct-fire mount for large ships.
const Rules& rulesWithMount() {
    static const Rules rules = [] {
        ruleset::Ruleset rs = buildEngineRuleset();
        ruleset::WeaponMount m;
        m.longName = "Test Heavy Mount";
        m.shortName = "Heavy";
        m.code = "H";
        m.damagePercent = 150;
        m.tonnagePercent = 200;
        m.costPercent = 150;
        m.rangeModifier = 1;
        m.minimumVehicleSize = 200;
        m.weaponTypeRequirement = "Direct Fire";
        m.vehicleType = "Ship";
        rs.weaponMounts.push_back(m);
        for (auto& c : rs.components)
            if (c.name.starts_with("Test Laser")) c.generalGroup = "Weapons";
        rs.reindex();
        return Rules{std::move(rs)};
    }();
    return rules;
}

} // namespace

TEST_CASE("design tools: upgrade replaces components with the newest of their family") {
    const Rules& r = engineRules();
    const uint32_t engine1 = componentIndex(r, "Test Engine"), engine2 = componentIndex(r, "Test Engine II");
    const uint32_t laser1 = componentIndex(r, "Test Laser"), laser2 = componentIndex(r, "Test Laser II");
    const uint32_t bridge = componentIndex(r, "Test Bridge");
    const std::vector<DesignEntry> original{{bridge, -1}, {engine1, -1}, {laser1, 0}, {engine1, -1}};

    SUBCASE("nothing newer known") {
        const Empire e = empireWith(r, {{"Test Construction", 1}, {"Test Propulsion", 1}, {"Test Beams", 1}});
        auto entries = original;
        CHECK_FALSE(upgradeEntries(r, e, entries));
        CHECK(entries == original);
        CHECK(isLatestComponent(r, e, engine1));
        CHECK(isLatestComponent(r, e, bridge));  // no family
    }
    SUBCASE("newer engines and lasers known") {
        const Empire e = empireWith(r, {{"Test Construction", 1}, {"Test Propulsion", 3}, {"Test Beams", 3}});
        auto entries = original;
        CHECK(upgradeEntries(r, e, entries));
        const std::vector<DesignEntry> want{{bridge, -1}, {engine2, -1}, {laser2, 0}, {engine2, -1}};
        CHECK(entries == want);  // order and mounts kept
        CHECK_FALSE(isLatestComponent(r, e, engine1));
        CHECK(isLatestComponent(r, e, engine2));
    }
}

TEST_CASE("design tools: an upgraded starting design is valid and can be created") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 10);
    Empire& e = s.empires[0];
    const Design scout = s.design(e.designs.front());  // a copy: creating a design reallocates the list
    e.techLevels[techArea(r, "Test Propulsion").index()] = 3;
    std::vector<DesignEntry> entries = scout.entries;
    REQUIRE(upgradeEntries(r, e, entries));

    Design d;
    d.name = nextVersionName(s, e, scout.name);
    CHECK(d.name == scout.name + " II");
    d.hull = scout.hull;
    d.entries = entries;
    d.designType = scout.designType;
    CHECK(computeDesignStats(r, &e, d).problems.empty());
    const size_t before = e.designs.size();
    const CommandResult res = apply(r, s, e.id, cmd::CreateDesign{d});
    CHECK_MESSAGE(res.ok, res.error);
    CHECK(s.empires[0].designs.size() == before + 1);
    CHECK(designNameTaken(s, s.empires[0], scout.name + " II"));
    CHECK(nextVersionName(s, s.empires[0], scout.name) == scout.name + " III");
    CHECK(nextVersionName(s, s.empires[0], scout.name + " II") == scout.name + " III");
}

TEST_CASE("design tools: condensed view groups identical entries") {
    const std::vector<DesignEntry> entries{{1, -1}, {2, -1}, {1, -1}, {1, 0}, {2, -1}, {1, -1}};
    const auto groups = groupEntries(entries);
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].entry == DesignEntry{1, -1});
    CHECK(groups[0].count == 3);
    CHECK(groups[0].last == 5);
    CHECK(groups[1].entry == DesignEntry{2, -1});
    CHECK(groups[1].count == 2);
    CHECK(groups[1].last == 4);
    CHECK(groups[2].entry == DesignEntry{1, 0});  // a mounted part is a different part
    CHECK(groups[2].count == 1);
    CHECK(groupEntries({}).empty());
}

TEST_CASE("design tools: designer component list follows hull, tech, group and Only Latest") {
    const Rules& r = rulesWithMount();
    const Empire e = empireWith(r, {{"Test Construction", 2}, {"Test Propulsion", 3}, {"Test Beams", 3}, {"Test Units", 1}});
    const uint32_t frigate = hullIndex(r, "Test Frigate"), fighter = hullIndex(r, "Test Fighter Hull");
    const uint32_t laser1 = componentIndex(r, "Test Laser"), laser2 = componentIndex(r, "Test Laser II");
    const uint32_t gun = componentIndex(r, "Test Fighter Gun"), cloak = componentIndex(r, "Test Cloak");

    const auto all = designerComponents(r, e, frigate, {}, false);
    CHECK(contains(all, laser1));
    CHECK(contains(all, laser2));
    CHECK(contains(all, componentIndex(r, "Test Bridge")));
    CHECK_FALSE(contains(all, gun));    // fighters only
    CHECK_FALSE(contains(all, cloak));  // not researched

    const auto latest = designerComponents(r, e, frigate, {}, true);
    CHECK_FALSE(contains(latest, laser1));
    CHECK(contains(latest, laser2));

    const auto weapons = designerComponents(r, e, frigate, "weapons", false);  // group names ignore case
    CHECK(weapons == std::vector<uint32_t>{laser1, laser2});
    CHECK(componentGroups(r, e, frigate) == std::vector<std::string>{"Weapons"});

    CHECK(contains(designerComponents(r, e, fighter, {}, false), gun));
    CHECK(isUnitHull(r.hull(fighter).type));
    CHECK_FALSE(isUnitHull(r.hull(frigate).type));
    CHECK_FALSE(isUnitHull(r.hull(hullIndex(r, "Test Station")).type));
}

TEST_CASE("design tools: weapon mounts") {
    const Rules& r = rulesWithMount();
    const Empire e = empireWith(r, {{"Test Construction", 2}, {"Test Beams", 3}, {"Test Physics", 2}, {"Test Missiles", 1}});
    const uint32_t mount = static_cast<uint32_t>(r.data().weaponMounts.size() - 1);
    const uint32_t frigate = hullIndex(r, "Test Frigate"), cruiser = hullIndex(r, "Test Cruiser");
    const uint32_t laser = componentIndex(r, "Test Laser"), missile = componentIndex(r, "Test Missile");
    const uint32_t bridge = componentIndex(r, "Test Bridge");

    CHECK(hullMounts(r, e, frigate).empty());  // 150 kT is below every mount's minimum
    CHECK(contains(hullMounts(r, e, cruiser), mount));
    CHECK(mountFor(r, cruiser, laser, static_cast<int32_t>(mount)) == static_cast<int32_t>(mount));
    CHECK(mountFor(r, cruiser, missile, static_cast<int32_t>(mount)) == -1);  // direct fire only
    CHECK(mountFor(r, cruiser, bridge, static_cast<int32_t>(mount)) == -1);
    CHECK(mountFor(r, frigate, laser, static_cast<int32_t>(mount)) == -1);
    CHECK(mountFor(r, cruiser, laser, -1) == -1);

    // The Weapons Report ignores the hull.
    CHECK(mountFitsWeapon(r, mount, laser));
    CHECK_FALSE(mountFitsWeapon(r, mount, missile));
    CHECK_FALSE(mountFitsWeapon(r, mount + 1, laser));

    // Mounted damage is shifted by the range modifier and scaled.
    const DesignEntry mounted{laser, static_cast<int32_t>(mount)};
    CHECK(weaponDamageAtRange(r, mounted, 1) == 18);
    CHECK(weaponDamageAtRange(r, mounted, 3) == 18);
    CHECK(weaponMaxRange(r, mounted) == 7);
}

TEST_CASE("design tools: known weapons by type") {
    const Rules& r = engineRules();
    const Empire e = empireWith(r, {{"Test Beams", 1}, {"Test Physics", 2}, {"Test Missiles", 1}});
    const auto all = knownWeapons(r, e, ruleset::WeaponKind::None, false);
    CHECK(contains(all, componentIndex(r, "Test Laser")));
    CHECK(contains(all, componentIndex(r, "Test Missile")));
    CHECK(contains(all, componentIndex(r, "Test Point Defense")));
    CHECK_FALSE(contains(all, componentIndex(r, "Test Laser II")));  // Beams 3
    CHECK_FALSE(contains(all, componentIndex(r, "Test Bridge")));    // not a weapon
    const auto seeking = knownWeapons(r, e, ruleset::WeaponKind::Seeking, false);
    CHECK(seeking == std::vector<uint32_t>{componentIndex(r, "Test Missile")});
}

TEST_CASE("design tools: design names") {
    GameState s = newEngineGame(5, 2, 10);
    const Empire& e = s.empires[0];
    const std::string taken = s.design(e.designs.front()).name;
    const std::vector<std::string> list{taken, "Aurora", "Borealis"};
    size_t cursor = 0;
    CHECK(suggestDesignName(s, e, list, cursor) == "Aurora");
    CHECK(suggestDesignName(s, e, list, cursor) == "Borealis");
    CHECK(suggestDesignName(s, e, list, cursor) == "Aurora");  // wraps, skipping the taken name
    const std::vector<std::string> none{taken};
    CHECK(suggestDesignName(s, e, none, cursor).empty());

    CHECK(romanNumeral(1) == "I");
    CHECK(romanNumeral(4) == "IV");
    CHECK(romanNumeral(9) == "IX");
    CHECK(romanNumeral(14) == "XIV");
    CHECK(romanNumeral(39) == "XXXIX");
    CHECK(romanNumeral(0).empty());
    CHECK(nextVersionName(s, e, "Lancer IV") == "Lancer V");
    CHECK(nextVersionName(s, e, "Lancer I") == "Lancer II");
    CHECK(nextVersionName(s, e, "Mark IVy") == "Mark IVy II");
}

TEST_CASE("design tools: name lists are read as Windows-1252") {
    const auto names = parseNameList("Alpha\r\nBeta  \r\n\r\n  Caf\xE9\r\n\x80uro\nLast");
    REQUIRE(names.size() == 5);
    CHECK(names[0] == "Alpha");
    CHECK(names[1] == "Beta");
    CHECK(names[2] == "Caf\xC3\xA9");
    CHECK(names[3] == "\xE2\x82\xACuro");
    CHECK(names[4] == "Last");
    CHECK(parseNameList("").empty());
}
