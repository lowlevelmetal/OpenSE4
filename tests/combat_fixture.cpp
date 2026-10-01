// The combat test fixture (combat_fixture.hpp): the engine fixture's rules
// plus invented weapons, armor and hulls, an arena to fight in, and helpers.

#include "combat_fixture.hpp"

#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/setup.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>

namespace opense4::ctest {

using namespace opense4::game;
using namespace opense4::test;
using ruleset::VehicleType;
using ruleset::WeaponKind;


ruleset::Ability ab(AbilityKind k, int64_t v1, int64_t v2) {
    ruleset::Ability a;
    a.type = std::string(game::identifier(k));
    a.value1 = std::to_string(v1);
    a.value2 = std::to_string(v2);
    return a;
}

const std::vector<std::string> kHitsAll{"Ships", "Planets", "Ftr", "Sat", "Drone"};

ruleset::Component& part(ruleset::Ruleset& rs, std::string name, int structure, std::vector<ruleset::Ability> abilities,
                         ruleset::VehicleTypeMask mask, int family) {
    ruleset::Component c;
    c.name = std::move(name);
    c.tonnage = 10;
    c.structure = structure;
    c.vehicles = mask;
    c.abilities = std::move(abilities);
    c.family = family;
    rs.components.push_back(std::move(c));
    return rs.components.back();
}

ruleset::Component& gun(ruleset::Ruleset& rs, std::string name, WeaponKind kind, std::vector<int> damage, std::string type,
                        std::vector<std::string> targets, int modifier) {
    ruleset::Component& c = part(rs, std::move(name), 10, {});
    c.weapon.kind = kind;
    c.weapon.damageAtRange = std::move(damage);
    c.weapon.damageType = std::move(type);
    c.weapon.reloadRate = 1;
    c.weapon.targets = std::move(targets);
    c.weapon.modifier = modifier;
    return c;
}

// The engine fixture plus combat parts, appended so the fixture's indices stay valid.
ruleset::Ruleset buildCombatRuleset() {
    ruleset::Ruleset rs = buildEngineRuleset();
    using WK = WeaponKind;
    gun(rs, "CT Gun", WK::DirectFire, {10, 10, 10, 10, 10}, "Normal");
    gun(rs, "CT Big Gun", WK::DirectFire, {100, 100, 100, 100, 100, 100}, "Normal");
    gun(rs, "CT Long Gun", WK::DirectFire, {8, 8, 8, 8, 8, 8, 8, 8}, "Normal");
    gun(rs, "CT Short Gun", WK::DirectFire, {20, 20}, "Normal");
    gun(rs, "CT Neutron Bomb", WK::DirectFire, {50, 50, 50, 50, 50}, "Only Planet Population", {"Planets"});
    gun(rs, "CT Plague Bomb", WK::DirectFire, {1, 1, 1, 1, 1}, "Plague Level 2", {"Planets"});
    gun(rs, "CT Tractor", WK::DirectFire, {3, 3, 3, 3}, "Pushes Target");
    gun(rs, "CT Converter", WK::DirectFire, {100, 100, 100, 100, 100}, "Crew Conversion");
    gun(rs, "CT Jammer", WK::DirectFire, {3, 3, 3, 3, 3}, "Increase Reload Time");
    ruleset::Component& torpedo = gun(rs, "CT Torpedo", WK::Seeking, {37, 37, 37, 37, 37, 37, 37, 37, 37, 37}, "Normal");
    torpedo.weapon.seekerSpeed = 4;
    torpedo.weapon.seekerDamageResistance = 5;
    gun(rs, "CT PD", WK::PointDefense, {20, 20, 20}, "Normal", {"Ftr", "Sat", "Seekers", "Drone"}, 70);
    gun(rs, "CT Platform Gun", WK::DirectFire, {15, 15, 15, 15, 15, 15, 15, 15}, "Normal");
    part(rs, "CT Phased Shield", 10, {ab(AbilityKind::PhasedShieldGeneration, 20)});
    part(rs, "CT Emissive Armor", 40, {ab(AbilityKind::Armor), ab(AbilityKind::EmissiveArmor, 15)});
    part(rs, "CT Organic Armor", 40, {ab(AbilityKind::Armor), ab(AbilityKind::ArmorRegeneration, 10)});
    part(rs, "CT Crystal Armor", 40, {ab(AbilityKind::Armor), ab(AbilityKind::ShieldGenerationFromDamage, 5)});
    part(rs, "CT Big Armor", 500, {ab(AbilityKind::Armor)});
    part(rs, "CT Always Hit", 10, {ab(AbilityKind::WeaponsAlwaysHit)});
    part(rs, "CT Platform Core", 30, {});
    part(rs, "CT Drone Bay", 20, {ab(AbilityKind::LaunchDrones, 2), ab(AbilityKind::CargoStorage, 100)});
    {
        // A cloaking device: level 2 in every sight type while cloaked (spec 01 §6.3).
        std::vector<ruleset::Ability> cloak;
        for (const char* type : {"EM Active", "EM Passive", "Psychic", "Gravitic", "Temporal"}) {
            ruleset::Ability a;
            a.type = std::string(game::identifier(AbilityKind::CloakLevel));
            a.value1 = type;
            a.value2 = "2";
            cloak.push_back(std::move(a));
        }
        part(rs, "CT Cloak", 10, std::move(cloak));
    }
    part(rs, "CT Fuel Pod", 10, {ab(AbilityKind::SupplyStorage, 1000)});
    part(rs, "CT Fighter Fuel", 5, {ab(AbilityKind::SupplyStorage, 100)});
    part(rs, "CT Combat Thruster", 10, {ab(AbilityKind::CombatMovement, 2)});
    part(rs, "CT ECM A", 10, {ab(AbilityKind::CombatToHitDefensePlus, 20)}, kAll, 71);
    part(rs, "CT ECM B", 10, {ab(AbilityKind::CombatToHitDefensePlus, 15)}, kAll, 71);
    part(rs, "CT Stealth Armor", 40, {ab(AbilityKind::Armor), ab(AbilityKind::CombatToHitDefensePlus, 10)}, kAll, 72);
    part(rs, "CT Troop Scope", 5, {ab(AbilityKind::CombatToHitOffensePlus, 120)}, kAll, 90);
    part(rs, "CT Planet Shield", 10, {ab(AbilityKind::ShieldGeneration, 30)});
    gun(rs, "CT Twenty Gun", WK::DirectFire, std::vector<int>(20, 5), "Normal");
    gun(rs, "CT Odd Gun", WK::DirectFire, {15, 25}, "Normal");
    gun(rs, "CT Puller", WK::DirectFire, {6, 6, 6, 6, 6, 6, 6, 6}, "Pulls Target");
    gun(rs, "CT Quad Gun", WK::DirectFire, {15, 15, 15}, "Quad Damage To Shields");
    gun(rs, "CT Slow Gun", WK::DirectFire, std::vector<int>(20, 4), "Normal").weapon.reloadRate = 3;
    gun(rs, "CT Climate Bomb", WK::DirectFire, {1, 1, 1, 1, 1}, "Only Planet Conditions", {"Planets"});

    part(rs, "CT Battle Computer", 10, {ab(AbilityKind::CombatModifierSystem, 7)});
    {
        // Sensors that see through the cloaking device (spec 01 §6.3).
        std::vector<ruleset::Ability> sensors;
        for (const char* type : {"EM Active", "EM Passive", "Psychic", "Gravitic", "Temporal"}) {
            ruleset::Ability a;
            a.type = std::string(game::identifier(AbilityKind::SensorLevel));
            a.value1 = type;
            a.value2 = "3";
            sensors.push_back(std::move(a));
        }
        part(rs, "CT Sensor", 10, std::move(sensors));
    }
    for (int value : {10, 4}) {
        ruleset::Facility f;
        f.name = std::format("CT Combat Center {}", value);
        f.abilities = {ab(AbilityKind::CombatModifierSystem, value)};
        rs.facilities.push_back(std::move(f));
    }

    ruleset::VehicleSize platform;
    platform.name = platform.shortName = "CT Platform Hull";
    platform.type = VehicleType::WeaponPlatform;
    platform.tonnage = 50;
    rs.vehicleSizes.push_back(platform);
    ruleset::VehicleSize giant;
    giant.name = giant.shortName = "CT Giant Hull";
    giant.type = VehicleType::Ship;
    giant.tonnage = 900;
    giant.enginesPerMove = 1;
    rs.vehicleSizes.push_back(giant);

    ruleset::WeaponMount mount;
    mount.longName = mount.shortName = "CT Long Mount";
    mount.damagePercent = 150;
    mount.structurePercent = 150;
    mount.supplyPercent = 150;
    mount.rangeModifier = 2;
    mount.toHitModifier = 40;
    mount.weaponTypeRequirement = "Direct Fire";
    mount.vehicleType = "Any";
    rs.weaponMounts.push_back(mount);
    rs.reindex();
    return rs;
}

const Rules& combatRules() {
    static const Rules rules{buildCombatRuleset()};
    return rules;
}

int32_t mountIndex(const Rules& r, std::string_view name) {
    for (size_t i = 0; i < r.data().weaponMounts.size(); ++i)
        if (r.data().weaponMounts[i].longName == name) return static_cast<int32_t>(i);
    FAIL("no mount " << name);
    return -1;
}

void setTreaty(GameState& s, EmpireId a, EmpireId b, Treaty t) {
    s.empire(a).relation(b).treaty = t;
    s.empire(b).relation(a).treaty = t;
}


// A fresh game on the given rules with no vehicles, every empire at war, and an empty sector to fight in.
Arena makeArena(const Rules& rules, uint64_t seed, int empires) {
    GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = 12;
    setup.options.simultaneous = true;  // written for simultaneous turns; turn-based tests set it off
    for (int i = 0; i < empires; ++i) {
        EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        setup.empires.push_back(std::move(e));
    }
    auto g = createGame(rules, setup);
    REQUIRE_MESSAGE(g.has_value(), (g ? std::string{} : g.error()));
    Arena ar{std::move(*g), {}};
    ar.s.vehicles.clear();
    ar.s.fleets.clear();
    for (size_t i = 0; i < ar.s.empires.size(); ++i)
        for (size_t j = i + 1; j < ar.s.empires.size(); ++j) setTreaty(ar.s, EmpireId{i}, EmpireId{j}, Treaty::War);
    const SystemId sys = ar.s.galaxy.object(homeworld(ar.s, ar.a).planet).system;
    bool found = false;
    for (int y = 3; y < 10 && !found; ++y)
        for (int x = 3; x < 10 && !found; ++x) {
            const Sector sec{x, y};
            bool used = false;
            for (ObjectId o : ar.s.galaxy.system(sys).objects) used = used || ar.s.galaxy.object(o).sector == sec;
            if (!used) {
                ar.loc = {sys, sec};
                found = true;
            }
        }
    REQUIRE(found);
    return ar;
}

Arena makeArena(uint64_t seed, int empires) { return makeArena(combatRules(), seed, empires); }

DesignId design(GameState& s, EmpireId owner, std::string_view name, std::string_view hull, std::initializer_list<std::string_view> parts) {
    return addTestDesign(s, combatRules(), owner, name, hull, parts);
}

VehicleId spawn(GameState& s, DesignId d, Location where, int count) {
    Vehicle& v = addTestVehicle(s, combatRules(), d, where);
    v.count = count;
    return v.id;
}

// A crewed frigate with `engines` engines plus the given parts, and a fuel pod
// last (a ship without supply storage can never fire, spec 04 §6).
DesignId frigate(GameState& s, EmpireId owner, std::string_view name, int engines, std::initializer_list<std::string_view> extra,
                 const Rules& r) {
    Design d;
    d.owner = owner;
    d.name = std::string(name);
    d.hull = hullIndex(r, "Test Frigate");
    for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters"}) d.entries.push_back({componentIndex(r, c), -1});
    for (int i = 0; i < engines; ++i) d.entries.push_back({componentIndex(r, "Test Engine"), -1});
    for (auto c : extra) d.entries.push_back({componentIndex(r, c), -1});
    d.entries.push_back({componentIndex(r, "CT Fuel Pod"), -1});
    return addDesign(s, std::move(d));
}

void useStrategy(GameState& s, EmpireId e, std::vector<std::pair<std::string, std::string>> settings) {
    s.empire(e).strategies = {ruleset::CombatStrategy{"Test Plan", std::move(settings)}};
}

// Marks a vehicle as having moved in this turn from the sector (dx, dy) away.
void arriveFrom(GameState& s, VehicleId id, int dx, int dy) {
    Vehicle& v = *s.vehicle(id);
    v.cameFrom = {v.location.system, Sector{v.location.sector.x + dx, v.location.sector.y + dy}};
    v.cameFromTurn = s.turn;
}

// Marks a vehicle as having come through a warp point this turn (from another system).
void warpIn(GameState& s, VehicleId id) {
    Vehicle& v = *s.vehicle(id);
    const uint32_t systems = static_cast<uint32_t>(s.galaxy.systems.size());
    v.cameFrom = {SystemId{(v.location.system.value + 1) % systems}, Sector{0, 0}};
    v.cameFromTurn = s.turn;
}

int moodCount(const TurnContext& ctx, EmpireId e, std::string_view trigger) {
    int n = 0;
    for (const MoodEvent& m : ctx.moodEvents)
        if (m.empire == e && m.trigger == trigger) n += m.count;
    return n;
}

int damageTaken(const GameState& s, VehicleId id) { return vehicleDamageTaken(s, *s.vehicle(id)); }

int countEvents(const CombatRecord& rec, CombatEvent::Kind k) {
    return static_cast<int>(std::count_if(rec.events.begin(), rec.events.end(), [&](const CombatEvent& e) { return e.kind == k; }));
}

int pieceOf(const CombatRecord& rec, VehicleId id) {
    for (size_t i = 0; i < rec.pieces.size(); ++i)
        if (rec.pieces[i].vehicle == id) return static_cast<int>(i);
    FAIL("no piece for vehicle " << id.value);
    return -1;
}

// Damage recorded by Hit events on a piece (the hit's value before shields).
int64_t hitsOn(const CombatRecord& rec, int piece) {
    int64_t total = 0;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Hit && static_cast<int>(e.target) == piece) total += e.amount;
    return total;
}

TurnContext context(GameState& s, const Rules& r) { return TurnContext{r, s, {}, {}, {}}; }

bool destroyed(const Rules& r, const GameState& s, const Vehicle& v, size_t entry) { return !entryIntact(r, s, v, entry); }

std::pair<GameState, Location> battleScenario(int variant, uint64_t seed) {
    Arena ar = makeArena(seed, variant % 3 == 2 ? 3 : 2);
    GameState& s = ar.s;
    Rng pick(seed * 31 + static_cast<uint64_t>(variant));
    const DesignId fighter = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    const DesignId drone = design(s, ar.b, "Dart", "Test Drone Hull", {"Test Engine", "Test Engine", "Test Warhead"});
    const DesignId sat = design(s, ar.b, "Eye", "Test Satellite Hull", {"Test Satellite Gun", "Test Armor Plate"});
    if (variant % 4 == 1) useStrategy(s, ar.a, {{"Primary Movement Strategy", "Board Enemy Ships"}, {"Secondary Movement Strategy", "Maximum Weapons Range"}});
    if (variant % 4 == 2) useStrategy(s, ar.b, {{"Primary Movement Strategy", "Ram"}, {"Secondary Movement Strategy", "Point Blank"}});
    if (variant % 4 == 3) useStrategy(s, ar.b, {{"Primary Movement Strategy", "Don't Get Hurt"}});
    std::vector<VehicleId> fleetA;
    const int na = 2 + pick.rangeInt(0, 3);
    for (int i = 0; i < na; ++i) {
        const int kind = pick.rangeInt(0, 4);
        DesignId d;
        switch (kind) {
            case 0: d = frigate(s, ar.a, std::format("Lancer {}", i), 3, {"Test Laser", "Test Laser", "Test Armor Plate", "Test Shield"}); break;
            case 1: d = frigate(s, ar.a, std::format("Archer {}", i), 2, {"Test Missile", "CT Torpedo", "Test Armor Plate"}); break;
            case 2: d = frigate(s, ar.a, std::format("Carrier {}", i), 2, {"Test Fighter Bay", "Test Laser", "CT Big Armor"}); break;
            case 3: d = frigate(s, ar.a, std::format("Boarder {}", i), 4, {"Test Boarding Party", "Test Boarding Party", "Test Point Defense"}); break;
            default: d = frigate(s, ar.a, std::format("Tug {}", i), 3, {"CT Tractor", "CT Jammer", "Test Disruptor", "CT Stealth Armor"}); break;
        }
        const VehicleId v = spawn(s, d, ar.loc);
        if (kind == 2) s.vehicle(v)->cargo.units.push_back({fighter, 3 + pick.rangeInt(0, 4)});
        fleetA.push_back(v);
    }
    if (variant % 2 == 0) {
        Fleet f;
        f.owner = ar.a;
        f.name = "Alpha";
        f.members = fleetA;
        f.leader = fleetA.front();
        const FleetId fid = s.addFleet(f).id;
        for (VehicleId v : fleetA) s.vehicle(v)->fleet = fid;
    }
    const int nb = 2 + pick.rangeInt(0, 3);
    for (int i = 0; i < nb; ++i) {
        const int kind = pick.rangeInt(0, 4);
        DesignId d;
        switch (kind) {
            case 0: d = frigate(s, ar.b, std::format("Raider {}", i), 3, {"CT Torpedo", "Test Laser", "Test Armor Plate"}); break;
            case 1: d = frigate(s, ar.b, std::format("Picket {}", i), 2, {"CT PD", "Test Disruptor", "Test Armor Plate"}); break;
            case 2: d = frigate(s, ar.b, std::format("Drone Carrier {}", i), 1, {"CT Drone Bay", "CT Big Armor"}); break;
            case 3: d = frigate(s, ar.b, std::format("Preacher {}", i), 3, {"CT Converter", "CT Quad Gun", "CT Emissive Armor"}); break;
            default: d = frigate(s, ar.b, std::format("Brawler {}", i), 4, {"CT Short Gun", "CT Organic Armor", "CT Crystal Armor", "Test Warhead"}); break;
        }
        const VehicleId v = spawn(s, d, ar.loc);
        if (kind == 2) s.vehicle(v)->cargo.units.push_back({drone, 2 + pick.rangeInt(0, 3)});
        // B arrives: from a neighbouring sector (an edge of the map), or through a
        // warp point (the middle, beside A, spec 04 §3).
        // (dy is drawn before dx: two draws as arguments of one call would come
        // in a different order with different compilers.)
        if (pick.rangeInt(0, 2) == 0) {
            const int dy = pick.rangeInt(-1, 1);
            const int dx = pick.rangeInt(-1, 1);
            arriveFrom(s, v, dx, dy);
        } else {
            warpIn(s, v);
        }
    }
    if (variant % 3 == 1) spawn(s, sat, ar.loc, 3);
    if (variant % 3 == 2) {
        spawn(s, frigate(s, ar.c, "Third", 2, {"Test Laser", "CT Long Gun", "Test Armor Plate"}), ar.loc);
    }
    if (variant % 5 == 4) {
        // The fight moves to B's homeworld with platforms and invaders.
        Colony& hw = homeworld(s, ar.b);
        const Location there = locationOf(s.galaxy, hw.planet);
        const DesignId platform = design(s, ar.b, "Bastion", "CT Platform Hull", {"CT Platform Gun", "CT Platform Gun", "CT Platform Core"});
        hw.cargo.units.push_back({platform, 3});
        const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
        const VehicleId tr = spawn(s, frigate(s, ar.a, "Transport", 2, {"Test Cargo Bay", "Test Cargo Bay"}), there);
        s.vehicle(tr)->cargo.units.push_back({trooper, 4});
        for (Vehicle& v : s.vehicles) v.location = there;
        useStrategy(s, ar.a, {{"Primary Movement Strategy", "Drop Troops"}, {"Secondary Movement Strategy", "Optimal Weapons Range"}});
        ar.loc = there;
    }
    return {std::move(ar.s), ar.loc};
}

} // namespace opense4::ctest
