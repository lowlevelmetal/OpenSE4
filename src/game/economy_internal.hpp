#pragma once

// Helpers shared by the economy translation units (economy*.cpp). Not part of
// the public engine interface.

#include "game/economy.hpp"
#include "game/query.hpp"

#include <span>
#include <vector>

namespace opense4::game::economy::detail {

// Facilities only work on a colony that has population (query.hpp).
inline bool facilitiesWork(const Colony& c) { return c.totalPopulation() > 0; }

// Abilities of the planet itself plus its working facilities.
std::vector<ParsedAbility> workingAbilities(const Rules& r, const GameState& s, const Colony& c);

// Colonies of `owner` in a system, in object order (stable).
std::vector<const Colony*> coloniesInSystem(const GameState& s, EmpireId owner, SystemId sys);

// Best (highest) Val 1 of an ability over the owner's working colonies in a
// system ("only one effective per system"). `found` tells absence from 0.
int64_t bestInSystem(const Rules& r, const GameState& s, EmpireId owner, SystemId sys, AbilityKind k, bool* found = nullptr);

// Same, over a single ability list.
int64_t bestOf(std::span<const ParsedAbility> list, AbilityKind k, bool* found = nullptr);

// Design cost (hull + mounted components) without validation work.
Resources designCost(const Rules& r, const Design& d);

// Ships and bases appear in space when built; everything else is a unit that
// goes into cargo (spec 03 §1).
bool isShipOrBase(ruleset::VehicleType t);

// The system holding the empire's homeworld colony, if it still owns one.
SystemId homeSystem(const GameState& s, EmpireId e);

// Number of stars in a system (solar generation).
int starCount(const GameState& s, SystemId sys);

// Percentage points a race characteristic adds to a modifier sum.
inline int charBonus(const Race& race, Characteristic c) { return race.characteristic(c) - 100; }

// Resource index helpers.
inline size_t idx(Resource res) { return static_cast<size_t>(res); }

// A queue owned by an empire, identified the same way as commands do.
struct QueueRef {
    cmd::QueueTarget target;
    Location location;
};

// Projected per-turn usage of a queue given the treasury left (no mutation).
Resources projectQueueUsage(const Rules& r, const GameState& s, EmpireId e, const QueueRef& q, const ConstructionQueue& queue,
                            Resources& treasury);

// Every construction queue of an empire in processing order: colonies by
// planet id, then space-yard vehicles by vehicle id.
std::vector<QueueRef> empireQueues(const GameState& s, EmpireId e);

// Runs one turn of construction for every queue of an empire; returns what
// was spent. Advances nothing else (mode counters: advanceQueueModes).
Resources runConstruction(TurnContext& ctx, EmpireId e);

// Emergency / slow build turn counters (spec 02 §6.4), for every queue.
void advanceQueueModes(const Rules& r, GameState& s);

} // namespace opense4::game::economy::detail
