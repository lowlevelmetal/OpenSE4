// Combat (docs/spec/04): to-hit, damage at range, shields/armor/damage types,
// the whole-component damage model, seekers and point defense, units, mines,
// planets, boarding, conversion, strategies, experience, mood events, the
// map and start boxes, the replay record, ground combat and determinism. All
// content is invented for the tests.

#include "combat_fixture.hpp"
#include "engine_fixture.hpp"

#include "datafile/datafile.hpp"

#include "game/combat.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <set>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
namespace combat = opense4::game::combat;

using namespace opense4::ctest;
using ruleset::VehicleType;
using ruleset::WeaponKind;

// ---- Identifiers, strategies, settings --------------------------------------------------------------

TEST_CASE("combat: damage types and weapon targets parse") {
    for (size_t i = 0; i < static_cast<size_t>(combat::DamageType::Count); ++i) {
        const auto t = static_cast<combat::DamageType>(i);
        CHECK(combat::parseDamageType(combat::identifier(t)) == t);
    }
    CHECK(combat::parseDamageType("skips  normal shields") == combat::DamageType::SkipsNormalShields);
    CHECK(combat::parseDamageType("Something From A Mod") == combat::DamageType::Normal);
    CHECK(combat::isPlanetOnlyDamage(combat::DamageType::PlagueLevel3));
    CHECK(combat::isSpecialEffect(combat::DamageType::CrewConversion));
    CHECK_FALSE(combat::isSpecialEffect(combat::DamageType::SkipsArmor));
    // The types that use and feed the damage pool (spec 04 §9.1).
    CHECK(combat::isHullDamaging(combat::DamageType::Normal));
    CHECK(combat::isHullDamaging(combat::DamageType::QuarterDamageToShields));
    CHECK(combat::isHullDamaging(combat::DamageType::SkipsShieldsAndArmor));
    CHECK_FALSE(combat::isHullDamaging(combat::DamageType::OnlyEngines));
    CHECK_FALSE(combat::isHullDamaging(combat::DamageType::ShieldsOnly));
    CHECK_FALSE(combat::isHullDamaging(combat::DamageType::PushesTarget));

    const std::vector<std::string> classic{"Ships", "Planets", "Ftr", "Sat", "Drone"};
    CHECK(combat::parseWeaponTargets(classic) ==
          (combat::kTargetShips | combat::kTargetPlanets | combat::kTargetFighters | combat::kTargetSatellites | combat::kTargetDrones));
    const std::vector<std::string> overrideList{"Seekers, Fighters"};
    CHECK(combat::parseWeaponTargets(overrideList) == (combat::kTargetSeekers | combat::kTargetFighters));
    const std::vector<std::string> all{"All"};
    CHECK(combat::parseWeaponTargets(all) == 63);
    const std::vector<std::string> prose{"Only ships and planets"};
    CHECK(combat::parseWeaponTargets(prose) == (combat::kTargetShips | combat::kTargetPlanets));
    CHECK(combat::targetMaskOf(VehicleType::Base) == combat::kTargetShips);
    CHECK(combat::targetMaskOf(VehicleType::Mine) == 0);
}

TEST_CASE("combat: strategy records become typed settings") {
    ruleset::CombatStrategy raw{"Skirmisher",
                                {{"Primary Movement Strategy", "Maximum Weapons Range"},
                                 {"Secondary Movement Strategy", "Don't Get Hurt"},
                                 {"Targeting Priority 1", "Weakest"},
                                 {"Targeting Priority 2", "Does Not Have Weapons"},
                                 {"Use Type Priority First", "True"},
                                 {"Type Priority Seekers(On Us)", "2"},
                                 {"Type Priority Bases(No Weapons)", "9"},
                                 {"Dont Fire On Planets", "True"},
                                 {"Break Formation Fighters", "TRUE"},
                                 {"Fighters Launch Group Amount", "6"},
                                 {"Drones Per Target", "4"},
                                 {"Damage Percent Per Ship", "55"},
                                 {"Damage Until All Weapons Gone", "true"}}};
    const combat::Strategy st = combat::parseStrategy(raw);
    CHECK(st.name == "Skirmisher");
    CHECK(st.primary == combat::MoveStrategy::MaximumRange);
    CHECK(st.secondary == combat::MoveStrategy::DontGetHurt);
    CHECK(st.targeting[0] == combat::TargetKey::Weakest);
    CHECK(st.targeting[1] == combat::TargetKey::NoWeapons);
    CHECK(st.typePriorityFirst);
    CHECK(st.typePriority[static_cast<size_t>(combat::TargetCategory::SeekersOnUs)] == 2);
    CHECK(st.typePriority[static_cast<size_t>(combat::TargetCategory::BasesNoWeapons)] == 9);
    CHECK(st.dontFireOn[static_cast<size_t>(combat::TargetCategory::Planets)]);
    CHECK_FALSE(st.dontFireOn[static_cast<size_t>(combat::TargetCategory::Ships)]);
    CHECK(st.breakFormation[static_cast<size_t>(combat::TargetCategory::Fighters)]);
    CHECK(st.fighterLaunchGroup == 6);
    CHECK(st.dronesPerTarget == 4);
    CHECK(st.damagePercentShip == 55);
    CHECK(st.damageUntilWeaponsGone);
    CHECK(combat::Strategy{}.dronesPerTarget == 3);   // the default (spec 04 §10.7)

    CHECK(combat::parseStrategy({"Troops", {{"Primary Movement Strategy", "Drop Troops (if carrying)"}}}).primary ==
          combat::MoveStrategy::DropTroops);
    CHECK(combat::parseStrategy({"Boarders", {{"Primary Movement Strategy", "Board Enemy Ships"}}}).primary ==
          combat::MoveStrategy::BoardEnemyShips);
    CHECK(combat::parseStrategy({"Kamikaze", {{"Primary Movement Strategy", "Ram"}}}).primary == combat::MoveStrategy::Ram);

    // The fixture's own record, and the defaults for everything it leaves out.
    const auto& fixture = engineRules().data().combatStrategies;
    REQUIRE_FALSE(fixture.empty());
    const combat::Strategy cautious = combat::parseStrategy(fixture.front());
    CHECK(cautious.primary == combat::MoveStrategy::OptimalRange);
    CHECK(cautious.fighterLaunchGroup == 5);
    CHECK(cautious.typePriority[static_cast<size_t>(combat::TargetCategory::Ships)] == 1);
}

TEST_CASE("combat: settings come from Settings.txt with stock defaults") {
    const combat::CombatSettings cs = combat::loadSettings(combatRules());
    CHECK(cs.spaceTurns == 30);
    CHECK(cs.baseToHit == 100);
    CHECK(cs.toHitPerSquare == 10);
    CHECK(cs.planetDefense == -200);
    CHECK(cs.damagePerPopulation == 10);
    // Militia: one per 20M, truncated, no minimum (spec 04 §13).
    CHECK(combat::militiaCount(cs, int64_t{0}) == 0);
    CHECK(combat::militiaCount(cs, int64_t{7}) == 0);
    CHECK(combat::militiaCount(cs, int64_t{45}) == 2);
    const std::vector<PopulationGroup> groups{{EmpireId{0u}, 39}, {EmpireId{1u}, 39}};
    CHECK(combat::militiaCount(cs, groups) == 2);   // per group: 1 + 1, not 78 / 20 = 3
}

// ---- To-hit and damage at range ------------------------------------------------------------------------

TEST_CASE("combat: chance to hit") {
    const combat::CombatSettings cs = combat::loadSettings(combatRules());
    // The spec's worked example: 100 - 50 + 25 - 40 = 35 %.
    CHECK(combat::detail::toHitChance(cs, 5, 25, 40, 0) == 35);
    CHECK(combat::detail::toHitChance(cs, 1, 200, 0, 0) == 99);
    CHECK(combat::detail::toHitChance(cs, 15, 0, 0, 0) == 1);
    CHECK(combat::detail::toHitChance(cs, 2, 0, 0, 30) == 50);

    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    const VehicleId shooter = spawn(s, frigate(s, ar.a, "Spotter", 1, {"Test Combat Sensor", "Test Laser", "CT Always Hit"}), ar.loc);
    const VehicleId dodger = spawn(s, frigate(s, ar.b, "Dodger", 1, {"Test ECM"}), ar.loc);
    const size_t laser = 5;
    // Sensors +10, ECM +10.
    CHECK(combat::toHitPercent(r, s, *s.vehicle(dodger), 0, *s.vehicle(shooter), 1) == 0);   // not a weapon
    s.vehicle(shooter)->damage[6] = 1000;   // the talisman is destroyed: roll normally
    CHECK(combat::toHitPercent(r, s, *s.vehicle(shooter), laser, *s.vehicle(dodger), 3) == 70);
    CHECK(combat::toHitPercent(r, s, *s.vehicle(shooter), laser, *s.vehicle(dodger), 9) == 10);
    CHECK(combat::toHitPercent(r, s, *s.vehicle(shooter), laser, *s.vehicle(dodger), 12) == 1);
    // Racial aggressiveness and crew experience count as points.
    s.empire(ar.a).race.characteristics[static_cast<size_t>(Characteristic::Aggressiveness)] = 120;
    s.vehicle(dodger)->experience = 15;
    CHECK(combat::toHitPercent(r, s, *s.vehicle(shooter), laser, *s.vehicle(dodger), 5) == 100 - 50 + 10 + 20 - 10 - 15);
    // A mount's to-hit bonus, then the clamp.
    Design mounted = s.design(s.vehicle(shooter)->design);
    mounted.name = "Mounted";
    mounted.entries[laser].mount = mountIndex(r, "CT Long Mount");
    s.vehicle(shooter)->design = addDesign(s, mounted);
    CHECK(combat::toHitPercent(r, s, *s.vehicle(shooter), laser, *s.vehicle(dodger), 1) == 99);
    // Weapons Always Hit, direct fire and point-defense alike (confirmed: binary).
    s.vehicle(shooter)->damage[6] = 0;
    CHECK(combat::toHitPercent(r, s, *s.vehicle(shooter), laser, *s.vehicle(dodger), 9) == 100);
    const VehicleId picket = spawn(s, frigate(s, ar.a, "Picket", 1, {"CT PD", "CT Always Hit"}), ar.loc);
    CHECK(combat::toHitPercent(r, s, *s.vehicle(picket), 4, *s.vehicle(dodger), 9) == 100);
}

TEST_CASE("combat: to-hit parts add up by family, the system bonus is offense only") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    const VehicleId gunner = spawn(s, frigate(s, ar.a, "Gunner", 1, {"Test Laser"}), ar.loc);
    // Two parts of one family: only the best counts; another family adds (spec 04 §7).
    const VehicleId one = spawn(s, frigate(s, ar.b, "One", 1, {"CT ECM A", "CT ECM B"}), ar.loc);
    const VehicleId two = spawn(s, frigate(s, ar.b, "Two", 1, {"CT ECM A", "CT Stealth Armor"}), ar.loc);
    const size_t laser = 4;
    CHECK(vehicleToHitDefense(r, s, *s.vehicle(one)) == 20);
    CHECK(vehicleToHitDefense(r, s, *s.vehicle(two)) == 30);
    CHECK(combat::toHitPercent(r, s, *s.vehicle(gunner), laser, *s.vehicle(one), 2) == 100 - 20 - 20);
    CHECK(combat::toHitPercent(r, s, *s.vehicle(gunner), laser, *s.vehicle(two), 2) == 100 - 20 - 30);
    // The family's best intact part counts: with ECM A destroyed, ECM B remains.
    s.vehicle(one)->damage[4] = 1000;
    CHECK(vehicleToHitDefense(r, s, *s.vehicle(one)) == 15);
    // Mothballed ships have no offense or defense.
    s.vehicle(two)->status = VehicleStatus::Mothballed;
    CHECK(combat::detail::vehicleDefense(r, s, *s.vehicle(two), 0, 0) == 0);
}

TEST_CASE("combat: system modifiers add up over colonies and vehicles") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    const SystemId sys = ar.loc.system;
    CHECK(combat::detail::systemModifier(r, s, ar.a, sys, AbilityKind::CombatModifierSystem) == 0);
    // Each colony adds its best facility; each vehicle its best component (confirmed: binary).
    Colony& home = homeworld(s, ar.a);
    home.facilities.push_back(facilityIndex(r, "CT Combat Center 10"));
    home.facilities.push_back(facilityIndex(r, "CT Combat Center 4"));
    CHECK(combat::detail::systemModifier(r, s, ar.a, sys, AbilityKind::CombatModifierSystem) == 10);
    spawn(s, frigate(s, ar.a, "Flagship", 1, {"CT Battle Computer", "CT Battle Computer"}), ar.loc);
    spawn(s, frigate(s, ar.a, "Escort", 1, {"CT Battle Computer"}), ar.loc);
    CHECK(combat::detail::systemModifier(r, s, ar.a, sys, AbilityKind::CombatModifierSystem) == 10 + 7 + 7);
    CHECK(combat::detail::systemModifier(r, s, ar.b, sys, AbilityKind::CombatModifierSystem) == 0);
    // The system bonus helps offense only (spec 04 §7).
    const VehicleId gunner = spawn(s, frigate(s, ar.a, "Gunner", 1, {"Test Laser"}), ar.loc);
    const VehicleId target = spawn(s, frigate(s, ar.b, "Target", 1, {}), ar.loc);
    CHECK(combat::toHitPercent(r, s, *s.vehicle(gunner), 4, *s.vehicle(target), 5) == 50 + 24);
    CHECK(combat::toHitPercent(r, s, *s.vehicle(target), 4, *s.vehicle(gunner), 5) == 0);   // not a weapon entry
}

TEST_CASE("combat: damage at range with and without a mount") {
    const Rules& r = combatRules();
    const int32_t longMount = mountIndex(r, "CT Long Mount");
    const DesignEntry plain{componentIndex(r, "CT Gun"), -1};
    const DesignEntry mounted{componentIndex(r, "CT Gun"), longMount};
    CHECK(combat::weaponDamage(r, plain, 1) == 10);
    CHECK(combat::weaponDamage(r, plain, 0) == 0);   // outside 1..20 without a mount
    CHECK(combat::weaponDamage(r, plain, 6) == 0);
    CHECK(combat::weaponReach(r, plain) == 5);
    // Index clamp(r - 2, 1, 20): ranges 1..3 read entry 1; the table ends two squares later; damage x1.5.
    CHECK(combat::weaponDamage(r, mounted, 1) == 15);
    CHECK(combat::weaponDamage(r, mounted, 3) == 15);
    CHECK(combat::weaponDamage(r, mounted, 7) == 15);
    CHECK(combat::weaponDamage(r, mounted, 8) == 0);
    CHECK(combat::weaponReach(r, mounted) == 7);
    // With a mount, ranges past the table read entry 20 (confirmed: binary).
    const DesignEntry twenty{componentIndex(r, "CT Twenty Gun"), longMount};
    CHECK(combat::weaponDamage(r, {componentIndex(r, "CT Twenty Gun"), -1}, 21) == 0);
    CHECK(combat::weaponDamage(r, twenty, 40) == 8);   // round(5 x 1.5) = 8 (7.5 rounds to even)
    CHECK(combat::weaponReach(r, twenty) == combat::kCombatMapWidth);
    // The mount's percentage is rounded, ties to even.
    const DesignEntry odd{componentIndex(r, "CT Odd Gun"), longMount};
    CHECK(combat::weaponDamage(r, odd, 3) == 22);   // 15 x 1.5 = 22.5
    CHECK(combat::weaponDamage(r, odd, 4) == 38);   // 25 x 1.5 = 37.5
    // Structure and supply scale too, rounded.
    CHECK(combat::detail::combatStructure(r, odd) == 15);   // 10 x 1.5
    CHECK(combat::detail::supplyPerShot(r, {componentIndex(r, "Test Laser"), -1}) == 5);
    CHECK(combat::detail::supplyPerShot(r, {componentIndex(r, "Test Laser"), longMount}) == 8);   // 7.5 -> 8
    // A direct-fire mount does nothing for a seeking weapon.
    const DesignEntry torpedo{componentIndex(r, "CT Torpedo"), longMount};
    CHECK_FALSE(combat::detail::mountApplies(r, torpedo));
    CHECK(combat::weaponDamage(r, torpedo, 1) == 37);
    CHECK(combat::weaponDamage(r, torpedo, 11) == 0);
    // Warheads and troop weapons count with their largest entry.
    CHECK(combat::weaponLargestDamage(r, {componentIndex(r, "Test Warhead"), -1}) == 60);
}

// ---- The damage pipeline --------------------------------------------------------------------------------

TEST_CASE("combat: one shield pool, whole components, armor first, the damage pool") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    // Entries: 0 bridge, 1 life support, 2 crew, 3 engine, 4 shield(20), 5 phased shield(20), 6-7 armor(40), 8 laser(15), 9 fuel.
    const DesignId d = frigate(s, ar.a, "Target", 1, {"Test Shield", "CT Phased Shield", "Test Armor Plate", "Test Armor Plate", "Test Laser"});
    const VehicleId id = spawn(s, d, ar.loc);
    const Vehicle base = *s.vehicle(id);
    Rng rng(99);
    using DT = combat::DamageType;
    using Kind = combat::detail::ShieldState::Kind;

    struct Result {
        combat::detail::HitResult out;
        combat::detail::ShieldState sh;
        int64_t pool = 0;
        Vehicle v;
    };
    auto hit = [&](int64_t damage, DT type, int64_t pool = 0) {
        Result res{{}, {}, pool, base};
        combat::detail::refreshShields(r, s, res.v, 0, 0, res.sh, true);
        res.out = combat::detail::hitVehicle(r, s, res.v, res.sh, res.pool, damage, type, rng);
        return res;
    };
    auto armorLost = [&](const Vehicle& v) { return int(destroyed(r, s, v, 6)) + int(destroyed(r, s, v, 7)); };
    auto internalsLost = [&](const Vehicle& v) {
        int n = 0;
        for (size_t e : {0, 1, 2, 3, 4, 5, 8, 9}) n += destroyed(r, s, v, e);
        return n;
    };

    {
        const auto res = hit(0, DT::Normal);
        CHECK(res.sh.max == 40);   // normal and phased generators fill one pool
        CHECK(res.sh.kind == Kind::Normal);
    }
    {
        const auto res = hit(15, DT::Normal);
        CHECK(res.sh.current == 25);
        CHECK(res.pool == 0);
    }
    {
        const auto res = hit(70, DT::Normal);   // 30 gets through: too little for a 40-point plate, so it waits in the pool
        CHECK(res.sh.current == 0);
        CHECK(res.out.reached == 30);
        CHECK(armorLost(res.v) == 0);
        CHECK(res.pool == 30);
    }
    {
        const auto res = hit(100, DT::Normal);   // 60: one plate falls whole, 20 left over
        CHECK(armorLost(res.v) == 1);
        CHECK(internalsLost(res.v) == 0);
        CHECK(res.pool == 20);
    }
    {
        const auto res = hit(30, DT::SkipsAllShields, 30);   // the pool joins the next hit
        CHECK(res.sh.current == 40);
        CHECK(armorLost(res.v) == 1);
        CHECK(res.pool == 20);
    }
    {
        const auto res = hit(15, DT::SkipsNormalShields);   // the pool is normal, so it is skipped
        CHECK(res.sh.current == 40);
        CHECK(res.pool == 15);
    }
    {
        const auto res = hit(100, DT::ShieldsOnly);
        CHECK(res.sh.current == 0);
        CHECK(res.out.reached == 0);
        CHECK(res.pool == 0);
    }
    {
        const auto res = hit(15, DT::QuadDamageToShields);   // 60 against 40 shields: 20 left, back to 5
        CHECK(res.sh.current == 0);
        CHECK(res.pool == 5);
    }
    {
        const auto res = hit(10, DT::HalfDamageToShields);
        CHECK(res.sh.current == 35);
        CHECK(res.out.reached == 0);
    }
    {
        const auto res = hit(30, DT::SkipsShieldsAndArmor);   // internals only; the pool keeps what did not fit
        CHECK(armorLost(res.v) == 0);
        CHECK(internalsLost(res.v) >= 1);
        int lost = 0;
        for (size_t e : {0, 1, 2, 3, 4, 5, 8, 9})
            if (destroyed(r, s, res.v, e)) lost += combat::detail::combatStructure(r, s.design(d).entries[e]);
        CHECK(lost + res.pool == 30);
    }
    {
        const auto res = hit(25, DT::OnlyEngines);   // shields still absorb (history 1.70)
        CHECK_FALSE(destroyed(r, s, res.v, 3));
        const auto big = hit(50, DT::OnlyEngines);
        CHECK(destroyed(r, s, big.v, 3));
        CHECK(armorLost(big.v) + internalsLost(big.v) == 1);
        CHECK(big.pool == 0);   // the "Only" types lose their leftover
    }
    {
        const auto res = hit(100, DT::OnlyWeapons);   // ignores shields and armor, harmless beyond the weapons
        CHECK(res.sh.current == 40);
        CHECK(destroyed(r, s, res.v, 8));
        CHECK(internalsLost(res.v) == 1);
        CHECK_FALSE(res.out.destroyed);
        CHECK_FALSE(combat::detail::canAffectVehicle(r, s, res.v, res.sh, DT::OnlyWeapons));
    }
    {
        auto res = hit(100, DT::OnlyShieldGenerators);   // losing generators caps the shields
        combat::detail::refreshShields(r, s, res.v, 0, 0, res.sh, false);
        CHECK(res.sh.max == 0);
        CHECK(res.sh.current == 0);
    }
    {
        const int total = combat::detail::designStructure(r, s.design(d));
        CHECK(total == 195);
        const auto res = hit(total + 15, DT::SkipsAllShields);
        CHECK(res.out.destroyed);
        CHECK(vehicleDestroyed(r, s, res.v));
        CHECK(res.pool == 15);
    }
    // Armor always goes first, whatever the draw (confirmed: binary).
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Rng local(seed);
        Vehicle v = base;
        const int64_t left = combat::detail::destroyComponents(r, s, v, 110, DT::Normal, local);
        CHECK(armorLost(v) == 2);
        CHECK(left <= 30);
    }
    CHECK_FALSE(combat::detail::canAffectVehicle(r, s, base, {}, DT::OnlyPlanetPopulation));
    CHECK_FALSE(combat::detail::canAffectVehicle(r, s, base, {}, DT::ShieldsOnly));
}

TEST_CASE("combat: shield kinds, the system bonus and disruption") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    using Kind = combat::detail::ShieldState::Kind;
    const Vehicle phased = *s.vehicle(spawn(s, frigate(s, ar.a, "Ghost", 1, {"CT Phased Shield", "CT Phased Shield"}), ar.loc));
    combat::detail::ShieldState sh;
    combat::detail::refreshShields(r, s, phased, 0, 0, sh, true);
    CHECK(sh.kind == Kind::Phased);   // phased only when every generator is
    CHECK(sh.max == 40);
    int64_t pool = 0;
    Rng rng(3);
    Vehicle v = phased;
    combat::detail::hitVehicle(r, s, v, sh, pool, 15, combat::DamageType::SkipsNormalShields, rng);
    CHECK(sh.current == 25);   // a phased pool stops shield-skipping damage
    // The system modifier adds only when positive and only to a ship that has shields; disruption subtracts.
    combat::detail::refreshShields(r, s, phased, 25, 10, sh, true);
    CHECK(sh.max == 55);
    combat::detail::refreshShields(r, s, phased, -25, 0, sh, true);
    CHECK(sh.max == 40);
    combat::detail::refreshShields(r, s, phased, 0, 100, sh, true);
    CHECK(sh.max == 0);
    const Vehicle bare = *s.vehicle(spawn(s, frigate(s, ar.a, "Bare", 1, {}), ar.loc));
    combat::detail::refreshShields(r, s, bare, 25, 0, sh, true);
    CHECK(sh.max == 0);
    // No supplies: no shields.
    Vehicle dry = phased;
    dry.supply = 0;
    combat::detail::refreshShields(r, s, dry, 0, 0, sh, true);
    CHECK(sh.max == 0);
}

TEST_CASE("combat: emissive, crystalline and organic armor") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    Rng rng(5);
    {
        const VehicleId id = spawn(s, design(s, ar.a, "Glow", "Test Frigate", {"Test Bridge", "CT Emissive Armor"}), ar.loc);
        Vehicle v = *s.vehicle(id);
        combat::detail::ShieldState sh;
        int64_t pool = 10;
        CHECK(combat::detail::hitVehicle(r, s, v, sh, pool, 15, combat::DamageType::Normal, rng).reached == 0);
        CHECK(pool == 10);   // a hit no larger than E does nothing, and the pool keeps its value
        CHECK(combat::detail::hitVehicle(r, s, v, sh, pool, 16, combat::DamageType::Normal, rng).reached == 11);   // 16 + 10 - 15
        CHECK(pool == 11);
        // Skips Armor is not reduced by emissive armor: 20 destroys the 20-point bridge.
        int64_t other = 0;
        combat::detail::hitVehicle(r, s, v, sh, other, 20, combat::DamageType::SkipsArmor, rng);
        CHECK(destroyed(r, s, v, 0));
    }
    {
        const VehicleId id = spawn(s, design(s, ar.a, "Prism", "Test Frigate", {"Test Bridge", "Test Shield", "CT Crystal Armor"}), ar.loc);
        Vehicle v = *s.vehicle(id);
        v.supply = 10;
        combat::detail::ShieldState sh;
        combat::detail::refreshShields(r, s, v, 0, 0, sh, true);
        int64_t pool = 0;
        combat::detail::hitVehicle(r, s, v, sh, pool, 20, combat::DamageType::Normal, rng);
        CHECK(sh.current == 0);
        const auto out = combat::detail::hitVehicle(r, s, v, sh, pool, 12, combat::DamageType::Normal, rng);
        CHECK(sh.current == 5);   // 5 points of what got through became shields
        CHECK(out.reached == 12);   // and the damage itself is not reduced
    }
    {
        const VehicleId id = spawn(s, design(s, ar.a, "Moss", "Test Frigate", {"Test Bridge", "CT Organic Armor", "CT Organic Armor"}), ar.loc);
        Vehicle v = *s.vehicle(id);
        v.damage[1] = 40;
        v.damage[2] = 40;
        CHECK(combat::detail::hasDestroyedRegeneratingArmor(r, s, v));
        // Whole components in design order, each costing its structure.
        CHECK(combat::detail::restoreRegeneratingArmor(r, s, v, 50) == 40);
        CHECK(v.damage[1] == 0);
        CHECK(v.damage[2] == 40);
        CHECK(combat::detail::restoreRegeneratingArmor(r, s, v, 10000) == 40);
        CHECK_FALSE(combat::detail::hasDestroyedRegeneratingArmor(r, s, v));
    }
}

TEST_CASE("combat: unit hit points and experience") {
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    // Fighters, troops and platforms count shields twice, once less when the type skips them (spec 04 §9.4).
    const DesignId fighter = design(s, ar.a, "Shielded Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Shield"});
    CHECK(combat::detail::unitHitPoints(r, s.design(fighter), combat::DamageType::Normal) == 5 + 10 + 2 * 20);
    CHECK(combat::detail::unitHitPoints(r, s.design(fighter), combat::DamageType::SkipsAllShields) == 5 + 10 + 20);
    CHECK(combat::detail::unitHitPoints(r, s.design(fighter), combat::DamageType::Normal, false) == 15);
    const DesignId sat = design(s, ar.a, "Shielded Sat", "Test Satellite Hull", {"Test Satellite Gun", "Test Shield"});
    CHECK(combat::detail::unitHitPoints(r, s.design(sat), combat::DamageType::Normal) == 10 + 10 + 20);
    // Experience in tenths, capped at 50; a gain past 50 sets it to 50.
    int whole = 49, tenths = 5;
    combat::detail::addExperience(whole, tenths, 10);
    CHECK(whole == 50);
    CHECK(tenths == 0);
    whole = 3;
    tenths = 9;
    combat::detail::addExperience(whole, tenths, 1);
    CHECK(whole == 4);
    CHECK(tenths == 0);
}

// ---- Battles ------------------------------------------------------------------------------------------------

TEST_CASE("combat: combatPossible respects treaties, cloaking and mines") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    const VehicleId x = spawn(s, frigate(s, ar.a, "X", 1, {"CT Cloak"}), ar.loc);
    CHECK_FALSE(combat::combatPossible(r, s, ar.loc));   // nobody to fight
    const VehicleId y = spawn(s, frigate(s, ar.b, "Y", 1, {}), ar.loc);
    CHECK(combat::combatPossible(r, s, ar.loc));   // unarmed ships still fight (spec 04 §2)
    setTreaty(s, ar.a, ar.b, Treaty::NonAggression);
    CHECK_FALSE(combat::combatPossible(r, s, ar.loc));
    s.empire(ar.a).relation(ar.b).treaty = Treaty::War;   // one side hostile is enough
    CHECK(combat::combatPossible(r, s, ar.loc));
    // An undetected cloaked ship does not start combat.
    s.empire(ar.b).knowledge.present.assign(s.galaxy.systems.size(), 0);
    s.vehicle(x)->status = VehicleStatus::Cloaked;
    CHECK_FALSE(combat::combatPossible(r, s, ar.loc));
    s.vehicle(x)->status = VehicleStatus::Normal;
    CHECK(combat::combatPossible(r, s, ar.loc));
    // Mines alone start an encounter with a hostile vehicle.
    s.vehicle(y)->count = 0;
    s.removeDeadVehicles();
    CHECK_FALSE(combat::combatPossible(r, s, ar.loc));
    const DesignId mine = design(s, ar.b, "Mine", "Test Mine Hull", {"Test Warhead"});
    spawn(s, mine, ar.loc, 3);
    CHECK(combat::combatPossible(r, s, ar.loc));
    setTreaty(s, ar.a, ar.b, Treaty::NonAggression);
    CHECK_FALSE(combat::combatPossible(r, s, ar.loc));
}

TEST_CASE("combat: a won battle - damage, kills, experience, mood, logs and the record") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId hunterDesign = frigate(s, ar.a, "Hunter", 3, {"CT Big Gun", "CT Big Armor"});
    const DesignId preyDesign = frigate(s, ar.b, "Prey", 1, {});
    const VehicleId hunter = spawn(s, hunterDesign, ar.loc);
    const VehicleId prey = spawn(s, preyDesign, ar.loc);
    Fleet f;
    f.owner = ar.a;
    f.name = "Hunters";
    f.members = {hunter};
    f.leader = hunter;
    const FleetId fid = s.addFleet(f).id;
    s.vehicle(hunter)->fleet = fid;
    s.vehicle(hunter)->orders.push_back(Order{OrderKind::MoveTo, ar.loc});

    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);

    CHECK(s.vehicle(prey)->count == 0);
    CHECK(s.vehicle(hunter)->count == 1);
    CHECK(s.vehicle(hunter)->orders.size() == 1);   // combat does not clear orders (spec 03 §6.3)
    // Experience only for kills: +1.0 for the ship; the fleet may gain 0.1 (spec 04 §15).
    CHECK(s.vehicle(hunter)->experience == 1);
    CHECK(s.vehicle(hunter)->experienceTenths == 0);
    CHECK(s.fleet(fid)->experience == 0);
    CHECK(s.fleet(fid)->experienceTenths <= 1);
    CHECK(s.design(hunterDesign).kills == 1);
    CHECK(s.design(preyDesign).lost == 1);
    // Enemy tonnage destroyed: the prey's hull tonnage (spec 04 §15; inferred measure).
    CHECK(s.design(hunterDesign).enemyTonnageDestroyed == combatRules().hull(s.design(preyDesign).hull).tonnage);
    CHECK(s.design(hunterDesign).enemyTonnageDestroyed > 0);
    CHECK(s.design(preyDesign).enemyTonnageDestroyed == 0);

    CHECK(moodCount(ctx, ar.a, "Battle in System - Win") == 1);
    CHECK(moodCount(ctx, ar.b, "Battle in System - Loss") == 1);
    CHECK(moodCount(ctx, ar.b, "Any Ship Lost") == 1);
    CHECK(moodCount(ctx, ar.b, "Ship Lost in System") == 1);
    CHECK(moodCount(ctx, ar.a, "Any Ship Lost") == 0);
    REQUIRE(ctx.battleSites.size() == 1);
    CHECK(ctx.battleSites.front() == ar.loc);
    for (EmpireId e : {ar.a, ar.b}) {
        const auto& log = s.empire(e).log;
        CHECK(std::any_of(log.begin(), log.end(),
                          [](const LogEntry& l) { return l.category == LogCategory::Combat && l.title.starts_with("Battle at"); }));
    }
    CHECK(designSeenTurn(s.empire(ar.b).knowledge, hunterDesign) == s.turn);  // seen in this battle

    // The replay record.
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    CHECK(rec.location == ar.loc);
    CHECK(rec.participants == std::vector<EmpireId>{ar.a, ar.b});
    REQUIRE(rec.pieces.size() == 2);
    CHECK(rec.pieces[0].vehicle == hunter);
    CHECK(rec.pieces[1].vehicle == prey);
    CHECK(rec.pieces[0].owner == ar.a);
    CHECK(rec.pieces[0].name == s.vehicle(hunter)->name);
    CHECK(rec.pieces[0].startX != rec.pieces[1].startX);
    CHECK(countEvents(rec, CombatEvent::Kind::Fire) > 0);
    CHECK(countEvents(rec, CombatEvent::Kind::Hit) > 0);
    CHECK(countEvents(rec, CombatEvent::Kind::Destroyed) == 1);
    for (const CombatEvent& e : rec.events) {
        CHECK(e.piece < rec.pieces.size());
        CHECK(e.target < rec.pieces.size());
        CHECK(e.round >= 1);
        CHECK(e.round <= 29);
        CHECK(e.x >= 0);
        CHECK(e.x < combat::kCombatMapWidth);
        CHECK(e.y >= 0);
        CHECK(e.y < combat::kCombatMapHeight);
    }
    CHECK(rec.summary.size() >= 2);

    s.removeDeadVehicles();
    CHECK(s.vehicle(prey) == nullptr);
}

TEST_CASE("combat: unarmed sides end in a stalemate after one turn fewer than the setting") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Point Blank"}});
    useStrategy(s, ar.b, {{"Primary Movement Strategy", "Point Blank"}});
    spawn(s, frigate(s, ar.a, "Scout A", 1, {}), ar.loc);
    spawn(s, frigate(s, ar.b, "Scout B", 1, {}), ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    CHECK(moodCount(ctx, ar.a, "Battle in System - Stalemate") == 1);
    CHECK(moodCount(ctx, ar.b, "Battle in System - Stalemate") == 1);
    REQUIRE(s.combats.size() == 1);
    CHECK(countEvents(s.combats.front(), CombatEvent::Kind::Fire) == 0);
    for (const CombatEvent& e : s.combats.front().events) CHECK(e.round <= 29);
}

TEST_CASE("combat: the battle lasts one turn fewer than Number Of Space Combat Turns") {
    ruleset::Ruleset rs = buildCombatRuleset();
    rs.settings.set("Number Of Space Combat Turns", "4");
    const Rules shortRules{std::move(rs)};
    Arena ar = makeArena(shortRules);
    GameState& s = ar.s;
    // Two gunboats that miss a lot: Long Gun damage never destroys the big armor in three turns.
    const VehicleId a = spawn(s, frigate(s, ar.a, "A", 2, {"CT Long Gun", "CT Big Armor"}, shortRules), ar.loc);
    spawn(s, frigate(s, ar.b, "B", 2, {"CT Long Gun", "CT Big Armor"}, shortRules), ar.loc);
    TurnContext ctx = context(s, shortRules);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.combats.size() == 1);
    int last = 0;
    for (const CombatEvent& e : s.combats.front().events) last = std::max(last, int(e.round));
    CHECK(last == 3);
    CHECK(s.vehicle(a)->count == 1);
}

TEST_CASE("combat: every owned object is a piece, but a bystander is never fired on") {
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    setTreaty(s, ar.c, ar.a, Treaty::MilitaryAlliance);
    setTreaty(s, ar.c, ar.b, Treaty::MilitaryAlliance);
    spawn(s, frigate(s, ar.a, "A", 2, {"CT Gun", "CT Big Armor"}), ar.loc);
    spawn(s, frigate(s, ar.b, "B", 2, {"CT Gun", "CT Big Armor"}), ar.loc);
    const VehicleId c = spawn(s, frigate(s, ar.c, "C", 1, {"CT Gun"}), ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.combats.size() == 1);
    // The bystander is a piece (confirmed: binary), but no one fires on it and it fires on no one.
    CHECK(s.combats.front().participants == std::vector<EmpireId>{ar.a, ar.b, ar.c});
    const int cp = pieceOf(s.combats.front(), c);
    for (const CombatEvent& e : s.combats.front().events) {
        if (e.kind != CombatEvent::Kind::Fire) continue;
        CHECK(static_cast<int>(e.piece) != cp);
        CHECK(static_cast<int>(e.target) != cp);
    }
    CHECK(damageTaken(s, c) == 0);
    CHECK(moodCount(ctx, ar.c, "Battle in System - Win") + moodCount(ctx, ar.c, "Battle in System - Stalemate") == 0);
}

TEST_CASE("combat: fleets start in formation, turned to the leader's facing") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId d = frigate(s, ar.a, "Liner", 1, {"CT Gun"});
    const VehicleId lead = spawn(s, d, ar.loc), left = spawn(s, d, ar.loc), right = spawn(s, d, ar.loc);
    Fleet f;
    f.owner = ar.a;
    f.members = {lead, left, right};
    f.leader = lead;
    f.formation = 0;   // Test Line: slots one square either side of the leader
    const FleetId fid = s.addFleet(f).id;
    for (VehicleId v : {lead, left, right}) s.vehicle(v)->fleet = fid;
    spawn(s, frigate(s, ar.b, "Target", 1, {"CT Big Armor"}), ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.combats.size() == 1);
    const auto& pieces = s.combats.front().pieces;
    auto find = [&](VehicleId id) { return pieces[static_cast<size_t>(pieceOf(s.combats.front(), id))]; };
    const CombatPiece L = find(lead), M1 = find(left), M2 = find(right);
    // Both empires start in the middle: A's box is west of the centre, facing east, so
    // the template's left/right slots become north/south of the leader.
    CHECK(L.startX < 36);
    CHECK(M1.startX == L.startX);
    CHECK(M2.startX == L.startX);
    CHECK(M1.startY == L.startY - 1);
    CHECK(M2.startY == L.startY + 1);
}

TEST_CASE("combat: the map, start boxes and attackers") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    // A was here; B came in from the north-west sector this turn.
    const VehicleId defender = spawn(s, frigate(s, ar.a, "Holder", 2, {"CT Gun", "CT Big Armor"}), ar.loc);
    const VehicleId attacker = spawn(s, frigate(s, ar.b, "Raider", 2, {"CT Gun", "CT Big Armor"}), ar.loc);
    arriveFrom(s, attacker, -1, -1);
    CHECK(combat::detail::arrivedThisTurn(s, *s.vehicle(attacker)));
    CHECK_FALSE(combat::detail::arrivedThisTurn(s, *s.vehicle(defender)));
    CHECK(combat::detail::arrivalDirection(s, *s.vehicle(attacker)) == std::pair{-1, -1});
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    const CombatPiece& d = rec.pieces[static_cast<size_t>(pieceOf(rec, defender))];
    const CombatPiece& a = rec.pieces[static_cast<size_t>(pieceOf(rec, attacker))];
    // Two pieces: 6-square boxes. The attacker starts in the top-left corner, the defender around (36, 31).
    CHECK(a.startX < 6);
    CHECK(a.startY < 6);
    CHECK(d.startX >= 33);
    CHECK(d.startX <= 38);
    CHECK(d.startY >= 28);
    CHECK(d.startY <= 33);
    // Defenders act first: the first event of the battle is the defender's.
    REQUIRE_FALSE(rec.events.empty());
    CHECK(rec.events.front().piece == static_cast<uint32_t>(pieceOf(rec, defender)));
    // A warp arrival (another system) starts in the middle but still attacks.
    Vehicle w = *s.vehicle(attacker);
    w.cameFrom = {SystemId{(ar.loc.system.value + 1) % static_cast<uint32_t>(s.galaxy.systems.size())}, Sector{0, 0}};
    CHECK(combat::detail::arrivedThisTurn(s, w));
    CHECK(combat::detail::arrivalDirection(s, w) == std::pair{0, 0});
    // Last turn's move does not count.
    w.cameFromTurn = s.turn + 1;
    CHECK_FALSE(combat::detail::arrivedThisTurn(s, w));
}

TEST_CASE("combat: planets and neutral obstacles cover 4x4 squares") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    // A sector with a star in B's home system.
    const SystemId sys = s.galaxy.object(homeworld(s, ar.b).planet).system;
    std::optional<ObjectId> star;
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (s.galaxy.object(o).kind == ObjectKind::Star) star = o;
    REQUIRE(star.has_value());
    const Location there = locationOf(s.galaxy, *star);
    spawn(s, frigate(s, ar.a, "A", 2, {"CT Gun", "CT Big Armor"}), there);
    spawn(s, frigate(s, ar.b, "B", 2, {"CT Gun", "CT Big Armor"}), there);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, there);
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    auto obstacle = std::find_if(rec.pieces.begin(), rec.pieces.end(), [&](const CombatPiece& p) { return p.kind == CombatPiece::Kind::Obstacle; });
    REQUIRE(obstacle != rec.pieces.end());
    CHECK(obstacle->planet == *star);
    CHECK_FALSE(obstacle->owner.valid());
    CHECK(rec.participants == std::vector<EmpireId>{ar.a, ar.b});
    // No two pieces overlap at the start; big pieces keep their footprint on the map.
    std::set<std::pair<int, int>> used;
    for (const CombatPiece& p : rec.pieces) {
        const int size = p.kind == CombatPiece::Kind::Obstacle || p.kind == CombatPiece::Kind::Planet ? 4 : 1;
        CHECK(p.startX + size <= combat::kCombatMapWidth);
        CHECK(p.startY + size <= combat::kCombatMapHeight);
        for (int dy = 0; dy < size; ++dy)
            for (int dx = 0; dx < size; ++dx) CHECK(used.insert({p.startX + dx, p.startY + dy}).second);
    }
    // Nobody fires on an obstacle.
    const auto oi = static_cast<uint32_t>(obstacle - rec.pieces.begin());
    for (const CombatEvent& e : rec.events) CHECK(!(e.kind == CombatEvent::Kind::Fire && e.target == oi));
}

TEST_CASE("combat: combat movement is half the speed rounded up plus the best Combat Movement part") {
    auto movesInRoundOne = [](int engines, bool thruster) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        useStrategy(s, ar.a, {{"Primary Movement Strategy", "Point Blank"}});
        const VehicleId mover = thruster ? spawn(s, frigate(s, ar.a, "Mover", engines, {"CT Gun", "CT Combat Thruster", "CT Combat Thruster"}), ar.loc)
                                         : spawn(s, frigate(s, ar.a, "Mover", engines, {"CT Gun"}), ar.loc);
        const VehicleId far = spawn(s, frigate(s, ar.b, "Far", 1, {"CT Big Armor"}), ar.loc);
        arriveFrom(s, far, 1, 1);   // the target starts in the far corner
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const CombatRecord& rec = s.combats.front();
        const int p = pieceOf(rec, mover);
        int moves = 0;
        for (const CombatEvent& e : rec.events)
            if (e.kind == CombatEvent::Kind::Move && e.round == 1 && static_cast<int>(e.piece) == p) ++moves;
        return moves;
    };
    CHECK(movesInRoundOne(5, false) == 3);   // 5 / 2 rounded up
    CHECK(movesInRoundOne(4, false) == 2);
    CHECK(movesInRoundOne(4, true) == 4);    // + 2 from the best thruster; two thrusters do not add
}

TEST_CASE("combat: seekers wait a turn, hit on arrival and point defense shoots them down") {
    // Targets are bases, so the seekers never run out of range chasing them.
    auto fight = [](bool pointDefense) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        spawn(s, frigate(s, ar.a, "Launcher", 2, {"CT Torpedo", "CT Big Armor"}), ar.loc);
        const DesignId base = pointDefense ? design(s, ar.b, "Guarded", "Test Station", {"Test Bridge", "CT PD", "CT Big Armor", "CT Big Armor"})
                                           : design(s, ar.b, "Plain", "Test Station", {"Test Bridge", "CT Big Armor", "CT Big Armor"});
        spawn(s, base, ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const CombatRecord& rec = s.combats.front();
        int impacts = 0, shotDown = 0, launched = 0;
        int64_t impactDamage = 0;
        std::map<uint32_t, int> launchRound;
        for (const CombatEvent& e : rec.events) {
            if (e.kind == CombatEvent::Kind::Seeker) {
                ++launched;
                launchRound.emplace(e.piece, e.round);
            }
            if (e.kind == CombatEvent::Kind::Move && rec.pieces[e.piece].kind == CombatPiece::Kind::Seeker) {
                CHECK(e.round > launchRound[e.piece]);   // no movement in the launch turn
            }
            if (e.kind != CombatEvent::Kind::Hit) continue;
            if (rec.pieces[e.piece].kind == CombatPiece::Kind::Seeker) {
                ++impacts;
                impactDamage += e.amount;
            }
            if (rec.pieces[e.target].kind == CombatPiece::Kind::Seeker) ++shotDown;
        }
        return std::tuple{impacts, shotDown, launched, impactDamage};
    };
    const auto [impacts, shotDown, launched, damage] = fight(false);
    CHECK(launched > 0);
    CHECK(impacts > 0);
    CHECK(shotDown == 0);
    CHECK(damage == impacts * 37);   // no roll: every arrival hits for the table value
    const auto [impacts2, shotDown2, launched2, damage2] = fight(true);
    CHECK(launched2 > 0);
    CHECK(shotDown2 > 0);
    CHECK(impacts2 < launched2);
}

TEST_CASE("combat: fighters launch from carriers, combine their hits and land again") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId fighter = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    const DesignId carrierDesign = frigate(s, ar.a, "Carrier", 1, {"Test Fighter Bay", "Test Fighter Bay", "CT Big Armor"});
    const VehicleId carrier = spawn(s, carrierDesign, ar.loc);
    s.vehicle(carrier)->cargo.units.push_back({fighter, 5});
    const VehicleId target = spawn(s, design(s, ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    int launched = 0;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Launch) {
            launched += e.amount;
            CHECK(rec.pieces[e.piece].kind == CombatPiece::Kind::UnitGroup);
            CHECK(rec.pieces[e.piece].design == fighter);
        }
    CHECK(launched == 5);   // one group of the strategy's size 5
    // The group's weapons fire together: one Hit event of several guns' damage.
    const int tp = pieceOf(rec, target);
    int combined = 0;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Hit && static_cast<int>(e.target) == tp && rec.pieces[e.piece].kind == CombatPiece::Kind::UnitGroup) {
            CHECK((e.amount % 6 == 0 || e.amount % 4 == 0));   // whole guns of 6 (4 at range 3)
            if (e.amount > 6) ++combined;
        }
    CHECK(combined > 0);
    CHECK(hitsOn(rec, tp) > 0);
    // Survivors land on their carrier after the battle.
    CHECK(s.vehicle(carrier)->cargo.unitCount(fighter) == 5);
    CHECK(std::none_of(s.vehicles.begin(), s.vehicles.end(), [&](const Vehicle& v) { return v.design == fighter; }));
}

TEST_CASE("combat: fighters already in space stay there; only launched groups land") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId fighter = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    const VehicleId carrier = spawn(s, frigate(s, ar.a, "Carrier", 1, {"Test Fighter Bay", "Test Fighter Bay", "CT Big Armor"}), ar.loc);
    const VehicleId group = spawn(s, fighter, ar.loc, 3);
    s.vehicle(group)->supply = 100;
    spawn(s, design(s, ar.b, "Hulk", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    REQUIRE(s.vehicle(group) != nullptr);
    CHECK(s.vehicle(group)->count == 3);
    CHECK(s.vehicle(carrier)->cargo.unitCount(fighter) == 0);
}

TEST_CASE("combat: unit groups lose members one by one; satellites fire each weapon") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId sat = design(s, ar.b, "Sentinel", "Test Satellite Hull", {"Test Satellite Gun", "Test Armor Plate"});
    const VehicleId group = spawn(s, sat, ar.loc, 5);
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Point Blank"}});
    const DesignId gunship = frigate(s, ar.a, "Gunship", 3, {"CT Gun", "CT Big Armor"});   // 10 a hit: five hits per satellite
    const VehicleId gun = spawn(s, gunship, ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    const int left = s.vehicle(group)->count;
    CHECK(left < 5);
    CHECK(s.design(sat).lost == 5 - left);
    CHECK(s.design(gunship).kills == 5 - left);
    CHECK(s.design(gunship).enemyTonnageDestroyed == (5 - left) * int64_t{combatRules().hull(s.design(sat).hull).tonnage});
    CHECK(moodCount(ctx, ar.b, "Any Ship Lost") == 0);   // units are not ships
    // A whole group killed gives +0.1; single members give nothing (spec 04 §15).
    if (left == 0) CHECK(s.vehicle(gun)->experienceTenths == 1);
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    const int sp = pieceOf(rec, group);
    std::map<int, int> firesPerRound;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Fire && static_cast<int>(e.piece) == sp) ++firesPerRound[e.round];
    int most = 0;
    for (const auto& [round, n] : firesPerRound) most = std::max(most, n);
    CHECK(most >= 2);   // each satellite's gun fires on its own
    CHECK(most <= 5);
}

TEST_CASE("combat: mines strike the entering group only, straight to the components") {
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        const DesignId mine = design(s, ar.b, "Mine", "Test Mine Hull", {"Test Warhead"});
        const VehicleId field = spawn(s, mine, ar.loc, 3);
        const VehicleId victim = spawn(s, frigate(s, ar.a, "Victim", 1, {"Test Armor Plate", "Test Shield"}), ar.loc);   // 130 structure
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        CHECK(s.vehicle(victim)->count == 0);   // 60-point warheads; shields do not help
        CHECK(s.vehicle(field)->count <= 1);
        CHECK(s.combats.empty());               // mines are not a battle
        CHECK(moodCount(ctx, ar.a, "Any Ship Lost") == 1);
        CHECK(s.design(mine).kills == 1);
        CHECK(s.design(mine).enemyTonnageDestroyed == combatRules().hull(s.design(s.vehicle(victim)->design).hull).tonnage);
    }
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        const DesignId mine = design(s, ar.b, "Mine", "Test Mine Hull", {"Test Warhead"});
        const VehicleId field = spawn(s, mine, ar.loc, 3);
        const VehicleId sweeper = spawn(s, frigate(s, ar.a, "Sweeper", 1, {"Test Mine Sweeper"}), ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        CHECK(s.vehicle(field)->count == 0);
        CHECK(damageTaken(s, sweeper) == 0);
        CHECK_FALSE(combat::combatPossible(combatRules(), s, ar.loc));
    }
    {
        // Only the group that moved in is struck; a ship already there is left alone.
        Arena ar = makeArena();
        GameState& s = ar.s;
        const DesignId mine = design(s, ar.b, "Mine", "Test Mine Hull", {"Test Warhead"});
        const VehicleId field = spawn(s, mine, ar.loc, 1);
        const VehicleId resident = spawn(s, frigate(s, ar.a, "Resident", 1, {"Test Armor Plate"}), ar.loc);
        const VehicleId newcomer = spawn(s, frigate(s, ar.a, "Newcomer", 1, {"Test Armor Plate"}), ar.loc);
        arriveFrom(s, newcomer, 1, 0);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        CHECK(s.vehicle(field)->count == 0);
        CHECK(damageTaken(s, resident) == 0);
        CHECK(damageTaken(s, newcomer) > 0);
    }
    {
        // A group of an empire at peace with the mine owner is left alone. The
        // vehicles movement names are struck group by group (a fleet, or one
        // vehicle), so the peaceful group does not shield the hostile one.
        Arena ar = makeArena(7, 3);
        GameState& s = ar.s;
        setTreaty(s, ar.b, ar.c, Treaty::NonAggression);
        const DesignId mine = design(s, ar.b, "Mine", "Test Mine Hull", {"Test Warhead"});
        const VehicleId field = spawn(s, mine, ar.loc, 2);
        const VehicleId hostile = spawn(s, frigate(s, ar.a, "Hostile", 1, {"Test Armor Plate"}), ar.loc);
        const VehicleId friendly = spawn(s, frigate(s, ar.c, "Friendly", 1, {"Test Armor Plate"}), ar.loc);
        setTreaty(s, ar.a, ar.c, Treaty::NonAggression);
        TurnContext ctx = context(s);
        const std::vector<VehicleId> peaceful{friendly};
        combat::resolveSpaceCombat(ctx, ar.loc, peaceful);
        CHECK(s.vehicle(field)->count == 2);
        CHECK(damageTaken(s, friendly) == 0);
        // Nobody entered: no strike either.
        combat::resolveSpaceCombat(ctx, ar.loc, std::span<const VehicleId>{});
        CHECK(s.vehicle(field)->count == 2);
        const std::vector<VehicleId> both{hostile, friendly};
        combat::resolveSpaceCombat(ctx, ar.loc, both);
        const bool used = s.vehicle(field) == nullptr || s.vehicle(field)->count < 2;
        CHECK(used);
        CHECK(damageTaken(s, friendly) == 0);
        CHECK(damageTaken(s, hostile) > 0);
    }
}

TEST_CASE("combat: planets fight with platforms, then lose population and facilities") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& home = homeworld(s, ar.b);
    const ObjectId planet = home.planet;
    const Location there = locationOf(s.galaxy, planet);
    home.population = {{ar.b, 1000}};
    const size_t facilitiesBefore = home.facilities.size();
    const DesignId platform = design(s, ar.b, "Bastion", "CT Platform Hull", {"CT Platform Core", "CT Platform Gun"});
    home.cargo.units.push_back({platform, 2});
    const DesignId bomber = frigate(s, ar.a, "Bomber", 3, {"CT Big Gun", "CT Big Armor", "CT Big Armor"});
    const VehicleId attacker = spawn(s, bomber, there);

    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, there);
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    auto pl = std::find_if(rec.pieces.begin(), rec.pieces.end(), [&](const CombatPiece& p) { return p.kind == CombatPiece::Kind::Planet && p.planet == planet; });
    REQUIRE(pl != rec.pieces.end());
    CHECK(pl->startX + 4 <= combat::kCombatMapWidth);
    const Colony* after = s.colony(planet);
    REQUIRE(after != nullptr);
    CHECK(after->cargo.unitCount(platform) == 0);   // platforms go first
    CHECK(s.design(platform).lost == 2);
    const int64_t killed = 1000 - after->totalPopulation();
    CHECK(killed > 0);
    CHECK(killed % 10 == 0);   // each 100-point hit kills 100 / 10 = 10M
    CHECK(moodCount(ctx, ar.b, "1M Population Killed") == killed);
    CHECK(moodCount(ctx, ar.b, "Battle in Sector - Loss") + moodCount(ctx, ar.b, "Battle in Sector - Stalemate") == 1);
    // Facilities fall to at most (hit points) / (starting hit points / facilities) (spec 04 §11).
    CHECK(after->facilities.size() <= facilitiesBefore);
    if (facilitiesBefore > 0) {
        const int64_t start = 1000 * 10 + 2 * 40;
        const int64_t per = start / static_cast<int64_t>(facilitiesBefore);
        CHECK(static_cast<int64_t>(after->facilities.size()) >= after->totalPopulation() * 10 / per);
    }
    CHECK(s.vehicle(attacker)->count == 1);
}

TEST_CASE("combat: platforms shield the population until destroyed") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& home = homeworld(s, ar.b);
    const ObjectId planet = home.planet;
    home.population = {{ar.b, 1000}};
    const DesignId fortress = design(s, ar.b, "Fortress", "CT Platform Hull", {"CT Big Armor", "CT Big Armor", "CT Platform Gun"});
    home.cargo.units.push_back({fortress, 2});
    const VehicleId plinker =
        spawn(s, frigate(s, ar.a, "Plinker", 3, {"CT Gun", "CT Big Armor", "CT Big Armor", "CT Big Armor"}), locationOf(s.galaxy, planet));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, locationOf(s.galaxy, planet));
    REQUIRE(s.colony(planet) != nullptr);
    REQUIRE(s.combats.size() == 1);
    CHECK(hitsOn(s.combats.front(), pieceOf(s.combats.front(), plinker)) > 0);   // the platforms fire back
    CHECK(s.colony(planet)->cargo.unitCount(fortress) == 2);
    CHECK(s.colony(planet)->totalPopulation() == 1000);
    CHECK(moodCount(ctx, ar.b, "1M Population Killed") == 0);
}

TEST_CASE("combat: each platform weapon fires on its own and keeps its reload") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& home = homeworld(s, ar.b);
    home.population = {{ar.b, 1000}};
    const DesignId platform = design(s, ar.b, "Mortar", "CT Platform Hull", {"CT Big Armor", "CT Slow Gun"});
    home.cargo.units.push_back({platform, 2});
    spawn(s, frigate(s, ar.a, "Tank", 2, {"CT Big Armor", "CT Big Armor", "CT Big Armor"}), locationOf(s.galaxy, home.planet));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, locationOf(s.galaxy, home.planet));
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    std::map<int, int> perRound;
    for (const CombatEvent& e : rec.events)
        if (e.kind == CombatEvent::Kind::Fire && rec.pieces[e.piece].kind == CombatPiece::Kind::Planet) ++perRound[e.round];
    REQUIRE_FALSE(perRound.empty());
    // Two platforms, one gun each: two shots in a turn, then two turns of reloading.
    for (const auto& [round, n] : perRound) {
        CHECK(n == 2);
        CHECK(perRound.count(round + 1) == 0);
        CHECK(perRound.count(round + 2) == 0);
    }
}

TEST_CASE("combat: planet-only weapons") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& home = homeworld(s, ar.b);
    const ObjectId planet = home.planet;
    home.population = {{ar.b, 1000}};
    const DesignId fortress = design(s, ar.b, "Fortress", "CT Platform Hull", {"CT Big Armor", "CT Big Armor"});
    home.cargo.units.push_back({fortress, 1});
    spawn(s, frigate(s, ar.a, "Plague Ship", 3, {"CT Neutron Bomb", "CT Plague Bomb", "CT Big Armor"}), locationOf(s.galaxy, planet));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, locationOf(s.galaxy, planet));
    REQUIRE(s.colony(planet) != nullptr);
    CHECK(s.colony(planet)->cargo.unitCount(fortress) == 1);   // untouched
    const int64_t killed = 1000 - s.colony(planet)->totalPopulation();
    CHECK(killed > 0);
    CHECK(killed % 5 == 0);   // 50 / 10 per hit
    CHECK(s.colony(planet)->plagueLevel == 2);
}

TEST_CASE("combat: conditions weapons lower a planet's conditions by D x 0.1") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& home = homeworld(s, ar.b);
    home.population = {{ar.b, 1000}};
    s.galaxy.object(home.planet).conditions = Conditions::hundredths(120);
    spawn(s, frigate(s, ar.a, "Polluter", 3, {"CT Climate Bomb", "CT Big Armor"}), locationOf(s.galaxy, home.planet));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, locationOf(s.galaxy, home.planet));
    const int64_t after = s.galaxy.object(home.planet).conditions.inHundredths();
    CHECK(after < 120);
    CHECK(after >= 0);
    CHECK((120 - after) % 10 == 0);   // each 1-point hit costs 0.1
    CHECK(s.colony(home.planet)->totalPopulation() == 1000);
}

TEST_CASE("combat: a planet that takes damage sheds cargo above its capacity") {
    // Spec 02 §2, §13 Q49: the population held as cargo goes first, 1M at a time.
    const Rules& r = combatRules();
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& home = homeworld(s, ar.b);
    home.population = {{ar.b, 1000}};
    const int64_t capacity = colonyCargoCapacity(r, s, home);
    const int64_t mass = r.setting("Population Mass", 5);
    home.cargo.units.clear();
    home.cargo.population = {{ar.b, capacity / mass + 3}};
    REQUIRE(cargoSpaceUsed(r, s, home.cargo) > capacity);
    spawn(s, frigate(s, ar.a, "Bomber", 3, {"CT Neutron Bomb", "CT Big Armor"}), locationOf(s.galaxy, home.planet));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, locationOf(s.galaxy, home.planet));
    REQUIRE(s.colony(home.planet) != nullptr);
    REQUIRE(s.colony(home.planet)->totalPopulation() < 1000);  // it took damage
    REQUIRE(s.colony(home.planet)->cargo.population.size() == 1);
    CHECK(s.colony(home.planet)->cargo.population[0].millions == capacity / mass);
}

TEST_CASE("combat: bombardment can wipe out a colony; the planet stays on the map") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& home = homeworld(s, ar.b);
    const ObjectId planet = home.planet;
    home.population = {{ar.b, 1}};
    const std::array<int, 3> values = s.galaxy.object(planet).value;
    spawn(s, frigate(s, ar.a, "Bomber", 3, {"CT Big Gun"}), locationOf(s.galaxy, planet));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, locationOf(s.galaxy, planet));
    CHECK(s.colony(planet) == nullptr);
    // The population died out (spec 02 §2): each value drops by `Planet Value Percent Loss After Owner Death`.
    for (size_t k = 0; k < 3; ++k) CHECK(s.galaxy.object(planet).value[k] == std::max(0, values[k] - 10));
    CHECK(moodCount(ctx, ar.b, "Any Planet Lost") == 1);
    CHECK(moodCount(ctx, ar.b, "1M Population Killed") == 1);
    CHECK(moodCount(ctx, ar.a, "Battle in System - Win") == 1);
    REQUIRE(s.combats.size() == 1);
    CHECK(countEvents(s.combats.front(), CombatEvent::Kind::Destroyed) == 1);
}

TEST_CASE("combat: planets launch up to 100 of each kind per combat turn") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& home = homeworld(s, ar.b);
    home.population = {{ar.b, 1000}};
    const DesignId fighter = design(s, ar.b, "Hornet", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
    home.cargo.units.push_back({fighter, 150});
    spawn(s, frigate(s, ar.a, "Visitor", 2, {"CT Big Armor", "CT Big Armor"}), locationOf(s.galaxy, home.planet));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, locationOf(s.galaxy, home.planet));
    REQUIRE(s.combats.size() == 1);
    std::map<int, int> perRound;
    for (const CombatEvent& e : s.combats.front().events)
        if (e.kind == CombatEvent::Kind::Launch) perRound[e.round] += e.amount;
    CHECK(perRound[1] == 100);
    CHECK(perRound[2] == 50);
}

TEST_CASE("combat: boarding captures ships; crew quarters, security and self-destruct resist") {
    auto board = [](std::initializer_list<std::string_view> defenderParts) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        useStrategy(s, ar.a, {{"Primary Movement Strategy", "Board Enemy Ships"}, {"Secondary Movement Strategy", "Don't Get Hurt"}});
        const DesignId boarderDesign = frigate(s, ar.a, "Boarder", 4, {"Test Boarding Party", "Test Boarding Party"});
        const VehicleId boarder = spawn(s, boarderDesign, ar.loc);
        Design prize;
        prize.owner = ar.b;
        prize.name = "Prize";
        prize.hull = hullIndex(combatRules(), "Test Frigate");
        for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"})
            prize.entries.push_back({componentIndex(combatRules(), c), -1});
        for (auto c : defenderParts) prize.entries.push_back({componentIndex(combatRules(), c), -1});
        const DesignId prizeDesign = addDesign(s, prize);
        const VehicleId target = spawn(s, prizeDesign, ar.loc);
        s.vehicle(target)->experience = 12;
        s.vehicle(target)->orders.push_back(Order{OrderKind::Sentry});
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        return std::tuple{std::move(ar), boarder, target, std::move(ctx.moodEvents)};
    };
    {
        auto [ar, boarder, target, moods] = board({});
        const GameState& s = ar.s;
        REQUIRE(s.vehicle(target) != nullptr);
        CHECK(s.vehicle(target)->owner == ar.a);   // 40 attack against 4 (one crew quarters)
        CHECK(s.vehicle(target)->experience == 0);
        CHECK(s.vehicle(target)->orders.empty());
        // The boarding parties are spent.
        CHECK_FALSE(entryIntact(combatRules(), s, *s.vehicle(boarder), 7));
        CHECK_FALSE(entryIntact(combatRules(), s, *s.vehicle(boarder), 8));
        REQUIRE(s.combats.size() == 1);
        CHECK(countEvents(s.combats.front(), CombatEvent::Kind::Captured) == 1);
        int lost = 0;
        for (const MoodEvent& m : moods)
            if (m.empire == ar.b && m.trigger == "Any Ship Lost") lost += m.count;
        CHECK(lost == 1);
    }
    {
        auto [ar, boarder, target, moods] = board({"Test Security Station", "Test Security Station"});
        const GameState& s = ar.s;
        CHECK(s.vehicle(target)->owner == ar.b);   // 40 attack against 40 + 4: a strict comparison
        CHECK_FALSE(entryIntact(combatRules(), s, *s.vehicle(boarder), 7));
    }
    {
        auto [ar, boarder, target, moods] = board({"Test Crew Quarters", "Test Crew Quarters", "Test Crew Quarters", "Test Crew Quarters",
                                                   "Test Crew Quarters", "Test Crew Quarters", "Test Crew Quarters", "Test Crew Quarters",
                                                   "Test Crew Quarters"});
        CHECK(ar.s.vehicle(target)->owner == ar.b);   // ten crew quarters: 40 against 40
    }
    {
        auto [ar, boarder, target, moods] = board({"Test Self Destruct"});
        const GameState& s = ar.s;
        CHECK(s.vehicle(target)->count == 0);
        CHECK(s.vehicle(boarder)->count == 0);
    }
}

TEST_CASE("combat: crew conversion takes ships without a master computer") {
    for (const bool computer : {false, true}) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        spawn(s, frigate(s, ar.a, "Preacher", 3, {"CT Converter", "CT Always Hit", "CT Big Armor"}), ar.loc);
        const VehicleId target = computer ? spawn(s, frigate(s, ar.b, "Robot", 1, {"Test Master Computer"}), ar.loc)
                                          : spawn(s, frigate(s, ar.b, "Crewed", 1, {}), ar.loc);
        s.vehicle(target)->experience = 7;
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        CHECK(s.vehicle(target)->owner == (computer ? ar.b : ar.a));
        CHECK(s.vehicle(target)->experience == 7);   // conversion keeps the crew's experience
    }
}

TEST_CASE("combat: tractor beams push by hull size and jammers slow reloads") {
    auto moves = [](std::string_view tugHull, bool satellites) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        Design tug;
        tug.owner = ar.a;
        tug.name = "Tug";
        tug.hull = hullIndex(combatRules(), tugHull);
        for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Engine", "Test Engine", "CT Tractor",
                       "CT Always Hit", "CT Fuel Pod"})
            tug.entries.push_back({componentIndex(combatRules(), c), -1});
        spawn(s, addDesign(s, tug), ar.loc);
        const VehicleId target = satellites ? spawn(s, design(s, ar.b, "Buoy", "Test Satellite Hull", {"Test Armor Plate"}), ar.loc, 2)
                                            : spawn(s, design(s, ar.b, "Station", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const CombatRecord& rec = s.combats.front();
        const int tp = pieceOf(rec, target);
        int n = 0;
        for (const CombatEvent& e : rec.events)
            if (e.kind == CombatEvent::Kind::Move && static_cast<int>(e.piece) == tp) ++n;
        return n;
    };
    CHECK(moves("Test Frigate", true) > 0);      // satellites cannot move by themselves
    CHECK(moves("Test Frigate", false) == 0);    // a 150 kT hull cannot move a 500 kT base
    CHECK(moves("CT Giant Hull", false) > 0);    // a 900 kT one can
    auto shotsByStation = [](bool jam) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        if (jam) spawn(s, frigate(s, ar.a, "Jammer", 3, {"CT Jammer", "CT Always Hit", "CT Big Armor", "CT Big Armor"}), ar.loc);
        else spawn(s, frigate(s, ar.a, "Gunner", 3, {"CT Gun", "CT Always Hit", "CT Big Armor", "CT Big Armor"}), ar.loc);
        const VehicleId station = spawn(s, design(s, ar.b, "Gun Station", "Test Station", {"Test Bridge", "CT Long Gun", "CT Big Armor"}), ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const CombatRecord& rec = s.combats.front();
        int shots = 0;
        for (const CombatEvent& e : rec.events)
            if (e.kind == CombatEvent::Kind::Fire && rec.pieces[e.piece].vehicle == station) ++shots;
        return shots;
    };
    const int free = shotsByStation(false);
    const int jammed = shotsByStation(true);
    CHECK(free > 0);
    CHECK(jammed < free);
}

TEST_CASE("combat: a ship without supplies neither fires nor raises shields; bases need none") {
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        const VehicleId dry = spawn(s, frigate(s, ar.a, "Dry", 2, {"CT Gun", "Test Shield", "CT Big Armor"}), ar.loc);
        s.vehicle(dry)->supply = 0;
        spawn(s, frigate(s, ar.b, "Target", 1, {"CT Big Armor"}), ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const int dp = pieceOf(s.combats.front(), dry);
        for (const CombatEvent& e : s.combats.front().events) CHECK(!(e.kind == CombatEvent::Kind::Fire && static_cast<int>(e.piece) == dp));
    }
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        const VehicleId base = spawn(s, design(s, ar.a, "Fort", "Test Station", {"Test Bridge", "CT Gun", "CT Big Armor"}), ar.loc);
        // Bases hold unlimited supply (spec 03 §7); even with none they fire.
        s.vehicle(base)->supply = 0;
        spawn(s, frigate(s, ar.b, "Target", 1, {"CT Gun", "CT Big Armor"}), ar.loc);   // armed, so it closes in
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const int bp = pieceOf(s.combats.front(), base);
        CHECK(std::any_of(s.combats.front().events.begin(), s.combats.front().events.end(),
                          [&](const CombatEvent& e) { return e.kind == CombatEvent::Kind::Fire && static_cast<int>(e.piece) == bp; }));
    }
}

TEST_CASE("combat: don't get hurt keeps out of reach") {
    // A fast skirmisher with a long gun against a slow brawler with a short one.
    auto fight = [](std::string_view movement) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        useStrategy(s, ar.a, {{"Primary Movement Strategy", std::string(movement)}});
        const VehicleId skirmisher = spawn(s, frigate(s, ar.a, "Skirmisher", 6, {"CT Long Gun", "CT Big Armor"}), ar.loc);
        const VehicleId brawler = spawn(s, frigate(s, ar.b, "Brawler", 1, {"CT Short Gun", "CT Big Armor"}), ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        const CombatRecord& rec = s.combats.front();
        return std::pair{hitsOn(rec, pieceOf(rec, skirmisher)), hitsOn(rec, pieceOf(rec, brawler))};
    };
    const auto [evaderHurt, evaderDealt] = fight("Don't Get Hurt");
    const auto [closerHurt, closerDealt] = fight("Point Blank");
    CHECK(evaderDealt > 0);            // it still fires whenever the enemy comes in range
    CHECK(closerHurt >= 100);          // at point blank the brawler hits nearly every turn
    CHECK(evaderHurt * 5 <= closerHurt);
}

TEST_CASE("combat: kamikaze ships and drones ram") {
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        useStrategy(s, ar.a, {{"Primary Movement Strategy", "Ram"}});
        const VehicleId rammer = spawn(s, frigate(s, ar.a, "Rammer", 4, {"Test Warhead"}), ar.loc);
        const VehicleId hulk = spawn(s, frigate(s, ar.b, "Hulk", 1, {"CT Big Armor"}), ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        // The target takes 60 % of the rammer's 120 structure plus its 60-point warhead;
        // the rammer takes the hulk's 580 plus that warhead and does not survive (spec 04 §10.3).
        REQUIRE(s.combats.size() == 1);
        const CombatRecord& rec = s.combats.front();
        CHECK(hitsOn(rec, pieceOf(rec, hulk)) == 72 + 60);
        CHECK(hitsOn(rec, pieceOf(rec, rammer)) == 0);   // the return blow is no weapon hit
        CHECK(s.vehicle(rammer)->count == 0);
    }
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        const DesignId drone = design(s, ar.a, "Dart", "Test Drone Hull", {"Test Engine", "Test Engine", "Test Warhead"});
        const VehicleId carrier = spawn(s, frigate(s, ar.a, "Drone Carrier", 1, {"CT Drone Bay", "CT Big Armor"}), ar.loc);
        s.vehicle(carrier)->cargo.units.push_back({drone, 2});
        const VehicleId hulk = spawn(s, frigate(s, ar.b, "Hulk", 1, {"CT Big Armor"}), ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const CombatRecord& rec = s.combats.front();
        // One group of two drones: each warhead strikes on its own, then 60 % of the group's 60 hit points.
        CHECK(hitsOn(rec, pieceOf(rec, hulk)) == 60 + 60 + 36);
        CHECK(s.vehicle(carrier)->cargo.unitCount(drone) == 0);
        CHECK(s.design(drone).lost == 2);   // spent by their ram
    }
}

// ---- Ground combat ------------------------------------------------------------------------------------------

TEST_CASE("combat: landing troops needs a hostile, uncontested planet") {
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    const Rules& r = combatRules();
    Colony& target = homeworld(s, ar.b);
    const Location there = locationOf(s.galaxy, target.planet);
    target.population = {{ar.b, 100}};
    const DesignId troopA = design(s, ar.a, "Trooper A", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const DesignId troopC = design(s, ar.c, "Trooper C", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transportA = spawn(s, frigate(s, ar.a, "Transport A", 1, {"Test Cargo Bay"}), there);
    const VehicleId transportC = spawn(s, frigate(s, ar.c, "Transport C", 1, {"Test Cargo Bay"}), there);
    s.vehicle(transportA)->cargo.units.push_back({troopA, 4});
    s.vehicle(transportC)->cargo.units.push_back({troopC, 4});

    setTreaty(s, ar.a, ar.b, Treaty::NonAggression);
    CHECK(combat::landTroops(r, s, transportA, target.planet, troopA, 4) == 0);
    CHECK(target.militia == -1);
    setTreaty(s, ar.a, ar.b, Treaty::War);
    CHECK(combat::landTroops(r, s, transportA, target.planet, troopA, 3) == 3);
    CHECK(s.vehicle(transportA)->cargo.unitCount(troopA) == 1);
    CHECK(target.cargo.unitCount(troopA) == 3);
    CHECK(target.militia == 5);   // the first landing raises the militia pool: 100M / 20
    CHECK(combat::invaders(r, s, target) == std::vector<EmpireId>{ar.a});
    // A third empire may not land on a contested planet.
    CHECK(combat::landTroops(r, s, transportC, target.planet, troopC, 4) == 0);
    // Wrong sector.
    s.vehicle(transportA)->location = ar.loc;
    CHECK(combat::landTroops(r, s, transportA, target.planet, troopA, 1) == 0);
}

TEST_CASE("combat: ground combat captures a planet") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    Colony& target = homeworld(s, ar.b);
    const ObjectId planet = target.planet;
    target.population = {{ar.b, 20}};   // one militia unit
    QueueItem item;
    item.kind = QueueItem::Kind::Facility;
    target.queue.items.push_back(item);
    const size_t facilities = target.facilities.size();
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transport = spawn(s, frigate(s, ar.a, "Transport", 1, {"Test Cargo Bay"}), locationOf(s.galaxy, planet));
    s.vehicle(transport)->cargo.units.push_back({trooper, 10});
    REQUIRE(combat::landTroops(r, s, transport, planet, trooper, 10) == 10);

    TurnContext ctx = context(s);
    combat::runGroundCombat(ctx, ar.a);
    const Colony* after = s.colony(planet);
    REQUIRE(after != nullptr);
    CHECK(after->owner == ar.a);
    CHECK_FALSE(after->homeworld);
    CHECK(after->queue.items.empty());
    CHECK(after->facilities.size() == facilities);   // no facilities are lost in ground combat
    CHECK(after->totalPopulation() == 20);            // the population now serves the captor
    CHECK(after->cargo.unitCount(trooper) > 0);
    CHECK(after->militia == -1);
    CHECK(combat::invaders(r, s, *after).empty());
    CHECK(moodCount(ctx, ar.b, "Any Our Planet Captured") == 1);
    CHECK(moodCount(ctx, ar.b, "Homeworld Lost") == 1);
    CHECK(moodCount(ctx, ar.b, "Any Planet Lost") == 1);
    CHECK(moodCount(ctx, ar.a, "Any Enemy Planet Captured") == 1);
    CHECK(std::any_of(s.empire(ar.a).log.begin(), s.empire(ar.a).log.end(), [](const LogEntry& l) { return l.category == LogCategory::Combat; }));
    // Both histories list the capture under the other empire, at the planet.
    for (auto [owner, other] : {std::pair{ar.a, ar.b}, std::pair{ar.b, ar.a}}) {
        const auto& record = s.empire(owner).historyEvents;
        REQUIRE(record.size() == 1);
        CHECK(record[0].empire == other);
        CHECK(record[0].location == std::optional<Location>(locationOf(s.galaxy, planet)));
    }
}

TEST_CASE("combat: ground rounds carry damage and multiply by the ground percentage") {
    // Troops with a +120 scope always hit (60 + 50 - 0 >= 100). Three rifles of 8
    // make 24 a round; times 30 % that is 7, carried back as 23, and so on: the
    // single militia unit (30 hit points) falls in round 5 (spec 04 §13).
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& target = homeworld(s, ar.b);
    const ObjectId planet = target.planet;
    target.population = {{ar.b, 20}};
    const DesignId trooper = design(s, ar.a, "Marksman", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor", "CT Troop Scope"});
    target.cargo.units.push_back({trooper, 3});
    target.militia = 1;
    combat::detail::GroundFight fight;
    fight.attacker = ar.a;
    fight.defender = ar.b;
    fight.cargo = &target.cargo;
    fight.population = &target.population;
    fight.militia = &target.militia;
    Rng rng(4);
    const combat::detail::GroundOutcome o = combat::detail::fightGround(combatRules(), s, combat::loadSettings(combatRules()), fight, rng);
    // The attackers' side of the fight, worked out with the same arithmetic.
    const int racial = combat::detail::groundModifier(combatRules(), s.empire(ar.a));
    int expected = 0;
    int64_t carry = 0;
    for (int round = 1; round <= 10 && expected == 0; ++round) {
        const int64_t base = xmath::pctTrunc(24 + carry, 30);
        const int64_t total = base + xmath::pctRound(base, racial);
        if (total >= 30) expected = round;
        carry = (xmath::Ext(total) / xmath::percent(30)).trunc();
    }
    CHECK(expected >= 4);
    CHECK(o.captured);
    CHECK(o.rounds == expected);
    CHECK(o.militiaLost == 1);
    CHECK(o.attackersLost == 0);
    CHECK(target.militia == 0);   // the survivors are the new pool
    CHECK(s.colony(planet)->cargo.unitCount(trooper) == 3);
    CHECK((xmath::Ext(7) / xmath::percent(30)).trunc() == 23);
}

TEST_CASE("combat: militia repel a small invasion") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    Colony& target = homeworld(s, ar.b);
    const ObjectId planet = target.planet;
    target.population = {{ar.b, 2000}};   // 100 militia
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    target.cargo.units.push_back({trooper, 2});
    const DesignId guard = design(s, ar.b, "Guard", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    target.cargo.units.push_back({guard, 1});
    TurnContext ctx = context(s);
    combat::runGroundCombat(ctx, ar.a);
    const Colony* after = s.colony(planet);
    REQUIRE(after != nullptr);
    CHECK(after->owner == ar.b);
    CHECK(after->cargo.unitCount(trooper) == 0);
    CHECK(s.design(trooper).lost == 2);
    CHECK(combat::invaders(r, s, *after).empty());
    CHECK(after->militia == -1);   // the invasion is over
    CHECK(after->totalPopulation() == 2000);   // militia losses cost no population
    // Peace stops the fighting at once.
    target.cargo.units.push_back({trooper, 2});
    setTreaty(s, ar.a, ar.b, Treaty::NonAggression);
    TurnContext calm = context(s);
    combat::runGroundCombat(calm, ar.a);
    CHECK(s.colony(planet)->cargo.unitCount(trooper) == 2);
}

TEST_CASE("combat: troops dropped during a space battle fight at once") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Drop Troops (if carrying)"}, {"Secondary Movement Strategy", "Don't Get Hurt"}});
    Colony& target = homeworld(s, ar.b);
    const ObjectId planet = target.planet;
    const Location there = locationOf(s.galaxy, planet);
    target.population = {{ar.b, 20}};
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor", "CT Troop Scope"});
    const VehicleId transport = spawn(s, frigate(s, ar.a, "Lander", 3, {"Test Cargo Bay"}), there);
    s.vehicle(transport)->cargo.units.push_back({trooper, 8});
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, there);
    CHECK(s.vehicle(transport)->cargo.unitCount(trooper) == 0);
    // The ground combat was fought in the middle of the battle and the planet changed sides.
    REQUIRE(s.colony(planet) != nullptr);
    CHECK(s.colony(planet)->owner == ar.a);
    CHECK(s.colony(planet)->cargo.unitCount(trooper) > 0);
    REQUIRE(s.combats.size() == 1);
    CHECK(countEvents(s.combats.front(), CombatEvent::Kind::Launch) >= 1);
    CHECK(countEvents(s.combats.front(), CombatEvent::Kind::Captured) == 1);
    CHECK(moodCount(ctx, ar.b, "Any Our Planet Captured") == 1);
    // The Ground Combat window's record: both sides as the fight began, and what was left.
    const CombatRecord& rec = s.combats.front();
    REQUIRE(rec.grounds.size() == 1);
    const GroundCombat& g = rec.grounds.front();
    CHECK(g.planet == planet);
    CHECK(g.attacker == ar.a);
    CHECK(g.defender == ar.b);
    CHECK(g.population == 20);
    CHECK(g.captured);
    CHECK(g.rounds >= 1);
    CHECK(rec.pieces[g.planetPiece].planet == planet);
    CHECK(rec.pieces[g.troopShip].vehicle == transport);
    REQUIRE(g.attackers == std::vector<UnitStack>{{trooper, 8}});
    REQUIRE(g.attackersLeft.size() == 1);
    CHECK(g.attackersLeft.front().count == s.colony(planet)->cargo.unitCount(trooper));
    CHECK(g.militia == 1);   // 20M: one militia unit
    CHECK(g.militiaLeft == 0);
}

// ---- Determinism --------------------------------------------------------------------------------------------

TEST_CASE("combat: battles are deterministic") {
    auto run = [] {
        Arena ar = makeArena(21);
        GameState& s = ar.s;
        const DesignId fighter = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
        const VehicleId carrier = spawn(s, frigate(s, ar.a, "Carrier", 2, {"Test Fighter Bay", "Test Laser", "Test Shield"}), ar.loc);
        s.vehicle(carrier)->cargo.units.push_back({fighter, 3});
        spawn(s, frigate(s, ar.a, "Escort", 3, {"Test Laser", "Test Laser", "Test Armor Plate"}), ar.loc);
        const VehicleId raider = spawn(s, frigate(s, ar.b, "Raider", 3, {"CT Torpedo", "Test Laser", "Test Armor Plate"}), ar.loc);
        spawn(s, frigate(s, ar.b, "Picket", 2, {"CT PD", "Test Disruptor", "Test Armor Plate"}), ar.loc);
        arriveFrom(s, raider, 0, -1);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        return std::move(ar.s);
    };
    const GameState x = run();
    const GameState y = run();
    REQUIRE(x.combats.size() == 1);
    REQUIRE(y.combats.size() == 1);
    const auto& ex = x.combats.front().events;
    const auto& ey = y.combats.front().events;
    REQUIRE(ex.size() == ey.size());
    CHECK(ex.size() > 10);
    for (size_t i = 0; i < ex.size(); ++i) {
        CHECK(ex[i].kind == ey[i].kind);
        CHECK(ex[i].round == ey[i].round);
        CHECK(ex[i].piece == ey[i].piece);
        CHECK(ex[i].target == ey[i].target);
        CHECK(ex[i].x == ey[i].x);
        CHECK(ex[i].y == ey[i].y);
        CHECK(ex[i].amount == ey[i].amount);
    }
    REQUIRE(x.vehicles.size() == y.vehicles.size());
    for (size_t i = 0; i < x.vehicles.size(); ++i) {
        CHECK(x.vehicles[i].damage == y.vehicles[i].damage);
        CHECK(x.vehicles[i].count == y.vehicles[i].count);
        CHECK(x.vehicles[i].cargo.units == y.vehicles[i].cargo.units);
    }
    CHECK(x.rng == y.rng);
}

// ---- Turn pipeline ------------------------------------------------------------------------------------------

TEST_CASE("combat: ground combat runs in the turn pipeline") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    Colony& target = homeworld(s, ar.b);
    const ObjectId planet = target.planet;
    target.population = {{ar.b, 10}};   // no militia below 20M
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    target.cargo.units.push_back({trooper, 10});
    std::vector<EmpireOrders> none;
    TurnOptions opts;
    opts.aiForMissing = false;
    processTurn(r, s, none, opts);
    CHECK(s.colony(planet)->owner == ar.a);
}

// ---- Installed data (opt-in: OPENSE4_CLASSIC_DATA=auto or a data directory) --------------------------------

namespace {

const Rules* installedRules() {
    static const std::unique_ptr<Rules> rules = []() -> std::unique_ptr<Rules> {
        const char* env = std::getenv("OPENSE4_CLASSIC_DATA");
        if (!env) return nullptr;
        auto dir = ruleset::findInstalledDataDir(std::string_view(env) == "auto" ? std::filesystem::path{} : std::filesystem::path(env));
        if (!dir) return nullptr;
        auto loaded = ruleset::loadRuleset(*dir);
        if (!loaded.ruleset) return nullptr;
        return std::make_unique<Rules>(std::move(*loaded.ruleset), dir->parent_path());
    }();
    return rules.get();
}

} // namespace

TEST_CASE("installed data set: combat identifiers and strategies parse (opt-in)") {
    const Rules* r = installedRules();
    if (!r) return;
    int weapons = 0;
    for (const ruleset::Component& c : r->data().components) {
        if (!c.isWeapon()) continue;
        ++weapons;
        INFO(c.name);
        const combat::DamageType t = combat::parseDamageType(c.weapon.damageType);
        CHECK(datafile::keysEqual(combat::identifier(t), c.weapon.damageType));
        CHECK(combat::parseWeaponTargets(c.weapon.targets) != 0);
    }
    CHECK(weapons > 50);
    REQUIRE(r->data().combatStrategies.size() >= 5);
    for (const ruleset::CombatStrategy& raw : r->data().combatStrategies) {
        INFO(raw.name);
        const combat::Strategy st = combat::parseStrategy(raw);
        CHECK(st.targeting[0] != combat::TargetKey::None);
        for (size_t c = 0; c < combat::kTargetCategories; ++c) CHECK(st.typePriority[c] > 0);
    }
}

TEST_CASE("installed data set: a battle between starting warships (opt-in)") {
    const Rules* r = installedRules();
    if (!r) return;
    GameSetup setup;
    setup.seed = 5;
    setup.options.systemCount = 20;
    setup.options.startTechLevel = 2;
    for (size_t i = 0; i < 2; ++i) {
        EmpireSetup e;
        e.preset = r->racePresets()[i * 5 % r->racePresets().size()].folder;
        setup.empires.push_back(e);
    }
    auto game = createGame(*r, setup);
    REQUIRE(game.has_value());
    GameState& s = *game;
    setTreaty(s, EmpireId{0u}, EmpireId{1u}, Treaty::War);
    const Location where = locationOf(s.galaxy, homeworld(s, EmpireId{1u}).planet);
    // The automatic warship may carry special weapons; give it the first plain direct-fire gun instead.
    std::optional<uint32_t> plainGun;
    for (uint32_t c = 0; c < r->data().components.size() && !plainGun; ++c) {
        const ruleset::Component& comp = r->component(c);
        if (comp.weapon.kind == WeaponKind::DirectFire && datafile::keysEqual(comp.weapon.damageType, "Normal") &&
            (comp.vehicles & ruleset::maskOf(VehicleType::Ship)) &&
            (combat::parseWeaponTargets(comp.weapon.targets) & combat::kTargetShips))
            plainGun = c;
    }
    REQUIRE(plainGun.has_value());
    for (uint32_t e = 0; e < 2; ++e) {
        auto d = autoDesign(*r, s.empire(EmpireId{e}), "warship");
        REQUIRE(d.has_value());
        d->name = "Test Warship";
        for (DesignEntry& entry : d->entries)
            if (r->component(entry.component).isWeapon()) entry = {*plainGun, -1};
        const DesignId id = addDesign(s, *d);
        for (int k = 0; k < 3; ++k) movement::spawnVehicle(*r, s, EmpireId{e}, id, where);
    }
    CHECK(combat::combatPossible(*r, s, where));
    TurnContext ctx{*r, s, {}, {}, {}};
    combat::resolveSpaceCombat(ctx, where);
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& rec = s.combats.front();
    CHECK(countEvents(rec, CombatEvent::Kind::Fire) > 0);
    CHECK(countEvents(rec, CombatEvent::Kind::Hit) > 0);
    for (const CombatEvent& e : rec.events) {
        CHECK(e.round <= 29);
        CHECK(e.x < combat::kCombatMapWidth);
        CHECK(e.y < combat::kCombatMapHeight);
    }
    // The homeworld is a 4x4 piece; the system's other objects in its sector are obstacles.
    CHECK(std::any_of(rec.pieces.begin(), rec.pieces.end(), [](const CombatPiece& p) { return p.kind == CombatPiece::Kind::Planet; }));
}

