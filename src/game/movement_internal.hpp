#pragma once

// Helpers shared by the movement implementation files (movement*.cpp).
// Not part of the engine's public interface.

#include "game/abilities.hpp"
#include "game/movement.hpp"
#include "game/turn.hpp"

#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace opense4::game::movement::detail {

inline bool alive(const Vehicle& v) { return v.count > 0; }

// Objects removed by stellar manipulation leave their system's object list
// (ids stay stable; the object record remains as a tombstone).
bool inSystem(const Galaxy& g, ObjectId o);

// Ability values written in a data record (stellar objects, system types).
int64_t rawBest(const std::vector<ruleset::Ability>& list, AbilityKind k);
int64_t rawSum(const std::vector<ruleset::Ability>& list, AbilityKind k);

bool isShipOrBase(ruleset::VehicleType t);

// Held in place by sabotage or an event (Vehicle::immobileUntil).
inline bool heldInPlace(const GameState& s, const Vehicle& v) { return s.turn < v.immobileUntil; }
// Movement points for this turn: vehicleMaxMovement, or 0 while held in place.
int turnMovement(const Rules& r, const GameState& s, const Vehicle& v);
bool isMobileType(ruleset::VehicleType t);  // ships, fighters, drones

// A computer player (or a neutral empire): its ad-hoc groups gather every own
// vehicle with an identical head order (spec 03 §8).
bool computerPlayer(const GameState& s, EmpireId e);

// An Attack order's pursuit is over: the target no longer exists, belongs to
// `owner`, or is a planet without a colony; there is no visibility test
// (spec 03 §8, confirmed: binary).
bool pursuitOver(const GameState& s, EmpireId owner, const Order& o);

// ---- Destructive centres (spec 03 §6.2, confirmed: binary) --------------------------------------
// The system's total System - Destructive Center (0: none) and System -
// Movement Towards Center (the radius of the zone around the centre).
int64_t destructiveCentre(const GameState& s, SystemId sys);
int64_t centreZone(const GameState& s, SystemId sys);
// The cost map toward `target`: the target costs 1; entering a square costs
// 1 + (30 - round(distance to the centre)) + 1000 within the zone; each square
// keeps its cheapest cost and only squares costing at most 500 spread. -1: not reached.
using CentreCostMap = std::array<int64_t, kSystemSize * kSystemSize>;
CentreCostMap centreCostMap(Sector target, int64_t zone);
// The cheapest of the nine squares around `at` (its own included; the lowest
// sector number on a tie), or nullopt when none was reached.
std::optional<Sector> centreStep(const CentreCostMap& map, Sector at);

// ---- Supply (spec 03 §7) ----------------------------------------------------------------------
// round(amount × (100 + racial Supply Cost) %) when the racial total is not 0.
int64_t scaledSupply(const Rules& r, const GameState& s, EmpireId owner, int64_t amount);
// Takes `amount`, leaving at least 0. Unlimited supply is held at its marker
// and never charged; a vehicle that lost its reactor first drops back to its
// capacity (inferred: "a normal supply value").
void spendSupply(const Rules& r, const GameState& s, Vehicle& v, int64_t amount);
// Refills to the maximum (drones never; satellites and mines have no supply).
void refillSupply(const Rules& r, const GameState& s, Vehicle& v);
// Unlimited supply back at its marker; otherwise supply clamped to the capacity.
void holdSupply(const Rules& r, const GameState& s, Vehicle& v);
// Fleet pooling (§7): equal shares of the total, each capped by the member's
// capacity; what did not fit goes in member order to members with room.
void poolSupply(const Rules& r, GameState& s, std::span<const VehicleId> members);

// ---- Losses -------------------------------------------------------------------------------------
// Marks a vehicle destroyed outside combat (count 0) with logs, mood and design statistics.
void vehicleLost(TurnContext& ctx, Vehicle& v, std::string_view cause);
// damageVehicle + vehicleLost.
bool hurt(TurnContext& ctx, VehicleId id, int amount, std::string_view cause);

// ---- Mines ----------------------------------------------------------------------------------------
// A minefield in `where` acts on this group: one of an empire that rates
// every member's owner below Non-Aggression (the mine owner's side of the
// treaty), with no member of its own (spec 03 §12, confirmed: binary).
bool minefieldActs(const Rules& r, const GameState& s, Location where, std::span<const VehicleId> group);
// A group with a sweeper entering one of its owner's tagged minefields where
// a minefield acts: its cloaked members decloak first (spec 03 §12).
void decloakSweepers(const Rules& r, GameState& s, Location where, std::span<const VehicleId> group);

// ---- Colonization (spec 03 §8) --------------------------------------------------------------------
// `colonizer` founds a colony on `planet`, which it has checked it may: the
// ship is consumed and its people and cargo land (movement_upkeep.cpp).
void foundColony(TurnContext& ctx, VehicleId colonizer, ObjectId planet);

// ---- Cargo and units (spec 03 §11-12) ------------------------------------------------------------
int64_t freeCargo(const Rules& r, const GameState& s, const Vehicle& v);
// Cargo that no longer fits (hold destroyed) is lost: population first, 1M at a
// time from the first entry, then units one at a time from the first stack (§11).
void trimCargo(const Rules& r, const GameState& s, Vehicle& v);
// Load/drop at the vehicle's sector: own colonies first, then own ships and
// bases outside `group` (troops are also dropped onto a hostile colony to
// invade it). `unit` invalid means population. amount < 0: as much as possible.
int64_t loadCargo(TurnContext& ctx, VehicleId id, DesignId unit, int64_t amount, std::span<const VehicleId> group = {});
int64_t dropCargo(TurnContext& ctx, VehicleId id, DesignId unit, int64_t amount, std::span<const VehicleId> group = {});
// Colony ships take colonists from an own colony where the order starts.
int64_t loadColonists(TurnContext& ctx, VehicleId id);

// Who launches or recovers: a vehicle with the matching ability, or an own
// colonized planet, which needs none (§12).
struct Launcher {
    VehicleId vehicle;
    ObjectId planet;
};
// Units launched this turn per launcher and unit kind (the per-game-turn budget).
struct UnitBudget {
    std::map<std::tuple<VehicleId, ObjectId, AbilityKind>, int64_t> launched;
    // Turn-based games: a new group gets its full movement at once, so it can
    // move and be recovered in the same turn (spec 03 §12, confirmed: binary).
    bool turnBased = false;
};
AbilityKind launcherFor(ruleset::VehicleType unitType);
int64_t launchUnits(TurnContext& ctx, UnitBudget& budget, Launcher from, const Order& o);
int64_t recoverUnits(TurnContext& ctx, Launcher into, const Order& o);

// ---- One-shot components ---------------------------------------------------------------------------
// Uses design entry `entry` (Emergency Energy, Emergency Resupply or
// Self-Destruct). Returns the movement points gained (-1 = nothing usable).
// No supply is charged (spec 03 §8).
int useComponent(TurnContext& ctx, VehicleId id, int entry);

// ---- Cloaking (spec 03 §8) ---------------------------------------------------------------------------
// A working part gives Cloak Level 2 or more in some sight type.
bool canCloak(const Rules& r, const GameState& s, const Vehicle& v);
// Σ mounted Supply Amount Used of the working Cloak Level parts (one unit's).
int64_t cloakSupply(const Rules& r, const GameState& s, const Vehicle& v);

// ---- Stellar manipulation (spec 01 §9) --------------------------------------------------------------
// The location the acting ship must stand on (Open Warp Point acts where the ship is).
std::optional<Location> stellarTarget(const GameState& s, const Order& o, Location here);
// Performs the action with the first capable member. Returns an empty string
// on success, else the reason it failed.
// `consumed` is false for a harmless no-op (closing an already closed link).
std::string stellarManipulation(TurnContext& ctx, std::span<const VehicleId> members, const Order& o, bool& consumed);

} // namespace opense4::game::movement::detail
