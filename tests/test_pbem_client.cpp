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
        e.passwordHash = net::passwordVerifier(net::hashPassword(std::format("pw{}", i)));
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
    REQUIRE(net::pbem::writeTurnFiles(r, gam, dir).has_value());
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
    // The empire's own view, exactly as the host makes it of its game.
    CHECK(game->empire == game::EmpireId{0u});
    CHECK(game::stateChecksum(game->state) == game::stateChecksum(game::redactForEmpire(r, hostGame(gam), game::EmpireId{0u})));
    for (const game::Empire& e : game->state.empires) CHECK(e.passwordHash.empty());
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
    auto turn = beginPbemTurn(*game, game::EmpireId{0u}, "pw0");
    REQUIRE_MESSAGE(turn.has_value(), (turn ? std::string{} : turn.error()));
    CHECK(turn->ordersDir == tmp.path());  // next to the turn file (inferred)
    CHECK_FALSE(turn->turnBased);
    CHECK(turn->turn == 0);
    CHECK(turn->startChecksum == game->viewChecksum);
    CHECK_FALSE(turn->legacyPassword);

    auto session = ClassicSession::pbem(sharedRules(), std::move(*game), *turn);
    CHECK(session->kind() == SessionKind::Pbem);
    CHECK(session->player() == game::EmpireId{0u});
    CHECK_FALSE(session->waitingForOthers());
    REQUIRE(session->issue(game::cmd::SetSystemNote{game::SystemId{0u}, "by mail"}).ok);
    CHECK_FALSE(session->issue(game::cmd::SetSystemNote{game::SystemId{999u}, "nowhere"}).ok);
    CHECK(noteOf(session->state(), game::EmpireId{0u}) == "by mail");  // shown at once
    // A new password is stored as the verifier the host checks .plr files against.
    REQUIRE(session->issue(game::cmd::SetEmpireOptions{.passwordHash = session->empirePasswordValue("new0")}).ok);
    CHECK(session->state().empire(game::EmpireId{0u}).passwordHash == net::passwordVerifier(net::hashPassword("new0")));
    session->simulateTurns(2);                                            // only the host plays PBEM turns
    CHECK(session->state().turn == 0);

    // End Turn writes the signed .plr next to the turn file and ends the turn here.
    session->endTurn();
    CHECK(session->pbemError().empty());
    CHECK(session->ordersFile() == tmp.path() / "Post_Game_01.plr");
    CHECK(fs::exists(session->ordersFile()));
    CHECK(session->waitingForOthers());
    CHECK_FALSE(session->issue(game::cmd::SetSystemNote{game::SystemId{0u}, "late"}).ok);
    auto plr = net::pbem::readOrdersFile(session->ordersFile());
    REQUIRE(plr.has_value());
    CHECK(plr->orders.commands.size() == 2);  // only the accepted commands
    // Signed with the password this turn was opened with; the hash itself is not in the file.
    CHECK(plr->verifier == net::passwordVerifier(net::hashPassword("pw0")));
    CHECK(net::checkPasswordSignature(plr->verifier, net::pbem::ordersDigest(*plr), plr->signature));
    CHECK(plr->legacyPasswordHash.empty());
    auto raw = game::readFileBytes(session->ordersFile());
    REQUIRE(raw.has_value());
    const std::string hash = net::hashPassword("pw0");
    CHECK(std::search(raw->begin(), raw->end(), hash.begin(), hash.end(), [](uint8_t a, char b) { return a == static_cast<uint8_t>(b); }) ==
          raw->end());

    // The host processes it; the other human is played by the computer.
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), {});
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
    CHECK(next->state.turn == 1);
    CHECK_FALSE(beginPbemTurn(*next, game::EmpireId{0u}, "pw0").has_value());  // the password changed
    CHECK(beginPbemTurn(*next, game::EmpireId{0u}, "new0").has_value());
    auto other = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{1u}));
    REQUIRE(other.has_value());
    CHECK(noteOf(other->state, game::EmpireId{0u}).empty());  // Ann's notes are hers
    auto elsewhere = beginPbemTurn(*other, game::EmpireId{1u}, "pw1", tmp / "outbox");
    REQUIRE(elsewhere.has_value());
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
    REQUIRE(game->state.playerTurn.started);
    CHECK(pbemActivePlayer(*game) == game::EmpireId{0u});
    const auto choices = pbemEmpires(*game);
    CHECK(choices[0].yourTurn);
    CHECK_FALSE(choices[1].yourTurn);
    CHECK(beginPbemTurn(*game, game::EmpireId{1u}, "pw1").error().find("Ask the host for your own") != std::string::npos);
    auto turn = beginPbemTurn(*game, game::EmpireId{0u}, "pw0");
    REQUIRE(turn.has_value());
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
    CHECK(net::pbem::readOrdersFile(*draft)->verifier.empty());  // unsigned
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
    auto plr = net::pbem::readOrdersFile(session->ordersFile());
    REQUIRE(plr.has_value());
    CHECK(plr->orders.commands.size() == session->ordersThisTurn().size());
    CHECK(plr->startChecksum == turn->startChecksum);

    // The host carries the commands out on its whole game, then ends the turn.
    game::GameState expected = hostGame(gam);
    game::resumeTurnBased(r, expected);
    for (const game::Command& c : plr->orders.commands) game::applyLive(r, expected, game::EmpireId{0u}, c);
    game::endPlayerTurn(r, expected, game::EmpireId{0u});
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), {});
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
    CHECK(noteOf(next->state, game::EmpireId{0u}).empty());
    CHECK(beginPbemTurn(*next, game::EmpireId{1u}, "pw1").has_value());
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
    // A view that does not match its checksum (changed on the way).
    f = *original;
    f.viewChecksum ^= 1;
    rewrite(f);
    CHECK(loadPbemGame(r, file).error().find("damaged") != std::string::npos);
    // A finished game.
    f = *original;
    auto view = game::deserializeState(f.view);
    REQUIRE(view.has_value());
    view->gameOver = true;
    f.view = game::serializeState(*view);
    f.viewChecksum = game::stateChecksum(*view);
    rewrite(f);
    CHECK(loadPbemGame(r, file).error() == "The game is over.");
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
    CHECK(s.passwordMatches(first, "pw0"));
    CHECK_FALSE(s.passwordMatches(first, "pw1"));
    CHECK(s.empirePasswordValue("new") == net::passwordVerifier(net::hashPassword("new")));
    // Saved again, it stays a game with verifier passwords.
    const fs::path again = dir.path() / "again.gam";
    REQUIRE(s.save(again, "Post Game").has_value());
    auto reloaded = ClassicSession::load(sharedRules(), again);
    REQUIRE(reloaded.has_value());
    CHECK((*reloaded)->passwordMatches((*reloaded)->state().empire(game::EmpireId{1u}), "pw1"));
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
    REQUIRE(net::pbem::writeTurnFiles(r, gam, tmp.path()).has_value());
    auto game = loadPbemGame(r, turnFile(tmp.path(), game::EmpireId{0u}));
    REQUIRE(game.has_value());
    CHECK_FALSE(game->state.colony(planet)->cloaked);
    auto turn = beginPbemTurn(*game, game::EmpireId{0u}, "pw0");
    REQUIRE(turn.has_value());
    REQUIRE(writePbemOrders(*turn, {}).has_value());
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), {});
    REQUIRE(rep.has_value());
    CHECK(rep->submitted == std::vector<std::string>{"Empire 1"});  // made from the very view the host makes
    auto after = game::loadGame(gam);
    REQUIRE(after.has_value());
    CHECK_FALSE(after->first.colony(planet)->cloaked);
}
