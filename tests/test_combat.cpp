// Combat (docs/spec/04): to-hit, damage at range, shields/armor/damage types,
// seekers and point defense, units, mines, planets, boarding, conversion,
// strategies, experience, mood events, the replay record, ground combat and
// determinism. All content is invented for the tests.

#include "engine_fixture.hpp"

#include "datafile/datafile.hpp"

#include "game/combat.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
namespace combat = opense4::game::combat;

namespace {

using ruleset::VehicleType;
using ruleset::WeaponKind;

ruleset::Ability ab(AbilityKind k, int64_t v1 = 0, int64_t v2 = 0) {
    ruleset::Ability a;
    a.type = std::string(game::identifier(k));
    a.value1 = std::to_string(v1);
    a.value2 = std::to_string(v2);
    return a;
}

constexpr ruleset::VehicleTypeMask kAll = 0xFF;
const std::vector<std::string> kHitsAll{"Ships", "Planets", "Ftr", "Sat", "Drone"};

ruleset::Component& part(ruleset::Ruleset& rs, std::string name, int structure, std::vector<ruleset::Ability> abilities,
                         ruleset::VehicleTypeMask mask = kAll) {
    ruleset::Component c;
    c.name = std::move(name);
    c.tonnage = 10;
    c.structure = structure;
    c.vehicles = mask;
    c.abilities = std::move(abilities);
    rs.components.push_back(std::move(c));
    return rs.components.back();
}

ruleset::Component& gun(ruleset::Ruleset& rs, std::string name, WeaponKind kind, std::vector<int> damage, std::string type,
                        std::vector<std::string> targets = kHitsAll, int modifier = 0) {
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

    ruleset::VehicleSize platform;
    platform.name = platform.shortName = "CT Platform Hull";
    platform.type = VehicleType::WeaponPlatform;
    platform.tonnage = 50;
    rs.vehicleSizes.push_back(platform);

    ruleset::WeaponMount mount;
    mount.longName = mount.shortName = "CT Long Mount";
    mount.damagePercent = 150;
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

struct Arena {
    GameState s;
    Location loc;               // an empty sector in empire A's home system
    EmpireId a{0u}, b{1u}, c{2u};
};

// A fresh game on the combat rules with no vehicles, every empire at war, and an empty sector to fight in.
Arena makeArena(uint64_t seed = 7, int empires = 2) {
    GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = 12;
    for (int i = 0; i < empires; ++i) {
        EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        setup.empires.push_back(std::move(e));
    }
    auto g = createGame(combatRules(), setup);
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

DesignId design(GameState& s, EmpireId owner, std::string_view name, std::string_view hull, std::initializer_list<std::string_view> parts) {
    return addTestDesign(s, combatRules(), owner, name, hull, parts);
}

VehicleId spawn(GameState& s, DesignId d, Location where, int count = 1) {
    Vehicle& v = addTestVehicle(s, combatRules(), d, where);
    v.count = count;
    return v.id;
}

// A crewed frigate with `engines` engines plus the given parts.
DesignId frigate(GameState& s, EmpireId owner, std::string_view name, int engines, std::initializer_list<std::string_view> extra) {
    const Rules& r = combatRules();
    Design d;
    d.owner = owner;
    d.name = std::string(name);
    d.hull = hullIndex(r, "Test Frigate");
    for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters"}) d.entries.push_back({componentIndex(r, c), -1});
    for (int i = 0; i < engines; ++i) d.entries.push_back({componentIndex(r, "Test Engine"), -1});
    for (auto c : extra) d.entries.push_back({componentIndex(r, c), -1});
    return addDesign(s, std::move(d));
}

void useStrategy(GameState& s, EmpireId e, std::vector<std::pair<std::string, std::string>> settings) {
    s.empire(e).strategies = {ruleset::CombatStrategy{"Test Plan", std::move(settings)}};
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

TurnContext context(GameState& s) { return TurnContext{combatRules(), s, {}, {}, {}}; }

} // namespace

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

    const std::vector<std::string> classic{"Ships", "Planets", "Ftr", "Sat", "Drone"};
    CHECK(combat::parseWeaponTargets(classic) ==
          (combat::kTargetShips | combat::kTargetPlanets | combat::kTargetFighters | combat::kTargetSatellites | combat::kTargetDrones));
    const std::vector<std::string> overrideList{"Seekers, Fighters"};
    CHECK(combat::parseWeaponTargets(overrideList) == (combat::kTargetSeekers | combat::kTargetFighters));
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
    CHECK(st.damagePercentShip == 55);
    CHECK(st.damageUntilWeaponsGone);

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
    CHECK(combat::militiaCount(cs, 0) == 0);
    CHECK(combat::militiaCount(cs, 7) == 1);
    CHECK(combat::militiaCount(cs, 45) == 2);
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
    // Weapons Always Hit.
    s.vehicle(shooter)->damage[6] = 0;
    CHECK(combat::toHitPercent(r, s, *s.vehicle(shooter), laser, *s.vehicle(dodger), 9) == 100);
}

TEST_CASE("combat: mounts scale damage and shift range") {
    const Rules& r = combatRules();
    const DesignEntry plain{componentIndex(r, "CT Gun"), -1};
    const DesignEntry mounted{componentIndex(r, "CT Gun"), mountIndex(r, "CT Long Mount")};
    CHECK(weaponDamageAtRange(r, plain, 1) == 10);
    CHECK(weaponDamageAtRange(r, plain, 6) == 0);
    CHECK(weaponMaxRange(r, plain) == 5);
    // Range 1..3 all read entry 1; the table ends two squares later; damage x1.5.
    CHECK(weaponDamageAtRange(r, mounted, 1) == 15);
    CHECK(weaponDamageAtRange(r, mounted, 3) == 15);
    CHECK(weaponDamageAtRange(r, mounted, 7) == 15);
    CHECK(weaponDamageAtRange(r, mounted, 8) == 0);
    CHECK(weaponMaxRange(r, mounted) == 7);
}

// ---- The damage pipeline --------------------------------------------------------------------------------

TEST_CASE("combat: shields, armor and damage types") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const Rules& r = combatRules();
    // Entries: 0 bridge, 1 life support, 2 crew, 3 engine, 4 shield(20), 5 phased shield(20), 6-7 armor, 8 laser.
    const DesignId d = frigate(s, ar.a, "Target", 1, {"Test Shield", "CT Phased Shield", "Test Armor Plate", "Test Armor Plate", "Test Laser"});
    const VehicleId id = spawn(s, d, ar.loc);
    const Vehicle base = *s.vehicle(id);
    Rng rng(99);
    using DT = combat::DamageType;

    struct Result {
        combat::detail::HitOutcome out;
        combat::detail::ShieldState sh;
        Vehicle v;
    };
    auto hit = [&](int damage, DT type) {
        Result res{{}, {}, base};
        combat::detail::refreshShields(r, s, res.v, res.sh, true);
        res.out = combat::detail::hitUnit(r, s, res.v, res.sh, damage, type, rng);
        return res;
    };
    auto armorDamage = [](const Vehicle& v) { return v.damage[6] + v.damage[7]; };
    auto internalDamage = [](const Vehicle& v) {
        return v.damage[0] + v.damage[1] + v.damage[2] + v.damage[3] + v.damage[4] + v.damage[5] + v.damage[8];
    };

    {
        const auto res = hit(15, DT::Normal);   // normal shields absorb first
        CHECK(res.sh.normal == 5);
        CHECK(res.sh.phased == 20);
        CHECK(res.out.structureDamage == 0);
    }
    {
        const auto res = hit(30, DT::Normal);   // then the phased pool
        CHECK(res.sh.normal == 0);
        CHECK(res.sh.phased == 10);
        CHECK(res.out.structureDamage == 0);
    }
    {
        const auto res = hit(70, DT::Normal);   // armor takes the rest before internals
        CHECK(res.out.shieldDamage == 40);
        CHECK(armorDamage(res.v) == 30);
        CHECK(internalDamage(res.v) == 0);
    }
    {
        const auto res = hit(15, DT::SkipsNormalShields);
        CHECK(res.sh.normal == 20);
        CHECK(res.sh.phased == 5);
    }
    {
        const auto res = hit(10, DT::SkipsAllShields);
        CHECK(res.sh.normal + res.sh.phased == 40);
        CHECK(armorDamage(res.v) == 10);
    }
    {
        const auto res = hit(100, DT::ShieldsOnly);
        CHECK(res.sh.normal + res.sh.phased == 0);
        CHECK(res.out.structureDamage == 0);
    }
    {
        const auto res = hit(15, DT::QuadDamageToShields);   // 60 against 40 shields: 10 raw used, 5 left
        CHECK(res.sh.normal + res.sh.phased == 0);
        CHECK(res.out.structureDamage == 5);
    }
    {
        const auto res = hit(10, DT::HalfDamageToShields);
        CHECK(res.sh.normal == 15);
        CHECK(res.out.structureDamage == 0);
    }
    {
        const auto res = hit(30, DT::SkipsShieldsAndArmor);
        CHECK(armorDamage(res.v) == 0);
        CHECK(internalDamage(res.v) == 30);
    }
    {
        const auto res = hit(25, DT::OnlyEngines);   // shields still absorb (history 1.70)
        CHECK(res.v.damage[3] == 0);
        const auto big = hit(50, DT::OnlyEngines);
        CHECK(big.v.damage[3] == 10);
        CHECK(armorDamage(big.v) + internalDamage(big.v) - big.v.damage[3] == 0);
    }
    {
        const auto res = hit(100, DT::OnlyWeapons);   // ignores shields and armor, harmless beyond the weapons
        CHECK(res.sh.normal + res.sh.phased == 40);
        CHECK(res.v.damage[8] == 15);
        CHECK(res.out.structureDamage == 15);
        CHECK_FALSE(res.out.destroyed);
        CHECK_FALSE(combat::detail::canAffectVehicle(r, s, res.v, res.sh, DT::OnlyWeapons));
    }
    {
        const auto res = hit(100, DT::OnlyShieldGenerators);   // losing generators caps the shields
        CHECK(res.sh.maxNormal == 0);
        CHECK(res.sh.normal == 0);
        CHECK(res.sh.phased == 0);
    }
    {
        const int total = vehicleStructure(r, s, base);
        CHECK(total == 185);
        const auto res = hit(total + 15, DT::SkipsAllShields);
        CHECK(res.out.destroyed);
        CHECK(res.out.excess == 15);
        CHECK(vehicleDestroyed(r, s, res.v));
    }
    CHECK_FALSE(combat::detail::canAffectVehicle(r, s, base, {}, DT::OnlyPlanetPopulation));
    CHECK_FALSE(combat::detail::canAffectVehicle(r, s, base, {}, DT::ShieldsOnly));
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
        CHECK(combat::detail::hitUnit(r, s, v, sh, 15, combat::DamageType::Normal, rng).structureDamage == 0);
        CHECK(combat::detail::hitUnit(r, s, v, sh, 16, combat::DamageType::Normal, rng).structureDamage == 16);
    }
    {
        const VehicleId id = spawn(s, design(s, ar.a, "Prism", "Test Frigate", {"Test Bridge", "Test Shield", "CT Crystal Armor"}), ar.loc);
        Vehicle v = *s.vehicle(id);
        combat::detail::ShieldState sh;
        combat::detail::refreshShields(r, s, v, sh, true);
        combat::detail::hitUnit(r, s, v, sh, 20, combat::DamageType::Normal, rng);
        CHECK(sh.normal == 0);
        const auto out = combat::detail::hitUnit(r, s, v, sh, 12, combat::DamageType::Normal, rng);
        CHECK(sh.normal == 5);   // 5 points of the hit became shields
        CHECK(out.structureDamage == 7);
    }
    {
        const VehicleId id = spawn(s, design(s, ar.a, "Moss", "Test Frigate", {"Test Bridge", "CT Organic Armor"}), ar.loc);
        Vehicle v = *s.vehicle(id);
        v.damage[1] = 30;
        combat::detail::restoreRegeneratingArmor(r, s, v);
        CHECK(v.damage[1] == 0);
    }
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
    // An undetected cloaked ship neither triggers nor joins combat.
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
    CHECK(s.vehicle(hunter)->orders.empty());   // a ship that fights loses its orders
    CHECK(s.vehicle(hunter)->experience == 6);   // fired (1) + one kill (5)
    CHECK(s.fleet(fid)->experience == 6);
    CHECK(s.design(hunterDesign).kills == 1);
    CHECK(s.design(preyDesign).lost == 1);

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
    CHECK(std::binary_search(s.empire(ar.b).knowledge.seenDesigns.begin(), s.empire(ar.b).knowledge.seenDesigns.end(), hunterDesign));

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
    CHECK(countEvents(rec, CombatEvent::Kind::Move) > 0);
    CHECK(countEvents(rec, CombatEvent::Kind::Fire) > 0);
    CHECK(countEvents(rec, CombatEvent::Kind::Hit) > 0);
    CHECK(countEvents(rec, CombatEvent::Kind::Destroyed) == 1);
    for (const CombatEvent& e : rec.events) {
        CHECK(e.piece < rec.pieces.size());
        CHECK(e.target < rec.pieces.size());
        CHECK(e.round >= 1);
        CHECK(e.round <= 30);
    }
    CHECK(rec.summary.size() >= 2);

    s.removeDeadVehicles();
    CHECK(s.vehicle(prey) == nullptr);
}

TEST_CASE("combat: unarmed sides end in a stalemate") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    spawn(s, frigate(s, ar.a, "Scout A", 1, {}), ar.loc);
    spawn(s, frigate(s, ar.b, "Scout B", 1, {}), ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    CHECK(moodCount(ctx, ar.a, "Battle in System - Stalemate") == 1);
    CHECK(moodCount(ctx, ar.b, "Battle in System - Stalemate") == 1);
    REQUIRE(s.combats.size() == 1);
    CHECK(countEvents(s.combats.front(), CombatEvent::Kind::Fire) == 0);
}

TEST_CASE("combat: an allied bystander sits the battle out") {
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
    CHECK(s.combats.front().participants == std::vector<EmpireId>{ar.a, ar.b});
    CHECK(damageTaken(s, c) == 0);
    CHECK(moodCount(ctx, ar.c, "Battle in System - Win") + moodCount(ctx, ar.c, "Battle in System - Stalemate") == 0);
}

TEST_CASE("combat: fleets start in formation") {
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
    auto find = [&](VehicleId id) {
        return *std::find_if(pieces.begin(), pieces.end(), [&](const CombatPiece& p) { return p.vehicle == id; });
    };
    const CombatPiece L = find(lead), M1 = find(left), M2 = find(right);
    // Facing east, the template's left/right slots become north/south of the leader.
    CHECK(M1.startX == L.startX);
    CHECK(M2.startX == L.startX);
    CHECK(M1.startY == L.startY - 1);
    CHECK(M2.startY == L.startY + 1);
}

TEST_CASE("combat: seekers hit on arrival and point defense shoots them down") {
    // Targets are bases, so the seekers never run out of range chasing them.
    auto fight = [](bool pointDefense) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        spawn(s, frigate(s, ar.a, "Launcher", 2, {"CT Torpedo", "CT Big Armor"}), ar.loc);
        const DesignId base = pointDefense ? design(s, ar.b, "Guarded", "Test Station", {"Test Bridge", "CT PD", "CT Big Armor", "CT Big Armor"})
                                           : design(s, ar.b, "Plain", "Test Station", {"Test Bridge", "CT Big Armor", "CT Big Armor"});
        const VehicleId t = spawn(s, base, ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const CombatRecord& rec = s.combats.front();
        int impacts = 0, shotDown = 0, launched = 0;
        for (const CombatEvent& e : rec.events) {
            if (e.kind == CombatEvent::Kind::Seeker) ++launched;
            if (e.kind != CombatEvent::Kind::Hit) continue;
            if (rec.pieces[e.piece].kind == CombatPiece::Kind::Seeker) ++impacts;
            if (rec.pieces[e.target].kind == CombatPiece::Kind::Seeker) ++shotDown;
        }
        return std::tuple{impacts, shotDown, launched, damageTaken(s, t)};
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
    CHECK(damage2 < damage);
}

TEST_CASE("combat: fighters launch from carriers and land again") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId fighter = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
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
    CHECK(damageTaken(s, target) > 0);
    // Survivors land on their carrier after the battle.
    CHECK(s.vehicle(carrier)->cargo.unitCount(fighter) == 5);
    CHECK(std::none_of(s.vehicles.begin(), s.vehicles.end(), [&](const Vehicle& v) { return v.design == fighter; }));
}

TEST_CASE("combat: unit groups lose members one by one") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    const DesignId sat = design(s, ar.b, "Sentinel", "Test Satellite Hull", {"Test Satellite Gun", "Test Armor Plate"});
    const VehicleId group = spawn(s, sat, ar.loc, 5);
    const DesignId gunship = frigate(s, ar.a, "Gunship", 3, {"CT Big Gun", "CT Big Armor"});
    spawn(s, gunship, ar.loc);
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, ar.loc);
    const int left = s.vehicle(group)->count;
    CHECK(left < 5);
    CHECK(s.design(sat).lost == 5 - left);
    CHECK(s.design(gunship).kills == 5 - left);
    CHECK(moodCount(ctx, ar.b, "Any Ship Lost") == 0);   // units are not ships
}

TEST_CASE("combat: mines strike hostile vehicles and sweepers clear them") {
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        const DesignId mine = design(s, ar.b, "Mine", "Test Mine Hull", {"Test Warhead"});
        const VehicleId field = spawn(s, mine, ar.loc, 3);
        const VehicleId victim = spawn(s, frigate(s, ar.a, "Victim", 1, {"Test Armor Plate"}), ar.loc);   // 110 structure
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        CHECK(s.vehicle(victim)->count == 0);   // two 60-point warheads
        CHECK(s.vehicle(field)->count == 1);    // the third mine is left
        CHECK(s.combats.empty());               // mines are not a battle
        CHECK(moodCount(ctx, ar.a, "Any Ship Lost") == 1);
        CHECK(s.design(mine).kills == 1);
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
}

TEST_CASE("combat: planets fight with platforms, then lose population") {
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
    CHECK(std::any_of(rec.pieces.begin(), rec.pieces.end(),
                      [&](const CombatPiece& p) { return p.kind == CombatPiece::Kind::Planet && p.planet == planet; }));
    const Colony* after = s.colony(planet);
    REQUIRE(after != nullptr);
    CHECK(after->cargo.unitCount(platform) == 0);   // platforms go first
    CHECK(s.design(platform).lost == 2);
    const int64_t killed = 1000 - after->totalPopulation();
    CHECK(killed > 0);
    CHECK(moodCount(ctx, ar.b, "1M Population Killed") == killed);
    CHECK(moodCount(ctx, ar.b, "Battle in Sector - Loss") + moodCount(ctx, ar.b, "Battle in Sector - Stalemate") == 1);
    CHECK(after->facilities.size() == facilitiesBefore - facilitiesBefore * static_cast<size_t>(killed) / 1000);
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
    CHECK(damageTaken(s, plinker) > 0);   // the platforms fire back
    CHECK(s.colony(planet)->cargo.unitCount(fortress) == 2);
    CHECK(s.colony(planet)->totalPopulation() == 1000);
    CHECK(moodCount(ctx, ar.b, "1M Population Killed") == 0);
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
    CHECK(s.colony(planet)->totalPopulation() < 1000);
    CHECK(s.colony(planet)->plagueLevel == 2);
}

TEST_CASE("combat: bombardment can wipe out a colony") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    Colony& home = homeworld(s, ar.b);
    const ObjectId planet = home.planet;
    home.population = {{ar.b, 1}};
    spawn(s, frigate(s, ar.a, "Bomber", 3, {"CT Big Gun"}), locationOf(s.galaxy, planet));
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, locationOf(s.galaxy, planet));
    CHECK(s.colony(planet) == nullptr);
    CHECK(moodCount(ctx, ar.b, "Any Planet Lost") == 1);
    CHECK(moodCount(ctx, ar.b, "1M Population Killed") == 1);
    CHECK(moodCount(ctx, ar.a, "Battle in System - Win") == 1);
}

TEST_CASE("combat: boarding captures ships, security and self-destruct resist") {
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
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        return std::tuple{std::move(ar), boarder, target, std::move(ctx.moodEvents)};
    };
    {
        auto [ar, boarder, target, moods] = board({});
        const GameState& s = ar.s;
        REQUIRE(s.vehicle(target) != nullptr);
        CHECK(s.vehicle(target)->owner == ar.a);
        CHECK(s.vehicle(target)->experience == 0);
        // The boarding parties are used up.
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
        auto [ar, boarder, target, moods] = board({"Test Security Station", "Test Security Station", "Test Security Station"});
        const GameState& s = ar.s;
        CHECK(s.vehicle(target)->owner == ar.b);   // 40 attack against 60 defense
        CHECK_FALSE(entryIntact(combatRules(), s, *s.vehicle(boarder), 7));
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
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        CHECK(s.vehicle(target)->owner == (computer ? ar.b : ar.a));
    }
}

TEST_CASE("combat: tractor beams push and jammers slow reloads") {
    {
        Arena ar = makeArena();
        GameState& s = ar.s;
        spawn(s, frigate(s, ar.a, "Tug", 3, {"CT Tractor", "CT Always Hit"}), ar.loc);
        const VehicleId station = spawn(s, design(s, ar.b, "Station", "Test Station", {"Test Bridge", "CT Big Armor"}), ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        REQUIRE(s.combats.size() == 1);
        const CombatRecord& rec = s.combats.front();
        int moves = 0;
        for (const CombatEvent& e : rec.events)
            if (e.kind == CombatEvent::Kind::Move && rec.pieces[e.piece].vehicle == station) ++moves;
        CHECK(moves > 0);   // a base cannot move by itself
    }
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

TEST_CASE("combat: don't get hurt keeps out of reach") {
    // A fast skirmisher with a long gun against a slow brawler with a short one.
    auto fight = [](std::string_view movement) {
        Arena ar = makeArena();
        GameState& s = ar.s;
        useStrategy(s, ar.a, {{"Primary Movement Strategy", std::string(movement)}});
        const VehicleId skirmisher = spawn(s, frigate(s, ar.a, "Skirmisher", 4, {"CT Long Gun", "CT Big Armor"}), ar.loc);
        const VehicleId brawler = spawn(s, frigate(s, ar.b, "Brawler", 1, {"CT Short Gun", "CT Big Armor"}), ar.loc);
        TurnContext ctx = context(s);
        combat::resolveSpaceCombat(ctx, ar.loc);
        return std::pair{damageTaken(s, skirmisher), damageTaken(s, brawler)};
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
        // The warhead (60) plus the rammer's own structure (100) hit the target; the rammer does not survive.
        CHECK(damageTaken(s, hulk) >= 160);
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
        CHECK(damageTaken(s, hulk) >= 2 * 60);
        CHECK(s.vehicle(carrier)->cargo.unitCount(drone) == 0);
        CHECK(s.design(drone).lost == 2);   // spent by their rams
    }
}

// ---- Ground combat ------------------------------------------------------------------------------------------

TEST_CASE("combat: landing troops needs a hostile, uncontested planet") {
    Arena ar = makeArena(7, 3);
    GameState& s = ar.s;
    const Rules& r = combatRules();
    Colony& target = homeworld(s, ar.b);
    const Location there = locationOf(s.galaxy, target.planet);
    const DesignId troopA = design(s, ar.a, "Trooper A", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const DesignId troopC = design(s, ar.c, "Trooper C", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transportA = spawn(s, frigate(s, ar.a, "Transport A", 1, {"Test Cargo Bay"}), there);
    const VehicleId transportC = spawn(s, frigate(s, ar.c, "Transport C", 1, {"Test Cargo Bay"}), there);
    s.vehicle(transportA)->cargo.units.push_back({troopA, 4});
    s.vehicle(transportC)->cargo.units.push_back({troopC, 4});

    setTreaty(s, ar.a, ar.b, Treaty::NonAggression);
    CHECK(combat::landTroops(r, s, transportA, target.planet, troopA, 4) == 0);
    setTreaty(s, ar.a, ar.b, Treaty::War);
    CHECK(combat::landTroops(r, s, transportA, target.planet, troopA, 3) == 3);
    CHECK(s.vehicle(transportA)->cargo.unitCount(troopA) == 1);
    CHECK(target.cargo.unitCount(troopA) == 3);
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
    combat::runGroundCombat(ctx);
    const Colony* after = s.colony(planet);
    REQUIRE(after != nullptr);
    CHECK(after->owner == ar.a);
    CHECK_FALSE(after->homeworld);
    CHECK(after->queue.items.empty());
    CHECK(after->facilities.size() == facilities);   // facilities are kept
    CHECK(after->totalPopulation() == 20);             // the population now serves the captor
    CHECK(after->cargo.unitCount(trooper) > 0);
    CHECK(combat::invaders(r, s, *after).empty());
    CHECK(moodCount(ctx, ar.b, "Any Our Planet Captured") == 1);
    CHECK(moodCount(ctx, ar.b, "Homeworld Lost") == 1);
    CHECK(moodCount(ctx, ar.b, "Any Planet Lost") == 1);
    CHECK(moodCount(ctx, ar.a, "Any Enemy Planet Captured") == 1);
    CHECK(std::any_of(s.empire(ar.a).log.begin(), s.empire(ar.a).log.end(), [](const LogEntry& l) { return l.category == LogCategory::Combat; }));
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
    combat::runGroundCombat(ctx);
    const Colony* after = s.colony(planet);
    REQUIRE(after != nullptr);
    CHECK(after->owner == ar.b);
    CHECK(after->cargo.unitCount(trooper) == 0);
    CHECK(s.design(trooper).lost == 2);
    CHECK(combat::invaders(r, s, *after).empty());
    // Peace stops the fighting at once.
    target.cargo.units.push_back({trooper, 2});
    setTreaty(s, ar.a, ar.b, Treaty::NonAggression);
    TurnContext calm = context(s);
    combat::runGroundCombat(calm);
    CHECK(s.colony(planet)->cargo.unitCount(trooper) == 2);
}

TEST_CASE("combat: troop ships drop troops during a space battle") {
    Arena ar = makeArena();
    GameState& s = ar.s;
    useStrategy(s, ar.a, {{"Primary Movement Strategy", "Drop Troops (if carrying)"}, {"Secondary Movement Strategy", "Don't Get Hurt"}});
    Colony& target = homeworld(s, ar.b);
    const ObjectId planet = target.planet;
    const Location there = locationOf(s.galaxy, planet);
    target.population = {{ar.b, 20}};
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transport = spawn(s, frigate(s, ar.a, "Lander", 3, {"Test Cargo Bay"}), there);
    s.vehicle(transport)->cargo.units.push_back({trooper, 8});
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, there);
    CHECK(s.vehicle(transport)->cargo.unitCount(trooper) == 0);
    CHECK(s.colony(planet)->cargo.unitCount(trooper) == 8);
    REQUIRE(s.combats.size() == 1);
    CHECK(countEvents(s.combats.front(), CombatEvent::Kind::Launch) >= 1);
    combat::runGroundCombat(ctx);
    CHECK(s.colony(planet)->owner == ar.a);
}

// ---- Determinism --------------------------------------------------------------------------------------------

TEST_CASE("combat: battles are deterministic") {
    auto run = [] {
        Arena ar = makeArena(21);
        GameState& s = ar.s;
        const DesignId fighter = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun"});
        const VehicleId carrier = spawn(s, frigate(s, ar.a, "Carrier", 2, {"Test Fighter Bay", "Test Laser", "Test Shield"}), ar.loc);
        s.vehicle(carrier)->cargo.units.push_back({fighter, 3});
        spawn(s, frigate(s, ar.a, "Escort", 3, {"Test Laser", "Test Laser", "Test Armor Plate"}), ar.loc);
        spawn(s, frigate(s, ar.b, "Raider", 3, {"CT Torpedo", "Test Laser", "Test Armor Plate"}), ar.loc);
        spawn(s, frigate(s, ar.b, "Picket", 2, {"CT PD", "Test Disruptor", "Test Armor Plate"}), ar.loc);
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
    target.population = {{ar.b, 10}};
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
    CHECK(countEvents(s.combats.front(), CombatEvent::Kind::Fire) > 0);
    CHECK(countEvents(s.combats.front(), CombatEvent::Kind::Hit) > 0);
}

