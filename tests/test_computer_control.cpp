// Player Computer Control (docs/spec/06 §1.2.1, spec 05 §7.1, confirmed:
// binary): the window's switch, the TCP/IP host's mark-only toggle, the
// stored difficulty, and local games ending when no human is left.

#include "engine_fixture.hpp"
#include "temp_dir.hpp"

#include "client/classic/finale.hpp"
#include "client/classic/session.hpp"
#include "game/ai.hpp"
#include "game/commands.hpp"
#include "game/query.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"
#include "net/pbem.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <memory>
#include <variant>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
namespace fs = std::filesystem;

namespace {

const EmpireId kMe{0u};
const EmpireId kOther{1u};

GameState newGame(bool simultaneous, bool allHuman) {
    GameState s = newEngineGame(7, 2, 12, allHuman);
    s.options.simultaneous = simultaneous;
    return s;
}

std::shared_ptr<const Rules> sharedRules() { return std::shared_ptr<const Rules>(&engineRules(), [](const Rules*) {}); }

bool flagsAre(const GameState& s, EmpireId e, bool on) {
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && v.minister != on) return false;
    for (const Fleet& f : s.fleets)
        if (f.owner == e && f.minister != on) return false;
    for (const auto& c : s.colonies)
        if (c && c->owner == e && c->minister != on) return false;
    return true;
}

} // namespace

TEST_CASE("computer control: the window's switch sets the mark, every minister and every flag, and nothing else") {
    const Rules& r = engineRules();
    GameState s = newGame(true, true);
    Empire& me = s.empire(kMe);
    me.ministerStyle = "Aggressive";
    me.passwordHash = "secret";
    me.aiMinimalChanges = true;
    me.ministersForNewVehicles = false;
    me.ministers = kIndividualMinisters;
    // A ship in a fleet too, so every kind of individual flag is there.
    const DesignId design = addTestDesign(s, r, kMe, "Courier", "Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
    const VehicleId ship = addTestVehicle(s, r, design, locationOf(s.galaxy, homeworld(s, kMe).planet)).id;
    REQUIRE(apply(r, s, kMe, cmd::CreateFleet{"F", {ship}}).ok);
    REQUIRE(me.aiDifficulty < 0);

    REQUIRE(ai::setComputerControl(s, kMe, true));
    CHECK(s.empire(kMe).kind == PlayerKind::Computer);
    CHECK(s.empire(kMe).ministers == kAllMinisters);
    CHECK(s.empire(kMe).ministerAll);
    CHECK(flagsAre(s, kMe, true));
    // Kept: style, password, the two options; a human's stored difficulty is Medium.
    CHECK(s.empire(kMe).ministerStyle == "Aggressive");
    CHECK(s.empire(kMe).passwordHash == "secret");
    CHECK(s.empire(kMe).aiMinimalChanges);
    CHECK_FALSE(s.empire(kMe).ministersForNewVehicles);
    CHECK(s.empire(kMe).aiDifficulty == kDifficultyMedium);
    CHECK(flagsAre(s, kOther, false));  // only that empire

    // Back to human: the mark and every switch off; the earlier settings do not come back.
    s.empire(kMe).aiDifficulty = kDifficultyHigh;
    REQUIRE(ai::setComputerControl(s, kMe, false));
    CHECK(s.empire(kMe).kind == PlayerKind::Human);
    CHECK(s.empire(kMe).ministers == 0);
    CHECK_FALSE(s.empire(kMe).ministerAll);
    CHECK(flagsAre(s, kMe, false));
    CHECK(s.empire(kMe).aiDifficulty == kDifficultyHigh);  // stored difficulty untouched

    // The TCP/IP host's toggle: only the mark.
    s.empire(kOther).ministers = 5;
    REQUIRE(ai::setComputerMark(s, kOther, true));
    CHECK(s.empire(kOther).kind == PlayerKind::Computer);
    CHECK(s.empire(kOther).ministers == 5);
    CHECK_FALSE(s.empire(kOther).ministerAll);
    // Neutral empires keep their kind (OpenSE4 keeps neutrality in the mark's field).
    s.empire(kOther).kind = PlayerKind::Neutral;
    CHECK_FALSE(ai::setComputerControl(s, kOther, false));
    CHECK(s.empire(kOther).kind == PlayerKind::Neutral);
    CHECK_FALSE(ai::setComputerControl(s, EmpireId{99u}, true));
}

TEST_CASE("computer control: a human empire whose ministers act stores Medium difficulty") {
    // Spec 05 §7.5, spec 06 §1.2.1 (confirmed: binary): a random computer
    // player handed to a human, whose ministers act, plays at Medium once
    // handed back.
    const Rules& r = engineRules();
    GameState s = newGame(true, true);
    s.empire(kMe).aiDifficulty = kDifficultyHigh;
    TurnContext ctx{r, s, {}, {}, {}};
    s.empire(kMe).ministers = 0;
    ai::recordAiDecisions(ctx);
    CHECK(s.empire(kMe).aiDifficulty == kDifficultyHigh);  // no minister at work
    s.empire(kMe).ministerAll = true;
    ai::recordAiDecisions(ctx);
    CHECK(s.empire(kMe).aiDifficulty == kDifficultyMedium);
    REQUIRE(ai::setComputerControl(s, kMe, true));
    CHECK(ai::difficultyOf(s, kMe) == kDifficultyMedium);
}

TEST_CASE("computer control: a turn-based local game plays no further turn when no human is left") {
    const Rules& r = engineRules();
    for (const bool stop : {true, false}) {
        CAPTURE(stop);
        GameState s = newGame(false, false);  // one human, one computer
        LiveOptions options;
        options.endWithoutHumans = stop;
        resumeTurnBased(r, s, options);
        REQUIRE(s.playerTurn.empire == kMe);
        REQUIRE(s.playerTurn.started);
        // Switched in its own turn: still played by hand until End Turn,
        // whose processing runs every minister.
        REQUIRE(ai::setComputerControl(s, kMe, true));
        CHECK_FALSE(ai::anyHumanLeft(s));
        const uint32_t turn = s.turn;
        endPlayerTurn(r, s, kMe, options);
        if (stop) {
            CHECK(s.turn == turn);  // nothing further was played
            CHECK_FALSE(s.playerTurn.started);
        } else {
            CHECK(s.turn == turn + 1);  // an all-computer game plays a game turn per call
        }
    }
}

TEST_CASE("computer control: the local session switches empires and ends with the last human") {
    for (const bool simultaneous : {true, false}) {
        CAPTURE(simultaneous);
        GameState s = newGame(simultaneous, true);  // two humans: hotseat
        client::classic::ClassicSession session(sharedRules(), std::move(s), kMe, client::classic::SessionKind::Hotseat);
        CHECK_FALSE(session.hasMasterPassword());
        // One human left; still a hotseat game, so the humans left are asked in turn.
        session.setComputerControl({{kOther, true}});
        CHECK(session.kind() == client::classic::SessionKind::Hotseat);
        CHECK(session.state().empire(kOther).kind == PlayerKind::Computer);
        CHECK_FALSE(session.humansGone());
        // A row clicked twice (back to its start) still applies: the ministers go off.
        session.setComputerControl({{kMe, false}});
        CHECK(session.state().empire(kMe).ministers == 0);
        // The last human hands over: the game ends at End Turn.
        session.setComputerControl({{kMe, true}});
        CHECK(session.humansGone());
        const uint32_t turn = session.state().turn;
        session.endTurn();
        CHECK(session.state().turn == turn);
        CHECK(client::classic::finaleKind(session.state(), kMe, session.kind()) == client::classic::FinaleKind::HumanDead);
        // Handed back, the game goes on.
        session.setComputerControl({{kMe, false}});
        CHECK_FALSE(session.humansGone());
    }
    // A local game that gets a second human becomes hotseat.
    GameState s = newGame(true, false);
    client::classic::ClassicSession local(sharedRules(), std::move(s), kMe, client::classic::SessionKind::Local);
    local.setComputerControl({{kOther, false}});
    CHECK(local.kind() == client::classic::SessionKind::Hotseat);
}

TEST_CASE("computer control: on a player's copy only the copy changes, and the orders carry its own ministers") {
    GameState s = newGame(true, true);
    client::classic::ClassicSession session(sharedRules(), std::move(s), kMe, client::classic::SessionKind::Pbem);
    session.setComputerControl({{kOther, true}});
    CHECK(session.state().empire(kOther).kind == PlayerKind::Computer);  // the local copy
    CHECK(session.ordersThisTurn().empty());                               // nothing about other empires
    session.setComputerControl({{kMe, true}});
    CHECK(session.state().empire(kMe).kind == PlayerKind::Computer);
    // The orders carry the minister switches and flags, never the mark.
    const auto& orders = session.ordersThisTurn();
    REQUIRE(orders.size() == 2);
    const auto* m = std::get_if<cmd::SetMinisters>(&orders[0]);
    REQUIRE(m);
    CHECK(m->areas == kAllMinisters);
    CHECK(m->individual == true);
    const auto* all = std::get_if<cmd::SetMinister>(&orders[1]);
    REQUIRE(all);
    CHECK(all->empireWide);
    CHECK(all->on);
    // The host, reading them, switches every minister on but keeps the empire human.
    GameState host = newGame(true, true);
    for (const Command& c : orders) REQUIRE(apply(engineRules(), host, kMe, c).ok);
    CHECK(host.empire(kMe).kind == PlayerKind::Human);
    CHECK(host.empire(kMe).ministerAll);
    CHECK(host.empire(kMe).ministers == kAllMinisters);
}

TEST_CASE("computer control: a game file with a master password asks for it, exactly") {
    const Rules& r = engineRules();
    const test::TempDir tmp("computer_control_master");
    GameState s = newGame(true, true);
    SaveInfo info;
    info.gameName = "Guarded";
    info.gameId = 77;
    info.dataSet = dataSetIdentity(r);
    info.masterPasswordVerifier = net::passwordVerifier("Master Key", info.gameId);
    const fs::path file = tmp / "guarded.gam";
    REQUIRE(saveGame(file, s, info).has_value());
    auto session = client::classic::ClassicSession::load(sharedRules(), file);
    REQUIRE(session.has_value());
    CHECK((*session)->hasMasterPassword());
    CHECK((*session)->masterPasswordMatches("Master Key").value());
    CHECK_FALSE((*session)->masterPasswordMatches("master key").value());  // letter case counts
    CHECK_FALSE((*session)->masterPasswordMatches(" Master Key").value());  // so do spaces
}

TEST_CASE("reset passwords: the e-mail host writes them in after reading the orders") {
    const Rules& r = engineRules();
    const test::TempDir tmp("pbem_reset_passwords");
    GameState s = newGame(true, true);
    SaveInfo info;
    info.gameName = "Mail";
    info.gameId = 78;
    info.dataSet = dataSetIdentity(r);
    const fs::path gam = tmp / "mail.gam";
    REQUIRE(saveGame(gam, s, info).has_value());
    const fs::path inbox = tmp / "inbox";
    fs::create_directories(inbox);
    net::pbem::ProcessOptions o;
    o.hostKey = net::crypto::newKeyPair();
    o.resetPasswords = {kOther};
    auto rep = net::pbem::processGameFile(r, gam, inbox, o);
    REQUIRE(rep.has_value());
    REQUIRE(rep->passwordResets.size() == 1);
    CHECK(rep->passwordResets.front().first == kOther);
    CHECK(rep->passwordResets.front().second.size() == 12);  // six numbers from 11 to 99 (the original: three)
    auto after = loadGame(gam);
    REQUIRE(after.has_value());
    CHECK(net::checkPassword(after->first.empire(kOther).passwordHash, rep->passwordResets.front().second, info.gameId));
    // Turn-based games have none.
    after->first.options.simultaneous = false;
    REQUIRE(saveGame(gam, after->first, after->second).has_value());
    CHECK_FALSE(net::pbem::processGameFile(r, gam, inbox, o).has_value());
}
