#include "game/rules.hpp"

#include "datafile/datafile.hpp"

#include <algorithm>
#include <format>

namespace opense4::game {

Rules::Rules(ruleset::Ruleset data, std::filesystem::path gameRoot) : data_(std::move(data)), gameRoot_(std::move(gameRoot)) {
    for (const auto& c : data_.components) components_.push_back(parseAbilities(c.abilities));
    for (const auto& f : data_.facilities) facilities_.push_back(parseAbilities(f.abilities));
    for (const auto& h : data_.vehicleSizes) hulls_.push_back(parseAbilities(h.abilities));
    for (const auto& t : data_.systemTypes) systemTypes_.push_back(parseAbilities(t.abilities));
    // The game folder's files: those the data set came with (mods layered
    // over the install), else the install at gameRoot.
    if (gameRoot_.empty() && data_.files) gameRoot_ = data_.files->root();
    if (!gameRoot_.empty() && (!data_.files || data_.files->root() != gameRoot_)) data_.files = ruleset::openInstallFiles(gameRoot_, data_.dataDir);
    if (data_.files) races_ = ruleset::loadRacePresets(*data_.files);
    // Rows past the ones the file lists read as amount 0 at 100 %; a sane
    // bound keeps a broken row count from filling memory.
    const int64_t rows = std::min<int64_t>(setting("Number Of Population Modifiers", 0), 10'000);
    for (int64_t row = 1; row <= rows; ++row)
        populationRows_.push_back({setting(std::format("Pop Modifier {} Population Amount", row), 0),
                                   static_cast<int>(setting(std::format("Pop Modifier {} Production Modifier Percent", row), 100)),
                                   static_cast<int>(setting(std::format("Pop Modifier {} SY Rate Modifier Percent", row), 100))});
}

std::optional<int64_t> Rules::declaredAbility(std::span<const ParsedAbility> list, std::string_view name) const {
    const ruleset::DeclaredAbility* d = data_.findDeclaredAbility(name);
    if (!d) return std::nullopt;
    int64_t sum = 0;
    std::optional<int64_t> best;
    for (const ParsedAbility& a : list) {
        if (a.kind != AbilityKind::Unknown || !datafile::keysEqual(a.raw, d->name)) continue;
        switch (d->combine) {
            case ruleset::Combine::Sum: sum = std::min(sum + a.value1, kAbilitySumCap); break;
            case ruleset::Combine::Max: best = best ? std::max(*best, a.value1) : a.value1; break;
            case ruleset::Combine::Min: best = best ? std::min(*best, a.value1) : a.value1; break;
            case ruleset::Combine::Count: break;
        }
    }
    return d->combine == ruleset::Combine::Sum ? sum : best.value_or(0);
}

std::optional<int64_t> Rules::declaredAbilityOfComponent(uint32_t component, std::string_view name) const {
    if (component >= components_.size()) return data_.findDeclaredAbility(name) ? std::optional<int64_t>(0) : std::nullopt;
    return declaredAbility(components_[component], name);
}

std::optional<int64_t> Rules::declaredAbilityOfFacility(uint32_t facility, std::string_view name) const {
    if (facility >= facilities_.size()) return data_.findDeclaredAbility(name) ? std::optional<int64_t>(0) : std::nullopt;
    return declaredAbility(facilities_[facility], name);
}

std::optional<int64_t> Rules::declaredAbilityOfHull(uint32_t hull, std::string_view name) const {
    if (hull >= hulls_.size()) return data_.findDeclaredAbility(name) ? std::optional<int64_t>(0) : std::nullopt;
    return declaredAbility(hulls_[hull], name);
}

std::optional<int64_t> Rules::declaredAbilityOfDesign(const Design& design, std::string_view name) const {
    std::vector<ParsedAbility> list;
    if (design.hull < hulls_.size()) list.insert(list.end(), hulls_[design.hull].begin(), hulls_[design.hull].end());
    for (const DesignEntry& e : design.entries)
        if (e.component < components_.size()) list.insert(list.end(), components_[e.component].begin(), components_[e.component].end());
    return declaredAbility(list, name);
}

std::optional<int64_t> Rules::declaredAbilityOfColony(const Colony& colony, std::string_view name) const {
    std::vector<ParsedAbility> list;
    for (uint32_t f : colony.facilities)
        if (f < facilities_.size()) list.insert(list.end(), facilities_[f].begin(), facilities_[f].end());
    return declaredAbility(list, name);
}

std::optional<int64_t> Rules::declaredAbilityOfSystem(const Galaxy& galaxy, SystemId system, std::string_view name) const {
    if (!system.valid() || system.index() >= galaxy.systems.size()) return data_.findDeclaredAbility(name) ? std::optional<int64_t>(0) : std::nullopt;
    const StarSystem& s = galaxy.system(system);
    std::vector<ParsedAbility> list = parseAbilities(s.abilities);
    for (ObjectId o : s.objects) {
        if (!o.valid() || o.index() >= galaxy.objects.size()) continue;
        const std::vector<ParsedAbility> own = parseAbilities(galaxy.object(o).abilities);
        list.insert(list.end(), own.begin(), own.end());
    }
    return declaredAbility(list, name);
}

bool Rules::meets(const Empire& e, std::span<const ruleset::TechRequirement> reqs) const {
    for (const auto& r : reqs)
        if (e.techLevel(r.area) < r.level) return false;
    return true;
}

bool Rules::mountAvailable(const Empire& e, uint32_t m) const { return m < data_.weaponMounts.size() && meets(e, data_.weaponMounts[m].requirements); }

bool Rules::designTechnology(const Empire& e, const Design& d) const {
    if (d.hull >= data_.vehicleSizes.size() || !hullAvailable(e, d.hull)) return false;
    for (const DesignEntry& entry : d.entries) {
        if (entry.component >= data_.components.size() || !componentAvailable(e, entry.component)) return false;
        if (entry.mount >= 0 && !mountAvailable(e, static_cast<uint32_t>(entry.mount))) return false;
    }
    return true;
}

bool Rules::techVisible(const GameState& s, const Empire& e, ruleset::TechAreaId a) const {
    return techAreaOpen(s, e, a) && meets(e, tech(a).requirements);
}

bool Rules::techAreaOpen(const GameState& s, const Empire& e, ruleset::TechAreaId a) const {
    const ruleset::TechArea& t = tech(a);
    if (!s.options.techAreasAllowed.empty() && a.index() < s.options.techAreasAllowed.size() && !s.options.techAreasAllowed[a.index()])
        return false;
    if (t.racialArea > 0) {
        bool ok = false;
        for (uint32_t ti : e.race.traits) {
            if (ti >= data_.racialTraits.size()) continue;
            const auto& trait = data_.racialTraits[ti];
            if (datafile::keysEqual(trait.traitType, "Tech Area") && !trait.values.empty() &&
                datafile::parseInteger(trait.values.front()).value_or(-1) == t.racialArea)
                ok = true;
        }
        if (!ok) return false;
    }
    if (t.uniqueArea > 0 &&
        std::find(e.uniqueAreasUnlocked.begin(), e.uniqueAreasUnlocked.end(), t.uniqueArea) == e.uniqueAreasUnlocked.end())
        return false;
    return true;
}

int64_t Rules::techLevelCost(ruleset::TechAreaId a, int level, int techCost) const {
    // Spec 05 §1.3 (confirmed: binary), with LC the area's Level Cost and L
    // the level being researched: Low LC × L, Medium max(LC × L, trunc(LC ×
    // L² / 2)), High LC × L². Every cost is capped.
    const int64_t lc = tech(a).levelCost;
    const int64_t l = level;
    int64_t cost = 0;
    switch (techCost) {
        case 0: cost = lc * l; break;
        case 2: cost = lc * l * l; break;
        default: cost = std::max(lc * l, lc * l * l / 2); break;
    }
    return std::min(cost, kMaxTechLevelCost);
}

std::optional<uint32_t> Rules::latestFacilityOfFamily(const Empire& e, int family) const {
    std::optional<uint32_t> best;
    for (uint32_t i = 0; i < data_.facilities.size(); ++i) {
        const auto& f = data_.facilities[i];
        if (f.family != family || !facilityAvailable(e, i)) continue;
        if (!best || f.romanNumeral > data_.facilities[*best].romanNumeral) best = i;
    }
    return best;
}

std::optional<uint32_t> Rules::componentUpgradeTarget(const Empire& e, int family) const {
    for (uint32_t i = static_cast<uint32_t>(data_.components.size()); i-- > 0;)
        if (data_.components[i].family == family && componentAvailable(e, i)) return i;
    return std::nullopt;
}

namespace {

template <class Family>
std::vector<uint32_t> lastOfFamilyRuns(std::span<const uint32_t> items, Family family) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < items.size(); ++i)
        if (i + 1 == items.size() || family(items[i + 1]) != family(items[i])) out.push_back(items[i]);
    return out;
}

} // namespace

std::vector<uint32_t> Rules::onlyLatestFacilities(std::span<const uint32_t> items) const {
    return lastOfFamilyRuns(items, [&](uint32_t i) { return data_.facilities[i].family; });
}

std::vector<uint32_t> Rules::onlyLatestComponents(std::span<const uint32_t> items) const {
    return lastOfFamilyRuns(items, [&](uint32_t i) { return data_.components[i].family; });
}

std::optional<uint32_t> Rules::bestFacilityWith(const Empire& e, AbilityKind k) const {
    std::optional<uint32_t> best;
    int64_t bestValue = 0;
    for (uint32_t i = 0; i < data_.facilities.size(); ++i) {
        if (!facilityAvailable(e, i) || !hasAbility(facilities_[i], k)) continue;
        const int64_t v = k == AbilityKind::SpaceYard ? spaceYardRates(facilities_[i]).total() : bestValue1(facilities_[i], k);
        if (!best || v > bestValue) {
            best = i;
            bestValue = v;
        }
    }
    return best;
}

int64_t Rules::traitValue(const Race& race, std::string_view traitType) const {
    int64_t total = 0;
    for (uint32_t ti : race.traits) {
        if (ti >= data_.racialTraits.size()) continue;
        const auto& t = data_.racialTraits[ti];
        if (datafile::keysEqual(t.traitType, traitType) && !t.values.empty()) total += datafile::parseInteger(t.values.front()).value_or(0);
    }
    return total;
}

bool Rules::hasTrait(const Race& race, std::string_view traitType) const {
    for (uint32_t ti : race.traits)
        if (ti < data_.racialTraits.size() && datafile::keysEqual(data_.racialTraits[ti].traitType, traitType)) return true;
    return false;
}

const ruleset::Culture* Rules::culture(const Race& race) const {
    return race.culture < data_.cultures.size() ? &data_.cultures[race.culture] : nullptr;
}

} // namespace opense4::game
