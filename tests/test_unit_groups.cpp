// Unit groups that mix designs (docs/spec/03 §1, §12; docs/spec/04 §6, §9.4,
// §10.4-§10.7): one group per (owner, unit kind, sector) holding several
// designs, through launch and recovery, supply, movement, damage, mines, the
// save, redaction and combat. All content is invented for the tests.

#include "combat_fixture.hpp"
#include "movement_fixture.hpp"

#include "game/combat.hpp"
#include "game/movement_internal.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "game/tactical.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace opense4;
using namespace opense4::game;
using opense4::mvtest::World;
using ruleset::VehicleType;

namespace {

constexpr EmpireId kA{0u}, kB{1u};

Order launchOf(DesignId d, int amount = -1) {
    Order o;
    o.kind = OrderKind::LaunchUnits;
    o.design = d;
    o.amount = amount;
    return o;
}

Order recoverOf(DesignId d, int amount = -1) {
    Order o = launchOf(d, amount);
    o.kind = OrderKind::RecoverUnits;
    return o;
}

// The owner's unit groups of one kind in a sector.
std::vector<VehicleId> groupsAt(const Rules& r, const GameState& s, EmpireId owner, Location at, ruleset::VehicleType kind) {
    std::vector<VehicleId> out;
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.owner == owner && v.location == at && vehicleType(r, s, v) == kind) out.push_back(v.id);
    return out;
}

Location at(SystemId s, int x, int y) { return Location{s, Sector{x, y}}; }

} // namespace

TEST_CASE("unit groups: launches of several designs join one group per kind and sector") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const Location here = at(a, 5, 5);
    // A slow fighter and a fast one with twice the fuel.
    const DesignId slow = w.design(kA, "Slow Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const DesignId fast = w.design(kA, "Fast Wasp", "Test Fighter Hull",
                                   {"Test Fighter Engine", "Test Fighter Engine", "Mv Fighter Tank", "Mv Fighter Tank"});
    const VehicleId carrier = w.spawn(w.ship(kA, "Carrier", 3, {"Mv Fighter Bay", "Mv Fighter Bay"}), here);
    w.v(carrier).cargo.units = {{slow, 2}, {fast, 2}};
    w.order(carrier, launchOf(slow));
    w.order(carrier, launchOf(fast));
    w.move();
    w.move();
    const std::vector<VehicleId> groups = groupsAt(r, w.s, kA, here, VehicleType::Fighter);
    REQUIRE(groups.size() == 1);   // one group mixing both designs
    const Vehicle& g = w.v(groups.front());
    CHECK(g.count == 4);
    CHECK(g.design == slow);       // the first design to join
    CHECK(groupUnits(g, slow) == 2);
    CHECK(groupUnits(g, fast) == 2);
    CHECK(g.mixed.size() == 2);
    CHECK(w.v(carrier).cargo.units.empty());
    // Supply is every unit's (2 × 12 + 2 × 24) and launching refilled it.
    CHECK(vehicleSupplyCapacity(r, w.s, g) == 2 * 12 + 2 * 24);
    CHECK(g.supply == 72);
    // The group moves at its slowest design's speed; out of supply at 1.
    CHECK(vehicleMaxMovement(r, w.s, g) == 1);
    // A step costs every unit's engines, design by design.
    Vehicle one = g;
    setGroupStacks(w.s, one, {{slow, 1}});
    const int64_t slowStep = movement::moveSupplyCost(r, w.s, one);
    setGroupStacks(w.s, one, {{fast, 1}});
    const int64_t fastStep = movement::moveSupplyCost(r, w.s, one);
    CHECK(movement::moveSupplyCost(r, w.s, g) == 2 * slowStep + 2 * fastStep);
    CHECK(fastStep == 2 * slowStep);

    // Recovery takes one design out; the rest stays a group of the other design.
    w.order(carrier, recoverOf(slow));
    w.move();
    REQUIRE(w.s.vehicle(groups.front()));
    const Vehicle& left = w.v(groups.front());
    CHECK(left.count == 2);
    CHECK(left.design == fast);
    CHECK(left.mixed.empty());
    CHECK(vehicleMaxMovement(r, w.s, left) == 2);   // the slow design is gone
    CHECK(left.supply <= vehicleSupplyCapacity(r, w.s, left));
    CHECK(w.v(carrier).cargo.unitCount(slow) == 2);
}

TEST_CASE("unit groups: the per-sector caps count every design") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const Location here = at(a, 7, 7);
    const DesignId mineA = w.design(kA, "Mine A", "Mv Mine Hull", {"Test Warhead"});
    const DesignId mineB = w.design(kA, "Mine B", "Mv Mine Hull", {"Test Warhead", "Test Warhead"});
    const VehicleId field = w.spawn(mineA, here);
    w.v(field).count = 97;
    const VehicleId layer = w.spawn(w.ship(kA, "Layer", 3, {"Mv Mine Layer"}), here);
    w.v(layer).cargo.units = {{mineB, 10}};
    w.order(layer, launchOf(mineB));
    w.move();
    CHECK(w.v(field).count == 100);   // cut to the room left, across designs
    CHECK(groupUnits(w.v(field), mineB) == 3);
    CHECK(groupsAt(r, w.s, kA, here, VehicleType::Mine).size() == 1);
    CHECK(w.v(layer).cargo.unitCount(mineB) == 7);

    // Sweepers clear a minefield that mixes designs in the order its mines were laid.
    const DesignId sweeper = w.ship(kB, "Sweeper", 3, {"Mv Sweeper", "Mv Sweeper"});
    w.setTreaty(kA, kB, Treaty::War);
    const VehicleId sw = w.spawn(sweeper, here);
    TurnContext ctx{r, w.s, {}, {}, {}};
    movement::detail::sweepMines(ctx, sw);
    CHECK(w.v(field).count == 94);
    CHECK(groupUnits(w.v(field), mineA) == 91);
    CHECK(groupUnits(w.v(field), mineB) == 3);
    CHECK(w.s.design(mineA).lost == 6);
}

TEST_CASE("unit groups: damage outside combat kills whole units; the save keeps the designs") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const Location here = at(a, 3, 3);
    const DesignId light = w.design(kA, "Light Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Mv Fighter Tank"});
    const DesignId heavy = w.design(kA, "Heavy Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const VehicleId id = w.spawn(light, here);
    Vehicle& g = w.v(id);
    addGroupUnits(w.s, g, light, 2);   // 3 light
    addGroupUnits(w.s, g, heavy, 2);
    REQUIRE(g.count == 5);
    REQUIRE(g.mixed.size() == 2);
    g.supply = vehicleSupplyCapacity(r, w.s, g);

    // The save: stacks round-trip; a group whose stacks do not add up is refused.
    const auto bytes = serializeState(w.s);
    auto back = deserializeState(bytes);
    REQUIRE(back.has_value());
    CHECK(back->vehicle(id)->mixed == g.mixed);
    CHECK(validateState(*back, &r).empty());
    GameState broken = *back;
    broken.vehicle(id)->count = 9;
    CHECK_FALSE(validateState(broken, &r).empty());

    // One light unit's structure (engine 5 + tank 5) kills one unit, whichever design is drawn.
    const int lightHp = vehicleStructure(r, w.s, *w.s.vehicle(id));
    CHECK_FALSE(movement::damageVehicle(r, w.s, g, lightHp));
    CHECK(g.count == 4);
    CHECK(w.s.design(light).lost + w.s.design(heavy).lost == 1);
    CHECK(std::all_of(g.damage.begin(), g.damage.end(), [](int d) { return d == 0; }));   // no partial damage
    // Enough for everyone destroys the group.
    CHECK(movement::damageVehicle(r, w.s, g, 1000));
    CHECK(g.count == 0);
    CHECK(w.s.design(light).lost + w.s.design(heavy).lost == 5);
}

TEST_CASE("unit groups: every design of a foreign group is seen") {
    World w;
    const Rules& r = w.rules();
    const SystemId a = w.system("A");
    const Location here = at(a, 4, 4);
    w.exploreAll(kA);
    const DesignId one = w.design(kB, "Their Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Mv Fighter Tank"});
    const DesignId two = w.design(kB, "Their Hornet", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Mv Fighter Tank"});
    const VehicleId id = w.spawn(one, here);
    addGroupUnits(w.s, w.v(id), two, 3);
    w.s.empire(kA).knowledge.visibleVehicles = {id};
    const GameState view = redactForEmpire(w.s, kA);
    CHECK(view.design(one).name == "Their Wasp");
    CHECK(view.design(two).name == "Their Hornet");   // not "Unknown design"
    CHECK(view.vehicle(id)->mixed.size() == 2);
    CHECK(validateState(view, &r).empty());
}

// ---- Combat --------------------------------------------------------------------------------------------

using namespace opense4::ctest;

TEST_CASE("combat: a unit group that mixes designs fights as one piece") {
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    // Satellites of two designs in one group: each unit fires each of its weapons.
    const DesignId gunSat = design(s, ar.a, "Gun Sat", "Test Satellite Hull", {"Test Satellite Gun", "Test Armor Plate"});
    const DesignId laserSat = design(s, ar.a, "Laser Sat", "Test Satellite Hull", {"Test Laser", "Test Armor Plate"});
    const VehicleId group = spawn(s, gunSat, ar.loc, 3);
    addGroupUnits(s, *s.vehicle(group), laserSat, 2);
    useStrategy(s, ar.b, {{"Primary Movement Strategy", "Point Blank"}});
    const DesignId gunship = frigate(s, ar.b, "Gunship", 3, {"CT Gun", "CT Gun", "CT Big Armor"});
    spawn(s, gunship, ar.loc);

    // Stepped with the owner as a player: the battle waits at its first phase.
    combat::TacticalBattle battle(r, s, {ar.loc, std::vector<VehicleId>{}, {ar.a}});
    REQUIRE(battle.started());
    int piece = -1;
    for (size_t i = 0; i < battle.pieces().size(); ++i)
        if (battle.pieces()[i].vehicle == group) piece = static_cast<int>(i);
    REQUIRE(piece >= 0);
    const combat::TacticalPiece& p = battle.pieces()[static_cast<size_t>(piece)];
    CHECK(p.kind == CombatPiece::Kind::UnitGroup);
    CHECK(p.count == 5);
    CHECK(p.units.size() == 2);
    REQUIRE(p.weapons.size() == 2);   // one per design
    CHECK(p.weapons[0].instances == 3);
    CHECK(p.weapons[1].instances == 2);
    CHECK(p.budget >= 5);             // a satellite group engages as many targets as it has units
    CHECK(battle.record().pieces[static_cast<size_t>(piece)].count == 5);

    // The same battle for real: losses come out of both designs, and the group keeps what is left.
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    const int lostUnits = s.design(gunSat).lost + s.design(laserSat).lost;
    int recorded = 0;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::UnitsLost && static_cast<int>(e.piece) == piece) recorded += e.amount;
    CHECK(recorded == lostUnits);
    CHECK(lostUnits > 0);
    if (const Vehicle* v = s.vehicle(group); v && v->count > 0) {
        CHECK(v->count == 5 - lostUnits);
        CHECK(groupUnits(*v, gunSat) == 3 - s.design(gunSat).lost);
        CHECK(groupUnits(*v, laserSat) == 2 - s.design(laserSat).lost);
        CHECK(validateState(s, &r).empty());
    } else {
        CHECK(lostUnits == 5);
    }
    CHECK(s.design(gunship).kills == lostUnits);
}

TEST_CASE("combat: fighters of several designs fire their identical guns as one shot") {
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId wasp = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    const DesignId hornet = design(s, ar.a, "Hornet", "Test Fighter Hull",
                                   {"Test Fighter Engine", "Test Fighter Gun", "Test Fighter Gun", "CT Fighter Fuel"});
    const VehicleId group = spawn(s, wasp, ar.loc, 2);
    addGroupUnits(s, *s.vehicle(group), hornet, 3);
    s.vehicle(group)->supply = vehicleSupplyCapacity(r, s, *s.vehicle(group));
    spawn(s, design(s, ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc);
    combat::TacticalBattle battle(r, s, {ar.loc, std::vector<VehicleId>{}, {ar.a}});
    REQUIRE(battle.started());
    const auto it = std::find_if(battle.pieces().begin(), battle.pieces().end(), [&](const combat::TacticalPiece& p) { return p.vehicle == group; });
    REQUIRE(it != battle.pieces().end());
    REQUIRE(it->weapons.size() == 1);            // the same gun in both designs
    CHECK(it->weapons[0].together == 2 * 1 + 3 * 2);
    CHECK(it->budget == 1);
    // Its speed is the slower design's (both 1 here) and its shots land as combined hits.
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    const CombatRecord& rec = s.combats.front();
    const int gp = pieceOf(rec, group);
    int biggest = 0;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Hit && static_cast<int>(e.piece) == gp) biggest = std::max(biggest, e.amount);
    CHECK(biggest > 12);   // more than two guns' worth in one hit
}

TEST_CASE("combat: units launched in a battle that stay in space join the sector's group") {
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId wasp = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    const DesignId hornet = design(s, ar.a, "Hornet", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel", "CT Fighter Fuel"});
    // A group already in space, and a carrier whose bays are full of another design; the carrier dies.
    const VehicleId group = spawn(s, wasp, ar.loc, 2);
    const VehicleId carrier = spawn(s, frigate(s, ar.a, "Carrier", 1, {"Test Fighter Bay", "Test Fighter Bay"}), ar.loc);
    s.vehicle(carrier)->cargo.units = {{hornet, 4}};
    // The enemy fires only on ships: the carrier dies, its fighters live on.
    useStrategy(s, ar.b, {{"Primary Movement Strategy", "Point Blank"}, {"Dont Fire On Fighters", "TRUE"}});
    spawn(s, frigate(s, ar.b, "Killer", 3, {"CT Big Gun", "CT Big Gun", "CT Big Armor", "CT Big Armor"}), ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    s.removeDeadVehicles();
    CHECK(s.vehicle(carrier) == nullptr);
    const std::vector<VehicleId> groups = groupsAt(r, s, ar.a, ar.loc, VehicleType::Fighter);
    REQUIRE(groups.size() == 1);   // never two fighter groups of one owner here
    CHECK(groups.front() == group);
    const Vehicle& g = *s.vehicle(group);
    CHECK(groupUnits(g, wasp) == 2);
    CHECK(groupUnits(g, hornet) == 4);
    CHECK(g.supply == vehicleSupplyCapacity(r, s, g));   // joining refills the group
    CHECK(validateState(s, &r).empty());
}
