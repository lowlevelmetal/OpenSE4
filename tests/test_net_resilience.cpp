// Network games that go wrong, on loopback: a player who drops out in the
// middle of a turn (simultaneous and turn-based) and comes back with the
// orders given so far; a player's copy of the game that differs from the
// host's (a desync), detected, named and repaired; and garbage, stale and
// repeated messages, which leave the host's turns alone.

#include "engine_fixture.hpp"
#include "net_fixture.hpp"
#include "temp_dir.hpp"

#include "client/classic/net_transport.hpp"
#include "client/classic/session.hpp"
#include "core/log.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace opense4;
using namespace opense4::test;
using net::EventType;

namespace {

size_t countHost(const Loop& loop, EventType t) {
    return static_cast<size_t>(std::count_if(loop.hostEvents.begin(), loop.hostEvents.end(), [&](const net::Event& e) { return e.type == t; }));
}

bool submitted(const net::HostSession& host, game::EmpireId e) {
    const auto& t = host.turnStatus();
    return e.index() < t.empires.size() && t.empires[e.index()].submitted;
}

// Waits for the host to see the player gone, then brings the player back.
void reconnect(Loop& loop, net::ClientSession& c, game::EmpireId e) {
    REQUIRE(loop.until([&] { return !loop.host.turnStatus().empires[e.index()].connected; }));
    REQUIRE(c.connect().has_value());
    REQUIRE(loop.until([&] { return c.phase() == net::ClientPhase::Playing && loop.host.turnStatus().empires[e.index()].connected; }));
}

// Polls only the host (its clients read nothing meanwhile).
bool hostUntil(Loop& loop, const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& e : loop.host.poll(1)) loop.hostEvents.push_back(std::move(e));
        if (done()) return true;
    }
    return false;
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

// ---- Dropping out and coming back --------------------------------------------------------------------------

TEST_CASE("net resilience: simultaneous: a player who drops in the middle of a turn keeps the orders given") {
    TwoPlayerGame g;
    net::HostSession& host = g.host;
    Loop& loop = *g.loop;
    const game::EmpireId aliceE = g.alice.empire();

    // Orders sent but lost with the connection (never left this computer):
    // they go out again once the player is back.
    REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "lost on the way")).has_value());
    g.alice.dropConnection();
    reconnect(loop, g.alice, aliceE);
    REQUIRE(loop.until([&] { return submitted(host, aliceE) && g.alice.ordersAccepted(); }));
    CHECK(host.state()->turn == 0);  // the host waits for Bob, and never lost Alice's turn

    // Orders given while the connection is down wait for it.
    g.alice.dropConnection();
    REQUIRE(loop.until([&] { return !host.turnStatus().empires[aliceE.index()].connected; }));
    loop.clear();
    REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "given while away")).has_value());
    loop.step();
    REQUIRE(loop.clientSaw(0, EventType::Info) != nullptr);
    CHECK(loop.clientSaw(0, EventType::Info)->text.find("Not connected") != std::string::npos);
    reconnect(loop, g.alice, aliceE);
    REQUIRE(loop.until([&] { return g.alice.ordersAccepted(); }));

    // Acknowledged orders survive a drop on the host, and are not doubled.
    g.alice.dropConnection();
    reconnect(loop, g.alice, aliceE);
    REQUIRE(g.bob.submitOrders(noteOrders(g.bob, "bob's turn")).has_value());
    REQUIRE(loop.until([&] { return g.alice.state()->turn == 1 && g.bob.state()->turn == 1; }));
    CHECK(noteOf(*host.state(), aliceE) == "given while away");
    CHECK(viewMatches(g.alice, host));
    CHECK(viewMatches(g.bob, host));
}

TEST_CASE("net resilience: turn-based: a player who drops in the middle of a turn resumes it, and nothing is carried out twice") {
    TwoPlayerGame g(true);
    net::HostSession& host = g.host;
    Loop& loop = *g.loop;
    const game::EmpireId aliceE = g.alice.empire();
    REQUIRE(loop.until([&] { return g.alice.myTurn(); }));

    // A command the host carried out, whose answer was lost with the
    // connection: sent again after the reconnect, and answered again.
    loop.clear();
    REQUIRE(g.alice.play(game::cmd::SetSystemNote{game::SystemId{0u}, "first"}).has_value());
    g.alice.poll(0);  // sends it
    REQUIRE(hostUntil(loop, [&] { return noteOf(*host.state(), aliceE) == "first"; }));
    // The answer leaves the host; Alice reads nothing.
    for (int i = 0; i < 3; ++i)
        for (auto& e : host.poll(1)) loop.hostEvents.push_back(std::move(e));
    g.alice.dropConnection();
    CHECK(g.alice.pendingRequests() == 1);
    reconnect(loop, g.alice, aliceE);
    REQUIRE(loop.until([&] { return g.alice.pendingRequests() == 0; }));
    CHECK(countHost(loop, EventType::OrdersReceived) == 1);  // carried out once
    const net::Event* done = loop.clientSaw(0, EventType::CommandsDone);
    REQUIRE(done != nullptr);

    // A command lost on the way to the host: sent again, carried out once.
    loop.clear();
    REQUIRE(g.alice.play(game::cmd::SetSystemNote{game::SystemId{0u}, "second"}).has_value());
    g.alice.dropConnection();
    CHECK(noteOf(*host.state(), aliceE) == "first");
    reconnect(loop, g.alice, aliceE);
    REQUIRE(loop.until([&] { return g.alice.pendingRequests() == 0; }));
    CHECK(noteOf(*host.state(), aliceE) == "second");
    CHECK(countHost(loop, EventType::OrdersReceived) == 1);
    CHECK(viewMatches(g.alice, host));
    CHECK(host.activeEmpire() == aliceE);  // still her turn: the host never gave it away

    // Commands and End Turn given while the connection is down go out when it is back.
    g.alice.dropConnection();
    REQUIRE(loop.until([&] { return !host.turnStatus().empires[aliceE.index()].connected; }));
    loop.clear();
    REQUIRE(g.alice.play(game::cmd::SetSystemNote{game::SystemId{0u}, "third"}).has_value());
    REQUIRE(g.alice.endTurn().has_value());
    CHECK(g.alice.pendingRequests() == 2);
    REQUIRE(g.alice.connect().has_value());
    REQUIRE(loop.until([&] { return g.alice.pendingRequests() == 0 && host.activeEmpire() != aliceE; }));
    CHECK(noteOf(*host.state(), aliceE) == "third");
    CHECK(countHost(loop, EventType::OrdersReceived) == 1);
    REQUIRE(loop.until([&] { return g.bob.myTurn(); }));
    CHECK(viewMatches(g.alice, host));
}

// ---- Desyncs ---------------------------------------------------------------------------------------------------

TEST_CASE("net resilience: a player's copy that differs from the host's is detected, named and replaced") {
    const TempDir tmp("desync_log");
    REQUIRE(log::setFile(tmp / "opense4.log"));

    SUBCASE("simultaneous") {
        TwoPlayerGame g;
        net::HostSession& host = g.host;
        Loop& loop = *g.loop;
        // A bug (here: the test) changes the player's copy behind the host's back.
        g.alice.mutableState()->empire(g.alice.empire()).knowledge.notes[0] = "not what the host sent";
        CHECK_FALSE(viewMatches(g.alice, host));
        REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "orders on a bad copy")).has_value());
        REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::Desync) != nullptr && viewMatches(g.alice, host); }));
        const net::Event* seen = loop.clientSaw(0, EventType::Desync);
        CHECK(seen->text.find("Turn 0") != std::string::npos);
        CHECK(seen->text.find("empires") != std::string::npos);
        CHECK(seen->text.find("vehicles") == std::string::npos);  // only the part that differs
        CHECK(loop.hostSaw(EventType::Desync));
        CHECK(loop.clientSaw(0, EventType::StateUpdated));
        // The orders still count, and the game goes on.
        REQUIRE(loop.until([&] { return g.alice.ordersAccepted(); }));
        REQUIRE(g.bob.submitOrders(noteOrders(g.bob, "fine")).has_value());
        REQUIRE(loop.until([&] { return g.alice.state()->turn == 1; }));
        CHECK(noteOf(*host.state(), g.alice.empire()) == "orders on a bad copy");
        CHECK(viewMatches(g.alice, host));
        CHECK_FALSE(loop.clientSaw(1, EventType::Desync));
    }

    SUBCASE("turn-based") {
        TwoPlayerGame g(true);
        net::HostSession& host = g.host;
        Loop& loop = *g.loop;
        REQUIRE(loop.until([&] { return g.alice.myTurn(); }));
        g.alice.mutableState()->peacefulTurns += 3;
        REQUIRE(g.alice.play(game::cmd::SetSystemNote{game::SystemId{0u}, "a command"}).has_value());
        REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::Desync) != nullptr && g.alice.pendingRequests() == 0; }));
        CHECK(loop.clientSaw(0, EventType::Desync)->text.find("counters") != std::string::npos);
        CHECK(viewMatches(g.alice, host));
        CHECK(noteOf(*host.state(), g.alice.empire()) == "a command");
    }

    SUBCASE("in the game client: shown, logged and repaired, the orders kept") {
        TwoPlayerGame g;
        net::HostSession& host = g.host;
        // Alice's client belongs to her game session from here on.
        auto alice = std::make_unique<net::ClientSession>(g.alice.config());
        net::ClientSession* a = alice.get();
        g.alice.disconnect();
        Loop rest(host, {&g.bob});
        REQUIRE(a->connect().has_value());
        REQUIRE(rest.until([&] {
            a->poll(1);
            return a->state() != nullptr;
        }));
        const std::shared_ptr<const game::Rules> rules(&engineRules(), [](const game::Rules*) {});
        client::classic::ClassicSession session(rules, *a->state(), a->empire(), client::classic::SessionKind::NetworkClient);
        auto transport = std::make_unique<client::classic::ClientTransport>(std::move(alice));
        client::classic::ClientTransport* t = transport.get();
        session.setTransport(std::move(transport));
        REQUIRE(session.issue(game::cmd::SetSystemNote{game::SystemId{0u}, "kept"}).ok);
        a->mutableState()->empire(a->empire()).knowledge.notes[1] = "a bad copy";
        session.endTurn();
        REQUIRE(rest.until([&] {
            session.poll();
            return std::any_of(t->log().lines().begin(), t->log().lines().end(), [](const std::string& l) { return l.find("[desync]") != std::string::npos; });
        }));
        REQUIRE(rest.until([&] {
            session.poll();
            return viewMatches(*a, host);
        }));
        session.poll();
        CHECK(session.waitingForOthers());                      // End Turn stays given
        CHECK(noteOf(session.state(), a->empire()) == "kept");  // the orders, on the host's copy
        CHECK(session.ordersThisTurn().size() == 1);
        CHECK(readFile(tmp / "opense4.log").find("differed from the host's") != std::string::npos);
        REQUIRE(g.bob.submitOrders(noteOrders(g.bob, "go")).has_value());
        REQUIRE(rest.until([&] {
            session.poll();
            return session.state().turn == 1;
        }));
        CHECK(noteOf(*host.state(), a->empire()) == "kept");
    }
    log::setFile({});
}

// ---- Hostile and confused peers ---------------------------------------------------------------------------------

TEST_CASE("net resilience: garbage, stale and repeated messages leave the host's turns alone") {
    SUBCASE("simultaneous") {
        TwoPlayerGame g;
        net::HostSession& host = g.host;
        Loop& loop = *g.loop;
        const game::EmpireId bobE = g.bob.empire();
        // Bob's empire, by hand: the new connection takes over his session.
        RawPeer raw;
        REQUIRE(raw.join(loop, host.port(), "bob", "b-secret", 77));
        REQUIRE(raw.waitFor(loop, net::proto::MsgType::State));
        auto ack = [&](net::proto::SubmitOrders m) {
            raw.frames.clear();
            raw.conn->send(net::proto::MsgType::SubmitOrders, m);
            REQUIRE(raw.waitFor(loop, net::proto::MsgType::OrdersAck));
            net::proto::OrdersAck a;
            std::string error;
            REQUIRE(net::proto::decode(raw.find(net::proto::MsgType::OrdersAck)->payload, a, error));
            return a;
        };
        // Garbage where the orders should be.
        net::proto::SubmitOrders garbage{0, {1, 2, 3, 4, 5}, {}};
        CHECK(ack(garbage).text.find("Unreadable") != std::string::npos);
        // A stale turn.
        net::proto::SubmitOrders stale{0, game::serializeOrders(game::EmpireOrders{bobE, 7, {}}), {}};
        stale.turn = 7;
        CHECK_FALSE(ack(stale).ok);
        // The same orders twice: the second replaces the first.
        net::proto::SubmitOrders twice{0, game::serializeOrders(game::EmpireOrders{bobE, 0, {game::cmd::SetSystemNote{game::SystemId{0u}, "bob once"}}}), {}};
        CHECK(ack(twice).ok);
        CHECK(ack(twice).ok);
        CHECK(submitted(host, bobE));
        CHECK(host.state()->turn == 0);
        // An undecodable message ends only that connection.
        raw.frames.clear();
        raw.conn->sendRaw(net::proto::MsgType::ChatSend, std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 9});
        REQUIRE(loop.until([&] {
            raw.pump(loop);
            return raw.conn->peerClosed() || raw.conn->failed() || raw.find(net::proto::MsgType::Bye);
        }));
        // The turn goes on with Alice's orders and Bob's, which the host kept.
        REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "alice")).has_value());
        REQUIRE(loop.until([&] { return host.state()->turn == 1; }));
        CHECK(noteOf(*host.state(), bobE) == "bob once");
        CHECK(noteOf(*host.state(), g.alice.empire()) == "alice");
    }

    SUBCASE("turn-based") {
        TwoPlayerGame g(true);
        net::HostSession& host = g.host;
        Loop& loop = *g.loop;
        const game::EmpireId aliceE = g.alice.empire();
        REQUIRE(loop.until([&] { return host.activeEmpire() == aliceE; }));
        RawPeer raw;
        REQUIRE(raw.join(loop, host.port(), "alice", "a-secret", 99));
        REQUIRE(raw.waitFor(loop, net::proto::MsgType::State));
        auto answer = [&](uint8_t type, const std::vector<uint8_t>& payload) {
            raw.frames.clear();
            raw.conn->sendRaw(static_cast<net::proto::MsgType>(type), payload);
            REQUIRE(raw.waitFor(loop, net::proto::MsgType::PlayResult));
            net::proto::PlayResult r;
            std::string error;
            REQUIRE(net::proto::decode(raw.find(net::proto::MsgType::PlayResult)->payload, r, error));
            return r;
        };
        auto play = [&](uint32_t request, uint32_t turn, const std::string& note) {
            net::proto::PlayCommands m;
            m.turn = turn;
            m.request = request;
            m.orders = game::serializeOrders(game::EmpireOrders{aliceE, turn, {game::cmd::SetSystemNote{game::SystemId{0u}, note}}});
            return net::proto::encode(m);
        };
        const auto playType = static_cast<uint8_t>(net::proto::MsgType::PlayCommands);
        loop.clear();
        // Request 1 carried out; repeated, it is answered again but not carried out.
        CHECK(answer(playType, play(1, 0, "once")).ok);
        CHECK(noteOf(*host.state(), aliceE) == "once");
        const net::proto::PlayResult again = answer(playType, play(1, 0, "changed in the repeat"));
        CHECK(again.ok);
        CHECK(noteOf(*host.state(), aliceE) == "once");
        CHECK(countHost(loop, EventType::OrdersReceived) == 1);
        // A stale turn, and garbage commands: refused, nothing changes.
        const net::proto::PlayResult stale = answer(playType, play(2, 5, "stale"));
        CHECK_FALSE(stale.ok);
        net::proto::PlayCommands junk;
        junk.turn = 0;
        junk.request = 3;
        junk.orders = {9, 9, 9};
        CHECK_FALSE(answer(playType, net::proto::encode(junk)).ok);
        CHECK(noteOf(*host.state(), aliceE) == "once");
        // End Turn twice: the turn passes once.
        const auto endType = static_cast<uint8_t>(net::proto::MsgType::EndTurn);
        CHECK(answer(endType, net::proto::encode(net::proto::EndTurn{0, 4})).ok);
        REQUIRE(loop.until([&] { return host.activeEmpire() == g.bob.empire(); }));
        CHECK(answer(endType, net::proto::encode(net::proto::EndTurn{0, 4})).ok);
        CHECK(host.activeEmpire() == g.bob.empire());
        // Garbage frames end the connection; the host's game goes on.
        raw.conn->sendRaw(net::proto::MsgType::PlayCommands, std::vector<uint8_t>{1});
        REQUIRE(loop.until([&] {
            raw.pump(loop);
            return raw.conn->peerClosed() || raw.conn->failed() || raw.find(net::proto::MsgType::Bye);
        }));
        REQUIRE(loop.until([&] { return g.bob.myTurn(); }));
        std::vector<Sent> log;
        endTurn(loop, g.bob, log);
        CHECK(host.state()->turn == 1);
    }
}
