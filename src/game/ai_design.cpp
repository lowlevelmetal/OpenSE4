// Computer player: ship designer (spec 05 §7.5 AI_DesignCreation).
//
// A template names a vehicle type, a tonnage window, must-have abilities,
// speeds, weapon-family preferences and "spaces per one" densities. We read a
// density d as "one component per d kT of hull, rounded up" (inferred: it
// gives the stock templates one of each misc part on small hulls and more on
// big ones). The designer tries every hull the empire has for the vehicle
// type and keeps the best-scoring valid result.

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::ai::detail {

namespace {

using datafile::keysEqual;
using ruleset::VehicleType;
using ruleset::WeaponKind;

int ceilDiv(int a, int b) { return b > 0 ? (a + b - 1) / b : 0; }

bool isWeaponName(std::string_view ability) { return keysEqual(ability, "Weapon") || keysEqual(ability, "Weapons"); }

// Majority abilities worth stacking to fill a hull.
bool stackable(std::string_view ability) {
    if (ability.empty()) return false;
    if (isWeaponName(ability)) return true;
    const auto k = parseAbilityKind(ability);
    return k == AbilityKind::CargoStorage || k == AbilityKind::LaunchRecoverFighters || k == AbilityKind::LayMines ||
           k == AbilityKind::LaunchRecoverSatellites || k == AbilityKind::LaunchDrones || k == AbilityKind::BoardingAttack ||
           k == AbilityKind::MineSweeping || k == AbilityKind::DropTroops;
}

bool abilityMatches(const ParsedAbility& a, std::string_view ability) {
    const auto k = parseAbilityKind(ability);
    if (!k) return false;
    if (*k != AbilityKind::Unknown && *k != AbilityKind::AITag) return a.kind == *k;
    return keysEqual(a.raw, ability);
}

int64_t abilityAmount(std::span<const ParsedAbility> list, std::string_view ability) {
    int64_t best = 0;
    for (const auto& a : list) {
        if (!abilityMatches(a, ability)) continue;
        int64_t v = a.value1 != 0 ? a.value1 : a.value2;
        if (a.kind == AbilityKind::SpaceYard) v = a.value2;
        best += std::max<int64_t>(1, v);
    }
    return best;
}

struct Picker {
    const Rules& r;
    const Empire& e;
    VehicleType type;

    bool usable(uint32_t c) const { return (r.component(c).vehicles & ruleset::maskOf(type)) && r.componentAvailable(e, c); }

    // Best part with an ability: most of it per kT, newest, fewest side abilities.
    std::optional<uint32_t> withAbility(std::string_view ability) const {
        std::optional<uint32_t> best;
        int64_t bestScore = 0;
        for (uint32_t i = 0; i < r.data().components.size(); ++i) {
            if (!usable(i)) continue;
            const auto ab = r.componentAbilities(i);
            const int64_t amount = abilityAmount(ab, ability);
            if (amount <= 0) continue;
            const auto& c = r.component(i);
            const int64_t score = amount * 10000 / std::max(1, c.tonnage) * 100 + c.romanNumeral * 10 - static_cast<int64_t>(ab.size());
            if (!best || score > bestScore) {
                best = i;
                bestScore = score;
            }
        }
        if (!best && parseAbilityKind(ability) == AbilityKind::PointDefense) return weaponOfKind(WeaponKind::PointDefense);
        return best;
    }

    int64_t weaponScore(uint32_t i) const {
        Design one;
        one.entries.push_back({i, -1});
        return weaponStrength(r, one) * 1000 / std::max(1, r.component(i).tonnage);
    }

    std::optional<uint32_t> weaponOfKind(WeaponKind kind) const {
        std::optional<uint32_t> best;
        int64_t bestScore = 0;
        for (uint32_t i = 0; i < r.data().components.size(); ++i) {
            const auto& c = r.component(i);
            if (c.weapon.kind != kind || !usable(i)) continue;
            const int64_t score = weaponScore(i);
            if (!best || score > bestScore) {
                best = i;
                bestScore = score;
            }
        }
        return best;
    }

    // A weapon from the preferred families (in order), else the best general one.
    std::optional<uint32_t> weapon(const std::array<int, 5>& families) const {
        for (int family : families) {
            if (family == 0) continue;
            std::optional<uint32_t> best;
            for (uint32_t i = 0; i < r.data().components.size(); ++i) {
                const auto& c = r.component(i);
                if (!c.isWeapon() || c.weapon.family != family || !usable(i)) continue;
                if (!best || c.romanNumeral > r.component(*best).romanNumeral ||
                    (c.romanNumeral == r.component(*best).romanNumeral && weaponScore(i) > weaponScore(*best)))
                    best = i;
            }
            if (best) return best;
        }
        std::optional<uint32_t> best;
        int64_t bestScore = 0;
        for (uint32_t i = 0; i < r.data().components.size(); ++i) {
            const auto& c = r.component(i);
            if (!c.isWeapon() || c.weapon.kind == WeaponKind::PointDefense || !usable(i)) continue;
            const int64_t score = weaponScore(i);
            if (!best || score > bestScore) {
                best = i;
                bestScore = score;
            }
        }
        return best;
    }

    std::optional<uint32_t> armor() const {
        std::optional<uint32_t> best;
        int64_t bestScore = 0;
        for (uint32_t i = 0; i < r.data().components.size(); ++i) {
            if (!usable(i) || !hasAbility(r.componentAbilities(i), AbilityKind::Armor)) continue;
            const auto& c = r.component(i);
            const int64_t score = int64_t{c.structure} * 1000 / std::max(1, c.tonnage);
            if (!best || score > bestScore) {
                best = i;
                bestScore = score;
            }
        }
        return best;
    }

    std::optional<uint32_t> engine() const {
        std::optional<uint32_t> best;
        int64_t bestScore = 0;
        for (uint32_t i = 0; i < r.data().components.size(); ++i) {
            if (!usable(i)) continue;
            const auto ab = r.componentAbilities(i);
            const int64_t v = sumValue1(ab, AbilityKind::StandardShipMovement);
            if (v <= 0) continue;
            const int64_t score = v * 100000 + bestValue1(ab, AbilityKind::MovementBonus) * 1000 + r.component(i).romanNumeral * 10 -
                                  r.component(i).tonnage;
            if (!best || score > bestScore) {
                best = i;
                bestScore = score;
            }
        }
        return best;
    }

    std::optional<uint32_t> pick(std::string_view ability, const std::array<int, 5>& families) const {
        if (isWeaponName(ability)) return weapon(families);
        return withAbility(ability);
    }
};

class HullFiller {
public:
    static constexpr size_t kMaxEntries = 200;

    HullFiller(const Rules& r, const Empire& e, const DesignTemplate& t, uint32_t hull)
        : r_(r), e_(e), t_(t), hull_(r.hull(hull)), pick_{r, e, r.hull(hull).type} {
        d_.owner = e.id;
        d_.hull = hull;
        d_.designType = t.name;
    }

    std::optional<Design> run() {
        if (!control()) return std::nullopt;
        if (!percentRules()) return std::nullopt;
        for (const std::string& a : t_.mustHave)
            if (!satisfied(a) && !add(pick_.pick(a, t_.majorityFamilies))) return std::nullopt;
        if (!engines()) return std::nullopt;
        if (hull_.type == VehicleType::Ship) add(pick_.withAbility("Supply Storage"));
        fillers();
        majority();
        if (hull_.type != VehicleType::Mine && (t_.armorSpacesPerOne > 0 || isWeaponName(t_.majority.ability)))
            while (add(pick_.armor())) {}
        const DesignStats st = computeDesignStats(r_, &e_, d_);
        if (!st.problems.empty() || d_.entries.empty()) return std::nullopt;
        return d_;
    }

private:
    bool fits(uint32_t c) const {
        const auto& comp = r_.component(c);
        if (d_.entries.size() >= kMaxEntries) return false;  // also stops weightless parts from stacking forever
        if (used_ + comp.tonnage > hull_.tonnage) return false;
        if (comp.maxPerVehicle > 0 && count(c) >= comp.maxPerVehicle) return false;
        if (hasAbility(r_.componentAbilities(c), AbilityKind::StandardShipMovement) && engines_ >= (hull_.usesEngines ? hull_.maxEngines : 0))
            return false;
        return true;
    }
    int count(uint32_t c) const {
        return static_cast<int>(std::count_if(d_.entries.begin(), d_.entries.end(), [&](const DesignEntry& en) { return en.component == c; }));
    }
    bool add(std::optional<uint32_t> c) {
        if (!c || !fits(*c)) return false;
        d_.entries.push_back({*c, -1});
        used_ += r_.component(*c).tonnage;
        if (hasAbility(r_.componentAbilities(*c), AbilityKind::StandardShipMovement)) ++engines_;
        return true;
    }
    bool satisfied(std::string_view ability) const {
        for (const auto& en : d_.entries) {
            if (isWeaponName(ability) ? r_.component(en.component).isWeapon() : abilityAmount(r_.componentAbilities(en.component), ability) > 0)
                return true;
        }
        return false;
    }
    int movement() const { return computeDesignStats(r_, &e_, d_).movement; }

    bool control() {
        auto need = [&](AbilityKind k, int n) {
            for (int i = 0; i < n; ++i)
                if (!add(pick_.withAbility(identifier(k)))) return false;
            return true;
        };
        if (hull_.mustHaveBridge && !need(AbilityKind::ShipBridge, 1)) return false;
        return need(AbilityKind::ShipLifeSupport, hull_.minLifeSupport) && need(AbilityKind::ShipCrewQuarters, hull_.minCrewQuarters);
    }

    // Hulls that demand a share of colony modules, cargo or bays can only
    // carry templates built around that ability.
    bool percentRules() {
        auto involves = [&](AbilityKind k) {
            if (abilityMatchesName(t_.majority.ability, k)) return true;
            for (const auto& a : t_.mustHave)
                if (abilityMatchesName(a, k)) return true;
            return false;
        };
        auto meet = [&](int pct, std::initializer_list<AbilityKind> kinds) {
            if (pct <= 0) return true;
            std::optional<AbilityKind> use;
            for (AbilityKind k : kinds)
                if (involves(k)) use = k;
            if (!use) return false;
            const auto part = pick_.withAbility(identifier(*use));
            int tons = 0;
            while (tons * 100 < hull_.tonnage * pct) {
                if (!add(part)) return false;
                tons += r_.component(*part).tonnage;
            }
            return true;
        };
        return meet(hull_.maxPercentColonyModules, {AbilityKind::ColonizeRock, AbilityKind::ColonizeIce, AbilityKind::ColonizeGas}) &&
               meet(hull_.maxPercentCargo, {AbilityKind::CargoStorage}) &&
               meet(hull_.maxPercentFighterBays, {AbilityKind::LaunchRecoverFighters});
    }
    static bool abilityMatchesName(std::string_view name, AbilityKind k) { return parseAbilityKind(name) == k; }

    bool engines() {
        const bool mobile = hull_.usesEngines && hull_.maxEngines > 0;
        if (!mobile) return t_.minSpeed <= 0 || hull_.type == VehicleType::Base;
        const auto engine = pick_.engine();
        if (!engine) return t_.minSpeed <= 0;
        while (movement() < t_.minSpeed)
            if (!add(engine)) return false;
        // Toward the desired speed, spending at most a third of the free space
        // on it (all of it for designs without a main component).
        const int budget = t_.majority.ability.empty() ? hull_.tonnage : (hull_.tonnage - used_) / 3;
        int spent = 0;
        while (movement() < t_.desiredSpeed && spent + r_.component(*engine).tonnage <= budget && add(engine))
            spent += r_.component(*engine).tonnage;
        return true;
    }

    void fillers() {
        struct Want {
            std::optional<uint32_t> part;
            int count;
        };
        std::vector<Want> wants;
        const int tons = hull_.tonnage;
        if (t_.shieldsSpacesPerOne > 0) wants.push_back({pick_.withAbility("Shield Generation"), ceilDiv(tons, t_.shieldsSpacesPerOne)});
        if (t_.armorSpacesPerOne > 0) wants.push_back({pick_.armor(), ceilDiv(tons, t_.armorSpacesPerOne)});
        for (const auto& m : t_.misc) {
            if (m.spacesPerOne <= 0) continue;
            const int want = ceilDiv(tons, m.spacesPerOne) - (keysEqual(m.ability, "Supply Storage") ? satisfiedCount(m.ability) : 0);
            if (want > 0) wants.push_back({pick_.withAbility(m.ability), want});
        }
        if (!t_.secondary.ability.empty() && t_.secondary.spacesPerOne > 0)
            wants.push_back({pick_.pick(t_.secondary.ability, t_.secondaryFamilies), ceilDiv(tons, t_.secondary.spacesPerOne)});
        // Leave most of a combat hull to its main component.
        const int budget = stackable(t_.majority.ability) ? (hull_.tonnage - used_) * 45 / 100 : hull_.tonnage;
        int spent = 0;
        for (bool progress = true; progress;) {
            progress = false;
            for (Want& w : wants) {
                if (w.count <= 0 || !w.part) continue;
                const int t = r_.component(*w.part).tonnage;
                if (spent + t > budget || !add(w.part)) {
                    w.count = 0;
                    continue;
                }
                spent += t;
                --w.count;
                progress = true;
            }
        }
    }
    int satisfiedCount(std::string_view ability) const {
        int n = 0;
        for (const auto& en : d_.entries) n += abilityAmount(r_.componentAbilities(en.component), ability) > 0;
        return n;
    }

    void majority() {
        if (t_.majority.ability.empty()) return;
        const auto part = pick_.pick(t_.majority.ability, t_.majorityFamilies);
        if (!part) return;
        if (stackable(t_.majority.ability)) {
            while (add(part)) {}
            return;
        }
        const int want = std::max(1, ceilDiv(hull_.tonnage, t_.majority.spacesPerOne));
        for (int have = satisfiedCount(t_.majority.ability); have < want; ++have)
            if (!add(part)) break;
    }

    const Rules& r_;
    const Empire& e_;
    const DesignTemplate& t_;
    const ruleset::VehicleSize& hull_;
    Picker pick_;
    Design d_;
    int used_ = 0;
    int engines_ = 0;
};

uint32_t strategyIndex(const Empire& e, std::string_view name) {
    if (name.empty()) return 0;
    for (size_t i = 0; i < e.strategies.size(); ++i)
        if (keysEqual(e.strategies[i].name, name)) return static_cast<uint32_t>(i);
    return 0;
}

int tonnageCap(const Planner& p) {
    for (const auto& [tons, turns] : p.prof.settings.tonnageCaps)
        if (tons > 0 && turns > 0 && static_cast<int>(p.st.turn) < turns) return tons;
    return 0;
}

} // namespace

int64_t designScore(const Rules& r, const Design& d, const DesignStats& st, const DesignTemplate& t) {
    const int64_t attack = weaponStrength(r, d);
    const int64_t defense = st.structure + int64_t{st.shields} * 2 + int64_t{st.phasedShields} * 3;
    const int64_t cost = st.cost.total();
    const bool combat = isWeaponName(t.majority.ability) ||
                        std::any_of(t.mustHave.begin(), t.mustHave.end(), [](const std::string& a) { return isWeaponName(a); });
    if (combat) return attack * 3 + defense + int64_t{st.movement} * 15 + st.supplyCapacity / 50;
    const auto major = parseAbilityKind(t.majority.ability);
    if (st.canColonizeRock || st.canColonizeIce || st.canColonizeGas)
        return int64_t{st.movement} * 200 + st.supplyCapacity / 20 + int64_t{st.cargoCapacity} * 2 - cost / 20;
    if (keysEqual(t.name, "Scout")) return int64_t{st.movement} * 300 + st.supplyCapacity / 10 - cost / 20;
    if (major == AbilityKind::CargoStorage) return int64_t{st.cargoCapacity} * 10 + int64_t{st.movement} * 100 - cost / 20;
    int64_t special = 0;
    if (!t.majority.ability.empty())
        for (const auto& en : d.entries) special += abilityAmount(r.componentAbilities(en.component), t.majority.ability);
    return special * 50 + defense + int64_t{st.movement} * 100 - cost / 50;
}

std::optional<Design> buildDesign(const Rules& r, const Empire& e, const DesignTemplate& t, int cap) {
    std::optional<Design> best;
    int64_t bestScore = 0;
    int64_t bestCost = 0;
    for (uint32_t h = 0; h < r.data().vehicleSizes.size(); ++h) {
        const ruleset::VehicleSize& hull = r.hull(h);
        if (hull.type != t.vehicleType || !r.hullAvailable(e, h)) continue;
        if (hull.tonnage < t.minTonnage || hull.tonnage > t.maxTonnage) continue;
        if (cap > 0 && hull.tonnage > cap) continue;
        auto d = HullFiller(r, e, t, h).run();
        if (!d) continue;
        const DesignStats st = computeDesignStats(r, &e, *d);
        const int64_t score = designScore(r, *d, st, t);
        const int64_t cost = st.cost.total();
        if (!best || score > bestScore || (score == bestScore && cost < bestCost)) {
            best = std::move(d);
            bestScore = score;
            bestCost = cost;
        }
    }
    if (best) best->strategy = strategyIndex(e, t.defaultStrategy);
    return best;
}

void planDesigns(Planner& p) {
    // The design types this state's construction wants, plus the basics.
    std::vector<std::string> needed;
    auto want = [&](std::string type) {
        for (const auto& n : needed)
            if (keysEqual(n, type)) return;
        needed.push_back(std::move(type));
    };
    const Empire& e = p.emp();
    for (std::string_view surface : {"Rock", "Ice", "Gas"}) {
        const AbilityKind k = surface == "Rock" ? AbilityKind::ColonizeRock
                              : surface == "Ice" ? AbilityKind::ColonizeIce
                                                 : AbilityKind::ColonizeGas;
        for (uint32_t c = 0; c < p.r.data().components.size(); ++c)
            if (p.r.componentAvailable(e, c) && hasAbility(p.r.componentAbilities(c), k)) {
                want(colonyTypeName(surface));
                break;
            }
    }
    if (!p.neutral) want("Scout");
    want(p.neutral ? "Defense Base" : "Attack Ship");
    want("Defense Base");
    if (const VehicleQueue* q = p.prof.vehicleQueue(p.state))
        for (const auto& entry : q->entries)
            if (!keysEqual(entry.type, "Colonizer")) want(entry.type);

    const int cap = tonnageCap(p);
    const int margin = std::array{20, 10, 5, 5}[static_cast<size_t>(p.difficulty)];
    int created = 0;
    for (const std::string& type : needed) {
        if (created >= 3) break;
        const DesignTemplate* t = p.prof.design(type);
        if (!t) continue;
        if (p.neutral && t->vehicleType != VehicleType::Base && t->vehicleType != VehicleType::WeaponPlatform &&
            t->vehicleType != VehicleType::Satellite && !type.starts_with("Colony"))
            continue;
        auto candidate = buildDesign(p.r, p.emp(), *t, t->vehicleType == VehicleType::Ship ? cap : 0);
        if (!candidate) continue;
        const DesignStats st = computeDesignStats(p.r, &p.emp(), *candidate);
        const int64_t score = designScore(p.r, *candidate, st, *t);
        const std::vector<DesignId> existing = p.designsOfType(t->name);
        if (!existing.empty()) {
            const Design& old = p.st.design(existing.front());
            const int64_t oldScore = designScore(p.r, old, p.info(existing.front()).stats, *t);
            if (old.hull == candidate->hull && old.entries == candidate->entries) continue;
            if (score * 100 <= oldScore * (100 + margin) && !(oldScore <= 0 && score > oldScore)) continue;
        }
        // "Attack Ship 3": numbered after the designs of this type so far.
        int n = 1;
        for (DesignId d : p.emp().designs) n += keysEqual(p.info(d).aiType, t->name);
        auto taken = [&](const std::string& name) {
            for (DesignId d : p.emp().designs)
                if (p.st.design(d).name == name) return true;
            return false;
        };
        std::string name = std::format("{} {}", t->name, n);
        while (taken(name)) name = std::format("{} {}", t->name, ++n);
        candidate->name = std::move(name);
        if (!p.emit(cmd::CreateDesign{std::move(*candidate)})) continue;
        ++created;
        for (DesignId old : existing) p.emit(cmd::SetDesignObsolete{old, true});
    }
}

} // namespace opense4::game::ai::detail
