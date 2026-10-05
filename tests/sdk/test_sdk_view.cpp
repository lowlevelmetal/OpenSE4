// The view a script sees (src/sdk/view.hpp): it follows the docs' schema,
// names only what it holds, keeps the fog of war unless it is whole, shows
// the engine's own figures, and never changes the game.

#include "engine_fixture.hpp"
#include "sdk/sdk_test_util.hpp"

#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/serialize.hpp"
#include "sdk/view.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <map>

using namespace opense4;
using namespace opense4::game;
using opense4::script::Value;
using namespace opense4::sdktest;

namespace {

const Value* findById(const Value& list, int64_t id) {
    for (const Value& x : list.asList())
        if (const Value* i = x.find("id"); i && i->isInt() && i->asInt() == id) return &x;
    return nullptr;
}

const Value* findBy(const Value& list, std::string_view key, int64_t id) {
    for (const Value& x : list.asList())
        if (const Value* i = x.find(key); i && i->isInt() && i->asInt() == id) return &x;
    return nullptr;
}

int64_t resource(const Value& r, std::string_view name) { return intAt(r, name); }

bool sameResources(const Value& v, const Resources& r) {
    return resource(v, "minerals") == r[Resource::Minerals] && resource(v, "organics") == r[Resource::Organics] &&
           resource(v, "radioactives") == r[Resource::Radioactives];
}

// Every id the view names, by kind, must be the id of something it lists.
void checkReferences(const Value& view, const References& refs) {
    std::map<std::string, std::set<int64_t>, std::less<>> listed{
        {"system", idsOf(at(view, "systems"))},   {"object", idsOf(at(view, "objects"))}, {"empire", idsOf(at(view, "empires"))},
        {"vehicle", idsOf(at(view, "vehicles"))}, {"fleet", idsOf(at(view, "fleets"))},   {"design", idsOf(at(view, "designs"))},
        {"message", idsOf(at(view, "messages"))},
    };
    size_t bad = 0;
    for (const auto& [kind, id] : refs.named) {
        const auto it = listed.find(kind);
        REQUIRE_MESSAGE(it != listed.end(), "the docs name an unknown kind of id: " << kind);
        if (it->second.contains(id)) continue;
        if (++bad <= 10) FAIL_CHECK("the view names " << kind << " " << id << " but does not list it");
    }
    CHECK(bad == 0);
}

} // namespace

TEST_CASE("sdk view: every field is documented, every documented field is there, every id it names is in it") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const GameState& s = g.state;
    for (const bool whole : {false, true}) {
        for (const Empire& e : s.empires) {
            INFO(std::format("{} of empire {}", whole ? "whole view" : "view", e.id.value));
            const Value v = sdk::buildView(r, s, e.id, {whole});
            References refs;
            const auto problems = validate(docsSchema(), v, "view", &refs);
            CHECK_MESSAGE(problems.empty(), joined(problems));
            checkReferences(v, refs);
            CHECK(refs.named.size() > 100);
        }
    }
    // The fixture has some of everything the schema describes.
    const Value v = sdk::buildView(r, s, EmpireId{0u});
    const Value& my = at(v, "my");
    for (std::string_view list : {"colonies", "vehicles", "fleets", "designs", "messages", "log", "battles", "objects"})
        CHECK_MESSAGE(at(v, list).size() > 0, list);
    CHECK(at(at(my, "research"), "queue").size() > 0);
    CHECK(at(at(my, "intel"), "queue").size() > 0);
    CHECK(at(my, "notes").size() > 0);
    CHECK(at(my, "strategies").size() > 0);
    bool queued = false;
    for (const Value& c : at(v, "colonies").asList())
        if (const Value& q = at(c, "queue"); q.isMap()) queued = queued || at(q, "items").size() > 0;
    CHECK(queued);
}

TEST_CASE("sdk view: an empire sees only what it knows, the whole view everything") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const GameState& s = g.state;
    const EmpireId me{0u}, other{1u};
    const Value fair = sdk::buildView(r, s, me);
    const Value whole = sdk::buildView(r, s, me, {.whole = true});
    CHECK(at(fair, "whole") == Value(false));
    CHECK(at(whole, "whole") == Value(true));
    CHECK(intAt(fair, "empire") == 0);

    // Foreign vehicles: only those it sees this turn, without their plans.
    const auto& visible = s.empire(me).knowledge.visibleVehicles;
    for (const Value& v : at(fair, "vehicles").asList()) {
        if (intAt(v, "owner") == 0) {
            CHECK(at(v, "orders").isList());
            continue;
        }
        CHECK(std::find(visible.begin(), visible.end(), VehicleId{static_cast<uint32_t>(intAt(v, "id"))}) != visible.end());
        CHECK(at(v, "orders").isNull());
        CHECK(at(v, "cargo").isNull());
        CHECK(at(v, "supply").isNull());
        CHECK(at(v, "fleet").isNull());
    }
    CHECK(findById(at(fair, "vehicles"), g.seenForeign.value) != nullptr);
    CHECK(findById(at(fair, "vehicles"), g.hiddenForeign.value) == nullptr);
    const Value* hidden = findById(at(whole, "vehicles"), g.hiddenForeign.value);
    REQUIRE(hidden != nullptr);
    CHECK(at(*hidden, "orders").isList());
    CHECK(at(whole, "vehicles").size() == s.vehicles.size());

    // The unexplored system: where it is, not what it holds.
    const Value* sys = findById(at(fair, "systems"), g.unexplored.value);
    REQUIRE(sys != nullptr);
    CHECK(at(*sys, "explored") == Value(false));
    CHECK(at(*sys, "name").isNull());
    CHECK(at(*sys, "objects").isNull());
    CHECK(at(at(*sys, "position"), "x").isInt());
    for (const Value& o : at(fair, "objects").asList()) CHECK(intAt(o, "system") != g.unexplored.value);
    const Value* wsys = findById(at(whole, "systems"), g.unexplored.value);
    REQUIRE(wsys != nullptr);
    CHECK(at(*wsys, "name").asString() == s.galaxy.system(g.unexplored).name);
    CHECK(at(*wsys, "explored") == Value(false));   // still the empire's own knowledge
    CHECK(at(*wsys, "objects").size() == s.galaxy.system(g.unexplored).objects.size());

    // Colonies: foreign ones only in explored systems, never their contents.
    size_t colonies = 0;
    for (const auto& c : s.colonies) colonies += c.has_value();
    CHECK(at(whole, "colonies").size() == colonies);
    for (const Value& c : at(fair, "colonies").asList()) {
        const ObjectId planet{static_cast<uint32_t>(intAt(c, "planet"))};
        if (intAt(c, "owner") == 0) {
            CHECK(at(c, "facilities").isList());
            continue;
        }
        CHECK(s.empire(me).hasExplored(s.galaxy.object(planet).system));
        CHECK(at(c, "facilities").isNull());
        CHECK(at(c, "queue").isNull());
        CHECK(at(c, "cargo").isNull());
    }

    // Other empires' treasuries and plans.
    const Value* them = findById(at(fair, "empires"), other.value);
    REQUIRE(them != nullptr);
    CHECK(at(*them, "stored").isNull());
    CHECK(at(*them, "economy").isNull());
    CHECK(at(*them, "home_system").isNull());
    CHECK(at(*them, "is_me") == Value(false));
    CHECK(at(*them, "relation").isMap());
    const Value* wthem = findById(at(whole, "empires"), other.value);
    REQUIRE(wthem != nullptr);
    CHECK(sameResources(at(*wthem, "stored"), s.empire(other).stockpile));

    // Messages to or from us only; in the whole view every one.
    for (const Value& m : at(fair, "messages").asList()) CHECK((intAt(m, "from_empire") == 0 || intAt(m, "to_empire") == 0));
    CHECK(at(whole, "messages").size() == s.messages.size());

    // Designs: foreign ones known by sight show their parts, not their records.
    for (const Value& d : at(fair, "designs").asList()) {
        if (intAt(d, "owner") == 0) {
            CHECK(at(d, "built").isInt());
            continue;
        }
        CHECK(at(d, "built").isNull());
        CHECK(at(d, "known").isBool());
    }
    const Value* raider = findById(at(fair, "vehicles"), g.seenForeign.value);
    REQUIRE(raider != nullptr);
    const Value* raiderDesign = findById(at(fair, "designs"), intAt(*raider, "design"));
    REQUIRE(raiderDesign != nullptr);
    CHECK(at(*raiderDesign, "known") == Value(true));
    CHECK(at(*raiderDesign, "figures").isMap());
}

TEST_CASE("sdk view: the empire's own figures are the engine's") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const GameState& s = g.state;
    const EmpireId me{0u};
    const Empire& e = s.empire(me);
    const Value v = sdk::buildView(r, s, me);
    const Value& my = at(v, "my");
    CHECK(sameResources(at(my, "stored"), e.stockpile));
    CHECK(intAt(at(my, "economy"), "research") == e.economy.research);
    CHECK(intAt(my, "maintenance_percent") == economy::maintenancePercent(r, e));

    // Research: the queue with each project's cost and the engine's estimate.
    const Value& research = at(my, "research");
    CHECK(intAt(research, "points") == e.researchPool);
    REQUIRE(at(research, "queue").size() == e.research.size());
    for (size_t i = 0; i < e.research.size(); ++i) {
        const Value& p = at(research, "queue").asList()[i];
        CHECK(intAt(p, "area") == e.research[i].area.value);
        CHECK(intAt(p, "eta") == research::etaTurns(r, s, e, i));
        CHECK(intAt(p, "cost") == research::levelCost(r, s, e.research[i].area, e.techLevel(e.research[i].area) + 1));
    }
    CHECK(at(research, "levels").size() == e.techLevels.size());

    // Vehicles: movement, supply and cargo as the rules count them.
    size_t checked = 0;
    for (const Vehicle& x : s.vehicles) {
        if (x.owner != me) continue;
        const Value* vv = findById(at(v, "vehicles"), x.id.value);
        REQUIRE(vv != nullptr);
        CHECK(intAt(*vv, "max_movement") == vehicleMaxMovement(r, s, x));
        CHECK(intAt(*vv, "movement") == x.movement);
        CHECK(intAt(*vv, "supply") == x.supply);
        CHECK(intAt(*vv, "supply_capacity") == vehicleSupplyCapacity(r, s, x));
        CHECK(intAt(*vv, "cargo_capacity") == vehicleCargoCapacity(r, s, x));
        CHECK(intAt(*vv, "structure") == vehicleStructure(r, s, x));
        CHECK(intAt(at(*vv, "location"), "system") == x.location.system.value);
        ++checked;
    }
    CHECK(checked > 2);
    for (const Fleet& f : s.fleets) {
        if (f.owner != me) continue;
        const Value* fv = findById(at(v, "fleets"), f.id.value);
        REQUIRE(fv != nullptr);
        CHECK(intAt(*fv, "speed") == movement::fleetSpeed(r, s, f));
        CHECK(at(*fv, "members").size() == f.members.size());
    }

    // Colonies: queues at the engine's rate and costs.
    size_t queues = 0;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != me) continue;
        const Value* cv = findBy(at(v, "colonies"), "planet", c->planet.value);
        REQUIRE(cv != nullptr);
        CHECK(intAt(*cv, "total_population") == c->totalPopulation());
        CHECK(intAt(*cv, "max_population") == maxPopulation(r, s, *c));
        const Value& q = at(*cv, "queue");
        const cmd::QueueTarget t{c->planet, {}};
        CHECK(sameResources(at(q, "rate"), economy::constructionRate(r, s, me, t)));
        REQUIRE(at(q, "items").size() == c->queue.items.size());
        for (size_t i = 0; i < c->queue.items.size(); ++i) {
            CHECK(sameResources(at(at(q, "items").asList()[i], "cost"), economy::itemCost(r, s, me, t, c->queue.items[i])));
            ++queues;
        }
    }
    CHECK(queues > 0);

    // Designs: the designer's figures.
    for (DesignId d : e.designs) {
        const Value* dv = findById(at(v, "designs"), d.value);
        REQUIRE(dv != nullptr);
        const DesignStats st = computeDesignStats(r, nullptr, s.design(d));
        const Value& f = at(*dv, "figures");
        CHECK(sameResources(at(f, "cost"), st.cost));
        CHECK(intAt(f, "tonnage_used") == st.tonnageUsed);
        CHECK(intAt(f, "movement") == st.movement);
        CHECK(intAt(f, "supply") == st.supplyCapacity);
        CHECK(sameResources(at(f, "maintenance"), economy::designMaintenance(r, s, s.design(d))));
    }
}

TEST_CASE("sdk view: building a view changes nothing, and the same game gives the same view") {
    const Rules& r = test::engineRules();
    const SdkGame g = sdkGame();
    const uint64_t before = stateChecksum(g.state);
    const Value a = sdk::buildView(r, g.state, EmpireId{1u});
    const Value b = sdk::buildView(r, g.state, EmpireId{1u});
    const Value wa = sdk::buildView(r, g.state, EmpireId{1u}, {.whole = true});
    const Value wb = sdk::buildView(r, g.state, EmpireId{1u}, {.whole = true});
    CHECK(stateChecksum(g.state) == before);
    CHECK(a == b);
    CHECK(wa == wb);
    CHECK_FALSE(a == wa);
    // A copy of the game gives the same view.
    const GameState copy = g.state;
    CHECK(sdk::buildView(r, copy, EmpireId{1u}) == a);
}
