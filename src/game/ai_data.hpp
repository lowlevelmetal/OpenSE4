#pragma once

// The computer player's data tables (docs/spec/05 §7.2-§7.5): the classic
// `<prefix>_AI_<Name>.txt` files, read at runtime from the player's install.
//
// Lookup (spec 05 §7.2): per-race files come from the race's own folder
// (Pictures/Races/<Race>/ or Pictures/RaceNeutral/<Race>/) and fall back to
// Ai/Default_AI_<Name>.txt; the global tables (construction, planet types,
// speech, strategies) always come from Ai/. A minister style reads its
// Anger/General/Politics/Settings/Speech from Ai/<Style>/. Anything missing
// uses the built-in defaults below (our own numbers).

#include "game/ai.hpp"
#include "game/types.hpp"
#include "ruleset/ruleset.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opense4::game::ai {

// A set of AI states (bit i = AiState i).
using StateMask = uint16_t;
inline constexpr StateMask kAllStates = static_cast<StateMask>((1u << kAiStates) - 1);
constexpr StateMask maskOf(AiState s) { return static_cast<StateMask>(1u << static_cast<unsigned>(s)); }
// "Exploration, Infrastructure, ..." -> mask. Unknown names are ignored.
StateMask parseStateList(std::string_view text);

// Messages the anger and politics tables name. The demand/request types have
// per-type politics rows (send/accept thresholds).
inline constexpr size_t kMessageTypes = static_cast<size_t>(MessageType::Count);
bool isDemand(MessageType t);  // DemandGift .. DemandStopAttacks

// ---- AI_Anger ------------------------------------------------------------------------------
struct AngerTable {
    int perAttackLocation = 1;
    int perNoTreatyShip = 1;
    int perAllyShip = 0;
    int perEnemyShip = 2;
    int minimum = 0;
    int regularDecrease = -2;
    int megaEvilEmpire = 40;
    int attackingWon = 2, attackingLost = 8, attackingStalemate = 1;
    int defendingWon = 2, defendingLost = 12, defendingStalemate = 1;
    int intelligenceAgainstUs = 4;
    std::array<int, kMessageTypes> receive{};      // "Receive <type>"
    int receiveAcceptTribute = -1;                 // replies to a tribute (AcceptGift/RefuseGift)
    int receiveRefuseTribute = 4;
};

// ---- AI_Politics ----------------------------------------------------------------------------
// One treaty rule's threshold modifiers (spec 05 §7.4).
struct TreatyRule {
    int baseAnger = 0;
    int perOtherWars = 0;
    int first50Turns = 0;
    int strongerPercent = 0, strongerAmount = 0;
    int weakerPercent = 0, weakerAmount = 0;
};

struct DemandRule {
    int sendScorePercent = 0;       // send when P <= this
    bool sendToFriend = false, sendToEnemy = false;
    int acceptScorePercent = 1000;  // accept when P >= this
    bool acceptFromFriend = false, acceptFromEnemy = false;
};

struct PoliticsTable {
    int demandingTonePercent = 75;
    int pleadingTonePercent = 130;
    Treaty highestAllowedTreaty = Treaty::Partnership;
    int turnsSinceWarBeforeFriendly = 12;

    TreatyRule accept{35, 25, 8, 160, 40, 60, -25};
    int acceptPerHigherLevel = -4;
    int acceptMinimumChance = 3;
    int acceptMinimumTurnsSinceTreaty = 8;
    int acceptSubjugationPercent = 700;
    int acceptProtectoratePercent = 500;

    int proposeChancePercent = 10;
    TreatyRule propose{28, 25, 8, 180, 40, 60, -25};
    std::vector<std::pair<Treaty, int>> proposeTypes;   // best first: treaty, anger below computed

    TreatyRule breakTreaty{65, 25, 0, 180, 40, 60, -25};
    TreatyRule declareWar{85, 20, 0, 180, 45, 60, -25};

    int maxAngerAcceptGift = 85;
    int maxAngerAcceptTribute = 85;
    std::array<DemandRule, kMessageTypes> demands{};    // only isDemand() entries are used

    int giftBaseFriend = 5000, giftBaseEnemy = 5000;
    int tributeBaseFriend = 5000, tributeBaseEnemy = 5000;
    int giftPerPercentFriend = 100, giftPerPercentEnemy = 50;
    int tributePerPercentFriend = 100, tributePerPercentEnemy = 50;
    int giftMaxAngerFriend = 70, giftMaxAngerEnemy = 70;
    int tributeMaxAngerFriend = 70, tributeMaxAngerEnemy = 70;
    int acceptTradeFriendPercent = 95;
    int acceptTradeEnemyPercent = 120;
};

// ---- AI_Settings --------------------------------------------------------------------------------
struct SettingsTable {
    std::array<std::pair<int, int>, 3> tonnageCaps{{{400, 15}, {700, 35}, {0, 0}}};  // (max tonnage, until turn)
    int turnsBetweenAttacks = 8;
    int maxMaintenancePercent = 75;
    int64_t maxResearchPoints = 1'000'000'000;
    int64_t maxIntelligencePoints = 1'000'000'000;
    int maxSystemsToDefend = 2;
    bool angryOverAlliedPlanets = true;
    bool angryOverEnemyPlanets = true;
    int alliedPlanetsPercent = 8;
    int enemyPlanetsPercent = 6;
    int personalityGroup = 0;
    bool avoidMinefields = false;
    bool avoidRestrictedSystems = false;
    bool clearOrdersOnEnemy = false;
    bool clearOrdersOnAll = false;
    int satellitesKeptPercent = 50;
    int dronesKeptPercent = 50;
    int antiShipDronesPerTarget = 2;
    int antiPlanetDronesPerTarget = 2;
    int antiShipDroneRange = 4;
    int antiPlanetDroneRange = 4;
};

// ---- AI_Fleets ------------------------------------------------------------------------------------
struct FleetDivision {
    int maxShips = 0;    // 0 = use maxPlanets
    int maxPlanets = 0;
    int fleets = 0;
};
struct FleetsTable {
    std::vector<FleetDivision> divisions{{15, 0, 2}, {40, 0, 4}, {90, 0, 6}, {1'000'000, 0, 8}};
    int percentInFleets = 75;
    int dontUseForTurns = 12;
    std::string defaultFormation;   // empty: the first formation
    std::string defaultStrategy;    // empty: the empire's first strategy
    int percentForDefense = 60;
};

// ---- AI_General ------------------------------------------------------------------------------------
struct GeneralInfo {
    std::string name, description, demeanor, culture;
};

// ---- Row tables -------------------------------------------------------------------------------------
struct ResearchRow {
    StateMask states = kAllStates;
    std::string area;
    int level = 0;           // 9999 = the area's maximum
    int minPercent = 25;
};

struct PlanetTypeRow {
    StateMask states = kAllStates;
    std::string type;
    int maxPerSystem = 100;
    int percentOfColonies = 100;
    std::string minimumSize;           // PlanetSize stellar size name, empty = any
    std::array<int, 3> values{};       // ratio thresholds (0 = unused)
    int maxInEmpire = 0;               // 0 = unlimited
};

struct FacilityEntry {
    std::string ability;               // ability identifier
    int amount = 1;
};
struct FacilityQueue {
    StateMask states = kAllStates;
    std::string queueType;             // "Homeworld" or a colony type
    std::vector<FacilityEntry> entries;
};

struct VehicleEntry {
    std::string type;                  // AI design type, or "Colonizer"
    int planetsPerItem = 0;            // tenths of a planet per item (0 = unused)
    int mustHave = 0;
};
struct VehicleQueue {
    StateMask states = kAllStates;
    std::vector<VehicleEntry> entries;
};

// ---- AI_DesignCreation -----------------------------------------------------------------------------
struct DensityEntry {
    std::string ability;               // ability identifier, or "Weapon"
    int spacesPerOne = 0;              // one component per this many kT of hull (0 = none)
};
struct DesignTemplate {
    std::string name;                  // the AI design type
    std::string designType;
    ruleset::VehicleType vehicleType = ruleset::VehicleType::Ship;
    std::string defaultStrategy;
    int minTonnage = 0, maxTonnage = 1'000'000;
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
    std::vector<std::string> sources;     // files read, for diagnostics ("built-in" when none)

    const DesignTemplate* design(std::string_view aiType) const;
    const VehicleQueue* vehicleQueue(AiState s) const;
    const FacilityQueue* facilityQueue(AiState s, std::string_view queueType) const;
};

// Built-in defaults: used when no install is present (tests, our own content).
const AiProfile& builtinProfile();

// Reads a profile from an install (uncached). `raceStyle` is the race's art
// folder ("Terran", "Neutral003"); `ministerStyle` is "Aggressive",
// "Defensive" or "Neutral" for human ministers, or empty for the race's own files.
AiProfile loadProfile(const std::filesystem::path& gameRoot, std::string_view raceStyle, std::string_view ministerStyle = {});

// Cached profile for this rules set (thread-safe; references stay valid for the
// life of the program).
const AiProfile& profileFor(const Rules& r, std::string_view raceStyle, std::string_view ministerStyle = {});
const AiProfile& profileFor(const Rules& r, const Empire& e);

// Name helpers shared by the tables.
bool parseTreatyName(std::string_view text, Treaty& out);   // accepts "Trade and Research Alliance"
std::string_view angerKeyName(MessageType t);               // "Give Gift", "Want a gift", ...

} // namespace opense4::game::ai
