#pragma once

// Internals shared by the combat sources (combat.cpp, combat_space.cpp,
// combat_ground.cpp) and the combat unit tests. Not part of the engine API.

#include "core/rng.hpp"
#include "game/combat.hpp"
#include "game/design.hpp"

#include <optional>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::combat::detail {

bool enemies(const GameState& s, EmpireId a, EmpireId b);   // hostile in either direction
// Mines, troops and weapon platforms are never combat pieces.
bool canBePiece(ruleset::VehicleType t);
// Whether `viewer`, which has pieces in the vehicle's sector, sees it:
// uncloaked vehicles sharing a sector are seen unless a storm or nebula
// obscures it; otherwise the sight module decides (spec 04 §2).
bool visibleTo(const Rules& r, const GameState& s, EmpireId viewer, const Vehicle& v);

// Who fights in a sector (spec 04 §2): a vehicle takes part when a hostile
// empire present there can see it (undetected cloaked vehicles sit out); an
// empire takes part when it has such vehicles or a colony there and so does
// an empire it is hostile to. Sorted by id.
struct Forces {
    std::vector<EmpireId> empires;
    std::vector<VehicleId> vehicles;
    std::vector<ObjectId> colonies;
};
Forces battleForces(const Rules& r, const GameState& s, Location where);

// ---- Component and hull ability lookups ----------------------------------------------------------

int64_t componentSum(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k);   // intact components
int64_t componentBest(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k);  // best single intact component
bool hasIntactComponent(const Rules& r, const GameState& s, const Vehicle& v, AbilityKind k);
bool designHasComponent(const Rules& r, const Design& d, AbilityKind k);                   // destroyed or not
int64_t hullSum(const Rules& r, const Design& d, AbilityKind k);
bool vehicleArmed(const Rules& r, const GameState& s, const Vehicle& v);                   // any intact weapon
// Designs without any supply storage never run dry (inferred), nor do vehicles with a Quantum Reactor.
bool needsSupply(const Rules& r, const GameState& s, const Vehicle& v);
int supplyPerShot(const Rules& r, const GameState& s, const Vehicle& v, const DesignEntry& e);
int remainingStructure(const Rules& r, const GameState& s, const Vehicle& v);   // one unit
ruleset::VehicleType typeOf(const Rules& r, const GameState& s, const Vehicle& v);

// ---- To-hit (spec 04 §7) -------------------------------------------------------------------------

int clampChance(int chance);   // 1..99
int toHitChance(const CombatSettings& cs, int distance, int offense, int defense, int interference);
// Racial modifiers of an empire: culture Space Combat + (characteristic - 100).
int racialOffense(const Rules& r, const Empire& e);
int racialDefense(const Rules& r, const Empire& e);
// Best `Combat Modifier - System` / `Damage Modifier - System` / `Shield
// Modifier - System` among the empire's populated colonies in the system.
int systemModifier(const Rules& r, const GameState& s, EmpireId e, SystemId sys, AbilityKind k);
// `Sensor Interference` and `Shield Disruption` at a sector (its objects and the system).
int sensorInterference(const GameState& s, Location where);
int shieldDisruption(const GameState& s, Location where);
// Vehicle offense/defense before the weapon's own modifier, the system bonus,
// and target-kind modifiers: best component + hull + experience + racial.
int vehicleOffense(const Rules& r, const GameState& s, const Vehicle& v, bool unitGroup, int crewExperience);
int vehicleDefense(const Rules& r, const GameState& s, const Vehicle& v, bool unitGroup, int crewExperience);
int fleetExperience(const GameState& s, const Vehicle& v);

// ---- Damage (spec 04 §9) --------------------------------------------------------------------------

enum class Layer : uint8_t {
    Armor, Internal, Engines, Weapons, ShieldGenerators, MasterComputers, BoardingParties, SecurityStations, PlanetDestroyers
};

struct DamageRule {
    enum class Shields : uint8_t { Both, PhasedOnly, None };
    Shields shields = Shields::Both;
    int shieldNum = 1, shieldDen = 1;   // damage counts × num/den against shields
    bool shieldsOnly = false;           // the remainder is discarded
    bool skipsArmor = false;            // internals only
    std::optional<Layer> only;          // restricted to one kind of component
    bool structural = true;             // false: special effect, no structure damage
};
DamageRule damageRule(DamageType t);

struct ShieldState {
    int normal = 0, phased = 0;
    int maxNormal = 0, maxPhased = 0;
    int bonus = 0;   // system shield modifier minus shield disruption
};
// Recomputes the maxima from intact generators; `fill` sets current to max,
// otherwise current is capped at the new max (inferred, spec 04 §9.2).
void refreshShields(const Rules& r, const GameState& s, const Vehicle& v, ShieldState& sh, bool fill);
// Drains shields per the rule; returns the damage left over.
int absorbShields(ShieldState& sh, int damage, const DamageRule& rule);

// Applies `amount` damage to random intact components of the layer; returns the damage used.
int damageLayer(const Rules& r, const GameState& s, Vehicle& v, Layer layer, int amount, Rng& rng);

struct HitOutcome {
    int shieldDamage = 0;
    int structureDamage = 0;
    int excess = 0;          // left over after the unit was destroyed
    bool destroyed = false;
};
// One structural hit on one unit (ship, base, or the front member of a unit group).
HitOutcome hitUnit(const Rules& r, const GameState& s, Vehicle& unit, ShieldState& sh, int damage, DamageType type, Rng& rng);
// Whether a hit of this type can change anything on this vehicle (mines and targeting skip it otherwise).
bool canAffectVehicle(const Rules& r, const GameState& s, const Vehicle& v, const ShieldState& sh, DamageType type);

// Fully restores components with Armor Regeneration (after a battle, history 1.79).
void restoreRegeneratingArmor(const Rules& r, const GameState& s, Vehicle& v);

// ---- Mines (spec 04 §10.6) -------------------------------------------------------------------------

bool minesCanStrike(const Rules& r, const GameState& s, Location where);
void resolveMines(TurnContext& ctx, Location where, Rng& rng);

// ---- Record helpers -----------------------------------------------------------------------------------

std::string sectorName(const GameState& s, Location where);

} // namespace opense4::game::combat::detail
