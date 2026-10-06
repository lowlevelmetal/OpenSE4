// The SDK's tools for computer players, run as a modder runs them, on our own
// fixtures (docs/sdk/bots-and-arena.md): the arena and its report, the same
// games for a seed and a game played again; an external bot in the arena; the
// training environment; opense4-sdk test and run; and external bots on the
// dedicated server beside a network player.

#include "bots_fixture.hpp"
#include "mod_fixture.hpp"
#include "players_fixture.hpp"

#include "game/serialize.hpp"
#include "sdk/process.hpp"

#include <doctest/doctest.h>

#include <thread>

using namespace opense4;
using namespace opense4::test;
using namespace opense4::sdktest;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

const std::string kSdk = OPENSE4_SDK_EXE;
const std::string kServer = OPENSE4_SERVER_EXE;

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

// A mod with a computer player and tests of its own (not in tests/fixtures/mods, which
// other tests read as a mods folder).
fs::path testedMod() { return fs::path(OPENSE4_FIXTURE_DIR) / "sdk" / "ai-tested"; }

// A CSV without one of its columns (by name).
std::string withoutColumn(const std::string& csv, std::string_view column) {
    std::string out;
    std::optional<size_t> drop;
    for (const std::string& line : lines(csv)) {
        std::vector<std::string> cells;
        std::istringstream in(line);
        for (std::string cell; std::getline(in, cell, ',');) cells.push_back(cell);
        if (!drop)
            for (size_t i = 0; i < cells.size(); ++i)
                if (cells[i] == column) drop = i;
        for (size_t i = 0; i < cells.size(); ++i)
            if (!drop || i != *drop) out += cells[i] + ",";
        out += "\n";
    }
    return out;
}

// Waits until a program's log holds `part` (or the program ended); the log.
std::string awaitLog(const fs::path& file, std::string_view part, sdk::Process& p, std::chrono::seconds limit = 30s) {
    const auto until = std::chrono::steady_clock::now() + limit;
    std::string text;
    while (std::chrono::steady_clock::now() < until) {
        text = slurp(file);
        if (contains(text, part) || p.poll()) break;
        std::this_thread::sleep_for(50ms);
    }
    return text;
}

int portAfter(const std::string& text, std::string_view marker) {
    const size_t at = text.find(marker);
    if (at == std::string::npos) return 0;
    return std::atoi(text.c_str() + at + marker.size());
}

} // namespace

TEST_CASE("sdk arena: a mod's player against the classic AI on fixture data, with a report, the same games for a seed, and a game played again") {
    if (underEmulator()) {
        MESSAGE("under an emulator: skipped");
        return;
    }
    GameFolder g("arena");
    TempDir dir("sdk_arena");
    const std::vector<std::pair<std::string, std::string>> env{{"OPENSE4_USER_DIR", (dir.path() / "user").string()}};
    auto arena = [&](const fs::path& out) {
        return runProgram({kSdk, "arena", "--data=" + g.root.string(), "--mod=" + fixtureMod("ai-fixture").string(), "--ai=test.ai-fixture:Captain",
                           "--ai=builtin", "--games=2", "--turns=3", "--seed=4", "--jobs=2", "--out=" + out.string(),
                           "--ratings=" + (dir.path() / "ratings.json").string()},
                          env);
    };
    const fs::path a = dir.path() / "a";
    const Ran first = arena(a);
    REQUIRE_MESSAGE(first.code == 0, first.out);
    CHECK(contains(first.out, "game 1/2, seed 4:"));
    CHECK(contains(first.out, "game 2/2, seed 5:"));
    for (const char* file : {"report.json", "report.csv", "games.csv", "over_time.csv", "games/game-0000.gam", "games/game-0001.gam",
                             "games/game-0000.json", "games/game-0001.result.json", "games/game-0000.log"})
        CHECK_MESSAGE(fs::exists(a / file), file);

    const std::string report = slurp(a / "report.json");
    for (const char* field : {"\"arena\"", "\"ais\"", "\"games\"", "\"wins\"", "\"win_rate\"", "\"score_mean\"", "\"score_median\"", "\"colonies_mean\"",
                              "\"systems_mean\"", "\"tech_levels_mean\"", "\"research_mean\"", "\"ships_mean\"", "\"battles_won\"", "\"battles_lost\"",
                              "\"eliminations\"", "\"requests\"", "\"failures\"", "\"fallbacks\"", "\"player_ms_per_turn\"", "\"elo\"", "\"over_time\"",
                              "\"checksum\"", "\"won_by\""})
        CHECK_MESSAGE(contains(report, field), field);
    const std::vector<std::string> perAi = lines(slurp(a / "report.csv"));
    REQUIRE(perAi.size() == 3);
    CHECK(perAi[0].starts_with("ai,spec,games,seats,wins,win_rate,score_mean,score_median,colonies_mean,systems_mean,tech_levels_mean,"));
    CHECK(perAi[1].starts_with("test.ai-fixture:Captain,test.ai-fixture:Captain,2,2,"));
    CHECK(perAi[2].starts_with("builtin,builtin,2,2,"));
    const std::vector<std::string> games = lines(slurp(a / "games.csv"));
    CHECK(games.size() == 5);   // two games of two seats
    CHECK(lines(slurp(a / "over_time.csv")).size() == 1 + 2 * 3);

    // Each game as its process wrote it: the players change seats from one game to the next.
    auto r0 = script::parseJson(slurp(a / "games" / "game-0000.result.json"));
    auto r1 = script::parseJson(slurp(a / "games" / "game-0001.result.json"));
    REQUIRE(r0.has_value());
    REQUIRE(r1.has_value());
    CHECK(r0->find("seats")->asList()[0].find("ai")->asString() == "test.ai-fixture:Captain");
    CHECK(r1->find("seats")->asList()[0].find("ai")->asString() == "builtin");
    CHECK(r0->find("turns_played")->asInt() == 3);
    for (const script::Value& seat : r0->find("seats")->asList()) {
        CHECK(seat.find("failures")->asInt() == 0);
        CHECK(seat.find("series")->find("score")->size() == 3);
    }
    CHECK(r0->find("seats")->asList()[0].find("requests")->asInt() > 0);
    auto saved = game::loadGame(a / "games" / "game-0000.gam");
    REQUIRE_MESSAGE(saved.has_value(), (saved ? std::string{} : saved.error()));
    CHECK(saved->first.turn == 3);
    CHECK(saved->first.empire(game::EmpireId{0u}).controller == scriptPlayer("Captain"));

    // The same seed, the same games.
    const fs::path b = dir.path() / "b";
    const Ran second = arena(b);
    REQUIRE_MESSAGE(second.code == 0, second.out);
    CHECK(slurp(a / "games.csv") == slurp(b / "games.csv"));
    CHECK(slurp(a / "over_time.csv") == slurp(b / "over_time.csv"));
    // Ratings across both runs.
    const std::string ratings = slurp(dir.path() / "ratings.json");
    CHECK(contains(ratings, "\"elo_tenths\""));
    CHECK(contains(ratings, "\"games\": 4"));

    // Any game plays again to the same end.
    const Ran replay = runProgram({kSdk, "arena", "--replay=" + (a / "games" / "game-0001.json").string()}, env);
    CHECK_MESSAGE(replay.code == 0, replay.out);
    CHECK_MESSAGE(contains(replay.out, "the same as when it was played"), replay.out);

    // Mistakes are said before any game.
    const Ran unknown = runProgram({kSdk, "arena", "--data=" + g.root.string(), "--ai=test.ai-fixture:Captain", "--ai=builtin", "--out=" + (dir.path() / "c").string()}, env);
    CHECK(unknown.code == 2);
    CHECK_MESSAGE(contains(unknown.out, "which the game does not use"), unknown.out);
    CHECK(runProgram({kSdk, "arena", "--ai=external:3"}, env).code == 2);
}

TEST_CASE("sdk arena: an external CPython bot plays its seats as the same player in the game does") {
    const auto python = cpythonExe();
    if (!python || underEmulator()) {
        MESSAGE("python3 (3.10 or newer) is not installed, or under an emulator: skipped");
        return;
    }
    GameFolder g("arena_bot");
    TempDir dir("sdk_arena_bot");
    const std::vector<std::pair<std::string, std::string>> env{{"OPENSE4_USER_DIR", (dir.path() / "user").string()}};
    const std::string bot = std::format("external:{} -B -m opense4.bot package_player:Captain --path \"{}\"", *python, fixtureAiDir().string());
    auto arena = [&](const std::string& first, const fs::path& out) {
        return runProgram({kSdk, "arena", "--data=" + g.root.string(), "--mod=" + fixtureMod("ai-fixture").string(), "--ai=one=" + first,
                           "--ai=two=test.ai-fixture:Captain", "--games=2", "--turns=3", "--seed=9", "--out=" + out.string()},
                          env);
    };
    const Ran withBot = arena(bot, dir.path() / "bot");
    REQUIRE_MESSAGE(withBot.code == 0, withBot.out);
    CHECK(fs::exists(dir.path() / "bot" / "python" / "opense4" / "external.py"));
    CHECK_MESSAGE(contains(slurp(dir.path() / "bot" / "games" / "game-0000-bots" / "bot-0.log"), "requests answered"),
                  slurp(dir.path() / "bot" / "games" / "game-0000-bots" / "bot-0.log"));
    const Ran inGame = arena("test.ai-fixture:Captain", dir.path() / "game");
    REQUIRE_MESSAGE(inGame.code == 0, inGame.out);
    // The same games, but for the checksum (which counts who plays each empire).
    CHECK(withoutColumn(slurp(dir.path() / "bot" / "games.csv"), "checksum") == withoutColumn(slurp(dir.path() / "game" / "games.csv"), "checksum"));
    for (const char* game : {"game-0000.result.json", "game-0001.result.json"}) {
        auto result = script::parseJson(slurp(dir.path() / "bot" / "games" / game));
        REQUIRE(result.has_value());
        for (const script::Value& seat : result->find("seats")->asList()) {
            CHECK(seat.find("failures")->asInt() == 0);
            CHECK(seat.find("fallbacks")->asInt() == 0);
            CHECK(seat.find("requests")->asInt() > 0);
        }
    }
}

TEST_CASE("sdk env: the training environment's engine resets and steps the same way for a seed") {
    const auto python = cpythonExe();
    if (!python || underEmulator()) {
        MESSAGE("python3 (3.10 or newer) is not installed, or under an emulator: skipped");
        return;
    }
    GameFolder g("env");
    TempDir dir("sdk_env");
    const fs::path tests = fs::path(OPENSE4_SDK_TEST_DIR) / "python";
    const Ran run = runProgram({*python, "-B", (tests / "run_external_tests.py").string(), "-k", "test_env_"},
                               {{"PYTHONPATH", pythonDir().string()},
                                {"OPENSE4_SDK", kSdk},
                                {"OPENSE4_ENV_DATA", g.root.string()},
                                {"OPENSE4_USER_DIR", (dir.path() / "user").string()}});
    CHECK_MESSAGE(run.code == 0, run.out);
    CHECK_MESSAGE(contains(run.out, "2 passed, 0 failed, 0 skipped"), run.out);
}

TEST_CASE("sdk test: a mod's Python tests and a game for each of its players, with a summary; what fails, fails it") {
    if (underEmulator()) {
        MESSAGE("under an emulator: skipped");
        return;
    }
    GameFolder g("sdk_test");
    TempDir dir("sdk_test_cmd");
    const std::vector<std::pair<std::string, std::string>> env{{"OPENSE4_USER_DIR", (dir.path() / "user").string()}};
    const Ran ok = runProgram({kSdk, "test", testedMod().string(), "--data=" + g.root.string(), "--turns=3"}, env);
    CHECK_MESSAGE(ok.code == 0, ok.out);
    for (const char* line : {"ok    test_scout.test_it_keeps_the_classic_orders_and_counts_its_turns", "ok    test_scout.test_it_prefers_research_colonies",
                             "ok    test_scout.test_the_data_set_has_components", "Games (seed 1, 3 turns, simultaneous):",
                             "ok    test.ai-tested:Scout against builtin: 3 turns", "4 passed, 0 failed, 0 skipped"})
        CHECK_MESSAGE(contains(ok.out, line), line << "\n" << ok.out);

    // Without a data set, only the Python tests, and those that need a game are skipped.
    const Ran alone = runProgram({kSdk, "test", testedMod().string(), "--no-games", "--data=" + (dir.path() / "nothing").string()}, env);
    CHECK_MESSAGE(alone.code == 0, alone.out);
    CHECK_MESSAGE(contains(alone.out, "1 passed, 0 failed, 2 skipped"), alone.out);

    // A mod whose test and player fail.
    ModDir bad("sdk_test_bad", "test.ai-broken", "1.0.0", "\n[[ai.players]]\nname = \"Broken\"\nmodule = \"broken\"\nclass = \"Broken\"\n");
    bad.file("ai/broken.py", "from opense4 import ai\n\n\nclass Broken(ai.Player):\n    def orders(self, view, orders):\n"
                             "        raise RuntimeError('broken on purpose')\n");
    bad.file("tests/test_broken.py", "def test_fails():\n    assert 1 + 1 == 3, 'arithmetic'\n\n\ndef test_passes():\n    pass\n");
    const Ran broken = runProgram({kSdk, "test", bad.root.string(), "--data=" + g.root.string(), "--turns=2"}, env);
    CHECK(broken.code == 1);
    for (const char* line : {"FAIL  test_broken.test_fails: AssertionError: arithmetic", "ok    test_broken.test_passes",
                             "FAIL  test.ai-broken:Broken against builtin", "broken on purpose", "1 passed, 2 failed, 0 skipped"})
        CHECK_MESSAGE(contains(broken.out, line), line << "\n" << broken.out);
    CHECK(runProgram({kSdk, "test"}, env).code == 2);
}

TEST_CASE("sdk run: starts the game with the mod and its player (opt-in: OPENSE4_CLASSIC_DATA)") {
    const char* data = std::getenv("OPENSE4_CLASSIC_DATA");
    const fs::path client = fs::path(kSdk).parent_path() / sdk::executableName("opense4");
    if (!data || underEmulator() || !fs::exists(client)) {
        MESSAGE("skipped: set OPENSE4_CLASSIC_DATA (with the game client built) to start the game");
        return;
    }
    TempDir dir("sdk_run");
    std::vector<std::string> args{kSdk, "run", testedMod().string()};
    if (std::string_view(data) != "auto") args.push_back(std::string("--data=") + data);
    for (const std::string a : {"--", "--quick-start", "--seed=3", "--turns=2", "--no-audio", "--frames=3"}) args.push_back(a);
    args.push_back("--screenshot=" + (dir.path() / "shot.png").string());
    const Ran run = runProgram(args, {{"SDL_VIDEO_DRIVER", "offscreen"}, {"OPENSE4_USER_DIR", (dir.path() / "user").string()}});
    CHECK_MESSAGE(run.code == 0, run.out);
    CHECK_MESSAGE(contains(run.out, "--mod=" + fs::absolute(testedMod()).string()), run.out);
    CHECK_MESSAGE(contains(run.out, "--ai=test.ai-tested:Scout"), run.out);
    CHECK(fs::exists(dir.path() / "shot.png"));
}

TEST_CASE("sdk bots: on the dedicated server a bot plays a computer empire beside a network player, in simultaneous and turn-based games") {
    const auto python = cpythonExe();
    if (!python || underEmulator()) {
        MESSAGE("python3 (3.10 or newer) is not installed, or under an emulator: skipped");
        return;
    }
    GameFolder g("bots_server");
    const std::string mod = fixtureMod("ai-fixture").string();
    for (const bool turnBased : {false, true}) {
        CAPTURE(turnBased);
        TempDir dir(turnBased ? "sdk_server_tb" : "sdk_server_sim");
        const std::string user = (dir.path() / "user").string();
        writeText(dir.path() / "setup.toml", std::format("name = \"Bots\"\nseed = 5\n[options]\nsystems = 6\nsimultaneous = {}\n\n"
                                                         "[[empire]]\nkind = \"computer\"\nai = \"external:0\"\n",
                                                         turnBased ? "false" : "true"));
        sdk::ProcessOptions server;
        server.args = {kServer, "--port=0", "--players=1", "--no-upnp", "--no-lan-discovery", "--setup=" + (dir.path() / "setup.toml").string(),
                       "--data=" + g.root.string(), "--mod=" + mod, "--bot-port=0", "--bot-token=t0k3n", "--max-turns=2",
                       "--save-dir=" + dir.path().string(), "--host-key=" + (dir.path() / "key.txt").string()};
        server.environment = {{"OPENSE4_USER_DIR", user}};
        server.output = dir.path() / "server.log";
        auto host = sdk::Process::start(server);
        REQUIRE_MESSAGE(host.has_value(), (host ? std::string{} : host.error()));
        std::string serverLog = awaitLog(server.output, "waiting for players on TCP port", *host);
        const int port = portAfter(serverLog, "waiting for players on TCP port ");
        const int botPort = portAfter(serverLog, "connect to 127.0.0.1:");
        REQUIRE_MESSAGE(port > 0, serverLog);
        REQUIRE_MESSAGE(botPort > 0, serverLog);

        sdk::ProcessOptions botOptions;
        botOptions.args = {*python, "-B", "-m", "opense4.bot", "package_player:Captain", "--path", fixtureAiDir().string(), "--port",
                           std::to_string(botPort)};
        botOptions.environment = {{"PYTHONPATH", pythonDir().string()}, {"OPENSE4_BOT_TOKEN", "t0k3n"}};
        botOptions.output = dir.path() / "bot.log";
        auto bot = sdk::Process::start(botOptions);
        REQUIRE(bot.has_value());

        const Ran alice = runProgram({kServer, "bot", "--name=alice", std::format("--connect=127.0.0.1:{}", port), "--turns=2",
                                      "--data=" + g.root.string(), "--mod=" + mod, "--timeout=120"},
                                     {{"OPENSE4_USER_DIR", user}}, 180s);
        CHECK_MESSAGE(alice.code == 0, alice.out << "\n" << slurp(server.output));
        CHECK(host->wait(60s) == 0);
        CHECK(bot->wait(30s) == 0);
        serverLog = slurp(server.output);
        CHECK_MESSAGE(contains(serverLog, "Bots: Captain connected to slot 0"), serverLog);
        CHECK_MESSAGE(!contains(serverLog, "failed"), serverLog);
        CHECK_MESSAGE(contains(slurp(botOptions.output), "the server is shutting down"), slurp(botOptions.output));

        auto saved = game::loadGame(dir.path() / "Bots.gam");
        REQUIRE_MESSAGE(saved.has_value(), (saved ? std::string{} : saved.error()));
        const game::GameState& s = saved->first;
        REQUIRE(s.empires.size() == 2);
        const game::EmpireId cpu{1u};
        CHECK(s.empire(cpu).controller == externalPlayer(0));
        CHECK(s.empire(cpu).script.failures == 0);
        CHECK(intIn(memoryOf(s, cpu), "plans") >= 2);
    }
}
