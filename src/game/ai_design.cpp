// Computer player: the Design minister (spec 05 §7.5 AI_DesignCreation,
// confirmed: binary unless marked).
//
// A template names a vehicle type, a tonnage window, must-have abilities,
// speeds, weapon-family preferences and "spaces per one" densities. The
// designer always takes the largest allowed hull, fills it in a fixed order
// and makes every older design of the same design type obsolete. There is no
// scout design type.

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::ai::detail {

namespace {

using datafile::keysEqual;
using ruleset::VehicleType;

bool isWeaponName(std::string_view ability) { return keysEqual(ability, "Weapon") || keysEqual(ability, "Weapons"); }

bool abilityMatches(const ParsedAbility& a, std::string_view ability) {
    const auto k = parseAbilityKind(ability);
    if (!k) return false;
    if (*k != AbilityKind::Unknown && *k != AbilityKind::AITag) return a.kind == *k;
    return keysEqual(a.raw, ability);
}

bool componentHas(const Rules& r, uint32_t c, std::string_view ability) {
    for (const ParsedAbility& a : r.componentAbilities(c))
        if (abilityMatches(a, ability)) return true;
    return false;
}

// Abilities whose parts are ranked by their Amount 1 (spec 05 §7.5).
bool amountAbility(AbilityKind k) {
    switch (k) {
        case AbilityKind::ShieldGeneration:
        case AbilityKind::PhasedShieldGeneration:
        case AbilityKind::CargoStorage:
        case AbilityKind::SupplyStorage:
        case AbilityKind::StandardShipMovement:
        case AbilityKind::LaunchRecoverFighters:
        case AbilityKind::LaunchRecoverSatellites:
        case AbilityKind::LaunchDrones:
        case AbilityKind::LayMines:
        case AbilityKind::MineSweeping:
        case AbilityKind::DropTroops: return true;
        default: return false;
    }
}

int64_t techSum(const Rules& r, uint32_t c) {
    int64_t n = 0;
    for (const auto& q : r.component(c).requirements) n += q.level;
    return n;
}

int64_t engineAmount(const Rules& r, uint32_t c) { return sumValue1(r.componentAbilities(c), AbilityKind::StandardShipMovement); }

struct Parts {
    const Rules& r;
    const Empire& e;
    VehicleType type;

    bool usable(uint32_t c) const { return (r.component(c).vehicles & ruleset::maskOf(type)) && r.componentAvailable(e, c); }

    // The best researched part with an ability; ties go to the later component.
    std::optional<uint32_t> best(std::string_view ability) const {
        const auto kind = parseAbilityKind(ability);
        if (!kind) return std::nullopt;
        std::optional<uint32_t> out;
        int64_t outScore = 0;
        for (uint32_t c = 0; c < r.data().components.size(); ++c) {
            if (!usable(c) || !componentHas(r, c, ability)) continue;
            const auto ab = r.componentAbilities(c);
            // A part that also has Space Yard or Cloak counts only for that ability.
            if (*kind != AbilityKind::SpaceYard && hasAbility(ab, AbilityKind::SpaceYard)) continue;
            if (*kind != AbilityKind::CloakLevel && hasAbility(ab, AbilityKind::CloakLevel)) continue;
            int64_t score = 0;
            if (*kind == AbilityKind::CloakLevel) {
                for (const ParsedAbility& a : ab)
                    if (a.kind == AbilityKind::CloakLevel) score += a.value2;
            } else if (amountAbility(*kind)) {
                score = sumValue1(ab, *kind);
            } else {
                score = techSum(r, c);
            }
            if (!out || score >= outScore) {
                out = c;
                outScore = score;
            }
        }
        return out;
    }
    std::optional<uint32_t> best(AbilityKind k) const { return best(identifier(k)); }

    // The first family pick with a researched weapon, and in it the weapon
    // with the highest tech-requirement sum.
    std::optional<uint32_t> weapon(const std::array<int, 5>& families) const {
        for (int family : families) {
            if (family == 0) continue;
            std::optional<uint32_t> out;
            for (uint32_t c = 0; c < r.data().components.size(); ++c) {
                const auto& comp = r.component(c);
                if (!comp.isWeapon() || comp.weapon.family != family || !usable(c)) continue;
                if (!out || techSum(r, c) >= techSum(r, *out)) out = c;
            }
            if (out) return out;
        }
        return std::nullopt;
    }

    std::optional<uint32_t> part(std::string_view ability, const std::array<int, 5>& families) const {
        return isWeaponName(ability) ? weapon(families) : best(ability);
    }
};

// Research event logged last turn (a new level, a new area, all projects done).
bool researchEventLastTurn(const Empire& e, uint32_t turn) {
    for (const LogEntry& l : e.log)
        if (l.category == LogCategory::Research && l.turn + 1 == turn) return true;
    return false;
}

bool colonizeAbility(std::string_view ability, AbilityKind& out) {
    const auto k = parseAbilityKind(ability);
    if (k == AbilityKind::ColonizeRock || k == AbilityKind::ColonizeIce || k == AbilityKind::ColonizeGas) {
        out = *k;
        return true;
    }
    return false;
}

bool majorityIs(const DesignTemplate& t, AbilityKind k) { return parseAbilityKind(t.majority.ability) == k; }

// Candidate hulls (spec 05 §7.5), in data order.
std::vector<uint32_t> candidateHulls(const Rules& r, uint32_t date, const Empire& e, const DesignTemplate& t, const SettingsTable& settings) {
    std::vector<uint32_t> out;
    AbilityKind colonize;
    const bool colonyMajority = colonizeAbility(t.majority.ability, colonize);
    for (uint32_t h = 0; h < r.data().vehicleSizes.size(); ++h) {
        const ruleset::VehicleSize& hull = r.hull(h);
        if (hull.type != t.vehicleType || !r.hullAvailable(e, h)) continue;
        if (hull.tonnage < t.minTonnage || hull.tonnage > t.maxTonnage) continue;
        if (hull.maxEngines < t.minSpeed) continue;
        if ((hull.maxPercentFighterBays > 0) != majorityIs(t, AbilityKind::LaunchRecoverFighters)) continue;
        if ((hull.maxPercentColonyModules > 0) != colonyMajority) continue;
        if ((hull.maxPercentCargo > 0) != majorityIs(t, AbilityKind::CargoStorage)) continue;
        bool capped = false;
        for (const auto& [amount, turns] : settings.tonnageCaps)
            if (amount > 0 && static_cast<int64_t>(date) < turns && hull.tonnage > amount) capped = true;  // the date the ministers see
        if (!capped) out.push_back(h);
    }
    return out;
}

std::optional<uint32_t> largestHull(const Rules& r, const std::vector<uint32_t>& hulls) {
    std::optional<uint32_t> best;
    for (uint32_t h : hulls)
        if (!best || r.hull(h).tonnage > r.hull(*best).tonnage) best = h;
    return best;
}

class Builder {
public:
    Builder(const Rules& r, const GameState& s, const Empire& e, const DesignTemplate& t, uint32_t hull)
        : r_(r), s_(s), e_(e), t_(t), hull_(r.hull(hull)), parts_{r, e, r.hull(hull).type} {
        d_.owner = e.id;
        d_.hull = hull;
        d_.designType = t.designType;
    }

    std::optional<Design> run() {
        control();
        engines(t_.minSpeed, false);
        hullParts();
        for (const std::string& a : t_.mustHave)
            if (!has(a)) add(parts_.part(a, t_.majorityFamilies));
        engines(t_.minSpeed, false);
        engines(t_.desiredSpeed, true);
        densities();
        fill();
        mounts();
        d_.strategy = strategy();
        if (!computeDesignStats(r_, &e_, d_).problems.empty() || d_.entries.empty()) return std::nullopt;
        return d_;
    }

private:
    const Rules& r_;
    [[maybe_unused]] const GameState& s_;
    const Empire& e_;
    const DesignTemplate& t_;
    const ruleset::VehicleSize& hull_;
    Parts parts_;
    Design d_;
    int used_ = 0;

    int count(uint32_t c) const {
        return static_cast<int>(std::count_if(d_.entries.begin(), d_.entries.end(), [&](const DesignEntry& en) { return en.component == c; }));
    }
    int engineCount() const {
        int n = 0;
        for (const auto& en : d_.entries) n += hasAbility(r_.componentAbilities(en.component), AbilityKind::StandardShipMovement);
        return n;
    }
    int64_t speed() const {
        int64_t n = 0;
        for (const auto& en : d_.entries) n += engineAmount(r_, en.component);
        return n;
    }
    bool fits(uint32_t c) const {
        const auto& comp = r_.component(c);
        if (used_ + comp.tonnage > hull_.tonnage || d_.entries.size() >= 500) return false;
        if (comp.maxPerVehicle > 0 && count(c) >= comp.maxPerVehicle) return false;
        if (hasAbility(r_.componentAbilities(c), AbilityKind::StandardShipMovement) && (!hull_.usesEngines || engineCount() >= hull_.maxEngines))
            return false;
        return true;
    }
    bool add(std::optional<uint32_t> c) {
        if (!c || !fits(*c)) return false;
        d_.entries.push_back({*c, -1});
        used_ += r_.component(*c).tonnage;
        return true;
    }
    bool has(std::string_view ability) const {
        for (const auto& en : d_.entries)
            if (isWeaponName(ability) ? r_.component(en.component).isWeapon() : componentHas(r_, en.component, ability)) return true;
        return false;
    }
    bool hasKind(AbilityKind k) const { return has(identifier(k)); }

    // A Master Computer replaces the bridge, life support and crew quarters.
    void control() {
        if (auto master = parts_.best(AbilityKind::MasterComputer)) {
            add(master);
            return;
        }
        if (hull_.mustHaveBridge) add(parts_.best(AbilityKind::ShipBridge));
        for (int i = 0; i < hull_.minLifeSupport; ++i) add(parts_.best(AbilityKind::ShipLifeSupport));
        for (int i = 0; i < hull_.minCrewQuarters; ++i) add(parts_.best(AbilityKind::ShipCrewQuarters));
    }

    // Speed counts engines (Standard Ship Movement Amount 1), not movement points.
    void engines(int target, bool desired) {
        if (!hull_.usesEngines) return;
        const auto engine = parts_.best(AbilityKind::StandardShipMovement);
        if (!engine) return;
        for (int tries = 0; tries < 100 && speed() < target; ++tries) {
            if (desired && engineCount() >= hull_.maxEngines) break;
            if (!add(engine)) break;
        }
    }

    void hullParts() {
        auto share = [&](int pct, AbilityKind k) {
            const auto part = parts_.best(k);
            if (!part) return;
            const int size = std::max(1, r_.component(*part).tonnage);
            const int n = static_cast<int>(xmath::pctTrunc(hull_.tonnage, pct) / size + 1);  // trunc(tonnage x pct / 100) / size + 1
            for (int i = 0; i < n; ++i) add(part);
        };
        if (hull_.maxPercentFighterBays > 0) share(hull_.maxPercentFighterBays, AbilityKind::LaunchRecoverFighters);
        if (hull_.maxPercentCargo > 0) share(hull_.maxPercentCargo, AbilityKind::CargoStorage);
        AbilityKind colonize;
        if (hull_.maxPercentColonyModules > 0 && colonizeAbility(t_.majority.ability, colonize)) add(parts_.best(colonize));
    }

    // "Spaces Per One" N: floor(hull tonnage / N) copies, at least one; 0 or less adds none.
    void density(std::optional<uint32_t> part, int spacesPerOne) {
        if (!part || spacesPerOne <= 0) return;
        const bool yard = hasAbility(r_.componentAbilities(*part), AbilityKind::SpaceYard);
        int copies = std::max(1, hull_.tonnage / spacesPerOne);
        if (yard) {  // one copy per step, none once the design has two
            int yards = 0;
            for (const auto& en : d_.entries) yards += hasAbility(r_.componentAbilities(en.component), AbilityKind::SpaceYard);
            if (yards >= 2) return;
            copies = 1;
        }
        for (int i = 0; i < copies; ++i) add(part);
    }

    std::optional<uint32_t> shieldPart() const {
        if (auto phased = parts_.best(AbilityKind::PhasedShieldGeneration)) return phased;
        return parts_.best(AbilityKind::ShieldGeneration);
    }

    void densities() {
        density(shieldPart(), t_.shieldsSpacesPerOne);
        density(parts_.best(AbilityKind::Armor), t_.armorSpacesPerOne);
        if (!t_.majority.ability.empty()) density(parts_.part(t_.majority.ability, t_.majorityFamilies), t_.majority.spacesPerOne);
        if (!t_.secondary.ability.empty()) density(parts_.part(t_.secondary.ability, t_.secondaryFamilies), t_.secondary.spacesPerOne);
        for (const DensityEntry& m : t_.misc) {
            const auto k = parseAbilityKind(m.ability);
            if (k == AbilityKind::CombatToHitOffensePlus && hasKind(AbilityKind::WeaponsAlwaysHit)) continue;
            if ((k == AbilityKind::EmergencyResupply || k == AbilityKind::SupplyStorage) && hasKind(AbilityKind::QuantumReactor)) continue;
            density(parts_.part(m.ability, t_.majorityFamilies), m.spacesPerOne);
        }
    }

    void fill() {
        if (!t_.majority.ability.empty() && t_.majority.spacesPerOne > 0) density(parts_.part(t_.majority.ability, t_.majorityFamilies), 10);
        density(shieldPart(), 10);
        density(parts_.best(AbilityKind::Armor), 10);
        // Any space left: the highest-numbered pure armor part.
        std::optional<uint32_t> armor;
        for (uint32_t c = 0; c < r_.data().components.size(); ++c) {
            const auto ab = r_.componentAbilities(c);
            if (parts_.usable(c) && !ab.empty() &&
                std::all_of(ab.begin(), ab.end(), [](const ParsedAbility& a) { return a.kind == AbilityKind::Armor; }))
                armor = c;
        }
        while (add(armor)) {}
    }

    // Each part takes the last mount the empire has that suits it; a mount
    // that would overfill the hull is left off (inferred).
    void mounts() {
        const auto& list = r_.data().weaponMounts;
        for (DesignEntry& en : d_.entries) {
            for (size_t m = list.size(); m-- > 0;) {
                const uint32_t mount = static_cast<uint32_t>(m);
                if (!r_.mountAvailable(e_, mount) || !mountAllowed(r_, d_.hull, en.component, mount)) continue;
                const int before = mounted(r_, en).tonnage;
                DesignEntry trial = en;
                trial.mount = static_cast<int32_t>(mount);
                const int after = mounted(r_, trial).tonnage;
                if (used_ - before + after > hull_.tonnage) continue;
                used_ += after - before;
                en.mount = trial.mount;
                break;
            }
        }
    }

    uint32_t strategy() const {
        const auto& list = e_.strategies;
        auto find = [&](std::string_view name) -> std::optional<uint32_t> {
            for (size_t i = 0; i < list.size(); ++i)
                if (keysEqual(list[i].name, name)) return static_cast<uint32_t>(i);
            return std::nullopt;
        };
        if (!t_.defaultStrategy.empty()) {
            if (auto i = find(t_.defaultStrategy)) return *i;
            if (keysEqual(t_.defaultStrategy, "Ram"))
                if (auto i = find("Kamikaze")) return *i;
        }
        // Otherwise the first strategy whose primary movement suits the type
        // (spec 05 §7.5 step 10, confirmed: binary): Don't Get Hurt for the
        // colony, warp, stellar, space-yard, mine, transport and carrier types
        // (a Satellite Layer counts with the carriers, inferred), Drop Troops
        // for troop ships, Board Enemy Ships for boarding ships, Ram for
        // drones, Optimal Weapons Range for all others. So transports, colony
        // ships and the other unarmed types never get Optimal, with which an
        // unarmed ship rams (spec 04 §16.1).
        const std::string_view type = t_.designType;
        std::string_view want = "Optimal Weapons Range";
        static constexpr std::array<std::string_view, 12> kCareful{"Colony", "Warp Point", "Planet", "Star",      "Storm",   "Nebulae",
                                                                   "Black Hole", "Space Yard", "Mine", "Transport", "Carrier", "Layer"};
        const std::string norm = datafile::normalizeKey(type);
        for (std::string_view k : kCareful)
            if (norm.find(datafile::normalizeKey(k)) != std::string::npos) want = "Don't Get Hurt";
        if (keysEqual(type, "Troop Transport") || keysEqual(type, "Troop")) want = "Drop Troops";
        if (keysEqual(type, "Boarding Ship")) want = "Board Enemy Ships";
        if (norm.find("drone") != std::string::npos && norm.find("carrier") == std::string::npos) want = "Ram";
        const std::string wanted = datafile::normalizeKey(want);
        for (size_t i = 0; i < list.size(); ++i)
            for (const auto& [key, value] : list[i].settings)
                if (keysEqual(key, "Primary Movement Strategy") && datafile::normalizeKey(value).starts_with(wanted)) return static_cast<uint32_t>(i);
        return 0;
    }
};

// Can the latest design of this template be improved (spec 05 §7.5)?
bool improvable(const Rules& r, const Empire& e, const Design& d, const DesignTemplate& t, const Parts& parts,
                const std::vector<uint32_t>& hulls) {
    const auto& comps = r.data().components;
    for (const DesignEntry& en : d.entries) {
        const auto& c = r.component(en.component);
        if (c.family == 0) continue;
        for (uint32_t o = 0; o < comps.size(); ++o)
            if (comps[o].family == c.family && comps[o].romanNumeral > c.romanNumeral && parts.usable(o)) return true;
    }
    auto outranks = [&](std::optional<uint32_t> weapon) {
        if (!weapon) return false;
        const int family = r.component(*weapon).weapon.family;
        int64_t best = -1;
        for (const DesignEntry& en : d.entries)
            if (r.component(en.component).isWeapon() && r.component(en.component).weapon.family == family)
                best = std::max(best, techSum(r, en.component));
        return techSum(r, *weapon) > best;
    };
    if (isWeaponName(t.majority.ability) && outranks(parts.weapon(t.majorityFamilies))) return true;
    if (isWeaponName(t.secondary.ability) && outranks(parts.weapon(t.secondaryFamilies))) return true;
    for (uint32_t h : hulls)
        if (r.hull(h).tonnage > r.hull(d.hull).tonnage) return true;
    const auto engine = parts.best(AbilityKind::StandardShipMovement);
    for (const DesignEntry& en : d.entries)
        if (hasAbility(r.componentAbilities(en.component), AbilityKind::StandardShipMovement) && engine && en.component != *engine) return true;
    auto designHas = [&](AbilityKind k) {
        return std::any_of(d.entries.begin(), d.entries.end(), [&](const DesignEntry& en) { return hasAbility(r.componentAbilities(en.component), k); });
    };
    if (t.shieldsSpacesPerOne > 0 && (parts.best(AbilityKind::ShieldGeneration) || parts.best(AbilityKind::PhasedShieldGeneration)) &&
        !designHas(AbilityKind::ShieldGeneration) && !designHas(AbilityKind::PhasedShieldGeneration))
        return true;
    const bool wantsReactor = std::any_of(t.misc.begin(), t.misc.end(), [](const DensityEntry& m) {
        return parseAbilityKind(m.ability) == AbilityKind::QuantumReactor;
    });
    if (wantsReactor && parts.best(AbilityKind::QuantumReactor) && !designHas(AbilityKind::QuantumReactor)) return true;
    (void)e;
    return false;
}

// "II" .. "XV" for the name rounds.
std::string roman(int n) {
    static constexpr std::array<std::pair<int, std::string_view>, 4> kDigits{{{10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}}};
    std::string out;
    for (const auto& [value, text] : kDigits)
        while (n >= value) {
            out += text;
            n -= value;
        }
    out.append(static_cast<size_t>(std::max(0, n)), 'I');
    return out;
}

// The name of a new design (spec 05 §7.5, confirmed: binary). The counter is
// the number of designs this empire's Design minister has made (those with a
// template name; saved with the game). The name is the first line of the
// race's design-name file whose position is beyond the counter and that no
// design of any empire uses; then rounds with "II" up to "XV", the position
// count carrying on across the rounds. With no file the name is "Design
// <counter + 1>", unchecked. With every name used the original leaves the
// name empty; OpenSE4 refuses a design without a name, so it takes "Design
// <counter + 1>" then (an OpenSE4 choice, spec 05 open question 37).
std::string designName(const Planner& p) {
    int64_t counter = 0;
    for (const Design& d : p.st.designs) counter += d.owner == p.id && !d.templateName.empty();
    const std::vector<std::string>& names = designNameList(p.r, p.emp().race.designNameFile);
    const int64_t n = static_cast<int64_t>(names.size());
    for (int round = 1; round <= 15 && n > 0; ++round)
        for (int64_t i = 0; i < n; ++i) {
            if ((round - 1) * n + i + 1 <= counter) continue;
            std::string name = round == 1 ? names[static_cast<size_t>(i)] : std::format("{} {}", names[static_cast<size_t>(i)], roman(round));
            if (!designNameInUse(p.st, name)) return name;
        }
    return std::format("Design {}", counter + 1);
}

} // namespace

std::optional<Design> buildDesign(const Rules& r, const GameState& s, const Empire& e, const DesignTemplate& t) {
    const auto hull = largestHull(r, candidateHulls(r, aiDate(s), e, t, profileFor(r, e).settings));
    if (!hull) return std::nullopt;
    return Builder(r, s, e, t, *hull).run();
}

void planDesigns(Planner& p) {
    const Empire& e = p.emp();
    if (!researchEventLastTurn(e, p.st.turn) && !e.designs.empty() && p.date % 10 != 0) return;
    for (const DesignTemplate& t : p.prof.designs) {
        const Parts parts{p.r, p.emp(), t.vehicleType};
        // The designer's latest non-obsolete design of this template.
        std::optional<DesignId> latest;
        for (DesignId d : p.emp().designs) {
            const Design& design = p.st.design(d);
            if (design.obsolete || !keysEqual(design.templateName, t.name)) continue;
            if (!latest || std::pair(design.createdTurn, d) > std::pair(p.st.design(*latest).createdTurn, *latest)) latest = d;
        }
        if (latest && p.st.design(*latest).createdTurn == p.st.turn) continue;
        bool partsExist = true;
        for (const std::string& a : t.mustHave)
            if (!parts.part(a, t.majorityFamilies)) partsExist = false;
        if (!partsExist) continue;
        const std::vector<uint32_t> hulls = candidateHulls(p.r, p.date, p.emp(), t, p.prof.settings);
        if (hulls.empty()) continue;
        if (latest && !improvable(p.r, p.emp(), p.st.design(*latest), t, parts, hulls)) continue;
        auto design = Builder(p.r, p.st, p.emp(), t, *largestHull(p.r, hulls)).run();
        if (!design) continue;
        if (latest && p.st.design(*latest).hull == design->hull && p.st.design(*latest).entries == design->entries) continue;
        design->name = designName(p);
        design->templateName = t.name;
        if (!p.emit(cmd::CreateDesign{std::move(*design)})) continue;
        const DesignId created = p.emp().designs.back();
        // Every older design of this player and design type becomes obsolete.
        std::vector<DesignId> older;
        for (DesignId d : p.emp().designs)
            if (d != created && !p.st.design(d).obsolete && keysEqual(p.st.design(d).designType, t.designType)) older.push_back(d);
        for (DesignId d : older) p.emit(cmd::SetDesignObsolete{d, true});
    }
}

} // namespace opense4::game::ai::detail
