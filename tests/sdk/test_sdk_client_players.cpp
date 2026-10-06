// Script computer players as the client shows them (client/classic/computer_players.hpp):
// their notes for the AI notes view, resolved to what they name and where it is, and their
// failures for the main window's notice.

#include "players_fixture.hpp"

#include "client/classic/computer_players.hpp"
#include "client/classic/session.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::sdktest;
namespace classic = opense4::client::classic;

TEST_CASE("client players: the notes a player wrote, with what each names and where it is") {
    InstalledPlayers installed({}, true);   // Captain is written with the opense4 package
    const Rules& r = test::engineRules();
    GameState s = playersGame(5, true, {Controller{}, scriptPlayer("Captain")});
    processTurn(r, s, {});
    const EmpireId captain{1u};
    REQUIRE_FALSE(s.empire(captain).aiNotes.empty());
    const std::vector<classic::ShownNote> notes = classic::computerPlayerNotes(s);
    REQUIRE(notes.size() == s.empire(captain).aiNotes.size());
    // Its first colony: a stellar object, by its planet.
    const auto colony = std::find_if(notes.begin(), notes.end(), [](const classic::ShownNote& n) { return n.kind == "object"; });
    REQUIRE(colony != notes.end());
    CHECK(colony->empire == captain);
    CHECK(colony->author == s.empire(captain).name + " (Captain)");
    CHECK(colony->text == "colony, turn 0");
    CHECK(colony->turn == 0);
    const SpaceObject& planet = s.galaxy.object(ObjectId{static_cast<uint32_t>(colony->object)});
    CHECK(colony->subject == planet.name);
    CHECK(colony->system == planet.system);
    REQUIRE(colony->sector);
    CHECK(*colony->sector == planet.sector);
    CHECK(classic::noteLine(*colony) == planet.name + ": colony, turn 0");
    CHECK(classic::notesAbout(notes, "object", colony->object).size() == 1);
    CHECK(classic::notesAbout(notes, "vehicle", colony->object).empty());
    // Its first ship, where it is.
    for (const classic::ShownNote& n : notes)
        if (n.kind == "vehicle") {
            const Vehicle* v = s.vehicle(VehicleId{static_cast<uint32_t>(n.object)});
            REQUIRE(v);
            CHECK(n.subject == v->name);
            CHECK(n.system == v->location.system);
        }

    // Notes about the other kinds of things, about nothing, and about things gone.
    Empire& e = s.empire(captain);
    e.aiNotes = {{0, "system", 0, "a system"},  {0, "empire", 0, "an empire"},   {0, "design", 0, "a design"},
                 {0, "message", 7, "a message"}, {0, "object", -1, "nothing"}, {0, "vehicle", 999999, "a ship gone"}};
    const std::vector<classic::ShownNote> others = classic::computerPlayerNotes(s);
    REQUIRE(others.size() == 6);
    CHECK(others[0].subject == s.galaxy.systems[0].name);
    CHECK(others[0].system == SystemId{0u});
    CHECK_FALSE(others[0].sector);
    CHECK(others[1].subject == s.empires[0].name);
    CHECK_FALSE(others[1].system.valid());
    if (!s.designs.empty()) CHECK(others[2].subject == s.designs[0].name);
    CHECK(others[3].subject == "message 7");
    CHECK(others[4].subject.empty());
    CHECK(classic::noteLine(others[4]) == "nothing");
    CHECK(others[5].subject == "vehicle 999999");
    CHECK_FALSE(others[5].system.valid());
}

TEST_CASE("client players: the notes live in the whole game this computer holds") {
    const Rules& r = test::engineRules();
    const std::shared_ptr<const Rules> rules(&r, [](const Rules*) {});
    GameState s = playersGame(5, true, {Controller{}, Controller{}});
    s.empire(EmpireId{0u}).kind = PlayerKind::Human;
    const classic::ClassicSession local(rules, s, EmpireId{0u}, classic::SessionKind::Local);
    CHECK(classic::wholeGame(local) == &local.state());
    // A network player's copy holds no player's notes (their host keeps them).
    const classic::ClassicSession joined(rules, s, EmpireId{0u}, classic::SessionKind::NetworkClient);
    CHECK(classic::wholeGame(joined) == nullptr);
}

TEST_CASE("client players: failures are kept for the game being played, and the notice says who failed in what") {
    classic::forgetPlayerFailures();
    CHECK(classic::playerFailures().empty());
    sdk::PlayerFailure f;
    f.empire = EmpireId{2u};
    f.empireName = "Xiati";
    f.player = "test.ai-fixture:Faulty";
    f.turn = 3;
    f.call = "orders";
    f.error = "ValueError: no ships.";
    f.traceback = "Traceback (most recent call last):\n  File \"player.py\", line 1, in orders\nValueError: no ships.";
    classic::notePlayerFailure(f);
    CHECK(classic::playerFailureCount() == 1);
    CHECK(classic::failureNotice(f) == "test.ai-fixture:Faulty, playing Xiati, failed in its orders: ValueError: no ships. The classic AI answered.");
    f.call = "battle_round";
    f.outForTurn = true;
    classic::notePlayerFailure(f);
    CHECK(classic::failureNotice(f) ==
          "test.ai-fixture:Faulty, playing Xiati, failed in a combat turn: ValueError: no ships. The classic AI plays it for the rest of the turn.");
    const std::vector<sdk::PlayerFailure> kept = classic::playerFailures();
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].call == "orders");
    CHECK(kept[1].traceback == f.traceback);
    // The game ended: its failures go with it.
    classic::forgetPlayerFailures();
    CHECK(classic::playerFailureCount() == 0);
}
