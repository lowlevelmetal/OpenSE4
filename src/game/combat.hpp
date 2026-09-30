#pragma once

// Space and ground combat (docs/spec/04). Strategic (auto-resolved) combat
// with a recorded replay in GameState::combats.
//
// Cross-module contracts
// ----------------------
// * Movement calls combatPossible() for a sector where a vehicle moved and,
//   when it is true, resolveSpaceCombat() for that sector (at most once per
//   sector per movement phase, spec 05 §9.3). resolveSpaceCombat() first lets
//   hostile mines in the sector strike every vehicle there that they are
//   hostile to (after the victims' sweepers clear what they can), then fights
//   the battle if two hostile sides still see each other. combatPossible() is
//   also true when only mines face a vehicle they can hurt, so the same call
//   pair handles "a vehicle enters a mined sector" (spec 04 §10.6).
//   Destroyed vehicles get count = 0; the caller runs removeDeadVehicles().
//   Every vehicle that fought loses its orders (spec 04 §2); its sector is
//   appended to TurnContext::battleSites.
//
// * Troops on the ground (spec 04 §11, §13). Invading troops are stored as
//   ordinary UnitStack entries in the target Colony::cargo.units. A troop
//   stack belongs to the empire that owns its design (Design::owner); a troop
//   stack whose design owner is hostile to the colony owner is an invader.
//   Ground combat (runGroundCombat, turn phase 4) fights every colony that
//   holds invaders. To land troops outside space combat (e.g. a Drop Cargo
//   order onto an enemy planet), movement calls landTroops(), which moves the
//   stack from the carrier's cargo into the colony's cargo. During a space
//   battle, ships with a Drop Troops strategy land their troops the same way.
//   Known limitation: a troop unit captured in a ship's cargo keeps the
//   allegiance of its design owner.
//
// The replay record (GameState::combats)
// --------------------------------------
// CombatRecord::pieces lists every piece in creation order: planets and
// vehicles at setup (startX/startY on a kCombatGridSize square grid; a planet
// covers 2x2 squares from its start), then launched unit groups and seekers
// as they appear. A seeker piece carries its launcher's design and the weapon
// name. Events, in order, all with the combat turn (`round`, 1-based) and the
// acting piece's square (x, y) at that moment:
//   Move       piece moved to (x, y): one square per event, except that a
//              Random Target Movement hit jumps (seekers and forced moves too)
//   Fire       piece fired at target; component = component index, amount =
//              design entry index (rams and boarding attempts: 0)
//   Hit        piece (a shooter, seeker or rammer) hit target; amount = damage
//              dealt before shields, summed over a group's members
//   Miss       piece missed target
//   Destroyed  piece destroyed; target = the piece that did it (itself when a
//              seeker expires)
//   Captured   piece changed owner; target = the capturer; amount = new owner id
//   Launch     piece = new unit group, target = its carrier, amount = units;
//              or piece = a troop ship, target = the planet, amount = troops landed
//   Seeker     piece = new seeker, target = its target, amount = members, component
// With Settings `Create Combat Replay` off, events are left out (pieces and
// the summary lines remain).

#include "game/rules.hpp"
#include "game/state.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::combat {

// ---- Settings.txt values used by combat (spec 04 §18.3) -----------------------------------

struct CombatSettings {
    int spaceTurns = 30;
    int groundTurns = 10;
    int baseToHit = 100;
    int toHitPerSquare = 10;
    int seekerDefense = 40;
    int planetOffense = 30;
    int planetDefense = -200;
    int ramSourcePercent = 60;
    int ramTargetPercent = 100;
    int capturedReload = 10;
    int fighterGroup = 20;
    int mineGroup = 20;
    int satelliteGroup = 20;
    bool fightersHitByMines = true;
    bool dronesHitByMines = true;
    int defendingUnitsPerPopulation = 20;
    int militiaAttack = 10;
    int militiaHitPoints = 30;
    int groundDamagePercent = 30;
    int damagePerPopulation = 10;
    bool createReplay = true;
};
CombatSettings loadSettings(const Rules& r);

// Largest damage one weapon deals per shot (history 1.14).
inline constexpr int kMaxShotDamage = 50000;
// Combat experience (percentage points of to-hit) is capped here (inferred).
inline constexpr int kMaxCombatExperience = 50;
// The combat map is a square grid of this size (inferred, spec 04 §19 Q1).
inline constexpr int kCombatGridSize = 32;

// ---- Weapon damage types (spec 04 §9.5; every identifier the data format allows) ----------

enum class DamageType : uint8_t {
    Normal,
    ShieldsOnly,
    SkipsNormalShields,
    SkipsAllShields,
    SkipsArmor,
    SkipsShieldsAndArmor,
    QuadDamageToShields,
    DoubleDamageToShields,
    HalfDamageToShields,
    QuarterDamageToShields,
    OnlyEngines,
    OnlyWeapons,
    OnlyShieldGenerators,
    OnlyMasterComputers,
    OnlyBoardingParties,
    OnlySecurityStations,
    OnlyPlanetDestroyers,
    IncreaseReloadTime,
    DisruptReloadTime,
    CrewConversion,
    PushesTarget,
    PullsTarget,
    RandomTargetMovement,
    PlagueLevel1,
    PlagueLevel2,
    PlagueLevel3,
    PlagueLevel4,
    PlagueLevel5,
    OnlyPlanetPopulation,
    OnlyPlanetConditions,
    OnlyResupplyDepots,
    OnlySpaceports,
    Count
};
std::string_view identifier(DamageType t);
// Unknown identifiers (mods) read as Normal.
DamageType parseDamageType(std::string_view text);
bool isPlanetOnlyDamage(DamageType t);   // plague, population, conditions, facility killers
bool isSpecialEffect(DamageType t);      // reload, conversion, push/pull/random: no structure damage

// ---- Weapon target sets (spec 04 §18.1) ----------------------------------------------------

enum TargetMask : uint8_t {
    kTargetShips = 1,       // ships and bases
    kTargetPlanets = 2,
    kTargetFighters = 4,
    kTargetSatellites = 8,
    kTargetSeekers = 16,
    kTargetDrones = 32,
};
// Parses `Weapon Target` ("Ships\Planets\Ftr") or a list override ("Ships, Fighters").
uint8_t parseWeaponTargets(std::span<const std::string> targets);
// The target-set bit of a vehicle of this type (0 for mines, troops and platforms).
uint8_t targetMaskOf(ruleset::VehicleType t);

// ---- Combat strategies (DefaultStrategies.txt, spec 04 §16) ---------------------------------

enum class MoveStrategy : uint8_t {
    DontGetHurt, DropTroops, MaximumRange, OptimalRange, ShortRange, PointBlank, BoardEnemyShips, Ram
};
enum class TargetKey : uint8_t {
    None, Nearest, Farthest, Largest, Smallest, MostDamaged, LeastDamaged, Fastest, Slowest, Strongest, Weakest,
    HasWeapons, NoWeapons
};
// The 14 categories of "Type Priority", "Dont Fire On" and "Break Formation".
enum class TargetCategory : uint8_t {
    Planets, Fighters, SeekersOnUs, SeekersOnOthers, Mines, Carriers, ColonyShips, Transports,
    BasesNoWeapons, ShipsNoWeapons, Bases, Ships, Satellites, Drones, Count
};
inline constexpr size_t kTargetCategories = static_cast<size_t>(TargetCategory::Count);
std::string_view identifier(TargetCategory c);   // as used in the strategy keys, e.g. "Seekers(On Us)"
std::string_view identifier(MoveStrategy m);

struct Strategy {
    std::string name;
    MoveStrategy primary = MoveStrategy::OptimalRange;
    MoveStrategy secondary = MoveStrategy::PointBlank;
    std::array<TargetKey, 4> targeting{TargetKey::HasWeapons, TargetKey::Nearest, TargetKey::None, TargetKey::None};
    bool typePriorityFirst = false;
    std::array<int, kTargetCategories> typePriority{};   // 1 = engage first
    std::array<bool, kTargetCategories> dontFireOn{};
    std::array<bool, kTargetCategories> breakFormation{};
    int fighterLaunchGroup = 10;
    int damagePercentShip = 100;
    int damagePercentPlanet = 100;
    int damagePercentFighters = 100;
    int damagePercentSatellites = 100;
    bool damageUntilWeaponsGone = false;

    Strategy();   // neutral defaults for keys a record leaves out
};
Strategy parseStrategy(const ruleset::CombatStrategy& s);
// An empire's strategy by index (its first one, or defaults, when out of range).
Strategy empireStrategy(const GameState& s, EmpireId e, uint32_t index);

// ---- Turn phases -----------------------------------------------------------------------------

// True when hostile empires that can see each other have combat pieces in the
// sector, or hostile mines there can strike a vehicle (spec 04 §2, §10.6).
bool combatPossible(const Rules& r, const GameState& s, Location where);
// Mines first, then the battle: appends a CombatRecord, applies damage,
// destruction, capture, experience, design statistics, mood events and logs.
void resolveSpaceCombat(TurnContext& ctx, Location where);
// Turn phase 4: invading troops against planets; capture of planets.
void runGroundCombat(TurnContext& ctx);

// ---- Queries and helpers ------------------------------------------------------------------------

// Chance (percent) that a weapon of `attacker` hits `defender` at `range`
// (spec 04 §7). Seekers and warheads never roll: 100. Non-weapons: 0.
int toHitPercent(const Rules& r, const GameState& s, const Vehicle& attacker, size_t weaponEntry, const Vehicle& defender, int range);

// Empires whose troops are invading this colony (hostile troop stacks in its cargo), by id.
std::vector<EmpireId> invaders(const Rules& r, const GameState& s, const Colony& c);
bool isTroopDesign(const Rules& r, const GameState& s, DesignId d);
// Moves up to `count` troop units of `design` from a carrier onto a hostile
// colony in the carrier's sector (spec 04 §11 Drop Troops). Refused when the
// colony is not hostile or is already contested by another invader (spec 04
// §13). Returns the number landed.
int landTroops(const Rules& r, GameState& s, VehicleId carrier, ObjectId planet, DesignId design, int count);

// Militia raised from a population (spec 04 §13, inferred: one per
// `Defending Units Per Population` M, at least one while anyone lives there).
int militiaCount(const CombatSettings& cs, int64_t populationMillions);

} // namespace opense4::game::combat
