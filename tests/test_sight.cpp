// Sight, cloaking and empire knowledge (docs/spec/01 §6).

#include "movement_fixture.hpp"

#include <doctest/doctest.h>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::mvtest;

namespace {

Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }

bool visibleIn(const GameState& s, EmpireId e, VehicleId id) {
    const auto& list = s.empire(e).knowledge.visibleVehicles;
    return std::find(list.begin(), list.end(), id) != list.end();
}
bool seen(const GameState& s, EmpireId e, DesignId d) {
    const auto& list = s.empire(e).knowledge.seenDesigns;
    return std::binary_search(list.begin(), list.end(), d);
}

} // namespace

TEST_CASE("sight: presence comes from vehicles and colonies; baseline sensors see plain ships") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 5, 0), c = w.system("C", 10, 0);
    const VehicleId mine = w.spawn(w.ship(kA, "Mine", 1), at(a, 1, 1));
    const VehicleId theirsHere = w.spawn(w.ship(kB, "Here", 1), at(a, 9, 9));
    const VehicleId theirsThere = w.spawn(w.ship(kB, "There", 1), at(b, 2, 2));
    w.colony(w.planet(c, {4, 4}), kA, 0);  // even an empty colony gives presence

    CHECK(sight::hasPresence(w.s, kA, a));
    CHECK_FALSE(sight::hasPresence(w.s, kA, b));
    CHECK(sight::hasPresence(w.s, kA, c));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(mine)));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(theirsHere)));
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(theirsThere)));
    const auto sensors = sight::sensorLevels(r, w.s, kA, a);
    CHECK(sensors[static_cast<size_t>(SightType::EMActive)] == 1);
    CHECK(sensors[static_cast<size_t>(SightType::Psychic)] == 0);
    CHECK(sight::sensorLevels(r, w.s, kA, b) == sight::SightVector{});

    // A destroyed vehicle is never seen.
    w.v(theirsHere).count = 0;
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(theirsHere)));
}

TEST_CASE("sight: cloaks hide ships until some sight type reaches the cloak level") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const VehicleId watcher = w.spawn(w.ship(kA, "Watcher", 1), at(a, 0, 0));
    const VehicleId ghost = w.spawn(w.ship(kB, "Ghost", 1, {"Mv Cloak"}), at(a, 5, 5));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(ghost)));  // the device only works while cloaked
    w.v(ghost).status = VehicleStatus::Cloaked;
    CHECK(sight::obscuration(r, w.s, w.v(ghost)) == sight::SightVector{3, 3, 3, 3, 3});
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(ghost)));
    CHECK(sight::canSeeVehicle(r, w.s, kB, w.v(ghost)));  // owners always see their own

    // A level-2 EM Active sensor is not enough; level 3 is.
    const VehicleId eye2 = w.spawn(w.ship(kA, "Eye2", 1, {"Test Sensor"}), at(a, 1, 1));
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(ghost)));
    w.v(eye2).count = 0;
    const VehicleId eye3 = w.spawn(w.ship(kA, "Eye3", 1, {"Mv Sensor 3"}), at(a, 1, 1));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(ghost)));
    // A destroyed sensor stops working.
    w.v(eye3).damage[5] = 100;
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(ghost)));

    // A cloak that covers only the EM types is pierced by any other type.
    const VehicleId half = w.spawn(w.ship(kB, "Half", 1, {"Mv EM Cloak"}), at(a, 6, 6));
    w.v(half).status = VehicleStatus::Cloaked;
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(half)));
    w.spawn(w.ship(kA, "Mind", 1, {"Mv Psychic Sensor"}), at(a, 2, 2));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(half)));
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(ghost)));  // psychic 2 < cloak 3
    (void)watcher;
}

TEST_CASE("sight: hull cloaks are always on; mothballed ships have no abilities") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    w.spawn(w.ship(kA, "Watcher", 1, {"Mv Sensor 3"}), at(a, 0, 0));
    const VehicleId mines = w.spawn(w.design(kB, "Mine", "Mv Mine Hull", {"Test Warhead"}), at(a, 3, 3));
    CHECK(sight::obscuration(r, w.s, w.v(mines))[0] == 5);
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(mines)));
    CHECK(sight::canSeeVehicle(r, w.s, kB, w.v(mines)));

    const VehicleId sleeper = w.spawn(w.ship(kA, "Sleeper", 1, {"Mv Sensor 3"}), at(a, 8, 8));
    w.v(sleeper).status = VehicleStatus::Mothballed;
    const VehicleId ghost = w.spawn(w.ship(kB, "Ghost", 1, {"Mv Cloak"}), at(a, 8, 8));
    w.v(ghost).status = VehicleStatus::Cloaked;
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(ghost)));  // the watcher's sensor, not the sleeper's
    for (Vehicle& v : w.s.vehicles)
        if (v.owner == kA && v.id != sleeper) v.count = 0;
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(ghost)));
}

TEST_CASE("sight: storms and nebulae hide ships and planets but not units or stars") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), n = w.system("N", 5, 0);
    const ObjectId storm = w.object(a, ObjectKind::Storm, {3, 3});
    w.s.galaxy.object(storm).abilities.push_back(ab(AbilityKind::SectorSightObscuration, 2));
    w.spawn(w.ship(kA, "Watcher", 1), at(a, 0, 0));
    const VehicleId hidden = w.spawn(w.ship(kB, "Hidden", 1), at(a, 3, 3));
    const VehicleId outside = w.spawn(w.ship(kB, "Outside", 1), at(a, 4, 4));
    const VehicleId fighters = w.spawn(w.design(kB, "Fighter", "Test Fighter Hull", {"Test Fighter Engine", "Mv Fighter Tank"}), at(a, 3, 3));
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(hidden)));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(outside)));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(fighters)));  // units do not benefit
    w.spawn(w.ship(kA, "Eye", 1, {"Test Sensor"}), at(a, 0, 1));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(hidden)));

    // A nebula covers the whole system, planets included; stars stay visible.
    w.s.galaxy.system(n).abilities.push_back(ab(AbilityKind::SectorSightObscuration, 2));
    const ObjectId star = w.object(n, ObjectKind::Star, {6, 6});
    const ObjectId planet = w.planet(n, {2, 2});
    w.s.empire(kA).knowledge.explored[n.index()] = 1;
    CHECK(sight::planetObscuration(w.s, star) == sight::SightVector{1, 1, 1, 1, 1});
    CHECK(sight::canSeePlanet(r, w.s, kA, star));
    CHECK_FALSE(sight::canSeePlanet(r, w.s, kA, planet));
    const VehicleId inNebula = w.spawn(w.ship(kA, "Diver", 1), at(n, 0, 0));
    CHECK_FALSE(sight::canSeePlanet(r, w.s, kA, planet));
    w.v(inNebula).count = 0;
    w.spawn(w.ship(kA, "Probe", 1, {"Test Sensor"}), at(n, 0, 0));
    CHECK(sight::canSeePlanet(r, w.s, kA, planet));
    w.colony(planet, kB, 10);
    CHECK(sight::canSeePlanet(r, w.s, kB, planet));  // own colonies are always seen

    // Unexplored systems show nothing.
    const SystemId far = w.system("Far", 20, 0);
    const ObjectId farPlanet = w.planet(far, {1, 1});
    CHECK_FALSE(sight::canSeePlanet(r, w.s, kA, farPlanet));
}

TEST_CASE("sight: omnipresence gives presence everywhere but does not reveal cloaks") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 5, 0);
    const VehicleId plain = w.spawn(w.ship(kB, "Plain", 1), at(b, 1, 1));
    const VehicleId ghost = w.spawn(w.ship(kB, "Ghost", 1, {"Mv Cloak"}), at(b, 2, 2));
    w.v(ghost).status = VehicleStatus::Cloaked;
    w.s.options.omnipresent = true;
    CHECK(sight::hasPresence(w.s, kA, b));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(plain)));
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(ghost)));
    sight::updateKnowledge(r, w.s);
    CHECK(w.s.empire(kA).hasExplored(a));
    CHECK(w.s.empire(kA).knowledge.present[b.index()]);
    CHECK(visibleIn(w.s, kA, plain));
    CHECK_FALSE(visibleIn(w.s, kA, ghost));
}

TEST_CASE("sight: knowledge tracks exploration, presence, last seen and visible vehicles") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 5, 0);
    const VehicleId scout = w.spawn(w.ship(kA, "Scout", 1), at(a, 0, 0));
    const VehicleId other = w.spawn(w.ship(kB, "Other", 1), at(a, 5, 5));
    const VehicleId away = w.spawn(w.ship(kB, "Away", 1), at(b, 5, 5));
    w.s.turn = 4;
    sight::updateKnowledge(r, w.s);
    const Knowledge& k = w.s.empire(kA).knowledge;
    CHECK(k.explored[a.index()]);
    CHECK(k.present[a.index()]);
    CHECK(k.lastSeen[a.index()] == 4);
    CHECK_FALSE(k.explored[b.index()]);
    CHECK(visibleIn(w.s, kA, other));
    CHECK_FALSE(visibleIn(w.s, kA, away));
    CHECK_FALSE(visibleIn(w.s, kA, scout));  // only foreign vehicles are listed
    CHECK(std::is_sorted(w.s.empire(kB).knowledge.visibleVehicles.begin(), w.s.empire(kB).knowledge.visibleVehicles.end()));

    // Leaving keeps the exploration, not the presence.
    w.v(scout).location = at(b, 0, 0);
    w.s.turn = 5;
    sight::updateKnowledge(r, w.s);
    CHECK(k.explored[a.index()]);
    CHECK_FALSE(k.present[a.index()]);
    CHECK(k.lastSeen[a.index()] == 4);
    CHECK(k.lastSeen[b.index()] == 5);
    CHECK(visibleIn(w.s, kA, away));
    CHECK_FALSE(visibleIn(w.s, kA, other));
}

TEST_CASE("sight: long range scanners reveal designs; jammers block them") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 5, 0);
    w.spawn(w.ship(kA, "Scanner", 1, {"Mv Scanner"}), at(a, 0, 0));
    const DesignId nearD = w.ship(kB, "Near", 1);
    const DesignId farD = w.ship(kB, "Far", 1);
    const DesignId jamD = w.ship(kB, "Jam", 1, {"Mv Jammer"});
    w.spawn(nearD, at(a, 2, 2));
    w.spawn(farD, at(a, 5, 5));
    w.spawn(jamD, at(a, 1, 1));
    sight::updateKnowledge(r, w.s);
    CHECK(seen(w.s, kA, nearD));
    CHECK_FALSE(seen(w.s, kA, farD));
    CHECK_FALSE(seen(w.s, kA, jamD));

    // A system scanner on a populated colony covers the whole system.
    w.colony(w.planet(a, {9, 9}), kA, 100, {"Mv System Scanner"});
    sight::updateKnowledge(r, w.s);
    CHECK(seen(w.s, kA, farD));
    CHECK_FALSE(seen(w.s, kA, jamD));
    CHECK_FALSE(seen(w.s, kB, nearD));  // never one's own designs
    (void)b;
}

TEST_CASE("sight: partnerships share the live view, also through chains") {
    World w(rules(), 4);
    const Rules& r = w.rules();
    const EmpireId kC{2u}, kD{3u};
    const SystemId a = w.system("A"), c = w.system("C", 5, 0);
    const auto [ac, ca] = w.link(a, {12, 6}, c, {0, 6});
    w.spawn(w.ship(kC, "Far Eye", 1, {"Mv Scanner"}), at(c, 0, 0));
    const DesignId stranger = w.ship(kD, "Stranger", 1);
    const VehicleId target = w.spawn(stranger, at(c, 1, 1));
    const VehicleId partnerShip = w.spawn(w.ship(kB, "Partner", 1), at(a, 3, 3));
    w.s.empire(kC).knowledge.knownWarpLink[ca.index()] = 1;
    w.setTreaty(kA, kB, Treaty::Partnership);
    w.setTreaty(kB, kC, Treaty::Partnership);
    w.setTreaty(kA, kC, Treaty::War);

    CHECK(sight::sightGroup(w.s, kA) == std::vector<EmpireId>{kA, kB, kC});
    CHECK(sight::hasPresence(w.s, kA, c));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(target)));
    CHECK(sight::canSeeVehicle(r, w.s, kA, w.v(partnerShip)));
    sight::updateKnowledge(r, w.s);
    CHECK(w.s.empire(kA).hasExplored(c));  // seen live, as if present
    CHECK(w.s.empire(kA).knowledge.present[c.index()]);
    CHECK(visibleIn(w.s, kA, target));
    CHECK(seen(w.s, kC, stranger));
    // Maps and scanned designs pass between partners in diplomacy::updateContacts.
    CHECK_FALSE(sight::knowsWarpLink(w.s, kA, ca));
    CHECK_FALSE(seen(w.s, kA, stranger));
    CHECK_FALSE(seen(w.s, kD, stranger));

    // Without the chain, the view is gone (exploration stays).
    w.setTreaty(kB, kC, Treaty::None);
    CHECK_FALSE(sight::hasPresence(w.s, kA, c));
    CHECK_FALSE(sight::canSeeVehicle(r, w.s, kA, w.v(target)));
    sight::updateKnowledge(r, w.s);
    CHECK(w.s.empire(kA).hasExplored(c));
    CHECK_FALSE(w.s.empire(kA).knowledge.present[c.index()]);
    (void)ac;
}

TEST_CASE("sight: the Galaxy Seen trait and the all-systems-seen option reveal the map") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A"), b = w.system("B", 5, 0);
    const auto [ab, ba] = w.link(a, {12, 6}, b, {0, 6});
    w.s.empire(kA).race.traits.push_back(traitIndex(r, "Mv Far Sight"));
    sight::updateKnowledge(r, w.s);
    CHECK(w.s.empire(kA).hasExplored(a));
    CHECK(w.s.empire(kA).hasExplored(b));
    CHECK(sight::knowsWarpLink(w.s, kA, ab));
    CHECK_FALSE(w.s.empire(kB).hasExplored(b));
    CHECK_FALSE(w.s.empire(kA).knowledge.present[b.index()]);  // seen is not present

    w.s.options.allSystemsSeen = true;
    sight::updateKnowledge(r, w.s);
    CHECK(w.s.empire(kB).hasExplored(b));
    CHECK_FALSE(w.s.empire(kB).knowledge.present[b.index()]);

    sight::learnWarpLink(w.s, kB, ba);
    CHECK(sight::knowsWarpLink(w.s, kB, ab));  // the way back is known too
    sight::markExplored(w.s, kB, a);
    CHECK(w.s.empire(kB).hasExplored(a));
}
