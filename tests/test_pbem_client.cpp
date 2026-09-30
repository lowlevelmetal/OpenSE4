// Play by e-mail from the game client (src/client/classic/pbem_play.*,
// ClassicSession with SessionKind::Pbem): the player opens the game file,
// plays the turn and writes the .plr; the host library then processes it.

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "client/classic/pbem_play.hpp"
#include "client/classic/session.hpp"
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

// Two human empires (passwords pw0 and pw1) and a computer empire, saved as a PBEM game file.
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
    return gam;
}

std::string noteOf(const game::GameState& s, game::EmpireId e) {
    const auto& notes = s.empire(e).knowledge.notes;
    return notes.empty() ? std::string{} : notes[0];
}

bool warned(const net::pbem::ProcessReport& rep, std::string_view what) {
    return std::any_of(rep.warnings.begin(), rep.warnings.end(), [&](const std::string& w) { return w.find(what) != std::string::npos; });
}

} // namespace

TEST_CASE("pbem client: a simultaneous game file is opened with the empire's password and its orders reach the host") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("pbem_client");
    const fs::path gam = writeGameFile(tmp.path(), true, false, 99);
    CHECK(listGameFiles(tmp.path()) == std::vector<fs::path>{gam});

    auto game = loadPbemGame(r, gam);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    const auto choices = pbemEmpires(*game);
    REQUIRE(choices.size() == 3);
    CHECK(choices[0].playable);
    CHECK(choices[0].password);
    CHECK(choices[0].player == "ann");
    CHECK(choices[0].yourTurn);
    CHECK(choices[1].playable);
    CHECK_FALSE(choices[2].playable);  // a computer empire
    CHECK_FALSE(pbemActivePlayer(*game).valid());

    // The password is checked as the host checks the .plr; computer empires cannot be played.
    CHECK(beginPbemTurn(*game, game::EmpireId{0u}, "pw1").error().find("Wrong password") != std::string::npos);
    CHECK(beginPbemTurn(*game, game::EmpireId{2u}, "pw2").error().find("computer") != std::string::npos);
    CHECK_FALSE(beginPbemTurn(*game, game::EmpireId{7u}, "").has_value());
    auto turn = beginPbemTurn(*game, game::EmpireId{0u}, "pw0");
    REQUIRE_MESSAGE(turn.has_value(), (turn ? std::string{} : turn.error()));
    CHECK(turn->ordersDir == tmp.path());  // next to the game file (inferred)
    CHECK_FALSE(turn->turnBased);
    CHECK(turn->turn == 0);

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

    // End Turn writes the .plr next to the game file and ends the turn here.
    session->endTurn();
    CHECK(session->pbemError().empty());
    CHECK(session->ordersFile() == tmp.path() / "Post_Game_01.plr");
    CHECK(fs::exists(session->ordersFile()));
    CHECK(session->waitingForOthers());
    CHECK_FALSE(session->issue(game::cmd::SetSystemNote{game::SystemId{0u}, "late"}).ok);
    auto plr = net::pbem::readOrdersFile(session->ordersFile());
    REQUIRE(plr.has_value());
    CHECK(plr->orders.commands.size() == 2);  // only the accepted commands
    CHECK(plr->passwordHash == net::hashPassword("pw0"));  // the password this turn was opened with

    // The host processes it; the other human is played by the computer.
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), {});
    REQUIRE_MESSAGE(rep.has_value(), (rep ? std::string{} : rep.error()));
    CHECK(rep->submitted == std::vector<std::string>{"Empire 1"});
    CHECK(rep->playedByComputer == std::vector<std::string>{"Empire 2"});
    CHECK(rep->warnings.empty());
    auto after = game::loadGame(gam);
    REQUIRE(after.has_value());
    CHECK(after->first.turn == 1);
    CHECK(noteOf(after->first, game::EmpireId{0u}) == "by mail");

    // The orders can go to another folder, which is created.
    auto next = loadPbemGame(r, gam);
    REQUIRE(next.has_value());
    CHECK_FALSE(beginPbemTurn(*next, game::EmpireId{0u}, "pw0").has_value());  // the password changed
    CHECK(beginPbemTurn(*next, game::EmpireId{0u}, "new0").has_value());
    auto elsewhere = beginPbemTurn(*next, game::EmpireId{1u}, "pw1", tmp / "outbox");
    REQUIRE(elsewhere.has_value());
    CHECK(elsewhere->turn == 1);
    // A draft of another turn is not used.
    auto stale = beginPbemTurn(*next, game::EmpireId{1u}, "pw1");
    REQUIRE(stale.has_value());
    stale->turn = 0;
    REQUIRE(writePbemDraft(*stale, tmp / "drafts", next->state, std::vector<game::Command>{game::cmd::SetSystemNote{game::SystemId{0u}, "old"}})
                .has_value());
    CHECK_FALSE(readPbemDraft(*elsewhere, tmp / "drafts").has_value());
    auto second = ClassicSession::pbem(sharedRules(), std::move(*next), *elsewhere, tmp / "drafts");
    CHECK(second->pbemResumed() == 0);
    second->endTurn();
    CHECK(second->ordersFile() == tmp / "outbox" / "Post_Game_02.plr");
    CHECK(fs::exists(second->ordersFile()));
}

TEST_CASE("pbem client: a turn-based game file is played command by command and the host's replay matches") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("pbem_client_tb");
    // Saved between player turns: opening it plays on to the first human, as the host does.
    const fs::path gam = writeGameFile(tmp.path(), false, false, 123);
    auto game = loadPbemGame(r, gam);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    REQUIRE(game->state.playerTurn.started);
    CHECK(pbemActivePlayer(*game) == game::EmpireId{0u});
    const auto choices = pbemEmpires(*game);
    CHECK(choices[0].yourTurn);
    CHECK_FALSE(choices[1].yourTurn);
    CHECK(beginPbemTurn(*game, game::EmpireId{1u}, "pw1").error().find("It is Empire 1's turn") != std::string::npos);
    auto turn = beginPbemTurn(*game, game::EmpireId{0u}, "pw0");
    REQUIRE(turn.has_value());
    CHECK(turn->turnBased);
    CHECK(turn->startChecksum == game::stateChecksum(game->state));

    const fs::path drafts = tmp / "drafts";
    auto first = ClassicSession::pbem(sharedRules(), std::move(*game), *turn, drafts);
    CHECK(first->pbemResumed() == 0);
    CHECK(first->myTurn());
    CHECK_FALSE(first->waitingForOthers());
    // Commands are carried out at once; a refused one is kept for the host's replay too.
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

    // Saved mid-turn and opened again: the commands are given again, and the game is the same.
    auto draft = first->savePbemDraft();
    REQUIRE_MESSAGE(draft.has_value(), (draft ? std::string{} : draft.error()));
    CHECK(draft->parent_path() == drafts);
    CHECK(net::pbem::readOrdersFile(*draft)->passwordHash.empty());
    auto again = loadPbemGame(r, gam);
    REQUIRE(again.has_value());
    auto sameTurn = beginPbemTurn(*again, game::EmpireId{0u}, "pw0");
    REQUIRE(sameTurn.has_value());
    auto session = ClassicSession::pbem(sharedRules(), std::move(*again), *sameTurn, drafts);
    CHECK(session->pbemResumed() == first->ordersThisTurn().size());
    CHECK(session->ordersThisTurn().size() == first->ordersThisTurn().size());
    CHECK(game::stateChecksum(session->state()) == game::stateChecksum(first->state()));
    CHECK(session->takeStrategicBattles().empty());  // battles of the replayed commands were seen before
    const game::GameState played = session->state();

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
    CHECK(plr->endChecksum == game::stateChecksum(played));

    game::GameState expected = played;
    game::endPlayerTurn(r, expected, game::EmpireId{0u});
    auto rep = net::pbem::processGameFile(r, gam, tmp.path(), {});
    REQUIRE_MESSAGE(rep.has_value(), (rep ? std::string{} : rep.error()));
    CHECK(rep->submitted == std::vector<std::string>{"Empire 1"});
    CHECK_FALSE(warned(*rep, "differs"));  // the host's replay is the player's game
    CHECK(rep->next == "Empire 2");
    auto after = game::loadGame(gam);
    REQUIRE(after.has_value());
    CHECK(game::stateChecksum(after->first) == game::stateChecksum(expected));
    CHECK(noteOf(after->first, game::EmpireId{0u}) == "turn by mail");

    // Now it is Empire 2's turn in the new file.
    auto next = loadPbemGame(r, gam);
    REQUIRE(next.has_value());
    CHECK(pbemActivePlayer(*next) == game::EmpireId{1u});
    CHECK(beginPbemTurn(*next, game::EmpireId{1u}, "pw1").has_value());
}

TEST_CASE("pbem client: game files that cannot be played here are refused") {
    const game::Rules& r = test::engineRules();
    const test::TempDir tmp("pbem_client_bad");
    CHECK_FALSE(loadPbemGame(r, tmp / "missing.gam").has_value());
    const fs::path gam = writeGameFile(tmp.path(), true, false, 5);
    auto loaded = game::loadGame(gam);
    REQUIRE(loaded.has_value());
    // Another data set.
    loaded->second.dataSet = "Other#0123456789abcdef";
    REQUIRE(game::saveGame(gam, loaded->first, loaded->second).has_value());
    CHECK(loadPbemGame(r, gam).error().find("data set") != std::string::npos);
    // A finished game.
    loaded->second.dataSet = game::dataSetIdentity(r);
    loaded->first.gameOver = true;
    REQUIRE(game::saveGame(gam, loaded->first, loaded->second).has_value());
    CHECK(loadPbemGame(r, gam).error() == "The game is over.");
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
}
