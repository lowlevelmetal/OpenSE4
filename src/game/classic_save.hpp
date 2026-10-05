#pragma once

// The original's saved games (`.gam`), docs/spec/08-saved-games.md: the
// container (§2), a typed model of every section (§3), import into a
// GameState (§6) and export from one (§7).
//
//   decodeClassicSave / encodeClassicSave   bytes <-> ClassicSave (the model)
//   importClassicSave                       ClassicSave -> GameState (+ notes)
//   exportClassicSave                       GameState -> ClassicSave (+ notes)
//   readClassicGame / writeClassicGame      the same, on files
//
// The model mirrors the file field by field, in file order, with the spec's
// meanings: players are 1-based player numbers (0 none), systems, objects
// and designs 1-based list positions, data-file records 1-based record
// positions, dates the date counter (24000 = 2400.0). Fields the original
// writes but never reads are kept so that a decoded file encodes again to the
// same values. Floats are kept as their 80-bit pattern; converting them is
// the only floating-point step, done in the exact x87 emulation of
// xmath.hpp, never in the host's floating point.

#include "game/state.hpp"
#include "game/xmath.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {
class Rules;
}

namespace opense4::game::classic {

// The only version this reader accepts (§5.2): every save of the last release.
inline constexpr std::string_view kVersion = "1.95";
// Player numbers run 1..20 (§3.6).
inline constexpr int kMaxPlayers = 20;
// The date counter of 2400.0 (§3): GameState::turn = date - kDateBase.
inline constexpr int32_t kDateBase = 24000;
// The turn counter of a new game (§3.1).
inline constexpr int32_t kTurnCounterBase = 367;
// "Any" as an intelligence project's specific target (§3.6.3).
inline constexpr int32_t kAnyTarget = 60000;
// Unlimited supply (§3.8.6).
inline constexpr int32_t kUnlimitedSupplyValue = 60000;

// ---- Values ------------------------------------------------------------------------------------

// The six numbers of the key header (§2.2): K1 selector, K2 first seed, K3
// and K4 unused, K5 mask, K6 the second seed for K1 (written, never read).
struct Keys {
    std::array<int32_t, 6> k{};
    bool operator==(const Keys&) const = default;
};
// The second seed the loader takes from its table by the selector (§2.2).
int32_t secondSeed(int32_t selector);
// Keys as the original draws them: K1 in 1..10, K2..K5 in 1..10000, K6 its
// table value. `seed` picks them (callers pass a random number).
Keys drawKeys(uint64_t seed);
// The fixed keys of the spec's test vector (§2.4).
Keys testVectorKeys();

// A float of the file: tag 5 and the 80-bit x87 extended value, kept as its
// ten bytes (little-endian: 64-bit significand, then sign and exponent).
struct Float80 {
    std::array<uint8_t, 10> bytes{};
    bool operator==(const Float80&) const = default;
};
Float80 toFloat80(xmath::Ext v);
xmath::Ext fromFloat80(const Float80& f);
// n / 10 rounded to a double and widened: how the export writes experience.
Float80 tenthsToFloat80(int64_t tenths);
// round(value × 10), ties to even: experience in tenths.
int64_t float80Tenths(const Float80& f);

// A set (§2.5): its capacity n and ceil(n / 32) words.
struct BitSet {
    uint16_t capacity = 0;
    std::vector<uint32_t> words;
    bool test(size_t i) const { return i < size_t{capacity} && i / 32 < words.size() && ((words[i / 32] >> (i % 32)) & 1u) != 0; }
    void set(size_t i);   // grows the capacity to i + 1 when needed
    static BitSet sized(uint16_t capacity);
    bool operator==(const BitSet&) const = default;
};

// An ability (§3.8.1): the built-in ability id (Appendix A), not a data position.
struct Ability {
    uint16_t id = 0;
    std::string description;
    int32_t value1 = 0;
    int32_t value2 = 0;
    bool operator==(const Ability&) const = default;
};
// Appendix A: the ability name of an id ("" past the table), and back (any
// case and spacing; nullopt for a name the table lacks).
std::string_view abilityName(uint16_t id);
std::optional<uint16_t> abilityId(std::string_view name);
inline constexpr uint16_t kAbilityIds = 169;

// ---- Shared records (§3.8.7, §3.8.8) ------------------------------------------------------------

struct PopulationEntry {
    uint8_t player = 0;
    int32_t millions = 0;
    bool operator==(const PopulationEntry&) const = default;
};
struct UnitEntry {
    uint16_t design = 0;   // design id; 65000 + race: militia
    uint16_t count = 0;
    uint16_t killed = 0;
    bool operator==(const UnitEntry&) const = default;
};
struct CargoRecord {
    std::vector<PopulationEntry> population;
    bool hasUnits = false;
    std::vector<UnitEntry> units;
    bool operator==(const CargoRecord&) const = default;
};
struct QueueEntry {
    uint8_t kind = 2;      // 1 facility, 2 vehicle or unit, 3 facility upgrade
    uint16_t item = 0;     // facility position or design id
    uint16_t count = 1;
    bool operator==(const QueueEntry&) const = default;
};
struct QueueRecord {
    bool onHold = false;
    bool emergency = false;
    bool repeat = false;
    uint8_t rallyWaypoint = 0;    // 1..10, 0 none
    uint8_t counter = 0;          // emergency or slow-mode turns
    std::array<int32_t, 3> spent{};
    std::vector<QueueEntry> items;
    bool operator==(const QueueRecord&) const = default;
};
struct OrderRecord {
    uint8_t kind = 0;
    uint8_t system = 0;
    uint8_t sector = 0;
    uint8_t extra = 0;
    uint16_t target = 0;
    std::string targetName;
    bool operator==(const OrderRecord&) const = default;
};
struct OrderList {
    bool repeat = false;
    uint16_t current = 1;   // 1-based
    std::vector<OrderRecord> orders;
    bool operator==(const OrderList&) const = default;
};

// ---- The summary, prologue and globals (§2.6, §3.1 - §3.5) ------------------------------------

struct SummaryRow {
    int number = 0;
    std::string name;     // "name type"
    std::string leader;   // "title name"
    std::string email;
    bool alive = true;
    bool operator==(const SummaryRow&) const = default;
};
struct Summary {
    std::string version{kVersion};
    int32_t date = kDateBase;
    int empires = 0;
    bool simultaneous = false;
    bool differentMachines = false;
    int humans = 0;
    std::vector<SummaryRow> rows;
    bool operator==(const Summary&) const = default;
};

struct EmpireCopy {
    std::string fullName, leader, raceFolder;
    bool operator==(const EmpireCopy&) const = default;
};
struct Prologue {
    bool unused1 = false, unused2 = false;
    bool autosave = false;
    uint8_t empireCountCopy = 0;
    uint8_t currentPlayerCopy = 0;
    int32_t dateCopy = kDateBase;
    std::string versionCopy{kVersion};
    bool handOver = false;
    std::vector<EmpireCopy> copies;   // empireCountCopy entries
    uint8_t empireCount = 0;          // the count used
    int32_t date = kDateBase;         // the game date
    int32_t turnCounter = kTurnCounterBase;
    std::string version{kVersion};
    bool operator==(const Prologue&) const = default;
};

struct Options {
    uint16_t quadrantType = 1;
    uint8_t quadrantSize = 2;
    bool allWarpPointsConnected = true, noWarpPoints = false, warpPointsAnywhere = false, allSystemsSeen = false, omnipresent = false,
         finiteResources = false, allPlanetsSameSize = true;
    uint8_t eventFrequency = 2, maxEventSeverity = 4, techCost = 2;
    std::vector<std::string> techAreasAllowed;   // removable areas that are allowed, alphabetical
    uint8_t startingResources = 2, homePlanetValue = 2, startingPlanets = 1;
    bool sameSystemAllowed = false, evenlyDistributed = true;
    uint8_t scoreDisplay = 2, startTechLevel = 1, racialPoints = 2;
    bool randomComputerEmpires = true, randomNeutralEmpires = true;
    uint8_t computerPlayers = 2, aiDifficulty = 2, aiBonus = 1;
    std::string gameMasterPassword;
    uint16_t maxUnitsPerPlayer = 2000, maxShipsPerPlayer = 200;
    bool cheatCodes = false, teamMode = false, noTacticalCombat = false, unused1 = false, unused2 = false, completeTechTree = false;
    bool allowGifts = true, allowTechTrades = true, allowSurrender = true, allowIntel = true, noRuins = false, onlyBreathable = false,
         onlyHomeType = false, playersCanSaveMap = false;
    uint8_t playStyle = 1;
    std::string gameName, saveFolder;
    uint8_t connection = 1;
    uint8_t autosaveTurns = 0;
    bool turnBased = true, simultaneous = false;
    int32_t replayCounter = 0, gameCode = 0, turnCode = 0, programSum = 0;
    std::array<int32_t, 7> checksums{};
    bool operator==(const Options&) const = default;
};

struct Victory {
    bool score = false;            int32_t scoreValue = 5'000'000;
    bool years = false;            int32_t yearsTurns = 100;
    bool percentOfSecond = false;  int32_t percentOfSecondValue = 300;
    bool techPercent = false;      uint16_t techPercentValue = 50;
    bool peace = false;            int32_t peaceTurns = 10;
    int32_t peacefulTurns = 0;
    bool delay = false;            int32_t delayTurns = 50;
    bool completed = false;
    bool operator==(const Victory&) const = default;
};

struct TimedEvent {
    uint16_t event = 0;     // Events.txt position, 0 free
    int32_t date = 0;
    uint8_t system = 0;
    uint8_t sector = 0;
    uint16_t target = 0;
    uint8_t player = 0;
    bool operator==(const TimedEvent&) const = default;
};

struct Globals {
    uint8_t viewSystem = 0;
    uint8_t viewSector = 0;
    uint8_t currentPlayer = 0;
    int32_t seed = 0;
    bool scenario = false;
    bool tutorial = false;
    std::string scenarioStem;
    uint16_t scenarioPage = 1;
    bool operator==(const Globals&) const = default;
};

struct SystemRecord {
    std::string name;
    uint8_t number = 0;
    uint8_t x = 1, y = 1;
    std::string typeDescription;
    uint8_t physicalType = 1;     // 1 Normal, 2 Nebulae, 3 Black Hole
    bool canStart = false, maskBackground = false, nonTiledCenter = false;
    std::string backgroundBitmap;
    bool changed = false;
    std::vector<Ability> abilities;
    BitSet explored, claimed;
    std::array<std::string, kMaxPlayers> notes;
    bool operator==(const SystemRecord&) const = default;
};

struct StartPointRecord {
    uint16_t system = 0;
    uint16_t sector = 0;
    uint8_t player = 0;   // 0 for the common list
    bool operator==(const StartPointRecord&) const = default;
};

// ---- Empires (§3.6) -------------------------------------------------------------------------------

struct ResearchItem {
    uint16_t area = 0;     // TechArea.txt position
    uint8_t weight = 2;
    int32_t spent = 0;
    bool operator==(const ResearchItem&) const = default;
};
struct IntelItem {
    uint16_t project = 0;  // IntelProjects.txt position
    uint8_t target = 0;    // player
    int32_t spent = 0;
    int32_t specific = kAnyTarget;
    bool operator==(const IntelItem&) const = default;
};
struct PoliticsEntry {
    uint8_t treaty = 3;    // 3: no contact
    bool dominant = false;
    uint16_t tradeCounter = 0;
    bool operator==(const PoliticsEntry&) const = default;
};
struct QueueTemplate {
    std::string name;
    QueueRecord queue;
    bool operator==(const QueueTemplate&) const = default;
};

struct EmpireOptions {
    uint8_t unusedA = 20, unusedB = 20;
    bool showLogAtStart = true;
    uint8_t pause = 0;
    bool confirmEndTurn = true, unused1 = false, confirmScrap = true, confirmStellar = true, confirmDeleteResearch = true,
         confirmDeleteIntel = true, confirmDeleteFirstQueueItem = true, noteSimilar = true, skipUnderConstruction = false,
         avoidTaggedMines = true, avoidRestricted = true, skipDamaged = false, stopOncePerLocation = false, skipInFleets = false,
         clearOnEnemy = true, clearOnAny = false, unused2 = false, warpPointNames = true, planetNames = false;
    bool facilityMarker1 = false;
    bool colonizableMarkers = true, coordinateLocation = true;
    std::array<bool, 11> facilityMarkers2to12{};
    bool galaxyGridLines = true, galaxyWarpLines = true, latestConstruction = false, latestComponents = false, designsStatsView = false,
         designsHideObsolete = false, chooseColonyType = true;
    uint8_t turnEndSystem = 0, turnEndSector = 0;
    bool autoClaim = true;
    uint8_t coloniesTab = 1, planetsTab = 1, shipsTab = 1, queuesTab = 1, setQueueTab = 1, designsTab = 1, politicsTab = 1, logFilter = 1,
            cargoTransferTab = 3, unitsTransferTab = 3;
    std::array<bool, 3> shipsShown{true, true, true};
    std::array<bool, 4> queuesShown{true, true, true, true};
    bool designerCondensed = false, designerToHit = false, galaxyNames = true, galaxyDistances = false, planetsHideAvoided = false,
         simulatorNoObsolete = false, systemGrid = false;
    bool replayFast = false, replayAnimate = true, replayGrid = false, replayViewRect = true;
    // Slot k (0..4) of the sort-key history: Colonies, Planets, Ships\Units, Construction Queues.
    std::array<std::array<uint8_t, 4>, 5> sortKeys{};
    bool operator==(const EmpireOptions&) const = default;
};

struct Waypoint10 {
    std::string name;
    uint8_t system = 0;
    uint8_t sector = 0;
    bool operator==(const Waypoint10&) const = default;
};

struct PackageItemRecord {
    std::string text;
    uint8_t kind = 0;
    uint16_t value = 0;
    uint8_t quantity = 0;
    bool operator==(const PackageItemRecord&) const = default;
};
struct MessageRecord {
    uint8_t type = 1;
    uint8_t sender = 0, recipient = 0;
    uint8_t tone = 2;
    uint8_t treaty = 0;
    uint8_t third = 0;
    uint16_t system = 0;
    uint16_t planet = 0;
    std::vector<PackageItemRecord> offered, requested;
    bool operator==(const MessageRecord&) const = default;
};
struct BattleShip {
    std::string name, hullCode;
    uint16_t hull = 0;
    bool operator==(const BattleShip&) const = default;
};
struct BattleSurvivor {
    std::string name;
    uint8_t damage = 0;
    bool operator==(const BattleSurvivor&) const = default;
};
struct BattleSide {
    uint8_t player = 0;
    bool tookPart = false;
    std::vector<BattleShip> forces;
    std::vector<BattleSurvivor> survivors;
    bool operator==(const BattleSide&) const = default;
};
struct BattleRecord {
    uint16_t number = 0;
    std::array<BattleSide, kMaxPlayers> sides{};
    bool operator==(const BattleRecord&) const = default;
};
struct LogRecord {
    uint8_t owner = 0;
    uint8_t system = 0, sector = 0;
    int32_t date = 0;
    std::string title, text;
    uint8_t target = 0;       // go-to: 0 none, 1 location, 2 queues, 3 research, 4 intelligence, 5 empire options, 6 designs, 7 empires
    uint16_t picture = 0;
    bool eventNotice = false;
    uint8_t otherEmpire = 0;
    uint8_t kind = 36;
    uint8_t category = 7;     // 1 Construction .. 7 Misc
    int32_t dateRead = 0;
    uint8_t eventKind = 0;
    uint16_t techArea = 0;
    std::optional<MessageRecord> message;
    std::optional<BattleRecord> battle;
    bool operator==(const LogRecord&) const = default;
};

struct FleetRecord {
    uint16_t number = 0;
    uint8_t owner = 0;
    std::string name;
    uint8_t system = 0, sector = 0;
    Float80 experience;
    uint16_t formation = 0;
    uint16_t strategy = 1;
    bool minister = false;
    uint16_t leader = 0;
    bool operator==(const FleetRecord&) const = default;
};

inline constexpr size_t kStrategyCategories = 14;
struct StrategyRecord {
    uint16_t position = 0;
    std::string name;
    uint8_t primary = 4, secondary = 6;
    bool typePriorityFirst = false;
    std::array<uint8_t, 4> targeting{11, 1, 0, 0};
    std::array<uint8_t, kStrategyCategories> typePriority{};
    std::array<bool, kStrategyCategories> dontFireOn{};
    uint16_t fighterGroup = 10;
    uint16_t dronesPerTarget = 3;
    std::array<bool, kStrategyCategories> breakFormation{};
    std::array<uint8_t, 4> damagePercent{100, 100, 100, 100};
    bool damageUntilWeaponsGone = false;
    bool operator==(const StrategyRecord&) const = default;
};

struct EmpireRecord {
    // §3.6.1
    std::string leaderName, leaderTitle, name, type;
    uint8_t player = 0;
    bool computer = false;
    bool useRaceMinisterStyle = false;
    std::string raceFolder, artFolder, emblemFolder, shipNameFile;
    uint8_t homeSystem = 0, homeSector = 0;
    uint8_t atmosphere = 3, surface = 1;
    std::string biology, society, history, demeanor, happinessType;
    int32_t experience = 0;
    uint16_t defaultFormation = 1, defaultStrategy = 1, planetStrategy = 1;
    bool neutral = false;
    uint8_t difficulty = 2;
    uint8_t unusedByte = 100;
    uint16_t unusedWord1 = 0, unusedWord2 = 4;
    uint8_t maintenancePercent = 0, reproductionPercent = 0;
    std::array<int32_t, 3> stored{};
    int32_t researchPoints = 0, intelPoints = 0;
    std::array<bool, 3> canColonize{};
    std::array<bool, 5> breathes{};
    std::string password, email;
    // §3.6.2
    bool repeatResearch = false, researchEvenly = true;
    std::vector<uint16_t> uniqueAreas;
    std::vector<uint16_t> techLevels;
    std::vector<ResearchItem> research;
    // §3.6.3
    std::vector<IntelItem> intel;
    bool repeatIntel = false, intelEvenly = true;
    // §3.6.4
    std::array<PoliticsEntry, kMaxPlayers> politics{};
    // §3.6.5
    uint16_t culture = 0;
    std::vector<bool> traits;          // one per RacialTraits.txt record (the count is the data set's)
    std::array<int32_t, 15> characteristics{};
    // §3.6.6
    std::vector<std::string> designTypes, colonyTypes;
    std::vector<QueueTemplate> queueTemplates;
    // §3.6.7
    EmpireOptions options;
    // §3.6.8
    std::array<Waypoint10, 10> waypoints{};
    std::vector<std::string> repairPriorities;
    std::vector<std::pair<uint8_t, uint8_t>> taggedMinefields;
    std::vector<uint8_t> systemsToAvoid;
    // §3.6.9
    uint8_t aiState = 1, staging = 0, secured = 0, incursion = 0;
    uint16_t turnsInState = 0, afterAttack = 0;
    std::vector<uint8_t> defend, targets;
    std::string ministerStyle;
    std::array<bool, 25> ministers{};
    bool ministersForNewVehicles = false, aiMinimalChanges = false;
    std::array<uint8_t, kMaxPlayers> anger{};
    std::array<uint16_t, kMaxPlayers> turnsSinceWar{};
    uint8_t zero = 0;
    std::array<uint8_t, 7> unused7{};
    uint16_t shipNameIndex = 0;
    std::array<bool, 8> enemyCapabilities{};
    uint16_t droneNameCounter = 0;
    // §3.6.10
    bool destroyed = false;
    std::string networkName;
    // §3.6.11 - §3.6.13
    std::vector<LogRecord> log;
    std::vector<FleetRecord> fleets;
    uint16_t fleetsCreated = 0;
    std::vector<StrategyRecord> strategies;
    bool operator==(const EmpireRecord&) const = default;
};

// ---- Designs (§3.7) ------------------------------------------------------------------------------------

struct DesignPart {
    uint16_t component = 0;  // Components.txt position
    uint8_t mount = 0;       // CompEnhancement.txt position, 0 none
    bool operator==(const DesignPart&) const = default;
};
struct DesignRecord {
    uint16_t id = 0;
    uint8_t owner = 0;       // 0: a free slot
    uint16_t hull = 0;
    std::string type, templateName, name;
    int32_t created = 0;
    bool obsolete = false;
    bool everBuilt = false;
    uint8_t speed = 0;
    std::array<int32_t, 3> cost{};
    std::vector<DesignPart> parts;
    uint16_t strategy = 1;
    uint16_t typeCode = 1;
    std::array<int32_t, kMaxPlayers> lastSeen{};
    std::vector<Ability> abilities;
    int32_t built = 0, lost = 0, scrapped = 0, tonnageDestroyed = 0;
    bool changed = false;
    bool operator==(const DesignRecord&) const = default;
};

// ---- Space objects (§3.8) -------------------------------------------------------------------------------

enum class ObjectClass : uint8_t { Star = 1, WarpPoint, Storm, Planet, Ship, Comet, MineField, SatelliteGroup, FighterGroup, DroneGroup };
std::string_view displayName(ObjectClass c);

struct FacilityEntry {
    uint16_t facility = 0;   // Facility.txt position
    uint8_t count = 0;
    uint8_t destroyed = 0;
    bool operator==(const FacilityEntry&) const = default;
};
struct ColonyRecord {
    uint8_t owner = 0;
    std::string type;
    std::vector<PopulationEntry> population;
    uint8_t anger = 25;
    uint8_t plague = 0;
    bool cloaked = false;
    uint8_t atmosphereTurns = 0;
    CargoRecord cargo;
    std::vector<FacilityEntry> facilities;
    QueueRecord queue;
    std::vector<UnitEntry> landedTroops;
    uint8_t invader = 0;
    uint16_t militia = 0;
    OrderList orders;
    bool capital = false;
    bool minister = false;
    bool operator==(const ColonyRecord&) const = default;
};

// One object of the list; which fields are written depends on the class.
struct ObjectRecord {
    uint8_t cls = 0;            // ObjectClass, the byte before the record
    uint8_t recordClass = 0;    // the record's own class byte (the same)
    uint16_t id = 0;
    uint8_t system = 0, sector = 0;
    std::vector<Ability> abilities;    // stellar objects; ships (always empty)
    uint16_t sectorType = 0;           // stars, warp points, storms, planets, comets
    std::string name;                  // stars, planets, ships, drone groups
    uint8_t destSystem = 0, destSector = 0;   // warp points
    bool changed = false;              // planets, ships, unit groups
    Float80 conditions;                // planets
    std::array<int32_t, 3> value{};
    std::optional<ColonyRecord> colony;
    Float80 dayAccumulator;            // ships, fighter and drone groups
    uint16_t design = 0;               // ships
    uint8_t owner = 0;                 // ships, unit groups
    uint8_t heading = 0;
    uint8_t movement = 0, maxMovement = 0;
    int32_t supply = 0;
    uint8_t status = 0;
    Float80 experience;
    bool cloaked = false;
    uint16_t fleet = 0;
    bool minister = false;
    OrderList orders;
    CargoRecord cargo;
    BitSet destroyedParts;
    std::optional<QueueRecord> queue;
    std::vector<UnitEntry> units;      // unit groups

    ObjectClass objectClass() const { return static_cast<ObjectClass>(cls); }
    // A blank (a free slot), by the rules of §3.8.
    bool blank() const;
    bool operator==(const ObjectRecord&) const = default;
};

struct LaunchRecord {
    uint16_t launcher = 0;   // object id
    uint8_t kind = 0;        // 3 fighters, 4 satellites, 5 mines, 7 drones
    uint16_t count = 0;
    bool operator==(const LaunchRecord&) const = default;
};

// ---- The whole file ----------------------------------------------------------------------------------------

struct ClassicSave {
    Keys keys;
    std::string version{kVersion};
    Summary summary;
    Prologue prologue;
    Options options;
    Victory victory;
    std::vector<TimedEvent> events;
    Globals globals;
    std::vector<SystemRecord> systems;
    std::vector<StartPointRecord> specificStarts;
    std::vector<StartPointRecord> commonStarts;
    std::vector<EmpireRecord> empires;
    std::vector<DesignRecord> designs;
    std::vector<ObjectRecord> objects;
    std::vector<LaunchRecord> launched;
    // Not in the file: the racial-trait count it was read with (§3.6.5).
    size_t traitCount = 0;
    bool operator==(const ClassicSave&) const = default;
};

// ---- The container ---------------------------------------------------------------------------------------------

// A file starts with the original's key header: six compact integers, the
// first a selector of 0..255 (§2.2). OpenSE4's own saves start with "OSE4".
bool looksLikeClassicSave(std::span<const uint8_t> bytes);

// Decodes a file. `traitCount` is the number of RacialTraits.txt records of
// the data set (§3.6.5): the file does not store it. Errors name the section,
// the record and the byte where reading failed.
std::expected<ClassicSave, std::string> decodeClassicSave(std::span<const uint8_t> bytes, size_t traitCount);
// When a file does not decode with the data set's trait count, the counts
// with which it does (0..64), to tell a data-set mismatch from damage.
std::vector<size_t> traitCountsThatDecode(std::span<const uint8_t> bytes);
// Encodes with save.keys (draw new ones with drawKeys). The summary is
// written from save.summary as it is. Fails on values the format cannot
// hold (a list longer than its count, a byte out of range).
std::expected<std::vector<uint8_t>, std::string> encodeClassicSave(const ClassicSave& save);

// The key stream of §2.3 / §2.4, for tests: the first `n` values of rows 1,
// 57 and 123, and the character keys of `text` followed by the next number key.
struct KeyRows {
    std::vector<uint8_t> r1, r57, r123;
};
KeyRows keyRows(const Keys& keys, size_t n);

// The plain-text summary block of §2.6, laid out.
std::string summaryText(const Summary& s);

// ---- Import and export -------------------------------------------------------------------------------------------

// What a conversion approximated or dropped: short lines for the player
// (`notes`) and every detail (`details`, for the log). Counts are merged:
// "12 load orders mapped by cargo kind".
struct ConversionReport {
    std::vector<std::string> notes;
    std::vector<std::string> details;
    void note(std::string line);
    void detail(std::string line);
};

// The data set must be the one the save was made with: the same record counts
// (tech areas, racial traits) and every data position inside it. Errors name
// the record. On success the state has gone through OpenSE4's load-time
// recomputation (sight, colony cloaks, reports).
std::expected<GameState, std::string> importClassicSave(const Rules& rules, const ClassicSave& save, ConversionReport& report);

struct ExportOptions {
    uint64_t keySeed = 0;     // draws the keys (drawKeys)
};
std::expected<ClassicSave, std::string> exportClassicSave(const Rules& rules, const GameState& s, ConversionReport& report,
                                                          const ExportOptions& options = {});

// Files.
std::expected<GameState, std::string> readClassicGame(const Rules& rules, const std::filesystem::path& file, ConversionReport& report);
std::expected<void, std::string> writeClassicGame(const Rules& rules, const GameState& s, const std::filesystem::path& file,
                                                  ConversionReport& report, const ExportOptions& options = {});

// A readable summary of a decoded file (the converter's --info): versions,
// date, empires, counts per section and object class.
std::string describe(const ClassicSave& save);

// Differences between two decoded files, field by field ("empire 2 > log
// entry 3 > kind: 4 != 36"), at most `limit` lines. Keys are ignored.
std::vector<std::string> compareSaves(const ClassicSave& a, const ClassicSave& b, size_t limit = 200);

// The design type code of §3.7 for a design type name (one of the 39 the
// computer players know); nullopt for another name.
std::optional<uint16_t> designTypeCode(std::string_view designType);
std::string_view designTypeName(uint16_t code);

} // namespace opense4::game::classic
