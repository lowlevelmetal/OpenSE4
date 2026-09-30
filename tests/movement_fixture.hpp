#pragma once

// Test support for movement and sight: the engine test rules extended with
// original parts for sensors, cloaks, launchers and stellar manipulation,
// plus a builder for small handcrafted galaxies. All names are invented.

#include "engine_fixture.hpp"

#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <string>
#include <vector>

namespace opense4::mvtest {

using namespace opense4::game;
using ruleset::VehicleType;

inline ruleset::Ability ab(AbilityKind k, int64_t v1 = 0, int64_t v2 = 0) {
    ruleset::Ability a;
    a.type = std::string(identifier(k));
    a.value1 = std::to_string(v1);
    a.value2 = std::to_string(v2);
    return a;
}

inline ruleset::Ability abText(AbilityKind k, std::string v1, int64_t v2) {
    ruleset::Ability a;
    a.type = std::string(identifier(k));
    a.value1 = std::move(v1);
    a.value2 = std::to_string(v2);
    return a;
}

inline std::vector<ruleset::Ability> allTypes(AbilityKind k, int level) {
    std::vector<ruleset::Ability> out;
    for (const char* t : {"EM Active", "EM Passive", "Psychic", "Gravitic", "Temporal"}) out.push_back(abText(k, t, level));
    return out;
}

inline ruleset::Ruleset buildRuleset() {
    using ruleset::maskOf;
    ruleset::Ruleset rs = test::buildEngineRuleset();
    const ruleset::VehicleTypeMask ship = maskOf(VehicleType::Ship);
    const ruleset::VehicleTypeMask shipBase = ship | maskOf(VehicleType::Base);
    auto comp = [&](std::string name, int tons, int structure, ruleset::VehicleTypeMask mask, std::vector<ruleset::Ability> abilities,
                    int supplyUsed = 0, std::string group = "Miscellaneous", int customGroup = 0) -> ruleset::Component& {
        ruleset::Component c;
        c.name = std::move(name);
        c.tonnage = tons;
        c.structure = structure;
        c.cost = {10, 0, 0};
        c.vehicles = mask;
        c.abilities = std::move(abilities);
        c.supplyUsed = supplyUsed;
        c.generalGroup = std::move(group);
        c.customGroup = customGroup;
        c.romanNumeral = 1;
        rs.components.push_back(std::move(c));
        return rs.components.back();
    };
    auto facility = [&](std::string name, std::vector<ruleset::Ability> abilities) {
        ruleset::Facility f;
        f.name = std::move(name);
        f.group = "Test";
        f.cost = {100, 0, 0};
        f.abilities = std::move(abilities);
        rs.facilities.push_back(std::move(f));
    };

    // Ship systems with supply use and repair groups.
    comp("Mv Engine", 10, 10, ship | maskOf(VehicleType::Drone), {ab(AbilityKind::StandardShipMovement, 1)}, 10, "Engines");
    comp("Mv Armor", 10, 30, shipBase, {ab(AbilityKind::Armor)}, 0, "Armor");
    comp("Mv Tank", 10, 10, shipBase, {ab(AbilityKind::SupplyStorage, 100)}, 0, "Supply");
    comp("Mv Fuel Cell", 10, 10, shipBase, {ab(AbilityKind::SupplyStorage, 1'000'000)}, 0, "Supply");
    comp("Mv Sensor 3", 10, 10, shipBase, {abText(AbilityKind::SensorLevel, "EM Active", 3)}, 0, "Sensors");
    comp("Mv Psychic Sensor", 10, 10, shipBase, {abText(AbilityKind::SensorLevel, "Psychic", 2)}, 0, "Sensors");
    comp("Mv Cloak", 10, 10, shipBase, allTypes(AbilityKind::CloakLevel, 3), 20, "Sensors");
    comp("Mv EM Cloak", 10, 10, shipBase, {abText(AbilityKind::CloakLevel, "EM Active", 3), abText(AbilityKind::CloakLevel, "EM Passive", 3)});
    comp("Mv Jammer", 10, 10, shipBase, {ab(AbilityKind::ScannerJammer)}, 0, "Sensors");
    comp("Mv Scanner", 10, 10, shipBase, {ab(AbilityKind::LongRangeScanner, 2)}, 0, "Sensors");
    comp("Mv Sweeper", 10, 10, ship, {ab(AbilityKind::MineSweeping, 3)}, 0, "Miscellaneous");
    comp("Mv Energy Cell", 10, 10, ship, {ab(AbilityKind::EmergencyEnergy, 4), ab(AbilityKind::ComponentDestroyedOnUse)}, 0);
    comp("Mv Spare Tank", 10, 10, ship, {ab(AbilityKind::EmergencyResupply, 60), ab(AbilityKind::ComponentDestroyedOnUse)}, 0);
    // Launchers: Val 1 per combat turn, Val 2 per game turn.
    comp("Mv Fighter Bay", 20, 10, shipBase, {ab(AbilityKind::LaunchRecoverFighters, 1, 3), ab(AbilityKind::CargoStorage, 200)});
    comp("Mv Satellite Bay", 20, 10, shipBase, {ab(AbilityKind::LaunchRecoverSatellites, 1, 2), ab(AbilityKind::CargoStorage, 400)});
    comp("Mv Mine Layer", 20, 10, shipBase, {ab(AbilityKind::LayMines, 1, 5), ab(AbilityKind::CargoStorage, 400)});
    comp("Mv Drone Rack", 20, 10, shipBase, {ab(AbilityKind::LaunchDrones, 1, 2), ab(AbilityKind::CargoStorage, 400)});
    comp("Mv Fighter Tank", 2, 5, maskOf(VehicleType::Fighter), {ab(AbilityKind::SupplyStorage, 12)});
    comp("Mv Drone Tank", 5, 5, maskOf(VehicleType::Drone), {ab(AbilityKind::SupplyStorage, 300)});
    comp("Mv Fighter Engine", 5, 5, maskOf(VehicleType::Fighter), {ab(AbilityKind::StandardShipMovement, 1)}, 2, "Engines");
    comp("Mv Drone Panel", 5, 5, maskOf(VehicleType::Drone), {ab(AbilityKind::SolarSupplyGeneration, 500)});
    comp("Mv Repair Drone Bay", 5, 5, maskOf(VehicleType::Satellite), {ab(AbilityKind::ComponentRepair, 2)});
    // Stellar manipulation devices.
    const auto once = ab(AbilityKind::ComponentDestroyedOnUse);
    comp("Mv Planet Maker", 20, 10, ship, {ab(AbilityKind::CreatePlanetSize, 3), once}, 50);
    comp("Mv Planet Breaker", 20, 10, ship, {ab(AbilityKind::DestroyPlanetSize, 3), once});
    comp("Mv Star Maker", 20, 10, ship, {ab(AbilityKind::CreateStar), once});
    comp("Mv Star Breaker", 20, 10, ship, {ab(AbilityKind::DestroyStar)});
    comp("Mv Warp Opener", 20, 10, ship, {ab(AbilityKind::OpenWarpPointDistance, 10), once});
    comp("Mv Warp Closer", 20, 10, ship, {ab(AbilityKind::CloseWarpPoint), once});
    comp("Mv Storm Maker", 20, 10, ship, {ab(AbilityKind::CreateStorm)});
    comp("Mv Storm Breaker", 20, 10, ship, {ab(AbilityKind::DestroyStorm)});
    comp("Mv Nebula Maker", 20, 10, ship, {ab(AbilityKind::CreateNebulae)});
    comp("Mv Nebula Breaker", 20, 10, ship, {ab(AbilityKind::DestroyNebulae)});
    comp("Mv Hole Maker", 20, 10, ship, {ab(AbilityKind::CreateBlackHole)});
    comp("Mv Hole Breaker", 20, 10, ship, {ab(AbilityKind::DestroyBlackHole)});
    comp("Mv World Builder", 20, 10, ship, {ab(AbilityKind::CreateConstructedPlanet, 1), ab(AbilityKind::ConstructedPlanetRequirements, 42, 20), once});
    comp("Mv Girder", 10, 10, shipBase, {}, 0, "Construction", 42);

    // Hulls: a mine hull that is always cloaked.
    {
        ruleset::VehicleSize h;
        h.name = "Mv Mine Hull";
        h.shortName = h.name;
        h.type = VehicleType::Mine;
        h.tonnage = 10;
        h.abilities = allTypes(AbilityKind::CloakLevel, 5);
        rs.vehicleSizes.push_back(std::move(h));
    }

    // Facilities.
    facility("Mv Trainer", {ab(AbilityKind::ShipTraining, 5, 12)});
    facility("Mv Fleet Trainer", {ab(AbilityKind::FleetTraining, 3, 9)});
    facility("Mv System Scanner", {ab(AbilityKind::LongRangeScannerSystem)});
    facility("Mv Planet Guard", {ab(AbilityKind::StopPlanetDestroyer)});
    facility("Mv Star Guard", {ab(AbilityKind::StopStarDestroyer)});
    facility("Mv Warp Guard", {ab(AbilityKind::StopOpenWarpPoint), ab(AbilityKind::StopCloseWarpPoint)});

    // System types for created nebulae and black holes.
    {
        ruleset::SystemType neb;
        neb.name = "Mv Nebula";
        neb.physicalType = "Nebulae";
        neb.abilities = {ab(AbilityKind::SectorSightObscuration, 3)};
        rs.systemTypes.push_back(neb);
        ruleset::SystemType hole;
        hole.name = "Mv Black Hole";
        hole.physicalType = "Black Hole";
        hole.abilities = {ab(AbilityKind::SystemMovementTowardsCenter, 2), ab(AbilityKind::SystemDestructiveCenter, 25)};
        rs.systemTypes.push_back(hole);
    }

    // Racial traits.
    {
        ruleset::RacialTrait seen;
        seen.name = "Mv Far Sight";
        seen.traitType = "Galaxy Seen";
        rs.racialTraits.push_back(seen);
        ruleset::RacialTrait frugal;
        frugal.name = "Mv Frugal";
        frugal.traitType = "Supply Cost";
        frugal.values = {"-50"};
        rs.racialTraits.push_back(frugal);
    }
    rs.settings.set("Created Storm Maximum Obscuration Level", "2");
    rs.settings.set("Created Storm Maximum Turbulence Damage", "7");
    rs.settings.set("Created Storm Maximum Shield Disruption", "9");
    rs.reindex();
    return rs;
}

inline const Rules& rules() {
    static const Rules r{buildRuleset()};
    return r;
}

inline uint32_t traitIndex(const Rules& r, std::string_view name) {
    for (uint32_t i = 0; i < r.data().racialTraits.size(); ++i)
        if (r.data().racialTraits[i].name == name) return i;
    FAIL("no trait " << name);
    return 0;
}

inline uint32_t sectorTypeOf(const Rules& r, std::string_view physical) {
    for (uint32_t i = 0; i < r.data().sectorObjectTypes.size(); ++i)
        if (r.data().sectorObjectTypes[i].physicalType == physical) return i;
    return 0;
}

// ---- Handcrafted galaxies ---------------------------------------------------------------------------

// A game on `r` with `empires` human empires whose galaxy, vehicles and
// colonies are replaced by what the test builds.
class World {
public:
    explicit World(const Rules& r = opense4::mvtest::rules(), int empires = 2) : r_(r) {
        GameSetup setup;
        setup.seed = 99;
        setup.options.systemCount = 6 * empires;
        for (int i = 0; i < empires; ++i) {
            EmpireSetup e;
            e.name = "Empire " + std::to_string(i + 1);
            setup.empires.push_back(e);
        }
        auto g = createGame(r, setup);
        REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
        s = std::move(*g);
        s.galaxy = Galaxy{};
        s.galaxy.width = s.galaxy.height = 40;
        s.vehicles.clear();
        s.fleets.clear();
        s.colonies.clear();
        for (Empire& e : s.empires) {
            e.claimedSystems.clear();
            e.knowledge = Knowledge{};
        }
    }

    const Rules& rules() const { return r_; }

    SystemId system(std::string name, int x = 0, int y = 0) {
        StarSystem sys;
        sys.id = SystemId{s.galaxy.systems.size()};
        sys.name = std::move(name);
        sys.position = {x, y};
        sys.physicalType = "Normal";
        s.galaxy.systems.push_back(std::move(sys));
        sync();
        return s.galaxy.systems.back().id;
    }

    ObjectId object(SystemId sys, ObjectKind kind, Sector at, std::string name = {}) {
        SpaceObject o;
        o.id = ObjectId{s.galaxy.objects.size()};
        o.kind = kind;
        o.system = sys;
        o.sector = at;
        o.name = name.empty() ? s.galaxy.system(sys).name + " " + std::string(displayName(kind)) : std::move(name);
        o.sectorType = sectorTypeOf(r_, displayName(kind));
        if (kind == ObjectKind::Asteroids) o.size = "Small";
        s.galaxy.system(sys).objects.push_back(o.id);
        s.galaxy.objects.push_back(std::move(o));
        sync();
        return s.galaxy.objects.back().id;
    }

    ObjectId planet(SystemId sys, Sector at, std::string surface = "Rock", std::string atmosphere = "Oxygen", std::string size = "Medium") {
        const ObjectId id = object(sys, ObjectKind::Planet, at);
        SpaceObject& p = s.galaxy.object(id);
        p.surface = std::move(surface);
        p.atmosphere = std::move(atmosphere);
        p.size = std::move(size);
        p.name = s.galaxy.system(sys).name + " P" + std::to_string(id.value);
        return id;
    }

    std::pair<ObjectId, ObjectId> link(SystemId a, Sector sa, SystemId b, Sector sb) {
        const ObjectId wa = object(a, ObjectKind::WarpPoint, sa);
        const ObjectId wb = object(b, ObjectKind::WarpPoint, sb);
        s.galaxy.object(wa).destination = wb;
        s.galaxy.object(wb).destination = wa;
        return {wa, wb};
    }

    Colony& colony(ObjectId planet, EmpireId owner, int64_t population = 1000, std::initializer_list<std::string_view> facilities = {}) {
        Colony c;
        c.planet = planet;
        c.owner = owner;
        c.colonyType = "Test";
        if (population > 0) c.population.push_back({owner, population});
        for (auto f : facilities) c.facilities.push_back(test::facilityIndex(r_, f));
        s.colonies[planet.index()] = c;
        return *s.colonies[planet.index()];
    }

    DesignId design(EmpireId owner, std::string name, std::string_view hull, std::initializer_list<std::string_view> parts) {
        return test::addTestDesign(s, r_, owner, name, hull, parts);
    }

    // A frigate with bridge, life support, crew, `engines` supply-using engines and a supply tank.
    DesignId ship(EmpireId owner, std::string name, int engines, std::initializer_list<std::string_view> extra = {}) {
        Design d;
        d.owner = owner;
        d.name = std::move(name);
        d.hull = test::hullIndex(r_, "Test Frigate");
        for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Mv Tank"}) d.entries.push_back({test::componentIndex(r_, c), -1});
        for (int i = 0; i < engines; ++i) d.entries.push_back({test::componentIndex(r_, "Mv Engine"), -1});
        for (auto c : extra) d.entries.push_back({test::componentIndex(r_, c), -1});
        return addDesign(s, std::move(d));
    }

    VehicleId spawn(DesignId d, Location at) { return movement::spawnVehicle(r_, s, s.design(d).owner, d, at).id; }

    Vehicle& v(VehicleId id) {
        Vehicle* p = s.vehicle(id);
        REQUIRE(p);
        return *p;
    }

    void order(VehicleId id, Order o, bool repeat = false) {
        v(id).orders.push_back(o);
        v(id).repeatOrders = repeat;
    }

    // Gives orders the way a player does (cmd::SetOrders), appended to the
    // vehicle's list: composite orders are expanded as they are given (spec 03 §8).
    void give(VehicleId id, std::vector<Order> more, bool repeat = false) {
        std::vector<Order> list = v(id).orders;
        list.insert(list.end(), more.begin(), more.end());
        const CommandResult res = apply(r_, s, v(id).owner, cmd::SetOrders{id, {}, std::move(list), repeat});
        REQUIRE_MESSAGE(res.ok, res.error);
    }

    void setTreaty(EmpireId a, EmpireId b, Treaty t) {
        s.empire(a).relation(b).treaty = t;
        s.empire(b).relation(a).treaty = t;
    }

    void exploreAll(EmpireId e) {
        std::fill(s.empire(e).knowledge.explored.begin(), s.empire(e).knowledge.explored.end(), 1);
        std::fill(s.empire(e).knowledge.knownWarpLink.begin(), s.empire(e).knowledge.knownWarpLink.end(), 1);
    }

    // Movement phase only (start + 30 days), then dead vehicles are removed.
    void move(const movement::CombatHooks& hooks = {}) {
        TurnContext ctx{r_, s, {}, {}, {}};
        movement::startTurn(ctx);
        movement::runMovementAndCombat(ctx, hooks);
        s.removeDeadVehicles();
        lastMoods = ctx.moodEvents;
    }

    void colonize() {
        TurnContext ctx{r_, s, {}, {}, {}};
        movement::runColonization(ctx);
        lastMoods = ctx.moodEvents;
    }

    // Each empire's repair, supply and training steps, then the design
    // cleanup when a new year starts (spec 05 §8).
    void upkeep() {
        TurnContext ctx{r_, s, {}, {}, {}};
        for (const Empire& e : s.empires) {
            if (!e.alive) continue;
            movement::repairEmpire(ctx, e.id);
            movement::supplyEmpire(ctx, e.id);
            movement::trainEmpire(ctx, e.id);
        }
        if ((s.turn + 1) % 10 == 0) movement::purgeObsoleteDesigns(ctx);
        s.removeDeadVehicles();
    }

    // The event step's pull, drift and centre damage.
    void hazards() {
        TurnContext ctx{r_, s, {}, {}, {}};
        movement::runStellarHazards(ctx);
        lastMoods = ctx.moodEvents;
    }

    void fullTurn() {
        TurnOptions o;
        o.aiForMissing = false;
        processTurn(r_, s, {}, o);
    }

    bool logged(EmpireId e, std::string_view fragment) const {
        for (const LogEntry& l : s.empire(e).log)
            if (l.title.find(fragment) != std::string::npos || l.text.find(fragment) != std::string::npos) return true;
        return false;
    }

    GameState s;
    std::vector<MoodEvent> lastMoods;

private:
    void sync() {
        s.colonies.resize(s.galaxy.objects.size());
        for (Empire& e : s.empires) {
            Knowledge& k = e.knowledge;
            k.explored.resize(s.galaxy.systems.size(), 0);
            k.present.resize(s.galaxy.systems.size(), 0);
            k.lastSeen.resize(s.galaxy.systems.size(), 0);
            k.notes.resize(s.galaxy.systems.size());
            k.knownWarpLink.resize(s.galaxy.objects.size(), 0);
        }
    }

    const Rules& r_;
};

// Records the combat calls movement makes; `fight` decides combatPossible.
// Like combat, a resolved sector where hostile non-mine vehicles meet gets a
// CombatRecord listing them; `entered` keeps the entering vehicles passed each time.
struct CombatSpy {
    std::vector<Location> asked;
    std::vector<std::pair<uint32_t, Location>> fought;  // (call order, location)
    std::vector<std::vector<VehicleId>> entered;        // per resolve call: the vehicles that stepped in that day
    std::function<bool(const GameState&, Location)> fight;

    static bool isMine(const TurnContext& ctx, const Vehicle& v) { return vehicleType(ctx.rules, ctx.state, v) == VehicleType::Mine; }

    movement::CombatHooks hooks() {
        return {[this](const Rules&, const GameState& s, Location l) {
                    asked.push_back(l);
                    return fight && fight(s, l);
                },
                [this](TurnContext& ctx, Location l, std::span<const VehicleId> in) {
                    fought.emplace_back(static_cast<uint32_t>(fought.size()), l);
                    entered.emplace_back(in.begin(), in.end());
                    GameState& s = ctx.state;
                    CombatRecord rec;
                    rec.turn = s.turn;
                    rec.location = l;
                    for (const Vehicle& a : s.vehicles) {
                        if (a.count <= 0 || a.location != l || isMine(ctx, a)) continue;
                        for (const Vehicle& b : s.vehicles)
                            if (b.count > 0 && b.location == l && !isMine(ctx, b) && hostile(s, a.owner, b.owner)) {
                                CombatPiece p;
                                p.owner = a.owner;
                                p.vehicle = a.id;
                                rec.pieces.push_back(p);
                                break;
                            }
                    }
                    if (rec.pieces.empty()) return;
                    // Like the combat module, the spy clears no orders (spec 03 §6.3).
                    s.combats.push_back(std::move(rec));
                }};
    }
};

// Hostile vehicles of different empires share the sector.
inline bool hostilesMeet(const GameState& s, Location l) {
    for (const Vehicle& a : s.vehicles)
        for (const Vehicle& b : s.vehicles)
            if (a.count > 0 && b.count > 0 && a.location == l && b.location == l && hostile(s, a.owner, b.owner)) return true;
    return false;
}

inline constexpr EmpireId kA{0u};
inline constexpr EmpireId kB{1u};

} // namespace opense4::mvtest
