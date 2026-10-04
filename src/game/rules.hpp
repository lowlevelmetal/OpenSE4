#pragma once

// Rules: the loaded data set plus derived, read-only caches the engine needs
// every turn (pre-parsed abilities, family tables, settings with defaults).
// Build once per game; never mutated during play.

#include "game/abilities.hpp"
#include "game/state.hpp"
#include "ruleset/races.hpp"
#include "ruleset/ruleset.hpp"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace opense4::game {

// The cap on the cost of one tech level (spec 05 §1.3, confirmed: binary).
inline constexpr int64_t kMaxTechLevelCost = 2'000'000'000;

class Rules {
public:
    Rules() = default;
    // gameRoot: directory holding Data/, Pictures/, Ai/ (empty for test data sets).
    Rules(ruleset::Ruleset data, std::filesystem::path gameRoot = {});

    const ruleset::Ruleset& data() const { return data_; }
    const std::filesystem::path& gameRoot() const { return gameRoot_; }
    const std::vector<ruleset::RacePreset>& racePresets() const { return races_; }

    // Pre-parsed abilities.
    std::span<const ParsedAbility> componentAbilities(uint32_t component) const { return components_[component]; }
    std::span<const ParsedAbility> facilityAbilities(uint32_t facility) const { return facilities_[facility]; }
    std::span<const ParsedAbility> hullAbilities(uint32_t hull) const { return hulls_[hull]; }
    std::span<const ParsedAbility> systemAbilities(uint32_t systemType) const { return systemTypes_[systemType]; }

    const ruleset::Component& component(uint32_t i) const { return data_.components[i]; }
    const ruleset::Facility& facility(uint32_t i) const { return data_.facilities[i]; }
    const ruleset::VehicleSize& hull(uint32_t i) const { return data_.vehicleSizes[i]; }
    const ruleset::TechArea& tech(ruleset::TechAreaId a) const { return data_.techAreas[a.index()]; }

    // Settings.txt with the documented stock defaults when a key is missing.
    int64_t setting(std::string_view key, int64_t fallback) const { return data_.settings.integer(key, fallback); }
    bool settingFlag(std::string_view key, bool fallback) const { return data_.settings.boolean(key, fallback); }

    // The Settings population rows (spec 02 §5.2) in file order, read once:
    // `Pop Modifier N Population Amount`, production and SY rate percent.
    struct PopulationRow {
        int64_t amount = 0;
        int production = 100;
        int shipyard = 100;
    };
    std::span<const PopulationRow> populationRows() const { return populationRows_; }

    // Technology gates.
    bool meets(const Empire& e, std::span<const ruleset::TechRequirement> reqs) const;
    bool componentAvailable(const Empire& e, uint32_t c) const { return meets(e, data_.components[c].requirements); }
    bool facilityAvailable(const Empire& e, uint32_t f) const { return meets(e, data_.facilities[f].requirements); }
    bool hullAvailable(const Empire& e, uint32_t h) const { return meets(e, data_.vehicleSizes[h].requirements); }
    bool mountAvailable(const Empire& e, uint32_t m) const;
    // The empire has the technology for the design's hull, every part and
    // every mount: what "its owner can still build it" means for design
    // knowledge (spec 05 §2.3, §8 "Design knowledge").
    bool designTechnology(const Empire& e, const Design& d) const;
    // A tech area this empire may research at all (allowed, racial/unique checks, requirements).
    bool techVisible(const GameState& s, const Empire& e, ruleset::TechAreaId a) const;
    // The same without the requirements: allowed in this game and passing the
    // racial and unique checks (spec 05 §1.2).
    bool techAreaOpen(const GameState& s, const Empire& e, ruleset::TechAreaId a) const;
    // Research points to go from `level - 1` to `level` under the Technology
    // Cost option (GameOptions::techCost: 0 low, 1 medium, 2 high), capped at
    // kMaxTechLevelCost (spec 05 §1.3).
    int64_t techLevelCost(ruleset::TechAreaId a, int level, int techCost) const;

    // Newest available facility/component of a family (highest Roman
    // Numeral): the target of upgrades (spec 02 §6.6) and of design Upgrade.
    std::optional<uint32_t> latestFacilityOfFamily(const Empire& e, int family) const;
    std::optional<uint32_t> latestComponentOfFamily(const Empire& e, int family) const;
    // Only Latest (spec 02 §6.4, confirmed: binary): `items` are data-file
    // indices in data-file order that passed a window's other filters; every
    // item whose next one in that list has the same family (`Facility
    // Family`, `Family`) is dropped, so of each run of neighbouring items of
    // one family only the last stays, in its place. Numerals and names play
    // no part, family 0 is an ordinary family, and a family split into
    // separate runs keeps one item per run.
    std::vector<uint32_t> onlyLatestFacilities(std::span<const uint32_t> items) const;
    std::vector<uint32_t> onlyLatestComponents(std::span<const uint32_t> items) const;
    // First facility (lowest numeral, available) that has ability `k`.
    std::optional<uint32_t> bestFacilityWith(const Empire& e, AbilityKind k) const;

    // Racial trait values: sum of Value 1 over the race's traits of this Trait Type.
    int64_t traitValue(const Race& race, std::string_view traitType) const;
    bool hasTrait(const Race& race, std::string_view traitType) const;
    const ruleset::Culture* culture(const Race& race) const;

private:
    ruleset::Ruleset data_;
    std::filesystem::path gameRoot_;
    std::vector<ruleset::RacePreset> races_;
    std::vector<std::vector<ParsedAbility>> components_, facilities_, hulls_, systemTypes_;
    std::vector<PopulationRow> populationRows_;
};

} // namespace opense4::game
