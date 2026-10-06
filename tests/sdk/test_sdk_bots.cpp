// External bots over their connection (docs/sdk/ai-protocol.md §10,
// docs/sdk/bots-and-arena.md): the handshake and its refusals, a request with
// services during it, a bot that answers late, one that breaks off, one that
// sends what is not JSON; external empires playing a game through it, and the
// journal answering a turn played again without the bot; and the same player
// class in the game and as a CPython bot, deciding alike.

#include "bots_fixture.hpp"
#include "players_fixture.hpp"

#include "net_fixture.hpp"

#include "game/players.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "net/pbem.hpp"
#include "sdk/bots.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <mutex>
#include <thread>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;
using script::Value;
using script::ValueList;
using script::ValueMap;
using namespace std::chrono_literals;

namespace {

Value map(std::initializer_list<std::pair<std::string, Value>> entries) { return Value(ValueMap(entries)); }

std::unique_ptr<sdk::BotHost> openHost(std::vector<uint32_t> slots, std::chrono::milliseconds timeout = 10s, size_t maxMessage = size_t{64} << 20) {
    sdk::BotHostOptions o;
    o.slots = std::move(slots);
    o.requestTimeout = timeout;
    o.maxMessageBytes = maxMessage;
    o.report = [](const std::string&) {};
    auto h = sdk::BotHost::open(std::move(o));
    REQUIRE_MESSAGE(h.has_value(), (h ? std::string{} : h.error()));
    return std::move(*h);
}

Value request(std::string_view call) {
    return map({{"api", Value(1)}, {"call", Value(call)}, {"empire", Value(1)}, {"turn", Value(3)}, {"seed", Value(1)}, {"view", Value()},
                {"args", Value::emptyMap()}});
}

std::expected<Value, std::string> someServices(std::string_view name, const Value&) {
    if (name == "rules") return map({{"components", Value::emptyList()}});
    return std::unexpected(std::string("ValueError: no such query here"));
}

int64_t idOf(const Value& message) {
    const Value* id = message.find("id");
    return id && id->isInt() ? id->asInt() : -1;
}

Value reply(const Value& body, int64_t id) { return map({{"response", body}, {"id", Value(id)}}); }

void checkRefused(const Value& answer, std::string_view part) {
    const Value* r = answer.find("refused");
    REQUIRE_MESSAGE(r, script::describe(answer));
    const std::string& message = r->find("message")->asString();
    CHECK_MESSAGE(message.find(part) != std::string::npos, message);
}

// A thread that is joined when the test leaves its scope.
struct Joined {
    std::thread thread;
    template <class F>
    explicit Joined(F f) : thread(std::move(f)) {}
    ~Joined() {
        if (thread.joinable()) thread.join();
    }
};

} // namespace

TEST_CASE("sdk bots: a bot is welcomed with the host's token; a wrong token, another api, a slot the game lacks and a first message that is no hello are refused") {
    auto host = openHost({0, 1});
    CHECK(host->token().size() == 32);
    CHECK(host->address() == std::format("127.0.0.1:{}", host->port()));
    RawBot a(host->port());
    const Value w = a.hello(host->token(), 0);
    REQUIRE_MESSAGE(w.find("welcome"), script::describe(w));
    CHECK(w.find("welcome")->find("slot")->asInt() == 0);
    CHECK(w.find("welcome")->find("api")->asInt() == 1);
    CHECK(w.find("welcome")->find("timeout_ms")->asInt() == 10'000);
    CHECK(host->waitFor(std::vector<uint32_t>{0}, 5s));
    CHECK(host->bot(0) != nullptr);
    CHECK(host->bot(1) == nullptr);

    {
        RawBot b(host->port());
        checkRefused(b.hello("not the token", 1), "wrong token");
        CHECK(b.closed());
    }
    {
        RawBot c(host->port());
        checkRefused(c.hello(host->token(), 1, 2), "api 1");
    }
    {
        RawBot d(host->port());
        d.send(map({{"response", Value::emptyMap()}}));
        auto r = d.receive();
        REQUIRE(r.has_value());
        checkRefused(*r, "hello");
    }
    {
        RawBot e(host->port());
        checkRefused(e.hello(host->token(), 5), "no external slot 5");
    }
    // No slot asked: the first free one.
    RawBot f(host->port());
    const Value wf = f.hello(host->token(), std::nullopt);
    REQUIRE_MESSAGE(wf.find("welcome"), script::describe(wf));
    CHECK(wf.find("welcome")->find("slot")->asInt() == 1);
    {
        RawBot g(host->port());
        checkRefused(g.hello(host->token(), std::nullopt), "every external slot");
    }
    CHECK(host->connectedSlots() == std::vector<uint32_t>{0, 1});

    // A bot that connects again takes its slot back; the old connection is told and closed.
    RawBot a2(host->port());
    REQUIRE(a2.hello(host->token(), 0).find("welcome"));
    auto bye = a.receive();
    REQUIRE(bye.has_value());
    CHECK(bye->find("bye"));
    CHECK(a.closed());
    CHECK(host->connected(0));
}

TEST_CASE("sdk bots: a request goes to the bot, which asks services while it works, and its response comes back") {
    auto host = openHost({0});
    RawBot bot(host->port());
    REQUIRE(bot.hello(host->token(), 0).find("welcome"));
    REQUIRE(host->waitFor(std::vector<uint32_t>{0}, 5s));
    Joined player([&] {
        auto r = bot.receive();
        if (!r) return;
        const int64_t id = idOf(*r);
        CHECK(r->find("request")->find("call")->asString() == "orders");
        bot.send(map({{"service", map({{"name", Value("rules")}, {"args", Value::emptyMap()}})}, {"id", Value(id)}}));
        auto result = bot.receive();
        if (!result) return;
        CHECK(result->find("result")->find("components"));
        CHECK(idOf(*result) == id);
        bot.send(map({{"service", map({{"name", Value("query")}, {"args", map({{"name", Value("nothing")}})}})}, {"id", Value(id)}}));
        auto refused = bot.receive();
        if (!refused) return;
        CHECK(refused->find("service_error")->find("type")->asString() == "ValueError");
        CHECK(refused->find("service_error")->find("message")->asString() == "no such query here");
        bot.send(reply(map({{"commands", Value::emptyList()}, {"memory", map({{"n", Value(1)}})}}), id));
    });
    sdk::ExternalBot* b = host->bot(0);
    REQUIRE(b);
    std::vector<std::string> asked;
    auto response = b->request(request("orders"), [&](std::string_view name, const Value& args) {
        asked.emplace_back(name);
        return someServices(name, args);
    });
    REQUIRE_MESSAGE(response.has_value(), (response ? std::string{} : response.error()));
    CHECK(response->find("memory")->find("n")->asInt() == 1);
    CHECK(asked == std::vector<std::string>{"rules", "query"});
}

TEST_CASE("sdk bots: a bot that does not answer in time fails the request; its late answer is dropped and it answers the next one") {
    auto host = openHost({0}, 300ms);
    RawBot bot(host->port());
    REQUIRE(bot.hello(host->token(), 0).find("welcome"));
    REQUIRE(host->waitFor(std::vector<uint32_t>{0}, 5s));
    Joined player([&] {
        auto first = bot.receive();
        if (!first) return;
        std::this_thread::sleep_for(700ms);
        bot.send(reply(map({{"memory", map({{"late", Value(true)}})}}), idOf(*first)));
        auto second = bot.receive();
        if (!second) return;
        CHECK(idOf(*second) == idOf(*first) + 1);
        bot.send(reply(map({{"memory", map({{"late", Value(false)}})}}), idOf(*second)));
    });
    auto late = host->bot(0)->request(request("orders"), someServices);
    REQUIRE_FALSE(late.has_value());
    CHECK_MESSAGE(late.error().find("no answer within 300 ms") != std::string::npos, late.error());
    CHECK(host->connected(0));   // a late bot keeps its connection
    host->setRequestTimeout(10s);
    auto next = host->bot(0)->request(request("economy"), someServices);
    REQUIRE_MESSAGE(next.has_value(), (next ? std::string{} : next.error()));
    CHECK(next->find("memory")->find("late")->asBool() == false);
}

TEST_CASE("sdk bots: a bot that breaks off during a request fails it; a bot that connects again plays on") {
    auto host = openHost({0});
    {
        RawBot bot(host->port());
        REQUIRE(bot.hello(host->token(), 0).find("welcome"));
        REQUIRE(host->waitFor(std::vector<uint32_t>{0}, 5s));
        Joined player([&] {
            auto r = bot.receive();
            if (!r) return;
            bot.send(map({{"service", map({{"name", Value("rules")}, {"args", Value::emptyMap()}})}, {"id", Value(idOf(*r))}}));
            bot.socket.close();   // as a bot that crashed
        });
        auto failed = host->bot(0)->request(request("orders"), someServices);
        REQUIRE_FALSE(failed.has_value());
        CHECK_FALSE(failed.error().empty());
    }
    CHECK_FALSE(host->connected(0));
    CHECK(host->bot(0) == nullptr);

    RawBot again(host->port());
    REQUIRE(again.hello(host->token(), 0).find("welcome"));
    REQUIRE(host->waitFor(std::vector<uint32_t>{0}, 5s));
    Joined player([&] {
        auto r = again.receive();
        if (r) again.send(reply(map({{"answer", Value(true)}}), idOf(*r)));
    });
    auto ok = host->bot(0)->request(request("enter_sector"), someServices);
    REQUIRE_MESSAGE(ok.has_value(), (ok ? std::string{} : ok.error()));
    CHECK(ok->find("answer")->asBool());
}

TEST_CASE("sdk bots: a message that is not JSON fails the request and keeps the connection; one too long ends it") {
    auto host = openHost({0}, 10s, 1024);
    RawBot bot(host->port());
    REQUIRE(bot.hello(host->token(), 0).find("welcome"));
    REQUIRE(host->waitFor(std::vector<uint32_t>{0}, 5s));
    Joined player([&] {
        auto r1 = bot.receive();
        if (!r1) return;
        bot.sendText(std::format("{{\"response\": {{\"memory\": 1.5}}, \"id\": {}}}\n", idOf(*r1)));   // a fraction: not the protocol's JSON
        auto r2 = bot.receive();
        if (!r2) return;
        bot.send(reply(map({{"memory", Value(2)}}), idOf(*r2)));
        auto r3 = bot.receive();
        if (!r3) return;
        bot.sendText(std::string(3000, 'x'));
    });
    auto bad = host->bot(0)->request(request("orders"), someServices);
    REQUIRE_FALSE(bad.has_value());
    CHECK_MESSAGE(bad.error().find("not valid JSON") != std::string::npos, bad.error());
    CHECK(host->connected(0));
    auto good = host->bot(0)->request(request("orders"), someServices);
    REQUIRE_MESSAGE(good.has_value(), (good ? std::string{} : good.error()));
    CHECK(good->find("memory")->asInt() == 2);
    auto huge = host->bot(0)->request(request("orders"), someServices);
    REQUIRE_FALSE(huge.has_value());
    CHECK_MESSAGE(huge.error().find("longer than 1024 bytes") != std::string::npos, huge.error());
    CHECK_FALSE(host->connected(0));
}

TEST_CASE("sdk bots: external empires play a game through the connection; the journal plays a turn again without asking the bot") {
    const Rules& r = test::engineRules();
    auto host = openHost({0});
    RawBot bot(host->port());
    REQUIRE(bot.hello(host->token(), 0).find("welcome"));
    REQUIRE(host->waitFor(std::vector<uint32_t>{0}, 5s));
    std::atomic<bool> stop{false};
    std::mutex callsMutex;
    std::vector<std::string> calls;
    Joined player([&] {
        // As the test double: the classic ministers' commands for the planning calls, nothing for the rest.
        while (!stop) {
            auto m = bot.receive(100);
            if (!m || !m->find("request")) continue;
            const int64_t id = idOf(*m);
            const std::string call = m->find("request")->find("call")->asString();
            {
                std::lock_guard lock(callsMutex);
                calls.push_back(call);
            }
            Value response = Value::emptyMap();
            if (call == "politics" || call == "orders" || call == "economy") {
                bot.send(map({{"service", map({{"name", Value("builtin")}, {"args", map({{"call", Value(call)}})}})}, {"id", Value(id)}}));
                auto result = bot.receive();
                if (!result) return;
                if (const Value* commands = result->find("result")) response.set("commands", *commands);
            }
            bot.send(reply(response, id));
        }
    });
    InstalledPlayers installed(host->playerSetup());
    GameState s = playersGame(11, true, {Controller{}, externalPlayer(0)});
    for (int t = 0; t < 3; ++t) processTurn(r, s, {});
    const GameState before = s;
    processTurn(r, s, {});
    const GameState after = s;
    stop = true;
    player.thread.join();
    bot.socket.close();
    {
        std::lock_guard lock(callsMutex);
        for (const char* call : {"politics", "orders", "economy", "end_session"})
            CHECK_MESSAGE(std::count(calls.begin(), calls.end(), call) >= 4, call);
    }
    CHECK(after.empire(EmpireId{1u}).script.failures == 0);
    CHECK_FALSE(journalOf(after, EmpireId{1u}, "orders").empty());

    // Played again from the journal: nobody is asked, and it comes out the same with the bot gone.
    GameState again = before;
    replayJournal(again, after);
    processTurn(r, again, {});
    CHECK(again.empire(EmpireId{1u}).script.failures == 0);
    CHECK(stateChecksum(again) == stateChecksum(after));
    // Without the journal the bot is missed: three failures, then the classic AI for the rest of the turn.
    GameState unjournaled = before;
    processTurn(r, unjournaled, {});
    CHECK(unjournaled.empire(EmpireId{1u}).script.failures == 3);
    CHECK_FALSE(host->connected(0));
}

namespace {

void playTurns(const Rules& r, GameState& s, int turns) {
    for (int t = 0; t < turns; ++t) {
        if (turnBased(s)) resumeTurnBased(r, s);
        else processTurn(r, s, {});
    }
}

} // namespace

TEST_CASE("sdk bots: the same player class decides alike in the game and as an external CPython bot") {
    const auto python = cpythonExe();
    if (!python) {
        MESSAGE("python3 (3.10 or newer) is not installed: skipped");
        return;
    }
    if (underEmulator()) {
        MESSAGE("under an emulator: skipped");
        return;
    }
    const Rules& r = test::engineRules();
    for (const bool simultaneous : {true, false}) {
        CAPTURE(simultaneous);
        GameState inGame;
        {
            InstalledPlayers installed({}, true);
            GameState s = playersGame(23, simultaneous, {Controller{}, scriptPlayer("Captain"), Controller{}});
            playTurns(r, s, 5);
            inGame = std::move(s);
        }
        auto host = openHost({0});
        sdk::ProcessOptions po;
        po.args = {*python, "-B", "-m", "opense4.bot", "package_player:Captain", "--path", fixtureAiDir().string(), "--port",
                   std::to_string(host->port()), "--slot", "0"};
        po.environment = {{"PYTHONPATH", pythonDir().string()}, {"OPENSE4_BOT_TOKEN", host->token()}};
        test::TempDir logs("sdk_bots_same");
        po.output = logs.path() / "bot.log";
        auto bot = sdk::Process::start(po);
        REQUIRE_MESSAGE(bot.has_value(), (bot ? std::string{} : bot.error()));
        REQUIRE_MESSAGE(host->waitFor(std::vector<uint32_t>{0}, 30s), slurp(po.output));
        GameState asBot;
        {
            InstalledPlayers installed(host->playerSetup(), true);
            GameState s = playersGame(23, simultaneous, {Controller{}, externalPlayer(0), Controller{}});
            playTurns(r, s, 5);
            asBot = std::move(s);
        }
        host->sayGoodbye(map({{"reason", Value("the test is over")}}));
        CHECK(bot->wait(10s) == 0);
        const std::string botLog = slurp(po.output);
        CHECK_MESSAGE(botLog.find("requests answered") != std::string::npos, botLog);

        const EmpireId e{1u};
        CHECK(asBot.empire(e).script.failures == 0);
        CHECK(inGame.empire(e).script.failures == 0);
        const auto a = journalOf(inGame, e);
        const auto b = journalOf(asBot, e);
        REQUIRE(a.size() == b.size());
        CHECK_FALSE(a.empty());
        for (size_t i = 0; i < a.size(); ++i) {
            CAPTURE(i);
            CHECK(a[i].call == b[i].call);
            CHECK(a[i].digest == b[i].digest);
            CHECK(a[i].response == b[i].response);
        }
        CHECK(memoryOf(inGame, e) == memoryOf(asBot, e));
        CHECK(intIn(memoryOf(asBot, e), "plans") >= 5);
        // The whole game alike, but for who plays the empire.
        asBot.empire(e).controller = scriptPlayer("Captain");
        CHECK(stateChecksum(asBot) == stateChecksum(inGame));
    }
}

TEST_CASE("sdk bots: the Python side of the connection, under CPython") {
    const auto python = cpythonExe();
    if (!python) {
        MESSAGE("python3 (3.10 or newer) is not installed: skipped");
        return;
    }
    const std::filesystem::path tests = std::filesystem::path(OPENSE4_SDK_TEST_DIR) / "python";
    const Ran run = runProgram({*python, "-B", (tests / "run_external_tests.py").string()}, {{"PYTHONPATH", pythonDir().string()}});
    CHECK_MESSAGE(run.code == 0, run.out);
    CHECK_MESSAGE(run.out.find(" 0 failed") != std::string::npos, run.out);
    MESSAGE(run.out.substr(run.out.rfind('\n', run.out.size() - 2) + 1));
}

namespace {

// A bot as a test double, in the process: it records each request's call and turn.
struct CountingBot final : sdk::ExternalBot {
    std::vector<std::pair<std::string, int64_t>> asked;
    std::expected<Value, std::string> request(const Value& r, const sdk::ServiceCall&) override {
        asked.emplace_back(r.find("call")->asString(), r.find("turn")->asInt());
        return Value::emptyMap();
    }
    size_t count(std::string_view call, int64_t turn) const {
        return static_cast<size_t>(std::count_if(asked.begin(), asked.end(), [&](const auto& a) { return a.first == call && a.second == turn; }));
    }
};

} // namespace

TEST_CASE("sdk bots: a turn-based play-by-e-mail game file between players' turns keeps what its bot decided, so processing asks it nothing again") {
    namespace fs = std::filesystem;
    CountingBot bot;
    sdk::PlayerSetup players;
    players.externals = [&bot](uint32_t slot) -> sdk::ExternalBot* { return slot == 0 ? &bot : nullptr; };
    InstalledPlayers installed(std::move(players));
    const Rules& r = test::engineRules();
    const test::TempDir tmp("sdk_bots_pbem");
    // Empire 0 the bot's, playing first; empire 1 a human.
    GameSetup setup = playersSetup(3, false, {externalPlayer(0), Controller{}}, 8);
    setup.empires[1].kind = PlayerKind::Human;
    setup.empires[1].passwordHash = net::passwordVerifier("pw1", 77);
    auto created = createGame(r, setup);
    REQUIRE(created.has_value());
    REQUIRE_FALSE(created->playerTurn.started);   // saved between players' turns, as files of OpenSE4 0.6 can be
    SaveInfo info;
    info.gameName = "Mail";
    info.gameId = 77;
    info.dataSet = dataSetIdentity(r);
    const fs::path gam = tmp.path() / "mail.gam";
    REQUIRE(saveGame(gam, *created, info).has_value());

    // The turn files: the bot's empire plays its turn first, and the game file keeps it.
    const auto host = test::newPbemHost();
    auto files = net::pbem::writeTurnFiles(r, gam, tmp.path(), host);
    REQUIRE_MESSAGE(files.has_value(), (files ? std::string{} : files.error()));
    CHECK(bot.count("orders", 0) == 1);
    auto saved = loadGame(gam);
    REQUIRE(saved.has_value());
    CHECK(saved->first.playerTurn.started);
    CHECK(activePlayer(saved->first) == EmpireId{1u});

    // Processing (the human sent nothing: the computer plays that turn) goes on from there.
    net::pbem::ProcessOptions opts;
    opts.host = host;
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), opts);
    REQUIRE_MESSAGE(rep.has_value(), (rep ? std::string{} : rep.error()));
    CHECK(rep->turnBefore == 0);
    CHECK(rep->turnAfter >= 1);
    CHECK(bot.count("orders", 0) == 1);   // never asked twice
    CHECK(bot.count("orders", 1) == 1);   // its next turn
}
