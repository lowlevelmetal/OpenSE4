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
    for (Empire& e : s.empires) std::fill(e.knowledge.explored.begin(), e.knowledge.explored.end(), uint8_t{1});
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

TEST_CASE("classic ui: planet survey and filters") {
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
        CHECK(matches(PlanetFilter::All, p));
        CHECK(matches(PlanetFilter::Asteroids, p) == (s.galaxy.object(p.id).kind == ObjectKind::Asteroids));
        CHECK(matches(PlanetFilter::Colonizable, p) == p.problem.empty());
        if (matches(PlanetFilter::ColonizableBreathable, p)) CHECK(p.breathable);
    }
    const PlanetInfo mine = find(all, myHome);
    CHECK(mine.own);
    CHECK(matches(PlanetFilter::AllColonies, mine));
    CHECK_FALSE(matches(PlanetFilter::EnemyColonies, mine));
    const PlanetInfo theirs = find(all, theirHome);
    CHECK(theirs.enemy);  // no treaty: we fight on contact
    CHECK_FALSE(theirs.ally);

    s.empire(kMe).relation(kOther).treaty = Treaty::NonAggression;
    all = surveyPlanets(r, s, kMe);
    CHECK(find(all, theirHome).ally);
    CHECK(matches(PlanetFilter::AllyColonies, find(all, theirHome)));

    // Planets in the other empire's home system are not "empty".
    for (const PlanetInfo& p : all)
        if (p.system == s.galaxy.object(theirHome).system) CHECK_FALSE(matches(PlanetFilter::ColonizableEmpty, p));

    // Unexplored systems are not listed.
    std::fill(s.empire(kMe).knowledge.explored.begin(), s.empire(kMe).knowledge.explored.end(), uint8_t{0});
    s.empire(kMe).knowledge.explored[s.galaxy.object(myHome).system.index()] = 1;
    for (const PlanetInfo& p : surveyPlanets(r, s, kMe)) CHECK(p.system == s.galaxy.object(myHome).system);
}

TEST_CASE("classic ui: Send Colony Ship picks an idle ship that can colonize") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 2, 12);
    exploreAll(s);
    const ObjectId rock = freePlanet(s, "Rock");
    const ObjectId ice = freePlanet(s, "Ice");

    const auto ship = findColonyShip(r, s, kMe, rock);
    REQUIRE(ship.has_value());
    CHECK(s.vehicle(*ship)->owner == kMe);
    CHECK(s.design(s.vehicle(*ship)->design).id == colonyShipDesign(s, r, kMe));
    CHECK_FALSE(findColonyShip(r, s, kMe, ice).has_value());  // no ice colony module

    // The nearer of two idle colony ships goes.
    const Location target = locationOf(s.galaxy, rock);
    Vehicle& near = addTestVehicle(s, r, colonyShipDesign(s, r, kMe), target);
    const VehicleId nearId = near.id;
    CHECK(findColonyShip(r, s, kMe, rock) == nearId);

    CHECK(apply(r, s, kMe, colonizeOrders(s, nearId, rock)).ok);
    CHECK(colonyShipEnroute(s, kMe, rock));
    CHECK_FALSE(colonyShipEnroute(s, kOther, rock));
    CHECK(findColonyShip(r, s, kMe, rock) == *ship);  // the busy ship is no longer idle
    for (const PlanetInfo& p : surveyPlanets(r, s, kMe)) CHECK(matches(PlanetFilter::ShipEnroute, p) == (p.id == rock));
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
    auto latest = facilityChoices(r, s.empire(kMe), true);
    CHECK(std::find(latest.begin(), latest.end(), mine) != latest.end());

    s.empire(kMe).techLevels[techArea(r, "Test Economics").index()] = 3;
    const auto upgrades = possibleUpgrades(r, s, kMe, home);
    REQUIRE(upgradesMine(upgrades));
    latest = facilityChoices(r, s.empire(kMe), true);
    CHECK(std::find(latest.begin(), latest.end(), mine) == latest.end());
    CHECK(std::find(latest.begin(), latest.end(), mine2) != latest.end());
    const auto everything = facilityChoices(r, s.empire(kMe), false);
    CHECK(std::find(everything.begin(), everything.end(), mine) != everything.end());

    // Once queued, the family is no longer offered.
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
    CHECK(queues[1].kind == QueueKind::Ship);
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
