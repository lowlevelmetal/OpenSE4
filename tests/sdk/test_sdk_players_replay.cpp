// Computer players and the journal (docs/sdk/ai-protocol.md §8): a turn
// played again, a call made again after a battle stop, a battle a window
// showed, all answered from the journal without asking the players again;
// external bots through the host's interface (a test double here); script
// empires in network and play-by-e-mail games, which their host plays.

#include "players_fixture.hpp"

#include "movement_fixture.hpp"
#include "net_fixture.hpp"
#include "temp_dir.hpp"

#include "game/players.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "game/serialize_io.hpp"
#include "game/tactical.hpp"
#include "game/turn.hpp"
#include "net/pbem.hpp"
#include "sdk/codec.hpp"

#include <doctest/doctest.h>

#include <filesystem>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;
using script::Value;
using script::ValueList;
using script::ValueMap;

namespace {

// An external bot as a test double: it counts what it is asked and answers
// with `answer` (nothing by default).
struct Bot final : sdk::ExternalBot {
    std::vector<std::string> calls;
    std::function<Value(const Value& request, const sdk::ServiceCall& services)> answer;

    std::expected<Value, std::string> request(const Value& r, const sdk::ServiceCall& services) override {
        calls.push_back(r.find("call")->asString());
        if (answer) return answer(r, services);
        return Value(ValueMap{});
    }
    size_t count(std::string_view call) const { return static_cast<size_t>(std::count(calls.begin(), calls.end(), call)); }
};

sdk::PlayerSetup botSetup(Bot& bot) {
    sdk::PlayerSetup setup;
    setup.externals = [&bot](uint32_t slot) -> sdk::ExternalBot* { return slot == 0 ? &bot : nullptr; };
    return setup;
}

// A package whose dispatcher fails every request: a player asked again shows.
std::vector<std::pair<std::string, std::string>> refusingPackage() {
    return {{"opense4/__init__.py", ""}, {"opense4/_engine.py", "def dispatch(request):\n    raise RuntimeError('asked again')\n"}};
}

Value map(std::initializer_list<std::pair<std::string, Value>> entries) { return Value(ValueMap(entries)); }

Location at(SystemId s, int x, int y) { return {s, Sector{x, y}}; }

} // namespace

TEST_CASE("sdk players: a turn played again gives the answers the journal holds, without asking the players") {
    const Rules& r = test::engineRules();
    GameState before;
    GameState after;
    {
        InstalledPlayers installed;
        GameState s = playersGame(17, true, {scriptPlayer("Steady"), Controller{}, scriptPlayer("Steady"), Controller{}});
        for (int t = 0; t < 4; ++t) processTurn(r, s, {});
        before = s;
        processTurn(r, s, {});
        after = s;
        CHECK_FALSE(journalOf(after, EmpireId{0u}).empty());
        // A replay that asks the players again gets the same answers: they are deterministic.
        GameState asked = before;
        processTurn(r, asked, {});
        CHECK(stateChecksum(asked) == stateChecksum(after));
    }
    // Played again from the journal, with players that would fail if asked.
    sdk::PlayerSetup refusing;
    refusing.package = refusingPackage();
    InstalledPlayers installed(std::move(refusing));
    GameState again = before;
    replayJournal(again, after);
    CHECK_FALSE(again.journal.replay.empty());
    processTurn(r, again, {});
    CHECK(again.journal.replay.empty());
    CHECK(again.empire(EmpireId{0u}).script.failures == 0);
    CHECK(stateChecksum(again) == stateChecksum(after));
    // Without the journal those players fail, and the built-in AI plays.
    GameState unjournaled = before;
    processTurn(r, unjournaled, {});
    CHECK(unjournaled.empire(EmpireId{0u}).script.failures == 3);
    CHECK(stateChecksum(unjournaled) != stateChecksum(after));
}

namespace {

// A turn-based game on a small map: empire A a human, empire B an external
// bot whose warship stands next to A's ship. In its turn the bot sends the
// warship into A's sector, where they fight.
struct Skirmish {
    mvtest::World w;
    SystemId sys;
    VehicleId scout, warship;

    Skirmish() {
        using namespace mvtest;
        w.s.options.simultaneous = false;
        sys = w.system("A");
        w.exploreAll(kA);
        w.exploreAll(kB);
        scout = w.spawn(w.ship(kA, "Scout", 2, {"Mv Sensor 3", "Test Laser"}), at(sys, 4, 6));
        warship = w.spawn(w.ship(kB, "Warship", 2, {"Mv Sensor 3", "Test Laser", "Test Laser"}), at(sys, 5, 6));
        for (VehicleId id : {scout, warship}) w.v(id).supply = 1000;
        w.s.empire(kB).kind = PlayerKind::Computer;
        w.s.empire(kB).controller = externalPlayer(0);
        sight::updateKnowledge(w.rules(), w.s);
        w.s.playerTurn = PlayerTurn{};
    }

    // The bot: its orders move the warship into the scout's sector; it
    // enters, and fights by its strategies (null rounds) unless told.
    void play(Bot& bot) {
        const VehicleId ship = warship;
        const Location target = at(sys, 4, 6);
        bot.answer = [ship, target](const Value& request, const sdk::ServiceCall&) {
            const std::string call = request.find("call")->asString();
            if (call == "orders") {
                cmd::SetOrders move;
                move.vehicle = ship;
                Order o;
                o.kind = OrderKind::MoveTo;
                o.location = target;
                move.orders = {o};
                return map({{"commands", sdk::encodeCommands(std::vector<Command>{move})}});
            }
            if (call == "enter_sector") return map({{"answer", Value(true)}});
            if (call == "battle_round") return map({{"answer", map({{"orders", Value(ValueList{map({{"kind", Value("auto_phase")}})})}})}});
            return Value(ValueMap{});
        };
    }
};

} // namespace

TEST_CASE("sdk players: a call made again after a battle stop gives the journal's answers; asking again gives the same") {
    using namespace mvtest;
    for (const bool askAgain : {false, true}) {
        INFO("ask again " << askAgain);
        Bot bot;
        Skirmish k;
        k.play(bot);
        InstalledPlayers installed(botSetup(bot));
        const Rules& r = k.w.rules();
        std::vector<BattleAnswer> answers;
        TurnResult res = resumeTurnBased(r, k.w.s, {}, &answers);   // A's turn starts
        REQUIRE_FALSE(res.battle);
        REQUIRE(k.w.s.playerTurn.empire == kA);
        res = endPlayerTurn(r, k.w.s, kA, {}, &answers);              // B's turn: its warship attacks, and the battle stops the call
        REQUIRE(res.battle);
        CHECK(bot.calls == std::vector<std::string>{"politics", "orders", "enter_sector"});
        CHECK(k.w.s.playerTurn.empire == kA);   // the game is as before the call
        REQUIRE(k.w.s.journal.replay.size() == 3);
        if (askAgain) k.w.s.journal.replay.clear();
        answers.push_back(BattleAnswer{});      // fought strategically
        res = endPlayerTurn(r, k.w.s, kA, {}, &answers);
        REQUIRE_FALSE(res.battle);
        CHECK(k.w.s.turn == 1);
        if (askAgain)
            CHECK(bot.calls == std::vector<std::string>{"politics", "orders", "enter_sector", "politics", "orders", "enter_sector", "economy",
                                                        "end_session"});
        else
            CHECK(bot.calls == std::vector<std::string>{"politics", "orders", "enter_sector", "economy", "end_session"});
        CHECK(bot.count("battle_round") == 0);   // the window did not ask it: its side followed its strategies
        static uint64_t first = 0;
        if (!askAgain) first = stateChecksum(k.w.s);
        else CHECK(stateChecksum(k.w.s) == first);
    }
}

TEST_CASE("sdk players: a battle a window showed with its script sides is fought again with their answers, not asking them twice") {
    using namespace mvtest;
    Bot bot;
    Skirmish k;
    k.play(bot);
    sdk::PlayerSetup setup = botSetup(bot);
    InstalledPlayers installed(setup);
    const Rules& r = k.w.rules();
    std::vector<BattleAnswer> answers;
    resumeTurnBased(r, k.w.s, {}, &answers);
    TurnResult res = endPlayerTurn(r, k.w.s, kA, {}, &answers);
    REQUIRE(res.battle);
    const BattleQuestion& q = *res.battle;
    REQUIRE(q.state);

    // The window: the human fights tactically (here it hands its side to the
    // strategies at once); the bot's side asks the bot each combat turn.
    GameState shown = *q.state;
    auto window = sdk::makeSession(r, shown, std::make_shared<const sdk::PlayerSetup>(botSetup(bot)));
    combat::TacticalBattle::Setup ts;
    ts.where = q.where;
    ts.entering = q.entering;
    ts.players = {kA};
    ts.check = q.check;
    ts.scriptPlayers = window.get();
    combat::TacticalBattle battle(r, shown, ts);
    REQUIRE(battle.started());
    while (!battle.finished()) {
        if (battle.awaitingOrders()) {
            combat::TacticalOrder o;
            o.kind = combat::TacticalOrder::Kind::EndPhase;
            o.empire = battle.phaseEmpire();
            REQUIRE(battle.submit(o).empty());
        } else {
            break;
        }
    }
    battle.finish();
    const size_t rounds = bot.count("battle_round");
    CHECK(rounds > 0);
    const std::vector<JournalEntry> decisions = battle.decisions();
    CHECK(decisions.size() == rounds);
    for (const JournalEntry& d : decisions) CHECK(d.call == "battle_round");

    // The call made again with the answer: the bot's rounds come from the window.
    BattleAnswer answer;
    answer.tactical = {kA};
    answer.orders = battle.script();
    answer.decisions = decisions;
    answers.push_back(answer);
    res = endPlayerTurn(r, k.w.s, kA, {}, &answers);
    REQUIRE_FALSE(res.battle);
    CHECK(bot.count("battle_round") == rounds);
    CHECK(bot.count("politics") == 1);
    // The battle came out as the window showed it.
    const CombatRecord* fought = nullptr;
    for (const CombatRecord& c : k.w.s.combats)
        if (c.location == q.where) fought = &c;
    REQUIRE(fought);
    CHECK(fought->events.size() == battle.record().events.size());
    CHECK(serial::hash(fought->events) == serial::hash(battle.record().events));
}

TEST_CASE("sdk players: an external bot gets the requests and may ask the services; it fails like a script player") {
    using namespace mvtest;
    Bot bot;
    InstalledPlayers installed(botSetup(bot));
    const Rules& r = test::engineRules();
    GameState s = playersGame(5, true, {Controller{}, externalPlayer(0), externalPlayer(1)});
    std::vector<Value> seen;
    bot.answer = [&](const Value& request, const sdk::ServiceCall& services) -> Value {
        seen.push_back(request);
        if (request.find("call")->asString() == "orders") {
            auto rules = services("rules", Value(ValueMap{}));
            REQUIRE(rules.has_value());
            CHECK(rules->find("components")->size() == r.data().components.size());
            auto builtin = services("builtin", map({{"call", Value("orders")}}));
            REQUIRE(builtin.has_value());
            CHECK(builtin->isList());
            CHECK_FALSE(services("nonsense", Value(ValueMap{})).has_value());
            return map({{"commands", *builtin}});
        }
        return Value(ValueMap{});
    };
    processTurn(r, s, {});
    REQUIRE(seen.size() == 4);
    CHECK(seen[0].find("player")->find("slot")->asInt() == 0);
    CHECK(seen[0].find("view")->isMap());
    CHECK(s.empire(EmpireId{1u}).script.failures == 0);
    // Slot 1 has no bot: its requests fail, three times, then the built-in AI plays it for the turn.
    CHECK(s.empire(EmpireId{2u}).script.failures == 3);
}

TEST_CASE("sdk players: in a network game the host runs the script players; players see none of their memory") {
    InstalledPlayers installed;
    net::HostSession host(test::engineRules(), test::hostConfig(2, false));
    REQUIRE(host.start().has_value());
    net::ClientSession alice{net::ClientConfig{}}, bob{net::ClientConfig{}};
    alice.config() = test::clientConfig(host, "alice", "a-secret");
    bob.config() = test::clientConfig(host, "bob", "b-secret");
    test::Loop loop(host, {&alice, &bob});
    REQUIRE(alice.connect().has_value());
    REQUIRE(bob.connect().has_value());
    REQUIRE(loop.until([&] { return alice.phase() == net::ClientPhase::Lobby && bob.phase() == net::ClientPhase::Lobby; }));
    EmpireSetup computer;
    computer.controller = scriptPlayer("Steady");
    REQUIRE(host.addComputerEmpire(computer).has_value());
    alice.setReady(true);
    bob.setReady(true);
    REQUIRE(loop.until([&] { return host.lobby().slots.size() == 3 && host.lobby().slots[0].ready && host.lobby().slots[1].ready; }));
    REQUIRE(host.startGame().has_value());
    REQUIRE(loop.until([&] { return alice.state() && bob.state(); }));
    const EmpireId cpu{2u};
    CHECK(host.state()->empire(cpu).controller == scriptPlayer("Steady"));
    for (uint32_t turn = 1; turn <= 2; ++turn) {
        REQUIRE(alice.submitOrders(test::noteOrders(alice, "a")).has_value());
        REQUIRE(bob.submitOrders(test::noteOrders(bob, "b")).has_value());
        REQUIRE(loop.until([&] { return alice.state()->turn == turn && bob.state()->turn == turn; }));
    }
    CHECK(intIn(memoryOf(*host.state(), cpu), "sessions") == 2);
    CHECK(alice.state()->empire(cpu).script.memory.empty());
    CHECK(alice.state()->empire(cpu).controller == scriptPlayer("Steady"));
    CHECK(alice.state()->journal.entries.empty());
    CHECK(test::viewMatches(alice, host));
    CHECK(test::viewMatches(bob, host));
}

TEST_CASE("sdk players: the lobby shows joining players each computer slot's player and the options; the host changes them before the start") {
    InstalledPlayers installed;
    net::HostSession host(test::engineRules(), test::hostConfig(1, false));
    REQUIRE(host.start().has_value());
    net::ClientSession alice{net::ClientConfig{}};
    alice.config() = test::clientConfig(host, "alice", "a-secret");
    test::Loop loop(host, {&alice});
    REQUIRE(alice.connect().has_value());
    REQUIRE(loop.until([&] { return alice.phase() == net::ClientPhase::Lobby; }));
    EmpireSetup computer;
    computer.controller = scriptPlayer("Steady");
    const auto slot = host.addComputerEmpire(computer);
    REQUIRE(slot.has_value());
    REQUIRE(loop.until([&] { return alice.lobby().slots.size() == 2; }));
    CHECK(alice.lobby().slots[1].setup.controller == scriptPlayer("Steady"));
    CHECK_FALSE(alice.lobby().options.aiSeesEverything);

    // The host plays it with another player and lets the computer players see everything.
    EmpireSetup changed = host.lobby().slots[1].setup;
    changed.controller = scriptPlayer("Captain");
    REQUIRE(host.setSlotSetup(*slot, changed).has_value());
    GameOptions options = host.lobby().options;
    options.aiSeesEverything = true;
    REQUIRE(host.setOptions(options).has_value());
    REQUIRE(loop.until([&] { return alice.lobby().options.aiSeesEverything && alice.lobby().slots[1].setup.controller == scriptPlayer("Captain"); }));

    alice.setReady(true);
    REQUIRE(loop.until([&] { return host.lobby().slots[0].ready; }));
    REQUIRE(host.startGame().has_value());
    REQUIRE(loop.until([&] { return alice.state() != nullptr; }));
    CHECK(host.state()->options.aiSeesEverything);
    CHECK(host.state()->empire(EmpireId{1u}).controller == scriptPlayer("Captain"));
    // Once the game started, the options stay.
    CHECK_FALSE(host.setOptions(GameOptions{}).has_value());
}

TEST_CASE("sdk players: in a play-by-e-mail game the host runs the script players, and their memory stays in its game file") {
    namespace fs = std::filesystem;
    InstalledPlayers installed;
    const Rules& r = test::engineRules();
    const test::TempDir tmp("sdk_pbem");
    GameSetup setup = playersSetup(3, true, {Controller{}, Controller{}, scriptPlayer("Steady")}, 8);
    for (int i = 0; i < 2; ++i) {
        setup.empires[static_cast<size_t>(i)].kind = PlayerKind::Human;
        setup.empires[static_cast<size_t>(i)].passwordHash = net::passwordVerifier(std::format("pw{}", i), 77);
    }
    auto created = createGame(r, setup);
    REQUIRE(created.has_value());
    SaveInfo info;
    info.gameName = "Mail";
    info.gameId = 77;
    info.dataSet = dataSetIdentity(r);
    const fs::path gam = tmp.path() / "mail.gam";
    REQUIRE(saveGame(gam, *created, info).has_value());
    net::pbem::ProcessOptions opts;
    opts.host = test::newPbemHost();
    for (int turn = 0; turn < 2; ++turn) {
        auto rep = net::pbem::processGameFile(r, gam, tmp.path(), opts);
        REQUIRE_MESSAGE(rep.has_value(), (rep ? std::string{} : rep.error()));
    }
    auto loaded = loadGame(gam);
    REQUIRE(loaded.has_value());
    const EmpireId cpu{2u};
    CHECK(loaded->first.turn == 2);
    CHECK(intIn(memoryOf(loaded->first, cpu), "sessions") == 2);
    // The players' turn files hold their own view: no memory of another empire.
    CHECK(net::pbem::playerView(r, loaded->first, EmpireId{0u}).empire(cpu).script.memory.empty());
}
