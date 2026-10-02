#include "game/design.hpp"

#include "datafile/datafile.hpp"
#include "game/generate.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <map>

namespace opense4::game {

namespace {

using ruleset::VehicleType;
using ruleset::WeaponKind;
using xmath::pctRound;
using xmath::pctTrunc;

// Classes that never move by themselves (spec 03 §1).
bool isImmobile(VehicleType t) {
    return t == VehicleType::Base || t == VehicleType::Satellite || t == VehicleType::Mine || t == VehicleType::Troop ||
           t == VehicleType::WeaponPlatform;
}

bool isUnit(VehicleType t) { return t != VehicleType::Ship && t != VehicleType::Base; }

// Satellites and mines have no supply at all; troops and platforms never use any (spec 03 §1, §12).
bool typeUsesSupply(VehicleType t) {
    return t == VehicleType::Ship || t == VehicleType::Base || t == VehicleType::Fighter || t == VehicleType::Drone;
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

int toInt(int64_t v) {
    return static_cast<int>(std::clamp<int64_t>(v, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}

// The bonus B of §6.1 over a list: the smallest `Movement Bonus` plus the
// first-per-id `Extra Movement Generation` (confirmed: binary).
int64_t movementBonus(std::span<const ParsedAbility> list) {
    return abilitySmallest(list, AbilityKind::MovementBonus) + abilityFirstPerId(list, AbilityKind::ExtraMovementGeneration);
}

// Appends a component's abilities, marked with its family; shields scaled by the mount.
void appendComponent(const Rules& r, const DesignEntry& e, std::vector<ParsedAbility>& out) {
    const ruleset::Component& c = r.component(e.component);
    const MountedComponent m = mounted(r, e);
    const auto abilities = r.componentAbilities(e.component);
    // A mount scales the component's summed shields, rounded once (§4.3).
    int64_t shields[2] = {abilitySum(abilities, AbilityKind::ShieldGeneration), abilitySum(abilities, AbilityKind::PhasedShieldGeneration)};
    if (m.mountApplies)
        for (int64_t& v : shields) v = pctRound(v, m.shieldPercent);
    bool shieldGiven[2] = {false, false};
    for (ParsedAbility a : abilities) {
        a.fromComponent = true;
        a.family = c.family;
        const int slot = a.kind == AbilityKind::ShieldGeneration ? 0 : a.kind == AbilityKind::PhasedShieldGeneration ? 1 : -1;
        if (slot >= 0) {
            a.value1 = shieldGiven[slot] ? 0 : shields[slot];
            shieldGiven[slot] = true;
        }
        out.push_back(std::move(a));
    }
}

// Hull abilities followed by the components' (all of them, or the working ones).
std::vector<ParsedAbility> designAbilities(const Rules& r, uint32_t hull, std::span<const DesignEntry> entries) {
    std::vector<ParsedAbility> out(r.hullAbilities(hull).begin(), r.hullAbilities(hull).end());
    for (const DesignEntry& e : entries) appendComponent(r, e, out);
    return out;
}

} // namespace

// ---- Weapon mounts ------------------------------------------------------------------------------

bool mountApplies(const Rules& r, uint32_t component, uint32_t mount) {
    if (mount >= r.data().weaponMounts.size()) return false;
    const ruleset::WeaponMount& m = r.data().weaponMounts[mount];
    const ruleset::Component& c = r.component(component);
    const std::string& req = m.weaponTypeRequirement;
    bool type = false;
    if (req.empty()) type = true;  // no requirement written (inferred: every component)
    else if (datafile::keysEqual(req, "None")) type = !c.isWeapon();
    else if (datafile::keysEqual(req, "Any")) type = c.isWeapon();  // every weapon type, not non-weapons
    else type = c.isWeapon() && datafile::keysEqual(req, weaponKindName(c.weapon.kind));
    if (!type) return false;
    return m.familyRequirement.empty() ||
           std::find(m.familyRequirement.begin(), m.familyRequirement.end(), c.family) != m.familyRequirement.end();
}

bool mountFitsHull(const Rules& r, uint32_t hullIndex, uint32_t mount) {
    if (mount >= r.data().weaponMounts.size()) return false;
    const ruleset::WeaponMount& m = r.data().weaponMounts[mount];
    const int tons = r.hull(hullIndex).tonnage;
    return tons >= m.minimumVehicleSize && (m.maximumVehicleSize <= 0 || tons <= m.maximumVehicleSize);
}

bool mountOffered(const Rules& r, uint32_t hullIndex, uint32_t mount) {
    if (mount >= r.data().weaponMounts.size()) return false;
    const std::string& text = r.data().weaponMounts[mount].vehicleType;
    if (text.empty() || datafile::keysEqual(text, "Any")) return true;
    return text.find(ruleset::displayName(r.hull(hullIndex).type)) != std::string::npos;  // matching case (confirmed: binary)
}

bool mountAllowed(const Rules& r, uint32_t hullIndex, uint32_t component, uint32_t mount) {
    return mountOffered(r, hullIndex, mount) && mountFitsHull(r, hullIndex, mount) && mountApplies(r, component, mount);
}

MountedComponent mounted(const Rules& r, const DesignEntry& e) {
    const ruleset::Component& c = r.component(e.component);
    MountedComponent m;
    m.tonnage = c.tonnage;
    m.structure = c.structure;
    m.cost = Resources::from(c.cost);
    m.supplyUsed = c.supplyUsed;
    m.toHitModifier = c.weapon.modifier;
    if (e.mount < 0 || !mountApplies(r, e.component, static_cast<uint32_t>(e.mount))) return m;
    const ruleset::WeaponMount& mt = r.data().weaponMounts[static_cast<size_t>(e.mount)];
    // Each value in floating point, rounded half to even (§4.3, confirmed: binary).
    m.mountApplies = true;
    m.tonnage = toInt(pctRound(c.tonnage, mt.tonnagePercent));
    m.structure = toInt(pctRound(c.structure, mt.structurePercent));
    for (Resource res : kResources) m.cost[res] = pctRound(m.cost[res], mt.costPercent);
    m.supplyUsed = toInt(pctRound(c.supplyUsed, mt.supplyPercent));
    m.damagePercent = mt.damagePercent;
    m.rangeModifier = mt.rangeModifier;
    m.toHitModifier += mt.toHitModifier;
    m.shieldPercent = mt.shieldPercent;
    return m;
}

namespace {

constexpr int kDamageTableSize = 20;
constexpr int64_t kMaxMountedDamage = 50'000;

int tableDamage(const ruleset::Component& c, int index) {
    if (index < 1 || index > kDamageTableSize || index > static_cast<int>(c.weapon.damageAtRange.size())) return 0;
    return c.weapon.damageAtRange[static_cast<size_t>(index - 1)];
}

} // namespace

int weaponDamageAtRange(const Rules& r, const DesignEntry& e, int range) {
    const ruleset::Component& c = r.component(e.component);
    if (!c.isWeapon() || range < 1) return 0;
    const MountedComponent m = mounted(r, e);
    if (!m.mountApplies) return tableDamage(c, range);  // 0 beyond range 20
    // The table index is shifted by the range modifier and clamped to 1..20;
    // closer ranges use the range-1 damage (§4.3, confirmed: binary).
    const int index = std::clamp(range - m.rangeModifier, 1, kDamageTableSize);
    return toInt(std::min(kMaxMountedDamage, pctRound(tableDamage(c, index), m.damagePercent)));
}

int weaponMaxRange(const Rules& r, const DesignEntry& e) {
    // The largest range from 1 to 20 with damage, so at most 20, even when a
    // mounted weapon does damage further out (spec 03 §19 Q42, confirmed: binary).
    int last = 0;
    for (int range = 1; range <= kDamageTableSize; ++range)
        if (weaponDamageAtRange(r, e, range) > 0) last = range;
    return last;
}

// ---- Designs --------------------------------------------------------------------------------------

bool DesignStats::canColonize(std::string_view surface) const {
    if (datafile::keysEqual(surface, "Rock")) return canColonizeRock;
    if (datafile::keysEqual(surface, "Ice")) return canColonizeIce;
    return canColonizeGas;  // "Gas Giant"
}

int designMovement(const Rules& r, uint32_t hullIndex, std::span<const DesignEntry> entries) {
    if (hullIndex >= r.data().vehicleSizes.size()) return 0;
    const ruleset::VehicleSize& hull = r.hull(hullIndex);
    if (isImmobile(hull.type)) return 0;
    const std::vector<ParsedAbility> list = designAbilities(r, hullIndex, entries);
    const int64_t engines = abilitySum(list, AbilityKind::StandardShipMovement);
    int64_t mp = hull.enginesPerMove > 0 ? engines / hull.enginesPerMove : 0;
    const int64_t bonus = movementBonus(list);
    if (bonus > 0 && bonus < 100) mp += bonus;  // (§4.4, confirmed: binary)
    return toInt(std::max<int64_t>(0, mp));
}

DesignStats computeDesignStats(const Rules& r, const Empire* owner, uint32_t hullIndex, std::span<const DesignEntry> entries) {
    DesignStats st;
    auto problem = [&](std::string s) { st.problems.push_back(std::move(s)); };
    // 1. A hull: without one, this is the only warning.
    if (hullIndex >= r.data().vehicleSizes.size()) {
        problem("Choose a hull for the design");
        return st;
    }
    const ruleset::VehicleSize& hull = r.hull(hullIndex);
    const int T = hull.tonnage;
    st.vehicleType = hull.type;
    st.tonnageMax = T;
    st.cost = Resources::from(hull.cost);

    // Per component: its mounted values and which abilities it has (a component
    // counts once for an ability, however many entries of it it has).
    struct Part {
        const ruleset::Component* c;
        MountedComponent m;
        std::span<const ParsedAbility> abilities;
        bool has(AbilityKind k) const { return hasAbility(abilities, k); }
    };
    std::vector<Part> parts;
    for (const DesignEntry& e : entries) parts.push_back({&r.component(e.component), mounted(r, e), r.componentAbilities(e.component)});
    auto count = [&](AbilityKind k) { return static_cast<int>(std::count_if(parts.begin(), parts.end(), [&](const Part& p) { return p.has(k); })); };
    auto size = [&](AbilityKind k) {
        int64_t total = 0;
        for (const Part& p : parts)
            if (p.has(k)) total += p.m.tonnage;
        return total;
    };

    for (size_t i = 0; i < parts.size(); ++i) {
        const Part& p = parts[i];
        st.tonnageUsed += p.m.tonnage;
        st.structure += p.m.structure;
        st.cost += p.m.cost;
        if (p.has(AbilityKind::StandardShipMovement)) ++st.engines;
        if (p.c->isWeapon()) {
            ++st.weapons;
            st.maxWeaponRange = std::max(st.maxWeaponRange, weaponMaxRange(r, entries[i]));
        }
    }
    const std::vector<ParsedAbility> list = designAbilities(r, hullIndex, entries);
    st.movement = designMovement(r, hullIndex, entries);
    st.supplyCapacity = typeUsesSupply(hull.type) ? abilitySum(list, AbilityKind::SupplyStorage) : 0;
    st.cargoCapacity = toInt(abilitySum(list, AbilityKind::CargoStorage));
    st.shields = toInt(abilitySum(list, AbilityKind::ShieldGeneration));
    st.phasedShields = toInt(abilitySum(list, AbilityKind::PhasedShieldGeneration));
    st.spaceYard = count(AbilityKind::SpaceYard) > 0;
    st.canColonizeRock = count(AbilityKind::ColonizeRock) > 0;
    st.canColonizeIce = count(AbilityKind::ColonizeIce) > 0;
    st.canColonizeGas = count(AbilityKind::ColonizeGas) > 0;

    // 2. Technology of the hull and the components (mounts: rule 5).
    if (owner && !r.hullAvailable(*owner, hullIndex)) problem(std::format("{} hull is not yet researched", hull.name));
    if (owner)
        for (size_t i = 0; i < parts.size(); ++i)
            if (!r.componentAvailable(*owner, entries[i].component)) {
                problem(std::format("{} is not yet researched", parts[i].c->name));
                break;
            }
    // 3. Space.
    if (st.tonnageUsed > T) problem(std::format("Components use {} kT of {} kT", st.tonnageUsed, T));
    // 4. One space yard at most.
    if (count(AbilityKind::SpaceYard) > 1) problem("A design can have only one space yard");
    // 5. Mounts: size bounds first, then technology; one warning at most.
    bool sizeFail = false, techFail = false;
    for (const DesignEntry& e : entries) {
        if (e.mount < 0) continue;
        const auto m = static_cast<uint32_t>(e.mount);
        if (!mountFitsHull(r, hullIndex, m)) sizeFail = true;
        else if (owner && !r.mountAvailable(*owner, m)) techFail = true;
    }
    if (sizeFail) problem("A weapon mount is not allowed on a hull of this size");
    else if (techFail) problem("A weapon mount is beyond our technology");
    // 6. Restrictions: the whole family at most N times; only the first violation.
    for (size_t i = 0; i < parts.size(); ++i) {
        const int limit = parts[i].c->maxPerVehicle;
        if (limit <= 0) continue;
        int others = 0;
        for (size_t j = 0; j < parts.size(); ++j) others += j != i && parts[j].c->family == parts[i].c->family;
        if (others >= limit) {
            problem(std::format("At most {} of the {} family per vehicle", limit, parts[i].c->name));
            break;
        }
    }
    // 7. Control requirements, lifted by a Master Computer.
    if (count(AbilityKind::MasterComputer) == 0) {
        if (hull.mustHaveBridge && count(AbilityKind::ShipBridge) != 1) {
            const char* part = hull.type == VehicleType::Fighter || hull.type == VehicleType::Troop ? "cockpit"
                               : hull.type == VehicleType::Satellite || hull.type == VehicleType::Drone ||
                                         hull.type == VehicleType::WeaponPlatform
                                   ? "computer core"
                                   : "bridge";
            problem(std::format("Needs exactly one {}", part));
        }
        if (hull.canHaveAuxControl && count(AbilityKind::ShipAuxiliaryControl) > 1) problem("At most one auxiliary control");
        if (hull.minLifeSupport > 0 && count(AbilityKind::ShipLifeSupport) < hull.minLifeSupport)
            problem(std::format("Needs {} life support", hull.minLifeSupport));
        if (hull.minCrewQuarters > 0 && count(AbilityKind::ShipCrewQuarters) < hull.minCrewQuarters)
            problem(std::format("Needs {} crew quarters", hull.minCrewQuarters));
    }
    // 8. Engines (a Master Computer does not matter); no minimum.
    if (!hull.usesEngines && st.engines > 0) problem("This hull cannot use engines");
    else if (hull.usesEngines && hull.maxEngines > 0 && st.engines > hull.maxEngines) problem(std::format("At most {} engines", hull.maxEngines));
    // 9. Percentages of the hull, by mounted size, against truncate(T × p %).
    auto pct = [&](int64_t have, int required, const char* what) {
        if (required > 0 && have < pctTrunc(T, required)) problem(std::format("At least {}% of the hull must be {}", required, what));
    };
    pct(size(AbilityKind::LaunchRecoverFighters), hull.maxPercentFighterBays, "fighter bays");
    pct(size(AbilityKind::ColonizeRock) + size(AbilityKind::ColonizeIce) + size(AbilityKind::ColonizeGas), hull.maxPercentColonyModules,
        "colony modules");
    pct(size(AbilityKind::CargoStorage), hull.maxPercentCargo, "cargo space");
    // Not a warning in the original, but the designer never offers such parts,
    // so a created design must respect it (§4.2).
    for (const Part& p : parts)
        if (!(p.c->vehicles & ruleset::maskOf(hull.type))) {
            problem(std::format("{} cannot be placed on a {}", p.c->name, ruleset::displayName(hull.type)));
            break;
        }
    return st;
}

bool designNameInUse(const GameState& s, std::string_view name) {
    for (const Empire& e : s.empires)
        for (DesignId id : e.designs)
            if (id.index() < s.designs.size() && s.design(id).name == name) return true;
    return false;
}

std::string uniqueDesignName(const GameState& s, std::string_view wanted) {
    const std::string base = wanted.empty() ? std::string("Design") : std::string(wanted);
    if (!designNameInUse(s, base)) return base;
    for (int n = 2;; ++n)
        if (std::string name = std::format("{} {}", base, romanNumeral(n)); !designNameInUse(s, name)) return name;
}

void resetDesignStatistics(Design& d) {
    d.built = d.lost = d.scrapped = 0;
    d.enemyTonnageDestroyed = 0;
}

bool designIsPrototype(const Design& d) { return d.built == 0 && !d.retrofitted; }

std::string nextVehicleName(const GameState& s, const Design& d) {
    int highest = 0;
    for (const Vehicle& v : s.vehicles) {
        if (v.design != d.id || v.name.size() < 4) continue;
        const std::string_view tail = std::string_view(v.name).substr(v.name.size() - 4);
        if (std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; }))
            highest = std::max(highest, (tail[0] - '0') * 1000 + (tail[1] - '0') * 100 + (tail[2] - '0') * 10 + (tail[3] - '0'));
    }
    return std::format("{} {:04d}", d.name, highest + 1);
}

bool designInQueue(const GameState& s, EmpireId empire, DesignId design) {
    auto holds = [&](const ConstructionQueue& q) {
        return std::any_of(q.items.begin(), q.items.end(), [&](const QueueItem& it) { return it.kind == QueueItem::Kind::Vehicle && it.design == design; });
    };
    for (const auto& c : s.colonies)
        if (c && c->owner == empire && holds(c->queue)) return true;
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.owner == empire && holds(v.queue)) return true;
    return false;
}

int64_t designTonnage(const Rules& r, const Design& d) {
    return d.hull < r.data().vehicleSizes.size() ? std::max(0, r.hull(d.hull).tonnage) : 0;
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

ruleset::VehicleType vehicleType(const Rules& r, const GameState& s, const Vehicle& v) { return r.hull(s.design(v.design).hull).type; }

std::vector<ParsedAbility> vehicleAbilities(const Rules& r, const GameState& s, const Vehicle& v) {
    std::vector<ParsedAbility> out;
    if (v.status == VehicleStatus::Mothballed) return out;  // no abilities at all (§3.1)
    if (!v.mixed.empty()) {
        // A group that mixes designs: each design's list once (units are whole).
        for (const UnitStack& st : v.mixed) {
            const Design& d = s.design(st.design);
            for (const auto& a : r.hullAbilities(d.hull)) out.push_back(a);
            for (const DesignEntry& e : d.entries) appendComponent(r, e, out);
        }
        return out;
    }
    const Design& d = s.design(v.design);
    for (const auto& a : r.hullAbilities(d.hull)) out.push_back(a);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i)) appendComponent(r, d.entries[i], out);
    return out;
}

int64_t vehicleAbilityTotal(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k, bool value2) {
    if (!isUnit(vehicleType(r, s, v))) return abilitySum(vehicleAbilities(r, s, v), k, value2);
    if (v.mixed.empty()) return abilitySum(vehicleAbilities(r, s, v), k, value2) * std::max(1, v.count);
    // Every unit lists its design's abilities once (spec 03 §12).
    int64_t total = 0;
    for (const UnitStack& st : v.mixed) total += abilitySum(vehicleAbilities(r, s, stackProbe(s, v, st)), k, value2) * st.count;
    return total;
}

// ---- Unit groups ------------------------------------------------------------------------------

std::vector<UnitStack> groupStacks(const Vehicle& v) {
    if (v.count <= 0) return {};   // gone
    if (!v.mixed.empty()) return v.mixed;
    return {UnitStack{v.design, v.count}};
}

int groupUnits(const Vehicle& v, DesignId d) {
    int n = 0;
    for (const UnitStack& st : groupStacks(v))
        if (st.design == d) n += std::max(0, st.count);
    return n;
}

void setGroupStacks(const GameState& s, Vehicle& v, std::vector<UnitStack> stacks) {
    std::vector<UnitStack> merged;
    for (const UnitStack& st : stacks) {
        if (st.count <= 0 || !st.design.valid()) continue;
        auto it = std::find_if(merged.begin(), merged.end(), [&](const UnitStack& m) { return m.design == st.design; });
        if (it == merged.end()) merged.push_back(st);
        else it->count += st.count;
    }
    if (merged.empty()) {
        v.mixed.clear();
        v.count = 0;
        return;
    }
    const DesignId first = merged.front().design;
    if (first != v.design || v.damage.size() != s.design(first).entries.size()) v.damage.assign(s.design(first).entries.size(), 0);
    v.design = first;
    int64_t total = 0;
    for (const UnitStack& st : merged) total += st.count;
    v.count = toInt(total);
    if (merged.size() == 1) v.mixed.clear();
    else v.mixed = std::move(merged);
}

void addGroupUnits(const GameState& s, Vehicle& v, DesignId d, int n) {
    if (n <= 0) return;
    std::vector<UnitStack> stacks = v.count > 0 ? groupStacks(v) : std::vector<UnitStack>{};
    stacks.push_back({d, n});
    setGroupStacks(s, v, std::move(stacks));
}

int removeGroupUnits(const GameState& s, Vehicle& v, DesignId d, int n) {
    std::vector<UnitStack> stacks = groupStacks(v);
    int removed = 0;
    for (UnitStack& st : stacks)
        if (st.design == d && n > removed) {
            const int take = std::min(st.count, n - removed);
            st.count -= take;
            removed += take;
        }
    if (removed > 0) setGroupStacks(s, v, std::move(stacks));
    return removed;
}

Vehicle stackProbe(const GameState& s, const Vehicle& group, const UnitStack& st) {
    Vehicle p;
    p.id = group.id;
    p.owner = group.owner;
    p.design = st.design;
    p.name = group.name;
    p.location = group.location;
    p.count = st.count;
    p.damage.assign(s.design(st.design).entries.size(), 0);
    p.supply = group.supply;
    p.status = group.status;
    return p;
}

bool vehicleHasQuantumReactor(const Rules& r, const GameState& s, const Vehicle& v) {
    return hasAbility(vehicleAbilities(r, s, v), AbilityKind::QuantumReactor);
}

bool vehicleHasControl(const Rules& r, const GameState& s, const Vehicle& v) {
    if (isUnit(vehicleType(r, s, v))) return true;
    const std::vector<ParsedAbility> list = vehicleAbilities(r, s, v);
    if (hasAbility(list, AbilityKind::MasterComputer)) return true;
    return (hasAbility(list, AbilityKind::ShipBridge) || hasAbility(list, AbilityKind::ShipAuxiliaryControl)) &&
           abilityCount(list, AbilityKind::ShipCrewQuarters) > 0 && abilityCount(list, AbilityKind::ShipLifeSupport) > 0;
}

int vehicleMaxMovement(const Rules& r, const GameState& s, const Vehicle& v) {
    if (v.status == VehicleStatus::Mothballed) return 0;
    const Design& d = s.design(v.design);
    const ruleset::VehicleSize& hull = r.hull(d.hull);
    if (isImmobile(hull.type)) return 0;
    if (isUnit(hull.type)) {
        // A unit group moves at the lowest speed of its designs, or 1 at zero supply (§12).
        int speed = designMovement(r, d.hull, d.entries);
        for (const UnitStack& st : v.mixed) {
            const Design& sd = s.design(st.design);
            speed = std::min(speed, designMovement(r, sd.hull, sd.entries));
        }
        return speed > 0 && v.supply <= 0 ? 1 : speed;
    }
    // Ships (spec 03 §6.1, confirmed: binary).
    const std::vector<ParsedAbility> list = vehicleAbilities(r, s, v);
    const int64_t engines = abilitySum(list, AbilityKind::StandardShipMovement) & 0xff;  // the original keeps 8 bits
    int64_t mp = hull.enginesPerMove > 0 ? engines / hull.enginesPerMove : 0;
    const int64_t bonus = movementBonus(list);
    if (mp > 0 && bonus < 100) mp = std::max<int64_t>(0, mp + bonus);  // the original wraps below 0; we clamp
    if (mp > 0 && v.owner.valid() && v.owner.index() < s.empires.size())
        mp = std::max<int64_t>(0, mp + r.traitValue(s.empire(v.owner).race, "Vehicle Speed"));
    if (mp <= 0) return 0;
    if (v.supply <= 0 && !vehicleHasUnlimitedSupply(r, s, v)) return 1;
    if (!hasAbility(list, AbilityKind::MasterComputer)) {
        const bool missing[] = {!hasAbility(list, AbilityKind::ShipBridge) && !hasAbility(list, AbilityKind::ShipAuxiliaryControl),
                                abilityCount(list, AbilityKind::ShipCrewQuarters) == 0, abilityCount(list, AbilityKind::ShipLifeSupport) == 0};
        for (bool m : missing)
            if (m) mp = std::max<int64_t>(1, mp / 2);
    }
    return toInt(mp);
}

namespace {
// Plus − minus per family; a group that mixes designs takes its best design (spec 04 §7).
int64_t toHitTerms(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind plus, AbilityKind minus) {
    if (!v.mixed.empty()) {
        int64_t best = std::numeric_limits<int64_t>::min();
        for (const UnitStack& st : v.mixed) best = std::max(best, toHitTerms(r, s, stackProbe(s, v, st), plus, minus));
        return best;
    }
    const std::vector<ParsedAbility> list = vehicleAbilities(r, s, v);
    return abilityPerFamily(list, plus) - abilityPerFamily(list, minus);
}
} // namespace

int64_t vehicleToHitOffense(const Rules& r, const GameState& s, const Vehicle& v) {
    return toHitTerms(r, s, v, AbilityKind::CombatToHitOffensePlus, AbilityKind::CombatToHitOffenseMinus);
}

int64_t vehicleToHitDefense(const Rules& r, const GameState& s, const Vehicle& v) {
    return toHitTerms(r, s, v, AbilityKind::CombatToHitDefensePlus, AbilityKind::CombatToHitDefenseMinus);
}

// ---- Supply -------------------------------------------------------------------------------------

bool vehicleHasUnlimitedSupply(const Rules& r, const GameState& s, const Vehicle& v) {
    if (v.status == VehicleStatus::Mothballed) return false;
    return vehicleType(r, s, v) == VehicleType::Base || vehicleHasQuantumReactor(r, s, v);
}

bool vehicleUsesSupply(const Rules& r, const GameState& s, const Vehicle& v) { return typeUsesSupply(vehicleType(r, s, v)); }

int64_t vehicleSupplyCapacity(const Rules& r, const GameState& s, const Vehicle& v) {
    if (!vehicleUsesSupply(r, s, v)) return 0;
    return vehicleAbilityTotal(r, s, v, AbilityKind::SupplyStorage);  // a group: the sum over its units (§12)
}

int64_t initialSupply(const Rules& r, const GameState& s, const Vehicle& v) {
    return vehicleHasUnlimitedSupply(r, s, v) ? kUnlimitedSupply : vehicleSupplyCapacity(r, s, v);
}

int vehicleCargoCapacity(const Rules& r, const GameState& s, const Vehicle& v) {
    if (isUnit(vehicleType(r, s, v))) return 0;  // unit groups in space hold no cargo (§11)
    return toInt(abilitySum(vehicleAbilities(r, s, v), AbilityKind::CargoStorage));
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
