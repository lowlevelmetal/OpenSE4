#pragma once

// UI-side logic of the research, intelligence, diplomacy and log windows that
// does not draw anything: which areas, projects and message types to offer,
// what a tech level unlocks, statistics metrics and text exports. Kept free
// of ImGui so the tests can exercise it.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// ---- Empires -------------------------------------------------------------------------------

// Empires `me` has met (Relation::contact), alive, in id order, without `me`.
std::vector<game::EmpireId> contactedEmpires(const game::GameState& s, game::EmpireId me);
// The empires sorted by name, the letter case ignored, as the original's
// empire pickers list them (spec 06 §7 Q113 (e), confirmed: binary).
std::vector<game::EmpireId> empiresByName(const game::GameState& s, std::vector<game::EmpireId> empires);
// The Treaty Grid's code for a treaty (spec 06 §1.5, confirmed: binary):
// War, NI, NA, Sub, Pro, TA, TR, MA, P; None has none (empty).
std::string_view treatyCode(game::Treaty t);
// A computer leader's anger toward us (Relation::anger, 0..100) as one of the
// nine labels of spec 05 §7.3. Not a colony's mood: that is its own scale and
// bands (spec 02 §4, game::economy::moodName).
std::string_view moodWord(int anger);
// Whether `viewer` may see the treaty between two empires (spec 05 §3.2,
// confirmed: binary): its own, and those of the empires it holds
// Non-Aggression or better with (game::diplomacy::treatyVisible).
bool treatyVisibleTo(const game::GameState& s, game::EmpireId viewer, game::EmpireId a, game::EmpireId b);
// The Treaty Grid's cell for row `a` and column `b` as `viewer` sees it (spec
// 06 §1.5, confirmed: binary): blank unless the viewer may see their treaty
// and the two hold a real one (in contact, not None); otherwise its code,
// and for a Subjugation or Protectorate the roles in (row, column) order,
// "S" the subordinate and "D" the dominant side: "Sub (S, D)".
std::string treatyGridCell(const game::GameState& s, game::EmpireId viewer, game::EmpireId a, game::EmpireId b);
// The bracketed roles after a Subjugation or Protectorate in the Empires
// window (spec 06 §1.5, confirmed: binary), from our side of the relation:
// "(We Dominate Them)", "(They Dominate Us)", "(We Protect Them)", "(They
// Protect Us)"; empty for any other treaty.
std::string treatyRoles(const game::Relation& ours);
// The Empires window's status line about `them` (spec 06 §1.5, confirmed:
// binary), the first that applies: "Message Sent" when we wrote to them this
// turn; "Offered Trade", "Offered Gift" or "Offered Tribute" when their
// newest message (replyableMessage) is one; "Proposed Treaty: <treaty>" when
// it proposes a treaty other than the current one; else empty.
std::string empireStatusLine(const game::GameState& s, game::EmpireId me, game::EmpireId them);

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
// Whether the type's package is a trade (Edit Trade) rather than one side's
// (Edit Package: Gift, Tribute, Want a gift, Want a tribute; spec 06 §4.2).
bool tradePackage(game::MessageType t);
// Message types worth composing to an empire under treaty `t` (spec 06 §4.2,
// confirmed: binary). Replies such as Accept/Refuse/Counter are not included:
// they answer a received message (replyTypes). `subjugated`: our race is
// subjugated, so no Propose Treaty (game::diplomacy::isSubjugated).
std::vector<game::MessageType> sendableMessageTypes(game::Treaty t, const game::GameOptions& o, bool subjugated = false);
// Whether a received message can be accepted or refused (cmd::AnswerMessage).
bool answerable(game::MessageType t);
// How many messages from `from` wait for `me` to answer them, 0 or 1: the
// newest recent one (replyableMessage), when it takes an answer, has none
// yet and is still in the Log (spec 05 §3.4, confirmed: binary: only that
// one can be answered). Once its entry has left the Log (spec 05 §3.4 "Log
// lifetime") the chance to reply has passed and it waits no more (Empires'
// "Inbox: N waiting", GitHub issue #4).
int messagesAwaitingReply(const game::GameState& s, game::EmpireId me, game::EmpireId from);
// The counter-proposal type for a received proposal (General when there is none).
game::MessageType counterType(game::MessageType received);
// The newest political message from `from` that Communicate, opened from
// `from`'s portrait, can answer (spec 06 §4.2, spec 05 §3.4, confirmed:
// binary): delivered to `me` and dated (DiplomaticMessage::dated) after the
// date − 2, or in a turn-based game after the date − 1 when `from`'s player
// number is lower than ours (game::diplomacy::newestMessage). Only the
// newest counts; nullptr when there is none. It is the only message the
// engine lets a player answer (cmd::AnswerMessage).
const game::DiplomaticMessage* replyableMessage(const game::GameState& s, game::EmpireId me, game::EmpireId from);
// The reply types Communicate adds for a received message (spec 06 §4.2,
// confirmed: binary): Accept, Refuse and Counter Treaty after a treaty
// proposal or counter-proposal; Accept, Refuse and Counter Trade after a
// trade; Accept and Refuse Gift after a gift, Accept and Refuse Tribute after
// a tribute (both AcceptGift/RefuseGift); Accept and Refuse Demand after any
// demand or request. None for the rest.
std::vector<game::MessageType> replyTypes(game::MessageType received);
// A reply type's name as the list shows it: "Accept Tribute" for AcceptGift
// answering a tribute, otherwise game::displayName.
std::string replyTypeName(game::MessageType reply, game::MessageType received);
// Whether a reply type (replyTypes) accepts (true) or refuses (false) the
// message it answers; nullopt for Counter Treaty and Counter Trade, which are
// new proposals.
std::optional<bool> replyAccepts(game::MessageType reply);
// Communicate's View Trade, View Gift and View Tribute buttons (spec 06 §1.5,
// confirmed: binary): the one the type of the newest message from the empire
// (replyableMessage) lights, if any.
enum class OfferView : uint8_t { None, Trade, Gift, Tribute };
OfferView offerView(const game::DiplomaticMessage* newest);
// Treaties a Propose or Counter Treaty may name while the current treaty is
// `current` (spec 06 §4.2, confirmed: binary): Non-Intercourse to
// Partnership, never War or None; the current treaty stays listed.
std::vector<game::Treaty> proposableTreaties(game::Treaty current);
// The package's Treaty tab: every treaty but None and the current one, War included.
std::vector<game::Treaty> packageTreaties(game::Treaty current);
std::string_view toneName(int tone);  // 0 pleading, 1 neutral, 2 demanding
// OpenSE4's text for a new message of this type, when the race has no line
// for it (the player can edit it).
std::string defaultMessageText(game::MessageType t, game::Treaty treaty);
// The AI_Speech pool whose line fills a new message's text (spec 06 §4.2,
// confirmed: binary): "Send <type>" by the AI's name of the type (an
// answer to a tribute "Send Accept Tribute"); an Accept or Refuse Demand
// "Response Friend|Enemy YES|NO <demand>", Friend when we hold Non-Aggression
// or better with the recipient. `received` is the message answered (General
// when none).
std::string messageSpeechPool(const game::GameState& s, game::EmpireId me, game::EmpireId to, game::MessageType t,
                              game::MessageType received = game::MessageType::General);
// The text a new message starts with: a line of our race's own AI_Speech
// pool (messageSpeechPool), the `pick`-th modulo its size, its tokens filled
// in (game::ai::fillSpeech); else defaultMessageText. `pick` comes from a
// random source of the window's own, never the game's.
std::string messageText(const game::Rules& r, const game::GameState& s, game::EmpireId me, const game::DiplomaticMessage& draft,
                        game::MessageType received, uint64_t pick);

// Package items. A take-side item whose id is left invalid stands for "Any":
// the other side chooses what to give (inferred; spec 05 §3.4).
bool isAnyItem(const game::PackageItem& item);
bool packageHasAny(const std::vector<game::PackageItem>& items);
std::string packageItemText(const game::Rules& r, const game::GameState& s, const game::PackageItem& item);
std::string_view packageKindName(game::PackageItem::Kind k);

// ---- Select Package (spec 05 §3.4, spec 06 §1.5, confirmed: binary) ------------------------

enum class PackageTab : uint8_t { Systems, Planets, Resources, Technology, Ships, Units, StarCharts, Treaty, CommChannels, Count };
std::string_view packageTabName(PackageTab t);
// Whether the tab can be chosen: Technology is dim when the game does not
// allow technology trades.
bool packageTabEnabled(const game::GameOptions& o, PackageTab t);
// The items Select Package lists on a tab for `me` writing to `them`, on the
// side we give (`give`) or the side we ask for; an "Any" placeholder (an item
// whose id is left invalid) comes first where the original offers one:
// - Systems: every system we have explored, on both sides; no "Any".
// - Planets: our colonies; theirs that we see now (a colony whose cloak beats
//   our detection left out), sorted by name, after "Any".
// - Technology (only with technology trades allowed): the areas we hold a
//   level in; every area we can see, maxed ones included, after "Any"; both
//   in alphabetical order.
// - Ships: our ships, bases and other vehicles that are not units; theirs that
//   we see, after "Any".
// - Units: our ships and bases carrying units (loaded transports); every ship
//   and base of theirs that we see, loaded or not, after "Any".
// - Star Charts: every system we have explored, on both sides; "Any" on the
//   side we ask for.
// - Treaty: packageTreaties.
// - Comm Channels: on both sides, the living empires we are in contact with
//   other than us and the recipient; "Any" first on the side we ask for.
// Resources are typed in, so their tab lists nothing.
std::vector<game::PackageItem> packageChoices(const game::Rules& r, const game::GameState& s, game::EmpireId me, game::EmpireId them,
                                              PackageTab tab, bool give);

// Select Package's lines (spec 05 §3.4, confirmed: binary). Each line has an
// item and a number of steps: a resource line holds one resource, a step
// being 1,000 of it; an "Any" line holds that many of its kind ("Any Planet -
// 2"); every other line one item. A line holds at most kMaxPackageSteps.
inline constexpr int kMaxPackageSteps = 250;
inline constexpr int64_t kPackageStepAmount = 1000;
struct PackageLine {
    game::PackageItem item;  // a resource line: the resource's amount is the line's (steps × 1,000)
    int steps = 1;
};
// The steps a click adds or takes off: 10 with the x10000 toggle lit or
// Shift held, 100 with x100000 (x10000 is tested first, then x100000, then
// Shift), else 1.
int packageClickSteps(bool x10000, bool x100000, bool shift);
// The resource row a click on `res` adds: a line item of that resource alone.
game::PackageItem resourceRow(game::Resource res);
// A click on a resource row or an "Any" row adds `steps` (spec 05 §3.4,
// confirmed: binary): every line of the same kind and value with room gets
// them, in package order (a quirk: two such lines, possible only after a
// removal, both grow); a line that would pass kMaxPackageSteps is filled to it
// and the rest starts a new line; with no line with room, a new line. Any
// other item is added once (OpenSE4's: a second click on it does nothing; a
// treaty replaces the side's treaty).
void addPackageClick(std::vector<PackageLine>& lines, const game::PackageItem& row, int steps);
// A click on a package line: a resource or "Any" line loses `steps`, or goes
// when that is at least what it holds; any other line goes.
void takePackageClick(std::vector<PackageLine>& lines, size_t line, int steps);
// The package's lines from its items (resource items split into one line per
// resource, at most kMaxPackageSteps each; consecutive identical "Any" items
// merged), and back (a resource line becomes one item, an "Any" line as many
// items as it has steps).
std::vector<PackageLine> packageLines(const std::vector<game::PackageItem>& items);
std::vector<game::PackageItem> packageItems(const std::vector<PackageLine>& lines);
// Whether two package sides hold the same items in the same order.
bool samePackage(const std::vector<game::PackageItem>& a, const std::vector<game::PackageItem>& b);
// A line's text: "Minerals - 5000", "Any Planet - 2", else packageItemText.
std::string packageLineText(const game::Rules& r, const game::GameState& s, const PackageLine& line);

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
// The levels of an area from `first` up to its maximum that make something
// available, each with what it brings (techUnlocks); levels that bring nothing
// are left out. For the area report's "Later levels bring" (ours, GitHub issue #11).
struct LevelUnlocks {
    int level = 0;
    std::vector<TechUnlock> items;
};
std::vector<LevelUnlocks> levelUnlocksFrom(const game::Rules& r, ruleset::TechAreaId area, int first);
// "Physics 2, Chemistry 1" (or "None").
std::string requirementText(const game::Rules& r, const std::vector<ruleset::TechRequirement>& reqs);
// Areas that list `area` as a prerequisite.
std::vector<ruleset::TechAreaId> dependentAreas(const game::Rules& r, ruleset::TechAreaId area);
// Owned levels and the sum of maximum levels over the areas allowed in this game.
std::pair<int, int> techProgress(const game::Rules& r, const game::GameState& s, const game::Empire& e);
// "1.3 years" for a turn count (-1 = "Never").
std::string etaText(int turns);

// ---- Tech Tree (spec 06 §7 Q92, confirmed: binary) -----------------------------------------

// `areas` under their Group: the groups in alphabetical order (the letter
// case ignored, ours), the areas in data order under each. The Research
// list and the Tech Tree both use it.
std::vector<std::pair<std::string, std::vector<ruleset::TechAreaId>>> techAreasByGroup(const game::Rules& r,
                                                                                       const std::vector<ruleset::TechAreaId>& areas);

// One row of the Tech Tree's lists and of its export: three cells.
struct TechTreeRow {
    ruleset::TechAreaId area;
    std::array<std::string, 3> cells;
};
// The Tech Areas view's rows of an area (columns Tech Name, Required Tech,
// Level): one per requirement; a racial area adds a row naming the racial
// trait with "Racial Trait" as its level, a unique area one reading "Unique
// Technology" and "(Must Be Found)". An area that needs nothing gets one row
// with its name only (inferred). Each row names the area (inferred).
std::vector<TechTreeRow> techAreaRows(const game::Rules& r, ruleset::TechAreaId area);
// The Tech Levels view's rows of an area (columns Tech Name, Level, "Item
// [Number of Tech Dependencies]"): for each level from 1 to its maximum, one
// row per vehicle size, component, facility and intelligence project, in that
// order, that needs exactly that level of the area, its name followed by
// " [N]" when it has N > 1 requirements. A level that makes nothing available
// gets one row without an item (inferred).
std::vector<TechTreeRow> techLevelRows(const game::Rules& r, ruleset::TechAreaId area);
// Every tech area of the data set, nothing hidden, in the window's order.
std::vector<TechTreeRow> techTreeRows(const game::Rules& r, bool levels);
// The export (TechAreas.txt, TechLevels.txt): one line per row, its cells
// joined by commas, no heading line (inferred).
std::string techTreeExport(const std::vector<TechTreeRow>& rows);

// ---- Intelligence ---------------------------------------------------------------------------

// What a project asks for when it is added (spec 05 §2.1, confirmed: binary):
// nothing (Intelligence Defense), the target empire only, or the empire and
// then a ship, a planet, a technology area or a third empire of a second list.
enum class IntelTarget : uint8_t { None, Empire, Planet, Vehicle, Tech, ThirdEmpire };
// From the project's `Type`: the seven ship sabotage types a ship; Conditions
// Change to Facility Damage and Planet - Info a planet; Research - Steal an
// area; the five Politics types a third empire; every other type (a mod's
// too) the empire only.
IntelTarget intelTargetKind(const ruleset::IntelProject& p);
// Projects whose requirements the empire meets, in data order.
std::vector<uint32_t> availableIntelProjects(const game::Rules& r, const game::Empire& e);

// One of the list windows an intelligence project asks for when `me` adds it
// (spec 05 §2.1, confirmed: binary): its title and its rows, each the order a
// click on it makes. `stage` 0 is "Select Empire", the living empires `me` is
// in contact with, with no "Any" row; stage 1 the second list of the
// project's kind for `order.target`, its "Any ..." row first: "Select Ship"
// (the target's vehicles `me` sees now), "Select Planet" (its colonies `me`
// sees), "Select Technology" (the areas `me` can research now) or "Select
// Empire" (`me`'s other contacts). Every list is sorted by name with the
// letter case ignored (confirmed: binary). `last`: a click on a row of this
// list completes the order.
struct IntelTargetRow {
    std::string label;
    game::IntelProjectOrder order;
};
struct IntelTargetList {
    std::string title;
    std::vector<IntelTargetRow> rows;
    bool last = true;
};
IntelTargetList intelTargetList(const game::Rules& r, const game::GameState& s, game::EmpireId me, const game::IntelProjectOrder& order,
                                int stage);
// The line of a project's box that names its target (spec 05 §2.1, confirmed:
// binary): for a project with a second list the target picked, or "Any Ship",
// "Any Planet", "Any Technology", "Any Empire"; empty for the others.
std::string intelTargetLine(const game::Rules& r, const game::GameState& s, const game::IntelProjectOrder& order);

// ---- Statistics -------------------------------------------------------------------------------

enum class Metric : uint8_t { Score, Resources, Research, Intelligence, TechLevels, Systems, Planets, Population, Units, Ships, Bases, Count };
std::string_view metricName(Metric m);
int64_t metricValue(const game::TurnStats& t, Metric m);
// Statistics of an empire over time: its recorded history plus the current turn.
std::vector<game::TurnStats> statsSeries(const game::Rules& r, const game::GameState& s, game::EmpireId e);
// The Comparisons window's rows for `empire` (spec 05 §5, confirmed: binary):
// only those of the viewer's statistics record (Empire::statistics), by
// turn, so a turn when the empire was not visible is a gap. A game saved
// before the record existed (it is empty while the viewer's history is not)
// falls back to the empire's history while its score is visible.
std::vector<game::TurnStats> comparisonRows(const game::GameState& s, game::EmpireId viewer, game::EmpireId empire);
// The empires the Comparisons window lists: every one with a row
// (comparisonRows), destroyed ones and those no longer visible included, in
// empire order.
std::vector<game::EmpireId> comparisonEmpires(const game::GameState& s, game::EmpireId viewer);

// ---- Scores (spec 06 §1.5, confirmed: binary) --------------------------------------------------

// A figure of the Score window: thousands with one decimal and "K" ("12.3K"),
// millions from 1000K ("4.5M"). Population, kept in millions: billions with
// one decimal and "B", trillions from 1000B ("T").
std::string scoreFigure(int64_t v);
std::string populationFigure(int64_t millions);
// A rank as "1st", "2nd", "3rd", else the number and "th" ("4th", "21th").
std::string rankText(int rank);
// Dense ranks of the scores, best first: equal scores share a rank and the
// next score gets the next number.
std::vector<int> denseRanks(const std::vector<int64_t>& scores);

// ---- Victory Conditions (spec 06 §1.5, confirmed: binary) ---------------------------------------

// Whether the Victory Conditions window gives `other` a column for `viewer`:
// the viewer always, a destroyed empire never, everyone once the game is
// over; otherwise the Score Display option read one step off, as the
// original reads it: "own empire only" shows the empires we hold
// Non-Aggression or better with, and "Non-Aggression partners" shows all.
bool victoryColumnVisible(const game::GameState& s, game::EmpireId viewer, game::EmpireId other);
// The window's rows, in its order.
enum class VictoryRow : uint8_t { Score, Years, PercentOfSecond, Tech, Peace, Delay, Count };
// Whether the condition is set in this game (an unset row shows its title only).
bool victoryRowOn(const game::VictoryConditions& v, VictoryRow row);
// Whether the window marks `e` with an "X" in `row`: its score has reached
// the value; it has the percentage of every other living empire's score; its
// capped tech levels reach the share of its maximum. The years, the peace
// and the "no victory before" qualifier are game-wide: once met (the date
// has come, the peace counter is full, the date has passed) every shown
// empire is marked. Nothing is marked in a row that is off.
bool victoryMark(const game::Rules& r, const game::GameState& s, VictoryRow row, game::EmpireId e);

// Timeline lines derived from consecutive TurnStats ("Colonized 2 planets").
struct HistoryEvent {
    uint32_t turn = 0;
    std::string text;
};
std::vector<HistoryEvent> statsEvents(const std::vector<game::TurnStats>& series);

// ---- History window (spec 05 §3.4) ------------------------------------------------------------

// The empire lists the History window offers `viewer`: itself, the empires
// it knows, and the empires its record mentions (destroyed ones included), in
// empire order. The Other Events list comes after these.
std::vector<game::EmpireId> historyEmpires(const game::GameState& s, game::EmpireId viewer);

struct HistoryLine {
    uint32_t turn = 0;
    std::string text;
    std::optional<game::Location> where;
};
// One list of the viewer's History window, oldest first, as the original
// reads its file (spec 06 §4.3, confirmed: binary): the record's entries
// about `empire` (invalid: the Other Events list). An empire's list also
// starts with its founding and, with `stats`, the changes its statistics show
// (statsEvents), both OpenSE4's.
std::vector<HistoryLine> historyLines(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::EmpireId empire,
                                      bool stats);
// The decade heading the History window puts over a turn's lines: "2400 -
// 2410" (spec 06 §4.3, confirmed: binary).
std::string decadeHeading(uint32_t turn);

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
