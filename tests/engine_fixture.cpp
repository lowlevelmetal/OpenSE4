#include "engine_fixture.hpp"

#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"

#include <doctest/doctest.h>

#include <format>

namespace opense4::test {

namespace {

using namespace opense4::ruleset;
using game::AbilityKind;

Ability ab(AbilityKind k, int64_t v1 = 0, int64_t v2 = 0) {
    Ability a;
    a.type = std::string(game::identifier(k));
    a.value1 = std::to_string(v1);
    a.value2 = std::to_string(v2);
    return a;
}

Ability abText(AbilityKind k, std::string v1, int64_t v2 = 0) {
    Ability a;
    a.type = std::string(game::identifier(k));
    a.value1 = std::move(v1);
    a.value2 = std::to_string(v2);
    return a;
}

struct Builder {
    Ruleset& rs;

    TechRequirement req(std::string_view area, int level) {
        for (uint32_t i = 0; i < rs.techAreas.size(); ++i)
            if (rs.techAreas[i].name == area) return {TechAreaId{i}, level};
        FAIL("unknown test tech area " << area);
        return {};
    }

    void tech(std::string name, std::string group, int maxLevel, int64_t cost, int start, int raise = 0,
              std::vector<TechRequirement> reqs = {}, int racial = 0, int unique = 0) {
        TechArea t;
        t.name = std::move(name);
        t.group = std::move(group);
        t.maxLevel = maxLevel;
        t.levelCost = cost;
        t.startLevel = start;
        t.raiseLevel = raise;
        t.requirements = std::move(reqs);
        t.racialArea = racial;
        t.uniqueArea = unique;
        rs.techAreas.push_back(std::move(t));
    }

    Component& comp(std::string name, int tons, int structure, Cost cost, VehicleTypeMask vehicles, std::vector<Ability> abilities,
                    std::vector<TechRequirement> reqs = {}, int family = 0, int numeral = 1) {
        Component c;
        c.name = std::move(name);
        c.tonnage = tons;
        c.structure = structure;
        c.cost = cost;
        c.vehicles = vehicles;
        c.abilities = std::move(abilities);
        c.requirements = std::move(reqs);
        c.family = family;
        c.romanNumeral = numeral;
        c.supplyUsed = 0;
        rs.components.push_back(std::move(c));
        return rs.components.back();
    }

    Component& weapon(std::string name, WeaponKind kind, int tons, int structure, Cost cost, VehicleTypeMask vehicles,
                      std::vector<int> damage, int reload, std::vector<TechRequirement> reqs = {}, std::string damageType = "Normal",
                      int family = 0, int numeral = 1) {
        Component& c = comp(std::move(name), tons, structure, cost, vehicles, {}, std::move(reqs), family, numeral);
        c.weapon.kind = kind;
        c.weapon.damageAtRange = std::move(damage);
        c.weapon.reloadRate = reload;
        c.weapon.family = family;  // the "Weapon Family" the AI's design templates pick from
        c.weapon.damageType = std::move(damageType);
        c.weapon.targets = {"Ships", "Planets", "Ftr", "Sat", "Seekers", "Drone"};
        c.supplyUsed = 5;
        return c;
    }

    void facility(std::string name, std::string group, Cost cost, std::vector<Ability> abilities, std::vector<TechRequirement> reqs = {},
                  int family = 0, int numeral = 1) {
        Facility f;
        f.name = std::move(name);
        f.group = std::move(group);
        f.cost = cost;
        f.abilities = std::move(abilities);
        f.requirements = std::move(reqs);
        f.family = family;
        f.romanNumeral = numeral;
        rs.facilities.push_back(std::move(f));
    }

    VehicleSize& hull(std::string name, VehicleType type, int tons, Cost cost, std::vector<TechRequirement> reqs = {}) {
        VehicleSize h;
        h.name = std::move(name);
        h.shortName = h.name;
        h.code = "TT";
        h.type = type;
        h.tonnage = tons;
        h.cost = cost;
        h.requirements = std::move(reqs);
        rs.vehicleSizes.push_back(std::move(h));
        return rs.vehicleSizes.back();
    }
};

constexpr VehicleTypeMask kShip = maskOf(VehicleType::Ship);
constexpr VehicleTypeMask kBase = maskOf(VehicleType::Base);
constexpr VehicleTypeMask kShipBase = kShip | kBase;
constexpr VehicleTypeMask kCrewed = kShip | kBase | maskOf(VehicleType::Drone);

} // namespace

ruleset::Ruleset buildEngineRuleset() {
    auto loaded = loadRuleset(std::filesystem::path(OPENSE4_FIXTURE_DIR) / "minimal_dataset");
    REQUIRE(loaded.ruleset);
    Ruleset rs = std::move(*loaded.ruleset);
    rs.techAreas.clear();
    rs.components.clear();
    rs.facilities.clear();
    rs.vehicleSizes.clear();
    Builder b{rs};

    // ---- Technology.
    b.tech("Test Physics", "Theoretical Science", 5, 1000, 1, 2);
    b.tech("Test Construction", "Applied Science", 10, 1000, 1, 2);
    b.tech("Test Propulsion", "Applied Science", 10, 1000, 1, 2);
    b.tech("Test Rock Colonies", "Applied Science", 1, 2000, 0);
    b.tech("Test Ice Colonies", "Applied Science", 1, 2000, 0);
    b.tech("Test Gas Colonies", "Applied Science", 1, 2000, 0);
    b.tech("Test Beams", "Weapon Technology", 10, 1000, 1, 2);
    b.tech("Test Missiles", "Weapon Technology", 10, 1500, 0, 1, {b.req("Test Physics", 2)});
    b.tech("Test Armor", "Applied Science", 10, 1000, 1, 1);
    b.tech("Test Shields", "Applied Science", 10, 1500, 0, 1, {b.req("Test Physics", 2)});
    b.tech("Test Economics", "Applied Science", 10, 1000, 1, 1);
    b.tech("Test Espionage", "Applied Science", 5, 1000, 1, 1);
    b.tech("Test Units", "Applied Science", 5, 1000, 1, 1);
    b.tech("Test Cloaking", "Applied Science", 3, 2000, 0, 0, {b.req("Test Physics", 3)});
    b.tech("Test Psionics", "Theoretical Science", 3, 2000, 0, 0, {}, 1);
    b.tech("Test Relics", "Theoretical Science", 3, 2000, 0, 0, {}, 0, 7);

    const auto con = b.req("Test Construction", 1);
    const auto prop = b.req("Test Propulsion", 1);

    // ---- Components (ship systems).
    b.comp("Test Bridge", 10, 20, {20, 0, 0}, kCrewed, {ab(AbilityKind::ShipBridge)}, {con});
    b.comp("Test Aux Control", 10, 20, {20, 0, 0}, kShipBase, {ab(AbilityKind::ShipAuxiliaryControl)}, {con});
    b.comp("Test Life Support", 10, 20, {10, 10, 0}, kShipBase, {ab(AbilityKind::ShipLifeSupport)}, {con});
    b.comp("Test Crew Quarters", 10, 20, {10, 10, 0}, kShipBase, {ab(AbilityKind::ShipCrewQuarters)}, {con});
    b.comp("Test Engine", 10, 10, {20, 0, 10}, kShip | maskOf(VehicleType::Drone), {ab(AbilityKind::StandardShipMovement, 1)}, {prop},
           100, 1);
    b.comp("Test Engine II", 10, 10, {30, 0, 15}, kShip | maskOf(VehicleType::Drone),
           {ab(AbilityKind::StandardShipMovement, 1), ab(AbilityKind::MovementBonus, 1)}, {b.req("Test Propulsion", 3)}, 100, 2);
    b.comp("Test Supply Pod", 10, 10, {10, 0, 0}, kShipBase, {ab(AbilityKind::SupplyStorage, 500)}, {con});
    b.comp("Test Cargo Bay", 20, 10, {20, 0, 0}, kShipBase, {ab(AbilityKind::CargoStorage, 50)}, {con});
    b.comp("Test Rock Pod", 40, 20, {100, 50, 20}, kShip, {ab(AbilityKind::ColonizeRock), ab(AbilityKind::CargoStorage, 10)},
           {b.req("Test Rock Colonies", 1)});
    b.comp("Test Ice Pod", 40, 20, {100, 50, 20}, kShip, {ab(AbilityKind::ColonizeIce), ab(AbilityKind::CargoStorage, 10)},
           {b.req("Test Ice Colonies", 1)});
    b.comp("Test Gas Pod", 40, 20, {100, 50, 20}, kShip, {ab(AbilityKind::ColonizeGas), ab(AbilityKind::CargoStorage, 10)},
           {b.req("Test Gas Colonies", 1)});
    b.comp("Test Armor Plate", 10, 40, {15, 0, 0}, kShipBase | maskOf(VehicleType::Satellite), {ab(AbilityKind::Armor)},
           {b.req("Test Armor", 1)});
    b.comp("Test Shield", 10, 10, {30, 0, 20}, kShipBase | maskOf(VehicleType::Satellite), {ab(AbilityKind::ShieldGeneration, 20)},
           {b.req("Test Shields", 1)});
    b.comp("Test Sensor", 10, 10, {20, 0, 10}, kShipBase, {abText(AbilityKind::SensorLevel, "EM Active", 2)}, {con});
    b.comp("Test Cloak", 10, 10, {40, 0, 40}, kShipBase, {abText(AbilityKind::CloakLevel, "EM Active", 1)}, {b.req("Test Cloaking", 1)});
    b.comp("Test Scanner", 10, 10, {30, 0, 10}, kShipBase, {ab(AbilityKind::LongRangeScanner, 3)}, {con});
    b.comp("Test Yard Module", 50, 20, {200, 50, 50}, kShipBase,
           {ab(AbilityKind::SpaceYard, 1, 500), ab(AbilityKind::SpaceYard, 2, 500), ab(AbilityKind::SpaceYard, 3, 500)}, {con});
    b.comp("Test Repair Bay", 30, 20, {100, 0, 20}, kShipBase, {ab(AbilityKind::ComponentRepair, 5)}, {con});
    b.comp("Test Fighter Bay", 30, 20, {50, 0, 0}, kShipBase,
           {ab(AbilityKind::LaunchRecoverFighters, 4), ab(AbilityKind::CargoStorage, 60)}, {b.req("Test Units", 1)});
    b.comp("Test Mine Layer", 30, 20, {50, 0, 0}, kShipBase, {ab(AbilityKind::LayMines, 4), ab(AbilityKind::CargoStorage, 60)},
           {b.req("Test Units", 1)});
    b.comp("Test Troop Bay", 30, 20, {50, 0, 0}, kShipBase, {ab(AbilityKind::DropTroops, 10), ab(AbilityKind::CargoStorage, 60)},
           {b.req("Test Units", 1)});
    b.comp("Test Mine Sweeper", 20, 10, {30, 0, 0}, kShip, {ab(AbilityKind::MineSweeping, 5)}, {b.req("Test Units", 1)});
    b.comp("Test Combat Sensor", 10, 10, {30, 0, 10}, kShipBase, {ab(AbilityKind::CombatToHitOffensePlus, 10)}, {con});
    b.comp("Test ECM", 10, 10, {30, 0, 10}, kShipBase, {ab(AbilityKind::CombatToHitDefensePlus, 10)}, {con});
    b.comp("Test Boarding Party", 10, 10, {20, 20, 0}, kShip, {ab(AbilityKind::BoardingAttack, 20)}, {b.req("Test Units", 1)});
    b.comp("Test Security Station", 10, 10, {20, 20, 0}, kShipBase, {ab(AbilityKind::BoardingDefense, 20)}, {con});
    b.comp("Test Master Computer", 10, 20, {100, 0, 100}, kShipBase, {ab(AbilityKind::MasterComputer)}, {b.req("Test Physics", 4)});
    b.comp("Test Quantum Reactor", 20, 20, {100, 0, 100}, kShip, {ab(AbilityKind::QuantumReactor)}, {b.req("Test Physics", 5)});
    b.comp("Test Solar Panel", 10, 10, {20, 0, 0}, kShipBase, {ab(AbilityKind::SolarSupplyGeneration, 50)}, {con});
    b.comp("Test Emergency Thruster", 10, 10, {20, 0, 20}, kShip,
           {ab(AbilityKind::EmergencyEnergy, 3), ab(AbilityKind::ComponentDestroyedOnUse)}, {prop});
    b.comp("Test Self Destruct", 10, 10, {20, 0, 20}, kShipBase, {ab(AbilityKind::SelfDestruct, 50)}, {con});
    b.comp("Test Planet Maker", 100, 50, {2000, 500, 2000}, kShip, {ab(AbilityKind::CreatePlanetSize, 1)}, {b.req("Test Physics", 5)});

    // ---- Weapons.
    b.weapon("Test Laser", WeaponKind::DirectFire, 20, 15, {40, 0, 10}, kShipBase | maskOf(VehicleType::Satellite),
             {12, 12, 10, 8, 6, 4}, 1, {b.req("Test Beams", 1)}, "Normal", 200, 1);
    b.weapon("Test Laser II", WeaponKind::DirectFire, 20, 15, {50, 0, 15}, kShipBase | maskOf(VehicleType::Satellite),
             {16, 16, 14, 12, 10, 8}, 1, {b.req("Test Beams", 3)}, "Normal", 200, 2);
    b.weapon("Test Disruptor", WeaponKind::DirectFire, 20, 15, {50, 0, 20}, kShipBase, {10, 10, 10}, 1, {b.req("Test Beams", 2)},
             "Skips Normal Shields", 201, 1);
    b.weapon("Test Missile", WeaponKind::Seeking, 20, 15, {40, 0, 20}, kShipBase, {30, 30, 30, 30, 30, 30, 30, 30}, 2,
             {b.req("Test Missiles", 1)}, "Normal", 202, 1)
        .weapon.seekerSpeed = 3;
    b.weapon("Test Point Defense", WeaponKind::PointDefense, 10, 10, {30, 0, 10}, kShipBase, {10, 10}, 1, {b.req("Test Beams", 1)},
             "Normal", 203, 1);
    b.weapon("Test Fighter Gun", WeaponKind::DirectFire, 5, 5, {10, 0, 0}, maskOf(VehicleType::Fighter), {6, 6, 4}, 1,
             {b.req("Test Units", 1)}, "Normal", 204, 1);
    b.weapon("Test Warhead", WeaponKind::Warhead, 10, 10, {20, 0, 20}, maskOf(VehicleType::Mine) | maskOf(VehicleType::Drone), {60}, 1,
             {b.req("Test Units", 1)}, "Normal", 205, 1);
    b.weapon("Test Troop Rifle", WeaponKind::DirectFire, 5, 10, {10, 10, 0}, maskOf(VehicleType::Troop), {8}, 1,
             {b.req("Test Units", 1)}, "Normal", 206, 1);
    b.comp("Test Troop Armor", 5, 20, {10, 0, 0}, maskOf(VehicleType::Troop), {ab(AbilityKind::Armor)}, {b.req("Test Units", 1)});
    b.comp("Test Fighter Engine", 5, 5, {10, 0, 5}, maskOf(VehicleType::Fighter), {ab(AbilityKind::StandardShipMovement, 1)},
           {b.req("Test Units", 1)});
    b.weapon("Test Satellite Gun", WeaponKind::DirectFire, 10, 10, {20, 0, 5}, maskOf(VehicleType::Satellite), {10, 10, 8}, 1,
             {b.req("Test Units", 1)}, "Normal", 207, 1);

    // ---- Facilities.
    const auto econ = b.req("Test Economics", 1);
    b.facility("Test Space Yard", "Space Yards", {500, 0, 100},
               {ab(AbilityKind::SpaceYard, 1, 2000), ab(AbilityKind::SpaceYard, 2, 2000), ab(AbilityKind::SpaceYard, 3, 2000)}, {con}, 10,
               1);
    b.facility("Test Space Yard II", "Space Yards", {800, 0, 200},
               {ab(AbilityKind::SpaceYard, 1, 3000), ab(AbilityKind::SpaceYard, 2, 3000), ab(AbilityKind::SpaceYard, 3, 3000)},
               {b.req("Test Construction", 3)}, 10, 2);
    b.facility("Test Spaceport", "Logistics", {400, 0, 0}, {ab(AbilityKind::Spaceport)}, {con}, 11, 1);
    b.facility("Test Depot", "Logistics", {400, 0, 100}, {ab(AbilityKind::SupplyGeneration, 5000)}, {con}, 12, 1);
    b.facility("Test Mine", "Resources", {300, 0, 0}, {ab(AbilityKind::ResourceGenMinerals, 800)}, {econ}, 20, 1);
    b.facility("Test Mine II", "Resources", {400, 0, 0}, {ab(AbilityKind::ResourceGenMinerals, 1200)}, {b.req("Test Economics", 3)}, 20,
               2);
    b.facility("Test Farm", "Resources", {300, 0, 0}, {ab(AbilityKind::ResourceGenOrganics, 800)}, {econ}, 21, 1);
    b.facility("Test Refinery", "Resources", {300, 0, 0}, {ab(AbilityKind::ResourceGenRadioactives, 800)}, {econ}, 22, 1);
    b.facility("Test Lab", "Research", {400, 0, 100}, {ab(AbilityKind::PointGenResearch, 500)}, {econ}, 23, 1);
    b.facility("Test Intel Center", "Intelligence", {400, 0, 100}, {ab(AbilityKind::PointGenIntelligence, 300)},
               {b.req("Test Espionage", 1)}, 24, 1);
    b.facility("Test Mineral Store", "Storage", {200, 0, 0}, {ab(AbilityKind::ResourceStorageMinerals, 20000)}, {econ}, 25, 1);
    b.facility("Test Organic Store", "Storage", {200, 0, 0}, {ab(AbilityKind::ResourceStorageOrganics, 20000)}, {econ}, 26, 1);
    b.facility("Test Radioactive Store", "Storage", {200, 0, 0}, {ab(AbilityKind::ResourceStorageRadioactives, 20000)}, {econ}, 27, 1);
    b.facility("Test Planet Shield", "Defense", {600, 0, 200}, {ab(AbilityKind::PlanetShieldGeneration, 100)}, {b.req("Test Shields", 1)},
               28, 1);
    b.facility("Test Climate Station", "Environment", {800, 200, 0}, {ab(AbilityKind::PlanetChangeConditions, 1)},
               {b.req("Test Economics", 2)}, 29, 1);
    b.facility("Test Entertainment", "Happiness", {400, 200, 0}, {ab(AbilityKind::PlanetChangePopulationHappiness, 10)}, {econ}, 30, 1);
    b.facility("Test Medical Lab", "Medical", {400, 200, 0}, {ab(AbilityKind::PlaguePreventionSystem, 2)}, {econ}, 31, 1);
    b.facility("Test Recycler", "Resources", {400, 0, 0}, {ab(AbilityKind::ResourceReclamation, 60)}, {econ}, 32, 1);
    b.facility("Test Sensor Array", "Defense", {400, 0, 100}, {abText(AbilityKind::SensorLevel, "EM Active", 3)}, {con}, 33, 1);
    b.facility("Test Training Ground", "Training", {600, 0, 0}, {ab(AbilityKind::ShipTraining, 5)}, {con}, 34, 1);
    b.facility("Test Repair Yard", "Repair", {600, 0, 100}, {ab(AbilityKind::ComponentRepair, 20)}, {con}, 35, 1);

    // ---- Hulls.
    auto& frigate = b.hull("Test Frigate", VehicleType::Ship, 150, {100, 0, 10}, {con});
    frigate.enginesPerMove = 1;
    frigate.mustHaveBridge = true;
    frigate.minLifeSupport = 1;
    frigate.minCrewQuarters = 1;
    frigate.usesEngines = true;
    frigate.maxEngines = 6;
    auto& cruiser = b.hull("Test Cruiser", VehicleType::Ship, 300, {250, 0, 25}, {b.req("Test Construction", 2)});
    cruiser.enginesPerMove = 2;
    cruiser.mustHaveBridge = true;
    cruiser.canHaveAuxControl = true;
    cruiser.minLifeSupport = 1;
    cruiser.minCrewQuarters = 1;
    cruiser.usesEngines = true;
    cruiser.maxEngines = 12;
    auto& base = b.hull("Test Station", VehicleType::Base, 500, {300, 0, 30}, {con});
    base.mustHaveBridge = true;
    base.minLifeSupport = 1;
    base.minCrewQuarters = 1;
    auto& fighter = b.hull("Test Fighter Hull", VehicleType::Fighter, 20, {20, 0, 0}, {b.req("Test Units", 1)});
    fighter.enginesPerMove = 1;
    fighter.usesEngines = true;
    fighter.maxEngines = 4;
    b.hull("Test Satellite Hull", VehicleType::Satellite, 50, {40, 0, 0}, {b.req("Test Units", 1)});
    b.hull("Test Mine Hull", VehicleType::Mine, 10, {10, 0, 5}, {b.req("Test Units", 1)});
    b.hull("Test Troop Hull", VehicleType::Troop, 20, {10, 10, 0}, {b.req("Test Units", 1)});
    auto& drone = b.hull("Test Drone Hull", VehicleType::Drone, 40, {30, 0, 10}, {b.req("Test Units", 1)});
    drone.enginesPerMove = 1;
    drone.usesEngines = true;
    drone.maxEngines = 4;

    // ---- A roomier start system so homeworlds have neighbours.
    for (auto& st : rs.systemTypes)
        if (st.empiresCanStartIn) {
            for (int ring = 2; ring <= 5; ++ring) {
                SystemObjectTemplate t;
                t.physicalType = "Planet";
                t.position = std::format("Ring {}", ring);
                t.stellarAbilityType = "None";
                t.size = "Any";
                t.atmosphere = "Any";
                t.composition = "Any";
                st.objects.push_back(t);
            }
        }

    // ---- Settings the engine reads (stock-like values; see docs/spec).
    auto set = [&](std::string k, std::string v) { rs.settings.set(std::move(k), std::move(v)); };
    set("Empire Starting Percent Reproduction", "10");
    set("Population Mass", "5");
    set("Empire Starting Percent Maint Cost", "25");
    set("Minimum Empire Point Storage", "50000");
    set("Scrap Ship Percent Returned", "30");
    set("Scrap Unit Percent Returned", "30");
    set("Scrap Facility Percent Returned", "30");
    set("UnMothball Ship Percent Cost", "20");

    rs.reindex();
    return rs;
}

const game::Rules& engineRules() {
    static const game::Rules rules{buildEngineRuleset()};
    return rules;
}

game::GameState newEngineGame(uint64_t seed, int empires, int systems, bool allHuman) {
    game::GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = systems;
    for (int i = 0; i < empires; ++i) {
        game::EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = (allHuman || i == 0) ? game::PlayerKind::Human : game::PlayerKind::Computer;
        setup.empires.push_back(std::move(e));
    }
    auto g = game::createGame(engineRules(), setup);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    return std::move(*g);
}

uint32_t componentIndex(const game::Rules& r, std::string_view name) {
    for (uint32_t i = 0; i < r.data().components.size(); ++i)
        if (r.data().components[i].name == name) return i;
    FAIL("no component " << name);
    return 0;
}

uint32_t facilityIndex(const game::Rules& r, std::string_view name) {
    for (uint32_t i = 0; i < r.data().facilities.size(); ++i)
        if (r.data().facilities[i].name == name) return i;
    FAIL("no facility " << name);
    return 0;
}

uint32_t hullIndex(const game::Rules& r, std::string_view name) {
    for (uint32_t i = 0; i < r.data().vehicleSizes.size(); ++i)
        if (r.data().vehicleSizes[i].name == name) return i;
    FAIL("no hull " << name);
    return 0;
}

ruleset::TechAreaId techArea(const game::Rules& r, std::string_view name) {
    auto id = r.data().findTechArea(name);
    REQUIRE_MESSAGE(id.has_value(), "no tech area " << name);
    return *id;
}

game::DesignId addTestDesign(game::GameState& s, const game::Rules& r, game::EmpireId owner, std::string_view name, std::string_view hull,
                             std::initializer_list<std::string_view> components) {
    game::Design d;
    d.owner = owner;
    d.name = std::string(name);
    d.hull = hullIndex(r, hull);
    for (auto c : components) d.entries.push_back({componentIndex(r, c), -1});
    return game::addDesign(s, std::move(d));
}

game::Vehicle& addTestVehicle(game::GameState& s, const game::Rules& r, game::DesignId design, game::Location where) {
    return game::movement::spawnVehicle(r, s, s.design(design).owner, design, where);
}

game::Colony& homeworld(game::GameState& s, game::EmpireId e) {
    for (auto& c : s.colonies)
        if (c && c->owner == e && c->homeworld) return *c;
    FAIL("no homeworld");
    return *s.colonies.front();
}

} // namespace opense4::test
