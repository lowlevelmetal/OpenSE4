#pragma once

// The game questions behind the Planets, Colonies, Construction Queues and
// Set Construction Queue windows (docs/spec/06 §1.2, spec 02 §6 and §11).
// Read-only over the engine state and free of ImGui, so the tests can use it.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"

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

// Whether the empire's race breathes the planet's atmosphere (a colony would not be domed).
bool breathableBy(const game::GameState& s, game::EmpireId e, const game::SpaceObject& planet);

// Why the empire cannot colonize the planet now; empty when it can. Checks
// that the planet is a free planet, the colony tech, and the game options
// "only breathable" and "only home planet type" (spec 02 §2).
std::string colonizeProblem(const game::Rules& r, const game::GameState& s, game::EmpireId e, game::ObjectId planet,
                            const ColonizeTech& tech);

// An own vehicle or fleet already has a Colonize order for the planet.
bool colonyShipEnroute(const game::GameState& s, game::EmpireId e, game::ObjectId planet);

// The nearest idle own ship (no orders, not in a fleet, not mothballed) that
// can colonize the planet's surface. Nearest by movement ETA when the engine
// can route, else by straight-line distance.
std::optional<game::VehicleId> findColonyShip(const game::Rules& r, const game::GameState& s, game::EmpireId e, game::ObjectId planet);
// The order that sends a ship to colonize a planet.
game::cmd::SetOrders colonizeOrders(const game::GameState& s, game::VehicleId ship, game::ObjectId planet);

// ---- Planets window -----------------------------------------------------------------------------

enum class PlanetFilter : uint8_t {
    All, Colonizable, AllColonies, EnemyColonies, AllyColonies, ColonizableEmpty, ColonizableBreathable, ShipEnroute, Asteroids, Special,
    Count
};

// What the planet list needs to know about one planet or asteroid field.
struct PlanetInfo {
    game::ObjectId id;
    game::SystemId system;
    bool asteroids = false;
    bool colonized = false;
    game::EmpireId owner;
    bool own = false;
    bool enemy = false;           // colony of an empire we fight on contact
    bool ally = false;            // colony of another empire we do not fight
    bool colonizable = false;     // colonizeProblem() is empty
    bool breathable = false;      // our race breathes the atmosphere
    bool special = false;         // has stellar abilities (ruins, value bonuses, ...)
    bool enroute = false;         // an own ship has orders to colonize it
    bool foreignSystem = false;   // another empire has a colony in the same system
    bool avoided = false;         // the system is on our Systems To Avoid list
    std::string problem;          // why it is not colonizable
};

// Every planet and asteroid field in the systems the empire has explored.
std::vector<PlanetInfo> surveyPlanets(const game::Rules& r, const game::GameState& s, game::EmpireId e);
bool matches(PlanetFilter f, const PlanetInfo& p);

// ---- Colonies ------------------------------------------------------------------------------------

// Status icon numbers (docs/spec/06 §4.4, 1-based) that apply to an own colony.
std::vector<int> colonyStatusIcons(const game::Rules& r, const game::GameState& s, const game::Colony& c, bool connected);

// Scrap commands that remove every facility of one type from the given
// colonies (all own colonies when `only` is empty). Slots are listed from the
// highest down, so the commands stay valid when applied in order.
std::vector<game::cmd::Scrap> scrapFacilityType(const game::GameState& s, game::EmpireId e, uint32_t facility,
                                                const std::vector<game::ObjectId>& only = {});

// Short descriptions of this turn's orders that concern a planet.
std::vector<std::string> planetOrders(const std::vector<game::Command>& orders, game::ObjectId planet);

// ---- Construction queues -------------------------------------------------------------------------

enum class QueueKind : uint8_t {
    Planet,       // a colony without a space yard (facilities and units only)
    PlanetYard,   // a colony with a space yard
    Ship,         // a mobile ship with a space yard component
    Base,         // a stationary vehicle (base) with a space yard component
};

struct QueueEntry {
    game::cmd::QueueTarget target;
    QueueKind kind = QueueKind::Planet;
    game::Location where;
    std::string name;
};

// Every queue the empire has: all own colonies, then vehicles with a space yard.
std::vector<QueueEntry> empireQueues(const game::Rules& r, const game::GameState& s, game::EmpireId e);
const game::ConstructionQueue* queueOf(const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t);
bool sameTarget(const game::cmd::QueueTarget& a, const game::cmd::QueueTarget& b);

std::string queueItemName(const game::Rules& r, const game::GameState& s, const game::QueueItem& item);

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
