// Economy (docs/spec/02): planet output, delivery, storage, maintenance,
// construction queues, population, mood, riots and plague, on the original
// engine test rules. The last two tests calibrate against, and run, the
// player's installed data set (opt-in: OPENSE4_CLASSIC_DATA=auto or a data
// directory).

#include "engine_fixture.hpp"

#include "datafile/datafile.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

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
// owner's race, 100 % value, Pleasant conditions, Indifferent mood.
Colony& plainHome(const Rules& r, GameState& s, std::initializer_list<std::string_view> facilities, EmpireId e = kMe) {
    Colony& c = homeworld(s, e);
    c.facilities.clear();
    for (auto f : facilities) c.facilities.push_back(facilityIndex(r, f));
    c.population = {{e, 1000}};
    c.anger = 350;
    SpaceObject& p = s.galaxy.object(c.planet);
    p.value = {100, 100, 100};
    p.conditions = 90;
    return c;
}

void dropVehicles(GameState& s, EmpireId e) { std::erase_if(s.vehicles, [&](const Vehicle& v) { return v.owner == e; }); }

std::vector<MoodEvent> economyTurn(const Rules& r, GameState& s) {
    TurnContext ctx{r, s, {}, {}, {}};
    economy::runEconomy(ctx);
    return ctx.moodEvents;
}

std::vector<MoodEvent> populationTurn(const Rules& r, GameState& s, std::vector<MoodEvent> moods = {}) {
    TurnContext ctx{r, s, std::move(moods), {}, {}};
    economy::runPopulation(ctx);
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
    c.anger = 350;
    s.colonies[planet.index()] = c;
    SpaceObject& p = s.galaxy.object(planet);
    p.atmosphere = s.empire(e).race.atmosphere;
    p.value = {100, 100, 100};
    p.conditions = 90;
    return *s.colonies[planet.index()];
}

bool logged(const GameState& s, EmpireId e, std::string_view text) {
    for (const LogEntry& l : s.empire(e).log)
        if (l.title.find(text) != std::string::npos || l.text.find(text) != std::string::npos) return true;
    return false;
}

int countMood(const std::vector<MoodEvent>& events, std::string_view trigger) {
    int n = 0;
    for (const MoodEvent& m : events) n += m.trigger == trigger;
    return n;
}

DesignId frigate(GameState& s, const Rules& r, EmpireId e, std::string_view name = "Frigate") {
    return addTestDesign(s, r, e, name, "Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
}

void setChar(GameState& s, EmpireId e, Characteristic c, int v) { s.empire(e).race.characteristics[static_cast<size_t>(c)] = v; }

} // namespace

// ---- Modifier tables ----------------------------------------------------------------------------

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
    CHECK(populationModifier(*r, 5000).shipyard == 120);  // the last row caps the table
    CHECK(populationModifier(engineRules(), 5000).production == 100);  // no table: no change

    CHECK(economy::moodOutputPercent(engineRules(), Mood::Rioting) == 0);
    CHECK(economy::moodOutputPercent(engineRules(), Mood::Angry) == 80);
    CHECK(economy::moodOutputPercent(engineRules(), Mood::Indifferent) == 100);
    CHECK(economy::moodOutputPercent(engineRules(), Mood::Jubilant) == 120);
    CHECK(economy::moodOutputPercent(*r, Mood::Happy) == 150);
    CHECK(economy::moodReproduction(Mood::Rioting) == -5);
    CHECK(economy::moodReproduction(Mood::Jubilant) == 5);

    CHECK(economy::conditionsBand(95) == economy::ConditionsBand::Pleasant);
    CHECK(economy::conditionsBand(45) == economy::ConditionsBand::Unpleasant);
    CHECK(economy::conditionsBand(0) == economy::ConditionsBand::Deadly);
    CHECK(economy::conditionsName(economy::ConditionsBand::Harsh) == "Harsh");
    CHECK(economy::conditionsReproductionPenalty(economy::ConditionsBand::Deadly, 100) == 5);
    CHECK(economy::conditionsReproductionPenalty(economy::ConditionsBand::Deadly, 160) == 2);
}

// ---- Planet output ---------------------------------------------------------------------------------

TEST_CASE("economy: planet output adds its modifiers and applies planet value") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {"Test Spaceport", "Test Mine", "Test Mine", "Test Farm", "Test Refinery", "Test Lab", "Test Intel Center"});
    s.galaxy.object(home.planet).value = {100, 50, 150};

    // Indifferent, no population table, Steady culture +5 % production.
    auto out = economy::colonyOutput(r, s, home);
    CHECK(out.production == Resources{1680, 420, 1260});
    CHECK(out.research == 500);
    CHECK(out.intelligence == 300);
    CHECK(out.connected);
    CHECK_FALSE(out.blockaded);
    CHECK(out.deliveryPercent == 100);
    CHECK(out.mood == Mood::Indifferent);
    CHECK(out.facilitiesOperating == 6);
    CHECK(out.productionPercent[0] == 105);

    home.anger = 200;  // Happy: +10
    out = economy::colonyOutput(r, s, home);
    CHECK(out.mood == Mood::Happy);
    CHECK(out.production[Resource::Minerals] == 1600 * 115 / 100);
    CHECK(out.research == 550);

    setChar(s, kMe, Characteristic::MiningAptitude, 120);
    setChar(s, kMe, Characteristic::Intelligence, 130);
    setChar(s, kMe, Characteristic::Cunning, 80);
    out = economy::colonyOutput(r, s, home);
    CHECK(out.production[Resource::Minerals] == 1600 * 135 / 100);
    CHECK(out.production[Resource::Organics] == 400 * 115 / 100);
    CHECK(out.research == 700);
    CHECK(out.intelligence == 270);

    // Modifiers never make output negative.
    setChar(s, kMe, Characteristic::MiningAptitude, 1);
    CHECK(economy::colonyOutput(r, s, home).production[Resource::Minerals] == 1600 * 16 / 100);
    setChar(s, kMe, Characteristic::MiningAptitude, 100);

    home.anger = 800;  // rioting: nothing at all
    out = economy::colonyOutput(r, s, home);
    CHECK(out.mood == Mood::Rioting);
    CHECK(out.production.isZero());
    CHECK(out.research == 0);
    CHECK(out.deliveryPercent == 0);
}

TEST_CASE("economy: planet and system modifiers, solar power and depots") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        addFacility(rs, "Booster", {ability(AbilityKind::ResourceGenModPlanetMinerals, 20)});
        addFacility(rs, "Weak Booster", {ability(AbilityKind::ResourceGenModPlanetMinerals, 10)});
        addFacility(rs, "System Booster", {ability(AbilityKind::ResourceGenModSystemMinerals, 30)});
        addFacility(rs, "Think Tank", {ability(AbilityKind::PlanetPointGenModResearch, 50)});
        addFacility(rs, "Solar Plant", {ability(AbilityKind::SolarResourceGenOrganics, 100)});
    });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {"Test Mine", "Booster", "Weak Booster", "Test Lab", "Think Tank", "Solar Plant", "Test Depot"});
    const SystemId sys = s.galaxy.object(home.planet).system;
    int stars = 0;
    for (ObjectId o : s.galaxy.system(sys).objects) stars += s.galaxy.object(o).kind == ObjectKind::Star;
    REQUIRE(stars > 0);

    auto out = economy::colonyOutput(*r, s, home);
    CHECK(out.production[Resource::Minerals] == 800 * 125 / 100);  // best planet modifier (20) + culture (5)
    CHECK(out.research == 750);
    CHECK(out.solar == Resources{0, 100 * stars, 0});                // no modifiers on solar output
    CHECK(out.production[Resource::Organics] == 100 * stars);
    CHECK(out.supply == 5000);

    // A system-wide booster on another colony in the system helps every colony there.
    Colony& moon = addColony(s, kMe, freePlanet(s, sys, true), 100);
    moon.facilities = {facilityIndex(*r, "System Booster")};
    out = economy::colonyOutput(*r, s, home);
    CHECK(out.production[Resource::Minerals] == 800 * 155 / 100);

    // A tiny population still runs every facility (spec 02 §13 Q3).
    home.population = {{kMe, 4}};
    out = economy::colonyOutput(*r, s, home);
    CHECK(out.facilitiesOperating == 3);
    CHECK(out.production[Resource::Minerals] == 800 * 155 / 100);
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

    auto out = economy::colonyOutput(*r, s, home);
    CHECK_FALSE(out.connected);
    CHECK(out.deliveryPercent == 25);  // the home system still delivers a quarter (inferred)

    Colony& far = addColony(s, kMe, freePlanet(s, homeSys, false), 1000);
    far.facilities = {facilityIndex(*r, "Test Mine")};
    CHECK(economy::colonyOutput(*r, s, far).deliveryPercent == 0);

    economyTurn(*r, s);
    const EconomyReport& rep = s.empire(kMe).economy;
    CHECK(rep.colonies[Resource::Minerals] == 840 / 4);
    CHECK(rep.undelivered[Resource::Minerals] == 840 - 840 / 4 + 840);

    far.facilities.push_back(facilityIndex(*r, "Test Spaceport"));
    out = economy::colonyOutput(*r, s, far);
    CHECK(out.connected);
    CHECK(out.deliveryPercent == 100);

    // The No Spaceports trait connects every system.
    for (uint32_t i = 0; i < r->data().racialTraits.size(); ++i)
        if (r->data().racialTraits[i].name == "Free Traders") s.empire(kMe).race.traits.push_back(i);
    CHECK(economy::colonyOutput(*r, s, home).deliveryPercent == 100);
}

TEST_CASE("economy: enemy ships in orbit blockade a planet") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {"Test Spaceport", "Test Mine"});
    const DesignId raider = frigate(s, r, kThem, "Raider");
    const VehicleId id = addTestVehicle(s, r, raider, locationOf(s.galaxy, home.planet)).id;

    auto out = economy::colonyOutput(r, s, home);
    CHECK(out.blockaded);
    CHECK(out.deliveryPercent == 0);
    CHECK(out.production[Resource::Minerals] == 840);  // still made, but lost

    s.vehicle(id)->status = VehicleStatus::Mothballed;
    CHECK_FALSE(economy::colonyBlockaded(r, s, home));
    s.vehicle(id)->status = VehicleStatus::Cloaked;
    CHECK_FALSE(economy::colonyBlockaded(r, s, home));
    s.vehicle(id)->status = VehicleStatus::Normal;
    CHECK(economy::colonyBlockaded(r, s, home));
    s.empire(kMe).relation(kThem).treaty = Treaty::NonAggression;
    s.empire(kThem).relation(kMe).treaty = Treaty::NonAggression;
    CHECK_FALSE(economy::colonyBlockaded(r, s, home));
}

// ---- Treasury ----------------------------------------------------------------------------------------

TEST_CASE("economy: storage caps the treasury and storage facilities raise it") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    dropVehicles(s, kMe);
    plainHome(r, s, {"Test Spaceport", "Test Mine", "Test Mineral Store"});
    CHECK(economy::storageCapacity(r, s, kMe) == Resources{70000, 50000, 50000});
    s.empire(kMe).stockpile = {69900, 49990, 0};
    economyTurn(r, s);
    const EconomyReport& rep = s.empire(kMe).economy;
    CHECK(rep.storageCap == Resources{70000, 50000, 50000});
    CHECK(rep.otherIncome == Resources{0, 200, 200});  // the income floor
    CHECK(s.empire(kMe).stockpile == Resources{70000, 50000, 200});
    CHECK(rep.lostToStorage == Resources{740, 190, 0});
}

TEST_CASE("economy: generated points, the income floor and the opening research pool") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) {
        addFacility(rs, "Mint", {ability(AbilityKind::GeneratePointsMinerals, 300), ability(AbilityKind::GeneratePointsResearch, 50)});
    });
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    plainHome(*r, s, {"Mint"});  // no spaceport: flat points need none
    s.empire(kMe).stockpile = {};
    economyTurn(*r, s);
    const EconomyReport& rep = s.empire(kMe).economy;
    CHECK(rep.otherIncome == Resources{300, 200, 200});
    CHECK(rep.research == 50 + s.options.startingResources[Resource::Minerals]);  // turn 1 pool (inferred size)
    CHECK(s.empire(kMe).stockpile == Resources{300, 200, 200});
    s.turn = 1;
    economyTurn(*r, s);
    CHECK(s.empire(kMe).economy.research == 50);
}

TEST_CASE("economy: maintenance rate, hull and system modifiers, mothballing") {
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
    CHECK(economy::vehicleMaintenance(*r, s, *s.vehicle(c)) == Resources{42, 2, 3});  // bases pay half
    s.vehicle(a)->status = VehicleStatus::Mothballed;
    CHECK(economy::vehicleMaintenance(*r, s, *s.vehicle(a)).isZero());

    Resources total;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kMe) total += economy::vehicleMaintenance(*r, s, v);
    CHECK(economy::maintenanceCost(*r, s, kMe) == total);
}

TEST_CASE("economy: unpaid maintenance scuttles vehicles") {
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
    for (int i = 0; i < 10; ++i) addTestVehicle(s, *r, ship, locationOf(s.galaxy, home.planet));
    s.empire(kMe).stockpile = {};
    CHECK(economy::maintenanceCost(*r, s, kMe) == Resources{400, 50, 50});

    const auto moods = economyTurn(*r, s);
    int left = 0;
    for (const Vehicle& v : s.vehicles) left += v.owner == kMe;
    CHECK(left == 5);  // 500 unpaid, one vehicle per 100
    CHECK(s.design(ship).lost == 5);
    CHECK(countMood(moods, "Any Ship Lost") == 5);
    CHECK(logged(s, kMe, "scuttled"));
    CHECK(s.empire(kMe).stockpile.isZero());
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

    home.facilities.clear();  // no yard: the empire base rate
    CHECK(economy::constructionRate(*r, s, kMe, q) == Resources{1400, 2800, 2800});
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

    // Ship yards have no population and no planetary bonus.
    const DesignId yardShip = addTestDesign(s, *r, kMe, "Tender", "Test Station",
                                            {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Yard Module"});
    const VehicleId v = addTestVehicle(s, *r, yardShip, locationOf(s.galaxy, home.planet)).id;
    CHECK(economy::constructionRate(*r, s, kMe, cmd::QueueTarget{{}, v}) == Resources{650, 650, 650});
    CHECK(economy::constructionRate(*r, s, kThem, cmd::QueueTarget{{}, v}).isZero());  // not theirs
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

TEST_CASE("economy: queues spend the rate, keep progress and pass leftovers on") {
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
    CHECK(home.queue.items[0].spent == Resources{2000, 0, 0});
    CHECK(s.empire(kMe).economy.construction == Resources{2000, 0, 0});
    turn();
    CHECK(home.queue.items[0].spent == Resources{4000, 0, 0});
    const auto moods = turn();  // the monument finishes; the rest of the rate builds both mines
    CHECK(home.queue.items.empty());
    CHECK(s.empire(kMe).economy.construction == Resources{1600, 0, 0});
    CHECK(home.facilities.size() == 5);
    CHECK(countMood(moods, "Facility Constructed") == 3);
    CHECK(logged(s, kMe, "Monument completed"));
}

TEST_CASE("economy: a short treasury slows construction") {
    auto r = queueRules();
    GameState s = newGame(*r);
    dropVehicles(s, kMe);
    Colony& home = plainHome(*r, s, {"Test Space Yard"});
    const cmd::QueueTarget q{home.planet, {}};
    REQUIRE(apply(*r, s, kMe, cmd::QueueAdd{q, facilityItem(*r, "Monument")}).ok);
    s.empire(kMe).stockpile = {300, 0, 0};
    economyTurn(*r, s);
    CHECK(home.queue.items[0].spent == Resources{500, 0, 0});  // 300 + the 200 income floor
    CHECK(logged(s, kMe, "Construction slowed"));
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
    home.anger = 800;
    CHECK(turn() == 0);  // rioting planets build nothing
    home.anger = 350;
    CHECK(turn() == 2000);

    REQUIRE(apply(*r, s, kMe, cmd::QueueFlags{q, false, false, true, -1}).ok);
    CHECK(turn() == 3000);
    CHECK(home.queue.emergencyTurns == 1);
    for (int i = 0; i < 9; ++i) CHECK(turn() == 3000);
    CHECK_FALSE(home.queue.emergency);  // 10 turns is the most
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
}

TEST_CASE("economy: repeat build keeps the top item") {
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
    const auto moods = economyTurn(r, s);
    REQUIRE(s.vehicles.size() == before + 1);
    const Vehicle& built = s.vehicles.back();
    CHECK(built.design == ship);
    CHECK(built.location == homeLoc);
    REQUIRE(built.orders.size() == 1);
    CHECK(built.orders[0].location == rally);
    CHECK(countMood(moods, "Ship Constructed") == 1);
    CHECK(countMood(moods, "Any Ship Constructed") == 1);
    CHECK(home.queue.items.empty());

    s.options.maxShipsPerPlayer = shipCount(r, s, kMe);
    home.queue.items.push_back(item);
    economyTurn(r, s);
    CHECK(s.vehicles.size() == before + 1);
    CHECK(home.queue.items.size() == 1);  // waits, progress kept
    CHECK(home.queue.items[0].spent == Resources{160, 20, 20});
    CHECK(logged(s, kMe, "limit on ships"));
}

TEST_CASE("economy: units go into cargo, overflow to other holds, or wait") {
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

    // A full planet: the units go to a ship's hold anywhere in the empire.
    home.cargo.population = {{kMe, 1'000'000}};
    const DesignId hauler = addTestDesign(s, r, kMe, "Hauler", "Test Frigate",
                                          {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Cargo Bay"});
    const VehicleId h = addTestVehicle(s, r, hauler, Location{homeLoc.system, Sector{0, 0}}).id;
    item.count = 2;
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, item}).ok);
    economyTurn(r, s);
    CHECK(s.vehicle(h)->cargo.unitCount(fighter) == 2);  // 50 kT holds two 20 kT fighters
    CHECK(home.queue.items.empty());

    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, item}).ok);
    economyTurn(r, s);
    CHECK(home.queue.items.size() == 1);  // no room: not built
    CHECK(s.design(fighter).built == 5);
    CHECK(logged(s, kMe, "No room"));
}

TEST_CASE("economy: facilities need a free slot; upgrades replace older levels") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    dropVehicles(s, kMe);
    Colony& home = plainHome(r, s, {});
    const int slots = facilitySlots(r, s, home);
    home.facilities.assign(static_cast<size_t>(slots - 1), facilityIndex(r, "Test Farm"));
    home.queue.items = {facilityItem(r, "Test Mine"), facilityItem(r, "Test Mine")};  // the second overfills
    s.empire(kMe).stockpile = {40000, 40000, 40000};
    economyTurn(r, s);
    CHECK(static_cast<int>(home.facilities.size()) == slots);
    REQUIRE(home.queue.items.size() == 1);
    CHECK(home.queue.items[0].spent == Resources{300, 0, 0});
    CHECK(logged(s, kMe, "No facility slot"));

    // Upgrades: every older mine on the planet becomes the newest level.
    home.queue.items.clear();
    home.facilities = {facilityIndex(r, "Test Mine"), facilityIndex(r, "Test Mine"), facilityIndex(r, "Test Farm")};
    s.empire(kMe).techLevels[techArea(r, "Test Economics").index()] = 3;
    QueueItem up;
    up.kind = QueueItem::Kind::Upgrade;
    up.facility = facilityIndex(r, "Test Mine");
    const cmd::QueueTarget q{home.planet, {}};
    REQUIRE(apply(r, s, kMe, cmd::QueueAdd{q, up}).ok);
    CHECK(economy::upgradeableCount(r, s, kMe, home, up.facility) == 2);
    CHECK(economy::itemCost(r, s, kMe, q, home.queue.items[0]) == Resources{400, 0, 0});  // 50 % of 400, twice
    economyTurn(r, s);
    CHECK(home.facilities[0] == facilityIndex(r, "Test Mine II"));
    CHECK(home.facilities[1] == facilityIndex(r, "Test Mine II"));
    CHECK(home.facilities[2] == facilityIndex(r, "Test Farm"));
    CHECK(home.queue.items.empty());
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

// ---- Population -------------------------------------------------------------------------------------

TEST_CASE("economy: population growth by race, mood and conditions") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {});
    SpaceObject& planet = s.galaxy.object(home.planet);
    populationTurn(r, s);
    CHECK(home.totalPopulation() == 1010);  // 10 % a year = 1 % a turn

    home.anger = 200;  // Happy: +2
    CHECK(economy::reproductionPercent(r, s, home, kMe) == 12);
    CHECK(economy::colonyOutput(r, s, home).reproductionPercent == 12);
    planet.conditions = 10;  // Deadly: -5
    CHECK(economy::reproductionPercent(r, s, home, kMe) == 7);
    setChar(s, kMe, Characteristic::EnvironmentalResistance, 150);
    CHECK(economy::reproductionPercent(r, s, home, kMe) == 10);
    setChar(s, kMe, Characteristic::Reproduction, 120);
    CHECK(economy::reproductionPercent(r, s, home, kMe) == 30);

    home.population = {{kMe, 5}};
    home.anger = 350;
    planet.conditions = 90;
    setChar(s, kMe, Characteristic::Reproduction, 100);
    populationTurn(r, s);
    CHECK(home.totalPopulation() == 6);  // small colonies still grow

    const int64_t cap = maxPopulation(r, s, home);
    home.population = {{kMe, cap - 3}};
    populationTurn(r, s);
    CHECK(home.totalPopulation() == cap);

    // Negative rates shrink a colony, never below 1M.
    setChar(s, kMe, Characteristic::Reproduction, 80);
    home.population = {{kMe, 1000}};
    populationTurn(r, s);
    CHECK(home.totalPopulation() == 990);
}

TEST_CASE("economy: growth runs every Reproduction Check Frequency turns") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) { setKey(rs, "Reproduction Check Frequency", 2); });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {});
    s.turn = 4;
    populationTurn(*r, s);
    CHECK(home.totalPopulation() == 1020);
    s.turn = 5;
    populationTurn(*r, s);
    CHECK(home.totalPopulation() == 1020);
}

TEST_CASE("economy: replicants add population in proportion to the races present") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) { addFacility(rs, "Cloning Vat", {ability(AbilityKind::ChangePopulationSystem, 10)}); });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {"Cloning Vat"});
    home.population = {{kMe, 300}, {kThem, 100}};
    setChar(s, kMe, Characteristic::Reproduction, 90);    // no natural growth for either race
    setChar(s, kThem, Characteristic::Reproduction, 90);
    populationTurn(*r, s);
    CHECK(home.population[0].millions == 308);
    CHECK(home.population[1].millions == 102);
}

TEST_CASE("economy: plague kills until prevented or cured") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {});
    home.plagueLevel = 2;
    populationTurn(r, s);
    CHECK(home.totalPopulation() == 1010 - 20);  // growth, then 2 % dead
    CHECK(economy::plagueProtection(r, s, home) == 0);
    home.facilities = {facilityIndex(r, "Test Medical Lab")};  // prevents level 2
    CHECK(economy::plagueProtection(r, s, home) == 2);
    populationTurn(r, s);
    CHECK(home.plagueLevel == 0);
    CHECK(logged(s, kMe, "cured"));
}

TEST_CASE("economy: atmosphere converters and domes") {
    auto r = tweakedRules([](ruleset::Ruleset& rs) { addFacility(rs, "Air Plant", {ability(AbilityKind::PlanetChangeAtmosphere, 3)}); });
    GameState s = newGame(*r);
    Colony& home = plainHome(*r, s, {"Air Plant"});
    SpaceObject& planet = s.galaxy.object(home.planet);
    planet.atmosphere = "Methane";
    CHECK_FALSE(breathable(s, home));
    const int64_t domed = maxPopulation(*r, s, home);
    home.population = {{kMe, 5000}};
    populationTurn(*r, s);
    CHECK(home.totalPopulation() == domed);  // the surplus has no room under the dome
    CHECK(logged(s, kMe, "overcrowded"));
    CHECK(planet.atmosphere == "Methane");
    populationTurn(*r, s);
    CHECK(planet.atmosphere == "Methane");
    populationTurn(*r, s);
    CHECK(planet.atmosphere == s.empire(kMe).race.atmosphere);
    CHECK(home.atmosphereCountdown == -1);
    CHECK(breathable(s, home));
}

// ---- Mood ---------------------------------------------------------------------------------------------

namespace {

std::unique_ptr<Rules> moodRules() {
    return tweakedRules([](ruleset::Ruleset& rs) {
        auto& m = rs.happinessModels[0];
        m.maxPositiveChange = 100;
        m.maxNegativeChange = -100;
        m.triggers.emplace_back("Battle in System - Loss", 20);
        m.triggers.emplace_back("Facility Constructed", -5);
        m.triggers.emplace_back("Natural Decrease", -3);
        m.triggers.emplace_back("Natural Decrease for Other Races", 7);
        m.triggers.emplace_back("Our Ship in Sector", -10);
        m.triggers.emplace_back("Enemy Ship in Sector", 8);
        m.triggers.emplace_back("New Treaty War", 50);
        addTrait(rs, "Stoic", "Population Emotionless", 0);
    });
}

} // namespace

TEST_CASE("economy: mood events by scope, natural drift and clamps") {
    auto r = moodRules();
    GameState s = newGame(*r);
    s.vehicles.clear();
    Colony& home = plainHome(*r, s, {});
    const SystemId sys = s.galaxy.object(home.planet).system;
    Colony& moon = addColony(s, kMe, freePlanet(s, sys, true), 1000);
    Colony& far = addColony(s, kMe, freePlanet(s, sys, false), 1000);
    home.anger = moon.anger = far.anger = 400;

    std::vector<MoodEvent> events;
    events.push_back({kMe, "Any Ship Lost", {}, {}, 3});                   // +6 everywhere
    events.push_back({kMe, "Battle in System - Loss", sys, {}, 1});        // +20 in the system
    events.push_back({kMe, "Facility Constructed", sys, home.planet, 1});  // -5 at home only
    events.push_back({kThem, "Battle in System - Loss", sys, {}, 1});      // someone else's
    populationTurn(*r, s, events);
    CHECK(home.anger == 400 + 6 + 20 - 5 - 3);
    CHECK(moon.anger == 400 + 6 + 20 - 3);
    CHECK(far.anger == 400 + 6 - 3);

    // Clamped to the model's largest change per turn.
    populationTurn(*r, s, {{kMe, "New Treaty War", {}, {}, 1}, {kMe, "Any Ship Lost", {}, {}, 100}});
    CHECK(far.anger == 403 + 100);

    // Our ships calm their sector; enemy ships anger theirs.
    const VehicleId ours = addTestVehicle(s, *r, frigate(s, *r, kMe), locationOf(s.galaxy, home.planet)).id;
    addTestVehicle(s, *r, frigate(s, *r, kThem, "Raider"), locationOf(s.galaxy, far.planet));
    const int homeBefore = home.anger, moonBefore = moon.anger, farBefore = far.anger;
    populationTurn(*r, s);
    CHECK(home.anger == homeBefore - 10 - 3);
    CHECK(moon.anger == moonBefore - 3);
    CHECK(far.anger == farBefore + 8 - 3);
    s.vehicle(ours)->status = VehicleStatus::Mothballed;  // mothballed ships do nothing
    const int homeMid = home.anger;
    populationTurn(*r, s);
    CHECK(home.anger == homeMid - 3);

    // Other races resent their rulers; happier races calm faster.
    moon.population = {{kMe, 500}, {kThem, 500}};
    const int moonMid = moon.anger;
    populationTurn(*r, s);
    CHECK(moon.anger == moonMid + (-3 * 500 + 7 * 500) / 1000);
    setChar(s, kMe, Characteristic::Happiness, 200);
    const int homeCalm = home.anger;
    populationTurn(*r, s, {{kMe, "Any Ship Lost", {}, {}, 1}});
    CHECK(home.anger == homeCalm + 2 - 6);

    // Emotionless populations never change mood.
    for (uint32_t i = 0; i < r->data().racialTraits.size(); ++i)
        if (r->data().racialTraits[i].name == "Stoic") s.empire(kMe).race.traits.push_back(i);
    const int stoic = home.anger;
    populationTurn(*r, s, {{kMe, "New Treaty War", {}, {}, 1}});
    CHECK(home.anger == stoic);
}

TEST_CASE("economy: bad conditions anger, happiness facilities calm") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    s.vehicles.clear();
    Colony& home = plainHome(r, s, {});
    s.galaxy.object(home.planet).conditions = 30;  // Harsh
    home.anger = 400;
    populationTurn(r, s);
    CHECK(home.anger == 400 + 5);  // +15, clamped by the test model's +5 limit
    home.facilities = {facilityIndex(r, "Test Entertainment")};
    s.galaxy.object(home.planet).conditions = 90;
    populationTurn(r, s);
    CHECK(home.anger == 405 - 5);
}

TEST_CASE("economy: riots stop output and building; long riots end in rebellion") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    s.vehicles.clear();
    Colony& home = plainHome(r, s, {"Test Spaceport", "Test Mine"});
    home.anger = 800;
    populationTurn(r, s);
    CHECK(home.riotTurns == 1);
    CHECK(logged(s, kMe, "Riots on"));
    CHECK(economy::colonyOutput(r, s, home).production.isZero());
    home.anger = 350;
    populationTurn(r, s);
    CHECK(home.riotTurns == 0);

    const ObjectId farId = freePlanet(s, s.galaxy.object(home.planet).system, false);
    Colony& far = addColony(s, kMe, farId, 400);
    far.anger = 1000;
    far.riotTurns = 50;
    s.galaxy.object(farId).conditions = 0;
    const size_t empires = s.empires.size();
    for (int i = 0; i < 300 && s.colony(farId)->owner == kMe; ++i) populationTurn(r, s);
    const Colony& rebel = *s.colony(farId);
    REQUIRE(rebel.owner != kMe);
    REQUIRE(s.empires.size() == empires + 1);
    const Empire& rebels = s.empire(rebel.owner);
    CHECK(rebels.kind == PlayerKind::Computer);
    CHECK(rebels.alive);
    CHECK(rebel.population[0].race == rebels.id);
    CHECK(rebel.totalPopulation() >= 1);
    CHECK(rebel.anger < 750);
    for (const Empire& e : s.empires) CHECK(e.relations.size() == s.empires.size());
    CHECK(rebels.knowledge.explored.size() == s.galaxy.systems.size());
    CHECK(logged(s, kMe, "rebelled"));
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
    auto out = economy::colonyOutput(r, s, home);
    CHECK(out.production == Resources{840, 840, 0});
    CHECK(out.depletion == Resources{840, 840, 0});
    economyTurn(r, s);
    CHECK(planet.value[0] == 160);
    CHECK(planet.value[1] == 100000 - 840);
    CHECK(economy::colonyOutput(r, s, home).production[Resource::Minerals] == 160);
}

TEST_CASE("economy: remote mining takes from uncolonized planets") {
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
    CHECK(s.empire(kMe).economy.remoteMining == Resources{250 * 150 / 100, 0, 0});  // one extractor per location
    CHECK(s.galaxy.object(rock).value[0] == 149);
}

TEST_CASE("economy: yearly drift of planet value and conditions") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = plainHome(r, s, {"Test Climate Station"});
    s.galaxy.object(home.planet).conditions = 50;
    s.turn = 9;
    economyTurn(r, s);
    CHECK(s.galaxy.object(home.planet).conditions == 50);
    s.turn = 10;
    economyTurn(r, s);
    CHECK(s.galaxy.object(home.planet).conditions == 51);
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
    planet.value = {100, 98, 102};  // as observed; our generator gives every homeworld 100 %
    planet.conditions = 50;         // "Unpleasant"
    CHECK(economy::conditionsName(economy::conditionsBand(planet.conditions)) == "Unpleasant");

    CHECK(home.totalPopulation() == 2000);
    CHECK(maxPopulation(*r, s, home) == 2000);
    CHECK(home.facilities.size() == 15);
    CHECK(s.empire(kMe).stockpile == Resources{20000, 20000, 20000});
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
    CHECK(s.empire(kMe).stockpile == Resources{20000, 20000, 20000} + rep.colonies - rep.maintenance);
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
