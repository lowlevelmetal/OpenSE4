// Vehicle rules settled from the executable on 2026-10-14 (docs/spec/03 §6,
// §8, §9, §11, §12, §15; spec 04 §15): cargo orders, colonization, the
// silent failures and OpenSE4's "Explain failed orders", movement limits,
// self-destruct, tagged orders and fleet names. All content is invented.

#include "movement_fixture.hpp"

#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"
#include "game/orders.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>

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
Order moveTo(SystemId s, int x, int y) { return Order{OrderKind::MoveTo, at(s, x, y)}; }

// Gives the vehicle a huge fuel cell and fills it, as test_movement.cpp does.
void fuel(World& w, VehicleId id) {
    Design d = w.s.design(w.v(id).design);
    d.name += " (fuelled)";
    d.entries.push_back({test::componentIndex(w.rules(), "Mv Fuel Cell"), -1});
    const DesignId fuelled = addDesign(w.s, std::move(d));
    Vehicle& v = w.v(id);
    v.design = fuelled;
    v.damage.resize(w.s.design(fuelled).entries.size(), 0);
    v.supply = 1'000'000;
}

// A turn-based run of empire `e`'s orders, with the human's questions and
// boxes (LiveMove::ask), as a player's own order sets it off: the boxes it raised.
std::vector<PlayerMessage> runLiveTurn(World& w, EmpireId e, bool explain = false) {
    TurnContext ctx{w.rules(), w.s, {}, {}, {}};
    ctx.explainFailedOrders = explain;
    movement::startTurn(ctx, e);   // the player's turn: movement back to the maximum
    movement::LiveMove m{e};
    m.ask = true;
    movement::runLive(ctx, m, {});
    w.s.removeDeadVehicles();
    return ctx.messages;
}

int unitsIn(const Cargo& c, DesignId d) { return c.unitCount(d); }

} // namespace

// ---- Load Cargo (findings 1, 2) -----------------------------------------------------------------------

TEST_CASE("vehicle rules: Load Cargo takes from own colonies, then own bases that are not cloaked, never from ships; a plague is no bar") {
    World w;
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {4, 4});
    const DesignId fighter = w.design(kA, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Mv Fighter Tank"});
    Colony& c = w.colony(home, kA, 1000);
    c.cargo.units.push_back({fighter, 2});
    c.plagueLevel = 3;   // only the Cargo Transfer window refuses a plagued planet (spec 03 §11)
    const DesignId depotDesign = w.design(kA, "Depot", "Test Station", {"Test Bridge", "Test Cargo Bay"});
    const VehicleId depot = w.spawn(depotDesign, at(a, 4, 4));
    w.v(depot).cargo.units.push_back({fighter, 1});
    const VehicleId hidden = w.spawn(depotDesign, at(a, 4, 4));
    w.v(hidden).cargo.units.push_back({fighter, 1});
    w.v(hidden).status = VehicleStatus::Cloaked;
    const VehicleId donor = w.spawn(w.ship(kA, "Donor", 2, {"Test Cargo Bay"}), at(a, 4, 4));
    w.v(donor).cargo.units.push_back({fighter, 1});
    const VehicleId hauler = w.spawn(w.ship(kA, "Hauler", 2, {"Test Cargo Bay", "Test Cargo Bay", "Test Cargo Bay"}), at(a, 4, 4));
    fuel(w, hauler);
    w.order(hauler, byKind(OrderKind::LoadCargo, CargoKind::Fighters));
    w.move();
    CHECK(w.v(hauler).orders.empty());
    CHECK(unitsIn(w.v(hauler).cargo, fighter) == 3);
    CHECK(unitsIn(w.s.colony(home)->cargo, fighter) == 0);   // the plagued colony gave its two
    CHECK(unitsIn(w.v(depot).cargo, fighter) == 0);
    CHECK(unitsIn(w.v(hidden).cargo, fighter) == 1);          // a cloaked base is no source
    CHECK(unitsIn(w.v(donor).cargo, fighter) == 1);           // nor is a ship

    // Population from a plagued colony too, the colony keeping 1M.
    w.order(hauler, byKind(OrderKind::LoadCargo, CargoKind::Population));
    w.move();
    CHECK(w.v(hauler).cargo.totalPopulation() > 0);
}

// ---- Drop Cargo (findings 3, 17, 18) -----------------------------------------------------------------

TEST_CASE("vehicle rules: a group's Drop Cargo succeeds or fails on its last living member alone") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {4, 4});
    w.colony(home, kA, 100);
    const DesignId fighter = w.design(kA, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Mv Fighter Tank"});
    const VehicleId one = w.spawn(w.ship(kA, "One", 2, {"Test Cargo Bay"}), at(a, 4, 4));
    const VehicleId two = w.spawn(w.ship(kA, "Two", 2, {"Test Cargo Bay"}), at(a, 4, 4));
    fuel(w, one);
    fuel(w, two);
    REQUIRE(apply(r, w.s, kA, cmd::CreateFleet{"Pair", {one, two}}).ok);
    const FleetId f = w.s.fleets.back().id;
    REQUIRE(fleetGroup(w.s, *w.s.fleet(f)) == std::vector<VehicleId>{one, two});
    auto give = [&](std::vector<Order> list) {
        for (VehicleId id : fleetGroup(w.s, *w.s.fleet(f))) w.v(id).orders = list;
    };

    // The first member drops, the last has nothing: the order fails and the lists are cleared.
    w.v(one).cargo.units = {{fighter, 2}};
    give({byKind(OrderKind::DropCargo, CargoKind::Fighters), moveTo(a, 8, 8)});
    w.move();
    CHECK(unitsIn(w.s.colony(home)->cargo, fighter) == 2);
    CHECK(w.v(one).orders.empty());
    CHECK(w.v(two).orders.empty());
    CHECK(w.v(one).location == at(a, 4, 4));
    CHECK(w.s.empire(kA).log.empty());   // the original says nothing (spec 03 §8)

    // The first has nothing, the last drops: done, and the next order runs.
    w.v(two).cargo.units = {{fighter, 1}};
    give({byKind(OrderKind::DropCargo, CargoKind::Fighters), moveTo(a, 8, 8)});
    w.move();
    CHECK(unitsIn(w.s.colony(home)->cargo, fighter) == 3);
    CHECK(w.v(one).location != at(a, 4, 4));
}

TEST_CASE("vehicle rules: Load and Drop that find no room give a box to the human whose turn it is, and nothing else") {
    World w;
    w.s.options.simultaneous = false;
    const SystemId a = w.system("A");
    const ObjectId home = w.planet(a, {4, 4});
    const DesignId fighter = w.design(kA, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Mv Fighter Tank"});
    Colony& c = w.colony(home, kA, 1000);
    c.cargo.units.push_back({fighter, 5});
    // A hold already full: nothing comes aboard.
    const VehicleId full = w.spawn(w.ship(kA, "Full", 2, {"Test Cargo Bay"}), at(a, 4, 4));
    w.v(full).cargo.units = {{fighter, 2}};   // 2 × 20 kT of the bay's 50
    w.order(full, byKind(OrderKind::LoadCargo, CargoKind::Fighters));
    std::vector<PlayerMessage> boxes = runLiveTurn(w, kA);
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0].title == "Load Cargo");
    CHECK(boxes[0].text.find("Full") != std::string::npos);
    CHECK(w.v(full).orders.empty());   // Load is always done
    CHECK(unitsIn(w.v(full).cargo, fighter) == 2);

    // A colony with no room left: the drop gives the box and fails.
    Colony& col = *w.s.colony(home);
    const int64_t room = colonyCargoCapacity(w.rules(), w.s, col) - cargoSpaceUsed(w.rules(), w.s, col.cargo);
    col.cargo.units = {{fighter, static_cast<int>(room / 20) + 5}};
    REQUIRE(colonyCargoCapacity(w.rules(), w.s, col) - cargoSpaceUsed(w.rules(), w.s, col.cargo) < 20);
    w.order(full, byKind(OrderKind::DropCargo, CargoKind::Fighters));
    w.order(full, moveTo(a, 6, 6));
    boxes = runLiveTurn(w, kA);
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0].title == "Drop Cargo");
    CHECK(w.v(full).orders.empty());
    CHECK(w.v(full).location == at(a, 4, 4));

    // Nothing of the kind aboard: a silent failure, explained only with OpenSE4's option.
    w.order(full, byKind(OrderKind::DropCargo, CargoKind::Mines));
    CHECK(runLiveTurn(w, kA).empty());
    CHECK(w.v(full).orders.empty());
    w.order(full, byKind(OrderKind::DropCargo, CargoKind::Mines));
    boxes = runLiveTurn(w, kA, true);
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0].title == "Drop Cargo");
    CHECK(w.s.empire(kA).log.empty());   // the boxes are the news: nothing logged

    // A simultaneous game writes no entry for either.
    w.s.options.simultaneous = true;
    w.order(full, byKind(OrderKind::LoadCargo, CargoKind::Fighters));
    w.order(full, byKind(OrderKind::DropCargo, CargoKind::Fighters));
    w.move();
    CHECK(w.s.empire(kA).log.empty());
}

TEST_CASE("vehicle rules: a silent failure says nothing; with Explain failed orders it is an entry, or a box in the player's turn") {
    World w;
    const SystemId a = w.system("A"), b = w.system("B", 10, 0);   // nothing leads to B
    const VehicleId ship = w.spawn(w.ship(kA, "Lost", 3), at(a, 0, 0));
    fuel(w, ship);
    const Order waypoint{OrderKind::MoveToWaypoint};   // amount 0: waypoint 1, unset
    for (const Order& o : {moveTo(b, 3, 3), waypoint}) {
        w.explainFailedOrders = false;
        w.s.empire(kA).log.clear();
        w.order(ship, o);
        w.move();
        CHECK(w.v(ship).orders.empty());
        CHECK(w.s.empire(kA).log.empty());
        w.explainFailedOrders = true;
        w.order(ship, o);
        w.move();
        CHECK(w.v(ship).orders.empty());
        REQUIRE(w.s.empire(kA).log.size() == 1);
        CHECK(w.s.empire(kA).log[0].title == "Unable to Move");
    }
    // A turn-based player's turn: a box with the option, nothing without.
    w.s.options.simultaneous = false;
    w.s.empire(kA).log.clear();
    w.order(ship, moveTo(b, 3, 3));
    CHECK(runLiveTurn(w, kA).empty());
    w.order(ship, moveTo(b, 3, 3));
    const std::vector<PlayerMessage> boxes = runLiveTurn(w, kA, true);
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0].title == "Move");
    CHECK(w.s.empire(kA).log.empty());

    // A space yard building is the one Move failure the original reports, whatever the option.
    const VehicleId yard = w.spawn(w.ship(kA, "Yard", 2, {"Test Yard Module"}), at(a, 2, 2));
    QueueItem item;
    item.kind = QueueItem::Kind::Vehicle;
    item.design = w.s.vehicle(ship)->design;
    w.v(yard).queue.items.push_back(item);
    w.order(yard, moveTo(a, 3, 3));
    const std::vector<PlayerMessage> busy = runLiveTurn(w, kA);
    REQUIRE(busy.size() == 1);
    CHECK(busy[0].title == "Move");
    CHECK(w.v(yard).location == at(a, 2, 2));
}

// ---- Colonization (findings 4, 8, 9, 10, 18, 19) ------------------------------------------------------

TEST_CASE("vehicle rules: a colony founded: minister flag, 100 experience, the colony ship counted as scrapped") {
    World w;
    const SystemId a = w.system("A");
    const ObjectId target = w.planet(a, {5, 6});
    const DesignId settlerDesign = w.ship(kA, "Settler", 2, {"Test Rock Pod"});
    const VehicleId ship = w.spawn(settlerDesign, at(a, 5, 6));
    w.v(ship).cargo.population.push_back({kA, 1});
    Empire& e = w.s.empire(kA);
    e.ministersForNewVehicles = true;
    e.experience = 7;
    const int built = w.s.design(settlerDesign).built;
    w.order(ship, Order{OrderKind::Colonize, {}, target});
    w.move();
    REQUIRE(w.s.colony(target));
    CHECK(w.s.colony(target)->minister);
    CHECK(w.s.empire(kA).experience == 107);
    CHECK(w.s.design(settlerDesign).scrapped == 1);
    CHECK(w.s.design(settlerDesign).built - w.s.design(settlerDesign).lost - w.s.design(settlerDesign).scrapped == built - 1);

    // Without the option the flag stays off.
    const ObjectId second = w.planet(a, {7, 7});
    e.ministersForNewVehicles = false;
    const VehicleId other = w.spawn(settlerDesign, at(a, 7, 7));
    w.v(other).cargo.population.push_back({kA, 1});
    w.order(other, Order{OrderKind::Colonize, {}, second});
    w.move();
    REQUIRE(w.s.colony(second));
    CHECK_FALSE(w.s.colony(second)->minister);
    CHECK(w.s.empire(kA).experience == 207);
}

TEST_CASE("vehicle rules: colonization unloads the ship by the Drop Cargo rules, kind by kind; a unit too large is skipped") {
    ruleset::Ruleset rs = mvtest::buildRuleset();
    rs.settings.set("Automatic Colonization Population", "4");
    rs.reindex();
    const Rules r{std::move(rs)};
    World w(r);
    w.s.options.simultaneous = false;
    const SystemId a = w.system("A");
    const ObjectId target = w.planet(a, {5, 6});
    const DesignId settlerDesign = w.ship(kA, "Settler", 2, {"Test Rock Pod"});
    // Units: fighters and troops, 20 kT each; troops land first whatever the cargo order.
    const DesignId trooper = w.design(kA, "Trooper", "Test Troop Hull", {"Test Troop Armor"});
    const DesignId wasp = w.design(kA, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Mv Fighter Tank"});
    const VehicleId ship = w.spawn(settlerDesign, at(a, 5, 6));
    // An empty colony there to measure the room, then removed again.
    w.colony(target, kA, 0);
    const int64_t maxPop = maxPopulation(r, w.s, *w.s.colony(target));
    const int64_t cargoRoom = colonyCargoCapacity(r, w.s, *w.s.colony(target));
    w.s.colonies[target.index()].reset();
    REQUIRE(maxPop > 4);
    REQUIRE(cargoRoom >= 40);
    // Another race's people first, then the owner's: each entry in the ship's
    // order, up to the room left after the automatic colonists.
    w.v(ship).cargo.population = {{kB, maxPop}, {kA, 3}};
    const int troops = static_cast<int>(cargoRoom / 20) + 2;
    w.v(ship).cargo.units = {{wasp, 1}, {trooper, troops}};
    w.order(ship, Order{OrderKind::Colonize, {}, target});
    TurnContext ctx{r, w.s, {}, {}, {}};
    movement::startTurn(ctx, kA);
    movement::LiveMove m{kA};
    m.ask = true;
    movement::runLive(ctx, m, {});
    w.s.removeDeadVehicles();
    const Colony* c = w.s.colony(target);
    REQUIRE(c);
    // The owner's automatic colonists, then the other race fills the room; the owner's 3 aboard find none.
    CHECK(c->totalPopulation() == maxPop);
    auto pop = [&](EmpireId race) {
        for (const PopulationGroup& p : c->population)
            if (p.race == race) return p.millions;
        return int64_t{0};
    };
    CHECK(pop(kA) == 4);
    CHECK(pop(kB) == maxPop - 4);
    // Troops come before fighters: they fill the hold, and the fighter finds no room.
    CHECK(c->cargo.unitCount(trooper) == static_cast<int>(cargoRoom / 20));
    CHECK(c->cargo.unitCount(wasp) == 0);
    // What found no room is lost; the player whose turn it is got the Drop Cargo box.
    REQUIRE(ctx.messages.size() == 1);
    CHECK(ctx.messages[0].title == "Drop Cargo");
}
