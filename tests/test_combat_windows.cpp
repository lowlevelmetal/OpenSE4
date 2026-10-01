// The logic behind the combat windows (client/classic/screens/combat_logic.hpp,
// docs/spec/06 §1.10): the Strategic Combat forces list, the Combat Piece
// Report lines, the Drop Troops target, the Combat Simulator's rows, items
// and names, and how the session shows battles fought by the strategies. All
// content is invented for the tests.

#include "combat_fixture.hpp"

#include "client/classic/screens/combat_logic.hpp"
#include "client/classic/session.hpp"

#include "game/commands.hpp"
#include "game/query.hpp"
#include "game/serialize.hpp"
#include "game/simulator.hpp"
#include "game/tactical.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::ctest;
using opense4::test::homeworld;
namespace combat = opense4::game::combat;
namespace classic = opense4::client::classic;
using combat::TacticalBattle;
using combat::TacticalOrder;
using OK = combat::TacticalOrder::Kind;

namespace {

CombatEvent ev(CombatEvent::Kind kind, uint8_t round, uint32_t piece, uint32_t target = 0, int x = 0, int y = 0, int amount = 0) {
    CombatEvent e;
    e.kind = kind;
    e.round = round;
    e.piece = piece;
    e.target = target;
    e.x = static_cast<int16_t>(x);
    e.y = static_cast<int16_t>(y);
    e.amount = amount;
    return e;
}

CombatPiece piece(CombatPiece::Kind kind, EmpireId owner, DesignId design, int count = 1) {
    CombatPiece p;
    p.kind = kind;
    p.owner = owner;
    p.design = design;
    p.count = count;
    p.startX = 10;
    p.startY = 10;
    return p;
}

const classic::ForceRow* row(const classic::CombatForces& f, EmpireId e, std::string_view name) {
    for (const classic::ForceSide& side : f.sides())
        if (side.empire == e)
            for (const classic::ForceRow& r : side.rows)
                if (r.name == name) return &r;
    return nullptr;
}

} // namespace

TEST_CASE("combat windows: the forces list's rows are made at set-up; counts per hull, losses from the highest count") {
    Arena ar = makeArena(5);
    GameState& s = ar.s;
    const DesignId frig = frigate(s, ar.a, "Lancer", 2, {"Test Laser"});
    const DesignId cruiser = design(s, ar.b, "Bulwark", "Test Cruiser", {"Test Laser"});
    const DesignId wasp = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    const Colony& colony = homeworld(s, ar.b);
    CombatRecord rec;
    rec.participants = {ar.a, ar.b};
    rec.pieces = {piece(CombatPiece::Kind::Vehicle, ar.a, frig), piece(CombatPiece::Kind::Vehicle, ar.a, frig),
                  piece(CombatPiece::Kind::UnitGroup, ar.a, wasp, 6), piece(CombatPiece::Kind::Vehicle, ar.b, cruiser),
                  piece(CombatPiece::Kind::Seeker, ar.b, {}), piece(CombatPiece::Kind::Planet, ar.b, {})};
    rec.pieces[5].planet = colony.planet;
    rec.pieces[5].name = "Home";
    rec.events = {
        ev(CombatEvent::Kind::Launch, 1, 2, 0, 11, 11),        // the fighters appear
        ev(CombatEvent::Kind::Seeker, 1, 4, 0, 12, 12),
        ev(CombatEvent::Kind::Destroyed, 2, 1),                // a frigate is lost
        ev(CombatEvent::Kind::UnitsLost, 2, 2, 3, 0, 0, 2),    // two fighters
        ev(CombatEvent::Kind::Captured, 3, 3, 0),              // the cruiser is taken by A
        ev(CombatEvent::Kind::Destroyed, 3, 5),                // the planet falls
    };
    rec.pieces[2].startX = 0;   // launched: off the map until its Launch event
    classic::CombatPlayback play(rec);
    classic::CombatForces forces;
    const Rules& r = combatRules();

    forces.setup(r, s, rec, play.pieces());
    REQUIRE(forces.sides().size() == 2);
    REQUIRE(row(forces, ar.a, "Test Frigate"));
    CHECK(row(forces, ar.a, "Test Frigate")->current == 2);
    CHECK(row(forces, ar.b, "Test Cruiser")->current == 1);
    REQUIRE(row(forces, ar.b, "Home"));
    CHECK(row(forces, ar.b, "Home")->planet);
    CHECK(row(forces, ar.b, "Home")->current == 1);

    // Units launched later of a hull the empire did not have at set-up get no row.
    play.seekRound(1);
    forces.count(r, s, rec, play.pieces());
    CHECK(row(forces, ar.a, "Test Fighter Hull") == nullptr);

    play.seekRound(2);
    forces.count(r, s, rec, play.pieces());
    CHECK(row(forces, ar.a, "Test Frigate")->current == 1);
    CHECK(row(forces, ar.a, "Test Frigate")->lost == 1);

    play.seekRound(3);
    forces.count(r, s, rec, play.pieces());
    // The captured cruiser is lost for B; A had no cruiser at set-up, so it gets no row.
    CHECK(row(forces, ar.b, "Test Cruiser")->current == 0);
    CHECK(row(forces, ar.b, "Test Cruiser")->lost == 1);
    CHECK(row(forces, ar.a, "Test Cruiser") == nullptr);
    CHECK(row(forces, ar.b, "Home")->current == 0);
    CHECK(row(forces, ar.b, "Home")->lost == 1);
    for (const classic::ForceSide& side : forces.sides())
        for (const classic::ForceRow& fr : side.rows) CHECK(fr.name != "Unknown");
}

TEST_CASE("combat windows: a live battle's forces count each stack of a mixed group under its own hull; planets per empire") {
    const Rules& r = combatRules();
    Arena ar = makeArena(5);
    GameState& s = ar.s;
    const DesignId wasp = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
    const DesignId dart = design(s, ar.a, "Dart", "Test Drone Hull", {"Test Engine", "Test Warhead"});
    std::vector<combat::TacticalPiece> pieces(1);
    pieces[0].kind = CombatPiece::Kind::UnitGroup;
    pieces[0].owner = ar.a;
    pieces[0].design = wasp;
    pieces[0].units = {{wasp, 4}, {dart, 2}};
    for (int k = 0; k < 7; ++k) {
        combat::TacticalPiece p;
        p.kind = CombatPiece::Kind::Planet;
        p.owner = ar.b;
        p.name = "World " + std::to_string(k);
        pieces.push_back(p);
    }
    classic::CombatForces forces;
    forces.setup(r, s, pieces);
    REQUIRE(row(forces, ar.a, "Test Fighter Hull"));
    CHECK(row(forces, ar.a, "Test Fighter Hull")->current == 4);
    REQUIRE(row(forces, ar.a, "Test Drone Hull"));
    CHECK(row(forces, ar.a, "Test Drone Hull")->current == 2);
    // At most five planets per empire, the first in piece order.
    CHECK(row(forces, ar.b, "World 4"));
    CHECK(row(forces, ar.b, "World 5") == nullptr);
    pieces[0].units = {{wasp, 1}, {dart, 2}};
    pieces[1].alive = false;
    forces.count(r, s, pieces);
    CHECK(row(forces, ar.a, "Test Fighter Hull")->current == 1);
    CHECK(row(forces, ar.a, "Test Fighter Hull")->lost == 3);
    CHECK(row(forces, ar.b, "World 0")->current == 0);
    CHECK(row(forces, ar.b, "World 0")->lost == 1);
    CHECK(row(forces, ar.b, "World 1")->current == 1);
}

TEST_CASE("combat windows: the piece report lines and the Drop Troops colony") {
    Arena ar = makeArena(9);
    GameState& s = ar.s;
    Colony& colony = homeworld(s, ar.b);
    colony.population = {{ar.b, 10}};
    colony.cargo = {};
    const Location there = locationOf(s.galaxy, colony.planet);
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transport = spawn(s, frigate(s, ar.a, "Transport", 4, {"Test Cargo Bay", "Test Cargo Bay", "CT Combat Thruster"}), there);
    s.vehicle(transport)->cargo.units.push_back({trooper, 4});
    TacticalBattle b(combatRules(), s, TacticalBattle::Setup{there, std::nullopt, {ar.a}});
    REQUIRE(b.started());
    int ship = -1, planet = -1;
    for (size_t j = 0; j < b.pieces().size(); ++j) {
        if (b.pieces()[j].vehicle == transport) ship = int(j);
        if (b.pieces()[j].kind == CombatPiece::Kind::Planet && b.pieces()[j].planet == colony.planet) planet = int(j);
    }
    REQUIRE(ship >= 0);
    REQUIRE(planet >= 0);

    const auto lines = classic::pieceReportLines(combatRules(), b.state(), b.pieces(), ship);
    std::vector<std::string> labels;
    for (const auto& [label, value] : lines) labels.push_back(label);
    CHECK(labels == std::vector<std::string>{"Movement", "Shields", "Damage", "Supply", "Max Targets", "Combat Group", "Formation"});
    CHECK(lines[5].second == "None");
    // Damage taken against the full design structure.
    const combat::TacticalPiece& sp = b.pieces()[size_t(ship)];
    CHECK(sp.fullHitPoints > 0);
    CHECK(lines[2].second == std::format("0/{}", sp.fullHitPoints));
    CHECK(lines[3].second == std::format("{}/{}", sp.supply, sp.supplyCapacity));
    const auto planetLines = classic::pieceReportLines(combatRules(), b.state(), b.pieces(), planet);
    REQUIRE(planetLines.size() >= 4);
    CHECK(planetLines[0] == std::pair<std::string, std::string>{"Population", "10M"});
    CHECK(planetLines[3] == std::pair<std::string, std::string>{"Supply", "Never"});
    CHECK(planetLines[2].second == std::format("0/{}", b.pieces()[size_t(planet)].fullHitPoints));

    // Drop Troops: the adjacent colony of another empire, without a click.
    bool landed = false;
    for (int round = 0; round < 12 && b.awaitingOrders() && !landed; ++round) {
        if (b.distance(ship, planet) > 1) {
            CHECK(classic::dropTroopsColony(b, ship) < 0);
            CHECK_FALSE(b.check(classic::dropTroopsOrder(b, ship)).empty());
            TacticalOrder mv{OK::Move, ar.a, ship};
            mv.x = b.pieces()[size_t(planet)].x + 1;
            mv.y = b.pieces()[size_t(planet)].y + 1;
            if (b.check(mv).empty()) b.submit(mv);
        }
        if (b.distance(ship, planet) <= 1) {
            CHECK(classic::dropTroopsColony(b, ship) == planet);
            const TacticalOrder drop = classic::dropTroopsOrder(b, ship);
            CHECK(drop.kind == OK::DropTroops);
            CHECK(drop.empire == ar.a);
            CHECK(b.submit(drop).empty());
            landed = true;
        } else {
            b.submit(TacticalOrder{OK::EndPhase, ar.a});
        }
    }
    CHECK(landed);
}

TEST_CASE("combat windows: every line of the Combat Piece Report") {
    using combat::TacticalPiece;
    const Rules& r = combatRules();
    Arena ar = makeArena(10);
    GameState& s = ar.s;
    std::vector<TacticalPiece> pieces(6);
    // 0: a ship leading group 3, with a big supply.
    TacticalPiece& lead = pieces[0];
    lead.kind = CombatPiece::Kind::Vehicle;
    lead.name = "Lead";
    lead.movement = 3;
    lead.movementMax = 6;
    lead.shields = 5;
    lead.shieldsMax = 20;
    lead.fullHitPoints = 300;
    lead.hitPoints = 120;
    lead.supply = 150000;
    lead.supplyCapacity = 2500000;
    lead.budget = 2;
    lead.group = 3;
    lead.isLeader = true;
    lead.formation = r.data().formations.empty() ? -1 : 0;
    // 1: its wingman; unlimited supply.
    TacticalPiece& wing = pieces[1];
    wing = lead;
    wing.name = "Wing";
    wing.isLeader = false;
    wing.formation = -1;
    wing.unlimitedSupply = true;
    // 2: a drone group aimed at the leader.
    TacticalPiece& drone = pieces[2];
    drone.kind = CombatPiece::Kind::UnitGroup;
    drone.type = ruleset::VehicleType::Drone;
    drone.droneTarget = 0;
    drone.fullHitPoints = 40;
    drone.hitPoints = 50;   // more than full: taken never goes below 0
    // 3: satellites; 4: a seeker; 5: a planet with plague.
    pieces[3].kind = CombatPiece::Kind::UnitGroup;
    pieces[3].type = ruleset::VehicleType::Satellite;
    pieces[4].kind = CombatPiece::Kind::Seeker;
    pieces[5].kind = CombatPiece::Kind::Planet;
    pieces[5].population = 0;
    pieces[5].plague = 2;

    auto value = [&](int piece, std::string_view label) {
        for (const auto& [l, v] : classic::pieceReportLines(r, s, pieces, piece))
            if (l == label) return v;
        return std::string("(none)");
    };
    CHECK(value(0, "Movement") == "3/6");
    CHECK(value(0, "Shields") == "5/20");
    CHECK(value(0, "Damage") == "180/300");
    CHECK(value(0, "Supply") == "150K/2500K");
    CHECK(value(0, "Max Targets") == "2");
    CHECK(value(0, "Combat Group") == "Group 3 - Leader");
    if (!r.data().formations.empty()) CHECK(value(0, "Formation") == r.data().formations[0].name);
    CHECK(value(1, "Combat Group") == "Group 3 - Wingman");
    CHECK(value(1, "Formation") == "None");
    CHECK(value(1, "Supply") == "Endless");
    CHECK(value(2, "Target") == "Lead");
    CHECK(value(2, "Combat Group") == "(none)");
    CHECK(value(2, "Formation") == "(none)");
    CHECK(value(2, "Damage") == "0/40");
    CHECK(value(3, "Supply") == "Never");
    CHECK(value(4, "Supply") == "None");
    CHECK(value(5, "Population") == "0M");
    CHECK(value(5, "Movement") == "(none)");
    CHECK(value(5, "Conditions") == "Plague 2");
    // A former leader still shows its formation; a drone whose target is gone shows None.
    lead.isLeader = false;
    lead.group = -1;
    CHECK(value(0, "Combat Group") == "None");
    if (!r.data().formations.empty()) CHECK(value(0, "Formation") == r.data().formations[0].name);
    pieces[0].alive = false;
    CHECK(value(2, "Target") == "None");
}

TEST_CASE("combat windows: the report's formation stays with a leader that stops leading") {
    Arena ar = makeArena(12);
    GameState& s = ar.s;
    const DesignId armed = frigate(s, ar.a, "Liner", 1, {"CT Gun"});
    const VehicleId first = spawn(s, armed, ar.loc), second = spawn(s, armed, ar.loc);
    Fleet f;
    f.owner = ar.a;
    f.members = {first, second};
    f.leader = first;
    f.formation = 0;
    const FleetId fid = s.addFleet(f).id;
    for (VehicleId v : f.members) s.vehicle(v)->fleet = fid;
    warpIn(s, spawn(s, design(s, ar.b, "Target", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc));
    TacticalBattle b(combatRules(), s, TacticalBattle::Setup{ar.loc, std::vector<VehicleId>{}, {ar.a}});
    REQUIRE(b.started());
    int lead = -1, wing = -1;
    for (size_t j = 0; j < b.pieces().size(); ++j) {
        if (b.pieces()[j].vehicle == first) lead = int(j);
        if (b.pieces()[j].vehicle == second) wing = int(j);
    }
    REQUIRE(lead >= 0);
    REQUIRE(wing >= 0);
    REQUIRE(b.pieces()[size_t(lead)].isLeader);
    CHECK(b.pieces()[size_t(lead)].formation == 0);
    CHECK(b.pieces()[size_t(wing)].formation == -1);
    while (!b.awaitingOrders() && !b.finished()) b.submit(TacticalOrder{OK::EndPhase, b.phaseEmpire()});
    REQUIRE(b.awaitingOrders());
    REQUIRE(b.submit(TacticalOrder{OK::ClearGroup, ar.a, lead}).empty());
    CHECK_FALSE(b.pieces()[size_t(lead)].isLeader);
    CHECK(b.pieces()[size_t(lead)].formation == 0);
}

TEST_CASE("combat windows: the simulator adds one vehicle a click, lists rows per vehicle and group, and removes rows") {
    Arena ar = makeArena(19);
    GameState& s = ar.s;
    const Rules& r = combatRules();
    const DesignId lancer = frigate(s, ar.a, "Lancer", 3, {"Test Laser"});
    const DesignId wasp = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    const DesignId drone = design(s, ar.a, "Bee", "Test Drone Hull", {"Test Warhead"});
    combat::SimulatorSetup setup;
    setup.viewer = ar.a;
    for (int k = 0; k < combat::kSimulatorMaxSides; ++k) setup.sides.push_back({std::format("Race {}", k + 1), k > 0});
    using Item = combat::SimulatorItem;
    CHECK(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, lancer, {}, 0}));
    CHECK(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, lancer, {}, 0}));
    CHECK(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, lancer, {}, 1}));
    CHECK(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, wasp, {}, 0}));
    CHECK(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, wasp, {}, 0}));
    CHECK(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, drone, {}, 0}));
    CHECK(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, drone, {}, 0}));
    const ObjectId home = homeworld(s, ar.a).planet;
    CHECK(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Planet, {}, home, 1}));
    CHECK_FALSE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Planet, {}, home, 2}));   // once
    // The home system's unowned objects come as neutral obstacles.
    const auto objects = combat::simulatorPlanets(s, ar.a);
    ObjectId neutral;
    for (ObjectId o : objects)
        if (!s.colony(o) && o != home) neutral = o;
    REQUIRE(neutral.valid());
    CHECK(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Planet, {}, neutral, 0}));

    const auto rows = classic::simulatorRows(r, s, setup);
    std::vector<std::string> names;
    for (const auto& row : rows) names.push_back(row.name);
    const std::string star = s.galaxy.object(neutral).name;
    CHECK(names == std::vector<std::string>{"Lancer 0001", "Lancer 0002", "Wasp", "Bee", "Bee", "Lancer 0001", s.galaxy.object(home).name, star});
    CHECK(rows[2].units == 2);         // one fighter group of two
    CHECK(rows[5].side == 1);
    CHECK(rows.back().side == -1);     // neutral
    REQUIRE(combat::simulatorProblem(r, s, setup).empty());

    // The engine names the ships the same way, and the neutral object stands in the battle sector.
    const combat::Simulation sim = combat::buildSimulation(r, s, setup);
    std::vector<std::string> ships;
    for (const Vehicle& v : sim.state.vehicles)
        if (v.location == sim.where && !isUnitType(vehicleType(r, sim.state, v))) ships.push_back(v.name);
    std::sort(ships.begin(), ships.end());
    CHECK(ships == std::vector<std::string>{"Lancer 0001", "Lancer 0001", "Lancer 0002"});
    int copies = 0;
    for (ObjectId o : sim.state.galaxy.system(sim.where.system).objects)
        if (sim.state.galaxy.object(o).name == star) {
            ++copies;
            CHECK_FALSE(sim.state.colony(o));
        }
    CHECK(copies == 1);

    // Removing the fighter group row removes both clicks' worth.
    classic::simulatorRemove(setup, rows[2]);
    CHECK(std::none_of(setup.items.begin(), setup.items.end(), [&](const Item& i) { return i.design == wasp; }));
    CHECK(classic::simulatorRows(r, s, setup).size() == rows.size() - 1);
}

TEST_CASE("combat windows: the simulator numbers each side's ships with one counter for all designs") {
    Arena ar = makeArena(20);
    GameState& s = ar.s;
    const Rules& r = combatRules();
    const DesignId lancer = frigate(s, ar.a, "Lancer", 3, {"Test Laser"});
    const DesignId pike = frigate(s, ar.a, "Pike", 2, {"Test Laser"});
    combat::SimulatorSetup setup;
    setup.viewer = ar.a;
    for (int k = 0; k < combat::kSimulatorMaxSides; ++k) setup.sides.push_back({std::format("Race {}", k + 1), k > 0});
    using Item = combat::SimulatorItem;
    REQUIRE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, lancer, {}, 0}));
    REQUIRE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, pike, {}, 0}));
    REQUIRE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, pike, {}, 1}));
    auto names = [&] {
        std::vector<std::string> out;
        for (const auto& row : classic::simulatorRows(r, s, setup)) out.push_back(row.name);
        return out;
    };
    CHECK(names() == std::vector<std::string>{"Lancer 0001", "Pike 0002", "Pike 0001"});
    // Removing a ship never lowers the counter; it lasts as long as the setup.
    classic::simulatorRemove(setup, classic::simulatorRows(r, s, setup)[0]);
    REQUIRE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, lancer, {}, 0}));
    CHECK(names() == std::vector<std::string>{"Pike 0002", "Lancer 0003", "Pike 0001"});
    const combat::SimulatorSetup kept = setup;   // as the simulator keeps it across a tactical battle
    combat::SimulatorSetup again = kept;
    REQUIRE(classic::simulatorAdd(r, s, again, Item{Item::Kind::Design, pike, {}, 0}));
    CHECK(classic::simulatorRows(r, s, again)[2].name == "Pike 0004");
    // The sandbox names its ships the same way and says which items made them.
    const combat::Simulation sim = combat::buildSimulation(r, s, setup);
    REQUIRE(sim.itemVehicles.size() == setup.items.size());
    std::vector<std::string> ships;
    for (const auto& vehicles : sim.itemVehicles) {
        REQUIRE(vehicles.size() == 1);
        ships.push_back(sim.state.vehicle(vehicles.front())->name);
    }
    CHECK(ships == std::vector<std::string>{"Pike 0002", "Pike 0001", "Lancer 0003"});
}

TEST_CASE("combat windows: Fleets For Plr and Change Cargo work on a sandbox and the setup takes back what changed") {
    Arena ar = makeArena(23);
    GameState& s = ar.s;
    const Rules& r = combatRules();
    const DesignId lancer = frigate(s, ar.a, "Lancer", 3, {"Test Laser"});
    const DesignId hauler = frigate(s, ar.a, "Hauler", 2, {"Test Cargo Bay", "Test Cargo Bay"});
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    Colony& home = homeworld(s, ar.a);
    home.cargo.units = {{trooper, 6}};
    combat::SimulatorSetup setup;
    setup.viewer = ar.a;
    for (int k = 0; k < combat::kSimulatorMaxSides; ++k) setup.sides.push_back({std::format("Race {}", k + 1), k > 0});
    using Item = combat::SimulatorItem;
    REQUIRE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, lancer, {}, 0}));
    REQUIRE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, lancer, {}, 0}));
    REQUIRE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, hauler, {}, 0}));
    REQUIRE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Planet, {}, home.planet, 0}));
    REQUIRE(classic::simulatorAdd(r, s, setup, Item{Item::Kind::Design, lancer, {}, 1}));
    const GameState before = s;

    // Fleets For Plr for side 1: a fleet of the two Lancers.
    {
        classic::SimulatorSandbox box = classic::simulatorSandbox(r, s, setup, 0, false);
        REQUIRE(box.side.valid());
        CHECK(box.anchorVehicle.valid());
        GameState& sb = box.sim.state;
        const VehicleId a = box.sim.itemVehicles[0].front(), b = box.sim.itemVehicles[1].front();
        REQUIRE(apply(r, sb, box.side, cmd::CreateFleet{"Spear", {a, b}}).ok);
        const FleetId fleet = sb.vehicle(a)->fleet;
        REQUIRE(fleet.valid());
        REQUIRE(apply(r, sb, box.side, cmd::SetFleetOptions{fleet, 0, 0}).ok);
        classic::simulatorTakeBack(r, box, sb, setup);
        REQUIRE(setup.fleets.size() == 1);
        CHECK(setup.fleets[0].side == 0);
        CHECK(setup.fleets[0].name == "Spear");
        CHECK(setup.items[0].fleet == 0);
        CHECK(setup.items[1].fleet == 0);
        CHECK(setup.items[2].fleet == -1);
        CHECK(setup.items[4].fleet == -1);
    }
    // Change Cargo: three troops from the colony onto the Hauler.
    {
        classic::SimulatorSandbox box = classic::simulatorSandbox(r, s, setup, 0, true);
        GameState& sb = box.sim.state;
        CHECK(box.anchorPlanet.valid());
        const VehicleId ship = box.sim.itemVehicles[2].front();
        const ObjectId planet = box.sim.itemObjects[3];
        REQUIRE(sb.colony(planet));
        REQUIRE_FALSE(sb.colony(planet)->cargo.units.empty());
        cmd::TransferCargo t;
        t.fromPlanet = planet;
        t.toVehicle = ship;
        t.unitDesign = sb.colony(planet)->cargo.units.front().design;
        t.amount = 3;
        REQUIRE(apply(r, sb, box.side, t).ok);
        classic::simulatorTakeBack(r, box, sb, setup);
        REQUIRE(setup.items[2].cargo.size() == 1);
        CHECK(setup.items[2].cargo[0].design == trooper);   // the real design, not the sandbox copy
        CHECK(setup.items[2].cargo[0].count == 3);
        CHECK(setup.items[3].replaceCargo);
        REQUIRE(setup.items[3].cargo.size() == 1);
        CHECK(setup.items[3].cargo[0].count == 3);
        CHECK(setup.fleets.size() == 1);   // fleets untouched
        CHECK(combat::simulatorProblem(r, s, setup).empty());
    }
    // The real game never changed.
    CHECK(stateChecksum(s) == stateChecksum(before));
}

TEST_CASE("combat windows: a side whose first item is another empire's colony copies that empire") {
    Arena ar = makeArena(21);
    GameState& s = ar.s;
    const Rules& r = combatRules();
    const DesignId lancer = frigate(s, ar.a, "Lancer", 3, {"Test Laser"});
    // B's colony in A's home system.
    ObjectId spot;
    const SystemId home = s.empire(ar.a).homeSystem;
    for (ObjectId o : s.galaxy.system(home).objects)
        if (s.galaxy.object(o).kind == ObjectKind::Planet && !s.colony(o)) spot = o;
    REQUIRE(spot.valid());
    if (s.colonies.size() < s.galaxy.objects.size()) s.colonies.resize(s.galaxy.objects.size());
    Colony c;
    c.planet = spot;
    c.owner = ar.b;
    c.population = {{ar.b, 50}};
    s.colonies[spot.index()] = c;
    combat::SimulatorSetup setup;
    setup.viewer = ar.a;
    setup.sides = {{"Race 1", true}, {"Race 2", true}};
    using Item = combat::SimulatorItem;
    setup.items.push_back(Item{Item::Kind::Design, lancer, {}, 0});
    setup.items.push_back(Item{Item::Kind::Planet, {}, spot, 1});
    REQUIRE(combat::simulatorProblem(r, s, setup).empty());
    const combat::Simulation sim = combat::buildSimulation(r, s, setup);
    CHECK(sim.state.empire(sim.sides[1]).race.name == s.empire(ar.b).race.name);
    CHECK(sim.state.empire(sim.sides[0]).race.name == s.empire(ar.a).race.name);
}

TEST_CASE("combat windows: every battle with a human side stops to be shown; fought in the window it applies the same") {
    for (const bool offered : {true, false}) {
        CAPTURE(offered);
        auto make = [&] {
            Arena ar = makeArena(29);
            GameState& s = ar.s;
            s.options.simultaneous = false;
            s.options.noTacticalCombat = !offered;
            s.empire(ar.a).kind = PlayerKind::Human;
            s.empire(ar.b).kind = PlayerKind::Computer;
            const Location to = ar.loc, from{to.system, Sector{to.sector.x - 1, to.sector.y}};
            spawn(s, frigate(s, ar.a, "Warship", 3, {"Test Laser", "CT Big Armor"}), from);
            spawn(s, frigate(s, ar.b, "Picket", 1, {"Test Laser"}), to);
            for (Empire& e : s.empires) std::fill(e.knowledge.explored.begin(), e.knowledge.explored.end(), 1);
            return ar;
        };
        Arena ar = make();
        const VehicleId warship = ar.s.vehicles.front().id;
        const Location to = ar.loc;
        auto rules = std::make_shared<const Rules>(buildCombatRuleset());
        classic::ClassicSession session(rules, ar.s, ar.a, classic::SessionKind::Local);
        Order o;
        o.kind = OrderKind::MoveTo;
        o.location = to;
        session.issue(cmd::SetOrders{warship, {}, {o}});
        session.answer(true);
        // The battle is set up and waits, in the question form or in Begin and Close form.
        REQUIRE(session.battleQuestion().has_value());
        CHECK(session.battleQuestion()->kind == (offered ? BattleQuestion::Kind::Choose : BattleQuestion::Kind::Show));
        CHECK(session.state().combats.empty());
        // The Strategic Combat window fights it with the strategies, one phase at a time.
        const BattleQuestion q = *session.battleQuestion();
        TacticalBattle::Setup setup{q.where, q.entering, {}};
        setup.stepped = true;
        classic::TacticalFight fight;
        fight.kind = classic::TacticalFight::Kind::Game;
        fight.battle = std::make_unique<TacticalBattle>(*rules, *q.state, setup);
        REQUIRE_FALSE(fight.battle->finished());
        int phases = 0;
        while (fight.battle->step()) ++phases;
        CHECK(phases > 0);
        CHECK(fight.battle->finished());
        session.startTactical(std::move(fight));
        session.endTactical();
        CHECK_FALSE(session.battleQuestion().has_value());
        REQUIRE(session.state().combats.size() == 1);
        CHECK(session.takeStrategicBattles().empty());   // shown already
        // The same game played without stopping (a network host, automation) comes out the same.
        Arena silent = make();
        resumeTurnBased(*rules, silent.s);
        applyLive(*rules, silent.s, silent.a, cmd::SetOrders{warship, {}, {o}}, nullptr);
        applyLive(*rules, silent.s, silent.a, cmd::EnterSector{warship, {}, to, true}, nullptr);
        CHECK(stateChecksum(session.state()) == stateChecksum(silent.s));
    }
}
