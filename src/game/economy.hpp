#pragma once

// Economy (docs/spec/02): planet output, income, trade and tariffs, storage,
// maintenance, construction queues, population growth, happiness and plague.
//
// Turn structure (spec 02 §12, spec 05 §8). The original runs a fixed list of
// end-of-turn steps for each empire in turn (turn.cpp). The economy's part of
// that list is one function per step, called for one empire at a time in
// this order:
//
//     collectIncome        income: production, remote mining, generated points,
//                          the income floor, tariffs paid (the master gets its
//                          share at once), the computer bonus; research and
//                          intelligence go into the pools
//     collectTrade         trade from partners (inside diplomacy::treatyStep)
//     payMaintenance       maintenance and the shortfall penalty
//     processPlanets       growth, planet changes, plague, atmosphere converters
//     updateHappiness      anger (skipped for an Emotionless race)
//     runConstruction      construction queues and their mode counters
//     applyStorageCap      minerals, organics and radioactives cut to the cap
//     applySystemAbilities system-wide happiness, population, plague prevention
//                          and (every 10th turn) value and conditions changes
//
// (Repair and supply sit between runConstruction and applyStorageCap.)
//
// Cross-module contracts (see also turn.hpp):
// - collectIncome takes diplomacy::tariffDue(e) on all five incomes, once,
//   before the computer bonus (spec 05 §3.3, §8), and nothing else subtracts
//   it. The master receives the resource part in the same call; the research
//   and intelligence parts are lost. What is left of research and
//   intelligence, times the bonus, goes into the pools with
//   research::addToPools, to be spent by the next turn's research and
//   intelligence steps. collectTrade adds traded points the same way.
// - collectIncome and collectTrade also set Empire::economy for this turn;
//   updateReports replaces it with the projection for the next turn at the
//   end of the turn.
// - diplomacy::tradeIncome(e) is everything e receives from trade *and*
//   tariffs; diplomacy::tariffsPaid(e) is what e pays its master.
// - Other modules report mood triggers with TurnContext::mood(); location
//   triggers must name a planet (any object) in the sector concerned. Presence
//   triggers (our/enemy ships in the sector or system, troops on the planet,
//   a plagued planet) are computed here and must not be reported by other
//   modules. updateHappiness(e) consumes e's events from TurnContext::moodEvents;
//   events reported later in the turn stay there, and processTurn carries them
//   over to e's next update (GameState::pendingMood).

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <array>
#include <string_view>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::economy {

// ---- Scales and dates -------------------------------------------------------------------------------

// The turn number the original tests for "every N turns" rules. A simultaneous
// turn advances the date before the end-of-turn steps (spec 05 §8), while
// processTurn increments GameState::turn at the very end, so during a turn this
// is turn + 1. A turn-based game advances it after the last player's
// end-of-turn processing, so there it is the turn itself.
uint32_t processingTurn(const GameState& s);

// ---- Racial effects (spec 02 §8.2) ----------------------------------------------------------------

// Each effect is (characteristic - 100) + the culture field + the Val1 of the
// race's traits of the matching type, in percentage points (confirmed: binary).
enum class RacialEffect : uint8_t {
    Reproduction,
    MineralOutput,      // Mining, culture Production, Mineral Production and Production traits
    OrganicsOutput,     // Farming, ...
    RadioactivesOutput, // Refining, ...
    Research,           // Intelligence, culture Research, Research Production
    Intelligence,       // Cunning, culture Intelligence, Intelligence Production
    ShipyardRate,       // Construction, culture SY Rate, SY Rate
    Maintenance,        // Maintenance Aptitude, culture Maintenance, Maintenance Cost
    Happiness,          // Happiness, culture Happiness, Population Happiness
    EnvironmentalResistance,  // Environmental Resistance, Tollerance
    Trade,              // Political Savvy, culture Trade, Trade
};
int racialEffect(const Rules& r, const Race& race, RacialEffect e);

// Racial points one characteristic at `value` costs, or refunds when negative
// (spec 02 §8.1, confirmed: binary): the value clamped to Min/Max Pct, then c
// per point up to the threshold, then P per point above it (N refunded per
// point below it). setup's racialPointCost sums it over the characteristics.
int characteristicPointCost(const Rules& r, Characteristic c, int value);

// ---- Modifier tables (spec 02 §1.2, §5.2) ----------------------------------------------------

struct PopulationModifier {
    int production = 100;  // % applied to planet output
    int shipyard = 100;    // % applied to the planet's construction rate
};
// The first Settings `Pop Modifier N` row, in file order, whose Population
// Amount is at least `population`; 100 % / 100 % when no row is large enough.
PopulationModifier populationModifier(const Rules& r, int64_t population);
// Settings `Mood ... Modifier`: the output percent of a mood band.
int moodOutputPercent(const Rules& r, Mood m);
// Reproduction points of a mood band: Angry -5, Unhappy -2, Indifferent 0,
// Happy +2, Jubilant +5; Rioting 0 (a rioting colony does not grow at all).
int moodReproduction(Mood m);

// ---- Planet conditions (spec 02 §2) -----------------------------------------------------------

// SpaceObject::conditions is the real number itself (conditions.hpp).
enum class ConditionsBand : uint8_t { Optimal, Good, Mild, Unpleasant, Harsh, Deadly };
ConditionsBand conditionsBand(Conditions conditions);
std::string_view conditionsName(ConditionsBand b);
// Reproduction points of a band: -20 (Deadly) to +5 (Optimal).
int conditionsReproduction(ConditionsBand b);

// ---- Mood (spec 02 §4) ------------------------------------------------------------------------

bool emotionless(const Rules& r, const GameState& s, EmpireId e);
// The mood a colony shows: its band, or "Emotionless" for such a race.
std::string_view moodName(const Rules& r, const GameState& s, const Colony& c);
// Sets a colony's anger, clamped to 0..Colony::maxAnger(). For an Emotionless
// race any change sets 35 (Indifferent) instead.
void setAnger(const Rules& r, const GameState& s, Colony& c, int anger);

// ---- Planet output (spec 02 §5) ---------------------------------------------------------------

// Resupply depots whose ability has no amount resupply without limit.
inline constexpr int64_t kUnlimitedSupply = 1'000'000'000;
// Every treasury and point pool is limited to this (spec 02 §5.6).
inline constexpr int64_t kTreasuryLimit = 2'000'000'000;

// What one colony produces this turn (spec 02 §5.1), before the system
// modifier and the spaceport rule, which work on each system's total (§5.5).
struct ColonyOutput {
    Resources production;            // steps 1-5, solar included; 0 while rioting, blockaded or empty
    int64_t research = 0;
    int64_t intelligence = 0;
    int64_t supply = 0;              // resupply depot output (0 = none)
    bool connected = true;           // spaceport rule satisfied in the system (spec 02 §5.5)
    bool blockaded = false;
    int reproductionPercent = 0;     // per year (spec 02 §3)
    Mood mood = Mood::Indifferent;

    // Details for reports and the UI.
    int deliveryPercent = 100;       // share of the system's output that reaches the treasury (0, the home share or 100)
    std::array<int, 3> productionPercent{100, 100, 100};  // step 4 percentage per resource
    int researchPercent = 100;
    int intelligencePercent = 100;
    Resources solar;                 // part of `production` from solar generation
    Resources depletion;             // finite games: stock this output draws
    int facilitiesOperating = 0;     // facilities that generate resources or points
};
ColonyOutput colonyOutput(const Rules& r, const GameState& s, const Colony& c);

// What an empire's colonies deliver this turn, system by system (spec 02 §5.5).
struct Production {
    Resources resources;      // delivered to the treasury
    int64_t research = 0;
    int64_t intelligence = 0;
    Resources undelivered;    // lost to the spaceport rule
    Resources depletion;      // finite games: stock drawn from the planets
};
Production empireProduction(const Rules& r, const GameState& s, EmpireId e);

// Spaceport rule: output of `sys` reaches the empire (a Spaceport facility on
// any of its colonies there, or the No Spaceports trait).
bool spaceportInSystem(const Rules& r, const GameState& s, EmpireId e, SystemId sys);
// A visible, unmothballed ship, base or planet of an empire below
// Non-Aggression in the colony's sector (spec 02 §2).
bool colonyBlockaded(const Rules& r, const GameState& s, const Colony& c);
// Growth rate in % per year for the whole colony (spec 02 §3).
int reproductionPercent(const Rules& r, const GameState& s, const Colony& c);

// ---- Construction (spec 02 §6) ------------------------------------------------------------------

// Per-turn construction rate of a queue (spec 02 §6.2), with the computer
// bonus and emergency or slow mode.
Resources constructionRate(const Rules& r, const GameState& s, EmpireId e, const cmd::QueueTarget& t);
// Full cost of a queue item (vehicle design cost × count, facility, upgrade).
Resources itemCost(const Rules& r, const GameState& s, EmpireId e, const cmd::QueueTarget& t, const QueueItem& item);
// Turns to finish `remaining` at `rate` (max over resources, ceil); -1 = never.
int turnsToComplete(const Resources& remaining, const Resources& rate);
// Facilities of `facility`'s family on a colony below the newest level the empire has.
int upgradeableCount(const Rules& r, const GameState& s, EmpireId e, const Colony& c, uint32_t facility);

// ---- Treasury (spec 02 §5.6, §7) ------------------------------------------------------------------

Resources storageCapacity(const Rules& r, const GameState& s, EmpireId e);
Resources maintenanceCost(const Rules& r, const GameState& s, EmpireId e);
// Maintenance rate in % of cost per turn (spec 02 §7), at least 5.
int maintenancePercent(const Rules& r, const Empire& e);
// One ship's or base's maintenance per turn; 0 when mothballed and for units.
Resources vehicleMaintenance(const Rules& r, const GameState& s, const Vehicle& v);

// ---- Colonies ending ------------------------------------------------------------------------------

// A colony whose population died out is removed: each planet value drops by
// `Planet Value Percent Loss After Owner Death`, and the owner gets `Homeworld
// Lost` (first default colony type) or `Any Planet Lost` (spec 02 §2). The
// owner's history record notes the loss, with `cause` when given.
void colonyDiesOut(TurnContext& ctx, ObjectId planet, std::string_view cause = {});
// A planet that rebels founds a new computer empire and becomes its capital
// (spec 02 §4): effects::breakAway (events.hpp) does it.

// ---- Per-empire end-of-turn steps (see the top of this file) -----------------------------------------

void collectIncome(TurnContext& ctx, EmpireId e);
void collectTrade(TurnContext& ctx, EmpireId e);
void payMaintenance(TurnContext& ctx, EmpireId e);
void processPlanets(TurnContext& ctx, EmpireId e);
void updateHappiness(TurnContext& ctx, EmpireId e);
void runConstruction(TurnContext& ctx, EmpireId e);
void applyStorageCap(TurnContext& ctx, EmpireId e);
void applySystemAbilities(TurnContext& ctx, EmpireId e);

// ---- Reports ---------------------------------------------------------------------------------------

// Recomputes Empire::economy (projected income/expenses) without changing
// anything else. Called after setup and at the end of each turn for the UI.
void updateReports(const Rules& r, GameState& s);

} // namespace opense4::game::economy
