#pragma once

// Scores, statistics history and victory (docs/spec/05 §5–§6, spec 01 §11).
//
// Entry points for the spec 05 §8 turn order (turn.cpp):
// - recordStatistics(ctx, e): step 2 of each empire's end-of-turn processing.
// - checkDestruction(ctx, e): after each empire's end-of-turn processing.
// - checkVictory(ctx, date): once per game turn, after every empire's
//   end-of-turn processing and before the event step, with the date already
//   advanced for this turn.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <vector>

namespace opense4::game {
struct TurnContext;
}

namespace opense4::game::score {

// Score terms (spec 05 §5, confirmed: binary).
inline constexpr int64_t kTonnageWeight = 10;
inline constexpr int64_t kTechLevelWeight = 200;
inline constexpr int64_t kEverythingBonus = 50'000;

// The parts the score is made of.
struct ScoreParts {
    int64_t tonnage = 0;      // hull Tonnage of every ship and base, mothballed ones excluded
    int64_t production = 0;   // minerals + organics + radioactives + research + intelligence produced this turn
    int64_t techLevels = 0;   // tech levels, each capped at its area's maximum
    bool everything = false;  // every area allowed and visible to the empire at its maximum
};
// 10 × tonnage + production + 200 × tech levels + 50,000 when everything is researched.
int64_t scoreOf(const ScoreParts& p);
ScoreParts scoreParts(const Rules& r, const GameState& s, EmpireId e);

int64_t empireScore(const Rules& r, const GameState& s, EmpireId e);
// The statistics columns of the Scores window (spec 05 §5) and the score.
TurnStats currentStats(const Rules& r, const GameState& s, EmpireId e);
// Living empires, best score first (ties: lower empire number first).
std::vector<EmpireId> ranking(const Rules& r, const GameState& s);
// Whether `viewer` may see `other`'s score under the Score Display option
// (spec 05 §5): own only; own plus the empires we hold Non-Aggression or
// better with; everyone. Once the game is over every score is visible.
// Destroyed empires are not shown.
bool scoreVisible(const GameState& s, EmpireId viewer, EmpireId other);
// True if the empire has nothing left: no populated planet and no ship or
// base; units do not count (spec 05 §6).
bool defeated(const Rules& r, const GameState& s, EmpireId e);

// The destruction check of one empire (spec 05 §6, confirmed: binary): a
// defeated empire is destroyed; every empire in contact with it is told, all
// treaties with it return to "no contact", intelligence projects aimed at it
// are removed and its remaining objects (empty colonies, units) go.
void checkDestruction(TurnContext& ctx, EmpireId e);
// Appends this turn's statistics to a living empire's history (spec 05 §5:
// written at the start of the empire's end-of-turn processing).
void recordStatistics(TurnContext& ctx, EmpireId e);
// Whether the peace condition's pairs all hold: every two living empires have
// contact and a treaty of Non-Aggression or better.
bool galaxyAtPeace(const GameState& s);
// The victory check (spec 05 §6, confirmed: binary). `date` is in turns
// since 2400.0, already advanced for this turn. Before the "After X years"
// qualifier's date nothing is checked and the peace counter stands still.
// Meeting a condition ends the game (GameState::gameOver) without naming a
// winner; GameState::winner is set to the best score (ties to the lower
// empire number, neutral empires excluded) for the game-over screen
// (OpenSE4 choice, inferred).
void checkVictory(TurnContext& ctx, uint32_t date);

} // namespace opense4::game::score
