// What mods declare to the rules tier (docs/sdk/rules.md): orders given by
// a human (in both turn styles, over the network), by a script computer
// player and by an external bot, and played again from the journal; the
// client's list of orders; events, intelligence projects, options, victory
// conditions, scenarios, ability values and data generators.

#include "rules_fixture.hpp"

#include "mod_fixture.hpp"

#include "client/classic/screens/setup_model.hpp"
#include "game/hooks.hpp"
#include "game/intel.hpp"
#include "game/query.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "mods/data_set.hpp"
#include "sdk/codec.hpp"
#include "sdk/players.hpp"
#include "sdk/rules.hpp"
#include "sdk/rules_view.hpp"
#include "sdk/scenario.hpp"
#include "sdk/view.hpp"
#include "server/setup_file.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;
using script::Value;
using script::ValueList;
using script::ValueMap;

namespace {

void play(const Rules& r, GameState& s, int turns) {
    for (int t = 0; t < turns && !s.gameOver; ++t) processTurn(r, s, {});
}

cmd::ModCommand order(std::string name, std::string args = {}) {
    cmd::ModCommand c;
    c.mod = kRulesMod;
    c.name = std::move(name);
    c.args = std::move(args);
    return c;
}

int64_t intIn(const std::vector<ModData>& data, std::string_view key) {
    const Value v = modDataIn(data);
    const Value* x = v.find(key);
    return x && x->isInt() ? x->asInt() : 0;
}

const Vehicle* ownVehicle(const GameState& s, EmpireId e) {
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && v.count > 0) return &v;
    return nullptr;
}

const Colony* ownColony(const GameState& s, EmpireId e) {
    for (const auto& c : s.colonies)
        if (c && c->owner == e) return &*c;
    return nullptr;
}

// A game whose first empire is a human player's.
GameState humanGame(uint64_t seed, bool simultaneous) {
    GameSetup setup = playersSetup(seed, simultaneous, std::vector<Controller>(3), 10);
    setup.empires[0].kind = PlayerKind::Human;
    auto created = createGame(rulesFixtureRules(), setup);
    REQUIRE_MESSAGE(created.has_value(), (created ? std::string{} : created.error()));
    test::addHomeShips(*created, rulesFixtureRules());
    return std::move(*created);
}

struct Bot final : sdk::ExternalBot {
    std::function<Value(const Value& request)> answer;
    std::expected<Value, std::string> request(const Value& r, const sdk::ServiceCall&) override {
        // The request and the answer cross as JSON text, as on a connection.
        auto sent = script::parseJson(*script::toJson(r));
        REQUIRE(sent.has_value());
        const Value reply = answer ? answer(*sent) : Value(ValueMap{});
        auto back = script::parseJson(*script::toJson(reply));
        REQUIRE(back.has_value());
        return *back;
    }
};

} // namespace

TEST_CASE("sdk rules orders: a human's mod order travels with the turn's orders and takes effect") {
    InstalledRules installed;
    const Rules& r = rulesFixtureRules();
    GameState s = humanGame(71, true);
    GameState without = s;
    // The orders go to the host as the network and e-mail games send them.
    EmpireOrders mine{EmpireId{0u}, s.turn, {order("bounty", R"({"times": 3})")}};
    auto received = deserializeOrders(serializeOrders(mine));
    REQUIRE(received.has_value());
    REQUIRE(std::holds_alternative<cmd::ModCommand>(received->commands.front()));
    CHECK(std::get<cmd::ModCommand>(received->commands.front()) == std::get<cmd::ModCommand>(mine.commands.front()));
    const TurnResult result = processTurn(r, s, std::vector<EmpireOrders>{*received});
    CHECK_MESSAGE(result.rejected.empty(), (result.rejected.empty() ? std::string() : result.rejected.front().second));
    CHECK(intIn(s.empire(EmpireId{0u}).modData, "bounties") == 3);
    processTurn(r, without, std::vector<EmpireOrders>{EmpireOrders{EmpireId{0u}, without.turn, {}}});
    // The bounty option's default, 25 minerals, three times.
    CHECK(s.empire(EmpireId{0u}).stockpile[Resource::Minerals] - without.empire(EmpireId{0u}).stockpile[Resource::Minerals] == 75);
}

TEST_CASE("sdk rules orders: what the declaration and the mod's check refuse") {
    InstalledRules installed;
    const Rules& r = rulesFixtureRules();
    GameState s = humanGame(73, true);
    const EmpireId me{0u};
    const Vehicle* mine = ownVehicle(s, me);
    const Vehicle* theirs = ownVehicle(s, EmpireId{1u});
    const Colony* colony = ownColony(s, me);
    REQUIRE(mine != nullptr);
    REQUIRE(colony != nullptr);
    auto refusal = [&](const cmd::ModCommand& c) { return apply(r, s, me, c).error; };
    auto refuses = [&](const cmd::ModCommand& c, std::string_view words) {
        const std::string why = refusal(c);
        CHECK_MESSAGE(why.find(words) != std::string::npos, "expected '" << words << "', got '" << why << "'");
    };
    cmd::ModCommand c = order("bounty");
    c.mod = "test.nobody";
    refuses(c, "no rules of the mod test.nobody");
    refuses(order("teleport"), "has no order named teleport");
    c = order("bounty");
    c.vehicle = mine->id;
    refuses(c, "given to the empire itself");
    refuses(order("tag", R"({"note": "x"})"), "given to one vehicle");
    c = order("tag", R"({"note": "x"})");
    if (theirs) {
        c.vehicle = theirs->id;
        refuses(c, "needs one of your vehicles");
    }
    c.vehicle = mine->id;
    c.args = R"({"note": "x", "colour": 3})";
    refuses(c, "has no argument 'colour'");
    c.args = "{}";
    refuses(c, "needs its argument 'note'");
    c.args = R"({"note": 4})";
    refuses(c, "'note' should be text");
    c.args = R"({"note": "x", "about": 99})";
    refuses(c, "'about' names no empire");
    refuses(order("bounty", R"({"times": 9})"), "outside 1 to 5");
    refuses(order("bounty", R"({"times": "two"})"), "should be a whole number");
    c = order("refused");
    c.planet = colony->planet;
    refuses(c, "The fixture refuses this order.");
    // Accepted: its effect runs at once.
    c = order("tag", R"({"note": "flagship", "about": 1})");
    c.vehicle = mine->id;
    CHECK(apply(r, s, me, c).ok);
    const Value data = modDataIn(s.vehicle(mine->id)->modData);
    CHECK(data.find("tag")->asString() == "flagship");
    CHECK(data.find("about")->asInt() == 1);
    // Without the SDK a mod order is refused, saying why.
    sdk::uninstallPlayers();
    CHECK(apply(r, s, me, order("bounty")).error.find("needs the modding SDK") != std::string::npos);
    sdk::installPlayers();
}

TEST_CASE("sdk rules orders: the client lists the orders an object takes and builds them") {
    InstalledRules installed;
    const Rules& r = rulesFixtureRules();
    GameState s = humanGame(79, false);
    const EmpireId me{0u};
    const std::span<const mods::Package> extra(&rulesFixtureMod(), 1);
    auto names = [&](const sdk::ModOrderTarget& t) {
        std::vector<std::string> out;
        for (const sdk::ModOrderChoice& c : sdk::modOrders(r, s, me, t, extra)) out.push_back(c.order.name);
        return out;
    };
    CHECK(names({"self", -1}) == std::vector<std::string>{"bounty"});
    const Vehicle* mine = ownVehicle(s, me);
    REQUIRE(mine != nullptr);
    CHECK(names({"vehicle", mine->id.value}) == std::vector<std::string>{"tag"});
    CHECK(names({"colony", ownColony(s, me)->planet.value}) == std::vector<std::string>{"refused"});
    if (const Vehicle* theirs = ownVehicle(s, EmpireId{1u})) CHECK(names({"vehicle", theirs->id.value}).empty());
    CHECK(names({"empire", 1}).empty());   // the fixture has no order for another empire
    const std::vector<sdk::ModOrderChoice> choices = sdk::modOrders(r, s, me, {"self", -1}, extra);
    REQUIRE(choices.size() == 1);
    CHECK(choices[0].order.label == "Claim a bounty");
    REQUIRE(choices[0].order.args.size() == 1);
    CHECK(choices[0].order.args[0].defaultValue == Value(int64_t{1}));
    // In a turn-based game the order acts at once, in the player's turn.
    resumeTurnBased(r, s);
    REQUIRE(activePlayer(s) == me);
    const cmd::ModCommand c = sdk::modOrderCommand(choices[0], {"self", -1}, Value(ValueMap{{"times", Value(int64_t{2})}}));
    const TurnResult res = applyLive(r, s, me, c);
    CHECK(res.rejected.empty());
    CHECK(intIn(s.empire(me).modData, "bounties") == 2);
    // The codec carries it for scripts and bots: {"kind": "mod_command", ...}.
    const Value encoded = sdk::encodeCommand(c);
    CHECK(encoded.find("kind")->asString() == "mod_command");
    CHECK(encoded.find("args")->find("times")->asInt() == 2);
    auto decoded = sdk::decodeCommand(encoded);
    REQUIRE(decoded.has_value());
    CHECK(std::get<cmd::ModCommand>(*decoded) == c);
    CHECK_FALSE(sdk::decodeCommand(Value(ValueMap{{"kind", Value("mod_command")}, {"args", Value(int64_t{3})}})).has_value());
}

TEST_CASE("sdk rules orders: a script computer player gives a mod order, and a replay gives it again from the journal") {
    sdk::PlayerSetup setup;
    InstalledRules installed(std::move(setup));
    const Rules& r = rulesFixtureRules();
    Controller hunter;
    hunter.kind = Controller::Kind::Script;
    hunter.mod = kRulesMod;
    hunter.player = "Bounty Hunter";
    GameState s = rulesGame(83, true, 3, 10, {hunter});
    play(r, s, 3);
    CHECK(intIn(s.empire(EmpireId{0u}).modData, "bounties") == 6);
    CHECK(memoryOf(s, EmpireId{0u}).find("orders")->asInt() == 3);
    const GameState before = s;
    processTurn(r, s, {});
    CHECK(intIn(s.empire(EmpireId{0u}).modData, "bounties") == 8);
    GameState again = before;
    replayJournal(again, s);
    REQUIRE_FALSE(again.journal.replay.empty());
    processTurn(r, again, {});
    CHECK(again.journal.replay.empty());
    CHECK(stateChecksum(again) == stateChecksum(s));
}

TEST_CASE("sdk rules orders: an external bot gives a mod order through the protocol") {
    Bot bot;
    bot.answer = [](const Value& request) -> Value {
        if (request.find("call")->asString() != "orders") return Value(ValueMap{});
        ValueList commands;
        commands.push_back(Value(ValueMap{{"kind", Value("mod_command")}, {"mod", Value(kRulesMod)}, {"name", Value("bounty")},
                                          {"args", Value(ValueMap{{"times", Value(int64_t{4})}})}}));
        return Value(ValueMap{{"commands", Value(std::move(commands))}});
    };
    sdk::PlayerSetup setup;
    setup.externals = [&bot](uint32_t slot) -> sdk::ExternalBot* { return slot == 0 ? &bot : nullptr; };
    InstalledRules installed(std::move(setup));
    Controller external;
    external.kind = Controller::Kind::External;
    GameState s = rulesGame(89, true, 3, 10, {external});
    play(rulesFixtureRules(), s, 2);
    CHECK(intIn(s.empire(EmpireId{0u}).modData, "bounties") == 8);
}

TEST_CASE("sdk rules orders: a mod's event fires with its chance and every mod hears of it") {
    InstalledRules installed;
    const Rules& r = rulesFixtureRules();
    GameState s = rulesGame(97, true, 3, 10);
    play(r, s, 2);
    CHECK(intIn(s.modData, "windfalls") == 0);   // not before turn 2
    play(r, s, 3);
    CHECK(intIn(s.modData, "windfalls") == 3);   // a 100 % chance each turn from then on
    // event_fired names the mod's events with their mod.
    bool heard = false;
    GameState t = rulesGame(97, true, 3, 10);
    for (int i = 0; i < 5 && !heard; ++i) {
        processTurn(r, t, {});
        const Value last = lastOf(t, "event_fired");
        heard = last.isList() && last.asList()[0] == Value("windfall") && last.asList()[1] == Value(kRulesMod);
    }
    CHECK(heard);
}

TEST_CASE("sdk rules orders: a mod's intelligence project runs next to the classic ones") {
    InstalledRules installed;
    const Rules& r = rulesFixtureRules();
    GameState s = rulesGame(101, true, 3, 10);
    const EmpireId spy{0u}, victim{1u};
    s.empire(spy).relation(victim).contact = true;
    s.empire(victim).relation(spy).contact = true;
    s.empire(spy).kind = PlayerKind::Human;   // its queue is its own
    uint32_t project = 0;
    for (uint32_t i = 0; i < r.data().intelProjects.size(); ++i)
        if (r.data().intelProjects[i].type == "Mod - Test Theft") project = i;
    IntelProjectOrder o;
    o.project = project;
    o.target = victim;
    o.progress = r.data().intelProjects[project].cost;
    s.empire(spy).intel = {o};
    s.empire(spy).intelPool = 0;
    CHECK(intel::orderProblem(r, s, spy, o).empty());
    processTurn(r, s, std::vector<EmpireOrders>{EmpireOrders{spy, s.turn, {}}});
    CHECK(intIn(s.modData, "thefts") == 1);
    const std::vector<LogEntry>& log = s.empire(spy).log;
    CHECK(std::any_of(log.begin(), log.end(), [](const LogEntry& l) { return l.title == "Test Theft"; }));
}

TEST_CASE("sdk rules orders: options are declared, set, kept with the game and read by the mod") {
    const std::vector<sdk::ModOptionChoice> choices = sdk::modOptions(std::span<const mods::Package>(&rulesFixtureMod(), 1));
    REQUIRE(choices.size() == 2);
    CHECK(choices[0].key() == "test.rules-fixture:bounty");
    CHECK(choices[1].option.isSwitch);
    GameOptions o;
    CHECK(sdk::setModOption(o, choices, "test.rules-fixture:bounty", 40).empty());
    CHECK(sdk::modOptionValue(o, choices[0]) == 40);
    CHECK(sdk::setModOption(o, choices, "test.rules-fixture:bounty", 4000).find("takes 0 to 1000") != std::string::npos);
    CHECK(sdk::setModOption(o, choices, "test.rules-fixture:beacons", 2).find("switch") != std::string::npos);
    CHECK(sdk::setModOption(o, choices, "test.rules-fixture:nope", 1).find("declares the option") != std::string::npos);
    // A new game holds every declared option, at its default unless set.
    InstalledRules installed;
    GameSetup setup = playersSetup(103, true, std::vector<Controller>(2), 10);
    setup.options = o;
    setup.options.systemCount = 10;
    setup.options.simultaneous = true;
    auto created = createGame(rulesFixtureRules(), setup);
    REQUIRE(created.has_value());
    REQUIRE(created->options.modOptions.size() == 2);
    CHECK(created->options.modOptions[0].value == 40);
    CHECK(created->options.modOptions[1].value == 0);
    auto loaded = deserializeState(serializeState(*created));
    REQUIRE(loaded.has_value());
    CHECK(loaded->options.modOptions == created->options.modOptions);
    // A computer player sees them in its view.
    const Value v = sdk::buildView(rulesFixtureRules(), *created, EmpireId{0u});
    CHECK(v.find("game")->find("options")->find("mod_options")->size() == 2);
}

TEST_CASE("sdk rules orders: a victory condition and check_victory end the game with a winner and a reason") {
    {
        InstalledRules installed;
        const Rules& r = rulesFixtureRules();
        GameSetup setup = playersSetup(107, true, std::vector<Controller>(3), 10);
        const auto choices = sdk::modOptions(std::span<const mods::Package>(&rulesFixtureMod(), 1));
        REQUIRE(sdk::setModOption(setup.options, choices, "test.rules-fixture:beacons", 1).empty());
        auto created = createGame(r, setup);
        REQUIRE(created.has_value());
        GameState s = std::move(*created);
        play(r, s, 2);
        CHECK_FALSE(s.gameOver);
        REQUIRE(sdktest::modDataIn(s.modData).isMap());
        Value data = modDataIn(s.modData);
        data.set("beacons", Value(int64_t{2}));
        for (ModData& d : s.modData)
            if (d.mod == kRulesMod) d.value = *script::toJson(data);
        processTurn(r, s, {});
        CHECK(s.gameOver);
        CHECK(s.winner == EmpireId{2u});
        CHECK(s.endReason == "The beacons were lit.");
        const std::vector<LogEntry>& log = s.empire(EmpireId{0u}).log;
        CHECK(std::any_of(log.begin(), log.end(), [](const LogEntry& l) { return l.title == "The game is over"; }));
    }
    // The switch off: the same data ends nothing.
    {
        InstalledRules installed;
        const Rules& r = rulesFixtureRules();
        GameState s = rulesGame(107, true, 3, 10);
        play(r, s, 2);
        Value data = modDataIn(s.modData);
        data.set("beacons", Value(int64_t{2}));
        for (ModData& d : s.modData)
            if (d.mod == kRulesMod) d.value = *script::toJson(data);
        processTurn(r, s, {});
        CHECK_FALSE(s.gameOver);
    }
    // check_victory's fx.victory, in a turn-based game.
    test::ModDir dir("victory", "test.victory");
    dir.file("scripts/victory.py", R"PY(
from opense4 import rules


@rules.on("check_victory")
def two(game, fx):
    if game.turn == 2:
        fx.victory(game.empire(1), "Turn two came.")


@rules.on("turn_start")
def early(game, fx):
    try:
        fx.victory(None, "too soon")
    except RuntimeError:
        game.mod_data["refused"] = True
)PY");
    const mods::Package p = dir.open();
    sdk::PlayerSetup ps;
    ps.mods.push_back(p);
    sdk::installPlayers(ps);
    const Rules r(rulesRuleset({&p}));
    auto created = createGame(r, playersSetup(109, false, std::vector<Controller>(3), 10));
    REQUIRE(created.has_value());
    GameState s = std::move(*created);
    play(r, s, 5);
    CHECK(s.gameOver);
    CHECK(s.turn == 3);
    CHECK(s.winner == EmpireId{1u});
    CHECK(s.endReason == "Turn two came.");
    CHECK(modDataIn(s.modData, "test.victory").find("refused") != nullptr);
    sdk::uninstallPlayers();
}

TEST_CASE("sdk rules orders: a scenario sets up the game and its objectives end it") {
    const std::vector<std::string> names = sdk::scenarioNames(rulesFixtureMod());
    CHECK(names == std::vector<std::string>{"outpost"});
    auto scenario = sdk::loadScenario(rulesFixtureMod(), "outpost");
    REQUIRE_MESSAGE(scenario.has_value(), (scenario ? std::string{} : scenario.error().front()));
    CHECK(scenario->title == "Outpost");
    CHECK(scenario->setup.empires.size() == 2);
    CHECK(scenario->setup.options.simultaneous);
    REQUIRE(scenario->objectives.size() == 1);
    CHECK(scenario->objectives[0].victory);
    CHECK(learn::describe(scenario->objectives[0].when) == "colonies = 2");
    InstalledRules installed;
    const Rules& r = rulesFixtureRules();
    auto started = sdk::startScenario(r, rulesFixtureMod(), "outpost");
    REQUIRE_MESSAGE(started.has_value(), (started ? std::string{} : started.error()));
    GameState s = std::move(*started);
    CHECK(s.scenario.mod == kRulesMod);
    CHECK(s.scenario.name == "outpost");
    REQUIRE_FALSE(s.options.modOptions.empty());
    CHECK(s.options.modOptions[0].value == 40);   // the scenario's option
    play(r, s, 80);
    REQUIRE(s.gameOver);
    REQUIRE(s.winner.valid());
    CHECK(s.endReason == "Found a second colony");
    CHECK(std::find(s.scenario.met.begin(), s.scenario.met.end(), std::format("second_colony@{}", s.winner.value)) != s.scenario.met.end());
    CHECK(intIn(s.modData, "rewarded") == s.winner.value);
    // Its mistakes name the file and line.
    auto bad = sdk::parseScenario("title = \"X\"\n[setup]\nsystems = 5\n[[setup.empire]]\nname = \"A\"\n[[objective]]\nname = \"o\"\nwhen = { "
                                  "colonise = 2 }\n",
                                  "scenarios/bad.toml", "test.x", "bad");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().front().find("scenarios/bad.toml:8") != std::string::npos);
}

TEST_CASE("sdk rules orders: ability values for rules scripts and computer players") {
    const Rules& r = rulesFixtureRules();
    GameState s = test::newEngineGame(113, 2, 10);
    const DesignId d = test::addTestDesign(s, r, EmpireId{0u}, "Plated", "Test Frigate", {"Test Armor Plate", "Test Armor Plate"});
    const Vehicle& v = test::addTestVehicle(s, r, d, locationOf(s.galaxy, ownColony(s, EmpireId{0u})->planet));
    auto value = sdk::abilityValue(r, s, "vehicle", v.id.value, "Test Field Battery");
    REQUIRE(value.has_value());
    CHECK(value->value_or(-1) == 10);   // summed over its two plates
    CHECK(sdk::abilityValue(r, s, "design", d.value, "Armor").value().has_value());
    CHECK_FALSE(sdk::abilityValue(r, s, "colony", ownColony(s, EmpireId{0u})->planet.value, "Test Field Battery").value().has_value());
    CHECK_FALSE(sdk::abilityValue(r, s, "vehicle", 999999, "Armor").has_value());
    CHECK_FALSE(sdk::abilityValue(r, s, "vehicle", v.id.value, "No Such Thing").has_value());
    // The rules view lists the declared ability with how it combines.
    const Value rv = sdk::buildRulesView(r);
    bool listed = false;
    for (const Value& a : rv.find("abilities")->asList()) listed = listed || a.find("name")->asString() == "Test Field Battery";
    CHECK(listed);
}

TEST_CASE("sdk rules orders: data generators make patches at load, in the script runtime") {
    test::GameFolder g("rules_generators");
    test::ModDir m("gen_rules", "test.gen-rules");
    m.file("data/beam_line.py", R"PY(
def generate():
    adds = []
    for level in range(2, 5):
        adds.append({"name": "Needle Beam %d" % level, "copy_from": "Needle Beam", "set": {"Tonnage Space Taken": 10 * level}})
    return {"components": {"add": adds}}
)PY");
    InstalledRules installed;
    const mods::LoadedDataSet d = mods::loadDataSet(g.root, g.data(), test::modSet({m.open()}));
    REQUIRE(d.ruleset);
    CHECK_MESSAGE(d.diagnostics.errors.empty(), test::allErrors(d.diagnostics));
    const ruleset::Component* top = d.ruleset->findComponent("Needle Beam 4");
    REQUIRE(top != nullptr);
    CHECK(top->tonnage == 40);
    // The same mod loads to the same data every time.
    const mods::LoadedDataSet again = mods::loadDataSet(g.root, g.data(), test::modSet({m.open()}));
    REQUIRE(again.ruleset);
    CHECK(again.ruleset->components.size() == d.ruleset->components.size());
    // A generator's mistakes name its file and line.
    test::ModDir broken("gen_broken", "test.gen-broken");
    broken.file("data/broken.py", "def generate():\n    x = 1\n    return {'components': {'add': [1 / 0]}}\n");
    const mods::LoadedDataSet b = mods::loadDataSet(g.root, g.data(), test::modSet({broken.open()}));
    CHECK(test::hasError(b.diagnostics, "data/broken.py: generate() failed: Exception: ZeroDivisionError"));
    CHECK(test::hasError(b.diagnostics, "line 3"));
    test::ModDir nameless("gen_nameless", "test.gen-nameless");
    nameless.file("data/no-name.py", "def generate():\n    return {}\n");
    CHECK(test::hasError(mods::loadDataSet(g.root, g.data(), test::modSet({nameless.open()})).diagnostics, "must be a Python name"));
    test::ModDir floats("gen_floats", "test.gen-floats");
    floats.file("data/floats.py", "def generate():\n    return {'components': {'change': [{'name': 'Needle Beam', 'set': {'Tonnage Space Taken': 1.5}}]}}\n");
    CHECK(test::hasError(mods::loadDataSet(g.root, g.data(), test::modSet({floats.open()})).diagnostics, "float"));
}

TEST_CASE("sdk rules orders: the setup model and the server's setup files set the mods' options") {
    // The fixture data set with the rules fixture as its mod.
    test::GameFolder folder("rules_options");
    mods::LoadedDataSet data = mods::loadDataSet(folder.root, folder.data(), test::modSet({rulesFixtureMod()}));
    REQUIRE_MESSAGE(data.ruleset.has_value(), test::allErrors(data.diagnostics));
    const Rules r(*data.ruleset);
    namespace setupm = opense4::client::classic::setup;
    setupm::NewGameSettings settings = setupm::defaultSettings(r, 5);
    std::vector<setupm::ModOptionRow> rows = setupm::modOptionRows(r, settings);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].key == "test.rules-fixture:bounty");
    CHECK(rows[0].label == "Bounty per order (minerals)");
    CHECK(rows[0].value == 25);
    CHECK(rows[1].isSwitch);
    CHECK_FALSE(setupm::setModOption(r, settings, "test.rules-fixture:bounty", 60).has_value());
    CHECK(setupm::setModOption(r, settings, "test.rules-fixture:bounty", -1).has_value());
    CHECK(setupm::modOptionRows(r, settings)[0].value == 60);
    // The settings' options go into the game's setup as they are.
    CHECK(settings.options.modOptions.size() == 1);
    // The server's setup files: [options.mod."<mod id>"].
    auto file = server::parseSetup(R"(
[options]
rules_hook_budget = 1000000
rules_turn_budget = 50000000
mod_data_limit = 4096

[options.mod."test.rules-fixture"]
bounty = 90
beacons = true

[[empire]]
kind = "computer"
)",
                                   "game.toml", r);
    REQUIRE_MESSAGE(file.has_value(), (file ? std::string{} : file.error()));
    CHECK(file->options.rulesHookBudget == 1'000'000);
    CHECK(file->options.rulesTurnBudget == 50'000'000);
    CHECK(file->options.modDataLimit == 4096);
    REQUIRE(file->options.modOptions.size() == 2);
    const std::vector<sdk::ModOptionChoice> choices = sdk::modOptions(r);
    CHECK(sdk::modOptionValue(file->options, choices[0]) == 90);
    CHECK(sdk::modOptionValue(file->options, choices[1]) == 1);
    auto bad = server::parseSetup("[options.mod.\"test.rules-fixture\"]\nbounty = 5000\nnope = 1\n[[empire]]\nkind = \"computer\"\n", "bad.toml", r);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().find("bad.toml:2: the option test.rules-fixture:bounty takes 0 to 1000") != std::string::npos);
    CHECK(bad.error().find("bad.toml:3: no mod of the game declares the option test.rules-fixture:nope") != std::string::npos);
}

TEST_CASE("sdk rules orders: opense4-sdk check matches a mod's scripts with its declarations") {
    CHECK(sdk::checkModRules(rulesFixtureMod()).errors.empty());
    test::ModDir m("rules_check", "test.rules-check", "1.0.0", R"(
[[rules.orders]]
name = "forgotten"

[[rules.events]]
name = "quake"
chance = 5
)");
    m.file("scripts/check_me.py", R"PY(
from opense4 import rules


@rules.event("quake")
def quake(game, event, fx):
    pass


@rules.order("stray")
def stray(game, order, fx):
    pass
)PY");
    m.file("scripts/bad-name.py", "x = 1\n");
    m.file("scenarios/broken.toml", "title = 3\n");
    const sdk::PlayerCheck c = sdk::checkModRules(m.open());
    auto any = [&](const std::vector<std::string>& lines, std::string_view part) {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
    };
    CHECK(any(c.errors, "the order 'forgotten' has no effect"));
    CHECK(any(c.errors, "register a order 'stray' that mod.toml does not declare"));
    CHECK_FALSE(any(c.errors, "quake"));
    CHECK(any(c.warnings, "scripts/bad-name.py: not a Python module name"));
    CHECK(any(c.errors, "scenarios/broken.toml"));
    // Scripts that do not import: the error says where.
    test::ModDir broken("rules_check_broken", "test.rules-broken");
    broken.file("scripts/oops.py", "from opense4 import rules\n\n\n@rules.on('no_such_hook')\ndef f(game, fx):\n    pass\n");
    const sdk::PlayerCheck b = sdk::checkModRules(broken.open());
    CHECK(any(b.errors, "no hook 'no_such_hook'"));
    CHECK(any(b.errors, "oops.py"));
}
