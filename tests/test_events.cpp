// Random and timed events, the shared effect handlers and the message
// tokens (docs/spec/05 §4, §2.3).

#include "engine_fixture.hpp"
#include "politics_fixture.hpp"

#include "game/design.hpp"
#include "game/events.hpp"
#include "game/intel.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
using effects::Effect;

namespace {

const EmpireId kA{0u}, kB{1u}, kC{2u};

ruleset::EventType event(Effect e, int amount, std::string severity = "Low", std::string to = "Owner", int turns = 0) {
    ruleset::EventType ev;
    ev.type = std::string(effects::identifier(e));
    ev.severity = std::move(severity);
    ev.effectAmount = amount;
    ev.messageTo = std::move(to);
    ev.messages = {{"Omen", "At [%PlanetName][%SystemName]: [%ActualAmount] ([%SourceEmpireName])."}};
    ev.picture = "OmenPicture";
    ev.turnsToComplete = turns;
    if (turns > 0) ev.startMessages = {{"Foreboding", "Something stirs near [%PlanetName]."}};
    return ev;
}

// Rules whose Events.txt holds exactly `events` (the rest as politicsRules).
std::unique_ptr<Rules> rulesWith(std::vector<ruleset::EventType> events) {
    ruleset::Ruleset rs = buildPoliticsRuleset();
    rs.eventTypes = std::move(events);
    return std::make_unique<Rules>(std::move(rs));
}

TurnContext context(const Rules& r, GameState& s) { return turnContext(r, s); }

// The event step's timed events and new-event roll, with the date after this
// turn, as processTurn runs them (spec 05 §8 step 9).
void eventStep(TurnContext& ctx) {
    GameState& s = ctx.state;
    Rng rng = s.rng.fork();
    events::fireDueEvents(ctx, rng);
    events::rollNewEvent(ctx, s.turn + 1, rng);
    s.removeDeadVehicles();
}

effects::Target onEmpire(EmpireId e) {
    effects::Target t;
    t.empire = e;
    return t;
}
effects::Target onObject(EmpireId e, ObjectId o) {
    effects::Target t = onEmpire(e);
    t.object = o;
    return t;
}
effects::Target onShip(EmpireId e, VehicleId v) {
    effects::Target t = onEmpire(e);
    t.vehicle = v;
    return t;
}
effects::Target inSystem(EmpireId e, SystemId sys) {
    effects::Target t = onEmpire(e);
    t.system = sys;
    return t;
}

effects::Outcome hit(GameState& s, Effect e, effects::Target request, int amount, uint64_t seed = 1) {
    Rng rng(seed);
    if (!request.empire.valid()) request.empire = kA;
    auto t = effects::pickTarget(politicsRules(), s, e, request, rng);
    REQUIRE_MESSAGE(t.has_value(), effects::identifier(e));
    TurnContext ctx = context(politicsRules(), s);
    return effects::apply(ctx, e, *t, amount, rng);
}

// A planet (not the homeworld) in `empire`'s home system, colonized with `pop` M.
ObjectId secondColony(GameState& s, EmpireId empire, int64_t pop) {
    const SystemId sys = s.galaxy.object(homeworld(s, empire).planet).system;
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (s.galaxy.object(o).kind == ObjectKind::Planet && !s.colony(o)) {
            Colony c;
            c.planet = o;
            c.owner = empire;
            c.population = {{empire, pop}};
            s.colonies[o.index()] = c;
            return o;
        }
    FAIL("no free planet");
    return {};
}

} // namespace

TEST_CASE("events: message tokens") {
    effects::Tokens t;
    t.planetName = "Rho II";
    t.systemName = "Rho";
    t.targetEmpireName = "Realm Union";
    t.actualAmount = 42;
    CHECK(effects::substitute("[%PlanetName] in [%SYSTEMNAME]: [%ActualAmount]M", t) == "Rho II in Rho: 42M");
    CHECK(effects::substitute("[%VehicleName]gone", t) == "gone");  // known token, no value
    CHECK(effects::substitute("[%ModToken] stays", t) == "[%ModToken] stays");
    CHECK(effects::substitute("broken [%PlanetName", t) == "broken [%PlanetName");
    CHECK(effects::substitute("", t).empty());

    GameState s = newPoliticsGame();
    CHECK(effects::empireFullName(s.empire(kA)) == "Realm 1 Union");
    CHECK(effects::emperorFullName(s.empire(kA)) == "Speaker Leader 1");
    effects::Tokens names;
    effects::setEmpireTokens(names, s, kA, kB, kC);
    CHECK(names.sourceEmpireName == "Realm 1 Union");
    CHECK(names.targetEmperorName == "Speaker Leader 2");
    CHECK(names.otherEmpireName == "Realm 3 Union");
}

TEST_CASE("events: severity, frequency and the original's record pick") {
    CHECK(events::parseSeverity("catastrophic") == events::Severity::Catastrophic);
    CHECK(events::parseSeverity("High") == events::Severity::High);
    CHECK(events::parseSeverity("Medium") == events::Severity::Medium);
    CHECK(events::parseSeverity("") == events::Severity::Low);

    auto r = rulesWith({event(Effect::StarDestroyed, 1, "Catastrophic"), event(Effect::PlanetPlague, 1, "Medium"),
                        event(Effect::WarpPointClosed, 1, "High"), event(Effect::ShipDamage, 1, "Low"),
                        event(Effect::PointsSteal, 1, "Low")});
    GameState s = newPoliticsGame();
    s.options.eventFrequency = 0;
    CHECK(events::eventChance(*r, s) == 0);
    s.options.eventFrequency = 1;
    CHECK(events::eventChance(*r, s) == 5);
    s.options.eventFrequency = 2;
    CHECK(events::eventChance(*r, s) == 10);
    s.options.eventFrequency = 3;
    CHECK(events::eventChance(*r, s) == 100);

    // N counts the records the severity allows; the pick is among the first
    // N records of the file whatever their severity (a quirk of the original).
    s.options.maxEventSeverity = 0;
    CHECK(events::allowedRecordCount(*r, s) == 2);
    std::array<int, 5> picked{};
    Rng rng(8);
    for (int i = 0; i < 2000; ++i) ++picked[*events::pickRecord(*r, s, rng)];
    CHECK(picked[0] > 800);  // the Catastrophic first record
    CHECK(picked[1] > 800);
    CHECK(picked[2] + picked[3] + picked[4] == 0);
    s.options.maxEventSeverity = 3;
    CHECK(events::allowedRecordCount(*r, s) == 5);
    auto none = rulesWith({event(Effect::PlanetPlague, 1, "High")});
    s.options.maxEventSeverity = 1;
    CHECK_FALSE(events::pickRecord(*none, s, rng).has_value());

    // The target lists by type; the other types never fire.
    CHECK(events::targetKind(Effect::ShipMoved) == events::TargetKind::Ship);
    CHECK(events::targetKind(Effect::PlanetDestroyed) == events::TargetKind::Planet);
    CHECK(events::targetKind(Effect::PlanetPlagueCured) == events::TargetKind::Planet);
    CHECK(events::targetKind(Effect::PoliticsTreatyInfo) == events::TargetKind::Empire);
    CHECK(events::targetKind(Effect::StarDestroyed) == events::TargetKind::Star);
    CHECK(events::targetKind(Effect::WarpPointClosed) == events::TargetKind::WarpPoint);
    for (Effect e : {Effect::PointsChange, Effect::PlanetCreated, Effect::StarCreated, Effect::WarpPointOpened})
        CHECK(events::targetKind(e) == events::TargetKind::None);

    CHECK(effects::isBad(Effect::PointsChange, -1));
    CHECK_FALSE(effects::isBad(Effect::PointsChange, 5));
    CHECK(effects::isBad(Effect::PlanetPopulationAngerChange, 5));
    CHECK_FALSE(effects::isBad(Effect::PlanetPlagueCured, 1));
    CHECK(effects::isBad(Effect::StarDestroyed, 1));
}

TEST_CASE("events: one roll for the whole galaxy, none before 2402.0, types without targets never fire") {
    auto r = rulesWith({event(Effect::ShipExperienceChange, 1)});
    GameState s = newPoliticsGame();
    s.options.eventFrequency = 3;  // 100 % in the test settings
    auto omens = [&] {
        int n = 0;
        for (const Empire& e : s.empires)
            for (const LogEntry& l : e.log) n += l.title == "Omen" ? 1 : 0;
        return n;
    };
    Rng rng(2);
    for (uint32_t turn = 0; turn < 19; ++turn) {
        s.turn = turn;
        TurnContext ctx = context(*r, s);
        events::rollNewEvent(ctx, turn + 1, rng);
    }
    CHECK(omens() == 0);  // the first 19 turns: the date has not reached 2402.0
    for (uint32_t turn = 19; turn < 29; ++turn) {
        s.turn = turn;
        TurnContext ctx = context(*r, s);
        eventStep(ctx);
    }
    CHECK(omens() == 10);  // one event a turn for the whole galaxy, not one per empire

    auto never = rulesWith({event(Effect::PointsChange, -1000)});
    s.empire(kA).stockpile = {5000, 5000, 5000};
    for (uint32_t turn = 30; turn < 40; ++turn) {
        s.turn = turn;
        TurnContext ctx = context(*never, s);
        eventStep(ctx);
    }
    CHECK(s.empire(kA).stockpile == Resources{5000, 5000, 5000});
}

TEST_CASE("events: targets come from the whole galaxy") {
    auto r = rulesWith({event(Effect::PlanetValueChange, 5)});
    GameState s = newPoliticsGame();
    // Most planets belong to nobody, and they can be hit too.
    std::map<bool, int> hits;
    Rng rng(4);
    for (int i = 0; i < 300; ++i) {
        const auto t = events::pickEventTarget(*r, s, 0, rng);
        REQUIRE(t);
        REQUIRE(t->object.valid());
        ++hits[s.colony(t->object) != nullptr];
        if (const Colony* c = s.colony(t->object)) CHECK(t->empire == c->owner);
        else CHECK_FALSE(t->empire.valid());
    }
    CHECK(hits[false] > 0);
    CHECK(hits[true] > 0);
}

TEST_CASE("events: an immediate event changes the state and informs the owner") {
    auto r = rulesWith({event(Effect::PointsChange, -3000)});
    GameState s = newPoliticsGame();
    s.empire(kA).stockpile = {5000, 2000, 0};
    TurnContext ctx = context(*r, s);
    Rng rng(1);
    REQUIRE(events::trigger(ctx, 0, onEmpire(kA), rng));
    CHECK(s.empire(kA).stockpile == Resources{2000, 0, 0});
    const LogEntry* l = findLog(s, kA, "Omen");
    REQUIRE(l);
    CHECK(l->category == LogCategory::Events);
    CHECK(l->picture == "OmenPicture");
    CHECK(l->text == "At : 5000 (Realm 1 Union).");
    CHECK_FALSE(hasLog(s, kB, "Omen"));
}

TEST_CASE("events: timed events warn first and strike exactly N turns later") {
    auto r = rulesWith({event(Effect::PlanetPopulationChange, -100, "Medium", "Owner", 3)});
    GameState s = newPoliticsGame();
    const ObjectId home = homeworld(s, kA).planet;
    const int64_t pop = homeworld(s, kA).totalPopulation();
    TurnContext ctx = context(*r, s);
    Rng rng(1);
    REQUIRE(events::trigger(ctx, 0, onObject(kA, home), rng));
    REQUIRE(s.pendingEvents.size() == 1);
    CHECK(s.pendingEvents[0].fireTurn == 3);
    const LogEntry* warn = findLog(s, kA, "Foreboding");
    REQUIRE(warn);
    CHECK(warn->text == "Something stirs near " + s.galaxy.object(home).name + ".");
    CHECK(homeworld(s, kA).totalPopulation() == pop);

    for (uint32_t turn = 0; turn < 3; ++turn) {
        s.turn = turn;
        eventStep(ctx);
        CHECK(homeworld(s, kA).totalPopulation() == pop);
    }
    s.turn = 3;
    eventStep(ctx);
    CHECK(s.pendingEvents.empty());
    CHECK(homeworld(s, kA).totalPopulation() == pop - 100);
    CHECK(hasMood(ctx, kA, "1M Population Killed"));
    const LogEntry* done = findLog(s, kA, "Omen");
    REQUIRE(done);
    CHECK(done->text.find(": 100 (") != std::string::npos);

    // A timed event whose target no longer exists is dropped silently.
    auto shipRules = rulesWith({event(Effect::ShipDamage, 5, "Low", "Owner", 2)});
    TurnContext sctx = context(*shipRules, s);
    VehicleId ship;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kA) ship = v.id;
    REQUIRE(events::trigger(sctx, 0, onShip(kA, ship), rng));
    s.vehicle(ship)->count = 0;
    s.removeDeadVehicles();
    s.turn = 10;
    const size_t logs = s.empire(kA).log.size();
    eventStep(sctx);
    CHECK(s.pendingEvents.empty());
    CHECK(s.empire(kA).log.size() == logs);
}

TEST_CASE("events: who hears about an event") {
    GameState s = newPoliticsGame();
    const ObjectId home = homeworld(s, kA).planet;
    const Location there = locationOf(s.galaxy, home);
    // B has a ship in A's home system (another sector); C is elsewhere.
    for (Vehicle& v : s.vehicles)
        if (v.owner == kB) {
            v.location = {there.system, Sector{0, 0}};
            break;
        }
    for (const char* to : {"Owner", "Sector", "System", "All", "None"}) {
        auto r = rulesWith({event(Effect::PlanetValueChange, -1, "Low", to)});
        GameState copy = s;
        TurnContext ctx = context(*r, copy);
        Rng rng(1);
        REQUIRE(events::trigger(ctx, 0, onObject(kA, home), rng));
        const std::string_view name = to;
        INFO(name);
        CHECK(hasLog(copy, kA, "Omen") == (name != "None"));
        CHECK(hasLog(copy, kB, "Omen") == (name == "System" || name == "All"));
        CHECK(hasLog(copy, kC, "Omen") == (name == "All"));
    }
}

TEST_CASE("events: the luck roll applies to every event, good or bad") {
    const Rules& pr = politicsRules();
    auto r = rulesWith({event(Effect::PoliticsTreatyInfo, 1)});  // empire targets
    GameState s = newPoliticsGame();
    s.empire(kB).alive = false;
    s.empire(kC).alive = false;  // A is the only candidate
    auto passes = [&](uint64_t seed) {
        Rng rng(seed);
        int n = 0;
        for (int i = 0; i < 1000; ++i) n += events::pickEventTarget(*r, s, 0, rng).has_value() ? 1 : 0;
        return n;
    };
    // A normal race: a roll of 1–100 below 100, so 99 %.
    int n = passes(1);
    CHECK(n > 970);
    CHECK(n < 1000);
    // Luck −50: below 50, so 49 %.
    s.empire(kA).race.traits.push_back(traitIndex(pr, "Test Half Luck"));
    n = passes(2);
    CHECK(n > 430);
    CHECK(n < 550);
    // Luck −100 twice over: a total of 0 or less skips the roll.
    s.empire(kA).race.traits = {traitIndex(pr, "Test Charmed"), traitIndex(pr, "Test Charmed")};
    CHECK(passes(3) == 1000);
}

TEST_CASE("events: a star event rolls luck for every empire present, a total of 100 skipping it") {
    const Rules& pr = politicsRules();
    auto r = rulesWith({event(Effect::StarDestroyed, 1, "Low")});
    GameState s = newPoliticsGame();
    // Keep a single star: the one in A's home system.
    const SystemId homeA = s.galaxy.object(homeworld(s, kA).planet).system;
    for (StarSystem& sys : s.galaxy.systems)
        if (sys.id != homeA) std::erase_if(sys.objects, [&](ObjectId o) { return s.galaxy.object(o).kind == ObjectKind::Star; });
    auto passes = [&](uint64_t seed) {
        Rng rng(seed);
        int n = 0;
        for (int i = 0; i < 1000; ++i) n += events::pickEventTarget(*r, s, 0, rng).has_value() ? 1 : 0;
        return n;
    };
    CHECK(passes(1) == 1000);  // A's total is exactly 100: no roll
    s.empire(kA).race.traits.push_back(traitIndex(pr, "Test Half Luck"));
    const int n = passes(2);
    CHECK(n > 430);
    CHECK(n < 550);
    // High and Catastrophic star events spare the systems of home planets.
    auto high = rulesWith({event(Effect::StarDestroyed, 1, "High")});
    Rng rng(5);
    CHECK_FALSE(events::pickEventTarget(*high, s, 0, rng).has_value());
}

TEST_CASE("events: High and Catastrophic planet events spare homeworlds") {
    auto low = rulesWith({event(Effect::PlanetValueChange, 1, "Low")});
    auto high = rulesWith({event(Effect::PlanetValueChange, 1, "High")});
    GameState s = newPoliticsGame();
    int lowHomes = 0, highHomes = 0;
    Rng rng(6);
    for (int i = 0; i < 1500; ++i) {
        if (auto t = events::pickEventTarget(*low, s, 0, rng); t && s.colony(t->object) && s.colony(t->object)->homeworld) ++lowHomes;
        if (auto t = events::pickEventTarget(*high, s, 0, rng); t && s.colony(t->object) && s.colony(t->object)->homeworld) ++highHomes;
    }
    CHECK(lowHomes > 0);
    CHECK(highHomes == 0);
}

TEST_CASE("events: the bad-event ability counts only unowned objects and positive values") {
    const Rules& pr = politicsRules();
    auto r = rulesWith({event(Effect::PoliticsTreatyInfo, 1)});
    GameState s = newPoliticsGame();
    // A colony's facility belongs to an empire: it never counts.
    homeworld(s, kB).facilities.push_back(facilityIndex(pr, "Test Security Center"));
    const SystemId homeB = s.galaxy.object(homeworld(s, kB).planet).system;
    CHECK(effects::unownedChanceValue(s, homeB, AbilityKind::ChangeBadEventChanceSystem) == 0);
    // A stellar ability of a planet nobody owns does, when positive.
    ObjectId free;
    for (ObjectId o : s.galaxy.system(homeB).objects)
        if (!s.colony(o)) free = o;
    REQUIRE(free.valid());
    ruleset::Ability a;
    a.type = std::string(identifier(AbilityKind::ChangeBadEventChanceSystem));
    a.value1 = "-40";
    s.galaxy.object(free).abilities.push_back(a);
    CHECK(effects::unownedChanceValue(s, homeB, AbilityKind::ChangeBadEventChanceSystem) == 0);
    a.value1 = "60";
    s.galaxy.object(free).abilities.push_back(a);
    CHECK(effects::unownedChanceValue(s, homeB, AbilityKind::ChangeBadEventChanceSystem) == 60);
    (void)r;
}

TEST_CASE("events: immunities") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    Rng rng(1);
    s.empire(kA).race.traits.push_back(traitIndex(r, "Test Machine Folk"));
    s.empire(kB).race.traits.push_back(traitIndex(r, "Test Stoics"));
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PlanetPlague, onEmpire(kA), rng).has_value());
    CHECK(effects::pickTarget(r, s, Effect::PlanetPlague, onEmpire(kB), rng).has_value());
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PlanetPopulationRiot, onEmpire(kB), rng).has_value());
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PlanetPopulationAngerChange, onEmpire(kB), rng).has_value());
    CHECK(effects::pickTarget(r, s, Effect::PlanetPopulationRiot, onEmpire(kC), rng).has_value());

    // A clinic in the system prevents plagues up to its level.
    homeworld(s, kC).facilities.push_back(facilityIndex(r, "Test Clinic"));
    auto out = hit(s, Effect::PlanetPlague, onEmpire(kC), 3);
    CHECK_FALSE(out.applied);
    out = hit(s, Effect::PlanetPlague, onEmpire(kC), 4);
    CHECK(out.applied);
    CHECK(homeworld(s, kC).plagueLevel == 4);
    out = hit(s, Effect::PlanetPlagueCured, onEmpire(kC), 1);
    CHECK(out.applied);
    CHECK(homeworld(s, kC).plagueLevel == 0);
}

TEST_CASE("events: ship effects") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const Location home = locationOf(s.galaxy, homeworld(s, kA).planet);
    const DesignId tank = addTestDesign(s, r, kA, "Tank", "Test Frigate", {"Test Bridge", "Test Armor Plate", "Test Life Support"});
    const VehicleId id = addTestVehicle(s, r, tank, home).id;
    auto target = [&] { return onShip(kA, id); };

    // Armor soaks damage first.
    auto out = hit(s, Effect::ShipDamage, target(), 50);
    CHECK(out.actual == 50);
    CHECK(s.vehicle(id)->damage[1] == 40);
    CHECK(s.vehicle(id)->damage[0] + s.vehicle(id)->damage[2] == 10);
    CHECK(out.tokens.vehicleName == s.vehicle(id)->name);

    // This turn's movement points only.
    s.vehicle(id)->movement = 5;
    out = hit(s, Effect::ShipLoseMovement, target(), 2);
    CHECK(s.vehicle(id)->movement == 3);
    CHECK(s.vehicle(id)->immobileUntil <= s.turn);
    s.vehicle(id)->supply = 700;
    out = hit(s, Effect::ShipLoseSupply, target(), 1000);
    CHECK(out.actual == 700);
    CHECK(s.vehicle(id)->supply == 0);
    s.vehicle(id)->experience = 30;
    out = hit(s, Effect::ShipExperienceChange, target(), -50);
    CHECK(s.vehicle(id)->experience == 0);
    CHECK(out.actual == -30);

    s.vehicle(id)->cargo.population = {{kA, 10}};
    out = hit(s, Effect::ShipCargoDamage, target(), 1);  // "some": half
    CHECK(s.vehicle(id)->cargo.totalPopulation() == 5);

    // Orders Change: one order to move to a random system of the quadrant.
    std::vector<uint8_t> destinations(s.galaxy.systems.size(), 0);
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        out = hit(s, Effect::ShipOrdersChange, target(), 1, seed);
        REQUIRE(s.vehicle(id)->orders.size() == 1);
        CHECK(s.vehicle(id)->orders[0].kind == OrderKind::MoveTo);
        destinations[s.vehicle(id)->orders[0].location.system.index()] = 1;
    }
    CHECK(std::count(destinations.begin(), destinations.end(), uint8_t{1}) > 3);

    // Ship Moved: a random system anywhere, orders cleared; the amount is not used.
    std::fill(destinations.begin(), destinations.end(), uint8_t{0});
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        out = hit(s, Effect::ShipMoved, target(), 2, seed);
        CHECK(s.vehicle(id)->orders.empty());
        CHECK(s.empire(kA).hasExplored(s.vehicle(id)->location.system));
        destinations[s.vehicle(id)->location.system.index()] = 1;
    }
    CHECK(std::count(destinations.begin(), destinations.end(), uint8_t{1}) > 3);

    TurnContext ctx = context(r, s);
    Rng rng(3);
    auto t = effects::pickTarget(r, s, Effect::ShipDamage, target(), rng);
    REQUIRE(t);
    effects::apply(ctx, Effect::ShipDamage, *t, 100000, rng);
    CHECK(s.vehicle(id)->count == 0);
    CHECK(s.design(tank).lost == 1);
    CHECK(hasMood(ctx, kA, "Any Ship Lost"));
    s.removeDeadVehicles();
    CHECK(s.vehicle(id) == nullptr);
}

TEST_CASE("events: a damaged ship keeps only the supply and cargo it still has room for") {
    // Storage destroyed outside combat (events and sabotage strike after the
    // owner's end of turn) takes supply and cargo with it at once (spec 03 §7, §11).
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const Location home = locationOf(s.galaxy, homeworld(s, kA).planet);
    const DesignId hauler = addTestDesign(s, r, kA, "Hauler", "Test Frigate",
                                          {"Test Bridge", "Test Supply Pod", "Test Supply Pod", "Test Cargo Bay", "Test Cargo Bay"});
    const VehicleId id = addTestVehicle(s, r, hauler, home).id;
    auto intact = [&](size_t a, size_t b) { return int{entryIntact(r, s, *s.vehicle(id), a)} + int{entryIntact(r, s, *s.vehicle(id), b)}; };
    const int64_t mass = r.setting("Population Mass", 5);
    {
        Vehicle& v = *s.vehicle(id);
        v.damage[0] = entryStructure(r, s.design(hauler), 0);  // only the storage is left to hit
        v.supply = vehicleSupplyCapacity(r, s, v);
        v.cargo.population = {{kA, vehicleCargoCapacity(r, s, v) / mass}};
    }
    const int64_t fullSupply = s.vehicle(id)->supply;
    const int64_t fullCargo = s.vehicle(id)->cargo.totalPopulation();
    // Each 10-point hit wrecks one pod or bay; stop once one of each is gone.
    for (uint64_t seed = 1; (intact(1, 2) == 2 || intact(3, 4) == 2) && seed < 10; ++seed) hit(s, Effect::ShipDamage, onShip(kA, id), 10, seed);
    const Vehicle& v = *s.vehicle(id);
    REQUIRE(v.count > 0);
    REQUIRE(intact(1, 2) < 2);
    REQUIRE(intact(3, 4) < 2);
    CHECK(v.supply == vehicleSupplyCapacity(r, s, v));
    CHECK(v.supply < fullSupply);
    CHECK(cargoSpaceUsed(r, s, v.cargo) <= vehicleCargoCapacity(r, s, v));
    CHECK(v.cargo.totalPopulation() == vehicleCargoCapacity(r, s, v) / mass);
    CHECK(v.cargo.totalPopulation() < fullCargo);
}

TEST_CASE("events: planet effects") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const ObjectId home = homeworld(s, kA).planet;
    auto target = [&](ObjectId o) { return onObject(kA, o); };

    SpaceObject& planet = s.galaxy.object(home);
    // Conditions stay within the 0–1.5 scale (spec 02 §2).
    planet.conditions = Conditions::hundredths(5);
    hit(s, Effect::PlanetConditionsChange, target(home), -8);
    CHECK(planet.conditions == Conditions{});
    planet.conditions = Conditions::hundredths(140);
    hit(s, Effect::PlanetConditionsChange, target(home), 25);
    CHECK(planet.conditions == kOptimalConditions);
    // Each value changes by the amount, within Minimum/Maximum Planet Percent
    // Value (10 and 150 in the test settings).
    planet.value = {100, 145, 25};
    auto out = hit(s, Effect::PlanetValueChange, target(home), -20);
    CHECK(planet.value == std::array<int, 3>{80, 125, 10});
    hit(s, Effect::PlanetValueChange, target(home), 30);
    CHECK(planet.value == std::array<int, 3>{110, 150, 40});
    // Finite resources: the amount × 1,000 while that stays within ±500,000.
    s.options.finiteResources = true;
    planet.value = {50000, 20000, 0};
    hit(s, Effect::PlanetValueChange, target(home), -10);
    CHECK(planet.value == std::array<int, 3>{40000, 10000, 0});
    hit(s, Effect::PlanetValueChange, target(home), 600);
    CHECK(planet.value == std::array<int, 3>{40600, 10600, 600});
    s.options.finiteResources = false;

    Colony& c = homeworld(s, kA);
    c.population = {{kA, 300}, {kB, 100}};
    TurnContext ctx = context(r, s);
    Rng rng(5);
    auto t = effects::pickTarget(r, s, Effect::PlanetPopulationChange, target(home), rng);
    REQUIRE(t);
    out = effects::apply(ctx, Effect::PlanetPopulationChange, *t, -100, rng);
    CHECK(out.actual == 100);
    CHECK(c.population == std::vector<PopulationGroup>{{kA, 225}, {kB, 75}});
    CHECK(hasMood(ctx, kA, "1M Population Killed"));
    CHECK(ctx.moodEvents.back().count == 100);
    out = hit(s, Effect::PlanetPopulationChange, target(home), 1000000);
    CHECK(c.totalPopulation() == maxPopulation(r, s, c));

    // The amount is in tenths; anger is a whole percent, and a homeworld is a
    // capital, capped at 80 (spec 02 §4).
    c.anger = 40;
    hit(s, Effect::PlanetPopulationAngerChange, target(home), 25);
    CHECK(c.anger == 42);
    c.anger = 70;
    hit(s, Effect::PlanetPopulationAngerChange, target(home), 200);
    CHECK(c.anger == 80);
    c.anger = 10;
    hit(s, Effect::PlanetPopulationRiot, target(home), 1);
    CHECK(c.anger == 80);
    c.homeworld = false;
    hit(s, Effect::PlanetPopulationRiot, target(home), 1);
    CHECK(moodFromAnger(c.anger) == Mood::Rioting);
    c.homeworld = true;

    const size_t facilities = c.facilities.size();
    REQUIRE(facilities >= 2);
    out = hit(s, Effect::PlanetFacilityDamage, target(home), 2);
    CHECK(out.actual == 2);
    CHECK(c.facilities.size() == facilities - 2);
    CHECK_FALSE(out.tokens.facilityName.empty());

    c.cargo.population = {{kA, 8}};
    hit(s, Effect::PlanetCargoDamage, target(home), 1000);
    CHECK(c.cargo.empty());

    // As an event, a rebel colony breaks away as a new independent empire.
    const ObjectId other = secondColony(s, kA, 50);
    out = hit(s, Effect::PlanetPopulationRebel, target(other), 1);
    CHECK(out.applied);
    REQUIRE(s.empires.size() == 4);
    const EmpireId rebel{3u};
    REQUIRE(s.colony(other));
    CHECK(s.colony(other)->owner == rebel);
    CHECK(s.colony(other)->homeworld);
    CHECK(s.empire(rebel).alive);
    CHECK(s.empire(rebel).kind == PlayerKind::Computer);
    CHECK(s.empire(rebel).race.name == s.empire(kA).race.name);
    CHECK(s.empire(rebel).techLevels == s.empire(kA).techLevels);
    CHECK(s.empire(rebel).relation(kA).treaty == Treaty::War);
    CHECK(s.empire(kA).relation(rebel).contact);
    for (const Empire& e : s.empires) CHECK(e.relations.size() == s.empires.size());
    CHECK(s.empire(rebel).knowledge.explored.size() == s.galaxy.systems.size());

    // The new empire plays its turns like any other.
    std::vector<EmpireOrders> none;
    for (int turn = 0; turn < 3; ++turn) processTurn(r, s, none);
    CHECK(s.empires.size() == 4);

    // With 20 empires in the galaxy nothing happens.
    const ObjectId third = secondColony(s, kA, 40);
    while (s.empires.size() < effects::kMaxEmpires) {
        Empire extra = s.empire(kC);
        extra.id = EmpireId{s.empires.size()};
        extra.alive = false;
        s.empires.push_back(extra);
    }
    for (Empire& e : s.empires) e.relations.resize(s.empires.size());
    out = hit(s, Effect::PlanetPopulationRebel, target(third), 1);
    CHECK_FALSE(out.applied);
    CHECK(s.colony(third)->owner == kA);
}

TEST_CASE("events: points and projects") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    s.empire(kA).stockpile = {100, 5000, 0};
    auto out = hit(s, Effect::PointsChange, {}, -1000);
    CHECK(s.empire(kA).stockpile == Resources{0, 4000, 0});
    CHECK(out.actual == 1100);

    s.empire(kA).research = {{techArea(r, "Test Beams"), 1500}};
    out = hit(s, Effect::ResearchDeleteProject, {}, 1);
    CHECK(s.empire(kA).research.empty());
    CHECK(out.tokens.techName == "Test Beams");
    CHECK(out.actual == 1500);
    Rng rng(1);
    CHECK_FALSE(effects::pickTarget(r, s, Effect::ResearchDeleteProject, onEmpire(kA), rng).has_value());

    IntelProjectOrder probe;
    probe.project = projectFor(Effect::ShipDamage);
    probe.target = kB;
    probe.progress = 700;
    s.empire(kA).intel = {probe};
    out = hit(s, Effect::IntelDeleteProject, {}, 1);
    CHECK(s.empire(kA).intel.empty());
}

TEST_CASE("events: stellar events") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const SystemId homeSys = s.galaxy.object(homeworld(s, kA).planet).system;

    // Planet destroyed: it becomes an asteroid field and its colony is lost.
    const ObjectId other = secondColony(s, kA, 20);
    auto out = hit(s, Effect::PlanetDestroyed, onObject(kA, other), 1);
    CHECK(out.applied);
    CHECK(s.galaxy.object(other).kind == ObjectKind::Asteroids);
    CHECK(s.colony(other) == nullptr);
    Rng rng(1);
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PlanetDestroyed, onObject(kA, homeworld(s, kA).planet), rng).has_value());

    // ...and a planet may form from an asteroid field.
    out = hit(s, Effect::PlanetCreated, onObject(kA, other), 1);
    CHECK(s.galaxy.object(other).kind == ObjectKind::Planet);

    // Stars and warp points may appear and vanish.
    const size_t objects = s.galaxy.objects.size();
    out = hit(s, Effect::StarCreated, inSystem(kA, homeSys), 1);
    CHECK(s.galaxy.objects.size() == objects + 1);
    CHECK(s.galaxy.objects.back().kind == ObjectKind::Star);
    CHECK(s.colonies.size() == s.galaxy.objects.size());
    CHECK(out.tokens.starName == s.galaxy.objects.back().name);

    const size_t links = s.galaxy.neighbors(homeSys).size();
    out = hit(s, Effect::WarpPointOpened, inSystem(kA, homeSys), 1);
    CHECK(s.galaxy.neighbors(homeSys).size() == links + 1);
    CHECK(s.colonies.size() == s.galaxy.objects.size());
    for (const Empire& e : s.empires) CHECK(e.knowledge.knownWarpLink.size() == s.galaxy.objects.size());
    const ObjectId newWp = s.galaxy.objects[s.galaxy.objects.size() - 2].id;
    const SystemId far = s.galaxy.object(s.galaxy.object(newWp).destination).system;
    out = hit(s, Effect::WarpPointClosed, onObject(kA, newWp), 1);
    CHECK(s.galaxy.neighbors(homeSys).size() == links);
    const auto farLinks = s.galaxy.neighbors(far);
    CHECK(std::find(farLinks.begin(), farLinks.end(), homeSys) == farLinks.end());

    // A star with no homeworld nearby may explode, taking everything with it.
    CHECK_FALSE(effects::pickTarget(r, s, Effect::StarDestroyed, onEmpire(kA), rng).has_value());
    SystemId away;
    ObjectId star, planet;
    for (const StarSystem& sys : s.galaxy.systems) {
        if (sys.id == homeSys) continue;
        star = planet = ObjectId{};
        for (ObjectId o : sys.objects) {
            if (s.galaxy.object(o).kind == ObjectKind::Star) star = o;
            if (s.galaxy.object(o).kind == ObjectKind::Planet && !s.colony(o)) planet = o;
        }
        bool homes = false;
        for (ObjectId o : sys.objects)
            if (const Colony* c = s.colony(o); c && c->homeworld) homes = true;
        if (star.valid() && planet.valid() && !homes) {
            away = sys.id;
            break;
        }
    }
    REQUIRE(away.valid());
    Colony outpost;
    outpost.planet = planet;
    outpost.owner = kA;
    outpost.population = {{kA, 10}};
    s.colonies[planet.index()] = outpost;
    const DesignId scoutDesign = s.empire(kA).designs.front();
    const VehicleId doomed = addTestVehicle(s, r, scoutDesign, {away, Sector{1, 1}}).id;
    TurnContext ctx = context(r, s);
    auto t = effects::pickTarget(r, s, Effect::StarDestroyed, onEmpire(kA), rng);
    REQUIRE(t);
    CHECK(s.galaxy.object(t->object).system == away);
    const std::string planetName = s.galaxy.object(planet).name;
    const auto planetValue = s.galaxy.object(planet).value;
    const size_t warpPoints = s.galaxy.warpPoints(away).size();
    out = effects::apply(ctx, Effect::StarDestroyed, *t, 1, rng);
    CHECK(out.applied);
    // The shockwave: no star remains, planets become asteroid fields that keep
    // their name and values, colonies and vehicles are lost, warp points stay.
    for (ObjectId o : s.galaxy.system(away).objects) {
        const ObjectKind k = s.galaxy.object(o).kind;
        CHECK((k == ObjectKind::Asteroids || k == ObjectKind::WarpPoint));
    }
    CHECK(s.galaxy.object(planet).kind == ObjectKind::Asteroids);
    CHECK(s.galaxy.object(planet).name == planetName);
    CHECK(s.galaxy.object(planet).value == planetValue);
    CHECK(s.galaxy.warpPoints(away).size() == warpPoints);
    CHECK(s.colony(planet) == nullptr);
    CHECK(s.vehicle(doomed)->count == 0);
    CHECK(hasMood(ctx, kA, "Any Planet Lost"));
}

TEST_CASE("events: a colony that breaks away mid-turn becomes an empire that plays on") {
    ruleset::Ruleset rs = buildPoliticsRuleset();
    rs.eventTypes = {event(Effect::PlanetPopulationRebel, 1, "Low", "Owner")};
    // New colonies get people: a rebel whose only planet is an empty colony
    // (the computer players colonize early) is destroyed at once (spec 05 §6).
    rs.settings.set("Automatic Colonization Population", "10");
    auto r = std::make_unique<Rules>(std::move(rs));
    GameState s = newPoliticsGame(12);
    s.options.eventFrequency = 3;
    s.turn = 19;
    std::vector<EmpireOrders> none;
    for (int t = 0; t < 150 && s.empires.size() == 3; ++t) processTurn(*r, s, none);
    REQUIRE(s.empires.size() > 3);
    const EmpireId rebel{3u};
    CHECK(s.empire(rebel).alive);
    CHECK(hasLog(s, rebel, "First Contact"));
    // Founded in the event step, after every empire's end-of-turn processing:
    // its own processing starts next turn (spec 05 §8).
    const size_t recorded = s.empire(rebel).history.size();
    CHECK(recorded == 0);
    // The former owner is at war with it and may well crush it in the next
    // turns (its warships are close by); the turns go on either way.
    for (int t = 0; t < 3; ++t) processTurn(*r, s, none);
    for (const Empire& e : s.empires) CHECK(e.relations.size() == s.empires.size());
    if (s.empire(rebel).alive) CHECK(s.empire(rebel).history.size() >= recorded + 3);
}

TEST_CASE("events: rolling is deterministic") {
    std::vector<ruleset::EventType> all;
    for (size_t i = 0; i < static_cast<size_t>(Effect::Count); ++i) {
        const auto e = static_cast<Effect>(i);
        if (effects::needsSource(e) || e == Effect::IntelligenceDefense) continue;
        const bool timed = e == Effect::PlanetDestroyed || e == Effect::WarpPointClosed;
        all.push_back(event(e, e == Effect::PointsChange ? -500 : 2, "Low", "System", timed ? 2 : 0));
    }
    auto r = rulesWith(all);
    auto play = [&] {
        GameState s = newPoliticsGame(33, 3, 14);
        s.options.eventFrequency = 3;
        for (uint32_t turn = 0; turn < 100; ++turn) {
            s.turn = turn;
            TurnContext ctx = context(*r, s);
            eventStep(ctx);
        }
        std::vector<std::string> out;
        for (const Empire& e : s.empires)
            for (const LogEntry& l : e.log) out.push_back(l.title + "|" + l.text);
        out.push_back(std::to_string(s.galaxy.objects.size()) + "/" + std::to_string(s.vehicles.size()));
        return std::pair{out, s.rng};
    };
    const auto a = play();
    const auto b = play();
    CHECK(a.first.size() > 20);
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
}

namespace {

const Rules* installedRules() {
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

TEST_CASE("installed data set: every intelligence project and event type is implemented (opt-in)") {
    const Rules* r = installedRules();
    if (!r) return;
    CHECK_FALSE(r->data().intelProjects.empty());
    CHECK_FALSE(r->data().eventTypes.empty());
    for (const auto& p : r->data().intelProjects) CHECK_MESSAGE(effects::parseEffect(p.type).has_value(), p.type);
    for (const auto& e : r->data().eventTypes) CHECK_MESSAGE(effects::parseEffect(e.type).has_value(), e.type);

    // A few turns with frequent events and every project queued do not break anything.
    GameSetup setup;
    setup.seed = 99;
    setup.options.systemCount = 20;
    setup.options.eventFrequency = 3;
    setup.options.maxEventSeverity = 3;
    for (size_t i = 0; i < 3; ++i) {
        EmpireSetup e;
        e.preset = r->racePresets()[i % r->racePresets().size()].folder;
        setup.empires.push_back(e);
    }
    auto game = createGame(*r, setup);
    REQUIRE(game.has_value());
    GameState& s = *game;
    for (Empire& e : s.empires)
        for (Empire& o : s.empires)
            if (e.id != o.id) e.relation(o.id).contact = true;
    // New events start at 2402.0, so run past it.
    for (int turn = 0; turn < 26; ++turn) {
        for (Empire& e : s.empires) {
            if (!e.alive) continue;
            e.intelPool = 1000000;
            e.intel.clear();
            for (uint32_t p = 0; p < r->data().intelProjects.size() && e.intel.size() < 12; ++p) {
                IntelProjectOrder o;
                o.project = p;
                o.target = EmpireId{(e.id.index() + 1) % 3};
                e.intel.push_back(o);
            }
        }
        TurnContext ctx = turnContext(*r, s);
        for (size_t i = 0; i < s.empires.size(); ++i)
            if (s.empires[i].alive) intel::intelStep(ctx, EmpireId{i});
        eventStep(ctx);
        ++s.turn;
    }
    CHECK(s.turn == 26);
}
