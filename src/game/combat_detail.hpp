#pragma once

// Internals shared by the combat sources (combat.cpp, combat_space.cpp,
// combat_ground.cpp) and the combat unit tests. Not part of the engine API.

#include "core/rng.hpp"
#include "game/combat.hpp"
#include "game/design.hpp"

#include <optional>
#include <span>
#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::combat::detail {

bool enemies(const GameState& s, EmpireId a, EmpireId b);   // hostile in either direction
// Mines, troops and weapon platforms are never combat pieces.
bool canBePiece(ruleset::VehicleType t);
// Whether `viewer` sees a vehicle, or a colony, for the battle check: the
// sight rules of spec 01 §6.3 worked out afresh, by current sensors (spec 04
// §2, §19.2 Q74).
bool visibleTo(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& v);
bool colonyVisibleTo(const Rules& r, const GameState& s, EmpireId viewer, ObjectId planet);

// Who is in a sector (spec 04 §2, §3). A battle starts whenever the battle
// check passes (BattleCheck), even with nobody hostile having pieces there
// (only minefields seen): it then ends at its first end check. Once it
// starts, every owned vehicle that can be a piece and every colony there take
// part, including empires hostile to nobody present.
struct Forces {
    bool battle = false;               // the check passed
    std::vector<EmpireId> empires;     // every empire with a piece, by id
    std::vector<VehicleId> vehicles;   // every vehicle that becomes a piece, by id
    std::vector<ObjectId> colonies;    // every colony there, in object order
    std::vector<ObjectId> obstacles;   // stars, warp points, comets, uncolonised planets
};
Forces battleForces(const Rules& r, const GameState& s, Location where, const BattleCheck& check = {});

// Whether a vehicle moved into its sector this turn (spec 04 §3), whether it
// came through a warp point (the sector it left is in another system), and
// the direction of the neighbouring sector it came from: (dx, dy) in -1..1
// each, (0, 0) when it was already there or came through a warp point.
bool arrivedThisTurn(const GameState& s, const Vehicle& v);
bool arrivedByWarp(const GameState& s, const Vehicle& v);
std::pair<int, int> arrivalDirection(const GameState& s, const Vehicle& v);

// ---- Component and hull ability lookups ----------------------------------------------------------

int64_t componentSum(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k);   // intact components
int64_t componentBest(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k);  // best single intact component
// Spec 04 §7 over a list of facilities (a planet's): the best of each facility
// Family, summed. A vehicle's per-family to-hit is vehicleToHitOffense/Defense
// (design.hpp).
int64_t facilityFamilyBest(const Rules& r, std::span<const uint32_t> facilities, AbilityKind k);
bool hasIntactComponent(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k);
bool designHasComponent(const Rules& r, const Design& d, AbilityKind k);                   // destroyed or not
int64_t hullSum(const Rules& r, const Design& d, AbilityKind k);
bool vehicleArmed(const Rules& r, const GameState& s, const Vehicle& v);                   // any intact weapon

// ---- Mounts (spec 04 §8, §18.2) ---------------------------------------------------------------------

// Whether a design entry's mount applies to its component (weapon type requirement).
bool mountApplies(const Rules& r, const DesignEntry& e);
// A component's structure in combat: Tonnage Structure × the mount's structure
// percent (rounded) when the mount applies.
int combatStructure(const Rules& r, const DesignEntry& e);
// Shield Generation or Phased Shield Generation of a part, scaled by the mount's shield percent (rounded).
int64_t mountedShield(const Rules& r, const DesignEntry& e, AbilityKind k);

// ---- Supply (spec 04 §6) --------------------------------------------------------------------------

// Bases and vehicles with an intact Quantum Reactor never run out.
bool unlimitedSupply(const Rules& r, const GameState& s, const Vehicle& v);
// Satellites, drones, weapon platforms and planets never need supplies to fire.
bool usesSupply(const Rules& r, const GameState& s, const Vehicle& v);
// Whether the vehicle has supplies to fire and hold shields.
bool hasSupplies(const Rules& r, const GameState& s, const Vehicle& v);
// Supply used by one weapon's shot: Supply Amount Used × the mount's supply percent (rounded).
int supplyPerShot(const Rules& r, const DesignEntry& e);

ruleset::VehicleType typeOf(const Rules& r, const GameState& s, const Vehicle& v);
int remainingStructure(const Rules& r, const GameState& s, const Vehicle& v);   // one unit, intact parts
int designStructure(const Rules& r, const Design& d);                           // every part intact

// ---- To-hit (spec 04 §7) -------------------------------------------------------------------------

int clampChance(int chance);   // 1..99
// Base + offense − defense − per-square × aim distance − interference, clamped once.
int toHitChance(const CombatSettings& cs, int aimDistance, int offense, int defense, int interference);
// Racial modifiers of an empire: culture Space Combat + (characteristic - 100).
int racialOffense(const Rules& r, const Empire& e);
int racialDefense(const Rules& r, const Empire& e);
// An empire's `Combat Modifier - System` / `Damage Modifier - System` / `Shield
// Modifier - System` total in a system: the system's own value plus, for each
// of its colonies and vehicles there, the best single facility or component.
int systemModifier(const Rules& r, const GameState& s, EmpireId e, SystemId sys, AbilityKind k);
// `Sector - Sensor Interference` and `Sector - Shield Disruption` at a sector
// (the system's own abilities and the objects in the sector).
int sensorInterference(const GameState& s, Location where);
int shieldDisruption(const GameState& s, Location where);
// A ship's or base's offense/defense (spec 04 §7) without the weapon's own
// modifier and the system bonus: family-best components + hull + truncated
// crew and fleet experience + racial. Mothballed: 0.
int vehicleOffense(const Rules& r, const GameState& s, const Vehicle& v, int crewExperience, int fleetExperience);
int vehicleDefense(const Rules& r, const GameState& s, const Vehicle& v, int crewExperience, int fleetExperience);
// A unit group's: the design's (plus − minus), never below 0, + racial.
int unitOffense(const Rules& r, const GameState& s, const Vehicle& v);
int unitDefense(const Rules& r, const GameState& s, const Vehicle& v);
int fleetExperience(const GameState& s, const Vehicle& v);

// ---- Damage (spec 04 §9) --------------------------------------------------------------------------

enum class Layer : uint8_t { Engines, Weapons, ShieldGenerators, MasterComputers, BoardingParties, SecurityStations, PlanetDestroyers };

struct DamageRule {
    enum class Shields : uint8_t { Absorb, PhasedOnly, Ignore };
    Shields shields = Shields::Absorb;
    int shieldMultiply = 1, shieldDivide = 1;   // Quad/Double/Half/Quarter Damage To Shields
    bool shieldsOnly = false;                   // drains shields and nothing else
    bool hullDamaging = true;                   // uses and feeds the damage pool
    bool armorSpecials = true;                  // crystalline and emissive armor act
    bool skipsArmor = false;                    // non-armor components while any are left
    std::optional<Layer> only;                  // restricted to one kind of component
    bool structural = true;                     // false: special effect or planet-only, never reaches components
};
DamageRule damageRule(DamageType t);

// A piece's single shield pool (spec 04 §9.2).
struct ShieldState {
    enum class Kind : uint8_t { None, Normal, Phased };
    int current = 0;
    int max = 0;
    Kind kind = Kind::None;   // Normal if any normal generator, Phased if all are phased
};
// Maximum and kind from intact generators, the positive system modifier and
// the sector's disruption. No shields without supplies or when mothballed.
// `fill` sets current to the maximum; otherwise current is capped at it.
void refreshShields(const Rules& r, const GameState& s, const Vehicle& v, int systemBonus, int disruption, ShieldState& sh, bool fill);
// Whether a hit of this rule is stopped by these shields.
bool shieldsApply(const DamageRule& rule, const ShieldState& sh);
// Step 6 of spec 04 §9.1: drains the shields; returns what gets past them.
int64_t absorbShields(ShieldState& sh, int64_t damage, const DamageRule& rule);

// Spec 04 §9.1a: destroys whole components of `v`, drawn at random weighted by
// structure, armor first; returns the damage left over. Marks a destroyed
// component with damage >= its structure.
int64_t destroyComponents(const Rules& r, const GameState& s, Vehicle& v, int64_t damage, DamageType type, Rng& rng);
void destroyEntry(const Rules& r, const Design& d, Vehicle& v, size_t entry);

// One hit through the component pipeline of a ship or base (spec 04 §9.1
// steps 2 and 6-9): the pool, shields, crystalline and emissive armor, then
// components. `damage` is after the system damage modifier. `pool` is the
// ship's damage pool. Returns the damage that reached the components.
struct HitResult {
    int64_t shieldDamage = 0;
    int64_t reached = 0;   // damage that went to the components
    bool destroyed = false;
};
HitResult hitVehicle(const Rules& r, const GameState& s, Vehicle& v, ShieldState& sh, int64_t& pool, int64_t damage, DamageType type,
                     Rng& rng);
// Whether a hit of this type can change anything on this vehicle (mines and targeting skip it otherwise).
bool canAffectVehicle(const Rules& r, const GameState& s, const Vehicle& v, const ShieldState& sh, DamageType type);

// A unit's toughness (spec 04 §9.4): its design's structure and shields X.
// Its hit points H are structure + X, where fighters, troops and weapon
// platforms count X twice (their structure already includes X once).
struct UnitToughness {
    int64_t structure = 0;
    int64_t shields = 0;
    bool doubled = false;
    ShieldState::Kind kind = ShieldState::Kind::None;
    int64_t hitPoints() const { return structure + shields * (doubled ? 2 : 1); }
};
UnitToughness unitToughness(const Rules& r, const Design& d);
int64_t unitHitPoints(const Rules& r, const Design& d);   // H

// A unit group's damage pool never exceeds this; a hit tries up to this many draws (spec 04 §9.4).
inline constexpr int64_t kMaxUnitPool = 50000;
inline constexpr int kMaxUnitDraws = 20;
// Spec 04 §9.4: one hit of `damage` on units kept as stacks (a unit group, or
// the units stored on a planet), with the group's damage pool P (`pool`) and
// shield pool Q (`shieldPool`). `entries` are the stacks that form the group.
// A Shields Only hit adds to Q; a hull-damaging hit joins P (at most
// kMaxUnitPool); any other type is judged on its own damage and leaves P as it
// was. Then up to 20 draws, each of an entry at random (a dead one wastes the
// draw): a unit dies when P + Q reaches H (P alone reaching H − X when the type
// skips its shields); then H leaves P, or, with Q above 0, H − X leaves P and X
// leaves Q. Returns the units killed; `killed` counts them per stack.
int hitUnits(const Rules& r, const GameState& s, std::vector<UnitStack>& stacks, std::span<const size_t> entries, int64_t& pool,
             int64_t& shieldPool, int64_t damage, DamageType type, Rng& rng, std::vector<int>& killed);

// Restores destroyed components with Armor Regeneration in design order, each
// costing its structure, while `budget` lasts; a part that costs more than is
// left is skipped (spec 04 §9.3). Returns what was used.
int64_t restoreRegeneratingArmor(const Rules& r, const GameState& s, Vehicle& v, int64_t budget);
bool hasDestroyedRegeneratingArmor(const Rules& r, const GameState& s, const Vehicle& v);

// ---- Ground combat (spec 04 §13) --------------------------------------------------------------------

struct GroundFight {
    EmpireId attacker, defender;
    // The landed troops, fighting for `attacker` (losses lower the counts; empty stacks are left in place).
    std::vector<UnitStack>* invaders = nullptr;
    // The planet's stored units, all serving `defender`: its troops and other units (the same).
    Cargo* cargo = nullptr;
    const std::vector<PopulationGroup>* population = nullptr;
    int* militia = nullptr;                               // the colony's militia pool (-1: raise it now)
    int64_t groundDefensePercent = 0;                     // Planet - Change Ground Defense
};
struct GroundOutcome {
    int rounds = 0;
    int attackersLost = 0, defendersLost = 0, militiaLost = 0;
    int attackersAtStart = 0;
    bool captured = false;        // the defenders are gone and the attackers remain
    bool attackersGone = false;
};
GroundOutcome fightGround(const Rules& r, GameState& s, const CombatSettings& cs, const GroundFight& f, Rng& rng);
// The captor takes the colony with its facilities, stored units and
// population, and its surviving landed troops join the cargo: logs and mood events.
void capturePlanet(TurnContext& ctx, Colony& c, EmpireId captor);
// Adds units to a list of stacks (merging by design; empty stacks are skipped).
void joinUnits(std::vector<UnitStack>& into, std::span<const UnitStack> units);
// The invasion of a colony is over: its landed troops go (with `joinCargo`,
// into its cargo, where they serve the owner), and so does its militia pool.
void endInvasion(Colony& c, bool joinCargo);
// Culture Ground Combat + (Physical Strength − 100).
int groundModifier(const Rules& r, const Empire& e);

// ---- Mines (spec 04 §10.6) -------------------------------------------------------------------------

// The groups that entered a sector for the mines: the given vehicles as one
// group, or (none given) the vehicles that moved in this turn, one group per
// empire; when no vehicle anywhere is marked as moved this turn, every
// vehicle there, by empire (inferred).
std::vector<std::vector<VehicleId>> enteringGroups(const Rules& r, const GameState& s, Location where, std::span<const VehicleId> entering);
bool minesCanStrike(const Rules& r, const GameState& s, Location where, std::span<const VehicleId> entering);
void resolveMines(TurnContext& ctx, Location where, std::span<const VehicleId> entering, Rng& rng);

// ---- Experience (spec 04 §15) ----------------------------------------------------------------------

// Adds `tenths` of a point to (whole, tenths), capped at kMaxCombatExperience.
void addExperience(int& whole, int& tenths, int gainTenths);

// ---- Record helpers -----------------------------------------------------------------------------------

std::string sectorName(const GameState& s, Location where);

} // namespace opense4::game::combat::detail
