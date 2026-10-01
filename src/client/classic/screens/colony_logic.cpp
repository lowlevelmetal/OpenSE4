#include "client/classic/screens/colony_logic.hpp"

#include "datafile/datafile.hpp"
#include "datafile/reader.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <limits>
#include <map>
#include <set>
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

game::cmd::SetOrders sendColonyShipOrders(const game::GameState& s, game::VehicleId ship, game::ObjectId planet) {
    game::cmd::SetOrders c;
    c.vehicle = ship;
    const game::Vehicle* v = s.vehicle(ship);
    const game::Location where = game::locationOf(s.galaxy, planet);
    if (v && v->cargo.totalPopulation() == 0) {
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

void SortHistory::click(int column) {
    std::erase(columns_, column);
    columns_.insert(columns_.begin(), column);
    if (columns_.size() > kKeys) columns_.resize(kKeys);
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
    std::vector<int> icons;
    const auto abilities = game::colonyAbilities(r, s, c);
    if (game::colonyHasSpaceYard(r, c)) icons.push_back(1);
    if (!c.queue.items.empty() && !c.queue.onHold) icons.push_back(13);
    if (c.minister) icons.push_back(11);
    if (game::hasAbility(abilities, game::AbilityKind::ComponentRepair)) icons.push_back(12);
    if (game::hasAbility(abilities, game::AbilityKind::AncientRuins) || game::hasAbility(abilities, game::AbilityKind::AncientRuinsUnique))
        icons.push_back(16);
    if (!game::breathable(s, c)) icons.push_back(18);
    // Cargo contents by unit class.
    std::set<ruleset::VehicleType> kinds;
    for (const game::UnitStack& u : c.cargo.units)
        if (u.count > 0) kinds.insert(r.hull(s.design(u.design).hull).type);
    using ruleset::VehicleType;
    const std::array<std::pair<VehicleType, int>, 5> cargoIcons{
        {{VehicleType::Fighter, 19}, {VehicleType::Satellite, 20}, {VehicleType::Mine, 21}, {VehicleType::Troop, 26}, {VehicleType::WeaponPlatform, 27}}};
    for (const auto& [kind, icon] : cargoIcons)
        if (kinds.contains(kind)) icons.push_back(icon);
    if (c.cargo.totalPopulation() > 0) icons.push_back(23);
    if (!connected) icons.push_back(31);
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

std::vector<std::string> planetOrders(const std::vector<game::Command>& orders, game::ObjectId planet) {
    std::vector<std::pair<std::string, int>> counted;
    auto add = [&](std::string text) {
        for (auto& [t, n] : counted)
            if (t == text) {
                ++n;
                return;
            }
        counted.emplace_back(std::move(text), 1);
    };
    for (const game::Command& command : orders)
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                namespace cmd = game::cmd;
                if constexpr (std::is_same_v<T, cmd::QueueAdd>) {
                    if (c.target.planet == planet && !c.target.vehicle.valid()) add("Queued an item");
                } else if constexpr (std::is_same_v<T, cmd::QueueRemove>) {
                    if (c.target.planet == planet && !c.target.vehicle.valid()) add("Removed a queue item");
                } else if constexpr (std::is_same_v<T, cmd::QueueMove>) {
                    if (c.target.planet == planet && !c.target.vehicle.valid()) add("Reordered the queue");
                } else if constexpr (std::is_same_v<T, cmd::QueueSetCount>) {
                    if (c.target.planet == planet && !c.target.vehicle.valid()) add("Changed a batch size");
                } else if constexpr (std::is_same_v<T, cmd::QueueFlags>) {
                    if (c.target.planet == planet && !c.target.vehicle.valid()) add("Queue settings");
                } else if constexpr (std::is_same_v<T, cmd::SetColonyType>) {
                    if (c.planet == planet) add("Colony type: " + c.colonyType);
                } else if constexpr (std::is_same_v<T, cmd::AbandonPlanet>) {
                    if (c.planet == planet) add("Abandon");
                } else if constexpr (std::is_same_v<T, cmd::Scrap>) {
                    if (c.facilityPlanet == planet && !c.vehicle.valid()) add("Scrapped a facility");
                } else if constexpr (std::is_same_v<T, cmd::Rename>) {
                    if (c.planet == planet && !c.vehicle.valid() && !c.fleet.valid() && !c.design.valid()) add("Renamed");
                } else if constexpr (std::is_same_v<T, cmd::SetMinister>) {
                    if (c.planet == planet && !c.vehicle.valid() && !c.empireWide) add(c.on ? "Minister on" : "Minister off");
                } else if constexpr (std::is_same_v<T, cmd::TransferCargo>) {
                    if (c.fromPlanet == planet || c.toPlanet == planet) add("Cargo transfer");
                }
            },
            command);
    std::vector<std::string> out;
    for (auto& [text, n] : counted) out.push_back(n > 1 ? std::format("{} (x{})", text, n) : text);
    return out;
}

// ---- Construction queues -------------------------------------------------------------------------

namespace {

// An intact Space Yard part, whatever the vehicle's status.
bool hasYardPart(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    const game::Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (game::entryIntact(r, s, v, i) && game::hasAbility(r.componentAbilities(d.entries[i].component), game::AbilityKind::SpaceYard))
            return true;
    return false;
}

} // namespace

bool workingVehicleYard(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    return v.status != game::VehicleStatus::Cloaked && game::vehicleHasSpaceYard(r, s, v);
}

std::vector<QueueEntry> empireQueues(const game::Rules& r, const game::GameState& s, game::EmpireId e) {
    std::vector<QueueEntry> out;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        QueueEntry q;
        q.target.planet = c->planet;
        // Our engine has no colony cloaking, so a colony's yard always works.
        q.kind = game::colonyHasSpaceYard(r, *c) ? QueueKind::PlanetYard : QueueKind::Planet;
        q.where = game::locationOf(s.galaxy, c->planet);
        q.name = s.galaxy.object(c->planet).name;
        out.push_back(std::move(q));
    }
    for (const game::Vehicle& v : s.vehicles) {
        if (v.owner != e || v.count <= 0) continue;
        const bool working = workingVehicleYard(r, s, v);
        if (!working && v.queue.items.empty() && !hasYardPart(r, s, v)) continue;
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

std::string queueItemName(const game::Rules& r, const game::GameState& s, const game::QueueItem& item) {
    switch (item.kind) {
        case game::QueueItem::Kind::Vehicle: {
            if (!item.design.valid() || item.design.index() >= s.designs.size()) return "Unknown design";
            const std::string& name = s.design(item.design).name;
            return item.count > 1 ? std::format("{} x{}", name, item.count) : name;
        }
        case game::QueueItem::Kind::Facility:
            return item.facility < r.data().facilities.size() ? r.facility(item.facility).name : std::string("Unknown facility");
        case game::QueueItem::Kind::Upgrade:
            return item.facility < r.data().facilities.size() ? "Upgrade to " + r.facility(item.facility).name : std::string("Upgrade");
    }
    return {};
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

std::string queueYearsText(int turns) {
    if (turns < 0 || turns >= kNeverTurns) return "Never";
    return std::format("{}.{} Years", turns / 10, turns % 10);
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

std::vector<std::string> similarSystemAbilities(const game::Rules& r, const game::GameState& s, game::EmpireId e, game::ObjectId planet,
                                                uint32_t facility) {
    std::vector<std::string> out;
    if (facility >= r.data().facilities.size()) return out;
    const game::SystemId sys = s.galaxy.object(planet).system;
    // System-wide: an ability whose identifier names "System" (inferred).
    auto systemWide = [](game::AbilityKind k) { return game::identifier(k).find("System") != std::string_view::npos; };
    std::set<game::AbilityKind> wanted;
    for (const game::ParsedAbility& a : r.facilityAbilities(facility))
        if (a.kind != game::AbilityKind::Unknown && systemWide(a.kind)) wanted.insert(a.kind);
    if (wanted.empty()) return out;
    std::set<game::AbilityKind> found;
    for (game::ObjectId id : s.galaxy.system(sys).objects) {
        const game::Colony* c = s.colony(id);
        if (!c || c->owner != e) continue;
        for (uint32_t f : c->facilities)
            for (const game::ParsedAbility& a : r.facilityAbilities(f))
                if (wanted.contains(a.kind)) found.insert(a.kind);
    }
    for (game::AbilityKind k : found) out.emplace_back(game::identifier(k));
    return out;
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
