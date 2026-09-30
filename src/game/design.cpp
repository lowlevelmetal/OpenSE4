#include "game/design.hpp"

#include "datafile/datafile.hpp"

#include <algorithm>
#include <format>
#include <map>

namespace opense4::game {

namespace {

using ruleset::VehicleType;
using ruleset::WeaponKind;

bool isImmobile(VehicleType t) {
    return t == VehicleType::Base || t == VehicleType::Satellite || t == VehicleType::Mine || t == VehicleType::Troop ||
           t == VehicleType::WeaponPlatform;
}

std::string_view weaponKindName(WeaponKind k) {
    switch (k) {
        case WeaponKind::DirectFire: return "Direct Fire";
        case WeaponKind::Seeking: return "Seeking";
        case WeaponKind::Warhead: return "Warhead";
        case WeaponKind::PointDefense: return "Point-Defense";
        case WeaponKind::None: break;
    }
    return "None";
}

// Movement from a set of (component index, abilities) pairs, per spec 03 §6.1.
int movementFrom(const Rules& r, const Race* race, uint32_t hullIndex, const std::vector<std::span<const ParsedAbility>>& comps) {
    const ruleset::VehicleSize& hull = r.hull(hullIndex);
    if (isImmobile(hull.type)) return 0;
    int64_t engines = 0;
    std::optional<int64_t> bonus;
    std::map<int64_t, int64_t> extra;  // stacking id -> best
    for (const auto& abilities : comps) {
        const int64_t e = sumValue1(abilities, AbilityKind::StandardShipMovement);
        if (e > 0) {
            engines += e;
            const int64_t b = bestValue1(abilities, AbilityKind::MovementBonus);
            bonus = bonus ? std::min(*bonus, b) : b;
        }
        for (const auto& a : abilities)
            if (a.kind == AbilityKind::ExtraMovementGeneration) extra[a.value2] = std::max(extra[a.value2], a.value1);
    }
    for (const auto& a : r.hullAbilities(hullIndex))
        if (a.kind == AbilityKind::ExtraMovementGeneration) extra[a.value2] = std::max(extra[a.value2], a.value1);
    int64_t mp = hull.enginesPerMove > 0 ? engines / hull.enginesPerMove : 0;
    if (engines > 0 && bonus) mp += *bonus;
    for (const auto& [id, v] : extra) mp += v;
    if (race && (engines > 0 || !extra.empty())) mp += r.traitValue(*race, "Vehicle Speed");
    return static_cast<int>(std::max<int64_t>(0, mp));
}

} // namespace

MountedComponent mounted(const Rules& r, const DesignEntry& e) {
    const ruleset::Component& c = r.component(e.component);
    MountedComponent m;
    m.tonnage = c.tonnage;
    m.structure = c.structure;
    m.cost = Resources::from(c.cost);
    m.supplyUsed = c.supplyUsed;
    m.toHitModifier = c.weapon.modifier;
    if (e.mount >= 0 && static_cast<size_t>(e.mount) < r.data().weaponMounts.size()) {
        const ruleset::WeaponMount& mt = r.data().weaponMounts[static_cast<size_t>(e.mount)];
        m.tonnage = c.tonnage * mt.tonnagePercent / 100;
        m.structure = c.structure * mt.structurePercent / 100;
        m.cost = m.cost.percent(mt.costPercent);
        m.supplyUsed = c.supplyUsed * mt.supplyPercent / 100;
        m.damagePercent = mt.damagePercent;
        m.rangeModifier = mt.rangeModifier;
        m.toHitModifier += mt.toHitModifier;
    }
    return m;
}

int weaponDamageAtRange(const Rules& r, const DesignEntry& e, int range) {
    const ruleset::Component& c = r.component(e.component);
    if (!c.isWeapon() || range < 1) return 0;
    const MountedComponent m = mounted(r, e);
    const int index = std::max(1, range - m.rangeModifier);
    if (index > static_cast<int>(c.weapon.damageAtRange.size())) return 0;
    return c.weapon.damageAtRange[static_cast<size_t>(index - 1)] * m.damagePercent / 100;
}

int weaponMaxRange(const Rules& r, const DesignEntry& e) {
    const ruleset::Component& c = r.component(e.component);
    if (!c.isWeapon()) return 0;
    int last = 0;
    for (size_t i = 0; i < c.weapon.damageAtRange.size(); ++i)
        if (c.weapon.damageAtRange[i] > 0) last = static_cast<int>(i) + 1;
    return last > 0 ? last + mounted(r, e).rangeModifier : 0;
}

bool mountAllowed(const Rules& r, uint32_t hullIndex, uint32_t component, uint32_t mount) {
    if (mount >= r.data().weaponMounts.size()) return false;
    const ruleset::WeaponMount& m = r.data().weaponMounts[mount];
    const ruleset::VehicleSize& hull = r.hull(hullIndex);
    const ruleset::Component& c = r.component(component);
    if (hull.tonnage < m.minimumVehicleSize) return false;
    if (!m.vehicleType.empty() && !datafile::keysEqual(m.vehicleType, "Any") &&
        !datafile::keysEqual(m.vehicleType, ruleset::displayName(hull.type)))
        return false;
    const std::string& req = m.weaponTypeRequirement;
    if (req.empty() || datafile::keysEqual(req, "Any")) return true;
    if (datafile::keysEqual(req, "None")) return !c.isWeapon();
    return c.isWeapon() && datafile::keysEqual(req, weaponKindName(c.weapon.kind));
}

bool DesignStats::canColonize(std::string_view surface) const {
    if (datafile::keysEqual(surface, "Rock")) return canColonizeRock;
    if (datafile::keysEqual(surface, "Ice")) return canColonizeIce;
    return canColonizeGas;  // "Gas Giant"
}

DesignStats computeDesignStats(const Rules& r, const Empire* owner, uint32_t hullIndex, std::span<const DesignEntry> entries) {
    DesignStats st;
    const ruleset::VehicleSize& hull = r.hull(hullIndex);
    st.vehicleType = hull.type;
    st.tonnageMax = hull.tonnage;
    st.cost = Resources::from(hull.cost);
    auto problem = [&](std::string s) { st.problems.push_back(std::move(s)); };

    if (owner && !r.hullAvailable(*owner, hullIndex)) problem(std::format("{} hull is not yet researched", hull.name));

    int bridges = 0, aux = 0, lifeSupport = 0, crew = 0, engines = 0;
    bool masterComputer = false;
    int bayTonnage = 0, colonyTonnage = 0, cargoTonnage = 0;
    std::map<uint32_t, int> perComponent;
    std::vector<std::span<const ParsedAbility>> abilities;
    for (const DesignEntry& e : entries) {
        const ruleset::Component& c = r.component(e.component);
        const MountedComponent m = mounted(r, e);
        const auto ab = r.componentAbilities(e.component);
        abilities.push_back(ab);
        st.tonnageUsed += m.tonnage;
        st.structure += m.structure;
        st.cost += m.cost;
        ++perComponent[e.component];
        if (owner && !r.componentAvailable(*owner, e.component)) problem(std::format("{} is not yet researched", c.name));
        if (!(c.vehicles & ruleset::maskOf(hull.type))) problem(std::format("{} cannot be placed on a {}", c.name, ruleset::displayName(hull.type)));
        if (e.mount >= 0 && !mountAllowed(r, hullIndex, e.component, static_cast<uint32_t>(e.mount)))
            problem(std::format("{} cannot use that mount", c.name));

        bridges += hasAbility(ab, AbilityKind::ShipBridge);
        aux += hasAbility(ab, AbilityKind::ShipAuxiliaryControl);
        lifeSupport += hasAbility(ab, AbilityKind::ShipLifeSupport);
        crew += hasAbility(ab, AbilityKind::ShipCrewQuarters);
        masterComputer = masterComputer || hasAbility(ab, AbilityKind::MasterComputer);
        if (hasAbility(ab, AbilityKind::StandardShipMovement)) ++engines;
        if (hasAbility(ab, AbilityKind::LaunchRecoverFighters)) bayTonnage += m.tonnage;
        if (hasAbility(ab, AbilityKind::ColonizeRock) || hasAbility(ab, AbilityKind::ColonizeIce) || hasAbility(ab, AbilityKind::ColonizeGas))
            colonyTonnage += m.tonnage;
        if (hasAbility(ab, AbilityKind::CargoStorage)) cargoTonnage += m.tonnage;

        st.supplyCapacity += sumValue1(ab, AbilityKind::SupplyStorage);
        st.cargoCapacity += static_cast<int>(sumValue1(ab, AbilityKind::CargoStorage));
        st.shields += static_cast<int>(sumValue1(ab, AbilityKind::ShieldGeneration) * m.shieldPercent / 100);
        st.phasedShields += static_cast<int>(sumValue1(ab, AbilityKind::PhasedShieldGeneration) * m.shieldPercent / 100);
        st.spaceYard = st.spaceYard || hasAbility(ab, AbilityKind::SpaceYard);
        st.canColonizeRock = st.canColonizeRock || hasAbility(ab, AbilityKind::ColonizeRock);
        st.canColonizeIce = st.canColonizeIce || hasAbility(ab, AbilityKind::ColonizeIce);
        st.canColonizeGas = st.canColonizeGas || hasAbility(ab, AbilityKind::ColonizeGas);
        if (c.isWeapon()) {
            ++st.weapons;
            st.maxWeaponRange = std::max(st.maxWeaponRange, weaponMaxRange(r, e));
        }
    }
    st.engines = engines;
    st.movement = movementFrom(r, owner ? &owner->race : nullptr, hullIndex, abilities);

    if (st.tonnageUsed > st.tonnageMax) problem(std::format("Components use {} kT of {} kT", st.tonnageUsed, st.tonnageMax));
    if (hull.mustHaveBridge && bridges == 0 && !masterComputer) problem("Needs a bridge");
    if (!hull.canHaveAuxControl && aux > 0) problem("This hull cannot have auxiliary control");
    if (!masterComputer && lifeSupport < hull.minLifeSupport) problem(std::format("Needs {} life support", hull.minLifeSupport));
    if (!masterComputer && crew < hull.minCrewQuarters) problem(std::format("Needs {} crew quarters", hull.minCrewQuarters));
    const int maxEngines = hull.usesEngines ? hull.maxEngines : 0;
    if (engines > maxEngines) problem(std::format("At most {} engines", maxEngines));
    auto pct = [&](int have, int required, const char* what) {
        if (required > 0 && have * 100 < hull.tonnage * required)
            problem(std::format("At least {}% of the hull must be {}", required, what));
    };
    pct(bayTonnage, hull.maxPercentFighterBays, "fighter bays");
    pct(colonyTonnage, hull.maxPercentColonyModules, "colony modules");
    pct(cargoTonnage, hull.maxPercentCargo, "cargo space");
    for (const auto& [comp, n] : perComponent) {
        const int limit = r.component(comp).maxPerVehicle;
        if (limit > 0 && n > limit) problem(std::format("At most {} {} per vehicle", limit, r.component(comp).name));
    }
    return st;
}

// ---- Vehicles -------------------------------------------------------------------------

int entryStructure(const Rules& r, const Design& d, size_t entry) { return mounted(r, d.entries[entry]).structure; }

bool entryIntact(const Rules& r, const GameState& s, const Vehicle& v, size_t entry) {
    if (entry >= v.damage.size()) return true;
    return v.damage[entry] < entryStructure(r, s.design(v.design), entry);
}

int vehicleStructure(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    int total = 0;
    for (size_t i = 0; i < d.entries.size(); ++i) total += entryStructure(r, d, i);
    return total;
}

int vehicleDamageTaken(const GameState&, const Vehicle& v) {
    int total = 0;
    for (int d : v.damage) total += d;
    return total;
}

bool vehicleDestroyed(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    if (d.entries.empty()) return false;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i)) return false;
    return true;
}

std::vector<ParsedAbility> vehicleAbilities(const Rules& r, const GameState& s, const Vehicle& v) {
    std::vector<ParsedAbility> out;
    if (v.status == VehicleStatus::Mothballed) return out;
    const Design& d = s.design(v.design);
    for (const auto& a : r.hullAbilities(d.hull)) out.push_back(a);
    for (size_t i = 0; i < d.entries.size(); ++i) {
        if (!entryIntact(r, s, v, i)) continue;
        const int shieldPct = mounted(r, d.entries[i]).shieldPercent;
        for (ParsedAbility a : r.componentAbilities(d.entries[i].component)) {
            if (a.kind == AbilityKind::ShieldGeneration || a.kind == AbilityKind::PhasedShieldGeneration) a.value1 = a.value1 * shieldPct / 100;
            out.push_back(std::move(a));
        }
    }
    return out;
}

bool vehicleHasControl(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    const ruleset::VehicleSize& hull = r.hull(d.hull);
    bool bridge = false, lifeSupport = false, crew = false;
    for (size_t i = 0; i < d.entries.size(); ++i) {
        if (!entryIntact(r, s, v, i)) continue;
        const auto ab = r.componentAbilities(d.entries[i].component);
        if (hasAbility(ab, AbilityKind::MasterComputer)) return true;
        bridge = bridge || hasAbility(ab, AbilityKind::ShipBridge) || hasAbility(ab, AbilityKind::ShipAuxiliaryControl);
        lifeSupport = lifeSupport || hasAbility(ab, AbilityKind::ShipLifeSupport);
        crew = crew || hasAbility(ab, AbilityKind::ShipCrewQuarters);
    }
    if (hull.mustHaveBridge && !bridge) return false;
    if (hull.minLifeSupport > 0 && !lifeSupport) return false;
    if (hull.minCrewQuarters > 0 && !crew) return false;
    return true;
}

bool vehicleHasQuantumReactor(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i) && hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::QuantumReactor)) return true;
    return false;
}

ruleset::VehicleType vehicleType(const Rules& r, const GameState& s, const Vehicle& v) { return r.hull(s.design(v.design).hull).type; }

int vehicleMaxMovement(const Rules& r, const GameState& s, const Vehicle& v) {
    if (v.status == VehicleStatus::Mothballed) return 0;
    const Design& d = s.design(v.design);
    std::vector<std::span<const ParsedAbility>> intact;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i)) intact.push_back(r.componentAbilities(d.entries[i].component));
    const Race* race = v.owner.valid() && v.owner.index() < s.empires.size() ? &s.empire(v.owner).race : nullptr;
    int mp = movementFrom(r, race, d.hull, intact);
    if (mp > 1) {
        if (v.supply <= 0 && !vehicleHasQuantumReactor(r, s, v)) mp = 1;
        if (!vehicleHasControl(r, s, v)) mp = 1;
    }
    return mp;
}

int64_t vehicleSupplyCapacity(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    int64_t total = 0;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i)) total += sumValue1(r.componentAbilities(d.entries[i].component), AbilityKind::SupplyStorage);
    return total;
}

int vehicleCargoCapacity(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    int64_t total = sumValue1(r.hullAbilities(d.hull), AbilityKind::CargoStorage);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i)) total += sumValue1(r.componentAbilities(d.entries[i].component), AbilityKind::CargoStorage);
    return static_cast<int>(total);
}

int64_t cargoSpaceUsed(const Rules& r, const GameState& s, const Cargo& c) {
    const int64_t mass = r.setting("Population Mass", 5);
    int64_t used = c.totalPopulation() * mass;
    for (const UnitStack& u : c.units) used += int64_t{u.count} * r.hull(s.design(u.design).hull).tonnage;
    return used;
}

// ---- Automatic designs -----------------------------------------------------------------------

namespace {

// Best available component (highest numeral, then larger value) with ability `k` for this hull.
std::optional<uint32_t> bestComponent(const Rules& r, const Empire& e, VehicleType type, AbilityKind k) {
    std::optional<uint32_t> best;
    int64_t bestScore = 0;
    for (uint32_t i = 0; i < r.data().components.size(); ++i) {
        const auto& c = r.component(i);
        if (!(c.vehicles & ruleset::maskOf(type)) || !r.componentAvailable(e, i)) continue;
        const auto ab = r.componentAbilities(i);
        if (!hasAbility(ab, k)) continue;
        // Prefer pure parts (fewer abilities) so a bridge isn't a bridge+sensor combo.
        const int64_t score = std::max<int64_t>(1, sumValue1(ab, k)) * 1000 + c.romanNumeral * 10 - static_cast<int64_t>(ab.size());
        if (!best || score > bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

std::optional<uint32_t> bestWeapon(const Rules& r, const Empire& e, VehicleType type, WeaponKind kind) {
    // General-purpose weapons only: special damage types (shields only,
    // population only, ...) cannot hurt every target. Fall back to any.
    for (const bool generalOnly : {true, false}) {
        std::optional<uint32_t> best;
        int64_t bestScore = 0;
        for (uint32_t i = 0; i < r.data().components.size(); ++i) {
            const auto& c = r.component(i);
            if (c.weapon.kind != kind || !(c.vehicles & ruleset::maskOf(type)) || !r.componentAvailable(e, i)) continue;
            if (generalOnly && !c.weapon.damageType.empty() && !datafile::keysEqual(c.weapon.damageType, "Normal")) continue;
            int64_t total = 0;
            for (int d : c.weapon.damageAtRange) total += d;
            const int64_t score = total * 100 / std::max(1, c.tonnage) / std::max(1, c.weapon.reloadRate);
            if (!best || score > bestScore) {
                best = i;
                bestScore = score;
            }
        }
        if (best) return best;
    }
    return std::nullopt;
}

// Armor: a component whose only role is structure (has the Armor ability).
std::optional<uint32_t> bestArmor(const Rules& r, const Empire& e, VehicleType type) {
    std::optional<uint32_t> best;
    int64_t bestScore = 0;
    for (uint32_t i = 0; i < r.data().components.size(); ++i) {
        const auto& c = r.component(i);
        if (!(c.vehicles & ruleset::maskOf(type)) || !r.componentAvailable(e, i)) continue;
        if (!hasAbility(r.componentAbilities(i), AbilityKind::Armor)) continue;
        const int64_t score = int64_t{c.structure} * 100 / std::max(1, c.tonnage);
        if (!best || score > bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

} // namespace

std::optional<Design> autoDesign(const Rules& r, const Empire& owner, std::string_view role) {
    VehicleType type = VehicleType::Ship;
    std::string surface;
    if (role == "base") type = VehicleType::Base;
    else if (role == "fighter") type = VehicleType::Fighter;
    else if (role == "satellite") type = VehicleType::Satellite;
    else if (role == "mine") type = VehicleType::Mine;
    else if (role == "troop") type = VehicleType::Troop;
    else if (role.starts_with("colony:")) surface = std::string(role.substr(7));

    // Candidate hulls of the type, smallest first (scouts and colony ships want small, warships large).
    std::vector<uint32_t> hulls;
    for (uint32_t i = 0; i < r.data().vehicleSizes.size(); ++i)
        if (r.hull(i).type == type && r.hullAvailable(owner, i)) hulls.push_back(i);
    std::sort(hulls.begin(), hulls.end(), [&](uint32_t a, uint32_t b) { return r.hull(a).tonnage < r.hull(b).tonnage; });
    const bool wantLarge = role == "warship" || role == "transport" || role == "base";
    if (wantLarge) std::reverse(hulls.begin(), hulls.end());

    for (uint32_t h : hulls) {
        const ruleset::VehicleSize& hull = r.hull(h);
        Design d;
        d.owner = owner.id;
        d.hull = h;
        auto add = [&](std::optional<uint32_t> c, int n = 1) {
            for (int i = 0; c && i < n; ++i) d.entries.push_back({*c, -1});
            return c.has_value();
        };
        auto fits = [&](uint32_t c) {
            int used = 0;
            for (const auto& e : d.entries) used += r.component(e.component).tonnage;
            return used + r.component(c).tonnage <= hull.tonnage;
        };
        if (hull.mustHaveBridge) add(bestComponent(r, owner, type, AbilityKind::ShipBridge));
        add(bestComponent(r, owner, type, AbilityKind::ShipLifeSupport), hull.minLifeSupport);
        add(bestComponent(r, owner, type, AbilityKind::ShipCrewQuarters), hull.minCrewQuarters);

        if (!surface.empty()) {
            const AbilityKind k = datafile::keysEqual(surface, "Rock")  ? AbilityKind::ColonizeRock
                                  : datafile::keysEqual(surface, "Ice") ? AbilityKind::ColonizeIce
                                                                        : AbilityKind::ColonizeGas;
            if (!add(bestComponent(r, owner, type, k))) return std::nullopt;
        }
        if (hull.usesEngines && hull.maxEngines > 0) {
            const int n = role == "scout" ? hull.maxEngines : std::max(1, hull.maxEngines / 2);
            if (auto engine = bestComponent(r, owner, type, AbilityKind::StandardShipMovement))
                for (int i = 0; i < n && fits(*engine); ++i) add(engine);
        }
        if (type == VehicleType::Ship)
            if (auto supply = bestComponent(r, owner, type, AbilityKind::SupplyStorage); supply && fits(*supply)) add(supply);

        if (role == "transport") {
            if (auto cargo = bestComponent(r, owner, type, AbilityKind::CargoStorage))
                while (fits(*cargo)) add(cargo);
        } else if (role == "warship" || role == "base" || role == "satellite" || role == "fighter" || role == "troop") {
            const auto weapon = bestWeapon(r, owner, type, WeaponKind::DirectFire);
            const auto armor = bestArmor(r, owner, type);
            for (bool turnWeapon = true;; turnWeapon = !turnWeapon) {
                const auto pick = (turnWeapon || !armor) ? weapon : armor;
                if (pick && fits(*pick)) add(pick);
                else if (armor && pick != armor && fits(*armor)) add(armor);
                else break;
            }
        } else if (role == "mine") {
            if (auto warhead = bestWeapon(r, owner, type, WeaponKind::Warhead)) add(warhead);
        }

        const DesignStats st = computeDesignStats(r, &owner, d);
        if (st.problems.empty() && !d.entries.empty()) return d;
    }
    return std::nullopt;
}

} // namespace opense4::game
