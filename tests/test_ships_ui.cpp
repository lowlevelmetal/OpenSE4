// Rules-side helpers of the classic client's ship windows
// (src/client/classic/screens/ships_logic.hpp), on our own test rules.

#include "engine_fixture.hpp"

#include "client/classic/screens/ships_logic.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/xmath.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
namespace shipui = opense4::client::classic::shipui;

namespace {

const std::initializer_list<std::string_view> kHullBasics{"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"};

DesignId frigate(GameState& s, const Rules& r, EmpireId owner, std::string_view name, std::initializer_list<std::string_view> extra) {
    Design d;
    d.owner = owner;
    d.name = std::string(name);
    d.hull = hullIndex(r, "Test Frigate");
    for (auto c : kHullBasics) d.entries.push_back({componentIndex(r, c), -1});
    for (auto c : extra) d.entries.push_back({componentIndex(r, c), -1});
    return addDesign(s, std::move(d));
}

} // namespace

TEST_CASE("ship windows: immediate orders run first, in the order given") {
    const Location here{SystemId{0u}, Sector{3, 3}};
    const Location there{SystemId{0u}, Sector{5, 5}};
    std::vector<Order> orders{Order{OrderKind::MoveTo, there}, Order{OrderKind::Explore}};
    Order launch{OrderKind::LaunchUnits, here};
    launch.amount = 5;
    shipui::insertImmediate(orders, launch, here);
    Order recover{OrderKind::RecoverUnits, here};
    shipui::insertImmediate(orders, recover, here);
    REQUIRE(orders.size() == 4);
    CHECK(orders[0] == launch);
    CHECK(orders[1] == recover);
    CHECK(orders[2].kind == OrderKind::MoveTo);

    // A remote launch elsewhere is not "here": a new immediate order goes before it.
    std::vector<Order> remote{Order{OrderKind::LaunchUnits, there}};
    shipui::insertImmediate(remote, launch, here);
    CHECK(remote.front() == launch);
}

TEST_CASE("ship windows: moving orders and transfer steps") {
    std::vector<Order> orders{Order{OrderKind::Explore}, Order{OrderKind::Sentry}, Order{OrderKind::Resupply}};
    CHECK(shipui::moveOrder(orders, 2, -1) == 1);
    CHECK(orders[1].kind == OrderKind::Resupply);
    CHECK(shipui::moveOrder(orders, 0, -1) == 0);  // already at the top
    CHECK(shipui::moveOrder(orders, 1, 5) == 2);   // clamped to the bottom
    CHECK(orders[2].kind == OrderKind::Resupply);

    using shipui::Step;
    CHECK(shipui::stepAmount(Step::One, 40) == 1);
    CHECK(shipui::stepAmount(Step::Five, 3) == 3);
    CHECK(shipui::stepAmount(Step::Ten, 40) == 10);
    CHECK(shipui::stepAmount(Step::All, 40) == 40);
    CHECK(shipui::stepAmount(Step::All, 0) == 0);
}

TEST_CASE("ship windows: orders go to the fleet of a fleet member") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId d = frigate(s, r, me, "Plain", {});
    const VehicleId a = addTestVehicle(s, r, d, home).id;
    const VehicleId b = addTestVehicle(s, r, d, home).id;
    CHECK(shipui::orderOwner(s, a).vehicle == a);
    REQUIRE(apply(r, s, me, cmd::CreateFleet{"Alpha", {a, b}}).ok);
    const shipui::OrderOwner owner = shipui::orderOwner(s, b);
    CHECK(owner.fleet == s.vehicle(a)->fleet);
    CHECK_FALSE(owner.vehicle.valid());
    REQUIRE(apply(r, s, me, shipui::withAppended(s, owner, Order{OrderKind::Sentry})).ok);
    CHECK(s.fleet(owner.fleet)->orders.size() == 1);
    CHECK(shipui::ownerLocation(s, owner) == home);
}

TEST_CASE("ship windows: scrap window values") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    Colony& home = homeworld(s, me);
    const Location where = locationOf(s.galaxy, home.planet);
    const DesignId plain = frigate(s, r, me, "Plain", {});
    const DesignId armed = frigate(s, r, me, "Armed", {"Test Laser", "Test Self Destruct"});
    const VehicleId a = addTestVehicle(s, r, plain, where).id;
    const Resources cost = computeDesignStats(r, nullptr, s.design(plain)).cost;
    // round(cost × P %) per resource, in floating point (spec 03 §15).
    auto rounded = [&](int64_t pct) {
        return Resources{xmath::pctRound(cost.v[0], pct), xmath::pctRound(cost.v[1], pct), xmath::pctRound(cost.v[2], pct)};
    };
    CHECK(shipui::scrapValue(r, s, *s.vehicle(a)) == rounded(30));
    CHECK(shipui::unmothballCost(r, s, *s.vehicle(a)) == rounded(20));
    CHECK_FALSE(shipui::selfDestructEntry(r, s, *s.vehicle(a)).has_value());
    CHECK_FALSE(shipui::canBeFiredOn(r, s, *s.vehicle(a), {a}));

    const VehicleId b = addTestVehicle(s, r, armed, where).id;
    CHECK(shipui::selfDestructEntry(r, s, *s.vehicle(b)) == kHullBasics.size() + 1);
    CHECK(shipui::vehicleArmed(r, s, *s.vehicle(b)));
    CHECK(shipui::canBeFiredOn(r, s, *s.vehicle(a), {a}));
    CHECK_FALSE(shipui::canBeFiredOn(r, s, *s.vehicle(a), {a, b}));  // the gunship is selected too
    CHECK_FALSE(shipui::canBeFiredOn(r, s, *s.vehicle(b), {b}));     // nobody else is armed

    // A recycler in the sector raises the refund.
    home.facilities.push_back(facilityIndex(r, "Test Recycler"));
    CHECK(shipui::scrapValue(r, s, *s.vehicle(a)) == rounded(60));
    const size_t slot = home.facilities.size() - 1;
    CHECK(shipui::facilityScrapValue(r, s, home, slot) == Resources::from(r.facility(home.facilities[slot]).cost).percent(60));

    // Mothballed vehicles cost no maintenance; groups pay per unit.
    Vehicle& va = *s.vehicle(a);
    const Resources single = shipui::vehicleMaintenance(r, s, va);
    CHECK_FALSE(single.isZero());
    va.count = 3;
    CHECK(shipui::vehicleMaintenance(r, s, va) == single + single + single);
    va.status = VehicleStatus::Mothballed;
    CHECK(shipui::vehicleMaintenance(r, s, va).isZero());

    // Research potential counts components the empire cannot build.
    const shipui::ResearchPotential p = shipui::researchPotential(r, s, s.empire(me), {s.vehicle(b)});
    CHECK(p.total == static_cast<int>(s.design(armed).entries.size()));
    CHECK(std::string(shipui::researchPotentialLabel({0, 5})) == "None");
    CHECK(std::string(shipui::researchPotentialLabel({5, 5})) == "High");
}

TEST_CASE("ship windows: a dry run reports the cost without changing the game") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const Location where = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId a = frigate(s, r, me, "A", {"Test Laser"});
    const DesignId b = frigate(s, r, me, "B", {"Test Armor Plate"});
    const VehicleId id = addTestVehicle(s, r, a, where).id;
    const Resources before = s.empire(me).stockpile;
    const shipui::DryRun dr = shipui::dryRun(r, s, me, cmd::Retrofit{id, b});
    CHECK(dr.result.ok);
    CHECK(dr.cost == Resources{15 * 120 / 100 + 40 * 30 / 100, 0, 10 * 30 / 100});
    CHECK(s.vehicle(id)->design == a);
    CHECK(s.empire(me).stockpile == before);
    CHECK_FALSE(shipui::dryRun(r, s, me, cmd::Retrofit{id, DesignId{9999u}}).result.ok);
}

TEST_CASE("ship windows: launchers and unit designs") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const Location where = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId carrier = frigate(s, r, me, "Carrier", {"Test Fighter Bay"});
    const DesignId fighter = addTestDesign(s, r, me, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    const DesignId mine = addTestDesign(s, r, me, "Mine", "Test Mine Hull", {"Test Warhead"});
    const Vehicle& v = addTestVehicle(s, r, carrier, where);
    CHECK(shipui::isUnitDesign(r, s, fighter));
    CHECK_FALSE(shipui::isUnitDesign(r, s, carrier));
    const shipui::LaunchRates rates = shipui::launchRates(r, s, v);
    CHECK(rates.fighters >= 0);
    CHECK(rates.mines < 0);
    CHECK(shipui::canLaunch(r, s, v, fighter));
    CHECK_FALSE(shipui::canLaunch(r, s, v, mine));
}

TEST_CASE("ship windows: stellar manipulation checks") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const Location home = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId maker = frigate(s, r, me, "Maker", {"Test Planet Maker"});
    const DesignId plain = frigate(s, r, me, "Plain", {});
    const VehicleId id = addTestVehicle(s, r, maker, home).id;
    const Vehicle& other = addTestVehicle(s, r, plain, home);

    const shipui::StellarCheck none = shipui::checkStellar(r, s, other, StellarAction::CreatePlanet);
    CHECK_FALSE(none.hasAbility);
    CHECK_FALSE(none.possible);
    CHECK_FALSE(shipui::checkStellar(r, s, *s.vehicle(id), StellarAction::CreateStorm).hasAbility);

    // No asteroids at the homeworld.
    const shipui::StellarCheck noRocks = shipui::checkStellar(r, s, *s.vehicle(id), StellarAction::CreatePlanet);
    CHECK(noRocks.hasAbility);
    CHECK_FALSE(noRocks.possible);

    // Turn another planet of the system into an asteroid field and move there.
    ObjectId field;
    for (ObjectId o : s.galaxy.system(home.system).objects)
        if (s.galaxy.object(o).kind == ObjectKind::Planet && !s.colony(o)) field = o;
    REQUIRE(field.valid());
    s.galaxy.object(field).kind = ObjectKind::Asteroids;
    s.vehicle(id)->location = locationOf(s.galaxy, field);
    const shipui::StellarCheck ok = shipui::checkStellar(r, s, *s.vehicle(id), StellarAction::CreatePlanet);
    CHECK_MESSAGE(ok.possible, ok.reason);
    CHECK(ok.target == field);
    // The engine's own checks (docs/spec/01 §9), for example: not while cloaked.
    s.vehicle(id)->status = VehicleStatus::Cloaked;
    const shipui::StellarCheck cloaked = shipui::checkStellar(r, s, *s.vehicle(id), StellarAction::CreatePlanet);
    CHECK_FALSE(cloaked.possible);
    CHECK(cloaked.reason == "A cloaked ship cannot manipulate stars.");
    s.vehicle(id)->status = VehicleStatus::Normal;

    const Order o = shipui::stellarOrder(*s.vehicle(id), StellarAction::CreatePlanet, ok.target);
    CHECK(o.kind == OrderKind::StellarManipulation);
    CHECK(o.amount == static_cast<int>(StellarAction::CreatePlanet));
    CHECK(o.location == s.vehicle(id)->location);
    CHECK(std::string(shipui::stellarInfo(StellarAction::CreateConstructedPlanet).picture) == "Ring");
    CHECK(shipui::stellarInfo(StellarAction::CreateConstructedPlanet).frames == 16);
}

// ---- The scrap window's actions in the engine (docs/spec/03 §9, §14, §15) ----------------------------

TEST_CASE("ship windows: retrofit follows the original's checks and costs") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    Colony& home = homeworld(s, me);
    const Location where = locationOf(s.galaxy, home.planet);
    const DesignId light = frigate(s, r, me, "Light", {"Test Laser"});
    const DesignId heavy = frigate(s, r, me, "Heavy", {"Test Laser", "Test Laser", "Test Laser", "Test Laser", "Test Laser"});
    const DesignId bare = frigate(s, r, me, "Bare", {});
    const DesignId yarded = frigate(s, r, me, "Yarded", {"Test Yard Module"});
    const VehicleId id = addTestVehicle(s, r, light, where).id;

    CHECK_FALSE(apply(r, s, me, cmd::Retrofit{id, light}).ok);  // identical: nothing to do
    // Only an increase is limited: 5 lasers cost far more than 50 % over the light design.
    const CommandResult tooDear = apply(r, s, me, cmd::Retrofit{id, heavy});
    CHECK_FALSE(tooDear.ok);
    CHECK(tooDear.error.find("costs too much") != std::string::npos);
    // Removing parts only is allowed and free.
    const Resources before = s.empire(me).stockpile;
    REQUIRE(apply(r, s, me, cmd::Retrofit{id, bare}).ok);
    CHECK(s.empire(me).stockpile == before);
    CHECK(s.vehicle(id)->design == bare);
    // Space yards cannot be added.
    CHECK_FALSE(apply(r, s, me, cmd::Retrofit{id, yarded}).ok);
    // Cargo forbids it; so does a cloak.
    s.vehicle(id)->cargo.population.push_back({me, 1});
    CHECK_FALSE(apply(r, s, me, cmd::Retrofit{id, light}).ok);
    s.vehicle(id)->cargo = {};
    s.vehicle(id)->status = VehicleStatus::Cloaked;
    CHECK_FALSE(apply(r, s, me, cmd::Retrofit{id, light}).ok);
    s.vehicle(id)->status = VehicleStatus::Normal;
    // A cloaked yard ship does not count as a yard.
    home.facilities.erase(std::remove_if(home.facilities.begin(), home.facilities.end(),
                                         [&](uint32_t f) { return hasAbility(r.facilityAbilities(f), AbilityKind::SpaceYard); }),
                          home.facilities.end());
    const DesignId tender = addTestDesign(s, r, me, "Tender", "Test Station", {"Test Bridge", "Test Yard Module"});
    const VehicleId yard = addTestVehicle(s, r, tender, where).id;
    s.vehicle(yard)->status = VehicleStatus::Cloaked;
    CHECK_FALSE(apply(r, s, me, cmd::Retrofit{id, light}).ok);
    s.vehicle(yard)->status = VehicleStatus::Normal;
    // Adding a part costs truncate(cost x 120 %); it starts destroyed.
    const Resources funds = s.empire(me).stockpile;
    REQUIRE(apply(r, s, me, cmd::Retrofit{id, light}).ok);
    CHECK(funds - s.empire(me).stockpile == Resources{xmath::pctTrunc(40, 120), 0, xmath::pctTrunc(10, 120)});
    CHECK(s.vehicle(id)->damage.back() == entryStructure(r, s.design(light), s.design(light).entries.size() - 1));
}

TEST_CASE("ship windows: scrap and mothball rules") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const Location where = locationOf(s.galaxy, homeworld(s, me).planet);
    const DesignId mineD = addTestDesign(s, r, me, "Mine", "Test Mine Hull", {"Test Warhead"});
    const DesignId droneD = addTestDesign(s, r, me, "Drone", "Test Drone Hull", {"Test Engine", "Test Warhead"});
    const DesignId fighterD = addTestDesign(s, r, me, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    CHECK_FALSE(apply(r, s, me, cmd::Scrap{addTestVehicle(s, r, mineD, where).id}).ok);
    CHECK_FALSE(apply(r, s, me, cmd::Scrap{addTestVehicle(s, r, droneD, where).id}).ok);
    // A fighter group returns the unit percentage per unit.
    Vehicle& group = addTestVehicle(s, r, fighterD, where);
    group.count = 3;
    const VehicleId gid = group.id;
    const Resources unitCost = computeDesignStats(r, nullptr, s.design(fighterD)).cost;
    const Resources before = s.empire(me).stockpile;
    REQUIRE(apply(r, s, me, cmd::Scrap{gid}).ok);
    CHECK(s.empire(me).stockpile - before ==
          Resources{3 * xmath::pctRound(unitCost.v[0], 30), 3 * xmath::pctRound(unitCost.v[1], 30), 3 * xmath::pctRound(unitCost.v[2], 30)});

    // Mothballing needs no cargo; the ship loses its supply; unmothballing needs no yard.
    const DesignId d = frigate(s, r, me, "Sleeper", {"Test Supply Pod"});
    const VehicleId ship = addTestVehicle(s, r, d, where).id;
    s.vehicle(ship)->cargo.population.push_back({me, 1});
    CHECK_FALSE(apply(r, s, me, cmd::Mothball{ship, true}).ok);
    s.vehicle(ship)->cargo = {};
    REQUIRE(apply(r, s, me, cmd::Mothball{ship, true}).ok);
    CHECK(s.vehicle(ship)->supply == 0);
    CHECK(vehicleMaxMovement(r, s, *s.vehicle(ship)) == 0);
    s.vehicle(ship)->location = {where.system, Sector{where.sector.x == 0 ? 1 : 0, where.sector.y}};
    REQUIRE(apply(r, s, me, cmd::Mothball{ship, false}).ok);  // away from any yard or depot
    CHECK(s.vehicle(ship)->supply == 0);                      // no depot: it stays empty
    // Bases come back with unlimited supply.
    const DesignId baseD = addTestDesign(s, r, me, "Fort", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters"});
    const VehicleId base = addTestVehicle(s, r, baseD, where).id;
    REQUIRE(apply(r, s, me, cmd::Mothball{base, true}).ok);
    REQUIRE(apply(r, s, me, cmd::Mothball{base, false}).ok);
    CHECK(s.vehicle(base)->supply == kUnlimitedSupply);
}

TEST_CASE("ship windows: who can join a fleet") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u};
    const Location where = locationOf(s.galaxy, homeworld(s, me).planet);
    const VehicleId ship = addTestVehicle(s, r, frigate(s, r, me, "Lead", {}), where).id;
    const VehicleId fighters = addTestVehicle(s, r, addTestDesign(s, r, me, "F", "Test Fighter Hull", {"Test Fighter Engine"}), where).id;
    const VehicleId sat = addTestVehicle(s, r, addTestDesign(s, r, me, "S", "Test Satellite Hull", {"Test Satellite Gun"}), where).id;
    const VehicleId mine = addTestVehicle(s, r, addTestDesign(s, r, me, "M", "Test Mine Hull", {"Test Warhead"}), where).id;
    const VehicleId drone = addTestVehicle(s, r, addTestDesign(s, r, me, "D", "Test Drone Hull", {"Test Engine", "Test Warhead"}), where).id;
    const VehicleId base = addTestVehicle(s, r, addTestDesign(s, r, me, "B", "Test Station", {"Test Bridge"}), where).id;
    REQUIRE(apply(r, s, me, cmd::CreateFleet{"Wing", {ship, fighters}}).ok);  // fighter groups can join
    const FleetId wing = s.fleets.back().id;
    CHECK_FALSE(apply(r, s, me, cmd::JoinFleet{wing, sat}).ok);
    CHECK_FALSE(apply(r, s, me, cmd::JoinFleet{wing, mine}).ok);
    CHECK_FALSE(apply(r, s, me, cmd::JoinFleet{wing, drone}).ok);
    CHECK_FALSE(apply(r, s, me, cmd::JoinFleet{wing, base}).ok);  // Bases Can Join Fleets is off
}

TEST_CASE("ship windows: a renamed design needs a name no empire uses") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const EmpireId me{0u}, them{1u};
    const DesignId mine = frigate(s, r, me, "Mine", {});
    frigate(s, r, them, "Theirs", {});
    CHECK_FALSE(apply(r, s, me, cmd::Rename{{}, {}, mine, {}, "Theirs"}).ok);
    CHECK(apply(r, s, me, cmd::Rename{{}, {}, mine, {}, "theirs"}).ok);  // exact, case-sensitive comparison
    CHECK(apply(r, s, me, cmd::Rename{{}, {}, mine, {}, "theirs"}).ok);  // keeping its own name is fine
    CHECK(designNameInUse(s, "Theirs"));
    CHECK_FALSE(designNameInUse(s, "Nobody's"));
}
