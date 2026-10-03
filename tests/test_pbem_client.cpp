// Play by e-mail from the game client (src/client/classic/pbem_play.*,
// ClassicSession with SessionKind::Pbem): the player opens their turn file
// (the game as their empire knows it), plays the turn and writes the signed
// .plr; the host library then processes it on the whole game.

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "client/classic/pbem_play.hpp"
#include "client/classic/session.hpp"
#include "game/redact.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"
#include "net/pbem.hpp"
#include "net/secure.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>

using namespace opense4;
using namespace opense4::client::classic;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<const game::Rules> sharedRules() {
    return std::shared_ptr<const game::Rules>(&test::engineRules(), [](const game::Rules*) {});
}

// The PBEM host's keys: the turn files are signed with one and name the
// other, which the orders files are encrypted to.
const net::secure::PbemHostKeys& hostKeys() {
    static const net::secure::PbemHostKeys keys = [] {
        net::crypto::Key secret{};
        net::crypto::randomBytes(secret);
        return net::secure::hostIdentity(secret).pbem;
    }();
    return keys;
}
const net::crypto::KeyPair& hostKey() { return hostKeys().box; }

net::pbem::ProcessOptions hostOptions() {
    net::pbem::ProcessOptions o;
    o.host = hostKeys();
    return o;
}

// Whether `bytes` hold `text` anywhere.
bool holds(const std::vector<uint8_t>& bytes, std::string_view text) {
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end(), [](uint8_t a, char b) { return a == static_cast<uint8_t>(b); }) !=
           bytes.end();
}

// Two human empires (passwords pw0 and pw1) and a computer empire, saved as a
// PBEM game file, with the turn files of the players who play now beside it.
fs::path writeGameFile(const fs::path& dir, bool simultaneous, bool startTurn, uint64_t gameId) {
    const game::Rules& r = test::engineRules();
    game::GameSetup setup;
    setup.seed = 11;
    setup.options.systemCount = 8;
    setup.options.simultaneous = simultaneous;
    for (int i = 0; i < 3; ++i) {
        game::EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = i < 2 ? game::PlayerKind::Human : game::PlayerKind::Computer;
        e.passwordHash = net::passwordVerifier(std::format("pw{}", i), gameId);
        setup.empires.push_back(e);
    }
    auto state = game::createGame(r, setup);
    REQUIRE(state.has_value());
    if (startTurn && !simultaneous) game::resumeTurnBased(r, *state);
    game::SaveInfo info;
    info.gameName = "Post Game";
    info.gameId = gameId;
    info.dataSet = game::dataSetIdentity(r);
    info.players = {"ann", "ben", ""};
    const fs::path gam = dir / "post.gam";
    REQUIRE(game::saveGame(gam, *state, info).has_value());
    REQUIRE(net::pbem::writeTurnFiles(r, gam, dir, hostKeys()).has_value());
    return gam;
}

fs::path turnFile(const fs::path& dir, game::EmpireId e) { return dir / std::format("Post_Game_{:02}.turn", e.value + 1); }

std::string noteOf(const game::GameState& s, game::EmpireId e) {
    const auto& notes = s.empire(e).knowledge.notes;
    return notes.empty() ? std::string{} : notes[0];
}

// The host's whole game as it reads it for a turn.
game::GameState hostGame(const fs::path& gam) {
    auto loaded = game::loadGame(gam);
    REQUIRE(loaded.has_value());
    net::pbem::readForTurn(test::engineRules(), loaded->first);
    return loaded->first;
}

} // namespace

TEST_CASE("pbem client: a simultaneous turn file is opened with the empire's password and its orders reach the host") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("pbem_client");
    const fs::path gam = writeGameFile(tmp.path(), true, false, 99);
    std::vector<fs::path> listed = listTurnFiles(tmp.path());
    std::sort(listed.begin(), listed.end());
    CHECK(listed == std::vector<fs::path>{turnFile(tmp.path(), game::EmpireId{0u}), turnFile(tmp.path(), game::EmpireId{1u})});

    // The host's own game file is not for players.
    CHECK(loadPbemGame(r, gam).error().find("host's game file") != std::string::npos);

    auto game = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{0u}));
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    CHECK(game->empire == game::EmpireId{0u});
    CHECK(game->state.empires.empty());  // the view opens with the password only
    const auto choices = pbemEmpires(*game);
    REQUIRE(choices.size() == 3);
    CHECK(choices[0].playable);
    CHECK(choices[0].password);
    CHECK(choices[0].player == "ann");
    CHECK(choices[0].yourTurn);
    CHECK_FALSE(choices[1].playable);  // another player's empire: their own turn file
    CHECK_FALSE(choices[2].playable);  // a computer empire
    CHECK_FALSE(pbemActivePlayer(*game).valid());

    // The password is checked as the host checks the .plr; only the file's empire plays.
    CHECK(beginPbemTurn(*game, game::EmpireId{0u}, "pw1").error().find("Wrong password") != std::string::npos);
    CHECK(beginPbemTurn(*game, game::EmpireId{1u}, "pw1").error().find("Ask the host for your own") != std::string::npos);
    CHECK_FALSE(beginPbemTurn(*game, game::EmpireId{7u}, "").has_value());
    CHECK(game->state.empires.empty());
    auto turn = beginPbemTurn(*game, game::EmpireId{0u}, "pw0");
    REQUIRE_MESSAGE(turn.has_value(), (turn ? std::string{} : turn.error()));
    // The empire's own view, exactly as the host makes it of its game.
    CHECK(game::stateChecksum(game->state) == game::stateChecksum(game::redactForEmpire(r, hostGame(gam), game::EmpireId{0u})));
    for (const game::Empire& e : game->state.empires) CHECK(e.passwordHash.empty());
    CHECK(game->state.seed == 0);
    CHECK(turn->ordersDir == tmp.path());  // next to the turn file (inferred)
    CHECK_FALSE(turn->turnBased);
    CHECK(turn->turn == 0);
    CHECK(turn->startChecksum == game->viewChecksum);
    CHECK(turn->legacyPasswordHash.empty());

    auto session = ClassicSession::pbem(sharedRules(), std::move(*game), *turn);
    CHECK(session->kind() == SessionKind::Pbem);
    CHECK(session->player() == game::EmpireId{0u});
    CHECK_FALSE(session->waitingForOthers());
    REQUIRE(session->issue(game::cmd::SetSystemNote{game::SystemId{0u}, "by mail"}).ok);
    CHECK_FALSE(session->issue(game::cmd::SetSystemNote{game::SystemId{999u}, "nowhere"}).ok);
    CHECK(noteOf(session->state(), game::EmpireId{0u}) == "by mail");  // shown at once
    // A new password is stored as the verifier the host checks .plr files against.
    REQUIRE(session->issue(game::cmd::SetEmpireOptions{.passwordHash = session->empirePasswordValue("new0").value()}).ok);
    CHECK(session->state().empire(game::EmpireId{0u}).passwordHash == net::passwordVerifier("new0", 99));
    session->simulateTurns(2);                                            // only the host plays PBEM turns
    CHECK(session->state().turn == 0);

    // End Turn writes the signed .plr next to the turn file and ends the turn here.
    session->endTurn();
    CHECK(session->pbemError().empty());
    CHECK(session->ordersFile() == tmp.path() / "Post_Game_01.plr");
    CHECK(fs::exists(session->ordersFile()));
    CHECK(session->waitingForOthers());
    CHECK_FALSE(session->issue(game::cmd::SetSystemNote{game::SystemId{0u}, "late"}).ok);
    auto plr = net::pbem::readOrdersFile(session->ordersFile(), hostKey());
    REQUIRE(plr.has_value());
    CHECK(plr->orders.commands.size() == 2);  // only the accepted commands
    // Signed with the password this turn was opened with; nothing of the password is in the file.
    CHECK(plr->verifier == net::passwordVerifier("pw0", 99));
    CHECK(net::checkPasswordSignature(plr->verifier, net::pbem::ordersDigest(*plr), plr->signature));
    CHECK(plr->legacyPasswordHash.empty());
    auto raw = game::readFileBytes(session->ordersFile());
    REQUIRE(raw.has_value());
    CHECK_FALSE(holds(*raw, "pw0"));
    CHECK_FALSE(holds(*raw, net::legacyPasswordHash("pw0")));
    CHECK_FALSE(holds(*raw, "by mail"));  // encrypted to the host: another player reads nothing of it
    CHECK_FALSE(net::pbem::readOrdersFile(session->ordersFile(), net::crypto::newKeyPair()).has_value());

    // The host processes it; the other human is played by the computer.
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), hostOptions());
    REQUIRE_MESSAGE(rep.has_value(), (rep ? std::string{} : rep.error()));
    CHECK(rep->submitted == std::vector<std::string>{"Empire 1"});
    CHECK(rep->playedByComputer == std::vector<std::string>{"Empire 2"});
    CHECK(rep->warnings.empty());
    CHECK(rep->turnFiles.size() == 2);
    auto after = game::loadGame(gam);
    REQUIRE(after.has_value());
    CHECK(after->first.turn == 1);
    CHECK(noteOf(after->first, game::EmpireId{0u}) == "by mail");

    // The next turn files: the new password counts; orders can go to another folder.
    auto next = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{0u}));
    REQUIRE(next.has_value());
    CHECK(next->info.turn == 1);
    CHECK_FALSE(beginPbemTurn(*next, game::EmpireId{0u}, "pw0").has_value());  // the password changed
    CHECK(beginPbemTurn(*next, game::EmpireId{0u}, "new0").has_value());
    auto other = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{1u}));
    REQUIRE(other.has_value());
    auto elsewhere = beginPbemTurn(*other, game::EmpireId{1u}, "pw1", tmp / "outbox");
    REQUIRE(elsewhere.has_value());
    CHECK(noteOf(other->state, game::EmpireId{0u}).empty());  // Ann's notes are hers
    CHECK(elsewhere->turn == 1);
    // A draft of another turn is not used.
    auto stale = beginPbemTurn(*other, game::EmpireId{1u}, "pw1");
    REQUIRE(stale.has_value());
    stale->turn = 0;
    REQUIRE(writePbemDraft(*stale, tmp / "drafts", std::vector<game::Command>{game::cmd::SetSystemNote{game::SystemId{0u}, "old"}}).has_value());
    CHECK_FALSE(readPbemDraft(*elsewhere, tmp / "drafts").has_value());
    auto second = ClassicSession::pbem(sharedRules(), std::move(*other), *elsewhere, tmp / "drafts");
    CHECK(second->pbemResumed() == 0);
    second->endTurn();
    CHECK(second->ordersFile() == tmp / "outbox" / "Post_Game_02.plr");
    CHECK(fs::exists(second->ordersFile()));
}

TEST_CASE("pbem client: a turn-based turn file is played command by command, and the host carries the commands out on the whole game") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("pbem_client_tb");
    // Saved between player turns: the host plays on to the first human for the turn file.
    const fs::path gam = writeGameFile(tmp.path(), false, false, 123);
    CHECK(fs::exists(turnFile(tmp.path(), game::EmpireId{0u})));
    CHECK_FALSE(fs::exists(turnFile(tmp.path(), game::EmpireId{1u})));  // only the player whose turn it is
    auto game = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{0u}));
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    CHECK(game->turnBased);
    CHECK(pbemActivePlayer(*game) == game::EmpireId{0u});
    const auto choices = pbemEmpires(*game);
    CHECK(choices[0].yourTurn);
    CHECK_FALSE(choices[1].yourTurn);
    CHECK(beginPbemTurn(*game, game::EmpireId{1u}, "pw1").error().find("Ask the host for your own") != std::string::npos);
    auto turn = beginPbemTurn(*game, game::EmpireId{0u}, "pw0");
    REQUIRE(turn.has_value());
    REQUIRE(game->state.playerTurn.started);
    CHECK(turn->turnBased);
    CHECK(turn->startChecksum == game::stateChecksum(game->state));

    const fs::path drafts = tmp / "drafts";
    auto first = ClassicSession::pbem(sharedRules(), std::move(*game), *turn, drafts);
    CHECK(first->pbemResumed() == 0);
    CHECK(first->myTurn());
    CHECK_FALSE(first->waitingForOthers());
    // Commands are carried out at once on the copy; a refused one is kept for the host too.
    REQUIRE(first->issue(game::cmd::SetSystemNote{game::SystemId{0u}, "turn by mail"}).ok);
    CHECK_FALSE(first->issue(game::cmd::SetSystemNote{game::SystemId{999u}, "nowhere"}).ok);
    game::Order explore;
    explore.kind = game::OrderKind::Explore;
    std::vector<game::VehicleId> idle;
    for (const game::Vehicle& v : first->state().vehicles)
        if (v.owner == game::EmpireId{0u} && v.orders.empty() && !v.fleet.valid() && v.movement > 0) idle.push_back(v.id);
    for (game::VehicleId id : idle) {
        game::cmd::SetOrders o;
        o.vehicle = id;
        o.orders = {explore};
        first->issue(o);
        while (!first->questions().empty()) first->answer(true);
    }
    CHECK(first->ordersThisTurn().size() >= 2);

    // Saved mid-turn and opened again: the commands are given again, and the copy is the same.
    auto draft = first->savePbemDraft();
    REQUIRE_MESSAGE(draft.has_value(), (draft ? std::string{} : draft.error()));
    CHECK(draft->parent_path() == drafts);
    CHECK(net::pbem::readDraft(*draft)->verifier.empty());  // unsigned, kept on this machine
    auto again = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{0u}));
    REQUIRE(again.has_value());
    auto sameTurn = beginPbemTurn(*again, game::EmpireId{0u}, "pw0");
    REQUIRE(sameTurn.has_value());
    auto session = ClassicSession::pbem(sharedRules(), std::move(*again), *sameTurn, drafts);
    CHECK(session->pbemResumed() == first->ordersThisTurn().size());
    CHECK(session->ordersThisTurn().size() == first->ordersThisTurn().size());
    CHECK(game::stateChecksum(session->state()) == game::stateChecksum(first->state()));
    CHECK(session->takeStrategicBattles().empty());  // battles of the replayed commands were seen before

    session->endTurn();
    REQUIRE(fs::exists(session->ordersFile()));
    CHECK_FALSE(fs::exists(*draft));  // the draft is gone once the orders are saved
    CHECK_FALSE(session->savePbemDraft().has_value());
    CHECK(session->waitingForOthers());
    CHECK(session->state().playerTurn.empire == game::EmpireId{0u});  // the host ends the turn, not the player's copy
    auto plr = net::pbem::readOrdersFile(session->ordersFile(), hostKey());
    REQUIRE(plr.has_value());
    CHECK(plr->orders.commands.size() == session->ordersThisTurn().size());
    CHECK(plr->startChecksum == turn->startChecksum);

    // The host carries the commands out on its whole game, then ends the turn.
    game::GameState expected = hostGame(gam);
    game::resumeTurnBased(r, expected);
    for (const game::Command& c : plr->orders.commands) game::applyLive(r, expected, game::EmpireId{0u}, c);
    game::endPlayerTurn(r, expected, game::EmpireId{0u});
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), hostOptions());
    REQUIRE_MESSAGE(rep.has_value(), (rep ? std::string{} : rep.error()));
    CHECK(rep->submitted == std::vector<std::string>{"Empire 1"});
    CHECK(rep->warnings.empty());
    CHECK(rep->next == "Empire 2");
    REQUIRE(rep->turnFiles.size() == 1);
    CHECK(rep->turnFiles[0].first == game::EmpireId{1u});
    auto after = game::loadGame(gam);
    REQUIRE(after.has_value());
    CHECK(game::stateChecksum(after->first) == game::stateChecksum(expected));
    CHECK(noteOf(after->first, game::EmpireId{0u}) == "turn by mail");

    // Now it is Empire 2's turn, in its own turn file, which shows nothing of Empire 1's notes.
    auto next = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{1u}));
    REQUIRE(next.has_value());
    CHECK(pbemActivePlayer(*next) == game::EmpireId{1u});
    CHECK(beginPbemTurn(*next, game::EmpireId{1u}, "pw1").has_value());
    CHECK(noteOf(next->state, game::EmpireId{0u}).empty());
}

TEST_CASE("pbem client: turn files that cannot be played here are refused") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("pbem_client_bad");
    CHECK_FALSE(loadPbemGame(r, tmp / "missing.turn").has_value());
    writeGameFile(tmp.path(), true, false, 5);
    const fs::path file = turnFile(tmp.path(), game::EmpireId{0u});
    auto bytes = game::readFileBytes(file);
    REQUIRE(bytes.has_value());
    auto original = net::pbem::decodeTurnFile(*bytes);
    REQUIRE(original.has_value());
    auto rewrite = [&](const net::pbem::TurnFile& f) { REQUIRE(game::writeFileAtomic(file, net::pbem::encodeTurnFile(f)).has_value()); };
    // Another data set.
    net::pbem::TurnFile f = *original;
    f.info.dataSet = "Other#0123456789abcdef";
    rewrite(f);
    CHECK(loadPbemGame(r, file).error().find("data set") != std::string::npos);
    // An encrypted view changed on the way does not open.
    f = *original;
    REQUIRE(f.encrypted);
    f.content[f.content.size() / 2] ^= 1;
    rewrite(f);
    auto changed = loadPbemGame(r, file);
    REQUIRE(changed.has_value());
    CHECK(beginPbemTurn(*changed, game::EmpireId{0u}, "pw0").error().find("changed") != std::string::npos);
    // A view in the clear (an empire without a password) is checked against its checksum.
    auto plain = [&](game::GameState view, uint64_t checksum) {
        net::pbem::TurnFile clear = *original;
        clear.verifier.clear();
        clear.encrypted = false;
        clear.content = net::pbem::encodeTurnView(net::pbem::TurnView{checksum, game::serializeState(view)});
        net::pbem::signTurnFile(clear, hostKeys().signing);  // as the host made it
        rewrite(clear);
    };
    const game::GameState host = game::redactForEmpire(r, hostGame(tmp / "post.gam"), game::EmpireId{0u});
    plain(host, game::stateChecksum(host) ^ 1);
    auto damaged = loadPbemGame(r, file);
    REQUIRE(damaged.has_value());
    CHECK(beginPbemTurn(*damaged, game::EmpireId{0u}, "").error().find("damaged") != std::string::npos);
    // A finished game.
    game::GameState over = host;
    over.gameOver = true;
    plain(over, game::stateChecksum(over));
    auto finished = loadPbemGame(r, file);
    REQUIRE(finished.has_value());
    CHECK(beginPbemTurn(*finished, game::EmpireId{0u}, "").error() == "The game is over.");
}

TEST_CASE("pbem client: a turn file counts only as its game's host made it") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("pbem_client_host_key");
    const PbemTrust trust{tmp / "known_hosts.txt"};
    writeGameFile(tmp.path(), true, false, 0x7a11);
    const fs::path file = turnFile(tmp.path(), game::EmpireId{0u});
    auto bytes = game::readFileBytes(file);
    REQUIRE(bytes.has_value());
    const net::pbem::TurnFile original = net::pbem::decodeTurnFile(*bytes).value();
    CHECK(original.encrypted);
    CHECK(original.hostKey == hostKey().publicKey);
    CHECK(original.hostSigningKey == hostKeys().signing.publicKey);
    CHECK(net::pbem::turnFileSigned(original));
    auto rewrite = [&](const net::pbem::TurnFile& f) { REQUIRE(game::writeFileAtomic(file, net::pbem::encodeTurnFile(f)).has_value()); };
    auto beginWith = [&](const PbemTrust& t) {
        auto g = loadPbemGame(r, file);
        REQUIRE(g.has_value());
        return beginPbemTurn(*g, game::EmpireId{0u}, "pw0", {}, {}, t);
    };
    auto begin = [&] { return beginWith(trust); };
    const game::GameState view = game::redactForEmpire(r, hostGame(tmp / "post.gam"), game::EmpireId{0u});
    const auto clearView = net::pbem::encodeTurnView(net::pbem::TurnView{game::stateChecksum(view), game::serializeState(view)});
    // Someone else's keys: a turn file made in full, as a host would, by another.
    net::crypto::Key otherSecret{};
    net::crypto::randomBytes(otherSecret);
    const net::secure::PbemHostKeys other = net::secure::hostIdentity(otherSecret).pbem;

    // The first turn file of a game: its host key is trusted from then on.
    {
        auto g = loadPbemGame(r, file);
        REQUIRE(g.has_value());
        CHECK(pbemHostKey(*g, trust.knownHosts).status == net::pbem::HostKeyStatus::First);
        CHECK(pbemHostKey(*g, trust.knownHosts).fingerprint == net::crypto::fingerprint(hostKeys().signing.publicKey));
    }
    REQUIRE(begin().has_value());
    CHECK(net::secure::KnownHosts(trust.knownHosts).findGame(0x7a11) == hostKeys().signing.publicKey);
    {
        auto g = loadPbemGame(r, file);
        REQUIRE(g.has_value());
        CHECK(pbemHostKey(*g, trust.knownHosts).status == net::pbem::HostKeyStatus::Known);
    }

    // Changed on its way, not signed again: refused.
    net::pbem::TurnFile f = original;
    f.turnBased = !f.turnBased;
    rewrite(f);
    CHECK(begin().error().find("changed") != std::string::npos);

    // The view in the clear although the empire's password has a box key
    // (the verifier kept): refused, unsigned, signed by someone else, and even
    // signed by the host's key.
    f = original;
    f.encrypted = false;
    f.content = clearView;
    f.ephemeral = {};
    f.mac = {};
    rewrite(f);
    CHECK(begin().error().find("signature") != std::string::npos);
    net::pbem::signTurnFile(f, other.signing);
    rewrite(f);
    CHECK(begin().error().find("trusts") != std::string::npos);
    net::pbem::signTurnFile(f, hostKeys().signing);
    rewrite(f);
    CHECK(begin().error().find("in the clear") != std::string::npos);

    // A whole turn file made by someone else with keys of their own, encrypted
    // to the empire's key and signed: another key than the game's host's.
    f = original;
    f.hostKey = other.box.publicKey;
    f.content = clearView;
    net::crypto::sealFromSender(other.box, net::verifierKeys(f.verifier)->box, "OpenSE4 turn file v2", {}, f.content, f.ephemeral, f.mac);
    net::pbem::signTurnFile(f, other.signing);
    rewrite(f);
    {
        auto g = loadPbemGame(r, file);
        REQUIRE(g.has_value());
        const auto check = pbemHostKey(*g, trust.knownHosts);
        CHECK(check.status == net::pbem::HostKeyStatus::Changed);
        CHECK(check.trusted == net::crypto::fingerprint(hostKeys().signing.publicKey));
        CHECK(check.fingerprint == net::crypto::fingerprint(other.signing.publicKey));
    }
    const auto refused = begin();
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().find(net::crypto::fingerprint(other.signing.publicKey)) != std::string::npos);
    CHECK(refused.error().find(net::crypto::fingerprint(hostKeys().signing.publicKey)) != std::string::npos);
    // Nothing of it was trusted.
    CHECK(net::secure::KnownHosts(trust.knownHosts).findGame(0x7a11) == hostKeys().signing.publicKey);

    // The view is bound to the host's box key the file names (in the key
    // agreement and the additional data): a view sealed with another box key
    // does not open, even in a file the host's key signed.
    f.hostKey = hostKey().publicKey;
    net::pbem::signTurnFile(f, hostKeys().signing);
    rewrite(f);
    CHECK(begin().error().find("does not open") != std::string::npos);

    // A host that made a new key: the player trusts it (after comparing the
    // fingerprint with the host's), and the game goes on with it.
    REQUIRE(net::pbem::writeTurnFiles(r, tmp / "post.gam", tmp.path(), other).has_value());
    CHECK(begin().error().find("trusts") != std::string::npos);
    PbemTrust trusting = trust;
    trusting.trustChangedHostKey = true;
    auto turn = beginWith(trusting);
    REQUIRE_MESSAGE(turn.has_value(), (turn ? std::string{} : turn.error()));
    CHECK(turn->hostKey == other.box.publicKey);
    CHECK(net::secure::KnownHosts(trust.knownHosts).findGame(0x7a11) == other.signing.publicKey);
    CHECK(begin().has_value());  // trusted from now on
}

TEST_CASE("pbem client: Load Game opens a PBEM game file as a local game whose passwords still work") {
    test::TempDir dir("pbem_load_game");
    const fs::path gam = writeGameFile(dir.path(), true, false, 0x1234u);
    auto loaded = ClassicSession::load(sharedRules(), gam);
    REQUIRE(loaded.has_value());
    ClassicSession& s = **loaded;
    CHECK(s.kind() == SessionKind::Hotseat);
    const game::Empire& first = s.state().empire(game::EmpireId{0u});
    // The empire passwords are the host's verifiers, not local hashes.
    CHECK(s.passwordMatches(first, "pw0").value());
    CHECK_FALSE(s.passwordMatches(first, "pw1").value());
    CHECK(s.empirePasswordValue("new").value() == net::passwordVerifier("new", 0x1234u));
    {
        // A key that cannot be made (Argon2id's memory not to be had) is said so, not taken for a wrong password.
        const net::PasswordWork work = net::passwordWork();
        net::setPasswordWork({4, 1});
        const auto value = s.empirePasswordValue("newer");
        const auto matches = s.passwordMatches(s.state().empire(game::EmpireId{1u}), "pw1");
        net::setPasswordWork(work);
        REQUIRE_FALSE(value.has_value());
        CHECK(value.error().find("password key") != std::string::npos);
        // pw1's verifier names its own work, which can be made.
        CHECK(matches.value());
    }
    // Saved again, it stays a game with verifier passwords.
    const fs::path again = dir.path() / "again.gam";
    REQUIRE(s.save(again, "Post Game").has_value());
    auto reloaded = ClassicSession::load(sharedRules(), again);
    REQUIRE(reloaded.has_value());
    CHECK((*reloaded)->passwordMatches((*reloaded)->state().empire(game::EmpireId{1u}), "pw1").value());
    // A player's turn file is no saved game.
    CHECK_FALSE(ClassicSession::load(sharedRules(), turnFile(dir.path(), game::EmpireId{0u})).has_value());
}

TEST_CASE("pbem client: the host reads the game for the turn files and for processing, recalculating every colony") {
    // Spec 01 §6.9, §14 Q44 (confirmed: binary): the game file is read the
    // same way for the players' turn files and for the turn, a colony that
    // can no longer cloak decloaking as by Decloak.
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("pbem_client_recalc");
    const fs::path gam = writeGameFile(tmp.path(), true, false, 9);
    auto loaded = game::loadGame(gam);
    REQUIRE(loaded.has_value());
    game::ObjectId planet;
    for (auto& c : loaded->first.colonies)
        if (c && c->owner == game::EmpireId{0u}) {
            c->cloaked = true;  // marked cloaked (after a battle, say) with no cloaking facility
            planet = c->planet;
            break;
        }
    REQUIRE(planet.valid());
    REQUIRE(game::saveGame(gam, loaded->first, loaded->second).has_value());
    REQUIRE(net::pbem::writeTurnFiles(r, gam, tmp.path(), hostKeys()).has_value());
    auto game = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{0u}));
    REQUIRE(game.has_value());
    auto turn = beginPbemTurn(*game, game::EmpireId{0u}, "pw0");
    REQUIRE(turn.has_value());
    CHECK_FALSE(game->state.colony(planet)->cloaked);
    REQUIRE(writePbemOrders(*turn, {}).has_value());
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), hostOptions());
    REQUIRE(rep.has_value());
    CHECK(rep->submitted == std::vector<std::string>{"Empire 1"});  // made from the very view the host makes
    auto after = game::loadGame(gam);
    REQUIRE(after.has_value());
    CHECK_FALSE(after->first.colony(planet)->cloaked);
}

TEST_CASE("pbem client: an empire of an OpenSE4 0.6 game moves to a new password with its first turn") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("pbem_client_legacy");
    const fs::path gam = writeGameFile(tmp.path(), true, false, 31);
    auto loaded = game::loadGame(gam);
    REQUIRE(loaded.has_value());
    loaded->first.empire(game::EmpireId{0u}).passwordHash = net::legacyPasswordVerifier(net::legacyPasswordHash("pw0"));
    REQUIRE(game::saveGame(gam, loaded->first, loaded->second).has_value());
    REQUIRE(net::pbem::writeTurnFiles(r, gam, tmp.path(), hostKeys()).has_value());
    auto game = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{0u}));
    REQUIRE(game.has_value());
    CHECK(pbemNeedsNewPassword(*game));
    CHECK(beginPbemTurn(*game, game::EmpireId{0u}, "pw0").error().find("new password") != std::string::npos);
    CHECK(beginPbemTurn(*game, game::EmpireId{0u}, "pw0", {}, "pw0").error().find("other than the old") != std::string::npos);
    CHECK(beginPbemTurn(*game, game::EmpireId{0u}, "wrong", {}, "fresh").error().find("Wrong password") != std::string::npos);
    // The old password's form goes to the host only once the player compared
    // the host's key and agreed: the refusal names that key.
    const auto unconfirmed = beginPbemTurn(*game, game::EmpireId{0u}, "pw0", {}, "fresh");
    REQUIRE_FALSE(unconfirmed.has_value());
    CHECK(unconfirmed.error().find(net::crypto::fingerprint(hostKeys().signing.publicKey)) != std::string::npos);
    CHECK(unconfirmed.error().find("reset your password") != std::string::npos);
    PbemTrust agreed;
    agreed.showOldPassword = true;
    auto turn = beginPbemTurn(*game, game::EmpireId{0u}, "pw0", {}, "fresh", agreed);
    REQUIRE_MESSAGE(turn.has_value(), (turn ? std::string{} : turn.error()));
    CHECK(turn->legacyPasswordHash == net::legacyPasswordHash("pw0"));
    REQUIRE(turn->keys.has_value());
    CHECK(turn->keys->verifier() == net::passwordVerifier("fresh", 31));  // from the new password, nothing of the old
    auto file = writePbemOrders(*turn, {});
    REQUIRE(file.has_value());
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), hostOptions());
    REQUIRE_MESSAGE(rep.has_value(), (rep ? std::string{} : rep.error()));
    CHECK(rep->submitted == std::vector<std::string>{"Empire 1"});
    auto next = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{0u}));
    REQUIRE(next.has_value());
    CHECK_FALSE(pbemNeedsNewPassword(*next));
    CHECK_FALSE(beginPbemTurn(*next, game::EmpireId{0u}, "pw0").has_value());
    CHECK(beginPbemTurn(*next, game::EmpireId{0u}, "fresh").has_value());
}
