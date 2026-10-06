// The queries a script may ask (src/sdk/queries.hpp): answers that are the
// engine's own, results that follow the docs, and arguments checked.

#include "engine_fixture.hpp"
#include "sdk/sdk_test_util.hpp"

#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/serialize.hpp"
#include "sdk/queries.hpp"

#include <doctest/doctest.h>

#include <map>

using namespace opense4;
using namespace opense4::game;
using opense4::script::Value;
using opense4::script::ValueList;
using opense4::script::ValueMap;
using namespace opense4::sdktest;

namespace {

Value map(std::initializer_list<std::pair<std::string, Value>> entries) { return Value(ValueMap(entries)); }
Value list(std::initializer_list<Value> items) { return Value(ValueList(items)); }
Value loc(const Location& l) { return map({{"system", Value(l.system.value)}, {"x", Value(int{l.sector.x})}, {"y", Value(int{l.sector.y})}}); }

Value ask(const sdk::Queries& q, std::string_view name, const Value& args, std::string_view resultType) {
    const auto res = q.call(name, args);
    REQUIRE_MESSAGE(res.has_value(), name << ": " << (res ? std::string{} : res.error().text()));
    const auto problems = validate(docsSchema(), *res, resultType);
    CHECK_MESSAGE(problems.empty(), name << " returns what the docs do not say:\n" << joined(problems));
    return *res;
}

std::string askError(const sdk::Queries& q, std::string_view name, const Value& args) {
    const auto res = q.call(name, args);
    return res ? std::string("(answered)") : res.error().text();
}

bool sameResources(const Value& v, const Resources& r) {
    return intAt(v, "minerals") == r[Resource::Minerals] && intAt(v, "organics") == r[Resource::Organics] &&
           intAt(v, "radioactives") == r[Resource::Radioactives];
}

const Vehicle& ownScout(const GameState& s, EmpireId me) {
    for (const Vehicle& v : s.vehicles)
        if (v.owner == me && !v.fleet.valid() && s.design(v.design).designType == "Scout") return v;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == me) return v;
    FAIL("no vehicle");
    return s.vehicles.front();
}

} // namespace

TEST_CASE("sdk queries: the table lists every query once") {
    const SdkGame g = sdkGame();
    const sdk::Queries q(test::engineRules(), g.state, EmpireId{0u});
    std::set<std::string> names;
    for (const auto& f : q.functions()) CHECK(names.insert(f.name).second);
    for (std::string_view n : {"path", "movement", "design_figures", "abilities", "queue_forecast", "research_forecast", "colonize_problem",
                               "queue_item_problem"})
        CHECK_MESSAGE(names.contains(std::string(n)), n);
    CHECK(askError(q, "teleport", Value()) == "'teleport' is not a query");
}

TEST_CASE("sdk queries: paths are the engine's routes over what the empire knows") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const GameState& s = g.state;
    const EmpireId me{0u};
    const sdk::Queries q(r, s, me);
    const Vehicle& scout = ownScout(s, me);

    // Between places, over the routes the empire knows.
    size_t routes = 0;
    for (const StarSystem& sys : s.galaxy.systems) {
        if (!s.empire(me).hasExplored(sys.id)) continue;
        const Location to{sys.id, Sector{2, 9}};
        const Value res = ask(q, "path", map({{"origin", loc(scout.location)}, {"destination", loc(to)}}), "path_result");
        const auto engine = movement::findPath(r, s, me, scout.location, to);
        CHECK(at(res, "found") == Value(engine.has_value()));
        if (!engine) continue;
        ++routes;
        CHECK(intAt(res, "length") == engine->length);
        REQUIRE(at(res, "steps").size() == engine->steps.size());
        int jumps = 0;
        Location here = scout.location;
        for (size_t i = 0; i < engine->steps.size(); ++i) {
            CHECK(intAt(at(res, "steps").asList()[i], "system") == engine->steps[i].system.value);
            jumps += engine->steps[i].system != here.system;
            here = engine->steps[i];
        }
        CHECK(intAt(res, "jumps") == jumps);
        CHECK(at(res, "turns").isNull());
    }
    CHECK(routes > 0);

    // For a vehicle: from where it is, with its estimate of turns.
    const Location home = scout.location;
    const Location target{home.system, Sector{0, 0}};
    const Value res = ask(q, "path", map({{"vehicle", Value(scout.id.value)}, {"destination", loc(target)}}), "path_result");
    CHECK(at(res, "found") == Value(true));
    CHECK(intAt(res, "turns") == movement::etaTurns(r, s, scout, target));
    CHECK(intAt(res, "jumps") == 0);
    // A system id stands for the system's centre.
    const Value bySystem = ask(q, "path", map({{"origin", Value(home.system.value)}, {"destination", Value(home.system.value)}}), "path_result");
    CHECK(at(bySystem, "found") == Value(true));

    // What it does not know it cannot route through, unless the view is whole.
    CHECK(askError(q, "path", map({{"origin", loc(home)}, {"destination", loc(home)}, {"omniscient", true}})).starts_with("omniscient: "));
    const sdk::Queries whole(r, s, me, {.whole = true});
    const Location far{g.unexplored, Sector{3, 3}};
    const Value known = ask(q, "path", map({{"origin", loc(home)}, {"destination", loc(far)}}), "path_result");
    const Value all = ask(whole, "path", map({{"origin", loc(home)}, {"destination", loc(far)}, {"omniscient", true}}), "path_result");
    CHECK(at(known, "found") == Value(movement::findPath(r, s, me, home, far).has_value()));
    CHECK(at(all, "found") == Value(movement::findPath(r, s, EmpireId{}, home, far).has_value()));
    // A whole view routes over everything by default, and as the empire knows when asked.
    CHECK(ask(whole, "path", map({{"origin", loc(home)}, {"destination", loc(far)}}), "path_result") == all);
    CHECK(ask(whole, "path", map({{"origin", loc(home)}, {"destination", loc(far)}, {"omniscient", false}}), "path_result") == known);

    CHECK(askError(q, "path", map({{"destination", loc(home)}})) == "origin: missing: give a place, a vehicle or a fleet");
    CHECK(askError(q, "path", map({{"origin", loc(home)}})) == "destination: missing");
    CHECK(askError(q, "path", map({{"vehicle", Value(g.hiddenForeign.value)}, {"destination", loc(home)}})) == "vehicle: no such vehicle in view");
    CHECK(askError(q, "path", map({{"vehicle", Value(g.seenForeign.value)}, {"destination", loc(home)}})) == "vehicle: not one of our vehicles");
    CHECK(askError(q, "path", map({{"origin", 5000}, {"destination", loc(home)}})) == "origin: no such place");
    CHECK(askError(q, "path", map({{"origin", "home"}})) == "origin: expected a map");
}

TEST_CASE("sdk queries: movement and supply range") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const GameState& s = g.state;
    const EmpireId me{0u};
    const sdk::Queries q(r, s, me);
    const Vehicle& scout = ownScout(s, me);
    const Value v = ask(q, "movement", map({{"vehicle", Value(scout.id.value)}}), "vehicle_movement");
    CHECK(intAt(v, "max_movement") == vehicleMaxMovement(r, s, scout));
    CHECK(intAt(v, "moves_per_turn") == movement::movesPerTurn(s, vehicleMaxMovement(r, s, scout)));
    CHECK(intAt(v, "supply") == scout.supply);
    CHECK(intAt(v, "supply_per_move") == movement::moveSupplyCost(r, s, scout));
    if (const int64_t cost = movement::moveSupplyCost(r, s, scout); cost > 0 && !vehicleHasUnlimitedSupply(r, s, scout))
        CHECK(intAt(v, "moves_on_supply") == scout.supply / cost);

    bool fleets = false;
    for (const Fleet& f : s.fleets) {
        if (f.owner != me) continue;
        fleets = true;
        const Value fv = ask(q, "movement", map({{"fleet", Value(f.id.value)}}), "fleet_movement");
        CHECK(intAt(fv, "max_movement") == movement::fleetSpeed(r, s, f));
        CHECK(at(fv, "members").size() == fleetMembersAt(s, f).size());
    }
    CHECK(fleets);
    CHECK(askError(q, "movement", Value()) == "vehicle: missing: name a vehicle or a fleet");
}

TEST_CASE("sdk queries: a design's figures, legal or not, as the designer shows them") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const GameState& s = g.state;
    const EmpireId me{0u};
    const sdk::Queries q(r, s, me);

    // An existing design.
    const DesignId d = s.empire(me).designs.front();
    const Value existing = ask(q, "design_figures", map({{"design", Value(d.value)}}), "design_figures");
    const DesignStats st = computeDesignStats(r, nullptr, s.design(d));
    CHECK(sameResources(at(existing, "cost"), st.cost));
    CHECK(intAt(existing, "structure") == st.structure);
    CHECK(sameResources(at(existing, "maintenance"), economy::designMaintenance(r, s, s.design(d))));

    // A proposal from components: the empire's technology decides what is legal.
    const uint32_t hull = test::hullIndex(r, "Test Frigate");
    ValueList parts;
    std::vector<DesignEntry> entries;
    for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser"}) {
        parts.push_back(Value(test::componentIndex(r, c)));
        entries.push_back({test::componentIndex(r, c), -1});
    }
    const Value proposed = ask(q, "design_figures", map({{"hull", Value(hull)}, {"components", Value(parts)}}), "design_figures");
    const DesignStats pst = computeDesignStats(r, &s.empire(me), hull, entries);
    CHECK(at(proposed, "valid") == Value(pst.problems.empty()));
    CHECK(at(proposed, "problems").size() == pst.problems.size());
    CHECK(intAt(proposed, "tonnage_used") == pst.tonnageUsed);
    const auto [offense, defense] = designToHit(r, hull, entries);
    CHECK(intAt(proposed, "offense_bonus") == offense);
    CHECK(intAt(proposed, "defense_bonus") == defense);

    // Too much for the hull: illegal, and the engine says why.
    ValueList many = parts;
    for (int i = 0; i < 40; ++i) many.push_back(Value(test::componentIndex(r, "Test Armor Plate")));
    const Value heavy = ask(q, "design_figures", map({{"hull", Value(hull)}, {"components", Value(many)}}), "design_figures");
    CHECK(at(heavy, "valid") == Value(false));
    CHECK(at(heavy, "problems").size() > 0);

    CHECK(askError(q, "design_figures", map({{"hull", 9999}})) == "hull: no such hull");
    CHECK(askError(q, "design_figures", map({{"hull", Value(hull)}, {"components", list({9999})}})) == "components[0]: no such component");
    CHECK(askError(q, "design_figures", map({{"hull", Value(hull)}, {"entries", list({map({{"component", 0}, {"mount", 77}})})}})) ==
          "entries[0].mount: no such mount");
    CHECK(askError(q, "design_figures", Value()) == "hull: missing: name a design, or a hull and its components");
}

TEST_CASE("sdk queries: ability values read in each ability's own mode") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const GameState& s = g.state;
    const EmpireId me{0u};
    const sdk::Queries q(r, s, me);
    const Vehicle& scout = ownScout(s, me);
    const Value v = ask(q, "abilities", map({{"vehicle", Value(scout.id.value)}}), "ability_report");
    const std::vector<ParsedAbility> list = vehicleAbilities(r, s, scout);
    CHECK(at(v, "entries").size() == list.size());
    bool supply = false;
    for (const Value& x : at(v, "values").asList()) {
        if (at(x, "name").asString() != identifier(AbilityKind::SupplyStorage)) continue;
        supply = true;
        CHECK(intAt(x, "value") == abilityValue(list, AbilityKind::SupplyStorage));
        CHECK(at(x, "aggregation") == Value("sum"));
    }
    CHECK(supply);

    const Colony& home = test::homeworld(const_cast<GameState&>(s), me);
    const Value planet = ask(q, "abilities", map({{"planet", Value(home.planet.value)}}), "ability_report");
    CHECK(at(planet, "entries").size() == colonyAbilities(r, s, home).size());
    ask(q, "abilities", map({{"facility", Value(test::facilityIndex(r, "Test Lab"))}}), "ability_report");
    ask(q, "abilities", map({{"component", Value(test::componentIndex(r, "Test Supply Pod"))}}), "ability_report");
    ask(q, "abilities", map({{"hull", Value(test::hullIndex(r, "Test Frigate"))}}), "ability_report");
    ask(q, "abilities", map({{"design", Value(scout.design.value)}}), "ability_report");
    ask(q, "abilities", map({{"system", Value(scout.location.system.value)}}), "ability_report");
    CHECK(askError(q, "abilities", map({{"vehicle", Value(g.hiddenForeign.value)}})) == "vehicle: no such vehicle in view");
    CHECK(askError(q, "abilities", map({{"system", Value(g.unexplored.value)}})) == "system: no such system explored");
    CHECK(askError(q, "abilities", Value()).starts_with("name exactly one of"));
}

TEST_CASE("sdk queries: an ability a mod declares combines as the mod declares it") {
    // Three declared abilities, each twice on one component: summed, the
    // largest and the smallest value (docs/sdk/packages-and-data.md).
    ruleset::Ruleset rs = test::engineRules().data();
    rs.declaredAbilities.push_back({"Test Total", ruleset::Combine::Sum, "test.declared"});
    rs.declaredAbilities.push_back({"Test Largest", ruleset::Combine::Max, "test.declared"});
    rs.declaredAbilities.push_back({"Test Smallest", ruleset::Combine::Min, "test.declared"});
    ruleset::Component pod = rs.components.front();
    pod.name = "Test Declared Pod";
    pod.abilities.clear();
    for (const char* name : {"Test Total", "Test Largest", "Test Smallest"})
        for (const char* value : {"4", "9"}) pod.abilities.push_back({name, "", value, "0"});
    rs.components.push_back(pod);
    rs.reindex();
    const Rules r(std::move(rs));
    const SdkGame g = sdkGame();
    const sdk::Queries q(r, g.state, EmpireId{0u});
    const Value v = ask(q, "abilities", map({{"component", Value(static_cast<int64_t>(r.data().components.size() - 1))}}), "ability_report");
    CHECK(at(v, "entries").size() == 6);
    std::map<std::string, std::pair<std::string, int64_t>> totals;
    for (const Value& x : at(v, "values").asList()) totals[at(x, "name").asString()] = {at(x, "aggregation").asString(), intAt(x, "value")};
    CHECK(totals["Test Total"] == std::pair<std::string, int64_t>{"sum", 13});
    CHECK(totals["Test Largest"] == std::pair<std::string, int64_t>{"largest", 9});
    CHECK(totals["Test Smallest"] == std::pair<std::string, int64_t>{"smallest", 4});
    // As the rules read them.
    CHECK(r.declaredAbilityOfComponent(static_cast<uint32_t>(r.data().components.size() - 1), "Test Smallest") == 4);
}

TEST_CASE("sdk queries: construction and research forecasts") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const GameState& s = g.state;
    const EmpireId me{0u};
    const Empire& e = s.empire(me);
    const sdk::Queries q(r, s, me);
    const Colony& home = test::homeworld(const_cast<GameState&>(s), me);
    const cmd::QueueTarget t{home.planet, {}};

    const Value now = ask(q, "queue_forecast", map({{"planet", Value(home.planet.value)}}), "queue");
    const Resources rate = economy::constructionRate(r, s, me, t);
    CHECK(sameResources(at(now, "rate"), rate));
    REQUIRE(at(now, "items").size() == home.queue.items.size());
    int done = 0;
    for (size_t i = 0; i < home.queue.items.size(); ++i) {
        const Value& item = at(now, "items").asList()[i];
        const Resources cost = economy::itemCost(r, s, me, t, home.queue.items[i]);
        CHECK(sameResources(at(item, "cost"), cost));
        const int turns = economy::turnsToComplete(max(cost - home.queue.items[i].spent, Resources{}), rate);
        if (turns < 0 || home.queue.onHold) break;
        done += std::max(1, turns);
        CHECK(intAt(item, "done_in") == done);
    }
    // A queue that is not there: what it would finish and when.
    QueueItem lab;
    lab.kind = QueueItem::Kind::Facility;
    lab.facility = test::facilityIndex(r, "Test Lab");
    const Value plan = ask(q, "queue_forecast",
                           map({{"planet", Value(home.planet.value)},
                                {"items", list({map({{"kind", "facility"}, {"facility", Value(lab.facility)}, {"count", 2}})})}}),
                           "queue");
    REQUIRE(at(plan, "items").size() == 1);
    lab.count = 2;
    const Resources labCost = economy::itemCost(r, s, me, t, lab);
    CHECK(sameResources(at(at(plan, "items").asList()[0], "cost"), labCost));
    CHECK(intAt(at(plan, "items").asList()[0], "turns") == std::max(1, economy::turnsToComplete(labCost, rate)));
    CHECK(askError(q, "queue_forecast", map({{"planet", Value(static_cast<int64_t>(g.state.galaxy.objects.size() - 1))}})).starts_with("planet: "));
    CHECK(askError(q, "queue_forecast", map({{"planet", Value(home.planet.value)}, {"items", list({map({{"facility", 9999}, {"kind", "facility"}})})}})) ==
          "items[0].facility: no such facility");

    // Research: each level's cost, and the turns when nothing else is researched.
    const ruleset::TechAreaId area = test::techArea(r, "Test Beams");
    const int current = e.techLevel(area);
    const Value res = ask(q, "research_forecast", map({{"area", Value(area.value)}, {"level", current + 2}}), "research_forecast");
    CHECK(intAt(res, "current_level") == current);
    REQUIRE(at(res, "levels").size() == 2);
    CHECK(intAt(at(res, "levels").asList()[0], "cost") == research::levelCost(r, s, area, current + 1));
    CHECK(intAt(at(res, "levels").asList()[1], "cost") == research::levelCost(r, s, area, current + 2));
    CHECK(intAt(res, "total_cost") == research::levelCost(r, s, area, current + 1) + research::levelCost(r, s, area, current + 2));
    Empire alone = e;
    alone.research = {{area, 0}};
    for (const ResearchProject& p : e.research)
        if (p.area == area) alone.research[0].progress = p.progress;
    CHECK(intAt(at(res, "levels").asList()[0], "turns") == research::etaTurns(r, s, alone, 0));
    CHECK(at(res, "researchable") == Value(research::isResearchable(r, s, e, area)));
    CHECK(askError(q, "research_forecast", map({{"area", 9999}})) == "area: no such tech area");
}

TEST_CASE("sdk queries: problems, as the engine states them") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const GameState& s = g.state;
    const EmpireId me{0u};
    const sdk::Queries q(r, s, me);
    const Colony& home = test::homeworld(const_cast<GameState&>(s), me);
    const Vehicle& scout = ownScout(s, me);
    const Value colonize = ask(q, "colonize_problem", map({{"vehicle", Value(scout.id.value)}, {"planet", Value(home.planet.value)}}), "problem");
    CHECK(at(colonize, "problem").asString() == movement::colonizeProblem(r, s, scout, home.planet));
    CHECK_FALSE(at(colonize, "problem").asString().empty());

    QueueItem item;
    item.kind = QueueItem::Kind::Facility;
    item.facility = test::facilityIndex(r, "Test Lab");
    const Value fits = ask(q, "queue_item_problem",
                           map({{"planet", Value(home.planet.value)}, {"item", map({{"kind", "facility"}, {"facility", Value(item.facility)}})}}),
                           "problem");
    CHECK(at(fits, "problem").asString() == queueItemProblem(r, s, me, {home.planet, {}}, item));
}

TEST_CASE("sdk queries: asking changes nothing") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const uint64_t before = stateChecksum(g.state);
    const sdk::Queries fair(r, g.state, EmpireId{0u});
    const sdk::Queries whole(r, g.state, EmpireId{0u}, {.whole = true});
    const Vehicle& scout = ownScout(g.state, EmpireId{0u});
    for (const sdk::Queries* q : {&fair, &whole}) {
        (void)q->call("path", map({{"vehicle", Value(scout.id.value)}, {"destination", loc({scout.location.system, Sector{1, 1}})}}));
        (void)q->call("movement", map({{"vehicle", Value(scout.id.value)}}));
        (void)q->call("research_forecast", map({{"area", 0}, {"level", 5}}));
    }
    CHECK(stateChecksum(g.state) == before);
}
