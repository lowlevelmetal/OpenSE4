#pragma once

// Loopback multiplayer helpers shared by the network tests: a host and its
// clients polled together, a started two-player game, turn-based play from a
// client's own view, and a connection that speaks the protocol by hand.

#include "engine_fixture.hpp"

#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "net/auth.hpp"
#include "net/client.hpp"
#include "net/connection.hpp"
#include "net/host.hpp"
#include "net/protocol.hpp"
#include "net/secure.hpp"
#include "net/socket.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace opense4::test {

// A PBEM host's keys, from a fresh secret (as a host key file holds).
inline net::secure::PbemHostKeys newPbemHost() {
    net::crypto::Key secret{};
    net::crypto::randomBytes(secret);
    return net::secure::hostIdentity(secret).pbem;
}

inline net::HostConfig hostConfig(int humans = 2, bool turnBased = false) {
    net::HostConfig c;
    c.gameName = "Loopback";
    c.bindAddress = "127.0.0.1";
    c.port = 0;
    c.humanSlots = humans;
    c.upnp.enabled = false;
    c.setup.seed = 21;
    c.setup.options.systemCount = 10;
    c.setup.options.simultaneous = !turnBased;
    return c;
}

inline net::ClientConfig clientConfig(const net::HostSession& host, std::string name, std::string password = {}) {
    net::ClientConfig c;
    c.port = host.port();
    c.playerName = std::move(name);
    c.password = std::move(password);
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
    bool hostSaw(net::EventType t) const {
        return std::any_of(hostEvents.begin(), hostEvents.end(), [&](const net::Event& e) { return e.type == t; });
    }
    const net::Event* clientSaw(size_t i, net::EventType t) const {
        for (const auto& e : clientEvents[i])
            if (e.type == t) return &e;
        return nullptr;
    }
    void clear() {
        hostEvents.clear();
        for (auto& v : clientEvents) v.clear();
    }
};

inline game::EmpireOrders noteOrders(const net::ClientSession& c, const std::string& note) {
    return game::EmpireOrders{c.empire(), c.state()->turn, {game::cmd::SetSystemNote{game::SystemId{0u}, note}}};
}

inline std::string noteOf(const game::GameState& s, game::EmpireId e) {
    const auto& notes = s.empire(e).knowledge.notes;
    return notes.empty() ? std::string{} : notes[0];
}

// Two players in a started game with one computer empire.
struct TwoPlayerGame {
    net::HostSession host;
    net::ClientSession alice{net::ClientConfig{}};
    net::ClientSession bob{net::ClientConfig{}};
    std::unique_ptr<Loop> loop;

    explicit TwoPlayerGame(bool turnBased = false) : host(engineRules(), hostConfig(2, turnBased)) {
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


// What a client sent in a turn-based game, in the order the host received
// it: a command, or End Turn (no command).
struct Sent {
    game::EmpireId empire;
    std::optional<game::Command> command;
};

inline bool viewMatches(const net::ClientSession& c, const net::HostSession& host) {
    return c.state() && game::stateChecksum(*c.state()) == game::stateChecksum(game::redactForEmpire(engineRules(), *host.state(), c.empire()));
}

// Sends one command and waits for the host's answer.
inline void playOne(Loop& loop, net::ClientSession& c, game::Command cmd, std::vector<Sent>& log) {
    auto request = c.play(cmd);
    REQUIRE_MESSAGE(request.has_value(), (request ? std::string{} : request.error()));
    log.push_back({c.empire(), std::move(cmd)});
    REQUIRE(loop.until([&] { return c.pendingRequests() == 0; }));
}

// The turn of the client whose turn it is, as a player would play it from
// its own view: a note, then every idle ship sent exploring, one command at
// a time; an Attack Sector question is answered with "enter".
inline void playTurn(Loop& loop, net::ClientSession& c, std::vector<Sent>& log) {
    REQUIRE(c.myTurn());
    playOne(loop, c, game::cmd::SetSystemNote{game::SystemId{0u}, std::format("{} was here on turn {}", c.config().playerName, c.state()->turn)},
            log);
    std::vector<game::VehicleId> idle;
    for (const game::Vehicle& v : c.state()->vehicles)
        if (v.owner == c.empire() && v.orders.empty() && !v.fleet.valid() && v.movement > 0) idle.push_back(v.id);
    game::Order explore;
    explore.kind = game::OrderKind::Explore;
    for (game::VehicleId id : idle) {
        game::cmd::SetOrders o;
        o.vehicle = id;
        o.orders = {explore};
        playOne(loop, c, o, log);
        // Each answer is carried out before the next question is looked at.
        while (!c.questions().empty()) {
            const game::EntryQuestion q = c.questions().front();
            playOne(loop, c, game::cmd::EnterSector{q.vehicle, q.fleet, q.where, true}, log);
        }
        CHECK(viewMatches(c, loop.host));
    }
}

inline void endTurn(Loop& loop, net::ClientSession& c, std::vector<Sent>& log) {
    const game::EmpireId e = c.empire();
    const uint32_t turn = c.state()->turn;
    REQUIRE(c.endTurn().has_value());
    log.push_back({e, std::nullopt});
    REQUIRE(loop.until([&] {
        return c.pendingRequests() == 0 && (loop.host.activeEmpire() != e || loop.host.state()->turn != turn) &&
               !loop.host.turnStatus().processing;
    }));
}


// A connection that speaks the protocol by hand, to send what ClientSession would not.
struct RawPeer {
    std::optional<net::Connection> conn;
    std::vector<net::Frame> frames;

    void pump(Loop& loop) {
        loop.step();
        net::PollItem item{conn->socket().native(), true, conn->wantsWrite()};
        net::pollSockets(std::span(&item, 1), 1);
        conn->flush();
        if (item.readable) conn->receive();
        while (auto f = conn->nextFrame()) frames.push_back(std::move(*f));
    }
    const net::Frame* find(net::proto::MsgType t) const {
        for (const auto& f : frames)
            if (f.type == t) return &f;
        return nullptr;
    }
    bool waitFor(Loop& loop, net::proto::MsgType t) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline && !find(t) && !conn->failed() && !conn->keysDiffer()) pump(loop);
        return find(t) != nullptr;
    }
    // The handshake and login, by hand, as ClientSession makes them.
    bool join(Loop& loop, uint16_t port, const std::string& name, const std::string& password, uint64_t clientId = 0) {
        auto sock = net::connectTcp("127.0.0.1", port);
        REQUIRE(sock.has_value());
        conn.emplace(std::move(*sock), size_t{64} << 20);
        const net::crypto::KeyPair mine = net::crypto::newKeyPair();
        net::proto::ClientHello hello;
        hello.app = std::string(net::appVersion());
        hello.ephemeralKey.assign(mine.publicKey.begin(), mine.publicKey.end());
        const std::vector<uint8_t> helloBytes = net::proto::encode(hello);
        conn->sendRaw(net::proto::MsgType::ClientHello, helloBytes);
        if (!waitFor(loop, net::proto::MsgType::ServerHello)) return false;
        const std::vector<uint8_t> answerBytes = find(net::proto::MsgType::ServerHello)->payload;
        net::proto::ServerHello answer;
        std::string error;
        REQUIRE(net::proto::decode(answerBytes, answer, error));
        auto keys = net::secure::clientKeys(mine, answer.ephemeralKey, answer.hostKey, net::crypto::Key{}, helloBytes, answerBytes);
        REQUIRE(keys.has_value());
        conn->startEncryption(keys->send, keys->receive);
        net::proto::Login login;
        login.dataSet = game::dataSetIdentity(engineRules());
        login.player = name;
        login.clientId = clientId;
        const auto passwordKeys = net::passwordKeys(password, answer.gameId);
        login.passwordVerifier = passwordKeys ? passwordKeys->verifier() : std::string{};
        login.passwordProof = net::signWith(passwordKeys, net::secure::loginDigest(keys->sessionId, "player", name));
        conn->send(net::proto::MsgType::Login, login);
        frames.clear();
        return waitFor(loop, net::proto::MsgType::Welcome);
    }
};


} // namespace opense4::test
