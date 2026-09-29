#include "test_support.hpp"

#include "sim/commands.hpp"
#include "sim/mutate.hpp"
#include "sim/pathfinding.hpp"
#include "sim/rules.hpp"
#include "sim/turn.hpp"

using namespace opense4;
using namespace opense4::sim;

namespace {

const Ship* firstShipWithRole(const GameState& s, EmpireId e, std::string_view role) {
    for (const Ship& ship : s.ships)
        if (ship.owner == e && s.design(ship.design).role == role) return &ship;
    return nullptr;
}

DesignId designWithRole(const GameState& s, EmpireId e, std::string_view role) {
    for (DesignId d : s.empire(e).designs)
        if (s.design(d).role == role) return d;
    return {};
}

} // namespace

TEST_CASE("ships move at most their speed per turn and arrive") {
    auto game = test::newGame(11);
    GameState& s = game.state;
    const Content& c = test::content();
    const Ship* scout = firstShipWithRole(s, game.player, "scout");
    REQUIRE(scout);
    const ShipId id = scout->id;
    const int speed = s.statsOf(*scout).speed;
    REQUIRE(speed > 0);

    // Pick a destination far enough to need several turns.
    const auto far = findPathTo(s, scout->location, [&](Location l) { return l.system != scout->location.system; });
    REQUIRE(far);
    const Location dest = far->back();
    const size_t total = far->size();
    REQUIRE(applyCommand(s, c, game.player, cmd::MoveShip{id, dest}));

    processTurn(s, c);
    const Ship* moved = s.findShip(id);
    REQUIRE(moved);
    CHECK(moved->path.size() == (total > static_cast<size_t>(speed) ? total - speed : 0));

    for (int turn = 0; turn < 20 && s.findShip(id)->location != dest; ++turn) processTurn(s, c);
    CHECK(s.findShip(id)->location == dest);
    CHECK(s.findShip(id)->order.type == OrderType::None);
    CHECK(s.empire(game.player).hasExplored(dest.system));
}

TEST_CASE("movement is interleaved by speed within a turn") {
    // A speed-4 scout crosses sector P while a speed-3 enemy frigate moves onto P.
    // The scout reaches P at time k/4, the frigate at time j/3; whoever is first decides
    // whether the scout slips past or gets intercepted (and then destroyed in combat).
    const Content& c = test::content();
    auto run = [&](SectorPos scoutStart, SectorPos scoutDest, SectorPos frigateStart) {
        auto game = test::newGame(12);
        GameState& s = game.state;
        const EmpireId enemy{1u};
        // An empty system far from everyone's ships.
        SystemId arena;
        for (const StarSystem& sys : s.systems)
            if (shipsInSystem(s, sys.id).empty()) arena = sys.id;
        REQUIRE(arena.valid());

        const ShipId scout = firstShipWithRole(s, game.player, "scout")->id;
        const ShipId frigate = firstShipWithRole(s, enemy, "warship")->id;
        REQUIRE(s.statsOf(*s.findShip(scout)).speed == 4);
        REQUIRE(s.statsOf(*s.findShip(frigate)).speed == 3);
        s.findShip(scout)->location = {arena, scoutStart};
        s.findShip(frigate)->location = {arena, frigateStart};
        const Location p{arena, SectorPos{-3, 0}};
        REQUIRE(applyCommand(s, c, game.player, cmd::MoveShip{scout, {arena, scoutDest}}));
        REQUIRE(applyCommand(s, c, enemy, cmd::MoveShip{frigate, p}));
        processTurn(s, c);
        const Ship* survivor = s.findShip(scout);
        return survivor ? std::optional<Location>(survivor->location) : std::nullopt;
    };

    // Scout at P at t=1/4, frigate arrives at t=2/3: the scout slips past.
    const auto early = run({-4, 0}, {0, 0}, {-3, 2});
    REQUIRE(early);
    CHECK(early->sector == SectorPos{0, 0});

    // Scout at P at t=2/4, frigate already there since t=1/3: intercepted and destroyed.
    const auto late = run({-5, 0}, {-1, 0}, {-3, 1});
    CHECK_FALSE(late.has_value());
}

namespace {

// Moves two ships into an empty system so tests control exactly who is where.
struct Arena {
    GameState* s;
    SystemId system;
};

Arena makeArena(GameState& s) {
    Arena a{&s, {}};
    for (const StarSystem& sys : s.systems)
        if (shipsInSystem(s, sys.id).empty()) a.system = sys.id;
    REQUIRE(a.system.valid());
    return a;
}

} // namespace

TEST_CASE("interception stops the hostile ships that were already there") {
    // An enemy frigate (speed 3) moves onto our colony ship at t=1/3; the colony
    // ship would leave at t=1/2. Both must stop and fight - the target can't slip away.
    auto game = test::newGame(12);
    GameState& s = game.state;
    const Content& c = test::content();
    const EmpireId enemy{1u};
    const Arena arena = makeArena(s);
    const ShipId colony = firstShipWithRole(s, game.player, "colony")->id;
    const ShipId frigate = firstShipWithRole(s, enemy, "warship")->id;
    s.findShip(colony)->location = {arena.system, SectorPos{-3, 0}};
    s.findShip(frigate)->location = {arena.system, SectorPos{-3, 1}};
    REQUIRE(applyCommand(s, c, game.player, cmd::MoveShip{colony, {arena.system, SectorPos{-1, 0}}}));
    REQUIRE(applyCommand(s, c, enemy, cmd::MoveShip{frigate, {arena.system, SectorPos{-3, 0}}}));
    processTurn(s, c);
    CHECK(s.findShip(colony) == nullptr);  // caught and destroyed
    REQUIRE(s.findShip(frigate));
    CHECK(s.findShip(frigate)->location.sector == SectorPos{-3, 0});
}

TEST_CASE("combat rounds are simultaneous, so ship ids give no advantage") {
    // Two identical frigates: each deals 20 per round to the other's 175 structure,
    // so both are destroyed in round 9. With sequential fire the lower id would survive.
    for (uint64_t seed : {12ull, 13ull, 99ull}) {
        auto game = test::newGame(seed);
        GameState& s = game.state;
        const Content& c = test::content();
        const Arena arena = makeArena(s);
        const ShipId a = firstShipWithRole(s, game.player, "warship")->id;
        const ShipId b = firstShipWithRole(s, EmpireId{1u}, "warship")->id;
        s.findShip(a)->location = s.findShip(b)->location = {arena.system, SectorPos{2, 2}};
        processTurn(s, c);
        CHECK(s.findShip(a) == nullptr);
        CHECK(s.findShip(b) == nullptr);
    }
}

TEST_CASE("orders only use what the empire knows") {
    auto game = test::newGame(33);
    GameState& s = game.state;
    const Content& c = test::content();
    const Empire& me = s.empire(game.player);
    const ShipId scout = firstShipWithRole(s, game.player, "scout")->id;
    const SystemId home = s.planet(me.homeworld).system;
    const auto hops = warpHopDistances(s, home);

    // One jump away: reachable through a known warp point. Two jumps: needs an unseen one.
    SystemId near, far;
    for (size_t i = 0; i < hops.size(); ++i) {
        if (hops[i] == 1) near = SystemId{i};
        if (hops[i] == 2) far = SystemId{i};
    }
    REQUIRE(near.valid());
    REQUIRE(far.valid());
    CHECK(applyCommand(s, c, game.player, cmd::MoveShip{scout, {near, SectorPos{}}}));
    CHECK_FALSE(applyCommand(s, c, game.player, cmd::MoveShip{scout, {far, SectorPos{}}}));
    CHECK(findPath(s, s.findShip(scout)->location, {far, SectorPos{}}).has_value());  // omniscient query still works

    // Colonizing a planet in an unexplored system is refused without revealing anything about it.
    const ShipId colonyShip = firstShipWithRole(s, game.player, "colony")->id;
    for (const Planet& p : s.planets) {
        if (me.hasExplored(p.system)) continue;
        const auto result = applyCommand(s, c, game.player, cmd::Colonize{colonyShip, p.id});
        REQUIRE_FALSE(result);
        CHECK(result.error() == "That system has not been explored.");
        break;
    }
}

TEST_CASE("colony ships found colonies") {
    auto game = test::newGame(21);
    GameState& s = game.state;
    const Content& c = test::content();
    Empire& player = s.empire(game.player);
    const Ship* colonyShip = firstShipWithRole(s, game.player, "colony");
    REQUIRE(colonyShip);
    const ShipId shipId = colonyShip->id;

    // Make a guaranteed target: an uncolonized native-surface planet somewhere else.
    Planet* target = nullptr;
    for (Planet& p : s.planets)
        if (!p.colony && p.system != s.planet(player.homeworld).system) {
            target = &p;
            break;
        }
    REQUIRE(target);
    target->surface = player.nativeSurface;
    const PlanetId targetId = target->id;
    std::fill(player.explored.begin(), player.explored.end(), uint8_t{1});  // orders need known routes

    REQUIRE(applyCommand(s, c, game.player, cmd::Colonize{shipId, targetId}));
    for (int turn = 0; turn < 60 && s.findShip(shipId); ++turn) processTurn(s, c);

    CHECK(s.findShip(shipId) == nullptr);  // consumed
    REQUIRE(s.planet(targetId).colony);
    CHECK(s.planet(targetId).colony->owner == game.player);
    CHECK(colonyCount(s, game.player) == 2);
}

TEST_CASE("colonize command rejects unsuitable planets") {
    auto game = test::newGame(22);
    GameState& s = game.state;
    const Content& c = test::content();
    const Ship* colonyShip = firstShipWithRole(s, game.player, "colony");
    const Ship* scout = firstShipWithRole(s, game.player, "scout");
    REQUIRE(colonyShip);
    REQUIRE(scout);
    const PlanetId home = s.empire(game.player).homeworld;
    CHECK_FALSE(applyCommand(s, c, game.player, cmd::Colonize{colonyShip->id, home}));  // already colonized

    PlanetId free;
    for (const Planet& p : s.planets)
        if (!p.colony) free = p.id;
    REQUIRE(free.valid());
    CHECK_FALSE(applyCommand(s, c, game.player, cmd::Colonize{scout->id, free}));  // no colony module

    // Other empires' ships cannot be ordered around.
    const EmpireId enemy{1u};
    const Ship* enemyShip = firstShipWithRole(s, enemy, "scout");
    REQUIRE(enemyShip);
    CHECK_FALSE(applyCommand(s, c, game.player, cmd::StopShip{enemyShip->id}));
}

TEST_CASE("economy: income, construction and research") {
    auto game = test::newGame(31);
    GameState& s = game.state;
    const Content& c = test::content();
    const EmpireId me = game.player;
    const PlanetId home = s.empire(me).homeworld;

    const Resources before = s.empire(me).stockpile;
    const ColonyOutput out = colonyOutput(c, s, s.planet(home));
    CHECK(out.resources[ResourceType::Minerals] > 0);
    CHECK(out.research > 0);

    // Build a scout and queue research.
    const DesignId scoutDesign = designWithRole(s, me, "scout");
    REQUIRE(applyCommand(s, c, me, cmd::BuildShip{home, scoutDesign}));
    const TechIndex physics = *c.findTech("physics");
    REQUIRE(applyCommand(s, c, me, cmd::SetResearchQueue{{physics}}));
    const int physicsBefore = s.empire(me).techLevel(physics);
    const int shipsBefore = shipCount(s, me);

    processTurn(s, c);
    const Resources cost = s.design(scoutDesign).stats.cost;
    CHECK(s.empire(me).stockpile == before + out.resources - cost);
    CHECK(shipCount(s, me) == shipsBefore + 1);
    CHECK(s.planet(home).colony->queue.empty());

    for (int turn = 0; turn < 10 && s.empire(me).techLevel(physics) == physicsBefore; ++turn) processTurn(s, c);
    CHECK(s.empire(me).techLevel(physics) == physicsBefore + 1);
    CHECK(s.empire(me).researchQueue.empty());
}

TEST_CASE("facilities respect slots and cancellations refund") {
    auto game = test::newGame(32);
    GameState& s = game.state;
    const Content& c = test::content();
    const EmpireId me = game.player;
    const PlanetId home = s.empire(me).homeworld;
    const FacilityIndex miner = *c.findFacility("mineral_miner");

    const int freeSlots = facilitySlots(c, s.planet(home)) - usedFacilitySlots(*s.planet(home).colony);
    REQUIRE(freeSlots > 0);
    for (int i = 0; i < freeSlots; ++i) REQUIRE(applyCommand(s, c, me, cmd::BuildFacility{home, miner}));
    CHECK_FALSE(applyCommand(s, c, me, cmd::BuildFacility{home, miner}));

    // Starve the stockpile so the first item is only partly paid for this turn.
    s.empire(me).stockpile = Resources{30, 0, 0};
    s.planet(home).colony->facilities.clear();  // no income either
    processTurn(s, c);
    const Colony& col = *s.planet(home).colony;
    REQUIRE(col.queue.size() == static_cast<size_t>(freeSlots));
    CHECK(col.queue.front().spent == Resources{30, 0, 0});
    CHECK(col.queue.front().percentComplete() == 30);
    REQUIRE(applyCommand(s, c, me, cmd::CancelConstruction{home, 0}));
    CHECK(s.empire(me).stockpile == Resources{30, 0, 0});
}

TEST_CASE("armed ships fight and destroy unarmed enemies") {
    auto game = test::newGame(41);
    GameState& s = game.state;
    const Content& c = test::content();
    const EmpireId me = game.player;
    const EmpireId enemy{1u};
    const Ship* frigate = firstShipWithRole(s, me, "warship");
    const Ship* enemyScout = firstShipWithRole(s, enemy, "scout");
    REQUIRE(frigate);
    REQUIRE(enemyScout);
    const ShipId victim = enemyScout->id;
    s.findShip(victim)->location = frigate->location;

    processTurn(s, c);
    CHECK(s.findShip(victim) == nullptr);
    bool reported = false;
    for (const GameEvent& e : s.events)
        if (e.empire == me && e.kind == EventKind::Combat) reported = true;
    CHECK(reported);
}

TEST_CASE("all-AI games are deterministic and make progress") {
    auto a = test::newGame(2024, 35, 5, true);
    auto b = test::newGame(2024, 35, 5, true);
    const Content& c = test::content();
    for (int turn = 0; turn < 60; ++turn) {
        advanceTurn(a.state, c);
        advanceTurn(b.state, c);
        REQUIRE_MESSAGE(stateChecksum(a.state) == stateChecksum(b.state), "desync at turn " << a.state.turn);
    }
    CHECK(a.state.turn == 61);

    int colonies = 0;
    int researched = 0;
    for (const Empire& e : a.state.empires) {
        colonies += colonyCount(a.state, e.id);
        for (size_t t = 0; t < c.techs.size(); ++t) researched += e.techLevels[t];
    }
    CHECK(colonies > static_cast<int>(a.state.empires.size()));  // AIs expanded beyond their homeworlds
    CHECK(researched > 0);
}
