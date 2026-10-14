// Stellar manipulation details and destroyed stars (docs/spec/01 §5.4, §5.6,
// §9, §14 Q33-Q34). The broad manipulation tests are in test_movement.cpp.

#include "movement_fixture.hpp"

#include "client/classic/screens/ships_logic.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::mvtest;

namespace {

Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }

Order stellar(StellarAction a, ObjectId obj = {}) {
    Order o;
    o.kind = OrderKind::StellarManipulation;
    o.object = obj;
    o.amount = static_cast<int>(a);
    return o;
}

bool hasMood(const std::vector<MoodEvent>& moods, EmpireId e, std::string_view trigger) {
    return std::any_of(moods.begin(), moods.end(), [&](const MoodEvent& m) { return m.empire == e && m.trigger == trigger; });
}

size_t countKind(const GameState& s, SystemId sys, ObjectKind k) {
    size_t n = 0;
    for (ObjectId o : s.galaxy.system(sys).objects) n += s.galaxy.object(o).kind == k;
    return n;
}

bool inSystemList(const GameState& s, ObjectId o) {
    const auto& list = s.galaxy.system(s.galaxy.object(o).system).objects;
    return std::find(list.begin(), list.end(), o) != list.end();
}

const LogEntry* destructiveReport(const World& w, EmpireId e) {
    for (const LogEntry& l : w.s.empire(e).log)
        if (movement::isDestructiveStellarReport(l.title)) return &l;
    return nullptr;
}

void meet(World& w, Treaty t) {
    w.s.empire(kA).relation(kB).contact = w.s.empire(kB).relation(kA).contact = true;
    w.setTreaty(kA, kB, t);
}

} // namespace

TEST_CASE("stellar manipulation: only visible, active ships and bases and colonies of hostile empires block") {
    World w;
    const SystemId a = w.system("A");
    w.object(a, ObjectKind::Star, {6, 6});
    const DesignId stormMaker = w.ship(kA, "Maker", 3, {"Mv Storm Maker"});

    // Unit groups never block, and neither does a mothballed ship.
    const VehicleId fighters = w.spawn(w.design(kB, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Mv Fighter Tank"}), at(a, 2, 2));
    const VehicleId laidUp = w.spawn(w.ship(kB, "Laid Up", 1), at(a, 2, 2));
    w.v(laidUp).status = VehicleStatus::Mothballed;
    const VehicleId first = w.spawn(stormMaker, at(a, 2, 2));
    w.order(first, stellar(StellarAction::CreateStorm));
    w.move();
    CHECK(countKind(w.s, a, ObjectKind::Storm) == 1);
    CHECK(w.s.vehicle(fighters) != nullptr);

    // An active ship does.
    w.v(laidUp).status = VehicleStatus::Normal;
    const VehicleId second = w.spawn(stormMaker, at(a, 2, 2));
    w.order(second, stellar(StellarAction::CreateStorm));
    w.move();
    CHECK(w.logged(kA, "hostile"));
    CHECK(countKind(w.s, a, ObjectKind::Storm) == 1);

    // So does a colony of an empire below Non-Aggression, but not one of an empire at peace.
    const ObjectId rock = w.planet(a, {4, 4});
    w.colony(rock, kB, 500);
    const VehicleId third = w.spawn(stormMaker, at(a, 4, 4));
    w.order(third, stellar(StellarAction::CreateStorm));
    w.move();
    CHECK(countKind(w.s, a, ObjectKind::Storm) == 1);
    meet(w, Treaty::NonAggression);
    w.order(third, stellar(StellarAction::CreateStorm));
    w.move();
    CHECK(countKind(w.s, a, ObjectKind::Storm) == 2);

    // Destroy Planet makes no such check: an armed enemy in the sector does not stop it.
    meet(w, Treaty::War);
    const ObjectId target = w.planet(a, {8, 8});
    w.colony(target, kB, 500).colonyType = "Homeworld";
    w.spawn(w.ship(kB, "Guard", 1), at(a, 8, 8));
    const VehicleId breaker = w.spawn(w.ship(kA, "Breaker", 3, {"Mv Planet Breaker"}), at(a, 8, 8));
    w.order(breaker, stellar(StellarAction::DestroyPlanet, target));
    w.move();
    CHECK_FALSE(inSystemList(w.s, target));
    CHECK(w.objectAt(a, {8, 8}, ObjectKind::Asteroids).valid());
    CHECK(w.s.colony(target) == nullptr);
    // The colony type decides Homeworld Lost (spec 02 §2).
    CHECK(hasMood(w.lastMoods, kB, "Homeworld Lost"));
    CHECK(hasMood(w.lastMoods, kB, "Any Planet Lost"));
}

TEST_CASE("stellar manipulation: a destroyed planet is reported to the empires still present once it is gone, mine fields aside (spec 05 Q50)") {
    World w(opense4::mvtest::rules(), 4);
    const EmpireId kC{2u}, kD{3u};
    const SystemId a = w.system("A");
    w.object(a, ObjectKind::Star, {6, 6});
    meet(w, Treaty::War);
    const ObjectId target = w.planet(a, {8, 8});
    w.colony(target, kB, 500);
    const ObjectId other = w.planet(a, {2, 9});
    w.colony(other, kC, 500);
    // D has only a mine field in the system: not present.
    w.spawn(w.design(kD, "Mine", "Mv Mine Hull", {}), at(a, 1, 1));
    const VehicleId breaker = w.spawn(w.ship(kA, "Breaker", 3, {"Mv Planet Breaker"}), at(a, 8, 8));
    w.order(breaker, stellar(StellarAction::DestroyPlanet, target));
    w.move();
    REQUIRE_FALSE(inSystemList(w.s, target));
    // B had nothing in the system but the destroyed colony: once the planet is
    // gone it is not present, so it gets no report (confirmed: binary).
    CHECK(destructiveReport(w, kB) == nullptr);
    // C's other colony is still there: it gets the report naming A.
    const LogEntry* seen = destructiveReport(w, kC);
    REQUIRE(seen != nullptr);
    CHECK(movement::stellarReportNames(w.s, *seen, kA));
    CHECK_FALSE(movement::stellarReportNames(w.s, *seen, kC));
    REQUIRE(destructiveReport(w, kA) != nullptr);
    CHECK(destructiveReport(w, kD) == nullptr);

    // B keeps a ship in the system: it is present after the result and told.
    const ObjectId again = w.planet(a, {4, 9});
    w.colony(again, kB, 500);
    w.spawn(w.ship(kB, "Watcher", 1), at(a, 0, 12));
    const VehicleId breaker2 = w.spawn(w.ship(kA, "Breaker 2", 3, {"Mv Planet Breaker"}), at(a, 4, 9));
    for (Empire& e : w.s.empires) e.log.clear();
    w.order(breaker2, stellar(StellarAction::DestroyPlanet, again));
    w.move();
    CHECK(destructiveReport(w, kB) != nullptr);
}

TEST_CASE("stellar manipulation: the shockwave reports to every empire that lost an object there, mine fields included (spec 05 Q50)") {
    World w(opense4::mvtest::rules(), 4);
    const EmpireId kC{2u}, kD{3u};
    const SystemId a = w.system("A"), elsewhere = w.system("B", 5, 0);
    const ObjectId star = w.object(a, ObjectKind::Star, {6, 6});
    meet(w, Treaty::War);
    w.colony(w.planet(a, {8, 8}), kB, 500);                     // B loses a colony
    w.spawn(w.design(kD, "Mine", "Mv Mine Hull", {}), at(a, 1, 1));  // D loses a mine field
    w.spawn(w.ship(kC, "Far", 1), at(elsewhere, 1, 1));          // C has nothing there
    const VehicleId nova = w.spawn(w.ship(kA, "Nova", 3, {"Mv Star Breaker"}), at(a, 6, 6));
    w.order(nova, stellar(StellarAction::DestroyStar, star));
    w.move();
    CHECK(w.s.vehicle(nova) == nullptr);
    CHECK(destructiveReport(w, kB) != nullptr);
    CHECK(destructiveReport(w, kD) != nullptr);
    CHECK(destructiveReport(w, kC) == nullptr);
    // The culprit is told too; the report names itself.
    const LogEntry* own = destructiveReport(w, kA);
    REQUIRE(own != nullptr);
    CHECK(movement::stellarReportNames(w.s, *own, kA));
}

TEST_CASE("stellar manipulation: the shockwave is one pass in slot order; a new field may take a slot emptied earlier in it (spec 03 Q72)") {
    World w;
    const SystemId a = w.system("A");
    const ObjectId storm = w.object(a, ObjectKind::Storm, {1, 1});        // slot 0: removed first
    const ObjectId rock = w.planet(a, {3, 3});                           // slot 1
    const ObjectId field = w.object(a, ObjectKind::Asteroids, {5, 5});   // slot 2
    const ObjectId star = w.object(a, ObjectKind::Star, {6, 6});         // slot 3: removed after the pass
    const auto [wp, far] = w.link(a, {12, 6}, w.system("B", 5, 0), {0, 6});
    REQUIRE(w.s.galaxy.object(storm).slot == 0);
    REQUIRE(w.s.galaxy.object(star).slot == 3);
    const VehicleId nova = w.spawn(w.ship(kA, "Nova", 3, {"Mv Star Breaker"}), at(a, 6, 6));
    w.order(nova, stellar(StellarAction::DestroyStar, star));
    w.move();
    // The storm's slot 0 was empty when the planet's turn came: its new field
    // takes it; the field's new field takes the planet's slot 1, emptied then.
    const ObjectId fromRock = w.objectAt(a, {3, 3}, ObjectKind::Asteroids);
    const ObjectId fromField = w.objectAt(a, {5, 5}, ObjectKind::Asteroids);
    REQUIRE(fromRock.valid());
    REQUIRE(fromField.valid());
    CHECK(fromRock != rock);
    CHECK(fromField != field);
    CHECK(w.s.galaxy.object(fromRock).slot == 0);
    CHECK(w.s.galaxy.object(fromField).slot == 1);
    CHECK_FALSE(inSystemList(w.s, storm));
    CHECK_FALSE(inSystemList(w.s, star));
    CHECK(inSystemList(w.s, wp));
    CHECK(w.s.vehicle(nova) == nullptr);
    (void)far;
}

TEST_CASE("stellar manipulation: Create Planet with no planet record of the size still removes the field and is paid (spec 03 Q72)") {
    ruleset::Ruleset data = buildRuleset();
    // No natural planet record of the Small size.
    std::erase_if(data.sectorObjectTypes, [](const ruleset::SectorObjectType& t) {
        return datafile::keysEqual(t.physicalType, "Planet") && datafile::keysEqual(t.planetSize, "Small");
    });
    data.reindex();
    const Rules r{std::move(data)};
    World w(r);
    const SystemId a = w.system("A");
    w.object(a, ObjectKind::Star, {6, 6});
    const ObjectId rocks = w.object(a, ObjectKind::Asteroids, {2, 2});
    const VehicleId maker = w.spawn(w.ship(kA, "Maker", 3, {"Mv Planet Maker"}), at(a, 2, 2));
    const int64_t supply = w.v(maker).supply;
    w.order(maker, stellar(StellarAction::CreatePlanet, rocks));
    w.move();
    CHECK_FALSE(inSystemList(w.s, rocks));
    CHECK_FALSE(w.objectAt(a, {2, 2}, ObjectKind::Planet).valid());
    CHECK(w.v(maker).supply < supply);
    CHECK(w.logged(kA, "Planet Created"));
}

TEST_CASE("stellar manipulation: Create Planet needs an asteroid field without a colony and names the planet after the planets") {
    World w;
    const SystemId a = w.system("A");
    w.object(a, ObjectKind::Star, {6, 6});
    w.planet(a, {1, 1});
    w.s.galaxy.objects.back().name = "A II";
    const ObjectId moon = w.planet(a, {1, 1});
    w.s.galaxy.object(moon).name = "A VII";  // the second planet of its sector: not looked at
    const ObjectId belt = w.object(a, ObjectKind::Asteroids, {9, 9}, "A Asteroid Belt IX");  // fields do not count
    const ObjectId settled = w.object(a, ObjectKind::Asteroids, {3, 3}, "A Asteroid Belt I");
    w.colony(settled, kA, 100);

    // A colonized field is not a valid target, even the maker's own.
    const VehicleId maker = w.spawn(w.ship(kA, "Maker", 3, {"Mv Planet Maker"}), at(a, 3, 3));
    w.order(maker, stellar(StellarAction::CreatePlanet, settled));
    w.move();
    CHECK(w.logged(kA, "No asteroid field without a colony"));
    CHECK(w.s.galaxy.object(settled).kind == ObjectKind::Asteroids);
    CHECK(w.s.colony(settled) != nullptr);

    // A free field in the same sector is taken instead; its rolled ability is not
    // carried over and the planet rolls none.
    const ObjectId free = w.object(a, ObjectKind::Asteroids, {3, 3}, "A Asteroid Belt II");
    w.s.galaxy.object(free).abilities.push_back(ab(AbilityKind::SectorDamage, 5));
    w.order(maker, stellar(StellarAction::CreatePlanet));
    w.move();
    CHECK_FALSE(inSystemList(w.s, free));
    const ObjectId planet = w.objectAt(a, {3, 3}, ObjectKind::Planet);
    REQUIRE(planet.valid());
    const SpaceObject& made = w.s.galaxy.object(planet);
    CHECK(made.kind == ObjectKind::Planet);
    CHECK(made.abilities.empty());
    CHECK(made.name == "A III");  // one above A II; A VII and the belts do not count
    CHECK(w.s.galaxy.object(settled).kind == ObjectKind::Asteroids);
    CHECK(w.s.galaxy.object(belt).kind == ObjectKind::Asteroids);
}

TEST_CASE("stellar manipulation: Construct counts every component by design and uses up the builder's ships") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId star = w.object(a, ObjectKind::Star, {6, 6});
    meet(w, Treaty::TradeAlliance);
    // The builder: its own girder (10 kT) and a second ship whose girder is shot away.
    const VehicleId builder =
        w.spawn(w.design(kA, "Builder", "Test Frigate", {"Test Bridge", "Mv Tank", "Mv World Builder", "Mv Girder"}), at(a, 6, 6));
    const DesignId girders = w.ship(kA, "Girders", 1, {"Mv Girder"});
    const VehicleId wreck = w.spawn(girders, at(a, 6, 6));
    const size_t girder = w.s.design(girders).entries.size() - 1;
    w.v(wreck).damage[girder] = entryStructure(r, w.s.design(girders), girder);
    REQUIRE_FALSE(entryIntact(r, w.s, w.v(wreck), girder));
    // A mothballed ship's girders do not count.
    const VehicleId laidUp = w.spawn(girders, at(a, 6, 6));
    w.v(laidUp).status = VehicleStatus::Mothballed;
    // 20 kT are needed: the damaged girder counts, so the builder has them.
    w.order(builder, stellar(StellarAction::CreateConstructedPlanet, star));
    const size_t before = w.s.galaxy.objects.size();
    w.move();
    REQUIRE(w.s.galaxy.objects.size() == before + 1);
    CHECK(w.s.galaxy.objects.back().size == "Ringworld");
    // Every builder ship with the device or a girder is gone, the mothballed one too.
    CHECK(w.s.vehicle(builder) == nullptr);
    CHECK(w.s.vehicle(wreck) == nullptr);
    CHECK(w.s.vehicle(laidUp) == nullptr);

    // Without the damaged girder, a mothballed ship's does not make up the difference.
    World m;
    const SystemId b = m.system("B");
    const ObjectId star2 = m.object(b, ObjectKind::Star, {6, 6});
    const VehicleId builder2 =
        m.spawn(m.design(kA, "Builder", "Test Frigate", {"Test Bridge", "Mv Tank", "Mv World Builder", "Mv Girder"}), at(b, 6, 6));
    const VehicleId idle = m.spawn(m.ship(kA, "Girders", 1, {"Mv Girder"}), at(b, 6, 6));
    m.v(idle).status = VehicleStatus::Mothballed;
    m.order(builder2, stellar(StellarAction::CreateConstructedPlanet, star2));
    m.move();
    CHECK(m.logged(kA, "materials"));
    CHECK(m.s.vehicle(idle) != nullptr);
    // A base carries materials like a ship.
    m.spawn(m.design(kA, "Depot", "Test Station", {"Test Bridge", "Mv Girder"}), at(b, 6, 6));
    m.order(builder2, stellar(StellarAction::CreateConstructedPlanet, star2));
    m.move();
    CHECK(m.s.vehicle(builder2) == nullptr);
    CHECK(countKind(m.s, b, ObjectKind::Planet) == 1);
}

TEST_CASE("destroyed stars count as stars for solar collectors and solar resource generation") {
    ruleset::Ruleset data = buildRuleset();
    ruleset::Facility plant;
    plant.name = "Mv Solar Plant";
    plant.group = "Test";
    plant.cost = {100, 0, 0};
    plant.abilities = {ab(AbilityKind::SolarResourceGenMinerals, 30)};
    data.facilities.push_back(plant);
    data.reindex();
    const Rules r{std::move(data)};
    World w(r);
    const SystemId a = w.system("A");
    w.object(a, ObjectKind::Star, {6, 6});
    w.object(a, ObjectKind::DestroyedStar, {2, 9});
    CHECK(isStarKind(ObjectKind::DestroyedStar));

    const VehicleId collector = w.spawn(w.ship(kA, "Collector", 1, {"Test Solar Panel"}), at(a, 3, 3));
    w.v(collector).supply = 0;
    w.upkeep();
    CHECK(w.v(collector).supply == 2 * 50);  // 50 per star, two stars

    Colony& c = w.colony(w.planet(a, {4, 4}), kA, 1000, {"Mv Solar Plant"});
    const auto out = economy::colonyOutput(r, w.s, c);
    CHECK(out.solar == Resources{2 * 30, 0, 0});
}

// ---- The Stellar Manipulation window's orders (docs/spec/06 §1.3, spec 01 §9) ------------------------

namespace {

namespace shipui = opense4::client::classic::shipui;

// The test rules with a second constructed world (Special Ability ID 2) and a
// device that can build up to it.
Rules sphereRules() {
    ruleset::Ruleset data = buildRuleset();
    ruleset::PlanetSize sphere;
    for (const ruleset::PlanetSize& ps : data.planetSizes)
        if (ps.constructed) sphere = ps;
    REQUIRE(sphere.constructed);
    sphere.name = "Sphereworld";
    sphere.specialAbilityId = 2;
    data.planetSizes.push_back(sphere);
    for (const ruleset::Component& c : data.components)
        if (c.name == "Mv World Builder") {
            ruleset::Component big = c;
            big.name = "Mv Sphere Builder";
            big.abilities = {ab(AbilityKind::CreateConstructedPlanet, 2), ab(AbilityKind::ConstructedPlanetRequirements, 42, 20),
                             ab(AbilityKind::ComponentDestroyedOnUse)};
            data.components.push_back(big);
            break;
        }
    data.reindex();
    return Rules{std::move(data)};
}

Order construct(ObjectId star, int size) {
    Order o = stellar(StellarAction::CreateConstructedPlanet, star);
    o.planetSize = static_cast<uint8_t>(size);
    return o;
}

} // namespace

TEST_CASE("stellar manipulation: Construct builds the world its order names, up to the device's value (spec 06 §1.3)") {
    const Rules r = sphereRules();
    auto build = [&](std::string_view device, int size) {
        World w(r);
        const SystemId a = w.system("A");
        const ObjectId star = w.object(a, ObjectKind::Star, {6, 6});
        const VehicleId builder = w.spawn(w.design(kA, "Builder", "Test Frigate", {"Test Bridge", "Mv Tank", device, "Mv Girder"}), at(a, 6, 6));
        w.spawn(w.ship(kA, "Girders", 1, {"Mv Girder"}), at(a, 6, 6));
        w.order(builder, construct(star, size));
        w.move();
        std::string made;
        for (ObjectId o : w.s.galaxy.system(a).objects)
            if (w.s.galaxy.object(o).kind == ObjectKind::Planet) made = w.s.galaxy.object(o).size;
        return std::pair{made, w.logged(kA, "cannot build a world that large")};
    };
    // The size chosen, at most the device's value.
    CHECK(build("Mv Sphere Builder", 1).first == "Ringworld");
    CHECK(build("Mv Sphere Builder", 2).first == "Sphereworld");
    // No size: the one the device's value names (the computer players' orders).
    CHECK(build("Mv Sphere Builder", 0).first == "Sphereworld");
    CHECK(build("Mv World Builder", 0).first == "Ringworld");
    // A size above the device's value is refused.
    const auto tooLarge = build("Mv World Builder", 2);
    CHECK(tooLarge.first.empty());
    CHECK(tooLarge.second);
}

TEST_CASE("stellar manipulation: the window lights a button by the vehicle's own ability and finds the target afterwards") {
    const Rules r = sphereRules();
    World w(r);
    const SystemId a = w.system("A");
    const ObjectId first = w.object(a, ObjectKind::Star, {6, 6});
    const ObjectId gone = w.object(a, ObjectKind::DestroyedStar, {6, 6});
    w.object(a, ObjectKind::Star, {2, 2});   // another sector
    const VehicleId ship = w.spawn(w.ship(kA, "Breaker", 1, {"Mv Star Breaker", "Mv Sphere Builder", "Mv Storm Maker"}), at(a, 6, 6));

    // Lit by the ability alone: cloaked, the order's checks fail, the button stays lit.
    w.v(ship).status = VehicleStatus::Cloaked;
    const shipui::StellarCheck cloaked = shipui::checkStellar(r, w.s, w.v(ship), StellarAction::DestroyStar);
    CHECK(cloaked.lit);
    CHECK_FALSE(cloaked.possible);
    w.v(ship).status = VehicleStatus::Normal;
    CHECK_FALSE(shipui::checkStellar(r, w.s, w.v(ship), StellarAction::CloseWarpPoint).lit);
    // A storm needs no object: the button is lit in an empty sector too.
    CHECK(shipui::checkStellar(r, w.s, w.v(ship), StellarAction::CreateStorm).lit);

    // The aims.
    CHECK(shipui::stellarAim(StellarAction::CreateStorm) == shipui::StellarAim::None);
    CHECK(shipui::stellarAim(StellarAction::CreatePlanet) == shipui::StellarAim::None);
    CHECK(shipui::stellarAim(StellarAction::OpenWarpPoint) == shipui::StellarAim::System);
    CHECK(shipui::stellarAim(StellarAction::CloseWarpPoint) == shipui::StellarAim::Object);
    CHECK(shipui::stellarTargetKind(StellarAction::CreateConstructedPlanet) == ObjectKind::Star);
    CHECK(shipui::stellarTargetKind(StellarAction::DestroyPlanet) == ObjectKind::Planet);

    // The stars of the vehicle's sector, a destroyed one too, in the system's object order.
    const std::vector<ObjectId> stars = shipui::stellarCandidates(w.s, w.v(ship), StellarAction::DestroyStar);
    REQUIRE(stars.size() == 2);
    CHECK(stars[0] == first);
    CHECK(stars[1] == gone);
    CHECK(shipui::stellarCandidates(w.s, w.v(ship), StellarAction::DestroyStorm).empty());

    // Construct's sizes: every constructed world up to the device's value, in file order.
    const std::vector<shipui::ConstructSize> sizes = shipui::constructSizes(r, w.s, w.v(ship));
    REQUIRE(sizes.size() == 2);
    CHECK(sizes[0].name == "Ringworld");
    CHECK(sizes[0].id == 1);
    CHECK(sizes[1].name == "Sphereworld");
    const Order o = shipui::stellarOrder(w.v(ship), StellarAction::CreateConstructedPlanet, first, sizes[0].id);
    CHECK(o.planetSize == 1);
    CHECK(o.object == first);
    // Open Warp Point names the system picked on the Galaxy Map (its sector is not used).
    const SystemId b = w.system("B", 5, 5);
    const Order open = shipui::stellarOrder(w.v(ship), StellarAction::OpenWarpPoint, {}, 0, b);
    CHECK(open.location == Location{b, Sector{}});
}

TEST_CASE("stellar manipulation: the window's order goes to a fleet member's own list, carried out at once in a turn-based game") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId maker = w.spawn(w.ship(kA, "Maker", 1, {"Mv Storm Maker"}), at(a, 6, 6));
    const VehicleId mate = w.spawn(w.ship(kA, "Mate", 1), at(a, 6, 6));
    w.spawn(w.ship(kB, "Keeper", 1), at(a, 12, 12));   // B stays alive
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {maker, mate}}).ok);
    const Order storm = shipui::stellarOrder(w.v(maker), StellarAction::CreateStorm, {});

    // A fleet member's order normally goes to the whole fleet; the window's goes to its own list.
    const cmd::SetOrders own = shipui::stellarCommand(w.v(maker), storm);
    CHECK(own.ownList);
    CHECK(own.vehicle == maker);
    REQUIRE(apply(r, w.s, kA, own).ok);
    CHECK(w.v(maker).orders == std::vector<Order>{storm});
    CHECK(w.v(mate).orders.empty());
    // Only a vehicle has a list of its own.
    cmd::SetOrders fleetOwn;
    fleetOwn.fleet = w.v(maker).fleet;
    fleetOwn.ownList = true;
    CHECK_FALSE(apply(r, w.s, kA, fleetOwn).ok);
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{maker, {}, {}, false}).ok);   // Clear Orders: the fleet's lists

    // Turn-based: the vehicle's list is carried out at once.
    w.s.options.simultaneous = false;
    resumeTurnBased(r, w.s);
    REQUIRE(w.s.playerTurn.empire == kA);
    const TurnResult res = applyLive(r, w.s, kA, shipui::stellarCommand(w.v(maker), storm));
    CHECK(res.rejected.empty());
    CHECK(countKind(w.s, a, ObjectKind::Storm) == 1);
    CHECK(w.v(maker).orders.empty());
    CHECK(w.v(mate).orders.empty());
}
