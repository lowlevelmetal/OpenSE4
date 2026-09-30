#pragma once

// Scores, statistics history and victory (docs/spec/05 §5-6).

#include "game/rules.hpp"
#include "game/state.hpp"

#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::score {

// The score is a weighted sum of the statistics columns. The original's
// weights are undocumented (spec 05 open question 7); ours can be changed in
// Settings.txt with these keys (weights per 1000 of the statistic, inferred).
struct Weights {
    int64_t resources = 100;     // per 1000 resources produced per turn
    int64_t research = 100;      // per 1000 RP per turn
    int64_t intelligence = 100;  // per 1000 IP per turn
    int64_t techLevels = 100000; // per 1000 tech levels
    int64_t systems = 100000;
    int64_t planets = 200000;
    int64_t population = 1000;   // per 1000 M
    int64_t units = 5000;
    int64_t ships = 50000;
    int64_t bases = 50000;
};
Weights weights(const Rules& r);
int64_t scoreOf(const TurnStats& t, const Weights& w);

int64_t empireScore(const Rules& r, const GameState& s, EmpireId e);
TurnStats currentStats(const Rules& r, const GameState& s, EmpireId e);
// Living empires, best score first (ties: lower id first).
std::vector<EmpireId> ranking(const Rules& r, const GameState& s);
// Whether `viewer` may see `other`'s score: self and allies, or everyone
// with the Score Display option.
bool scoreVisible(const GameState& s, EmpireId viewer, EmpireId other);
// True if the empire has nothing left: no populated colony and no ship or base.
bool defeated(const Rules& r, const GameState& s, EmpireId e);

// Turn phase 13 (before the turn counter advances): statistics and history
// for every living empire, eliminations, peace tracking, then the victory
// checks (sets GameState::gameOver / winner).
void endOfTurn(TurnContext& ctx);

} // namespace opense4::game::score
