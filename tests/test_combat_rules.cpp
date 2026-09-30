// The combat rules settled on 2026-09-30 (docs/spec/04 §2-§17): damage of
// unit groups and seekers, shield-multiplier types, organic armor, cargo lost
// at once, mines against unit groups, special damage on planets, planets'
// facilities, ground combat, the computer's launches and targeting, design
// statistics and experience, the tactical window's Auto, Resolve Combat and
// launch windows, who is asked, and the combat simulator. All content is
// invented for the tests.

#include "combat_fixture.hpp"

#include "game/combat.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/serialize.hpp"
#include "game/simulator.hpp"
#include "game/tactical.hpp"
#include "game/turn.hpp"
#include "game/turn_internal.hpp"
#include "game/xmath.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <map>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::ctest;
using opense4::test::addTestDesign;
using opense4::test::homeworld;
namespace combat = opense4::game::combat;
using combat::DamageType;
using combat::TacticalBattle;
using combat::TacticalOrder;
using OK = combat::TacticalOrder::Kind;

namespace {

int pieceIndex(const TacticalBattle& b, VehicleId v) {
    for (size_t i = 0; i < b.pieces().size(); ++i)
        if (b.pieces()[i].vehicle == v) return static_cast<int>(i);
    FAIL("no piece for vehicle " << v.value);
    return -1;
}

int planetPiece(const CombatRecord& rec, ObjectId planet) {
    for (size_t i = 0; i < rec.pieces.size(); ++i)
        if (rec.pieces[i].planet == planet) return static_cast<int>(i);
    FAIL("no piece for planet " << planet.value);
    return -1;
}

} // namespace

// ---- Damage -----------------------------------------------------------------------------------------------

TEST_CASE("combat rules: the shield-multiplier types scale on every hit, even with no shields left") {
    // Spec 04 §9.5, Q33: scaled and scaled back, so Half and Quarter lose their remainders.
    auto through = [](DamageType t, int64_t d, int shields) {
        combat::detail::ShieldState sh;
        sh.current = sh.max = shields;
        sh.kind = combat::detail::ShieldState::Kind::Normal;
        return combat::detail::absorbShields(sh, d, combat::detail::damageRule(t));
    };
    CHECK(through(DamageType::HalfDamageToShields, 1, 0) == 0);
    CHECK(through(DamageType::HalfDamageToShields, 5, 0) == 4);
    CHECK(through(DamageType::QuarterDamageToShields, 3, 0) == 0);
    CHECK(through(DamageType::QuarterDamageToShields, 7, 0) == 4);
    CHECK(through(DamageType::QuadDamageToShields, 5, 0) == 5);
    CHECK(through(DamageType::DoubleDamageToShields, 5, 0) == 5);
    // Against shields: 15 × 4 = 60, 20 absorbed, 40 left, back to 10.
    CHECK(through(DamageType::QuadDamageToShields, 15, 20) == 10);
}

TEST_CASE("combat rules: a unit group's damage pool, shield pool and draws") {
    // Spec 04 §9.4, Q38: H = structure + X (X twice for fighters); a unit dies when
    // P + Q reaches H; then H − X leaves P and X leaves Q (with Q above 0).
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId sat = design(s, ar.a, "Shielded Sat", "Test Satellite Hull", {"Test Satellite Gun", "Test Shield"});
    const combat::detail::UnitToughness u = combat::detail::unitToughness(r, s.design(sat));
    REQUIRE(u.shields == 20);
    REQUIRE_FALSE(u.doubled);
    const int64_t h = u.hitPoints();   // structure + 20
    std::vector<UnitStack> stacks{{sat, 5}};
    const std::vector<size_t> entries{0};
    std::vector<int> killed;
    Rng rng(3);
    int64_t pool = 0, shield = 0;
    // Shields Only fills the shield pool and kills nothing.
    CHECK(combat::detail::hitUnits(r, s, stacks, entries, pool, shield, 20, DamageType::ShieldsOnly, rng, killed) == 0);
    CHECK(shield == 20);
    CHECK(pool == 0);
    // A Normal hit of H − 20: with the shield pool it kills one unit; both pools pay.
    CHECK(combat::detail::hitUnits(r, s, stacks, entries, pool, shield, h - 20, DamageType::Normal, rng, killed) == 1);
    CHECK(stacks[0].count == 4);
    CHECK(pool == 0);
    CHECK(shield == 0);
    // A type that skips shields: dies when P reaches H − X; the shield pool does not help.
    shield = 1000;
    CHECK(combat::detail::hitUnits(r, s, stacks, entries, pool, shield, h - 21, DamageType::SkipsAllShields, rng, killed) == 0);
    CHECK(pool == h - 21);
    CHECK(combat::detail::hitUnits(r, s, stacks, entries, pool, shield, 1, DamageType::SkipsAllShields, rng, killed) == 1);
    shield = 0;
    // Another type is judged on its own damage and leaves the pool as it was.
    pool = 7;
    CHECK(combat::detail::hitUnits(r, s, stacks, entries, pool, shield, h - 1, DamageType::OnlyEngines, rng, killed) == 0);
    CHECK(pool == 7);
    CHECK(combat::detail::hitUnits(r, s, stacks, entries, pool, shield, h * 2, DamageType::OnlyEngines, rng, killed) == 2);
    CHECK(pool == 7);
    // The pool holds at most 50000.
    pool = 0;
    std::vector<UnitStack> big{{sat, 0}};
    CHECK(combat::detail::hitUnits(r, s, big, entries, pool, shield, 70000, DamageType::Normal, rng, killed) == 0);
    CHECK(pool == combat::detail::kMaxUnitPool);
}

TEST_CASE("combat rules: every design entry is drawn, and a dead one wastes the draw") {
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId light = design(s, ar.a, "Light", "Test Satellite Hull", {"Test Satellite Gun"});
    const DesignId heavy = design(s, ar.a, "Heavy", "Test Satellite Hull", {"Test Satellite Gun", "Test Armor Plate"});
    // One entry is dead: over many hits, fewer light units die than 20 draws a hit
    // would kill if the dead entry were skipped.
    int wasted = 0;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        std::vector<UnitStack> stacks{{heavy, 0}, {light, 40}};
        const std::vector<size_t> entries{0, 1};
        std::vector<int> killed;
        Rng rng(seed);
        int64_t pool = 0, shield = 0;
        const int64_t h = combat::detail::unitHitPoints(r, s.design(light));
        const int n = combat::detail::hitUnits(r, s, stacks, entries, pool, shield, h * 20, DamageType::Normal, rng, killed);
        CHECK(n <= 20);
        CHECK(killed[0] == 0);
        CHECK(killed[1] == n);
        if (n < 20) ++wasted;
    }
    CHECK(wasted > 10);   // about half the draws land on the dead entry
}

TEST_CASE("combat rules: organic armor skips the parts it cannot pay for") {
    // Spec 04 §9.3, Q42.
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId d = design(s, ar.a, "Grower", "Test Frigate", {"Test Bridge", "CT Big Armor", "CT Organic Armor"});
    Vehicle v;
    v.design = d;
    v.damage.assign(s.design(d).entries.size(), 0);
    // Both regenerating parts... only the organic one regenerates; destroy it and the big armor.
    combat::detail::destroyEntry(r, s.design(d), v, 1);
    combat::detail::destroyEntry(r, s.design(d), v, 2);
    CHECK(combat::detail::hasDestroyedRegeneratingArmor(r, s, v));
    CHECK(combat::detail::restoreRegeneratingArmor(r, s, v, 30) == 0);    // 40 structure: too dear
    CHECK(combat::detail::restoreRegeneratingArmor(r, s, v, 40) == 40);
    CHECK_FALSE(combat::detail::hasDestroyedRegeneratingArmor(r, s, v));
    // With two regenerating parts, a dear one first in design order is skipped and a cheaper one later restored.
    ruleset::Ruleset rs = buildCombatRuleset();
    part(rs, "CT Thick Organic", 90, {ab(AbilityKind::Armor), ab(AbilityKind::ArmorRegeneration, 5)});
    rs.reindex();
    const Rules rules{std::move(rs)};
    Arena ar2 = makeArena(rules);
    GameState& s2 = ar2.s;
    const DesignId d2 = addTestDesign(s2, rules, ar2.a, "Grower II", "Test Frigate", {"Test Bridge", "CT Thick Organic", "CT Organic Armor"});
    Vehicle w;
    w.design = d2;
    w.damage.assign(3, 0);
    combat::detail::destroyEntry(rules, s2.design(d2), w, 1);
    combat::detail::destroyEntry(rules, s2.design(d2), w, 2);
    CHECK(combat::detail::restoreRegeneratingArmor(rules, s2, w, 50) == 40);
    CHECK_FALSE(entryIntact(rules, s2, w, 1));
    CHECK(entryIntact(rules, s2, w, 2));
}

TEST_CASE("combat rules: cargo that no longer fits is lost at once, population first") {
    // Spec 04 §9.4, Q20; spec 03 §11: 1M at a time from the first group, then units from the first stack.
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle"});
    const DesignId d = frigate(s, ar.a, "Hauler", 1, {"Test Cargo Bay", "Test Cargo Bay"});
    const VehicleId id = spawn(s, d, ar.loc);
    Vehicle& v = *s.vehicle(id);
    REQUIRE(vehicleCargoCapacity(r, s, v) == 100);
    v.cargo.population = {{ar.a, 4}, {ar.b, 4}};   // 40 kT
    v.cargo.units = {{trooper, 3}};                // 60 kT
    // One bay destroyed: 50 kT left for 100 kT of cargo.
    size_t bay = 0;
    for (size_t i = 0; i < s.design(d).entries.size(); ++i)
        if (r.component(s.design(d).entries[i].component).name == "Test Cargo Bay") bay = i;
    combat::detail::destroyEntry(r, s.design(d), v, bay);
    combat::detail::cutCargo(r, s, v);
    CHECK(v.cargo.population.empty());                         // both groups, the first one first
    CHECK(v.cargo.units == std::vector<UnitStack>{{trooper, 2}});   // then one unit
    CHECK(cargoSpaceUsed(r, s, v.cargo) <= vehicleCargoCapacity(r, s, v));
}

// ---- Formations ------------------------------------------------------------------------------------------------

TEST_CASE("combat rules: a fleet's first armed member anchors its group when the leader is away") {
    // Spec 03 §9, §10: only armed members form the combat group; with the leader
    // elsewhere, the first armed member in piece order anchors it.
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId armed = frigate(s, ar.a, "Liner", 1, {"CT Gun"});
    const DesignId unarmed = frigate(s, ar.a, "Tender", 1, {"Test Cargo Bay"});
    const Location away{ar.loc.system, Sector{ar.loc.sector.x + 1, ar.loc.sector.y}};
    const VehicleId absent = spawn(s, armed, away);
    const VehicleId tender = spawn(s, unarmed, ar.loc);
    const VehicleId first = spawn(s, armed, ar.loc), second = spawn(s, armed, ar.loc);
    Fleet f;
    f.owner = ar.a;
    f.members = {absent, tender, first, second};
    f.leader = absent;
    f.formation = 0;
    const FleetId fid = s.addFleet(f).id;
    for (VehicleId v : f.members) s.vehicle(v)->fleet = fid;
    warpIn(s, spawn(s, design(s, ar.b, "Target", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc));
    TacticalBattle b(combatRules(), s, TacticalBattle::Setup{ar.loc, std::vector<VehicleId>{}, {ar.a}});
    REQUIRE(b.started());
    const combat::TacticalPiece& lead = b.pieces()[static_cast<size_t>(pieceIndex(b, first))];
    CHECK(lead.isLeader);
    CHECK(b.pieces()[static_cast<size_t>(pieceIndex(b, second))].leader == pieceIndex(b, first));
    CHECK(b.pieces()[static_cast<size_t>(pieceIndex(b, tender))].leader == -1);
    CHECK_FALSE(b.pieces()[static_cast<size_t>(pieceIndex(b, tender))].isLeader);
}

// ---- Mines ---------------------------------------------------------------------------------------------------

TEST_CASE("combat rules: mines strike unit groups by the unit group rule") {
    // Spec 04 §10.6, Q45: several units may die per warhead; every type affects a unit
    // group; the mine is spent whatever its warheads did; the mine's design is credited
    // only when the whole group dies, for every unit it had.
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId mine = design(s, ar.b, "Mine", "Test Mine Hull", {"Test Warhead"});
    const VehicleId field = spawn(s, mine, ar.loc, 2);
    const DesignId sat = design(s, ar.a, "Buoy", "Test Satellite Hull", {"Test Satellite Gun"});   // 10 hit points
    REQUIRE(combat::detail::unitHitPoints(r, s.design(sat)) == 10);
    const VehicleId group = spawn(s, sat, ar.loc, 12);
    arriveFrom(s, group, 1, 0);
    TurnContext ctx = context(s);
    const std::vector<VehicleId> entering{group};
    combat::resolveSpaceCombat(ctx, ar.loc, entering);
    // Two 60-point warheads: six units each.
    const bool spent = s.vehicle(field) == nullptr || s.vehicle(field)->count == 0;
    CHECK(spent);
    CHECK((s.vehicle(group) == nullptr || s.vehicle(group)->count == 0));
    CHECK(s.design(sat).lost == 12);
    CHECK(s.design(mine).enemyTonnageDestroyed == 12 * int64_t{r.hull(s.design(sat).hull).tonnage});
}

TEST_CASE("combat rules: a mine picks immune vehicles too, and is spent") {
    // Spec 04 §10.6: a warhead that cannot affect the vehicle is skipped, the mine used up.
    ruleset::Ruleset rs = buildCombatRuleset();
    gun(rs, "CT Engine Warhead", ruleset::WeaponKind::Warhead, {50}, "Only Engines");
    rs.reindex();
    const Rules rules{std::move(rs)};
    Arena ar = makeArena(rules);
    GameState& s = ar.s;
    const DesignId mine = addTestDesign(s, rules, ar.b, "Snare", "Test Mine Hull", {"CT Engine Warhead"});
    Vehicle& laid = opense4::test::addTestVehicle(s, rules, mine, ar.loc);
    laid.count = 3;
    const VehicleId field = laid.id;
    // A base without engines: no warhead of this mine can touch it.
    const VehicleId base = opense4::test::addTestVehicle(s, rules, addTestDesign(s, rules, ar.a, "Fort", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc).id;
    arriveFrom(s, base, 0, 1);
    TurnContext ctx = context(s, rules);
    const std::vector<VehicleId> entering{base};
    combat::resolveSpaceCombat(ctx, ar.loc, entering);
    const bool spent = s.vehicle(field) == nullptr || s.vehicle(field)->count == 0;
    CHECK(spent);
    CHECK(damageTaken(s, base) == 0);
}

// ---- Planets ---------------------------------------------------------------------------------------------------

namespace {

// A's gunship against B's homeworld (with its platforms), where the battle is.
struct Siege {
    Arena ar = makeArena(5);
    ObjectId planet;
    Location there;
    DesignId platform;

    Siege() {
        GameState& s = ar.s;
        Colony& hw = homeworld(s, ar.b);
        planet = hw.planet;
        there = locationOf(s.galaxy, planet);
        platform = design(s, ar.b, "Bastion", "CT Platform Hull", {"CT Platform Gun", "CT Platform Core"});
        hw.cargo = {};
    }
    VehicleId ship(std::string_view name, std::initializer_list<std::string_view> parts, bool warp = true) {
        const VehicleId id = spawn(ar.s, frigate(ar.s, ar.a, name, 3, parts), there);
        if (warp) warpIn(ar.s, id);
        return id;
    }
};

} // namespace

TEST_CASE("combat rules: other damage types against planets") {
    // Spec 04 §9.5 "Against planets", Q40.
    // Reload types lengthen the planet's reloads; its platforms fire less.
    auto platformShots = [](bool jam) {
        Siege g;
        GameState& s = g.ar.s;
        homeworld(s, g.ar.b).cargo.units = {{g.platform, 2}};
        useStrategy(s, g.ar.a, {{"Primary Movement Strategy", "Point Blank"}});
        g.ship(jam ? "Jammer" : "Gunner", {jam ? "CT Jammer" : "CT Gun", "CT Always Hit", "CT Big Armor", "CT Big Armor", "CT Big Armor"});
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, g.there);
        REQUIRE_FALSE(s.combats.empty());
        const CombatRecord& rec = s.combats.back();
        const int pp = planetPiece(rec, g.planet);
        int shots = 0;
        for (const CombatEvent& e : rec.events)
            if (e.kind == CombatEvent::Kind::Fire && static_cast<int>(e.piece) == pp) ++shots;
        return shots;
    };
    const int free = platformShots(false);
    const int jammed = platformShots(true);
    CHECK(free > 0);
    CHECK(jammed < free);
}

TEST_CASE("combat rules: a conversion weapon that can target planets turns the planet's piece, not the colony") {
    Siege g;
    GameState& s = g.ar.s;
    Colony& hw = homeworld(s, g.ar.b);
    hw.cargo.units = {{g.platform, 1}};
    useStrategy(s, g.ar.a, {{"Primary Movement Strategy", "Point Blank"}});
    g.ship("Preacher", {"CT Converter", "CT Always Hit", "CT Big Armor", "CT Big Armor"});   // 100 damage: always converts
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, g.there);
    REQUIRE_FALSE(s.combats.empty());
    const CombatRecord& rec = s.combats.back();
    const int pp = planetPiece(rec, g.planet);
    CHECK(std::any_of(rec.events.begin(), rec.events.end(), [&](const CombatEvent& e) {
        return e.kind == CombatEvent::Kind::Captured && static_cast<int>(e.piece) == pp && e.amount == static_cast<int>(g.ar.a.value);
    }));
    CHECK(s.colony(g.planet)->owner == g.ar.b);   // the colony keeps its owner (spec 04 §9.5)
}

TEST_CASE("combat rules: a planet's population falls from its first group, and lost facilities go at the end") {
    // Spec 04 §11: the population loses from the first group on; the facility roll
    // is made on every population hit; lost facilities keep working until the end.
    Siege g;
    GameState& s = g.ar.s;
    Colony& hw = homeworld(s, g.ar.b);
    hw.population = {{g.ar.b, 30}, {g.ar.a, 200}};
    const size_t facilities = hw.facilities.size();
    REQUIRE(facilities > 0);
    useStrategy(s, g.ar.a, {{"Primary Movement Strategy", "Point Blank"}});
    g.ship("Bomber", {"CT Gun", "CT Always Hit", "CT Big Armor", "CT Big Armor"});   // 1M a hit
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, g.there);
    const Colony* after = s.colony(g.planet);
    REQUIRE(after);
    const int64_t killed = 230 - after->totalPopulation();
    REQUIRE(killed > 0);
    if (killed < 30) {
        CHECK(after->population.front().millions == 30 - killed);
        CHECK(after->population.back().millions == 200);
    } else {
        CHECK(std::none_of(after->population.begin(), after->population.end(), [&](const PopulationGroup& p) { return p.race == g.ar.b; }));
    }
    CHECK(after->facilities.size() <= facilities);
}

TEST_CASE("combat rules: when the last platform dies the other stored units take the same hit again") {
    Siege g;
    GameState& s = g.ar.s;
    Colony& hw = homeworld(s, g.ar.b);
    const DesignId sat = design(s, g.ar.b, "Buoy", "Test Satellite Hull", {"Test Satellite Gun"});
    hw.cargo.units = {{g.platform, 1}, {sat, 2}};
    useStrategy(s, g.ar.a, {{"Primary Movement Strategy", "Point Blank"}});
    // One 100-point hit kills the platform (30 + 10 structure, 40 hit points) and both
    // satellites (20 each) with the same 100 again; the population is then hit too.
    g.ship("Bomber", {"CT Big Gun", "CT Always Hit", "CT Big Armor", "CT Big Armor", "CT Big Armor"});
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, g.there);
    CHECK(s.design(g.platform).lost == 1);
    CHECK(s.design(sat).lost == 2);
}

// ---- Ground combat --------------------------------------------------------------------------------------------

TEST_CASE("combat rules: ground combat hits armed troops first, then militia, then everything else") {
    // Spec 04 §13, Q41: damage reaches the defender's armed troops before its
    // unarmed ones, whatever the cargo order.
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& target = homeworld(s, ar.b);
    target.population = {{ar.b, 10}};   // no militia
    const DesignId marksman = design(s, ar.a, "Marksman", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor", "CT Troop Scope"});
    const DesignId unarmed = design(s, ar.b, "Porter", "Test Troop Hull", {"Test Troop Armor"});                 // 20 hit points
    const DesignId armed = design(s, ar.b, "Guard", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});   // 30 hit points
    target.cargo.units = {{unarmed, 1}, {armed, 1}};
    target.landedTroops = {{marksman, 20}};
    target.invader = ar.a;
    target.militia = -1;
    combat::detail::GroundFight fight;
    fight.attacker = ar.a;
    fight.defender = ar.b;
    fight.invaders = &target.landedTroops;
    fight.cargo = &target.cargo;
    fight.population = &target.population;
    fight.militia = &target.militia;
    combat::CombatSettings cs = combat::loadSettings(combatRules());
    cs.groundTurns = 1;
    // Twenty marksmen always hit: 160 a round, 30 % of it is 48, then the race's modifier.
    const int racial = combat::detail::groundModifier(combatRules(), s.empire(ar.a));
    const int64_t total = 48 + xmath::pctRound(48, racial);
    REQUIRE(total >= 30);
    REQUIRE(total < 50);
    Rng rng(9);
    const combat::detail::GroundOutcome o = combat::detail::fightGround(combatRules(), s, cs, fight, rng);
    CHECK(o.rounds == 1);
    CHECK(target.cargo.units[1].count == 0);   // the armed guard fell first
    CHECK(target.cargo.units[0].count == 1);   // what was left could not kill the porter
    // Each loss credits a stack of the killing side with the dead units' hull tonnage.
    CHECK(s.design(marksman).enemyTonnageDestroyed == combatRules().hull(s.design(armed).hull).tonnage);
}

TEST_CASE("combat rules: stored units other than troops do not stop a capture") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& target = homeworld(s, ar.b);
    target.population = {{ar.b, 10}};
    const DesignId marksman = design(s, ar.a, "Marksman", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor", "CT Troop Scope"});
    const DesignId fighter = design(s, ar.b, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "CT Big Armor"});
    target.cargo.units = {{fighter, 3}};
    target.landedTroops = {{marksman, 5}};
    target.invader = ar.a;
    TurnContext ctx = context(s);
    combat::runGroundCombat(ctx, ar.b);
    const Colony* after = s.colony(target.planet);
    REQUIRE(after);
    CHECK(after->owner == ar.a);
    CHECK(after->cargo.unitCount(marksman) == 5);   // the survivors join the cargo
}

// ---- The computer's launches and targets --------------------------------------------------------------------------

TEST_CASE("combat rules: the computer never launches satellites, and drones one per group within Drones Per Target") {
    // Spec 04 §10.4, §10.5, §10.7.
    Arena ar = makeArena();
    GameState& s = ar.s;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Don't Get Hurt"}, {"Drones Per Target", "1"}});
    const DesignId drone = design(s, ar.a, "Dart", "Test Drone Hull", {"Test Engine", "Test Engine", "Test Warhead"});
    const DesignId sat = design(s, ar.a, "Eye", "Test Satellite Hull", {"Test Satellite Gun"});
    const VehicleId carrier = spawn(s, frigate(s, ar.a, "Rack", 1, {"CT Drone Bay", "CT Drone Bay", "CT Big Armor"}), ar.loc);
    s.vehicle(carrier)->cargo.units = {{drone, 4}, {sat, 2}};
    const VehicleId hulk = spawn(s, design(s, ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor", "CT Big Armor"}), ar.loc);
    warpIn(s, hulk);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    const CombatRecord& rec = s.combats.front();
    std::map<int, int> perRound;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Launch && rec.pieces[e.piece].kind == CombatPiece::Kind::UnitGroup) {
            CHECK(rec.pieces[e.piece].design == drone);   // never a satellite
            CHECK(e.amount == 1);                         // one drone per group
            ++perRound[e.round];
        }
    // One hostile ship or base × 1 per target − the side's drones alive: one drone at a time.
    for (const auto& [round, n] : perRound) CHECK(n == 1);
    CHECK_FALSE(perRound.empty());
    const Vehicle* after = s.vehicle(carrier);
    if (after && after->count > 0) CHECK(after->cargo.unitCount(sat) == 2);
}

TEST_CASE("combat rules: a fighter amount of 0 launches all of a carrier's fighters in one group") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Don't Get Hurt"}, {"Fighters Launch Group Amount", "0"}});
    const DesignId fighter = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    const VehicleId carrier = spawn(s, frigate(s, ar.a, "Carrier", 1, {"Test Fighter Bay", "Test Fighter Bay", "CT Big Armor"}), ar.loc);
    s.vehicle(carrier)->cargo.units = {{fighter, 7}};
    warpIn(s, spawn(s, design(s, ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    const CombatRecord& rec = s.combats.front();
    std::vector<int> groups;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Launch && rec.pieces[e.piece].kind == CombatPiece::Kind::UnitGroup) groups.push_back(e.amount);
    REQUIRE_FALSE(groups.empty());
    CHECK(groups.front() == 7);
}

TEST_CASE("combat rules: holding fire for an invasion, and never on a planet where one's troops fight") {
    // Spec 04 §16, Q26.
    Siege g;
    GameState& s = g.ar.s;
    Colony& hw = homeworld(s, g.ar.b);
    hw.population = {{g.ar.b, 400}};   // twenty militia: the landing does not win at once
    const DesignId trooper = design(s, g.ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    hw.landedTroops = {{trooper, 1}};
    hw.invader = g.ar.a;
    hw.militia = 20;
    // A's own troops fight on the ground: its gunship never targets the planet.
    useStrategy(s, g.ar.a, {{"Primary Movement Strategy", "Point Blank"}});
    g.ship("Gunner", {"CT Gun", "CT Always Hit", "CT Big Armor"});
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, g.there);
    if (!s.combats.empty()) {
        const CombatRecord& rec = s.combats.back();
        const int pp = planetPiece(rec, g.planet);
        for (const CombatEvent& e : rec.events) CHECK(!(e.kind == CombatEvent::Kind::Fire && static_cast<int>(e.target) == pp));
    }
}

TEST_CASE("combat rules: a side with a troop ship on Drop Troops holds fire on planets without guns") {
    Siege g;
    GameState& s = g.ar.s;
    Colony& hw = homeworld(s, g.ar.b);
    hw.population = {{g.ar.b, 400}};
    const DesignId trooper = design(s, g.ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    useStrategy(s, g.ar.a, {{"Primary Movement Strategy", "Drop Troops"}, {"Secondary Movement Strategy", "Point Blank"}});
    const VehicleId lander = g.ship("Lander", {"Test Cargo Bay", "Test Cargo Bay", "CT Big Armor"}, false);
    s.vehicle(lander)->cargo.units = {{trooper, 1}};
    g.ship("Gunner", {"CT Gun", "CT Always Hit", "CT Big Armor"});
    // The gunner holds fire until the lander has landed.
    combat::TacticalBattle b(combatRules(), s, TacticalBattle::Setup{g.there, std::vector<VehicleId>{}, {}});
    REQUIRE(b.started());
    b.finish();
    const CombatRecord& rec = b.record();
    const int pp = planetPiece(rec, g.planet);
    int landedRound = 1000;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Launch && static_cast<int>(e.target) == pp) landedRound = std::min<int>(landedRound, e.round);
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Fire && static_cast<int>(e.target) == pp) CHECK(e.round >= landedRound);
}

// ---- Statistics and experience ------------------------------------------------------------------------------------

TEST_CASE("combat rules: a kill by a group that mixes designs credits every design in it") {
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId wasp = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    const DesignId hornet = design(s, ar.a, "Hornet", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "Test Fighter Gun", "CT Fighter Fuel"});
    const VehicleId group = spawn(s, wasp, ar.loc, 4);
    addGroupUnits(s, *s.vehicle(group), hornet, 6);
    s.vehicle(group)->supply = vehicleSupplyCapacity(r, s, *s.vehicle(group));
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Point Blank"}});
    const DesignId prey = design(s, ar.b, "Prey", "Test Station", {"Test Bridge"});
    warpIn(s, spawn(s, prey, ar.loc));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.design(prey).lost == 1);
    const int64_t tons = r.hull(s.design(prey).hull).tonnage;
    CHECK(s.design(wasp).enemyTonnageDestroyed == tons);
    CHECK(s.design(hornet).enemyTonnageDestroyed == tons);
}

TEST_CASE("combat rules: a capture changes no design statistic") {
    Arena ar = makeArena(3);
    GameState& s = ar.s;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Board Enemy Ships"}});
    const DesignId boarderDesign = frigate(s, ar.a, "Boarder", 4, {"Test Boarding Party", "Test Boarding Party", "CT Combat Thruster"});
    spawn(s, boarderDesign, ar.loc);
    const DesignId prizeDesign = design(s, ar.b, "Prize", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters"});
    const VehicleId prize = spawn(s, prizeDesign, ar.loc);
    warpIn(s, prize);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.vehicle(prize));
    REQUIRE(s.vehicle(prize)->owner == ar.a);
    CHECK(s.design(prizeDesign).lost == 0);
    CHECK(s.design(boarderDesign).enemyTonnageDestroyed == 0);
}

TEST_CASE("combat rules: a ram kill counts twice for the rammer") {
    // Spec 04 §10.3: the rammer's design gets a ship target's tonnage twice; a
    // surviving rammer's crew gains +2.0.
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Ram"}});
    const DesignId rammerDesign = frigate(s, ar.a, "Ram", 4, {"CT Big Armor", "CT Big Armor", "CT Big Armor", "CT Big Armor"});
    const VehicleId rammer = spawn(s, rammerDesign, ar.loc);
    const DesignId skiffDesign = design(s, ar.b, "Skiff", "Test Station", {"Test Bridge"});
    warpIn(s, spawn(s, skiffDesign, ar.loc));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.design(skiffDesign).lost == 1);
    REQUIRE(s.vehicle(rammer));
    CHECK(s.design(rammerDesign).enemyTonnageDestroyed == 2 * int64_t{r.hull(s.design(skiffDesign).hull).tonnage});
    if (s.vehicle(rammer)->count > 0) CHECK(s.vehicle(rammer)->experience == 2);
}

TEST_CASE("combat rules: experience level names") {
    // Spec 04 §15, Q19.
    CHECK(combat::experienceLevel(0) == "Novice");
    CHECK(combat::experienceLevel(5) == "Novice");
    CHECK(combat::experienceLevel(5, 1) == "Experienced");
    CHECK(combat::experienceLevel(10) == "Experienced");
    CHECK(combat::experienceLevel(14, 3) == "Veteran");
    CHECK(combat::experienceLevel(20) == "Veteran");
    CHECK(combat::experienceLevel(30) == "Elite");
    CHECK(combat::experienceLevel(31) == "Legendary");
    CHECK(combat::experienceLabel(14, 3) == "Veteran (+14%)");
    CHECK(combat::experienceLabel(50) == "Legendary (+50%)");
}

// ---- The tactical window ---------------------------------------------------------------------------------------

namespace {

struct Duel {
    Arena ar = makeArena(11);
    VehicleId carrier, gunner, target;
    DesignId wasp, hornet, dart;

    Duel() {
        GameState& s = ar.s;
        wasp = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
        hornet = design(s, ar.a, "Hornet", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel", "CT Fighter Fuel"});
        dart = design(s, ar.a, "Dart", "Test Drone Hull", {"Test Engine", "Test Engine", "Test Warhead"});
        carrier = spawn(s, frigate(s, ar.a, "Carrier", 1, {"Test Fighter Bay", "Test Fighter Bay", "CT Drone Bay", "CT Big Armor"}), ar.loc);
        s.vehicle(carrier)->cargo.units = {{wasp, 3}, {hornet, 3}, {dart, 2}};
        gunner = spawn(s, frigate(s, ar.a, "Gunner", 2, {"Test Laser", "CT Big Armor"}), ar.loc);
        target = spawn(s, design(s, ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor", "CT Big Armor"}), ar.loc);
        warpIn(s, target);
    }
};

} // namespace

TEST_CASE("combat rules: one Launch Units window groups units of one kind, whatever their design") {
    Duel d;
    TacticalBattle b(combatRules(), d.ar.s, TacticalBattle::Setup{d.ar.loc, std::nullopt, {d.ar.a}});
    REQUIRE(b.phaseEmpire() == d.ar.a);
    const int c = pieceIndex(b, d.carrier);
    TacticalOrder first{OK::Launch, d.ar.a, c};
    first.design = d.wasp;
    first.count = 2;
    first.group = 1;   // the window session
    const size_t before = b.pieces().size();
    REQUIRE(b.submit(first).empty());
    REQUIRE(b.pieces().size() == before + 1);
    TacticalOrder second = first;
    second.design = d.hornet;
    second.count = 1;
    REQUIRE(b.submit(second).empty());
    CHECK(b.pieces().size() == before + 1);   // joined the group made earlier in the window
    const combat::TacticalPiece& g = b.pieces()[before];
    CHECK(g.count == 3);
    CHECK(g.units.size() == 2);
    // A new window starts a new group.
    TacticalOrder third = second;
    third.group = 2;
    REQUIRE(b.submit(third).empty());
    CHECK(b.pieces().size() == before + 2);
    // Launch Fighters in Groups launches fighters only.
    TacticalOrder groups{OK::LaunchFighters, d.ar.a, c};
    groups.design = d.dart;
    groups.count = 1;
    groups.group = 5;
    CHECK(b.check(groups) == "Only fighters are launched in groups.");
    // Drones are one per group, whatever the window.
    TacticalOrder drones = first;
    drones.design = d.dart;
    drones.count = 2;
    REQUIRE(b.submit(drones).empty());
    CHECK(b.pieces().size() == before + 4);
    CHECK(b.pieces()[before + 2].count == 1);
    CHECK(b.pieces()[before + 3].count == 1);
    CHECK(b.check(groups) == "It carries no such units.");
}

TEST_CASE("combat rules: Auto is one toggle for every empire, and play pauses after the last player's phase") {
    // Spec 04 §4, Q49.
    auto [start, where] = battleScenario(3, 2);
    TacticalBattle b(combatRules(), start, TacticalBattle::Setup{where, std::nullopt, {EmpireId{0u}, EmpireId{1u}}});
    REQUIRE(b.awaitingOrders());
    const EmpireId first = b.phaseEmpire();
    const int round = b.round();
    TacticalOrder on{OK::Auto, first};
    on.on = true;
    REQUIRE(b.submit(on).empty());
    CHECK(b.autoOn());
    CHECK(b.phaseEmpire() == first);   // pressing it does not play the current phase
    CHECK_FALSE(b.paused());
    REQUIRE(b.submit(TacticalOrder{OK::EndPhase, first}).empty());
    if (!b.finished()) {
        // The other empire's phase is played by its strategies; play pauses after the
        // last player's phase of the combat turn.
        REQUIRE(b.paused());
        CHECK(b.round() == round);
        const EmpireId pausedFor = b.phaseEmpire();
        CHECK(b.check(TacticalOrder{OK::Move, pausedFor, 0}) == "Play is paused: End Turn goes on.");
        REQUIRE(b.submit(TacticalOrder{OK::EndPhase, pausedFor}).empty());
        if (!b.finished()) {
            CHECK(b.paused());
            CHECK(b.round() == round + 1);
            // Released: the players get their sides back from the next phase on.
            TacticalOrder off{OK::Auto, b.phaseEmpire()};
            off.on = false;
            REQUIRE(b.submit(off).empty());
            CHECK_FALSE(b.autoOn());
            REQUIRE(b.submit(TacticalOrder{OK::EndPhase, b.phaseEmpire()}).empty());
            if (!b.finished()) CHECK_FALSE(b.paused());
        }
    }
    b.finish();
    // The script replays to the same battle.
    TacticalBattle replay(combatRules(), start, TacticalBattle::Setup{where, std::nullopt, {EmpireId{0u}, EmpireId{1u}}});
    for (const TacticalOrder& o : b.script()) CHECK(replay.submit(o).empty());
    replay.finish();
    CHECK(stateChecksum(replay.state()) == stateChecksum(b.state()));
}

TEST_CASE("combat rules: Resolve Combat hands every empire to its strategies") {
    auto [start, where] = battleScenario(0, 3);
    TacticalBattle b(combatRules(), start, TacticalBattle::Setup{where, std::nullopt, {EmpireId{0u}, EmpireId{1u}}});
    REQUIRE(b.awaitingOrders());
    REQUIRE(b.submit(TacticalOrder{OK::ResolveCombat, b.phaseEmpire()}).empty());
    CHECK_FALSE(b.isPlayer(EmpireId{0u}));
    CHECK_FALSE(b.isPlayer(EmpireId{1u}));
    CHECK_FALSE(b.awaitingOrders());
    CHECK(b.finished());
}

TEST_CASE("combat rules: one question per battle for every human empire in it, hostile or not") {
    // Spec 04 §3 step 1, Q53: a bystander at peace with both is asked with them.
    Arena ar = makeArena(17, 3);
    GameState& s = ar.s;
    s.options.simultaneous = false;
    for (Empire& e : s.empires) e.kind = PlayerKind::Human;
    setTreaty(s, ar.a, ar.c, Treaty::NonAggression);
    setTreaty(s, ar.b, ar.c, Treaty::NonAggression);
    spawn(s, frigate(s, ar.a, "A", 2, {"Test Laser"}), ar.loc);
    spawn(s, frigate(s, ar.b, "B", 2, {"Test Laser"}), ar.loc);
    spawn(s, frigate(s, ar.c, "Bystander", 2, {"Test Laser"}), ar.loc);
    const std::vector<BattleAnswer> none;
    TurnContext::Battles battles{&none, 0};
    TurnContext ctx = context(s);
    ctx.battles = &battles;
    bool asked = false;
    try {
        combat::resolveSpaceCombat(ctx, ar.loc);
    } catch (const game::detail::BattleQuestionRaised& q) {
        asked = true;
        CHECK(q.question.humans == std::vector<EmpireId>{ar.a, ar.b, ar.c});
    }
    CHECK(asked);
}

// ---- The combat simulator ------------------------------------------------------------------------------------------

TEST_CASE("combat rules: the simulator's sides copy the empire of their first item and start by side number") {
    // Spec 04 §17, Q54.
    const Rules& r = combatRules();
    Arena ar = makeArena(19);
    GameState& s = ar.s;
    const DesignId lancer = frigate(s, ar.a, "Lancer", 3, {"Test Laser", "Test Armor Plate"});
    const DesignId raider = frigate(s, ar.b, "Raider", 3, {"Test Laser", "Test Armor Plate"});
    seeDesign(s.empire(ar.a).knowledge, raider, s.turn);
    s.empire(ar.b).race.name = "Other Race";
    combat::SimulatorSetup setup;
    setup.viewer = ar.a;
    for (int k = 0; k < 10; ++k) setup.sides.push_back({std::format("Side {}", k + 1), true});
    setup.items.push_back({combat::SimulatorItem::Kind::Design, lancer, {}, 0, 1});
    setup.items.push_back({combat::SimulatorItem::Kind::Design, raider, {}, 1, 1});
    REQUIRE(combat::simulatorProblem(r, s, setup).empty());
    combat::SimulatorSetup eleven = setup;
    eleven.sides.push_back({"Side 11", true});
    CHECK(combat::simulatorProblem(r, s, eleven) == "At most 10 sides.");
    combat::Simulation sim = combat::buildSimulation(r, s, setup);
    REQUIRE(sim.sides.size() == 10);
    CHECK(sim.state.empire(sim.sides[0]).race.name == s.empire(ar.a).race.name);
    CHECK(sim.state.empire(sim.sides[1]).race.name == "Other Race");   // a copy of the raider's owner
    // Side 1 arrives from the north, side 2 from the south.
    for (const Vehicle& v : sim.state.vehicles) {
        if (v.location != sim.where) continue;
        const auto dir = combat::detail::arrivalDirection(sim.state, v);
        if (v.owner == sim.sides[0]) CHECK(dir == std::pair{0, -1});
        if (v.owner == sim.sides[1]) CHECK(dir == std::pair{0, 1});
    }
    combat::TacticalBattle b = combat::startSimulation(r, std::move(sim));
    REQUIRE(b.started());
    const CombatRecord& rec = b.record();
    for (const CombatPiece& p : rec.pieces) {
        if (p.kind != CombatPiece::Kind::Vehicle) continue;
        if (p.owner == b.participants().front()) CHECK(p.startY <= 6);   // the top edge
    }
}

TEST_CASE("combat rules: in the simulator only side 1 gets hand control back when Auto is released") {
    const Rules& r = combatRules();
    Arena ar = makeArena(19);
    GameState& s = ar.s;
    const DesignId lancer = frigate(s, ar.a, "Lancer", 3, {"Test Laser", "Test Armor Plate", "Test Armor Plate"});
    combat::SimulatorSetup setup;
    setup.viewer = ar.a;
    setup.sides = {{"Blue", false}, {"Red", false}};
    setup.items.push_back({combat::SimulatorItem::Kind::Design, lancer, {}, 0, 1});
    setup.items.push_back({combat::SimulatorItem::Kind::Design, lancer, {}, 1, 1});
    combat::Simulation sim = combat::buildSimulation(r, s, setup);
    const std::vector<EmpireId> sides = sim.sides;
    combat::TacticalBattle b = combat::startSimulation(r, std::move(sim));
    REQUIRE(b.awaitingOrders());
    CHECK(b.isPlayer(sides[0]));
    CHECK(b.isPlayer(sides[1]));
    TacticalOrder on{OK::Auto, b.phaseEmpire()};
    on.on = true;
    REQUIRE(b.submit(on).empty());
    TacticalOrder off{OK::Auto, b.phaseEmpire()};
    off.on = false;
    REQUIRE(b.submit(off).empty());
    CHECK(b.isPlayer(sides[0]));
    CHECK_FALSE(b.isPlayer(sides[1]));
}
