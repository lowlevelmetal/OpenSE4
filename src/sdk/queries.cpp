#include "sdk/queries.hpp"

#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "sdk/parts.hpp"

#include <algorithm>

namespace opense4::sdk {

namespace detail {

namespace {

using game::DesignId;
using game::EmpireId;
using game::FleetId;
using game::Location;
using game::ObjectId;
using game::SystemId;
using game::VehicleId;
using Result = Queries::Result;

// A place: a location map, or a system id standing for the system's centre.
struct Place {
    std::optional<Location> at;
};

} // namespace

template <>
struct Codec<Place> {
    static Value enc(const Place& p) { return p.at ? detail::enc(*p.at) : Value(); }
    static bool dec(const Value& v, Ctx& c, Place& out) {
        out.at.reset();
        if (v.isNull()) return true;
        Location loc;
        if (v.isInt()) {
            if (!Codec<SystemId>::dec(v, c, loc.system)) return false;
            loc.sector = game::Sector{game::kSystemCenter, game::kSystemCenter};
        } else if (!detail::dec(v, c, loc)) {
            return false;
        }
        out.at = loc;
        return true;
    }
};

namespace {

// ---- Arguments --------------------------------------------------------------------------------------------

struct PathArgs {
    Place origin, destination;
    VehicleId vehicle;
    FleetId fleet;
    std::optional<bool> omniscient;   // default: whether the view is whole
};
template <class A>
void fields(A& a, PathArgs& x) {
    a("origin", x.origin);
    a("destination", x.destination);
    a("vehicle", x.vehicle);
    a("fleet", x.fleet);
    a("omniscient", x.omniscient);
}

struct GroupArgs {
    VehicleId vehicle;
    FleetId fleet;
};
template <class A>
void fields(A& a, GroupArgs& x) {
    a("vehicle", x.vehicle);
    a("fleet", x.fleet);
}

struct DesignArgs {
    DesignId design;
    std::optional<uint32_t> hull;
    std::optional<std::vector<game::DesignEntry>> entries;
    std::optional<std::vector<uint32_t>> components;
};
template <class A>
void fields(A& a, DesignArgs& x) {
    a("design", x.design);
    a("hull", x.hull);
    a("entries", x.entries);
    a("components", x.components);
}

struct AbilityArgs {
    VehicleId vehicle;
    ObjectId planet;
    SystemId system;
    DesignId design;
    std::optional<uint32_t> component, facility, hull;
};
template <class A>
void fields(A& a, AbilityArgs& x) {
    a("vehicle", x.vehicle);
    a("planet", x.planet);
    a("system", x.system);
    a("design", x.design);
    a("component", x.component);
    a("facility", x.facility);
    a("hull", x.hull);
}

struct QueueArgs {
    ObjectId planet;
    VehicleId vehicle;
    std::optional<std::vector<game::QueueItem>> items;
};
template <class A>
void fields(A& a, QueueArgs& x) {
    a("planet", x.planet);
    a("vehicle", x.vehicle);
    a("items", x.items);
}

struct ResearchArgs {
    ruleset::TechAreaId area;
    std::optional<int> level;
};
template <class A>
void fields(A& a, ResearchArgs& x) {
    a("area", x.area);
    a("level", x.level);
}

struct ColonizeArgs {
    VehicleId vehicle;
    ObjectId planet;
};
template <class A>
void fields(A& a, ColonizeArgs& x) {
    a("vehicle", x.vehicle);
    a("planet", x.planet);
}

struct QueueItemArgs {
    ObjectId planet;
    VehicleId vehicle;
    game::QueueItem item;
};
template <class A>
void fields(A& a, QueueItemArgs& x) {
    a("planet", x.planet);
    a("vehicle", x.vehicle);
    a("item", x.item);
}

template <class T>
std::expected<T, CodecError> decodeArgs(const Value& v) {
    T out{};
    if (v.isNull()) return out;
    Ctx c;
    if (!detail::dec(v, c, out)) return std::unexpected(c.takeError());
    return out;
}

std::unexpected<CodecError> bad(std::string path, std::string message) { return std::unexpected(CodecError{std::move(path), std::move(message)}); }

// ---- What the perspective may ask about ----------------------------------------------------------------------

bool ownedHere(const Perspective& p, EmpireId owner) { return p.whole() || (p.empire().valid() && owner == p.empire()); }

bool validLocation(const game::GameState& s, const Location& l) {
    return l.system.valid() && l.system.index() < s.galaxy.systems.size() && l.sector.valid();
}

// The system's contents are in the view.
bool systemShown(const Perspective& p, SystemId sys) {
    if (p.whole()) return true;
    const game::Empire* me = p.me();
    return me && me->hasExplored(sys);
}

// A stellar object the view lists: on the list of a system whose contents it shows.
bool objectShown(const Perspective& p, ObjectId o) {
    const game::GameState& s = p.state();
    if (!o.valid() || o.index() >= s.galaxy.objects.size()) return false;
    const game::SpaceObject& obj = s.galaxy.object(o);
    if (!systemShown(p, obj.system)) return false;
    const auto& list = s.galaxy.system(obj.system).objects;
    return std::find(list.begin(), list.end(), o) != list.end();
}

// A design whose components the empire knows: its own, one it has seen, or
// one of a vehicle it sees.
bool designKnown(const Perspective& p, DesignId d) {
    const game::GameState& s = p.state();
    if (!d.valid() || d.index() >= s.designs.size()) return false;
    if (ownedHere(p, s.design(d).owner)) return true;
    const game::Empire* me = p.me();
    if (me && game::knowsDesign(me->knowledge, d)) return true;
    for (const game::Vehicle& v : s.vehicles)
        for (const game::UnitStack& u : game::groupStacks(v))
            if (u.design == d) return true;
    return false;
}

// One of our vehicles (any vehicle in a whole view), or a fleet's leader.
struct Group {
    const game::Vehicle* lead = nullptr;
    const game::Fleet* fleet = nullptr;
};
std::expected<Group, CodecError> findGroup(const Perspective& p, VehicleId vehicle, FleetId fleet, bool required) {
    const game::GameState& s = p.state();
    if (vehicle.valid()) {
        const game::Vehicle* v = s.vehicle(vehicle);
        if (!v) return bad("vehicle", "no such vehicle in view");
        if (!ownedHere(p, v->owner)) return bad("vehicle", "not one of our vehicles");
        return Group{v, nullptr};
    }
    if (fleet.valid()) {
        const game::Fleet* f = s.fleet(fleet);
        if (!f) return bad("fleet", "no such fleet in view");
        if (!ownedHere(p, f->owner)) return bad("fleet", "not one of our fleets");
        const game::Vehicle* lead = game::fleetLeader(s, *f);
        if (!lead) return bad("fleet", "the fleet has no member at its location");
        return Group{lead, f};
    }
    if (required) return bad("vehicle", "missing: name a vehicle or a fleet");
    return Group{};
}

// ---- The queries ------------------------------------------------------------------------------------------------

Result path(const Perspective& p, const Value& args) {
    const auto a = decodeArgs<PathArgs>(args);
    if (!a) return std::unexpected(a.error());
    const game::Rules& r = p.rules();
    const game::GameState& s = p.state();
    const bool omniscient = a->omniscient.value_or(p.whole());
    if (omniscient && !p.whole()) return bad("omniscient", "only a whole view may route over what the empire does not know");
    const auto g = findGroup(p, a->vehicle, a->fleet, false);
    if (!g) return std::unexpected(g.error());
    Location from;
    if (a->origin.at) from = *a->origin.at;
    else if (g->fleet) from = g->fleet->location;
    else if (g->lead) from = g->lead->location;
    else return bad("origin", "missing: give a place, a vehicle or a fleet");
    if (!validLocation(s, from)) return bad("origin", "no such place");
    if (!a->destination.at) return bad("destination", "missing");
    const Location to = *a->destination.at;
    if (!validLocation(s, to)) return bad("destination", "no such place");

    // The group's own route options, as its moves and movement::etaTurns use them.
    game::movement::RouteOptions options;
    if (g->lead) {
        options.allowWarp = game::vehicleType(r, s, *g->lead) != ruleset::VehicleType::Fighter;
        options.sweeper = game::movement::leadsSweeperGroup(s, game::movement::sweeperOf(s, *g->lead));
    }
    const Location goals[] = {to};
    const auto found = game::movement::findPathToNearest(r, s, omniscient ? EmpireId{} : p.empire(), from, goals, options);
    Value turns = g->lead ? num(game::movement::etaTurns(r, s, *g->lead, to)) : Value();
    if (!found)
        return Map(5)("found", Value(false))("steps", Value::emptyList())("length", Value())("jumps", Value())("turns", std::move(turns)).done();
    int jumps = 0;
    Location at = from;
    for (const Location& step : found->path.steps) {
        jumps += step.system != at.system;
        at = step;
    }
    return Map(5)("found", Value(true))("steps", enc(found->path.steps))("length", num(found->path.length))("jumps", num(jumps))(
               "turns", std::move(turns))
        .done();
}

Value vehicleMovement(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    const int max = game::vehicleMaxMovement(r, s, v);
    const int moves = game::movement::movesPerTurn(s, max);
    const bool unlimited = game::vehicleHasUnlimitedSupply(r, s, v);
    const bool uses = game::vehicleUsesSupply(r, s, v);
    const int64_t cost = game::movement::moveSupplyCost(r, s, v);
    Value onSupply, turnsOnSupply;
    if (uses && !unlimited && cost > 0) {
        const int64_t n = std::max<int64_t>(0, v.supply) / cost;
        onSupply = num(n);
        if (moves > 0) turnsOnSupply = num(n / moves);
    }
    return Map(12)("vehicle", id(v.id))("movement", num(v.movement))("max_movement", num(max))("moves_per_turn", num(moves))(
               "supply", num(v.supply))("supply_capacity", num(game::vehicleSupplyCapacity(r, s, v)))("unlimited_supply", Value(unlimited))(
               "uses_supply", Value(uses))("supply_per_move", num(cost))("moves_on_supply", std::move(onSupply))(
               "turns_on_supply", std::move(turnsOnSupply))
        .done();
}

Result movementQuery(const Perspective& p, const Value& args) {
    const auto a = decodeArgs<GroupArgs>(args);
    if (!a) return std::unexpected(a.error());
    const auto g = findGroup(p, a->vehicle, a->fleet, true);
    if (!g) return std::unexpected(g.error());
    const game::Rules& r = p.rules();
    const game::GameState& s = p.state();
    if (!g->fleet) return vehicleMovement(r, s, *g->lead);
    const int speed = game::movement::fleetSpeed(r, s, *g->fleet);
    const int moves = game::movement::movesPerTurn(s, speed);
    ValueList members;
    std::optional<int> movement;
    std::optional<int64_t> onSupply;
    for (VehicleId m : game::fleetMembersAt(s, *g->fleet)) {
        const game::Vehicle& v = *s.vehicle(m);
        Value mv = vehicleMovement(r, s, v);
        movement = movement ? std::min(*movement, v.movement) : v.movement;
        if (const Value* n = mv.find("moves_on_supply"); n && n->isInt()) onSupply = onSupply ? std::min(*onSupply, n->asInt()) : n->asInt();
        members.push_back(std::move(mv));
    }
    return Map(7)("fleet", id(g->fleet->id))("movement", num(movement.value_or(0)))("max_movement", num(speed))("moves_per_turn", num(moves))(
               "moves_on_supply", onSupply ? num(*onSupply) : Value())(
               "turns_on_supply", onSupply && moves > 0 ? num(*onSupply / moves) : Value())("members", Value(std::move(members)))
        .done();
}

Result designFiguresQuery(const Perspective& p, const Value& args) {
    const auto a = decodeArgs<DesignArgs>(args);
    if (!a) return std::unexpected(a.error());
    const game::Rules& r = p.rules();
    const game::GameState& s = p.state();
    game::Design probe;
    probe.owner = p.empire();
    if (a->design.valid()) {
        if (!designKnown(p, a->design)) return bad("design", "no such design known");
        const game::Design& d = s.design(a->design);
        probe.owner = d.owner;
        probe.hull = d.hull;
        probe.entries = d.entries;
    } else {
        if (!a->hull) return bad("hull", "missing: name a design, or a hull and its components");
        probe.hull = *a->hull;
        if (a->entries) probe.entries = *a->entries;
        else if (a->components)
            for (uint32_t c : *a->components) probe.entries.push_back({c, -1});
    }
    if (probe.hull >= r.data().vehicleSizes.size()) return bad("hull", "no such hull");
    for (size_t i = 0; i < probe.entries.size(); ++i) {
        const game::DesignEntry& e = probe.entries[i];
        const std::string at = a->entries ? std::format("entries[{}]", i) : std::format("components[{}]", i);
        if (e.component >= r.data().components.size()) return bad(a->entries ? at + ".component" : at, "no such component");
        if (e.mount < -1 || (e.mount >= 0 && static_cast<size_t>(e.mount) >= r.data().weaponMounts.size())) return bad(at + ".mount", "no such mount");
    }
    // A proposed design is checked against our technology, as the designer does; an existing one is not.
    const game::Empire* owner = a->design.valid() ? nullptr : p.me();
    const game::DesignStats st = game::computeDesignStats(r, owner, probe.hull, probe.entries);
    const bool maintenance = probe.owner.valid() && probe.owner.index() < s.empires.size() && ownedHere(p, probe.owner);
    return designFigures(r, s, st, probe.hull, probe.entries, maintenance ? &probe : nullptr);
}

Value parsedEntries(std::span<const game::ParsedAbility> list) {
    return listOf(list, [](const game::ParsedAbility& a) {
        return Map(3)("name", Value(abilityName(a)))("value1", num(a.value1))("value2", num(a.value2)).done();
    });
}

Result abilitiesQuery(const Perspective& p, const Value& args) {
    const auto a = decodeArgs<AbilityArgs>(args);
    if (!a) return std::unexpected(a.error());
    const game::Rules& r = p.rules();
    const game::GameState& s = p.state();
    const int given = a->vehicle.valid() + a->planet.valid() + a->system.valid() + a->design.valid() + a->component.has_value() +
                      a->facility.has_value() + a->hull.has_value();
    if (given != 1) return bad("", "name exactly one of vehicle, planet, system, design, component, facility or hull");
    std::vector<game::ParsedAbility> list;
    if (a->vehicle.valid()) {
        const game::Vehicle* v = s.vehicle(a->vehicle);
        if (!v) return bad("vehicle", "no such vehicle in view");
        list = game::vehicleAbilities(r, s, *v);
    } else if (a->planet.valid()) {
        if (!objectShown(p, a->planet)) return bad("planet", "no such object in view");
        if (const game::Colony* c = s.colony(a->planet)) list = game::colonyAbilities(r, s, *c);
        else list = game::parseAbilities(s.galaxy.object(a->planet).abilities);
    } else if (a->system.valid()) {
        if (a->system.index() >= s.galaxy.systems.size() || !systemShown(p, a->system)) return bad("system", "no such system explored");
        list = game::parseAbilities(s.galaxy.system(a->system).abilities);
    } else if (a->design.valid()) {
        if (!designKnown(p, a->design)) return bad("design", "no such design known");
        game::Vehicle probe;
        probe.design = a->design;
        probe.owner = s.design(a->design).owner;
        probe.damage.assign(s.design(a->design).entries.size(), 0);
        list = game::vehicleAbilities(r, s, probe);
    } else if (a->component) {
        if (*a->component >= r.data().components.size()) return bad("component", "no such component");
        const auto l = r.componentAbilities(*a->component);
        list.assign(l.begin(), l.end());
    } else if (a->facility) {
        if (*a->facility >= r.data().facilities.size()) return bad("facility", "no such facility");
        const auto l = r.facilityAbilities(*a->facility);
        list.assign(l.begin(), l.end());
    } else {
        if (*a->hull >= r.data().vehicleSizes.size()) return bad("hull", "no such hull");
        const auto l = r.hullAbilities(*a->hull);
        list.assign(l.begin(), l.end());
    }
    return Map(2)("entries", parsedEntries(list))("values", abilityTotals(list)).done();
}

// One of our construction queues: a colony's or a vehicle's.
std::expected<std::pair<game::cmd::QueueTarget, const game::ConstructionQueue*>, CodecError> findQueue(const Perspective& p, ObjectId planet,
                                                                                                      VehicleId vehicle) {
    const game::GameState& s = p.state();
    if (planet.valid() == vehicle.valid()) return bad("", "name either a planet or a vehicle");
    if (planet.valid()) {
        const game::Colony* c = s.colony(planet);
        if (!c || !ownedHere(p, c->owner)) return bad("planet", "not one of our colonies");
        return std::pair{game::cmd::QueueTarget{planet, {}}, &c->queue};
    }
    const game::Vehicle* v = s.vehicle(vehicle);
    if (!v || !ownedHere(p, v->owner)) return bad("vehicle", "not one of our vehicles");
    return std::pair{game::cmd::QueueTarget{{}, vehicle}, &v->queue};
}

EmpireId queueOwner(const game::GameState& s, const game::cmd::QueueTarget& t) {
    if (t.vehicle.valid()) return s.vehicle(t.vehicle)->owner;
    return s.colony(t.planet)->owner;
}

Result queueForecast(const Perspective& p, const Value& args) {
    const auto a = decodeArgs<QueueArgs>(args);
    if (!a) return std::unexpected(a.error());
    const auto q = findQueue(p, a->planet, a->vehicle);
    if (!q) return std::unexpected(q.error());
    game::ConstructionQueue queue = *q->second;
    if (a->items) queue.items = *a->items;
    const game::Rules& r = p.rules();
    for (size_t i = 0; i < queue.items.size(); ++i) {
        const game::QueueItem& item = queue.items[i];
        if (item.kind == game::QueueItem::Kind::Vehicle && (!item.design.valid() || item.design.index() >= p.state().designs.size()))
            return bad(std::format("items[{}].design", i), "no such design");
        if (item.kind != game::QueueItem::Kind::Vehicle && item.facility >= r.data().facilities.size())
            return bad(std::format("items[{}].facility", i), "no such facility");
    }
    return queueValue(r, p.state(), queueOwner(p.state(), q->first), q->first, queue);
}

Result researchForecast(const Perspective& p, const Value& args) {
    const auto a = decodeArgs<ResearchArgs>(args);
    if (!a) return std::unexpected(a.error());
    const game::Rules& r = p.rules();
    const game::GameState& s = p.state();
    const game::Empire* me = p.me();
    if (!me) return bad("", "the perspective has no empire");
    if (!a->area.valid() || a->area.index() >= r.data().techAreas.size()) return bad("area", "no such tech area");
    const int current = me->techLevel(a->area);
    const int level = a->level.value_or(current + 1);
    const std::vector<LevelEstimate> est = estimateResearch(r, s, *me, a->area, level);
    int64_t cost = 0;
    int turns = 0;
    ValueList levels;
    for (const LevelEstimate& l : est) {
        cost += l.cost;
        turns = turns < 0 || l.turns < 0 ? -1 : turns + l.turns;
        levels.push_back(Map(3)("level", num(l.level))("cost", num(l.cost))("turns", num(l.turns)).done());
    }
    return Map(8)("area", id(a->area))("current_level", num(current))("max_level", num(r.tech(a->area).maxLevel))(
               "researchable", Value(game::research::isResearchable(r, s, *me, a->area)))("levels", Value(std::move(levels)))(
               "total_cost", num(cost))("turns", num(turns))
        .done();
}

Result colonizeProblem(const Perspective& p, const Value& args) {
    const auto a = decodeArgs<ColonizeArgs>(args);
    if (!a) return std::unexpected(a.error());
    const auto g = findGroup(p, a->vehicle, {}, true);
    if (!g) return std::unexpected(g.error());
    if (!objectShown(p, a->planet)) return bad("planet", "no such object in view");
    return Map(1)("problem", Value(game::movement::colonizeProblem(p.rules(), p.state(), *g->lead, a->planet))).done();
}

Result queueItemProblem(const Perspective& p, const Value& args) {
    const auto a = decodeArgs<QueueItemArgs>(args);
    if (!a) return std::unexpected(a.error());
    const auto q = findQueue(p, a->planet, a->vehicle);
    if (!q) return std::unexpected(q.error());
    return Map(1)("problem", Value(game::queueItemProblem(p.rules(), p.state(), queueOwner(p.state(), q->first), q->first, a->item))).done();
}

} // namespace

} // namespace detail

Queries::Queries(Perspective p) : p_(std::make_shared<const Perspective>(std::move(p))) {
    using Impl = Result (*)(const Perspective&, const script::Value&);
    const std::pair<std::string_view, Impl> table[] = {
        {"path", &detail::path},
        {"movement", &detail::movementQuery},
        {"design_figures", &detail::designFiguresQuery},
        {"abilities", &detail::abilitiesQuery},
        {"queue_forecast", &detail::queueForecast},
        {"research_forecast", &detail::researchForecast},
        {"colonize_problem", &detail::colonizeProblem},
        {"queue_item_problem", &detail::queueItemProblem},
    };
    for (const auto& [name, impl] : table)
        functions_.push_back({std::string(name), [p = p_, impl](const script::Value& args) { return impl(*p, args); }});
}

Queries::Queries(const game::Rules& r, const game::GameState& s, game::EmpireId empire, ViewOptions options)
    : Queries(Perspective(r, s, empire, options)) {}

Queries::Result Queries::call(std::string_view name, const script::Value& args) const {
    for (const Entry& e : functions_)
        if (e.name == name) return e.call(args);
    return std::unexpected(CodecError{"", std::format("'{}' is not a query", name)});
}

} // namespace opense4::sdk
