// The SDK's Python package (python/opense4, docs/sdk/python-api.md).
//
// Its tests (tests/sdk/python/test_sdk_*.py) run twice: in the game's runtime, and
// under CPython 3.10 or newer when python3 is installed (skipped otherwise). Both get
// the same fixtures, made here from a game of the engine fixture: views built by
// sdk::buildView, the rules view, the engine's routes, every command, order and
// type with its defaults, and the docs' schema. What the tests build comes back and
// is decoded by the engine's codec; what both runtimes publish must be the same.
//
// The other cases play computer players through opense4._engine.dispatch with the
// engine's services as native functions, as the game does, and measure what
// wrapping a large view costs.

#include "command_samples.hpp"
#include "engine_fixture.hpp"
#include "mod_fixture.hpp"
#include "script_test_util.hpp"
#include "sdk/sdk_test_util.hpp"
#include "temp_dir.hpp"

#include "script/json.hpp"
#include "sdk/codec.hpp"
#include "sdk/names.hpp"
#include "sdk/queries.hpp"
#include "sdk/rules_view.hpp"
#include "sdk/value_io.hpp"
#include "sdk/view.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

using namespace opense4;
using namespace opense4::game;
using opense4::script::Value;
using opense4::script::ValueList;
using opense4::script::ValueMap;
using namespace opense4::sdktest;
namespace fs = std::filesystem;

namespace {

Value map(std::initializer_list<std::pair<std::string, Value>> entries) { return Value(ValueMap(entries)); }

fs::path sourceDir() { return fs::path(OPENSE4_DOCS_DIR).parent_path(); }
fs::path pythonTestDir() { return fs::path(OPENSE4_SDK_TEST_DIR) / "python"; }

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), p.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

template <size_t... I>
std::vector<Command> defaultCommands(std::index_sequence<I...>) {
    return {Command{std::in_place_index<I>}...};
}

// Every command, order, tactical order and command type with the engine's defaults.
Value codecDefaults() {
    using sdk::detail::enc;
    ValueMap commands;
    for (const Command& c : defaultCommands(std::make_index_sequence<sdk::kCommandKinds>{}))
        commands.emplace_back(std::string(sdk::commandKindName(c)), sdk::encodeCommand(c));
    ValueMap orders;
    for (size_t k = 0; k < static_cast<size_t>(OrderKind::Count); ++k) {
        Order o;
        o.kind = static_cast<OrderKind>(k);
        orders.emplace_back(std::string(sdk::enumName(o.kind)), sdk::encodeOrder(o));
    }
    ValueMap tactical;
    using K = combat::TacticalOrder::Kind;
    for (size_t k = 0; k <= static_cast<size_t>(K::ResolveCombat); ++k) {
        combat::TacticalOrder o;
        o.kind = static_cast<K>(k);
        tactical.emplace_back(std::string(sdk::enumName(o.kind)), sdk::encodeTacticalOrder(o));
    }
    ValueMap types;
    types.emplace_back("location", enc(Location{}));
    types.emplace_back("sector", enc(Sector{}));
    types.emplace_back("resources", enc(Resources{}));
    types.emplace_back("queue_target", enc(cmd::QueueTarget{}));
    types.emplace_back("queue_item", enc(QueueItem{}));
    types.emplace_back("design", enc(Design{}));
    types.emplace_back("design_entry", enc(DesignEntry{}));
    types.emplace_back("research_project", enc(ResearchProject{}));
    types.emplace_back("intel_project_order", enc(IntelProjectOrder{}));
    types.emplace_back("message", enc(DiplomaticMessage{}));
    types.emplace_back("package_item", enc(PackageItem{}));
    types.emplace_back("waypoint", enc(Waypoint{}));
    types.emplace_back("strategy", enc(ruleset::CombatStrategy{}));
    types.emplace_back("strategy_setting", enc(std::pair<std::string, std::string>{}));
    types.emplace_back("population_group", enc(PopulationGroup{}));
    types.emplace_back("unit_stack", enc(UnitStack{}));
    types.emplace_back("interface_options", enc(InterfaceOptions{}));
    types.emplace_back("square", enc(combat::Square{}));
    ValueList kinds;
    for (std::string_view k : sdk::commandKindNames()) kinds.emplace_back(k);
    return map({{"kinds", Value(std::move(kinds))},
                {"commands", Value(std::move(commands))},
                {"orders", Value(std::move(orders))},
                {"tactical", Value(std::move(tactical))},
                {"types", Value(std::move(types))},
                {"samples", sdk::encodeCommands(test::everyCommandSample(test::engineRules()).commands)}});
}

// The docs' schema: {type: {enum, fields, values}}.
Value schemaValue() {
    ValueMap out;
    for (const auto& [name, s] : docsSchema().sections) {
        ValueList fields, values;
        for (const auto& row : s.rows) fields.emplace_back(row.field);
        for (const auto& v : s.values) values.emplace_back(v);
        out.emplace_back(name, map({{"file", Value(s.file)}, {"enum", Value(s.isEnum)}, {"fields", Value(std::move(fields))},
                                    {"values", Value(std::move(values))}}));
    }
    return Value(std::move(out));
}

// The engine's routes, centre to centre: between the systems empire 0 has explored
// over what it knows, or (`whole`) between every two systems over everything.
Value enginePaths(const Rules& r, const SdkGame& g, bool whole) {
    const EmpireId me{0u};
    const sdk::Queries q(r, g.state, me, {.whole = whole});
    ValueList out;
    for (const StarSystem& a : g.state.galaxy.systems) {
        if (!whole && !g.state.empire(me).hasExplored(a.id)) continue;
        for (const StarSystem& b : g.state.galaxy.systems) {
            if (!whole && !g.state.empire(me).hasExplored(b.id)) continue;
            const auto res = q.call("path", map({{"origin", Value(a.id.value)}, {"destination", Value(b.id.value)}}));
            REQUIRE(res.has_value());
            out.push_back(map({{"origin", Value(a.id.value)},
                               {"destination", Value(b.id.value)},
                               {"found", at(*res, "found")},
                               {"jumps", at(*res, "jumps")},
                               {"length", at(*res, "length")}}));
        }
    }
    return Value(std::move(out));
}

const SdkGame& fixtureGame() {
    static const SdkGame g = sdkGame();
    return g;
}

// The same game, where empire 0 has explored every system and knows every warp link:
// its own routes then cross the galaxy, with its options (a system to avoid, a tagged
// minefield) in the way.
const SdkGame& explorerGame() {
    static const SdkGame g = [] {
        SdkGame x = sdkGame();
        Knowledge& k = x.state.empire(EmpireId{0u}).knowledge;
        k.explored.assign(x.state.galaxy.systems.size(), 1);
        k.knownWarpLink.assign(x.state.galaxy.objects.size(), 1);
        return x;
    }();
    return g;
}

const Value& pythonFixtures() {
    static const Value v = [] {
        const Rules& r = test::engineRules();
        const SdkGame& g = fixtureGame();
        const SdkGame& explorer = explorerGame();
        return map({{"view", sdk::buildView(r, g.state, EmpireId{0u})},
                    {"explorer_view", sdk::buildView(r, explorer.state, EmpireId{0u})},
                    {"explorer_paths", enginePaths(r, explorer, false)},
                    {"whole_view", sdk::buildView(r, g.state, EmpireId{0u}, {.whole = true})},
                    {"view_1", sdk::buildView(r, g.state, EmpireId{1u})},
                    {"rules", sdk::buildRulesView(r)},
                    {"paths", enginePaths(r, g, false)},
                    {"whole_paths", enginePaths(r, g, true)},
                    {"codec", codecDefaults()},
                    {"schema", schemaValue()},
                    {"facts", map({{"empire", Value(0)},
                                   {"seen_foreign", Value(g.seenForeign.value)},
                                   {"hidden_foreign", Value(g.hiddenForeign.value)},
                                   {"unexplored", Value(g.unexplored.value)}})}});
    }();
    return v;
}

// The test modules the runtime gets (conftest.py is pytest's; the library's tests run elsewhere).
std::vector<fs::path> pythonTestFiles() {
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(pythonTestDir())) {
        const std::string name = e.path().filename().string();
        if (e.path().extension() != ".py" || name == "conftest.py" || name == "test_library.py" || name == "determinism.py") continue;
        files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

script::Limits roomyLimits() {
    script::Limits limits;
    limits.cStackBytes = size_t{1} << 20;   // sanitizer builds use more stack per call
    limits.heapBytes = size_t{96} << 20;
    limits.budget = 4'000'000'000;
    return limits;
}

// An interpreter with the opense4 package from the source tree (python/opense4), as the
// engine adds it for computer players.
std::unique_ptr<script::Interpreter> interpreterWithPackage() {
    auto interp = script::test::makeInterpreter(roomyLimits());
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(sourceDir() / "python" / "opense4"))
        if (e.path().extension() == ".py") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    REQUIRE(files.size() >= 10);
    for (const fs::path& f : files) {
        auto added = interp->addFile("opense4/" + f.filename().string(), slurp(f));
        REQUIRE_MESSAGE(added.has_value(), added.error().describe());
    }
    return interp;
}

std::unique_ptr<script::Interpreter> interpreterWithTests() {
    auto interp = interpreterWithPackage();
    for (const fs::path& f : pythonTestFiles()) {
        auto added = interp->addFile(f.filename().string(), slurp(f));
        REQUIRE_MESSAGE(added.has_value(), added.error().describe());
    }
    return interp;
}

std::vector<std::string> strings(const Value& list) {
    std::vector<std::string> out;
    for (const Value& v : list.asList()) out.push_back(v.asString());
    return out;
}

// The results of one run of the tests: every test passed, and what they built decodes.
void checkResults(const Value& results, std::string_view runtime) {
    INFO(runtime);
    for (const std::string& f : strings(at(results, "failed"))) FAIL_CHECK(f);
    const auto passed = strings(at(results, "passed"));
    const auto skipped = strings(at(results, "skipped"));
    MESSAGE(runtime << ": " << passed.size() << " Python tests passed, " << skipped.size() << " skipped");
    for (const std::string& s : skipped) MESSAGE("skipped: " << s);
    CHECK(passed.size() >= 33);
    CHECK(skipped.empty());

    const Value& outputs = at(results, "outputs");
    // Every command the constructors built from the engine's samples decodes back to itself.
    const Value& built = at(outputs, "commands_built");
    REQUIRE(built.size() >= sdk::kCommandKinds);
    for (const Value& c : built.asList()) {
        const auto decoded = sdk::decodeCommand(c);
        REQUIRE_MESSAGE(decoded.has_value(), script::describe(c) << ": " << (decoded ? std::string() : decoded.error().text()));
        CHECK_MESSAGE(sdk::encodeCommand(*decoded) == c, script::describe(c));
    }
    // So does every command the hand-written helpers and the wrappers built.
    for (const Value& c : at(outputs, "commands_from_view").asList()) {
        const auto decoded = sdk::decodeCommand(c);
        CHECK_MESSAGE(decoded.has_value(), script::describe(c) << ": " << (decoded ? std::string() : decoded.error().text()));
    }
    for (const Value& o : at(outputs, "orders_built").asList()) {
        const auto decoded = sdk::decodeOrder(o);
        REQUIRE_MESSAGE(decoded.has_value(), script::describe(o) << ": " << (decoded ? std::string() : decoded.error().text()));
        CHECK(sdk::encodeOrder(*decoded) == o);
    }
    for (const Value& o : at(outputs, "tactical_built").asList()) {
        const auto decoded = sdk::decodeTacticalOrder(o);
        REQUIRE_MESSAGE(decoded.has_value(), script::describe(o) << ": " << (decoded ? std::string() : decoded.error().text()));
        CHECK(sdk::encodeTacticalOrder(*decoded) == o);
    }
}

Value runEmbedded() {
    auto interp = interpreterWithTests();
    const auto t0 = std::chrono::steady_clock::now();
    auto r = interp->call("run_sdk_tests", "run", std::vector<Value>{pythonFixtures()});
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    REQUIRE_MESSAGE(r.has_value(), script::test::explain(r.error()) << "\n" << interp->output());
    MESSAGE(std::format("the Python tests in the game's runtime: {:.0f} ms, {} bytecodes", ms, interp->lastCallBudget()));
    return *r;
}

struct Shell {
    int code = -1;
    std::string out;
};

// Runs a command line; its output (and errors) come back as text.
Shell shell(const std::string& commandLine) {
    static test::TempDir outputs("sdk_python_shell");
    static int n = 0;
    const fs::path file = outputs.path() / std::format("run{}.txt", ++n);
    std::string command = std::format("{} > \"{}\" 2>&1", commandLine, file.string());
#if defined(_WIN32)
    command = "\"" + command + "\"";
#endif
    int code = std::system(command.c_str());
#if !defined(_WIN32)
    if (WIFEXITED(code)) code = WEXITSTATUS(code);
#endif
    std::error_code ec;
    return Shell{code, fs::exists(file, ec) ? slurp(file) : std::string()};
}

// python3, when it is installed and 3.10 or newer.
std::optional<std::string> cpython() {
    for (const char* exe : {"python3", "python"}) {
        const Shell s = shell(std::format("{} -c \"import sys; print('ok' if sys.version_info >= (3, 10) else 'old')\"", exe));
        if (s.code == 0 && s.out.starts_with("ok")) return std::string(exe);
    }
    return std::nullopt;
}

std::string quoted(const fs::path& p) { return "\"" + p.string() + "\""; }

// The engine's services as the native module _opense4, the way the game gives them:
// queries and the rules view for real, the classic AI's commands and answers prepared.
struct NativeServices {
    std::shared_ptr<sdk::Queries> queries;
    Value builtinCommands;            // what `builtin` answers
    std::vector<Value> builtinAsked;  // its arguments, each time
    std::vector<Value> applied;
    int rulesAsked = 0;

    void install(script::Interpreter& interp) {
        auto ok = [](auto r) { REQUIRE_MESSAGE(r.has_value(), r.error().describe()); };
        ok(interp.addNativeFunction("_opense4", "query", [this](std::span<const Value> a) -> Value {
            const auto r = queries->call(at(a[0], "name").asString(), at(a[0], "args"));
            if (!r) throw script::NativeError("ValueError", r.error().text());
            return *r;
        }));
        ok(interp.addNativeFunction("_opense4", "rules", [this](std::span<const Value>) -> Value {
            ++rulesAsked;
            return sdk::buildRulesView(test::engineRules());
        }));
        ok(interp.addNativeFunction("_opense4", "builtin", [this](std::span<const Value> a) -> Value {
            builtinAsked.push_back(a[0]);
            return builtinCommands;
        }));
        ok(interp.addNativeFunction("_opense4", "builtin_answer", [](std::span<const Value> a) -> Value {
            return at(a[0], "call").asString() == "colony_type" ? Value("Mining") : Value();
        }));
        ok(interp.addNativeFunction("_opense4", "apply", [this](std::span<const Value> a) -> Value {
            applied.push_back(at(a[0], "command"));
            const auto d = sdk::decodeCommand(at(a[0], "command"));
            return map({{"ok", Value(d.has_value())}, {"reason", Value(d ? std::string() : d.error().text())}});
        }));
    }
};

Value request(std::string_view call, int empire, Value view, Value args = Value::emptyMap()) {
    return map({{"api", Value(1)}, {"call", Value(call)}, {"empire", Value(empire)}, {"turn", Value(12)}, {"seed", Value(int64_t{987654321})},
                {"view", std::move(view)}, {"args", std::move(args)}});
}

Value firstRequest(std::string_view call, int empire, Value view, Value player, Value memory) {
    Value r = request(call, empire, std::move(view));
    r.set("player", std::move(player));
    r.set("memory", std::move(memory));
    return r;
}

Value dispatch(script::Interpreter& interp, const Value& req) {
    auto r = interp.call("opense4._engine", "dispatch", std::vector<Value>{req});
    REQUIRE_MESSAGE(r.has_value(), script::test::explain(r.error()) << "\n" << interp.output());
    return *r;
}

void checkNoError(const Value& response) {
    const Value* e = response.find("error");
    CHECK_MESSAGE(e == nullptr, (e ? script::describe(*e, 2000) : std::string()));
}

constexpr std::string_view kAdmiral = R"(
from opense4 import ai, cmd, order


class Admiral(ai.Player):
    """Keeps the classic economy, explores with idle ships, remembers its sessions."""

    def politics(self, view, orders):
        self.memory["sessions"] = self.memory.get("sessions", 0) + 1
        self.memory["calls"] = []
        self.memory["seed_draw"] = self.random.randint(1, 1000000)
        self.log("politics, turn " + str(self.turn))

    def orders(self, view, orders):
        self.memory["calls"].append("orders")
        ships = view.my.ships
        home = view.my.home_system
        route = self.query("path", vehicle=ships[0], destination=home)
        self.note(ships[0], "home in " + str(route.length))
        self.memory["route"] = [route.found, route.length, route.steps[-1].system_id if route.steps else None]
        for v in [ships[0]] + view.my.idle_vehicles[:2]:
            orders.add(cmd.give(v, [order.explore()]))
        orders.extend(ai.builtin.orders(view, skip=["attack", "exploration"]))
        bad = self.apply({"kind": "rename", "vehicle": ships[0].id, "nmae": "Typo"})
        self.memory["applied"] = [bad.ok, bool(bad.reason)]
        self.memory["components"] = len(self.rules.components)

    def colony_type(self, view, question):
        self.memory["calls"].append("colony_type")
        return ai.builtin.answer()

    def decloak(self, view, question):
        return self.cloak_plan(question)

    def cloak_plan(self, question):
        raise ValueError("no cloak plan for " + str(question.object_id))

    def end_session(self):
        self.memory["ended"] = self.memory.get("ended", 0) + 1
)";

} // namespace

TEST_CASE("sdk python: the package's tests pass in the game's runtime") {
    // OPENSE4_SDK_FIXTURES_OUT=file keeps the fixtures, for running the tests by hand
    // (run_sdk_tests.py --fixtures file, or pytest with OPENSE4_SDK_FIXTURES=file).
    if (const char* keep = std::getenv("OPENSE4_SDK_FIXTURES_OUT"); keep && *keep) test::writeText(keep, *script::toJson(pythonFixtures()));
    const Value results = runEmbedded();
    checkResults(results, "the game's runtime");
}

TEST_CASE("sdk python: the package's tests pass under CPython, with the same results") {
    const auto python = cpython();
    if (!python) {
        MESSAGE("python3 (3.10 or newer) is not installed: the CPython run is skipped");
        return;
    }
    test::TempDir dir("sdk_python_cpython");
    const fs::path fixtures = dir.path() / "fixtures.json";
    const fs::path out = dir.path() / "results.json";
    const auto json = script::toJson(pythonFixtures());
    REQUIRE(json.has_value());
    test::writeText(fixtures, *json);
    const Shell run = shell(std::format("{} -B {} --fixtures {} --out {}", *python, quoted(pythonTestDir() / "run_sdk_tests.py"),
                                        quoted(fixtures), quoted(out)));
    CHECK_MESSAGE(run.code == 0, run.out);
    const auto parsed = script::parseJson(slurp(out));
    REQUIRE_MESSAGE(parsed.has_value(), parsed.error().describe());
    checkResults(*parsed, "CPython");

    // The same numbers and the same commands on both runtimes.
    const Value embedded = runEmbedded();
    CHECK(at(at(*parsed, "outputs"), "random") == at(at(embedded, "outputs"), "random"));
    CHECK(at(at(*parsed, "outputs"), "commands_built") == at(at(embedded, "outputs"), "commands_built"));
    CHECK(at(at(*parsed, "outputs"), "commands_from_view") == at(at(embedded, "outputs"), "commands_from_view"));
    CHECK(at(*parsed, "passed") == at(embedded, "passed"));

    // The generated modules are those the docs give.
    const Shell gen = shell(std::format("{} -B {} --check", *python, quoted(sourceDir() / "tools" / "gen_sdk_python.py")));
    CHECK_MESSAGE(gen.code == 0, gen.out);
}

TEST_CASE("sdk python: computer players in the game, with the engine's services") {
    const Rules& r = test::engineRules();
    const SdkGame& g = fixtureGame();
    const EmpireId me{0u};
    const Value view = sdk::buildView(r, g.state, me);
    auto interp = interpreterWithPackage();
    NativeServices natives;
    natives.queries = std::make_shared<sdk::Queries>(r, g.state, me);
    natives.builtinCommands = sdk::encodeCommands(test::busyOrders(r, g.state, me, 1).commands);
    natives.install(*interp);
    // A mod's ai/ files go in at the root: ai/admiral.py is the module admiral (docs/sdk/ai-protocol.md, section 1).
    REQUIRE(interp->addFile("admiral.py", std::string(kAdmiral)).has_value());
    const Value player = map({{"mod", Value("test.admiral")}, {"name", Value("Admiral")}, {"module", Value("admiral")}, {"class", Value("Admiral")}});

    // A session: politics (memory null the first time), orders, a colony, an error, the end.
    Value res = dispatch(*interp, firstRequest("politics", 0, view, player, Value()));
    checkNoError(res);
    CHECK(at(res, "commands").size() == 0);
    CHECK(at(at(res, "memory"), "sessions") == Value(1));
    CHECK(at(res, "log") == Value(ValueList{Value("politics, turn 12")}));
    const Value draw = at(at(res, "memory"), "seed_draw");

    res = dispatch(*interp, request("orders", 0, view));
    checkNoError(res);
    const auto commands = sdk::decodeCommands(at(res, "commands"));
    REQUIRE_MESSAGE(commands.has_value(), commands.error().text());
    // Its own explorers first, then the classic ministers' commands it asked for.
    const size_t builtinCount = natives.builtinCommands.size();
    REQUIRE(commands->size() >= builtinCount + 1);
    CHECK(std::holds_alternative<cmd::SetOrders>(commands->front()));
    for (size_t i = 0; i < builtinCount; ++i)
        CHECK(at(res, "commands").asList()[commands->size() - builtinCount + i] == natives.builtinCommands.asList()[i]);
    REQUIRE(natives.builtinAsked.size() == 1);
    CHECK(at(natives.builtinAsked[0], "call") == Value("orders"));
    CHECK(at(natives.builtinAsked[0], "ministers").isNull());
    CHECK(at(natives.builtinAsked[0], "skip") == Value(ValueList{Value("attack"), Value("exploration")}));
    // The query answered with the engine's route; the refused apply came back refused.
    const Value& memory = at(res, "memory");
    CHECK(at(memory, "route").asList()[0] == Value(true));
    CHECK(at(memory, "applied") == Value(ValueList{Value(false), Value(true)}));
    REQUIRE(natives.applied.size() == 1);
    CHECK(intAt(memory, "components") == static_cast<int64_t>(at(sdk::buildRulesView(r), "components").size()));
    CHECK(natives.rulesAsked == 1);
    // A note on the ship it routed.
    REQUIRE(at(res, "notes").size() == 1);
    const Value& note = at(res, "notes").asList()[0];
    CHECK(at(note, "kind") == Value("vehicle"));
    CHECK(at(note, "text").asString().starts_with("home in "));

    res = dispatch(*interp, request("colony_type", 0, Value(), map({{"colony", Value(7)}, {"planet", Value(7)}, {"vehicle", Value()},
                                                                    {"choices", Value(ValueList{Value("Mining"), Value("Research")})}})));
    checkNoError(res);
    CHECK(at(res, "answer") == Value("Mining"));
    CHECK(at(res, "commands").size() == 0);

    // An exception: the error with its type, message and traceback; the session goes on.
    res = dispatch(*interp, request("decloak", 0, Value(), map({{"object", Value(31)}, {"reason", Value("attack")}})));
    REQUIRE(res.find("error") != nullptr);
    const Value& error = at(res, "error");
    CHECK(at(error, "type") == Value("ValueError"));
    CHECK(at(error, "message") == Value("no cloak plan for 31"));
    const std::string tb = at(error, "traceback").asString();
    CHECK(tb.starts_with("Traceback (most recent call last):"));
    CHECK_MESSAGE(tb.find("admiral.py\", line") != std::string::npos, tb);
    CHECK_MESSAGE(tb.find("in cloak_plan") != std::string::npos, tb);
    CHECK(at(res, "answer").isNull());
    CHECK(at(at(res, "memory"), "sessions") == Value(1));

    res = dispatch(*interp, request("end_session", 0, Value()));
    checkNoError(res);
    const Value kept = at(res, "memory");
    CHECK(at(kept, "ended") == Value(1));
    CHECK(at(kept, "calls") == Value(ValueList{Value("orders"), Value("colony_type")}));
    // The session is over: a request without `player` is refused.
    res = dispatch(*interp, request("orders", 0, view));
    CHECK(at(at(res, "error"), "type") == Value("ProtocolError"));

    // The next session starts from the memory the last one left, with the same seed's numbers.
    res = dispatch(*interp, firstRequest("politics", 0, view, player, kept));
    checkNoError(res);
    CHECK(at(at(res, "memory"), "sessions") == Value(2));
    CHECK(at(at(res, "memory"), "ended") == Value(1));
    CHECK(at(at(res, "memory"), "seed_draw") == draw);

    // A player that cannot be made: every request of its session says why.
    const Value missing = map({{"mod", Value("test.admiral")}, {"name", Value("Ghost")}, {"module", Value("ghost")}, {"class", Value("Ghost")}});
    res = dispatch(*interp, firstRequest("politics", 1, view, missing, Value()));
    REQUIRE(res.find("error") != nullptr);
    const Value why = at(res, "error");
    res = dispatch(*interp, request("orders", 1, view));
    CHECK(at(res, "error") == why);
}

TEST_CASE("sdk python: the ai template of opense4-sdk new plays a turn") {
    test::TempDir dir("sdk_python_template");
    const fs::path mod = dir.path() / "prospector";
    // Cross-compiled tests run the tool through their own emulator (OPENSE4_TEST_RUNNER).
    const char* runner = std::getenv("OPENSE4_TEST_RUNNER");
    const Shell made = shell(std::format("{}{}\"{}\" new ai {} --id=test.prospector", runner ? runner : "", runner ? " " : "", OPENSE4_SDK_EXE,
                                         quoted(mod)));
    REQUIRE_MESSAGE(made.code == 0, made.out);
    const std::string manifest = slurp(mod / "mod.toml");
    CHECK(manifest.find("[[ai.players]]") != std::string::npos);
    CHECK(manifest.find("module = \"player\"") != std::string::npos);

    const Rules& r = test::engineRules();
    const SdkGame& g = fixtureGame();
    const EmpireId me{0u};
    const Value view = sdk::buildView(r, g.state, me);
    auto interp = interpreterWithPackage();
    NativeServices natives;
    natives.queries = std::make_shared<sdk::Queries>(r, g.state, me);
    natives.builtinCommands = sdk::encodeCommands(test::busyOrders(r, g.state, me, 1).commands);
    natives.install(*interp);
    REQUIRE(interp->addFile("player.py", slurp(mod / "ai" / "player.py")).has_value());   // the engine adds ai/ at the root
    const auto cls = manifest.find("class = \"");
    REQUIRE(cls != std::string::npos);
    const std::string className = manifest.substr(cls + 9, manifest.find('"', cls + 9) - cls - 9);
    const Value player = map({{"mod", Value("test.prospector")}, {"name", Value("Prospector")}, {"module", Value("player")},
                              {"class", Value(className)}});
    size_t commands = 0;
    for (std::string_view call : {"politics", "orders", "economy"}) {
        INFO(call);
        const Value res = dispatch(*interp, call == "politics" ? firstRequest(call, 0, view, player, Value()) : request(call, 0, view));
        checkNoError(res);
        const auto decoded = sdk::decodeCommands(at(res, "commands"));
        CHECK_MESSAGE(decoded.has_value(), (decoded ? std::string() : decoded.error().text()));
        commands += at(res, "commands").size();
    }
    CHECK(commands > natives.builtinCommands.size());   // the classic economy and orders, and its own
    const Value end = dispatch(*interp, request("end_session", 0, Value()));
    checkNoError(end);
    CHECK(at(end, "memory").isMap());
}

TEST_CASE("sdk python: wrapping a large view stays cheap") {
    auto interp = interpreterWithTests();
    const Value& view = at(pythonFixtures(), "whole_view");   // every system's contents
    auto setup = interp->call("sdk_bench", "setup", std::vector<Value>{view, Value(40)});
    REQUIRE_MESSAGE(setup.has_value(), script::test::explain(setup.error()));
    MESSAGE("a large view: " << script::describe(*setup, 400));
    auto phases = interp->call("sdk_bench", "phase_names");
    REQUIRE(phases.has_value());
    std::string report;
    for (const Value& name : phases->asList()) {
        const auto t0 = std::chrono::steady_clock::now();
        auto r = interp->call("sdk_bench", "phase", std::vector<Value>{name});
        const auto us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        REQUIRE_MESSAGE(r.has_value(), script::test::explain(r.error()));
        report += std::format("\n  {:<28} {:>10.0f} us {:>12} bytecodes", name.asString(), us, interp->lastCallBudget());
        if (name.asString() == "wrap") CHECK(interp->lastCallBudget() < 2000);   // wrapping alone reads nothing
    }
    MESSAGE("costs in the game's runtime (debug build):" << report);

    const auto python = cpython();
    if (!python) return;
    test::TempDir dir("sdk_python_bench");
    const fs::path fixtures = dir.path() / "view.json";
    test::writeText(fixtures, *script::toJson(view));
    const Shell bench = shell(std::format("{} -B {} --view {} --copies 40", *python, quoted(pythonTestDir() / "sdk_bench.py"), quoted(fixtures)));
    CHECK_MESSAGE(bench.code == 0, bench.out);
    MESSAGE("costs under CPython:\n" << bench.out);
}
