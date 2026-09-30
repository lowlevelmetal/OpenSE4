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

// One dated line of an empire's long record, the History window (spec 05
// §3.4): the log keeps only the last turn, this record keeps every turn.
// Entries are listed per empire they concern, plus a General list, and may
// carry a map position. Which events are recorded is OpenSE4's choice
// (addHistory's callers, spec 05 open question 30, inferred).
struct HistoryEntry {
    uint32_t turn = 0;
    EmpireId empire;                    // the empire it concerns (us or another); invalid: General
    std::string text;
    std::optional<Location> location;   // where it happened, if anywhere
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

// A foreign design an empire knows and the turn it last saw it. Knowledge of
// a design not seen for more than kDesignMemoryTurns is forgotten in the
// empire's end-of-turn processing (spec 05 §8 step 12).
struct SeenDesign {
    DesignId design;
    uint32_t turn = 0;
};
inline constexpr uint32_t kDesignMemoryTurns = 50;

// What an empire knows about the galaxy (spec 01 §6).
struct Knowledge {
    std::vector<uint8_t> explored;        // per SystemId: stellar bodies known
    std::vector<uint8_t> present;         // per SystemId: has presence this turn (recomputed)
    std::vector<uint8_t> knownWarpLink;   // per ObjectId (warp points): destination known
    std::vector<uint32_t> lastSeen;       // per SystemId: turn last seen with presence
    std::vector<VehicleId> visibleVehicles;  // foreign vehicles visible this turn (sorted)
    std::vector<SeenDesign> seenDesigns;     // foreign designs learned (sorted by design), with the turn last seen
    std::vector<std::string> notes;          // per SystemId, player notes
};

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

    // The empire's home system (spec 02 §2, §5.5): recorded when the game is
    // created (a rebel empire: its capital's system when it is founded) and
    // never moved, even after the homeworld is lost. Every starting planet is
    // a capital (Colony::homeworld), so that flag alone does not tell which
    // one is home.
    SystemId homeSystem;
    std::vector<SystemId> claimedSystems;
    std::vector<SystemId> systemsToAvoid;
    std::vector<Location> taggedMinefields;
    std::array<Waypoint, 10> waypoints{};
    std::vector<std::string> designTypes;
    std::vector<std::string> colonyTypes;
    std::vector<ruleset::CombatStrategy> strategies;
    std::vector<std::string> repairPriorities;
    std::vector<DesignId> designs;          // own designs

    std::vector<LogEntry> log;              // this turn's messages (pruned at the end of each turn, spec 05 §3.4)
    std::vector<HistoryEntry> historyEvents;  // the History window's long record, oldest first
    std::vector<TurnStats> history;         // statistics per turn (Scores, Comparisons)
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

    // Ship Orders option (spec 03 §6.4, confirmed: binary): a warp transit into
    // a system holding such an empire's objects that the owner sees clears the
    // group's orders (movement reads it). "Clear on meeting an enemy" is on for
    // a new empire. Players set it with cmd::SetEncounterOptions; computer
    // players copy it from their AI_Settings each turn (spec 05 §7.5).
    EncounterClear clearOrdersOnEncounter = EncounterClear::Enemy;
    // Ship Movement options (spec 03 §6.2): routes go around the tagged
    // minefields, and never cross the systems to avoid, only while these are
    // on. Both are on for a new empire (inferred, spec 03 §19 Q58). Players set
    // them with cmd::SetEncounterOptions; computer players copy them from their
    // AI_Settings each turn (spec 05 §7.5).
    bool avoidTaggedMinefields = true;
    bool avoidRestrictedSystems = true;
    // "Choose the colony type on colonization" (spec 03 §8, on for a new
    // empire): a human player's colony founded in a turn-based game waits in
    // colonyTypeChoices for the player to pick its type in a dialog
    // (cmd::SetColonyType); until then it has the type the computer would pick.
    bool chooseColonyType = true;
    std::vector<ObjectId> colonyTypeChoices;

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
    RecoverUnits,   // design = one of the unit kind (every design of each group of that kind is recovered);
                    // vehicle = one named group, then only `design` from it, up to amount (the Launch/Recover window)
    Cloak,
    Decloak,
    SweepMines,
    UseComponent,   // amount = design entry index
    StellarManipulation,  // amount = StellarAction, object/location = target
    MoveToWaypoint, // amount = waypoint slot
    SelfDestruct,   // the whole object is destroyed (spec 03 §8, §15; movement::canSelfDestruct)
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
    uint32_t facility = 0;    // Facility: Facility.txt index; Upgrade: the target facility, fixed when queued
    int count = 1;            // built as a batch; Upgrade: facilities to convert, fixed when queued (spec 02 §6.6)
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
    bool homeworld = false;           // the capital flag: anger never above 80; every starting planet has it (spec 02 §2, §9)
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
    // The AI_DesignCreation template the Design minister made it from, the
    // template's `Name`; empty for hand-made and premade designs. The vehicle
    // list's entries look designs up by it, and the Design minister's name
    // counter counts such designs (spec 05 §7.5, confirmed: binary).
    std::string templateName;
    // A ship was retrofitted to it: it is no longer a prototype, even with
    // nothing built (spec 03 §4.1; designIsPrototype, design.hpp).
    bool retrofitted = false;
    // Statistics (spec 03 §4.1, spec 04 §15); resetDesignStatistics (design.hpp) zeroes them.
    int built = 0;
    int lost = 0;
    int kills = 0;
    int64_t enemyTonnageDestroyed = 0;  // hull tonnage of the enemy vehicles its vehicles destroyed
};

enum class VehicleStatus : uint8_t { Normal, Mothballed, Cloaked };

struct Vehicle {
    VehicleId id;
    // Its slot in the game's object list: the order objects act in during the
    // simultaneous movement phase (spec 03 §6.3). A new vehicle takes the
    // first slot a removed vehicle freed, else a new one at the end
    // (GameState::addVehicle); planets come before every vehicle (inferred:
    // the galaxy is made before any vehicle, and slots freed by removed
    // stellar objects are not reused by vehicles).
    uint32_t slot = 0;
    EmpireId owner;
    DesignId design;                // a unit group that mixes designs: its first design
    std::string name;
    Location location;
    int count = 1;                  // unit groups in space: the number of units, all designs together
    // A unit group in space that mixes designs (spec 03 §12): each design and
    // how many units of it, in the order they joined (at least two stacks).
    // Empty when the vehicle holds one design, which is then `design` × `count`.
    // Read and change it through the group helpers of design.hpp
    // (groupStacks, addGroupUnits, removeGroupUnits, setGroupStacks).
    std::vector<UnitStack> mixed;
    std::vector<int> damage;        // per entry of `design`; destroyed when >= structure (a unit group: its front unit's, from mines)
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
    // UnitsLost: unit group `piece` loses `amount` units to a hit by `target`
    // (it may be Destroyed next, when none is left).
    enum class Kind : uint8_t { Move, Fire, Hit, Miss, Destroyed, Captured, Launch, Seeker, UnitsLost };
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
    DesignId design;                   // a unit group that mixes designs: its first
    std::string name;
    int16_t startX = 0, startY = 0;
    int32_t count = 1;                 // units in a group at the start (seekers: members)
};

// A ground combat fought during a space battle, when troops landed (spec 04
// §11, §13): what the Ground Combat window shows (spec 06 §1.6).
struct GroundCombat {
    uint8_t round = 0;                      // the combat turn the troops landed in
    uint32_t planetPiece = 0;               // index into CombatRecord::pieces
    uint32_t troopShip = 0;                 // the piece that dropped them
    ObjectId planet;
    EmpireId attacker, defender;
    int64_t population = 0;                 // millions when the troops landed
    std::vector<uint32_t> facilities;       // Facilities.txt indices
    std::vector<UnitStack> attackers;       // the invading troops at the start
    std::vector<UnitStack> defenders;       // the planet's troops and other stored units at the start
    std::vector<UnitStack> attackersLeft;   // the same stacks after the fight
    std::vector<UnitStack> defendersLeft;
    int militia = 0, militiaLeft = 0;       // the colony's militia pool before and after
    int rounds = 0;
    bool captured = false;                  // the planet fell
};

struct CombatRecord {
    uint32_t turn = 0;
    Location location;
    // The "current player" when the battle was fought, who counts it as
    // Attacking in the anger terms (spec 05 §7.3, confirmed: binary): the
    // player whose turn it is in a turn-based game; in a simultaneous game the
    // highest player number, left over from the start-of-turn loop.
    EmpireId currentPlayer;
    std::vector<EmpireId> participants;
    std::vector<CombatPiece> pieces;
    std::vector<CombatEvent> events;
    std::vector<std::string> summary;  // human-readable lines
    std::vector<GroundCombat> grounds; // troops landed during the battle
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
    // "No Tactical Combat" (spec 04 §2): every battle strategic. Off by
    // default, so turn-based games ask (inferred: an opt-out check box).
    // Simultaneous games never offer tactical combat either way.
    bool noTacticalCombat = false;
    bool allowGifts = true;
    bool allowTechTrades = true;
    bool allowIntel = true;
    bool noRuins = false;
    bool onlyBreathable = false;
    bool onlyHomeType = false;
    bool teamMode = false;
    // "Allow Surrender" (spec 05 §7.4, confirmed: binary), on by default: with
    // it off a computer player never considers a surrender demand and a
    // Surrender message does nothing at all.
    bool allowSurrender = true;
    int scoreDisplay = 1;                // Score Display: 0 own, 1 own and Non-Aggression or better (the default), 2 all (spec 05 §5)
    int maxShipsPerPlayer = 200;
    int maxUnitsPerPlayer = 1000;
    int aiDifficulty = kDifficultyMedium;  // Computer Player Difficulty: the level random AI players get
    int aiBonus = 0;
    VictoryConditions victory;
    bool playersCanSaveMap = false;      // "Players can save map during a game": off by default; Save Map is disabled without it (spec 01 §2.2, §12, confirmed: binary)
    // Mechanics.
    int autosaveTurns = 0;               // Autosave: 0 None (the default), else every 1, 2, 3, 5 or 10 turns (spec 01 §2.2, §14 Q18)
    // Multiplayer. Turn style (spec 01 §2.2, spec 05 §8): simultaneous (every
    // player gives orders, then one turn processing carries them all out) or
    // turn-based (one player after another; orders execute as they are given,
    // turn_based in turn.hpp). A new game, Quick Start included, is
    // turn-based (spec 01 §2.2, §14 Q39, confirmed: binary).
    bool simultaneous = false;
    // Per EmpireId: 1 for players added by "Random Computer/Neutral Players".
    // Only they get the chosen aiDifficulty (spec 05 §7.1).
    std::vector<uint8_t> randomAiPlayers;
};

// ---- Turn-based games ----------------------------------------------------------------------

// What a vehicle did during the player turn in progress (turn-based games):
// the steps it made and the movement Emergency Energy gave it. Its remaining
// movement is capped at its maximum plus that bonus, less the steps, when the
// maximum drops (spec 03 §6.1, §6.4, §8).
struct TurnMoves {
    VehicleId vehicle;
    int steps = 0;
    int bonus = 0;
    bool operator==(const TurnMoves&) const = default;
};

// Units launched during the player turn in progress, per launcher and unit
// kind: the per-game-turn launch budget (spec 03 §12). `kind` is the
// launcher's AbilityKind.
struct TurnLaunches {
    VehicleId vehicle;
    ObjectId planet;
    uint16_t kind = 0;
    int64_t count = 0;
};

// A human player's group stopped before a sector with enemy forces: the
// player is asked whether to enter it (spec 03 §6.2) and answers with
// cmd::EnterSector. `vehicle` is invalid when a fleet moves together.
struct EntryQuestion {
    VehicleId vehicle;
    FleetId fleet;
    Location where;
    bool operator==(const EntryQuestion&) const = default;
};

// The player turn in progress in a turn-based game (spec 05 §8 "Turn-based
// game"). Unused in simultaneous games.
struct PlayerTurn {
    EmpireId empire;                    // whose turn it is; invalid: the next round has not started
    bool started = false;               // its start of turn has run (movement, continued orders, ministers)
    std::vector<TurnMoves> moves;       // sorted by vehicle
    std::vector<TurnLaunches> launched;
    // The player's Attack Sector questions still open, oldest first. Kept in
    // the game so that a saved or network game asks them again (inferred).
    std::vector<EntryQuestion> questions;
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
    // Battles of the last processed turn. Turn-based games keep those of the
    // game turn in progress and of the one before (CombatRecord::turn).
    std::vector<CombatRecord> combats;
    uint32_t nextVehicleId = 0;
    uint32_t nextFleetId = 0;
    uint32_t nextMessageId = 0;
    uint32_t peacefulTurns = 0;
    bool gameOver = false;
    EmpireId winner;
    Rng rng;
    PlayerTurn playerTurn;                        // turn-based games only
    // The starting points the game still holds, for Save Map during the game
    // (spec 01 §12, confirmed: binary): a game started from a map keeps all of
    // the map's specific points and the common points no player took; a
    // generated game has none.
    std::vector<StartingPoint> startingPoints;

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

// Appends a line dated the current turn to an empire's history record.
// `about` is the empire the event concerns (invalid: the General list).
void addHistory(GameState& s, EmpireId empire, EmpireId about, std::string text, std::optional<Location> where = std::nullopt);

// Foreign designs an empire has seen (Knowledge::seenDesigns).
bool knowsDesign(const Knowledge& k, DesignId d);
// When `d` was last seen; nullopt when it is not known.
std::optional<uint32_t> designSeenTurn(const Knowledge& k, DesignId d);
// Records that the design was seen at `turn`; a design already known keeps
// the later of the two turns.
void seeDesign(Knowledge& k, DesignId d, uint32_t turn);
// The designs of the list, in order.
std::vector<DesignId> seenDesignIds(const Knowledge& k);

} // namespace opense4::game
