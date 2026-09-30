// Combat rules shared by space and ground combat (docs/spec/04): settings,
// damage types, weapon target sets, strategies, damage at range and mounts,
// to-hit terms, the damage pipeline of ships and units, mines, and the
// combat-possible test. The battle itself is in combat_space.cpp, ground
// combat in combat_ground.cpp.
//
// Rules marked "(confirmed: binary)" follow the spec text of the same name;
// "(inferred)" marks our own choices where the spec is silent. Percentages the
// original applies in floating point go through game/xmath.hpp.

#include "game/combat.hpp"

#include "datafile/datafile.hpp"
#include "datafile/reader.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <climits>
#include <deque>
#include <format>
#include <map>

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

bool mineMayHit(const CombatSettings& cs, VehicleType t) {
    if (t == VehicleType::Fighter) return cs.fightersHitByMines;
    if (t == VehicleType::Drone) return cs.dronesHitByMines;
    return detail::canBePiece(t);
}

// A mine's warheads (entries of the mine design) whose damage type can affect this vehicle (spec 04 §10.6).
std::vector<size_t> mineWarheads(const Rules& r, const GameState& s, const Vehicle& mine, const Vehicle& victim) {
    std::vector<size_t> out;
    const Design& d = s.design(mine.design);
    const detail::ShieldState none;   // shields never act against mines
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const ruleset::Component& c = r.component(d.entries[i].component);
        if (c.weapon.kind != WeaponKind::Warhead || !entryIntact(r, s, mine, i)) continue;
        if (weaponLargestDamage(r, d.entries[i]) <= 0) continue;
        const DamageType t = parseDamageType(c.weapon.damageType);
        if (!detail::damageRule(t).structural || !detail::canAffectVehicle(r, s, victim, none, t)) continue;
        out.push_back(i);
    }
    return out;
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

bool isHullDamaging(DamageType t) { return detail::damageRule(t).hullDamaging; }

uint8_t parseWeaponTargets(std::span<const std::string> targets) {
    // `Weapon Target` joins fixed names with '\'; a list override contains the
    // category words in free text (confirmed: binary). Both are matched by word.
    uint8_t mask = 0;
    auto add = [&](std::string_view token) {
        const std::string v = compact(token);
        if (v.empty()) return;
        if (v == "all") mask |= kTargetShips | kTargetPlanets | kTargetFighters | kTargetSatellites | kTargetSeekers | kTargetDrones;
        if (v.starts_with("ship")) mask |= kTargetShips;
        if (v.starts_with("planet")) mask |= kTargetPlanets;
        if (v == "ftr" || v.starts_with("fighter")) mask |= kTargetFighters;
        if (v == "sat" || v.starts_with("satellite")) mask |= kTargetSatellites;
        if (v.starts_with("seeker")) mask |= kTargetSeekers;
        if (v.starts_with("drone")) mask |= kTargetDrones;
    };
    for (const std::string& t : targets) {
        size_t start = 0;
        for (size_t i = 0; i <= t.size(); ++i)
            if (i == t.size() || t[i] == '\\' || t[i] == ',' || t[i] == '/' || t[i] == ' ') {
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

// ---- Damage at range (spec 04 §8) ----------------------------------------------------------------

int weaponDamage(const Rules& r, const DesignEntry& e, int range) {
    const ruleset::Component& c = r.component(e.component);
    if (!c.isWeapon()) return 0;
    const std::vector<int>& table = c.weapon.damageAtRange;
    int64_t value = 0;
    if (detail::mountApplies(r, e)) {
        // With a valid mount the index is clamped, so ranges past the table read entry 20 (confirmed: binary).
        const MountedComponent m = mounted(r, e);
        const int index = std::clamp(range - m.rangeModifier, 1, 20);
        if (index > static_cast<int>(table.size())) return 0;
        value = xmath::pctRound(table[static_cast<size_t>(index - 1)], m.damagePercent);
    } else {
        if (range < 1 || range > 20 || range > static_cast<int>(table.size())) return 0;
        value = table[static_cast<size_t>(range - 1)];
    }
    return static_cast<int>(std::clamp<int64_t>(value, 0, kMaxShotDamage));
}

int weaponLargestDamage(const Rules& r, const DesignEntry& e) {
    const ruleset::Component& c = r.component(e.component);
    if (!c.isWeapon()) return 0;
    int64_t best = 0;
    const size_t n = std::min<size_t>(20, c.weapon.damageAtRange.size());
    for (size_t i = 0; i < n; ++i) best = std::max<int64_t>(best, c.weapon.damageAtRange[i]);
    if (detail::mountApplies(r, e)) best = xmath::pctRound(best, mounted(r, e).damagePercent);
    return static_cast<int>(std::clamp<int64_t>(best, 0, kMaxShotDamage));
}

int weaponReach(const Rules& r, const DesignEntry& e) {
    int reach = 0;
    for (int d = 1; d <= kCombatMapWidth; ++d)
        if (weaponDamage(r, e, d) > 0) reach = d;
    return reach;
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
        } else if (k == "dronespertarget") {
            st.dronesPerTarget = std::max(1, number(value, st.dronesPerTarget));
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

// Families of the intact components of a vehicle that have ability k, with their best Value 1.
template <class Each>
void forIntactParts(const Rules& r, const GameState& s, const Vehicle& v, Each&& each) {
    if (v.status == VehicleStatus::Mothballed) return;
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i)) each(i, d.entries[i]);
    // The other designs of a unit group that mixes them: one unit of each, whole.
    for (size_t k = 1; k < v.mixed.size(); ++k) {
        const Design& m = s.design(v.mixed[k].design);
        for (size_t i = 0; i < m.entries.size(); ++i) each(i, m.entries[i]);
    }
}
} // namespace

bool visibleTo(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& v) {
    if (v.owner == viewer) return true;
    if (sight::canSeeVehicle(r, s, viewer, v)) return true;
    if (v.status == VehicleStatus::Cloaked) return false;
    return !sectorObscured(s, v.location);
}

int64_t componentSum(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k) {
    int64_t total = 0;
    forIntactParts(r, s, v, [&](size_t, const DesignEntry& e) { total += sumValue1(r.componentAbilities(e.component), k); });
    return total;
}

int64_t componentBest(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k) {
    int64_t best = 0;
    forIntactParts(r, s, v, [&](size_t, const DesignEntry& e) {
        const auto ab = r.componentAbilities(e.component);
        if (hasAbility(ab, k)) best = std::max(best, bestValue1(ab, k));
    });
    return best;
}

int64_t facilityFamilyBest(const Rules& r, std::span<const uint32_t> facilities, AbilityKind k) {
    std::map<int, int64_t> best;
    for (uint32_t f : facilities) {
        const auto ab = r.facilityAbilities(f);
        if (!hasAbility(ab, k)) continue;
        const int family = r.facility(f).family;
        const int64_t value = bestValue1(ab, k);
        auto it = best.find(family);
        if (it == best.end()) best.emplace(family, value);
        else it->second = std::max(it->second, value);
    }
    int64_t total = 0;
    for (const auto& [family, value] : best) total += value;
    return total;
}

bool hasIntactComponent(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k) {
    bool found = false;
    forIntactParts(r, s, v, [&](size_t, const DesignEntry& e) { found = found || hasAbility(r.componentAbilities(e.component), k); });
    return found;
}

bool designHasComponent(const Rules& r, const Design& d, AbilityKind k) {
    return std::any_of(d.entries.begin(), d.entries.end(), [&](const DesignEntry& e) { return hasAbility(r.componentAbilities(e.component), k); });
}

int64_t hullSum(const Rules& r, const Design& d, AbilityKind k) { return sumValue1(r.hullAbilities(d.hull), k); }

bool vehicleArmed(const Rules& r, const GameState& s, const Vehicle& v) {
    bool armed = false;
    forIntactParts(r, s, v, [&](size_t, const DesignEntry& e) {
        const ruleset::Component& c = r.component(e.component);
        armed = armed || (c.isWeapon() && c.weapon.kind != WeaponKind::Warhead);
    });
    return armed;
}

// ---- Mounts ------------------------------------------------------------------------------------------

bool mountApplies(const Rules& r, const DesignEntry& e) {
    // The damage, range, structure and shield effects apply only to components
    // that meet the mount's weapon type and family requirements (spec 03 §4.3,
    // spec 04 §18.2). One implementation: game::mountApplies (design.hpp).
    return e.mount >= 0 && game::mountApplies(r, e.component, static_cast<uint32_t>(e.mount));
}

int combatStructure(const Rules& r, const DesignEntry& e) {
    const int base = r.component(e.component).structure;
    if (!mountApplies(r, e)) return base;
    const ruleset::WeaponMount& m = r.data().weaponMounts[static_cast<size_t>(e.mount)];
    return static_cast<int>(xmath::pctRound(base, m.structurePercent));
}

int64_t mountedShield(const Rules& r, const DesignEntry& e, AbilityKind k) {
    const int64_t value = sumValue1(r.componentAbilities(e.component), k);
    if (value == 0 || !mountApplies(r, e)) return value;
    return xmath::pctRound(value, mounted(r, e).shieldPercent);
}

// ---- Supply ------------------------------------------------------------------------------------------

bool unlimitedSupply(const Rules& r, const GameState& s, const Vehicle& v) {
    return typeOf(r, s, v) == VehicleType::Base || hasIntactComponent(r, s, v, AbilityKind::QuantumReactor);
}

bool usesSupply(const Rules& r, const GameState& s, const Vehicle& v) {
    const VehicleType t = typeOf(r, s, v);
    if (t != VehicleType::Ship && t != VehicleType::Fighter) return false;
    return !unlimitedSupply(r, s, v);
}

bool hasSupplies(const Rules& r, const GameState& s, const Vehicle& v) { return !usesSupply(r, s, v) || v.supply > 0; }

int supplyPerShot(const Rules& r, const DesignEntry& e) {
    const int base = r.component(e.component).supplyUsed;
    if (!mountApplies(r, e)) return std::max(0, base);
    const ruleset::WeaponMount& m = r.data().weaponMounts[static_cast<size_t>(e.mount)];
    return static_cast<int>(std::max<int64_t>(0, xmath::pctRound(base, m.supplyPercent)));
}

int remainingStructure(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    int total = 0;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i)) total += combatStructure(r, d.entries[i]);
    return total;
}

int designStructure(const Rules& r, const Design& d) {
    int total = 0;
    for (const DesignEntry& e : d.entries) total += combatStructure(r, e);
    return total;
}

// ---- To-hit ------------------------------------------------------------------------------------------

int clampChance(int chance) { return std::clamp(chance, 1, 99); }

int toHitChance(const CombatSettings& cs, int aimDistance, int offense, int defense, int interference) {
    // One clamp, after every modifier (history 1.56).
    return clampChance(cs.baseToHit + offense - defense - cs.toHitPerSquare * std::max(0, aimDistance) - interference);
}

// Racial traits that change combat to-hit would add here too; the stock data has none.
int racialOffense(const Rules& r, const Empire& e) {
    const ruleset::Culture* c = r.culture(e.race);
    return (c ? c->spaceCombat : 0) + e.race.characteristic(Characteristic::Aggressiveness) - 100;
}

int racialDefense(const Rules& r, const Empire& e) {
    const ruleset::Culture* c = r.culture(e.race);
    return (c ? c->spaceCombat : 0) + e.race.characteristic(Characteristic::Defensiveness) - 100;
}

int systemModifier(const Rules& r, const GameState& s, EmpireId e, SystemId sys, AbilityKind k) {
    if (!e.valid() || !sys.valid() || sys.index() >= s.galaxy.systems.size()) return 0;
    const StarSystem& system = s.galaxy.system(sys);
    int64_t total = 0;
    for (const auto& a : system.abilities)
        if (parseAbilityKind(a.type) == k) total += a.number1();
    // Each colony adds its best facility, each vehicle its best component (confirmed: binary).
    for (ObjectId o : system.objects) {
        const Colony* c = s.colony(o);
        if (!c || c->owner != e || c->totalPopulation() <= 0) continue;
        int64_t best = 0;
        for (uint32_t f : c->facilities) best = std::max(best, bestValue1(r.facilityAbilities(f), k));
        total += best;
    }
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && v.count > 0 && v.location.system == sys) total += componentBest(r, s, v, k);
    return static_cast<int>(total);
}

namespace {
int sectorAbility(const GameState& s, Location where, AbilityKind kind) {
    int64_t total = 0;
    const StarSystem& sys = s.galaxy.system(where.system);
    for (const auto& a : sys.abilities)
        if (parseAbilityKind(a.type) == kind) total += a.number1();
    for (ObjectId o : sys.objects) {
        const SpaceObject& obj = s.galaxy.object(o);
        if (obj.sector != where.sector) continue;
        for (const auto& a : obj.abilities)
            if (parseAbilityKind(a.type) == kind) total += a.number1();
    }
    return static_cast<int>(total);
}
} // namespace

int sensorInterference(const GameState& s, Location where) { return sectorAbility(s, where, AbilityKind::SectorSensorInterference); }

int shieldDisruption(const GameState& s, Location where) { return sectorAbility(s, where, AbilityKind::SectorShieldDisruption); }

int fleetExperience(const GameState& s, const Vehicle& v) {
    if (!v.fleet.valid()) return 0;
    const Fleet* f = s.fleet(v.fleet);
    return f && f->owner == v.owner ? f->experience : 0;
}

namespace {
// The ability part of offense or defense: the hull in full plus the best
// intact component of each family, Plus − Minus (spec 03 §3.2, spec 04 §7).
// One implementation, in design.hpp.
int toHitTerms(const Rules& r, const GameState& s, const Vehicle& v, bool offense) {
    const int64_t terms = offense ? vehicleToHitOffense(r, s, v) : vehicleToHitDefense(r, s, v);
    return static_cast<int>(std::clamp<int64_t>(terms, INT_MIN, INT_MAX));
}
int racial(const Rules& r, const GameState& s, EmpireId owner, bool offense) {
    if (!owner.valid() || owner.index() >= s.empires.size()) return 0;
    return offense ? racialOffense(r, s.empire(owner)) : racialDefense(r, s.empire(owner));
}
} // namespace

int vehicleOffense(const Rules& r, const GameState& s, const Vehicle& v, int crewExperience, int fleetExp) {
    if (v.status == VehicleStatus::Mothballed) return 0;
    return toHitTerms(r, s, v, true) + crewExperience + fleetExp +
           racial(r, s, v.owner, true);
}

int vehicleDefense(const Rules& r, const GameState& s, const Vehicle& v, int crewExperience, int fleetExp) {
    if (v.status == VehicleStatus::Mothballed) return 0;
    return toHitTerms(r, s, v, false) + crewExperience + fleetExp +
           racial(r, s, v.owner, false);
}

int unitOffense(const Rules& r, const GameState& s, const Vehicle& v) {
    return std::max(0, toHitTerms(r, s, v, true)) +
           racial(r, s, v.owner, true);
}

int unitDefense(const Rules& r, const GameState& s, const Vehicle& v) {
    return std::max(0, toHitTerms(r, s, v, false)) +
           racial(r, s, v.owner, false);
}

// ---- Damage pipeline -----------------------------------------------------------------------------------

DamageRule damageRule(DamageType t) {
    using S = DamageRule::Shields;
    DamageRule d;
    auto onlyType = [&](Layer layer, S shields) {
        d.only = layer;
        d.shields = shields;
        d.hullDamaging = false;
        d.armorSpecials = false;
    };
    switch (t) {
        case DamageType::Normal: break;
        case DamageType::ShieldsOnly:
            d.shieldsOnly = true;
            d.hullDamaging = false;
            d.armorSpecials = false;
            break;
        case DamageType::SkipsNormalShields: d.shields = S::PhasedOnly; break;
        case DamageType::SkipsAllShields: d.shields = S::Ignore; break;
        case DamageType::SkipsArmor:
            d.skipsArmor = true;
            d.armorSpecials = false;
            break;
        case DamageType::SkipsShieldsAndArmor:
            d.shields = S::Ignore;
            d.skipsArmor = true;
            d.armorSpecials = false;
            break;
        case DamageType::QuadDamageToShields: d.shieldMultiply = 4; break;
        case DamageType::DoubleDamageToShields: d.shieldMultiply = 2; break;
        case DamageType::HalfDamageToShields: d.shieldDivide = 2; break;
        case DamageType::QuarterDamageToShields: d.shieldDivide = 4; break;
        case DamageType::OnlyEngines: onlyType(Layer::Engines, S::Absorb); break;   // shields absorb (history 1.70)
        case DamageType::OnlyWeapons: onlyType(Layer::Weapons, S::Ignore); break;
        case DamageType::OnlyShieldGenerators: onlyType(Layer::ShieldGenerators, S::Ignore); break;
        case DamageType::OnlyMasterComputers: onlyType(Layer::MasterComputers, S::Ignore); break;
        case DamageType::OnlyBoardingParties: onlyType(Layer::BoardingParties, S::Absorb); break;
        case DamageType::OnlySecurityStations: onlyType(Layer::SecurityStations, S::Absorb); break;
        case DamageType::OnlyPlanetDestroyers: onlyType(Layer::PlanetDestroyers, S::Absorb); break;
        case DamageType::PushesTarget:
        case DamageType::PullsTarget:
        case DamageType::RandomTargetMovement:
            // The move comes first; then the value still counts as damage, shields first (confirmed: binary).
            d.hullDamaging = false;
            d.armorSpecials = false;
            break;
        case DamageType::IncreaseReloadTime:
        case DamageType::DisruptReloadTime:
        case DamageType::CrewConversion:
            d.shields = S::Ignore;
            d.hullDamaging = false;
            d.armorSpecials = false;
            d.structural = false;
            break;
        default:   // planet-only types: shields first, then the planet effect
            d.hullDamaging = false;
            d.armorSpecials = false;
            d.structural = false;
            break;
    }
    return d;
}

void refreshShields(const Rules& r, const GameState& s, const Vehicle& v, int systemBonus, int disruption, ShieldState& sh, bool fill) {
    int64_t normal = 0, phased = 0;
    if (v.status != VehicleStatus::Mothballed && hasSupplies(r, s, v)) {
        const Design& d = s.design(v.design);
        normal = hullSum(r, d, AbilityKind::ShieldGeneration);
        phased = hullSum(r, d, AbilityKind::PhasedShieldGeneration);
        for (size_t i = 0; i < d.entries.size(); ++i) {
            if (!entryIntact(r, s, v, i)) continue;
            normal += mountedShield(r, d.entries[i], AbilityKind::ShieldGeneration);
            phased += mountedShield(r, d.entries[i], AbilityKind::PhasedShieldGeneration);
        }
    }
    int64_t total = normal + phased;
    if (total > 0 && systemBonus > 0) total += systemBonus;
    total = std::max<int64_t>(0, total - disruption);
    sh.max = static_cast<int>(std::min<int64_t>(total, 1'000'000'000));
    // One pool: normal if any normal generator, phased only if all are phased (confirmed: binary).
    sh.kind = normal > 0 ? ShieldState::Kind::Normal : phased > 0 ? ShieldState::Kind::Phased : ShieldState::Kind::None;
    sh.current = fill ? sh.max : std::min(sh.current, sh.max);
}

bool shieldsApply(const DamageRule& rule, const ShieldState& sh) {
    switch (rule.shields) {
        case DamageRule::Shields::Absorb: return true;
        case DamageRule::Shields::PhasedOnly: return sh.kind == ShieldState::Kind::Phased;
        case DamageRule::Shields::Ignore: return false;
    }
    return true;
}

int64_t absorbShields(ShieldState& sh, int64_t damage, const DamageRule& rule) {
    if (damage <= 0 || !shieldsApply(rule, sh)) return damage;
    if (rule.shieldsOnly) {
        sh.current -= static_cast<int>(std::min<int64_t>(sh.current, damage));
        return 0;
    }
    if (rule.shieldMultiply != 1 || rule.shieldDivide != 1) {
        // Against shields the value is scaled first; what is left is scaled
        // back and continues as Normal (confirmed: binary). (inferred) With the
        // shields already down there is nothing to scale against.
        if (sh.current <= 0) return damage;
        int64_t effective = damage * rule.shieldMultiply / rule.shieldDivide;
        const int64_t absorbed = std::min<int64_t>(sh.current, effective);
        sh.current -= static_cast<int>(absorbed);
        effective -= absorbed;
        return effective * rule.shieldDivide / rule.shieldMultiply;
    }
    const int64_t absorbed = std::min<int64_t>(sh.current, damage);
    sh.current -= static_cast<int>(absorbed);
    return damage - absorbed;
}

namespace {
bool inLayer(const Rules& r, uint32_t component, Layer layer) {
    const auto ab = r.componentAbilities(component);
    switch (layer) {
        case Layer::Engines: return hasAbility(ab, AbilityKind::StandardShipMovement);
        case Layer::Weapons: return r.component(component).isWeapon();
        case Layer::ShieldGenerators:
            return hasAbility(ab, AbilityKind::ShieldGeneration) || hasAbility(ab, AbilityKind::PhasedShieldGeneration) ||
                   hasAbility(ab, AbilityKind::PlanetShieldGeneration);
        case Layer::MasterComputers: return hasAbility(ab, AbilityKind::MasterComputer);
        case Layer::BoardingParties: return hasAbility(ab, AbilityKind::BoardingAttack);
        case Layer::SecurityStations: return hasAbility(ab, AbilityKind::BoardingDefense);
        case Layer::PlanetDestroyers: return hasAbility(ab, AbilityKind::DestroyPlanetSize);
    }
    return false;
}

bool isArmor(const Rules& r, uint32_t component) { return hasAbility(r.componentAbilities(component), AbilityKind::Armor); }
} // namespace

void destroyEntry(const Rules& r, const Design& d, Vehicle& v, size_t entry) {
    if (v.damage.size() < d.entries.size()) v.damage.resize(d.entries.size(), 0);
    // At least the design's own structure figure, so entryIntact() agrees.
    v.damage[entry] = std::max(combatStructure(r, d.entries[entry]), entryStructure(r, d, entry));
}

int64_t destroyComponents(const Rules& r, const GameState& s, Vehicle& v, int64_t damage, DamageType type, Rng& rng) {
    if (damage <= 0) return std::max<int64_t>(0, damage);
    const Design& d = s.design(v.design);
    if (v.damage.size() < d.entries.size()) v.damage.resize(d.entries.size(), 0);
    const DamageRule rule = damageRule(type);

    // Candidates, narrowed by the damage type (confirmed: binary).
    struct Candidate {
        size_t entry = 0;
        int64_t structure = 0;
        bool armor = false;
    };
    std::vector<Candidate> pool;
    for (size_t i = 0; i < d.entries.size(); ++i) {
        if (!entryIntact(r, s, v, i)) continue;
        const uint32_t c = d.entries[i].component;
        if (rule.only && !inLayer(r, c, *rule.only)) continue;
        pool.push_back({i, combatStructure(r, d.entries[i]), isArmor(r, c)});
    }
    if (rule.skipsArmor && !rule.only) {
        // Armor-skipping damage reaches armor only once nothing else is left.
        const bool other = std::any_of(pool.begin(), pool.end(), [](const Candidate& c) { return !c.armor; });
        if (other) std::erase_if(pool, [](const Candidate& c) { return c.armor; });
    }
    if (pool.empty()) return damage;
    // Nothing can fall when the hit is smaller than every part that could come first.
    const bool anyArmor = std::any_of(pool.begin(), pool.end(), [](const Candidate& c) { return c.armor; });
    int64_t smallest = INT64_MAX;
    for (const Candidate& c : pool)
        if (!anyArmor || c.armor) smallest = std::min(smallest, c.structure);
    if (damage < smallest) return damage;

    // The order: draws weighted by structure; armor goes to the front, the rest to the back.
    std::deque<Candidate> queue;
    while (!pool.empty()) {
        int64_t total = 0;
        for (const Candidate& c : pool) total += std::max<int64_t>(0, c.structure);
        size_t pick = 0;
        if (total > 0) {
            int64_t roll = static_cast<int64_t>(rng.below(static_cast<uint64_t>(total)));
            for (size_t k = 0; k < pool.size(); ++k) {
                roll -= std::max<int64_t>(0, pool[k].structure);
                if (roll < 0) {
                    pick = k;
                    break;
                }
            }
        }
        if (pool[pick].armor) queue.push_front(pool[pick]);
        else queue.push_back(pool[pick]);
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(pick));
    }
    // Whole components fall while the damage covers them; the first it cannot destroy stops it.
    bool lost = false;
    for (const Candidate& c : queue) {
        if (damage < c.structure) break;
        destroyEntry(r, d, v, c.entry);
        damage -= c.structure;
        lost = true;
    }
    // Every destroyed component clamps supply and trims cargo at once (spec 03
    // §19 Q57, spec 04 §9.4, confirmed: binary).
    if (lost && !vehicleDestroyed(r, s, v)) movement::fitToCapacity(r, s, v);
    return damage;
}

HitResult hitVehicle(const Rules& r, const GameState& s, Vehicle& v, ShieldState& sh, int64_t& pool, int64_t damage, DamageType type,
                     Rng& rng) {
    HitResult out;
    const DamageRule rule = damageRule(type);
    if (!rule.structural || damage < 0) return out;
    const int64_t hit = damage;   // after the system damage modifier, without the pool
    const int64_t poolBefore = pool;
    if (rule.hullDamaging) {
        damage += pool;
        pool = 0;
    }
    const int before = sh.current;
    damage = absorbShields(sh, damage, rule);
    out.shieldDamage = before - sh.current;
    if (rule.shieldsOnly || damage <= 0) return out;
    if (rule.armorSpecials) {
        // Crystalline armor turns part of what got through into shields, up to the maximum (confirmed: binary).
        const int64_t crystal =
            componentSum(r, s, v, AbilityKind::ShieldGenerationFromDamage) + hullSum(r, s.design(v.design), AbilityKind::ShieldGenerationFromDamage);
        if (crystal > 0 && sh.max > 0) sh.current = static_cast<int>(std::min<int64_t>(sh.max, sh.current + std::min(crystal, damage)));
        // Emissive armor: a hit no larger than its value does nothing to the hull, a larger one loses it.
        const int64_t emissive = std::max(componentBest(r, s, v, AbilityKind::EmissiveArmor),
                                          bestValue1(r.hullAbilities(s.design(v.design).hull), AbilityKind::EmissiveArmor));
        if (emissive > 0) {
            if (hit <= emissive) {
                pool = poolBefore;
                return out;
            }
            damage -= emissive;
            if (damage <= 0) return out;
        }
    }
    out.reached = damage;
    const int64_t left = destroyComponents(r, s, v, damage, type, rng);
    if (rule.hullDamaging) pool = left;   // the "Only" and other types lose what is left
    out.destroyed = vehicleDestroyed(r, s, v);
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
    if (rule.shieldsOnly) return sh.current > 0;
    if (rule.only) {
        bool found = false;
        forIntactParts(r, s, v, [&](size_t, const DesignEntry& e) { found = found || inLayer(r, e.component, *rule.only); });
        return found;
    }
    return true;
}

int64_t unitHitPoints(const Rules& r, const Design& d, DamageType type, bool shielded) {
    int64_t normal = hullSum(r, d, AbilityKind::ShieldGeneration), phased = hullSum(r, d, AbilityKind::PhasedShieldGeneration);
    for (const DesignEntry& e : d.entries) {
        normal += mountedShield(r, e, AbilityKind::ShieldGeneration);
        phased += mountedShield(r, e, AbilityKind::PhasedShieldGeneration);
    }
    const VehicleType t = r.hull(d.hull).type;
    int times = t == VehicleType::Fighter || t == VehicleType::Troop || t == VehicleType::WeaponPlatform ? 2 : 1;
    ShieldState kind;
    kind.kind = normal > 0 ? ShieldState::Kind::Normal : phased > 0 ? ShieldState::Kind::Phased : ShieldState::Kind::None;
    if (!shieldsApply(damageRule(type), kind)) --times;   // counted once less when the type skips shields
    if (!shielded) times = 0;
    return designStructure(r, d) + (normal + phased) * times;
}

int64_t restoreRegeneratingArmor(const Rules& r, const GameState& s, Vehicle& v, int64_t budget) {
    const Design& d = s.design(v.design);
    int64_t used = 0;
    for (size_t i = 0; i < d.entries.size() && i < v.damage.size(); ++i) {
        if (entryIntact(r, s, v, i) || !hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::ArmorRegeneration)) continue;
        const int64_t cost = combatStructure(r, d.entries[i]);
        if (cost > budget - used) break;   // (inferred) restored strictly in design order
        v.damage[i] = 0;
        used += cost;
    }
    return used;
}

bool hasDestroyedRegeneratingArmor(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (!entryIntact(r, s, v, i) && hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::ArmorRegeneration)) return true;
    return false;
}

void addExperience(int& whole, int& tenths, int gainTenths) {
    const int64_t before = int64_t{whole} * 10 + tenths;
    const int64_t cap = int64_t{kMaxCombatExperience} * 10;
    const int64_t after = std::max(before, std::min(cap, before + gainTenths));   // a gain past 50 sets it to 50
    whole = static_cast<int>(after / 10);
    tenths = static_cast<int>(after % 10);
}

// ---- Who is in a sector --------------------------------------------------------------------------------

bool arrivedThisTurn(const GameState& s, const Vehicle& v) {
    return v.cameFromTurn == s.turn && v.cameFrom.system.valid() && v.cameFrom != v.location;
}

std::pair<int, int> arrivalDirection(const GameState& s, const Vehicle& v) {
    if (!arrivedThisTurn(s, v) || v.cameFrom.system != v.location.system) return {0, 0};
    const int dx = v.cameFrom.sector.x - v.location.sector.x;
    const int dy = v.cameFrom.sector.y - v.location.sector.y;
    if (std::max(std::abs(dx), std::abs(dy)) != 1) return {0, 0};   // (inferred) not a neighbour: the centre
    return {dx, dy};
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
    // A battle starts when two hostile empires present see each other; planets cannot hide (spec 04 §2).
    std::vector<EmpireId> seen;
    for (const Vehicle* v : vehicles)
        for (EmpireId b : present)
            if (b != v->owner && enemies(s, b, v->owner) && visibleTo(r, s, b, *v)) {
                seen.push_back(v->owner);
                break;
            }
    for (ObjectId o : f.colonies) seen.push_back(s.colony(o)->owner);
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    for (EmpireId a : seen)
        for (EmpireId b : seen)
            if (a < b && enemies(s, a, b)) f.battle = true;
    if (!f.battle) {
        f.colonies.clear();
        return f;
    }
    // Once it starts, every owned object in the sector is a piece (confirmed: binary; history 1.28).
    f.empires = present;
    for (const Vehicle* v : vehicles) f.vehicles.push_back(v->id);
    for (ObjectId o : s.galaxy.system(where.system).objects) {
        const SpaceObject& obj = s.galaxy.object(o);
        if (obj.sector != where.sector) continue;
        const bool obstacle = obj.kind == ObjectKind::Star || obj.kind == ObjectKind::WarpPoint || obj.kind == ObjectKind::Comet ||
                              (obj.kind == ObjectKind::Planet && !s.colony(o));
        if (obstacle) f.obstacles.push_back(o);
    }
    return f;
}

std::string sectorName(const GameState& s, Location where) {
    const std::string& name = where.system.valid() && where.system.index() < s.galaxy.systems.size() ? s.galaxy.system(where.system).name
                                                                                                    : std::string("?");
    return std::format("{} ({}, {})", name, where.sector.x, where.sector.y);
}

// ---- Mines (spec 04 §10.6) ---------------------------------------------------------------------------

std::vector<std::vector<VehicleId>> enteringGroups(const Rules& r, const GameState& s, Location where, std::span<const VehicleId> entering) {
    std::vector<std::vector<VehicleId>> groups;
    auto eligible = [&](const Vehicle& v) {
        return v.location == where && v.count > 0 && v.owner.valid() && typeOf(r, s, v) != VehicleType::Mine;
    };
    if (!entering.empty()) {
        // The vehicles movement names are struck group by group, as they moved:
        // a fleet together, any other vehicle on its own (inferred).
        std::map<std::pair<int, uint32_t>, size_t> index;
        for (VehicleId id : entering) {
            const Vehicle* v = s.vehicle(id);
            if (!v || !eligible(*v)) continue;
            const std::pair<int, uint32_t> key = v->fleet.valid() ? std::pair{0, v->fleet.value} : std::pair{1, v->id.value};
            auto [it, added] = index.emplace(key, groups.size());
            if (added) groups.emplace_back();
            groups[it->second].push_back(id);
        }
        return groups;
    }
    // Once movement records arrivals (any vehicle moved this turn), only the
    // vehicles that moved into this sector are struck. Without any record
    // (a battle outside the movement phase), every vehicle there counts (inferred).
    const bool recorded = std::any_of(s.vehicles.begin(), s.vehicles.end(), [&](const Vehicle& v) { return v.count > 0 && arrivedThisTurn(s, v); });
    std::vector<const Vehicle*> base;
    for (const Vehicle& v : s.vehicles)
        if (eligible(v) && (!recorded || arrivedThisTurn(s, v))) base.push_back(&v);
    std::map<uint32_t, std::vector<VehicleId>> byOwner;
    for (const Vehicle* v : base) byOwner[v->owner.value].push_back(v->id);
    for (auto& [owner, ids] : byOwner) groups.push_back(std::move(ids));
    return groups;
}

namespace {
// Minefields that strike this group: hostile to every vehicle in it; a vehicle
// of the owner or of an empire at Non-Aggression or better stops the field (confirmed: binary).
std::vector<VehicleId> activeMinefields(const Rules& r, const GameState& s, Location where, std::span<const VehicleId> group) {
    std::vector<VehicleId> out;
    for (const Vehicle& m : s.vehicles) {
        if (m.location != where || m.count <= 0 || !m.owner.valid() || typeOf(r, s, m) != VehicleType::Mine) continue;
        bool friendly = false;
        for (VehicleId id : group)
            if (const Vehicle* v = s.vehicle(id); v && (v->owner == m.owner || !enemies(s, m.owner, v->owner))) friendly = true;
        if (!friendly) out.push_back(m.id);
    }
    return out;
}
} // namespace

bool minesCanStrike(const Rules& r, const GameState& s, Location where, std::span<const VehicleId> entering) {
    if (!where.system.valid() || where.system.index() >= s.galaxy.systems.size()) return false;
    const CombatSettings cs = loadSettings(r);
    for (const std::vector<VehicleId>& group : enteringGroups(r, s, where, entering))
        for (VehicleId mid : activeMinefields(r, s, where, group))
            for (VehicleId id : group) {
                const Vehicle& v = *s.vehicle(id);
                if (mineMayHit(cs, typeOf(r, s, v)) && !mineWarheads(r, s, *s.vehicle(mid), v).empty()) return true;
            }
    return false;
}

void resolveMines(TurnContext& ctx, Location where, std::span<const VehicleId> entering, Rng& rng) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const CombatSettings cs = loadSettings(r);
    for (const std::vector<VehicleId>& group : enteringGroups(r, s, where, entering)) {
        const std::vector<VehicleId> fields = activeMinefields(r, s, where, group);
        if (fields.empty()) continue;
        const EmpireId victimOwner = s.vehicle(group.front())->owner;

        // The group's own uncloaked sweepers clear mines first (confirmed: binary).
        int64_t capacity = 0;
        for (VehicleId id : group) {
            const Vehicle& v = *s.vehicle(id);
            if (v.status == VehicleStatus::Cloaked) continue;
            // A unit group sweeps with every unit of each of its designs.
            if (isUnitType(typeOf(r, s, v)))
                for (const UnitStack& st : groupStacks(v)) capacity += componentSum(r, s, stackProbe(s, v, st), AbilityKind::MineSweeping) * st.count;
            else
                capacity += componentSum(r, s, v, AbilityKind::MineSweeping);
        }
        std::map<uint32_t, int> sweptBy;   // minefield owner -> mines lost
        for (VehicleId mid : fields) {
            if (capacity <= 0) break;
            Vehicle* m = s.vehicle(mid);
            // A minefield that mixes designs loses its mines in the order they were laid (inferred).
            for (const UnitStack& st : groupStacks(*m)) {
                if (capacity <= 0) break;
                const int n = removeGroupUnits(s, *m, st.design, static_cast<int>(std::min<int64_t>(capacity, st.count)));
                capacity -= n;
                sweptBy[m->owner.value] += n;
            }
        }
        int sweptTotal = 0;
        for (const auto& [owner, n] : sweptBy) {
            if (n <= 0) continue;
            sweptTotal += n;
            ctx.log(EmpireId{owner}, LogCategory::Combat, std::format("Mines swept at {}", sectorName(s, where)),
                    std::format("{} of our mines were cleared by enemy sweepers.", n), where);
        }
        if (sweptTotal > 0)
            ctx.log(victimOwner, LogCategory::Combat, std::format("Mines swept at {}", sectorName(s, where)),
                    std::format("Our sweepers cleared {} enemy mines.", sweptTotal), where);

        // Each mine strikes one random vehicle of the group with its warheads,
        // straight to the components; leftover damage is shared by the whole strike (history 1.70, 1.78).
        int64_t pool = 0;
        std::map<uint32_t, int> struck, lost;   // victim vehicle -> mines, units lost
        for (VehicleId mid : fields) {
            int used = 0, kills = 0;
            while (s.vehicle(mid)->count > 0) {
                // The minefield's mines go off in the order they were laid: the front
                // design first (a minefield that mixes designs, inferred).
                const Vehicle& mine = *s.vehicle(mid);
                const DesignId mineDesign = mine.design;
                std::vector<VehicleId> targets;
                for (VehicleId id : group) {
                    const Vehicle& v = *s.vehicle(id);
                    if (v.count > 0 && mineMayHit(cs, typeOf(r, s, v)) && !mineWarheads(r, s, mine, v).empty()) targets.push_back(id);
                }
                if (targets.empty()) break;
                Vehicle& victim = *s.vehicle(targets[rng.below(targets.size())]);
                const std::vector<size_t> warheads = mineWarheads(r, s, mine, victim);
                const Design& md = s.design(mineDesign);
                for (size_t w : warheads) {
                    if (victim.count <= 0) break;
                    const DamageType t = parseDamageType(r.component(md.entries[w].component).weapon.damageType);
                    int64_t dmg = weaponLargestDamage(r, md.entries[w]);
                    const bool hull = damageRule(t).hullDamaging;
                    if (hull) {
                        dmg += pool;
                        pool = 0;
                    }
                    const int64_t left = destroyComponents(r, s, victim, dmg, t, rng);
                    if (hull) pool = left;
                    if (vehicleDestroyed(r, s, victim)) {
                        // The unit at the front of a group (of its first design) dies; the next one is whole.
                        const DesignId dead = victim.design;
                        ++kills;
                        ++lost[victim.id.value];
                        ++s.design(dead).lost;
                        Design& killer = s.design(mineDesign);
                        ++killer.kills;
                        killer.enemyTonnageDestroyed += designTonnage(r, s.design(dead));
                        if (victim.count > 1 && isUnitType(typeOf(r, s, victim))) {
                            removeGroupUnits(s, victim, dead, 1);
                            victim.damage.assign(s.design(victim.design).entries.size(), 0);
                        } else {
                            victim.count = 0;
                            victim.mixed.clear();
                        }
                    }
                }
                ++struck[victim.id.value];
                removeGroupUnits(s, *s.vehicle(mid), mineDesign, 1);   // the mine is used up
                ++used;
            }
            if (used > 0) {
                const Vehicle& m = *s.vehicle(mid);
                ctx.log(m.owner, LogCategory::Combat, std::format("Mines detonated at {}", sectorName(s, where)),
                        std::format("{} of our mines struck {} enemy vehicles{}.", used, struck.size(),
                                    kills > 0 ? std::format(", destroying {}", kills) : std::string{}),
                        where);
            }
        }
        for (const auto& [id, strikes] : struck) {
            const Vehicle& v = *s.vehicle(VehicleId{id});
            const int unitsLost = lost[id];
            ctx.log(v.owner, LogCategory::Combat, std::format("Mines at {}", sectorName(s, where)),
                    std::format("{} was struck by {} enemy mines{}.", v.name, strikes, v.count <= 0 ? " and destroyed" : ""), where);
            if (unitsLost > 0 && !isUnitType(typeOf(r, s, v))) {
                ctx.mood(v.owner, "Any Ship Lost", {}, {}, unitsLost);
                ctx.mood(v.owner, "Ship Lost in System", where.system, {}, unitsLost);
            }
        }
    }
}

} // namespace detail

// ---- Entry points ------------------------------------------------------------------------------------

bool combatPossible(const Rules& r, const GameState& s, Location where) {
    if (!where.system.valid() || where.system.index() >= s.galaxy.systems.size()) return false;
    if (detail::minesCanStrike(r, s, where, {})) return true;
    return detail::battleForces(r, s, where).battle;
}

int toHitPercent(const Rules& r, const GameState& s, const Vehicle& attacker, size_t weaponEntry, const Vehicle& defender, int distance) {
    const Design& d = s.design(attacker.design);
    if (weaponEntry >= d.entries.size()) return 0;
    const ruleset::Component& c = r.component(d.entries[weaponEntry].component);
    if (!c.isWeapon()) return 0;
    if (c.weapon.kind == WeaponKind::Seeking || c.weapon.kind == WeaponKind::Warhead) return 100;
    // Weapons Always Hit covers direct fire and point-defense (confirmed: binary).
    if (detail::hasIntactComponent(r, s, attacker, AbilityKind::WeaponsAlwaysHit)) return 100;
    const CombatSettings cs = loadSettings(r);
    const bool aUnit = isUnitType(detail::typeOf(r, s, attacker));
    const bool dUnit = isUnitType(detail::typeOf(r, s, defender));
    const int offense = (aUnit ? detail::unitOffense(r, s, attacker)
                               : detail::vehicleOffense(r, s, attacker, attacker.experience, detail::fleetExperience(s, attacker))) +
                        mounted(r, d.entries[weaponEntry]).toHitModifier +
                        detail::systemModifier(r, s, attacker.owner, attacker.location.system, AbilityKind::CombatModifierSystem);
    const int defense = dUnit ? detail::unitDefense(r, s, defender)
                              : detail::vehicleDefense(r, s, defender, defender.experience, detail::fleetExperience(s, defender));
    return detail::toHitChance(cs, distance, offense, defense, detail::sensorInterference(s, attacker.location));
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
    const std::vector<EmpireId> already = invaders(r, s, *c);
    for (EmpireId e : already)
        if (e != troopOwner) return 0;   // already contested by another empire (spec 04 §13)
    auto it = std::find_if(v->cargo.units.begin(), v->cargo.units.end(), [&](const UnitStack& u) { return u.design == design; });
    if (it == v->cargo.units.end() || it->count <= 0) return 0;
    const int n = std::min(count, it->count);
    it->count -= n;
    if (it->count <= 0) v->cargo.units.erase(it);
    auto dst = std::find_if(c->cargo.units.begin(), c->cargo.units.end(), [&](const UnitStack& u) { return u.design == design; });
    if (dst != c->cargo.units.end()) dst->count += n;
    else c->cargo.units.push_back({design, n});
    // The first invading troops give the colony its militia pool (confirmed: binary).
    if (already.empty() || c->militia < 0) c->militia = militiaCount(loadSettings(r), c->population);
    return n;
}

int militiaCount(const CombatSettings& cs, int64_t populationMillions) {
    if (populationMillions <= 0) return 0;
    return static_cast<int>(std::min<int64_t>(INT32_MAX, populationMillions / std::max(1, cs.defendingUnitsPerPopulation)));
}

int militiaCount(const CombatSettings& cs, std::span<const PopulationGroup> population) {
    int64_t total = 0;
    for (const PopulationGroup& g : population) total += militiaCount(cs, g.millions);
    return static_cast<int>(std::min<int64_t>(INT32_MAX, total));
}

} // namespace opense4::game::combat
