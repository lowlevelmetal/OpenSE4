#pragma once

// The rules database: technologies, hulls, components, facilities, races and
// starting designs. Loaded from the TOML files in data/ so the game is moddable
// in the same spirit as SE4's text data files. Immutable once loaded.

#include "sim/types.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace opense4::sim {

// Abilities are the data-driven building blocks of components and facilities.
// Game code only ever asks "how much of ability X does this thing have".
enum class AbilityType : uint8_t {
    Movement,       // ship thrust; speed = total / hull engines_per_move
    Command,        // bridge
    LifeSupport,
    CrewQuarters,
    SupplyStorage,
    CargoStorage,
    Colonize,       // param = planet surface ("rock", "ice", "gas")
    Sensor,
    ShieldGeneration,
    ProduceMinerals,
    ProduceOrganics,
    ProduceRadioactives,
    ProduceResearch,
    SpaceYard,      // construction rate per resource per turn; enables ship building
    Count
};

std::string_view displayName(AbilityType t);
std::optional<AbilityType> parseAbilityType(std::string_view key);

struct Ability {
    AbilityType type = AbilityType::Count;
    int amount = 0;
    std::string param;
};

struct TechRequirement {
    TechIndex tech;
    int level = 1;
};

struct TechDef {
    std::string key;
    std::string name;
    std::string category;
    std::string description;
    int maxLevel = 1;
    int64_t baseCost = 100;
    std::vector<TechRequirement> prerequisites;

    // Research points needed to go from level-1 to `level`.
    int64_t costForLevel(int level) const { return baseCost * level; }
};

struct HullDef {
    std::string key;
    std::string name;
    std::string description;
    int size = 0;            // tonnage capacity (kT)
    int structure = 0;       // base hit points
    Resources cost;
    int enginesPerMove = 1;  // thrust needed per point of speed
    int maxEngines = 0;
    std::vector<TechRequirement> prerequisites;
};

struct WeaponStats {
    int damage = 0;
    int range = 0;  // sectors; used by tactical combat
};

struct ComponentDef {
    std::string key;
    std::string name;
    std::string description;
    int tonnage = 0;
    int structure = 0;
    Resources cost;
    std::vector<Ability> abilities;
    std::optional<WeaponStats> weapon;
    std::vector<TechRequirement> prerequisites;

    int abilityTotal(AbilityType t) const;
    bool hasAbility(AbilityType t) const { return abilityTotal(t) > 0; }
};

struct FacilityDef {
    std::string key;
    std::string name;
    std::string description;
    Resources cost;
    std::vector<Ability> abilities;
    std::vector<TechRequirement> prerequisites;

    int abilityTotal(AbilityType t) const;
};

struct RaceDef {
    std::string key;
    std::string name;
    std::string empireName;
    std::string description;
    uint32_t color = 0xffffff;  // 0xRRGGBB
    PlanetSurface nativeSurface = PlanetSurface::Rock;
    Atmosphere breathes = Atmosphere::Oxygen;
    ComponentIndex colonyComponent;  // substituted for "{colony_module}" in designs
    std::vector<TechRequirement> bonusTechs;
};

// A starting ship design, given to every empire at game start.
struct DesignTemplate {
    std::string name;
    std::string role;  // "scout", "colony", "warship" - used by the AI
    HullIndex hull;
    // Invalid index = the race's colony component placeholder.
    std::vector<ComponentIndex> components;
};

struct StartingShip {
    std::string design;
    int count = 1;
};

struct Rules {
    int sectorRadius = 6;
    int defaultSystemCount = 40;

    Resources startingResources;
    int64_t homeworldPopulation = 2000;
    std::vector<FacilityIndex> homeworldFacilities;
    std::vector<TechRequirement> startingTechs;
    std::vector<StartingShip> startingShips;

    int64_t colonyStartPopulation = 100;
    int popGrowthPercent = 5;
    int hostileAtmospherePopPercent = 25;
    std::array<int64_t, enumIndex(PlanetSize::Count)> maxPopulation{500, 1000, 2000, 3000, 4000};
    std::array<int, enumIndex(PlanetSize::Count)> facilitySlots{3, 5, 8, 11, 14};

    int64_t baseColonyConstructionRate = 100;
    std::vector<AbilityType> requiredShipAbilities;

    int combatRounds = 10;
};

class Content {
public:
    std::vector<TechDef> techs;
    std::vector<HullDef> hulls;
    std::vector<ComponentDef> components;
    std::vector<FacilityDef> facilities;
    std::vector<RaceDef> races;
    std::vector<DesignTemplate> designTemplates;
    Rules rules;

    const TechDef& tech(TechIndex i) const { return techs[i.index()]; }
    const HullDef& hull(HullIndex i) const { return hulls[i.index()]; }
    const ComponentDef& component(ComponentIndex i) const { return components[i.index()]; }
    const FacilityDef& facility(FacilityIndex i) const { return facilities[i.index()]; }
    const RaceDef& race(RaceIndex i) const { return races[i.index()]; }

    std::optional<TechIndex> findTech(std::string_view key) const;
    std::optional<HullIndex> findHull(std::string_view key) const;
    std::optional<ComponentIndex> findComponent(std::string_view key) const;
    std::optional<FacilityIndex> findFacility(std::string_view key) const;
    std::optional<RaceIndex> findRace(std::string_view key) const;
    const DesignTemplate* findDesignTemplate(std::string_view name) const;

    // Rebuilds the key -> index maps. Called by the loader.
    void buildIndices();

private:
    std::unordered_map<std::string, uint32_t> techIndex_, hullIndex_, componentIndex_, facilityIndex_, raceIndex_;
};

// Loads every data file from `dataDir`. On failure returns a list of
// human-readable problems (file, entry and field), so modders get useful errors.
std::expected<Content, std::vector<std::string>> loadContent(const std::filesystem::path& dataDir);

} // namespace opense4::sim
