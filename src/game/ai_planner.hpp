#pragma once

// Internal to the computer player (ai_*.cpp). The Planner holds one empire's
// view of the game for one turn. It works on a private copy of the state and
// applies each command to that copy as soon as it is planned (Planner::emit),
// so later decisions see earlier ones (a new design can be queued, a new
// fleet can be given orders) and every command it returns is known to be
// accepted when the turn pipeline applies the list in order.

#include "core/rng.hpp"
#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/design.hpp"

#include <deque>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace opense4::game::ai::detail {

enum class Mode : uint8_t {
    Computer,   // a computer player, or a human empire played by the computer
    Minimal,    // an absent human who asked for minimal changes (spec 05 §9.2)
    Minister,   // a human's ministers: only objects under minister control
};

// What a design is for, from the AI's point of view.
enum class Role : uint8_t { Other, Scout, Colonizer, Warship, Base, Transport, Carrier, Layer, Sweeper, YardShip, Unit };

struct DesignInfo {
    bool ready = false;
    std::string aiType;     // AI design type ("Attack Ship", "Colony (Rock)", "Scout", ...)
    Role role = Role::Other;
    DesignStats stats;
    int64_t attack = 0;     // summed weapon strength
    int64_t defense = 0;    // structure and shields
    int64_t combat() const { return attack + defense / 2; }
};

struct Link {
    ObjectId warpPoint;
    SystemId to;
};

class Planner {
public:
    Planner(const Rules& rules, const GameState& s, EmpireId e, Mode mode);
    PlanReport run();

    const Rules& r;
    GameState st;           // private copy; commands are applied to it as they are planned
    EmpireId id;
    Mode mode;
    const AiProfile& prof;
    AiState state;
    int difficulty = 1;     // 0 easy .. 3 hardest
    Rng rng;
    bool neutral = false;
    std::vector<Command> out;
    std::vector<std::string> dropped;

    // Applies a command to the private copy; keeps it when the rules accept it.
    bool emit(Command c);
    const Empire& emp() const { return st.empire(id); }

    // ---- What this planner may touch.
    bool fullControl() const;   // computer player, or a human with every minister on
    bool controlsColony(const Colony& c) const;
    bool controlsVehicle(const Vehicle& v) const;
    bool controlsFleet(const Fleet& f) const;

    // ---- Map knowledge (only what the empire knows).
    std::vector<std::vector<Link>> links;  // per system, warp points in creation order
    SystemId home;
    Location homeLocation;
    bool explored(SystemId s) const { return emp().hasExplored(s); }
    bool knownLink(ObjectId warpPoint) const;
    // Warp jumps from `from` over known links (-1 = no known route).
    std::vector<int> jumpsFrom(SystemId from) const;
    const std::vector<int>& jumpsFromHome();
    // Neutral empires never leave their home system (spec 05 §7.1).
    bool mayEnter(SystemId s) const { return !neutral || s == home; }

    // ---- Designs.
    const DesignInfo& info(DesignId d);
    // Own, non-obsolete, valid designs of an AI type, best first.
    std::vector<DesignId> designsOfType(std::string_view aiType);
    std::optional<DesignId> bestDesign(std::string_view aiType);

    // ---- Situation.
    std::vector<int64_t> threat;          // per system: visible hostile armed strength
    std::vector<uint8_t> ownSystem;       // per system: we have a colony there
    bool atWarWith(EmpireId o) const;
    bool fightsWith(EmpireId o) const;    // treaty lets ships fight on contact
    int colonyCount() const;
    int64_t vehicleCombat(const Vehicle& v);
    std::vector<VehicleId> ownVehicles() const;  // controlled, sorted by id

    // ---- Orders (emitted only when they differ from the current ones).
    bool setOrders(VehicleId v, std::vector<Order> orders, bool repeat = false);
    bool setFleetOrders(FleetId f, std::vector<Order> orders);
    std::set<VehicleId> busy;             // vehicles that got orders this turn
    std::set<FleetId> busyFleets;
    std::set<ObjectId> reservedPlanets;   // colonization targets taken this turn

private:
    std::deque<DesignInfo> infos_;  // stable references while designs are added
    std::vector<int> homeJumps_;
    bool homeJumpsReady_ = false;
};

// ---- Subsystems (one file each) ---------------------------------------------------------------
void planStrategies(Planner& p);
void planResearch(Planner& p);       // ai_research.cpp
void planIntel(Planner& p);
void planDesigns(Planner& p);        // ai_design.cpp
void planColonyTypes(Planner& p);    // ai_economy.cpp
void planConstruction(Planner& p);
void planExploration(Planner& p);    // ai_explore.cpp
void planColonization(Planner& p);
void planMilitary(Planner& p);       // ai_military.cpp
void planLogistics(Planner& p);
void planDiplomacy(Planner& p);      // ai_diplomacy.cpp

// ---- Shared helpers ------------------------------------------------------------------------------
// The AI design type of a design: its Design Type when that names a template,
// else a type inferred from what it can do.
std::string aiTypeOf(const Rules& r, const AiProfile& prof, const Design& d, const DesignStats& st);
Role roleOf(std::string_view aiType, const DesignStats& st);
int64_t weaponStrength(const Rules& r, const Design& d);
// "Rock", "Ice" or "Gas" for a planet surface ("Gas Giant" -> "Gas").
std::string_view surfaceKey(std::string_view surface);
std::string colonyTypeName(std::string_view surface);  // "Colony (Rock)"
// Builds the best design of a template the empire can make now.
std::optional<Design> buildDesign(const Rules& r, const Empire& e, const DesignTemplate& t, int tonnageCap);
int64_t designScore(const Rules& r, const Design& d, const DesignStats& st, const DesignTemplate& t);
// Could `e` colonize this planet with a ship that colonizes `surface`s?
bool colonizable(const Rules& r, const GameState& s, const Empire& e, const SpaceObject& planet);
int64_t colonyTargetValue(const Rules& r, const GameState& s, const Empire& e, const SpaceObject& planet);
// Resolves an ability identifier from the AI tables to facilities.
std::optional<uint32_t> bestFacilityFor(const Rules& r, const Empire& e, std::string_view ability);
bool facilityHas(const Rules& r, uint32_t facility, std::string_view ability);
bool systemWideAbility(std::string_view ability);
// Speech line with the [%...] tokens filled in (empty when the pool is empty).
std::string speechLine(Planner& p, std::string_view pool, EmpireId target, EmpireId other = {}, Treaty proposed = Treaty::None,
                       SystemId system = {}, ObjectId planet = {});
// Estimated fighting strength of another empire, from what the planner's empire can see.
int64_t knownStrength(Planner& p, EmpireId of);

// ---- Exploration and colonization knowledge (ai_explore.cpp) ---------------------------------
struct ColonyTarget {
    ObjectId planet;
    int64_t value = 0;
    int jumps = 0;
};
// Uncolonized planets the empire knows and may settle with a ship that
// colonizes `surface` ("Rock", "Ice", "Gas"), best first. Planets other own
// colony ships are already headed for are left out.
std::vector<ColonyTarget> colonyTargets(Planner& p, std::string_view surface, SystemId from);
// Warp points in explored systems that lead somewhere unexplored.
std::vector<ObjectId> explorationFrontier(Planner& p);
// Colonization targets claimed by existing Colonize orders (and this turn's).
bool planetClaimed(Planner& p, ObjectId planet);

} // namespace opense4::game::ai::detail
