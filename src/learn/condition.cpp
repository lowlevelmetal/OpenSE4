#include "learn/condition.hpp"

#include "core/hash.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <format>

namespace opense4::learn {

namespace {

constexpr FactInfo kFacts[] = {
    {Fact::Window, "window", true, false, "that window is open (a window id)"},
    {Fact::Selected, "selected", true, false, "the main window has that kind of thing selected"},
    {Fact::Command, "command", true, true, "the player gave a command of that type (game/commands.hpp)"},
    {Fact::Order, "order", true, true, "the player gave a ship, fleet or planet an order of that kind"},
    {Fact::Turn, "turn", false, false, "the game has reached turn N (the first turn is 0)"},
    {Fact::TurnsPassed, "turns_passed", false, true, "N turns have ended"},
    {Fact::Colonies, "colonies", false, false, "the empire has N colonies"},
    {Fact::Population, "population", false, false, "the empire's colonies hold N million people"},
    {Fact::Ships, "ships", false, false, "the empire has N ships (mothballed ones not counted)"},
    {Fact::Bases, "bases", false, false, "the empire has N bases (mothballed ones not counted)"},
    {Fact::Units, "units", false, false, "the empire has N units (fighters, satellites, mines, troops, drones, platforms)"},
    {Fact::Fleets, "fleets", false, false, "the empire has N fleets"},
    {Fact::Designs, "designs", false, false, "the empire has N designs that are not obsolete"},
    {Fact::ResearchQueued, "research_queued", false, false, "N research projects are queued"},
    {Fact::ConstructionQueued, "construction_queued", false, false, "N items wait in the empire's construction queues"},
    {Fact::TechsResearched, "techs_researched", false, true, "N tech levels have been researched"},
    {Fact::SystemsExplored, "systems_explored", false, false, "the empire has explored N systems (its home system included)"},
    {Fact::EmpiresMet, "empires_met", false, false, "the empire has met N other empires"},
    {Fact::Treaties, "treaties", false, false, "the empire holds N treaties (Non-Aggression or better)"},
    {Fact::EnemyShipsDestroyed, "enemy_ships_destroyed", false, true,
     "N ships or bases of other empires were destroyed in battles the empire fought"},
    {Fact::Score, "score", false, false, "the empire's score is N"},
    {Fact::Minerals, "minerals", false, false, "the empire has N minerals stored"},
    {Fact::Organics, "organics", false, false, "the empire has N organics stored"},
    {Fact::Radioactives, "radioactives", false, false, "the empire has N radioactives stored"},
};
static_assert(std::size(kFacts) == static_cast<size_t>(Fact::Count));

bool validEmpire(const game::GameState& s, game::EmpireId e) { return e.valid() && e.index() < s.empires.size(); }

bool contains(const std::vector<std::string>& v, std::string_view x) { return std::find(v.begin(), v.end(), x) != v.end(); }

std::span<const game::Command> commandsSince(const EvalContext& ctx) {
    const auto& all = ctx.tracker.commands();
    const size_t from = std::min(ctx.mark.commands, all.size());
    return std::span<const game::Command>(all).subspan(from);
}

} // namespace

std::span<const FactInfo> facts() { return kFacts; }

const FactInfo* findFact(std::string_view key) {
    for (const FactInfo& f : kFacts)
        if (f.key == key) return &f;
    return nullptr;
}

const FactInfo& factInfo(Fact f) { return kFacts[static_cast<size_t>(f)]; }

void Tracker::observe(const game::GameState& state, game::EmpireId empire) {
    for (const game::CombatRecord& rec : state.combats) {
        if (std::find(rec.participants.begin(), rec.participants.end(), empire) == rec.participants.end()) continue;
        // A battle is known by where and when it was fought and what happened in it.
        Hasher h;
        h.add(rec.turn).add(rec.location.system.value).add(rec.location.sector.x).add(rec.location.sector.y);
        h.add(rec.pieces.size()).add(rec.events.size());
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

Mark markNow(const game::Rules& rules, const game::GameState& state, game::EmpireId empire, const Tracker& tracker) {
    Mark m;
    m.turn = state.turn;
    m.commands = tracker.commands().size();
    m.techLevels = validEmpire(state, empire) ? game::research::totalLevels(rules, state.empire(empire)) : 0;
    m.enemyShipsDestroyed = tracker.enemyShipsDestroyed();
    return m;
}

int64_t factValue(Fact f, const EvalContext& ctx) {
    const game::GameState& s = ctx.state;
    const game::EmpireId me = ctx.empire;
    if (f == Fact::Turn) return s.turn;
    if (f == Fact::TurnsPassed) return s.turn >= ctx.mark.turn ? int64_t(s.turn - ctx.mark.turn) : 0;
    if (f == Fact::EnemyShipsDestroyed) return ctx.tracker.enemyShipsDestroyed() - ctx.mark.enemyShipsDestroyed;
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
        case Fact::Selected: return contains(ctx.client.selected, c.text);
        case Fact::Command:
            for (const game::Command& cmd : commandsSince(ctx))
                if (game::commandName(cmd) == c.text) return true;
            return false;
        case Fact::Order: {
            const auto kind = orderKindFromId(c.text);
            if (!kind) return false;
            for (const game::Command& cmd : commandsSince(ctx))
                if (const auto* o = std::get_if<game::cmd::SetOrders>(&cmd))
                    for (const game::Order& order : o->orders)
                        if (order.kind == *kind) return true;
            return false;
        }
        default: return factValue(c.fact, ctx) >= c.number;
    }
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
    return info.text ? std::format("{} = \"{}\"", info.key, c.text) : std::format("{} = {}", info.key, c.number);
}

} // namespace opense4::learn
