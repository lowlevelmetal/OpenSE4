// The object reports' pages (src/client/classic/reports.*): the Ability page's
// lines (docs/spec/06 §1.4 "The Ability page", §7 Q106), on the engine test
// rules with traits, a culture and population rows of our own.

#include "engine_fixture.hpp"

#include "client/classic/reports.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
using namespace opense4::client::classic;

namespace {

constexpr EmpireId kMe{0};
constexpr EmpireId kThem{1};

// The test rules with a planet trait, a ship trait, a culture with modifiers
// for both, two population rows and a Happy band at 110 %.
struct AbilityRules {
    Rules rules;
    uint32_t planetTrait = 0, shipTrait = 0, lucky = 0;
};

const AbilityRules& abilityRules() {
    static const AbilityRules built = [] {
        ruleset::Ruleset rs = buildEngineRuleset();
        auto trait = [&](std::string name, std::string type, std::string description) {
            ruleset::RacialTrait t;
            t.name = std::move(name);
            t.traitType = std::move(type);
            t.description = std::move(description);
            t.values = {"10"};
            rs.racialTraits.push_back(std::move(t));
            return uint32_t(rs.racialTraits.size() - 1);
        };
        AbilityRules out;
        const uint32_t planet = trait("Our Builders", "Planetary SY Rate", "Our words: builders build faster.");
        const uint32_t ship = trait("Our Gunners", "Ship Attack", "Our words: gunners aim better.");
        const uint32_t lucky = trait("Our Luck", "Luck", "Our words: luck.");
        REQUIRE_FALSE(rs.cultures.empty());
        ruleset::Culture& c = rs.cultures.front();
        c = ruleset::Culture{c.name, c.description};
        c.research = 10;
        c.happiness = -5;
        c.spaceCombat = 15;
        c.maintenance = -10;
        rs.settings.set("Number Of Population Modifiers", "2");
        rs.settings.set("Pop Modifier 1 Population Amount", "500");
        rs.settings.set("Pop Modifier 1 Production Modifier Percent", "100");
        rs.settings.set("Pop Modifier 1 SY Rate Modifier Percent", "100");
        rs.settings.set("Pop Modifier 2 Population Amount", "20000");
        rs.settings.set("Pop Modifier 2 Production Modifier Percent", "90");
        rs.settings.set("Pop Modifier 2 SY Rate Modifier Percent", "125");
        rs.settings.set("Mood Happy Modifier", "110");
        out.rules = Rules(std::move(rs));
        out.planetTrait = planet;
        out.shipTrait = ship;
        out.lucky = lucky;
        return out;
    }();
    return built;
}

bool has(const std::vector<std::string>& lines, std::string_view text) { return std::find(lines.begin(), lines.end(), text) != lines.end(); }

} // namespace

TEST_CASE("reports: the planet Ability page lists the planet, racial, cultural, population and mood lines, never facilities") {
    const AbilityRules& ar = abilityRules();
    const Rules& r = ar.rules;
    GameState s = newEngineGame(3, 2, 12);
    Colony& home = homeworld(s, kMe);
    REQUIRE_FALSE(home.facilities.empty());
    Race& race = s.empire(kMe).race;
    race.traits = {ar.lucky, ar.shipTrait, ar.planetTrait};
    race.culture = 0;
    race.characteristics.fill(100);
    race.characteristics[size_t(Characteristic::Intelligence)] = 120;
    race.characteristics[size_t(Characteristic::Aggressiveness)] = 80;   // a ship line: not here
    race.characteristics[size_t(Characteristic::MiningAptitude)] = 90;
    home.anger = 20;   // Happy: 110 %
    home.population = {{kMe, 1000}};   // the second row (501M to 20000M)
    SpaceObject& planet = s.galaxy.object(home.planet);
    ruleset::Ability natural;
    natural.type = "Planet Natural Test";
    natural.description = "Our words for something the planet does [%Amount1]";
    natural.value1 = "3";
    planet.abilities = {natural};

    const std::vector<std::string> lines = planetAbilityLines(r, s, home.planet, kMe);
    const std::vector<std::string> expected{
        natural.description,                          // 1. the planet's own, as written
        "Our words: builders build faster.",          // 2. a trait that concerns planets
        "Racial Trait: +20% research",                // 3. characteristics, in their order
        "Racial Trait: -10% mineral production",
        "Cultural Trait: +10% research",              // 4. the culture, in its order
        "Cultural Trait: -5% happiness",
        "Population level 501M:  -10% production",    // 5. the row the population falls in
        "Population level 501M: +25% construction rate",
        "Happiness: +10% production",                 // 6. the mood band
    };
    CHECK(lines == expected);
    // No facility line, whatever the colony holds.
    for (uint32_t f : home.facilities)
        for (const ruleset::Ability& a : r.facility(f).abilities)
            if (!a.description.empty()) CHECK_FALSE(has(lines, a.description));

    // The first row starts at 0; at 100 % nothing is written; Indifferent adds nothing.
    home.population = {{kMe, 300}};
    home.anger = 35;
    const std::vector<std::string> small = planetAbilityLines(r, s, home.planet, kMe);
    CHECK(std::none_of(small.begin(), small.end(), [](const std::string& l) { return l.starts_with("Population level") || l.starts_with("Happiness"); }));
    // Without the racial and culture lines (the Combat Simulator's reports).
    home.population = {{kMe, 1000}};
    const std::vector<std::string> sim = planetAbilityLines(r, s, home.planet, kMe, false);
    CHECK(sim == std::vector<std::string>{natural.description, "Population level 501M:  -10% production", "Population level 501M: +25% construction rate"});
    // Another empire's colony, or no colony: the planet's own abilities only.
    CHECK(planetAbilityLines(r, s, home.planet, kThem) == std::vector<std::string>{natural.description});
    // An empty page stays empty.
    planet.abilities.clear();
    CHECK(planetAbilityLines(r, s, home.planet, kThem).empty());
}

TEST_CASE("reports: the ship Ability page lists the hull's entries and the racial and culture lines, never components") {
    const AbilityRules& ar = abilityRules();
    const Rules& r = ar.rules;
    GameState s = newEngineGame(3, 2, 12);
    Race& race = s.empire(kMe).race;
    race.traits = {ar.planetTrait, ar.shipTrait};
    race.culture = 0;
    race.characteristics.fill(100);
    race.characteristics[size_t(Characteristic::Aggressiveness)] = 110;
    race.characteristics[size_t(Characteristic::Intelligence)] = 150;   // a planet line: not here
    const DesignId d = addTestDesign(s, r, kMe, "Lines", "Test Frigate", {"Test Bridge", "Test Cargo Bay", "Test Engine"});
    Vehicle& v = addTestVehicle(s, r, d, locationOf(s.galaxy, homeworld(s, kMe).planet));
    std::vector<std::string> expected;
    for (const ruleset::Ability& a : r.hull(s.design(d).hull).abilities) expected.push_back(a.description);
    const std::vector<std::string> hullOnly = expected;
    expected.insert(expected.end(), {"Our words: gunners aim better.", "Racial Trait: +10% ship attack", "+15% space combat", "-10% maintenance cost"});
    CHECK(vehicleAbilityLines(r, s, v) == expected);
    // Mothballing changes nothing; the Combat Simulator leaves the racial and culture lines out.
    v.status = VehicleStatus::Mothballed;
    CHECK(vehicleAbilityLines(r, s, v) == expected);
    CHECK(vehicleAbilityLines(r, s, v, false) == hullOnly);
    for (const DesignEntry& e : s.design(d).entries)
        for (const ruleset::Ability& a : r.component(e.component).abilities)
            if (!a.description.empty()) CHECK_FALSE(has(vehicleAbilityLines(r, s, v), a.description));
}
