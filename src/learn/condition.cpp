#include "learn/condition.hpp"

#include "core/hash.hpp"
#include "datafile/datafile.hpp"
#include "game/ai_data.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <cstddef>
#include <format>

namespace opense4::learn {

namespace {

// In the order of Fact (factInfo indexes it).
constexpr FactValue N = FactValue::Number, T = FactValue::Text, F = FactValue::Flag;
constexpr FactInfo kFacts[] = {
    {Fact::Window, "window", T, false, "that window is open (a window id)"},
    {Fact::Selected, "selected", T, true, "the player selected that kind of thing in the main window, and it is still selected"},
    {Fact::Command, "command", T, true, "the player gave a command of that type (game/commands.hpp)"},
    {Fact::Order, "order", T, true, "the player gave a ship, fleet or planet an order of that kind"},
    {Fact::Tab, "tab", T, false, "an open window shows that tab or filter (\"<window>:<tab>\")"},
    {Fact::Picking, "picking", T, false, "the main window waits for the place an order of that kind goes to (\"move-to\", ...)"},
    {Fact::MovementLines, "movement_lines", F, false, "the system view shows the ships' movement lines (Ctrl+L)"},
    {Fact::Route, "route", F, false, "the selected ship, or its fleet, has a Move To order to another sector or a Move To Waypoint order: a route to draw"},
    {Fact::DesignComponents, "design_components", N, false, "the design being built in the open Create Design window has N components",
     "Components on the design"},
    {Fact::DesignHullChosen, "design_hull_chosen", F, false, "the player picked a hull in the open Create Design window's Size list"},
    {Fact::DesignTypeChosen, "design_type_chosen", T, false, "the open Create Design window's Design Type box shows that design type"},
    {Fact::DesignNamed, "design_named", F, false, "the open Create Design window's Design Name box holds a name no other design has"},
    {Fact::DesignVehicle, "design_vehicle", T, false, "the open Create Design window designs that vehicle type (\"ship\", \"base\", ...)"},
    {Fact::SimulatorOwners, "simulator_owners", N, false, "the open Combat Simulator has items for N races", "Races with items"},
    {Fact::SimulatorItems, "simulator_items", N, false, "the open Combat Simulator has N items in the battle", "Items in the battle"},
    {Fact::SimulatorOwner, "simulator_owner", T, false, "the open Combat Simulator's Owner for item is that race (\"race-1\" to \"race-10\")"},
    {Fact::DraftMessageType, "draft_message_type", T, false, "the message being written in Communicate is of that type (\"propose-treaty\", ...)"},
    {Fact::DraftTreaty, "draft_treaty", T, false, "the message being written in Communicate names that treaty (\"non-aggression\", ...)"},
    {Fact::BattleBegun, "battle_begun", F, false, "the open Tactical Combat window's battle has begun (Begin was pressed)"},
    {Fact::BattleOrder, "battle_order", T, true, "the player gave an order of that kind in a tactical battle"},
    {Fact::BattleTurn, "battle_turn", N, false, "the open Tactical Combat window's battle has begun and reached combat turn N", "Combat turn"},
    {Fact::Turn, "turn", N, false, "the game has reached turn N (the first turn is 0)", "Turn"},
    {Fact::TurnsPassed, "turns_passed", N, true, "N turns have ended", "Turns"},
    {Fact::Colonies, "colonies", N, false, "the empire has N colonies", "Colonies"},
    {Fact::Population, "population", N, false, "the empire's colonies hold N million people", "Population (millions)"},
    {Fact::Ships, "ships", N, false, "the empire has N ships (mothballed ones not counted)", "Ships"},
    {Fact::Bases, "bases", N, false, "the empire has N bases (mothballed ones not counted)", "Bases"},
    {Fact::Units, "units", N, false, "the empire has N units (fighters, satellites, mines, troops, drones, platforms)", "Units"},
    {Fact::Fleets, "fleets", N, false, "the empire has N fleets", "Fleets"},
    {Fact::FleetShips, "fleet_ships", N, false, "one of the empire's fleets holds N ships (of the design type beside it, if one is)",
     "Ships in one fleet"},
    {Fact::Designs, "designs", N, false, "the empire has N designs that are not obsolete", "Designs"},
    {Fact::ResearchQueued, "research_queued", N, false, "N research projects are queued", "Research projects"},
    {Fact::ConstructionQueued, "construction_queued", N, false, "N items wait in the empire's construction queues", "Items in the queues"},
    {Fact::TechsResearched, "techs_researched", N, true, "N tech levels have been researched", "Tech levels researched"},
    {Fact::SystemsExplored, "systems_explored", N, false, "the empire has explored N systems (its home system included)", "Systems explored"},
    {Fact::EmpiresMet, "empires_met", N, false, "the empire has met N other empires", "Empires met"},
    {Fact::Treaties, "treaties", N, false, "the empire holds N treaties (Non-Aggression or better)", "Treaties"},
    {Fact::Treaty, "treaty", T, false, "the empire holds a treaty of that kind with another empire (\"war\": is at war with one)"},
    {Fact::EnemyShipsDestroyed, "enemy_ships_destroyed", N, true,
     "N ships or bases of other empires were destroyed in battles the empire fought", "Enemy ships destroyed"},
    {Fact::PlanetsCaptured, "planets_captured", N, true, "the empire took N colonies from empires it is hostile to", "Planets captured"},
    {Fact::Score, "score", N, false, "the empire's score is N", "Score"},
    {Fact::Minerals, "minerals", N, false, "the empire has N minerals stored", "Minerals"},
    {Fact::Organics, "organics", N, false, "the empire has N organics stored", "Organics"},
    {Fact::Radioactives, "radioactives", N, false, "the empire has N radioactives stored", "Radioactives"},
    {Fact::Option, "option", T, false, "that setting of the empire is on"},
};
static_assert(std::size(kFacts) == static_cast<size_t>(Fact::Count));
static_assert([] {
    for (size_t i = 0; i < std::size(kFacts); ++i)
        if (static_cast<size_t>(kFacts[i].fact) != i) return false;
    return true;
}());

bool validEmpire(const game::GameState& s, game::EmpireId e) { return e.valid() && e.index() < s.empires.size(); }

bool contains(const std::vector<std::string>& v, std::string_view x) { return std::find(v.begin(), v.end(), x) != v.end(); }

std::span<const game::Command> commandsSince(const EvalContext& ctx) {
    const auto& all = ctx.tracker.commands();
    const size_t from = std::min(ctx.mark.commands, all.size());
    return std::span<const game::Command>(all).subspan(from);
}

// The design type of a design, a vehicle and a fleet (one of its ships must
// have it: a ship added by mistake must not make a step impossible), checked
// against a `design_type` qualifier; nothing to check passes.
bool designOfType(const game::GameState& s, game::DesignId d, std::string_view wanted) {
    return d.valid() && d.index() < s.designs.size() && designTypeMatches(s.design(d).designType, wanted);
}
bool vehicleOfType(const game::GameState& s, game::VehicleId id, std::string_view wanted) {
    const game::Vehicle* v = id.valid() ? s.vehicle(id) : nullptr;
    return v && designOfType(s, v->design, wanted);
}
// The most ships one of the empire's fleets holds (of a design type, when
// one is given).
int64_t fleetShips(const game::GameState& s, game::EmpireId me, std::string_view wanted) {
    int64_t most = 0;
    for (const game::Fleet& fl : s.fleets) {
        if (fl.owner != me) continue;
        const int64_t n = std::count_if(fl.members.begin(), fl.members.end(),
                                        [&](game::VehicleId id) { return wanted.empty() ? id.valid() && s.vehicle(id) != nullptr : vehicleOfType(s, id, wanted); });
        most = std::max(most, n);
    }
    return most;
}
bool fleetOfType(const game::GameState& s, game::FleetId id, std::string_view wanted) {
    const game::Fleet* f = id.valid() ? s.fleet(id) : nullptr;
    return f && std::any_of(f->members.begin(), f->members.end(), [&](game::VehicleId m) { return vehicleOfType(s, m, wanted); });
}
// Whether a message is of the type and names the treaty a condition asks for.
bool messageOfKind(const game::Command& cmd, const Condition& c) {
    if (c.messageType.empty() && c.messageTreaty.empty()) return true;
    const auto* m = std::get_if<game::cmd::SendMessage>(&cmd);
    if (!m) return false;
    if (!c.messageType.empty() && optionId(game::displayName(m->message.type)) != c.messageType) return false;
    if (!c.messageTreaty.empty() && optionId(game::displayName(m->message.treaty)) != c.messageTreaty) return false;
    return true;
}

// Whether a command went to (or made) a vehicle or design of the wanted type.
bool commandOfType(const game::GameState& s, const game::Command& cmd, std::string_view wanted) {
    if (wanted.empty()) return true;
    if (const auto* o = std::get_if<game::cmd::SetOrders>(&cmd))
        return o->vehicle.valid() ? vehicleOfType(s, o->vehicle, wanted) : fleetOfType(s, o->fleet, wanted);
    if (const auto* q = std::get_if<game::cmd::QueueAdd>(&cmd))
        return q->item.kind == game::QueueItem::Kind::Vehicle && designOfType(s, q->item.design, wanted);
    if (const auto* d = std::get_if<game::cmd::CreateDesign>(&cmd)) return designTypeMatches(d->design.designType, wanted);
    if (const auto* j = std::get_if<game::cmd::JoinFleet>(&cmd)) return vehicleOfType(s, j->vehicle, wanted);
    if (const auto* f = std::get_if<game::cmd::CreateFleet>(&cmd))
        return std::any_of(f->members.begin(), f->members.end(), [&](game::VehicleId m) { return vehicleOfType(s, m, wanted); });
    return false;
}

void collectCounters(const Condition& c, const EvalContext& ctx, size_t limit, std::vector<Counter>& out) {
    switch (c.op) {
        case Condition::Op::All:
        case Condition::Op::Any:
            for (const Condition& x : c.children) collectCounters(x, ctx, limit, out);
            return;
        case Condition::Op::Not: return;   // "fewer than" counts nothing up
        case Condition::Op::Fact: break;
    }
    const FactInfo& info = factInfo(c.fact);
    if (info.value != FactValue::Number || info.counter.empty() || out.size() >= limit) return;
    if (std::any_of(out.begin(), out.end(), [&](const Counter& k) { return k.fact == c.fact; })) return;
    const int64_t now = c.fact == Fact::FleetShips ? fleetShips(ctx.state, ctx.empire, c.designType) : factValue(c.fact, ctx);
    out.push_back({c.fact, std::max<int64_t>(0, now), c.number, std::string(info.counter)});
}

} // namespace

std::span<const FactInfo> facts() { return kFacts; }

const FactInfo* findFact(std::string_view key) {
    for (const FactInfo& f : kFacts)
        if (f.key == key) return &f;
    return nullptr;
}

const FactInfo& factInfo(Fact f) { return kFacts[static_cast<size_t>(f)]; }

bool isDesignTypeName(std::string_view type) { return game::ai::isAiDesignType(type) || datafile::keysEqual(type, "Colony"); }

bool designTypeMatches(std::string_view actual, std::string_view wanted) {
    if (datafile::keysEqual(actual, wanted)) return true;
    constexpr std::string_view kColony = "Colony (";
    return datafile::keysEqual(wanted, "Colony") && actual.size() > kColony.size() &&
           datafile::keysEqual(actual.substr(0, kColony.size()), kColony);
}

bool commandTakesDesignType(std::string_view command) {
    return command == "SetOrders" || command == "QueueAdd" || command == "CreateDesign" || command == "JoinFleet" || command == "CreateFleet";
}

void Tracker::observe(const game::GameState& state, game::EmpireId empire) {
    // Colonies that came to us from an empire we are hostile to were taken
    // (by invasion: a gift needs a treaty).
    owners_.resize(std::max(owners_.size(), state.colonies.size()));
    for (size_t i = 0; i < state.colonies.size(); ++i) {
        const game::EmpireId now = state.colonies[i] ? state.colonies[i]->owner : game::EmpireId{};
        const game::EmpireId before = owners_[i];
        if (seeded_ && now == empire && before.valid() && before != empire && game::hostile(state, empire, before)) ++captured_;
        owners_[i] = now;
    }
    seeded_ = true;
    for (const game::CombatRecord& rec : state.combats) {
        if (std::find(rec.participants.begin(), rec.participants.end(), empire) == rec.participants.end()) continue;
        // A battle is known by where and when it was fought and what happened in it.
        Hasher h;
        h.add(rec.turn).add(rec.location.system.value).add(rec.location.sector.x).add(rec.location.sector.y);
        h.addSize(rec.pieces.size()).addSize(rec.events.size());
        for (const game::CombatEvent& ev : rec.events) h.add(ev.kind).add(ev.round).add(ev.piece).add(ev.target);
        const uint64_t key = h.value();
        const auto at = std::lower_bound(battles_.begin(), battles_.end(), key);
        if (at != battles_.end() && *at == key) continue;
        battles_.insert(at, key);
        std::vector<uint8_t> gone(rec.pieces.size(), 0);
        for (const game::CombatEvent& ev : rec.events) {
            if (ev.kind != game::CombatEvent::Kind::Destroyed || ev.piece >= rec.pieces.size() || gone[ev.piece]) continue;
            const game::CombatPiece& p = rec.pieces[ev.piece];
            if (p.kind != game::CombatPiece::Kind::Vehicle || !p.owner.valid() || p.owner == empire) continue;
            gone[ev.piece] = 1;
            ++destroyed_;
        }
    }
}

Mark markNow(const game::Rules& rules, const game::GameState& state, game::EmpireId empire, const Tracker& tracker, uint64_t selections,
             size_t battleOrders) {
    Mark m;
    m.turn = state.turn;
    m.commands = tracker.commands().size();
    m.techLevels = validEmpire(state, empire) ? game::research::totalLevels(rules, state.empire(empire)) : 0;
    m.enemyShipsDestroyed = tracker.enemyShipsDestroyed();
    m.planetsCaptured = tracker.planetsCaptured();
    m.selections = selections;
    m.battleOrders = battleOrders;
    return m;
}

int64_t factValue(Fact f, const EvalContext& ctx) {
    const game::GameState& s = ctx.state;
    const game::EmpireId me = ctx.empire;
    if (f == Fact::Turn) return s.turn;
    if (f == Fact::TurnsPassed) return s.turn >= ctx.mark.turn ? int64_t(s.turn - ctx.mark.turn) : 0;
    if (f == Fact::EnemyShipsDestroyed) return ctx.tracker.enemyShipsDestroyed() - ctx.mark.enemyShipsDestroyed;
    if (f == Fact::PlanetsCaptured) return ctx.tracker.planetsCaptured() - ctx.mark.planetsCaptured;
    // A window's work in progress; -1 while the window is closed, so no count holds.
    if (f == Fact::DesignComponents) return ctx.client.designComponents.value_or(-1);
    if (f == Fact::SimulatorOwners) return ctx.client.simulatorOwners;
    if (f == Fact::SimulatorItems) return ctx.client.simulatorItems;
    if (f == Fact::BattleTurn) return ctx.client.battleTurn;
    if (!validEmpire(s, me)) return 0;
    const game::Empire& e = s.empire(me);
    auto ownVehicles = [&](ruleset::VehicleType type) {
        int64_t n = 0;
        for (const game::Vehicle& v : s.vehicles)
            if (v.owner == me && v.count > 0 && v.status != game::VehicleStatus::Mothballed && game::vehicleType(ctx.rules, s, v) == type)
                ++n;
        return n;
    };
    switch (f) {
        case Fact::Colonies:
            return std::count_if(s.colonies.begin(), s.colonies.end(), [&](const auto& c) { return c && c->owner == me; });
        case Fact::Population: {
            int64_t n = 0;
            for (const auto& c : s.colonies)
                if (c && c->owner == me) n += c->totalPopulation();
            return n;
        }
        case Fact::Ships: return ownVehicles(ruleset::VehicleType::Ship);
        case Fact::Bases: return ownVehicles(ruleset::VehicleType::Base);
        case Fact::Units: return game::unitCount(ctx.rules, s, me);
        case Fact::Fleets: return std::count_if(s.fleets.begin(), s.fleets.end(), [&](const game::Fleet& fl) { return fl.owner == me; });
        case Fact::FleetShips: return fleetShips(s, me, {});
        case Fact::Designs:
            return std::count_if(e.designs.begin(), e.designs.end(),
                                 [&](game::DesignId d) { return d.index() < s.designs.size() && !s.design(d).obsolete; });
        case Fact::ResearchQueued: return static_cast<int64_t>(e.research.size());
        case Fact::ConstructionQueued: {
            int64_t n = 0;
            for (const auto& c : s.colonies)
                if (c && c->owner == me) n += static_cast<int64_t>(c->queue.items.size());
            for (const game::Vehicle& v : s.vehicles)
                if (v.owner == me) n += static_cast<int64_t>(v.queue.items.size());
            return n;
        }
        case Fact::TechsResearched: return game::research::totalLevels(ctx.rules, e) - ctx.mark.techLevels;
        case Fact::SystemsExplored: return std::count_if(e.knowledge.explored.begin(), e.knowledge.explored.end(), [](uint8_t x) { return x != 0; });
        case Fact::EmpiresMet:
        case Fact::Treaties: {
            int64_t n = 0;
            for (size_t i = 0; i < e.relations.size() && i < s.empires.size(); ++i) {
                if (i == me.index() || !s.empires[i].alive) continue;
                const game::Relation& r = e.relations[i];
                if (!r.contact) continue;
                if (f == Fact::EmpiresMet || r.treaty >= game::Treaty::NonAggression) ++n;
            }
            return n;
        }
        case Fact::Score: return game::score::empireScore(ctx.rules, s, me);
        case Fact::Minerals: return e.stockpile.v[0];
        case Fact::Organics: return e.stockpile.v[1];
        case Fact::Radioactives: return e.stockpile.v[2];
        default: return 0;
    }
}

bool holds(const Condition& c, const EvalContext& ctx) {
    switch (c.op) {
        case Condition::Op::All:
            return std::all_of(c.children.begin(), c.children.end(), [&](const Condition& x) { return holds(x, ctx); });
        case Condition::Op::Any:
            return std::any_of(c.children.begin(), c.children.end(), [&](const Condition& x) { return holds(x, ctx); });
        case Condition::Op::Not: return c.children.empty() || !holds(c.children.front(), ctx);
        case Condition::Op::Fact: break;
    }
    switch (c.fact) {
        case Fact::Window: return contains(ctx.client.openWindows, c.text);
        // Only a selection the player made since the step began: the one a
        // game starts with, or one made for an earlier step, does not count.
        case Fact::Selected:
            return contains(ctx.client.selected, c.text) && ctx.client.selections > ctx.mark.selections &&
                   (c.designType.empty() || vehicleOfType(ctx.state, ctx.client.selectedVehicle, c.designType));
        case Fact::Tab: return contains(ctx.client.tabs, c.text);
        case Fact::FleetShips: return fleetShips(ctx.state, ctx.empire, c.designType) >= c.number;
        case Fact::BattleBegun: return ctx.client.battleBegun == (c.number != 0);
        case Fact::BattleOrder: {
            const auto& log = ctx.client.battleOrders;
            return std::find(log.begin() + std::ptrdiff_t(std::min(ctx.mark.battleOrders, log.size())), log.end(), c.text) != log.end();
        }
        case Fact::DesignHullChosen: return ctx.client.designComponents.has_value() && ctx.client.designHullChosen == (c.number != 0);
        case Fact::DesignTypeChosen:
            return ctx.client.designComponents.has_value() && !ctx.client.designType.empty() && designTypeMatches(ctx.client.designType, c.text);
        case Fact::DesignNamed: return ctx.client.designComponents.has_value() && ctx.client.designNamed == (c.number != 0);
        case Fact::DesignVehicle: return ctx.client.designComponents.has_value() && ctx.client.designVehicle == c.text;
        case Fact::SimulatorOwner: return !ctx.client.simulatorOwner.empty() && ctx.client.simulatorOwner == c.text;
        case Fact::Picking: return !ctx.client.picking.empty() && ctx.client.picking == c.text;
        case Fact::MovementLines: return ctx.client.movementLines == (c.number != 0);
        case Fact::Route: {
            bool route = false;
            if (const game::Vehicle* v = ctx.client.selectedVehicle.valid() ? ctx.state.vehicle(ctx.client.selectedVehicle) : nullptr) {
                const game::Fleet* f = v->fleet.valid() ? ctx.state.fleet(v->fleet) : nullptr;
                const std::vector<game::Order>& orders = f ? game::fleetOrders(ctx.state, *f) : v->orders;
                route = std::any_of(orders.begin(), orders.end(), [&](const game::Order& o) {
                    return (o.kind == game::OrderKind::MoveTo && o.location != v->location) || o.kind == game::OrderKind::MoveToWaypoint;
                });
            }
            return route == (c.number != 0);
        }
        case Fact::DraftMessageType: return !ctx.client.draftMessageType.empty() && ctx.client.draftMessageType == c.text;
        case Fact::DraftTreaty: return !ctx.client.draftMessageType.empty() && ctx.client.draftTreaty == c.text;
        case Fact::Option: {
            if (!validEmpire(ctx.state, ctx.empire)) return false;
            return optionValue(ctx.state.empire(ctx.empire), c.text).value_or(false);
        }
        case Fact::Treaty: {
            const auto kind = treatyFromId(c.text);
            if (!kind || !validEmpire(ctx.state, ctx.empire)) return false;
            const game::Empire& e = ctx.state.empire(ctx.empire);
            for (size_t i = 0; i < e.relations.size() && i < ctx.state.empires.size(); ++i)
                if (i != ctx.empire.index() && ctx.state.empires[i].alive && e.relations[i].contact && e.relations[i].treaty == *kind) return true;
            return false;
        }
        case Fact::Command:
            for (const game::Command& cmd : commandsSince(ctx))
                if (game::commandName(cmd) == c.text && commandOfType(ctx.state, cmd, c.designType) && messageOfKind(cmd, c)) return true;
            return false;
        case Fact::Order: {
            const auto kind = orderKindFromId(c.text);
            if (!kind) return false;
            for (const game::Command& cmd : commandsSince(ctx))
                if (const auto* o = std::get_if<game::cmd::SetOrders>(&cmd))
                    if (std::any_of(o->orders.begin(), o->orders.end(), [&](const game::Order& order) { return order.kind == *kind; }) &&
                        commandOfType(ctx.state, cmd, c.designType))
                        return true;
            return false;
        }
        default: return factValue(c.fact, ctx) >= c.number;
    }
}

std::string Counter::text() const {
    if (target <= 0) return std::format("{}: {}", label, current);
    return std::format("{}: {} of {}", label, std::min(current, target), target);
}

Counter counterOf(Fact f, const EvalContext& ctx) {
    return {f, std::max<int64_t>(0, factValue(f, ctx)), 0, std::string(factInfo(f).counter)};
}

std::vector<Counter> counters(const Condition& c, const EvalContext& ctx, size_t limit) {
    std::vector<Counter> out;
    collectCounters(c, ctx, limit, out);
    return out;
}

std::string describe(const Condition& c) {
    auto list = [&](std::string_view op) {
        std::string out = std::format("{} = [", op);
        for (size_t i = 0; i < c.children.size(); ++i) out += (i ? ", " : "") + describe(c.children[i]);
        return out + "]";
    };
    switch (c.op) {
        case Condition::Op::All: return list("all");
        case Condition::Op::Any: return list("any");
        case Condition::Op::Not: return std::format("not = {{{}}}", c.children.empty() ? std::string{} : describe(c.children.front()));
        case Condition::Op::Fact: break;
    }
    const FactInfo& info = factInfo(c.fact);
    switch (info.value) {
        case FactValue::Text: {
            std::string out = std::format("{} = \"{}\"", info.key, c.text);
            if (!c.designType.empty()) out += std::format(", design_type = \"{}\"", c.designType);
            if (!c.messageType.empty()) out += std::format(", message_type = \"{}\"", c.messageType);
            if (!c.messageTreaty.empty()) out += std::format(", message_treaty = \"{}\"", c.messageTreaty);
            return out;
        }
        case FactValue::Flag: return std::format("{} = {}", info.key, c.number != 0 ? "true" : "false");
        case FactValue::Number: break;
    }
    if (!c.designType.empty()) return std::format("{} = {}, design_type = \"{}\"", info.key, c.number, c.designType);
    return std::format("{} = {}", info.key, c.number);
}

} // namespace opense4::learn
