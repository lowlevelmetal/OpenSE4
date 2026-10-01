// The game logic behind the classic client's Planets, Colonies and
// construction-queue windows (src/client/classic/screens/colony_logic).

#include "engine_fixture.hpp"

#include "client/classic/screens/colony_logic.hpp"
#include "game/commands.hpp"
#include "game/query.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
using namespace opense4::client::classic;

namespace {

constexpr EmpireId kMe{0};
constexpr EmpireId kOther{1};

// The first uncolonized planet with this surface outside the empires' home systems.
ObjectId freePlanet(const GameState& s, std::string_view surface) {
    for (const SpaceObject& o : s.galaxy.objects)
        if (o.kind == ObjectKind::Planet && o.surface == surface && !s.colony(o.id)) return o.id;
    FAIL("no free " << surface << " planet");
    return {};
}

void exploreAll(GameState& s) {
    for (Empire& e : s.empires) {
        std::fill(e.knowledge.explored.begin(), e.knowledge.explored.end(), uint8_t{1});
        std::fill(e.knowledge.knownWarpLink.begin(), e.knowledge.knownWarpLink.end(), uint8_t{1});
    }
}

// A settled colony with population and no facilities on a free planet.
Colony& addColony(GameState& s, const Rules& r, ObjectId planet, EmpireId owner) {
    Colony c;
    c.planet = planet;
    c.owner = owner;
    c.colonyType = "Balanced";
    c.population.push_back({owner, 1});
    s.colonies[planet.index()] = c;
    Colony& col = *s.colonies[planet.index()];
    col.population.front().millions = std::max<int64_t>(1, maxPopulation(r, s, col));
    return col;
}

DesignId colonyShipDesign(const GameState& s, const Rules& r, EmpireId e) {
    for (DesignId d : s.empire(e).designs)
        if (computeDesignStats(r, nullptr, s.design(d)).canColonizeRock) return d;
    FAIL("no colony ship design");
    return {};
}

} // namespace

TEST_CASE("classic ui: colonization technology and game options") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    const ColonizeTech tech = colonizeTech(r, s.empire(kMe));
    CHECK(tech.rock);
    CHECK_FALSE(tech.ice);
    CHECK_FALSE(tech.gas);
    CHECK(tech.allows("rock"));
    CHECK_FALSE(tech.allows("Gas Giant"));

    const ObjectId rock = freePlanet(s, "Rock");
    const ObjectId ice = freePlanet(s, "Ice");
    CHECK(colonizeProblem(r, s, kMe, rock, tech).empty());
    CHECK_FALSE(colonizeProblem(r, s, kMe, ice, tech).empty());
    CHECK_FALSE(colonizeProblem(r, s, kMe, homeworld(s, kMe).planet, tech).empty());
    CHECK_FALSE(colonizeProblem(r, s, kMe, homeworld(s, kOther).planet, tech).empty());

    // Only breathable atmospheres.
    s.options.onlyBreathable = true;
    s.galaxy.object(rock).atmosphere = s.empire(kMe).race.atmosphere;
    CHECK(breathableBy(s, kMe, s.galaxy.object(rock)));
    CHECK(colonizeProblem(r, s, kMe, rock, tech).empty());
    s.galaxy.object(rock).atmosphere = "Some Other Gas";
    CHECK_FALSE(colonizeProblem(r, s, kMe, rock, tech).empty());
    s.options.onlyBreathable = false;

    // Only the home planet type, even with the technology for others.
    s.empire(kMe).techLevels[techArea(r, "Test Ice Colonies").index()] = 1;
    const ColonizeTech more = colonizeTech(r, s.empire(kMe));
    REQUIRE(more.ice);
    CHECK(colonizeProblem(r, s, kMe, ice, more).empty());
    s.options.onlyHomeType = true;
    CHECK_FALSE(colonizeProblem(r, s, kMe, ice, more).empty());
    CHECK(colonizeProblem(r, s, kMe, rock, more).empty());
}

TEST_CASE("classic ui: planet survey and the Planets tabs (spec 06 §1.8.1)") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 12);
    exploreAll(s);
    const ObjectId myHome = homeworld(s, kMe).planet;
    const ObjectId theirHome = homeworld(s, kOther).planet;

    auto find = [](const std::vector<PlanetInfo>& list, ObjectId id) {
        auto it = std::find_if(list.begin(), list.end(), [&](const PlanetInfo& p) { return p.id == id; });
        REQUIRE(it != list.end());
        return *it;
    };
    std::vector<PlanetInfo> all = surveyPlanets(r, s, kMe);
    size_t planets = 0;
    for (const SpaceObject& o : s.galaxy.objects) planets += o.kind == ObjectKind::Planet || o.kind == ObjectKind::Asteroids;
    CHECK(all.size() == planets);
    for (const PlanetInfo& p : all) {
        const bool asteroids = s.galaxy.object(p.id).kind == ObjectKind::Asteroids;
        CHECK(matches(PlanetFilter::All, p) == !asteroids);  // All lists no asteroid fields
        CHECK(matches(PlanetFilter::Asteroids, p) == asteroids);
        // Colonizable says nothing about whether the planet is colonized.
        CHECK(matches(PlanetFilter::Colonizable, p) == colonizableType(s, kMe, p.id, colonizeTech(r, s.empire(kMe))));
        CHECK(matches(PlanetFilter::ColonizableEmpty, p) == (p.colonizable && !p.colonized));
        if (matches(PlanetFilter::ColonizableBreathable, p)) CHECK((p.breathable && !p.colonized));
    }
    const PlanetInfo mine = find(all, myHome);
    CHECK(mine.own);
    CHECK(mine.colonizable);  // our own homeworld's type
    CHECK(matches(PlanetFilter::Colonizable, mine));
    CHECK_FALSE(matches(PlanetFilter::ColonizableEmpty, mine));
    CHECK(matches(PlanetFilter::AllColonies, mine));
    CHECK_FALSE(matches(PlanetFilter::EnemyColonies, mine));
    CHECK_FALSE(matches(PlanetFilter::AllyColonies, mine));

    // Not met yet: an enemy. Met at Non-Aggression or better: an ally; below it: an enemy.
    s.empire(kMe).relation(kOther).contact = false;
    s.empire(kMe).relation(kOther).treaty = Treaty::NonAggression;
    CHECK(find(surveyPlanets(r, s, kMe), theirHome).enemy);
    s.empire(kMe).relation(kOther).contact = true;
    all = surveyPlanets(r, s, kMe);
    CHECK(find(all, theirHome).ally);
    CHECK(matches(PlanetFilter::AllyColonies, find(all, theirHome)));
    for (const Treaty t : {Treaty::War, Treaty::NonIntercourse, Treaty::None}) {
        s.empire(kMe).relation(kOther).treaty = t;
        CHECK(matches(PlanetFilter::EnemyColonies, find(surveyPlanets(r, s, kMe), theirHome)));
    }
    for (const Treaty t : {Treaty::Subjugation, Treaty::Protectorate, Treaty::Partnership}) {
        s.empire(kMe).relation(kOther).treaty = t;
        CHECK(matches(PlanetFilter::AllyColonies, find(surveyPlanets(r, s, kMe), theirHome)));
    }

    // Coloniz\Empty tests the planet itself, not its system.
    for (const PlanetInfo& p : surveyPlanets(r, s, kMe))
        if (p.system == s.galaxy.object(theirHome).system && p.id != theirHome && p.colonizable)
            CHECK(matches(PlanetFilter::ColonizableEmpty, p));

    // Special: only Ancient Ruins or Ancient Ruins Unique.
    const ObjectId rock = freePlanet(s, "Rock");
    s.galaxy.object(rock).abilities = {ruleset::Ability{"Planet - Change Conditions", "", "1", ""}};
    CHECK_FALSE(find(surveyPlanets(r, s, kMe), rock).special);
    s.galaxy.object(rock).abilities.push_back(ruleset::Ability{"Ancient Ruins Unique", "", "1", ""});
    CHECK(matches(PlanetFilter::Special, find(surveyPlanets(r, s, kMe), rock)));

    // Systems To Avoid are marked (the No Sys To Avoid toggle hides them).
    s.empire(kMe).systemsToAvoid = {s.galaxy.object(rock).system};
    CHECK(find(surveyPlanets(r, s, kMe), rock).avoided);

    // Unexplored systems are not listed.
    std::fill(s.empire(kMe).knowledge.explored.begin(), s.empire(kMe).knowledge.explored.end(), uint8_t{0});
    s.empire(kMe).knowledge.explored[s.galaxy.object(myHome).system.index()] = 1;
    for (const PlanetInfo& p : surveyPlanets(r, s, kMe)) CHECK(p.system == s.galaxy.object(myHome).system);
}

TEST_CASE("classic ui: Send Colony Ship takes the available colony ship with the shortest route") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    exploreAll(s);
    const ObjectId rock = freePlanet(s, "Rock");
    const ObjectId ice = freePlanet(s, "Ice");
    const Location target = locationOf(s.galaxy, rock);

    const auto ships = colonyShips(r, s, kMe);
    REQUIRE_FALSE(ships.empty());
    const auto first = chooseColonyShip(r, s, kMe, rock);
    REQUIRE(first.has_value());
    CHECK(s.vehicle(*first)->owner == kMe);
    CHECK_FALSE(chooseColonyShip(r, s, kMe, ice).has_value());  // no ship can colonize ice

    // The nearer of two available ships goes.
    Vehicle& near = addTestVehicle(s, r, colonyShipDesign(s, r, kMe), target);
    const VehicleId nearId = near.id;
    near.supply = 100;
    CHECK(chooseColonyShip(r, s, kMe, rock) == nearId);
    // Out of supplies or mothballed: not available.
    s.vehicle(nearId)->supply = 0;
    CHECK(chooseColonyShip(r, s, kMe, rock) == first);
    s.vehicle(nearId)->supply = 100;
    s.vehicle(nearId)->status = VehicleStatus::Mothballed;
    CHECK(chooseColonyShip(r, s, kMe, rock) == first);
    s.vehicle(nearId)->status = VehicleStatus::Normal;
    // Turn-based: only ships with movement left.
    s.options.simultaneous = false;
    s.vehicle(nearId)->movement = 0;
    CHECK(chooseColonyShip(r, s, kMe, rock) != nearId);
    s.vehicle(nearId)->movement = 3;
    CHECK(chooseColonyShip(r, s, kMe, rock) == nearId);
    s.options.simultaneous = true;

    // Load Cargo (only while it carries no population), Move To, Colonize.
    const cmd::SetOrders orders = sendColonyShipOrders(s, nearId, rock);
    REQUIRE(orders.orders.size() == 3);
    CHECK(orders.orders[0].kind == OrderKind::LoadCargo);
    CHECK(orders.orders[0].location == s.vehicle(nearId)->location);
    CHECK_FALSE(orders.orders[0].design.valid());
    CHECK(orders.orders[1].kind == OrderKind::MoveTo);
    CHECK(orders.orders[1].location == target);
    CHECK(orders.orders[2].kind == OrderKind::Colonize);
    CHECK(orders.orders[2].object == rock);
    s.vehicle(nearId)->cargo.population.push_back({kMe, 1});
    CHECK(sendColonyShipOrders(s, nearId, rock).orders.size() == 2);
    s.vehicle(nearId)->cargo.population.clear();

    CHECK(apply(r, s, kMe, orders).ok);
    // A colony ship with orders is no longer available; its last Colonize order marks the planet.
    CHECK(chooseColonyShip(r, s, kMe, rock) == first);
    for (const PlanetInfo& p : surveyPlanets(r, s, kMe)) {
        CHECK(matches(PlanetFilter::ShipEnroute, p) == (p.id == rock));
        if (p.id == rock) CHECK(p.enrouteShip == s.vehicle(nearId)->name);
    }
    const PlanetStatistics st = planetStatistics(s, kMe, surveyPlanets(r, s, kMe), colonyShips(r, s, kMe));
    CHECK(st.colonyShips == int(ships.size()) + 1);
    CHECK(st.available == st.colonyShips - 1);
}

TEST_CASE("classic ui: the Planets statistics and the sort history") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 12);
    exploreAll(s);
    s.empire(kMe).relation(kOther).contact = true;
    s.empire(kMe).relation(kOther).treaty = Treaty::War;
    const std::vector<PlanetInfo> all = surveyPlanets(r, s, kMe);
    const PlanetStatistics st = planetStatistics(s, kMe, all, {});
    CHECK(st.systems == int(s.galaxy.systems.size()));
    int planets = 0, colonizable = 0, enemy = 0, free = 0;
    for (const PlanetInfo& p : all) {
        planets += !p.asteroids;
        colonizable += p.colonizable;
        enemy += p.colonizable && p.colonized && p.enemy;
        free += p.colonizable && !p.colonized;
    }
    CHECK(st.planets == planets);
    CHECK(st.colonizable == colonizable);
    CHECK(st.enemy == enemy);
    CHECK(st.ally == 0);
    CHECK(st.nonAligned == 0);
    CHECK(st.free == free);
    CHECK(st.freeBreathable <= st.free);
    CHECK(st.colonyShips == 0);

    // The latest click is the first key; earlier ones break ties; at most five.
    SortHistory h(1);
    CHECK(h.columns() == std::vector<int>{1});
    h.click(3);
    h.click(2);
    h.click(3);
    CHECK(h.columns() == std::vector<int>{3, 2, 1});
    for (int c : {4, 5, 6}) h.click(c);
    CHECK(h.columns() == std::vector<int>{6, 5, 4, 3, 2});
    struct Row {
        int a, b;
    };
    std::vector<Row> rows{{1, 2}, {0, 9}, {1, 1}, {0, 3}};
    SortHistory two(0);
    two.click(1);  // b highest first ...
    two.click(0);  // ... within a lowest first
    two.sort(rows, [](int column, const Row& x, const Row& y) {
        if (column == 0) return x.a == y.a ? 0 : x.a < y.a ? -1 : 1;
        return x.b == y.b ? 0 : x.b > y.b ? -1 : 1;
    });
    CHECK(rows[0].b == 9);
    CHECK(rows[1].b == 3);
    CHECK(rows[2].b == 2);
    CHECK(rows[3].b == 1);
    CHECK(compareNames("alpha", "Beta") < 0);
    CHECK(compareNames("Gamma", "gamma") == 0);
    CHECK(compareNames("Ab", "a") > 0);
}

TEST_CASE("classic ui: facility upgrades and choices") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    Colony& home = homeworld(s, kMe);
    const uint32_t mine = facilityIndex(r, "Test Mine");
    const uint32_t mine2 = facilityIndex(r, "Test Mine II");
    REQUIRE(std::count(home.facilities.begin(), home.facilities.end(), mine) > 0);

    auto upgradesMine = [&](const std::vector<QueueItem>& list) {
        return std::any_of(list.begin(), list.end(), [&](const QueueItem& q) { return q.kind == QueueItem::Kind::Upgrade && q.facility == mine2; });
    };
    CHECK_FALSE(upgradesMine(possibleUpgrades(r, s, kMe, home)));
    QueueItem queuedMine;
    queuedMine.kind = QueueItem::Kind::Facility;
    queuedMine.facility = mine;
    queuedMine.spent = {7, 0, 0};
    home.queue.items.insert(home.queue.items.begin(), queuedMine);
    CHECK(queuedFacilitySwitches(r, s, kMe, home).empty());
    auto latest = facilityChoices(r, s.empire(kMe), true);
    CHECK(std::find(latest.begin(), latest.end(), mine) != latest.end());

    s.empire(kMe).techLevels[techArea(r, "Test Economics").index()] = 3;
    const auto upgrades = possibleUpgrades(r, s, kMe, home);
    REQUIRE(upgradesMine(upgrades));
    // A queued older mine moves to the newest level in place (spec 02 §6.6).
    const auto switches = queuedFacilitySwitches(r, s, kMe, home);
    REQUIRE(switches.size() == 1);
    CHECK(switches[0] == std::pair<uint32_t, uint32_t>{0, mine2});
    REQUIRE(apply(r, s, kMe, cmd::QueueReplaceFacility{{home.planet, {}}, switches[0].first, switches[0].second}).ok);
    CHECK(homeworld(s, kMe).queue.items.front().facility == mine2);
    CHECK(homeworld(s, kMe).queue.items.front().spent == Resources{7, 0, 0});
    CHECK(queuedFacilitySwitches(r, s, kMe, homeworld(s, kMe)).empty());
    latest = facilityChoices(r, s.empire(kMe), true);
    CHECK(std::find(latest.begin(), latest.end(), mine) == latest.end());
    CHECK(std::find(latest.begin(), latest.end(), mine2) != latest.end());
    const auto everything = facilityChoices(r, s.empire(kMe), false);
    CHECK(std::find(everything.begin(), everything.end(), mine) != everything.end());

    // Each item carries the count the queue stores: every older mine here.
    for (const QueueItem& item : upgrades)
        if (item.facility == mine2) CHECK(item.count == std::count(home.facilities.begin(), home.facilities.end(), mine));
    // Once queued, that upgrade is no longer offered.
    for (const QueueItem& item : upgrades) {
        CHECK(queueItemProblem(r, s, kMe, cmd::QueueTarget{home.planet, {}}, item).empty());
        CHECK(apply(r, s, kMe, cmd::QueueAdd{{home.planet, {}}, item, -1}).ok);
    }
    CHECK(possibleUpgrades(r, s, kMe, homeworld(s, kMe)).empty());
}

TEST_CASE("classic ui: scrapping one facility type everywhere") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    const uint32_t mine = facilityIndex(r, "Test Mine");
    const Colony& home = homeworld(s, kMe);
    const size_t before = home.facilities.size();
    const auto mines = static_cast<size_t>(std::count(home.facilities.begin(), home.facilities.end(), mine));
    REQUIRE(mines > 1);

    const auto commands = scrapFacilityType(s, kMe, mine);
    REQUIRE(commands.size() == mines);
    for (size_t i = 1; i < commands.size(); ++i) CHECK(commands[i - 1].facilitySlot > commands[i].facilitySlot);
    CHECK(scrapFacilityType(s, kMe, mine, {homeworld(s, kOther).planet}).empty());
    for (const auto& c : commands) CHECK(apply(r, s, kMe, c).ok);
    const Colony& after = homeworld(s, kMe);
    CHECK(after.facilities.size() == before - mines);
    CHECK(std::count(after.facilities.begin(), after.facilities.end(), mine) == 0);
}

TEST_CASE("classic ui: queue build-time estimates") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    const cmd::QueueTarget target{homeworld(s, kMe).planet, {}};
    ConstructionQueue q;
    QueueItem a;
    a.kind = QueueItem::Kind::Facility;
    a.facility = facilityIndex(r, "Test Mine");
    QueueItem b = a;
    b.facility = facilityIndex(r, "Test Lab");
    q.items = {a, b};

    const Resources costA = displayCost(r, s, kMe, target, a);
    const Resources costB = displayCost(r, s, kMe, target, b);
    REQUIRE_FALSE(costA.isZero());
    REQUIRE_FALSE(costB.isZero());
    const Resources rate{100, 100, 100};
    auto turns = [&](const Resources& cost) {
        int t = 1;
        for (int64_t v : cost.v) t = std::max(t, static_cast<int>((v + 99) / 100));
        return t;
    };
    auto est = estimateQueue(r, s, kMe, target, q, rate);
    REQUIRE(est.size() == 2);
    CHECK(est[0].cost == costA);
    CHECK(est[0].turns == turns(costA));
    CHECK(est[1].turns == turns(costB));
    CHECK(est[1].doneIn == turns(costA) + turns(costB));
    CHECK(queueUsage(r, s, kMe, target, q, rate) == min(costA, rate));

    // Progress shortens the top item; no rate means never.
    q.items[0].spent = costA - Resources{50, 0, 0};
    est = estimateQueue(r, s, kMe, target, q, rate);
    CHECK(est[0].remaining == Resources{50, 0, 0});
    CHECK(est[0].turns == 1);
    CHECK(queueUsage(r, s, kMe, target, q, rate) == Resources{50, 0, 0});
    est = estimateQueue(r, s, kMe, target, q, Resources{});
    CHECK(est[0].turns == -1);
    CHECK(est[1].doneIn == -1);
    q.onHold = true;
    CHECK(queueUsage(r, s, kMe, target, q, rate).isZero());
}

TEST_CASE("classic ui: queue lists and item names") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    const Location home = locationOf(s.galaxy, homeworld(s, kMe).planet);
    const DesignId yardShip = addTestDesign(s, r, kMe, "Yard Tender", "Test Frigate",
                                            {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Yard Module"});
    const VehicleId tender = addTestVehicle(s, r, yardShip, home).id;

    const auto queues = empireQueues(r, s, kMe);
    REQUIRE(queues.size() == 2);
    CHECK(queues[0].kind == QueueKind::PlanetYard);
    CHECK(queues[0].target.planet == homeworld(s, kMe).planet);
    CHECK(queues[1].kind == QueueKind::ShipYard);
    CHECK(queues[1].target.vehicle == tender);
    CHECK(queueOf(s, kMe, queues[1].target) == &s.vehicle(tender)->queue);
    CHECK(queueOf(s, kOther, queues[1].target) == nullptr);

    QueueItem unit;
    unit.design = yardShip;
    unit.count = 3;
    CHECK(queueItemName(r, s, unit) == "Yard Tender x3");
    const auto ships = designChoices(r, s, kMe, false, false);
    CHECK(std::find(ships.begin(), ships.end(), yardShip) != ships.end());
    s.design(yardShip).obsolete = true;
    const auto current = designChoices(r, s, kMe, false, true);
    CHECK(std::find(current.begin(), current.end(), yardShip) == current.end());
    CHECK(designChoices(r, s, kMe, true, false).empty() == std::none_of(s.empire(kMe).designs.begin(), s.empire(kMe).designs.end(),
                                                                          [&](DesignId d) { return isUnitDesign(r, s.design(d)); }));
}

TEST_CASE("classic ui: Construction Queues toggles follow a working space yard (spec 06 §1.8.2)") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    const Location home = locationOf(s.galaxy, homeworld(s, kMe).planet);
    const DesignId yardShip = addTestDesign(s, r, kMe, "Yard Tender", "Test Frigate",
                                            {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Yard Module"});
    const VehicleId tender = addTestVehicle(s, r, yardShip, home).id;
    auto kindOf = [&](const cmd::QueueTarget& t) {
        for (const QueueEntry& q : empireQueues(r, s, kMe))
            if (sameTarget(q.target, t)) return std::optional<QueueKind>(q.kind);
        return std::optional<QueueKind>{};
    };
    const cmd::QueueTarget ship{{}, tender};
    const cmd::QueueTarget planet{homeworld(s, kMe).planet, {}};
    CHECK(kindOf(ship) == QueueKind::ShipYard);
    CHECK(kindOf(planet) == QueueKind::PlanetYard);
    CHECK(queueCanBuild(r, s, kMe, ship, false));
    CHECK(queueCanBuild(r, s, kMe, ship, true));
    CHECK(queueCanBuild(r, s, kMe, planet, false));
    // A cloaked yard does not work: the queue moves to Ships.
    s.vehicle(tender)->status = VehicleStatus::Cloaked;
    CHECK(kindOf(ship) == QueueKind::Ship);
    CHECK_FALSE(workingVehicleYard(r, s, *s.vehicle(tender)));
    CHECK_FALSE(queueCanBuild(r, s, kMe, ship, false));
    s.vehicle(tender)->status = VehicleStatus::Normal;
    // A colony without a space yard is under Planets; it builds units, not ships.
    Colony& col = homeworld(s, kMe);
    const auto facilities = col.facilities;
    std::erase_if(col.facilities, [&](uint32_t f) { return game::hasAbility(r.facilityAbilities(f), AbilityKind::SpaceYard); });
    CHECK(kindOf(planet) == QueueKind::Planet);
    CHECK_FALSE(queueCanBuild(r, s, kMe, planet, false));
    CHECK(queueCanBuild(r, s, kMe, planet, true));
    col.facilities = facilities;
    // A ship without a yard part and with an empty queue has no queue.
    const DesignId plain = addTestDesign(s, r, kMe, "Plain", "Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
    const VehicleId plainShip = addTestVehicle(s, r, plain, home).id;
    CHECK_FALSE(kindOf(cmd::QueueTarget{{}, plainShip}).has_value());
    // The bits of the stored toggles: Ships, Planets, Ship SY, Planet SY.
    CHECK(queueKindBit(QueueKind::Ship) == 1);
    CHECK(queueKindBit(QueueKind::Planet) == 2);
    CHECK(queueKindBit(QueueKind::ShipYard) == 4);
    CHECK(queueKindBit(QueueKind::PlanetYard) == 8);
}

TEST_CASE("classic ui: queue times in years, mode notes, Multi-Add and similar abilities") {
    CHECK(queueYearsText(0) == "0.0 Years");
    CHECK(queueYearsText(3) == "0.3 Years");
    CHECK(queueYearsText(25) == "2.5 Years");
    CHECK(queueYearsText(-1) == "Never");
    CHECK(queueYearsText(kNeverTurns) == "Never");
    CHECK(queueYearsText(kNeverTurns - 1) == "999.8 Years");

    ConstructionQueue q;
    CHECK(queueModeNote(q).empty());
    q.slowTurns = 4;
    CHECK(queueModeNote(q) == "Slow (4 turns left)");
    q.emergency = true;
    q.emergencyTurns = 2;
    CHECK(queueModeNote(q) == "Emergency (2 of 10 turns)");

    // Every placed item, in order and with its count, goes to each tagged queue.
    const std::vector<cmd::QueueTarget> tagged{{ObjectId{1u}, {}}, {{}, VehicleId{2u}}};
    QueueItem a;
    a.design = DesignId{3u};
    a.count = 5;
    a.spent = Resources{1, 2, 3};
    QueueItem b;
    b.kind = QueueItem::Kind::Facility;
    b.facility = 4;
    const auto adds = multiAddCommands(tagged, {a, b});
    REQUIRE(adds.size() == 4);
    CHECK(sameTarget(adds[0].target, tagged[0]));
    CHECK(adds[0].item.design == DesignId{3u});
    CHECK(adds[0].item.count == 5);
    CHECK(adds[0].item.spent.isZero());
    CHECK(adds[1].item.facility == 4u);
    CHECK(sameTarget(adds[2].target, tagged[1]));
    CHECK(adds[3].position == -1);
    CHECK(multiAddCommands({}, {a}).empty());

    // A system-wide ability another colony of ours in the system already has.
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    const ObjectId homePlanet = homeworld(s, kMe).planet;
    const uint32_t medical = facilityIndex(r, "Test Medical Lab");
    const uint32_t mine = facilityIndex(r, "Test Mine");
    CHECK(similarSystemAbilities(r, s, kMe, homePlanet, medical).empty());
    ObjectId neighbour;
    for (ObjectId id : s.galaxy.system(s.galaxy.object(homePlanet).system).objects)
        if (id != homePlanet && s.galaxy.object(id).kind == ObjectKind::Planet && !s.colony(id)) neighbour = id;
    REQUIRE(neighbour.valid());
    addColony(s, r, neighbour, kMe).facilities.push_back(medical);
    const auto same = similarSystemAbilities(r, s, kMe, homePlanet, medical);
    REQUIRE(same.size() == 1);
    CHECK(same.front() == "Plague Prevention - System");
    CHECK(similarSystemAbilities(r, s, kMe, homePlanet, mine).empty());  // not system-wide
    CHECK(similarSystemAbilities(r, s, kOther, homePlanet, medical).empty());  // not their colony
}

TEST_CASE("classic ui: status icons and this turn's orders") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    Colony& home = homeworld(s, kMe);
    auto has = [](const std::vector<int>& icons, int n) { return std::find(icons.begin(), icons.end(), n) != icons.end(); };
    auto icons = colonyStatusIcons(r, s, home, true);
    CHECK(has(icons, 1));    // space yard
    CHECK_FALSE(has(icons, 13));
    CHECK_FALSE(has(icons, 31));
    home.minister = true;
    QueueItem item;
    item.kind = QueueItem::Kind::Facility;
    item.facility = facilityIndex(r, "Test Mine");
    home.queue.items.push_back(item);
    icons = colonyStatusIcons(r, s, home, false);
    CHECK(has(icons, 11));   // minister
    CHECK(has(icons, 13));   // building
    CHECK(has(icons, 31));   // not connected

    const ObjectId p = home.planet;
    const ObjectId other = homeworld(s, kOther).planet;
    const std::vector<Command> orders{cmd::QueueAdd{{p, {}}, item, -1}, cmd::QueueAdd{{p, {}}, item, -1}, cmd::SetColonyType{p, "Mining"},
                                      cmd::QueueAdd{{other, {}}, item, -1}, cmd::SetResearch{}};
    const auto summary = planetOrders(orders, p);
    REQUIRE(summary.size() == 2);
    CHECK(summary[0] == "Queued an item (x2)");
    CHECK(summary[1] == "Colony type: Mining");
    CHECK(planetOrders(orders, other).size() == 1);
}

TEST_CASE("classic ui: queue types (templates)") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    const ObjectId rock = freePlanet(s, "Rock");
    Colony& col = addColony(s, r, rock, kMe);
    const cmd::QueueTarget target{rock, {}};
    const int free = freeFacilitySlots(r, s, col);
    REQUIRE(free > 0);

    // Built-in: fill the free slots with the best mineral facility.
    const auto builtIns = builtInQueueTemplates();
    REQUIRE_FALSE(builtIns.empty());
    auto items = resolveTemplate(r, s, kMe, target, builtIns.front());
    CHECK(static_cast<int>(items.size()) == free);
    for (const QueueItem& it : items) CHECK(hasAbility(r.facilityAbilities(it.facility), AbilityKind::ResourceGenMinerals));
    CHECK(resolveTemplate(r, s, kMe, cmd::QueueTarget{{}, VehicleId{0}}, builtIns.front()).empty());  // not a planet

    // A saved queue: runs of one facility merge; names survive a round trip.
    ConstructionQueue q;
    QueueItem mine;
    mine.kind = QueueItem::Kind::Facility;
    mine.facility = facilityIndex(r, "Test Mine");
    QueueItem ship;
    ship.design = colonyShipDesign(s, r, kMe);
    ship.count = 2;
    q.items = {mine, mine, ship};
    const QueueTemplate saved = templateFromQueue(r, s, "Frontier", q);
    REQUIRE(saved.entries.size() == 2);
    CHECK(saved.entries[0].count == 2);
    CHECK(saved.entries[1].kind == QueueItem::Kind::Vehicle);
    const auto parsed = parseTemplates("# comment\n\nnonsense line\n" + serializeTemplates({builtIns.front(), saved}));
    REQUIRE(parsed.size() == 1);  // built-ins are not saved
    CHECK(parsed[0].name == "Frontier");
    REQUIRE(parsed[0].entries.size() == 2);
    CHECK(parsed[0].entries[0].name == "Test Mine");
    CHECK(parsed[0].entries[0].count == 2);
    CHECK(parsed[0].entries[1].name == s.design(ship.design).name);
    CHECK(parsed[0].entries[1].count == 2);

    // Facilities resolve to the newest researched level of their family.
    s.empire(kMe).techLevels[techArea(r, "Test Economics").index()] = 3;
    items = resolveTemplate(r, s, kMe, target, parsed[0]);
    REQUIRE(items.size() == 3);
    CHECK(items[0].facility == facilityIndex(r, "Test Mine II"));
    CHECK(items[2].kind == QueueItem::Kind::Vehicle);
    CHECK(items[2].count == 2);
}

TEST_CASE("classic ui: the Planets window leaves out a cloaked colony's planet it cannot see, and lists it as a colony when it can (spec 01 §6.9)") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(5, 2, 12);
    exploreAll(s);
    const ObjectId theirHome = homeworld(s, kOther).planet;
    auto listed = [&]() {
        const auto all = surveyPlanets(r, s, kMe);
        const auto it = std::find_if(all.begin(), all.end(), [&](const PlanetInfo& p) { return p.id == theirHome; });
        return it == all.end() ? std::optional<PlanetInfo>{} : std::optional<PlanetInfo>{*it};
    };
    REQUIRE(listed());
    Colony& theirs = homeworld(s, kOther);
    theirs.cloakLevels.fill(3);
    theirs.cloaked = true;
    CHECK_FALSE(listed());  // our sensors (none there, or level 1) do not reach level 3
    // Sensors that pierce the cloak show it again, as a colony.
    const Location at = locationOf(s.galaxy, theirHome);
    const DesignId eye = addTestDesign(s, r, kMe, "Eye", "Test Frigate", {"Test Bridge", "Test Sensor"});
    theirs.cloakLevels.fill(2);  // the fixture's Test Sensor gives EM Active 2
    addTestVehicle(s, r, eye, at);
    const auto seen = listed();
    REQUIRE(seen);
    CHECK(seen->colonized);
    // Our own cloaked colony is always listed.
    Colony& mine = homeworld(s, kMe);
    mine.cloakLevels.fill(3);
    mine.cloaked = true;
    const auto all = surveyPlanets(r, s, kMe);
    CHECK(std::any_of(all.begin(), all.end(), [&](const PlanetInfo& p) { return p.id == mine.planet && p.own; }));
}
