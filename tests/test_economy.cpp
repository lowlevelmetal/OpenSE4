// Economy (docs/spec/02): planet output, delivery, storage, maintenance,
// construction queues, population, happiness and plague, on the original
// engine test rules. The last two tests calibrate against, and run, the
// player's installed data set (opt-in: OPENSE4_CLASSIC_DATA=auto or a data
// directory).
//
// Where a rule applies a percentage in floating point, the expected values
// use the same xmath helpers (e.g. trunc(800 × 105 %) is 839, not 840).

#include "engine_fixture.hpp"

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/combat.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/events.hpp"
#include "game/query.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
using xmath::pctRound;
using xmath::pctTrunc;

namespace {

const EmpireId kMe{0u};
const EmpireId kThem{1u};

ruleset::Ability ability(AbilityKind k, int64_t v1 = 0, int64_t v2 = 0) {
    ruleset::Ability a;
    a.type = std::string(identifier(k));
    a.value1 = std::to_string(v1);
    a.value2 = std::to_string(v2);
    return a;
}

// Adds a facility with a family of its own (so no upgrade chain is affected).
void addFacility(ruleset::Ruleset& rs, std::string name, std::vector<ruleset::Ability> abilities, ruleset::Cost cost = {100, 0, 0}) {
    ruleset::Facility f;
    f.name = std::move(name);
    f.group = "Test";
    f.cost = cost;
    f.abilities = std::move(abilities);
    f.family = 5000 + static_cast<int>(rs.facilities.size());
    f.romanNumeral = 1;
    rs.facilities.push_back(std::move(f));
}

void setKey(ruleset::Ruleset& rs, std::string key, int64_t value) { rs.settings.set(std::move(key), std::to_string(value)); }

uint32_t addTrait(ruleset::Ruleset& rs, std::string name, std::string type, int value) {
    ruleset::RacialTrait t;
    t.name = std::move(name);
    t.generalType = "Advantage";
    t.traitType = std::move(type);
    t.values = {std::to_string(value)};
    rs.racialTraits.push_back(std::move(t));
    return static_cast<uint32_t>(rs.racialTraits.size() - 1);
}

void giveTrait(const Rules& r, GameState& s, EmpireId e, std::string_view name) {
    for (uint32_t i = 0; i < r.data().racialTraits.size(); ++i)
        if (r.data().racialTraits[i].name == name) s.empire(e).race.traits.push_back(i);
}

// Test rules: the shared engine fixture plus a tweak.
std::unique_ptr<Rules> tweakedRules(const std::function<void(ruleset::Ruleset&)>& tweak) {
    ruleset::Ruleset rs = buildEngineRuleset();
    tweak(rs);
    rs.reindex();
    return std::make_unique<Rules>(std::move(rs));
}

GameState newGame(const Rules& r, uint64_t seed = 7, int empires = 2) {
    GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = 12;
    setup.options.simultaneous = true;  // written for simultaneous turns
    for (int i = 0; i < empires; ++i) {
        EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        setup.empires.push_back(std::move(e));
    }
    auto g = createGame(r, setup);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    return std::move(*g);
}

// Empire 0's homeworld in a known state: the given facilities, 1000M of the
// owner's race, 100 % value, Mild conditions (1.0), Indifferent mood.
Colony& plainHome(const Rules& r, GameState& s, std::initializer_list<std::string_view> facilities, EmpireId e = kMe) {
    Colony& c = homeworld(s, e);
    c.facilities.clear();
    for (auto f : facilities) c.facilities.push_back(facilityIndex(r, f));
    c.population = {{e, 1000}};
    c.anger = 35;
    SpaceObject& p = s.galaxy.object(c.planet);
    p.value = {100, 100, 100};
    p.conditions = Conditions::hundredths(100);
    return c;
}

void dropVehicles(GameState& s, EmpireId e) { std::erase_if(s.vehicles, [&](const Vehicle& v) { return v.owner == e; }); }

// The economy's money steps of every living empire's end-of-turn processing,
// in their order (spec 02 §12): income, trade, maintenance, construction and
// the storage cap.
std::vector<MoodEvent> economyTurn(const Rules& r, GameState& s) {
    TurnContext ctx{r, s, {}, {}, {}};
    for (size_t i = 0; i < s.empires.size(); ++i) {
        const EmpireId id{i};
        if (!s.empire(id).alive) continue;
        economy::collectIncome(ctx, id);
        economy::collectTrade(ctx, id);
        economy::payMaintenance(ctx, id);
        economy::runConstruction(ctx, id);
        economy::applyStorageCap(ctx, id);
    }
    return ctx.moodEvents;
}

// The population steps of every living empire: planets, happiness (with
// `moods`), system-wide abilities.
std::vector<MoodEvent> populationTurn(const Rules& r, GameState& s, std::vector<MoodEvent> moods = {}) {
    TurnContext ctx{r, s, std::move(moods), {}, {}};
    for (size_t i = 0; i < s.empires.size(); ++i) {
        const EmpireId id{i};
        if (!s.empire(id).alive) continue;
        economy::processPlanets(ctx, id);
        economy::updateHappiness(ctx, id);
        economy::applySystemAbilities(ctx, id);
    }
    return ctx.moodEvents;
}

// An uncolonized planet: in `sys` (same = true) or outside it.
ObjectId freePlanet(const GameState& s, SystemId sys, bool same) {
    for (const SpaceObject& o : s.galaxy.objects)
        if (o.kind == ObjectKind::Planet && !s.colony(o.id) && (o.system == sys) == same) return o.id;
    FAIL("no free planet");
    return {};
}

Colony& addColony(GameState& s, EmpireId e, ObjectId planet, int64_t population) {
    Colony c;
    c.planet = planet;
    c.owner = e;
    c.population = {{e, population}};
    c.anger = 35;
    s.colonies[planet.index()] = c;
    SpaceObject& p = s.galaxy.object(planet);
    p.atmosphere = s.empire(e).race.atmosphere;
    p.value = {100, 100, 100};
    p.conditions = Conditions::hundredths(100);
    return *s.colonies[planet.index()];
}

bool logged(const GameState& s, EmpireId e, std::string_view text) {
    for (const LogEntry& l : s.empire(e).log)
        if (l.title.find(text) != std::string::npos || l.text.find(text) != std::string::npos) return true;
    return false;
}

int countMood(const std::vector<MoodEvent>& events, std::string_view trigger) {
    int n = 0;
    for (const MoodEvent& m : events) n += m.trigger == trigger ? std::max(1, m.count) : 0;
    return n;
}

DesignId frigate(GameState& s, const Rules& r, EmpireId e, std::string_view name = "Frigate") {
    return addTestDesign(s, r, e, name, "Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
}

void setChar(GameState& s, EmpireId e, Characteristic c, int v) { s.empire(e).race.characteristics[static_cast<size_t>(c)] = v; }

int vehiclesOf(const GameState& s, EmpireId e) {
    int n = 0;
    for (const Vehicle& v : s.vehicles) n += v.owner == e;
    return n;
}

} // namespace

// ---- Scales and tables ----------------------------------------------------------------------------

TEST_CASE("economy: mood bands in whole percent, capitals and new colonies") {
    CHECK(moodFromAnger(100) == Mood::Rioting);
    CHECK(moodFromAnger(90) == Mood::Rioting);
    CHECK(moodFromAnger(89) == Mood::Angry);
    CHECK(moodFromAnger(60) == Mood::Angry);
    CHECK(moodFromAnger(59) == Mood::Unhappy);
    CHECK(moodFromAnger(45) == Mood::Unhappy);
    CHECK(moodFromAnger(44) == Mood::Indifferent);
    CHECK(moodFromAnger(30) == Mood::Indifferent);
    CHECK(moodFromAnger(29) == Mood::Happy);
    CHECK(moodFromAnger(15) == Mood::Happy);
    CHECK(moodFromAnger(14) == Mood::Jubilant);
    CHECK(moodFromAnger(0) == Mood::Jubilant);

    Colony c;
    CHECK(c.anger == 25);  // a new colony starts Happy
    CHECK(moodFromAnger(c.anger) == Mood::Happy);
    CHECK(c.maxAnger() == 100);
    c.homeworld = true;  // a capital never riots
    CHECK(c.maxAnger() == 80);

    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = homeworld(s, kMe);
    CHECK(home.anger == 25);
    economy::setAnger(r, s, home, 95);
    CHECK(home.anger == 80);
    economy::setAnger(r, s, home, -4);
    CHECK(home.anger == 0);
}

TEST_CASE("economy: population modifier rows, mood percentages and condition bands") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        setKey(rs, "Number Of Population Modifiers", 3);
        setKey(rs, "Pop Modifier 1 Population Amount", 19);
        setKey(rs, "Pop Modifier 1 Production Modifier Percent", 100);
        setKey(rs, "Pop Modifier 1 SY Rate Modifier Percent", 100);
        setKey(rs, "Pop Modifier 2 Population Amount", 39);
        setKey(rs, "Pop Modifier 2 Production Modifier Percent", 101);
        setKey(rs, "Pop Modifier 2 SY Rate Modifier Percent", 102);
        setKey(rs, "Pop Modifier 3 Population Amount", 99);
        setKey(rs, "Pop Modifier 3 Production Modifier Percent", 110);
        setKey(rs, "Pop Modifier 3 SY Rate Modifier Percent", 120);
        setKey(rs, "Mood Happy Modifier", 150);
    });
    using economy::populationModifier;
    CHECK(populationModifier(*r, 0).production == 100);
    CHECK(populationModifier(*r, 19).production == 100);
    CHECK(populationModifier(*r, 20).production == 101);
    CHECK(populationModifier(*r, 20).shipyard == 102);
    CHECK(populationModifier(*r, 99).production == 110);
    CHECK(populationModifier(*r, 100).production == 100);  // above the last row: 100 %
    CHECK(populationModifier(*r, 5000).shipyard == 100);
    CHECK(populationModifier(engineRules(), 5000).production == 100);  // no table: no change

    // The first row in file order that covers the population, sorted or not.
    auto unsorted = tweakedRules([](ruleset::Ruleset& rs) {
        setKey(rs, "Number Of Population Modifiers", 2);
        setKey(rs, "Pop Modifier 1 Population Amount", 50);
        setKey(rs, "Pop Modifier 1 Production Modifier Percent", 150);
        setKey(rs, "Pop Modifier 2 Population Amount", 20);
        setKey(rs, "Pop Modifier 2 Production Modifier Percent", 120);
    });
    CHECK(populationModifier(*unsorted, 10).production == 150);

    CHECK(economy::moodOutputPercent(engineRules(), Mood::Rioting) == 0);
    CHECK(economy::moodOutputPercent(engineRules(), Mood::Angry) == 80);
    CHECK(economy::moodOutputPercent(engineRules(), Mood::Indifferent) == 100);
    CHECK(economy::moodOutputPercent(engineRules(), Mood::Jubilant) == 120);
    CHECK(economy::moodOutputPercent(*r, Mood::Happy) == 150);
    CHECK(economy::moodReproduction(Mood::Rioting) == 0);
    CHECK(economy::moodReproduction(Mood::Angry) == -5);
    CHECK(economy::moodReproduction(Mood::Unhappy) == -2);
    CHECK(economy::moodReproduction(Mood::Indifferent) == 0);
    CHECK(economy::moodReproduction(Mood::Happy) == 2);
    CHECK(economy::moodReproduction(Mood::Jubilant) == 5);

    // Conditions are hundredths of the 0-1.5 scale.
    using economy::ConditionsBand;
    const auto band = [](int64_t hundredths) { return economy::conditionsBand(Conditions::hundredths(hundredths)); };
    CHECK(band(150) == ConditionsBand::Optimal);
    CHECK(band(149) == ConditionsBand::Good);
    CHECK(band(130) == ConditionsBand::Good);
    CHECK(band(129) == ConditionsBand::Mild);
    CHECK(band(100) == ConditionsBand::Mild);
    CHECK(band(99) == ConditionsBand::Unpleasant);
    CHECK(band(50) == ConditionsBand::Unpleasant);
    CHECK(band(49) == ConditionsBand::Harsh);
    CHECK(band(31) == ConditionsBand::Harsh);
    CHECK(band(29) == ConditionsBand::Deadly);
    CHECK(band(0) == ConditionsBand::Deadly);
    // The edges are x87 constants (inferred): the double nearest 1.3 lies above
    // 1.3 and is Good, the double nearest 0.3 lies below 0.3 and is Deadly.
    CHECK(Conditions::hundredths(130).value() > xmath::Ext(13) / xmath::Ext(10));
    CHECK(Conditions::hundredths(30).value() < xmath::Ext(3) / xmath::Ext(10));
    CHECK(band(30) == ConditionsBand::Deadly);
    CHECK(economy::conditionsBand(Conditions::of(xmath::Ext(3) / xmath::Ext(10) + xmath::Ext(1) / xmath::Ext(1'000'000))) == ConditionsBand::Harsh);
    CHECK(economy::conditionsName(ConditionsBand::Good) == "Good");
    CHECK(economy::conditionsReproduction(ConditionsBand::Optimal) == 5);
    CHECK(economy::conditionsReproduction(ConditionsBand::Good) == 2);
    CHECK(economy::conditionsReproduction(ConditionsBand::Mild) == 0);
    CHECK(economy::conditionsReproduction(ConditionsBand::Unpleasant) == -2);
    CHECK(economy::conditionsReproduction(ConditionsBand::Harsh) == -5);
    CHECK(economy::conditionsReproduction(ConditionsBand::Deadly) == -20);
}

TEST_CASE("economy: racial effects add the characteristic, culture and traits") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        addTrait(rs, "Miners", "Mineral Production", 7);
        addTrait(rs, "Workers", "Production", 3);
        addTrait(rs, "Builders", "SY Rate", 4);
        addTrait(rs, "Frugal", "Maintenance Cost", 5);
        addTrait(rs, "Tough", "Tollerance", 15);
    });
    GameState s = newGame(*r);
    const Race& race = s.empire(kMe).race;
    using economy::RacialEffect;
    CHECK(economy::racialEffect(*r, race, RacialEffect::MineralOutput) == 5);  // culture Production
    setChar(s, kMe, Characteristic::MiningAptitude, 110);
    for (auto name : {"Miners", "Workers", "Builders", "Frugal", "Tough"}) giveTrait(*r, s, kMe, name);
    CHECK(economy::racialEffect(*r, race, RacialEffect::MineralOutput) == 10 + 5 + 7 + 3);
    CHECK(economy::racialEffect(*r, race, RacialEffect::OrganicsOutput) == 5 + 3);
    CHECK(economy::racialEffect(*r, race, RacialEffect::ShipyardRate) == 4);
    CHECK(economy::racialEffect(*r, race, RacialEffect::Maintenance) == 5);
    CHECK(economy::racialEffect(*r, race, RacialEffect::EnvironmentalResistance) == 15);
    CHECK(economy::maintenancePercent(*r, s.empire(kMe)) == 20);  // 25 - 5
}

TEST_CASE("economy: characteristic point costs beyond the threshold") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        setKey(rs, "Characteristic Mining Aptitude Pct Cost", 25);
        setKey(rs, "Characteristic Mining Aptitude Threshold", 20);
        setKey(rs, "Characteristic Mining Aptitude Threshhold Pct Cost Pos", 100);
        setKey(rs, "Characteristic Mining Aptitude Threshhold Pct Cost Neg", 10);
    });
    using economy::characteristicPointCost;
    CHECK(characteristicPointCost(*r, Characteristic::MiningAptitude, 100) == 0);
    CHECK(characteristicPointCost(*r, Characteristic::MiningAptitude, 110) == 250);
    CHECK(characteristicPointCost(*r, Characteristic::MiningAptitude, 120) == 500);
    CHECK(characteristicPointCost(*r, Characteristic::MiningAptitude, 130) == 25 * 20 + 100 * 10);  // P per point, not c × P %
    CHECK(characteristicPointCost(*r, Characteristic::MiningAptitude, 90) == -250);
    CHECK(characteristicPointCost(*r, Characteristic::MiningAptitude, 70) == -25 * 20 - 10 * 10);
    CHECK(characteristicPointCost(*r, Characteristic::Cunning, 130) == 0);  // no costs set
}

// ---- Planet output ---------------------------------------------------------------------------------

TEST_CASE("economy: planet output steps: value, race, mood and population") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {"Test Spaceport", "Test Mine", "Test Mine", "Test Farm", "Test Refinery", "Test Lab", "Test Intel Center"});
    s.galaxy.object(home.planet).value = {100, 50, 150};

    // Indifferent, no population table, Steady culture +5 % production.
    auto out = economy::colonyOutput(r, s, home);
    CHECK(out.production == Resources{pctTrunc(1600, 105), pctTrunc(pctRound(800, 50), 105), pctTrunc(pctRound(800, 150), 105)});
    CHECK(out.production == Resources{1679, 419, 1260});  // extended precision: 1600 × 105 % truncates to 1679
    CHECK(out.research == 500);
    CHECK(out.intelligence == 300);
    CHECK(out.connected);
    CHECK_FALSE(out.blockaded);
    CHECK(out.deliveryPercent == 100);
    CHECK(out.mood == Mood::Indifferent);
    CHECK(out.facilitiesOperating == 6);
    CHECK(out.productionPercent[0] == 105);

    home.anger = 20;  // Happy: +10
    out = economy::colonyOutput(r, s, home);
    CHECK(out.mood == Mood::Happy);
    CHECK(out.production[Resource::Minerals] == pctTrunc(1600, 115));
    CHECK(out.research == 550);

    setChar(s, kMe, Characteristic::MiningAptitude, 120);
    setChar(s, kMe, Characteristic::Intelligence, 130);
    setChar(s, kMe, Characteristic::Cunning, 80);
    out = economy::colonyOutput(r, s, home);
    CHECK(out.production[Resource::Minerals] == pctTrunc(1600, 135));
    CHECK(out.production[Resource::Organics] == pctTrunc(400, 115));
    CHECK(out.research == 700);
    CHECK(out.intelligence == 270);

    // The percentage never goes below 0.
    setChar(s, kMe, Characteristic::MiningAptitude, 1);
    CHECK(economy::colonyOutput(r, s, home).production[Resource::Minerals] == pctTrunc(1600, 16));
    setChar(s, kMe, Characteristic::MiningAptitude, -500);
    CHECK(economy::colonyOutput(r, s, home).production[Resource::Minerals] == 0);
    setChar(s, kMe, Characteristic::MiningAptitude, 100);

    home.anger = 95;  // rioting: nothing at all
    out = economy::colonyOutput(r, s, home);
    CHECK(out.mood == Mood::Rioting);
    CHECK(out.production.isZero());
    CHECK(out.research == 0);
    CHECK(out.intelligence == 0);
}

TEST_CASE("economy: planet modifier multiplies on its own; the system modifier works on the system total") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        addFacility(rs, "Booster", {ability(AbilityKind::ResourceGenModPlanetMinerals, 20)});
        addFacility(rs, "Weak Booster", {ability(AbilityKind::ResourceGenModPlanetMinerals, 10)});
        addFacility(rs, "Bad Booster", {ability(AbilityKind::ResourceGenModPlanetOrganics, -30)});
        addFacility(rs, "System Booster", {ability(AbilityKind::ResourceGenModSystemMinerals, 30)});
        addFacility(rs, "Think Tank", {ability(AbilityKind::PlanetPointGenModResearch, 50)});
        addFacility(rs, "Solar Plant", {ability(AbilityKind::SolarResourceGenOrganics, 100)});
    });
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    Colony& home =
        plainHome(*r, s, {"Test Spaceport", "Test Mine", "Booster", "Weak Booster", "Test Lab", "Think Tank", "Solar Plant", "Test Depot"});
    const SystemId sys = s.galaxy.object(home.planet).system;
    const int stars = [&] {
        int n = 0;
        for (ObjectId o : s.galaxy.system(sys).objects) n += s.galaxy.object(o).kind == ObjectKind::Star;
        return n;
    }();
    REQUIRE(stars > 0);

    auto out = economy::colonyOutput(*r, s, home);
    const int64_t minerals = pctTrunc(pctRound(800, 120), 105);  // best planet modifier, then culture
    CHECK(minerals == 1007);
    CHECK(out.production[Resource::Minerals] == minerals);
    CHECK(out.research == 750);
    CHECK(out.solar == Resources{0, 100 * stars, 0});  // no modifiers on solar output
    CHECK(out.production[Resource::Organics] == 100 * stars);
    CHECK(out.supply == 5000);

    // A negative "best" modifier never applies.
    home.facilities.push_back(facilityIndex(*r, "Bad Booster"));
    home.facilities.push_back(facilityIndex(*r, "Test Farm"));
    CHECK(economy::colonyOutput(*r, s, home).production[Resource::Organics] == pctTrunc(800, 105) + 100 * stars);
    home.facilities.resize(home.facilities.size() - 2);

    // A system booster on another colony raises the system's total, rounded once.
    Colony& moon = addColony(s, kMe, freePlanet(s, sys, true), 100);
    moon.facilities = {facilityIndex(*r, "System Booster"), facilityIndex(*r, "Test Mine")};
    CHECK(economy::colonyOutput(*r, s, home).production[Resource::Minerals] == minerals);  // not per colony
    const int64_t moonMinerals = pctTrunc(800, 105);
    const economy::Production p = economy::empireProduction(*r, s, kMe);
    CHECK(p.resources[Resource::Minerals] == pctRound(minerals + moonMinerals, 130));
    CHECK(p.resources[Resource::Organics] == 100 * stars);
    CHECK(p.research == 750);
    CHECK(p.undelivered.isZero());

    // A tiny population still runs every facility (spec 02 §13 Q3).
    home.population = {{kMe, 4}};
    out = economy::colonyOutput(*r, s, home);
    CHECK(out.facilitiesOperating == 3);
    CHECK(out.production[Resource::Minerals] == minerals);
    CHECK(out.research == 750);
    CHECK(out.supply == 5000);

    // No population: nothing works.
    home.population = {{kMe, 0}};
    out = economy::colonyOutput(*r, s, home);
    CHECK(out.production.isZero());
    CHECK(out.supply == 0);
}

TEST_CASE("economy: output reaches the treasury only through a spaceport") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) { addTrait(rs, "Free Traders", "No Spaceports", 0); });
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    Colony& home = plainHome(*r, s, {"Test Mine"});
    const SystemId homeSys = s.galaxy.object(home.planet).system;
    const int64_t mine = pctTrunc(800, 105);

    auto out = economy::colonyOutput(*r, s, home);
    CHECK_FALSE(out.connected);
    CHECK(out.deliveryPercent == 25);  // the home system still delivers a quarter

    Colony& far = addColony(s, kMe, freePlanet(s, homeSys, false), 1000);
    far.facilities = {facilityIndex(*r, "Test Mine")};
    CHECK(economy::colonyOutput(*r, s, far).deliveryPercent == 0);

    economyTurn(*r, s);
    const EconomyReport& rep = s.empire(kMe).economy;
    CHECK(rep.colonies[Resource::Minerals] == pctTrunc(mine, 25));
    CHECK(rep.undelivered[Resource::Minerals] == mine - pctTrunc(mine, 25) + mine);

    // A spaceport facility works even on a colony without population.
    far.facilities.push_back(facilityIndex(*r, "Test Spaceport"));
    far.population = {{kMe, 0}};
    out = economy::colonyOutput(*r, s, far);
    CHECK(out.connected);
    CHECK(out.deliveryPercent == 100);

    // The home system is recorded at game creation and never moves: after the
    // homeworld is lost its other colonies there keep the quarter, and a
    // capital elsewhere never gets it (spec 02 §2, §5.5).
    CHECK(s.empire(kMe).homeSystem == homeSys);
    far.facilities = {facilityIndex(*r, "Test Mine")};
    far.population = {{kMe, 1000}};
    far.homeworld = true;
    Colony& moon = addColony(s, kMe, freePlanet(s, homeSys, true), 1000);
    moon.facilities = {facilityIndex(*r, "Test Mine")};
    s.colonies[home.planet.index()].reset();
    CHECK(economy::colonyOutput(*r, s, moon).deliveryPercent == 25);
    CHECK(economy::colonyOutput(*r, s, far).deliveryPercent == 0);

    // The No Spaceports trait connects every system.
    giveTrait(*r, s, kMe, "Free Traders");
    CHECK(economy::colonyOutput(*r, s, far).deliveryPercent == 100);
}

TEST_CASE("economy: visible enemy ships and bases in the sector blockade a planet") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {"Test Spaceport", "Test Mine"});
    const Location here = locationOf(s.galaxy, home.planet);
    const DesignId raider = frigate(s, r, kThem, "Raider");
    const VehicleId id = addTestVehicle(s, r, raider, here).id;

    auto out = economy::colonyOutput(r, s, home);
    CHECK(out.blockaded);
    CHECK(out.production.isZero());  // nothing is produced; the output is lost

    s.vehicle(id)->status = VehicleStatus::Mothballed;
    CHECK_FALSE(economy::colonyBlockaded(r, s, home));
    s.vehicle(id)->status = VehicleStatus::Cloaked;
    CHECK_FALSE(economy::colonyBlockaded(r, s, home));
    s.empire(kMe).knowledge.visibleVehicles = {id};  // a cloaked ship we can see does blockade
    CHECK(economy::colonyBlockaded(r, s, home));
    s.empire(kMe).knowledge.visibleVehicles.clear();
    s.vehicle(id)->status = VehicleStatus::Normal;
    CHECK(economy::colonyBlockaded(r, s, home));
    s.empire(kMe).relation(kThem).treaty = Treaty::NonAggression;
    s.empire(kThem).relation(kMe).treaty = Treaty::NonAggression;
    CHECK_FALSE(economy::colonyBlockaded(r, s, home));
    s.empire(kMe).relation(kThem).treaty = Treaty::None;
    s.empire(kThem).relation(kMe).treaty = Treaty::None;

    // Units never blockade.
    s.vehicle(id)->count = 0;
    s.removeDeadVehicles();
    const DesignId fighter = addTestDesign(s, r, kThem, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    addTestVehicle(s, r, fighter, here);
    CHECK_FALSE(economy::colonyBlockaded(r, s, home));
}

// ---- Treasury ----------------------------------------------------------------------------------------

TEST_CASE("economy: storage caps the treasury; storage facilities and traits raise it") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) { addTrait(rs, "Hoarders", "Mineral Storage", 50); });
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    plainHome(*r, s, {"Test Spaceport", "Test Mine", "Test Mineral Store"});
    CHECK(economy::storageCapacity(*r, s, kMe) == Resources{70000, 50000, 50000});
    s.empire(kMe).stockpile = {69900, 49990, 0};
    economyTurn(*r, s);
    const EconomyReport& rep = s.empire(kMe).economy;
    const int64_t mine = pctTrunc(800, 105);
    CHECK(rep.storageCap == Resources{70000, 50000, 50000});
    CHECK(rep.otherIncome == Resources{0, 200, 200});  // nothing delivered: the Settings minimum instead
    CHECK(s.empire(kMe).stockpile == Resources{70000, 50000, 200});
    CHECK(rep.lostToStorage == Resources{69900 + mine - 70000, 190, 0});

    giveTrait(*r, s, kMe, "Hoarders");
    CHECK(economy::storageCapacity(*r, s, kMe) == Resources{50000 + pctRound(20000, 150), 50000, 50000});
}

TEST_CASE("economy: generated points, the minimum income and the opening research pool") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        addFacility(rs, "Mint", {ability(AbilityKind::GeneratePointsMinerals, 300), ability(AbilityKind::GeneratePointsResearch, 50)});
    });
    GameState s = newGame(*r);
    // The opening pools are set when the game is created: Starting Resources
    // plus one turn of research, no intelligence (spec 05 §1.1).
    CHECK(s.empire(kMe).researchPool == s.options.startingResources[Resource::Minerals] + economy::empireProduction(*r, s, kMe).research);
    CHECK(s.empire(kMe).intelPool == 0);
    dropVehicles(s, kMe);
    Colony& home = plainHome(*r, s, {"Mint"});  // no spaceport: flat points need none
    s.empire(kMe).stockpile = {};
    s.empire(kMe).researchPool = 0;
    economyTurn(*r, s);
    const EconomyReport& rep = s.empire(kMe).economy;
    // The colonies deliver exactly 0 of everything, so each resource gets the Settings
    // minimum; generated points come on top.
    CHECK(rep.otherIncome == Resources{500, 200, 200});
    CHECK(rep.research == 50);  // the income holds no opening pool
    CHECK(s.empire(kMe).researchPool == 50);  // the income step fills the pool
    CHECK(s.empire(kMe).stockpile == Resources{500, 200, 200});
    s.turn = 1;
    economyTurn(*r, s);
    CHECK(s.empire(kMe).economy.research == 50);

    // A small positive delivery is kept as it is: the minimum is not a floor.
    home.facilities = {facilityIndex(*r, "Test Spaceport"), facilityIndex(*r, "Test Mine")};
    s.galaxy.object(home.planet).value = {10, 100, 100};
    s.empire(kMe).stockpile = {};
    economyTurn(*r, s);
    CHECK(s.empire(kMe).economy.colonies[Resource::Minerals] == pctTrunc(pctRound(800, 10), 105));
    CHECK(s.empire(kMe).stockpile == Resources{84, 200, 200});

    // Every living empire gets it, also one left with ships only (spec 02 §5.6).
    for (auto& c : s.colonies)
        if (c && c->owner == kMe) c.reset();
    s.empire(kMe).stockpile = {};
    economyTurn(*r, s);
    CHECK(s.empire(kMe).stockpile == Resources{200, 200, 200});
}

TEST_CASE("economy: the computer player bonus multiplies income and construction") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    dropVehicles(s, kMe);
    Colony& home = plainHome(r, s, {"Test Spaceport", "Test Mine", "Test Lab", "Test Space Yard"});
    const cmd::QueueTarget q{home.planet, {}};
    s.turn = 1;  // past the opening research pool
    s.empire(kMe).stockpile = {};
    economyTurn(r, s);
    const int64_t mine = pctTrunc(800, 105);
    CHECK(s.empire(kMe).stockpile == Resources{mine, 200, 200});
    const int64_t research = s.empire(kMe).economy.research;

    s.empire(kMe).kind = PlayerKind::Computer;
    s.options.aiBonus = 3;  // High: income × 5, construction × 3
    CHECK(ai::incomeBonusFactor(s, kMe) == 5);
    s.empire(kMe).stockpile = {};
    economyTurn(r, s);
    CHECK(s.empire(kMe).stockpile == Resources{5 * mine, 1000, 1000});
    CHECK(s.empire(kMe).economy.research == 5 * research);
    CHECK(economy::constructionRate(r, s, kMe, q) == Resources{6000, 6000, 6000});
    s.options.aiBonus = 1;  // Low: × 1.5, truncated
    CHECK(economy::constructionRate(r, s, kMe, q) == Resources{3000, 3000, 3000});
    s.options.aiBonus = 0;
    CHECK(economy::constructionRate(r, s, kMe, q) == Resources{2000, 2000, 2000});
}

TEST_CASE("economy: maintenance truncates three times; units and mothballed ships pay nothing") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        for (auto& h : rs.vehicleSizes)
            if (h.name == "Test Station") h.abilities.push_back(ability(AbilityKind::ModifiedMaintenanceCost, -50));
        addFacility(rs, "Dry Dock", {ability(AbilityKind::ReducedMaintenanceSystem, 20)});
    });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {"Dry Dock"});
    const Location homeLoc = locationOf(s.galaxy, home.planet);
    CHECK(economy::maintenancePercent(*r, s.empire(kMe)) == 25);
    setChar(s, kMe, Characteristic::MaintenanceAptitude, 110);
    CHECK(economy::maintenancePercent(*r, s.empire(kMe)) == 15);
    setChar(s, kMe, Characteristic::MaintenanceAptitude, 150);
    CHECK(economy::maintenancePercent(*r, s.empire(kMe)) == 5);  // never below 5 %
    setChar(s, kMe, Characteristic::MaintenanceAptitude, 100);

    const DesignId ship = frigate(s, *r, kMe);  // costs {160, 20, 20}
    const DesignId base = addTestDesign(s, *r, kMe, "Fort", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters"});
    const Location away{s.galaxy.object(freePlanet(s, homeLoc.system, false)).system, Sector{1, 1}};
    const VehicleId a = addTestVehicle(s, *r, ship, away).id;
    const VehicleId b = addTestVehicle(s, *r, ship, homeLoc).id;
    const VehicleId c = addTestVehicle(s, *r, base, away).id;
    CHECK(economy::vehicleMaintenance(*r, s, *s.vehicle(a)) == Resources{40, 5, 5});
    CHECK(economy::vehicleMaintenance(*r, s, *s.vehicle(b)) == Resources{32, 4, 4});  // 20 % off in the dry dock's system
    QueueItem fort;
    fort.design = base;
    const Resources fortCost = economy::itemCost(*r, s, kMe, cmd::QueueTarget{home.planet, {}}, fort);
    Resources half;
    for (size_t k = 0; k < 3; ++k) half.v[k] = pctTrunc(pctTrunc(fortCost.v[k], 25), 50);  // bases pay half, each step truncated
    CHECK(economy::vehicleMaintenance(*r, s, *s.vehicle(c)) == half);
    s.vehicle(a)->status = VehicleStatus::Mothballed;
    CHECK(economy::vehicleMaintenance(*r, s, *s.vehicle(a)).isZero());

    // Units pay no maintenance, in space or in cargo.
    const DesignId fighter = addTestDesign(s, *r, kMe, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    const VehicleId f = addTestVehicle(s, *r, fighter, away).id;
    CHECK(economy::vehicleMaintenance(*r, s, *s.vehicle(f)).isZero());

    Resources total;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kMe) total += economy::vehicleMaintenance(*r, s, v);
    CHECK(economy::maintenanceCost(*r, s, kMe) == total);
}

TEST_CASE("economy: unpaid maintenance abandons whole vehicles, ships out of supply first") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        setKey(rs, "Maintenance Cost Amt Per Dead", 100);
        setKey(rs, "Minimum Empire Minerals Generation", 0);
        setKey(rs, "Minimum Empire Organics Generation", 0);
        setKey(rs, "Minimum Empire Radioactives Generation", 0);
    });
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    Colony& home = plainHome(*r, s, {});
    const DesignId ship = frigate(s, *r, kMe);
    for (int i = 0; i < 10; ++i) addTestVehicle(s, *r, ship, locationOf(s.galaxy, home.planet)).supply = 100;
    s.empire(kMe).stockpile = {};
    CHECK(economy::maintenanceCost(*r, s, kMe) == Resources{400, 50, 50});

    const auto moods = economyTurn(*r, s);
    CHECK(vehiclesOf(s, kMe) == 4);  // 500 unpaid: 500 div 100 + 1 = 6 abandoned
    CHECK(s.design(ship).lost == 6);
    CHECK(countMood(moods, "Any Ship Lost") == 0);  // no happiness event for abandoned ships
    CHECK(countMood(moods, "Ship Lost in System") == 0);
    CHECK(logged(s, kMe, "abandoned"));
    CHECK(s.empire(kMe).stockpile.isZero());

    // Ships out of supply are the only candidates while there are any.
    for (Vehicle& v : s.vehicles)
        if (v.owner == kMe) v.supply = 100;
    const VehicleId dry = s.vehicles.back().id;
    s.vehicle(dry)->supply = 0;
    s.empire(kMe).stockpile = {};
    economyTurn(*r, s);  // 200 unpaid: 3 victims, but only one candidate
    CHECK(vehiclesOf(s, kMe) == 3);
    CHECK(s.vehicle(dry) == nullptr);

    // A little unpaid still costs one vehicle.
    s.empire(kMe).stockpile = {120, 15, 14};
    economyTurn(*r, s);
    CHECK(vehiclesOf(s, kMe) == 2);
}

// ---- Construction ------------------------------------------------------------------------------------

TEST_CASE("economy: construction rates from yards, population, race, culture and modes") {
    uint32_t trait = 0;
    auto r = tweakedRules([&](ruleset::Ruleset& rs) {
        setKey(rs, "Number Of Population Modifiers", 2);
        setKey(rs, "Pop Modifier 1 Population Amount", 999);
        setKey(rs, "Pop Modifier 1 SY Rate Modifier Percent", 100);
        setKey(rs, "Pop Modifier 2 Population Amount", 99999);
        setKey(rs, "Pop Modifier 2 SY Rate Modifier Percent", 130);
        setKey(rs, "Empire Base Planet Mineral Usage Rate", 1000);
        rs.cultures[0].shipyardRate = 10;
        trait = addTrait(rs, "Hard Workers", "Planetary SY Rate", 25);
    });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {"Test Space Yard"});
    const cmd::QueueTarget q{home.planet, {}};
    CHECK(economy::constructionRate(*r, s, kMe, q) == Resources{2800, 2800, 2800});  // 2000 × (130 + 10) %

    // No yard: the Settings base rate with only the population modifier.
    home.facilities.clear();
    CHECK(economy::constructionRate(*r, s, kMe, q) == Resources{1300, 2600, 2600});
    home.population = {{kMe, 500}};
    CHECK(economy::constructionRate(*r, s, kMe, q) == Resources{1000, 2000, 2000});
    home.population = {{kMe, 1000}};
    home.facilities = {facilityIndex(*r, "Test Space Yard")};

    setChar(s, kMe, Characteristic::ConstructionAptitude, 120);
    CHECK(economy::constructionRate(*r, s, kMe, q) == Resources{3200, 3200, 3200});
    s.empire(kMe).race.traits.push_back(trait);
    CHECK(economy::constructionRate(*r, s, kMe, q) == Resources{3700, 3700, 3700});
    home.queue.emergency = true;
    CHECK(economy::constructionRate(*r, s, kMe, q) == Resources{5550, 5550, 5550});
    home.queue.emergency = false;
    home.queue.slowTurns = 3;
    CHECK(economy::constructionRate(*r, s, kMe, q) == Resources{925, 925, 925});
    home.queue.slowTurns = 0;

    // Rioting or empty colonies have no rate.
    home.anger = 95;
    CHECK(economy::constructionRate(*r, s, kMe, q).isZero());
    home.anger = 35;
    home.population = {{kMe, 0}};
    CHECK(economy::constructionRate(*r, s, kMe, q).isZero());
    home.population = {{kMe, 1000}};

    // Ship yards have no population and no planetary bonus.
    const DesignId yardShip = addTestDesign(s, *r, kMe, "Tender", "Test Station",
                                            {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Yard Module"});
    const VehicleId v = addTestVehicle(s, *r, yardShip, locationOf(s.galaxy, home.planet)).id;
    CHECK(economy::constructionRate(*r, s, kMe, cmd::QueueTarget{{}, v}) == Resources{650, 650, 650});
    CHECK(economy::constructionRate(*r, s, kThem, cmd::QueueTarget{{}, v}).isZero());  // not theirs
    s.vehicle(v)->status = VehicleStatus::Mothballed;
    CHECK(economy::constructionRate(*r, s, kMe, cmd::QueueTarget{{}, v}).isZero());
}

namespace {

std::unique_ptr<Rules> queueRules() {
    return tweakedRules([](ruleset::Ruleset& rs) {
        addFacility(rs, "Monument", {}, {5000, 0, 0});
        addFacility(rs, "Wonder", {}, {1000000, 0, 0});
    });
}

QueueItem facilityItem(const Rules& r, std::string_view name) {
    QueueItem it;
    it.kind = QueueItem::Kind::Facility;
    it.facility = facilityIndex(r, name);
    return it;
}

} // namespace

TEST_CASE("economy: a queue pays a whole turn and finishes at most one item") {
    auto r = queueRules();
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    Colony& home = plainHome(*r, s, {"Test Space Yard", "Test Spaceport"});
    const cmd::QueueTarget q{home.planet, {}};
    for (auto name : {"Monument", "Test Mine", "Test Mine"}) REQUIRE(apply(*r, s, kMe, cmd::QueueAdd{q, facilityItem(*r, name)}).ok);
    CHECK(economy::itemCost(*r, s, kMe, q, home.queue.items[0]) == Resources{5000, 0, 0});
    CHECK(economy::turnsToComplete({5000, 0, 0}, economy::constructionRate(*r, s, kMe, q)) == 3);

    auto turn = [&] {
        s.empire(kMe).stockpile = {40000, 40000, 40000};
        return economyTurn(*r, s);
    };
    turn();
    REQUIRE(home.queue.items.size() == 3);
    CHECK(home.queue.items[0].spent == Resources{2000, 2000, 2000});  // progress grows by the whole rate
    CHECK(s.empire(kMe).economy.construction == Resources{2000, 0, 0});  // only what is still needed is paid
    turn();
    CHECK(home.queue.items[0].spent == Resources{4000, 4000, 4000});
    auto moods = turn();  // the monument finishes; the rest of the rate is lost
    CHECK(s.empire(kMe).economy.construction == Resources{1000, 0, 0});
    REQUIRE(home.queue.items.size() == 2);
    CHECK(home.queue.items[0].spent.isZero());
    CHECK(home.facilities.size() == 3);
    CHECK(countMood(moods, "Facility Constructed") == 1);
    CHECK(logged(s, kMe, "Monument completed"));
    moods = turn();  // one mine a turn
    CHECK(home.queue.items.size() == 1);
    CHECK(s.empire(kMe).economy.construction == Resources{300, 0, 0});
    turn();
    CHECK(home.queue.items.empty());
    CHECK(home.facilities.size() == 5);
}

TEST_CASE("economy: a queue that cannot pay its whole turn builds nothing") {
    auto r = queueRules();
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    Colony& home = plainHome(*r, s, {"Test Space Yard"});
    const cmd::QueueTarget q{home.planet, {}};
    REQUIRE(apply(*r, s, kMe, cmd::QueueAdd{q, facilityItem(*r, "Monument")}).ok);
    s.empire(kMe).stockpile = {1500, 0, 0};
    economyTurn(*r, s);  // 1500 + the 200 minimum < 2000
    CHECK(home.queue.items[0].spent.isZero());
    CHECK(s.empire(kMe).economy.construction.isZero());
    CHECK(s.empire(kMe).stockpile == Resources{1700, 200, 200});
    CHECK(logged(s, kMe, "Lack of Resources"));
    s.empire(kMe).stockpile = {1900, 0, 0};
    economyTurn(*r, s);
    CHECK(home.queue.items[0].spent == Resources{2000, 2000, 2000});
    CHECK(s.empire(kMe).stockpile == Resources{100, 200, 200});
}

TEST_CASE("economy: spaceports, producers and supply facilities are built first") {
    auto r = queueRules();
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    Colony& home = plainHome(*r, s, {"Test Space Yard"});
    Colony& moon = addColony(s, kMe, freePlanet(s, s.galaxy.object(home.planet).system, true), 1000);
    // The earlier queue in queue order wants a monument, the later one a mine: the mine goes first.
    Colony& first = home.planet < moon.planet ? home : moon;
    Colony& second = home.planet < moon.planet ? moon : home;
    first.queue.items = {facilityItem(*r, "Monument")};
    second.queue.items = {facilityItem(*r, "Test Mine")};
    s.empire(kMe).stockpile = {2000 - 200, 0, 0};  // one queue's worth with the 200 minimum
    economyTurn(*r, s);
    CHECK(second.queue.items.empty());
    CHECK(first.queue.items[0].spent.isZero());
}

TEST_CASE("economy: hold, riots, emergency and slow build") {
    auto r = queueRules();
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    Colony& home = plainHome(*r, s, {"Test Space Yard"});
    const cmd::QueueTarget q{home.planet, {}};
    REQUIRE(apply(*r, s, kMe, cmd::QueueAdd{q, facilityItem(*r, "Wonder")}).ok);
    auto turn = [&] {
        s.empire(kMe).stockpile = {40000, 40000, 40000};
        economyTurn(*r, s);
        return s.empire(kMe).economy.construction[Resource::Minerals];
    };
    home.queue.onHold = true;
    CHECK(turn() == 0);
    home.queue.onHold = false;
    home.anger = 95;
    CHECK(turn() == 0);  // rioting planets build nothing
    home.anger = 35;
    CHECK(turn() == 2000);

    // Emergency runs until its counter has reached the maximum: 10 + 1 turns.
    REQUIRE(apply(*r, s, kMe, cmd::QueueFlags{q, false, false, true, -1}).ok);
    CHECK(turn() == 3000);
    CHECK(home.queue.emergencyTurns == 1);
    for (int i = 0; i < 9; ++i) CHECK(turn() == 3000);
    CHECK(home.queue.emergency);
    CHECK(home.queue.emergencyTurns == 10);
    CHECK(turn() == 3000);
    CHECK_FALSE(home.queue.emergency);
    CHECK(home.queue.slowTurns == 10);
    CHECK(turn() == 500);
    CHECK(home.queue.slowTurns == 9);
    CHECK_FALSE(apply(*r, s, kMe, cmd::QueueFlags{q, false, false, true, -1}).ok);
    home.queue.items.clear();  // clearing the queue does not end slow mode
    for (int i = 0; i < 9; ++i) turn();
    CHECK(home.queue.slowTurns == 0);

    // Switching emergency off early gives as many slow turns as were used.
    REQUIRE(apply(*r, s, kMe, cmd::QueueAdd{q, facilityItem(*r, "Wonder")}).ok);
    REQUIRE(apply(*r, s, kMe, cmd::QueueFlags{q, false, false, true, -1}).ok);
    turn();
    turn();
    REQUIRE(apply(*r, s, kMe, cmd::QueueFlags{q, false, false, false, -1}).ok);
    CHECK(home.queue.slowTurns == 2);
    CHECK(turn() == 500);
    CHECK(turn() == 500);
    CHECK(turn() == 2000);

    // Switched off before a turn has passed, it costs nothing (spec 02 §6.4).
    REQUIRE(apply(*r, s, kMe, cmd::QueueFlags{q, false, false, true, -1}).ok);
    REQUIRE(apply(*r, s, kMe, cmd::QueueFlags{q, false, false, false, -1}).ok);
    CHECK(home.queue.slowTurns == 0);
    CHECK(turn() == 2000);
}

TEST_CASE("economy: repeat build keeps the top item while it can be built") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    dropVehicles(s, kMe);
    Colony& home = plainHome(r, s, {"Test Space Yard"});
    const cmd::QueueTarget q{home.planet, {}};
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, facilityItem(r, "Test Mine")}).ok);
    REQUIRE(apply(r, s, kMe, cmd::QueueFlags{q, false, true, false, -1}).ok);
    for (int i = 0; i < 3; ++i) {
        s.empire(kMe).stockpile = {40000, 40000, 40000};
        economyTurn(r, s);
    }
    CHECK(std::count(home.facilities.begin(), home.facilities.end(), facilityIndex(r, "Test Mine")) == 3);  // one per turn
    REQUIRE(home.queue.items.size() == 1);
    CHECK(home.queue.items[0].spent.isZero());

    // Once the slots are full, the repeated item leaves the queue.
    const int slots = facilitySlots(r, s, home);
    while (static_cast<int>(home.facilities.size()) < slots - 1) home.facilities.push_back(facilityIndex(r, "Test Farm"));
    s.empire(kMe).stockpile = {40000, 40000, 40000};
    economyTurn(r, s);
    CHECK(static_cast<int>(home.facilities.size()) == slots);
    CHECK(home.queue.items.empty());
}

TEST_CASE("economy: ships appear at the yard, follow the queue's waypoint and respect the cap") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {"Test Space Yard"});
    const Location homeLoc = locationOf(s.galaxy, home.planet);
    const cmd::QueueTarget q{home.planet, {}};
    const DesignId ship = frigate(s, r, kMe, "Warbird");
    const Location rally{homeLoc.system, Sector{2, 2}};
    REQUIRE(apply(r, s, kMe, cmd::SetWaypoint{0, Waypoint{"Rally", rally, true}}).ok);
    QueueItem item;
    item.design = ship;
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, item}).ok);
    REQUIRE(apply(r, s, kMe, cmd::QueueFlags{q, false, false, false, 0}).ok);
    CHECK(economy::itemCost(r, s, kMe, q, home.queue.items[0]) == Resources{160, 20, 20});

    const size_t before = s.vehicles.size();
    const int experience = s.empire(kMe).experience;
    const auto moods = economyTurn(r, s);
    REQUIRE(s.vehicles.size() == before + 1);
    CHECK(s.empire(kMe).experience == experience + r.hull(s.design(ship).hull).tonnage / 10);  // spec 02 §9
    const Vehicle& built = s.vehicles.back();
    CHECK(built.design == ship);
    CHECK(built.location == homeLoc);
    REQUIRE(built.orders.size() == 1);
    CHECK(built.orders[0].location == rally);
    CHECK(countMood(moods, "Ship Constructed") == 1);
    CHECK(countMood(moods, "Any Ship Constructed") == 1);
    CHECK(home.queue.items.empty());

    // At the ship limit nothing is built, and the item must be paid for again.
    s.options.maxShipsPerPlayer = shipCount(r, s, kMe);
    home.queue.items.push_back(item);
    s.empire(kMe).stockpile = {10000, 10000, 10000};
    economyTurn(r, s);
    CHECK(s.vehicles.size() == before + 1);
    REQUIRE(home.queue.items.size() == 1);
    CHECK(home.queue.items[0].spent.isZero());
    CHECK(s.empire(kMe).economy.construction == Resources{160, 20, 20});
    CHECK(logged(s, kMe, "limit on ships"));
}

TEST_CASE("economy: units go into cargo, one at a time, in the same sector only") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    dropVehicles(s, kMe);
    Colony& home = plainHome(r, s, {});
    const Location homeLoc = locationOf(s.galaxy, home.planet);
    const cmd::QueueTarget q{home.planet, {}};
    const DesignId fighter = addTestDesign(s, r, kMe, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    QueueItem item;
    item.design = fighter;
    item.count = 3;
    CHECK(economy::itemCost(r, s, kMe, q, item) == Resources{120, 0, 15});
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, item}).ok);
    economyTurn(r, s);
    CHECK(home.cargo.unitCount(fighter) == 3);
    CHECK(s.design(fighter).built == 3);

    // A full planet: the units go to a ship's hold in the same sector.
    home.cargo.population = {{kMe, 1'000'000}};
    const DesignId hauler = addTestDesign(s, r, kMe, "Hauler", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Cargo Bay"});
    const VehicleId h = addTestVehicle(s, r, hauler, homeLoc).id;
    const VehicleId elsewhere = addTestVehicle(s, r, hauler, Location{homeLoc.system, Sector{homeLoc.sector.x == 0 ? 1 : 0, 0}}).id;
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, item}).ok);
    s.empire(kMe).log.clear();
    economyTurn(r, s);
    CHECK(s.vehicle(h)->cargo.unitCount(fighter) == 2);  // 50 kT holds two 20 kT fighters
    CHECK(s.vehicle(elsewhere)->cargo.unitCount(fighter) == 0);
    CHECK(s.design(fighter).built == 5);  // the third found no room and was not built
    // The last unit found no room: the item stays at the top with its full
    // count and no progress; one message per unit lost (spec 02 §6.5, Q39).
    REQUIRE(home.queue.items.size() == 1);
    CHECK(home.queue.items[0].count == 3);
    CHECK(home.queue.items[0].spent.isZero());
    const auto noRoom = [&] {
        return std::count_if(s.empire(kMe).log.begin(), s.empire(kMe).log.end(),
                             [](const LogEntry& l) { return l.title.starts_with("No Storage Available"); });
    };
    CHECK(noRoom() == 1);
    s.empire(kMe).log.clear();
    economyTurn(r, s);  // paid again; now nothing fits: three messages
    CHECK(s.design(fighter).built == 5);
    CHECK(noRoom() == 3);
    REQUIRE(home.queue.items.size() == 1);

    // Unit caps are checked at launch, not here.
    s.options.maxUnitsPerPlayer = 0;
    home.cargo.population.clear();
    REQUIRE(economy::itemCost(r, s, kMe, q, item) == Resources{120, 0, 15});
    economyTurn(r, s);
    CHECK(s.design(fighter).built == 8);
    CHECK(home.queue.items.empty());
}

TEST_CASE("economy: built units go to the holders in the game's object order") {
    // Spec 02 §6.5: the builder, then the other planets and ships of the
    // empire in the sector in object order. Planets come before every vehicle
    // (spec 03 §19 Q62), and a vehicle that took a freed slot comes before
    // later ones, wherever it is in the vehicle list.
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    dropVehicles(s, kMe);
    Colony& home = plainHome(r, s, {});
    home.cargo.population = {{kMe, 1'000'000}};  // the builder is full
    const Location homeLoc = locationOf(s.galaxy, home.planet);
    const cmd::QueueTarget q{home.planet, {}};
    const DesignId fighter = addTestDesign(s, r, kMe, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    const DesignId hauler = addTestDesign(s, r, kMe, "Hauler", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Cargo Bay"});
    const VehicleId gone = addTestVehicle(s, r, hauler, homeLoc).id;
    const VehicleId second = addTestVehicle(s, r, hauler, homeLoc).id;
    s.vehicle(gone)->count = 0;
    s.removeDeadVehicles();
    const VehicleId first = addTestVehicle(s, r, hauler, homeLoc).id;  // takes the freed slot
    REQUIRE(s.vehicle(first)->slot < s.vehicle(second)->slot);
    REQUIRE(s.vehicles.back().id == first);
    QueueItem item;
    item.design = fighter;
    item.count = 2;
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, item}).ok);
    economyTurn(r, s);
    CHECK(s.vehicle(first)->cargo.unitCount(fighter) == 2);
    CHECK(s.vehicle(second)->cargo.unitCount(fighter) == 0);
}

TEST_CASE("economy: facilities need a free slot; with one the whole count is built") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    dropVehicles(s, kMe);
    Colony& home = plainHome(r, s, {});
    const int slots = facilitySlots(r, s, home);
    home.facilities.assign(static_cast<size_t>(slots - 1), facilityIndex(r, "Test Farm"));
    home.queue.items = {facilityItem(r, "Test Mine"), facilityItem(r, "Test Mine")};  // the second overfills
    home.queue.items[0].count = 3;
    s.empire(kMe).stockpile = {40000, 40000, 40000};
    s.empire(kMe).experience = 0;
    auto moods = economyTurn(r, s);
    // One slot was free: all three are added, past the slots (spec 02 §6.5, Q38).
    CHECK(static_cast<int>(home.facilities.size()) == slots + 2);
    CHECK(countMood(moods, "Facility Constructed") == 3);
    CHECK(s.empire(kMe).experience == 3);  // a finished facility item adds its count
    REQUIRE(home.queue.items.size() == 1);
    CHECK(home.queue.items[0].spent.isZero());
    s.empire(kMe).log.clear();
    economyTurn(r, s);  // paid for, but no slot: the progress is lost and nothing is said
    CHECK(static_cast<int>(home.facilities.size()) == slots + 2);
    REQUIRE(home.queue.items.size() == 1);
    CHECK(home.queue.items[0].spent.isZero());
    CHECK(s.empire(kMe).economy.construction == Resources{300, 0, 0});
    CHECK_FALSE(logged(s, kMe, "Test Mine"));
    CHECK(s.empire(kMe).experience == 3);

    // A second space yard is not checked for again at completion.
    home.facilities = {facilityIndex(r, "Test Space Yard")};
    home.queue.items = {facilityItem(r, "Test Space Yard")};
    economyTurn(r, s);
    CHECK(std::count(home.facilities.begin(), home.facilities.end(), facilityIndex(r, "Test Space Yard")) == 2);
}

namespace {

// Test rules with a third mine level, researched from the start.
std::unique_ptr<Rules> upgradeRules(int upgradePercent = 50) {
    return tweakedRules([&](ruleset::Ruleset& rs) {
        ruleset::Facility f = rs.facilities[facilityIndex(engineRules(), "Test Mine II")];
        f.name = "Test Mine III";
        f.romanNumeral = 3;
        f.cost = {600, 20, 0};
        f.requirements.clear();
        rs.facilities.push_back(std::move(f));
        setKey(rs, "Upgrade Facility Cost Percent", upgradePercent);
    });
}

QueueItem upgradeTo(const Rules& r, std::string_view target) {
    QueueItem it;
    it.kind = QueueItem::Kind::Upgrade;
    it.facility = facilityIndex(r, target);
    return it;
}

} // namespace

TEST_CASE("economy: an upgrade stores its target and count when queued") {
    auto r = upgradeRules();
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    const uint32_t mine = facilityIndex(*r, "Test Mine"), mine2 = facilityIndex(*r, "Test Mine II"), mine3 = facilityIndex(*r, "Test Mine III");
    const uint32_t farm = facilityIndex(*r, "Test Farm");
    Colony& home = plainHome(*r, s, {"Test Mine", "Test Mine II", "Test Farm", "Test Mine"});
    s.empire(kMe).techLevels[techArea(*r, "Test Economics").index()] = 3;
    const cmd::QueueTarget q{home.planet, {}};
    CHECK(economy::upgradeCount(*r, home, mine3) == 3);
    CHECK(economy::upgradeCount(*r, home, mine2) == 2);

    // The count is every lower level of the family, whatever the client sends.
    QueueItem up = upgradeTo(*r, "Test Mine III");
    up.count = 1;
    REQUIRE(apply(*r, s, kMe, cmd::QueueAdd{q, up}).ok);
    REQUIRE(home.queue.items.size() == 1);
    CHECK(home.queue.items[0].facility == mine3);
    CHECK(home.queue.items[0].count == 3);
    // trunc(target cost × 50 %) × the stored count (spec 02 §6.6).
    CHECK(economy::itemCost(*r, s, kMe, q, home.queue.items[0]) == Resources{300 * 3, 10 * 3, 0});
    // The count cannot be changed, and a second upgrade to the same target is refused.
    CHECK_FALSE(apply(*r, s, kMe, cmd::QueueSetCount{q, 0, 1}).ok);
    CHECK_FALSE(apply(*r, s, kMe, cmd::QueueAdd{q, upgradeTo(*r, "Test Mine III")}).ok);
    REQUIRE(apply(*r, s, kMe, cmd::QueueAdd{q, upgradeTo(*r, "Test Mine II")}).ok);  // another target is fine
    CHECK(home.queue.items[1].count == 2);
    home.queue.items.pop_back();
    // Nothing to upgrade: refused.
    CHECK_FALSE(apply(*r, s, kMe, cmd::QueueAdd{q, upgradeTo(*r, "Test Farm")}).ok);

    // A mine built since does not change the price or the count: the stored
    // count converts facility types in their stored order (the order each type
    // first appears), not lowest level first.
    home.facilities.push_back(mine);
    CHECK(economy::itemCost(*r, s, kMe, q, home.queue.items[0]) == Resources{900, 30, 0});
    s.empire(kMe).stockpile = {40000, 40000, 40000};
    economyTurn(*r, s);
    CHECK(home.facilities == std::vector<uint32_t>{mine3, mine2, farm, mine3, mine3});
    CHECK(home.queue.items.empty());
    CHECK(logged(s, kMe, "3 facilities are now Test Mine III"));

    // Upgrades never repeat.
    REQUIRE(apply(*r, s, kMe, cmd::QueueAdd{q, upgradeTo(*r, "Test Mine III")}).ok);
    REQUIRE(apply(*r, s, kMe, cmd::QueueFlags{q, false, true, false, -1}).ok);
    economyTurn(*r, s);
    CHECK(home.facilities == std::vector<uint32_t>{mine3, mine3, farm, mine3, mine3});
    CHECK(home.queue.items.empty());
}

TEST_CASE("economy: an upgrade with fewer facilities left converts those, at the full price") {
    auto r = upgradeRules();
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    const uint32_t mine = facilityIndex(*r, "Test Mine"), mine3 = facilityIndex(*r, "Test Mine III");
    Colony& home = plainHome(*r, s, {"Test Mine", "Test Mine", "Test Mine"});
    const cmd::QueueTarget q{home.planet, {}};
    REQUIRE(apply(*r, s, kMe, cmd::QueueAdd{q, upgradeTo(*r, "Test Mine III")}).ok);
    REQUIRE(home.queue.items[0].count == 3);
    home.facilities.pop_back();  // one mine is lost before the upgrade finishes
    s.empire(kMe).stockpile = {40000, 40000, 40000};
    economyTurn(*r, s);
    CHECK(home.facilities == std::vector<uint32_t>{mine3, mine3});
    CHECK(s.empire(kMe).economy.construction == Resources{900, 30, 0});

    // At the start of a queue's turn an upgrade with nothing left is removed,
    // even from under the top item (spec 02 §6.1).
    home.facilities = {mine, mine};
    home.queue.items = {facilityItem(*r, "Test Farm")};
    home.queue.items[0].count = 1;
    home.queue.items.push_back(economy::upgradeItem(*r, home, mine3));
    REQUIRE(home.queue.items.back().count == 2);
    home.facilities = {mine3, mine3};
    home.queue.onHold = true;  // also on hold (inferred, spec 02 §13 Q53)
    economyTurn(*r, s);
    REQUIRE(home.queue.items.size() == 1);
    CHECK(home.queue.items[0].kind == QueueItem::Kind::Facility);
}

TEST_CASE("economy: upgrade prices truncate per facility") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) { setKey(rs, "Upgrade Facility Cost Percent", 33); });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {"Test Mine", "Test Mine", "Test Mine"});
    const QueueItem up = economy::upgradeItem(*r, home, facilityIndex(*r, "Test Mine II"));
    CHECK(up.count == 3);
    CHECK(economy::itemCost(*r, s, kMe, cmd::QueueTarget{home.planet, {}}, up) == Resources{pctTrunc(400, 33) * 3, 0, 0});
}

TEST_CASE("economy: ships leave a queue that has lost its yard at the start of its turn") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    dropVehicles(s, kMe);
    Colony& home = plainHome(r, s, {"Test Space Yard"});
    const cmd::QueueTarget q{home.planet, {}};
    const DesignId ship = frigate(s, r, kMe);
    const DesignId fighter = addTestDesign(s, r, kMe, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    QueueItem warship;
    warship.design = ship;
    QueueItem wasps;
    wasps.design = fighter;
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, warship}).ok);
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, wasps}).ok);
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, warship}).ok);
    home.facilities.clear();  // the yard is gone (lost in battle, say)
    CHECK(economy::itemObsolete(r, s, q, home.queue.items[0]));
    CHECK_FALSE(economy::itemObsolete(r, s, q, home.queue.items[1]));
    const size_t before = s.vehicles.size();
    s.empire(kMe).stockpile = {40000, 40000, 40000};
    economyTurn(r, s);
    // Both ships are gone before anything is paid; the units are built instead.
    CHECK(s.vehicles.size() == before);
    CHECK(home.queue.items.empty());
    CHECK(home.cargo.unitCount(fighter) == 1);
    CHECK_FALSE(logged(s, kMe, "cannot build"));
}

TEST_CASE("economy: space yard ships build where they are, but not while cloaked") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {});
    const Location spot{s.galaxy.object(home.planet).system, Sector{1, 1}};
    const DesignId tender = addTestDesign(s, r, kMe, "Tender", "Test Station",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Yard Module"});
    const VehicleId yard = addTestVehicle(s, r, tender, spot).id;
    const DesignId ship = frigate(s, r, kMe);
    QueueItem item;
    item.design = ship;
    const cmd::QueueTarget q{{}, yard};
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, item}).ok);
    s.vehicle(yard)->status = VehicleStatus::Cloaked;
    economyTurn(r, s);
    CHECK(s.vehicle(yard)->queue.items.size() == 1);
    s.vehicle(yard)->status = VehicleStatus::Normal;
    const size_t before = s.vehicles.size();
    economyTurn(r, s);
    REQUIRE(s.vehicles.size() == before + 1);
    CHECK(s.vehicles.back().location == spot);
    CHECK(s.vehicle(yard)->queue.items.empty());
}

TEST_CASE("economy: empire experience is capped and gives the race age") {
    // Spec 02 §9: each label covers experience up to its limit.
    CHECK(economy::raceAge(0) == "Newborn");
    CHECK(economy::raceAge(5'000) == "Newborn");
    CHECK(economy::raceAge(5'001) == "Infantile");
    CHECK(economy::raceAge(10'001) == "Young");
    CHECK(economy::raceAge(50'001) == "Moderate");
    CHECK(economy::raceAge(200'001) == "Old");
    CHECK(economy::raceAge(1'000'001) == "Ancient");
    CHECK(economy::raceAge(10'000'001) == "God-like");
    CHECK(economy::raceAge(100'000'001) == "Stellar Ancients");
    CHECK(economy::raceAge(400'000'000) == "Stellar Ancients");
    CHECK(economy::raceAge(400'000'001) == "First Ones");
    Empire e;
    economy::gainExperience(e, 499'999'990);
    economy::gainExperience(e, 100);
    CHECK(e.experience == economy::kMaxEmpireExperience);
    e.experience = 10;
    economy::gainExperience(e, -5);
    CHECK(e.experience == 10);
}

// ---- Population -------------------------------------------------------------------------------------

TEST_CASE("economy: growth rate from race, mood, conditions and resistance") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        addTrait(rs, "Fertile", "Reproduction", 3);
        addFacility(rs, "Nursery", {ability(AbilityKind::ModifyReproductionSystem, 4)});
    });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {});
    SpaceObject& planet = s.galaxy.object(home.planet);
    CHECK(economy::reproductionPercent(*r, s, home) == 10);
    home.anger = 20;  // Happy: +2
    CHECK(economy::reproductionPercent(*r, s, home) == 12);
    CHECK(economy::colonyOutput(*r, s, home).reproductionPercent == 12);
    home.anger = 70;  // Angry: -5
    CHECK(economy::reproductionPercent(*r, s, home) == 5);
    home.anger = 20;
    planet.conditions = Conditions::hundredths(140);  // Good: +2
    CHECK(economy::reproductionPercent(*r, s, home) == 14);
    planet.conditions = Conditions::hundredths(60);  // Unpleasant: -2 (the observed Quick Start homeworld shows 10 %)
    CHECK(economy::reproductionPercent(*r, s, home) == 10);
    planet.conditions = Conditions::hundredths(10);  // Deadly: -20, and the rate never goes below 0
    CHECK(economy::reproductionPercent(*r, s, home) == 0);
    setChar(s, kMe, Characteristic::EnvironmentalResistance, 154);  // + trunc(54 / 5)
    CHECK(economy::reproductionPercent(*r, s, home) == 2);
    setChar(s, kMe, Characteristic::Reproduction, 120);
    giveTrait(*r, s, kMe, "Fertile");
    CHECK(economy::reproductionPercent(*r, s, home) == 25);
    home.facilities = {facilityIndex(*r, "Nursery")};
    CHECK(economy::reproductionPercent(*r, s, home) == 29);

    // No growth at all while rioting or plagued.
    home.anger = 95;
    CHECK(economy::reproductionPercent(*r, s, home) == 0);
    home.anger = 20;
    home.plagueLevel = 1;
    CHECK(economy::reproductionPercent(*r, s, home) == 0);
}

TEST_CASE("economy: Planet Storage Space raises population, facility slots and cargo") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) { addTrait(rs, "Roomy", "Planet Storage Space", 25); });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {});
    const int64_t pop = maxPopulation(*r, s, home);
    const int slots = facilitySlots(*r, s, home);
    const int64_t cargo = colonyCargoCapacity(*r, s, home);
    giveTrait(*r, s, kMe, "Roomy");
    CHECK(maxPopulation(*r, s, home) == pctTrunc(pop, 125));
    CHECK(facilitySlots(*r, s, home) == pctTrunc(slots, 125));
    CHECK(colonyCargoCapacity(*r, s, home) == pctTrunc(cargo, 125));
}

TEST_CASE("economy: growth is rounded per race, earlier races first, never negative") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {});
    populationTurn(r, s);
    CHECK(home.totalPopulation() == 1010);  // 10 % a year = 1 % a turn

    home.population = {{kMe, 1050}};
    populationTurn(r, s);
    CHECK(home.totalPopulation() == 1060);  // 10.5 rounds to even
    home.population = {{kMe, 1150}};
    populationTurn(r, s);
    CHECK(home.totalPopulation() == 1162);  // 11.5 rounds to even

    home.population = {{kMe, 5}};
    populationTurn(r, s);
    CHECK(home.totalPopulation() == 6);  // at least 1M while the rate is positive

    const int64_t cap = maxPopulation(r, s, home);
    home.population = {{kMe, cap - 3}};
    populationTurn(r, s);
    CHECK(home.totalPopulation() == cap);

    // The first race fills the free room first.
    home.population = {{kMe, 1500}, {kThem, cap - 1510}};
    populationTurn(r, s);
    CHECK(home.population[0].millions == 1510);
    CHECK(home.population[1].millions == cap - 1510);

    // A rate that would be negative is 0: nobody shrinks.
    setChar(s, kMe, Characteristic::Reproduction, 80);
    home.population = {{kMe, 1000}};
    populationTurn(r, s);
    CHECK(home.totalPopulation() == 1000);
}

TEST_CASE("economy: growth runs every Reproduction Check Frequency turns, unscaled") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) { setKey(rs, "Reproduction Check Frequency", 2); });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {});
    s.turn = 4;  // processed as turn 5
    populationTurn(*r, s);
    CHECK(home.totalPopulation() == 1000);
    s.turn = 5;  // turn 6
    populationTurn(*r, s);
    CHECK(home.totalPopulation() == 1010);
}

TEST_CASE("economy: growth and every 10th turn test the date in tenths of a year") {
    // 24000 is not a multiple of 7: the date decides, not the turn number (spec 02 §13 Q48).
    auto r = tweakedRules([](ruleset::Ruleset& rs) { setKey(rs, "Reproduction Check Frequency", 7); });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {});
    CHECK(economy::processingDate(s) == 24001);  // a simultaneous turn is processed at the advanced date
    s.turn = 6;  // processed as turn 7, date 24007: not a multiple of 7
    populationTurn(*r, s);
    CHECK(home.totalPopulation() == 1000);
    s.turn = 2;  // date 24003 = 7 × 3429
    populationTurn(*r, s);
    CHECK(home.totalPopulation() == 1010);
    // A turn-based game processes its first round at 24000 (spec 05 §8).
    s.options.simultaneous = false;
    s.turn = 0;
    CHECK(economy::processingDate(s) == 24000);
}

TEST_CASE("economy: replicants add population in proportion to the races present") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        addFacility(rs, "Cloning Vat", {ability(AbilityKind::ChangePopulationSystem, 10)});
        addFacility(rs, "Big Vat", {ability(AbilityKind::ChangePopulationSystem, 25)});
    });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {"Cloning Vat"});
    home.population = {{kMe, 300}, {kThem, 100}};
    setChar(s, kMe, Characteristic::Reproduction, 90);  // no natural growth
    populationTurn(*r, s);
    CHECK(home.population[0].millions == 308);  // 7.5 rounds to 8
    CHECK(home.population[1].millions == 102);  // 2.5 rounds to 2

    // The share is round(P × q) with q = pop ÷ total stored as a double first
    // (spec 02 §3): the doubles nearest 0.1 and 0.9 lie above them, so 2.5 and
    // 22.5 come out a hair above the half and round up.
    home.facilities = {facilityIndex(*r, "Big Vat")};
    home.population = {{kMe, 1}, {kThem, 9}};
    populationTurn(*r, s);
    CHECK(home.population[0].millions == 1 + 3);
    CHECK(home.population[1].millions == 9 + 23);

    // Each share is capped by the room left: earlier races first, nothing when full.
    const int64_t cap = maxPopulation(*r, s, home);
    home.population = {{kMe, cap - 20}, {kThem, 10}};
    populationTurn(*r, s);
    CHECK(home.population[0].millions == cap - 10);
    CHECK(home.population[1].millions == 10);
    home.population = {{kMe, cap + 50}};
    populationTurn(*r, s);
    CHECK(home.totalPopulation() == cap + 50);  // above the maximum: nothing added, nothing removed
}

TEST_CASE("economy: plague kills a fixed amount by level until prevented or cured") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) { addTrait(rs, "Immune", "No Plagues", 0); });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {});
    home.plagueLevel = 2;
    populationTurn(*r, s);
    CHECK(home.totalPopulation() >= 1000 - 60);  // no growth, then 50 to 60M dead
    CHECK(home.totalPopulation() <= 1000 - 50);
    CHECK(home.plagueLevel == 2);
    home.facilities = {facilityIndex(*r, "Test Medical Lab")};  // prevents level 2, later the same turn
    const int64_t before = home.totalPopulation();
    populationTurn(*r, s);
    CHECK(home.plagueLevel == 0);
    CHECK(home.totalPopulation() < before);
    CHECK(logged(s, kMe, "cured"));

    // The loss comes from the races in their stored order.
    home.facilities.clear();
    home.population = {{kMe, 30}, {kThem, 100}};
    home.plagueLevel = 1;
    populationTurn(*r, s);
    CHECK(home.population[0].millions >= 18);
    CHECK(home.population[0].millions <= 20);
    CHECK(home.population[1].millions == 100);

    // No Plagues: cured the next time it would strike, with no loss.
    giveTrait(*r, s, kMe, "Immune");
    populationTurn(*r, s);
    CHECK(home.plagueLevel == 0);
    CHECK(home.population[1].millions == 100);
}

TEST_CASE("economy: cargo above the capacity stays until the planet loses population to plague") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {});
    const DesignId fighter = addTestDesign(s, r, kMe, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    const DesignId troop = addTestDesign(s, r, kMe, "Marines", "Test Troop Hull", {"Test Troop Rifle"});
    const int64_t capacity = colonyCargoCapacity(r, s, home);
    const int64_t size = r.hull(s.design(fighter).hull).tonnage;
    const int64_t mass = r.setting("Population Mass", 5);
    // Two fighters too many, plus 2M of people.
    home.cargo.units = {{fighter, static_cast<int>(capacity / size + 2)}, {troop, 1}};
    home.cargo.population = {{kThem, 1}, {kMe, 1}};
    populationTurn(r, s);  // no damage, no plague: nothing is removed
    const int64_t over = cargoSpaceUsed(r, s, home.cargo);
    CHECK(over > capacity);

    // The plague strikes: people first, 1M at a time from the first group,
    // then units one at a time from the first stack (spec 02 §2, §13 Q49).
    home.plagueLevel = 1;
    populationTurn(r, s);
    CHECK(home.cargo.population.empty());
    CHECK(cargoSpaceUsed(r, s, home.cargo) <= capacity);
    CHECK(home.cargo.unitCount(troop) == 1);
    CHECK(home.cargo.unitCount(fighter) * size + r.hull(s.design(troop).hull).tonnage <= capacity);
    CHECK(cargoSpaceUsed(r, s, home.cargo) + size > capacity);  // no more than needed
    CHECK(over - 2 * mass > capacity);
}

TEST_CASE("economy: a colony whose people all die is removed and its planet loses value") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const ObjectId planet = freePlanet(s, s.galaxy.object(homeworld(s, kMe).planet).system, false);
    Colony& c = addColony(s, kMe, planet, 400);
    c.colonyType = "Research";
    c.plagueLevel = 6;  // 500M or more
    s.galaxy.object(planet).value = {100, 5, 50};
    TurnContext ctx{r, s, {}, {}, {}};
    economy::processPlanets(ctx, kMe);
    const auto& moods = ctx.moodEvents;
    CHECK(s.colony(planet) == nullptr);
    CHECK(s.galaxy.object(planet).value == std::array<int, 3>{90, 0, 40});
    CHECK(countMood(moods, "Any Planet Lost") == 1);
    CHECK(countMood(moods, "Homeworld Lost") == 0);
    CHECK(logged(s, kMe, "wiped out"));

    // A colony of the first default colony type counts as a homeworld.
    Colony& d = addColony(s, kMe, planet, 400);
    d.colonyType = r.data().names.colonyTypes.front();
    d.plagueLevel = 6;
    ctx.moodEvents.clear();
    economy::processPlanets(ctx, kMe);
    CHECK(countMood(ctx.moodEvents, "Homeworld Lost") == 1);
    CHECK(countMood(ctx.moodEvents, "Any Planet Lost") == 0);
}

TEST_CASE("economy: atmosphere converters take Val1 + 1 turns; domes") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        addFacility(rs, "Air Plant", {ability(AbilityKind::PlanetChangeAtmosphere, 3)});
        addFacility(rs, "Slow Air Plant", {ability(AbilityKind::PlanetChangeAtmosphere, 250)});
    });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {"Air Plant"});
    SpaceObject& planet = s.galaxy.object(home.planet);
    planet.atmosphere = "Methane";
    CHECK_FALSE(breathable(s, home));
    const int64_t domed = maxPopulation(*r, s, home);
    REQUIRE(domed < 5000);
    home.population = {{kMe, 5000}};
    populationTurn(*r, s);
    // Nothing removes population above the maximum; it only has no room to grow (spec 02 §2, Q33).
    CHECK(home.totalPopulation() == 5000);
    CHECK(home.atmosphereTurns == 1);
    populationTurn(*r, s);
    CHECK(home.totalPopulation() == 5000);
    // Turns without the converter keep the count, which resumes later (spec 02 §2, Q41).
    home.facilities.clear();
    populationTurn(*r, s);
    populationTurn(*r, s);
    CHECK(home.atmosphereTurns == 2);
    home.facilities = {facilityIndex(*r, "Air Plant")};
    populationTurn(*r, s);
    CHECK(planet.atmosphere == "Methane");
    CHECK(home.atmosphereTurns == 3);
    populationTurn(*r, s);
    CHECK(planet.atmosphere == s.empire(kMe).race.atmosphere);
    CHECK(home.atmosphereTurns == 0);
    CHECK(breathable(s, home));
    // The right atmosphere leaves the counter as it is too.
    home.atmosphereTurns = 2;
    populationTurn(*r, s);
    CHECK(home.atmosphereTurns == 2);

    // The counter stops at 200, so a converter needing more never finishes.
    planet.atmosphere = "Methane";
    home.facilities = {facilityIndex(*r, "Slow Air Plant")};
    home.atmosphereTurns = 199;
    populationTurn(*r, s);
    populationTurn(*r, s);
    CHECK(home.atmosphereTurns == 200);
    CHECK(planet.atmosphere == "Methane");
}

// ---- Happiness ---------------------------------------------------------------------------------------

namespace {

// A mood model in tenths of a percent, with room for large changes.
std::unique_ptr<Rules> moodRules() {
    return tweakedRules([](ruleset::Ruleset& rs) {
        auto& m = rs.happinessModels[0];
        m.maxPositiveChange = 200;   // +20 % a turn at most
        m.maxNegativeChange = -150;  // -15 %
        m.triggers = {{"Any Ship Lost", 20},
                      {"Any Planet Colonized", 15},
                      {"Battle in System - Loss", 100},
                      {"Battle in Sector - Loss", 30},
                      {"Facility Constructed", -50},
                      {"Natural Decrease", -20},
                      {"Natural Decrease for Other Races", 30},
                      {"Our Ship in Sector", -10},
                      {"Our Ship in System", -5},
                      {"Enemy Ship in Sector", 40},
                      {"Enemy Ship in System", 20},
                      {"Our Troops on Planet", -10},
                      {"Enemy Troops on Planet", 70},
                      {"Planet Plagued", 50},
                      {"New Treaty War", 500}};
        addTrait(rs, "Stoic", "Population Emotionless", 0);
        addFacility(rs, "Theatre", {ability(AbilityKind::ChangePopulationHappinessSystem, 3)});
        addFacility(rs, "Prison", {ability(AbilityKind::PlanetChangePopulationHappiness, 4)});
    });
}

struct MoodWorld {
    std::unique_ptr<Rules> rules = moodRules();
    GameState s = newGame(*rules);
    Colony* home = nullptr;
    Colony* moon = nullptr;
    Colony* far = nullptr;
    SystemId sys;

    MoodWorld() {
        s.vehicles.clear();
        home = &plainHome(*rules, s, {});
        sys = s.galaxy.object(home->planet).system;
        moon = &addColony(s, kMe, freePlanet(s, sys, true), 1000);
        far = &addColony(s, kMe, freePlanet(s, sys, false), 1000);
        home->anger = moon->anger = far->anger = 40;  // Indifferent: no drift
    }
};

} // namespace

TEST_CASE("economy: happiness events by scope, truncated to whole percent") {
    MoodWorld w;
    const Rules& r = *w.rules;
    std::vector<MoodEvent> events;
    events.push_back({kMe, "Any Ship Lost", {}, {}, 3});                     // +6.0 everywhere
    events.push_back({kMe, "Battle in System - Loss", w.sys, {}, 1});        // +10.0 in the system
    events.push_back({kMe, "Battle in Sector - Loss", w.sys, w.home->planet, 1});  // +3.0 at home only
    events.push_back({kMe, "Facility Constructed", w.sys, w.home->planet, 1});     // -5.0 at home only
    events.push_back({kMe, "Any Planet Colonized", {}, {}, 1});              // +1.5
    events.push_back({kThem, "Battle in System - Loss", w.sys, {}, 1});      // someone else's
    populationTurn(r, w.s, events);
    CHECK(w.home->anger == 40 + (60 + 100 + 30 - 50 + 15) / 10);
    CHECK(w.moon->anger == 40 + (60 + 100 + 15) / 10);
    CHECK(w.far->anger == 40 + (60 + 15) / 10);  // 7.5 → 7

    // Conditions do not change anger.
    w.far->anger = 40;
    w.s.galaxy.object(w.far->planet).conditions = Conditions{};
    const int farBefore = w.far->anger;
    populationTurn(r, w.s);
    CHECK(w.far->anger == farBefore);
}

TEST_CASE("economy: happiness drifts towards Indifferent; other races resent their rulers") {
    MoodWorld w;
    const Rules& r = *w.rules;
    w.home->anger = 20;  // Happy drifts up
    w.moon->anger = 70;  // Angry drifts down
    w.far->anger = 35;   // Indifferent stays
    populationTurn(r, w.s);
    CHECK(w.home->anger == 22);
    CHECK(w.moon->anger == 68);
    CHECK(w.far->anger == 35);

    w.far->population = {{kMe, 400}, {kThem, 600}};  // the owner's race is under half
    populationTurn(r, w.s);
    CHECK(w.far->anger == 38);
    w.far->population = {{kMe, 500}, {kThem, 500}};  // exactly half: the usual drift
    populationTurn(r, w.s);
    CHECK(w.far->anger == 38);
}

TEST_CASE("economy: happiness clamps, capitals, the Happiness characteristic and planet facilities") {
    MoodWorld w;
    const Rules& r = *w.rules;
    populationTurn(r, w.s, {{kMe, "New Treaty War", {}, {}, 1}});  // +50, limited to +20
    CHECK(w.home->anger == 60);
    CHECK(w.moon->anger == 60);

    // A capital stops at 80, anything else at 100.
    REQUIRE(w.home->homeworld);
    for (int i = 0; i < 3; ++i) populationTurn(r, w.s, {{kMe, "New Treaty War", {}, {}, 1}});
    CHECK(w.home->anger == 80);
    CHECK(w.moon->anger == 100);
    CHECK(logged(w.s, kMe, "Riots on"));

    // A happier race calms by (Happiness - 100) / 5 tenths a turn; changes under 1 % are lost.
    w.far->anger = 40;
    setChar(w.s, kMe, Characteristic::Happiness, 149);  // -9 tenths: nothing
    populationTurn(r, w.s);
    CHECK(w.far->anger == 40);
    setChar(w.s, kMe, Characteristic::Happiness, 150);  // -10 tenths: -1
    populationTurn(r, w.s);
    CHECK(w.far->anger == 39);
    setChar(w.s, kMe, Characteristic::Happiness, 100);

    // A planet happiness facility adds whole percent: positive angers.
    w.far->facilities = {facilityIndex(r, "Prison")};
    populationTurn(r, w.s);
    CHECK(w.far->anger == 43);
    // A large negative change is limited to Max Negative Anger Change / 10.
    populationTurn(r, w.s, {{kMe, "Facility Constructed", w.s.galaxy.object(w.far->planet).system, w.far->planet, 5}});
    CHECK(w.far->anger == 43 - 15);
}

TEST_CASE("economy: ships present count per ship; allies count as ours") {
    MoodWorld w;
    const Rules& r = *w.rules;
    GameState& s = w.s;
    const Location homeLoc = locationOf(s.galaxy, w.home->planet);
    const DesignId ours = frigate(s, r, kMe);
    const DesignId theirs = frigate(s, r, kThem, "Raider");
    addTestVehicle(s, r, ours, homeLoc);
    addTestVehicle(s, r, ours, homeLoc);
    const VehicleId raider = addTestVehicle(s, r, theirs, locationOf(s.galaxy, w.far->planet)).id;
    addTestVehicle(s, r, theirs, locationOf(s.galaxy, w.far->planet));
    populationTurn(r, s);
    CHECK(w.home->anger == 40 - 2);  // two of ours in the sector
    CHECK(w.moon->anger == 40 - 1);  // two in the system
    CHECK(w.far->anger == 40 + 8);   // two enemies in the sector

    // Mothballed and cloaked ships count for nothing; allies count as ours.
    s.vehicle(raider)->status = VehicleStatus::Cloaked;
    populationTurn(r, s);
    CHECK(w.far->anger == 48 + (40 - 20) / 10);  // one enemy; Unhappy drifts down
    s.empire(kMe).relation(kThem).treaty = Treaty::NonAggression;
    s.empire(kThem).relation(kMe).treaty = Treaty::NonAggression;
    populationTurn(r, s);
    CHECK(w.far->anger == 50 + (-10 - 20) / 10);  // the ally's ship counts as ours

    // Ships without an owner never count.
    for (Vehicle& v : s.vehicles)
        if (v.owner == kThem) v.owner = {};
    w.far->anger = 40;
    populationTurn(r, s);
    CHECK(w.far->anger == 40);

    // Units never count.
    const DesignId fighter = addTestDesign(s, r, kMe, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    for (Vehicle& v : s.vehicles) v.status = VehicleStatus::Mothballed;
    addTestVehicle(s, r, fighter, homeLoc);
    w.home->anger = 40;
    populationTurn(r, s);
    CHECK(w.home->anger == 40);
}

TEST_CASE("economy: troops and plague on the planet") {
    MoodWorld w;
    const Rules& r = *w.rules;
    const DesignId troop = addTestDesign(w.s, r, kMe, "Marines", "Test Troop Hull", {"Test Troop Rifle"});
    w.home->cargo.units = {{troop, 3}};
    w.moon->plagueLevel = 1;
    populationTurn(r, w.s);
    CHECK(w.home->anger == 40 - 3);
    CHECK(w.moon->anger == 40 + 5);

    // Every troop unit in the cargo counts as ours, whoever owns it (spec 02 §4).
    const DesignId theirs = addTestDesign(w.s, r, kThem, "Legion", "Test Troop Hull", {"Test Troop Rifle"});
    w.s.empire(kMe).relation(kThem).treaty = Treaty::NonAggression;
    w.s.empire(kThem).relation(kMe).treaty = Treaty::NonAggression;
    w.home->cargo.units = {{troop, 1}, {theirs, 2}};
    w.home->anger = 40;
    w.moon->plagueLevel = 0;
    populationTurn(r, w.s);
    CHECK(w.home->anger == 40 - 3);
    // Troops an enemy landed that still fight for the planet count once as
    // enemies, and not as ours: they are kept apart from the cargo (spec 04 §13).
    w.s.empire(kMe).relation(kThem).treaty = Treaty::War;
    w.s.empire(kThem).relation(kMe).treaty = Treaty::War;
    w.home->cargo.units = {{troop, 1}};
    w.home->landedTroops = {{theirs, 2}};
    w.home->invader = kThem;
    REQUIRE(combat::invaders(r, w.s, *w.home) == std::vector<EmpireId>{kThem});
    w.home->anger = 40;
    populationTurn(r, w.s);
    CHECK(w.home->anger == 40 + (70 - 10) / 10);
}

TEST_CASE("economy: system happiness facilities calm every colony there after the update") {
    MoodWorld w;
    const Rules& r = *w.rules;
    w.moon->facilities = {facilityIndex(r, "Theatre")};
    populationTurn(r, w.s);
    CHECK(w.home->anger == 37);
    CHECK(w.moon->anger == 37);
    CHECK(w.far->anger == 40);
    w.home->anger = 1;
    populationTurn(r, w.s);
    CHECK(w.home->anger == 0);
}

TEST_CASE("economy: Emotionless races skip the update and sit at 35 after any change") {
    MoodWorld w;
    const Rules& r = *w.rules;
    giveTrait(r, w.s, kMe, "Stoic");
    w.home->anger = w.moon->anger = w.far->anger = 25;
    populationTurn(r, w.s, {{kMe, "New Treaty War", {}, {}, 1}});
    CHECK(w.far->anger == 25);  // a new colony keeps its starting 25
    CHECK(economy::colonyOutput(r, w.s, *w.far).mood == Mood::Happy);
    w.moon->facilities = {facilityIndex(r, "Theatre")};
    populationTurn(r, w.s);
    CHECK(w.home->anger == 35);
    CHECK(w.moon->anger == 35);
    CHECK(w.far->anger == 25);
    // It never riots and grows without a mood term.
    w.far->anger = 95;
    CHECK(economy::reproductionPercent(r, w.s, *w.far) == 10);
}

TEST_CASE("economy: riots stop output, growth and building, but never cause a rebellion") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    s.vehicles.clear();
    Colony& home = plainHome(r, s, {"Test Spaceport", "Test Mine"});
    const ObjectId farId = freePlanet(s, s.galaxy.object(home.planet).system, false);
    Colony& far = addColony(s, kMe, farId, 400);
    far.facilities = {facilityIndex(r, "Test Mine")};
    far.anger = 100;
    CHECK(economy::colonyOutput(r, s, far).production.isZero());
    const size_t empires = s.empires.size();
    for (int i = 0; i < 300; ++i) populationTurn(r, s);  // the test mood model allows no change: it stays rioting
    REQUIRE(s.colony(farId) != nullptr);
    CHECK(s.colony(farId)->owner == kMe);
    CHECK(s.colony(farId)->totalPopulation() == 400);
    CHECK(s.empires.size() == empires);
    CHECK(logged(s, kMe, "Riots on"));
}

TEST_CASE("economy: a rebel planet founds a new computer empire and becomes its capital") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const ObjectId farId = freePlanet(s, s.galaxy.object(homeworld(s, kMe).planet).system, false);
    Colony& far = addColony(s, kMe, farId, 400);
    far.anger = 100;
    TurnContext ctx{r, s, {}, {}, {}};
    const size_t empires = s.empires.size();
    const EmpireId id = effects::breakAway(ctx, farId);  // the rebellion event and intel operation use it
    REQUIRE(id.valid());
    REQUIRE(s.empires.size() == empires + 1);
    const Colony& rebel = *s.colony(farId);
    CHECK(rebel.owner == id);
    CHECK(rebel.homeworld);
    CHECK(s.empire(id).homeSystem == s.galaxy.object(farId).system);  // its capital's system
    CHECK(rebel.anger == effects::kRebelAnger);  // spec 05 §2.3
    CHECK(rebel.population[0].race == id);
    CHECK(s.empire(id).kind == PlayerKind::Computer);
    CHECK_FALSE(s.empire(id).relation(kMe).contact);  // it meets the empires that detect it
    CHECK(s.empire(id).relation(kMe).treaty == Treaty::None);
    for (const Empire& e : s.empires) CHECK(e.relations.size() == s.empires.size());
    CHECK(countMood(ctx.moodEvents, "Any Planet Lost") == 1);
    std::vector<EmpireOrders> none;
    processTurn(r, s, none);  // the new empire takes part in the next turn
}

// ---- Other income ------------------------------------------------------------------------------------

TEST_CASE("economy: finite resources are drawn down by production") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    s.options.finiteResources = true;
    dropVehicles(s, kMe);
    Colony& home = plainHome(r, s, {"Test Spaceport", "Test Mine", "Test Farm", "Test Refinery"});
    SpaceObject& planet = s.galaxy.object(home.planet);
    planet.value = {1000, 100000, 0};
    const int64_t made = pctTrunc(800, 105);
    auto out = economy::colonyOutput(r, s, home);
    CHECK(out.production == Resources{made, made, 0});
    CHECK(out.depletion == Resources{made, made, 0});
    economyTurn(r, s);
    CHECK(planet.value[0] == 1000 - made);
    CHECK(planet.value[1] == 100000 - made);
    CHECK(economy::colonyOutput(r, s, home).production[Resource::Minerals] == 1000 - made);  // capped by the stock left
    economyTurn(r, s);
    CHECK(planet.value[0] == 0);
    CHECK(economy::colonyOutput(r, s, home).production[Resource::Minerals] == 0);
}

TEST_CASE("economy: remote mining: the first miner in the sector works every object there") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        ruleset::Component c;
        c.name = "Test Mining Pod";
        c.tonnage = 20;
        c.structure = 10;
        c.cost = {50, 0, 0};
        c.vehicles = ruleset::maskOf(ruleset::VehicleType::Ship);
        c.abilities = {ability(AbilityKind::RemoteResourceGenMinerals, 100)};
        rs.components.push_back(c);
        c.name = "Test Big Mining Pod";
        c.abilities = {ability(AbilityKind::RemoteResourceGenMinerals, 250)};
        rs.components.push_back(c);
    });
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    Colony& home = plainHome(*r, s, {});
    const ObjectId rock = freePlanet(s, s.galaxy.object(home.planet).system, true);
    s.galaxy.object(rock).value = {150, 0, 0};
    const Location there = locationOf(s.galaxy, rock);
    const DesignId small = addTestDesign(s, *r, kMe, "Digger", "Test Frigate",
                                         {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Mining Pod"});
    const DesignId big = addTestDesign(s, *r, kMe, "Big Digger", "Test Frigate",
                                       {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Big Mining Pod"});
    addTestVehicle(s, *r, small, there);
    addTestVehicle(s, *r, big, there);
    economyTurn(*r, s);
    CHECK(s.empire(kMe).economy.remoteMining == Resources{150, 0, 0});  // the first miner only
    CHECK(s.galaxy.object(rock).value[0] == 149);

    s.options.finiteResources = true;
    s.galaxy.object(rock).value = {60, 0, 0};
    economyTurn(*r, s);
    CHECK(s.empire(kMe).economy.remoteMining == Resources{60, 0, 0});
    CHECK(s.galaxy.object(rock).value[0] == 0);
}

TEST_CASE("economy: every 10th turn: planet values sum, conditions multiply") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        addFacility(rs, "Deep Core", {ability(AbilityKind::PlanetChangeMineralsValue, 2)});
        addFacility(rs, "Terraformer", {ability(AbilityKind::PlanetConditionsChangeSystem, 10)});
        addFacility(rs, "Polluter", {ability(AbilityKind::PlanetChangeConditions, -20)});
        addFacility(rs, "Seeder", {ability(AbilityKind::PlanetValueChangeSystem, 5)});
    });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {"Test Climate Station", "Deep Core", "Deep Core"});
    SpaceObject& p = s.galaxy.object(home.planet);
    using xmath::Ext;
    p.conditions = Conditions::hundredths(50);
    s.turn = 8;  // processed as turn 9
    populationTurn(*r, s);
    CHECK(p.conditions == Conditions::hundredths(50));
    CHECK(p.value[0] == 100);
    s.turn = 9;  // turn 10
    populationTurn(*r, s);
    // × (1 + 1 / 100), kept as the real number: no rounding, no minimum step (spec 02 §13 Q46).
    const Conditions grown = Conditions::of(Conditions::hundredths(50).value() * (Ext(1) + Ext(1) / Ext(100)));
    CHECK(p.conditions == grown);
    CHECK(p.conditions > Conditions::hundredths(50));
    CHECK(p.conditions < Conditions::hundredths(51));
    CHECK(p.value[0] == 104);  // both facilities count
    s.turn = 19;
    populationTurn(*r, s);
    CHECK(p.conditions == Conditions::of(grown.value() * (Ext(1) + Ext(1) / Ext(100))));
    // A product of exactly 0 gives 0.1.
    p.conditions = Conditions{};
    s.turn = 29;
    populationTurn(*r, s);
    CHECK(p.conditions == Conditions::of(Ext(1) / Ext(10)));
    p.value = {104, 100, 100};

    // A negative sum does nothing; system changes take the best and multiply.
    home.facilities = {facilityIndex(*r, "Polluter"), facilityIndex(*r, "Terraformer"), facilityIndex(*r, "Seeder")};
    p.conditions = Conditions::hundredths(100);
    s.turn = 39;
    populationTurn(*r, s);
    CHECK(p.conditions == Conditions::of(Ext(1) * Ext(110) / Ext(100)));  // × (100 + 10) / 100
    CHECK(p.value == std::array<int, 3>{109, 105, 105});
    p.conditions = Conditions::hundredths(140);
    s.turn = 49;
    populationTurn(*r, s);
    CHECK(p.conditions == kOptimalConditions);  // never above 1.5
    CHECK(logged(s, kMe, "optimal conditions"));

    // Finite games: the system change is a percentage of the stock, truncated.
    s.options.finiteResources = true;
    p.value = {1001, 0, 50};
    s.turn = 59;
    populationTurn(*r, s);
    CHECK(p.value == std::array<int, 3>{pctTrunc(1001, 105), 0, pctTrunc(50, 105)});
}

// ---- Reports and determinism --------------------------------------------------------------------------

TEST_CASE("economy: updateReports projects without changing the game") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = homeworld(s, kMe);
    const cmd::QueueTarget q{home.planet, {}};
    QueueItem item;
    item.design = s.empire(kMe).designs.front();
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, item}).ok);
    const GameState before = s;
    economy::updateReports(r, s);
    for (size_t i = 0; i < s.empires.size(); ++i) {
        CHECK(s.empires[i].stockpile == before.empires[i].stockpile);
        CHECK(s.empires[i].log.size() == before.empires[i].log.size());
    }
    CHECK(s.vehicles.size() == before.vehicles.size());
    CHECK(s.rng == before.rng);
    CHECK(home.queue.items[0].spent == before.colony(home.planet)->queue.items[0].spent);
    for (size_t i = 0; i < s.galaxy.objects.size(); ++i) CHECK(s.galaxy.objects[i].value == before.galaxy.objects[i].value);

    const EconomyReport projected = s.empire(kMe).economy;
    CHECK(projected.construction == economy::itemCost(r, s, kMe, q, home.queue.items[0]));
    CHECK(projected.maintenance == economy::maintenanceCost(r, s, kMe));
    economyTurn(r, s);
    const EconomyReport& actual = s.empire(kMe).economy;
    CHECK(actual.colonies == projected.colonies);
    CHECK(actual.research == projected.research);
    CHECK(actual.maintenance == projected.maintenance);
    CHECK(actual.construction == projected.construction);
}

TEST_CASE("economy: full turns are deterministic and keep the books straight") {
    const Rules& r = engineRules();
    GameState a = newEngineGame(5, 3, 12);
    GameState b = newEngineGame(5, 3, 12);
    std::vector<EmpireOrders> none;
    for (int t = 0; t < 25; ++t) {
        processTurn(r, a, none);
        processTurn(r, b, none);
        for (size_t i = 0; i < a.empires.size(); ++i) {
            CHECK(a.empires[i].stockpile == b.empires[i].stockpile);
            CHECK(a.empires[i].economy.research == b.empires[i].economy.research);
            CHECK_FALSE(a.empires[i].stockpile.anyNegative());
            CHECK(a.empires[i].stockpile.covers(Resources{}));
            CHECK(economy::storageCapacity(r, a, a.empires[i].id).covers(a.empires[i].stockpile));
        }
        for (size_t i = 0; i < a.colonies.size(); ++i) {
            REQUIRE(a.colonies[i].has_value() == b.colonies[i].has_value());
            if (!a.colonies[i]) continue;
            CHECK(a.colonies[i]->population == b.colonies[i]->population);
            CHECK(a.colonies[i]->anger == b.colonies[i]->anger);
            CHECK(a.colonies[i]->totalPopulation() <= maxPopulation(r, a, *a.colonies[i]));
        }
        CHECK(a.rng == b.rng);
    }
    CHECK(homeworld(a, kMe).totalPopulation() > 0);
}

// ---- Calibration against the installed data set (opt-in) -------------------------------------------------

namespace {

const Rules* installRules() {
    static const std::unique_ptr<Rules> rules = []() -> std::unique_ptr<Rules> {
        const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
        if (!env) return nullptr;
        auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
        if (!dir) return nullptr;
        auto loaded = ruleset::loadRuleset(*dir);
        if (!loaded.ruleset) return nullptr;
        return std::make_unique<Rules>(std::move(*loaded.ruleset), dir->parent_path());
    }();
    return rules.get();
}

} // namespace

TEST_CASE("economy: installed data set reproduces the observed Quick Start homeworld (opt-in)") {
    const Rules* r = installRules();
    if (!r || !findPreset(*r, "Terran")) return;
    // A Terran empire (first preset tier) on a medium homeworld, default setup (docs/spec/07, Calibration).
    std::optional<GameState> game;
    for (uint64_t seed = 1; seed < 60 && !game; ++seed) {
        GameSetup setup;
        setup.seed = seed;
        setup.options.systemCount = 20;
        EmpireSetup e;
        e.preset = "Terran";
        setup.empires.push_back(e);
        auto g = createGame(*r, setup);
        REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
        if (datafile::keysEqual(g->galaxy.object(homeworld(*g, kMe).planet).size, "Medium")) game = std::move(*g);
    }
    REQUIRE(game.has_value());
    GameState& s = *game;
    Colony& home = homeworld(s, kMe);
    SpaceObject& planet = s.galaxy.object(home.planet);
    // The starting stockpile is Starting Resources plus one turn of income (spec 02 §9),
    // and each homeworld value is the setting plus R[1,10] - 5.
    const Resources start = s.empire(kMe).stockpile;
    CHECK(start == Resources{20000, 20000, 20000} + economy::empireProduction(*r, s, kMe).resources);
    for (int v : planet.value) {
        CHECK(v >= r->setting("Plr Planet Value Medium Percent", 100) - 4);
        CHECK(v <= r->setting("Plr Planet Value Medium Percent", 100) + 5);
    }
    // The second observed game: values 102 / 99 / 103 give 6120 / 1108 / 1153 and a
    // first-turn treasury of 26120 / 21108 / 21153 (docs/spec/07, Calibration notes).
    planet.value = {102, 99, 103};
    CHECK(Resources{20000, 20000, 20000} + economy::empireProduction(*r, s, kMe).resources == Resources{26120, 21108, 21153});
    planet.value = {100, 98, 102};  // as observed in the first game
    planet.conditions = Conditions::hundredths(50);  // "Unpleasant"
    CHECK(economy::conditionsName(economy::conditionsBand(planet.conditions)) == "Unpleasant");

    CHECK(home.totalPopulation() == 2000);
    CHECK(maxPopulation(*r, s, home) == 2000);
    CHECK(home.facilities.size() == 15);
    const auto out = economy::colonyOutput(*r, s, home);
    CHECK(out.mood == Mood::Happy);
    CHECK(out.reproductionPercent == 10);
    CHECK(out.production == Resources{6000, 1097, 1142});
    CHECK(out.research == 3950);
    CHECK(out.intelligence == 0);
    CHECK(out.connected);
    CHECK(economy::constructionRate(*r, s, kMe, cmd::QueueTarget{home.planet, {}}) == Resources{2600, 2600, 2600});
    CHECK(economy::maintenancePercent(*r, s.empire(kMe)) == 10);

    std::vector<EmpireOrders> none;
    TurnOptions idle;
    idle.aiForMissing = false;  // nobody spends anything this turn
    processTurn(*r, s, none, idle);
    const EconomyReport& rep = s.empire(kMe).economy;
    CHECK(rep.colonies == Resources{6000, 1097, 1142});
    CHECK(s.empire(kMe).stockpile == start + rep.colonies - rep.maintenance);
}

TEST_CASE("economy: installed data set keeps the books straight over many turns (opt-in)") {
    const Rules* r = installRules();
    if (!r || r->racePresets().size() < 4) return;
    GameSetup setup;
    setup.seed = 11;
    setup.options.systemCount = 24;
    for (size_t i = 0; i < 4; ++i) {
        EmpireSetup e;
        e.preset = r->racePresets()[i * 5 % r->racePresets().size()].folder;
        setup.empires.push_back(e);
    }
    auto game = createGame(*r, setup);
    REQUIRE(game.has_value());
    GameState& s = *game;
    for (Empire& e : s.empires) {
        // Keep every yard busy with the empire's first design.
        Colony& home = homeworld(s, e.id);
        QueueItem item;
        item.design = e.designs.front();
        apply(*r, s, e.id, cmd::QueueAdd{cmd::QueueTarget{home.planet, {}}, item});
        apply(*r, s, e.id, cmd::QueueFlags{cmd::QueueTarget{home.planet, {}}, false, true, false, -1});
    }
    std::vector<EmpireOrders> none;
    for (int t = 0; t < 30; ++t) {
        processTurn(*r, s, none);
        for (const Empire& e : s.empires) {
            CHECK_FALSE(e.stockpile.anyNegative());
            CHECK(economy::storageCapacity(*r, s, e.id).covers(e.stockpile));
        }
        for (const auto& c : s.colonies)
            if (c) CHECK(c->totalPopulation() <= maxPopulation(*r, s, *c));
    }
    for (const Empire& e : s.empires) CHECK(s.design(e.designs.front()).built >= 5);
}
