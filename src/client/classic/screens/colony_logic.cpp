#include "client/classic/screens/colony_logic.hpp"

#include "client/classic/status_icons.hpp"

#include "datafile/datafile.hpp"
#include "datafile/reader.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <type_traits>

namespace opense4::client::classic {

using datafile::keysEqual;

// ---- Colonization ---------------------------------------------------------------------------

bool ColonizeTech::allows(std::string_view surface) const {
    if (keysEqual(surface, "Rock")) return rock;
    if (keysEqual(surface, "Ice")) return ice;
    if (keysEqual(surface, "Gas Giant") || keysEqual(surface, "Gas")) return gas;
    return false;
}

ColonizeTech colonizeTech(const game::Rules& r, const game::Empire& e) {
    ColonizeTech t;
    for (uint32_t i = 0; i < r.data().components.size(); ++i) {
        const auto ab = r.componentAbilities(i);
        const bool rock = game::hasAbility(ab, game::AbilityKind::ColonizeRock);
        const bool ice = game::hasAbility(ab, game::AbilityKind::ColonizeIce);
        const bool gas = game::hasAbility(ab, game::AbilityKind::ColonizeGas);
        if (!(rock || ice || gas) || !r.componentAvailable(e, i)) continue;
        t.rock = t.rock || rock;
        t.ice = t.ice || ice;
        t.gas = t.gas || gas;
    }
    return t;
}

bool breathableBy(const game::GameState& s, game::EmpireId e, const game::SpaceObject& planet) {
    return e.valid() && keysEqual(s.empire(e).race.atmosphere, planet.atmosphere);
}

std::string colonizeProblem(const game::Rules&, const game::GameState& s, game::EmpireId e, game::ObjectId planet, const ColonizeTech& tech) {
    const game::SpaceObject& o = s.galaxy.object(planet);
    if (o.kind != game::ObjectKind::Planet) return "Only planets can be colonized";
    if (const game::Colony* c = s.colony(planet)) return c->owner == e ? "Already our colony" : "Already colonized";
    if (!tech.allows(o.surface)) return std::format("No colony module for {} planets yet", o.surface);
    const game::Race& race = s.empire(e).race;
    if (s.options.onlyBreathable && !breathableBy(s, e, o))
        return std::format("This game allows only {} planets", race.atmosphere.empty() ? std::string("breathable") : race.atmosphere);
    if (s.options.onlyHomeType && !keysEqual(o.surface, race.nativeSurface))
        return std::format("This game allows only {} planets", race.nativeSurface);
    return {};
}

bool colonizableType(const game::GameState& s, game::EmpireId e, game::ObjectId planet, const ColonizeTech& tech) {
    const game::SpaceObject& o = s.galaxy.object(planet);
    if (o.kind != game::ObjectKind::Planet || !tech.allows(o.surface)) return false;
    const game::Race& race = s.empire(e).race;
    if (s.options.onlyBreathable && !breathableBy(s, e, o)) return false;
    if (s.options.onlyHomeType && !keysEqual(o.surface, race.nativeSurface)) return false;
    return true;
}

// ---- Colony ships ------------------------------------------------------------------------------

namespace {

// Per design: its statistics, worked out once.
class DesignStatsCache {
public:
    DesignStatsCache(const game::Rules& r, const game::GameState& s) : r_(r), s_(s) {}
    const game::DesignStats& operator()(game::DesignId d) {
        auto [it, inserted] = stats_.try_emplace(d);
        if (inserted) it->second = game::computeDesignStats(r_, nullptr, s_.design(d));
        return it->second;
    }

private:
    const game::Rules& r_;
    const game::GameState& s_;
    std::map<game::DesignId, game::DesignStats> stats_;
};

bool colonyShipDesign(const game::DesignStats& st) {
    return !game::isUnitType(st.vehicleType) && (st.canColonizeRock || st.canColonizeIce || st.canColonizeGas);
}

} // namespace

std::vector<ColonyShip> colonyShips(const game::Rules& r, const game::GameState& s, game::EmpireId e) {
    std::vector<ColonyShip> out;
    DesignStatsCache stats(r, s);
    for (const game::Vehicle& v : s.vehicles) {
        if (v.owner != e || v.count <= 0 || !colonyShipDesign(stats(v.design))) continue;
        ColonyShip c;
        c.id = v.id;
        c.name = v.name;
        // "No orders": a fleet member holds copies of its fleet's orders (spec 03 §19 Q65).
        c.available = v.orders.empty() && v.status != game::VehicleStatus::Mothballed && v.supply > 0;
        for (const game::Order& o : v.orders)
            if (o.kind == game::OrderKind::Colonize && o.object.valid()) c.target = o.object;
        out.push_back(std::move(c));
    }
    return out;
}

std::optional<game::VehicleId> chooseColonyShip(const game::Rules& r, const game::GameState& s, game::EmpireId e, game::ObjectId planet) {
    const game::SpaceObject& p = s.galaxy.object(planet);
    const game::Location target{p.system, p.sector};
    DesignStatsCache stats(r, s);
    std::optional<game::VehicleId> best;
    int bestLength = std::numeric_limits<int>::max();
    for (const ColonyShip& c : colonyShips(r, s, e)) {
        if (!c.available) continue;
        const game::Vehicle& v = *s.vehicle(c.id);
        if (!stats(v.design).canColonize(p.surface)) continue;
        if (!s.options.simultaneous && v.movement <= 0) continue;  // turn-based: movement left
        const auto path = game::movement::findPath(r, s, e, v.location, target);
        if (!path) continue;
        if (path->length < bestLength) {
            bestLength = path->length;
            best = c.id;
        }
    }
    return best;
}

game::cmd::SetOrders sendColonyShipOrders(const game::Rules& r, const game::GameState& s, game::VehicleId ship, game::ObjectId planet) {
    game::cmd::SetOrders c;
    c.vehicle = ship;
    const game::Vehicle* v = s.vehicle(ship);
    const game::Location where = game::locationOf(s.galaxy, planet);
    if (v && v->cargo.totalPopulation() == 0 && game::vehicleCargoCapacity(r, s, *v) > 0) {
        game::Order load{game::OrderKind::LoadCargo, v->location};
        load.amount = -1;  // population: as much as fits
        c.orders.push_back(load);
    }
    c.orders.push_back(game::Order{game::OrderKind::MoveTo, where});
    game::Order colonize{game::OrderKind::Colonize, where};
    colonize.object = planet;
    c.orders.push_back(colonize);
    return c;
}

// ---- Planets window -----------------------------------------------------------------------------

std::vector<PlanetInfo> surveyPlanets(const game::Rules& r, const game::GameState& s, game::EmpireId e) {
    std::vector<PlanetInfo> out;
    const game::Empire& me = s.empire(e);
    const ColonizeTech tech = colonizeTech(r, me);
    // The targets of our colony ships' last Colonize orders.
    std::map<game::ObjectId, std::string> enroute;
    for (const ColonyShip& c : colonyShips(r, s, e))
        if (c.target.valid()) enroute.try_emplace(c.target, c.name);
    const auto& sizes = r.data().planetSizes;
    auto rankOf = [&](const game::SpaceObject& o) {
        const ruleset::PlanetSize* ps = game::planetSize(r, o);
        return ps ? int(ps - sizes.data()) : int(sizes.size());
    };
    auto atLeast = [](game::Treaty t, game::Treaty floor) { return static_cast<uint8_t>(t) >= static_cast<uint8_t>(floor); };

    for (const game::StarSystem& sys : s.galaxy.systems) {
        if (!me.hasExplored(sys.id)) continue;
        const bool avoided = std::find(me.systemsToAvoid.begin(), me.systemsToAvoid.end(), sys.id) != me.systemsToAvoid.end();
        for (game::ObjectId id : sys.objects) {
            const game::SpaceObject& o = s.galaxy.object(id);
            if (o.kind != game::ObjectKind::Planet && o.kind != game::ObjectKind::Asteroids) continue;
            // A cloaked colony the empire cannot see leaves its planet out; when
            // it is seen it is listed as a colony (spec 06 §1.8.1, spec 01 §6.9).
            if (!game::sight::colonyShown(r, s, e, id)) continue;
            PlanetInfo p;
            p.id = id;
            p.system = sys.id;
            p.asteroids = o.kind == game::ObjectKind::Asteroids;
            if (const game::Colony* c = s.colony(id)) {
                p.colonized = true;
                p.owner = c->owner;
                p.own = c->owner == e;
                if (!p.own && c->owner.valid() && c->owner.index() < s.empires.size()) {
                    // Ally: Non-Aggression or better; every other empire, one not met
                    // yet included, is an enemy (spec 06 §1.8.1, confirmed: binary).
                    const game::Relation& rel = me.relation(c->owner);
                    p.ally = rel.contact && atLeast(rel.treaty, game::Treaty::NonAggression);
                    p.enemy = !p.ally;
                }
            }
            p.colonizable = colonizableType(s, e, id, tech);
            p.problem = colonizeProblem(r, s, e, id, tech);
            p.breathable = !p.asteroids && breathableBy(s, e, o);
            for (const ruleset::Ability& a : o.abilities)
                if (const auto k = game::parseAbilityKind(a.type); k == game::AbilityKind::AncientRuins || k == game::AbilityKind::AncientRuinsUnique)
                    p.special = true;
            if (auto it = enroute.find(id); it != enroute.end()) {
                p.enroute = true;
                p.enrouteShip = it->second;
            }
            p.avoided = avoided;
            p.sizeRank = rankOf(o);
            out.push_back(std::move(p));
        }
    }
    return out;
}

bool matches(PlanetFilter f, const PlanetInfo& p) {
    switch (f) {
        case PlanetFilter::All: return !p.asteroids;
        case PlanetFilter::Colonizable: return p.colonizable;
        case PlanetFilter::AllColonies: return p.colonized;
        case PlanetFilter::EnemyColonies: return p.colonized && p.enemy;
        case PlanetFilter::AllyColonies: return p.colonized && p.ally;
        case PlanetFilter::ColonizableEmpty: return p.colonizable && !p.colonized;
        case PlanetFilter::ColonizableBreathable: return p.colonizable && !p.colonized && p.breathable;
        case PlanetFilter::ShipEnroute: return p.enroute;
        case PlanetFilter::Asteroids: return p.asteroids;
        case PlanetFilter::Special: return p.special;
        case PlanetFilter::Count: break;
    }
    return false;
}

PlanetStatistics planetStatistics(const game::GameState& s, game::EmpireId e, const std::vector<PlanetInfo>& planets,
                                  const std::vector<ColonyShip>& ships) {
    PlanetStatistics st;
    for (const game::StarSystem& sys : s.galaxy.systems) st.systems += s.empire(e).hasExplored(sys.id) ? 1 : 0;
    for (const PlanetInfo& p : planets) {
        if (p.asteroids) continue;
        ++st.planets;
        if (!p.colonizable) continue;
        ++st.colonizable;
        st.enemy += p.colonized && p.enemy;
        st.ally += p.colonized && p.ally;
        st.free += !p.colonized;
        st.freeBreathable += !p.colonized && p.breathable;
    }
    st.colonyShips = int(ships.size());
    for (const ColonyShip& c : ships) st.available += c.available;
    return st;
}

std::vector<int> sortKeys(const SortSlots& slots, int nameColumn) {
    std::vector<int> keys;
    for (uint8_t slot : slots)
        if (slot != 0) keys.push_back(int(slot) - 1);
    if (keys.empty()) keys.push_back(nameColumn);
    return keys;
}

SortSlots clickSort(SortSlots slots, int column, int nameColumn) {
    // A window that was never sorted starts with Name as its only key.
    if (std::all_of(slots.begin(), slots.end(), [](uint8_t x) { return x == 0; })) slots[0] = uint8_t(nameColumn + 1);
    for (size_t i = slots.size() - 1; i > 0; --i) slots[i] = slots[i - 1];
    slots[0] = uint8_t(std::clamp(column + 1, 1, 255));
    return slots;
}

int compareNames(std::string_view a, std::string_view b) {
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const int x = std::tolower(static_cast<unsigned char>(a[i])), y = std::tolower(static_cast<unsigned char>(b[i]));
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
}

// ---- Colonies ------------------------------------------------------------------------------------

std::vector<int> colonyStatusIcons(const game::Rules& r, const game::GameState& s, const game::Colony& c, bool connected) {
    // The icons the original draws, in its order (status_icons.hpp), as icon numbers (cell + 1).
    std::vector<int> icons = colonyStatusCells(r, s, c, connected);
    for (int& i : icons) ++i;
    return icons;
}

std::vector<game::cmd::Scrap> scrapFacilityType(const game::GameState& s, game::EmpireId e, uint32_t facility,
                                                const std::vector<game::ObjectId>& only) {
    std::vector<game::cmd::Scrap> out;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        if (!only.empty() && std::find(only.begin(), only.end(), c->planet) == only.end()) continue;
        for (size_t i = c->facilities.size(); i-- > 0;)
            if (c->facilities[i] == facility) out.push_back(game::cmd::Scrap{{}, c->planet, static_cast<int32_t>(i)});
    }
    return out;
}

// ---- The Colonies list's columns ---------------------------------------------------------------

std::vector<ColonyColumn> colonyTabColumns(ColonyTab tab) {
    using C = ColonyColumn;
    switch (tab) {
        case ColonyTab::General: return {C::Atmosphere, C::Conditions, C::Population, C::Mood};
        case ColonyTab::Value: return {C::ColonyType, C::MineralsValue, C::OrganicsValue, C::RadioactivesValue};
        case ColonyTab::Production: return {C::Minerals, C::Organics, C::Radioactives, C::Research, C::Intelligence};
        case ColonyTab::Facilities: return {C::FacilitiesBuilt, C::FacilitySlots, C::FacilityList};
        case ColonyTab::Cargo: return {C::CargoUsed, C::CargoCapacity, C::CargoItems};
        case ColonyTab::Construction: return {C::UnderConstruction, C::TimeRemaining};
        case ColonyTab::Status: return {C::Status};
        case ColonyTab::Races: return {C::RacePopulation};
        case ColonyTab::Orders: return {C::Orders};
        case ColonyTab::Count: break;
    }
    return {};
}

bool colonyColumnSorts(ColonyColumn c) {
    return c != ColonyColumn::FacilityList && c != ColonyColumn::CargoItems && c != ColonyColumn::Status && c != ColonyColumn::Orders &&
           c != ColonyColumn::Count;
}

std::optional<ColonyColumn> colonyColumnOf(int key) {
    if (key < 0 || key >= static_cast<int>(ColonyColumn::Count)) return std::nullopt;
    return static_cast<ColonyColumn>(key);
}

ColonySortValues colonySortValues(const game::Rules& r, const game::GameState& s, const game::Colony& c) {
    const game::SpaceObject& o = s.galaxy.object(c.planet);
    ColonySortValues v;
    v.name = o.name;
    const ruleset::PlanetSize* size = game::planetSize(r, o);
    v.sizeRank = size ? static_cast<int>(size - r.data().planetSizes.data()) : static_cast<int>(r.data().planetSizes.size());
    v.atmosphere = o.atmosphere;
    v.conditions = o.conditions;
    v.population = c.totalPopulation();
    if (v.population > 0) v.mood = std::string(game::economy::moodName(r, s, c));
    v.colonyType = c.colonyType;
    for (size_t i = 0; i < 3; ++i) v.value[i] = o.value[i];
    const game::economy::ColonyOutput out = game::economy::colonyOutput(r, s, c);
    v.production = out.production;
    v.research = out.research;
    v.intelligence = out.intelligence;
    v.facilities = static_cast<int>(c.facilities.size());
    v.slots = game::facilitySlots(r, s, c);
    v.cargoUsed = game::cargoSpaceUsed(r, s, c.cargo);
    v.cargoCapacity = game::colonyCargoCapacity(r, s, c);
    const game::cmd::QueueTarget target{c.planet, {}};
    v.underConstruction = underConstructionText(r, s, c.queue);
    v.timeRemaining = timeRemainingText(r, s, c.owner, target, c.queue, game::economy::constructionRate(r, s, c.owner, target));
    return v;
}

namespace {

template <class T>
int highestFirst(const T& a, const T& b) {
    return a == b ? 0 : a > b ? -1 : 1;
}
// By character code (case matters), A to Z.
int byCode(const std::string& a, const std::string& b) {
    const int d = a.compare(b);
    return d == 0 ? 0 : d < 0 ? -1 : 1;
}

} // namespace

int compareColonies(ColonyColumn c, const ColonySortValues& a, const ColonySortValues& b) {
    using C = ColonyColumn;
    switch (c) {
        case C::Picture: return a.sizeRank == b.sizeRank ? 0 : a.sizeRank < b.sizeRank ? -1 : 1;
        case C::Name: return compareNames(a.name, b.name);
        case C::Atmosphere: return byCode(a.atmosphere, b.atmosphere);
        case C::Conditions: return highestFirst(a.conditions, b.conditions);
        case C::Population: return highestFirst(a.population, b.population);
        case C::Mood: return byCode(a.mood, b.mood);
        case C::ColonyType: return byCode(a.colonyType, b.colonyType);
        case C::MineralsValue:
        case C::OrganicsValue:
        case C::RadioactivesValue: {
            const size_t i = static_cast<size_t>(c) - static_cast<size_t>(C::MineralsValue);
            return highestFirst(a.value[i], b.value[i]);
        }
        case C::Minerals:
        case C::Organics:
        case C::Radioactives: {
            const size_t i = static_cast<size_t>(c) - static_cast<size_t>(C::Minerals);
            return highestFirst(a.production.v[i], b.production.v[i]);
        }
        case C::Research: return highestFirst(a.research, b.research);
        case C::Intelligence: return highestFirst(a.intelligence, b.intelligence);
        case C::FacilitiesBuilt: return highestFirst(a.facilities, b.facilities);
        case C::FacilitySlots: return highestFirst(a.slots, b.slots);
        case C::CargoUsed: return highestFirst(a.cargoUsed, b.cargoUsed);
        case C::CargoCapacity: return highestFirst(a.cargoCapacity, b.cargoCapacity);
        case C::UnderConstruction: return -byCode(a.underConstruction, b.underConstruction);
        case C::TimeRemaining: return -byCode(a.timeRemaining, b.timeRemaining);
        case C::RacePopulation: return highestFirst(a.population, b.population);
        case C::FacilityList:
        case C::CargoItems:
        case C::Status:
        case C::Orders:
        case C::Count: break;
    }
    return 0;
}

std::vector<size_t> colonyRowOrder(const std::vector<ColonySortValues>& rows, const SortSlots& slots) {
    std::vector<size_t> order(rows.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    sortByKeys(order, sortKeys(slots, static_cast<int>(ColonyColumn::Name)), [&](int key, size_t a, size_t b) {
        const std::optional<ColonyColumn> c = colonyColumnOf(key);
        return c ? compareColonies(*c, rows[a], rows[b]) : 0;
    });
    return order;
}

// ---- Construction queues -------------------------------------------------------------------------

bool workingVehicleYard(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    return v.status != game::VehicleStatus::Cloaked && game::vehicleHasSpaceYard(r, s, v);
}

std::vector<QueueEntry> empireQueues(const game::Rules& r, const game::GameState& s, game::EmpireId e) {
    std::vector<QueueEntry> out;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        QueueEntry q;
        q.target.planet = c->planet;
        // A cloaked colony's yard does not work (spec 01 §6.9, spec 06 §1.8.2).
        q.kind = game::colonyHasWorkingYard(r, *c) ? QueueKind::PlanetYard : QueueKind::Planet;
        q.where = game::locationOf(s.galaxy, c->planet);
        q.name = s.galaxy.object(c->planet).name;
        out.push_back(std::move(q));
    }
    for (const game::Vehicle& v : s.vehicles) {
        if (v.owner != e || v.count <= 0 || v.status == game::VehicleStatus::Mothballed) continue;
        const bool working = workingVehicleYard(r, s, v);
        if (!working && v.queue.items.empty()) continue;
        QueueEntry q;
        q.target.vehicle = v.id;
        q.kind = working ? QueueKind::ShipYard : QueueKind::Ship;
        q.where = v.location;
        q.name = v.name;
        out.push_back(std::move(q));
    }
    return out;
}

bool queueCanBuild(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t, bool units) {
    if (t.vehicle.valid()) {
        const game::Vehicle* v = s.vehicle(t.vehicle);
        return v && v->owner == e && workingVehicleYard(r, s, *v);
    }
    const game::Colony* c = s.colony(t.planet);
    if (!c || c->owner != e || c->totalPopulation() == 0) return false;
    return units || game::colonyHasSpaceYard(r, *c);
}

const game::ConstructionQueue* queueOf(const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t) {
    return game::findQueue(const_cast<game::GameState&>(s), e, t);
}

bool sameTarget(const game::cmd::QueueTarget& a, const game::cmd::QueueTarget& b) { return a.planet == b.planet && a.vehicle == b.vehicle; }

std::string queueItemBaseName(const game::Rules& r, const game::GameState& s, const game::QueueItem& item) {
    switch (item.kind) {
        case game::QueueItem::Kind::Vehicle:
            if (!item.design.valid() || item.design.index() >= s.designs.size()) return "Unknown design";
            return s.design(item.design).name;
        case game::QueueItem::Kind::Facility:
            return item.facility < r.data().facilities.size() ? r.facility(item.facility).name : std::string("Unknown facility");
        case game::QueueItem::Kind::Upgrade:
            return item.facility < r.data().facilities.size() ? "Upg. " + r.facility(item.facility).name : std::string("Upg.");
    }
    return {};
}

std::string queueItemName(const game::Rules& r, const game::GameState& s, const game::QueueItem& item) {
    std::string name = queueItemBaseName(r, s, item);
    return item.count > 1 ? std::format("{} x {}", name, item.count) : name;
}

game::Resources displayCost(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t,
                            const game::QueueItem& item) {
    game::Resources c = game::economy::itemCost(r, s, e, t, item);
    if (!c.isZero()) return c;
    switch (item.kind) {
        case game::QueueItem::Kind::Vehicle:
            if (item.design.valid() && item.design.index() < s.designs.size()) {
                c = game::computeDesignStats(r, nullptr, s.design(item.design)).cost;
                for (auto& v : c.v) v *= std::max(1, item.count);
            }
            break;
        case game::QueueItem::Kind::Facility:
            if (item.facility < r.data().facilities.size()) c = game::Resources::from(r.facility(item.facility).cost);
            break;
        case game::QueueItem::Kind::Upgrade: break;  // depends on the economy's upgrade rate
    }
    return c;
}

std::vector<ItemEstimate> estimateQueue(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t,
                                        const game::ConstructionQueue& q, const game::Resources& rate) {
    std::vector<ItemEstimate> out;
    int total = 0;
    bool never = false;
    for (const game::QueueItem& item : q.items) {
        ItemEstimate est;
        est.cost = displayCost(r, s, e, t, item);
        est.remaining = game::max(est.cost - item.spent, game::Resources{});
        est.turns = game::economy::turnsToComplete(est.remaining, rate);
        if (est.turns >= 0) est.turns = std::max(1, est.turns);  // an item takes at least one turn
        if (est.turns < 0) never = true;
        else total += est.turns;
        est.doneIn = never ? -1 : total;
        out.push_back(est);
    }
    return out;
}

game::Resources queueUsage(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t,
                           const game::ConstructionQueue& q, const game::Resources& rate) {
    if (q.onHold || q.items.empty()) return {};
    const game::QueueItem& top = q.items.front();
    const game::Resources remaining = game::max(displayCost(r, s, e, t, top) - top.spent, game::Resources{});
    return game::max(game::min(remaining, rate), game::Resources{});
}

int timeRemainingTurns(const game::Resources& remaining, const game::Resources& rate) {
    int turns = 0;
    bool any = false;
    for (size_t i = 0; i < 3; ++i) {
        if (rate.v[i] <= 0) continue;  // a resource that is not produced does not count
        any = true;
        const int64_t left = std::max<int64_t>(0, remaining.v[i]);
        const int64_t t = (left + rate.v[i] - 1) / rate.v[i];
        turns = int(std::min<int64_t>(std::max<int64_t>(turns, t), kNeverTurns));
    }
    return any ? turns : kNeverTurns;
}

std::string queueYearsText(int turns) {
    if (turns < 0 || turns >= kNeverTurns) return "Never";
    turns = std::max(1, turns);  // 0 turns shows as one turn
    return std::format("{}.{} years", turns / 10, turns % 10);
}

std::string timeRemainingText(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::cmd::QueueTarget& t,
                              const game::ConstructionQueue& q, const game::Resources& rate) {
    if (q.items.empty()) return {};
    if (q.onHold) return "On Hold";
    const game::QueueItem& top = q.items.front();
    return queueYearsText(timeRemainingTurns(displayCost(r, s, e, t, top) - top.spent, rate));
}

std::string underConstructionText(const game::Rules& r, const game::GameState& s, const game::ConstructionQueue& q) {
    return q.items.empty() ? std::string("None") : queueItemName(r, s, q.items.front());
}

std::string queueModeNote(const game::ConstructionQueue& q) {
    if (q.emergency) return std::format("Emergency ({} of 10 turns)", q.emergencyTurns);
    if (q.slowTurns > 0) return std::format("Slow ({} turns left)", q.slowTurns);
    return {};
}

std::vector<game::cmd::QueueAdd> multiAddCommands(const std::vector<game::cmd::QueueTarget>& tagged, const std::vector<game::QueueItem>& items) {
    std::vector<game::cmd::QueueAdd> out;
    for (const game::cmd::QueueTarget& t : tagged)
        for (const game::QueueItem& item : items) {
            game::QueueItem add = item;
            add.spent = {};
            out.push_back(game::cmd::QueueAdd{t, add, -1});
        }
    return out;
}

bool similarAbilityCounts(game::AbilityKind k) {
    using K = game::AbilityKind;
    static constexpr std::array<K, 18> kCounted{{
        K::ResourceGenModSystemMinerals, K::ResourceGenModSystemOrganics, K::ResourceGenModSystemRadioactives,
        K::SystemPointGenModResearch, K::SystemPointGenModIntelligence, K::CombatModifierSystem, K::DamageModifierSystem,
        K::ChangeBadEventChanceSystem, K::ChangeBadIntelChanceSystem, K::ChangePopulationHappinessSystem, K::ModifyReproductionSystem,
        K::ChangePopulationSystem, K::PlaguePreventionSystem, K::ShipTrainingSystem, K::FleetTrainingSystem, K::LongRangeScannerSystem,
        K::ReducedMaintenanceSystem, K::ShieldModifierSystem,
    }};
    return std::find(kCounted.begin(), kCounted.end(), k) != kCounted.end();
}

std::vector<std::string> similarSystemAbilities(const game::Rules& r, const game::GameState& s, game::EmpireId e, game::ObjectId planet,
                                                uint32_t facility) {
    std::vector<std::string> out;
    if (facility >= r.data().facilities.size()) return out;
    const game::SystemId sys = s.galaxy.object(planet).system;
    std::set<game::AbilityKind> wanted;
    for (const game::ParsedAbility& a : r.facilityAbilities(facility))
        if (similarAbilityCounts(a.kind)) wanted.insert(a.kind);
    if (wanted.empty()) return out;
    std::set<game::AbilityKind> found;
    auto take = [&](std::span<const game::ParsedAbility> list) {
        for (const game::ParsedAbility& a : list)
            if (wanted.contains(a.kind)) found.insert(a.kind);
    };
    // Built facilities of our colonies in the system (queued ones do not count).
    for (game::ObjectId id : s.galaxy.system(sys).objects) {
        const game::Colony* c = s.colony(id);
        if (!c || c->owner != e) continue;
        for (uint32_t f : c->facilities)
            if (f < r.data().facilities.size()) take(r.facilityAbilities(f));
    }
    // The components of our ships and bases there.
    for (const game::Vehicle& v : s.vehicles) {
        if (v.owner != e || v.location.system != sys || v.count <= 0) continue;
        const ruleset::VehicleType type = game::vehicleType(r, s, v);
        if (type != ruleset::VehicleType::Ship && type != ruleset::VehicleType::Base) continue;
        take(game::vehicleAbilities(r, s, v));
    }
    for (game::AbilityKind k : found) out.emplace_back(game::identifier(k));
    return out;
}

std::vector<std::pair<uint32_t, uint32_t>> reorderMoves(const std::vector<size_t>& order) {
    std::vector<std::pair<uint32_t, uint32_t>> moves;
    std::vector<size_t> now(order.size());
    for (size_t i = 0; i < now.size(); ++i) now[i] = i;
    for (size_t to = 0; to < order.size(); ++to) {
        const auto it = std::find(now.begin() + std::ptrdiff_t(to), now.end(), order[to]);
        if (it == now.end()) continue;
        const size_t from = size_t(it - now.begin());
        if (from == to) continue;
        moves.emplace_back(uint32_t(from), uint32_t(to));
        const size_t item = *it;
        now.erase(it);
        now.insert(now.begin() + std::ptrdiff_t(to), item);
    }
    return moves;
}

std::vector<game::QueueItem> possibleUpgrades(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::Colony& c) {
    // One item per family, to the newest level researched, with the count the
    // queue will store: every lower-level facility of the family here. A queue
    // refuses a second upgrade to the same target (spec 02 §6.6).
    const game::Empire& emp = s.empire(e);
    std::set<uint32_t> queued;
    for (const game::QueueItem& q : c.queue.items)
        if (q.kind == game::QueueItem::Kind::Upgrade) queued.insert(q.facility);
    std::vector<game::QueueItem> out;
    std::set<int> seen;
    for (uint32_t f : c.facilities) {
        const int family = r.facility(f).family;
        if (seen.contains(family)) continue;
        const auto latest = r.latestFacilityOfFamily(emp, family);
        if (!latest || r.facility(*latest).romanNumeral <= r.facility(f).romanNumeral || queued.contains(*latest)) continue;
        seen.insert(family);
        out.push_back(game::economy::upgradeItem(r, c, *latest));
    }
    return out;
}

std::vector<std::pair<uint32_t, uint32_t>> queuedFacilitySwitches(const game::Rules& r, const game::GameState& s, game::EmpireId e,
                                                                  const game::Colony& c) {
    const game::Empire& emp = s.empire(e);
    std::vector<std::pair<uint32_t, uint32_t>> out;
    for (size_t i = 0; i < c.queue.items.size(); ++i) {
        const game::QueueItem& q = c.queue.items[i];
        if (q.kind != game::QueueItem::Kind::Facility || q.facility >= r.data().facilities.size()) continue;
        const int family = r.facility(q.facility).family;
        if (family == 0) continue;
        const auto latest = r.latestFacilityOfFamily(emp, family);
        if (latest && *latest != q.facility && r.facility(*latest).romanNumeral > r.facility(q.facility).romanNumeral)
            out.emplace_back(static_cast<uint32_t>(i), *latest);
    }
    return out;
}

std::vector<uint32_t> facilityChoices(const game::Rules& r, const game::Empire& e, bool onlyLatest) {
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < r.data().facilities.size(); ++i) {
        if (!r.facilityAvailable(e, i)) continue;
        if (onlyLatest && r.latestFacilityOfFamily(e, r.facility(i).family) != i) continue;
        out.push_back(i);
    }
    return out;
}

bool isUnitDesign(const game::Rules& r, const game::Design& d) {
    const ruleset::VehicleType t = r.hull(d.hull).type;
    return game::isUnitType(t) || t == ruleset::VehicleType::WeaponPlatform;
}

std::vector<game::DesignId> designChoices(const game::Rules& r, const game::GameState& s, game::EmpireId e, bool units, bool onlyLatest) {
    std::vector<game::DesignId> out;
    for (game::DesignId id : s.empire(e).designs) {
        if (!id.valid() || id.index() >= s.designs.size()) continue;
        const game::Design& d = s.design(id);
        if (d.owner != e || isUnitDesign(r, d) != units || (onlyLatest && d.obsolete)) continue;
        out.push_back(id);
    }
    return out;
}

int freeFacilitySlots(const game::Rules& r, const game::GameState& s, const game::Colony& c) {
    int queued = 0;
    for (const game::QueueItem& q : c.queue.items) queued += q.kind == game::QueueItem::Kind::Facility;
    return std::max(0, game::facilitySlots(r, s, c) - static_cast<int>(c.facilities.size()) - queued);
}

// ---- Queue types ---------------------------------------------------------------------------------

std::vector<QueueTemplate> builtInQueueTemplates() {
    using K = game::AbilityKind;
    return {
        {"Fill free slots: minerals", {}, {K::ResourceGenMinerals}},
        {"Fill free slots: organics", {}, {K::ResourceGenOrganics}},
        {"Fill free slots: radioactives", {}, {K::ResourceGenRadioactives}},
        {"Fill free slots: research", {}, {K::PointGenResearch}},
        {"Fill free slots: intelligence", {}, {K::PointGenIntelligence}},
        {"Fill free slots: balanced", {}, {K::ResourceGenMinerals, K::ResourceGenOrganics, K::ResourceGenRadioactives, K::PointGenResearch}},
    };
}

namespace {

std::optional<uint32_t> facilityByName(const game::Rules& r, std::string_view name) {
    for (uint32_t i = 0; i < r.data().facilities.size(); ++i)
        if (keysEqual(r.facility(i).name, name)) return i;
    return std::nullopt;
}

} // namespace

std::vector<game::QueueItem> resolveTemplate(const game::Rules& r, const game::GameState& s, game::EmpireId e,
                                             const game::cmd::QueueTarget& t, const QueueTemplate& tmpl) {
    const game::Empire& emp = s.empire(e);
    std::vector<game::QueueItem> out;
    if (tmpl.builtIn()) {
        const game::Colony* c = t.vehicle.valid() ? nullptr : s.colony(t.planet);
        if (!c) return out;
        const int free = freeFacilitySlots(r, s, *c);
        std::vector<uint32_t> choices;
        for (game::AbilityKind k : tmpl.fill)
            if (auto f = r.bestFacilityWith(emp, k)) choices.push_back(*f);
        for (int i = 0; i < free && !choices.empty(); ++i) {
            game::QueueItem item;
            item.kind = game::QueueItem::Kind::Facility;
            item.facility = choices[static_cast<size_t>(i) % choices.size()];
            out.push_back(item);
        }
        return out;
    }
    for (const QueueTemplateEntry& en : tmpl.entries) {
        game::QueueItem item;
        item.kind = en.kind;
        if (en.kind == game::QueueItem::Kind::Vehicle) {
            // The newest own design of that name that is not obsolete (or any, failing that).
            std::optional<game::DesignId> pick;
            for (game::DesignId id : emp.designs) {
                const game::Design& d = s.design(id);
                if (keysEqual(d.name, en.name) && (!pick || !d.obsolete)) pick = id;
            }
            if (!pick) continue;
            item.design = *pick;
            item.count = std::max(1, en.count);
            out.push_back(item);
            continue;
        }
        const auto f = facilityByName(r, en.name);
        if (!f) continue;
        const auto latest = r.latestFacilityOfFamily(emp, r.facility(*f).family);
        if (!latest) continue;
        item.facility = *latest;
        const int copies = en.kind == game::QueueItem::Kind::Facility ? std::max(1, en.count) : 1;
        for (int i = 0; i < copies; ++i) out.push_back(item);
    }
    return out;
}

QueueTemplate templateFromQueue(const game::Rules& r, const game::GameState& s, std::string name, const game::ConstructionQueue& q) {
    QueueTemplate t;
    t.name = std::move(name);
    for (const game::QueueItem& item : q.items) {
        QueueTemplateEntry en;
        en.kind = item.kind;
        if (item.kind == game::QueueItem::Kind::Vehicle) {
            if (!item.design.valid() || item.design.index() >= s.designs.size()) continue;
            en.name = s.design(item.design).name;
            en.count = std::max(1, item.count);
        } else {
            if (item.facility >= r.data().facilities.size()) continue;
            en.name = r.facility(item.facility).name;
        }
        // Runs of the same facility become one entry with a count.
        if (en.kind == game::QueueItem::Kind::Facility && !t.entries.empty() && t.entries.back().kind == en.kind && t.entries.back().name == en.name)
            ++t.entries.back().count;
        else
            t.entries.push_back(std::move(en));
    }
    return t;
}

namespace {

const char* kindWord(game::QueueItem::Kind k) {
    switch (k) {
        case game::QueueItem::Kind::Vehicle: return "vehicle";
        case game::QueueItem::Kind::Facility: return "facility";
        case game::QueueItem::Kind::Upgrade: return "upgrade";
    }
    return "facility";
}

std::vector<std::string_view> splitTabs(std::string_view line) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) break;
        start = tab + 1;
    }
    return out;
}

} // namespace

// Format: a "type<TAB>name" line starts a template; "item<TAB>kind<TAB>count<TAB>name" lines follow.
std::string serializeTemplates(const std::vector<QueueTemplate>& list) {
    std::string out = "# OpenSE4 construction queue types\n";
    for (const QueueTemplate& t : list) {
        if (t.builtIn()) continue;
        out += std::format("type\t{}\n", t.name);
        for (const QueueTemplateEntry& en : t.entries) out += std::format("item\t{}\t{}\t{}\n", kindWord(en.kind), en.count, en.name);
    }
    return out;
}

std::vector<QueueTemplate> parseTemplates(std::string_view text) {
    std::vector<QueueTemplate> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto f = splitTabs(line);
        if (f[0] == "type" && f.size() >= 2 && !f[1].empty()) {
            out.push_back(QueueTemplate{std::string(f[1]), {}, {}});
        } else if (f[0] == "item" && f.size() >= 4 && !out.empty() && !f[3].empty()) {
            QueueTemplateEntry en;
            en.kind = f[1] == "vehicle" ? game::QueueItem::Kind::Vehicle
                      : f[1] == "upgrade" ? game::QueueItem::Kind::Upgrade
                                          : game::QueueItem::Kind::Facility;
            en.count = static_cast<int>(std::clamp(datafile::parseInteger(f[2]).value_or(1), int64_t{1}, int64_t{9999}));
            en.name = std::string(f[3]);
            out.back().entries.push_back(std::move(en));
        }
    }
    return out;
}

} // namespace opense4::client::classic
