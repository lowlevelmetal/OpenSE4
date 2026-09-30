#pragma once

// Economy (docs/spec/02): planet output, trade income, storage, maintenance,
// construction queues, population growth, mood, riots and plague.
//
// Cross-module contracts (see also turn.hpp):
// - Phase 5 (runEconomy) sets Empire::economy, including `research` and
//   `intelligence`, which the research and intelligence phases spend right
//   after it.
// - diplomacy::tradeIncome(e) is everything e receives from trade *and*
//   tariffs; diplomacy::tariffsPaid(e) is what e pays its master.
// - Other modules report mood triggers with TurnContext::mood(); location
//   triggers must name a planet (any object) in the sector concerned. Presence
//   triggers (our/enemy ships in the sector or system, our troops on the
//   planet) are computed here and must not be reported by other modules.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <array>
#include <string_view>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::economy {

// ---- Modifier tables (spec 02 §1.2, §5.2) ----------------------------------------------------

struct PopulationModifier {
    int production = 100;  // % applied to planet output
    int shipyard = 100;    // % applied to the planet's construction rate
};
// The Settings `Pop Modifier N` row that covers a colony of `population` M:
// the first row whose Population Amount is at least the population.
PopulationModifier populationModifier(const Rules& r, int64_t population);
// Settings `Mood ... Modifier`: the output percent of a mood band.
int moodOutputPercent(const Rules& r, Mood m);
// Reproduction shift of a mood band, -5 when rioting to +5 when jubilant.
int moodReproduction(Mood m);

// ---- Planet conditions (spec 02 §2) -----------------------------------------------------------

enum class ConditionsBand : uint8_t { Pleasant, Mild, Unpleasant, Harsh, Deadly };
ConditionsBand conditionsBand(int conditions);
std::string_view conditionsName(ConditionsBand b);
// Reproduction points lost to the conditions, softened by Environmental Resistance.
int conditionsReproductionPenalty(ConditionsBand b, int environmentalResistance);
// Anger (tenths of a percent) the conditions add each turn.
int conditionsAnger(ConditionsBand b, int environmentalResistance);

// ---- Planet output (spec 02 §5, spec 05 §1.1 and §2.1) -----------------------------------------

// Resupply depots whose ability has no amount resupply without limit.
inline constexpr int64_t kUnlimitedSupply = 1'000'000'000;

// What a colony produces this turn, before delivery (spec 02 §5).
struct ColonyOutput {
    Resources production;            // incl. solar generation; 0 while rioting
    int64_t research = 0;
    int64_t intelligence = 0;
    int64_t supply = 0;              // resupply depot output (0 = none)
    bool connected = true;           // spaceport rule satisfied (spec 02 §5.5)
    bool blockaded = false;
    int reproductionPercent = 0;     // per year, for the owner's race
    Mood mood = Mood::Indifferent;

    // Details for reports and the UI.
    int deliveryPercent = 100;       // share of the output that reaches the treasury
    std::array<int, 3> productionPercent{100, 100, 100};  // combined modifier per resource
    int researchPercent = 100;
    int intelligencePercent = 100;
    Resources solar;                 // part of `production` from solar generation
    Resources depletion;             // finite games: stock used up this turn
    int facilitiesOperating = 0;     // facilities that generate resources or points

    Resources delivered() const { return production.percent(deliveryPercent); }
    int64_t deliveredResearch() const { return research * deliveryPercent / 100; }
    int64_t deliveredIntelligence() const { return intelligence * deliveryPercent / 100; }
};
ColonyOutput colonyOutput(const Rules& r, const GameState& s, const Colony& c);

// Spaceport rule: output of `sys` reaches the empire (a working Spaceport
// there, or the No Spaceports trait).
bool spaceportInSystem(const Rules& r, const GameState& s, EmpireId e, SystemId sys);
// Enemy vehicles in orbit that are neither mothballed nor cloaked (spec 02 §2).
bool colonyBlockaded(const Rules& r, const GameState& s, const Colony& c);
// Reproduction % per year of one race's population on a colony (spec 02 §3).
int reproductionPercent(const Rules& r, const GameState& s, const Colony& c, EmpireId race);
// Highest plague level the colony is protected against (Plague Prevention in
// the system, or the No Plagues trait: effectively unlimited).
int plagueProtection(const Rules& r, const GameState& s, const Colony& c);

// ---- Construction (spec 02 §6) ------------------------------------------------------------------

// Per-turn construction rate of a queue (spec 02 §6.2), with emergency or slow mode.
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
// One vehicle's (or unit group's) maintenance per turn; 0 when mothballed.
Resources vehicleMaintenance(const Rules& r, const GameState& s, const Vehicle& v);
// Research points added once, on the first processed turn (spec 05 §1.1).
int64_t openingResearchPool(const GameState& s);

// Recomputes Empire::economy (projected income/expenses) without changing
// anything else. Called after setup and at the end of each turn for the UI.
void updateReports(const Rules& r, GameState& s);

// Turn phase 5: production, trade, tariffs, maintenance, then queue spending.
void runEconomy(TurnContext& ctx);
// Turn phase 8: growth, mood (applies TurnContext::moodEvents), riots, rebellion, plague.
void runPopulation(TurnContext& ctx);

} // namespace opense4::game::economy
