// Multiplayer: password hashing, protocol, framing, UPnP fallback, a host
// with two clients on 127.0.0.1 (lobby, turns, reconnects, timeouts, admin,
// kicks, hostile input), save/resume, PBEM files and server setup files.
// Everything runs in one process on loopback with ephemeral ports; nothing
// needs a router or the Internet.

#include "engine_fixture.hpp"

#include "game/serialize.hpp"
#include "net/auth.hpp"
#include "net/client.hpp"
#include "net/connection.hpp"
#include "net/host.hpp"
#include "net/pbem.hpp"
#include "net/protocol.hpp"
#include "net/socket.hpp"
#include "net/upnp.hpp"
#include "server/setup_file.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>

using namespace opense4;
using namespace opense4::test;
using net::EventType;

// ---- Passwords --------------------------------------------------------------------------------

TEST_CASE("net: SHA-256 and password hashing") {
    CHECK(net::toHex(net::Sha256::of("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(net::toHex(net::Sha256::of("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(net::toHex(net::Sha256::of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    net::Sha256 chunked;
    const std::string million(1000000, 'a');
    for (size_t i = 0; i < million.size(); i += 777) chunked.update(std::string_view(million).substr(i, 777));
    CHECK(net::toHex(chunked.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    CHECK(net::hashPassword("").empty());
    CHECK(net::passwordVerifier("").empty());
    const std::string h = net::hashPassword("hunter2");
    CHECK(h.size() == 64);
    CHECK(h == net::hashPassword("hunter2"));
    CHECK(h != net::hashPassword("hunter3"));
    CHECK(h != net::toHex(net::Sha256::of("hunter2")));  // domain-separated
    const std::string v = net::passwordVerifier(h);
    CHECK(v != h);
    CHECK(net::checkPassword(v, h));
    CHECK_FALSE(net::checkPassword(v, net::hashPassword("nope")));
    CHECK_FALSE(net::checkPassword(v, ""));
    CHECK(net::checkPassword("", "anything"));  // no password set
    CHECK(net::constantTimeEquals("abc", "abc"));
    CHECK_FALSE(net::constantTimeEquals("abc", "abd"));
    CHECK_FALSE(net::constantTimeEquals("abc", "abcd"));
    CHECK(net::randomId() != net::randomId());
}

// ---- Protocol and framing ------------------------------------------------------------------------

TEST_CASE("net: protocol messages round trip") {
    net::LobbyInfo lobby;
    lobby.gameName = "Test";
    lobby.gameId = 99;
    lobby.humanSlots = 2;
    lobby.options.systemCount = 17;
    net::LobbySlot slot;
    slot.id = 4;
    slot.player = "alice";
    slot.ready = true;
    slot.setup.name = "Alice's Empire";
    slot.setup.customRace = game::Race{};
    lobby.slots = {slot, net::LobbySlot{}};
    net::LobbyInfo back;
    std::string error;
    REQUIRE(net::proto::decode(net::proto::encode(lobby), back, error));
    CHECK(back.gameName == "Test");
    CHECK(back.options.systemCount == 17);
    REQUIRE(back.slots.size() == 2);
    CHECK(back.slots[0].player == "alice");
    CHECK(back.slots[0].setup.customRace.has_value());
    CHECK(back.slots[1].open());

    net::proto::Hello hello;
    hello.player = "bob";
    hello.dataSet = "x#1";
    net::proto::Hello helloBack;
    REQUIRE(net::proto::decode(net::proto::encode(hello), helloBack, error));
    CHECK(helloBack.player == "bob");
    CHECK(helloBack.magic == net::proto::kMagic);

    std::vector<uint8_t> bytes = net::proto::encode(hello);
    bytes.pop_back();
    CHECK_FALSE(net::proto::decode(bytes, helloBack, error));
    CHECK_FALSE(error.empty());

    CHECK(net::proto::validPlayerName("Alice Smith"));
    CHECK_FALSE(net::proto::validPlayerName(""));
    CHECK_FALSE(net::proto::validPlayerName(" alice"));
    CHECK_FALSE(net::proto::validPlayerName(std::string(40, 'x')));
    CHECK_FALSE(net::proto::validPlayerName("a\nb"));
    CHECK(net::proto::sanitize("a\x01" "b\nc", 10) == "a b c");
    CHECK(net::describe(net::Event{EventType::Chat, "hi", "alice"}) == "[chat] alice: hi");
}

namespace {

// A connected pair of sockets on loopback.
std::pair<net::Socket, net::Socket> socketPair() {
    auto listener = net::listenTcp("127.0.0.1", 0);
    REQUIRE(listener.has_value());
    auto client = net::connectTcp("127.0.0.1", listener->localPort());
    REQUIRE(client.has_value());
    net::Socket server;
    for (int i = 0; i < 200 && !server.valid(); ++i) {
        net::PollItem item{listener->native(), true, false};
        net::pollSockets(std::span(&item, 1), 10);
        server = net::acceptConnection(*listener);
    }
    REQUIRE(server.valid());
    return {std::move(server), std::move(*client)};
}

} // namespace

TEST_CASE("net: framed connections") {
    auto [a, b] = socketPair();
    net::Connection left(std::move(a), 1024);
    net::Connection right(std::move(b), 1024);
    const std::vector<uint8_t> big(100000, 7);
    left.send(net::proto::MsgType::Chat, net::proto::Chat{"alice", "hello"});
    left.sendRaw(net::proto::MsgType::Notice, std::span<const uint8_t>{});
    right.setMaxIncoming(200000);
    left.sendRaw(net::proto::MsgType::State, big);

    std::vector<net::Frame> frames;
    for (int i = 0; i < 500 && frames.size() < 3; ++i) {
        left.flush();
        net::PollItem item{right.socket().native(), true, false};
        net::pollSockets(std::span(&item, 1), 5);
        right.receive();
        while (auto f = right.nextFrame()) frames.push_back(std::move(*f));
    }
    REQUIRE(frames.size() == 3);
    CHECK(frames[0].type == net::proto::MsgType::Chat);
    net::proto::Chat chat;
    std::string error;
    REQUIRE(net::proto::decode(frames[0].payload, chat, error));
    CHECK(chat.text == "hello");
    CHECK(frames[1].payload.empty());
    CHECK(frames[2].payload == big);

    // A frame over the limit fails the receiving connection.
    right.setMaxIncoming(1000);
    left.sendRaw(net::proto::MsgType::State, big);
    for (int i = 0; i < 500 && !right.failed(); ++i) {
        left.flush();
        net::PollItem item{right.socket().native(), true, false};
        net::pollSockets(std::span(&item, 1), 5);
        right.receive();
        right.nextFrame();
    }
    CHECK(right.failed());
    CHECK(right.error().find("too large") != std::string::npos);
}

// ---- UPnP --------------------------------------------------------------------------------------------

TEST_CASE("net: port mapper fallback without a router") {
    net::PortMapper mapper;
    CHECK(mapper.status().state == net::PortMapState::Disabled);
    net::PortMapperOptions off;
    off.enabled = false;
    mapper.start(6720, off);
    auto update = mapper.takeUpdate();
    REQUIRE(update.has_value());
    CHECK(update->state == net::PortMapState::Disabled);
    CHECK(update->internalPort == 6720);
    CHECK(update->message.find("forward TCP port 6720") != std::string::npos);
    CHECK_FALSE(mapper.takeUpdate().has_value());
    mapper.stop();
    mapper.stop();
    if (!net::PortMapper::supported()) {
        mapper.start(1234);  // enabled, but compiled out
        CHECK(mapper.status().state == net::PortMapState::Disabled);
        CHECK(mapper.status().message.find("no UPnP support") != std::string::npos);
    }
    CHECK(net::manualForwardingAdvice(6720, "192.168.1.5").find("192.168.1.5") != std::string::npos);
}

// ---- Host and clients on loopback -----------------------------------------------------------------------

namespace {

net::HostConfig hostConfig(int humans = 2) {
    net::HostConfig c;
    c.gameName = "Loopback";
    c.bindAddress = "127.0.0.1";
    c.port = 0;
    c.humanSlots = humans;
    c.upnp.enabled = false;
    c.setup.seed = 21;
    c.setup.options.systemCount = 10;
    return c;
}

net::ClientConfig clientConfig(const net::HostSession& host, std::string name, std::string password = {}) {
    net::ClientConfig c;
    c.port = host.port();
    c.playerName = std::move(name);
    c.passwordHash = net::hashPassword(password);
    c.dataSet = game::dataSetIdentity(engineRules());
    return c;
}

// Polls a host and its clients until a condition holds.
struct Loop {
    net::HostSession& host;
    std::vector<net::ClientSession*> clients;
    std::vector<net::Event> hostEvents;
    std::vector<std::vector<net::Event>> clientEvents;

    Loop(net::HostSession& h, std::vector<net::ClientSession*> cs) : host(h), clients(std::move(cs)), clientEvents(clients.size()) {}

    void step() {
        for (auto& e : host.poll(1)) hostEvents.push_back(std::move(e));
        for (size_t i = 0; i < clients.size(); ++i)
            for (auto& e : clients[i]->poll(1)) clientEvents[i].push_back(std::move(e));
    }
    bool until(const std::function<bool()>& done, int ms = 5000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < deadline) {
            step();
            if (done()) return true;
        }
        return false;
    }
    bool hostSaw(EventType t) const {
        return std::any_of(hostEvents.begin(), hostEvents.end(), [&](const net::Event& e) { return e.type == t; });
    }
    const net::Event* clientSaw(size_t i, EventType t) const {
        for (const auto& e : clientEvents[i])
            if (e.type == t) return &e;
        return nullptr;
    }
    void clear() {
        hostEvents.clear();
        for (auto& v : clientEvents) v.clear();
    }
};

game::EmpireOrders noteOrders(const net::ClientSession& c, const std::string& note) {
    return game::EmpireOrders{c.empire(), c.state()->turn, {game::cmd::SetSystemNote{game::SystemId{0u}, note}}};
}

std::string noteOf(const game::GameState& s, game::EmpireId e) {
    const auto& notes = s.empire(e).knowledge.notes;
    return notes.empty() ? std::string{} : notes[0];
}

// Two players in a started game with one computer empire.
struct TwoPlayerGame {
    net::HostSession host{engineRules(), hostConfig()};
    net::ClientSession alice{net::ClientConfig{}};
    net::ClientSession bob{net::ClientConfig{}};
    std::unique_ptr<Loop> loop;

    TwoPlayerGame() {
        REQUIRE(host.start().has_value());
        alice.config() = clientConfig(host, "alice", "a-secret");
        bob.config() = clientConfig(host, "bob", "b-secret");
        loop = std::make_unique<Loop>(host, std::vector<net::ClientSession*>{&alice, &bob});
        REQUIRE(alice.connect().has_value());
        REQUIRE(bob.connect().has_value());
        REQUIRE(loop->until([&] { return alice.phase() == net::ClientPhase::Lobby && bob.phase() == net::ClientPhase::Lobby; }));
        REQUIRE(host.addComputerEmpire().has_value());
        game::EmpireSetup a;
        a.name = "Alice's Realm";
        alice.submitSetup(a);
        alice.setReady(true);
        bob.setReady(true);
        REQUIRE(loop->until([&] {
            const auto& l = host.lobby();
            return l.slots.size() == 3 && l.slots[0].ready && l.slots[1].ready && l.slots[0].setup.name == "Alice's Realm";
        }));
        CHECK(host.startProblem().empty());
        REQUIRE(host.startGame().has_value());
        REQUIRE(loop->until([&] { return alice.state() && bob.state(); }));
    }
};

} // namespace

TEST_CASE("net: lobby, game start and a turn with two clients") {
    TwoPlayerGame g;
    net::HostSession& host = g.host;
    Loop& loop = *g.loop;
    CHECK(loop.hostSaw(EventType::Listening));
    CHECK(loop.hostSaw(EventType::PlayerJoined));
    CHECK(loop.clientSaw(0, EventType::Joined));
    CHECK(loop.clientSaw(0, EventType::GameStarted));
    REQUIRE(host.state());
    CHECK(host.state()->empires.size() == 3);
    CHECK(host.state()->empires[0].name == "Alice's Realm");
    CHECK(host.state()->empires[2].kind == game::PlayerKind::Computer);
    CHECK(g.alice.empire() == game::EmpireId{0u});
    CHECK(g.bob.empire() == game::EmpireId{1u});
    CHECK(g.alice.lobby().started);
    // Clients get the state without password verifiers; the host keeps them.
    CHECK_FALSE(host.state()->empires[0].passwordHash.empty());
    CHECK(net::checkPassword(host.state()->empires[0].passwordHash, net::hashPassword("a-secret")));
    CHECK(g.alice.state()->empires[0].passwordHash.empty());
    CHECK(g.alice.state()->galaxy.systems.size() == host.state()->galaxy.systems.size());

    // Chat reaches everyone.
    g.alice.chat("hello all");
    REQUIRE(loop.until([&] { return loop.clientSaw(1, EventType::Chat) != nullptr; }));
    CHECK(loop.clientSaw(1, EventType::Chat)->player == "alice");
    CHECK(loop.clientSaw(1, EventType::Chat)->text == "hello all");

    // The host waits until every human has sent orders.
    loop.clear();
    REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "alice was here")).has_value());
    REQUIRE(loop.until([&] { return g.alice.ordersAccepted(); }));
    REQUIRE(loop.until([&] {
        const auto& t = g.bob.turnStatus();
        return t.empires.size() == 3 && t.empires[0].submitted;
    }));
    CHECK_FALSE(g.bob.turnStatus().empires[1].submitted);
    CHECK(g.bob.turnStatus().empires[1].awaited());
    CHECK_FALSE(g.bob.turnStatus().empires[2].human);
    CHECK(host.state()->turn == 0);

    // Stale or foreign orders are refused.
    game::EmpireOrders wrong = noteOrders(g.bob, "x");
    wrong.turn = 5;
    CHECK_FALSE(g.bob.submitOrders(wrong).has_value());
    wrong = noteOrders(g.bob, "x");
    wrong.empire = game::EmpireId{0u};
    CHECK_FALSE(g.bob.submitOrders(wrong).has_value());

    REQUIRE(g.bob.submitOrders(noteOrders(g.bob, "bob was here")).has_value());
    REQUIRE(loop.until([&] { return g.alice.state()->turn == 1 && g.bob.state()->turn == 1; }));
    CHECK(host.state()->turn == 1);
    CHECK(noteOf(*g.alice.state(), game::EmpireId{0u}) == "alice was here");
    CHECK(noteOf(*g.bob.state(), game::EmpireId{1u}) == "bob was here");
    CHECK(loop.clientSaw(0, EventType::NewTurn));
    CHECK(loop.clientSaw(0, EventType::TurnProcessing));
    CHECK(loop.hostSaw(EventType::OrdersReceived));
    CHECK(loop.hostSaw(EventType::NewTurn));
    CHECK_FALSE(g.alice.ordersAccepted());
    // The clients' copies match the host's (apart from the redacted verifiers).
    game::GameState redacted = *host.state();
    for (auto& e : redacted.empires) e.passwordHash.clear();
    CHECK(game::stateChecksum(*g.alice.state()) == game::stateChecksum(redacted));

    // A second turn; resubmitting replaces earlier orders.
    REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "first try")).has_value());
    REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "second try")).has_value());
    REQUIRE(g.bob.submitOrders(noteOrders(g.bob, "bob again")).has_value());
    REQUIRE(loop.until([&] { return g.alice.state()->turn == 2 && g.bob.state()->turn == 2; }));
    CHECK(noteOf(*g.alice.state(), game::EmpireId{0u}) == "second try");

    // Leaving: the host notices.
    g.bob.disconnect("bye now");
    REQUIRE(loop.until([&] { return loop.hostSaw(EventType::PlayerLeft); }));
    CHECK_FALSE(host.turnStatus().empires[1].connected);
}

TEST_CASE("net: reconnecting, forced turns, turn timeout and computer control") {
    TwoPlayerGame g;
    net::HostSession& host = g.host;
    Loop& loop = *g.loop;

    // Bob drops; Alice's orders alone do not end the turn.
    g.bob.disconnect();
    REQUIRE(loop.until([&] { return !host.turnStatus().empires[1].connected; }));
    REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "waiting")).has_value());
    REQUIRE(loop.until([&] { return g.alice.ordersAccepted(); }));
    for (int i = 0; i < 20; ++i) loop.step();
    CHECK(host.state()->turn == 0);

    // The host forces the turn; the computer plays Bob's empire.
    REQUIRE(host.processTurnNow().has_value());
    REQUIRE(loop.until([&] { return g.alice.state()->turn == 1; }));

    // Wrong password, unknown player: refused. The right password gets the current turn.
    net::ClientSession mallory(clientConfig(host, "bob", "guess"));
    net::ClientSession stranger(clientConfig(host, "stranger"));
    Loop side(host, {&mallory, &stranger});
    REQUIRE(mallory.connect().has_value());
    REQUIRE(stranger.connect().has_value());
    REQUIRE(side.until([&] { return side.clientSaw(0, EventType::Rejected) && side.clientSaw(1, EventType::Rejected); }));
    CHECK(side.clientSaw(0, EventType::Rejected)->text.find("Wrong password") != std::string::npos);
    CHECK(side.clientSaw(1, EventType::Rejected)->text.find("no empire") != std::string::npos);

    loop.clear();
    REQUIRE(g.bob.connect().has_value());
    REQUIRE(loop.until([&] { return g.bob.state() && g.bob.state()->turn == 1 && loop.clientSaw(1, EventType::GameStarted); }));
    CHECK(loop.hostSaw(EventType::PlayerReconnected));
    CHECK(g.bob.empire() == game::EmpireId{1u});
    CHECK(host.turnStatus().empires[1].connected);

    // A second connection with the right password takes over the first.
    net::ClientSession bob2(clientConfig(host, "BOB", "b-secret"));  // names are case-insensitive
    Loop both(host, {&g.bob, &bob2});
    REQUIRE(bob2.connect().has_value());
    REQUIRE(both.until([&] { return bob2.state() != nullptr && g.bob.phase() == net::ClientPhase::Disconnected; }));
    CHECK(both.clientSaw(0, EventType::Disconnected)->text.find("logged in again") != std::string::npos);

    // Turn timeout: Bob (now bob2) sends nothing, the host processes anyway.
    host.setTurnTimeout(1);
    Loop timed(host, {&g.alice, &bob2});
    REQUIRE(timed.until([&] { return bob2.turnStatus().secondsLeft >= 0; }));
    REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "on time")).has_value());
    REQUIRE(timed.until([&] { return g.alice.state()->turn == 2; }, 5000));
    CHECK(std::any_of(timed.hostEvents.begin(), timed.hostEvents.end(),
                      [](const net::Event& e) { return e.type == EventType::Info && e.text.find("Turn time is up") != std::string::npos; }));
    host.setTurnTimeout(0);

    // Hand Bob's empire to the computer: Alice alone ends the turn.
    REQUIRE(host.setAiControl(game::EmpireId{1u}, true).has_value());
    REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "solo")).has_value());
    REQUIRE(timed.until([&] { return g.alice.state()->turn == 3; }));
    CHECK(host.turnStatus().empires[1].aiControl);
    CHECK_FALSE(host.setAiControl(game::EmpireId{2u}, false).has_value());  // a computer empire
}

TEST_CASE("net: joining is checked (data set, passwords, names, capacity)") {
    net::HostConfig cfg = hostConfig(1);
    cfg.joinPasswordHash = net::hashPassword("letmein");
    net::HostSession host(engineRules(), cfg);
    REQUIRE(host.start().has_value());

    net::ClientConfig badData = clientConfig(host, "a");
    badData.dataSet = "mod#0123456789abcdef";
    badData.joinPasswordHash = net::hashPassword("letmein");
    net::ClientConfig noPassword = clientConfig(host, "b");
    net::ClientConfig good = clientConfig(host, "carol", "pw");
    good.joinPasswordHash = net::hashPassword("letmein");
    net::ClientConfig late = good;
    late.playerName = "dave";

    net::ClientSession c1(badData), c2(noPassword), c3(good), c4(late);
    Loop loop(host, {&c1, &c2, &c3});
    REQUIRE(c1.connect().has_value());
    REQUIRE(c2.connect().has_value());
    REQUIRE(c3.connect().has_value());
    REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::Rejected) && loop.clientSaw(1, EventType::Rejected) && c3.phase() == net::ClientPhase::Lobby; }));
    CHECK(loop.clientSaw(0, EventType::Rejected)->text.find("data set") != std::string::npos);
    CHECK(loop.clientSaw(1, EventType::Rejected)->text.find("game password") != std::string::npos);

    Loop loop2(host, {&c4});
    REQUIRE(c4.connect().has_value());
    REQUIRE(loop2.until([&] { return loop2.clientSaw(0, EventType::Rejected) != nullptr; }));
    CHECK(loop2.clientSaw(0, EventType::Rejected)->text.find("full") != std::string::npos);

    net::ClientConfig badName = good;
    badName.playerName = "";
    net::ClientSession c5(badName);
    CHECK_FALSE(c5.connect().has_value());

    // Connecting to a closed port fails cleanly.
    net::ClientConfig nowhere = good;
    host.stop();
    net::ClientSession c6(nowhere);
    Loop loop3(host, {&c6});
    if (c6.connect()) {
        REQUIRE(loop3.until([&] { return c6.phase() == net::ClientPhase::Disconnected; }));
        CHECK(loop3.clientSaw(0, EventType::Disconnected));
    }
}

TEST_CASE("net: admin requests and kicks") {
    net::HostConfig cfg = hostConfig(2);
    cfg.masterPasswordHash = net::hashPassword("master");
    net::HostSession host(engineRules(), cfg);
    REQUIRE(host.start().has_value());
    net::ClientConfig adminCfg = clientConfig(host, "admin");
    adminCfg.masterPasswordHash = net::hashPassword("master");
    net::ClientSession admin(adminCfg);
    net::ClientSession pleb(clientConfig(host, "pleb"));
    Loop loop(host, {&admin, &pleb});
    REQUIRE(admin.connect().has_value());
    REQUIRE(pleb.connect().has_value());
    REQUIRE(loop.until([&] { return admin.phase() == net::ClientPhase::Lobby && pleb.phase() == net::ClientPhase::Lobby; }));
    CHECK(admin.admin());
    CHECK_FALSE(pleb.admin());

    pleb.requestAddComputer();
    REQUIRE(loop.until([&] { return loop.clientSaw(1, EventType::Info) != nullptr; }));
    CHECK(host.lobby().slots.size() == 2);
    admin.requestAddComputer();
    REQUIRE(loop.until([&] { return host.lobby().slots.size() == 3; }));
    CHECK(host.lobby().slots[2].kind == net::SlotKind::Computer);

    // Kick in the lobby: the slot opens and the name is refused from then on.
    REQUIRE(host.kick(host.lobby().slots[1].id, "testing").has_value());
    REQUIRE(loop.until([&] { return pleb.phase() == net::ClientPhase::Disconnected; }));
    CHECK(loop.clientSaw(1, EventType::Disconnected)->text.find("testing") != std::string::npos);
    CHECK(host.lobby().slots[1].open());
    loop.clear();
    REQUIRE(pleb.connect().has_value());
    REQUIRE(loop.until([&] { return loop.clientSaw(1, EventType::Rejected) != nullptr; }));

    // The admin starts the game over the network (it is not ready: force).
    admin.requestStart(false);
    REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::Info) != nullptr; }));
    CHECK(host.phase() == net::HostPhase::Lobby);
    admin.requestStart(true);
    REQUIRE(loop.until([&] { return admin.state() != nullptr; }));
    CHECK(host.state()->empires.size() == 2);  // the open slot was dropped
    admin.requestProcessTurn();
    REQUIRE(loop.until([&] { return admin.state()->turn == 1; }));
}

TEST_CASE("net: hostile connections do not disturb the host") {
    net::HostSession host(engineRules(), hostConfig(1));
    REQUIRE(host.start().has_value());
    Loop loop(host, {});

    // A huge frame length before the greeting.
    auto raw = net::connectTcp("127.0.0.1", host.port());
    REQUIRE(raw.has_value());
    const uint8_t huge[] = {0xf0, 0xff, 0xff, 0xff, 1, 2, 3};
    for (int i = 0; i < 50; ++i) {
        loop.step();
        if (raw->send(huge).status == net::IoStatus::Ok) break;
    }
    // Garbage that happens to be a small frame of an unknown type.
    auto junk = net::connectTcp("127.0.0.1", host.port());
    REQUIRE(junk.has_value());
    const uint8_t frame[] = {3, 0, 0, 0, 200, 9, 9};
    for (int i = 0; i < 50; ++i) {
        loop.step();
        if (junk->send(frame).status == net::IoStatus::Ok) break;
    }
    // Both get closed.
    auto closed = [&](net::Socket& s) {
        uint8_t buf[64];
        const auto r = s.receive(buf);
        return r.status == net::IoStatus::Closed || r.status == net::IoStatus::Error;
    };
    REQUIRE(loop.until([&] { return closed(*raw) && closed(*junk); }));

    // The host still works.
    net::ClientSession fine(clientConfig(host, "fine"));
    Loop loop2(host, {&fine});
    REQUIRE(fine.connect().has_value());
    REQUIRE(loop2.until([&] { return fine.phase() == net::ClientPhase::Lobby; }));
}

TEST_CASE("net: host saves and resumes a network game") {
    std::filesystem::path file = std::filesystem::temp_directory_path() / ("opense4_resume_" + std::to_string(net::randomId()) + ".gam");
    {
        TwoPlayerGame g;
        REQUIRE(g.alice.submitOrders(noteOrders(g.alice, "before save")).has_value());
        REQUIRE(g.bob.submitOrders(noteOrders(g.bob, "bob before save")).has_value());
        REQUIRE(g.loop->until([&] { return g.alice.state()->turn == 1; }));
        REQUIRE(g.host.save(file).has_value());
        auto info = game::readSaveInfo(file);
        REQUIRE(info.has_value());
        CHECK(info->players[0] == "alice");
        CHECK(info->players[1] == "bob");
        CHECK(info->players[2].empty());
        g.host.stop();
        REQUIRE(g.loop->until([&] { return g.alice.phase() == net::ClientPhase::Disconnected; }));
        CHECK(g.loop->clientSaw(0, EventType::Disconnected)->text.find("closed the game") != std::string::npos);
    }
    auto loaded = game::loadGame(file);
    REQUIRE(loaded.has_value());
    net::HostSession host(engineRules(), hostConfig());
    REQUIRE(host.resume(loaded->first, loaded->second).has_value());
    CHECK(host.phase() == net::HostPhase::Playing);
    CHECK(host.lobby().gameName == "Loopback");
    net::ClientSession alice(clientConfig(host, "alice", "a-secret"));
    net::ClientSession bob(clientConfig(host, "bob", "b-secret"));
    Loop loop(host, {&alice, &bob});
    REQUIRE(alice.connect().has_value());
    REQUIRE(bob.connect().has_value());
    REQUIRE(loop.until([&] { return alice.state() && bob.state(); }));
    CHECK(alice.state()->turn == 1);
    CHECK(noteOf(*alice.state(), game::EmpireId{0u}) == "before save");
    REQUIRE(alice.submitOrders(noteOrders(alice, "after load")).has_value());
    REQUIRE(bob.submitOrders(noteOrders(bob, "bob after load")).has_value());
    REQUIRE(loop.until([&] { return alice.state()->turn == 2; }));
    CHECK(noteOf(*alice.state(), game::EmpireId{0u}) == "after load");

    // A master password on the save must be matched.
    game::SaveInfo locked = loaded->second;
    locked.masterPasswordVerifier = net::passwordVerifier(net::hashPassword("m"));
    net::HostSession wrong(engineRules(), hostConfig());
    CHECK_FALSE(wrong.resume(loaded->first, locked).has_value());
    net::HostConfig right = hostConfig();
    right.masterPasswordHash = net::hashPassword("m");
    net::HostSession ok(engineRules(), right);
    CHECK(ok.resume(loaded->first, locked).has_value());
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

// ---- Play by e-mail ------------------------------------------------------------------------------------

TEST_CASE("net: PBEM turn processing from .plr files") {
    namespace fs = std::filesystem;
    const game::Rules& r = engineRules();
    const fs::path dir = fs::temp_directory_path() / ("opense4_pbem_" + std::to_string(net::randomId()));
    const fs::path orders = dir / "orders";
    fs::create_directories(orders);

    game::GameSetup setup;
    setup.seed = 3;
    setup.options.systemCount = 8;
    for (int i = 0; i < 3; ++i) {
        game::EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = i < 2 ? game::PlayerKind::Human : game::PlayerKind::Computer;
        e.passwordHash = net::passwordVerifier(net::hashPassword(std::format("pw{}", i)));
        setup.empires.push_back(e);
    }
    auto state = game::createGame(r, setup);
    REQUIRE(state.has_value());
    game::SaveInfo info;
    info.gameName = "Mail Game";
    info.gameId = 4242;
    info.dataSet = game::dataSetIdentity(r);
    info.masterPasswordVerifier = net::passwordVerifier(net::hashPassword("host"));
    const fs::path gam = dir / "mail.gam";
    REQUIRE(game::saveGame(gam, *state, info).has_value());

    auto make = [&](game::EmpireId e, uint32_t turn, const std::string& note) {
        return game::EmpireOrders{e, turn, {game::cmd::SetSystemNote{game::SystemId{0u}, note}}};
    };
    // Empire 1: valid.
    auto file1 = net::pbem::writePlayerOrders(orders, info, make(game::EmpireId{0u}, 0, "mailed"), net::hashPassword("pw0"));
    REQUIRE(file1.has_value());
    CHECK(file1->filename() == "Mail_Game_01.plr");
    // Empire 2: wrong password.
    REQUIRE(net::pbem::writePlayerOrders(orders, info, make(game::EmpireId{1u}, 0, "forged"), net::hashPassword("pw0")).has_value());
    // Stale turn and another game.
    net::pbem::OrdersFile stale{info.gameName, info.gameId, game::EmpireId{1u}, 7, net::hashPassword("pw1"), make(game::EmpireId{1u}, 7, "old")};
    REQUIRE(net::pbem::writeOrdersFile(orders / "stale.plr", stale).has_value());
    net::pbem::OrdersFile other{"Other", 1, game::EmpireId{1u}, 0, net::hashPassword("pw1"), make(game::EmpireId{1u}, 0, "other")};
    REQUIRE(net::pbem::writeOrdersFile(orders / "other.plr", other).has_value());
    REQUIRE(game::writeFileAtomic(orders / "garbage.plr", std::vector<uint8_t>{1, 2, 3}).has_value());

    net::pbem::ProcessOptions opts;
    opts.masterPasswordHash = net::hashPassword("wrong");
    CHECK_FALSE(net::pbem::processGameFile(r, gam, orders, opts).has_value());
    opts.masterPasswordHash = net::hashPassword("host");
    auto rep = net::pbem::processGameFile(r, gam, orders, opts);
    REQUIRE_MESSAGE(rep.has_value(), (rep ? std::string{} : rep.error()));
    CHECK(rep->turnBefore == 0);
    CHECK(rep->turnAfter == 1);
    REQUIRE(rep->submitted.size() == 1);
    CHECK(rep->submitted[0] == "Empire 1");
    CHECK(rep->playedByComputer == std::vector<std::string>{"Empire 2"});
    CHECK(rep->warnings.size() == 4);
    auto warned = [&](std::string_view what) {
        return std::any_of(rep->warnings.begin(), rep->warnings.end(), [&](const std::string& w) { return w.find(what) != std::string::npos; });
    };
    CHECK(warned("wrong password"));
    CHECK(warned("out of date"));
    CHECK(warned("another game"));
    CHECK(warned("garbage.plr"));
    CHECK_FALSE(fs::exists(*file1));           // used: deleted
    CHECK(fs::exists(orders / "stale.plr"));   // not used: kept
    CHECK(fs::exists(dir / "mail.gam.bak"));

    auto after = game::loadGame(gam);
    REQUIRE(after.has_value());
    CHECK(after->first.turn == 1);
    CHECK(after->second.gameId == 4242);
    CHECK(noteOf(after->first, game::EmpireId{0u}) == "mailed");
    CHECK(noteOf(after->first, game::EmpireId{1u}) != "forged");

    // Round trip of the file format itself.
    auto read = net::pbem::readOrdersFile(orders / "stale.plr");
    REQUIRE(read.has_value());
    CHECK(read->turn == 7);
    CHECK(read->orders.commands.size() == 1);
    CHECK_FALSE(net::pbem::decodeOrdersFile(game::serializeOrders(stale.orders)).has_value());

    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ---- Server setup files ----------------------------------------------------------------------------------

TEST_CASE("net: server setup files") {
    const game::Rules& r = engineRules();
    auto s = server::parseSetup(R"(
name = "Frontier"
seed = 99
master_password = "boss"

[options]
systems = 25
start_tech = 1
no_ruins = true
starting_resources = [1000, 2000, 3000]

[options.victory]
score = 12345

[[empire]]
name = "First"
kind = "human"
player = "alice"
password = "pw"

[[empire]]
kind = "computer"
tier = 2
)",
                                "test.toml", r);
    REQUIRE_MESSAGE(s.has_value(), (s ? std::string{} : s.error()));
    CHECK(s->gameName == "Frontier");
    CHECK(s->seed == 99u);
    CHECK(s->masterPasswordHash == net::hashPassword("boss"));
    CHECK(s->options.systemCount == 25);
    CHECK(s->options.startTechLevel == 1);
    CHECK(s->options.noRuins);
    CHECK(s->options.startingResources == game::Resources{1000, 2000, 3000});
    CHECK(s->options.victory.score);
    CHECK(s->options.victory.scoreValue == 12345);
    REQUIRE(s->empires.size() == 2);
    CHECK(s->empires[0].player == "alice");
    CHECK(s->empires[0].setup.passwordHash == net::hashPassword("pw"));
    CHECK(s->empires[1].setup.kind == game::PlayerKind::Computer);
    CHECK(s->empires[1].setup.presetTier == 2);

    auto bad = server::parseSetup("nmae = \"x\"\n[options]\nsystems = -4\nwarp = true\n[[empire]]\nkind = \"alien\"\n", "bad.toml", r);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().find("bad.toml:1: unknown key 'nmae'") != std::string::npos);
    CHECK(bad.error().find("'systems' must be between") != std::string::npos);
    CHECK(bad.error().find("unknown option 'warp'") != std::string::npos);
    CHECK(bad.error().find("'kind' must be") != std::string::npos);
    CHECK_FALSE(server::parseSetup("name = [", "broken.toml", r).has_value());
}
