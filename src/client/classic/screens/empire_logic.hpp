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

// The filter the Log opens with: the one stored with the empire
// (InterfaceOptions::logFilter: 0 All, else the category + 1), or All when
// this turn has no entry of that category. `counts` holds the entries per category.
uint8_t logOpeningFilter(uint8_t stored, const std::vector<int>& counts);
// The row the Log opens on: the stored position when the filtered list has
// it, else the first row; -1 for an empty list.
int logOpeningRow(int32_t stored, size_t rows);

// Windows Goto opens over the Log for an entry without a location (spec 06
// §4.1). Which entries name which window is not settled: we take it from the
// entry's category (inferred, spec 06 §7).
enum class LogWindow : uint8_t { ConstructionQueues, Research, Intelligence, EmpireOptions, Designs, Empires };
std::optional<LogWindow> logWindowTarget(game::LogCategory c);

// The Combat Forces / Damage list of a combat entry (spec 06 §4.1): for each
// empire in the battle a header row, then one row per ship, unit group and
// planet with its damage as a percentage, "Dead" (destroyed) or "Taken"
// (captured). Ships and bases left in the game show their damage now; unit
// groups the share of their units lost; planets the share of their hit points
// the record cannot give, so "0%" untouched and "Hit" otherwise (inferred).
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
