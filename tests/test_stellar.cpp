// Stellar manipulation details and destroyed stars (docs/spec/01 §5.4, §5.6,
// §9, §14 Q33-Q34). The broad manipulation tests are in test_movement.cpp.

#include "movement_fixture.hpp"

#include "game/economy.hpp"

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
    CHECK(w.s.galaxy.object(target).kind == ObjectKind::Asteroids);
    CHECK(w.s.colony(target) == nullptr);
    // The colony type decides Homeworld Lost (spec 02 §2).
    CHECK(hasMood(w.lastMoods, kB, "Homeworld Lost"));
    CHECK(hasMood(w.lastMoods, kB, "Any Planet Lost"));
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
    const SpaceObject& made = w.s.galaxy.object(free);
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
