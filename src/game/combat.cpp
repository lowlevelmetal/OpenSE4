// Combat rules shared by space and ground combat (docs/spec/04): settings,
// damage types, weapon target sets, strategies, to-hit, the damage pipeline,
// mines, and the combat-possible test. The battle itself is in
// combat_space.cpp, ground combat in combat_ground.cpp.

#include "game/combat.hpp"

#include "datafile/datafile.hpp"
#include "datafile/reader.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::combat {

namespace {

using ruleset::VehicleType;
using ruleset::WeaponKind;

// Lowercase letters and digits only, so "Seekers (On Us)" matches "Seekers(On Us)"
// and "Don't Get Hurt" matches "Dont Get Hurt".
std::string compact(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
    }
    return out;
}

constexpr std::array<std::string_view, static_cast<size_t>(DamageType::Count)> kDamageTypeIds{
    "Normal",
    "Shields Only",
    "Skips Normal Shields",
    "Skips All Shields",
    "Skips Armor",
    "Skips Shields And Armor",
    "Quad Damage To Shields",
    "Double Damage To Shields",
    "Half Damage To Shields",
    "Quarter Damage To Shields",
    "Only Engines",
    "Only Weapons",
    "Only Shield Generators",
    "Only Master Computers",
    "Only Boarding Parties",
    "Only Security Stations",
    "Only Planet Destroyers",
    "Increase Reload Time",
    "Disrupt Reload Time",
    "Crew Conversion",
    "Pushes Target",
    "Pulls Target",
    "Random Target Movement",
    "Plague Level 1",
    "Plague Level 2",
    "Plague Level 3",
    "Plague Level 4",
    "Plague Level 5",
    "Only Planet Population",
    "Only Planet Conditions",
    "Only Resupply Depots",
    "Only Spaceports",
};

constexpr std::array<std::string_view, kTargetCategories> kCategoryIds{
    "Planets", "Fighters", "Seekers(On Us)", "Seekers(On Others)", "Mines", "Carriers", "Colony Ships", "Transports",
    "Bases(No Weapons)", "Ships(No Weapons)", "Bases", "Ships", "Satellites", "Drones",
};

std::optional<MoveStrategy> parseMove(std::string_view text) {
    const std::string v = compact(text);
    if (v.empty()) return std::nullopt;
    if (v.starts_with("droptroops")) return MoveStrategy::DropTroops;
    if (v.find("dontgethurt") != std::string::npos) return MoveStrategy::DontGetHurt;
    if (v.find("maximum") != std::string::npos) return MoveStrategy::MaximumRange;
    if (v.find("optimal") != std::string::npos) return MoveStrategy::OptimalRange;
    if (v.find("short") != std::string::npos) return MoveStrategy::ShortRange;
    if (v.find("pointblank") != std::string::npos) return MoveStrategy::PointBlank;
    if (v.find("board") != std::string::npos || v.find("capture") != std::string::npos) return MoveStrategy::BoardEnemyShips;
    if (v.starts_with("ram")) return MoveStrategy::Ram;
    return std::nullopt;
}

std::optional<TargetKey> parseKey(std::string_view text) {
    const std::string v = compact(text);
    if (v.empty() || v == "none") return TargetKey::None;
    if (v == "nearest" || v == "closest") return TargetKey::Nearest;
    if (v == "farthest" || v == "furthest") return TargetKey::Farthest;
    if (v == "largest") return TargetKey::Largest;
    if (v == "smallest") return TargetKey::Smallest;
    if (v == "mostdamaged") return TargetKey::MostDamaged;
    if (v == "leastdamaged") return TargetKey::LeastDamaged;
    if (v == "fastest") return TargetKey::Fastest;
    if (v == "slowest") return TargetKey::Slowest;
    if (v == "strongest") return TargetKey::Strongest;
    if (v == "weakest") return TargetKey::Weakest;
    if (v == "hasweapons") return TargetKey::HasWeapons;
    if (v == "doesnothaveweapons" || v == "noweapons" || v == "hasnoweapons") return TargetKey::NoWeapons;
    return std::nullopt;
}

std::optional<TargetCategory> parseCategory(std::string_view compacted) {
    for (size_t i = 0; i < kCategoryIds.size(); ++i)
        if (compact(kCategoryIds[i]) == compacted) return static_cast<TargetCategory>(i);
    return std::nullopt;
}

bool flag(std::string_view text, bool fallback) {
    if (auto b = datafile::parseBoolean(text)) return *b;
    const std::string v = compact(text);
    if (v == "yes") return true;
    if (v == "no") return false;
    return fallback;
}

int number(std::string_view text, int fallback) {
    if (auto n = datafile::parseInteger(text)) return static_cast<int>(*n);
    return fallback;
}

// A mine's warheads that can hit this vehicle (entries of the mine design).
std::vector<size_t> mineWarheads(const Rules& r, const GameState& s, const Vehicle& mine, const Vehicle& victim,
                                 const detail::ShieldState& sh) {
    std::vector<size_t> out;
    const Design& d = s.design(mine.design);
    const uint8_t mask = targetMaskOf(detail::typeOf(r, s, victim));
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const ruleset::Component& c = r.component(d.entries[i].component);
        if (!c.isWeapon() || !entryIntact(r, s, mine, i)) continue;
        if (!(parseWeaponTargets(c.weapon.targets) & mask)) continue;
        if (weaponDamageAtRange(r, d.entries[i], 1) <= 0) continue;
        const DamageType t = parseDamageType(c.weapon.damageType);
        if (!detail::damageRule(t).structural || !detail::canAffectVehicle(r, s, victim, sh, t)) continue;
        out.push_back(i);
    }
    return out;
}

bool mineMayHit(const CombatSettings& cs, VehicleType t) {
    if (t == VehicleType::Fighter) return cs.fightersHitByMines;
    if (t == VehicleType::Drone) return cs.dronesHitByMines;
    return detail::canBePiece(t);
}

} // namespace

// ---- Settings ----------------------------------------------------------------------------------

CombatSettings loadSettings(const Rules& r) {
    CombatSettings c;
    auto get = [&](std::string_view key, int fallback) { return static_cast<int>(r.setting(key, fallback)); };
    c.spaceTurns = std::clamp(get("Number Of Space Combat Turns", 30), 1, 250);
    c.groundTurns = std::max(1, get("Number Of Ground Combat Turns", 10));
    c.baseToHit = get("Combat Base To Hit Value", 100);
    c.toHitPerSquare = get("Combat To Hit Modifier Per Square Distance", 10);
    c.seekerDefense = get("Seeker Combat Defense Modifier", 40);
    c.planetOffense = get("Planet Combat Offense Modifier", 30);
    c.planetDefense = get("Planet Combat Defense Modifier", -200);
    c.ramSourcePercent = get("Ram Ship Source Modifier Percent", 60);
    c.ramTargetPercent = get("Ram Ship Target Modifier Percent", 100);
    c.capturedReload = get("Captured Ship Additional Reload Combat Turns", 10);
    c.fighterGroup = std::max(1, get("Combat Fighter Group Amount", 20));
    c.mineGroup = std::max(1, get("Combat Mine Group Amount", 20));
    c.satelliteGroup = std::max(1, get("Combat Satellite Group Amount", 20));
    c.fightersHitByMines = r.settingFlag("Fighters Can Be Hit By Mines", true);
    c.dronesHitByMines = r.settingFlag("Drones Can Be Hit By Mines", true);
    c.defendingUnitsPerPopulation = std::max(1, get("Defending Units Per Population", 20));
    c.militiaAttack = get("Population Defender Attack Strength", 10);
    c.militiaHitPoints = std::max(1, get("Population Defender Hit Points", 30));
    c.groundDamagePercent = get("Ground Combat Damage Modifier Percent", 30);
    c.damagePerPopulation = std::max(1, get("Damage Points To Kill One Population", 10));
    c.createReplay = r.settingFlag("Create Combat Replay", true);
    return c;
}

// ---- Damage types and targets --------------------------------------------------------------------

std::string_view identifier(DamageType t) { return kDamageTypeIds[static_cast<size_t>(t)]; }

DamageType parseDamageType(std::string_view text) {
    const std::string v = compact(text);
    for (size_t i = 0; i < kDamageTypeIds.size(); ++i)
        if (compact(kDamageTypeIds[i]) == v) return static_cast<DamageType>(i);
    return DamageType::Normal;
}

bool isPlanetOnlyDamage(DamageType t) {
    switch (t) {
        case DamageType::PlagueLevel1:
        case DamageType::PlagueLevel2:
        case DamageType::PlagueLevel3:
        case DamageType::PlagueLevel4:
        case DamageType::PlagueLevel5:
        case DamageType::OnlyPlanetPopulation:
        case DamageType::OnlyPlanetConditions:
        case DamageType::OnlyResupplyDepots:
        case DamageType::OnlySpaceports: return true;
        default: return false;
    }
}

bool isSpecialEffect(DamageType t) {
    switch (t) {
        case DamageType::IncreaseReloadTime:
        case DamageType::DisruptReloadTime:
        case DamageType::CrewConversion:
        case DamageType::PushesTarget:
        case DamageType::PullsTarget:
        case DamageType::RandomTargetMovement: return true;
        default: return false;
    }
}

uint8_t parseWeaponTargets(std::span<const std::string> targets) {
    uint8_t mask = 0;
    auto add = [&](std::string_view token) {
        const std::string v = compact(token);
        if (v == "ships" || v == "ship") mask |= kTargetShips;
        else if (v == "planets" || v == "planet") mask |= kTargetPlanets;
        else if (v == "ftr" || v == "fighters" || v == "fighter") mask |= kTargetFighters;
        else if (v == "sat" || v == "satellites" || v == "satellite") mask |= kTargetSatellites;
        else if (v == "seekers" || v == "seeker") mask |= kTargetSeekers;
        else if (v == "drone" || v == "drones") mask |= kTargetDrones;
    };
    for (const std::string& t : targets) {
        size_t start = 0;
        for (size_t i = 0; i <= t.size(); ++i)
            if (i == t.size() || t[i] == '\\' || t[i] == ',' || t[i] == '/') {
                add(std::string_view(t).substr(start, i - start));
                start = i + 1;
            }
    }
    return mask;
}

uint8_t targetMaskOf(VehicleType t) {
    switch (t) {
        case VehicleType::Ship:
        case VehicleType::Base: return kTargetShips;
        case VehicleType::Fighter: return kTargetFighters;
        case VehicleType::Satellite: return kTargetSatellites;
        case VehicleType::Drone: return kTargetDrones;
        default: return 0;
    }
}

// ---- Strategies ----------------------------------------------------------------------------------

std::string_view identifier(TargetCategory c) { return kCategoryIds[static_cast<size_t>(c)]; }

std::string_view identifier(MoveStrategy m) {
    switch (m) {
        case MoveStrategy::DontGetHurt: return "Don't Get Hurt";
        case MoveStrategy::DropTroops: return "Drop Troops";
        case MoveStrategy::MaximumRange: return "Maximum Weapons Range";
        case MoveStrategy::OptimalRange: return "Optimal Weapons Range";
        case MoveStrategy::ShortRange: return "Short Weapons Range";
        case MoveStrategy::PointBlank: return "Point Blank";
        case MoveStrategy::BoardEnemyShips: return "Board Enemy Ships";
        case MoveStrategy::Ram: return "Ram";
    }
    return "?";
}

Strategy::Strategy() {
    // Our own neutral engagement order for records that leave the keys out:
    // armed warships and bases first, then planets and small craft, then the rest.
    using C = TargetCategory;
    constexpr std::array<std::pair<C, int>, kTargetCategories> order{{
        {C::Ships, 1}, {C::Bases, 2}, {C::Planets, 3}, {C::Drones, 4}, {C::Fighters, 5}, {C::SeekersOnUs, 6},
        {C::Satellites, 7}, {C::Carriers, 8}, {C::Transports, 9}, {C::ColonyShips, 10}, {C::ShipsNoWeapons, 11},
        {C::BasesNoWeapons, 12}, {C::SeekersOnOthers, 13}, {C::Mines, 14},
    }};
    for (const auto& [c, p] : order) typePriority[static_cast<size_t>(c)] = p;
}

Strategy parseStrategy(const ruleset::CombatStrategy& src) {
    Strategy st;
    st.name = src.name;
    for (const auto& [key, value] : src.settings) {
        const std::string k = compact(key);
        if (k == "primarymovementstrategy") {
            if (auto m = parseMove(value)) st.primary = *m;
        } else if (k == "secondarymovementstrategy") {
            if (auto m = parseMove(value)) st.secondary = *m;
        } else if (k.starts_with("targetingpriority") && k.size() > 17) {
            const int n = number(k.substr(17), 0);
            if (n >= 1 && n <= 4)
                if (auto t = parseKey(value)) st.targeting[static_cast<size_t>(n - 1)] = *t;
        } else if (k == "usetypepriorityfirst") {
            st.typePriorityFirst = flag(value, st.typePriorityFirst);
        } else if (k.starts_with("typepriority")) {
            if (auto c = parseCategory(k.substr(12))) st.typePriority[static_cast<size_t>(*c)] = number(value, 0);
        } else if (k.starts_with("dontfireon")) {
            if (auto c = parseCategory(k.substr(10))) st.dontFireOn[static_cast<size_t>(*c)] = flag(value, false);
        } else if (k.starts_with("breakformation")) {
            if (auto c = parseCategory(k.substr(14))) st.breakFormation[static_cast<size_t>(*c)] = flag(value, false);
        } else if (k == "fighterslaunchgroupamount") {
            st.fighterLaunchGroup = std::max(1, number(value, st.fighterLaunchGroup));
        } else if (k == "damagepercentpership") {
            st.damagePercentShip = number(value, st.damagePercentShip);
        } else if (k == "damagepercentperplanet") {
            st.damagePercentPlanet = number(value, st.damagePercentPlanet);
        } else if (k == "damagepercentperfightergroup") {
            st.damagePercentFighters = number(value, st.damagePercentFighters);
        } else if (k == "damagepercentpersatellitegroup") {
            st.damagePercentSatellites = number(value, st.damagePercentSatellites);
        } else if (k == "damageuntilallweaponsgone") {
            st.damageUntilWeaponsGone = flag(value, false);
        }
    }
    return st;
}

Strategy empireStrategy(const GameState& s, EmpireId e, uint32_t index) {
    if (!e.valid() || e.index() >= s.empires.size()) return Strategy{};
    const auto& list = s.empire(e).strategies;
    if (list.empty()) return Strategy{};
    return parseStrategy(list[index < list.size() ? index : 0]);
}

// ---- Helpers ---------------------------------------------------------------------------------------

namespace detail {

bool enemies(const GameState& s, EmpireId a, EmpireId b) { return hostile(s, a, b) || hostile(s, b, a); }

ruleset::VehicleType typeOf(const Rules& r, const GameState& s, const Vehicle& v) { return vehicleType(r, s, v); }

// Vehicles that can be combat pieces (mines, troops and platforms never are).
bool canBePiece(VehicleType t) { return t != VehicleType::Mine && t != VehicleType::Troop && t != VehicleType::WeaponPlatform; }

namespace {
bool sectorObscured(const GameState& s, Location where) {
    for (ObjectId o : s.galaxy.system(where.system).objects) {
        const SpaceObject& obj = s.galaxy.object(o);
        if (obj.sector != where.sector) continue;
        for (const auto& a : obj.abilities)
            if (parseAbilityKind(a.type) == AbilityKind::SectorSightObscuration) return true;
    }
    for (const auto& a : s.galaxy.system(where.system).abilities)
        if (parseAbilityKind(a.type) == AbilityKind::SectorSightObscuration) return true;
    return false;
}
} // namespace

// Whether `viewer` (which has pieces in the same sector) sees vehicle `v`.
// Uncloaked vehicles sharing a sector are seen unless a storm or nebula
// obscures it; otherwise the sight module decides (spec 04 §2).
bool visibleTo(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& v) {
    if (v.owner == viewer) return true;
    if (sight::canSeeVehicle(r, s, viewer, v)) return true;
    if (v.status == VehicleStatus::Cloaked) return false;
    return !sectorObscured(s, v.location);
}

int64_t componentSum(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k) {
    if (v.status == VehicleStatus::Mothballed) return 0;
    const Design& d = s.design(v.design);
    int64_t total = 0;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i)) total += sumValue1(r.componentAbilities(d.entries[i].component), k);
    return total;
}

int64_t componentBest(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k) {
    if (v.status == VehicleStatus::Mothballed) return 0;
    const Design& d = s.design(v.design);
    int64_t best = 0;
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const auto ab = r.componentAbilities(d.entries[i].component);
        if (hasAbility(ab, k) && entryIntact(r, s, v, i)) best = std::max(best, bestValue1(ab, k));
    }
    return best;
}

bool hasIntactComponent(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k) {
    if (v.status == VehicleStatus::Mothballed) return false;
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (hasAbility(r.componentAbilities(d.entries[i].component), k) && entryIntact(r, s, v, i)) return true;
    return false;
}

bool designHasComponent(const Rules& r, const Design& d, AbilityKind k) {
    return std::any_of(d.entries.begin(), d.entries.end(), [&](const DesignEntry& e) { return hasAbility(r.componentAbilities(e.component), k); });
}

int64_t hullSum(const Rules& r, const Design& d, AbilityKind k) { return sumValue1(r.hullAbilities(d.hull), k); }

bool vehicleArmed(const Rules& r, const GameState& s, const Vehicle& v) {
    if (v.status == VehicleStatus::Mothballed) return false;
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (r.component(d.entries[i].component).isWeapon() && entryIntact(r, s, v, i)) return true;
    return false;
}

bool needsSupply(const Rules& r, const GameState& s, const Vehicle& v) {
    if (hasIntactComponent(r, s, v, AbilityKind::QuantumReactor)) return false;
    const Design& d = s.design(v.design);
    int64_t capacity = hullSum(r, d, AbilityKind::SupplyStorage);
    for (const DesignEntry& e : d.entries) capacity += sumValue1(r.componentAbilities(e.component), AbilityKind::SupplyStorage);
    return capacity > 0;
}

int supplyPerShot(const Rules& r, const GameState& s, const Vehicle& v, const DesignEntry& e) {
    int64_t use = mounted(r, e).supplyUsed;
    if (v.owner.valid() && v.owner.index() < s.empires.size())
        use = use * (100 + r.traitValue(s.empire(v.owner).race, "Supply Cost")) / 100;
    return static_cast<int>(std::max<int64_t>(0, use));
}

int remainingStructure(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    int total = 0;
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const int st = entryStructure(r, d, i);
        const int dmg = i < v.damage.size() ? v.damage[i] : 0;
        total += std::max(0, st - dmg);
    }
    return total;
}

int clampChance(int chance) { return std::clamp(chance, 1, 99); }

int toHitChance(const CombatSettings& cs, int distance, int offense, int defense, int interference) {
    return clampChance(cs.baseToHit - cs.toHitPerSquare * std::max(0, distance) + offense - defense - interference);
}

int racialOffense(const Rules& r, const Empire& e) {
    const ruleset::Culture* c = r.culture(e.race);
    return (c ? c->spaceCombat : 0) + e.race.characteristic(Characteristic::Aggressiveness) - 100;
}

int racialDefense(const Rules& r, const Empire& e) {
    const ruleset::Culture* c = r.culture(e.race);
    return (c ? c->spaceCombat : 0) + e.race.characteristic(Characteristic::Defensiveness) - 100;
}

int systemModifier(const Rules& r, const GameState& s, EmpireId e, SystemId sys, AbilityKind k) {
    if (!e.valid() || !sys.valid()) return 0;
    int64_t best = 0;
    for (ObjectId o : s.galaxy.system(sys).objects) {
        const Colony* c = s.colony(o);
        if (!c || c->owner != e || c->totalPopulation() <= 0) continue;
        best = std::max(best, bestValue1(colonyAbilities(r, s, *c), k));
    }
    return static_cast<int>(best);
}

namespace {
int locationAbility(const GameState& s, Location where, AbilityKind sectorKind, std::optional<AbilityKind> systemKind) {
    int64_t total = 0;
    auto add = [&](const ruleset::Ability& a, bool sameSector) {
        const auto k = parseAbilityKind(a.type);
        if (!k) return;
        if ((*k == sectorKind && sameSector) || (systemKind && *k == *systemKind)) total += a.number1();
    };
    const StarSystem& sys = s.galaxy.system(where.system);
    for (ObjectId o : sys.objects) {
        const SpaceObject& obj = s.galaxy.object(o);
        for (const auto& a : obj.abilities) add(a, obj.sector == where.sector);
    }
    for (const auto& a : sys.abilities) add(a, true);  // system-type abilities cover every sector
    return static_cast<int>(total);
}
} // namespace

int sensorInterference(const GameState& s, Location where) {
    return locationAbility(s, where, AbilityKind::SectorSensorInterference, AbilityKind::SystemSensorInterference);
}

int shieldDisruption(const GameState& s, Location where) {
    return locationAbility(s, where, AbilityKind::SectorShieldDisruption, std::nullopt);
}

int fleetExperience(const GameState& s, const Vehicle& v) {
    if (!v.fleet.valid()) return 0;
    const Fleet* f = s.fleet(v.fleet);
    return f && f->owner == v.owner ? f->experience : 0;
}

int vehicleOffense(const Rules& r, const GameState& s, const Vehicle& v, bool unitGroup, int crewExperience) {
    const Design& d = s.design(v.design);
    int64_t o = componentBest(r, s, v, AbilityKind::CombatToHitOffensePlus) - componentBest(r, s, v, AbilityKind::CombatToHitOffenseMinus) +
                hullSum(r, d, AbilityKind::CombatToHitOffensePlus) - hullSum(r, d, AbilityKind::CombatToHitOffenseMinus);
    o += crewExperience;
    if (!unitGroup) o += fleetExperience(s, v);
    if (v.owner.valid() && v.owner.index() < s.empires.size()) o += racialOffense(r, s.empire(v.owner));
    return static_cast<int>(o);
}

int vehicleDefense(const Rules& r, const GameState& s, const Vehicle& v, bool unitGroup, int crewExperience) {
    const Design& d = s.design(v.design);
    int64_t o = componentBest(r, s, v, AbilityKind::CombatToHitDefensePlus) - componentBest(r, s, v, AbilityKind::CombatToHitDefenseMinus) +
                hullSum(r, d, AbilityKind::CombatToHitDefensePlus) - hullSum(r, d, AbilityKind::CombatToHitDefenseMinus);
    o += crewExperience;
    if (!unitGroup) o += fleetExperience(s, v);
    if (v.owner.valid() && v.owner.index() < s.empires.size()) o += racialDefense(r, s.empire(v.owner));
    return static_cast<int>(o);
}

// ---- Damage pipeline -----------------------------------------------------------------------------------

DamageRule damageRule(DamageType t) {
    using S = DamageRule::Shields;
    DamageRule d;
    switch (t) {
        case DamageType::Normal: break;
        case DamageType::ShieldsOnly: d.shieldsOnly = true; break;
        case DamageType::SkipsNormalShields: d.shields = S::PhasedOnly; break;
        case DamageType::SkipsAllShields: d.shields = S::None; break;
        case DamageType::SkipsArmor: d.skipsArmor = true; break;
        case DamageType::SkipsShieldsAndArmor:
            d.shields = S::None;
            d.skipsArmor = true;
            break;
        case DamageType::QuadDamageToShields: d.shieldNum = 4; break;
        case DamageType::DoubleDamageToShields: d.shieldNum = 2; break;
        case DamageType::HalfDamageToShields: d.shieldDen = 2; break;
        case DamageType::QuarterDamageToShields: d.shieldDen = 4; break;
        case DamageType::OnlyEngines: d.only = Layer::Engines; break;  // shields still absorb (history 1.70)
        case DamageType::OnlyWeapons:
            d.shields = S::None;
            d.only = Layer::Weapons;
            break;
        case DamageType::OnlyShieldGenerators:
            d.shields = S::None;
            d.only = Layer::ShieldGenerators;
            break;
        case DamageType::OnlyMasterComputers:
            d.shields = S::None;
            d.only = Layer::MasterComputers;
            break;
        case DamageType::OnlyBoardingParties:
            d.shields = S::None;
            d.only = Layer::BoardingParties;
            break;
        case DamageType::OnlySecurityStations:
            d.shields = S::None;
            d.only = Layer::SecurityStations;
            break;
        case DamageType::OnlyPlanetDestroyers:
            d.shields = S::None;
            d.only = Layer::PlanetDestroyers;
            break;
        default: d.structural = false; break;  // special effects and planet-only types
    }
    return d;
}

void refreshShields(const Rules& r, const GameState& s, const Vehicle& v, ShieldState& sh, bool fill) {
    int64_t normal = 0, phased = 0;
    if (v.status != VehicleStatus::Mothballed && !(needsSupply(r, s, v) && v.supply <= 0)) {
        const auto abilities = vehicleAbilities(r, s, v);   // mount Shield Percent already applied
        normal = sumValue1(abilities, AbilityKind::ShieldGeneration) + sh.bonus;
        phased = sumValue1(abilities, AbilityKind::PhasedShieldGeneration);
        if (normal < 0) {
            phased = std::max<int64_t>(0, phased + normal);
            normal = 0;
        }
    }
    sh.maxNormal = static_cast<int>(normal);
    sh.maxPhased = static_cast<int>(phased);
    if (fill) {
        sh.normal = sh.maxNormal;
        sh.phased = sh.maxPhased;
    } else {
        sh.normal = std::min(sh.normal, sh.maxNormal);
        sh.phased = std::min(sh.phased, sh.maxPhased);
    }
}

int absorbShields(ShieldState& sh, int damage, const DamageRule& rule) {
    using S = DamageRule::Shields;
    if (rule.shields == S::None || damage <= 0) return damage;
    int64_t effective = int64_t{damage} * rule.shieldNum / rule.shieldDen;
    int64_t drained = 0;
    if (rule.shields == S::Both) {
        const int64_t n = std::min<int64_t>(sh.normal, effective);
        sh.normal -= static_cast<int>(n);
        effective -= n;
        drained += n;
    }
    const int64_t p = std::min<int64_t>(sh.phased, effective);
    sh.phased -= static_cast<int>(p);
    drained += p;
    if (rule.shieldsOnly) return 0;
    const bool left = sh.phased > 0 || (rule.shields == S::Both && sh.normal > 0);
    if (left) return 0;  // the shields held
    const int64_t rawUsed = (drained * rule.shieldDen + rule.shieldNum - 1) / rule.shieldNum;  // (inferred) rounded up
    return static_cast<int>(std::max<int64_t>(0, damage - rawUsed));
}

namespace {
bool inLayer(const Rules& r, uint32_t component, Layer layer) {
    const auto ab = r.componentAbilities(component);
    switch (layer) {
        case Layer::Armor: return hasAbility(ab, AbilityKind::Armor);
        case Layer::Internal: return !hasAbility(ab, AbilityKind::Armor);
        case Layer::Engines: return hasAbility(ab, AbilityKind::StandardShipMovement);
        case Layer::Weapons: return r.component(component).isWeapon();
        case Layer::ShieldGenerators:
            return hasAbility(ab, AbilityKind::ShieldGeneration) || hasAbility(ab, AbilityKind::PhasedShieldGeneration);
        case Layer::MasterComputers: return hasAbility(ab, AbilityKind::MasterComputer);
        case Layer::BoardingParties: return hasAbility(ab, AbilityKind::BoardingAttack);
        case Layer::SecurityStations: return hasAbility(ab, AbilityKind::BoardingDefense);
        case Layer::PlanetDestroyers: return hasAbility(ab, AbilityKind::DestroyPlanetSize);
    }
    return false;
}
} // namespace

int damageLayer(const Rules& r, const GameState& s, Vehicle& v, Layer layer, int amount, Rng& rng) {
    const Design& d = s.design(v.design);
    if (v.damage.size() < d.entries.size()) v.damage.resize(d.entries.size(), 0);
    int used = 0;
    std::vector<size_t> candidates;
    while (amount > 0) {
        candidates.clear();
        for (size_t i = 0; i < d.entries.size(); ++i)
            if (entryIntact(r, s, v, i) && inLayer(r, d.entries[i].component, layer)) candidates.push_back(i);
        if (candidates.empty()) break;
        // (inferred) the next component is picked uniformly among the intact ones (spec 04 §19 Q6).
        const size_t pick = candidates[rng.below(candidates.size())];
        const int room = entryStructure(r, d, pick) - v.damage[pick];
        const int hit = std::min(room, amount);
        v.damage[pick] += hit;
        amount -= hit;
        used += hit;
    }
    return used;
}

HitOutcome hitUnit(const Rules& r, const GameState& s, Vehicle& unit, ShieldState& sh, int damage, DamageType type, Rng& rng) {
    HitOutcome out;
    const DamageRule rule = damageRule(type);
    if (!rule.structural || damage <= 0) return out;
    const int before = sh.normal + sh.phased;
    int rem = absorbShields(sh, std::min(damage, kMaxShotDamage), rule);
    out.shieldDamage = before - (sh.normal + sh.phased);
    if (rem <= 0) return out;

    if (rule.only) {
        const int used = damageLayer(r, s, unit, *rule.only, rem, rng);
        out.structureDamage += used;
        rem -= used;
    } else if (rule.skipsArmor) {
        // (inferred) with only armor left, armor-skipping damage is lost.
        const int used = damageLayer(r, s, unit, Layer::Internal, rem, rng);
        out.structureDamage += used;
        rem -= used;
    } else {
        // Emissive armor ignores a hit no larger than its value (inferred: after shields, largest value counts).
        const int64_t emissive = std::max(componentBest(r, s, unit, AbilityKind::EmissiveArmor),
                                          hullSum(r, s.design(unit.design), AbilityKind::EmissiveArmor));
        if (rem <= emissive) return out;
        // Crystalline armor turns part of the hit into shield points (inferred: capped at the maximum).
        const int64_t crystal = componentSum(r, s, unit, AbilityKind::ShieldGenerationFromDamage);
        if (crystal > 0) {
            const int conv = static_cast<int>(std::min<int64_t>({rem, crystal, std::max(0, sh.maxNormal - sh.normal)}));
            sh.normal += conv;
            rem -= conv;
        }
        int used = damageLayer(r, s, unit, Layer::Armor, rem, rng);
        out.structureDamage += used;
        rem -= used;
        used = damageLayer(r, s, unit, Layer::Internal, rem, rng);
        out.structureDamage += used;
        rem -= used;
    }
    out.destroyed = vehicleDestroyed(r, s, unit);
    if (out.destroyed) out.excess = std::max(0, rem);
    else refreshShields(r, s, unit, sh, false);  // lost generators lower the maximum (inferred: current is capped)
    return out;
}

bool canAffectVehicle(const Rules& r, const GameState& s, const Vehicle& v, const ShieldState& sh, DamageType type) {
    if (isPlanetOnlyDamage(type)) return false;
    const DamageRule rule = damageRule(type);
    const ruleset::VehicleType vt = typeOf(r, s, v);
    switch (type) {
        case DamageType::CrewConversion:
            // Ships only (history 1.80); any Master Computer, even destroyed, blocks it (history 1.81).
            return vt == ruleset::VehicleType::Ship && !designHasComponent(r, s.design(v.design), AbilityKind::MasterComputer);
        case DamageType::IncreaseReloadTime:
            return vehicleArmed(r, s, v) && !hasIntactComponent(r, s, v, AbilityKind::MasterComputer);
        case DamageType::DisruptReloadTime: return vehicleArmed(r, s, v);
        case DamageType::PushesTarget:
        case DamageType::PullsTarget:
        case DamageType::RandomTargetMovement: return true;
        default: break;
    }
    if (rule.shieldsOnly) return sh.normal + sh.phased > 0;
    if (rule.only) {
        const Design& d = s.design(v.design);
        for (size_t i = 0; i < d.entries.size(); ++i)
            if (entryIntact(r, s, v, i) && inLayer(r, d.entries[i].component, *rule.only)) return true;
        return false;
    }
    return true;
}

void restoreRegeneratingArmor(const Rules& r, const GameState& s, Vehicle& v) {
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size() && i < v.damage.size(); ++i)
        if (hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::ArmorRegeneration)) v.damage[i] = 0;
}

Forces battleForces(const Rules& r, const GameState& s, Location where) {
    Forces f;
    if (!where.system.valid() || where.system.index() >= s.galaxy.systems.size()) return f;
    std::vector<const Vehicle*> vehicles;
    std::vector<EmpireId> present;
    for (const Vehicle& v : s.vehicles)
        if (v.location == where && v.count > 0 && v.owner.valid() && canBePiece(typeOf(r, s, v))) {
            vehicles.push_back(&v);
            present.push_back(v.owner);
        }
    for (ObjectId o : planetsAt(s, where))
        if (const Colony* c = s.colony(o)) {
            f.colonies.push_back(o);
            present.push_back(c->owner);
        }
    std::sort(present.begin(), present.end());
    present.erase(std::unique(present.begin(), present.end()), present.end());
    // A vehicle joins when a hostile empire present sees it; planets cannot hide.
    std::vector<EmpireId> active;
    for (const Vehicle* v : vehicles)
        for (EmpireId b : present)
            if (b != v->owner && enemies(s, b, v->owner) && visibleTo(r, s, b, *v)) {
                f.vehicles.push_back(v->id);
                active.push_back(v->owner);
                break;
            }
    for (ObjectId o : f.colonies) active.push_back(s.colony(o)->owner);
    std::sort(active.begin(), active.end());
    active.erase(std::unique(active.begin(), active.end()), active.end());
    for (EmpireId a : active)
        for (EmpireId b : active)
            if (a != b && enemies(s, a, b)) {
                f.empires.push_back(a);
                break;
            }
    auto participates = [&](EmpireId e) { return std::binary_search(f.empires.begin(), f.empires.end(), e); };
    std::erase_if(f.vehicles, [&](VehicleId id) { return !participates(s.vehicle(id)->owner); });
    std::erase_if(f.colonies, [&](ObjectId o) { return !participates(s.colony(o)->owner); });
    return f;
}

std::string sectorName(const GameState& s, Location where) {
    const std::string& name = where.system.valid() && where.system.index() < s.galaxy.systems.size() ? s.galaxy.system(where.system).name
                                                                                                    : std::string("?");
    return std::format("{} ({}, {})", name, where.sector.x, where.sector.y);
}

// ---- Mines (spec 04 §10.6) ---------------------------------------------------------------------------

bool minesCanStrike(const Rules& r, const GameState& s, Location where) {
    const CombatSettings cs = loadSettings(r);
    const auto here = s.vehiclesAt(where);
    for (const Vehicle* m : here) {
        if (m->count <= 0 || typeOf(r, s, *m) != VehicleType::Mine) continue;
        for (const Vehicle* v : here) {
            if (v->count <= 0 || !mineMayHit(cs, typeOf(r, s, *v)) || !enemies(s, m->owner, v->owner)) continue;
            ShieldState sh;
            sh.bonus = -shieldDisruption(s, where);
            refreshShields(r, s, *v, sh, true);
            if (!mineWarheads(r, s, *m, *v, sh).empty()) return true;
        }
    }
    return false;
}

void resolveMines(TurnContext& ctx, Location where, Rng& rng) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const CombatSettings cs = loadSettings(r);
    std::vector<VehicleId> mines, victims;
    for (const Vehicle& v : s.vehicles) {
        if (v.location != where || v.count <= 0 || !v.owner.valid()) continue;
        if (typeOf(r, s, v) == VehicleType::Mine) mines.push_back(v.id);
    }
    if (mines.empty()) return;
    for (const Vehicle& v : s.vehicles) {
        if (v.location != where || v.count <= 0 || !v.owner.valid() || !mineMayHit(cs, typeOf(r, s, v))) continue;
        for (VehicleId m : mines)
            if (enemies(s, s.vehicle(m)->owner, v.owner)) {
                victims.push_back(v.id);
                break;
            }
    }
    if (victims.empty()) return;

    // Sweepers of each victim empire clear mines first.
    std::vector<EmpireId> sweeping;
    for (VehicleId id : victims) sweeping.push_back(s.vehicle(id)->owner);
    std::sort(sweeping.begin(), sweeping.end());
    sweeping.erase(std::unique(sweeping.begin(), sweeping.end()), sweeping.end());
    for (EmpireId e : sweeping) {
        int64_t capacity = 0;
        for (const Vehicle& v : s.vehicles)
            if (v.location == where && v.owner == e && v.count > 0) capacity += componentSum(r, s, v, AbilityKind::MineSweeping) * v.count;
        int swept = 0;
        for (VehicleId m : mines) {
            Vehicle* mv = s.vehicle(m);
            if (capacity <= 0) break;
            if (!enemies(s, mv->owner, e)) continue;
            const int n = static_cast<int>(std::min<int64_t>(capacity, mv->count));
            mv->count -= n;
            capacity -= n;
            swept += n;
            if (n > 0)
                ctx.log(mv->owner, LogCategory::Combat, std::format("Mines swept at {}", sectorName(s, where)),
                        std::format("{} of our mines were cleared by enemy sweepers.", n), where);
        }
        if (swept > 0)
            ctx.log(e, LogCategory::Combat, std::format("Mines swept at {}", sectorName(s, where)),
                    std::format("Our sweepers cleared {} enemy mines.", swept), where);
    }

    for (VehicleId id : victims) {
        Vehicle* v = s.vehicle(id);
        if (!v || v->count <= 0) continue;
        ShieldState sh;
        sh.bonus = -shieldDisruption(s, where);
        refreshShields(r, s, *v, sh, true);   // (inferred) shields are up when the mines strike
        const bool unit = isUnitType(typeOf(r, s, *v));
        int strikes = 0, unitsLost = 0;
        for (VehicleId mid : mines) {
            Vehicle* m = s.vehicle(mid);
            if (m->count <= 0 || !enemies(s, m->owner, v->owner)) continue;
            const std::vector<size_t> warheads = mineWarheads(r, s, *m, *v, sh);
            if (warheads.empty()) continue;   // a mine never detonates against a target it cannot hurt (history 1.70)
            const Design& md = s.design(m->design);
            const int lostBefore = unitsLost;
            int used = 0;
            while (m->count > 0 && v->count > 0) {
                for (size_t w : warheads) {
                    int dmg = std::min(kMaxShotDamage, weaponDamageAtRange(r, md.entries[w], 1));
                    const DamageType t = parseDamageType(r.component(md.entries[w].component).weapon.damageType);
                    while (dmg > 0 && v->count > 0) {
                        const HitOutcome o = hitUnit(r, s, *v, sh, dmg, t, rng);
                        if (!o.destroyed) break;
                        --v->count;
                        ++unitsLost;
                        if (v->count <= 0) break;
                        v->damage.assign(s.design(v->design).entries.size(), 0);
                        refreshShields(r, s, *v, sh, true);
                        dmg = o.excess;
                    }
                    if (v->count <= 0) break;
                }
                --m->count;
                ++used;
                ++strikes;
            }
            if (used > 0) {
                s.design(m->design).kills += unitsLost - lostBefore;
                ctx.log(m->owner, LogCategory::Combat, std::format("Mines detonated at {}", sectorName(s, where)),
                        std::format("{} of our mines struck {}{}.", used, v->name, v->count <= 0 ? ", destroying it" : ""), where);
            }
            if (v->count <= 0) break;
        }
        if (strikes == 0) continue;
        s.design(v->design).lost += unitsLost;
        ctx.log(v->owner, LogCategory::Combat, std::format("Mines at {}", sectorName(s, where)),
                std::format("{} was struck by {} enemy mines{}.", v->name, strikes, v->count <= 0 ? " and destroyed" : ""), where);
        if (unitsLost > 0 && !unit) {
            ctx.mood(v->owner, "Any Ship Lost", {}, {}, unitsLost);
            ctx.mood(v->owner, "Ship Lost in System", where.system, {}, unitsLost);
        }
    }
}

} // namespace detail

// ---- Entry points ------------------------------------------------------------------------------------

bool combatPossible(const Rules& r, const GameState& s, Location where) {
    if (!where.system.valid() || where.system.index() >= s.galaxy.systems.size()) return false;
    if (detail::minesCanStrike(r, s, where)) return true;
    return detail::battleForces(r, s, where).empires.size() >= 2;
}

int toHitPercent(const Rules& r, const GameState& s, const Vehicle& attacker, size_t weaponEntry, const Vehicle& defender, int range) {
    const Design& d = s.design(attacker.design);
    if (weaponEntry >= d.entries.size()) return 0;
    const ruleset::Component& c = r.component(d.entries[weaponEntry].component);
    if (!c.isWeapon()) return 0;
    if (c.weapon.kind == WeaponKind::Seeking || c.weapon.kind == WeaponKind::Warhead) return 100;
    if (c.weapon.kind == WeaponKind::DirectFire && detail::hasIntactComponent(r, s, attacker, AbilityKind::WeaponsAlwaysHit)) return 100;
    const CombatSettings cs = loadSettings(r);
    const bool aUnit = isUnitType(detail::typeOf(r, s, attacker));
    const bool dUnit = isUnitType(detail::typeOf(r, s, defender));
    const int offense = detail::vehicleOffense(r, s, attacker, aUnit, attacker.experience) + mounted(r, d.entries[weaponEntry]).toHitModifier +
                        detail::systemModifier(r, s, attacker.owner, attacker.location.system, AbilityKind::CombatModifierSystem);
    const int defense = detail::vehicleDefense(r, s, defender, dUnit, defender.experience) +
                        detail::systemModifier(r, s, defender.owner, defender.location.system, AbilityKind::CombatModifierSystem);
    return detail::toHitChance(cs, range, offense, defense, detail::sensorInterference(s, attacker.location));
}

// ---- Troops ----------------------------------------------------------------------------------------------

bool isTroopDesign(const Rules& r, const GameState& s, DesignId d) {
    return d.valid() && d.index() < s.designs.size() && r.hull(s.design(d).hull).type == VehicleType::Troop;
}

std::vector<EmpireId> invaders(const Rules& r, const GameState& s, const Colony& c) {
    std::vector<EmpireId> out;
    for (const UnitStack& u : c.cargo.units) {
        if (u.count <= 0 || !isTroopDesign(r, s, u.design)) continue;
        const EmpireId owner = s.design(u.design).owner;
        if (owner != c.owner && detail::enemies(s, owner, c.owner)) out.push_back(owner);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int landTroops(const Rules& r, GameState& s, VehicleId carrier, ObjectId planet, DesignId design, int count) {
    Vehicle* v = s.vehicle(carrier);
    Colony* c = s.colony(planet);
    if (!v || !c || count <= 0 || !isTroopDesign(r, s, design)) return 0;
    if (locationOf(s.galaxy, planet) != v->location) return 0;
    const EmpireId troopOwner = s.design(design).owner;
    if (troopOwner == c->owner || !detail::enemies(s, troopOwner, c->owner)) return 0;
    for (EmpireId e : invaders(r, s, *c))
        if (e != troopOwner) return 0;   // already contested by another empire (spec 04 §13)
    auto it = std::find_if(v->cargo.units.begin(), v->cargo.units.end(), [&](const UnitStack& u) { return u.design == design; });
    if (it == v->cargo.units.end() || it->count <= 0) return 0;
    const int n = std::min(count, it->count);
    it->count -= n;
    if (it->count <= 0) v->cargo.units.erase(it);
    auto dst = std::find_if(c->cargo.units.begin(), c->cargo.units.end(), [&](const UnitStack& u) { return u.design == design; });
    if (dst != c->cargo.units.end()) dst->count += n;
    else c->cargo.units.push_back({design, n});
    return n;
}

int militiaCount(const CombatSettings& cs, int64_t populationMillions) {
    if (populationMillions <= 0) return 0;
    return static_cast<int>(std::max<int64_t>(1, populationMillions / std::max(1, cs.defendingUnitsPerPopulation)));
}

} // namespace opense4::game::combat
