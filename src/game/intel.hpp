#pragma once

// Intelligence projects and counter-intelligence (docs/spec/05 §2).

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
// Percent chance that the victim of a successful operation learns who did it (inferred).
inline constexpr int kDetectionPercent = 50;
// Attack strength bonus when the operatives choose the target ("Any", inferred).
inline constexpr int kAnyTargetBonusPercent = 25;

// True for projects of Type `Intelligence Defense`.
bool isDefense(const Rules& r, uint32_t project);
// Counter-intelligence strength of an empire this turn (spec 05 §2.4):
// Σ progress × level over its defense projects, times `Intelligence Defense
// Modifier Percent` / 100.
int64_t defensePoints(const Rules& r, const GameState& s, EmpireId e);
// Attack strength of a project order.
int64_t attackStrength(const Rules& r, const IntelProjectOrder& order);

// Why a project order cannot run now (empty = it can).
std::string orderProblem(const Rules& r, const GameState& s, EmpireId source, const IntelProjectOrder& order);

// Turn phase 7: spends Empire::economy.intelligence on every queue, then
// executes finished projects empire by empire, testing each attack against
// the target's counter-intelligence.
void runIntel(TurnContext& ctx);

} // namespace opense4::game::intel
