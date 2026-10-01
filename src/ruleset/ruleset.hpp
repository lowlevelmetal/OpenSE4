#pragma once

// The complete, typed rules database of a classic-format data set: everything
// under a data set's Data/ directory. This mirrors the data files one-to-one;
// game rules interpret it (see docs/spec/).
//
// The same loader reads the player's own installed copy of the original game
// data at runtime, or our original content written in the same format. No
// original game data ships with this project.

#include "core/id.hpp"
#include "datafile/reader.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace opense4::ruleset {

using TechAreaId = Id<struct TechAreaTag>;
using VehicleSizeId = Id<struct VehicleSizeTag>;
using ComponentId = Id<struct ComponentTag>;
using FacilityId = Id<struct FacilityTag>;
using SystemTypeId = Id<struct SystemTypeTag>;
using StellarAbilityTypeId = Id<struct StellarAbilityTypeTag>;

struct Cost {
    int64_t minerals = 0;
    int64_t organics = 0;
    int64_t radioactives = 0;
};

struct TechRequirement {
    TechAreaId area;
    int level = 0;
};

// One "Ability N ..." entry. Values are kept as written: many abilities leave
// them blank, and a few use them as text.
struct Ability {
    std::string type;  // identifier, e.g. "Supply Storage"
    std::string description;
    std::string value1;
    std::string value2;

    int64_t number1() const;  // blank or non-numeric -> 0
    int64_t number2() const;
};

enum class VehicleType : uint8_t { Ship, Base, Fighter, Satellite, Mine, Troop, Drone, WeaponPlatform, Count };
using VehicleTypeMask = uint16_t;
constexpr VehicleTypeMask maskOf(VehicleType t) { return static_cast<VehicleTypeMask>(1u << static_cast<unsigned>(t)); }
std::string_view displayName(VehicleType t);

// ---- TechArea.txt -----------------------------------------------------------
struct TechArea {
    std::string name;
    std::string group;
    std::string description;
    int maxLevel = 0;
    int64_t levelCost = 0;
    int startLevel = 0;
    int raiseLevel = 0;
    int racialArea = 0;  // non-zero: only races with the matching trait may research it
    int uniqueArea = 0;  // non-zero: only available via special means (ruins etc.)
    bool canBeRemoved = true;
    std::vector<TechRequirement> requirements;
};

// ---- VehicleSize.txt ----------------------------------------------------------
struct VehicleSize {
    std::string name;
    std::string shortName;
    std::string description;
    std::string code;
    std::string primaryBitmap;
    std::string alternateBitmap;
    VehicleType type = VehicleType::Ship;
    int tonnage = 0;
    Cost cost;
    int enginesPerMove = 0;
    std::vector<TechRequirement> requirements;
    std::vector<Ability> abilities;

    // Design rules for this hull.
    bool mustHaveBridge = false;
    bool canHaveAuxControl = false;
    int minLifeSupport = 0;
    int minCrewQuarters = 0;
    bool usesEngines = false;
    int maxEngines = 0;
    int maxPercentFighterBays = 0;
    int maxPercentColonyModules = 0;
    int maxPercentCargo = 0;
};

// ---- Components.txt -------------------------------------------------------------
enum class WeaponKind : uint8_t { None, DirectFire, Seeking, Warhead, PointDefense };

struct Weapon {
    WeaponKind kind = WeaponKind::None;
    std::vector<std::string> targets;  // "Ships", "Planets", "Ftr", "Sat", "Seekers", "Drone"
    std::vector<int> damageAtRange;    // index = range in squares
    std::string damageType;            // identifier, e.g. "Skips Armor"
    int reloadRate = 0;
    std::string displayType;
    std::string display;
    int modifier = 0;
    std::string sound;
    int family = 0;
    int seekerSpeed = 0;
    int seekerDamageResistance = 0;
};

struct Component {
    std::string name;
    std::string description;
    int picture = 0;  // index into the component icon sheet
    int tonnage = 0;
    int structure = 0;
    Cost cost;
    VehicleTypeMask vehicles = 0;
    std::string vehicleDescription;  // shown in reports when the list override is used
    int supplyUsed = 0;
    int maxPerVehicle = 0;  // 0 = unlimited
    std::string generalGroup;
    int family = 0;
    int romanNumeral = 0;
    int customGroup = 0;
    std::vector<TechRequirement> requirements;
    std::vector<Ability> abilities;
    Weapon weapon;

    bool isWeapon() const { return weapon.kind != WeaponKind::None; }
};

// ---- Facility.txt ---------------------------------------------------------------
struct Facility {
    std::string name;
    std::string description;
    std::string group;
    int family = 0;
    int romanNumeral = 0;
    std::string restriction;  // e.g. "None", or a per-planet / per-system / per-empire limit
    int picture = 0;
    Cost cost;
    std::vector<TechRequirement> requirements;
    std::vector<Ability> abilities;
};

// ---- PlanetSize.txt ---------------------------------------------------------------
struct PlanetSize {
    std::string name;
    std::string physicalType;  // "Planet", "Asteroids", ...
    std::string stellarSize;   // "Tiny" ... "Huge"
    int maxFacilities = 0;
    int maxPopulation = 0;
    int maxCargo = 0;
    int maxFacilitiesDomed = 0;
    int maxPopulationDomed = 0;
    int maxCargoDomed = 0;
    bool constructed = false;  // ringworlds / sphereworlds
    int specialAbilityId = 0;
};

// ---- RacialTraits.txt ---------------------------------------------------------------
struct RacialTrait {
    std::string name;
    std::string description;
    int picture = 0;
    std::string generalType;
    int cost = 0;
    std::string traitType;
    std::vector<std::string> values;
    std::vector<std::string> requiredTraits;
    std::vector<std::string> restrictedTraits;
};

// ---- Cultures.txt (all modifiers are percentages) ---------------------------------------
struct Culture {
    std::string name;
    std::string description;
    int production = 0;
    int research = 0;
    int intelligence = 0;
    int trade = 0;
    int spaceCombat = 0;
    int groundCombat = 0;
    int happiness = 0;
    int maintenance = 0;
    int shipyardRate = 0;
    int repair = 0;
};

// ---- SectType.txt: the catalog of sector object appearances --------------------------
struct SectorObjectType {
    std::string physicalType;  // "Planet", "Star", "Storm", "Asteroids", "Warp Point", "Destroyed Star"
    int picture = 0;
    std::string description;
    // Attributes that apply to the physical type (others are empty).
    std::string planetSize, planetPhysicalType, planetAtmosphere;
    std::string starSize, starAge, starColor, starLuminosity;
    std::string stormSize;
    std::string combatTile;
    std::string warpPointSize;
    bool warpPointOneWay = false;
    bool unusual = false;
};

// ---- SystemTypes.txt ------------------------------------------------------------------
struct SystemObjectTemplate {
    std::string physicalType;
    std::string position;  // "Ring 3", "Circle Radius 4", ...
    std::string stellarAbilityType;
    std::string size, age, color, luminosity, atmosphere, composition;
};

struct SystemType {
    std::string name;
    std::string description;
    std::string physicalType;
    std::string backgroundBitmap;
    bool empiresCanStartIn = false;
    bool maskBackgroundObjects = false;
    bool nonTiledCenterPicture = false;  // the Stellar Manipulation preview shows the plain star field (docs/spec/06 §5.3)
    std::vector<Ability> abilities;
    std::string warpPointStellarAbilityType;
    std::vector<SystemObjectTemplate> objects;
};

// ---- QuadrantTypes.txt ------------------------------------------------------------------
struct QuadrantType {
    std::string name;
    std::string description;
    int minDistanceBetweenSystems = 0;
    std::string systemPlacement;  // "Random", "Spiral", "Clusters", "Diffuse", "Grid"
    int maxWarpPointsPerSystem = 0;
    int minAngleBetweenWarpPoints = 0;
    std::vector<std::pair<SystemTypeId, int>> systemTypeChances;
};

// ---- StellarAbilityTypes.txt ------------------------------------------------------------
struct StellarAbilityType {
    std::string name;
    std::vector<std::pair<int, Ability>> possibleAbilities;  // (percent chance, ability)
};

// ---- CompEnhancement.txt: weapon mounts ------------------------------------------------
struct WeaponMount {
    std::string longName;
    std::string shortName;
    std::string description;
    std::string code;
    int costPercent = 100;
    int tonnagePercent = 100;
    int structurePercent = 100;
    int damagePercent = 100;
    int supplyPercent = 100;
    int shieldPercent = 100;             // optional; missing means 100 (spec 03 §2.4)
    int rangeModifier = 0;
    int toHitModifier = 0;
    int minimumVehicleSize = 0;
    int maximumVehicleSize = 0;          // optional; 0 means no upper bound
    std::vector<int> familyRequirement;  // allowed component families; empty = every family
    std::string weaponTypeRequirement;
    std::string vehicleType;
    std::vector<TechRequirement> requirements;
};

// ---- Formations.txt ------------------------------------------------------------------
struct FormationSlot {
    int x = 0;
    int y = 0;
    std::string designType;
};

struct Formation {
    std::string name;
    std::string description;
    FormationSlot leader;
    std::vector<FormationSlot> positions;
};

// ---- Happiness.txt: mood models -------------------------------------------------------
struct HappinessModel {
    std::string name;
    std::string description;
    int maxPositiveChange = 0;
    int maxNegativeChange = 0;
    std::vector<std::pair<std::string, int>> triggers;  // event identifier -> mood change
};

// ---- IntelProjects.txt ------------------------------------------------------------------
struct Message {
    std::string title;
    std::string text;
};

struct IntelProject {
    std::string name;
    std::string description;
    std::string group;
    int64_t cost = 0;
    std::string type;
    int effectAmount = 0;
    std::vector<std::string> sourceMessages;
    std::vector<Message> targetMessages;
    std::string sourcePicture;
    std::string targetPicture;
    std::vector<TechRequirement> requirements;
};

// ---- Events.txt --------------------------------------------------------------------------
struct EventType {
    std::string type;
    std::string severity;
    int effectAmount = 0;
    std::string messageTo;
    std::vector<Message> messages;
    std::string picture;
    int turnsToComplete = 0;
    std::vector<Message> startMessages;
};

// ---- DefaultStrategies.txt: combat behavior presets --------------------------------------
// Kept as ordered key/value pairs for now; typed when combat AI lands.
struct CombatStrategy {
    std::string name;
    std::vector<std::pair<std::string, std::string>> settings;
};

// ---- Settings.txt: global tunables ---------------------------------------------------------
class Settings {
public:
    void set(std::string key, std::string value);
    bool has(std::string_view key) const;
    std::optional<std::string> text(std::string_view key) const;
    int64_t integer(std::string_view key, int64_t fallback = 0) const;
    bool boolean(std::string_view key, bool fallback = false) const;
    size_t size() const { return values_.size(); }

private:
    static std::string normalize(std::string_view key);
    std::unordered_map<std::string, std::string> values_;
};

struct NameLists {
    std::vector<std::string> empireNames, empireTypes, emperorNames, emperorTitles, demeanors, systemNames, repairPriorities;
    std::vector<std::string> designTypes, colonyTypes;  // DefaultDesignTypes.txt / DefaultColonyTypes.txt
};

struct Ruleset {
    std::filesystem::path dataDir;

    std::vector<TechArea> techAreas;
    std::vector<VehicleSize> vehicleSizes;
    std::vector<Component> components;
    std::vector<Facility> facilities;
    std::vector<PlanetSize> planetSizes;
    std::vector<RacialTrait> racialTraits;
    std::vector<Culture> cultures;
    std::vector<SectorObjectType> sectorObjectTypes;
    std::vector<SystemType> systemTypes;
    std::vector<QuadrantType> quadrantTypes;
    std::vector<StellarAbilityType> stellarAbilityTypes;
    std::vector<WeaponMount> weaponMounts;
    std::vector<Formation> formations;
    std::vector<HappinessModel> happinessModels;
    std::vector<IntelProject> intelProjects;
    std::vector<EventType> eventTypes;
    std::vector<CombatStrategy> combatStrategies;
    Settings settings;
    NameLists names;

    std::optional<TechAreaId> findTechArea(std::string_view name) const;
    std::optional<SystemTypeId> findSystemType(std::string_view name) const;
    std::optional<StellarAbilityTypeId> findStellarAbilityType(std::string_view name) const;
    const Component* findComponent(std::string_view name) const;
    const Facility* findFacility(std::string_view name) const;
    const VehicleSize* findVehicleSize(std::string_view name) const;

    const TechArea& techArea(TechAreaId id) const { return techAreas[id.index()]; }

    // Rebuilds the name lookups after the vectors change (the loader calls this).
    void reindex();

private:
    std::unordered_map<std::string, uint32_t> techIndex_, systemIndex_, stellarIndex_, componentIndex_, facilityIndex_, vehicleIndex_;
};

struct LoadResult {
    std::optional<Ruleset> ruleset;  // absent only if a required file could not be read
    datafile::Diagnostics diagnostics;
};

// Loads every data file of a data set from `dataDir` (the directory holding
// Components.txt etc.).
LoadResult loadRuleset(const std::filesystem::path& dataDir);

// Finds the data directory of an installed classic game, if any: checks
// `hint` first, then common Steam library locations.
std::optional<std::filesystem::path> findInstalledDataDir(const std::filesystem::path& hint = {});

} // namespace opense4::ruleset
