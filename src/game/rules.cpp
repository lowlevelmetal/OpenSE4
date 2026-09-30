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
    if (!gameRoot_.empty()) races_ = ruleset::loadRacePresets(gameRoot_);
    // Rows past the ones the file lists read as amount 0 at 100 %; a sane
    // bound keeps a broken row count from filling memory.
    const int64_t rows = std::min<int64_t>(setting("Number Of Population Modifiers", 0), 10'000);
    for (int64_t row = 1; row <= rows; ++row)
        populationRows_.push_back({setting(std::format("Pop Modifier {} Population Amount", row), 0),
                                   static_cast<int>(setting(std::format("Pop Modifier {} Production Modifier Percent", row), 100)),
                                   static_cast<int>(setting(std::format("Pop Modifier {} SY Rate Modifier Percent", row), 100))});
}

bool Rules::meets(const Empire& e, std::span<const ruleset::TechRequirement> reqs) const {
    for (const auto& r : reqs)
        if (e.techLevel(r.area) < r.level) return false;
    return true;
}

bool Rules::mountAvailable(const Empire& e, uint32_t m) const { return m < data_.weaponMounts.size() && meets(e, data_.weaponMounts[m].requirements); }

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

std::optional<uint32_t> Rules::latestComponentOfFamily(const Empire& e, int family) const {
    std::optional<uint32_t> best;
    for (uint32_t i = 0; i < data_.components.size(); ++i) {
        const auto& c = data_.components[i];
        if (c.family != family || !componentAvailable(e, i)) continue;
        if (!best || c.romanNumeral > data_.components[*best].romanNumeral) best = i;
    }
    return best;
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
