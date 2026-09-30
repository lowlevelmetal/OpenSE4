#pragma once

// Space and ground combat (docs/spec/04). Strategic (auto-resolved) combat
// with a recorded replay in GameState::combats. Tactical combat, the same
// battle stepped with a player's orders, is in tactical.hpp; the combat
// simulator in simulator.hpp.
//
// Cross-module contracts
// ----------------------
// * Movement calls combatPossible() for a sector where a vehicle moved and,
//   when it is true, resolveSpaceCombat() for that sector (at most once per
//   sector per movement phase, spec 05 §9.3). resolveSpaceCombat() first lets
//   hostile mines strike the group of vehicles that entered the sector (after
//   that group's sweepers clear what they can, spec 04 §10.6), then fights the
//   battle if two hostile empires present can see each other. The three-argument
//   form names the vehicles that just entered (movement passes the day's
//   steps; empty = nobody entered, no mine strike), struck group by group: a
//   fleet together, any other vehicle alone. The two-argument form takes the
//   vehicles that moved into the sector this turn (Vehicle::cameFrom), one
//   group per empire, or every vehicle there when none is marked (inferred
//   fallback).
//   combatPossible() is also true when only mines face a vehicle they can
//   hurt, so the same call pair handles "a vehicle enters a mined sector".
//   Destroyed vehicles get count = 0; the caller runs removeDeadVehicles().
//   Combat does not clear orders (spec 03 §6.3): only ships that change owner
//   lose theirs. Removing a leading Sentry order is movement's job. The
//   battle's sector is appended to TurnContext::battleSites. With
//   TurnContext::battles set (turn-based games with tactical combat, turn.hpp)
//   a battle with human sides takes the next answer, or raises the question.
//
// * Attackers and start boxes (spec 04 §3). A vehicle that moved into the
//   battle sector this turn has Vehicle::cameFrom set to the sector it left
//   and cameFromTurn == GameState::turn; movement records both on each step.
//   Every other vehicle, and every planet, was already in the sector.
//
// * Troops on the ground (spec 04 §11, §13). Units in cargo have no owner
//   of their own: a ship drops every troop unit aboard, of whatever design,
//   for the empire that owns the ship, and the units stored on a planet
//   always serve the planet's owner. Landed troops are kept apart, in
//   Colony::landedTroops, and fight for Colony::invader (one invader at a
//   time: a third empire may not land where another's troops fight). Ground
//   combat between turns (runGroundCombat) runs in the colony owner's
//   end-of-turn processing, at its ground-combat step (spec 05 §8): each of
//   its colonies with landed troops fights on, unless the owner is the
//   landed empire or at Non-Aggression or better with it, in which case the
//   troops join the colony's cargo and the invasion ends. To land troops
//   outside space combat (a Drop Cargo order onto an enemy planet), movement
//   calls landTroops(). During a space battle, ships land their troops the
//   same way and the ground combat is fought at once. Colony::militia holds
//   the militia pool of an invaded colony (-1 when nobody invades it).
//
// The replay record (GameState::combats)
// --------------------------------------
// CombatRecord::pieces lists every piece in creation order: planets and
// vehicles at setup, neutral obstacles (stars, warp points, comets,
// uncolonised planets; owner invalid, `planet` = the object), then launched
// unit groups and seekers as they appear. startX/startY are the top-left
// square on the kCombatMapWidth x kCombatMapHeight map; planets and obstacles
// cover 4x4 squares. A seeker piece carries its launcher's design and the
// weapon name. Events, in order, all with the combat turn (`round`, 1-based)
// and the acting piece's top-left square (x, y) at that moment:
//   Move       piece moved to (x, y): one square per event, except that a
//              Random Target Movement hit jumps (seekers and forced moves too)
//   Fire       piece fired at target; component = component index, amount =
//              design entry index (rams and boarding attempts: 0)
//   Hit        piece (a shooter, seeker or rammer) hit target; amount = damage
//              of the hit after the system damage modifier, before shields
//   Miss       piece missed target
//   Destroyed  piece destroyed; target = the piece that did it (itself when a
//              seeker expires). A planet whose colony dies stays on the map as
//              an unowned obstacle.
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
    int fighterGroup = 20;     // loaded, never used (spec 04 §10.4)
    int mineGroup = 20;        // loaded, never used
    int satelliteGroup = 20;   // loaded, never used
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

// Largest damage one weapon deals per shot (history 1.14); applied to the table value with the mount.
inline constexpr int kMaxShotDamage = 50000;
// Crew and fleet experience are capped here (confirmed: binary, spec 04 §15).
inline constexpr int kMaxCombatExperience = 50;
// The combat map (confirmed: binary, spec 04 §1): columns 0..71, rows 0..62.
inline constexpr int kCombatMapWidth = 72;
inline constexpr int kCombatMapHeight = 63;
// Planets and neutral obstacles cover this many squares on each side.
inline constexpr int kBigPieceSize = 4;

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
bool isSpecialEffect(DamageType t);      // reload, conversion, push/pull/random: act before shields
// The types that use and feed the damage pool (spec 04 §9.1 step 2).
bool isHullDamaging(DamageType t);

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
    // "Drones Per Target" (default 3): set in the strategies window and stored
    // with the game, not a DefaultStrategies.txt key (spec 04 §10.7). Read from
    // the empire's strategy record when it holds that key.
    int dronesPerTarget = 3;
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

// True when hostile empires that can see each other are present in the
// sector, or hostile mines there can strike an entering vehicle (spec 04 §2, §10.6).
bool combatPossible(const Rules& r, const GameState& s, Location where);
// Mines first, then the battle: appends a CombatRecord, applies damage,
// destruction, capture, experience, design statistics, mood events and logs.
void resolveSpaceCombat(TurnContext& ctx, Location where);
// The same with the vehicles that just moved in (the mines' victims; none when empty).
void resolveSpaceCombat(TurnContext& ctx, Location where, std::span<const VehicleId> entering);
// The ground-combat step of `owner`'s end-of-turn processing (spec 05 §8,
// spec 04 §13): on each of its colonies where landed troops still fight, the
// fight goes on, or, when the owner is the landed empire or at Non-Aggression
// or better with it, the troops join the colony's cargo and the invasion
// ends. A colony the invaders take changes owner; a colony that no longer
// holds invaders loses its militia pool.
void runGroundCombat(TurnContext& ctx, EmpireId owner);

// ---- Queries and helpers ------------------------------------------------------------------------

// Chance (percent) that a weapon of `attacker` hits `defender` whose top-left
// square is `distance` squares away (spec 04 §7). Seekers and warheads never
// roll: 100. Non-weapons: 0.
int toHitPercent(const Rules& r, const GameState& s, const Vehicle& attacker, size_t weaponEntry, const Vehicle& defender, int distance);

// Damage of a design entry's weapon at `range` squares (spec 04 §8): the
// table entry (index shifted by a valid mount's range modifier and clamped to
// 1..20), times the mount's damage percent (rounded), capped at kMaxShotDamage.
int weaponDamage(const Rules& r, const DesignEntry& e, int range);
// Warheads and troop weapons: the largest table entry, with the mount (spec 04 §8).
int weaponLargestDamage(const Rules& r, const DesignEntry& e);
// Longest range at which weaponDamage() is above 0 (0 = none; 20 when a mount clamps it).
int weaponReach(const Rules& r, const DesignEntry& e);

// The empire whose landed troops fight on this colony (Colony::invader, while
// any are left); empty when nobody invades it.
std::vector<EmpireId> invaders(const Rules& r, const GameState& s, const Colony& c);
bool isTroopDesign(const Rules& r, const GameState& s, DesignId d);
// Moves up to `count` troop units of `design` from a carrier onto a hostile
// colony in the carrier's sector (spec 04 §11 Drop Troops): they land for the
// carrier's owner. Refused when the colony is not hostile to it or is already
// contested by another invader (spec 04 §13). The first landing gives the
// colony its militia pool. Returns the number landed.
int landTroops(const Rules& r, GameState& s, VehicleId carrier, ObjectId planet, DesignId design, int count);

// Militia raised by one population group of `populationMillions` (spec 04 §13):
// one per `Defending Units Per Population` million, truncated, no minimum.
int militiaCount(const CombatSettings& cs, int64_t populationMillions);
// The militia of a whole colony: the sum over its population groups.
int militiaCount(const CombatSettings& cs, std::span<const PopulationGroup> population);

} // namespace opense4::game::combat
