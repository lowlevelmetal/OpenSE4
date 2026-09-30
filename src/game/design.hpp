#pragma once

// Designs and vehicle-level aggregates (docs/spec/03 §3-7). Shared by every
// subsystem: movement, combat, economy, AI and the UI.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <span>
#include <string>
#include <vector>

namespace opense4::game {

// Component values after a weapon mount (spec 03 §4.3).
struct MountedComponent {
    int tonnage = 0;
    int structure = 0;
    Resources cost;
    int supplyUsed = 0;
    int damagePercent = 100;
    int rangeModifier = 0;
    int toHitModifier = 0;
    int shieldPercent = 100;
};
MountedComponent mounted(const Rules& r, const DesignEntry& e);

// Damage of a mounted weapon at `range` squares (1-based), after mount modifiers.
int weaponDamageAtRange(const Rules& r, const DesignEntry& e, int range);
int weaponMaxRange(const Rules& r, const DesignEntry& e);

bool mountAllowed(const Rules& r, uint32_t hull, uint32_t component, uint32_t mount);

struct DesignStats {
    int tonnageUsed = 0;
    int tonnageMax = 0;
    Resources cost;
    int structure = 0;
    int movement = 0;           // with every component intact and supply available
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

// Validation rules 1-11 of spec 03 §4.2. `owner` gates technology; pass
// nullptr to skip tech checks (e.g. inspecting foreign designs).
DesignStats computeDesignStats(const Rules& r, const Empire* owner, uint32_t hull, std::span<const DesignEntry> entries);
inline DesignStats computeDesignStats(const Rules& r, const Empire* owner, const Design& d) {
    return computeDesignStats(r, owner, d.hull, d.entries);
}

// ---- Vehicle instances (damage-aware) ------------------------------------------------------

bool entryIntact(const Rules& r, const GameState& s, const Vehicle& v, size_t entry);
int entryStructure(const Rules& r, const Design& d, size_t entry);
int vehicleStructure(const Rules& r, const GameState& s, const Vehicle& v);          // max, per unit
int vehicleDamageTaken(const GameState& s, const Vehicle& v);
bool vehicleDestroyed(const Rules& r, const GameState& s, const Vehicle& v);

// Abilities from intact components + hull (+ nothing when mothballed).
std::vector<ParsedAbility> vehicleAbilities(const Rules& r, const GameState& s, const Vehicle& v);
// "Best single component" abilities (spec 03 §3.2) use bestValue1; others sum.
bool vehicleHasControl(const Rules& r, const GameState& s, const Vehicle& v);
int vehicleMaxMovement(const Rules& r, const GameState& s, const Vehicle& v);  // spec 03 §6.1
int64_t vehicleSupplyCapacity(const Rules& r, const GameState& s, const Vehicle& v);
int vehicleCargoCapacity(const Rules& r, const GameState& s, const Vehicle& v);
bool vehicleHasQuantumReactor(const Rules& r, const GameState& s, const Vehicle& v);
ruleset::VehicleType vehicleType(const Rules& r, const GameState& s, const Vehicle& v);

// Cargo space used by a cargo hold (population mass + unit hull tonnage).
int64_t cargoSpaceUsed(const Rules& r, const GameState& s, const Cargo& c);

// Starting designs: builds a plausible design for a role from the empire's
// available tech ("scout", "colony:<Surface>", "warship", "base",
// "transport", "fighter", "satellite", "mine", "troop"). Returns nullopt if
// nothing valid can be made.
std::optional<Design> autoDesign(const Rules& r, const Empire& owner, std::string_view role);

} // namespace opense4::game
