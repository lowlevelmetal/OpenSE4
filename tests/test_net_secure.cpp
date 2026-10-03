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
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
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
            if (auto h = net::connectTcp("127.0.0.1", hostPort)) host = std::move(*h);
            REQUIRE(host.valid());
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

    // Sealed to a recipient: sealTo from anyone, sealFromSender only from the
    // holder of the sender's secret (the recipient names the sender).
    {
        const crypto::KeyPair host = crypto::newKeyPair(), stranger = crypto::newKeyPair();
        std::vector<uint8_t> boxed(message.begin(), message.end());
        crypto::Key ephemeral{};
        crypto::Mac boxMac{};
        crypto::sealFromSender(host, bob.publicKey, "test domain", ad, boxed, ephemeral, boxMac);
        CHECK_FALSE(contains(boxed, "fleet"));
        auto opensFrom = [&](const crypto::KeyPair& to, const crypto::Key& from, std::string_view domain, std::vector<uint8_t> t) {
            return crypto::openFromSender(to, from, ephemeral, domain, ad, t, boxMac);
        };
        CHECK(opensFrom(bob, host.publicKey, "test domain", boxed));
        CHECK_FALSE(opensFrom(bob, stranger.publicKey, "test domain", boxed));  // not from the stranger
        CHECK_FALSE(opensFrom(alice, host.publicKey, "test domain", boxed));    // not for alice
        CHECK_FALSE(opensFrom(bob, host.publicKey, "other domain", boxed));
        // The same message sealed anonymously does not pass for the host's.
        std::vector<uint8_t> anonymous(message.begin(), message.end());
        crypto::Key anonymousEphemeral{};
        crypto::Mac anonymousMac{};
        crypto::sealTo(bob.publicKey, "test domain", ad, anonymous, anonymousEphemeral, anonymousMac);
        CHECK_FALSE(crypto::openFromSender(bob, host.publicKey, anonymousEphemeral, "test domain", ad, anonymous, anonymousMac));
        std::vector<uint8_t> opened = boxed;
        REQUIRE(crypto::openFromSender(bob, host.publicKey, ephemeral, "test domain", ad, opened, boxMac));
        CHECK(std::string(opened.begin(), opened.end()) == message);
    }

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

TEST_CASE("net security: password keys come from the password itself, salted per game, never from 0.6's hash") {
    const uint64_t game = 0x5eed;
    const auto keys = net::passwordKeys("hunter2", game);
    REQUIRE(keys.has_value());
    const std::string verifier = keys->verifier();
    CHECK(verifier == net::passwordVerifier("hunter2", game));
    // "pk2:", the work it was made with, then the signing key and the box key.
    const net::PasswordWork work = net::passwordWork();
    const std::string prefix = std::format("pk2:{}:{}:", work.kibibytes, work.passes);
    CHECK(verifier.starts_with(prefix));
    CHECK(verifier.size() == prefix.size() + 128);
    REQUIRE(net::verifierKeys(verifier).has_value());
    CHECK(net::verifierKeys(verifier)->signing == keys->signing.publicKey);
    CHECK(net::verifierKeys(verifier)->box == keys->box.publicKey);
    CHECK(net::verifierKeys(verifier)->work == work);
    CHECK(keys->work == work);
    CHECK(net::verifierProblem(verifier).empty());
    CHECK(net::usableVerifier(verifier));
    CHECK(net::usableVerifier(""));
    CHECK(net::checkPassword(verifier, "hunter2", game));
    CHECK_FALSE(net::checkPassword(verifier, "hunter3", game));
    CHECK_FALSE(net::checkPassword(verifier, "", game));
    CHECK(net::checkPassword("", "anything", game));
    // Another game: another salt, another verifier (a verifier of one game helps with no other).
    CHECK(net::passwordVerifier("hunter2", game + 1) != verifier);
    CHECK_FALSE(net::checkPassword(verifier, "hunter2", game + 1));
    CHECK_FALSE(net::passwordKeys("", game).has_value());

    // OpenSE4 0.6 sent its hash of the password in the clear. Nothing made
    // from that hash opens or signs anything of the current kind.
    const std::string oldHash = net::legacyPasswordHash("hunter2");
    CHECK(net::passwordVerifier(oldHash, game) != verifier);
    CHECK_FALSE(net::checkPassword(verifier, oldHash, game));

    // A login or an orders file proves the password by signing.
    const auto message = bytesOf("session 42");
    const crypto::Signature sig = net::signWith(keys, message);
    CHECK(net::checkPasswordSignature(verifier, message, sig));
    CHECK_FALSE(net::checkPasswordSignature(verifier, bytesOf("session 43"), sig));
    CHECK_FALSE(net::checkPasswordSignature(verifier, message, net::signWith(net::passwordKeys("guess", game), message)));
    CHECK_FALSE(net::checkPasswordSignature(verifier, message, net::signWith(net::passwordKeys(oldHash, game), message)));
    CHECK(net::checkPasswordSignature("", message, crypto::Signature{}));  // no password set
    CHECK(net::signWith(std::nullopt, message) == crypto::Signature{});

    // Verifiers a player cannot have made: malformed, or with a key of small order.
    crypto::Key identity{};
    identity[0] = 1;  // the neutral point of Edwards25519
    const std::string box = crypto::hex(keys->box.publicKey), signing = crypto::hex(keys->signing.publicKey);
    CHECK_FALSE(net::usableVerifier(prefix + crypto::hex(identity) + box));
    CHECK(net::verifierProblem(prefix + crypto::hex(identity) + box).find("small order") != std::string::npos);
    CHECK_FALSE(net::usableVerifier(prefix + signing + crypto::hex(crypto::Key{})));
    CHECK_FALSE(net::usableVerifier(prefix + signing));
    CHECK_FALSE(net::usableVerifier(std::string(64, 'a')));  // looks like 0.6's
    CHECK(net::verifierProblem(std::string(64, 'a')).find("OpenSE4 0.6") != std::string::npos);
    CHECK_FALSE(net::verifierKeys(prefix + "zz" + verifier.substr(prefix.size() + 2)).has_value());
    // The form before the work was written in (never released) is refused, with words saying what a verifier is.
    CHECK_FALSE(net::usableVerifier("pk1:" + signing + box));
    CHECK(net::verifierProblem("pk1:" + signing + box).find("pk2:") != std::string::npos);
    // The work: decimal, without leading zeros, within bounds.
    CHECK_FALSE(net::usableVerifier(std::format("pk2:0{}:{}:", work.kibibytes, work.passes) + signing + box));
    CHECK_FALSE(net::usableVerifier(std::format("pk2:{}::", work.kibibytes) + signing + box));
    CHECK_FALSE(net::usableVerifier("pk2:4:1:" + signing + box));                  // less memory than Argon2id takes
    CHECK_FALSE(net::usableVerifier("pk2:2097152:1:" + signing + box));            // 2 GiB: more than a verifier may ask
    CHECK_FALSE(net::usableVerifier("pk2:8192:17:" + signing + box));              // too many passes
    CHECK_FALSE(net::usableVerifier("pk2:8192:0:" + signing + box));
    CHECK_FALSE(net::usableVerifier("pk2:99999999999:1:" + signing + box));        // does not fit
    CHECK(net::verifierProblem("pk2:2097152:1:" + signing + box).find("out of bounds") != std::string::npos);
    CHECK(net::usableVerifier("pk2:8:1:" + signing + box));
    CHECK(net::usableVerifier("pk2:1048576:16:" + signing + box));

    // The verifier of OpenSE4 0.6: still checks the password once, but no signature.
    const std::string legacy = net::legacyPasswordVerifier(oldHash);
    CHECK(net::isLegacyVerifier(legacy));
    CHECK_FALSE(net::isLegacyVerifier(verifier));
    CHECK(net::checkPassword(legacy, "hunter2", game));
    CHECK(net::checkLegacyPassword(legacy, oldHash));
    CHECK_FALSE(net::checkPassword(legacy, "nope", game));
    CHECK_FALSE(net::checkPasswordSignature(legacy, message, sig));

    // Join keys: per host key and game; none without a password.
    const crypto::Key hostA = crypto::newKeyPair().publicKey, hostB = crypto::newKeyPair().publicKey;
    CHECK(net::joinKey("", hostA, game) == crypto::Key{});
    CHECK(net::joinKey("letmein", hostA, game) == net::joinKey("letmein", hostA, game));
    CHECK(net::joinKey("letmein", hostA, game) != net::joinKey("letmein", hostB, game));
    CHECK(net::joinKey("letmein", hostA, game) != net::joinKey("letmein", hostA, game + 1));
}

TEST_CASE("net security: the real Argon2id work") {
    // The tests run with less work (tests/main.cpp); the game's own is 128 MiB, three passes.
    const net::PasswordWork tests = net::passwordWork();
    CHECK(net::PasswordWork{}.kibibytes == 128 * 1024);
    CHECK(net::PasswordWork{}.passes == 3);
    const std::string light = net::passwordVerifier("hunter2", 7);
    net::setPasswordWork(net::PasswordWork{});
    const auto start = std::chrono::steady_clock::now();
    const std::string real = net::passwordVerifier("hunter2", 7);
    const auto took = std::chrono::steady_clock::now() - start;
    net::setPasswordWork(tests);
    MESSAGE("one password key with the game's own work took " << std::chrono::duration_cast<std::chrono::milliseconds>(took).count() << " ms");
    CHECK(real != light);  // the work is part of the key
    CHECK(net::usableVerifier(real));
    CHECK(real.starts_with("pk2:131072:3:"));
    // Each verifier records its work, so it checks with that work whatever
    // the work set now (a later version may raise the default).
    CHECK(net::checkPassword(real, "hunter2", 7));
    CHECK(net::checkPassword(light, "hunter2", 7));
    CHECK_FALSE(net::checkPassword(real, "hunter3", 7));
    CHECK(net::passwordKeysFor(real, "hunter2", 7)->verifier() == real);
    CHECK(net::passwordKeysFor(light, "hunter2", 7)->verifier() == light);
}

TEST_CASE("net security: a host's key is kept in a file, and players remember it per host") {
    const TempDir tmp("hostkey");
    const auto file = tmp / "keys" / "host_key.txt";
    auto first = net::secure::loadOrCreateHostKey(file);
    REQUIRE_MESSAGE(first.has_value(), (first ? std::string{} : first.error()));
    CHECK(std::filesystem::exists(file));
    auto again = net::secure::loadOrCreateHostKey(file);
    REQUIRE(again.has_value());
    CHECK(again->keys.network.publicKey == first->keys.network.publicKey);
    CHECK(again->keys.pbem.box.publicKey == first->keys.pbem.box.publicKey);
    CHECK(again->keys.pbem.signing.publicKey == first->keys.pbem.signing.publicKey);
    CHECK(first->warning.empty());
    // One secret, a key of its own for each use: the network handshake, the
    // PBEM box key and the PBEM signing key are three different keys.
    CHECK(first->keys.network.publicKey != first->keys.pbem.box.publicKey);
    CHECK(first->keys.network.secret != first->keys.pbem.box.secret);
    CHECK(first->keys.network.publicKey != first->keys.pbem.signing.publicKey);
    {
        std::ifstream in(file);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.find(crypto::fingerprint(first->keys.network.publicKey)) != std::string::npos);
        CHECK(text.find(crypto::fingerprint(first->keys.pbem.signing.publicKey)) != std::string::npos);
        // The secret is none of the keys used.
        CHECK(text.find(crypto::hex(first->keys.network.secret)) == std::string::npos);
        CHECK(text.find(crypto::hex(first->keys.pbem.box.secret)) == std::string::npos);
    }
#ifndef _WIN32
    // Readable by its owner only, in a folder only the owner enters.
    namespace fs = std::filesystem;
    CHECK((fs::status(file).permissions() & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
    CHECK((fs::status(file.parent_path()).permissions() & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
    // A key file others can read is used, with a warning for the host.
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read, fs::perm_options::replace);
    auto loose = net::secure::loadOrCreateHostKey(file);
    REQUIRE(loose.has_value());
    CHECK(loose->keys.network.publicKey == first->keys.network.publicKey);
    CHECK(loose->warning.find("other users") != std::string::npos);
    CHECK(loose->warning.find("644") != std::string::npos);
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
    CHECK(net::secure::loadOrCreateHostKey(file)->warning.empty());
#endif
    // Another program's key made meanwhile is never overwritten: it counts.
    const auto other = tmp / "taken.txt";
    {
        std::ofstream made(other);
        made << crypto::hex(crypto::newKeyPair().secret) << "\n";
    }
    const auto before = std::filesystem::last_write_time(other);
    auto read = net::secure::loadOrCreateHostKey(other);
    REQUIRE(read.has_value());
    CHECK(std::filesystem::last_write_time(other) == before);
    {
        std::ofstream broken(tmp / "broken.txt");
        broken << "not a key\n";
    }
    CHECK_FALSE(net::secure::loadOrCreateHostKey(tmp / "broken.txt").has_value());

    net::secure::KnownHosts known(tmp / "known_hosts.txt");
    CHECK_FALSE(known.find("Example.org", 6720).has_value());
    REQUIRE(known.remember("Example.org", 6720, first->keys.network.publicKey).has_value());
    REQUIRE(known.remember("other.example", 6721, crypto::newKeyPair().publicKey).has_value());
    CHECK(known.find("example.ORG", 6720) == first->keys.network.publicKey);  // host names in any case
    CHECK_FALSE(known.find("example.org", 6721).has_value());
    const crypto::Key newer = crypto::newKeyPair().publicKey;
    REQUIRE(known.remember("example.org", 6720, newer).has_value());
    CHECK(known.find("example.org", 6720) == newer);
    CHECK(known.find("other.example", 6721).has_value());
}

// ---- On the wire -------------------------------------------------------------------------------------------

TEST_CASE("net security: an eavesdropper sees neither the game nor the passwords") {
    net::HostConfig cfg = hostConfig(1);
    cfg.masterPassword = "master-pw";
    net::HostSession host(engineRules(), cfg);
    REQUIRE(host.start().has_value());
    REQUIRE(host.addComputerEmpire().has_value());
    Relay relay(host.port());
    net::ClientConfig cc = viaRelay(relay, host, "alice", "a-secret");
    cc.masterPassword = "master-pw";
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
        CHECK_FALSE(contains(*stream, "a-secret"));
        CHECK_FALSE(contains(*stream, "master-pw"));
        CHECK_FALSE(contains(*stream, net::legacyPasswordHash("a-secret")));
        CHECK_FALSE(contains(*stream, crypto::hex(net::verifierKeys(net::passwordVerifier("a-secret", host.gameId()))->signing)));
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
    cfg.joinPassword = "letmein";
    net::HostSession real(engineRules(), cfg);
    REQUIRE(real.start().has_value());

    // First contact: the client trusts the key it sees, and keeps it.
    net::ClientConfig first = clientConfig(real, "alice", "a-secret");
    first.joinPassword = "letmein";
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
    wrong.joinPassword = "guess";
    net::ClientSession guesser(wrong);
    Loop loop2(real, {&guesser});
    REQUIRE(guesser.connect().has_value());
    REQUIRE(loop2.until([&] { return loop2.clientSaw(0, EventType::Rejected) != nullptr; }));
    // Said in the clear, before anything was secured: shown as the host's word only.
    CHECK(loop2.clientSaw(0, EventType::Rejected)->text == "The host said, before the connection was secured: Wrong game password.");

    // Impostors, at another address the player was sent to.
    net::HostConfig openCfg = hostConfig(2);
    net::HostSession open(engineRules(), openCfg);       // asks for no join password
    REQUIRE(open.start().has_value());
    net::HostConfig guessCfg = hostConfig(2);
    guessCfg.joinPassword = "a guess";
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
    CHECK(tryJoin(guessing, std::nullopt).first.ends_with("Wrong game password."));
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

TEST_CASE("net security: a wrong master password gives no admin rights, and old verifiers move only with the player's consent") {
    // A game saved by OpenSE4 0.6: its verifiers are of the old kind.
    game::GameState s = newEngineGame(13, 2, 10, true);
    s.empire(game::EmpireId{0u}).passwordHash = net::legacyPasswordVerifier(net::legacyPasswordHash("a-secret"));
    s.empire(game::EmpireId{1u}).passwordHash = net::legacyPasswordVerifier(net::legacyPasswordHash("b-secret"));
    game::SaveInfo info;
    info.gameName = "Old Game";
    info.gameId = 51;
    info.dataSet = game::dataSetIdentity(engineRules());
    info.players = {"alice", "bob"};
    net::HostConfig cfg = hostConfig(2);
    cfg.masterPassword = "boss";
    cfg.joinPassword = "letmein";
    net::HostSession host(engineRules(), cfg);
    REQUIRE(host.resume(s, info).has_value());
    auto bobConfig = [&] {
        net::ClientConfig c = clientConfig(host, "bob", "b-secret");
        c.joinPassword = "letmein";
        return c;
    };

    // The host asks this player (and only when this player logs in) for the old form.
    net::ClientSession asked(bobConfig());
    Loop first(host, {&asked});
    REQUIRE(asked.connect().has_value());
    REQUIRE(first.until([&] { return first.clientSaw(0, EventType::Rejected) != nullptr; }));
    CHECK(asked.hostAskedOldPassword());
    CHECK(first.clientSaw(0, EventType::Rejected)->text.find("OpenSE4 0.6") != std::string::npos);
    CHECK(net::isLegacyVerifier(host.state()->empire(game::EmpireId{1u}).passwordHash));

    // The player agrees, but has not trusted the host's key beforehand: a host
    // met for the first time could be anyone claiming an old game, and the join
    // password vouches for nothing. Nothing is sent.
    net::ClientConfig unpinned = bobConfig();
    unpinned.sendOldPassword = true;
    net::ClientSession careless(unpinned);
    Loop second(host, {&careless});
    REQUIRE(careless.connect().has_value());
    REQUIRE(second.until([&] { return second.clientSaw(0, EventType::Rejected) != nullptr; }));
    CHECK(careless.hostKeyUnconfirmed());
    CHECK(second.clientSaw(0, EventType::Rejected)->text.find(host.hostFingerprint()) != std::string::npos);
    CHECK(net::isLegacyVerifier(host.state()->empire(game::EmpireId{1u}).passwordHash));

    // A trusted key and the player's consent: the password moves to the new
    // kind, made from the password itself (not from the old hash).
    net::ClientConfig wrongCfg = clientConfig(host, "alice", "nope");
    wrongCfg.joinPassword = "letmein";
    wrongCfg.hostKey = host.hostKey();
    wrongCfg.sendOldPassword = true;
    net::ClientSession wrongPw(wrongCfg);
    net::ClientConfig bobCfg = bobConfig();
    bobCfg.masterPassword = "not the boss";
    bobCfg.hostKey = host.hostKey();  // compared with the host's and trusted
    bobCfg.sendOldPassword = true;
    net::ClientSession bob(bobCfg);
    Loop loop(host, {&wrongPw, &bob});
    REQUIRE(wrongPw.connect().has_value());
    REQUIRE(bob.connect().has_value());
    REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::Rejected) && bob.state() != nullptr; }));
    CHECK(loop.clientSaw(0, EventType::Rejected)->text.find("Wrong password") != std::string::npos);
    CHECK_FALSE(bob.admin());
    CHECK_FALSE(bob.config().sendOldPassword);  // agreed once, for that login
    // The host's log says so.
    CHECK(std::any_of(loop.hostEvents.begin(), loop.hostEvents.end(), [](const net::Event& e) {
        return e.type == EventType::Info && e.text.starts_with("Password migration: bob");
    }));
    const std::string& upgraded = host.state()->empire(game::EmpireId{1u}).passwordHash;
    CHECK(upgraded == net::passwordVerifier("b-secret", info.gameId));
    CHECK_FALSE(net::checkPassword(upgraded, net::legacyPasswordHash("b-secret"), info.gameId));
    CHECK(net::isLegacyVerifier(host.state()->empire(game::EmpireId{0u}).passwordHash));

    net::ClientConfig aliceCfg = clientConfig(host, "alice", "a-secret");
    aliceCfg.joinPassword = "letmein";
    aliceCfg.masterPassword = "boss";
    aliceCfg.hostKey = host.hostKey();
    aliceCfg.sendOldPassword = true;
    net::ClientSession alice(aliceCfg);
    Loop loop2(host, {&alice});
    REQUIRE(alice.connect().has_value());
    REQUIRE(loop2.until([&] { return alice.state() != nullptr; }));
    CHECK(alice.admin());
    CHECK(host.state()->empire(game::EmpireId{0u}).passwordHash == net::passwordVerifier("a-secret", info.gameId));
}

TEST_CASE("net security: a host may refuse to move passwords of OpenSE4 0.6") {
    game::GameSetup setup;
    setup.seed = 5;
    setup.options.systemCount = 8;
    for (int i = 0; i < 2; ++i) {
        game::EmpireSetup e;
        e.kind = game::PlayerKind::Human;
        setup.empires.push_back(e);
    }
    auto s = game::createGame(engineRules(), setup);
    REQUIRE(s.has_value());
    s->empire(game::EmpireId{0u}).passwordHash = net::legacyPasswordVerifier(net::legacyPasswordHash("a-secret"));
    game::SaveInfo info;
    info.gameName = "Old Game";
    info.gameId = 52;
    info.dataSet = game::dataSetIdentity(engineRules());
    info.players = {"alice", "bob"};
    net::HostConfig cfg = hostConfig(2);
    cfg.passwordMigration = false;
    net::HostSession host(engineRules(), cfg);
    REQUIRE(host.resume(*s, info).has_value());
    net::ClientConfig aliceCfg = clientConfig(host, "alice", "a-secret");
    aliceCfg.hostKey = host.hostKey();  // trusted, and the player agreed: still refused
    aliceCfg.sendOldPassword = true;
    net::ClientSession alice(aliceCfg);
    Loop loop(host, {&alice});
    REQUIRE(alice.connect().has_value());
    REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::Rejected) != nullptr; }));
    CHECK(loop.clientSaw(0, EventType::Rejected)->text.find("reset your password") != std::string::npos);
    CHECK(net::isLegacyVerifier(host.state()->empire(game::EmpireId{0u}).passwordHash));
    CHECK_FALSE(std::any_of(loop.hostEvents.begin(), loop.hostEvents.end(), [](const net::Event& e) { return e.text.starts_with("Password migration"); }));
}

TEST_CASE("net security: a player can set only a password value of the current kind") {
    TwoPlayerGame g;
    net::HostSession& host = g.host;
    Loop& loop = *g.loop;
    const std::string before = host.state()->empire(g.alice.empire()).passwordHash;
    // A value that looks like OpenSE4 0.6's verifier would make the host ask
    // for the old form of the password, and an unreadable one lock the empire out.
    for (const std::string& bad : {std::string(64, 'a'), std::string("pk2:nonsense")}) {
        loop.clear();
        game::EmpireOrders orders = noteOrders(g.alice, "x");
        orders.commands.push_back(game::cmd::SetEmpireOptions{.passwordHash = bad});
        REQUIRE(g.alice.submitOrders(orders).has_value());
        REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::OrdersRejected) != nullptr; }));
        CHECK(loop.clientSaw(0, EventType::OrdersRejected)->text.find("password") != std::string::npos);
    }
    CHECK_FALSE(host.submitOrders(game::EmpireOrders{g.bob.empire(), 0, {game::cmd::SetEmpireOptions{.passwordHash = std::string(64, 'b')}}})
                    .has_value());
    // A verifier of the current kind is fine.
    game::EmpireOrders good = noteOrders(g.alice, "y");
    good.commands.push_back(game::cmd::SetEmpireOptions{.passwordHash = net::passwordVerifier("fresh", host.gameId())});
    REQUIRE(g.alice.submitOrders(good).has_value());
    REQUIRE(g.bob.submitOrders(noteOrders(g.bob, "z")).has_value());
    REQUIRE(loop.until([&] { return host.state()->turn == 1; }));
    CHECK(host.state()->empire(g.alice.empire()).passwordHash != before);
    CHECK(net::checkPassword(host.state()->empire(g.alice.empire()).passwordHash, "fresh", host.gameId()));
}

TEST_CASE("net security: the client reads only small frames before the host has shown its keys") {
    // A "host" that answers the greeting with a frame claiming 400 MiB.
    auto listener = net::listenTcp("127.0.0.1", 0);
    REQUIRE(listener.has_value());
    net::ClientConfig cc;
    cc.port = listener->localPort();
    cc.playerName = "careful";
    cc.dataSet = game::dataSetIdentity(engineRules());
    net::ClientSession client(cc);
    REQUIRE(client.connect().has_value());
    net::Socket server;
    std::vector<net::Event> events;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool sent = false;
    while (std::chrono::steady_clock::now() < deadline && client.phase() != net::ClientPhase::Disconnected) {
        for (auto& e : client.poll(1)) events.push_back(std::move(e));
        if (!server.valid()) server = net::acceptConnection(*listener);
        if (server.valid() && !sent) {
            const uint32_t length = 400u << 20;
            const uint8_t frame[] = {static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length >> 16),
                                     static_cast<uint8_t>(length >> 24), static_cast<uint8_t>(net::proto::MsgType::ServerHello)};
            sent = server.send(frame).status == net::IoStatus::Ok;
        }
    }
    CHECK(client.phase() == net::ClientPhase::Disconnected);
    const auto closed = std::find_if(events.begin(), events.end(), [](const net::Event& e) { return e.type == EventType::Disconnected; });
    REQUIRE(closed != events.end());
    CHECK(closed->text.find("too large") != std::string::npos);
    CHECK(closed->text.find(std::to_string(net::proto::kMaxHandshakeBytes)) != std::string::npos);
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

namespace {

// Sets the password work for one test, and back afterwards whatever happens.
struct WorkFor {
    net::PasswordWork before = net::passwordWork();
    explicit WorkFor(net::PasswordWork w) { net::setPasswordWork(w); }
    ~WorkFor() { net::setPasswordWork(before); }
    WorkFor(const WorkFor&) = delete;
    WorkFor& operator=(const WorkFor&) = delete;
};

} // namespace

TEST_CASE("net security: a password made with another work, or whose key cannot be made, is said so") {
    TwoPlayerGame g;
    const net::PasswordWork tests = net::passwordWork();
    {
        // Another OpenSE4 with another default: the host names both works
        // rather than saying "wrong password".
        WorkFor other({tests.kibibytes * 2, tests.passes});
        net::ClientSession alice(clientConfig(g.host, "alice", "a-secret"));
        Loop loop(g.host, {&alice});
        REQUIRE(alice.connect().has_value());
        REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::Rejected) != nullptr; }));
        const std::string text = loop.clientSaw(0, EventType::Rejected)->text;
        CHECK(text.find("Argon2id work of " + net::describe(tests)) != std::string::npos);
        CHECK(text.find(net::describe(net::passwordWork())) != std::string::npos);
    }
    {
        // A key that cannot be made (here: a work no verifier may name; on a
        // real computer: Argon2id's memory not to be had) ends the attempt
        // with that said, on the client and on a host starting.
        WorkFor none({4, 1});
        CHECK_THROWS_AS(net::passwordKeys("x", 1), net::PasswordWorkError);
        CHECK_THROWS_AS(net::joinKey("x", crypto::Key{}, 1), net::PasswordWorkError);
        net::ClientSession alice(clientConfig(g.host, "alice", "a-secret"));
        Loop loop(g.host, {&alice});
        REQUIRE(alice.connect().has_value());
        REQUIRE(loop.until([&] { return loop.clientSaw(0, EventType::Rejected) != nullptr; }));
        CHECK(loop.clientSaw(0, EventType::Rejected)->text.find("password key") != std::string::npos);
        net::HostConfig cfg = hostConfig(2);
        cfg.localPlayer = net::LocalPlayer{"me", "my-secret", {}};
        net::HostSession host(engineRules(), cfg);
        const auto started = host.start();
        REQUIRE_FALSE(started.has_value());
        CHECK(started.error().find("password key") != std::string::npos);
        CHECK(host.phase() == net::HostPhase::Stopped);
        CHECK(g.host.resetPasswords({g.alice.empire()}).error().find("password key") != std::string::npos);
    }
    // The cache of Argon2id results can be emptied (and is at exit); keys come out the same.
    const std::string before = net::passwordVerifier("hunter2", 99);
    net::forgetPasswordKeys();
    CHECK(net::passwordVerifier("hunter2", 99) == before);
}
