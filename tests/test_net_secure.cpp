// Encrypted connections (net/crypto, net/secure, the sealed frames of
// net/connection): the cryptography wrappers, password verifiers that sign,
// host keys and their pins, what an eavesdropper sees, tampered, replayed and
// reordered messages, impostor hosts, and the refusals between protocol
// versions. Everything runs on loopback.

#include "engine_fixture.hpp"
#include "net_fixture.hpp"
#include "temp_dir.hpp"

#include "net/crypto.hpp"
#include "net/secure.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace opense4;
using namespace opense4::test;
using net::EventType;
namespace crypto = opense4::net::crypto;

namespace {

std::span<const uint8_t> bytesOf(std::string_view s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

bool contains(const std::vector<uint8_t>& haystack, std::string_view needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                       [](uint8_t a, char b) { return a == static_cast<uint8_t>(b); }) != haystack.end();
}

// A TCP relay between one client and a host, as a router or a man in the
// middle sits: it sees every byte, and may change, repeat or reorder the
// client's frames (`onClientFrame` returns what to pass on for each).
struct Relay {
    net::Socket listener;
    uint16_t port = 0;
    uint16_t hostPort = 0;
    net::Socket client, host;
    bool closed = false;
    std::vector<uint8_t> pending;              // client bytes not yet a whole frame
    std::vector<uint8_t> toHost, toClient;     // waiting to be written
    std::vector<uint8_t> seenToHost, seenToClient;
    size_t clientFrames = 0;
    std::function<std::vector<std::vector<uint8_t>>(size_t, std::vector<uint8_t>)> onClientFrame;

    explicit Relay(uint16_t to) : hostPort(to) {
        auto l = net::listenTcp("127.0.0.1", 0);
        REQUIRE(l.has_value());
        listener = std::move(*l);
        port = listener.localPort();
    }

    void step() {
        if (!client.valid()) {
            client = net::acceptConnection(listener);
            if (!client.valid()) return;
            auto h = net::connectTcp("127.0.0.1", hostPort);
            REQUIRE(h.has_value());
            host = std::move(*h);
        }
        if (closed) return;
        std::array<uint8_t, 65536> buf{};
        for (;;) {
            const net::IoResult r = client.receive(buf);
            if (r.status != net::IoStatus::Ok) {
                if (r.status != net::IoStatus::WouldBlock) closed = true;
                break;
            }
            seenToHost.insert(seenToHost.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(r.bytes));
            pending.insert(pending.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(r.bytes));
        }
        while (pending.size() >= 4) {
            uint32_t length = 0;
            for (size_t i = 0; i < 4; ++i) length |= static_cast<uint32_t>(pending[i]) << (8 * i);
            if (pending.size() < 4 + size_t{length}) break;
            std::vector<uint8_t> frame(pending.begin(), pending.begin() + 4 + length);
            pending.erase(pending.begin(), pending.begin() + 4 + length);
            const size_t index = clientFrames++;
            std::vector<std::vector<uint8_t>> out;
            if (onClientFrame) out = onClientFrame(index, std::move(frame));
            else out.push_back(std::move(frame));
            for (const auto& f : out) toHost.insert(toHost.end(), f.begin(), f.end());
        }
        for (;;) {
            const net::IoResult r = host.receive(buf);
            if (r.status != net::IoStatus::Ok) {
                if (r.status != net::IoStatus::WouldBlock) closed = true;
                break;
            }
            seenToClient.insert(seenToClient.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(r.bytes));
            toClient.insert(toClient.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(r.bytes));
        }
        auto flush = [](net::Socket& s, std::vector<uint8_t>& out) {
            while (!out.empty()) {
                const net::IoResult r = s.send(out);
                if (r.status != net::IoStatus::Ok || r.bytes == 0) break;
                out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(r.bytes));
            }
        };
        flush(host, toHost);
        flush(client, toClient);
        if (closed) {
            client.close();
            host.close();
        }
    }
};

net::ClientConfig viaRelay(const Relay& relay, const net::HostSession& host, std::string name, std::string password = {}) {
    net::ClientConfig c = clientConfig(host, std::move(name), std::move(password));
    c.port = relay.port;
    return c;
}

} // namespace

// ---- The building blocks ----------------------------------------------------------------------------------

TEST_CASE("net security: the cryptography wrappers") {
    std::array<uint8_t, 32> a{}, b{};
    crypto::randomBytes(a);
    crypto::randomBytes(b);
    CHECK(a != b);
    CHECK(a != std::array<uint8_t, 32>{});

    // X25519: both sides agree; a hostile all-zero key is refused.
    const crypto::KeyPair alice = crypto::newKeyPair(), bob = crypto::newKeyPair();
    crypto::Key ab{}, ba{};
    REQUIRE(crypto::agree(ab, alice.secret, bob.publicKey));
    REQUIRE(crypto::agree(ba, bob.secret, alice.publicKey));
    CHECK(crypto::equal(ab, ba));
    crypto::Key bad{};
    CHECK_FALSE(crypto::agree(bad, alice.secret, crypto::Key{}));
    CHECK(crypto::keyPairFromSecret(alice.secret).publicKey == alice.publicKey);

    // XChaCha20-Poly1305 with a counter nonce: anything changed does not open.
    const crypto::Key key = ab;
    const std::string message = "a fleet moves at dawn";
    std::vector<uint8_t> text(message.begin(), message.end());
    const std::vector<uint8_t> ad{1, 2, 3};
    crypto::Mac mac{};
    crypto::seal(key, 7, ad, text, mac);
    CHECK_FALSE(contains(text, "fleet"));
    auto opens = [&](const crypto::Key& k, uint64_t counter, std::vector<uint8_t> ad2, std::vector<uint8_t> t, crypto::Mac m) {
        return crypto::open(k, counter, ad2, t, m);
    };
    CHECK(opens(key, 7, ad, text, mac));
    CHECK_FALSE(opens(key, 8, ad, text, mac));             // another counter: a replay or a reordering
    CHECK_FALSE(opens(key, 7, {1, 2, 4}, text, mac));      // another header
    CHECK_FALSE(opens(alice.secret, 7, ad, text, mac));                 // another key
    std::vector<uint8_t> flipped = text;
    flipped[3] ^= 1;
    CHECK_FALSE(opens(key, 7, ad, flipped, mac));
    std::vector<uint8_t> back = text;
    REQUIRE(crypto::open(key, 7, ad, back, mac));
    CHECK(std::string(back.begin(), back.end()) == message);

    // Signatures.
    const crypto::SigningKey signer = crypto::signingKey(a);
    CHECK(crypto::signingKey(a).publicKey == signer.publicKey);  // the same seed, the same key
    const crypto::Signature sig = crypto::sign(signer, bytesOf("orders"));
    CHECK(crypto::verify(sig, signer.publicKey, bytesOf("orders")));
    CHECK_FALSE(crypto::verify(sig, signer.publicKey, bytesOf("orderz")));
    CHECK_FALSE(crypto::verify(sig, crypto::signingKey(b).publicKey, bytesOf("orders")));

    // Text forms.
    CHECK(crypto::keyFromHex(crypto::hex(alice.publicKey)) == alice.publicKey);
    CHECK_FALSE(crypto::keyFromHex("xyz").has_value());
    const std::string fp = crypto::fingerprint(alice.publicKey);
    CHECK(fp.size() == 39);  // eight groups of four
    CHECK(fp != crypto::fingerprint(bob.publicKey));
}

TEST_CASE("net security: password verifiers check signatures, and those of OpenSE4 0.6 still work") {
    const std::string hash = net::hashPassword("hunter2");
    const std::string verifier = net::passwordVerifier(hash);
    CHECK(verifier.starts_with("pk1:"));
    CHECK(verifier.size() == 4 + 64);
    CHECK(net::verifierKey(verifier).has_value());
    CHECK(net::checkPassword(verifier, hash));
    CHECK_FALSE(net::checkPassword(verifier, net::hashPassword("hunter3")));
    CHECK_FALSE(net::checkPassword(verifier, ""));
    CHECK(net::checkPassword("", hash));

    // A login or an orders file proves the password by signing.
    const auto message = bytesOf("session 42");
    const crypto::Signature sig = net::signWithPassword(hash, message);
    CHECK(net::checkPasswordSignature(verifier, message, sig));
    CHECK_FALSE(net::checkPasswordSignature(verifier, bytesOf("session 43"), sig));
    CHECK_FALSE(net::checkPasswordSignature(verifier, message, net::signWithPassword(net::hashPassword("guess"), message)));
    CHECK(net::checkPasswordSignature("", message, crypto::Signature{}));  // no password set
    CHECK(net::signWithPassword("", message) == crypto::Signature{});

    // The verifier of OpenSE4 0.6: still checks the hash, but no signature.
    const std::string legacy = net::legacyPasswordVerifier(hash);
    CHECK(net::isLegacyVerifier(legacy));
    CHECK_FALSE(net::isLegacyVerifier(verifier));
    CHECK(net::checkPassword(legacy, hash));
    CHECK_FALSE(net::checkPassword(legacy, net::hashPassword("nope")));
    CHECK_FALSE(net::checkPasswordSignature(legacy, message, sig));
}

TEST_CASE("net security: a host's key is kept in a file, and players remember it per host") {
    const TempDir tmp("hostkey");
    const auto file = tmp / "keys" / "host_key.txt";
    auto first = net::secure::loadOrCreateHostKey(file);
    REQUIRE_MESSAGE(first.has_value(), (first ? std::string{} : first.error()));
    CHECK(std::filesystem::exists(file));
    auto again = net::secure::loadOrCreateHostKey(file);
    REQUIRE(again.has_value());
    CHECK(again->publicKey == first->publicKey);
    {
        std::ofstream broken(tmp / "broken.txt");
        broken << "not a key\n";
    }
    CHECK_FALSE(net::secure::loadOrCreateHostKey(tmp / "broken.txt").has_value());

    net::secure::KnownHosts known(tmp / "known_hosts.txt");
    CHECK_FALSE(known.find("Example.org", 6720).has_value());
    REQUIRE(known.remember("Example.org", 6720, first->publicKey).has_value());
    REQUIRE(known.remember("other.example", 6721, crypto::newKeyPair().publicKey).has_value());
    CHECK(known.find("example.ORG", 6720) == first->publicKey);  // host names in any case
    CHECK_FALSE(known.find("example.org", 6721).has_value());
    const crypto::Key newer = crypto::newKeyPair().publicKey;
    REQUIRE(known.remember("example.org", 6720, newer).has_value());
    CHECK(known.find("example.org", 6720) == newer);
    CHECK(known.find("other.example", 6721).has_value());
}

// ---- On the wire -------------------------------------------------------------------------------------------

TEST_CASE("net security: an eavesdropper sees neither the game nor the passwords") {
    net::HostConfig cfg = hostConfig(1);
    cfg.masterPasswordHash = net::hashPassword("master-pw");
    net::HostSession host(engineRules(), cfg);
    REQUIRE(host.start().has_value());
    REQUIRE(host.addComputerEmpire().has_value());
    Relay relay(host.port());
    net::ClientConfig cc = viaRelay(relay, host, "alice", "a-secret");
    cc.masterPasswordHash = net::hashPassword("master-pw");
    net::ClientSession alice(cc);
    Loop loop(host, {&alice});
    REQUIRE(alice.connect().has_value());
    REQUIRE(loop.until([&] {
        relay.step();
        return alice.phase() == net::ClientPhase::Lobby;
    }));
    CHECK(alice.admin());
    alice.chat("our secret chat");
    alice.setReady(true);
    REQUIRE(loop.until([&] {
        relay.step();
        return host.lobby().slots[0].ready;
    }));
    REQUIRE(host.startGame().has_value());
    REQUIRE(loop.until([&] {
        relay.step();
        return alice.state() != nullptr;
    }));
    REQUIRE(alice.submitOrders(noteOrders(alice, "the secret plan")).has_value());
    REQUIRE(loop.until([&] {
        relay.step();
        return alice.state()->turn == 1;
    }));
    CHECK(noteOf(*alice.state(), alice.empire()) == "the secret plan");

    // On the wire: the versions and the keys of the handshake, nothing else readable.
    for (const auto* stream : {&relay.seenToHost, &relay.seenToClient}) {
        CHECK_FALSE(contains(*stream, "the secret plan"));
        CHECK_FALSE(contains(*stream, "our secret chat"));
        CHECK_FALSE(contains(*stream, "alice"));
        CHECK_FALSE(contains(*stream, net::hashPassword("a-secret")));
        CHECK_FALSE(contains(*stream, net::hashPassword("master-pw")));
        CHECK_FALSE(contains(*stream, net::passwordVerifier(net::hashPassword("a-secret")).substr(4)));
        CHECK_FALSE(contains(*stream, "Loopback"));  // the game's name
        CHECK_FALSE(contains(*stream, game::dataSetIdentity(engineRules())));
    }
    CHECK(contains(relay.seenToHost, std::string(net::appVersion())));
}

TEST_CASE("net security: changed, repeated and reordered messages end the connection") {
    net::HostSession host(engineRules(), hostConfig(2));
    REQUIRE(host.start().has_value());
    // The client's frames: 0 its hello, 1 its login (the first sealed one), then 2 and 3 two chats.
    auto attack = [&](const std::function<std::vector<std::vector<uint8_t>>(size_t, std::vector<uint8_t>)>& onFrame) {
        Relay relay(host.port());
        relay.onClientFrame = onFrame;
        net::ClientSession c(viaRelay(relay, host, "carol"));
        Loop loop(host, {&c});
        REQUIRE(c.connect().has_value());
        REQUIRE(loop.until([&] {
            relay.step();
            return c.phase() == net::ClientPhase::Lobby;
        }));
        c.chat("one");
        c.chat("two");
        const bool dropped = loop.until([&] {
            relay.step();
            return loop.hostSaw(EventType::PlayerLeft);
        });
        std::string why;
        for (const auto& e : loop.hostEvents)
            if (e.type == EventType::PlayerLeft) why = e.text;
        int chats = 0;
        for (const auto& e : loop.hostEvents) chats += e.type == EventType::Chat ? 1 : 0;
        c.disconnect();
        for (int i = 0; i < 5; ++i) loop.step();
        return std::tuple{dropped, why, chats};
    };

    SUBCASE("a byte changed") {
        auto [dropped, why, chats] = attack([](size_t i, std::vector<uint8_t> f) {
            if (i == 2) f[f.size() / 2] ^= 0x40;
            return std::vector<std::vector<uint8_t>>{std::move(f)};
        });
        CHECK(dropped);
        CHECK(why.find("tampered") != std::string::npos);
        CHECK(chats == 0);
    }
    SUBCASE("a message repeated") {
        auto [dropped, why, chats] = attack([](size_t i, std::vector<uint8_t> f) {
            if (i == 2) return std::vector<std::vector<uint8_t>>{f, f};
            return std::vector<std::vector<uint8_t>>{std::move(f)};
        });
        CHECK(dropped);
        CHECK(why.find("tampered") != std::string::npos);
        CHECK(chats == 1);  // the first copy only
    }
    SUBCASE("two messages swapped") {
        std::vector<uint8_t> held;
        auto [dropped, why, chats] = attack([&](size_t i, std::vector<uint8_t> f) {
            if (i == 2) {
                held = std::move(f);
                return std::vector<std::vector<uint8_t>>{};
            }
            if (i == 3) return std::vector<std::vector<uint8_t>>{std::move(f), held};
            return std::vector<std::vector<uint8_t>>{std::move(f)};
        });
        CHECK(dropped);
        CHECK(why.find("tampered") != std::string::npos);
        CHECK(chats == 0);
    }
    // The host goes on.
    net::ClientSession fine(clientConfig(host, "fine"));
    Loop loop(host, {&fine});
    REQUIRE(fine.connect().has_value());
    REQUIRE(loop.until([&] { return fine.phase() == net::ClientPhase::Lobby; }));
}

TEST_CASE("net security: the host's key is pinned, and an impostor without the join password gets nothing") {
    net::HostConfig cfg = hostConfig(2);
    cfg.joinPasswordHash = net::hashPassword("letmein");
    net::HostSession real(engineRules(), cfg);
    REQUIRE(real.start().has_value());

    // First contact: the client trusts the key it sees, and keeps it.
    net::ClientConfig first = clientConfig(real, "alice", "a-secret");
    first.joinPasswordHash = net::hashPassword("letmein");
    net::ClientSession alice(first);
    Loop loop(real, {&alice});
    REQUIRE(alice.connect().has_value());
    REQUIRE(loop.until([&] { return alice.phase() == net::ClientPhase::Lobby; }));
    REQUIRE(alice.config().hostKey.has_value());
    CHECK(*alice.config().hostKey == real.hostKey());
    CHECK(alice.seenHostKey() == real.hostKey());
    CHECK(real.hostFingerprint() == crypto::fingerprint(real.hostKey()));

    // A wrong join password: the handshake itself fails.
    net::ClientConfig wrong = clientConfig(real, "bob", "b-secret");
    wrong.joinPasswordHash = net::hashPassword("guess");
    net::ClientSession guesser(wrong);
    Loop loop2(real, {&guesser});
    REQUIRE(guesser.connect().has_value());
    REQUIRE(loop2.until([&] { return loop2.clientSaw(0, EventType::Rejected) != nullptr; }));
    CHECK(loop2.clientSaw(0, EventType::Rejected)->text == "Wrong game password.");

    // Impostors, at another address the player was sent to.
    net::HostConfig openCfg = hostConfig(2);
    net::HostSession open(engineRules(), openCfg);       // asks for no join password
    REQUIRE(open.start().has_value());
    net::HostConfig guessCfg = hostConfig(2);
    guessCfg.joinPasswordHash = net::hashPassword("a guess");
    net::HostSession guessing(engineRules(), guessCfg);  // does not know the real one
    REQUIRE(guessing.start().has_value());
    auto tryJoin = [&](net::HostSession& at, std::optional<crypto::Key> pin) {
        net::ClientConfig c = first;
        c.port = at.port();
        c.hostKey = pin;
        net::ClientSession victim(c);
        Loop l(at, {&victim});
        REQUIRE(victim.connect().has_value());
        REQUIRE(l.until([&] { return victim.phase() == net::ClientPhase::Disconnected || victim.phase() == net::ClientPhase::Lobby; }));
        CHECK(victim.phase() == net::ClientPhase::Disconnected);
        CHECK_FALSE(l.hostSaw(EventType::PlayerJoined));
        const net::Event* e = l.clientSaw(0, EventType::Rejected);
        REQUIRE(e != nullptr);
        return std::pair{e->text, victim.hostKeyChanged()};
    };
    // Claiming no join password does not get around it...
    CHECK(tryJoin(open, std::nullopt).first.find("does not ask for a game password") != std::string::npos);
    // ... and without the right one the login cannot be read: the impostor learns no name or proof.
    CHECK(tryJoin(guessing, std::nullopt).first == "Wrong game password.");
    // With the real host's key pinned, any other key is refused before anything is sent.
    auto [text, changed] = tryJoin(guessing, real.hostKey());
    CHECK(changed);
    CHECK(text.find("not the one this computer trusts") != std::string::npos);
    CHECK(text.find(real.hostFingerprint()) != std::string::npos);

    // The real host still takes the pinned client back.
    alice.disconnect();
    REQUIRE(loop.until([&] { return !real.lobby().slots[0].connected; }));
    REQUIRE(alice.connect().has_value());
    REQUIRE(loop.until([&] { return alice.phase() == net::ClientPhase::Lobby; }));
}

TEST_CASE("net security: a wrong master password gives no admin rights, and old verifiers are replaced at login") {
    // A game saved by OpenSE4 0.6: its verifiers are of the old kind.
    game::GameState s = newEngineGame(13, 2, 10, true);
    s.empire(game::EmpireId{0u}).passwordHash = net::legacyPasswordVerifier(net::hashPassword("a-secret"));
    s.empire(game::EmpireId{1u}).passwordHash = net::legacyPasswordVerifier(net::hashPassword("b-secret"));
    game::SaveInfo info;
    info.gameName = "Old Game";
    info.dataSet = game::dataSetIdentity(engineRules());
    info.players = {"alice", "bob"};
    net::HostConfig cfg = hostConfig(2);
    cfg.masterPasswordHash = net::hashPassword("boss");
    net::HostSession host(engineRules(), cfg);
    REQUIRE(host.resume(s, info).has_value());

    net::ClientSession wrongPw(clientConfig(host, "alice", "nope"));
    net::ClientConfig bobCfg = clientConfig(host, "bob", "b-secret");
    bobCfg.masterPasswordHash = net::hashPassword("not the boss");
    net::ClientSession bob(bobCfg);
    Loop loop(host, {&wrongPw, &bob});
    REQUIRE(wrongPw.connect().has_value());
    REQUIRE(bob.connect().has_value());
    REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::Rejected) && bob.state() != nullptr; }));
    CHECK(loop.clientSaw(0, EventType::Rejected)->text.find("Wrong password") != std::string::npos);
    CHECK_FALSE(bob.admin());
    // Bob's verifier is now of the current kind, and still his password's.
    const std::string& upgraded = host.state()->empire(game::EmpireId{1u}).passwordHash;
    CHECK(upgraded.starts_with("pk1:"));
    CHECK(net::checkPassword(upgraded, net::hashPassword("b-secret")));
    CHECK(net::isLegacyVerifier(host.state()->empire(game::EmpireId{0u}).passwordHash));

    net::ClientConfig aliceCfg = clientConfig(host, "alice", "a-secret");
    aliceCfg.masterPasswordHash = net::hashPassword("boss");
    net::ClientSession alice(aliceCfg);
    Loop loop2(host, {&alice});
    REQUIRE(alice.connect().has_value());
    REQUIRE(loop2.until([&] { return alice.state() != nullptr; }));
    CHECK(alice.admin());
    CHECK(host.state()->empire(game::EmpireId{0u}).passwordHash.starts_with("pk1:"));
}

// ---- Versions ------------------------------------------------------------------------------------------------

namespace {

// Protocol 4's greeting (OpenSE4 0.6), field for field.
struct Protocol4Hello {
    uint32_t magic = net::proto::kMagic;
    uint32_t protocol = 4;
    std::string app = "OpenSE4 0.6.1";
    std::string dataSet;
    std::string player = "oldtimer";
    std::string passwordHash;
    std::string joinPasswordHash;
    std::string masterPasswordHash;
};
template <class Ar>
void io(Ar& ar, Protocol4Hello& m) {
    game::serial::fields(ar, m.magic, m.protocol, m.app, m.dataSet, m.player, m.passwordHash, m.joinPasswordHash, m.masterPasswordHash);
}

} // namespace

TEST_CASE("net security: older and newer versions refuse each other with a clear message") {
    // An OpenSE4 0.6 client at a new host: refused in a form it reads.
    net::HostSession host(engineRules(), hostConfig(2));
    REQUIRE(host.start().has_value());
    Loop loop(host, {});
    {
        RawPeer old;
        auto sock = net::connectTcp("127.0.0.1", host.port());
        REQUIRE(sock.has_value());
        old.conn.emplace(std::move(*sock), size_t{1} << 20);
        old.conn->send(net::proto::MsgType::ClientHello, Protocol4Hello{});
        REQUIRE(old.waitFor(loop, net::proto::MsgType::Reject));
        const net::Frame* f = old.find(net::proto::MsgType::Reject);
        CHECK_FALSE(f->sealed);
        net::proto::Reject reject;
        std::string error;
        REQUIRE(net::proto::decode(f->payload, reject, error));
        CHECK(reject.reason == net::proto::RejectReason::Protocol);
        CHECK(reject.text.find("protocol 4") != std::string::npos);
        CHECK(reject.text.find(std::format("network protocol {}", net::kProtocolVersion)) != std::string::npos);
    }

    // A new client at an OpenSE4 0.6 host, which reads its hello as its own
    // greeting and refuses it the way it refuses any other version.
    auto listener = net::listenTcp("127.0.0.1", 0);
    REQUIRE(listener.has_value());
    net::ClientConfig cc = clientConfig(host, "newcomer");
    cc.port = listener->localPort();
    net::ClientSession client(cc);
    REQUIRE(client.connect().has_value());
    std::optional<net::Connection> server;
    std::vector<net::Event> events;
    uint32_t sawProtocol = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && client.phase() != net::ClientPhase::Disconnected) {
        for (auto& e : client.poll(1)) events.push_back(std::move(e));
        if (!server) {
            net::Socket s = net::acceptConnection(*listener);
            if (s.valid()) server.emplace(std::move(s), size_t{1} << 16);
            continue;
        }
        net::PollItem item{server->socket().native(), true, server->wantsWrite()};
        net::pollSockets(std::span(&item, 1), 1);
        server->receive();
        if (auto f = server->nextFrame()) {
            Protocol4Hello hello;
            std::string error;
            REQUIRE(f->type == net::proto::MsgType::ClientHello);
            REQUIRE(net::proto::decode(f->payload, hello, error));  // the whole greeting, as protocol 4 reads it
            sawProtocol = hello.protocol;
            server->send(net::proto::MsgType::Reject,
                         net::proto::Reject{net::proto::RejectReason::Protocol,
                                            std::format("The host runs OpenSE4 0.6.1 (network protocol 4); you have {} (protocol {}). Both need the same version.",
                                                        hello.app, hello.protocol)});
        }
        server->flush();
    }
    CHECK(sawProtocol == net::kProtocolVersion);
    const auto rejected = std::find_if(events.begin(), events.end(), [](const net::Event& e) { return e.type == EventType::Rejected; });
    REQUIRE(rejected != events.end());
    CHECK(rejected->text.find("network protocol 4") != std::string::npos);
    CHECK(rejected->text.find(std::format("protocol {}", net::kProtocolVersion)) != std::string::npos);
}
