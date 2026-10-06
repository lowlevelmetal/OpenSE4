// The rules tier (docs/sdk/rules.md, src/sdk/rules_engine.hpp): every hook
// called with its documented arguments, in both turn styles; the setup hooks;
// mod data and its round trips; the effects and their refusals; budgets and
// failures; games without rules mods unchanged. The scripts are the rules
// fixture mod's (tests/fixtures/mods/rules-fixture) and small mods written on
// the spot, run by OpenSE4's own opense4 package.

#include "rules_fixture.hpp"

#include "mod_fixture.hpp"

#include "game/hooks.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "net/pbem.hpp"
#include "sdk/rules.hpp"
#include "sdk/view.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;
using script::Value;

namespace {

void play(const Rules& r, GameState& s, int turns) {
    for (int t = 0; t < turns && !s.gameOver; ++t) processTurn(r, s, {});
}

std::vector<std::string> texts(const Value& list) {
    std::vector<std::string> out;
    if (list.isList())
        for (const Value& v : list.asList()) out.push_back(v.isString() ? v.asString() : script::describe(v));
    return out;
}

int64_t intAt(const Value& list, size_t i) {
    REQUIRE(list.isList());
    REQUIRE(i < list.size());
    REQUIRE(list.asList()[i].isInt());
    return list.asList()[i].asInt();
}

std::string textAt(const Value& list, size_t i) {
    REQUIRE(list.isList());
    REQUIRE(i < list.size());
    REQUIRE(list.asList()[i].isString());
    return list.asList()[i].asString();
}

// A mod written on the spot with one rules script, and rules that use it.
struct SpotMod {
    test::ModDir dir;
    mods::Package package;
    std::unique_ptr<Rules> rules;
    SpotMod(std::string_view tag, std::string_view id, std::string_view script, std::string_view manifest = {})
        : dir(tag, id, "1.0.0", manifest) {
        dir.file("scripts/spot.py", script);
        package = dir.open();
        rules = std::make_unique<Rules>(rulesRuleset({&package}));
    }
    sdk::PlayerSetup setup() const {
        sdk::PlayerSetup s;
        s.mods.push_back(package);
        return s;
    }
    GameState game(uint64_t seed, bool simultaneous, int empires = 3, int systems = 10, game::GameOptions* options = nullptr) const {
        GameSetup setup = playersSetup(seed, simultaneous, std::vector<Controller>(static_cast<size_t>(empires)), systems);
        if (options) {
            options->simultaneous = simultaneous;
            setup.options = *options;
            setup.options.systemCount = systems;
        }
        auto created = createGame(*rules, setup);
        REQUIRE_MESSAGE(created.has_value(), (created ? std::string{} : created.error()));
        return std::move(*created);
    }
};

void checkHookArguments(const GameState& s, bool simultaneous) {
    // turn_start and turn_end name the turn being processed.
    CHECK(intAt(lastOf(s, "turn_start"), 0) + 1 == static_cast<int64_t>(s.turn));
    CHECK(intAt(lastOf(s, "turn_end"), 0) + 1 == static_cast<int64_t>(s.turn));
    CHECK(intAt(lastOf(s, "check_victory"), 0) + 1 == static_cast<int64_t>(s.turn));
    // orders_applied: no empire in a simultaneous turn; each player's in a turn-based one.
    const Value orders = lastOf(s, "orders_applied");
    if (simultaneous) CHECK(orders.asList()[1].isNull());
    else CHECK(intAt(orders, 1) >= 0);
    // movement_day: days 1 to 30 in a simultaneous turn, 0 for a live run.
    if (simultaneous) {
        CHECK(intAt(lastOf(s, "movement_day"), 0) == 30);
        CHECK(callsOf(s, "movement_day") == 30 * static_cast<int64_t>(s.turn));
    } else {
        CHECK(intAt(lastOf(s, "movement_day"), 0) == 0);
    }
    // empire_end_of_turn: around each of the ten steps, before then after, in order.
    const std::vector<std::string> steps = texts(*modDataIn(s.modData).find("steps"));
    REQUIRE(steps.size() == 20);
    const std::vector<std::string> names{"intelligence", "research", "income", "maintenance", "population", "happiness", "construction",
                                         "repair", "supply", "ground_combat"};
    for (size_t i = 0; i < names.size(); ++i) {
        CHECK(steps[2 * i] == names[i] + ":before");
        CHECK(steps[2 * i + 1] == names[i] + ":after");
    }
    // A step filter: income, after, only.
    const Value income = lastOf(s, "income_after");
    CHECK(textAt(income, 1) == "income");
    CHECK(textAt(income, 2) == "after");
    CHECK(callsOf(s, "income_after") * 20 == callsOf(s, "empire_end_of_turn"));
    // colony_end_of_turn names a colony of an empire.
    const Value colony = lastOf(s, "colony_end_of_turn");
    CHECK(intAt(colony, 1) >= 0);
    // The events name what they are about.
    const Value destroyed = lastOf(s, "vehicle_destroyed");
    const std::string cause = textAt(destroyed, 2);
    CHECK_MESSAGE((cause == "battle" || cause == "mines" || cause == "hazard" || cause == "event" || cause == "maintenance" ||
                   cause == "empire_destroyed"),
                  cause);
    const Value tech = lastOf(s, "tech_researched");
    CHECK(intAt(tech, 2) >= 1);
    CHECK(textAt(tech, 1).starts_with("Test "));
    const Value treaty = lastOf(s, "treaty_changed");
    CHECK(textAt(treaty, 2) != textAt(treaty, 3));
    CHECK(intAt(lastOf(s, "vehicle_built"), 2) >= 1);
    const Value fired = lastOf(s, "event_fired");
    CHECK(!textAt(fired, 0).empty());
    // The order of a turn's moments.
    const std::vector<std::string> order = texts(*modDataIn(s.modData).find("order"));
    auto first = [&](std::string_view hook) { return std::find(order.begin(), order.end(), hook) - order.begin(); };
    CHECK(first("new_game") < first("after_galaxy"));
    CHECK(first("after_galaxy") < first("turn_start"));
    CHECK(first("turn_start") < first("movement_day"));
    CHECK(first("movement_day") < first("empire_end_of_turn"));
    CHECK(first("empire_end_of_turn") < first("check_victory"));
    CHECK(first("check_victory") < first("turn_end"));
    if (simultaneous) CHECK(first("orders_applied") < first("movement_day"));
    else CHECK(first("orders_applied") < first("empire_end_of_turn"));
}

} // namespace

TEST_CASE("sdk rules: every hook runs with its arguments in a simultaneous game") {
    InstalledRules installed;
    const Rules& r = rulesFixtureRules();
    GameState s = rulesGame(41, true);
    // The setup hooks ran while the game was made: new_game with the setup,
    // generate_galaxy before the empires were placed (its rename stands),
    // after_galaxy once the game was made (its mod data stands).
    CHECK(callsOf(s, "new_game") == 1);
    CHECK(callsOf(s, "generate_galaxy") == 1);
    CHECK(callsOf(s, "after_galaxy") == 1);
    const Value newGame = lastOf(s, "new_game");
    CHECK(intAt(newGame, 0) == 41);
    CHECK(intAt(newGame, 1) == 4);
    CHECK(intAt(lastOf(s, "generate_galaxy"), 1) == 0);   // no empire yet
    CHECK(intAt(lastOf(s, "after_galaxy"), 1) == 4);
    CHECK(s.galaxy.systems.front().name == "Fixture Prime");
    CHECK(modDataIn(s.empires[2].modData).find("founded")->asInt() == 0);
    for (const auto& c : s.colonies)
        if (c) CHECK(modDataIn(c->modData).find("home") != nullptr);
    play(r, s, 30);
    for (std::string_view hook : {"turn_start", "orders_applied", "movement_day", "vehicle_entered_sector", "before_battle", "after_battle",
                                  "vehicle_destroyed", "empire_end_of_turn", "colony_end_of_turn", "colony_founded", "vehicle_built",
                                  "tech_researched", "treaty_changed", "message_sent", "event_fired", "check_victory", "turn_end"})
        CHECK_MESSAGE(callsOf(s, hook) > 0, hook);
    checkHookArguments(s, true);
    const ModRulesState* st = rulesStateOf(s);
    REQUIRE(st != nullptr);
    CHECK(st->failures == 0);
    CHECK(st->playersSee);
    // colony_end_of_turn kept data on each colony it saw.
    int counted = 0;
    for (const auto& c : s.colonies)
        if (c && modDataIn(c->modData).find("turns")) ++counted;
    CHECK(counted > 4);
}

TEST_CASE("sdk rules: every hook runs with its arguments in a turn-based game") {
    InstalledRules installed;
    const Rules& r = rulesFixtureRules();
    GameState s = rulesGame(43, false);
    CHECK(callsOf(s, "new_game") == 1);
    play(r, s, 30);
    for (std::string_view hook : {"turn_start", "orders_applied", "movement_day", "vehicle_entered_sector", "empire_end_of_turn",
                                  "colony_end_of_turn", "colony_founded", "vehicle_built", "tech_researched", "message_sent", "event_fired",
                                  "check_victory", "turn_end"})
        CHECK_MESSAGE(callsOf(s, hook) > 0, hook);
    // A game turn starts and ends once, whatever the number of players.
    CHECK(callsOf(s, "turn_start") == 30);
    CHECK(callsOf(s, "turn_end") == 30);
    // orders_applied: once per player's turn.
    CHECK(callsOf(s, "orders_applied") >= 30 * 3);
    checkHookArguments(s, false);
    CHECK(rulesStateOf(s)->failures == 0);
}

TEST_CASE("sdk rules: a game without rules mods makes no session and plays as without the SDK") {
    // The engine's test rules have no mods.
    const Rules& r = test::engineRules();
    GameSetup setup = playersSetup(17, true, std::vector<Controller>(3), 10);
    auto plain = createGame(r, setup);
    REQUIRE(plain.has_value());
    GameState a = *plain;
    play(r, a, 6);
    {
        InstalledRules installed;
        CHECK_FALSE(sdk::needsSession(r, *plain, sdk::PlayerSetup{}));
        auto created = createGame(r, setup);
        REQUIRE(created.has_value());
        GameState b = std::move(*created);
        play(r, b, 6);
        CHECK(stateChecksum(a) == stateChecksum(b));
        CHECK(b.modRules.empty());
        CHECK(b.modData.empty());
    }
    // A game whose mod set names the rules mod, but whose scripts the
    // sessions cannot find: no hooks run either.
    sdk::installPlayers();
    GameState c = rulesGame(17, true, 3, 10);
    CHECK(c.modRules.empty());
    sdk::uninstallPlayers();
}

TEST_CASE("sdk rules: mod data is saved, sent to its owner when the mod allows, and checksummed") {
    InstalledRules installed;
    const Rules& r = rulesFixtureRules();
    GameState s = rulesGame(47, true, 3, 10);
    play(r, s, 3);
    REQUIRE_FALSE(s.modData.empty());
    REQUIRE_FALSE(modDataIn(s.empires[1].modData).asMap().empty());
    // Saved and loaded: the same data, the same checksum.
    auto loaded = deserializeState(serializeState(s));
    REQUIRE(loaded.has_value());
    CHECK(loaded->modData == s.modData);
    CHECK(loaded->empires[1].modData == s.empires[1].modData);
    // What the game keeps about the mod, but the budget its functions used
    // this turn: a game loaded mid-turn starts that count afresh.
    std::vector<ModRulesState> kept = s.modRules;
    for (ModRulesState& m : kept) m.budgetUsed = 0;
    CHECK(loaded->modRules == kept);
    CHECK(stateChecksum(*loaded) == stateChecksum(s));
    // The checksum covers it.
    GameState changed = s;
    changed.empires[1].modData.front().value = "{\"founded\": 99}";
    CHECK(stateChecksum(changed) != stateChecksum(s));
    // What a player's computer is sent (network and e-mail games): the empire's
    // own things' data of a mod that lets players see it, nothing else.
    const GameState view = redactForEmpire(r, s, EmpireId{1u});
    CHECK(view.modData.empty());
    CHECK(view.empires[1].modData == s.empires[1].modData);
    CHECK(view.empires[0].modData.empty());
    for (const auto& c : view.colonies)
        if (c && c->owner != EmpireId{1u}) CHECK(c->modData.empty());
    auto sent = deserializeState(serializeState(view));
    REQUIRE(sent.has_value());
    CHECK(sent->empires[1].modData == s.empires[1].modData);
    // An e-mail game's turn file holds the same view, and its game file the whole game.
    CHECK(serializeState(net::pbem::playerView(r, s, EmpireId{1u})) == serializeState(view));
    CHECK(deserializeState(serializeState(s))->modData == s.modData);
    // A computer player's view shows its own things' data of such a mod.
    const Value v = sdk::buildView(r, s, EmpireId{1u});
    const Value* my = v.find("my");
    REQUIRE(my != nullptr);
    const Value* data = my->find("mod_data");
    REQUIRE(data != nullptr);
    const Value* mine = data->find(kRulesMod);
    REQUIRE(mine != nullptr);
    CHECK(mine->find("empire")->find("founded") != nullptr);
    CHECK(mine->find("colonies")->size() > 0);
    // A mod that keeps its data from players: none in the view.
    GameState hidden = s;
    for (ModRulesState& m : hidden.modRules) m.playersSee = false;
    CHECK(redactForEmpire(r, hidden, EmpireId{1u}).empires[1].modData.empty());
    CHECK(sdk::buildView(r, hidden, EmpireId{1u}).find("my")->find("mod_data")->size() == 0);
}

TEST_CASE("sdk rules: effects change the game and refuse what they cannot do") {
    SpotMod mod("effects", "test.effects", R"PY(
from opense4 import rules


def attempt(game, name, fn):
    out = game.mod_data.setdefault("results", {})
    try:
        out[name] = ["ok", fn()]
    except Exception as e:
        out[name] = ["refused", type(e).__name__, str(e)]


@rules.on("turn_end")
def probe(game, fx):
    if game.turn != 1:
        return
    me = game.empire(0)
    other = game.empire(1)
    home = [c for c in game.colonies if c.owner_id == 0][0]
    attempt(game, "add_resources", lambda: fx.add_resources(me, minerals=100, organics=-10**12)["organics"] < 0)
    attempt(game, "add_research", lambda: fx.add_research(me, 50))
    attempt(game, "add_intelligence", lambda: fx.add_intelligence(me, 7))
    attempt(game, "grant_tech", lambda: fx.grant_tech(me, 0))
    attempt(game, "change_population", lambda: fx.change_population(home, 1))
    attempt(game, "population_none_left", lambda: fx.change_population(home, -10**12))
    attempt(game, "change_happiness", lambda: fx.change_happiness(home, 5))
    attempt(game, "bad_colony_type", lambda: fx.set_colony_type(home, "Nonsense Type"))
    attempt(game, "set_plague", lambda: fx.set_plague(home, 2))
    designs = [d for d in game.designs if d.owner_id == 0]
    theirs = [d for d in game.designs if d.owner_id != 0]
    if theirs:
        attempt(game, "foreign_design", lambda: fx.create_vehicle(me, theirs[0], home.location))
    if designs:
        made = fx.create_vehicle(me, designs[0], home.location)
        game.mod_data["made"] = made.id
        attempt(game, "damage", lambda: fx.damage(made, 1, "Probed."))
        attempt(game, "damage_fraction", lambda: fx.damage(made, 1.5))
        attempt(game, "repair", lambda: fx.repair(made))
        attempt(game, "change_supply", lambda: fx.change_supply(made, -1))
        attempt(game, "tag", lambda: fx.set_mod_data(made, {"probe": True}))
    attempt(game, "add_facility", lambda: fx.add_facility(home, 0))
    attempt(game, "remove_facility", lambda: fx.remove_facility(home, 999))
    attempt(game, "set_treaty_unmet", lambda: fx.set_treaty(me, other, "war"))
    attempt(game, "log", lambda: fx.log(me, "Probe text.", title="Probe"))
    attempt(game, "fire_unknown", lambda: fx.fire_event("nope"))
    attempt(game, "set_planet", lambda: fx.set_planet(home.planet, minerals=150, conditions=120))
    attempt(game, "link_systems", lambda: fx.link_systems(game.systems[0], game.systems[-1]))
    attempt(game, "link_again", lambda: fx.link_systems(game.systems[0], game.systems[-1]))
    attempt(game, "replace_galaxy_here", lambda: fx.replace_galaxy("x"))
    attempt(game, "set_option_here", lambda: fx.set_option("event_frequency", 0))
    attempt(game, "unknown_effect", lambda: fx._do("teleport"))
    attempt(game, "no_such_empire", lambda: fx.add_research(77, 5))
    attempt(game, "rng", lambda: [game.rng.below(6), game.rng.range(1, 3), game.rng.chance(50)])
    attempt(game, "empty_range", lambda: game.rng.range(3, 1))
    attempt(game, "game_data", lambda: fx.set_mod_data(None, dict(game.mod_data, kept=1)))
)PY");
    InstalledRules installed(mod.setup());
    GameState s = mod.game(53, true);
    const Resources before = s.empire(EmpireId{0u}).stockpile;
    play(*mod.rules, s, 2);
    const Value data = modDataIn(s.modData, "test.effects");
    const Value* results = data.find("results");
    REQUIRE(results != nullptr);
    auto result = [&](std::string_view name) {
        const Value* r = results->find(name);
        REQUIRE_MESSAGE(r != nullptr, name);
        return *r;
    };
    auto ok = [&](std::string_view name) {
        const Value r = result(name);
        CHECK_MESSAGE(textAt(r, 0) == "ok", name << ": " << script::describe(r));
    };
    auto refused = [&](std::string_view name, std::string_view type, std::string_view words) {
        const Value r = result(name);
        CHECK_MESSAGE(textAt(r, 0) == "refused", name << ": " << script::describe(r));
        if (r.size() == 3) {
            CHECK_MESSAGE(textAt(r, 1) == type, name << ": " << script::describe(r));
            CHECK_MESSAGE(textAt(r, 2).find(words) != std::string::npos, name << ": " << script::describe(r));
        }
    };
    for (std::string_view name : {"add_resources", "add_research", "add_intelligence", "grant_tech", "change_population", "change_happiness",
                                  "set_plague", "log", "set_planet", "link_systems", "rng", "game_data"})
        ok(name);
    if (textAt(result("add_facility"), 0) != "ok") refused("add_facility", "ValueError", "no free facility slot");
    refused("population_none_left", "ValueError", "nobody left");
    refused("bad_colony_type", "ValueError", "not one of the owner's colony types");
    if (results->find("foreign_design")) refused("foreign_design", "ValueError", "no design");
    refused("remove_facility", "ValueError", "facilities");
    refused("set_treaty_unmet", "ValueError", "have not met");
    refused("fire_unknown", "ValueError", "declares no event");
    refused("link_again", "ValueError", "linked already");
    refused("replace_galaxy_here", "RuntimeError", "generate_galaxy");
    refused("set_option_here", "RuntimeError", "new_game");
    refused("unknown_effect", "ValueError", "teleport");
    refused("no_such_empire", "ValueError", "no empire 77");
    refused("empty_range", "ValueError", "empty");
    CHECK(result("add_resources").asList()[1] == Value(true));   // organics were taken, not more than there were
    CHECK(data.find("kept") != nullptr);
    if (const Value* made = data.find("made")) {
        ok("damage");
        ok("repair");
        ok("change_supply");
        ok("tag");
        const Value r = result("damage_fraction");
        CHECK(textAt(r, 0) == "refused");   // a fraction cannot cross into the engine
        const Vehicle* v = s.vehicle(VehicleId{static_cast<uint32_t>(made->asInt())});
        if (v) CHECK(modDataIn(v->modData, "test.effects").find("probe") != nullptr);
    }
    // What they did.
    const Empire& me = s.empire(EmpireId{0u});
    CHECK(std::any_of(me.log.begin(), me.log.end(), [](const LogEntry& l) { return l.title == "Probe" && l.text == "Probe text."; }));
    CHECK(me.techLevels[0] >= 1);
    (void)before;
    CHECK(rulesStateOf(s, "test.effects")->failures == 0);
}

TEST_CASE("sdk rules: a failing hook is skipped for the turn; three failures turn the mod's rules off for the turn") {
    SpotMod mod("failures", "test.failures", R"PY(
from opense4 import rules


@rules.on("turn_start")
def boom(game, fx):
    raise RuntimeError("boom")


@rules.on("movement_day")
def spin(game, day, fx):
    game.mod_data["days"] = game.mod_data.get("days", 0) + 1
    if day == 2:
        while True:
            pass


@rules.on("empire_end_of_turn", step="income", when="before")
def third(game, empire, step, when, fx):
    raise ValueError("third")


@rules.on("turn_end")
def end(game, fx):
    game.mod_data["ends"] = game.mod_data.get("ends", 0) + 1
)PY");
    InstalledRules installed(mod.setup());
    GameOptions options;
    options.rulesHookBudget = 2'000'000;
    GameState s = mod.game(59, true, 3, 10, &options);
    play(*mod.rules, s, 1);
    const ModRulesState* st = rulesStateOf(s, "test.failures");
    REQUIRE(st != nullptr);
    CHECK(st->failures == 3);
    CHECK(std::find(st->failedHooks.begin(), st->failedHooks.end(), "turn_start") != st->failedHooks.end());
    CHECK(std::find(st->failedHooks.begin(), st->failedHooks.end(), "movement_day") != st->failedHooks.end());
    const Value data = modDataIn(s.modData, "test.failures");
    // movement_day ran on day 1, ran out of budget on day 2 and was skipped after.
    CHECK(data.find("days")->asInt() == 1);
    // After the third failure the mod's rules were off: turn_end did not run.
    CHECK(data.find("ends") == nullptr);
    // The next turn starts afresh: the counters reset, turn_start fails again.
    play(*mod.rules, s, 1);
    CHECK(rulesStateOf(s, "test.failures")->failures == 3);
    CHECK(modDataIn(s.modData, "test.failures").find("days")->asInt() == 2);
    // The same game played again gives the same state: failures are deterministic.
    GameState again = mod.game(59, true, 3, 10, &options);
    play(*mod.rules, again, 2);
    CHECK(stateChecksum(again) == stateChecksum(s));
}

TEST_CASE("sdk rules: a mod's hooks stop for the turn when they use up its budget") {
    SpotMod mod("budget", "test.budget", R"PY(
from opense4 import rules


@rules.on("movement_day")
def busy(game, day, fx):
    total = 0
    for i in range(20000):
        total += i
    game.mod_data["days"] = game.mod_data.get("days", 0) + 1
)PY");
    InstalledRules installed(mod.setup());
    GameOptions options;
    options.rulesTurnBudget = 400'000;
    GameState s = mod.game(61, true, 3, 10, &options);
    play(*mod.rules, s, 1);
    const ModRulesState* st = rulesStateOf(s, "test.budget");
    REQUIRE(st != nullptr);
    CHECK(std::find(st->failedHooks.begin(), st->failedHooks.end(), "*budget*") != st->failedHooks.end());
    const int64_t days = modDataIn(s.modData, "test.budget").find("days")->asInt();
    CHECK(days > 0);
    CHECK(days < 30);
    CHECK(st->budgetUsed >= options.rulesTurnBudget);
}
