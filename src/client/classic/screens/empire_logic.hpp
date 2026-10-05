#pragma once

// UI-side logic of the research, intelligence, diplomacy and log windows that
// does not draw anything: which areas, projects and message types to offer,
// what a tech level unlocks, statistics metrics and text exports. Kept free
// of ImGui so the tests can exercise it.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// ---- Empires -------------------------------------------------------------------------------

// Empires `me` has met (Relation::contact), alive, in id order, without `me`.
std::vector<game::EmpireId> contactedEmpires(const game::GameState& s, game::EmpireId me);
// Two-letter code for the Treaty Grid ("TA" = Trade Alliance).
std::string_view treatyCode(game::Treaty t);
// A computer leader's anger toward us (Relation::anger, 0..100) as one of the
// nine labels of spec 05 §7.3. Not a colony's mood: that is its own scale and
// bands (spec 02 §4, game::economy::moodName).
std::string_view moodWord(int anger);
// Whether `viewer` may see the treaty between two other empires (spec 05 §3.2:
// only between empires the viewer is allied with).
bool treatyVisibleTo(const game::GameState& s, game::EmpireId viewer, game::EmpireId a, game::EmpireId b);

// ---- Messages ---------------------------------------------------------------------------------

// What a message type needs besides type, tone and text.
struct MessageNeeds {
    bool treaty = false;       // a proposed treaty
    bool offer = false;        // a package we give
    bool request = false;      // a package we ask for
    bool system = false;       // a system
    bool planet = false;       // a planet (theirs or a third party's)
    bool ownPlanet = false;    // one of our planets
    bool thirdEmpire = false;  // another empire
};
MessageNeeds messageNeeds(game::MessageType t);
// Message types worth composing to an empire under treaty `t` (replies such as
// Accept/Refuse/Counter are not included: they answer a received message).
std::vector<game::MessageType> sendableMessageTypes(game::Treaty t, const game::GameOptions& o);
// Whether a received message can be accepted or refused (cmd::AnswerMessage).
bool answerable(game::MessageType t);
// How many messages from `from` wait for `me` to answer them: delivered, not
// answered, of a type that takes an answer, and still in the Log. Only the
// Log's Send Reply answers a message (spec 06 §4.1), so once its entry has
// left the Log (spec 05 §3.4 "Log lifetime") the chance to reply has passed
// and it waits no more (Empires' "Inbox: N waiting", GitHub issue #4).
int messagesAwaitingReply(const game::GameState& s, game::EmpireId me, game::EmpireId from);
// The counter-proposal type for a received proposal (General when there is none).
game::MessageType counterType(game::MessageType received);
// Treaties that may be proposed while the current treaty is `current`.
std::vector<game::Treaty> proposableTreaties(game::Treaty current);
std::string_view toneName(int tone);  // 0 pleading, 1 neutral, 2 demanding
// Text we put in a new message of this type (the player can edit it).
std::string defaultMessageText(game::MessageType t, game::Treaty treaty);

// Package items. A take-side item whose id is left invalid stands for "Any":
// the other side chooses what to give (inferred; spec 05 §3.4).
bool isAnyItem(const game::PackageItem& item);
bool packageHasAny(const std::vector<game::PackageItem>& items);
std::string packageItemText(const game::Rules& r, const game::GameState& s, const game::PackageItem& item);
std::string_view packageKindName(game::PackageItem::Kind k);

// ---- Research -------------------------------------------------------------------------------

// Areas the empire may add to its queue: research::researchable, or (while the
// engine returns nothing) visible areas below their maximum level.
std::vector<ruleset::TechAreaId> researchableAreas(const game::Rules& r, const game::GameState& s, const game::Empire& e);

// The Research window's list (observed, spec 07 session 3): the areas the
// empire may research and, in their places, the areas it has completed, which
// stay listed (dimmed, with "Complete" as the cost). In data order.
struct ResearchListArea {
    ruleset::TechAreaId area;
    bool complete = false;
};
std::vector<ResearchListArea> researchListAreas(const game::Rules& r, const game::GameState& s, const game::Empire& e);

struct TechUnlock {
    enum class Kind : uint8_t { Component, Facility, Hull, IntelProject, TechArea };
    Kind kind = Kind::Component;
    uint32_t index = 0;
    std::string name;
};
std::string_view unlockKindName(TechUnlock::Kind k);
// Items whose requirements name exactly (area, level).
std::vector<TechUnlock> techUnlocks(const game::Rules& r, ruleset::TechAreaId area, int level);
// Names for the Research window: research::unlockedBy, or techUnlocks as a fallback.
std::vector<std::string> unlockNames(const game::Rules& r, ruleset::TechAreaId area, int level);
// "Physics 2, Chemistry 1" (or "None").
std::string requirementText(const game::Rules& r, const std::vector<ruleset::TechRequirement>& reqs);
// Areas that list `area` as a prerequisite.
std::vector<ruleset::TechAreaId> dependentAreas(const game::Rules& r, ruleset::TechAreaId area);
// Owned levels and the sum of maximum levels over the areas allowed in this game.
std::pair<int, int> techProgress(const game::Rules& r, const game::GameState& s, const game::Empire& e);
// "1.3 years" for a turn count (-1 = "Never").
std::string etaText(int turns);

// Tech Tree export (plain text).
std::string techAreasExport(const game::Rules& r, const game::GameState& s, const game::Empire& e);
std::string techLevelsExport(const game::Rules& r, const game::GameState& s, const game::Empire& e);

// ---- Intelligence ---------------------------------------------------------------------------

enum class IntelTarget : uint8_t { None, Empire, Planet, Vehicle, ThirdEmpire };
// What an intel project targets, from its `Type` (inferred from the effect list, spec 05 §2.3).
IntelTarget intelTargetKind(const ruleset::IntelProject& p);
// Projects whose requirements the empire meets, in data order.
std::vector<uint32_t> availableIntelProjects(const game::Rules& r, const game::Empire& e);

// ---- Statistics -------------------------------------------------------------------------------

enum class Metric : uint8_t { Score, Resources, Research, Intelligence, TechLevels, Systems, Planets, Population, Units, Ships, Bases, Count };
std::string_view metricName(Metric m);
int64_t metricValue(const game::TurnStats& t, Metric m);
// Statistics of an empire over time: its recorded history plus the current turn.
std::vector<game::TurnStats> statsSeries(const game::Rules& r, const game::GameState& s, game::EmpireId e);

// Timeline lines derived from consecutive TurnStats ("Colonized 2 planets").
struct HistoryEvent {
    uint32_t turn = 0;
    std::string text;
};
std::vector<HistoryEvent> statsEvents(const std::vector<game::TurnStats>& series);

// ---- History window (spec 05 §3.4) ------------------------------------------------------------

// The empire lists the History window offers `viewer`: itself, the empires
// it knows, and the empires its record mentions (destroyed ones included), in
// empire order. The General list comes on top of these.
std::vector<game::EmpireId> historyEmpires(const game::GameState& s, game::EmpireId viewer);

struct HistoryLine {
    uint32_t turn = 0;
    std::string text;
    std::optional<game::Location> where;
};
// One list of the viewer's History window, newest first: the record's
// entries about `empire` (invalid: the General list). An empire's list also
// starts with its founding and, with `stats`, the changes its statistics show
// (statsEvents).
std::vector<HistoryLine> historyLines(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::EmpireId empire,
                                      bool stats);

// ---- Log (spec 06 §4.1) ------------------------------------------------------------------------

inline constexpr int kLogCategories = int(game::LogCategory::Misc) + 1;

// Whether the Log lists an entry: those of the turn in progress and of the
// one before (an empire's end-of-turn processing keeps only these, spec 05
// §3.4 "Log lifetime").
bool logListsEntry(const game::GameState& s, const game::LogEntry& l);

// The filter the Log opens with: the one stored with the empire
// (InterfaceOptions::logFilter: 0 All, else the category + 1), or All when
// this turn has no entry of that category (the window then stores All).
// `counts` holds the entries per category.
uint8_t logOpeningFilter(uint8_t stored, const std::vector<int>& counts);
// The row the Log opens on (spec 06 §4.1, §7 Q42, confirmed: binary): the
// stored selection is an entry's index in the empire's whole log
// (InterfaceOptions::logPosition); the row of the filtered list that shows
// that entry, else the first row; -1 for an empty list. `shown` lists the
// log index of each row.
int logOpeningRow(int32_t stored, const std::vector<int32_t>& shown);
// The scroll position (rows) the Log opens with: the stored one when the
// stored entry is in the filtered list, else the top, where the first row is
// selected (spec 06 §4.1 "Selection": both are restored, or neither).
int logOpeningScroll(int32_t storedEntry, int32_t storedScroll, const std::vector<int32_t>& shown);
// The battle a combat entry reports, for its details and Combat Replay: the
// index in GameState::combats of the record at the entry's place (the battles
// of the game turn in progress and the one before), or -1.
int logCombatRecord(const game::GameState& s, const game::LogEntry& l);

// Windows Goto opens over the Log (game::LogGoto; spec 06 §4.1, §7 Q41).
enum class LogWindow : uint8_t { ConstructionQueues, Research, Intelligence, EmpireOptions, Designs, Empires };
// The window of a window target; none for None and Location.
std::optional<LogWindow> logWindowTarget(game::LogGoto target);

// The Combat Forces / Damage list of a combat entry (spec 06 §4.1, §7 Q43,
// confirmed: binary): for each empire in the battle a header row, then one
// row per piece of that empire present when the battle began, in setup order
// (CombatPiece::damage >= 0: no seekers, neutral obstacles or units launched
// during the battle). A ship or base reads "<name> (<hull Code>)". The damage
// is the percentage fixed at the battle's end; a piece whose name is not
// among its empire's survivors is "Taken" when the name is among another
// empire's survivors, else "Dead".
struct CombatDamageRow {
    game::EmpireId empire;
    bool header = false;        // the empire's own row (flag and name)
    uint32_t piece = 0;         // index into CombatRecord::pieces (rows that are not headers)
    std::string name;
    std::string damage;
};
std::vector<CombatDamageRow> combatDamageRows(const game::Rules& r, const game::GameState& s, const game::CombatRecord& c);

// ---- Lists ---------------------------------------------------------------------------------

// Moves v[from] to position `to` (both clamped); returns false when nothing moved.
template <class T>
bool moveEntry(std::vector<T>& v, size_t from, size_t to) {
    if (from >= v.size()) return false;
    if (to >= v.size()) to = v.size() - 1;
    if (from == to) return false;
    T item = std::move(v[from]);
    v.erase(v.begin() + static_cast<std::ptrdiff_t>(from));
    v.insert(v.begin() + static_cast<std::ptrdiff_t>(to), std::move(item));
    return true;
}

} // namespace opense4::client::classic
