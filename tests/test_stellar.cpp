// Stellar manipulation details and destroyed stars (docs/spec/01 §5.4, §5.6,
// §9, §14 Q33-Q34). The broad manipulation tests are in test_movement.cpp.

#include "movement_fixture.hpp"

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
