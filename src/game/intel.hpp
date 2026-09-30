#pragma once

// Intelligence projects and counter-intelligence (docs/spec/05 §2).
//
// Entry points for the spec 05 §8 turn order (turn.cpp):
// - intelStep(ctx, e): step 3 of one empire's end-of-turn processing, before
//   research::researchStep. Each empire funds and runs its own projects in
//   turn, so a defender with a lower empire number has already added this
//   turn's points to its defenses when a higher-numbered attacker strikes.
// - research::addToPools(e, rp, ip): the income step refills the pool.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <string>
#include <string_view>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::intel {

// Label put in front of every intelligence message (IntelProjects.txt header).
inline constexpr std::string_view kMinisterPrefix = "Intelligence Minister: ";
// The victim of a successful operation names the source as the suspect on a
// roll of 1–N equal to 1: 20 % (spec 05 §2.1, confirmed: binary).
inline constexpr int kSuspectRoll = 5;
// Cap on the running defense total D (spec 05 §2.4, confirmed: binary).
inline constexpr int64_t kDefenseCap = 1'000'000'000;

// The line added to the victim's message when the suspect roll names the
// source (`sourceFullName`, effects::empireFullName) of a successful operation.
std::string suspectLine(std::string_view sourceFullName);
// Whether a log entry of `victim` tells it that `culprit` carried out an
// operation against it: an Intelligence entry holding the suspect line. Blocked
// attempts and counter-intelligence reports never do. The computer players'
// anger and demands read this (spec 05 §7.3 term 3, §7.4).
bool namesCulprit(const GameState& s, const LogEntry& entry, EmpireId culprit);

// True for projects of Type `Intelligence Defense`.
bool isDefense(const Rules& r, uint32_t project);
// The technology a project needs, which a finished defense compares with its
// level: the sum of the levels in its requirement block (spec 05 §2.4,
// confirmed: binary).
int requirementLevel(const Rules& r, uint32_t project);
// Counter-intelligence an empire holds right now (for display): the sum over
// its defense projects of trunc(Amount × progress × `Intelligence Defense
// Modifier Percent` / 100), capped at kDefenseCap.
int64_t defensePoints(const Rules& r, const GameState& s, EmpireId e);
// Tests an attack of strength `attack` (its accumulated progress) against
// T's defenses (spec 05 §2.4, confirmed: binary): T's defense projects are
// taken from the bottom of its queue upwards, each adding its strength to D
// and losing its progress; once D >= attack the attack is defeated and the
// project that tipped the balance keeps what is left over. Returns the index
// of that project in T's queue, or -1 when the attack goes ahead (every
// defense has still been drained).
int counterIntelligence(const Rules& r, GameState& s, EmpireId target, int64_t attack);

// Why a project order cannot run now (empty = it can).
std::string orderProblem(const Rules& r, const GameState& s, EmpireId source, const IntelProjectOrder& order);

// One empire's intelligence step (spec 05 §2.1, confirmed: binary): skipped
// when intelligence is not allowed; otherwise the queue is dropped when the
// empire is in contact with no living empire and projects aimed at destroyed
// empires go; shares of the pool are added to progress; every project whose
// progress reaches its Cost runs in queue order (no success roll: attacks
// face only counter-intelligence, a finished defense deletes one hostile
// project); what ran leaves the queue unless Repeat is on (then it restarts
// at 0 in place); the pool is emptied.
void intelStep(TurnContext& ctx, EmpireId e);

} // namespace opense4::game::intel
