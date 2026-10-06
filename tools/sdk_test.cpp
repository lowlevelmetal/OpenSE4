// opense4-sdk test, run, bot and python (docs/sdk/bots-and-arena.md).

#include "sdk_tool.hpp"

#include "core/environment.hpp"
#include "game/players.hpp"
#include "game/setup.hpp"
#include "script/runtime.hpp"
#include "sdk/match.hpp"
#include "sdk/players.hpp"
#include "sdk/process.hpp"
#include "sdk/rules_view.hpp"
#include "sdk/view.hpp"
#include "sdk/worker.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>

namespace opense4::sdktool {

namespace fs = std::filesystem;
using script::Value;
using script::ValueList;
using script::ValueMap;

namespace {

// The runner of a mod's tests, in the game's Python: each test_ function of
// each tests/test_*.py module, one call (and one budget) each.
constexpr std::string_view kRunner = R"py("""opense4-sdk test: runs the test_ functions of a mod's tests/test_*.py modules."""

import sys


def _traceback(e):
    try:
        import traceback
        return "".join(traceback.format_exception(e))
    except Exception:
        return ""


def discover(modules):
    found = []
    errors = []
    for name in modules:
        try:
            __import__(name)
        except Exception as e:
            errors.append({"test": name, "message": type(e).__name__ + ": " + str(e), "traceback": _traceback(e)})
            continue
        module = sys.modules[name]
        for test in sorted(n for n in dir(module) if n.startswith("test_")):
            if callable(getattr(module, test)):
                found.append(name + "." + test)
    return {"tests": found, "errors": errors}


def run_one(name):
    module, _, test = name.rpartition(".")
    try:
        getattr(sys.modules[module], test)()
    except Exception as e:
        kind = type(e).__name__
        outcome = "skipped" if kind in ("Skip", "SkipTest") else "failed"
        return {"outcome": outcome, "message": kind + ": " + str(e), "traceback": _traceback(e)}
    return {"outcome": "passed", "message": "", "traceback": ""}
)py";

std::string readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const Value* field(const Value& v, std::string_view key) { return v.find(key); }
std::string text(const Value& v, std::string_view key) {
    const Value* x = v.find(key);
    return x && x->isString() ? x->asString() : std::string();
}

void printIndented(const std::string& block) {
    std::istringstream in(block);
    for (std::string line; std::getline(in, line);) std::printf("        %s\n", line.c_str());
}

struct Tally {
    int passed = 0, failed = 0, skipped = 0;
};

// A game of the mod's data set for opense4.testing.game_view(), made when first asked.
struct TestGame {
    const game::Rules* rules = nullptr;
    uint64_t seed = 1;
    std::optional<game::GameState> state;

    const game::GameState& get() {
        if (!state) {
            game::GameSetup setup;
            setup.seed = seed;
            setup.options.simultaneous = true;
            for (int i = 0; i < 2; ++i) {
                game::EmpireSetup e;
                e.kind = game::PlayerKind::Computer;
                setup.empires.push_back(std::move(e));
            }
            auto made = game::createGame(*rules, setup);
            if (!made) throw script::NativeError("RuntimeError", "the test game could not be made: " + made.error());
            state = std::move(*made);
        }
        return *state;
    }
};

// The mod's Python tests in the game's runtime.
void runPythonTests(const mods::Package& mod, const game::Rules* rules, uint64_t seed, int64_t budget, Tally& tally) {
    std::vector<std::pair<std::string, std::string>> files;
    std::vector<std::string> modules;
    for (const mods::PackageFile& f : mod.files) {
        if (!f.path.ends_with(".py")) continue;
        if (f.path.starts_with("ai/")) files.emplace_back(f.path.substr(3), readText(f.real));
        if (f.path.starts_with("tests/")) {
            const std::string rel = f.path.substr(6);
            files.emplace_back(rel, readText(f.real));
            if (rel.find('/') == std::string::npos && rel.starts_with("test_")) modules.push_back(rel.substr(0, rel.size() - 3));
        }
    }
    std::sort(modules.begin(), modules.end());
    std::printf("Python tests (tests/):\n");
    if (modules.empty()) {
        std::printf("  none: the mod has no tests/test_*.py\n");
        return;
    }
    TestGame game;
    game.rules = rules;
    game.seed = seed;
    sdk::Worker worker;   // the interpreter's C stack, whatever this thread has
    std::unique_ptr<script::Interpreter> interp;
    std::string problem;
    worker.run([&] {
        script::Limits limits;
        limits.heapBytes = size_t{128} << 20;
        limits.budget = int64_t{1} << 60;
        auto made = script::Interpreter::create(limits);
        if (!made) {
            problem = made.error().describe();
            return;
        }
        interp = std::move(*made);
        for (const script::LibraryFile& f : sdk::packageFiles()) (void)interp->addFile(f.path, std::string(f.text));
        for (const auto& [path, body] : files)
            if (auto r = interp->addFile(path, body); !r) problem += std::format("{}: {}\n", path, r.error().describe());
        (void)interp->addFile("_opense4_test_runner.py", std::string(kRunner));
        (void)interp->addNativeFunction("_opense4_testing", "view", [&](std::span<const Value> args) -> Value {
            if (!rules) throw script::NativeError("RuntimeError", "game_view() needs a data set: run opense4-sdk test with --data, or the installed game");
            int64_t empire = 0;
            if (!args.empty())
                if (const Value* e = args[0].find("empire"); e && e->isInt()) empire = e->asInt();
            const game::GameState& s = game.get();
            if (empire < 0 || static_cast<size_t>(empire) >= s.empires.size()) throw script::NativeError("ValueError", "no such empire in the test game");
            return sdk::buildView(*rules, s, game::EmpireId{static_cast<uint32_t>(empire)});
        });
        (void)interp->addNativeFunction("_opense4_testing", "rules", [&](std::span<const Value>) -> Value {
            if (!rules) throw script::NativeError("RuntimeError", "game_rules() needs a data set: run opense4-sdk test with --data, or the installed game");
            return sdk::buildRulesView(*rules);
        });
    });
    if (!problem.empty() && !interp) {
        std::printf("  FAIL  the game's runtime could not start: %s\n", problem.c_str());
        ++tally.failed;
        return;
    }
    if (!problem.empty()) std::printf("  warning: %s", problem.c_str());

    ValueList names;
    for (const std::string& m : modules) names.push_back(Value(m));
    script::Result<Value> found = std::unexpected(script::Error{});
    worker.run([&] { found = interp->call("_opense4_test_runner", "discover", std::vector<Value>{Value(std::move(names))}, script::CallOptions{budget}); });
    if (!found) {
        std::printf("  FAIL  the tests could not be read: %s\n", found.error().describe().c_str());
        ++tally.failed;
    } else {
        for (const Value& e : field(*found, "errors")->asList()) {
            std::printf("  FAIL  %s: %s\n", text(e, "test").c_str(), text(e, "message").c_str());
            printIndented(text(e, "traceback"));
            ++tally.failed;
        }
        for (const Value& t : field(*found, "tests")->asList()) {
            const auto t0 = std::chrono::steady_clock::now();
            script::Result<Value> r = std::unexpected(script::Error{});
            worker.run([&] { r = interp->call("_opense4_test_runner", "run_one", std::vector<Value>{t}, script::CallOptions{budget}); });
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!r) {
                std::printf("  FAIL  %s: %s\n", t.asString().c_str(), r.error().describe().c_str());
                printIndented(r.error().traceback);
                ++tally.failed;
                continue;
            }
            const std::string outcome = text(*r, "outcome");
            if (outcome == "passed") {
                std::printf("  ok    %s (%.0f ms)\n", t.asString().c_str(), ms);
                ++tally.passed;
            } else if (outcome == "skipped") {
                std::printf("  skip  %s: %s\n", t.asString().c_str(), text(*r, "message").c_str());
                ++tally.skipped;
            } else {
                std::printf("  FAIL  %s: %s\n", t.asString().c_str(), text(*r, "message").c_str());
                printIndented(text(*r, "traceback"));
                ++tally.failed;
            }
        }
    }
    if (const std::string& out = interp->output(); !out.empty()) {
        std::printf("  What the tests printed:\n");
        printIndented(out);
    }
    worker.run([&] { interp.reset(); });
}

// A short game for each of the mod's players against the classic AI.
void runGames(const mods::Package& mod, const game::Rules& rules, uint64_t seed, uint32_t turns, bool turnBased, Tally& tally) {
    std::printf("Games (seed %llu, %u turns, %s):\n", static_cast<unsigned long long>(seed), turns, turnBased ? "turn-based" : "simultaneous");
    if (mod.manifest.aiPlayers.empty()) {
        std::printf("  none: the mod declares no computer player\n");
        return;
    }
    for (const mods::AiPlayer& p : mod.manifest.aiPlayers) {
        GameChoice choice;
        choice.turnBased = turnBased;
        auto setup = makeGameSetup(rules, choice, seed, 2);
        const std::string label = std::format("{}:{} against builtin", mod.id(), p.name);
        if (!setup) {
            std::printf("  FAIL  %s: %s\n", label.c_str(), setup.error().c_str());
            ++tally.failed;
            continue;
        }
        sdk::MatchSetup match;
        match.game = std::move(*setup);
        match.turns = turns;
        match.seats.resize(2);
        match.seats[0].controller.kind = game::Controller::Kind::Script;
        match.seats[0].controller.mod = mod.id();
        match.seats[0].controller.player = p.name;
        auto played = sdk::playMatch(rules, std::move(match));
        if (!played) {
            std::printf("  FAIL  %s: %s\n", label.c_str(), played.error().c_str());
            ++tally.failed;
            continue;
        }
        const sdk::SeatResult& seat = played->seats[0];
        const double secs = std::chrono::duration<double>(played->time).count();
        if (seat.failures > 0 || seat.requests == 0) {
            std::printf("  FAIL  %s: %lld failed requests of %lld in %u turns%s\n", label.c_str(), static_cast<long long>(seat.failures),
                        static_cast<long long>(seat.requests), played->turnsPlayed, seat.requests == 0 ? " (the player was never asked)" : "");
            for (const std::string& e : seat.errors) printIndented(e);
            ++tally.failed;
        } else {
            std::printf("  ok    %s: %u turns, %lld requests, no failures, %.1f ms a turn (%.1f s)\n", label.c_str(), played->turnsPlayed,
                        static_cast<long long>(seat.requests),
                        played->turnsPlayed ? std::chrono::duration<double, std::milli>(seat.playerTime).count() / played->turnsPlayed : 0.0, secs);
            ++tally.passed;
        }
    }
}

constexpr std::string_view kTestUsage = R"(opense4-sdk test: a mod's tests (docs/sdk/bots-and-arena.md).

Usage:
  opense4-sdk test <mod> [options]

Runs each test_ function of the mod's tests/test_*.py modules in the game's own Python
(with the mod's ai/ folder and tests/ at the root, and opense4.testing), then a short
game against the classic AI for each computer player the mod declares, which fails on
any failed request (an exception, a wrong answer, a budget run out).

Options:
  --data=DIR       The game folder (or --classic-dir; default: the installed game)
  --mod=OTHER      A mod it requires, when not in the mods folder (repeatable); --mods-dir=DIR
  --turns=N        Turns of each game (default 10)
  --seed=N         The games' seed, and that of opense4.testing.game_view() (default 1)
  --turn-based     Turn-based games (default: simultaneous)
  --budget=N       Bytecodes each test may run (default 1000000000)
  --no-games       Only the Python tests (no data set needed)
  --no-tests       Only the games
Exit status: 0 when everything passed, 1 when something failed, 2 for usage errors.
)";

} // namespace

int cmdTest(const std::vector<std::string>& argv) {
    auto parsed = parseOptions(argv, 2, {"data", "classic-dir", "mod", "mods-dir", "turns", "seed", "budget"},
                               {"no-games", "no-tests", "turn-based", "help"});
    if (!parsed) return fail(parsed.error());
    const Options& o = *parsed;
    if (o.has("help")) {
        std::printf("%.*s", static_cast<int>(kTestUsage.size()), kTestUsage.data());
        return 0;
    }
    if (o.positional.size() != 1) return fail("test needs one mod (see opense4-sdk test --help)");
    auto turns = o.integer("turns", 10, 1, 10000);
    auto seed = o.integer("seed", 1, 0, std::numeric_limits<int64_t>::max());
    auto budget = o.integer("budget", 1'000'000'000, 1000, std::numeric_limits<int64_t>::max() / 4);
    for (const auto* v : {&turns, &seed, &budget})
        if (!*v) return fail(v->error());
    auto target = openMod(o.positional[0], o.get("mods-dir"));
    if (!target) return fail(target.error(), 2);
    const mods::Package mod = *target;
    auto set = modsWithDependencies({*target}, o.all("mod"), o.get("mods-dir"));
    if (!set) {
        std::string why = "the mods could not be loaded:";
        for (const std::string& e : set.error()) why += "\n  " + e;
        return fail(why, 2);
    }
    std::printf("Testing %s (%s)\n", mod.label().c_str(), mod.source.string().c_str());
    std::fflush(stdout);

    Tally tally;
    std::optional<LoadedRules> data;
    std::string dataProblem;
    if (auto loaded = loadRules(dataOption(o), std::move(*set))) data = std::move(*loaded);
    else dataProblem = loaded.error();

    if (!o.has("no-tests")) runPythonTests(mod, data ? data->rules.get() : nullptr, static_cast<uint64_t>(*seed), *budget, tally);
    std::fflush(stdout);
    if (!o.has("no-games")) {
        if (!data) {
            std::printf("Games:\n  FAIL  %s (or --no-games for the Python tests only)\n", dataProblem.c_str());
            ++tally.failed;
        } else {
            runGames(mod, *data->rules, static_cast<uint64_t>(*seed), static_cast<uint32_t>(*turns), o.has("turn-based"), tally);
        }
    }
    std::printf("\n%d passed, %d failed, %d skipped\n", tally.passed, tally.failed, tally.skipped);
    return tally.failed > 0 ? 1 : 0;
}

int cmdRun(const std::vector<std::string>& argv) {
    auto parsed = parseOptions(argv, 2, {"player", "data", "classic-dir", "mod", "mods-dir", "client"}, {"help"});
    if (!parsed) return fail(parsed.error());
    const Options& o = *parsed;
    if (o.has("help") || o.positional.size() != 1) {
        std::printf("opense4-sdk run <mod> [--player=NAME] [--data=DIR] [--mod=OTHER...] [--mods-dir=DIR] [--client=EXE] [-- OPTIONS...]\n\n"
                    "Starts the game with the mod, and the mods it requires, and with --ai for its computer player\n"
                    "(the first it declares, or --player) for the computer empires of new games. What follows --\n"
                    "goes to the game as it is, e.g. -- --quick-start=Terran.\n");
        return o.has("help") ? 0 : 2;
    }
    auto target = openMod(o.positional[0], o.get("mods-dir"));
    if (!target) return fail(target.error(), 2);
    const mods::Package mod = *target;
    auto set = modsWithDependencies({*target}, o.all("mod"), o.get("mods-dir"));
    if (!set) {
        std::string why = "the mods could not be loaded:";
        for (const std::string& e : set.error()) why += "\n  " + e;
        return fail(why, 2);
    }
    const mods::AiPlayer* player = nullptr;
    if (o.has("player")) {
        player = mod.manifest.aiPlayer(o.get("player"));
        if (!player) return fail(std::format("the mod {} declares no computer player named '{}'", mod.id(), o.get("player")), 2);
    } else if (!mod.manifest.aiPlayers.empty()) {
        player = &mod.manifest.aiPlayers.front();
    }
    const fs::path client = o.has("client") ? fs::path(o.get("client")) : sdk::executableDir() / sdk::executableName("opense4");
    sdk::ProcessOptions po;
    po.args.push_back(client.string());
    for (const mods::Package& p : set->packages) po.args.push_back("--mod=" + fs::absolute(p.source).string());
    if (const std::string data = dataOption(o); !data.empty()) po.args.push_back("--classic-dir=" + data);
    if (player) po.args.push_back(std::format("--ai={}:{}", mod.id(), player->name));
    po.args.insert(po.args.end(), o.rest.begin(), o.rest.end());
    std::string line;
    for (const std::string& a : po.args) line += (line.empty() ? "" : " ") + a;
    std::printf("Starting %s\n", line.c_str());
    std::fflush(stdout);
    auto p = sdk::Process::start(po);
    if (!p) return fail(p.error(), 1);
    return p->wait().value_or(1);
}

int cmdBot(const std::vector<std::string>& argv) {
    auto parsed = parseOptions(argv, 2, {"path", "host", "port", "token", "slot", "name", "python", "wait"}, {"reconnect", "help"});
    if (!parsed) return fail(parsed.error());
    const Options& o = *parsed;
    if (o.has("help") || o.positional.size() != 1) {
        std::printf("opense4-sdk bot <module:Class> [--path=DIR...] [--host=H] [--port=N] [--token=T] [--slot=N] [--name=NAME]\n"
                    "                [--reconnect] [--wait=SEC] [--python=EXE]\n\n"
                    "Plays an opense4.ai.Player subclass as an external bot of a game, under CPython 3.10 or newer\n"
                    "(--python, OPENSE4_PYTHON, else python3), with the opense4 package this OpenSE4 is built with.\n"
                    "The options are those of python -m opense4.bot; the token also comes from OPENSE4_BOT_TOKEN.\n");
        return o.has("help") ? 0 : 2;
    }
    auto dir = writePythonPackage(pythonPackageHome());
    if (!dir) return fail(dir.error(), 1);
    std::string python = o.get("python");
    if (python.empty()) python = core::environment("OPENSE4_PYTHON").value_or("");
#ifdef _WIN32
    if (python.empty()) python = "python";
#else
    if (python.empty()) python = "python3";
#endif
    sdk::ProcessOptions po;
    po.args = {python, "-m", "opense4.bot", o.positional[0]};
    for (const std::string& p : o.all("path")) po.args.push_back("--path=" + p);
    for (const char* key : {"host", "port", "slot", "name", "wait"})
        if (o.has(key)) po.args.push_back(std::format("--{}={}", key, o.get(key)));
    if (o.has("reconnect")) po.args.push_back("--reconnect");
    po.environment.emplace_back("PYTHONPATH", pythonPathWith(*dir));
    // The token in the environment, not on the command line others can see.
    if (o.has("token")) po.environment.emplace_back("OPENSE4_BOT_TOKEN", o.get("token"));
    auto p = sdk::Process::start(po);
    if (!p) return fail(std::format("{} (give the Python to use with --python)", p.error()), 1);
    return p->wait().value_or(1);
}

int cmdPython(const std::vector<std::string>& argv) {
    auto parsed = parseOptions(argv, 2, {"out"}, {"help"});
    if (!parsed) return fail(parsed.error());
    if (parsed->has("help")) {
        std::printf("opense4-sdk python [--out=DIR]\n\nWrites the opense4 package this OpenSE4 is built with into DIR/opense4 (default: %s)\n"
                    "and prints DIR, the folder to put on PYTHONPATH for external bots and opense4.env.\n",
                    pythonPackageHome().string().c_str());
        return 0;
    }
    const fs::path dir = parsed->has("out") ? fs::path(parsed->get("out")) : pythonPackageHome();
    auto written = writePythonPackage(dir);
    if (!written) return fail(written.error(), 1);
    std::printf("%s\n", fs::absolute(*written).string().c_str());
    return 0;
}

} // namespace opense4::sdktool
