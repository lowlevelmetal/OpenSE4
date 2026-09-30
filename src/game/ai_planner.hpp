#pragma once

// Internal to the computer player (ai_*.cpp).
//
// The Planner holds one empire's view of the game for one turn. It works on a
// private copy of the state and applies each command to that copy as soon as
// it is planned (Planner::emit), so later decisions see earlier ones (a new
// design can be queued, a new fleet can be given orders) and every command it
// returns is known to be accepted when the turn pipeline applies the list in
// order.
//
// The Situation is what the AI works out about the galaxy each turn (spec 05
// §7.2: territory, strength, the lists). The planner and the AI state update
// (ai::updateAiState) compute it the same way, so the state machine and the
// ministers agree.

#include "core/rng.hpp"
#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/design.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace opense4::game::ai::detail {

enum class Mode : uint8_t {
    Computer,   // a computer player, or a human empire played by the computer: every minister, every object
    Minister,   // a human's ministers: the areas switched on, individual ones only on flagged objects
};

// What a design is for, from its AI design type (spec 05 §7.7).
enum class Role : uint8_t {
    Other, Attack, Defense, Base, YardBase, Colonizer, Transport, TroopTransport, Carrier, DroneCarrier, MineLayer,
    SatelliteLayer, Sweeper, Boarding, Kamikaze, YardShip, Stellar, Unit
};

struct DesignInfo {
    bool ready = false;
    std::string aiType;     // one of the 39 AI design types, or empty
    Role role = Role::Other;
    DesignStats stats;
    int64_t rating = 0;     // spec 05 §7.2 strength rating of an undamaged vehicle
};

struct Link {
    ObjectId warpPoint;
    SystemId to;
};

// A hostile object the lists name (spec 05 §7.2).
struct Threat {
    SystemId system;
    EmpireId owner;
    VehicleId vehicle;     // a ship or unit group, or
    ObjectId planet;       // a populated colony
};

// A planet of another empire the AI could attack (spec 05 §7.2).
struct Candidate {
    ObjectId planet;
    SystemId system;
    EmpireId owner;
    int jumps = 0;
    int anger = 0;
    int64_t value = 0;     // strength at that spot plus the planet's defence
};

// A planet the colonization minister could settle (spec 05 §7.5).
struct ColonyTarget {
    ObjectId planet;
    SystemId system;
    int danger = 0;
    int jumps = 0;
    bool ruins = false;
    bool breathable = false;
    int size = 0;
    int64_t value = 0;
    bool settleable = false;   // we have the colony module for its surface
};

struct Situation {
    SystemId home;
    std::vector<uint8_t> territory;       // per system
    std::vector<int64_t> ours;            // our strength per system
    std::vector<int64_t> hostile;         // strength of the hostile empires we have met, per system
    std::vector<int> homeJumps;           // warp jumps from home over known links (-1 = unreachable)
    std::vector<Threat> enemyInTerritory;
    std::vector<Threat> enemyNearby;      // hostile mine fields in our territory
    std::vector<Candidate> candidates;    // attack candidates, best first
    std::vector<ObjectId> frontier;       // warp points into unexplored space
    std::vector<ObjectId> freeFrontier;   // ... that none of our ships is headed for
    std::vector<SystemId> defend;         // systems to defend, most urgent first
    std::vector<ColonyTarget> colonyTargets;  // in the colonization order
    bool contact = false;                 // we have met a living empire
    bool bordersUnexplored = false;       // a system of our territory borders unexplored space
    bool notConnected = false;            // the Not Connected test (spec 05 §7.2)
};

// Everything that reads the galaxy the way the AI does. `settings` is the
// empire's AI_Settings (defend list size).
Situation assess(const Rules& r, const GameState& s, EmpireId e, const AiProfile& prof);
// The systems the empire claims (spec 05 §7.2, §7.3), sorted: for a computer
// player its colony systems, the systems one jump away (not for neutrals),
// less other computer players' home systems and the systems it agreed to
// leave; for a human the systems claimed by hand plus the home system.
std::vector<SystemId> computeTerritory(const GameState& s, EmpireId e);
// Is this hostile object noticed this turn? Low difficulty misses each one
// with a 10 % chance per turn (spec 05 §7.2); the roll is the same for every
// caller on the same turn.
bool notices(const GameState& s, EmpireId e, uint64_t object);
inline uint64_t vehicleKey(VehicleId v) { return (uint64_t{1} << 40) | v.value; }
inline uint64_t planetKey(ObjectId o) { return (uint64_t{2} << 40) | o.value; }
// Spec 05 §7.2 strength rating of one vehicle (damaged weapons do not count).
int64_t vehicleRating(const Rules& r, const GameState& s, const Vehicle& v);
// Military hostility (spec 05 §7.2): below Non-Aggression, or not met.
bool hostileTo(const Empire& e, EmpireId other);
// A home system: the homeworld's, else the first colony's, else invalid.
SystemId homeSystem(const GameState& s, EmpireId e);

class Planner {
public:
    Planner(const Rules& rules, const GameState& s, EmpireId e, Mode mode, uint64_t salt);
    void runOrders();    // group 1 (ai.hpp)
    void runEconomy();   // group 2
    PlanReport report() { return {std::move(out), std::move(dropped)}; }

    const Rules& r;
    GameState st;           // private copy; commands are applied to it as they are planned
    EmpireId id;
    Mode mode;
    const AiProfile& prof;
    AiState state;
    int difficulty = kDifficultyMedium;
    Rng rng;
    bool neutral = false;
    std::vector<int64_t> scores;   // politicalScores
    Situation sit;
    std::vector<Command> out;
    std::vector<std::string> dropped;

    // Applies a command to the private copy; keeps it when the rules accept it.
    bool emit(Command c);
    const Empire& emp() const { return st.empire(id); }

    // ---- Which ministers act, and on what.
    bool on(Minister m) const;
    bool controlsColony(const Colony& c, Minister m) const;
    bool controlsVehicle(const Vehicle& v, Minister m) const;
    bool controlsFleet(const Fleet& f, Minister m) const;

    // ---- Map knowledge (only what the empire knows).
    std::vector<std::vector<Link>> links;  // per system, warp points in creation order
    Location homeLocation;
    bool explored(SystemId s) const { return emp().hasExplored(s); }
    bool knownLink(ObjectId warpPoint) const;
    // Warp jumps from `from` over known links (-1 = no known route).
    std::vector<int> jumpsFrom(SystemId from) const;
    // Neutral empires never leave their home system (spec 05 §7.1).
    bool mayEnter(SystemId s) const { return !neutral || s == sit.home; }

    // ---- Designs.
    const DesignInfo& info(DesignId d);
    // The newest valid, non-obsolete design of an AI design type (spec 05 §7.5).
    std::optional<DesignId> newestDesign(std::string_view aiType);
    // A table `Type` that is not a design type: the newest design whose name
    // or design type contains the text (inferred, spec 05 open question).
    std::optional<DesignId> newestDesignMatching(std::string_view text);

    // ---- Situation.
    bool atWarWith(EmpireId o) const;
    int colonyCount() const;
    int64_t strengthOf(const Vehicle& v);   // rating + 1, damage-aware
    std::vector<VehicleId> ownVehicles(Minister m) const;  // controlled by that minister, sorted by id
    bool idle(const Vehicle& v) const;      // no orders, not in a fleet with orders, not busy this turn

    // ---- Budget (spec 05 §7.5 "Budget and maintenance caps").
    Resources netIncome() const;
    Resources revenue() const;
    bool overCap(int extraPercent) const;   // soft cap: 0, hard cap: 20

    // ---- Orders (emitted only when they differ from the current ones).
    bool setOrders(VehicleId v, std::vector<Order> orders, bool repeat = false);
    bool setFleetOrders(FleetId f, std::vector<Order> orders);
    std::set<VehicleId> busy;             // vehicles that got orders this turn
    std::set<FleetId> busyFleets;
    std::set<ObjectId> reservedPlanets;   // colonization targets taken this turn

private:
    std::vector<DesignInfo> infos_;
};

// ---- Ministers (one file per group) ----------------------------------------------------------
void planStrategies(Planner& p);     // ai.cpp: AI_Strategies join the empire's list
void planPolitics(Planner& p);       // ai_diplomacy.cpp
void planDesigns(Planner& p);        // ai_design.cpp
void planResearch(Planner& p);       // ai_research.cpp
void planIntel(Planner& p);
void planFacilities(Planner& p, bool firstPass);  // ai_economy.cpp
void planShips(Planner& p);
void planColonization(Planner& p);   // ai_explore.cpp
void planExploration(Planner& p);
void planTroops(Planner& p);         // ai_military.cpp
void planTransports(Planner& p);
void planSpaceYardShips(Planner& p);
void planCarriers(Planner& p);
void planMinesSatellitesDrones(Planner& p);
void planFleets(Planner& p);
void planDefense(Planner& p);
void planAttack(Planner& p);
void planPatrol(Planner& p);
void planRepairAndResupply(Planner& p, bool repair);
void planScrap(Planner& p);
void planRetrofit(Planner& p);
void planStellarManipulation(Planner& p);

// ---- Shared helpers ------------------------------------------------------------------------------
// The AI design type of a design: its Design Type when that is one of the 39,
// else a type inferred from what it can do (inferred), else empty.
std::string aiTypeOf(const Rules& r, const Design& d, const DesignStats& st);
Role roleOf(std::string_view aiType, const DesignStats& st);
// Roles that fight: attack and defence ships and the other combat types (spec 05 §7.5 fleets).
bool combatRole(Role r);
// "Rock", "Ice" or "Gas" for a planet surface ("Gas Giant" -> "Gas").
std::string_view surfaceKey(std::string_view surface);
std::string colonyTypeName(std::string_view surface);  // "Colony (Rock)"
// The empire may settle this planet: it has the colony module for its surface
// and the game's breathable/home-type options allow it.
bool canSettle(const Rules& r, const GameState& s, const Empire& e, const SpaceObject& planet);
bool hasColonyModule(const Rules& r, const Empire& e, std::string_view surface);
// Builds the design of a template the empire would make now (spec 05 §7.5), or nullopt.
std::optional<Design> buildDesign(const Rules& r, const GameState& s, const Empire& e, const DesignTemplate& t);
// Resolves an ability identifier from the AI tables to the newest researched facility.
std::optional<uint32_t> bestFacilityFor(const Rules& r, const Empire& e, std::string_view ability);
bool facilityHas(const Rules& r, uint32_t facility, std::string_view ability);
// Speech line with the [%...] tokens filled in (empty when the pool is empty).
std::string speechLine(Planner& p, std::string_view pool, EmpireId target, EmpireId other = {}, Treaty proposed = Treaty::None,
                       SystemId system = {}, ObjectId planet = {});
// Order helpers.
Order moveOrder(Location where);
Order simpleOrder(OrderKind k);

} // namespace opense4::game::ai::detail
