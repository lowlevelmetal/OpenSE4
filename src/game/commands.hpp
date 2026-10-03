#pragma once

// Player commands: everything a player (human, AI or minister) can change
// during their turn. One empire's ordered command list for one turn is the
// equivalent of a classic `.plr` file (docs/spec/05 §9.5). The host applies
// the lists at the start of turn processing; clients apply them to their own
// copy immediately so the UI reflects the player's changes.
//
// Commands are validated against the state they are applied to; an invalid
// command is rejected with a reason and changes nothing.

#include "game/state.hpp"

#include <span>
#include <string>
#include <variant>
#include <vector>

namespace opense4::game {

class Rules;

namespace cmd {

// ---- Vehicles and fleets ----------------------------------------------------------------
struct SetOrders {           // replaces the order list (vehicle, fleet or planet)
    VehicleId vehicle;
    FleetId fleet;           // one of the three
    std::vector<Order> orders;
    bool repeat = false;
    ObjectId planet;         // an own colony: Launch Units and Recover Units only (spec 03 §12)
};
struct CreateFleet {         // new fleet from vehicles in one sector; first is the leader
    std::string name;
    std::vector<VehicleId> members;
};
struct JoinFleet { FleetId fleet; VehicleId vehicle; };
struct LeaveFleet { VehicleId vehicle; };
struct DisbandFleet { FleetId fleet; };
struct SetFleetOptions { FleetId fleet; uint32_t formation = 0; uint32_t strategy = 0; };
struct SetVehicleStrategy { DesignId design; uint32_t strategy = 0; };
struct Rename {              // vehicle, fleet, design or planet
    VehicleId vehicle;
    FleetId fleet;
    DesignId design;
    ObjectId planet;
    std::string name;
};
// The Scrap window's actions on one vehicle (spec 03 §15, scrap.hpp), given
// to each selected vehicle in turn: carried out at once in a turn-based game,
// and in a simultaneous game left as the vehicle's only order (its list
// cleared, Repeat off), carried out at its first action. The test of
// scrapActionProblem must pass when the command is given. Computer players'
// Scrap and Retrofit go the same way (inferred). Scrap with `facilityPlanet`
// instead is Scrap Facilities, at once in both turn styles (spec 02 §6.6).
// Scrap with `moveFirst` is the Scrap minister's order to a vehicle that can
// move and stands away from a yard (spec 05 §7.5 *Scrap*): in either turn
// style its orders become a Move To that sector, which must hold an own
// space yard, then Scrap, whose test is made when it is carried out.
struct Scrap {
    VehicleId vehicle;
    ObjectId facilityPlanet;
    int32_t facilitySlot = -1;
    Location moveFirst;
};
struct Mothball { VehicleId vehicle; bool mothball = true; };   // Mothball, or Unmothball
struct Analyze { VehicleId vehicle; };
struct SelfDestruct { VehicleId vehicle; };
struct FireOn { VehicleId vehicle; };
struct SetMinister { VehicleId vehicle; ObjectId planet; bool empireWide = false; bool on = true; };
// Turn-based games: the answer to the Attack Sector question (spec 03 §6.2).
// A move of the vehicle (or fleet) stopped before a sector with enemies;
// `enter` carries it on into that sector and its battle, otherwise the move
// stops and its order fails (the list is cleared).
struct EnterSector { VehicleId vehicle; FleetId fleet; Location where; bool enter = true; };

// ---- Construction queues ------------------------------------------------------------------
struct QueueTarget {         // a planet queue, or a vehicle with a Space Yard
    ObjectId planet;
    VehicleId vehicle;
};
struct QueueAdd { QueueTarget target; QueueItem item; int32_t position = -1; };  // -1 = end
struct QueueRemove { QueueTarget target; uint32_t index = 0; };
struct QueueMove { QueueTarget target; uint32_t from = 0; uint32_t to = 0; };
struct QueueSetCount { QueueTarget target; uint32_t index = 0; int count = 1; };
struct QueueFlags { QueueTarget target; bool onHold = false; bool repeat = false; bool emergency = false; int autoWaypoint = -1; };
// Switches a queued facility item, in place, to another researched facility
// of the same family, keeping its count and what was paid into it: the
// Upgrade Facilities button moves queued facility items to the newest level
// (spec 02 §6.6), and so do the computer player's upgrades (spec 05 §7.5).
struct QueueReplaceFacility { QueueTarget target; uint32_t index = 0; uint32_t facility = 0; };
struct Retrofit { VehicleId vehicle; DesignId design; };  // a Scrap window action (above), at an own space yard (spec 03 §14)

// ---- Planets -------------------------------------------------------------------------------
struct SetColonyType { ObjectId planet; std::string colonyType; };
struct AbandonPlanet { ObjectId planet; };
// Cloak or Decloak a colony (spec 01 §6.9, confirmed: binary): acts at once
// in both turn styles and is never queued; no cost, no supply, and the
// construction queue is untouched. Cloak needs a cloak level of 2 or more in
// some sight type from the colony's facilities; Decloak a cloaked colony.
// Sight is recalculated, and a Decloak runs the first-contact check at once.
// A simultaneous game's host carries it out when it applies the player's
// orders at the start of turn processing, in player and command order, so
// the colony ends in the state the player left it in; there a Decloak's
// contact check makes only the player's side of a first contact (spec 01
// §6.9, §14 Q44, spec 05 §9.2, confirmed: binary).
struct CloakColony { ObjectId planet; bool cloak = true; };
struct TransferCargo {       // immediate transfer between own holders in the same sector
    VehicleId fromVehicle;
    ObjectId fromPlanet;
    VehicleId toVehicle;
    ObjectId toPlanet;
    DesignId unitDesign;     // invalid = population
    EmpireId populationRace;
    int64_t amount = 0;      // units, or millions of population
};
// Jettison Cargo (spec 03 §8, confirmed: binary): not an order. It destroys,
// at once and in both turn styles, the cargo the player moved in its window,
// per population race and per unit stack, from an own ship or base that is
// not mothballed or from an own colony's stored cargo (never the colony's own
// population). No movement or supply is spent, the order list, Repeat and
// fleets are untouched, and nothing is logged; each jettisoned unit counts as
// lost in its design's statistics. Exactly the amounts named go (OpenSE4
// choice: the original window's two faults are not reproduced).
struct JettisonCargo {
    VehicleId vehicle;
    ObjectId planet;                           // one of the two
    std::vector<PopulationGroup> population;   // millions per race
    std::vector<UnitStack> units;              // units per design
};

// ---- Designs --------------------------------------------------------------------------------
struct CreateDesign { Design design; };   // id/owner are assigned by the engine
// Changes an own prototype design in place (spec 03 §4.1): refused for a
// design that was built or retrofitted to, or that is in a construction queue.
struct EditDesign { DesignId design; Design with; };
struct SetDesignObsolete { DesignId design; bool obsolete = true; };
struct DeleteDesign { DesignId design; };  // only never-built designs

// ---- Research and intelligence ------------------------------------------------------------
struct SetResearch { std::vector<ResearchProject> queue; bool evenly = true; bool repeat = false; };
struct SetIntel { std::vector<IntelProjectOrder> queue; bool evenly = true; bool repeat = false; };

// ---- Diplomacy -----------------------------------------------------------------------------
// id/from/turn are assigned. `minister`: a message the computer player or the
// empire's Politics minister writes, which does not go through the player's
// picker, so a request about a third empire is not checked (spec 05 question
// 52, confirmed: binary).
struct SendMessage {
    DiplomaticMessage message;
    bool minister = false;
};
struct AnswerMessage { MessageId message; bool accept = false; std::string text; };
// Declaring war on an empire sets the declaring empire's anger toward it to
// 100 (spec 05 §7.3, §7.4). A computer player (or a Politics minister) whose
// war declaration has an empty speech pool declares nothing, yet its anger
// still becomes 100 (spec 05 §7.5 AI_Speech): this command is that decision.
// The treaty does not change and no message is sent. `target` must be another
// living empire the player has met.
struct DecideWar { EmpireId target; };
// The Politics minister carries out a demand or request it accepted (spec 05
// §7.4, confirmed: binary): decided (with its 50 % chance) before the reply
// is sent, and carried out even when no Accept Demand reply follows because
// its speech pool is empty. Remove ships or colonies, or leave a planet: the
// system is marked to avoid; declare war on or support against, break with,
// make peace with a third empire: one entry naming it in the war, break or
// peace list; attack an empire in a system or a planet: an attack target;
// stop espionage or sabotage: the intelligence projects against the
// requester are cancelled; stop hostile actions against an empire: a promise
// about the empire it names; stop attacks in a system: nothing. `demand` must
// be a demand or request delivered to the player.
struct CarryOutDemand { MessageId demand; };
// A check of the Politics minister used up one entry of a demand list naming
// `about` (spec 05 §7.4 "Demand lists"): "wants war", "wants to break" and the
// treaty proposal. Refused when the list holds no such entry.
enum class DemandList : uint8_t { War, Break, Peace };
struct UseDemandEntry { DemandList list = DemandList::War; EmpireId about; };

// ---- Empire-wide lists ------------------------------------------------------------------------
struct SetWaypoint { int slot = 0; std::optional<Waypoint> waypoint; };
struct SetSystemFlags { SystemId system; std::optional<bool> avoid; std::optional<bool> claim; };
struct SetSystemNote { SystemId system; std::string note; };
struct TagMinefield { Location location; bool tagged = true; };
struct SetStrategy { int32_t index = -1; ruleset::CombatStrategy strategy; bool remove = false; };  // -1 = add
struct SetRepairPriorities { std::vector<std::string> priorities; };
struct SetDesignTypes { std::vector<std::string> designTypes; };
struct SetColonyTypes { std::vector<std::string> colonyTypes; };
struct SetEmpireOptions {
    std::optional<bool> aiMinimalChanges;
    std::optional<std::string> passwordHash;
    std::optional<bool> chooseColonyType;  // Empire::chooseColonyType (spec 03 §8)
};
// Change Email (Empire Status, spec 06 §7 Q95): the empire's e-mail address,
// kept as game::cleanEmail gives it; the password is kept.
struct SetEmail { std::string email; };

// ---- Ministers (spec 02 §10, spec 05 §7.1) -----------------------------------------------------
// The Ministers window's settings; fields left empty are not changed.
struct SetMinisters {
    std::optional<uint32_t> areas;          // Empire::ministers, bit i = Minister i (Select All / Select None)
    std::optional<std::string> style;       // Empire::ministerStyle: a folder under Ai/, "" for none
    std::optional<bool> useRaceStyle;       // "Use Race Minister Style"
    std::optional<bool> newVehicles;        // new vehicles and launched units start under minister control
    std::optional<bool> individual;         // "Indiv. Ministers On/Off": the flag on every own vehicle, fleet and colony
    std::optional<bool> completeAi;         // "Complete AI On/Off": all areas, every flag, the new-vehicle option and ministerAll
};

// ---- Ship Movement and Ship Orders options (spec 03 §6.2, §6.4) ----------------------------------
// The Empire Options window's switches that belong to the empire; fields
// left empty are not changed.
struct SetEncounterOptions {
    std::optional<EncounterClear> clearOrdersOnEncounter;  // Empire::clearOrdersOnEncounter
    std::optional<bool> avoidTaggedMinefields;             // Empire::avoidTaggedMinefields
    std::optional<bool> avoidRestrictedSystems;            // Empire::avoidRestrictedSystems
};

// ---- Empire Options and window memories (spec 06 §1.9) ---------------------------------------------
// Replaces Empire::interfaceOptions.
struct SetInterfaceOptions { InterfaceOptions options; };

// ---- Reports (spec 05 §8 "Design knowledge") ------------------------------------------------------
// A human player opened the report of a foreign vehicle: when its scanners
// reach it, the designs the report shows are learned (sight::learnFromReport).
struct OpenVehicleReport { VehicleId vehicle; };

} // namespace cmd

using Command = std::variant<
    cmd::SetOrders, cmd::CreateFleet, cmd::JoinFleet, cmd::LeaveFleet, cmd::DisbandFleet, cmd::SetFleetOptions,
    cmd::SetVehicleStrategy, cmd::Rename, cmd::Scrap, cmd::Mothball, cmd::SetMinister,
    cmd::QueueAdd, cmd::QueueRemove, cmd::QueueMove, cmd::QueueSetCount, cmd::QueueFlags, cmd::Retrofit,
    cmd::SetColonyType, cmd::AbandonPlanet, cmd::TransferCargo,
    cmd::CreateDesign, cmd::SetDesignObsolete, cmd::DeleteDesign,
    cmd::SetResearch, cmd::SetIntel,
    cmd::SendMessage, cmd::AnswerMessage,
    cmd::SetWaypoint, cmd::SetSystemFlags, cmd::SetSystemNote, cmd::TagMinefield, cmd::SetStrategy,
    cmd::SetRepairPriorities, cmd::SetDesignTypes, cmd::SetColonyTypes, cmd::SetEmpireOptions,
    cmd::SetMinisters, cmd::SetEncounterOptions, cmd::EnterSector, cmd::EditDesign, cmd::OpenVehicleReport,
    cmd::QueueReplaceFacility, cmd::DecideWar, cmd::SetInterfaceOptions, cmd::CarryOutDemand, cmd::UseDemandEntry, cmd::JettisonCargo,
    cmd::CloakColony, cmd::Analyze, cmd::SelfDestruct, cmd::FireOn, cmd::SetEmail>;

// One empire's turn (the `.plr` equivalent).
struct EmpireOrders {
    EmpireId empire;
    uint32_t turn = 0;               // the turn these orders were made for
    std::vector<Command> commands;
};

struct CommandResult {
    bool ok = true;
    std::string error;
    static CommandResult fail(std::string e) { return {false, std::move(e)}; }
};

// Validates and applies one command for `empire`.
CommandResult apply(const Rules& r, GameState& s, EmpireId empire, const Command& c);
// Short label for logs and debugging ("QueueAdd", ...).
std::string_view commandName(const Command& c);
// The planets whose colonies the commands name (a colony's orders, queue,
// cargo, name, type, minister flag, facilities or cloak), in first-named
// order without repeats: the colonies a player's orders carry to the host
// (spec 05 §9.2: the objects that got a command or a queue change; which
// commands count is inferred).
std::vector<ObjectId> coloniesNamed(std::span<const Command> commands);

// Scrap-window values shared with the UI (spec 03 §15): round(design cost × P %)
// per resource; a fighter or satellite group returns that per unit.
Resources scrapRefund(const Rules& r, const GameState& s, const Vehicle& v);
Resources unmothballCharge(const Rules& r, const GameState& s, const Vehicle& v);

// Queue helpers shared with the economy and the UI.
ConstructionQueue* findQueue(GameState& s, EmpireId empire, const cmd::QueueTarget& t);
// Why an item cannot go in this queue (empty = it can).
std::string queueItemProblem(const Rules& r, const GameState& s, EmpireId empire, const cmd::QueueTarget& t, const QueueItem& item);

} // namespace opense4::game
