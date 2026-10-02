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

// Strength ratings (spec 05 §7.2) are kept in tenths of a point: the half
// shields keep their fraction and a planet's shields count one fifth.
inline constexpr int64_t kStrengthScale = 10;
// Every AI jump count (spec 05 §7.2): an unreachable system is 999 jumps away.
inline constexpr int kUnreachable = 999;

struct DesignInfo {
    bool ready = false;
    std::string aiType;     // one of the 39 AI design types (every design has one, spec 05 §7.5)
    Role role = Role::Other;
    DesignStats stats;
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
    int64_t value = 0;     // tenths: the foreign ratings in its sector plus the planet's defence
};

// An enemy-in-territory entry of the defend list (spec 05 §7.2): one per
// (system, sector, owner).
struct DefendEntry {
    Location where;
    EmpireId owner;
    int64_t threat = 0;        // tenths
    int jumps = 0;             // from home
    int64_t ourMaxPopulation = 0;  // our colonies' maximum population in the sector
    bool planetSector = false; // the sector holds a planet (not an asteroid field)
    Threat latest;             // the entry's latest object
};
// The entries' order (spec 05 §7.2): fewest jumps, our colonies' maximum
// population, planet sectors first (the weaker threat first among them),
// then the threat itself, strongest first unless `weakestFirst`.
void sortDefendEntries(std::vector<DefendEntry>& entries, bool weakestFirst);

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
    bool colonized = false;    // a hostile empire's colony without population
};

struct Situation {
    SystemId home;
    std::vector<uint8_t> territory;       // per system
    std::vector<int64_t> ours;            // our strength per system
    std::vector<int64_t> hostile;         // strength of the hostile empires we have met, per system
    std::vector<int> homeJumps;           // warp jumps from home over every link (kUnreachable = none)
    std::vector<Threat> enemyInTerritory;
    std::vector<Threat> enemyNearby;      // hostile mine fields in our territory
    std::vector<Candidate> candidates;    // attack candidates, best first
    std::vector<ObjectId> frontier;       // warp points into unexplored space
    std::vector<ObjectId> freeFrontier;   // ... that none of our ships is headed for
    std::vector<DefendEntry> defendEntries;  // in the defend list's order (strongest threat first)
    std::vector<SystemId> defend;         // systems to defend, most urgent first
    std::vector<ColonyTarget> colonyTargets;  // in the colonization order
    bool contact = false;                 // we have met a living empire
    bool bordersUnexplored = false;       // a system of our territory borders unexplored space
    bool notConnected = false;            // the Not Connected test (spec 05 §7.2)
};

// Everything that reads the galaxy the way the AI does. `settings` is the
// empire's AI_Settings (defend list size).
Situation assess(const Rules& r, const GameState& s, EmpireId e, const AiProfile& prof);
// The systems the Politics minister claims now (spec 05 §7.2 "Territory",
// §7.3), sorted: every system holding one of the empire's colonies, and,
// unless the empire is neutral, every other system one jump away over every
// link, except a computer player's home system and the systems the empire
// agreed to leave (these two exclusions apply only to the neighbours).
std::vector<SystemId> computeTerritory(const GameState& s, EmpireId e);
// The territory the AI's lists use (spec 05 §7.2): the empire's claimed
// systems (Empire::claimedSystems), sorted. For a computer player they are
// the claims its Politics minister made during the previous turn.
std::vector<SystemId> territoryOf(const GameState& s, EmpireId e);
// Is this hostile object noticed this turn? Low difficulty misses each one
// with a 10 % chance per turn (spec 05 §7.2); the roll is the same for every
// caller on the same turn.
bool notices(const GameState& s, EmpireId e, uint64_t object);
inline uint64_t vehicleKey(VehicleId v) { return (uint64_t{1} << 40) | v.value; }
inline uint64_t planetKey(ObjectId o) { return (uint64_t{2} << 40) | o.value; }
// Spec 05 §7.2 strength rating of one vehicle, in tenths (kStrengthScale):
// undamaged weapons and Boarding Attack, half the shields when either is
// above 0, plus the fighters in its cargo; a unit group its number of units.
int64_t vehicleRating(const Rules& r, const GameState& s, const Vehicle& v);
// The summed best damage at any range over a design's weapon parts.
int64_t designWeaponDamage(const Rules& r, const Design& d);
// Warp jumps from `from` over every link of the map, known or not
// (kUnreachable when there is no route), as every AI jump count (spec 05 §7.2).
std::vector<int> jumpsOver(const GameState& s, SystemId from);
// Military hostility (spec 05 §7.2): below Non-Aggression, or not met.
bool hostileTo(const Empire& e, EmpireId other);
// A home system: the homeworld's, else the first colony's, else invalid.
SystemId homeSystem(const GameState& s, EmpireId e);

class Planner {
public:
    Planner(const Rules& rules, const GameState& s, EmpireId e, Mode mode, uint64_t salt);
    // Group 1 (ai.hpp); `politics` and `others` pick its parts: the Politics
    // minister, then the ministers after it.
    void runOrders(bool politics = true, bool others = true);
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
    uint32_t date = 0;      // the date the ministers see (aiDate, spec 05 §7.5 "The date")
    int64_t unitReserve = 0;  // the units reserve the vehicle list applies (ai::unitReserveLeft)
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

    // ---- The map.
    std::vector<std::vector<Link>> links;  // per system, warp points in creation order
    Location homeLocation;
    bool explored(SystemId s) const { return emp().hasExplored(s); }
    // Warp jumps from `from` over every link, known or not (kUnreachable = no route; spec 05 §7.2).
    std::vector<int> jumpsFrom(SystemId from) const;
    // Neutral empires never leave their home system (spec 05 §7.1).
    bool mayEnter(SystemId s) const { return !neutral || s == sit.home; }

    // ---- Designs.
    const DesignInfo& info(DesignId d);
    // The newest design of an AI design type the empire can build, by
    // creation turn (the first listed on a tie). `anyMark`: obsolete designs
    // count too, as in the vehicle list (spec 05 §7.5).
    std::optional<DesignId> newestDesign(std::string_view aiType, bool anyMark = false);
    // The newest buildable design the Design minister made from the
    // AI_DesignCreation template named `name` (ignoring case), obsolete or not.
    std::optional<DesignId> newestFromTemplate(std::string_view name);

    // ---- Situation.
    bool atWarWith(EmpireId o) const;
    int colonyCount() const;
    int64_t strengthOf(const Vehicle& v);   // rating + 1 (in tenths), damage-aware
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
// The AI design type of a design (spec 05 §7.5 "Design types of other
// designs", confirmed: binary): its type label when that is exactly one of the
// 39, else the first of the fixed tests on what it carries. Never empty.
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
// Abilities whose parts the Design minister ranks by their Amount 1 (spec
// 05 §7.5 `AI_DesignCreation`): shields, cargo, supply, movement, bays,
// mines, sweeping and troops (ai_design.cpp).
bool amountAbility(AbilityKind k);
// The researched facility that provides an ability of the AI tables best
// (spec 05 §7.5 `AI_Construction_Facilities`, confirmed: binary): the highest
// Value 1 for an amount-type ability, otherwise the highest sum of its tech
// requirement levels; a tie goes to the later facility in the file.
std::optional<uint32_t> bestFacilityFor(const Rules& r, const Empire& e, std::string_view ability);
bool facilityHas(const Rules& r, uint32_t facility, std::string_view ability);
// A line drawn from a pool of the empire's AI_Speech with the [%...] tokens
// filled in; nullopt when the pool is empty or missing, and then the message
// is not sent at all (spec 05 §7.5 AI_Speech, confirmed: binary).
std::optional<std::string> speechLine(Planner& p, std::string_view pool, EmpireId target, EmpireId other = {}, Treaty proposed = Treaty::None,
                                      SystemId system = {}, ObjectId planet = {});
// What an item of a trade, gift or tribute is worth to the receiving side
// (spec 05 §7.4 item values).
int64_t tradeItemValue(const Planner& p, const PackageItem& item, EmpireId giver, EmpireId receiver);
// A battle `e` lost while defending (spec 05 §7.3 term 1, "Combat Defending
// Lost", confirmed: binary): it took part, the verdict is a loss for it, and it
// was not the battle's current player (ai_anger.cpp).
bool lostWhileDefending(const CombatRecord& rec, EmpireId e);
// The mine and satellite layers (spec 05 §7.5 "Layers", confirmed: binary):
// the warp points of our colony systems whose far system holds any object of
// another empire, each with the sum of those empires' weights (mines 1, 4
// when hostile, 7 at war; satellites 1, 2 when hostile), less those whose
// sector already holds the cap of our units of the layer's kind.
std::vector<std::pair<Location, int>> layerCandidates(Planner& p, bool mines);
// The mine layers' star-destroyer flag of this turn (spec 05 §7.5 "Layer fallback").
bool starDestroyerFlag(const Planner& p);
// The movement points the Exploration minister compares with an explorer's
// distance, "at that moment" (spec 05 §7.5): what the vehicle's last movement
// left, as the Space Yard Ships minister reads it. In a simultaneous game
// that is its whole movement, since each action gives its points back. A
// turn-based game gives the points back only after the ministers (§8 step 3),
// which would keep a ship that moved last turn from ever warping, so there
// the movement of this turn's run counts (inferred, spec 05 Q64).
int movementNow(const Planner& p, const Vehicle& v);
// Order helpers.
Order moveOrder(Location where);
Order simpleOrder(OrderKind k);
// The ministers' movement orders (spec 05 §7.5 "How long the ministers'
// movement orders last", confirmed: binary): a Seek toward a sector, or after
// a ship or planet, that lasts one movement phase (the defence, attack, fleet
// goals, exploration, patrol, repair and Space Yard Ship orders); the stored
// Attack, which a ship already on its target's sector carries out at once and
// is done with; and the recruits' Join Fleet, which lasts until it joins.
Order seekOrder(Location where);
Order seekAfter(const Vehicle& target);
Order seekPlanet(const GameState& s, ObjectId planet);
Order attackHere();
Order joinFleetOrder(FleetId fleet);

} // namespace opense4::game::ai::detail
