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

#include <cstdlib>
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

TEST_CASE("events: severity, frequency and eligibility") {
    CHECK(events::parseSeverity("catastrophic") == events::Severity::Catastrophic);
    CHECK(events::parseSeverity("High") == events::Severity::High);
    CHECK(events::parseSeverity("Medium") == events::Severity::Medium);
    CHECK(events::parseSeverity("") == events::Severity::Low);

    auto r = rulesWith({event(Effect::PointsChange, -10, "Low"), event(Effect::PlanetPlague, 1, "Medium"),
                        event(Effect::WarpPointClosed, 1, "High"), event(Effect::StarDestroyed, 1, "Catastrophic"),
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
    s.options.maxEventSeverity = 0;
    CHECK(events::eligibleEvents(*r, s) == std::vector<uint32_t>{0});  // Points - Steal needs a source
    s.options.maxEventSeverity = 2;
    CHECK(events::eligibleEvents(*r, s) == std::vector<uint32_t>{0, 1, 2});
    s.options.maxEventSeverity = 3;
    CHECK(events::eligibleEvents(*r, s).size() == 4);

    CHECK(effects::isBad(Effect::PointsChange, -1));
    CHECK_FALSE(effects::isBad(Effect::PointsChange, 5));
    CHECK(effects::isBad(Effect::PlanetPopulationAngerChange, 5));
    CHECK_FALSE(effects::isBad(Effect::PlanetPlagueCured, 1));
    CHECK(effects::isBad(Effect::StarDestroyed, 1));
}

TEST_CASE("events: an immediate event changes the state and informs the owner") {
    auto r = rulesWith({event(Effect::PointsChange, -3000)});
    GameState s = newPoliticsGame();
    s.empire(kA).stockpile = {5000, 2000, 0};
    TurnContext ctx = context(*r, s);
    Rng rng(1);
    effects::Target t;
    t.empire = kA;
    REQUIRE(events::trigger(ctx, 0, t, rng));
    CHECK(s.empire(kA).stockpile == Resources{2000, 0, 0});
    const LogEntry* l = findLog(s, kA, "Omen");
    REQUIRE(l);
    CHECK(l->category == LogCategory::Events);
    CHECK(l->picture == "OmenPicture");
    CHECK(l->text == "At : 5000 (Realm 1 Union).");
    CHECK_FALSE(hasLog(s, kB, "Omen"));
}

TEST_CASE("events: timed events warn first and strike later") {
    auto r = rulesWith({event(Effect::PlanetPopulationChange, -100, "Medium", "Owner", 3)});
    GameState s = newPoliticsGame();
    const ObjectId home = homeworld(s, kA).planet;
    const int64_t pop = homeworld(s, kA).totalPopulation();
    TurnContext ctx = context(*r, s);
    Rng rng(1);
    REQUIRE(events::trigger(ctx, 0, {kA, {}, {}, {}, home, {}}, rng));
    REQUIRE(s.pendingEvents.size() == 1);
    CHECK(s.pendingEvents[0].fireTurn == 3);
    const LogEntry* warn = findLog(s, kA, "Foreboding");
    REQUIRE(warn);
    CHECK(warn->text == "Something stirs near " + s.galaxy.object(home).name + ".");
    CHECK(homeworld(s, kA).totalPopulation() == pop);

    for (uint32_t turn = 0; turn < 3; ++turn) {
        s.turn = turn;
        events::runEvents(ctx);
        CHECK(homeworld(s, kA).totalPopulation() == pop);
    }
    s.turn = 3;
    events::runEvents(ctx);
    CHECK(s.pendingEvents.empty());
    CHECK(homeworld(s, kA).totalPopulation() == pop - 100);
    CHECK(hasMood(ctx, kA, "1M Population Killed"));
    const LogEntry* done = findLog(s, kA, "Omen");
    REQUIRE(done);
    CHECK(done->text.find(": 100 (") != std::string::npos);

    // A timed event whose target is gone fizzles.
    REQUIRE(events::trigger(ctx, 0, {kA, {}, {}, {}, home, {}}, rng));
    s.colonies[home.index()].reset();
    s.turn = 10;
    const size_t logs = s.empire(kA).log.size();
    events::runEvents(ctx);
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
        auto r = rulesWith({event(Effect::PlanetConditionsChange, -1, "Low", to)});
        GameState copy = s;
        TurnContext ctx = context(*r, copy);
        Rng rng(1);
        REQUIRE(events::trigger(ctx, 0, {kA, {}, {}, {}, home, {}}, rng));
        const std::string_view name = to;
        INFO(name);
        CHECK(hasLog(copy, kA, "Omen") == (name != "None"));
        CHECK(hasLog(copy, kB, "Omen") == (name == "System" || name == "All"));
        CHECK(hasLog(copy, kC, "Omen") == (name == "All"));
    }
}

TEST_CASE("events: luck and bad-event abilities reduce bad events only") {
    const Rules& pr = politicsRules();
    auto bad = rulesWith({event(Effect::PointsChange, -1000)});
    auto good = rulesWith({event(Effect::PointsChange, 1000)});
    GameState s = newPoliticsGame();
    s.options.eventFrequency = 3;  // 100 % in the test settings
    s.empire(kA).race.traits.push_back(traitIndex(pr, "Test Charmed"));  // Luck -100
    s.empire(kA).stockpile = {50000, 50000, 50000};
    s.empire(kB).stockpile = {50000, 50000, 50000};
    for (int turn = 0; turn < 10; ++turn) {
        TurnContext ctx = context(*bad, s);
        events::runEvents(ctx);
    }
    CHECK(s.empire(kA).stockpile == Resources{50000, 50000, 50000});
    CHECK(s.empire(kB).stockpile == Resources{40000, 40000, 40000});

    TurnContext ctx = context(*good, s);
    events::runEvents(ctx);
    CHECK(s.empire(kA).stockpile == Resources{51000, 51000, 51000});

    // "Change Bad Event Chance - System" -100 % protects the planets of that system.
    auto planetary = rulesWith({event(Effect::PlanetConditionsChange, -10)});
    GameState t = newPoliticsGame();
    t.options.eventFrequency = 3;
    homeworld(t, kB).facilities.push_back(facilityIndex(pr, "Test Security Center"));
    const int condA = t.galaxy.object(homeworld(t, kA).planet).conditions;
    const int condB = t.galaxy.object(homeworld(t, kB).planet).conditions;
    TurnContext tctx = context(*planetary, t);
    events::runEvents(tctx);
    CHECK(t.galaxy.object(homeworld(t, kB).planet).conditions == condB);
    CHECK(t.galaxy.object(homeworld(t, kA).planet).conditions == std::max(0, condA - 10));
}

TEST_CASE("events: immunities") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    Rng rng(1);
    s.empire(kA).race.traits.push_back(traitIndex(r, "Test Machine Folk"));
    s.empire(kB).race.traits.push_back(traitIndex(r, "Test Stoics"));
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PlanetPlague, {kA, {}, {}, {}, {}, {}}, rng).has_value());
    CHECK(effects::pickTarget(r, s, Effect::PlanetPlague, {kB, {}, {}, {}, {}, {}}, rng).has_value());
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PlanetPopulationRiot, {kB, {}, {}, {}, {}, {}}, rng).has_value());
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PlanetPopulationAngerChange, {kB, {}, {}, {}, {}, {}}, rng).has_value());
    CHECK(effects::pickTarget(r, s, Effect::PlanetPopulationRiot, {kC, {}, {}, {}, {}, {}}, rng).has_value());

    // A clinic in the system prevents plagues up to its level.
    homeworld(s, kC).facilities.push_back(facilityIndex(r, "Test Clinic"));
    auto out = hit(s, Effect::PlanetPlague, {kC, {}, {}, {}, {}, {}}, 3);
    CHECK_FALSE(out.applied);
    out = hit(s, Effect::PlanetPlague, {kC, {}, {}, {}, {}, {}}, 4);
    CHECK(out.applied);
    CHECK(homeworld(s, kC).plagueLevel == 4);
    out = hit(s, Effect::PlanetPlagueCured, {kC, {}, {}, {}, {}, {}}, 1);
    CHECK(out.applied);
    CHECK(homeworld(s, kC).plagueLevel == 0);
}

TEST_CASE("events: ship effects") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const Location home = locationOf(s.galaxy, homeworld(s, kA).planet);
    const DesignId tank = addTestDesign(s, r, kA, "Tank", "Test Frigate", {"Test Bridge", "Test Armor Plate", "Test Life Support"});
    const VehicleId id = addTestVehicle(s, r, tank, home).id;
    auto target = [&] { return effects::Target{kA, {}, {}, id, {}, {}}; };

    // Armor soaks damage first.
    auto out = hit(s, Effect::ShipDamage, target(), 50);
    CHECK(out.actual == 50);
    CHECK(s.vehicle(id)->damage[1] == 40);
    CHECK(s.vehicle(id)->damage[0] + s.vehicle(id)->damage[2] == 10);
    CHECK(out.tokens.vehicleName == s.vehicle(id)->name);

    out = hit(s, Effect::ShipLoseMovement, target(), 2);
    CHECK(s.vehicle(id)->immobileUntil == s.turn + 3);
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

    out = hit(s, Effect::ShipOrdersChange, target(), 1);
    REQUIRE(s.vehicle(id)->orders.size() == 1);
    CHECK(s.vehicle(id)->orders[0].kind == OrderKind::MoveTo);
    CHECK(s.vehicle(id)->orders[0].location.system == home.system);

    out = hit(s, Effect::ShipMoved, target(), 2);
    CHECK(s.vehicle(id)->location.system != home.system);
    CHECK(s.vehicle(id)->orders.empty());
    CHECK(s.empire(kA).hasExplored(s.vehicle(id)->location.system));

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

TEST_CASE("events: planet effects") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const ObjectId home = homeworld(s, kA).planet;
    auto target = [&](ObjectId o) { return effects::Target{kA, {}, {}, {}, o, {}}; };

    SpaceObject& planet = s.galaxy.object(home);
    planet.conditions = 5;
    hit(s, Effect::PlanetConditionsChange, target(home), -8);
    CHECK(planet.conditions == 0);
    planet.value = {100, 155, 50};
    auto out = hit(s, Effect::PlanetValueChange, target(home), -20);
    CHECK(planet.value == std::array<int, 3>{80, 135, 40});  // Planet Value Low Percent 40 in the fixture
    hit(s, Effect::PlanetValueChange, target(home), 30);
    CHECK(planet.value == std::array<int, 3>{110, 160, 70});  // High Percent 160
    s.options.finiteResources = true;
    planet.value = {1000, 2000, 0};
    hit(s, Effect::PlanetValueChange, target(home), -10);
    CHECK(planet.value == std::array<int, 3>{900, 1800, 0});
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

    c.anger = 70;
    hit(s, Effect::PlanetPopulationAngerChange, target(home), 20);
    CHECK(c.anger == 80);  // whole percent; a homeworld is a capital, capped at 80 (spec 02 §4)
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

    // Rebellion without anyone to join: the colony is lost.
    const ObjectId other = secondColony(s, kA, 50);
    out = hit(s, Effect::PlanetPopulationRebel, target(other), 1);
    CHECK(out.applied);
    CHECK(s.colony(other) == nullptr);
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
    CHECK_FALSE(effects::pickTarget(r, s, Effect::ResearchDeleteProject, {kA, {}, {}, {}, {}, {}}, rng).has_value());

    s.empire(kA).intel = {{projectFor(Effect::ShipDamage), kB, {}, {}, {}, 700}};
    out = hit(s, Effect::IntelDeleteProject, {}, 1);
    CHECK(s.empire(kA).intel.empty());
}

TEST_CASE("events: stellar events") {
    const Rules& r = politicsRules();
    GameState s = newPoliticsGame();
    const SystemId homeSys = s.galaxy.object(homeworld(s, kA).planet).system;

    // Planet destroyed: it becomes an asteroid field and its colony is lost.
    const ObjectId other = secondColony(s, kA, 20);
    auto out = hit(s, Effect::PlanetDestroyed, {kA, {}, {}, {}, other, {}}, 1);
    CHECK(out.applied);
    CHECK(s.galaxy.object(other).kind == ObjectKind::Asteroids);
    CHECK(s.colony(other) == nullptr);
    Rng rng(1);
    CHECK_FALSE(effects::pickTarget(r, s, Effect::PlanetDestroyed, {kA, {}, {}, {}, homeworld(s, kA).planet, {}}, rng).has_value());

    // ...and a planet may form from an asteroid field.
    out = hit(s, Effect::PlanetCreated, {kA, {}, {}, {}, other, {}}, 1);
    CHECK(s.galaxy.object(other).kind == ObjectKind::Planet);

    // Stars and warp points may appear and vanish.
    const size_t objects = s.galaxy.objects.size();
    out = hit(s, Effect::StarCreated, {kA, {}, {}, {}, {}, homeSys}, 1);
    CHECK(s.galaxy.objects.size() == objects + 1);
    CHECK(s.galaxy.objects.back().kind == ObjectKind::Star);
    CHECK(s.colonies.size() == s.galaxy.objects.size());
    CHECK(out.tokens.starName == s.galaxy.objects.back().name);

    const size_t links = s.galaxy.neighbors(homeSys).size();
    out = hit(s, Effect::WarpPointOpened, {kA, {}, {}, {}, {}, homeSys}, 1);
    CHECK(s.galaxy.neighbors(homeSys).size() == links + 1);
    CHECK(s.colonies.size() == s.galaxy.objects.size());
    for (const Empire& e : s.empires) CHECK(e.knowledge.knownWarpLink.size() == s.galaxy.objects.size());
    const ObjectId newWp = s.galaxy.objects[s.galaxy.objects.size() - 2].id;
    const SystemId far = s.galaxy.object(s.galaxy.object(newWp).destination).system;
    out = hit(s, Effect::WarpPointClosed, {kA, {}, {}, {}, newWp, {}}, 1);
    CHECK(s.galaxy.neighbors(homeSys).size() == links);
    const auto farLinks = s.galaxy.neighbors(far);
    CHECK(std::find(farLinks.begin(), farLinks.end(), homeSys) == farLinks.end());

    // A star with no homeworld nearby may explode, taking everything with it.
    CHECK_FALSE(effects::pickTarget(r, s, Effect::StarDestroyed, {kA, {}, {}, {}, {}, {}}, rng).has_value());
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
    auto t = effects::pickTarget(r, s, Effect::StarDestroyed, {kA, {}, {}, {}, {}, {}}, rng);
    REQUIRE(t);
    CHECK(s.galaxy.object(t->object).system == away);
    out = effects::apply(ctx, Effect::StarDestroyed, *t, 1, rng);
    CHECK(out.applied);
    CHECK(s.galaxy.object(t->object).kind == ObjectKind::DestroyedStar);
    CHECK(s.galaxy.object(planet).kind == ObjectKind::Asteroids);
    CHECK(s.colony(planet) == nullptr);
    CHECK(s.vehicle(doomed)->count == 0);
    CHECK(hasMood(ctx, kA, "Any Planet Lost"));
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
        for (uint32_t turn = 0; turn < 25; ++turn) {
            s.turn = turn;
            TurnContext ctx = context(*r, s);
            events::runEvents(ctx);
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
    for (int turn = 0; turn < 5; ++turn) {
        for (Empire& e : s.empires) {
            e.economy.intelligence = 1000000;
            e.intel.clear();
            for (uint32_t p = 0; p < r->data().intelProjects.size() && e.intel.size() < 12; ++p)
                e.intel.push_back({p, EmpireId{(e.id.index() + 1) % s.empires.size()}, {}, {}, {}, 0});
        }
        TurnContext ctx = turnContext(*r, s);
        intel::runIntel(ctx);
        events::runEvents(ctx);
        ++s.turn;
    }
    CHECK(s.turn == 5);
}
