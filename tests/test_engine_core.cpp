// Core engine model: setup, designs, commands and the turn pipeline.

#include "engine_fixture.hpp"

#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;

TEST_CASE("engine: abilities parse by identifier") {
    CHECK(parseAbilityKind("Standard Ship Movement") == AbilityKind::StandardShipMovement);
    CHECK(parseAbilityKind("standard  ship movement") == AbilityKind::StandardShipMovement);
    CHECK(parseAbilityKind("AI Tag 12") == AbilityKind::AITag);
    CHECK(parseAbilityKind("None") == std::nullopt);
    CHECK(parseAbilityKind("Something From A Mod") == AbilityKind::Unknown);
    CHECK(identifier(AbilityKind::SpaceYard) == "Space Yard");
}

TEST_CASE("engine: setup creates homeworlds, tech, designs and ships") {
    const Rules& r = engineRules();
    GameState s = newEngineGame(3, 3, 12);
    REQUIRE(s.empires.size() == 3);
    for (const Empire& e : s.empires) {
        const Colony& home = homeworld(s, e.id);
        CHECK(home.owner == e.id);
        CHECK(home.totalPopulation() > 0);
        CHECK(home.totalPopulation() == maxPopulation(r, s, home));
        CHECK_FALSE(home.facilities.empty());
        CHECK(colonyHasSpaceYard(r, home));
        // Starting Resources plus one turn of income (spec 02 §9).
        const EconomyReport& rep = e.economy;
        CHECK(e.stockpile == s.options.startingResources + rep.colonies + rep.trade + rep.tariffsIn + rep.remoteMining + rep.otherIncome);
        CHECK(e.stockpile[Resource::Minerals] > s.options.startingResources[Resource::Minerals]);
        CHECK(e.techLevel(techArea(r, "Test Construction")) == 1);
        CHECK(e.techLevel(techArea(r, "Test Rock Colonies")) == 1);  // home type colonization
        CHECK(e.techLevel(techArea(r, "Test Ice Colonies")) == 0);
        CHECK(e.techLevel(techArea(r, "Test Missiles")) == 0);       // requirement not met at Low start
        CHECK(e.techLevel(techArea(r, "Test Psionics")) == 0);       // racial area
        CHECK(e.relations.size() == 3);
        CHECK(e.hasExplored(s.galaxy.object(home.planet).system));
        CHECK(e.designs.size() >= 2);
        for (DesignId d : e.designs) {
            const DesignStats st = computeDesignStats(r, &e, s.design(d));
            CHECK_MESSAGE(st.problems.empty(), s.design(d).name << ": " << (st.problems.empty() ? "" : st.problems.front()));
        }
    }
    int ships = 0;
    for (const Vehicle& v : s.vehicles) {
        ++ships;
        CHECK(v.location == locationOf(s.galaxy, homeworld(s, v.owner).planet));
        CHECK(vehicleMaxMovement(r, s, v) > 0);
    }
    CHECK(ships == 3 * 3);  // 2 scouts + 1 colony ship each
}

TEST_CASE("engine: setup is deterministic") {
    GameState a = newEngineGame(11, 2, 10);
    GameState b = newEngineGame(11, 2, 10);
    REQUIRE(a.galaxy.objects.size() == b.galaxy.objects.size());
    CHECK(a.vehicles.size() == b.vehicles.size());
    for (size_t i = 0; i < a.colonies.size(); ++i) CHECK(a.colonies[i].has_value() == b.colonies[i].has_value());
    CHECK(a.rng == b.rng);
}

TEST_CASE("engine: medium and high tech starts") {
    const Rules& r = engineRules();
    GameOptions o;
    Race race;
    o.startTechLevel = 1;
    auto medium = startingTechLevels(r, o, race);
    CHECK(medium[techArea(r, "Test Physics").index()] == 2);
    CHECK(medium[techArea(r, "Test Missiles").index()] == 1);  // unlocked by Physics 2
    o.startTechLevel = 2;
    auto high = startingTechLevels(r, o, race);
    CHECK(high[techArea(r, "Test Beams").index()] == 10);
    CHECK(high[techArea(r, "Test Psionics").index()] == 0);
}

TEST_CASE("engine: design validation and movement") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const Empire& e = s.empires[0];
    Design d;
    d.owner = e.id;
    d.hull = hullIndex(r, "Test Frigate");
    auto add = [&](std::string_view c) { d.entries.push_back({componentIndex(r, c), -1}); };

    auto st = computeDesignStats(r, &e, d);
    CHECK_FALSE(st.problems.empty());  // no bridge, life support, crew
    add("Test Bridge");
    add("Test Life Support");
    add("Test Crew Quarters");
    add("Test Engine");
    add("Test Engine");
    add("Test Engine");
    st = computeDesignStats(r, &e, d);
    CHECK(st.problems.empty());
    CHECK(st.movement == 3);
    CHECK(st.tonnageUsed == 60);
    CHECK(st.cost == Resources{100 + 20 + 10 + 10 + 60, 20, 10 + 30});

    for (int i = 0; i < 4; ++i) add("Test Engine");
    st = computeDesignStats(r, &e, d);
    CHECK_FALSE(st.problems.empty());  // 7 engines > max 6

    d.entries.resize(6);
    add("Test Missile");  // not researched
    st = computeDesignStats(r, &e, d);
    CHECK_FALSE(st.problems.empty());
    CHECK(computeDesignStats(r, nullptr, d).problems.empty());  // no tech gate without an owner
}

TEST_CASE("engine: vehicle movement caps without supply or control") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const DesignId id = addTestDesign(s, r, EmpireId{0u}, "Racer", "Test Frigate",
                                      {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Supply Pod", "Test Engine",
                                       "Test Engine", "Test Engine", "Test Engine"});
    Vehicle& v = addTestVehicle(s, r, id, locationOf(s.galaxy, homeworld(s, EmpireId{0u}).planet));
    CHECK(vehicleMaxMovement(r, s, v) == 4);
    CHECK(v.supply == 500);
    v.supply = 0;
    CHECK(vehicleMaxMovement(r, s, v) == 1);
    v.supply = 100;
    v.damage[0] = 1000;  // bridge destroyed: movement is halved (spec 03 §6.1)
    CHECK_FALSE(vehicleHasControl(r, s, v));
    CHECK(vehicleMaxMovement(r, s, v) == 2);
    v.damage[0] = 0;
    v.damage[4] = 1000;  // one engine destroyed
    CHECK(vehicleMaxMovement(r, s, v) == 3);
}

TEST_CASE("engine: commands - designs, queues, fleets, orders") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const EmpireId them{1u};
    Colony& home = homeworld(s, me);

    Design d;
    d.name = "Warbird";
    d.hull = hullIndex(r, "Test Frigate");
    for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser", "Test Armor Plate"})
        d.entries.push_back({componentIndex(r, c), -1});
    REQUIRE(apply(r, s, me, cmd::CreateDesign{d}).ok);
    const DesignId warbird = s.empires[0].designs.back();
    CHECK(s.design(warbird).owner == me);
    CHECK(s.design(warbird).name == "Warbird");
    // Design names are unique in the whole game (spec 03 §4.1): a name taken
    // since the order was given gets the first free numeral; renaming refuses.
    REQUIRE(apply(r, s, them, cmd::CreateDesign{d}).ok);
    CHECK(s.design(s.empires[1].designs.back()).name == "Warbird II");
    CHECK(uniqueDesignName(s, "Warbird") == "Warbird III");
    CHECK(uniqueDesignName(s, "Nightjar") == "Nightjar");
    CHECK_FALSE(apply(r, s, me, cmd::Rename{{}, {}, warbird, {}, "Warbird II"}).ok);
    // A new design starts without statistics.
    Design used = d;
    used.name = "Veteran";
    used.built = 4;
    used.kills = 2;
    used.enemyTonnageDestroyed = 900;
    REQUIRE(apply(r, s, me, cmd::CreateDesign{used}).ok);
    CHECK(s.design(s.empires[0].designs.back()).enemyTonnageDestroyed == 0);
    CHECK(s.design(s.empires[0].designs.back()).built == 0);

    const cmd::QueueTarget q{home.planet, {}};
    QueueItem item;
    item.design = warbird;
    CHECK(apply(r, s, me, cmd::QueueAdd{q, item}).ok);
    CHECK(home.queue.items.size() == 1);
    CHECK_FALSE(apply(r, s, them, cmd::QueueAdd{q, item}).ok);  // not their planet

    QueueItem fac;
    fac.kind = QueueItem::Kind::Facility;
    fac.facility = facilityIndex(r, "Test Space Yard");
    CHECK_FALSE(apply(r, s, me, cmd::QueueAdd{q, fac}).ok);  // one yard per planet
    fac.facility = facilityIndex(r, "Test Lab");
    home.facilities.resize(home.facilities.size() - 2);  // homeworlds start full: make room
    const auto slotsLeft = facilitySlots(r, s, home) - static_cast<int>(home.facilities.size());
    for (int i = 0; i < slotsLeft; ++i) CHECK(apply(r, s, me, cmd::QueueAdd{q, fac}).ok);
    CHECK_FALSE(apply(r, s, me, cmd::QueueAdd{q, fac}).ok);  // no free slots

    CHECK(apply(r, s, me, cmd::QueueMove{q, 0, 1}).ok);
    CHECK(home.queue.items[1].kind == QueueItem::Kind::Vehicle);
    CHECK(apply(r, s, me, cmd::QueueRemove{q, 1}).ok);
    CHECK(apply(r, s, me, cmd::QueueFlags{q, false, false, true, -1}).ok);
    CHECK(home.queue.emergency);

    // Fleets.
    std::vector<VehicleId> mine;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == me) mine.push_back(v.id);
    REQUIRE(mine.size() >= 2);
    CHECK(apply(r, s, me, cmd::CreateFleet{"Home Guard", mine}).ok);
    REQUIRE(s.fleets.size() == 1);
    CHECK(s.vehicle(mine[0])->fleet == s.fleets[0].id);
    CHECK(apply(r, s, me, cmd::LeaveFleet{mine[0]}).ok);
    CHECK_FALSE(s.fleets[0].leader.valid());  // the choice is cleared: the first member leads (spec 03 §9)
    CHECK(apply(r, s, me, cmd::DisbandFleet{s.fleets[0].id}).ok);
    CHECK(s.fleets.empty());

    // Orders.
    Order o;
    o.kind = OrderKind::MoveTo;
    o.location = {SystemId{1u}, Sector{3, 3}};
    CHECK(apply(r, s, me, cmd::SetOrders{mine[0], {}, {o}, false}).ok);
    CHECK(s.vehicle(mine[0])->orders.size() == 1);
    o.location.sector = Sector{20, 3};
    CHECK_FALSE(apply(r, s, me, cmd::SetOrders{mine[0], {}, {o}, false}).ok);

    // Rename, research.
    CHECK(apply(r, s, me, cmd::Rename{mine[0], {}, {}, {}, "Pathfinder"}).ok);
    CHECK(s.vehicle(mine[0])->name == "Pathfinder");
    CHECK(apply(r, s, me, cmd::SetResearch{{{techArea(r, "Test Beams"), 0}}, true, false}).ok);
    CHECK_FALSE(apply(r, s, me, cmd::SetResearch{{{techArea(r, "Test Missiles"), 0}}, true, false}).ok);
}

TEST_CASE("engine: scrap refunds and retrofit keeps damage") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId a = addTestDesign(s, r, me, "A", "Test Frigate",
                                     {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser"});
    const DesignId b = addTestDesign(s, r, me, "B", "Test Frigate",
                                     {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Armor Plate"});
    Vehicle& v = addTestVehicle(s, r, a, home);
    v.damage[0] = 5;
    const VehicleId id = v.id;
    const Resources before = s.empires[0].stockpile;
    REQUIRE(apply(r, s, me, cmd::Retrofit{id, b}).ok);
    const Vehicle* after = s.vehicle(id);
    CHECK(after->design == b);
    CHECK(after->damage[0] == 5);
    CHECK(after->damage[4] == entryStructure(r, s.design(b), 4));  // added part starts destroyed
    const Resources paid = before - s.empires[0].stockpile;
    CHECK(paid == Resources{15 * 120 / 100 + 40 * 30 / 100, 0, 10 * 30 / 100});

    const Resources pre = s.empires[0].stockpile;
    REQUIRE(apply(r, s, me, cmd::Scrap{id}).ok);
    CHECK(s.vehicle(id) == nullptr);
    // round(cost × 30 %) per resource, in floating point (spec 03 §15).
    const Resources cost = computeDesignStats(r, nullptr, s.design(b)).cost;
    const Resources refund{xmath::pctRound(cost.v[0], 30), xmath::pctRound(cost.v[1], 30), xmath::pctRound(cost.v[2], 30)};
    CHECK(s.empires[0].stockpile == pre + refund);
}

TEST_CASE("engine: a scrapped facility refunds round(cost × %) in floating point") {
    // Spec 02 §6.6: round, not truncate: 109 × 30 % gives 33, where integer maths gave 32.
    ruleset::Ruleset data = buildEngineRuleset();
    for (auto& f : data.facilities)
        if (f.name == "Test Lab") f.cost = {109, 5, 1};
    const Rules r{std::move(data)};
    GameSetup setup;
    setup.seed = 7;
    setup.options.systemCount = 12;
    for (int i = 0; i < 2; ++i) {
        EmpireSetup e;
        e.name = std::format("Scrapper {}", i + 1);
        setup.empires.push_back(e);
    }
    auto g = createGame(r, setup);
    REQUIRE(g.has_value());
    GameState& s = *g;
    const EmpireId me{0u};
    Colony& home = homeworld(s, me);
    const uint32_t lab = facilityIndex(r, "Test Lab");
    const auto slot = std::find(home.facilities.begin(), home.facilities.end(), lab) - home.facilities.begin();
    REQUIRE(static_cast<size_t>(slot) < home.facilities.size());
    const int64_t pct = std::max<int64_t>(30, reclamationPercentAt(r, s, me, locationOf(s.galaxy, home.planet)));
    const Resources before = s.empires[0].stockpile;
    REQUIRE(apply(r, s, me, cmd::Scrap{{}, home.planet, static_cast<int32_t>(slot)}).ok);
    const Resources got = s.empires[0].stockpile - before;
    CHECK(got == Resources{xmath::pctRound(109, pct), xmath::pctRound(5, pct), xmath::pctRound(1, pct)});
    if (pct == 30) CHECK(got.v[0] == 33);
    // The Resources helpers: percent truncates and percentRounded rounds, both in extended precision.
    CHECK(Resources{300, 100, 90}.percent(21) == Resources{62, 21, 18});
    CHECK(Resources{109, 5, 3}.percentRounded(30) == Resources{xmath::pctRound(109, 30), xmath::pctRound(5, 30), xmath::pctRound(3, 30)});
    CHECK(Resources{109, 0, 0}.percentRounded(30).v[0] == 33);
}

TEST_CASE("engine: processTurn advances and rejects stale orders") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    EmpireOrders stale{EmpireId{0u}, 99, {cmd::SetResearch{}}};
    EmpireOrders ok{EmpireId{1u}, 0, {cmd::Rename{{}, {}, {}, {}, ""}}};
    std::vector<EmpireOrders> orders{stale, ok};
    const TurnResult res = processTurn(r, s, orders);
    CHECK(s.turn == 1);
    CHECK(res.rejected.size() == 2);  // stale list + invalid rename
}
