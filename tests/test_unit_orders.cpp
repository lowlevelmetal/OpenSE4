// Cargo orders by kind, the launch and recovery warnings, and the commands
// of the window Launch \ Recover Units (docs/spec/03 §8, §12; spec 06 §1.3).
// All content is invented for the tests.

#include "movement_fixture.hpp"

#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/serialize.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace opense4;
using namespace opense4::game;
using opense4::mvtest::World;
using ruleset::VehicleType;

namespace {

constexpr EmpireId kA{0u}, kB{1u};

Location at(SystemId s, int x, int y) { return Location{s, Sector{x, y}}; }

Order byKind(OrderKind k, CargoKind cargo, Location where = {}) {
    Order o{k, where};
    o.cargo = cargo;
    o.amount = -1;
    return o;
}

int64_t inSpace(const Rules& r, const GameState& s, EmpireId owner, Location where, VehicleType type) {
    int64_t n = 0;
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.owner == owner && v.location == where && vehicleType(r, s, v) == type) n += v.count;
    return n;
}

int warnings(const GameState& s, EmpireId e, std::string_view title) {
    return static_cast<int>(std::count_if(s.empire(e).log.begin(), s.empire(e).log.end(), [&](const LogEntry& l) { return l.title == title; }));
}

// A turn-based game in `e`'s turn, as the window needs.
void turnBasedTurnOf(World& w, EmpireId e) {
    w.s.options.simultaneous = false;
    w.s.playerTurn = PlayerTurn{};
    w.s.playerTurn.empire = e;
    w.s.playerTurn.started = true;
}

} // namespace

TEST_CASE("unit orders: Load and Drop Cargo move every design of the kind") {
    World w;
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {4, 4});
    const ObjectId other = w.planet(a, {4, 4});
    const DesignId wasp = w.design(kA, "Wasp", "Test Fighter Hull", {"Mv Fighter Engine", "Mv Fighter Tank"});
    const DesignId hornet = w.design(kA, "Hornet", "Test Fighter Hull", {"Mv Fighter Engine", "Mv Fighter Engine", "Mv Fighter Tank"});
    const DesignId sat = w.design(kA, "Eye", "Test Satellite Hull", {"Test Satellite Gun"});
    w.colony(home, kA).cargo.units = {{wasp, 2}, {sat, 1}, {hornet, 3}};
    w.colony(other, kA);
    const VehicleId hauler = w.spawn(w.ship(kA, "Hauler", 3, {"Mv Fighter Bay"}), at(a, 4, 4));
    // Load Cargo of fighters: both fighter designs, the satellite stays.
    w.order(hauler, byKind(OrderKind::LoadCargo, CargoKind::Fighters));
    w.move();
    CHECK(w.v(hauler).orders.empty());
    CHECK(w.v(hauler).cargo.unitCount(wasp) == 2);
    CHECK(w.v(hauler).cargo.unitCount(hornet) == 3);
    CHECK(w.v(hauler).cargo.unitCount(sat) == 0);
    CHECK(w.s.colony(home)->cargo.units == std::vector<UnitStack>{{sat, 1}});
    // Drop Cargo of fighters into the other colony picked: both designs go there.
    Order drop = byKind(OrderKind::DropCargo, CargoKind::Fighters);
    drop.object = other;
    w.order(hauler, drop);
    w.move();
    CHECK(w.v(hauler).cargo.units.empty());
    CHECK(w.s.colony(other)->cargo.unitCount(wasp) == 2);
    CHECK(w.s.colony(other)->cargo.unitCount(hornet) == 3);
    // Population is a kind too: Load Cargo of population takes people, leaving the colony 1M.
    w.s.colony(other)->population = {{kA, 3}};
    w.order(hauler, byKind(OrderKind::LoadCargo, CargoKind::Population));
    w.move();
    CHECK(w.v(hauler).cargo.totalPopulation() > 0);
    CHECK(w.s.colony(other)->totalPopulation() >= 1);
    // A Drop of a kind the ship does not carry drops nothing, and fails (spec 03 §8).
    w.order(hauler, byKind(OrderKind::DropCargo, CargoKind::Mines));
    w.move();
    CHECK(w.logged(kA, "Drop Cargo order cancelled"));
}

TEST_CASE("unit orders: Launch Units launches every design of the kind, with warnings for a human owner") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const Location here = at(a, 6, 6);
    const DesignId mineA = w.design(kA, "Mine A", "Mv Mine Hull", {"Test Warhead"});
    const DesignId mineB = w.design(kA, "Mine B", "Mv Mine Hull", {"Test Warhead", "Test Warhead"});
    const VehicleId layer = w.spawn(w.ship(kA, "Layer", 3, {"Mv Mine Layer"}), here);
    w.v(layer).cargo.units = {{mineA, 2}, {mineB, 2}};
    w.order(layer, byKind(OrderKind::LaunchUnits, CargoKind::Mines));
    w.move();
    // Lay Mines Val 2 = 5 a game turn: every design of the kind, in cargo order.
    CHECK(inSpace(r, w.s, kA, here, VehicleType::Mine) == 4);
    CHECK(w.v(layer).cargo.units.empty());
    CHECK(warnings(w.s, kA, "Launch Warning") == 0);

    // The per-sector cap: refused at it, cut to the room left below it, with a warning.
    w.s.empire(kA).log.clear();
    const VehicleId field = [&] {
        for (const Vehicle& v : w.s.vehicles)
            if (v.count > 0 && v.owner == kA && vehicleType(r, w.s, v) == VehicleType::Mine) return v.id;
        return VehicleId{};
    }();
    REQUIRE(field.valid());
    w.v(field).count = 98;
    setGroupStacks(w.s, w.v(field), {{mineA, 98}});
    w.v(layer).cargo.units = {{mineB, 4}};
    w.order(layer, byKind(OrderKind::LaunchUnits, CargoKind::Mines));
    w.move();
    CHECK(inSpace(r, w.s, kA, here, VehicleType::Mine) == 100);
    CHECK(w.v(layer).cargo.unitCount(mineB) == 2);
    CHECK(warnings(w.s, kA, "Launch Warning") == 1);
    CHECK(w.logged(kA, "At most 100 of our mines"));

    // A kind that is never launched, and a ship without the launcher: warnings.
    w.s.empire(kA).log.clear();
    const DesignId troop = w.design(kA, "Marine", "Test Troop Hull", {"Test Troop Rifle"});
    const DesignId wasp = w.design(kA, "Wasp", "Test Fighter Hull", {"Mv Fighter Engine", "Mv Fighter Tank"});
    w.v(layer).cargo.units = {{troop, 1}, {wasp, 1}};
    w.order(layer, byKind(OrderKind::LaunchUnits, CargoKind::Troops));
    w.order(layer, byKind(OrderKind::LaunchUnits, CargoKind::Fighters));
    w.move();
    CHECK(w.v(layer).orders.empty());   // Launch Units is always done
    CHECK(w.v(layer).cargo.unitCount(troop) == 1);
    CHECK(w.v(layer).cargo.unitCount(wasp) == 1);
    CHECK(warnings(w.s, kA, "Launch Warning") == 2);
    CHECK(w.logged(kA, "cannot launch troops"));
    CHECK(w.logged(kA, "has nothing that can launch fighters"));

    // A computer player's launches log nothing.
    w.s.empire(kB).kind = PlayerKind::Computer;
    const DesignId theirs = w.design(kB, "Their Mine", "Mv Mine Hull", {"Test Warhead"});
    const VehicleId their = w.spawn(w.ship(kB, "Their Layer", 3), at(a, 2, 2));
    w.v(their).cargo.units = {{theirs, 3}};
    w.order(their, byKind(OrderKind::LaunchUnits, CargoKind::Mines));
    w.move();
    CHECK(w.v(their).cargo.unitCount(theirs) == 3);
    CHECK(warnings(w.s, kB, "Launch Warning") == 0);
}

TEST_CASE("unit orders: Recover Units by kind, and its warnings") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const Location here = at(a, 3, 3);
    const DesignId sat = w.design(kA, "Eye", "Test Satellite Hull", {"Test Satellite Gun"});
    const DesignId sat2 = w.design(kA, "Big Eye", "Test Satellite Hull", {"Test Satellite Gun", "Test Satellite Gun"});
    const VehicleId tender = w.spawn(w.ship(kA, "Tender", 3, {"Mv Satellite Bay"}), here);
    const VehicleId group = w.spawn(sat, here);
    addGroupUnits(w.s, w.v(group), sat2, 2);
    w.order(tender, byKind(OrderKind::RecoverUnits, CargoKind::Satellites));
    w.move();
    CHECK(inSpace(r, w.s, kA, here, VehicleType::Satellite) == 0);
    CHECK(w.v(tender).cargo.unitCount(sat) == 1);
    CHECK(w.v(tender).cargo.unitCount(sat2) == 2);
    CHECK(warnings(w.s, kA, "Recover Warning") == 0);
    // Drones never come back; a ship with no bay of the kind recovers nothing.
    const DesignId wasp = w.design(kA, "Wasp", "Test Fighter Hull", {"Mv Fighter Engine", "Mv Fighter Tank"});
    w.spawn(wasp, here);
    w.order(tender, byKind(OrderKind::RecoverUnits, CargoKind::Drones));
    w.order(tender, byKind(OrderKind::RecoverUnits, CargoKind::Fighters));
    w.move();
    CHECK(inSpace(r, w.s, kA, here, VehicleType::Fighter) == 1);
    CHECK(warnings(w.s, kA, "Recover Warning") == 2);
    CHECK(w.logged(kA, "Only fighters and satellites can be recovered."));
    CHECK(w.logged(kA, "has nothing that can recover fighters"));
}

TEST_CASE("unit orders: orders by kind and the window's commands survive the save") {
    World w;
    const SystemId a = w.system("A");
    const VehicleId ship = w.spawn(w.ship(kA, "Hauler", 3), at(a, 1, 1));
    w.order(ship, byKind(OrderKind::LoadCargo, CargoKind::WeaponPlatforms, at(a, 2, 2)));
    const auto bytes = serializeState(w.s);
    auto back = deserializeState(bytes);
    REQUIRE_MESSAGE(back.has_value(), (back ? std::string{} : back.error()));
    REQUIRE(back->vehicle(ship));
    CHECK(back->vehicle(ship)->orders == w.v(ship).orders);
    CHECK(back->vehicle(ship)->orders.front().cargo == CargoKind::WeaponPlatforms);
}

TEST_CASE("unit orders: the Launch \\ Recover Units window acts at once on the holder named") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const Location here = at(a, 5, 5);
    const DesignId wasp = w.design(kA, "Wasp", "Test Fighter Hull", {"Mv Fighter Engine", "Mv Fighter Tank"});
    const VehicleId carrier = w.spawn(w.ship(kA, "Carrier", 3, {"Mv Fighter Bay"}), here);
    const VehicleId mate = w.spawn(w.ship(kA, "Mate", 3, {"Mv Fighter Bay"}), here);
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pack", {carrier, mate}}).ok);
    w.v(carrier).cargo.units = {{wasp, 5}};
    w.v(mate).cargo.units = {{wasp, 5}};
    const Order kept = Order{OrderKind::MoveTo, at(a, 9, 9)};
    REQUIRE(apply(r, w.s, kA, cmd::SetOrders{carrier, {}, {kept}, false}).ok);
    const std::vector<Order> before = w.v(carrier).orders;

    // Only in a turn-based game, and in the empire's own turn.
    CHECK_FALSE(apply(r, w.s, kA, cmd::LaunchUnitsNow{carrier, {}, wasp, -1}).ok);
    turnBasedTurnOf(w, kA);
    CHECK_FALSE(apply(r, w.s, kB, cmd::LaunchUnitsNow{carrier, {}, wasp, -1}).ok);

    // The check tells what a click would do: the bay's 3 a turn.
    const movement::UnitTransfer plan = movement::checkLaunch(r, w.s, carrier, {}, wasp, -1);
    CHECK(plan.moved == 3);
    CHECK(plan.warnings.empty());
    const size_t logged = w.s.empire(kA).log.size();
    REQUIRE(apply(r, w.s, kA, cmd::LaunchUnitsNow{carrier, {}, wasp, 2}).ok);
    CHECK(w.v(carrier).cargo.unitCount(wasp) == 3);
    CHECK(w.v(mate).cargo.unitCount(wasp) == 5);         // exactly the holder named
    CHECK(w.v(carrier).orders == before);                // the list is not touched or run
    CHECK(w.s.empire(kA).log.size() == logged);          // nothing is logged
    CHECK(inSpace(r, w.s, kA, here, VehicleType::Fighter) == 2);
    // The turn's budget counts: one more this turn, then none.
    CHECK(movement::checkLaunch(r, w.s, carrier, {}, wasp, -1).moved == 1);
    REQUIRE(apply(r, w.s, kA, cmd::LaunchUnitsNow{carrier, {}, wasp, -1}).ok);
    CHECK(w.v(carrier).cargo.unitCount(wasp) == 2);
    CHECK_FALSE(apply(r, w.s, kA, cmd::LaunchUnitsNow{carrier, {}, wasp, -1}).ok);
    // A launched group has its full movement in a turn-based game.
    VehicleId group;
    for (const Vehicle& v : w.s.vehicles)
        if (v.count > 0 && v.design == wasp) group = v.id;
    REQUIRE(group.valid());
    CHECK(w.v(group).movement > 0);

    // Recovery into the holder named, the group's every design, up to the amount.
    REQUIRE(apply(r, w.s, kA, cmd::RecoverUnitsNow{mate, {}, group, {}, 2}).ok);
    CHECK(w.v(mate).cargo.unitCount(wasp) == 7);
    CHECK(w.v(carrier).cargo.unitCount(wasp) == 2);
    CHECK(w.v(group).count == 1);
    // A fighter group that has moved cannot come back aboard in a turn-based game: a warning.
    w.v(group).movement = 0;
    const movement::UnitTransfer moved = movement::checkRecover(r, w.s, mate, {}, group, {}, -1);
    CHECK(moved.moved == 0);
    REQUIRE(moved.warnings.size() == 1);
    CHECK(moved.warnings.front().find("full movement") != std::string::npos);
    CHECK_FALSE(apply(r, w.s, kA, cmd::RecoverUnitsNow{mate, {}, group, {}, -1}).ok);

    // A colony launches without a bay, up to 1000 a turn.
    const ObjectId home = w.planet(a, {5, 5});
    w.colony(home, kA).cargo.units = {{wasp, 4}};
    REQUIRE(apply(r, w.s, kA, cmd::LaunchUnitsNow{{}, home, wasp, -1}).ok);
    CHECK(w.s.colony(home)->cargo.units.empty());
}
