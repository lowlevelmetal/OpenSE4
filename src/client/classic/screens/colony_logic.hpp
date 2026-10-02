#pragma once

// The game questions behind the Planets, Colonies, Construction Queues and
// Set Construction Queue windows (docs/spec/06 §1.2, spec 02 §6 and §11).
// Read-only over the engine state and free of ImGui, so the tests can use it.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// ---- Colonization ---------------------------------------------------------------------------

// Planet surfaces the empire has a colony module for (researched components
// with a "Colonize Planet - <Type>" ability).
struct ColonizeTech {
    bool rock = false;
    bool ice = false;
    bool gas = false;
    bool allows(std::string_view surface) const;  // "Rock", "Ice", "Gas Giant"
};
ColonizeTech colonizeTech(const game::Rules& r, const game::Empire& e);

// The planet's colony as the empire sees it: its own always; another
// empire's only while the detection rule shows it (sight::canSeeColony, spec
// 01 §6.9), so a colony in a system without our sensors looks uncolonized.
const game::Colony* seenColony(const game::Rules& r, const game::GameState& s, game::EmpireId e, game::ObjectId planet);

// Whether the empire's race breathes the planet's atmosphere (a colony would not be domed).
bool breathableBy(const game::GameState& s, game::EmpireId e, const game::SpaceObject& planet);

// Why the empire cannot colonize the planet now; empty when it can. Checks
// that the planet is a free planet as the empire sees it (a colony it does
// not see is not checked: seenColony), the colony tech, and the game options
// "only breathable" and "only home planet type" (spec 02 §2). The map's
// colonize star and the Planets window use it (spec 01 §6.9).
std::string colonizeProblem(const game::Rules& r, const game::GameState& s, game::EmpireId e, game::ObjectId planet,
                            const ColonizeTech& tech);

// The planet's surface is one the empire may colonize (spec 06 §1.8.1
// "Colonizable"): a planet (not an asteroid field) of a type it has a colony
// module for, within the game options "only breathable" and "only home planet
// type". It says nothing about whether the planet is already colonized.
bool colonizableType(const game::GameState& s, game::EmpireId e, game::ObjectId planet, const ColonizeTech& tech);

// ---- Colony ships (spec 06 §1.8.1) ------------------------------------------------------------

// One of our ships (or bases) with a Colonize Planet ability. It is available
// when it has no orders (nor has its fleet), is not mothballed and still has
// supplies. A colony ship with orders has the planet of its last Colonize
// order as its target.
struct ColonyShip {
    game::VehicleId id;
    std::string name;
    bool available = false;
    game::ObjectId target;  // invalid without a Colonize order
};
std::vector<ColonyShip> colonyShips(const game::Rules& r, const game::GameState& s, game::EmpireId e);

// Send Colony Ship (spec 06 §7 Q25, confirmed: binary): among the available
// colony ships that can colonize the planet's type (in a turn-based game
// only those with movement left), the one with the shortest route to it
// (the first such ship in vehicle order on a tie). Nothing when none can go.
// Whether the planet is already colonized is not checked.
std::optional<game::VehicleId> chooseColonyShip(const game::Rules& r, const game::GameState& s, game::EmpireId e, game::ObjectId planet);
// Its orders: Load Cargo of population where it is (only when it has cargo
// space and carries no population), Move To the planet, Colonize.
game::cmd::SetOrders sendColonyShipOrders(const game::Rules& r, const game::GameState& s, game::VehicleId ship, game::ObjectId planet);

// ---- Planets window (spec 06 §1.8.1) ----------------------------------------------------------

// The ten tabs, in the button column's order (InterfaceOptions::planetsTab).
enum class PlanetFilter : uint8_t {
    All, Colonizable, AllColonies, EnemyColonies, AllyColonies, ColonizableEmpty, ColonizableBreathable, ShipEnroute, Asteroids, Special,
    Count
};

// What the planet list needs to know about one planet or asteroid field.
struct PlanetInfo {
    game::ObjectId id;
    game::SystemId system;
    bool asteroids = false;
    bool colonized = false;       // the planet itself holds a colony we see
    game::EmpireId owner;
    bool own = false;
    bool enemy = false;           // another empire below Non-Aggression with us, or not met yet
    bool ally = false;            // another empire at Non-Aggression or better
    bool colonizable = false;     // colonizableType()
    bool breathable = false;      // our race breathes the atmosphere
    bool special = false;         // Ancient Ruins or Ancient Ruins Unique
    bool enroute = false;         // the target of one of our colony ships
    std::string enrouteShip;      // that ship's name
    bool avoided = false;         // the system is on our Systems To Avoid list
    int sizeRank = 0;             // the planet size's place in the data set's size list (smallest first)
    std::string problem;          // why we cannot colonize it now (empty: we can)
};

// Every planet and asteroid field in the systems the empire has explored
// that it sees (sight::canSeePlanet: a colony's cloak, a storm or a nebula
// can hide one). A colony counts only while the empire sees it
// (seenColony); otherwise the planet is listed as uncolonized (spec 06
// §1.8.1, spec 01 §6.9).
std::vector<PlanetInfo> surveyPlanets(const game::Rules& r, const game::GameState& s, game::EmpireId e);
bool matches(PlanetFilter f, const PlanetInfo& p);

// The statistics box: counts over every planet (no asteroid fields) of the
// explored systems with its real owner and no sight test, so they include
// planets the tabs leave out or list as uncolonized (spec 06 §1.8.1,
// confirmed: binary).
struct PlanetStatistics {
    int systems = 0;            // known (explored) systems
    int planets = 0;            // planets, no asteroid fields
    int colonizable = 0;        // of a colonizable type
    int enemy = 0;              // colonizable and owned by an enemy
    int ally = 0;               // colonizable and owned by an ally
    int nonAligned = 0;         // always 0: the treaty test puts every empire in one of the groups above
    int free = 0;               // colonizable and not colonized
    int freeBreathable = 0;     // and breathable
    int colonyShips = 0;
    int available = 0;          // colony ships that are available
};
PlanetStatistics planetStatistics(const game::Rules& r, const game::GameState& s, game::EmpireId e, const std::vector<ColonyShip>& ships);

// Sorting by the latest header clicks (spec 06 §7 Q24, confirmed: binary).
// Each list window keeps five slots of column numbers with the empire
// (InterfaceOptions::planetsSort and the others): a slot holds column + 1,
// 0 is empty, and all-empty stands for the window's start, Name alone. A
// click shifts every slot down by one, drops the fifth and puts the clicked
// column first; an earlier copy of the same column stays (so a repeated
// click pushes the oldest key out). Empty slots are skipped. Each column
// sorts in its own fixed direction; a second click never reverses it.
using SortSlots = std::array<uint8_t, 5>;
// The keys in order, newest first: the Name column alone while every slot is empty.
std::vector<int> sortKeys(const SortSlots& slots, int nameColumn);
SortSlots clickSort(SortSlots slots, int column, int nameColumn);
// Sorts by the keys; `compare(column, a, b)` is negative, zero or positive in
// the column's own direction. (Stable, which the original is not: harmless.)
template <class Row, class Compare>
void sortByKeys(std::vector<Row>& rows, const std::vector<int>& keys, Compare&& compare) {
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        for (int c : keys)
            if (const int d = compare(c, a, b); d != 0) return d < 0;
        return false;
    });
}
// Case-insensitive comparison of names (A to Z): negative, zero or positive.
int compareNames(std::string_view a, std::string_view b);

// ---- Colonies ------------------------------------------------------------------------------------

// Status icon numbers (docs/spec/06 §4.4, 1-based) that apply to an own colony.
std::vector<int> colonyStatusIcons(const game::Rules& r, const game::GameState& s, const game::Colony& c, bool connected);

// Scrap commands that remove every facility of one type from the given
// colonies (all own colonies when `only` is empty). Slots are listed from the
// highest down, so the commands stay valid when applied in order.
std::vector<game::cmd::Scrap> scrapFacilityType(const game::GameState& s, game::EmpireId e, uint32_t facility,
                                                const std::vector<game::ObjectId>& only = {});


// ---- The Colonies list's columns (spec 06 §1.8.3, confirmed: binary) ----------------------------

enum class ColonyTab : uint8_t { General, Value, Production, Facilities, Cargo, Construction, Status, Races, Orders, Count };

// Every column of the Colonies window keeps one identity in every tab, and a
// sort key is that identity (InterfaceOptions::coloniesSort holds it + 1),
// so a key goes on sorting by its quantity while another tab is shown. Only
// the picture and Name appear in every tab; Name keeps the number 1 that the
// window's key lists always gave it.
enum class ColonyColumn : uint8_t {
    Picture, Name,
    Atmosphere, Conditions, Population, Mood,                                // General
    ColonyType, MineralsValue, OrganicsValue, RadioactivesValue,             // Value
    Minerals, Organics, Radioactives, Research, Intelligence,                // Production
    FacilitiesBuilt, FacilitySlots, FacilityList,                            // Facilities
    CargoUsed, CargoCapacity, CargoItems,                                    // Cargo
    UnderConstruction, TimeRemaining,                                        // Construction
    Status,                                                                  // Status
    RacePopulation,                                                          // Races
    Orders,                                                                  // Orders
    Count
};
// The columns a tab shows after the picture and Name, in order.
std::vector<ColonyColumn> colonyTabColumns(ColonyTab tab);
// Whether a click on the column sorts anything: Facilities, Cargo Items,
// Status and Orders have no key (a click still takes the first slot).
bool colonyColumnSorts(ColonyColumn c);

// What a colony is sorted by, whatever tab is shown.
struct ColonySortValues {
    std::string name;
    int sizeRank = 0;                 // the planet size's number in the data set (picture)
    std::string atmosphere;
    game::Conditions conditions;
    int64_t population = 0;
    std::string mood;                 // the mood word; empty without population
    std::string colonyType;
    std::array<int64_t, 3> value{};   // the planet's value percentages
    game::Resources production;
    int64_t research = 0, intelligence = 0;
    int facilities = 0, slots = 0;
    int64_t cargoUsed = 0, cargoCapacity = 0;
    std::string underConstruction;    // underConstructionText
    std::string timeRemaining;        // timeRemainingText
};
ColonySortValues colonySortValues(const game::Rules& r, const game::GameState& s, const game::Colony& c);
// Negative, zero or positive in the column's own direction: Name A to Z
// ignoring case; the picture by planet size, smallest first; Atmosphere, Mood
// and Colony Type A to Z by character code; Conditions best first; Under
// Construction and Time Remaining Z to A by character code; every number
// highest first. Zero for a column without a key, or an unknown one.
int compareColonies(ColonyColumn c, const ColonySortValues& a, const ColonySortValues& b);
// The column a stored key stands for (slot value − 1); nullopt when unknown.
std::optional<ColonyColumn> colonyColumnOf(int key);
// The rows' order under the stored keys (newest first; Name alone while no
// key is stored), as indices into `rows`. An unknown key sorts nothing.
std::vector<size_t> colonyRowOrder(const std::vector<ColonySortValues>& rows, const SortSlots& slots);

// ---- Construction queues -------------------------------------------------------------------------

// Which of the Construction Queues window's four toggles shows a queue (spec
// 06 §1.8.2, confirmed: binary): the split is by whether the space yard works
// right now, not by hull.
enum class QueueKind : uint8_t {
    Ship,         // "Ships": a ship or base whose yard does not work now (cloaked, or the yard is gone);
                  // its queue is cleared at its next update, so this is normally empty
    Planet,       // "Planets": a colony without a working space yard
    ShipYard,     // "Ship SY": a ship or base with a working space yard
    PlanetYard,   // "Planet SY": a colony with a working space yard
};
// The bit of InterfaceOptions::queuesShown for each toggle (the order above).
constexpr uint8_t queueKindBit(QueueKind k) { return uint8_t(1u << static_cast<unsigned>(k)); }

struct QueueEntry {
    game::cmd::QueueTarget target;
    QueueKind kind = QueueKind::Planet;
    game::Location where;
    std::string name;
};

// Every queue the empire has (spec 06 §1.8.2, §7 Q27): all own colonies, then
// its ships and bases that are not mothballed and have a working space yard
// ("Ship SY"), or whose yard stopped working while their queue still holds
// items ("Ships": the original empties such a queue at once, so in practice
// this group is empty). Mothballed vehicles are never listed.
std::vector<QueueEntry> empireQueues(const game::Rules& r, const game::GameState& s, game::EmpireId e);
// A vehicle's space yard works: an intact Space Yard part, not mothballed, not cloaked.
bool workingVehicleYard(const game::Rules& r, const game::GameState& s, const game::Vehicle& v);
// Whether a queue can take ships and bases (`units` false) or units: a colony
// with population (ships only with a space yard), or a vehicle with a working yard.
bool queueCanBuild(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t, bool units);
const game::ConstructionQueue* queueOf(const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t);
bool sameTarget(const game::cmd::QueueTarget& a, const game::cmd::QueueTarget& b);

// An item as the queue lists and reports name it (spec 06 §1.8.2, §7 Q48):
// the design or facility name, upgrades as "Upg. <facility>", and
// " x <count>" after it when the count is above 1, for every kind of item.
std::string queueItemName(const game::Rules& r, const game::GameState& s, const game::QueueItem& item);
// The name alone, without the count.
std::string queueItemBaseName(const game::Rules& r, const game::GameState& s, const game::QueueItem& item);

// The item's full cost from the economy; when the economy reports nothing
// (not yet computed), the base price of the design or facility.
game::Resources displayCost(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t,
                            const game::QueueItem& item);

struct ItemEstimate {
    game::Resources cost;
    game::Resources remaining;
    int turns = -1;    // for this item alone, once it reaches the top (-1 = never)
    int doneIn = -1;   // counting the items ahead of it (-1 = never)
};
// Build-time estimates for every item of a queue at a per-turn rate.
std::vector<ItemEstimate> estimateQueue(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t,
                                        const game::ConstructionQueue& q, const game::Resources& rate);
// What the queue will spend this turn (the top item, capped by the rate).
game::Resources queueUsage(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t,
                           const game::ConstructionQueue& q, const game::Resources& rate);

// Time Remaining (spec 06 §7 Q48, confirmed: binary): the first item with
// its whole count. For each resource whose construction rate is above 0,
// ceil(what is left / rate) turns; the largest of these. kNeverTurns when all
// three rates are 0.
inline constexpr int kNeverTurns = 9999;
int timeRemainingTurns(const game::Resources& remaining, const game::Resources& rate);
// Turns as years, one turn being 0.1 year: "0.3 years"; 0 turns shows as one
// turn ("0.1 years"); "Never" at kNeverTurns or more.
std::string queueYearsText(int turns);
// The text the planet and ship reports, the Colonies list and the
// Construction Queues rows show: nothing for an empty queue, "On Hold" for a
// held one, else the first item's time in years.
std::string timeRemainingText(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t,
                              const game::ConstructionQueue& q, const game::Resources& rate);
// "Under Construction": "None", or the first item's name (queueItemName).
std::string underConstructionText(const game::Rules& r, const game::GameState& s, const game::ConstructionQueue& q);
// The queue's build-mode note (the yellow line under the tab value): empty
// when the queue builds normally.
std::string queueModeNote(const game::ConstructionQueue& q);

// Multi-Add (spec 06 §1.8.2): every item placed in the temporary queue, in
// order and with its count, appended to each tagged queue.
std::vector<game::cmd::QueueAdd> multiAddCommands(const std::vector<game::cmd::QueueTarget>& tagged, const std::vector<game::QueueItem>& items);

// The "Note" after a facility is added by hand to a planet's queue (spec 06
// §7 Q29, confirmed: binary): the abilities of `facility`, among the 18
// system-wide ones that count (similarAbilityCounts), that an object of ours
// in the planet's system already has: a built facility of one of our
// colonies (the queue's own included) or a component of one of our ships
// or bases. Queued facilities do not count. Empty: no note.
std::vector<std::string> similarSystemAbilities(const game::Rules& r, const game::GameState& s, game::EmpireId e, game::ObjectId planet,
                                                uint32_t facility);
bool similarAbilityCounts(game::AbilityKind k);

// Reorder Queue (spec 06 §7 Q28): the moves (from, to), applied in turn with
// cmd::QueueMove, that put the items in `order` (indices into the queue as it
// is now, as the reorder list returns them).
std::vector<std::pair<uint32_t, uint32_t>> reorderMoves(const std::vector<size_t>& order);

// Upgrade items this colony could queue: one per facility family that has an
// older level than the newest one researched, skipping families already queued.
std::vector<game::QueueItem> possibleUpgrades(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::Colony& c);
// Queued facility items of an older level than the newest one researched in
// their family, with that newest level: what Upgrade Facilities switches in
// place (spec 02 §6.6, cmd::QueueReplaceFacility). Pairs of (item index, facility).
std::vector<std::pair<uint32_t, uint32_t>> queuedFacilitySwitches(const game::Rules& r, const game::GameState& s, game::EmpireId e,
                                                                  const game::Colony& c);

// Researched facilities; with onlyLatest, the newest level of each family.
std::vector<uint32_t> facilityChoices(const game::Rules& r, const game::Empire& e, bool onlyLatest);
// Own designs for the Ships tab (ships and bases) or the Units tab; with
// onlyLatest, obsolete designs are left out.
std::vector<game::DesignId> designChoices(const game::Rules& r, const game::GameState& s, game::EmpireId e, bool units, bool onlyLatest);
bool isUnitDesign(const game::Rules& r, const game::Design& d);

// Free facility slots after what is built and queued (planets only).
int freeFacilitySlots(const game::Rules& r, const game::GameState& s, const game::Colony& c);

// ---- Queue types (named templates, spec 02 §6.4) -------------------------------------------------

struct QueueTemplateEntry {
    game::QueueItem::Kind kind = game::QueueItem::Kind::Facility;
    std::string name;  // facility or design name
    int count = 1;
};

struct QueueTemplate {
    std::string name;
    std::vector<QueueTemplateEntry> entries;
    // Built-in types fill the free facility slots with the best researched
    // facilities having these abilities, taken in turn.
    std::vector<game::AbilityKind> fill;
    bool builtIn() const { return !fill.empty(); }
};

std::vector<QueueTemplate> builtInQueueTemplates();
// The items a template adds to a queue, resolved against current technology:
// facilities become the newest researched level of their family, unknown
// names are skipped. Items are not validated; the queue commands do that.
std::vector<game::QueueItem> resolveTemplate(const game::Rules& r, const game::GameState& s, game::EmpireId e,
                                             const game::cmd::QueueTarget& t, const QueueTemplate& tmpl);
QueueTemplate templateFromQueue(const game::Rules& r, const game::GameState& s, std::string name, const game::ConstructionQueue& q);

// Our own plain-text format for the user's saved queue types.
std::string serializeTemplates(const std::vector<QueueTemplate>& list);
std::vector<QueueTemplate> parseTemplates(std::string_view text);

} // namespace opense4::client::classic
