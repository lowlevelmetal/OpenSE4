#pragma once

// Complete state of one classic-rules game. Plain data: rules live in the
// subsystem modules (economy, movement, combat, research, ...), all state
// changes by players go through commands (commands.hpp), and all randomness
// goes through GameState::rng. Everything here is serialized: when adding a
// field, add it to its struct's io() list in serialize_io.hpp (a test fails
// until you do, and the golden-checksum test prints the new value).

#include "core/rng.hpp"
#include "game/galaxy.hpp"
#include "game/types.hpp"
#include "ruleset/ruleset.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace opense4::game {

// ---- Races and empires ------------------------------------------------------------

struct Race {
    std::string name;   // e.g. "Terran"
    std::string style;  // art set: folder under Pictures/Races (or RaceGeneric)
    std::string biology, society, history;
    std::array<int, kCharacteristics> characteristics = [] {
        std::array<int, kCharacteristics> a{};
        a.fill(100);  // every characteristic defaults to 100 %
        return a;
    }();
    std::vector<uint32_t> traits;    // RacialTraits.txt indices
    uint32_t culture = 0;            // Cultures.txt index
    uint32_t happinessModel = 0;     // Happiness.txt index
    std::string nativeSurface = "Rock";   // "Rock", "Ice", "Gas Giant"
    std::string atmosphere = "Oxygen";    // the gas this race breathes
    std::string demeanor;
    std::string designNameFile;

    int characteristic(Characteristic c) const { return characteristics[static_cast<size_t>(c)]; }
};

struct Waypoint {
    std::string name;
    Location location;
    bool set = false;
};

struct ResearchProject {
    ruleset::TechAreaId area;
    int64_t progress = 0;  // toward the next level of the area
};

struct IntelProjectOrder {
    uint32_t project = 0;       // IntelProjects.txt index
    EmpireId target;            // invalid for defense projects
    ObjectId targetPlanet;      // optional specific target ("Any" when invalid)
    VehicleId targetVehicle;
    EmpireId thirdEmpire;       // political operations
    ruleset::TechAreaId targetTech;  // Research - Steal: the area ("Any" when invalid)
    int64_t progress = 0;
};

// One empire's standing with another (spec 05 §3).
struct Relation {
    bool contact = false;
    Treaty treaty = Treaty::None;
    bool dominant = false;      // Subjugation/Protectorate: true if *we* are the master
    int tradeTurns = 0;         // trade counter (spec 05 §3.3); the trade % is min(this, the maximum)
    uint32_t treatyTurn = 0;    // when the current treaty took effect
    int32_t lastWarTurn = -1;
    int anger = 50;             // computer players: anger toward that empire, 0..100 (spec 05 §7.3)
    bool messageSentThisTurn = false;
    // What a computer player (or a Politics minister) remembers about that
    // empire between turns (spec 05 §7.3, §7.4). Kept by the AI memory steps
    // (ai::recordAiDecisions, updateAiState, politicalStep, rememberAiEvents).
    int turnsSinceWar = 999;    // 0 while at war, +1 each turn otherwise
    int treatyAge = 0;          // turns since the treaty changed (moves between Trade Alliance and better do not count)
    Treaty agedTreaty = Treaty::None;  // the treaty treatyAge was last updated for
    bool promise = false;       // we accepted their "stop hostile actions" request (-20 anger once)
    bool queuedWar = false;     // an accepted request asks us to declare war on them
    bool queuedBreak = false;   // ... to break our treaty with them
    bool queuedPeace = false;   // ... to make peace with them
    bool attackedUs = false;    // their ships attacked ours or our planets
    bool spiedOnUs = false;     // one of their intelligence operations against us was traced to them
    SystemId attackedIn;        // where they last attacked us
    int combatsThisTurn = 0;    // combats with them in the last processed turn
    int combatsLastTurn = 0;    // ... and in the turn before
};

// A computer player's plans between turns (spec 05 §7.2, §7.4).
struct AiMemory {
    std::vector<SystemId> targets;        // attack targets, at most 3
    SystemId staging;                     // where the attack gathers
    SystemId secured;                     // the system Secure Holdings watches
    std::vector<SystemId> defend;         // systems to defend, most urgent first
    int afterAttack = 0;                  // after-attack timer: 1 when an attack ends, +1 per turn
    std::vector<SystemId> avoid;          // systems we agreed to leave (accepted demands)
    std::vector<SystemId> attackSystems;  // systems an accepted request asked us to attack
    bool metMinefield = false;            // our ships have run into a mine field
};

struct LogEntry {
    uint32_t turn = 0;
    LogCategory category = LogCategory::Misc;
    std::string title;
    std::string text;
    std::optional<Location> location;
    std::string picture;  // Events/ picture name, if any
};

struct TurnStats {
    uint32_t turn = 0;
    int64_t score = 0;
    Resources production;
    int64_t research = 0;
    int64_t intelligence = 0;
    int techLevels = 0;
    int systems = 0;
    int planets = 0;
    int64_t population = 0;
    int units = 0;
    int ships = 0;
    int bases = 0;
};

// Income/expense breakdown of the last processed turn (Empire Status window).
struct EconomyReport {
    Resources colonies, trade, tariffsIn, remoteMining, otherIncome;
    Resources tariffsOut, maintenance, construction;
    Resources lostToStorage, undelivered;
    Resources storageCap;
    int64_t research = 0;
    int64_t intelligence = 0;
};

// What an empire knows about the galaxy (spec 01 §6).
struct Knowledge {
    std::vector<uint8_t> explored;        // per SystemId: stellar bodies known
    std::vector<uint8_t> present;         // per SystemId: has presence this turn (recomputed)
    std::vector<uint8_t> knownWarpLink;   // per ObjectId (warp points): destination known
    std::vector<uint32_t> lastSeen;       // per SystemId: turn last seen with presence
    std::vector<VehicleId> visibleVehicles;  // foreign vehicles visible this turn (sorted)
    std::vector<DesignId> seenDesigns;       // foreign designs learned (sorted)
    std::vector<std::string> notes;          // per SystemId, player notes
};

// Which empires met after a warp clear a vehicle's orders (spec 03 §6.4):
// none, an enemy empire, or any other empire.
enum class EncounterClear : uint8_t { Never, Enemy, Any };

struct Empire {
    EmpireId id;
    std::string name;          // e.g. "Terran"
    std::string empireType;    // e.g. "Confederation"
    std::string leaderTitle;
    std::string leaderName;
    Race race;
    uint32_t color = 0xffffff;
    PlayerKind kind = PlayerKind::Human;
    bool alive = true;
    std::string passwordHash;
    int racialPointsSpent = 0;

    Resources stockpile;
    EconomyReport economy;

    // Research (spec 05 §1).
    std::vector<int> techLevels;            // per TechAreaId
    std::vector<ResearchProject> research;  // queue, max 12
    bool researchEvenly = true;
    bool repeatResearch = false;
    std::vector<int> uniqueAreasUnlocked;   // Unique Area ids granted by ruins
    // Points for this turn's research step: produced at the end of the
    // previous turn, spent at the end of this one, then emptied (spec 05 §1.1).
    int64_t researchPool = 0;

    // Intelligence (spec 05 §2).
    std::vector<IntelProjectOrder> intel;
    bool intelEvenly = true;
    bool repeatIntel = false;
    int64_t intelPool = 0;                  // like researchPool (spec 05 §2.1)

    std::vector<Relation> relations;        // per EmpireId
    Knowledge knowledge;

    std::vector<SystemId> claimedSystems;
    std::vector<SystemId> systemsToAvoid;
    std::vector<Location> taggedMinefields;
    std::array<Waypoint, 10> waypoints{};
    std::vector<std::string> designTypes;
    std::vector<std::string> colonyTypes;
    std::vector<ruleset::CombatStrategy> strategies;
    std::vector<std::string> repairPriorities;
    std::vector<DesignId> designs;          // own designs

    std::vector<LogEntry> log;
    std::vector<TurnStats> history;
    int experience = 0;

    // Computer player state (spec 05 §7).
    int aiState = 0;
    int aiTurnsInState = 0;
    bool aiMinimalChanges = false;
    AiMemory aiMemory;
    int aiDifficulty = -1;                  // kDifficulty*; -1 until the AI step assigns it (ai::difficultyOf)

    bool ministerAll = false;               // full minister control
    uint32_t ministers = kIndividualMinisters;  // human empires: minister areas switched on (bit = Minister)
    std::string ministerStyle;              // "Aggressive", "Defensive", "Neutral"; empty: the race's own AI files
    bool useRaceMinisterStyle = false;      // "Use Race Minister Style": the race's files even with a style (spec 05 §7.1)
    bool ministersForNewVehicles = false;   // new vehicles and launched units start under minister control (spec 02 §10)

    // Movement option (spec 03 §6.4): a Warp into a system holding such an
    // empire's objects fails and clears the orders. Computer players copy it
    // from their AI_Settings each turn (spec 05 §7.5).
    EncounterClear clearOrdersOnEncounter = EncounterClear::Never;

    int techLevel(ruleset::TechAreaId a) const { return a.index() < techLevels.size() ? techLevels[a.index()] : 0; }
    const Relation& relation(EmpireId e) const { return relations[e.index()]; }
    Relation& relation(EmpireId e) { return relations[e.index()]; }
    bool hasExplored(SystemId s) const { return s.index() < knowledge.explored.size() && knowledge.explored[s.index()]; }
};

// ---- Orders (spec 03 §8) -------------------------------------------------------------------------

enum class OrderKind : uint8_t {
    MoveTo,         // location
    Warp,           // object = warp point
    Attack,         // vehicle or object target
    Resupply,
    Repair,
    Explore,
    Colonize,       // object = planet
    Sentry,
    LoadCargo,      // design (unit) or population (design invalid); amount (-1 = all that fit)
    DropCargo,      // same
    LaunchUnits,    // design, amount
    RecoverUnits,   // design, amount
    Cloak,
    Decloak,
    SweepMines,
    UseComponent,   // amount = design entry index
    StellarManipulation,  // amount = StellarAction, object/location = target
    MoveToWaypoint, // amount = waypoint slot
    Count
};
std::string_view displayName(OrderKind k);

enum class StellarAction : uint8_t {
    CreatePlanet, DestroyPlanet, CreateStar, DestroyStar, OpenWarpPoint, CloseWarpPoint,
    CreateStorm, DestroyStorm, CreateNebulae, DestroyNebulae, CreateBlackHole, DestroyBlackHole,
    CreateConstructedPlanet, Count
};

struct Order {
    OrderKind kind = OrderKind::MoveTo;
    Location location;
    ObjectId object;
    VehicleId vehicle;
    DesignId design;
    int amount = 0;
    bool operator==(const Order&) const = default;
};

// ---- Cargo, queues, colonies ------------------------------------------------------------

struct PopulationGroup {
    EmpireId race;          // the empire whose race this population is
    int64_t millions = 0;
    bool operator==(const PopulationGroup&) const = default;
};

struct UnitStack {
    DesignId design;
    int count = 0;
    bool operator==(const UnitStack&) const = default;
};

struct Cargo {
    std::vector<PopulationGroup> population;
    std::vector<UnitStack> units;

    int64_t totalPopulation() const {
        int64_t n = 0;
        for (const auto& p : population) n += p.millions;
        return n;
    }
    int unitCount(DesignId d) const {
        for (const auto& u : units)
            if (u.design == d) return u.count;
        return 0;
    }
    bool empty() const { return population.empty() && units.empty(); }
};

struct QueueItem {
    enum class Kind : uint8_t { Vehicle, Facility, Upgrade };
    Kind kind = Kind::Vehicle;
    DesignId design;          // Vehicle
    uint32_t facility = 0;    // Facility: Facility.txt index; Upgrade: family representative
    int count = 1;            // units built as a batch
    Resources spent;          // progress on the current item
};

struct ConstructionQueue {
    std::vector<QueueItem> items;
    bool onHold = false;
    bool repeat = false;
    bool emergency = false;
    int emergencyTurns = 0;   // consecutive emergency turns used
    int slowTurns = 0;        // remaining slow-mode turns after an emergency
    int autoWaypoint = -1;    // new vehicles get Move To this waypoint slot
};

struct Colony {
    ObjectId planet;
    EmpireId owner;
    std::string colonyType;
    std::vector<PopulationGroup> population;
    int anger = kNewColonyAnger;      // whole percent, 0..100 (spec 02 §4); a new colony starts Happy
    std::vector<uint32_t> facilities; // Facility.txt indices
    Cargo cargo;
    ConstructionQueue queue;
    int plagueLevel = 0;
    int atmosphereTurns = 0;          // turns spent with an atmosphere the majority cannot breathe (spec 02 §2)
    bool minister = false;
    bool homeworld = false;           // also the capital flag: anger never above 80 (spec 02 §2)
    uint32_t foundedTurn = 0;
    int militia = -1;                 // ground combat: militia left to raise; -1 = no invasion (spec 04 §13)
    // Planet orders (simultaneous games, spec 05 §9.2): Launch Units and
    // Recover Units, carried out in the movement phase (spec 03 §12).
    std::vector<Order> orders;

    int64_t totalPopulation() const {
        int64_t n = 0;
        for (const auto& p : population) n += p.millions;
        return n;
    }
    // The highest anger this colony can have: 80 for a capital, else 100 (spec 02 §2, §4).
    int maxAnger() const { return homeworld ? kCapitalMaxAnger : kMaxAnger; }
};

// ---- Designs and vehicles ------------------------------------------------------------------

struct DesignEntry {
    uint32_t component = 0;  // Components.txt index
    int32_t mount = -1;      // CompEnhancement.txt index, or -1
    bool operator==(const DesignEntry&) const = default;
};

struct Design {
    DesignId id;
    EmpireId owner;
    std::string name;
    std::string designType;
    uint32_t hull = 0;       // VehicleSize.txt index
    std::vector<DesignEntry> entries;
    uint32_t strategy = 0;   // index into the owner's strategies
    bool obsolete = false;
    uint32_t createdTurn = 0;
    int built = 0;
    int lost = 0;
    int kills = 0;
};

enum class VehicleStatus : uint8_t { Normal, Mothballed, Cloaked };

struct Vehicle {
    VehicleId id;
    EmpireId owner;
    DesignId design;
    std::string name;
    Location location;
    int count = 1;                  // unit groups in space hold several identical units
    std::vector<int> damage;        // per design entry; destroyed when >= structure
    int64_t supply = 0;
    int movement = 0;               // movement points left this turn
    std::vector<Order> orders;
    bool repeatOrders = false;
    FleetId fleet;
    Cargo cargo;
    int experience = 0;
    int experienceTenths = 0;       // tenths of a point beyond `experience` (0-9; combat gains, spec 04 §15)
    VehicleStatus status = VehicleStatus::Normal;
    bool minister = false;
    ConstructionQueue queue;        // used when the design has a Space Yard
    VehicleId targetVehicle;        // drones
    ObjectId targetObject;
    uint32_t builtTurn = 0;
    uint32_t immobileUntil = 0;     // no movement while turn < this (sabotage/events, spec 05 §2.3)
    // The sector the vehicle last left when it moved during turn `cameFromTurn`
    // (spec 04 §3: attackers and start boxes). Movement records it on each step;
    // it counts only while cameFromTurn == GameState::turn.
    Location cameFrom;
    uint32_t cameFromTurn = 0;
};

struct Fleet {
    FleetId id;
    EmpireId owner;
    std::string name;
    std::vector<VehicleId> members;
    VehicleId leader;
    uint32_t formation = 0;         // Formations.txt index
    uint32_t strategy = 0;          // owner's strategy index
    int experience = 0;
    int experienceTenths = 0;       // tenths beyond `experience` (0-9)
    std::vector<Order> orders;
    bool repeatOrders = false;
    bool minister = false;
};

// ---- Diplomacy ---------------------------------------------------------------------------

enum class MessageType : uint8_t {
    General,
    ProposeTreaty, AcceptTreaty, RefuseTreaty, CounterTreaty, BreakTreaty, DeclareWar,
    ProposeTrade, AcceptTrade, RefuseTrade, CounterTrade,
    Gift, Tribute, AcceptGift, RefuseGift,
    Surrender, GrantIndependence,
    // Non-binding demands and requests (spec 05 §3.4).
    DemandGift, DemandTribute, DemandSurrender, DemandRemoveShips, DemandRemoveColonies, DemandLeavePlanet,
    RequestStopHostilities, RequestBreakTreaty, RequestDeclareWar, RequestMakePeace, RequestSupport,
    RequestAttackEmpire, RequestAttackPlanet, DemandStopEspionage, DemandStopSabotage, DemandStopAttacks,
    AcceptDemand, RefuseDemand,
    Count
};
std::string_view displayName(MessageType t);

struct PackageItem {
    enum class Kind : uint8_t { Resources, Technology, Planet, Vehicle, StarChart, Treaty, CommChannel, System };
    Kind kind = Kind::Resources;
    Resources resources;
    ruleset::TechAreaId tech;
    ObjectId planet;
    VehicleId vehicle;
    SystemId system;
    Treaty treaty = Treaty::None;
    EmpireId empire;   // comm channel target
};

struct DiplomaticMessage {
    MessageId id;
    EmpireId from;
    EmpireId to;
    uint32_t sentTurn = 0;
    MessageType type = MessageType::General;
    int tone = 1;                        // 0 pleading, 1 neutral, 2 demanding
    std::string text;
    Treaty treaty = Treaty::None;        // treaty messages
    std::vector<PackageItem> offer;      // trade/gift/tribute: what the sender gives
    std::vector<PackageItem> request;    // what the sender asks for
    EmpireId thirdEmpire;                // requests about another empire
    SystemId system;
    ObjectId planet;
    MessageId inReplyTo;
    bool delivered = false;
    bool answered = false;
};

// ---- Combat records (replays and reports) ----------------------------------------------------

struct CombatEvent {
    enum class Kind : uint8_t { Move, Fire, Hit, Miss, Destroyed, Captured, Launch, Seeker };
    Kind kind = Kind::Move;
    uint8_t round = 0;
    uint32_t piece = 0;       // index into CombatRecord::pieces
    uint32_t target = 0;
    int16_t x = 0, y = 0;
    int32_t amount = 0;
    uint32_t component = 0;
};

struct CombatPiece {
    enum class Kind : uint8_t { Vehicle, Planet, UnitGroup, Seeker, Obstacle };
    Kind kind = Kind::Vehicle;
    EmpireId owner;
    VehicleId vehicle;
    ObjectId planet;
    DesignId design;
    std::string name;
    int16_t startX = 0, startY = 0;
};

struct CombatRecord {
    uint32_t turn = 0;
    Location location;
    std::vector<EmpireId> participants;
    std::vector<CombatPiece> pieces;
    std::vector<CombatEvent> events;
    std::vector<std::string> summary;  // human-readable lines
};

// ---- Events and options ------------------------------------------------------------------------

// Something that changes population mood (Happiness.txt triggers, spec 02 §4).
// Each empire's happiness update uses up the events raised since its previous
// update; those raised after it in the same turn wait for the next turn's.
struct MoodEvent {
    EmpireId empire;           // whose population reacts
    std::string trigger;       // Happiness.txt trigger identifier
    SystemId system;           // where it happened (invalid = empire-wide)
    ObjectId planet;           // the planet it happened at (optional)
    int count = 1;
};

struct PendingEvent {
    uint32_t eventType = 0;   // Events.txt index
    EmpireId empire;
    ObjectId object;
    VehicleId vehicle;
    SystemId system;
    uint32_t fireTurn = 0;
};

struct VictoryConditions {
    bool score = false;             int64_t scoreValue = 50000;
    bool years = false;             int yearsValue = 100;
    bool percentOfSecond = false;   int percentOfSecondValue = 200;
    bool techPercent = false;       int techPercentValue = 75;
    bool peace = false;             int peaceYears = 20;
    bool delay = false;             int delayYears = 10;
};

struct GameOptions {
    // Quadrant (spec 01 §2.2).
    std::string quadrantType;
    int systemCount = 0;                 // 0: rolled from quadrantSize; > 0: exactly this many
    int quadrantSize = 1;                // 0 small, 1 medium, 2 large
    bool allWarpPointsConnected = true;
    bool noWarpPoints = false;
    bool warpPointsAnywhere = false;
    bool allSystemsSeen = false;
    bool omnipresent = false;
    bool finiteResources = false;
    // Events.
    int eventFrequency = 1;              // 0 none, 1 low (the default, spec 01 §2.2), 2 medium, 3 high
    int maxEventSeverity = 2;            // 0 low .. 3 catastrophic
    // Technology.
    int techCost = 1;                    // Technology Cost: 0 low, 1 medium (the default), 2 high (spec 05 §1.3)
    int startTechLevel = 0;              // 0 low, 1 medium, 2 high
    std::vector<uint8_t> techAreasAllowed;  // per tech area; empty = all
    // Players.
    Resources startingResources{20000, 20000, 20000};
    int racialPoints = 2000;
    int homePlanetValue = 1;             // 0 low, 1 medium, 2 high (Bad, Average, Good)
    int startingPlanets = 1;             // 1, 3, 5 or 10
    bool allPlanetsSameSize = true;      // every homeworld has the Home Planet Value size
    bool sameSystemAllowed = false;
    bool evenlyDistributed = true;
    // Game settings.
    bool noTacticalCombat = true;
    bool allowGifts = true;
    bool allowTechTrades = true;
    bool allowIntel = true;
    bool noRuins = false;
    bool onlyBreathable = false;
    bool onlyHomeType = false;
    bool teamMode = false;
    int scoreDisplay = 1;                // Score Display: 0 own, 1 own and Non-Aggression or better (the default), 2 all (spec 05 §5)
    int maxShipsPerPlayer = 200;
    int maxUnitsPerPlayer = 1000;
    int aiDifficulty = kDifficultyMedium;  // Computer Player Difficulty: the level random AI players get
    int aiBonus = 0;
    VictoryConditions victory;
    // Multiplayer.
    bool simultaneous = true;
    // Per EmpireId: 1 for players added by "Random Computer/Neutral Players".
    // Only they get the chosen aiDifficulty (spec 05 §7.1).
    std::vector<uint8_t> randomAiPlayers;
};

// ---- The game --------------------------------------------------------------------------------

struct GameState {
    uint32_t turn = 0;   // game date = 2400.0 + turn / 10
    uint64_t seed = 0;
    GameOptions options;
    Galaxy galaxy;
    std::vector<std::optional<Colony>> colonies;  // indexed by ObjectId (planets)
    std::vector<Empire> empires;
    std::vector<Design> designs;                  // indexed by DesignId
    std::vector<Vehicle> vehicles;                // sorted by id
    std::vector<Fleet> fleets;                    // sorted by id
    std::vector<DiplomaticMessage> messages;      // not yet answered/expired
    std::vector<PendingEvent> pendingEvents;
    std::vector<MoodEvent> pendingMood;           // raised after an empire's happiness update, for its next one
    std::vector<CombatRecord> combats;            // battles of the last processed turn
    uint32_t nextVehicleId = 0;
    uint32_t nextFleetId = 0;
    uint32_t nextMessageId = 0;
    uint32_t peacefulTurns = 0;
    bool gameOver = false;
    EmpireId winner;
    Rng rng;

    // Accessors.
    Empire& empire(EmpireId id) { return empires[id.index()]; }
    const Empire& empire(EmpireId id) const { return empires[id.index()]; }
    Design& design(DesignId id) { return designs[id.index()]; }
    const Design& design(DesignId id) const { return designs[id.index()]; }

    Colony* colony(ObjectId planet) {
        return planet.index() < colonies.size() && colonies[planet.index()] ? &*colonies[planet.index()] : nullptr;
    }
    const Colony* colony(ObjectId planet) const { return const_cast<GameState*>(this)->colony(planet); }

    Vehicle* vehicle(VehicleId id) {
        auto it = std::lower_bound(vehicles.begin(), vehicles.end(), id, [](const Vehicle& v, VehicleId x) { return v.id < x; });
        return it != vehicles.end() && it->id == id ? &*it : nullptr;
    }
    const Vehicle* vehicle(VehicleId id) const { return const_cast<GameState*>(this)->vehicle(id); }

    Fleet* fleet(FleetId id) {
        auto it = std::lower_bound(fleets.begin(), fleets.end(), id, [](const Fleet& f, FleetId x) { return f.id < x; });
        return it != fleets.end() && it->id == id ? &*it : nullptr;
    }
    const Fleet* fleet(FleetId id) const { return const_cast<GameState*>(this)->fleet(id); }

    // Game date as tenths of a year since 2400 (display: 2400 + turn/10).
    int year() const { return 2400 + static_cast<int>(turn / 10); }

    // Adds with a fresh id (keeps `vehicles`/`fleets` sorted). References are
    // invalidated by the next add.
    Vehicle& addVehicle(Vehicle v);
    Fleet& addFleet(Fleet f);
    // Drops vehicles with count <= 0, cleans fleet membership and empty fleets.
    void removeDeadVehicles();
    std::vector<const Vehicle*> vehiclesAt(Location where) const;
};

// Appends to an empire's log for the current turn.
void addLog(GameState& s, EmpireId empire, LogCategory category, std::string title, std::string text = {},
            std::optional<Location> where = std::nullopt, std::string picture = {});

} // namespace opense4::game
