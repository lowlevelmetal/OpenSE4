#pragma once

// The computer player's data tables (docs/spec/05 §7.2-§7.5): the classic
// `<prefix>_AI_<Name>.txt` files, read at runtime from the player's install.
//
// Lookup (spec 05 §7.2, confirmed: binary): one rule for all twelve tables.
// An empire with a minister style reads Ai/<Style>/<Style>_AI_<Name>.txt, any
// other empire its race's file (Pictures/Races/<Race>/ or
// Pictures/RaceNeutral/<Race>/). When that file does not exist it reads
// Ai/Default_AI_<Name>.txt. There is no fallback from a style folder to the
// race folder. Without an install the built-in tables below (our own
// numbers) are used.
//
// Missing keys take the defaults the spec lists (§7.5 AI_Settings; 0 for the
// row tables and the design templates).

#include "game/ai.hpp"
#include "game/types.hpp"
#include "ruleset/ruleset.hpp"

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opense4::game::ai {

// A set of AI states (bit i = AiState i).
using StateMask = uint16_t;
inline constexpr StateMask kAllStates = static_cast<StateMask>((1u << kAiStates) - 1);
constexpr StateMask maskOf(AiState s) { return static_cast<StateMask>(1u << static_cast<unsigned>(s)); }
// The states a row's `AI State` text applies to (spec 05 §7.5, confirmed:
// binary): a state matches when its name occurs anywhere in the text, case
// sensitive. So "Attack" also matches "Prepare for Attack".
StateMask parseStateList(std::string_view text);

// Messages the anger and politics tables name. The demand/request types have
// per-type politics rows (send/accept flags).
inline constexpr size_t kMessageTypes = static_cast<size_t>(MessageType::Count);
bool isDemand(MessageType t);  // DemandGift .. DemandStopAttacks

// ---- Fixed lists built into the game ---------------------------------------------------------

// The 39 AI design types (spec 05 §7.7). None of them is a scout.
std::span<const std::string_view> aiDesignTypes();
bool isAiDesignType(std::string_view name);

// The nine colony types the ministers know (spec 05 §7.5).
enum class ColonyType : uint8_t {
    Homeworld, Mining, Farming, Refining, ResupplyBase, ResearchCompound, IntelligenceCompound, ConstructionYard,
    MilitaryInstallation, Count
};
std::string_view displayName(ColonyType t);
// "Imperial Center" is Homeworld; any other label is ColonyType::Count.
ColonyType parseColonyType(std::string_view text);

// ---- AI_Anger ------------------------------------------------------------------------------
struct AngerTable {
    int perAttackLocation = 1;
    int perNoTreatyShip = 1;
    int perAllyShip = 0;  // read but never used (spec 05 §7.3)
    int perEnemyShip = 2;
    int minimum = 0;
    int regularDecrease = -2;
    int megaEvilEmpire = 5;
    int attackingWon = 2, attackingLost = 8, attackingStalemate = 1;
    int defendingWon = 2, defendingLost = 12, defendingStalemate = 1;
    int intelligenceAgainstUs = 4;
    std::array<int, kMessageTypes> receive{};      // "Receive <type>"
    int receiveAcceptTribute = -1;                 // replies to a tribute (AcceptGift/RefuseGift)
    int receiveRefuseTribute = 4;
};

// ---- AI_Politics ----------------------------------------------------------------------------
// One treaty rule's threshold terms (spec 05 §7.4).
struct TreatyRule {
    int baseAnger = 0;
    int perOtherWars = 0;
    int first50Turns = 0;
    int strongerPercent = 0, strongerAmount = 0;
    int weakerPercent = 0, weakerAmount = 0;
};

// Demands and requests. `Score Percent to Send` has no effect (spec 05 §7.4)
// and is not kept.
struct DemandRule {
    bool sendToFriend = false, sendToEnemy = false;
    int acceptScorePercent = 1000;  // accept when P >= this
    bool acceptFromFriend = false, acceptFromEnemy = false;
};

struct PoliticsTable {
    int demandingTonePercent = 75;
    int pleadingTonePercent = 130;
    Treaty highestAllowedTreaty = Treaty::Partnership;
    int turnsSinceWarBeforeFriendly = 12;

    TreatyRule accept{55, -10, 5, 150, 10, 70, -10};
    int acceptPerHigherLevel = -5;
    int acceptMinimumChance = 5;          // a floor on the threshold, not a probability
    int acceptMinimumTurnsSinceTreaty = 8;
    int acceptSubjugationPercent = 700;
    int acceptProtectoratePercent = 500;

    int proposeChancePercent = 10;
    TreatyRule propose{45, -10, 5, 150, 10, 70, -10};
    std::vector<std::pair<Treaty, int>> proposeTypes;   // file order: treaty, "Anger Level Below Computed"

    TreatyRule breakTreaty{85, 5, 0, 150, 10, 70, -10};
    TreatyRule declareWar{95, 5, 0, 150, 10, 70, -10};

    int maxAngerAcceptGift = 85;
    int maxAngerAcceptTribute = 85;
    std::array<DemandRule, kMessageTypes> demands{};    // only isDemand() entries are used

    int giftBaseFriend = 5, giftBaseEnemy = 5;
    int tributeBaseFriend = 5, tributeBaseEnemy = 5;
    int giftPerPercentFriend = 1, giftPerPercentEnemy = 1;
    int tributePerPercentFriend = 1, tributePerPercentEnemy = 1;
    int giftMaxAngerFriend = 60, giftMaxAngerEnemy = 40;
    int tributeMaxAngerFriend = 60, tributeMaxAngerEnemy = 40;
    int acceptTradeFriendPercent = 95;
    int acceptTradeEnemyPercent = 120;
};

// ---- AI_Settings --------------------------------------------------------------------------------
// Defaults are the spec's defaults for absent keys (spec 05 §7.5).
struct SettingsTable {
    std::array<std::pair<int, int>, 3> tonnageCaps{};  // (max tonnage, turns); each applies when Amount > 0
    int turnsBetweenAttacks = 0;
    int maxMaintenancePercent = 80;
    int64_t maxResearchPoints = 300'000;
    int64_t maxIntelligencePoints = 300'000;
    int maxSystemsToDefend = 3;
    bool angryOverAlliedPlanets = true;
    bool angryOverEnemyPlanets = false;
    int alliedPlanetsPercent = 5;
    int enemyPlanetsPercent = 5;
    int personalityGroup = 0;
    bool avoidMinefields = false;
    bool avoidRestrictedSystems = false;
    bool clearOrdersOnEnemy = false;
    bool clearOrdersOnAll = false;
    int satellitesKeptPercent = 40;
    int dronesKeptPercent = 40;
    int antiShipDronesPerTarget = 3;
    int antiPlanetDronesPerTarget = 3;
    int antiShipDroneRange = 5;
    int antiPlanetDroneRange = 5;
};

// ---- AI_Fleets ------------------------------------------------------------------------------------
struct FleetDivision {
    int maxShips = 0;    // <= 0: compare maxPlanets with our planet count instead
    int maxPlanets = 0;
    int fleets = 0;
};
struct FleetsTable {
    std::vector<FleetDivision> divisions{{15, 0, 2}, {40, 0, 4}, {90, 0, 6}, {1'000'000, 0, 8}};
    int percentInFleets = 60;
    int dontUseForTurns = 30;
    std::string defaultFormation;   // empty: the first formation
    std::string defaultStrategy;    // empty: the empire's first strategy
    int percentForDefense = 40;
};

// ---- AI_General ------------------------------------------------------------------------------------
struct GeneralInfo {
    std::string name, description, demeanor, culture;
};

// ---- Row tables -------------------------------------------------------------------------------------
struct ResearchRow {
    StateMask states = 0;
    std::string area;
    int level = 0;           // 9999 = the area's maximum
    int minPercent = 0;
};

struct PlanetTypeRow {
    StateMask states = 0;
    std::string type;
    int maxPerSystem = 0;              // 0 = no limit
    int percentOfColonies = 0;         // 0 = no limit
    std::string minimumSize;           // PlanetSize.txt entry, empty = any
    std::array<int, 3> values{};       // minerals, organics, radioactives; only values above 100 count
    int maxInEmpire = 0;               // 0 = no limit
};

struct FacilityEntry {
    std::string ability;               // ability identifier
    int amount = 0;                    // 0: the entry is never used
};
struct FacilityQueue {
    StateMask states = 0;
    std::string queueType;             // "Homeworld" or a colony type
    std::vector<FacilityEntry> entries;
};

struct VehicleEntry {
    std::string type;                  // AI design type, "Colonizer", or text matched against the designs
    int planetsPerItem = 0;            // tenths of a planet per item (0 = unused)
    int mustHave = 0;
};
struct VehicleQueue {
    StateMask states = 0;
    std::vector<VehicleEntry> entries;
};

// ---- AI_DesignCreation -----------------------------------------------------------------------------
struct DensityEntry {
    std::string ability;               // ability identifier, or "Weapon"
    int spacesPerOne = 0;              // floor(hull tonnage / N) copies, at least one; 0 or less: none
};
struct DesignTemplate {
    std::string name;                  // the AI design type
    std::string designType;
    ruleset::VehicleType vehicleType = ruleset::VehicleType::Ship;
    std::string defaultStrategy;
    int minTonnage = 0, maxTonnage = 0;
    std::vector<std::string> mustHave;
    int minSpeed = 0, desiredSpeed = 0;
    std::array<int, 5> majorityFamilies{};
    std::array<int, 5> secondaryFamilies{};
    int shieldsSpacesPerOne = 0;
    int armorSpacesPerOne = 0;
    DensityEntry majority;
    DensityEntry secondary;
    std::vector<DensityEntry> misc;
};

// ---- AI_Speech ----------------------------------------------------------------------------------------
struct Speech {
    std::unordered_map<std::string, std::vector<std::string>> pools;  // normalized pool name -> lines
    const std::vector<std::string>* pool(std::string_view name) const;
};

// ---- The whole profile ---------------------------------------------------------------------------------
struct AiProfile {
    AngerTable anger;
    PoliticsTable politics;
    SettingsTable settings;
    FleetsTable fleets;
    GeneralInfo general;
    std::vector<ResearchRow> research;
    std::vector<PlanetTypeRow> planetTypes;
    std::vector<FacilityQueue> facilities;
    std::vector<VehicleQueue> vehicles;
    std::vector<DesignTemplate> designs;
    Speech speech;
    std::vector<ruleset::CombatStrategy> strategies;
    int unitReservePercent = 0;           // `_AI_Construction_Units`: resources to reserve for units
    std::vector<std::string> sources;     // files read, for diagnostics ("built-in" when none)

    const DesignTemplate* design(std::string_view aiType) const;
    // The last table in the file whose states include `s` (no fallback row).
    const VehicleQueue* vehicleQueue(AiState s) const;
    const FacilityQueue* facilityQueue(AiState s, std::string_view queueType) const;
};

// Built-in defaults: used when no install is present (tests, our own content).
const AiProfile& builtinProfile();

// Reads a profile from an install (uncached). `raceStyle` is the race's art
// folder ("Terran", "Neutral003"); `ministerStyle` is a folder under Ai/
// ("Aggressive", ...) or empty for the race's own files.
AiProfile loadProfile(const std::filesystem::path& gameRoot, std::string_view raceStyle, std::string_view ministerStyle = {});

// Cached profile for this rules set (thread-safe; references stay valid for the
// life of the program).
const AiProfile& profileFor(const Rules& r, std::string_view raceStyle, std::string_view ministerStyle = {});
// The empire's own tables: its minister style when it has one, else its race's.
const AiProfile& profileFor(const Rules& r, const Empire& e);

// The race's design-name file (Dsgnname/<file> in the install), one name per
// line; empty without an install or file. Cached.
const std::vector<std::string>& designNameList(const Rules& r, std::string_view file);

// Name helpers shared by the tables.
bool parseTreatyName(std::string_view text, Treaty& out);   // accepts "Trade and Research Alliance"
std::string_view angerKeyName(MessageType t);               // "Give Gift", "Want a gift", ...

} // namespace opense4::game::ai
