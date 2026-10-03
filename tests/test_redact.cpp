// Fog of war for network games (game/redact.hpp).

#include "engine_fixture.hpp"

#include "game/commands.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <format>
#include <string>

using namespace opense4;
using namespace opense4::game;

TEST_CASE("redact: an empire's view hides what it does not know") {
    const Rules& r = test::engineRules();
    GameState s = test::newEngineGame(13, 3, 12);
    for (int t = 0; t < 5; ++t) processTurn(r, s, {});
    const EmpireId me{0u}, other{1u};
    s.empire(other).research.push_back({ruleset::TechAreaId{0u}, 10});
    s.empire(other).stockpile = {123, 456, 789};
    // Design statistics belong to their owner (spec 04 §15; inferred): even a design we have seen shows none.
    REQUIRE_FALSE(s.empire(other).designs.empty());
    REQUIRE_FALSE(s.empire(me).designs.empty());
    const DesignId theirs = s.empire(other).designs.front();
    const DesignId ours = s.empire(me).designs.front();
    s.design(theirs).enemyTonnageDestroyed = 750;
    s.design(ours).enemyTonnageDestroyed = 120;
    seeDesign(s.empire(me).knowledge, theirs, s.turn);

    const GameState v = redactForEmpire(r, s, me);
    CHECK(v.design(theirs).name == s.design(theirs).name);  // seen: the design itself is known
    CHECK(v.design(theirs).enemyTonnageDestroyed == 0);
    CHECK(v.design(theirs).built == 0);
    CHECK(v.design(ours).enemyTonnageDestroyed == 120);
    CHECK(validateState(v, &r).empty());
    // Our own things are untouched.
    CHECK(v.empire(me).stockpile == s.empire(me).stockpile);
    CHECK(v.empire(me).research.size() == s.empire(me).research.size());
    size_t ownVehicles = 0;
    for (const Vehicle& x : s.vehicles) ownVehicles += x.owner == me;
    size_t viewOwn = 0;
    for (const Vehicle& x : v.vehicles) viewOwn += x.owner == me;
    CHECK(viewOwn == ownVehicles);
    // Other empires' plans and treasury are gone.
    CHECK(v.empire(other).research.empty());
    CHECK(v.empire(other).stockpile.isZero());
    CHECK(v.empire(other).log.empty());
    CHECK(v.empire(other).passwordHash.empty());
    // Where the others started: their home systems and the map's starting points.
    CHECK_FALSE(v.empire(other).homeSystem.valid());
    CHECK(v.empire(me).homeSystem == s.empire(me).homeSystem);
    s.startingPoints.push_back({s.empire(other).homeSystem, Sector{1, 1}, 1});
    CHECK(redactForEmpire(r, s, me).startingPoints.empty());
    // Foreign vehicles only when visible, and without their orders or cargo.
    for (const Vehicle& x : v.vehicles) {
        if (x.owner == me) continue;
        const auto& vis = s.empire(me).knowledge.visibleVehicles;
        CHECK(std::find(vis.begin(), vis.end(), x.id) != vis.end());
        CHECK(x.orders.empty());
        CHECK(x.cargo.empty());
    }
    // Foreign colonies only in explored systems, without facilities or queues.
    for (const auto& c : v.colonies) {
        if (!c || c->owner == me) continue;
        CHECK(s.empire(me).hasExplored(s.galaxy.object(c->planet).system));
        CHECK(c->facilities.empty());
        CHECK(c->queue.items.empty());
    }
    // Serializes and loads like any state.
    auto back = deserializeState(serializeState(v));
    REQUIRE(back.has_value());
    CHECK(stateChecksum(*back) == stateChecksum(v));
}

TEST_CASE("redact: spectators see no empire's private data") {
    const Rules& r = test::engineRules();
    GameState s = test::newEngineGame(3, 2, 10);
    const GameState v = redactForEmpire(r, s, EmpireId{});
    CHECK(validateState(v, &r).empty());
    CHECK(v.vehicles.empty());
    CHECK(v.fleets.empty());
    for (const Empire& e : v.empires) CHECK(e.stockpile.isZero());
}

TEST_CASE("e-mail address: set in Empire Setup, changed by Change Email, shown only to its player") {
    // Spec 06 §7 Q95: the address is the empire's, entered in Empire Setup's
    // Email box and changed with Change Email (the password is kept).
    const Rules& r = test::engineRules();
    GameSetup setup;
    setup.seed = 5;
    setup.options.systemCount = 12;
    for (int i = 0; i < 2; ++i) {
        EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.email = i == 0 ? "  first@example.org\t" : "";
        e.passwordHash = i == 0 ? "secret" : "";
        setup.empires.push_back(std::move(e));
    }
    auto created = createGame(r, setup);
    REQUIRE(created.has_value());
    GameState& s = *created;
    const EmpireId me{0u}, other{1u};
    CHECK(s.empire(me).email == "first@example.org");  // control characters and outer spaces dropped
    CHECK(s.empire(other).email.empty());

    REQUIRE(apply(r, s, other, cmd::SetEmpireOptions{.email = std::string("second@example.org")}).ok);
    CHECK(s.empire(other).email == "second@example.org");
    REQUIRE(apply(r, s, me, cmd::SetEmpireOptions{.email = std::string("new@example.org")}).ok);
    CHECK(s.empire(me).email == "new@example.org");
    CHECK(s.empire(me).passwordHash == "secret");
    // At most kMaxEmailBytes bytes, never cut inside a UTF-8 sequence.
    std::string longAddress(kMaxEmailBytes - 1, 'a');
    longAddress += "\xc3\xa9@example.org";
    CHECK(cleanEmail(longAddress) == std::string(kMaxEmailBytes - 1, 'a'));

    const GameState mine = redactForEmpire(r, s, me);
    CHECK(mine.empire(me).email == "new@example.org");
    CHECK(mine.empire(other).email.empty());
    const GameState spectator = redactForEmpire(r, s, EmpireId{});
    CHECK(spectator.empire(me).email.empty());
    CHECK(spectator.empire(other).email.empty());
}
