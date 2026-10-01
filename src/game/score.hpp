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

#include <string>
#include <string_view>
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
// Step 2 of an empire's end-of-turn processing (spec 05 §5, §8): appends this
// turn's statistics to a living empire's history, and for a human player
// adds the lines of its files to TurnContext::records.
void recordStatistics(TurnContext& ctx, EmpireId e);

// ---- Human players' files (spec 05 §3.4, §5, §8 step 2, confirmed: binary) --------------------
//
// At step 2 of a human player's end-of-turn processing the game writes three
// plain-text files of that player: statistics and history (appended) and a
// text copy of the log (rewritten). The engine makes the lines
// (TurnResult::records); whoever runs the game writes them (the classic
// client: a folder per game in the user data directory, an OpenSE4 choice,
// spec 05 open question 40). Fixed-width columns, one record per line.
struct PlayerRecords {
    EmpireId empire;
    uint32_t turn = 0;  // the turn processed (the first is 0: the files start afresh)
    // Appended: one row per empire whose score the player may see
    // (scoreVisible), statisticsLine with the date the end-of-turn processing
    // sees (fileDate).
    std::vector<std::string> statistics;
    // Appended, and the file opened only when there is a line: the entries
    // dated the turn before: treaties accepted, treaties broken and
    // declarations of war among the political messages the player sent or
    // received, and the empires destroyed, first contacts and contacts lost
    // of its log (a surrender is never recorded): historyLine.
    std::vector<std::string> history;
    // The whole file, rewritten (empty: the file is not written): only when
    // `Create Log Text Files for Players` is true (off when the key is
    // missing) and the log is not empty; logCopyHeader and 78 dashes, then
    // logLine for every entry of the log as it stands. Written with CR LF.
    std::vector<std::string> log;
};
PlayerRecords playerRecords(const Rules& r, const GameState& s, EmpireId e);
// The date an empire's end-of-turn processing sees, as a GameState::turn
// number: advanced in a simultaneous game (turn + 1), unadvanced in a
// turn-based one (spec 05 §5, §8).
uint32_t fileDate(const GameState& s);
// "2401.3": the date of a turn number (2400.0 + turn / 10).
std::string dateText(uint32_t turn);
// A statistics row (spec 05 §5, confirmed: binary): the empire's number
// (EmpireId + 1) in 5 characters, the date as a whole number of tenths of a
// year (24001 for 2400.1) in 8, then the Score window's eleven columns in 12
// each (score, resources, research, intelligence, tech levels, systems,
// planets, population, units, ships, bases), all right-aligned.
std::string statisticsLine(EmpireId e, uint32_t date, const TurnStats& t);
// A history row (spec 05 §3.4, confirmed: binary): the entry's date as tenths
// in 8 characters, the other empire's number in 5 (0 for none), two flags
// always 0 in 5 each, a space, then the text.
std::string historyLine(uint32_t date, EmpireId other, std::string_view text);
// A line of the log copy (spec 05 §3.4, spec 06 §6.1, confirmed: binary): the
// date as 2400.1 left-aligned in 9 characters, the title padded to 40, one
// space, then the text with each line break (a CR LF pair, or our texts'
// LF) turned into one space. The file's lines end in CR LF.
std::string logLine(uint32_t date, std::string_view title, std::string_view text);
// The log copy's first line: "Date" at column 1, "Header" at column 10 and
// "Text" at column 51 (spec 06 §6.1, §7 Q55); a line of 78 dashes follows.
std::string logCopyHeader();
// The log text of a destroyed empire's announcement (the history file finds
// the empire by it).
std::string destroyedText(const GameState& s, EmpireId gone);
// The victory tests' comparisons, in floating point (spec 05 §6, confirmed:
// binary; xmath's extended precision): "X % of second place" is score >=
// (X / 100) × other; "X % of tech" is levels >= maxLevels × X / 100.
bool leadsBy(int64_t score, int64_t other, int percent);
bool techShareMet(int64_t levels, int64_t maxLevels, int percent);
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
