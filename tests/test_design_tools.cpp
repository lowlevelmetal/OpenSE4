// Logic behind the classic Designs / Create Design / Weapons Report windows
// (src/client/classic/screens/design_tools.*), on the engine test rules.

#include "engine_fixture.hpp"

#include "client/classic/screens/design_tools.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <format>

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

namespace {

// The test rules with a family of its own for every part that had family 0,
// plus, appended so the test game's indices still hold: one engine family
// holding two lines (the later line's first numeral after the earlier line's
// third), and two plates of family 0.
const Rules& rulesWithFamilies() {
    static const Rules rules = [] {
        ruleset::Ruleset rs = buildEngineRuleset();
        for (size_t i = 0; i < rs.components.size(); ++i)
            if (rs.components[i].family == 0) rs.components[i].family = 1000 + static_cast<int>(i);
        auto areaOf = [&](std::string_view name) {
            for (uint32_t i = 0; i < rs.techAreas.size(); ++i)
                if (rs.techAreas[i].name == name) return ruleset::TechAreaId{i};
            return ruleset::TechAreaId{};
        };
        auto add = [&](std::string name, int family, int numeral, std::string_view area, int level) {
            ruleset::Component c = rs.components.front();
            c.abilities.clear();
            c.name = std::move(name);
            c.family = family;
            c.romanNumeral = numeral;
            c.requirements = {ruleset::TechRequirement{areaOf(area), level}};
            rs.components.push_back(std::move(c));
        };
        add("Test Ion Drive", 300, 1, "Test Propulsion", 1);
        add("Test Ion Drive II", 300, 2, "Test Propulsion", 2);
        add("Test Ion Drive III", 300, 3, "Test Propulsion", 3);
        add("Test Warp Drive", 300, 1, "Test Propulsion", 4);
        add("Test Warp Drive II", 300, 2, "Test Propulsion", 5);
        add("Test Hull Plate", 0, 0, "Test Armor", 1);
        add("Test Hull Plate Mk2", 0, 0, "Test Armor", 2);
        rs.reindex();
        return Rules{std::move(rs)};
    }();
    return rules;
}

} // namespace

TEST_CASE("design tools: Upgrade takes the last researched component of each family in data order (spec 03 §4.1)") {
    const Rules& r = rulesWithFamilies();
    const uint32_t ion1 = componentIndex(r, "Test Ion Drive"), ion3 = componentIndex(r, "Test Ion Drive III");
    const uint32_t warp1 = componentIndex(r, "Test Warp Drive"), warp2 = componentIndex(r, "Test Warp Drive II");
    const uint32_t plate = componentIndex(r, "Test Hull Plate"), plate2 = componentIndex(r, "Test Hull Plate Mk2");
    const uint32_t bridge = componentIndex(r, "Test Bridge");
    const std::vector<DesignEntry> original{{bridge, -1}, {ion1, -1}, {ion3, 0}, {plate, -1}};
    auto upgraded = [&](std::initializer_list<std::pair<std::string_view, int>> levels, std::vector<DesignEntry> entries = {}) {
        if (entries.empty()) entries = original;
        const Empire e = empireWith(r, levels);
        upgradeEntries(r, e, entries);
        return entries;
    };

    SUBCASE("nothing newer researched: every entry stays") {
        const Empire e = empireWith(r, {{"Test Construction", 1}, {"Test Propulsion", 1}, {"Test Armor", 1}});
        auto entries = original;
        entries[2] = {ion1, 0};
        const auto before = entries;
        CHECK_FALSE(upgradeEntries(r, e, entries));
        CHECK(entries == before);
        CHECK(isLatestComponent(r, e, ion1));
        CHECK(isLatestComponent(r, e, bridge));
        CHECK(isLatestComponent(r, e, plate));
    }
    SUBCASE("within one line: the highest researched numeral, which is also the last") {
        const std::vector<DesignEntry> want{{bridge, -1}, {ion3, -1}, {ion3, 0}, {plate, -1}};
        CHECK(upgraded({{"Test Construction", 1}, {"Test Propulsion", 3}, {"Test Armor", 1}}) == want);
    }
    SUBCASE("a later line's first numeral replaces an earlier line's third; mounts stay") {
        const Empire e = empireWith(r, {{"Test Construction", 1}, {"Test Propulsion", 4}, {"Test Armor", 1}});
        auto entries = original;
        CHECK(upgradeEntries(r, e, entries));
        const std::vector<DesignEntry> want{{bridge, -1}, {warp1, -1}, {warp1, 0}, {plate, -1}};
        CHECK(entries == want);
        CHECK_FALSE(isLatestComponent(r, e, ion3));
        CHECK(isLatestComponent(r, e, warp1));
        CHECK(upgraded({{"Test Propulsion", 5}}, {{ion3, -1}})[0].component == warp2);
    }
    SUBCASE("only what the owner researched counts: a newer part the owner lacks becomes the last it has") {
        CHECK(upgraded({{"Test Propulsion", 3}}, {{warp2, -1}})[0].component == ion3);
        // None of the family researched: the entry stays.
        CHECK(upgraded({}, {{warp2, -1}})[0].component == warp2);
    }
    SUBCASE("family 0 is an ordinary family") {
        CHECK(upgraded({{"Test Armor", 2}}, {{plate, -1}})[0].component == plate2);
        const Empire e = empireWith(r, {{"Test Armor", 2}});
        CHECK_FALSE(isLatestComponent(r, e, plate));
        CHECK(isLatestComponent(r, e, plate2));
    }
}

TEST_CASE("design tools: an upgraded design is valid and can be created") {
    const Rules& r = rulesWithFamilies();
    GameState s = newEngineGame(5, 2, 10);
    Empire& e = s.empires[0];
    const DesignId made = addTestDesign(s, r, e.id, "Courier", "Test Frigate",
                                        {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Engine"});
    const Design scout = s.design(made);  // a copy: creating a design reallocates the list
    e.techLevels[techArea(r, "Test Propulsion").index()] = 3;
    std::vector<DesignEntry> entries = scout.entries;
    REQUIRE(upgradeEntries(r, e, entries));
    CHECK(entries.back().component == componentIndex(r, "Test Engine II"));
    CHECK(entries.front() == scout.entries.front());

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

TEST_CASE("design tools: Only Latest keeps the last of each run of neighbours of one family") {
    // Spec 02 §6.4 (confirmed: binary): numerals and names play no part, family 0
    // is an ordinary family, and a family split into two runs keeps one per run.
    ruleset::Ruleset rs = buildEngineRuleset();
    REQUIRE(rs.components.size() >= 7);
    REQUIRE(rs.facilities.size() >= 4);
    const std::array<int, 7> families{4, 4, 0, 0, 4, 9, 9};
    for (size_t i = 0; i < families.size(); ++i) {
        rs.components[i].family = families[i];
        rs.components[i].romanNumeral = 7 - int(i);   // the last of a run has the lowest numeral
    }
    const std::array<int, 4> facilityFamilies{3, 3, 3, 0};
    for (size_t i = 0; i < facilityFamilies.size(); ++i) {
        rs.facilities[i].family = facilityFamilies[i];
        rs.facilities[i].romanNumeral = 3 - int(i);
    }
    const Rules r(std::move(rs));
    const std::vector<uint32_t> all{0, 1, 2, 3, 4, 5, 6};
    CHECK(r.onlyLatestComponents(all) == std::vector<uint32_t>{1, 3, 4, 6});
    // The rule runs on the list the other filters left: with item 4 filtered
    // out, items 1 and 3 belong to different runs still, and 0 and 1 to one.
    const std::vector<uint32_t> filtered{0, 1, 3, 5};
    CHECK(r.onlyLatestComponents(filtered) == std::vector<uint32_t>{1, 3, 5});
    const std::vector<uint32_t> neighbours{0, 4};   // the same family once the items between are filtered out
    CHECK(r.onlyLatestComponents(neighbours) == std::vector<uint32_t>{4});
    CHECK(r.onlyLatestComponents(std::vector<uint32_t>{}).empty());
    CHECK(r.onlyLatestFacilities(std::vector<uint32_t>{0, 1, 2, 3}) == std::vector<uint32_t>{2, 3});
    CHECK(r.onlyLatestFacilities(std::vector<uint32_t>{0, 2}) == std::vector<uint32_t>{2});
}

TEST_CASE("design tools: the vehicle types and hulls the designer offers") {
    const Rules& r = engineRules();
    const Empire ships = empireWith(r, {{"Test Construction", 1}});
    CHECK(designableTypes(r, ships) == std::vector<ruleset::VehicleType>{ruleset::VehicleType::Ship, ruleset::VehicleType::Base});
    const auto shipHulls = hullsOfType(r, ships, ruleset::VehicleType::Ship);
    CHECK(contains(shipHulls, hullIndex(r, "Test Frigate")));
    CHECK_FALSE(contains(shipHulls, hullIndex(r, "Test Cruiser")));  // needs Construction 2
    CHECK_FALSE(contains(shipHulls, hullIndex(r, "Test Station")));  // a base
    CHECK(std::is_sorted(shipHulls.begin(), shipHulls.end()));
    // A hull being edited stays listed.
    CHECK(contains(hullsOfType(r, ships, ruleset::VehicleType::Ship, hullIndex(r, "Test Cruiser")), hullIndex(r, "Test Cruiser")));
    const Empire units = empireWith(r, {{"Test Construction", 1}, {"Test Units", 1}});
    const auto types = designableTypes(r, units);
    CHECK(std::find(types.begin(), types.end(), ruleset::VehicleType::Fighter) != types.end());
    CHECK(std::find(types.begin(), types.end(), ruleset::VehicleType::Drone) != types.end());
    CHECK(std::is_sorted(types.begin(), types.end()));
    CHECK(designWindowTitle(ruleset::VehicleType::Ship) == "Ship Design");
    CHECK(designWindowTitle(ruleset::VehicleType::WeaponPlatform) == "Weapon Platform Design");
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
    const std::string taken = s.design(addTestDesign(s, engineRules(), e.id, "Courier", "Test Frigate", {"Test Bridge"})).name;
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

// ---- Engine design rules (docs/spec/03 §3.2, §4, §6.1) -----------------------------------------------

namespace {

ruleset::Ability dtAb(AbilityKind k, int64_t v1 = 0, int64_t v2 = 0) {
    ruleset::Ability a;
    a.type = std::string(identifier(k));
    a.value1 = std::to_string(v1);
    a.value2 = std::to_string(v2);
    return a;
}

// The engine test rules plus parts, mounts, a hull and a trait for the design rules.
const Rules& designRules() {
    static const Rules rules = [] {
        ruleset::Ruleset rs = buildEngineRuleset();
        const auto ship = ruleset::maskOf(ruleset::VehicleType::Ship);
        auto comp = [&](std::string name, int tons, std::vector<ruleset::Ability> abilities, int family = 0) -> ruleset::Component& {
            ruleset::Component c;
            c.name = std::move(name);
            c.tonnage = tons;
            c.structure = tons;
            c.cost = {10, 0, 0};
            c.vehicles = ship;
            c.abilities = std::move(abilities);
            c.family = family;
            rs.components.push_back(std::move(c));
            return rs.components.back();
        };
        comp("Dt Hold", 1, {dtAb(AbilityKind::CargoStorage, 1)});
        comp("Dt Warp Core", 10, {dtAb(AbilityKind::StandardShipMovement, 256)});
        comp("Dt Booster", 5, {dtAb(AbilityKind::ExtraMovementGeneration, 2, 7)});
        comp("Dt Big Booster", 5, {dtAb(AbilityKind::ExtraMovementGeneration, 5, 7)});
        comp("Dt Other Booster", 5, {dtAb(AbilityKind::ExtraMovementGeneration, 3, 8)});
        comp("Dt Slow Engine", 10, {dtAb(AbilityKind::StandardShipMovement, 1), dtAb(AbilityKind::MovementBonus, -1)});
        comp("Dt Aim A", 5, {dtAb(AbilityKind::CombatToHitOffensePlus, 10)}, 900);
        comp("Dt Aim A2", 5, {dtAb(AbilityKind::CombatToHitOffensePlus, 15)}, 900);
        comp("Dt Aim B", 5, {dtAb(AbilityKind::CombatToHitOffensePlus, 7)}, 901);
        comp("Dt Clumsy", 5, {dtAb(AbilityKind::CombatToHitOffenseMinus, 4)}, 902);
        comp("Dt Limited", 5, {}, 950).maxPerVehicle = 2;
        comp("Dt Limited II", 5, {}, 950);
        auto& longGun = comp("Dt Long Gun", 10, {});
        longGun.weapon.kind = ruleset::WeaponKind::DirectFire;
        longGun.weapon.damageAtRange.assign(20, 5);  // damage out to range 20

        auto mount = [&](std::string name, std::string req) -> ruleset::WeaponMount& {
            ruleset::WeaponMount m;
            m.longName = std::move(name);
            m.weaponTypeRequirement = std::move(req);
            m.vehicleType = "Ship\\Base";
            rs.weaponMounts.push_back(std::move(m));
            return rs.weaponMounts.back();
        };
        auto& big = mount("Dt Big", "Direct Fire");
        big.tonnagePercent = big.structurePercent = big.costPercent = big.supplyPercent = 150;
        big.damagePercent = 1000;
        big.rangeModifier = 2;
        mount("Dt Disruptors Only", "Direct Fire").familyRequirement = {201};
        mount("Dt Shielding", "None").shieldPercent = 150;
        mount("Dt Any", "Any").tonnagePercent = 200;
        mount("Dt Small Hulls", "Direct Fire").maximumVehicleSize = 200;
        auto& secret = mount("Dt Secret", "Direct Fire");
        secret.requirements = {{*rs.findTechArea("Test Physics"), 5}};
        mount("Dt Lower Case", "Direct Fire").vehicleType = "ship";

        ruleset::VehicleSize freighter;
        freighter.name = "Dt Freighter";
        freighter.type = ruleset::VehicleType::Ship;
        freighter.tonnage = 300;
        freighter.enginesPerMove = 1;
        freighter.usesEngines = true;
        freighter.minPercentCargo = 21;
        rs.vehicleSizes.push_back(freighter);

        ruleset::RacialTrait fast;
        fast.name = "Dt Fast";
        fast.traitType = "Vehicle Speed";
        fast.values = {"2"};
        rs.racialTraits.push_back(fast);
        rs.reindex();
        return Rules{std::move(rs)};
    }();
    return rules;
}

uint32_t mountIndex(const Rules& r, std::string_view name) {
    for (uint32_t i = 0; i < r.data().weaponMounts.size(); ++i)
        if (r.data().weaponMounts[i].longName == name) return i;
    FAIL("no mount " << name);
    return 0;
}

std::vector<DesignEntry> entries(const Rules& r, std::initializer_list<std::string_view> names) {
    std::vector<DesignEntry> out;
    for (auto n : names) out.push_back({componentIndex(r, n), -1});
    return out;
}

bool mentions(const std::vector<std::string>& problems, std::string_view text) {
    return std::any_of(problems.begin(), problems.end(), [&](const std::string& p) { return p.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("design rules: abilities are read through the original's aggregation modes") {
    auto parsed = [](AbilityKind k, int64_t v1, int64_t v2 = 0, int family = -1) {
        ParsedAbility a;
        a.kind = k;
        a.value1 = v1;
        a.value2 = v2;
        a.fromComponent = family >= 0;
        a.family = std::max(0, family);
        return a;
    };
    using K = AbilityKind;
    const std::vector<ParsedAbility> list{
        parsed(K::SupplyStorage, 1'500'000'000), parsed(K::SupplyStorage, 1'500'000'000),
        parsed(K::MedicalBay, -3),
        parsed(K::MovementBonus, 2), parsed(K::MovementBonus, 1),
        parsed(K::ExtraMovementGeneration, 2, 7), parsed(K::ExtraMovementGeneration, 9, 7), parsed(K::ExtraMovementGeneration, 3, 8),
        parsed(K::ExtraMovementGeneration, 4, 7 + 65536),  // ids compare as 16-bit values
        parsed(K::ShipLifeSupport, 0), parsed(K::ShipLifeSupport, 0),
        parsed(K::CombatToHitDefensePlus, 5),                // hull: always adds
        parsed(K::CombatToHitDefensePlus, 10, 0, 3), parsed(K::CombatToHitDefensePlus, 20, 0, 3), parsed(K::CombatToHitDefensePlus, 4, 0, 4),
    };
    CHECK(abilitySum(list, K::SupplyStorage) == kAbilitySumCap);
    CHECK(abilityLargest(list, K::MedicalBay) == 0);  // negatives never count
    CHECK(abilitySmallest(list, K::MovementBonus) == 1);
    CHECK(abilitySmallest(list, K::Armor) == 0);
    CHECK(abilitySmallest(std::vector<ParsedAbility>{parsed(K::MovementBonus, 99'999)}, K::MovementBonus) == 0);
    CHECK(abilityFirstPerId(list, K::ExtraMovementGeneration) == 2 + 3);  // the first of each id, even when smaller
    CHECK(abilityCount(list, K::ShipLifeSupport) == 2);
    CHECK(abilityPerFamily(list, K::CombatToHitDefensePlus) == 5 + 20 + 4);
    CHECK(abilityValue(list, K::ExtraMovementGeneration) == 5);
    CHECK(aggregationOf(K::CloakLevel) == Aggregation::PerSightType);
    CHECK(aggregationOf(K::ReducedMaintenanceSystem) == Aggregation::Smallest);

    ParsedAbility cloak = parsed(K::CloakLevel, 0, 3);
    cloak.text1 = "Psychic";
    ParsedAbility cloak2 = parsed(K::CloakLevel, 0, 2);
    cloak2.text1 = "Psychic";
    const std::vector<ParsedAbility> cloaks{cloak, cloak2};
    CHECK(abilityPerSightType(cloaks, K::CloakLevel, SightType::Psychic) == 3);
    CHECK(abilityPerSightType(cloaks, K::CloakLevel, SightType::EMActive) == 0);
}

TEST_CASE("design rules: validity follows the original's checks") {
    const Rules& r = designRules();
    const uint32_t frigate = hullIndex(r, "Test Frigate"), cruiser = hullIndex(r, "Test Cruiser");
    const uint32_t station = hullIndex(r, "Test Station"), freighter = hullIndex(r, "Dt Freighter");
    auto problems = [&](uint32_t hull, std::vector<DesignEntry> e, const Empire* owner = nullptr) {
        return computeDesignStats(r, owner, hull, e).problems;
    };
    const auto crew = entries(r, {"Test Bridge", "Test Life Support", "Test Crew Quarters"});
    auto with = [&](std::vector<DesignEntry> base, std::initializer_list<std::string_view> more) {
        for (auto n : more) base.push_back({componentIndex(r, n), -1});
        return base;
    };
    CHECK(problems(frigate, crew).empty());
    CHECK(problems(99999, crew) == std::vector<std::string>{"Choose a hull for the design"});
    // Exactly one bridge.
    CHECK(mentions(problems(frigate, with(crew, {"Test Bridge"})), "exactly one bridge"));
    // A Master Computer lifts the bridge, auxiliary control, life support and crew rules, not the engine rules.
    CHECK(problems(frigate, entries(r, {"Test Master Computer"})).empty());
    CHECK(mentions(problems(station, entries(r, {"Test Master Computer", "Test Engine"})), "cannot use engines"));
    // Auxiliary control: limited to one only on hulls that allow it.
    CHECK(problems(frigate, with(crew, {"Test Aux Control", "Test Aux Control"})).empty());
    CHECK(mentions(problems(cruiser, with(crew, {"Test Aux Control", "Test Aux Control"})), "auxiliary control"));
    // One space yard.
    CHECK(mentions(problems(cruiser, with(crew, {"Test Yard Module", "Test Yard Module"})), "one space yard"));
    // A restriction limits the whole family.
    CHECK(problems(frigate, with(crew, {"Dt Limited", "Dt Limited II"})).empty());
    CHECK(mentions(problems(frigate, with(crew, {"Dt Limited", "Dt Limited II", "Dt Limited II"})), "family"));
    // Engines: no minimum, a maximum.
    CHECK(problems(frigate, crew).empty());
    CHECK(mentions(problems(frigate, with(crew, {"Test Engine", "Test Engine", "Test Engine", "Test Engine", "Test Engine", "Test Engine",
                                                 "Test Engine"})),
                   "At most 6 engines"));
    // Percentages against truncate(T x p %) in floating point: 300 x 21 % gives 62, not 63.
    std::vector<DesignEntry> holds;
    for (int i = 0; i < 62; ++i) holds.push_back({componentIndex(r, "Dt Hold"), -1});
    CHECK(problems(freighter, holds).empty());
    holds.pop_back();
    CHECK(mentions(problems(freighter, holds), "cargo space"));
    // Mounts: size bounds, then technology; one warning at most.
    std::vector<DesignEntry> mounted = with(crew, {"Test Laser"});
    mounted.back().mount = static_cast<int32_t>(mountIndex(r, "Dt Small Hulls"));
    CHECK(mentions(problems(cruiser, mounted), "not allowed on a hull of this size"));
    mounted.back().mount = static_cast<int32_t>(mountIndex(r, "Dt Secret"));
    Empire e;
    e.techLevels.assign(r.data().techAreas.size(), 10);
    e.techLevels[techArea(r, "Test Physics").index()] = 4;
    const auto secretProblems = problems(cruiser, mounted, &e);
    CHECK(mentions(secretProblems, "beyond our technology"));
    CHECK(problems(cruiser, mounted).empty());  // no owner, no technology checks
    // Parts must suit the hull's class (the engine's own check).
    CHECK(mentions(problems(frigate, with(crew, {"Test Fighter Gun"})), "cannot be placed"));
}

TEST_CASE("design rules: mounts round their values and apply by weapon type and family") {
    const Rules& r = designRules();
    const uint32_t cruiser = hullIndex(r, "Test Cruiser"), frigate = hullIndex(r, "Test Frigate");
    const uint32_t laser = componentIndex(r, "Test Laser"), missile = componentIndex(r, "Test Missile");
    const uint32_t shield = componentIndex(r, "Test Shield"), bridge = componentIndex(r, "Test Bridge");
    const auto big = static_cast<int32_t>(mountIndex(r, "Dt Big"));
    // 20 kT, 15 structure, cost 40/0/10, supply 5, all x 150 %, rounded half to even.
    const MountedComponent m = mounted(r, {laser, big});
    CHECK(m.mountApplies);
    CHECK(m.tonnage == 30);
    CHECK(m.structure == 22);  // 22.5
    CHECK(m.cost == Resources{60, 0, 15});
    CHECK(m.supplyUsed == 8);  // 7.5
    // Damage: the table index shifts by the range modifier, clamped to 1..20; x 1000 %.
    CHECK(weaponDamageAtRange(r, {laser, big}, 1) == 120);
    CHECK(weaponDamageAtRange(r, {laser, big}, 3) == 120);
    CHECK(weaponDamageAtRange(r, {laser, big}, 5) == 100);
    CHECK(weaponMaxRange(r, {laser, big}) == 8);
    CHECK(weaponDamageAtRange(r, {laser, -1}, 21) == 0);
    // A mounted weapon with damage at range 20 fires further, but its maximum
    // range for strategies and reports is 20 (spec 03 §19 Q42, confirmed: binary).
    const uint32_t longGun = componentIndex(r, "Dt Long Gun");
    CHECK(weaponDamageAtRange(r, {longGun, big}, 22) > 0);
    CHECK(weaponMaxRange(r, {longGun, big}) == 20);
    CHECK(weaponMaxRange(r, {longGun, -1}) == 20);
    // A mount that does not apply changes nothing: here the family list or the weapon type.
    const auto family = static_cast<int32_t>(mountIndex(r, "Dt Disruptors Only"));
    CHECK_FALSE(mounted(r, {laser, family}).mountApplies);
    CHECK(mounted(r, {laser, family}).tonnage == 20);
    CHECK_FALSE(mountAllowed(r, cruiser, laser, static_cast<uint32_t>(family)));
    CHECK_FALSE(mounted(r, {missile, big}).mountApplies);
    // "Any" takes every weapon type but no other part; "None" only other parts.
    const auto any = static_cast<int32_t>(mountIndex(r, "Dt Any"));
    CHECK(mounted(r, {missile, any}).tonnage == 40);
    CHECK_FALSE(mounted(r, {bridge, any}).mountApplies);
    const auto shielding = static_cast<int32_t>(mountIndex(r, "Dt Shielding"));
    CHECK(computeDesignStats(r, nullptr, cruiser, std::vector<DesignEntry>{{shield, shielding}}).shields == 30);
    // The offer test matches the hull's class name with its case.
    CHECK(mountOffered(r, frigate, static_cast<uint32_t>(big)));
    CHECK_FALSE(mountOffered(r, frigate, mountIndex(r, "Dt Lower Case")));
}

TEST_CASE("design rules: ship movement per spec 03 §6.1") {
    const Rules& r = designRules();
    GameState s = newEngineGame(5, 2, 10);
    const EmpireId me{0u};
    auto ship = [&](std::initializer_list<std::string_view> parts) -> Vehicle& {
        const DesignId d = addTestDesign(s, r, me, std::format("D{}", s.designs.size()), "Test Frigate", parts);
        Vehicle& v = addTestVehicle(s, r, d, {SystemId{0u}, Sector{1, 1}});
        v.supply = 100;
        return v;
    };
    // The smallest Movement Bonus over the working parts that have one: 2 + 1 + (+1).
    Vehicle& mixed = ship({"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Engine", "Test Engine II"});
    CHECK(vehicleMaxMovement(r, s, mixed) == 3 + 1);
    const VehicleId mixedId = mixed.id;
    // Extra movement: the first entry of each stacking id, even if a later one is larger.
    Vehicle& boosted = ship({"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Dt Booster", "Dt Big Booster",
                             "Dt Other Booster"});
    CHECK(vehicleMaxMovement(r, s, boosted) == 1 + 2 + 3);
    // Without working engines the bonuses give nothing; the engine sum keeps 8 bits.
    Vehicle& drifting = ship({"Test Bridge", "Test Life Support", "Test Crew Quarters", "Dt Booster"});
    CHECK(vehicleMaxMovement(r, s, drifting) == 0);
    Vehicle& wrapped = ship({"Test Bridge", "Test Life Support", "Test Crew Quarters", "Dt Warp Core"});
    CHECK(vehicleMaxMovement(r, s, wrapped) == 0);
    // A negative bonus is added too.
    Vehicle& slow = ship({"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Dt Slow Engine"});
    CHECK(vehicleMaxMovement(r, s, slow) == 2 - 1);
    // Control: halved once per missing item, never below 1; a Master Computer prevents it.
    Vehicle& lean = ship({"Test Bridge", "Test Engine", "Test Engine", "Test Engine", "Test Engine", "Test Engine", "Test Engine"});
    CHECK(vehicleMaxMovement(r, s, lean) == 6 / 2 / 2);
    Vehicle& auxOnly = ship({"Test Aux Control", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Engine"});
    CHECK(vehicleMaxMovement(r, s, auxOnly) == 2);  // auxiliary control stands in for the bridge
    Vehicle& computer = ship({"Test Master Computer", "Test Engine", "Test Engine"});
    CHECK(vehicleMaxMovement(r, s, computer) == 2);
    Vehicle& bare = ship({"Test Engine", "Test Engine"});
    CHECK(vehicleMaxMovement(r, s, bare) == 1);  // 2 / 2 / 2 / 2, never below 1
    // Racial Vehicle Speed only when the engines give movement; the designer leaves it out.
    s.empire(me).race.traits.push_back(static_cast<uint32_t>(r.data().racialTraits.size() - 1));
    CHECK(vehicleMaxMovement(r, s, *s.vehicle(mixedId)) == 4 + 2);
    CHECK(computeDesignStats(r, &s.empire(me), s.design(s.vehicle(mixedId)->design)).movement == 4);
    // Out of supply: exactly 1.
    s.vehicle(mixedId)->supply = 0;
    CHECK(vehicleMaxMovement(r, s, *s.vehicle(mixedId)) == 1);
}

TEST_CASE("design rules: to-hit modifiers add the best of each family, and the hull in full") {
    const Rules& r = designRules();
    GameState s = newEngineGame(5, 2, 10);
    const DesignId d = addTestDesign(s, r, EmpireId{0u}, "Gunner", "Test Frigate",
                                     {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Dt Aim A", "Dt Aim A2", "Dt Aim B", "Dt Clumsy"});
    const Vehicle& v = addTestVehicle(s, r, d, {SystemId{0u}, Sector{1, 1}});
    CHECK(vehicleToHitOffense(r, s, v) == 15 + 7 - 4);
    CHECK(vehicleToHitDefense(r, s, v) == 0);
}

TEST_CASE("design rules: Edit changes an own prototype in place; built or queued designs are copied or upgraded") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 6);
    const EmpireId me{0u}, them{1u};
    Design d;
    d.name = "Proto";
    d.hull = hullIndex(r, "Test Frigate");
    for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters"}) d.entries.push_back({componentIndex(r, c), -1});
    REQUIRE(apply(r, s, me, cmd::CreateDesign{d}).ok);
    const DesignId id = s.designs.back().id;
    CHECK(designIsPrototype(s.design(id)));
    // Changed in place: the same design, which may keep its name or take a new one.
    Design changed = d;
    changed.entries.push_back({componentIndex(r, "Test Laser"), -1});
    const size_t count = s.designs.size();
    REQUIRE(apply(r, s, me, cmd::EditDesign{id, changed}).ok);
    CHECK(s.designs.size() == count);
    CHECK(s.design(id).entries.size() == 4);
    CHECK(s.design(id).name == "Proto");
    changed.name = "Proto Mk";
    REQUIRE(apply(r, s, me, cmd::EditDesign{id, changed}).ok);
    CHECK(s.design(id).name == "Proto Mk");
    // An edited design starts with no sightings and empty statistics.
    seeDesign(s.empire(them).knowledge, id, s.turn);
    s.design(id).lost = 3;
    REQUIRE(apply(r, s, me, cmd::EditDesign{id, changed}).ok);
    CHECK_FALSE(knowsDesign(s.empire(them).knowledge, id));
    CHECK(s.design(id).lost == 0);
    // Not another empire's, not one in a queue, not one that was built or retrofitted to.
    CHECK_FALSE(apply(r, s, them, cmd::EditDesign{id, changed}).ok);
    ObjectId home;
    for (size_t i = 0; i < s.colonies.size(); ++i)
        if (s.colonies[i] && s.colonies[i]->owner == me) home = ObjectId{static_cast<uint32_t>(i)};
    REQUIRE(home.valid());
    QueueItem item;
    item.design = id;
    s.colony(home)->queue.items.push_back(item);
    CHECK(designInQueue(s, me, id));
    CHECK_FALSE(apply(r, s, me, cmd::EditDesign{id, changed}).ok);
    s.colony(home)->queue.items.clear();
    s.design(id).retrofitted = true;
    CHECK_FALSE(designIsPrototype(s.design(id)));
    CHECK_FALSE(apply(r, s, me, cmd::EditDesign{id, changed}).ok);
    s.design(id).retrofitted = false;
    s.design(id).built = 1;
    CHECK_FALSE(apply(r, s, me, cmd::EditDesign{id, changed}).ok);
    // Copy and Upgrade make new designs (cmd::CreateDesign).
    Design copy = changed;
    copy.name = "Proto Copy";
    REQUIRE(apply(r, s, me, cmd::CreateDesign{copy}).ok);
    CHECK(s.designs.size() == count + 1);
    CHECK(designIsPrototype(s.designs.back()));
}

TEST_CASE("design tools: a hull report states its design rules as the designer checks them (spec 03 §2.2, §4.2)") {
    ruleset::VehicleSize h;
    h.usesEngines = true;
    h.enginesPerMove = 2;
    // Max Engines 0 with engines in use: no limit, not "no engines".
    CHECK(hullRuleLines(h) == std::vector<std::string>{"Engines: no limit", "2 engines per movement point"});
    h.maxEngines = 6;
    CHECK(hullRuleLines(h) == std::vector<std::string>{"Engines: at most 6", "2 engines per movement point"});
    // Can Have Aux Con limits auxiliary controls to one; without it nothing is checked.
    h.mustHaveBridge = true;
    h.canHaveAuxControl = true;
    h.minLifeSupport = 1;
    h.minCrewQuarters = 2;
    h.minPercentColonyModules = 50;
    CHECK(hullRuleLines(h) == std::vector<std::string>{"Needs a bridge", "Auxiliary control: at most 1", "Life support: at least 1",
                                                       "Crew quarters: at least 2", "Engines: at most 6", "2 engines per movement point",
                                                       "Colony modules: at least 50% of the hull"});
    ruleset::VehicleSize base;
    base.usesEngines = false;
    base.minPercentFighterBays = 20;
    base.minPercentCargo = 10;
    CHECK(hullRuleLines(base) ==
          std::vector<std::string>{"Cannot carry engines", "Fighter bays: at least 20% of the hull", "Cargo space: at least 10% of the hull"});
}
