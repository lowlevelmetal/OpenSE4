#pragma once

// Designs and vehicle-level aggregates (docs/spec/03 §3-7). Shared by every
// subsystem: movement, combat, economy, AI and the UI.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <span>
#include <string>
#include <vector>

namespace opense4::game {

// ---- Weapon mounts (spec 03 §2.4, §4.3) ----------------------------------------------------------

// Component values after a weapon mount. Every value is rounded to the
// nearest integer, halves to even, from a floating-point product (confirmed:
// binary). A mount that does not apply leaves the component unchanged.
struct MountedComponent {
    int tonnage = 0;
    int structure = 0;
    Resources cost;
    int supplyUsed = 0;
    int damagePercent = 100;
    int rangeModifier = 0;
    int toHitModifier = 0;
    int shieldPercent = 100;
    bool mountApplies = false;  // a mount is set and changes this component
};
MountedComponent mounted(const Rules& r, const DesignEntry& e);

// The mount changes the component: its weapon type meets the mount's
// `Weapon Type Requirement` and its family is on the mount's list (§4.3).
bool mountApplies(const Rules& r, uint32_t component, uint32_t mount);
// The hull's tonnage is within the mount's size bounds (§4.2 rule 5).
bool mountFitsHull(const Rules& r, uint32_t hull, uint32_t mount);
// The mount's `Vehicle Type` text names the hull's class (case-sensitive), or is
// "Any" in any case. Only the designer's list of mounts uses it (§2.4).
bool mountOffered(const Rules& r, uint32_t hull, uint32_t mount);
// What the designer offers for this component on this hull: all three above.
bool mountAllowed(const Rules& r, uint32_t hull, uint32_t component, uint32_t mount);

// Damage of a mounted weapon at `range` squares (1-based), after mount modifiers (§4.3).
int weaponDamageAtRange(const Rules& r, const DesignEntry& e, int range);
int weaponMaxRange(const Rules& r, const DesignEntry& e);

// ---- Designs --------------------------------------------------------------------------------------

struct DesignStats {
    int tonnageUsed = 0;
    int tonnageMax = 0;
    Resources cost;
    int structure = 0;
    int movement = 0;           // the designer's Movement (§4.4): no racial bonus or penalties
    int64_t supplyCapacity = 0;
    int cargoCapacity = 0;
    int shields = 0;
    int phasedShields = 0;
    int engines = 0;
    int weapons = 0;
    int maxWeaponRange = 0;
    bool spaceYard = false;
    bool canColonizeRock = false, canColonizeIce = false, canColonizeGas = false;
    ruleset::VehicleType vehicleType = ruleset::VehicleType::Ship;
    std::vector<std::string> problems;  // validation failures; empty = valid

    bool armed() const { return weapons > 0; }
    bool canColonize(std::string_view surface) const;
};

// The design rules of spec 03 §4.2, in the original's order (confirmed:
// binary), then the engine's own check that every component suits the hull's
// class. `owner` gates technology; pass nullptr to skip tech checks (e.g.
// inspecting foreign designs).
DesignStats computeDesignStats(const Rules& r, const Empire* owner, uint32_t hull, std::span<const DesignEntry> entries);
inline DesignStats computeDesignStats(const Rules& r, const Empire* owner, const Design& d) {
    return computeDesignStats(r, owner, d.hull, d.entries);
}

// The designer's Movement (§4.4): (Σ Standard Ship Movement) ÷ Engines Per Move,
// plus the bonus B of §6.1 when 0 < B < 100. It is also a unit's speed (§12).
int designMovement(const Rules& r, uint32_t hull, std::span<const DesignEntry> entries);

// A design name must differ from every design of every empire, exactly
// (case-sensitive) (spec 03 §4.1).
bool designNameInUse(const GameState& s, std::string_view name);
// A name no design in the game has yet: `wanted` itself when it is free, else
// `wanted` followed by the first free Roman numeral from II on ("Scout II",
// "Scout III", ...) (inferred numbering). Empty `wanted` gives "Design".
std::string uniqueDesignName(const GameState& s, std::string_view wanted);
// Zeroes a design's statistics: built, lost and enemy tonnage destroyed
// (a new, copied or redacted design starts without any).
void resetDesignStatistics(Design& d);
// What a destroyed vehicle of this design adds to its killer's "enemy tonnage
// destroyed" per unit: its hull's Tonnage (inferred, spec 04 §19).
int64_t designTonnage(const Rules& r, const Design& d);

// ---- Vehicle instances (damage-aware) ------------------------------------------------------

bool entryIntact(const Rules& r, const GameState& s, const Vehicle& v, size_t entry);
int entryStructure(const Rules& r, const Design& d, size_t entry);
int vehicleStructure(const Rules& r, const GameState& s, const Vehicle& v);          // max, per unit
int vehicleDamageTaken(const GameState& s, const Vehicle& v);
bool vehicleDestroyed(const Rules& r, const GameState& s, const Vehicle& v);
ruleset::VehicleType vehicleType(const Rules& r, const GameState& s, const Vehicle& v);

// The vehicle's ability list (§3.1): the hull's abilities, then those of every
// component that is not destroyed (marked with the component's family, and
// shields scaled by the mount). Empty when mothballed. A unit group's list
// holds the list of each of its designs once (a unit's list); rules that add
// up over the units use vehicleAbilityTotal.
std::vector<ParsedAbility> vehicleAbilities(const Rules& r, const GameState& s, const Vehicle& v);
// An ability summed over every unit of a unit group (each unit lists its
// design's abilities, spec 03 §12), or over a ship's or base's list.
int64_t vehicleAbilityTotal(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k, bool value2 = false);

// ---- Unit groups (spec 03 §12) ---------------------------------------------------------------
// Units in space are held in groups, one per (owner, unit kind, sector) and
// mixing designs; every drone is its own group (confirmed: binary). A group
// that holds one design keeps it in Vehicle::design and count; one that mixes
// designs lists them in Vehicle::mixed. Units are whole or dead in battles
// and hazards; only a mine strike leaves the front unit's damage (spec 04 §19.1).

// The designs of a vehicle and how many of each, in the order they joined: a
// mixed group's stacks, otherwise {design, count}.
std::vector<UnitStack> groupStacks(const Vehicle& v);
// Units of one design in the group (0 when it holds none).
int groupUnits(const Vehicle& v, DesignId d);
// Replaces what the group holds: empty stacks go, repeated designs merge, and
// `design` (the first stack's), `count` (the total), `mixed` and the damage
// list follow. A group left with no units has count 0 (it is gone).
void setGroupStacks(const GameState& s, Vehicle& v, std::vector<UnitStack> stacks);
void addGroupUnits(const GameState& s, Vehicle& v, DesignId d, int n);
// Removes up to n units of design d; returns how many went.
int removeGroupUnits(const GameState& s, Vehicle& v, DesignId d, int n);
// A copy of the group that holds only this stack, undamaged: the rules that
// read one design (speed, offense, defense, cloaks, weapons) look at it.
Vehicle stackProbe(const GameState& s, const Vehicle& group, const UnitStack& st);

// Maximum movement points (spec 03 §6.1 for ships and bases, §12 for units).
int vehicleMaxMovement(const Rules& r, const GameState& s, const Vehicle& v);
// A working Master Computer, or bridge-or-auxiliary control, crew quarters and
// life support all present: none of the §6.1 halvings applies. Always true for units.
bool vehicleHasControl(const Rules& r, const GameState& s, const Vehicle& v);

// Offense and defense modifiers from abilities: per-family Plus − Minus (§3.2).
int64_t vehicleToHitOffense(const Rules& r, const GameState& s, const Vehicle& v);
int64_t vehicleToHitDefense(const Rules& r, const GameState& s, const Vehicle& v);

// ---- Supply (spec 03 §7, §12) ------------------------------------------------------------------

// The value the original holds unlimited supply at, shown as "Endless".
inline constexpr int64_t kUnlimitedSupply = 60'000;
// Bases and vehicles with a working Quantum Reactor (not mothballed).
bool vehicleHasUnlimitedSupply(const Rules& r, const GameState& s, const Vehicle& v);
bool vehicleHasQuantumReactor(const Rules& r, const GameState& s, const Vehicle& v);
// Σ Supply Storage over the ability list; a unit group adds each unit's.
// Satellites and mines have none.
int64_t vehicleSupplyCapacity(const Rules& r, const GameState& s, const Vehicle& v);
// Whether the vehicle keeps supply at all (satellites and mines do not).
bool vehicleUsesSupply(const Rules& r, const GameState& s, const Vehicle& v);
// Supply a vehicle starts with: full, or the unlimited marker.
int64_t initialSupply(const Rules& r, const GameState& s, const Vehicle& v);

// Σ Cargo Storage over the ability list; unit groups in space hold none.
int vehicleCargoCapacity(const Rules& r, const GameState& s, const Vehicle& v);

// Cargo space used by a cargo hold (population mass + unit hull tonnage).
int64_t cargoSpaceUsed(const Rules& r, const GameState& s, const Cargo& c);

// Starting designs: builds a plausible design for a role from the empire's
// available tech ("scout", "colony:<Surface>", "warship", "base",
// "transport", "fighter", "satellite", "mine", "troop"). Returns nullopt if
// nothing valid can be made.
std::optional<Design> autoDesign(const Rules& r, const Empire& owner, std::string_view role);

} // namespace opense4::game
