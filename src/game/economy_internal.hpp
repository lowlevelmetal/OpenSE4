#pragma once

// Helpers shared by the economy translation units (economy*.cpp). Not part of
// the public engine interface.

#include "game/economy.hpp"
#include "game/query.hpp"
#include "game/xmath.hpp"

#include <span>
#include <vector>

namespace opense4::game::economy::detail {

// Abilities of the planet itself plus its facilities. Facility abilities do
// not depend on population (spec 02 §1.5, confirmed: binary); only output,
// growth and building do.
std::vector<ParsedAbility> workingAbilities(const Rules& r, const GameState& s, const Colony& c);

// Colonies of `owner` in a system, in object order (stable).
std::vector<const Colony*> coloniesInSystem(const GameState& s, EmpireId owner, SystemId sys);

// "Best" stacking (spec 02 §1.5): the largest Val 1, ignoring negative values,
// so the result is 0 when no value is positive. `found` tells whether the
// ability is present at all.
int64_t bestOf(std::span<const ParsedAbility> list, AbilityKind k, bool* found = nullptr);

// Best over every object `owner` has in a system (Sys scope, spec 02 §1.5): its
// colonies' facilities and, unless `coloniesOnly`, the abilities of its ships
// and units there.
int64_t bestInSystem(const Rules& r, const GameState& s, EmpireId owner, SystemId sys, AbilityKind k, bool coloniesOnly = false);

// Design cost (hull + mounted components) without validation work.
Resources designCost(const Rules& r, const Design& d);

// Ships and bases appear in space when built; everything else is a unit that
// goes into cargo (spec 03 §1).
bool isShipOrBase(ruleset::VehicleType t);

// The empire's recorded home system (Empire::homeSystem): it never moves.
SystemId homeSystem(const GameState& s, EmpireId e);

// Number of stars in a system (solar generation).
int starCount(const GameState& s, SystemId sys);

// A living empire id.
inline bool livingEmpire(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size() && s.empire(e).alive; }

// A planet value after a change, clamped to the Settings range; never negative (spec 02 §1.2).
int clampedValue(const Rules& r, const GameState& s, int64_t v);

// Adds to a treasury or pool, never beyond kTreasuryLimit.
inline int64_t addCapped(int64_t pool, int64_t amount) { return std::min(pool + amount, std::max(pool, kTreasuryLimit)); }

// A queue owned by an empire, identified the same way as commands do.
struct QueueRef {
    cmd::QueueTarget target;
    Location location;
};

// What a queue would spend this turn: min(rate, cost - progress) of its top
// item when the treasury covers all of it, else nothing (spec 02 §6.3). No
// mutation; `treasury` is reduced by what is spent.
Resources projectQueueUsage(const Rules& r, const GameState& s, EmpireId e, const QueueRef& q, const ConstructionQueue& queue,
                            Resources& treasury);

// Every construction queue of an empire in processing order (spec 02 §6.3):
// queues whose top item is a spaceport, then a mineral, organics or
// radioactives generator, then a supply facility; then the rest in the
// empire's queue order (colonies by planet id, then space-yard vehicles by id).
std::vector<QueueRef> empireQueues(const Rules& r, const GameState& s, EmpireId e);

} // namespace opense4::game::economy::detail
