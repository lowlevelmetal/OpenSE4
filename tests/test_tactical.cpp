// Tactical combat (docs/spec/04 §3, §4, §16; game/tactical.hpp): the battle
// stepped one phase at a time with orders for player sides, the same rules as
// strategic resolution, validated orders, scripts that replay a battle, and
// tactical battles in turn-based games (turn.hpp), and the combat simulator
// (game/simulator.hpp). All content is invented for the tests.

#include "combat_fixture.hpp"

#include "client/classic/session.hpp"

#include "game/combat.hpp"
#include "game/query.hpp"
#include "game/serialize.hpp"
#include "game/simulator.hpp"
#include "game/tactical.hpp"
#include "game/turn.hpp"
#include "game/turn_internal.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <optional>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::ctest;
using opense4::test::homeworld;
namespace combat = opense4::game::combat;
using combat::TacticalBattle;
using combat::TacticalOrder;
using OK = combat::TacticalOrder::Kind;

namespace {

TacticalOrder order(OK kind, EmpireId e, int piece = -1, int target = -1) { return TacticalOrder{kind, e, piece, target}; }

int pieceIndex(const TacticalBattle& b, VehicleId v) {
    for (size_t i = 0; i < b.pieces().size(); ++i)
        if (b.pieces()[i].vehicle == v) return static_cast<int>(i);
    FAIL("no piece for vehicle " << v.value);
    return -1;
}

GameState strategic(GameState s, Location where) {
    TurnContext ctx = context(s);
    combat::resolveSpaceCombat(ctx, where);
    return s;
}

// The player side's phases played by the strategies (AutoPhase), with the
// strategies' orders recorded phase by phase.
std::vector<std::vector<TacticalOrder>> playAuto(TacticalBattle& b) {
    std::vector<TacticalOrder> log;
    std::vector<std::vector<TacticalOrder>> phases;
    b.recordStrategies(&log);
    while (b.awaitingOrders()) {
        const size_t before = log.size();
        REQUIRE(b.submit(order(OK::AutoPhase, b.phaseEmpire())).empty());
        phases.emplace_back(log.begin() + static_cast<std::ptrdiff_t>(before), log.end());
    }
    b.recordStrategies(nullptr);
    b.finish();
    return phases;
}

// Whether the strategies launched drones in a phase: drones a player launches
// first act at the side's next phase (spec 04 §4), so such a phase cannot be
// given as orders by hand to the same effect.
bool launchesDrones(const GameState& s, const std::vector<TacticalOrder>& phase) {
    return std::any_of(phase.begin(), phase.end(), [&](const TacticalOrder& o) {
        return o.kind == OK::Launch && combatRules().hull(s.design(o.design).hull).type == ruleset::VehicleType::Drone;
    });
}

// Plays A's phases with a simple hand policy: close on the nearest enemy, then fire everything at it.
void playByHand(TacticalBattle& b, EmpireId me) {
    while (b.awaitingOrders()) {
        REQUIRE(b.phaseEmpire() == me);
        for (size_t i = 0; i < b.pieces().size(); ++i) {
            const combat::TacticalPiece& p = b.pieces()[i];
            if (!p.alive || p.owner != me || p.kind == CombatPiece::Kind::Seeker || p.type == ruleset::VehicleType::Drone) continue;
            int nearest = -1;
            for (size_t j = 0; j < b.pieces().size(); ++j) {
                const combat::TacticalPiece& q = b.pieces()[j];
                if (!q.alive || q.kind == CombatPiece::Kind::Seeker || q.kind == CombatPiece::Kind::Obstacle || !b.hostile(me, q.owner)) continue;
                if (nearest < 0 || b.distance(static_cast<int>(i), static_cast<int>(j)) < b.distance(static_cast<int>(i), nearest))
                    nearest = static_cast<int>(j);
            }
            if (nearest < 0) break;
            const combat::TacticalPiece& t = b.pieces()[static_cast<size_t>(nearest)];
            TacticalOrder mv = order(OK::Move, me, static_cast<int>(i));
            mv.x = t.x;
            mv.y = t.y;
            if (b.check(mv).empty()) b.submit(mv);
            TacticalOrder fire = order(OK::Fire, me, static_cast<int>(i), nearest);
            if (b.check(fire).empty()) b.submit(fire);
        }
        if (b.awaitingOrders()) REQUIRE(b.submit(order(OK::EndPhase, me)).empty());
    }
    b.finish();
}

} // namespace

// ---- One set of rules, two kinds of control ------------------------------------------------------

TEST_CASE("tactical: a battle stepped with the strategies' orders is the strategic battle") {
    // For each battle: (1) strategic resolution; (2) the player sides played by
    // the strategies (AutoPhase), which records their orders; (3) those orders
    // given by hand; (4) the accepted orders replayed as a script. (2) to (4) end
    // in the same state. So does (1), except that a battle with player sides ends
    // after the phase in which the last enemy falls, a strategic one only after
    // the whole combat turn (spec 04 §4): up to then both record the same events.
    // A player's side is not automated, so a hit on its group's leader never
    // dissolves the group as it does in strategic resolution (spec 03 §10, §19
    // Q60): (1) is compared only when no player side has a fleet.
    int battles = 0, orders = 0, whole = 0, compared = 0;
    for (int variant = 0; variant < 20; ++variant)
        for (uint64_t seed = 1; seed <= 3; ++seed) {
            CAPTURE(variant);
            CAPTURE(seed);
            const int control = (variant + static_cast<int>(seed)) % 3;
            std::vector<EmpireId> players = control == 0   ? std::vector<EmpireId>{EmpireId{0u}}
                                            : control == 1 ? std::vector<EmpireId>{EmpireId{1u}}
                                                           : std::vector<EmpireId>{EmpireId{0u}, EmpireId{1u}};
            auto [start, where] = battleScenario(variant, seed);
            const GameState resolved = strategic(start, where);
            const TacticalBattle::Setup setup{where, std::nullopt, players};
            const bool playerFleet = std::any_of(start.fleets.begin(), start.fleets.end(), [&](const Fleet& f) {
                return !f.members.empty() && std::find(players.begin(), players.end(), f.owner) != players.end();
            });

            TacticalBattle autoBattle(combatRules(), start, setup);
            REQUIRE(autoBattle.started());
            const std::vector<std::vector<TacticalOrder>> phases = playAuto(autoBattle);
            const uint64_t expected = stateChecksum(autoBattle.state());
            if (!playerFleet) {
                ++compared;
                const std::vector<CombatEvent>& mine = autoBattle.record().events;
                const std::vector<CombatEvent>& theirs = resolved.combats.back().events;
                REQUIRE(mine.size() <= theirs.size());
                bool prefix = true;
                for (size_t i = 0; i < mine.size() && prefix; ++i)
                    prefix = mine[i].kind == theirs[i].kind && mine[i].piece == theirs[i].piece && mine[i].target == theirs[i].target &&
                             mine[i].amount == theirs[i].amount && mine[i].x == theirs[i].x && mine[i].y == theirs[i].y;
                CHECK(prefix);
                if (mine.size() == theirs.size()) {
                    CHECK(expected == stateChecksum(resolved));
                    ++whole;
                }
            }

            TacticalBattle byHand(combatRules(), start, setup);
            size_t phase = 0;
            while (byHand.awaitingOrders()) {
                REQUIRE(phase < phases.size());
                if (launchesDrones(start, phases[phase])) {
                    REQUIRE(byHand.submit(order(OK::AutoPhase, byHand.phaseEmpire())).empty());
                    ++phase;
                    continue;
                }
                for (const TacticalOrder& o : phases[phase]) {
                    const std::string why = byHand.submit(o);
                    CHECK_MESSAGE(why.empty(), identifier(o.kind) << ": " << why);
                    ++orders;
                }
                REQUIRE(byHand.submit(order(OK::EndPhase, byHand.phaseEmpire())).empty());
                ++phase;
            }
            CHECK(phase == phases.size());
            byHand.finish();
            CHECK(stateChecksum(byHand.state()) == expected);
            CHECK(byHand.record().events.size() == autoBattle.record().events.size());

            TacticalBattle replay(combatRules(), start, setup);
            for (const TacticalOrder& o : byHand.script()) CHECK(replay.submit(o).empty());
            CHECK(replay.finished());
            replay.finish();
            CHECK(stateChecksum(replay.state()) == expected);
            ++battles;
        }
    CHECK(battles == 60);
    CHECK(orders > 1000);
    CHECK(compared >= 40);
    CHECK(whole > 20);
}

TEST_CASE("tactical: without player sides the battle is fought at once, as strategic resolution fights it") {
    auto [start, where] = battleScenario(4, 2);
    TacticalBattle b(combatRules(), start, TacticalBattle::Setup{where, std::nullopt, {}});
    CHECK(b.started());
    CHECK_FALSE(b.awaitingOrders());
    CHECK(b.finished());
    b.finish();
    CHECK(stateChecksum(b.state()) == stateChecksum(strategic(start, where)));
    CHECK(b.applied());
    CHECK(b.submit(order(OK::EndPhase, EmpireId{0u})) == "The battle is over.");
}

TEST_CASE("tactical: no battle where nobody hostile sees anyone") {
    Arena ar = makeArena();
    spawn(ar.s, frigate(ar.s, ar.a, "Alone", 2, {"Test Laser"}), ar.loc);
    TacticalBattle b(combatRules(), ar.s, TacticalBattle::Setup{ar.loc, std::nullopt, {ar.a}});
    CHECK_FALSE(b.started());
    CHECK_FALSE(b.awaitingOrders());
    CHECK(b.submit(order(OK::EndPhase, ar.a)) == "The battle is over.");
}

TEST_CASE("tactical: the same orders give the same battle") {
    auto run = [] {
        auto [start, where] = battleScenario(10, 5);
        TacticalBattle b(combatRules(), start, TacticalBattle::Setup{where, std::nullopt, {EmpireId{0u}}});
        playByHand(b, EmpireId{0u});
        return std::pair{stateChecksum(b.state()), b.script()};
    };
    const auto [x, scriptX] = run();
    const auto [y, scriptY] = run();
    CHECK(x == y);
    CHECK(scriptX == scriptY);
    CHECK(scriptX.size() > 10);
}

TEST_CASE("tactical: random orders are checked, never break the battle, and replay the same") {
    // Arbitrary orders, most of them invalid: refused ones change nothing, accepted ones
    // are the script, and the script replays to the same battle.
    for (int variant : {1, 4, 9, 14}) {
        CAPTURE(variant);
        auto [start, where] = battleScenario(variant, 2);
        const TacticalBattle::Setup setup{where, std::nullopt, {EmpireId{0u}, EmpireId{1u}}};
        TacticalBattle b(combatRules(), start, setup);
        Rng dice(static_cast<uint64_t>(variant) * 977u);
        int accepted = 0, refused = 0;
        for (int n = 0; n < 4000 && b.awaitingOrders(); ++n) {
            TacticalOrder o;
            o.kind = static_cast<OK>(dice.rangeInt(0, static_cast<int>(OK::ResolveCombat)));
            if (o.kind == OK::ResolveCombat && dice.rangeInt(0, 20) != 0) o.kind = OK::EndPhase;
            o.empire = dice.rangeInt(0, 9) == 0 ? EmpireId{static_cast<uint32_t>(dice.rangeInt(0, 2))} : b.phaseEmpire();
            const int pieces = static_cast<int>(b.pieces().size());
            o.piece = dice.rangeInt(-1, pieces);
            o.target = dice.rangeInt(-1, pieces);
            o.weapon = dice.rangeInt(-1, 4);
            o.instance = dice.rangeInt(-1, 3);
            o.x = dice.rangeInt(-2, combat::kCombatMapWidth + 1);
            o.y = dice.rangeInt(-2, combat::kCombatMapHeight + 1);
            if (dice.rangeInt(0, 3) == 0)
                for (int k = dice.rangeInt(1, 4); k > 0; --k)
                    o.path.push_back(combat::Square{static_cast<int16_t>(dice.rangeInt(0, 71)), static_cast<int16_t>(dice.rangeInt(0, 62))});
            if (o.piece >= 0 && o.piece < pieces && !b.pieces()[static_cast<size_t>(o.piece)].cargo.empty())
                o.design = b.pieces()[static_cast<size_t>(o.piece)].cargo.front().design;
            o.count = dice.rangeInt(-1, 12);
            o.group = dice.rangeInt(-1, 11);
            o.on = dice.rangeInt(0, 1) == 1;
            o.alone = dice.rangeInt(0, 1) == 1;
            const std::string predicted = b.check(o);
            const size_t script = b.script().size();
            const std::string why = b.submit(o);
            CHECK(why == predicted);   // check() says exactly what submit() does
            if (why.empty()) {
                ++accepted;
                CHECK(b.script().size() == script + 1);
            } else {
                ++refused;
                CHECK(b.script().size() == script);
            }
        }
        b.finish();
        CHECK(accepted > 20);
        CHECK(refused > 50);
        TacticalBattle replay(combatRules(), start, setup);
        for (const TacticalOrder& o : b.script()) CHECK(replay.submit(o).empty());
        replay.finish();
        CHECK(stateChecksum(replay.state()) == stateChecksum(b.state()));
    }
}

// ---- Orders and their checks ---------------------------------------------------------------------------

namespace {

struct Skirmish {
    Arena ar = makeArena(11);
    VehicleId gunner, boarder, carrier, target, shielded;
    DesignId fighter;

    Skirmish() {
        GameState& s = ar.s;
        fighter = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
        gunner = spawn(s, frigate(s, ar.a, "Gunner", 4, {"CT Short Gun", "Test Laser", "CT Combat Thruster"}), ar.loc);
        boarder = spawn(s, frigate(s, ar.a, "Boarder", 4, {"Test Boarding Party", "Test Boarding Party", "CT Combat Thruster"}), ar.loc);
        carrier = spawn(s, frigate(s, ar.a, "Carrier", 1, {"Test Fighter Bay", "Test Fighter Bay", "CT Big Armor"}), ar.loc);
        s.vehicle(carrier)->cargo.units.push_back({fighter, 7});
        // Unarmed bases: they neither shoot nor run away. They came through a warp
        // point, so they start beside A in the middle of the map (spec 04 §3).
        target = spawn(s, design(s, ar.b, "Hulk", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "CT Big Armor"}), ar.loc);
        shielded = spawn(s, design(s, ar.b, "Shielded", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Shield",
                                                                         "CT Big Armor"}),
                         ar.loc);
        warpIn(s, target);
        warpIn(s, shielded);
    }
};

// Plays until it is A's phase (B is on its strategies) and returns the battle.
TacticalBattle startSkirmish(const Skirmish& k) {
    TacticalBattle b(combatRules(), k.ar.s, TacticalBattle::Setup{k.ar.loc, std::nullopt, {k.ar.a}});
    REQUIRE(b.started());
    REQUIRE(b.awaitingOrders());
    REQUIRE(b.phaseEmpire() == k.ar.a);
    return b;
}

} // namespace

TEST_CASE("tactical: orders are refused out of turn and for pieces that are not the player's") {
    Skirmish k;
    TacticalBattle b = startSkirmish(k);
    const int gunner = pieceIndex(b, k.gunner), target = pieceIndex(b, k.target);
    CHECK_FALSE(b.paused());
    CHECK(b.check(order(OK::EndPhase, k.ar.b)) == "It is not that side's phase.");
    TacticalOrder mv = order(OK::Move, k.ar.a, target);
    mv.x = 1;
    mv.y = 1;
    CHECK(b.check(mv) == "That piece is not yours.");
    CHECK(b.check(order(OK::Fire, k.ar.a, gunner, pieceIndex(b, k.boarder))) == "That is not an enemy.");
    // A refused order changes nothing and is not part of the script.
    CHECK_FALSE(b.submit(mv).empty());
    CHECK(b.script().empty());
    CHECK(b.submit(order(OK::ClearAllGroups, k.ar.a)).empty());
    CHECK(b.script().size() == 1);
}

TEST_CASE("tactical: movement spends movement points square by square") {
    Skirmish k;
    TacticalBattle b = startSkirmish(k);
    const int gunner = pieceIndex(b, k.gunner);
    const combat::TacticalPiece start = b.pieces()[static_cast<size_t>(gunner)];
    REQUIRE(start.movement > 0);
    CHECK(start.movement == start.movementMax);
    // Toward a far corner: it goes as far as its movement points allow.
    TacticalOrder mv = order(OK::Move, k.ar.a, gunner);
    mv.x = 0;
    mv.y = 0;
    const std::vector<combat::Square> preview = b.pathTo(gunner, 0, 0);
    CHECK(static_cast<int>(preview.size()) == std::min(start.movement, std::max(start.x, start.y)));
    CHECK(b.submit(mv).empty());
    const combat::TacticalPiece& moved = b.pieces()[static_cast<size_t>(gunner)];
    CHECK(moved.x == preview.back().x);
    CHECK(moved.y == preview.back().y);
    CHECK(moved.movement == 0);
    CHECK(b.check(mv) == "No movement left this turn.");
    // A path must go square by square.
    const int boarder = pieceIndex(b, k.boarder);
    TacticalOrder jump = order(OK::Move, k.ar.a, boarder);
    jump.path = {combat::Square{static_cast<int16_t>(b.pieces()[static_cast<size_t>(boarder)].x + 2),
                                static_cast<int16_t>(b.pieces()[static_cast<size_t>(boarder)].y)}};
    CHECK(b.check(jump) == "The path must go square by square.");
    // Movement points come back at the start of the next combat turn (spec 04 §4).
    const int round = b.round();
    CHECK(b.submit(order(OK::EndPhase, k.ar.a)).empty());
    REQUIRE(b.phaseEmpire() == k.ar.a);
    CHECK(b.round() == round + 1);
    CHECK(b.pieces()[static_cast<size_t>(gunner)].movement == b.pieces()[static_cast<size_t>(gunner)].movementMax);
}

TEST_CASE("tactical: weapons fire when ready, in range and within the target budget") {
    Skirmish k;
    TacticalBattle b = startSkirmish(k);
    const EmpireId a = k.ar.a;
    const int gunner = pieceIndex(b, k.gunner), target = pieceIndex(b, k.target), shielded = pieceIndex(b, k.shielded);
    // Weapon 0 is the short gun (2 squares), weapon 1 the laser (6 squares).
    TacticalOrder shortGun = order(OK::Fire, a, gunner, target);
    shortGun.weapon = 0;
    TacticalOrder laser = shortGun;
    laser.weapon = 1;
    // Close in until the short gun reaches (the battle starts with the sides a few squares apart).
    for (int round = 0; round < 6 && b.distance(gunner, target) > 2; ++round) {
        if (b.phaseEmpire() != a) break;
        if (b.distance(gunner, target) > 2) CHECK(b.check(shortGun) == "Out of range.");
        TacticalOrder mv = order(OK::Move, a, gunner);
        mv.x = b.pieces()[static_cast<size_t>(target)].x;
        mv.y = b.pieces()[static_cast<size_t>(target)].y;
        if (b.check(mv).empty()) b.submit(mv);
        if (b.distance(gunner, target) > 2) REQUIRE(b.submit(order(OK::EndPhase, a)).empty());
    }
    REQUIRE(b.phaseEmpire() == a);
    REQUIRE(b.distance(gunner, target) <= 2);
    CHECK(b.fireProblem(gunner, 0, target).empty());
    const size_t events = b.record().events.size();
    CHECK(b.submit(shortGun).empty());
    CHECK(b.record().events.size() > events);
    CHECK(b.record().events[events].kind == CombatEvent::Kind::Fire);
    CHECK(b.check(shortGun) == "The weapon is reloading.");
    CHECK(b.pieces()[static_cast<size_t>(gunner)].weapons[0].reload[0] > 0);
    // One target per turn without Multiplex Tracking (spec 04 §6): the laser may join in on
    // the same target but not take a second one.
    TacticalOrder other = laser;
    other.target = shielded;
    if (b.fireProblem(gunner, 1, shielded) != "Out of range.") CHECK(b.check(other) == "It has engaged its one target this turn.");
    CHECK(b.check(laser).empty());
    CHECK(b.hitChance(gunner, 1, target) >= 1);
    CHECK(b.damageAt(gunner, 1, target) > 0);
}

TEST_CASE("tactical: weapons can be switched off, and Fire uses the ones left on") {
    Skirmish k;
    TacticalBattle b = startSkirmish(k);
    const EmpireId a = k.ar.a;
    const int gunner = pieceIndex(b, k.gunner), target = pieceIndex(b, k.target);
    TacticalOrder off = order(OK::ToggleWeapon, a, gunner);
    off.on = false;
    CHECK(b.submit(off).empty());   // all weapons off
    CHECK(b.check(order(OK::Fire, a, gunner, target)) == "No weapon is selected.");
    TacticalOrder laserOn = order(OK::ToggleWeapon, a, gunner);
    laserOn.weapon = 1;
    CHECK(b.submit(laserOn).empty());
    CHECK_FALSE(b.pieces()[static_cast<size_t>(gunner)].weapons[0].enabled);
    CHECK(b.pieces()[static_cast<size_t>(gunner)].weapons[1].enabled);
    // Close to laser range if needed, then fire: only the laser shoots.
    for (int round = 0; round < 6 && !b.fireProblem(gunner, 1, target).empty(); ++round) {
        TacticalOrder mv = order(OK::Move, a, gunner);
        mv.x = b.pieces()[static_cast<size_t>(target)].x;
        mv.y = b.pieces()[static_cast<size_t>(target)].y;
        if (b.check(mv).empty()) b.submit(mv);
        if (!b.fireProblem(gunner, 1, target).empty()) REQUIRE(b.submit(order(OK::EndPhase, a)).empty());
    }
    REQUIRE(b.fireProblem(gunner, 1, target).empty());
    const size_t events = b.record().events.size();
    CHECK(b.submit(order(OK::Fire, a, gunner, target)).empty());
    const uint32_t laser = b.pieces()[static_cast<size_t>(gunner)].weapons[1].component;
    int fires = 0;
    for (size_t i = events; i < b.record().events.size(); ++i)
        if (b.record().events[i].kind == CombatEvent::Kind::Fire && b.record().events[i].piece == static_cast<uint32_t>(gunner)) {
            CHECK(b.record().events[i].component == laser);
            ++fires;
        }
    CHECK(fires == 1);
}

TEST_CASE("tactical: carriers launch fighters in groups of the chosen size, up to their bays") {
    Skirmish k;
    TacticalBattle b = startSkirmish(k);
    const EmpireId a = k.ar.a;
    const int carrier = pieceIndex(b, k.carrier);
    CHECK(b.pieces()[static_cast<size_t>(carrier)].launchLeft[0] == 8);   // two bays of 4
    TacticalOrder launch = order(OK::LaunchFighters, a, carrier);
    launch.design = k.fighter;
    launch.count = 7;
    launch.group = 2;
    CHECK(b.check(launch) == "Fighter groups hold 5, 8, 10, 15, 20, 30, 40 or 50.");
    launch.group = 5;
    const size_t before = b.pieces().size();
    CHECK(b.submit(launch).empty());
    std::vector<int> groups;
    for (size_t i = before; i < b.pieces().size(); ++i) {
        CHECK(b.pieces()[i].kind == CombatPiece::Kind::UnitGroup);
        CHECK(b.pieces()[i].design == k.fighter);
        CHECK(b.pieces()[i].carrier == carrier);
        CHECK(b.pieces()[i].movement > 0);   // full movement at once (spec 04 §5)
        groups.push_back(b.pieces()[i].count);
    }
    CHECK(groups == std::vector<int>{5, 2});
    CHECK(b.pieces()[static_cast<size_t>(carrier)].launchLeft[0] == 1);
    CHECK(b.pieces()[static_cast<size_t>(carrier)].cargo.empty());
    CHECK(b.check(launch) == "It carries no such units.");
    // The new groups take orders like any piece.
    TacticalOrder mv = order(OK::Move, a, static_cast<int>(before));
    for (const auto& [dx, dy] : {std::pair{-3, 0}, std::pair{3, 0}, std::pair{0, -3}, std::pair{0, 3}, std::pair{3, 3}, std::pair{-3, -3}}) {
        mv.x = b.pieces()[before].x + dx;
        mv.y = b.pieces()[before].y + dy;
        if (b.check(mv).empty()) break;
    }
    CHECK_MESSAGE(b.check(mv).empty(), b.check(mv));
    // Launched units that survive land on their carrier after the battle.
    b.submit(order(OK::ResolveCombat, a));
    CHECK(b.finished());
    b.finish();
    const Vehicle* carrierAfter = b.state().vehicle(k.carrier);
    REQUIRE(carrierAfter);
    if (carrierAfter->count > 0) CHECK(carrierAfter->cargo.unitCount(k.fighter) > 0);
}

TEST_CASE("tactical: drones launched by a player act on their own") {
    Arena ar = makeArena(5);
    GameState& s = ar.s;
    const DesignId drone = design(s, ar.a, "Dart", "Test Drone Hull", {"Test Engine", "Test Engine", "Test Warhead"});
    const VehicleId rack = spawn(s, frigate(s, ar.a, "Drone Carrier", 1, {"CT Drone Bay", "CT Big Armor"}), ar.loc);
    s.vehicle(rack)->cargo.units.push_back({drone, 2});
    spawn(s, frigate(s, ar.b, "Hulk", 1, {"CT Big Armor"}), ar.loc);
    TacticalBattle b(combatRules(), s, TacticalBattle::Setup{ar.loc, std::nullopt, {ar.a}});
    while (b.awaitingOrders() && b.phaseEmpire() != ar.a) b.submit(order(OK::EndPhase, b.phaseEmpire()));
    REQUIRE(b.phaseEmpire() == ar.a);
    TacticalOrder launch = order(OK::Launch, ar.a, pieceIndex(b, rack));
    launch.design = drone;
    launch.count = 2;
    launch.group = 1;
    const size_t before = b.pieces().size();
    CHECK(b.submit(launch).empty());
    REQUIRE(b.pieces().size() == before + 2);
    // Each drone is a group of its own; it first acts at the side's next phase (spec 04 §4, §10.7).
    CHECK(b.pieces()[before].count == 1);
    CHECK(b.pieces()[before + 1].count == 1);
    CHECK(b.pieces()[before].acted);
    const int x0 = b.pieces()[before].x, y0 = b.pieces()[before].y;
    TacticalOrder mv = order(OK::Move, ar.a, static_cast<int>(before));
    mv.x = 0;
    mv.y = 0;
    CHECK(b.check(mv) == "Drones act on their own.");
    CHECK(b.check(order(OK::Auto, ar.a, static_cast<int>(before))) == "Drones act on their own.");
    // The strategies play the rest of the phase: the new drones still wait.
    CHECK(b.submit(order(OK::AutoPhase, ar.a)).empty());
    size_t moves = 0;
    for (const CombatEvent& e : b.record().events)
        if (e.kind == CombatEvent::Kind::Move && e.piece == static_cast<uint32_t>(before)) ++moves;
    // At the side's next phase they moved before the player got control.
    REQUIRE(b.phaseEmpire() == ar.a);
    CHECK(moves > 0);
    CHECK((b.pieces()[before].x != x0 || b.pieces()[before].y != y0 || !b.pieces()[before].alive));
    int firstRound = 0;
    for (const CombatEvent& e : b.record().events)
        if (e.kind == CombatEvent::Kind::Move && e.piece == static_cast<uint32_t>(before)) {
            firstRound = e.round;
            break;
        }
    CHECK(firstRound == b.round());   // not in the combat turn they were launched
}

TEST_CASE("tactical: combat groups follow their leader") {
    Skirmish k;
    TacticalBattle b = startSkirmish(k);
    const EmpireId a = k.ar.a;
    const int gunner = pieceIndex(b, k.gunner), boarder = pieceIndex(b, k.boarder), carrier = pieceIndex(b, k.carrier);
    TacticalOrder member = order(OK::SetMember, a, boarder);
    member.group = 3;
    CHECK(b.check(member) == "Group 3 has no leader.");
    TacticalOrder leader = order(OK::SetLeader, a, gunner);
    leader.group = 3;
    CHECK(b.check(leader) == "Pick a formation for the group.");   // the player picks one (spec 04 §5)
    leader.formation = 0;                                          // Test Line: one square either side
    CHECK(b.submit(leader).empty());
    // Refused: a number that already has a leader, and a piece already in a group.
    TacticalOrder second = order(OK::SetLeader, a, carrier);
    second.group = 3;
    second.formation = 0;
    CHECK(b.check(second) == "Group 3 already has a leader.");
    CHECK(b.submit(member).empty());
    CHECK(b.check(member) == "It already belongs to a group.");
    CHECK(b.pieces()[static_cast<size_t>(gunner)].isLeader);
    CHECK(b.pieces()[static_cast<size_t>(gunner)].group == 3);
    CHECK(b.pieces()[static_cast<size_t>(boarder)].leader == gunner);
    CHECK(b.pieces()[static_cast<size_t>(boarder)].group == 3);
    // The member takes the formation's next place, turned by the leader's facing:
    // its first slot is one square to the leader's left before turning.
    const combat::TacticalPiece lead = b.pieces()[static_cast<size_t>(gunner)];
    TacticalOrder mv = order(OK::Move, a, gunner);
    const int sx = lead.x > 36 ? -1 : 1;   // toward the middle of the map: room to move
    mv.path = {combat::Square{static_cast<int16_t>(lead.x + sx), static_cast<int16_t>(lead.y)},
               combat::Square{static_cast<int16_t>(lead.x + 2 * sx), static_cast<int16_t>(lead.y)}};
    REQUIRE(b.check(mv).empty());
    CHECK(b.submit(mv).empty());
    const combat::TacticalPiece& l = b.pieces()[static_cast<size_t>(gunner)];
    const combat::TacticalPiece& m = b.pieces()[static_cast<size_t>(boarder)];
    CHECK(l.x == lead.x + 2 * sx);
    // Facing east (1) the slot (-1, 0) is (0, -1); facing west (3) it is (0, 1).
    const int slotY = l.y + (sx > 0 ? -1 : 1);
    CHECK(std::max(std::abs(l.x - m.x), std::abs(slotY - m.y)) <= 2);   // there, or as close as its movement allowed
    CHECK(m.movement < m.movementMax);
    // Clearing the leader clears only it: the member keeps its number and
    // follows whichever piece leads that number later.
    CHECK(b.submit(order(OK::ClearGroup, a, gunner)).empty());
    CHECK_FALSE(b.pieces()[static_cast<size_t>(gunner)].isLeader);
    CHECK(b.pieces()[static_cast<size_t>(boarder)].leader == -1);
    CHECK(b.pieces()[static_cast<size_t>(boarder)].group == 3);
    second.piece = carrier;
    CHECK(b.submit(second).empty());
    CHECK(b.pieces()[static_cast<size_t>(boarder)].leader == carrier);
    CHECK(b.submit(order(OK::ClearGroup, a, boarder)).empty());
    CHECK(b.check(order(OK::ClearGroup, a, boarder)) == "It is in no group.");
}

TEST_CASE("tactical: ramming and boarding need an adjacent target; boarding needs its shields down") {
    Skirmish k;
    TacticalBattle b = startSkirmish(k);
    const EmpireId a = k.ar.a;
    const int boarder = pieceIndex(b, k.boarder), target = pieceIndex(b, k.target), shielded = pieceIndex(b, k.shielded);
    CHECK(b.check(order(OK::Capture, a, pieceIndex(b, k.gunner), target)) == "It has no boarding parties.");
    CHECK(b.check(order(OK::DropTroops, a, boarder, target)) == "No colony of another empire is adjacent.");
    // Close on the shielded ship: boarding waits for its shields, ramming only for adjacency.
    for (int round = 0; round < 10 && b.distance(boarder, shielded) > 1 && b.phaseEmpire() == a; ++round) {
        if (b.distance(boarder, shielded) > 1) {
            CHECK(b.check(order(OK::Ram, a, boarder, shielded)) == "The target must be adjacent.");
            CHECK(b.check(order(OK::Capture, a, boarder, shielded)) == "The target must be adjacent.");
        }
        TacticalOrder mv = order(OK::Move, a, boarder);
        mv.x = b.pieces()[static_cast<size_t>(shielded)].x;
        mv.y = b.pieces()[static_cast<size_t>(shielded)].y;
        if (b.check(mv).empty()) b.submit(mv);
        if (b.distance(boarder, shielded) > 1) REQUIRE(b.submit(order(OK::EndPhase, a)).empty());
    }
    REQUIRE(b.phaseEmpire() == a);
    REQUIRE(b.distance(boarder, shielded) <= 1);
    REQUIRE(b.pieces()[static_cast<size_t>(shielded)].shields > 0);
    CHECK(b.check(order(OK::Capture, a, boarder, shielded)) == "Its shields must be down first.");
    if (b.pieces()[static_cast<size_t>(boarder)].movement > 0) CHECK(b.check(order(OK::Ram, a, boarder, shielded)).empty());
    CHECK(b.check(order(OK::Ram, a, boarder, pieceIndex(b, k.gunner))) == "Only an enemy ship, unit group or planet can be rammed.");
}

TEST_CASE("tactical: a boarding party takes an unshielded ship") {
    Arena ar = makeArena(3);
    GameState& s = ar.s;
    const VehicleId boarder = spawn(s, frigate(s, ar.a, "Boarder", 4, {"Test Boarding Party", "Test Boarding Party", "CT Combat Thruster"}), ar.loc);
    const VehicleId prize = spawn(s, design(s, ar.b, "Prize", "Test Station", {"Test Bridge", "Test Life Support", "Test Crew Quarters"}), ar.loc);
    TacticalBattle b(combatRules(), s, TacticalBattle::Setup{ar.loc, std::nullopt, {ar.a}});
    const int i = pieceIndex(b, boarder), t = pieceIndex(b, prize);
    for (int round = 0; round < 10 && b.awaitingOrders(); ++round) {
        REQUIRE(b.phaseEmpire() == ar.a);
        if (b.distance(i, t) > 1) {
            TacticalOrder mv = order(OK::Move, ar.a, i);
            mv.x = b.pieces()[static_cast<size_t>(t)].x;
            mv.y = b.pieces()[static_cast<size_t>(t)].y;
            if (b.check(mv).empty()) b.submit(mv);
        }
        if (b.distance(i, t) <= 1) {
            CHECK(b.submit(order(OK::Capture, ar.a, i, t)).empty());
            break;
        }
        b.submit(order(OK::EndPhase, ar.a));
    }
    CHECK(b.pieces()[static_cast<size_t>(t)].owner == ar.a);
    CHECK(b.pieces()[static_cast<size_t>(t)].captured);
    CHECK(b.over());
    b.submit(order(OK::EndPhase, ar.a));
    CHECK(b.finished());
    b.finish();
    CHECK(b.state().vehicle(prize)->owner == ar.a);
}

TEST_CASE("tactical: troops land on an adjacent enemy planet and fight at once") {
    Arena ar = makeArena(9);
    GameState& s = ar.s;
    Colony& colony = homeworld(s, ar.b);
    colony.population = {{ar.b, 10}};   // no militia below 20M
    colony.cargo = {};
    const Location there = locationOf(s.galaxy, colony.planet);
    const DesignId trooper = design(s, ar.a, "Trooper", "Test Troop Hull", {"Test Troop Rifle", "Test Troop Armor"});
    const VehicleId transport = spawn(s, frigate(s, ar.a, "Transport", 4, {"Test Cargo Bay", "Test Cargo Bay", "CT Combat Thruster"}), there);
    s.vehicle(transport)->cargo.units.push_back({trooper, 4});
    TacticalBattle b(combatRules(), s, TacticalBattle::Setup{there, std::nullopt, {ar.a}});
    REQUIRE(b.started());
    int planet = -1;
    for (size_t j = 0; j < b.pieces().size(); ++j)
        if (b.pieces()[j].kind == CombatPiece::Kind::Planet && b.pieces()[j].planet == colony.planet) planet = static_cast<int>(j);
    REQUIRE(planet >= 0);
    const int i = pieceIndex(b, transport);
    CHECK(b.pieces()[static_cast<size_t>(i)].troops);
    bool landed = false;
    for (int round = 0; round < 12 && b.awaitingOrders() && !landed; ++round) {
        REQUIRE(b.phaseEmpire() == ar.a);
        if (b.distance(i, planet) > 1) {
            CHECK(b.check(order(OK::DropTroops, ar.a, i, planet)) == "No colony of another empire is adjacent.");
            TacticalOrder mv = order(OK::Move, ar.a, i);
            mv.x = b.pieces()[static_cast<size_t>(planet)].x + 1;
            mv.y = b.pieces()[static_cast<size_t>(planet)].y + 1;
            if (b.check(mv).empty()) b.submit(mv);
        }
        if (b.distance(i, planet) <= 1) {
            CHECK(b.submit(order(OK::DropTroops, ar.a, i, planet)).empty());
            landed = true;
        } else {
            b.submit(order(OK::EndPhase, ar.a));
        }
    }
    REQUIRE(landed);
    CHECK_FALSE(b.pieces()[static_cast<size_t>(i)].troops);
    // The ground fight ran at once: ten million people, no militia, four troops.
    CHECK(b.pieces()[static_cast<size_t>(planet)].owner == ar.a);
    b.finish();
    CHECK(b.state().colony(colony.planet)->owner == ar.a);
}

TEST_CASE("tactical: Resolve Combat hands the side to its strategies for the rest of the battle") {
    auto [start, where] = battleScenario(0, 3);
    TacticalBattle b(combatRules(), start, TacticalBattle::Setup{where, std::nullopt, {EmpireId{0u}}});
    REQUIRE(b.awaitingOrders());
    CHECK(b.isPlayer(EmpireId{0u}));
    CHECK(b.submit(order(OK::ResolveCombat, EmpireId{0u})).empty());
    CHECK_FALSE(b.isPlayer(EmpireId{0u}));
    CHECK_FALSE(b.awaitingOrders());
    CHECK(b.finished());
    b.finish();
    // Resolve Combat in the launch step of the first phase is the strategic battle.
    CHECK(stateChecksum(b.state()) == stateChecksum(strategic(start, where)));
}

TEST_CASE("tactical: finishing early lets the strategies play the phases left, as a script that runs out") {
    auto [start, where] = battleScenario(7, 1);
    const TacticalBattle::Setup setup{where, std::nullopt, {EmpireId{0u}}};
    TacticalBattle b(combatRules(), start, setup);
    REQUIRE(b.awaitingOrders());
    REQUIRE(b.submit(order(OK::EndPhase, EmpireId{0u})).empty());
    b.finish();
    CHECK(b.applied());
    TacticalBattle replay(combatRules(), start, setup);
    for (const TacticalOrder& o : b.script()) CHECK(replay.submit(o).empty());
    replay.finish();
    CHECK(stateChecksum(replay.state()) == stateChecksum(b.state()));
    CHECK(replay.record().events.size() == b.record().events.size());
}

// ---- Turn-based games ---------------------------------------------------------------------------------

namespace {

// A turn-based game in which A's warship stands one sector from B's picket.
struct TurnBasedDuel {
    Arena ar = makeArena(13);
    VehicleId warship, picket;
    Location from, to;

    TurnBasedDuel() {
        GameState& s = ar.s;
        s.options.simultaneous = false;
        s.options.noTacticalCombat = false;
        for (Empire& e : s.empires) e.kind = PlayerKind::Human;
        to = ar.loc;
        from = {to.system, Sector{to.sector.x - 1, to.sector.y}};
        warship = spawn(s, frigate(s, ar.a, "Warship", 3, {"Test Laser", "Test Laser", "CT Big Armor"}), from);
        picket = spawn(s, frigate(s, ar.b, "Picket", 1, {"Test Laser", "Test Armor Plate"}), to);
        for (Empire& e : s.empires) std::fill(e.knowledge.explored.begin(), e.knowledge.explored.end(), 1);
        resumeTurnBased(combatRules(), s);
        REQUIRE(activePlayer(s) == ar.a);
    }

    // Moves the warship into the picket's sector: the Attack Sector question, then the answer.
    TurnResult attack(const std::vector<BattleAnswer>* answers) {
        GameState& s = ar.s;
        Order o;
        o.kind = OrderKind::MoveTo;
        o.location = to;
        const TurnResult asked = applyLive(combatRules(), s, ar.a, cmd::SetOrders{warship, {}, {o}}, answers);
        REQUIRE(asked.questions.size() == 1);
        return applyLive(combatRules(), s, ar.a, cmd::EnterSector{warship, {}, to, true}, answers);
    }
};

} // namespace

TEST_CASE("turn-based tactical: a battle with human sides asks, and the state stays as it was") {
    TurnBasedDuel d;
    const std::vector<BattleAnswer> none;
    GameState& s = d.ar.s;
    Order o;
    o.kind = OrderKind::MoveTo;
    o.location = d.to;
    REQUIRE(applyLive(combatRules(), s, d.ar.a, cmd::SetOrders{d.warship, {}, {o}}, &none).questions.size() == 1);
    const uint64_t before = stateChecksum(s);
    const TurnResult res = applyLive(combatRules(), s, d.ar.a, cmd::EnterSector{d.warship, {}, d.to, true}, &none);
    REQUIRE(res.battle.has_value());
    CHECK(stateChecksum(s) == before);
    CHECK(s.combats.empty());
    // The Attack Sector question kept in the game is still open after the rollback;
    // the call made again with an answer drops it, as the first one would have.
    REQUIRE(s.playerTurn.questions.size() == 1);
    CHECK(s.playerTurn.questions.front() == EntryQuestion{d.warship, {}, d.to});
    const BattleQuestion& q = *res.battle;
    CHECK(q.where == d.to);
    CHECK(q.humans == std::vector<EmpireId>{d.ar.a, d.ar.b});
    CHECK(q.participants == std::vector<EmpireId>{d.ar.a, d.ar.b});
    CHECK(q.index == 0);
    REQUIRE(q.state);
    REQUIRE(q.entering.has_value());
    CHECK(std::find(q.entering->begin(), q.entering->end(), d.warship) != q.entering->end());
    // The copy is the game as the battle begins: the warship has entered the sector.
    CHECK(q.state->vehicle(d.warship)->location == d.to);
    const std::vector<BattleAnswer> strategicAnswer{BattleAnswer{}};
    CHECK_FALSE(applyLive(combatRules(), s, d.ar.a, cmd::EnterSector{d.warship, {}, d.to, true}, &strategicAnswer).battle.has_value());
    CHECK(s.playerTurn.questions.empty());
    CHECK(s.combats.size() == 1);
}

TEST_CASE("turn-based tactical: a strategic answer fights the battle as if nobody was asked") {
    TurnBasedDuel asked, silent;
    const std::vector<BattleAnswer> strategicAnswer{BattleAnswer{}};
    const TurnResult res = asked.attack(&strategicAnswer);
    CHECK_FALSE(res.battle.has_value());
    silent.attack(nullptr);
    REQUIRE(asked.ar.s.combats.size() == 1);
    CHECK(stateChecksum(asked.ar.s) == stateChecksum(silent.ar.s));
}

TEST_CASE("turn-based tactical: a battle fought in the client applies exactly like its sandbox") {
    TurnBasedDuel d;
    const std::vector<BattleAnswer> none;
    GameState& s = d.ar.s;
    Order o;
    o.kind = OrderKind::MoveTo;
    o.location = d.to;
    applyLive(combatRules(), s, d.ar.a, cmd::SetOrders{d.warship, {}, {o}}, &none);
    const TurnResult asked = applyLive(combatRules(), s, d.ar.a, cmd::EnterSector{d.warship, {}, d.to, true}, &none);
    REQUIRE(asked.battle);
    const BattleQuestion& q = *asked.battle;

    // A fights tactically by hand; B leaves it to its strategies.
    TacticalBattle sandbox(combatRules(), *q.state, TacticalBattle::Setup{q.where, q.entering, {d.ar.a}});
    REQUIRE(sandbox.started());
    playByHand(sandbox, d.ar.a);
    REQUIRE(sandbox.applied());
    CHECK_FALSE(sandbox.script().empty());

    const std::vector<BattleAnswer> answers{BattleAnswer{{d.ar.a}, sandbox.script()}};
    const TurnResult res = applyLive(combatRules(), s, d.ar.a, cmd::EnterSector{d.warship, {}, d.to, true}, &answers);
    CHECK_FALSE(res.battle.has_value());
    REQUIRE(s.combats.size() == 1);
    const CombatRecord& real = s.combats.back();
    const CombatRecord& fought = sandbox.state().combats.back();
    REQUIRE(real.events.size() == fought.events.size());
    for (size_t i = 0; i < real.events.size(); ++i) {
        CHECK(real.events[i].kind == fought.events[i].kind);
        CHECK(real.events[i].piece == fought.events[i].piece);
        CHECK(real.events[i].target == fought.events[i].target);
        CHECK(real.events[i].amount == fought.events[i].amount);
        CHECK(real.events[i].x == fought.events[i].x);
    }
    CHECK(real.summary == fought.summary);
    // The results are the sandbox's: the same damage, supplies and losses.
    for (VehicleId id : {d.warship, d.picket}) {
        const Vehicle* after = s.vehicle(id);
        const Vehicle* inSandbox = sandbox.state().vehicle(id);
        REQUIRE(inSandbox);
        if (inSandbox->count <= 0) {
            CHECK(after == nullptr);
            continue;
        }
        REQUIRE(after);
        CHECK(after->damage == inSandbox->damage);
        CHECK(after->supply == inSandbox->supply);
        CHECK(after->owner == inSandbox->owner);
        CHECK(after->experience == inSandbox->experience);
    }
    // Both empires' logs report the battle.
    for (EmpireId e : {d.ar.a, d.ar.b})
        CHECK(std::any_of(s.empire(e).log.begin(), s.empire(e).log.end(), [](const LogEntry& l) { return l.title.starts_with("Battle at"); }));
}

TEST_CASE("turn-based tactical: No Tactical Combat and simultaneous games never ask") {
    {
        TurnBasedDuel d, silent;
        d.ar.s.options.noTacticalCombat = true;
        silent.ar.s.options.noTacticalCombat = true;
        const std::vector<BattleAnswer> none;
        const TurnResult res = d.attack(&none);
        CHECK_FALSE(res.battle.has_value());
        silent.attack(nullptr);
        REQUIRE(d.ar.s.combats.size() == 1);
        CHECK(stateChecksum(d.ar.s) == stateChecksum(silent.ar.s));
    }
    CHECK_FALSE(tacticalOffered(makeArena().s));   // simultaneous by default
    GameState s = makeArena().s;
    s.options.simultaneous = false;
    CHECK(tacticalOffered(s));
    s.options.noTacticalCombat = true;
    CHECK_FALSE(tacticalOffered(s));
}

TEST_CASE("turn-based tactical: a battle between computer players asks nobody") {
    TurnBasedDuel d;
    for (Empire& e : d.ar.s.empires) e.kind = PlayerKind::Computer;
    TurnContext::Battles battles{nullptr, 0};
    const std::vector<BattleAnswer> none;
    battles.answers = &none;
    TurnContext ctx = context(d.ar.s);
    ctx.battles = &battles;
    // The picket's sector with the warship moved in: fought at once, no question.
    d.ar.s.vehicle(d.warship)->location = d.to;
    CHECK_NOTHROW(combat::resolveSpaceCombat(ctx, d.to));
    CHECK(d.ar.s.combats.size() == 1);
}

TEST_CASE("turn-based tactical: answers are taken in the order the battles come up") {
    Arena ar = makeArena(17);
    GameState& s = ar.s;
    s.options.simultaneous = false;
    for (Empire& e : s.empires) e.kind = PlayerKind::Human;
    const Location second{ar.loc.system, Sector{ar.loc.sector.x, ar.loc.sector.y + 1}};
    spawn(s, frigate(s, ar.a, "First A", 2, {"Test Laser"}), ar.loc);
    spawn(s, frigate(s, ar.b, "First B", 2, {"Test Laser"}), ar.loc);
    spawn(s, frigate(s, ar.a, "Second A", 2, {"Test Laser"}), second);
    spawn(s, frigate(s, ar.b, "Second B", 2, {"Test Laser"}), second);
    const std::vector<BattleAnswer> one{BattleAnswer{}};
    TurnContext::Battles battles{&one, 0};
    TurnContext ctx = context(s);
    ctx.battles = &battles;
    combat::resolveSpaceCombat(ctx, ar.loc);   // takes the first answer
    CHECK(battles.next == 1);
    CHECK(s.combats.size() == 1);
    bool asked = false;
    try {
        combat::resolveSpaceCombat(ctx, second);
    } catch (const game::detail::BattleQuestionRaised& q) {
        asked = true;
        CHECK(q.question.index == 1);
        CHECK(q.question.where == second);
        REQUIRE(q.question.state);
        CHECK(q.question.state->combats.size() == 1);   // the game as the second battle begins
    }
    CHECK(asked);
}

// ---- The combat simulator -----------------------------------------------------------------------------

namespace {

struct SimWorld {
    Arena ar = makeArena(19);
    DesignId lancer, raider, unseen, fighter, platform, mine;

    SimWorld() {
        GameState& s = ar.s;
        lancer = frigate(s, ar.a, "Lancer", 3, {"Test Laser", "Test Laser", "Test Armor Plate"});
        fighter = design(s, ar.a, "Wasp", "Test Fighter Hull", {"Test Fighter Engine", "Test Fighter Gun", "CT Fighter Fuel"});
        platform = design(s, ar.a, "Bastion", "CT Platform Hull", {"CT Platform Gun", "CT Platform Core"});
        mine = design(s, ar.a, "Mine", "Test Mine Hull", {"Test Warhead"});
        raider = frigate(s, ar.b, "Raider", 3, {"CT Torpedo", "Test Laser", "Test Armor Plate"});
        unseen = frigate(s, ar.b, "Secret", 3, {"Test Laser"});
        seeDesign(s.empire(ar.a).knowledge, raider, s.turn);
    }

    combat::SimulatorSetup duel(bool playerSide) const {
        combat::SimulatorSetup setup;
        setup.viewer = ar.a;
        setup.sides = {{"Blue", !playerSide}, {"Red", true}};
        setup.items.push_back({combat::SimulatorItem::Kind::Design, lancer, {}, 0, 2});
        setup.items.push_back({combat::SimulatorItem::Kind::Design, raider, {}, 1, 2});
        return setup;
    }
};

} // namespace

TEST_CASE("simulator: the designs and planets on offer") {
    SimWorld w;
    const auto designs = combat::simulatorDesigns(combatRules(), w.ar.s, w.ar.a, false);
    CHECK(std::find(designs.begin(), designs.end(), w.lancer) != designs.end());
    CHECK(std::find(designs.begin(), designs.end(), w.fighter) != designs.end());
    CHECK(std::find(designs.begin(), designs.end(), w.raider) != designs.end());    // seen
    CHECK(std::find(designs.begin(), designs.end(), w.unseen) == designs.end());    // never seen
    CHECK(std::find(designs.begin(), designs.end(), w.mine) == designs.end());      // no minefields
    CHECK(std::find(designs.begin(), designs.end(), w.platform) == designs.end());  // cargo only
    w.ar.s.design(w.lancer).obsolete = true;
    const auto current = combat::simulatorDesigns(combatRules(), w.ar.s, w.ar.a, true);
    CHECK(std::find(current.begin(), current.end(), w.lancer) == current.end());
    const auto cargo = combat::simulatorCargoDesigns(combatRules(), w.ar.s, w.ar.a, false);
    CHECK(std::find(cargo.begin(), cargo.end(), w.fighter) != cargo.end());
    CHECK(std::find(cargo.begin(), cargo.end(), w.platform) != cargo.end());
    CHECK(std::find(cargo.begin(), cargo.end(), w.mine) == cargo.end());
    const auto planets = combat::simulatorPlanets(w.ar.s, w.ar.a);
    REQUIRE_FALSE(planets.empty());
    CHECK(std::find(planets.begin(), planets.end(), homeworld(w.ar.s, w.ar.a).planet) != planets.end());
    CHECK(std::find(planets.begin(), planets.end(), homeworld(w.ar.s, w.ar.b).planet) == planets.end());
}

TEST_CASE("simulator: setups that cannot be fought") {
    SimWorld w;
    const Rules& r = combatRules();
    const GameState& s = w.ar.s;
    CHECK(combat::simulatorProblem(r, s, w.duel(false)).empty());
    combat::SimulatorSetup lonely = w.duel(false);
    lonely.items[1].side = 0;
    CHECK(combat::simulatorProblem(r, s, lonely) == "At least two sides need something to fight with.");
    combat::SimulatorSetup secret = w.duel(false);
    secret.items[1].design = w.unseen;
    CHECK(combat::simulatorProblem(r, s, secret) == "Only your designs and enemy designs you have seen can be used.");
    combat::SimulatorSetup mines = w.duel(false);
    mines.items[0].design = w.mine;
    CHECK(combat::simulatorProblem(r, s, mines) == "Minefields cannot be added.");
    combat::SimulatorSetup crammed = w.duel(false);
    crammed.items[0].cargo = {{w.fighter, 50}};   // a frigate without cargo space
    CHECK(combat::simulatorProblem(r, s, crammed) == "The cargo does not fit.");
    combat::SimulatorSetup elsewhere = w.duel(false);
    elsewhere.items.push_back({combat::SimulatorItem::Kind::Planet, {}, homeworld(w.ar.s, w.ar.b).planet, 1});
    CHECK(combat::simulatorProblem(r, s, elsewhere) == "Sample planets come from the home system.");
    combat::SimulatorSetup oneSide = w.duel(false);
    oneSide.sides.resize(1);
    CHECK(combat::simulatorProblem(r, s, oneSide) == "A battle needs at least two sides.");
}

TEST_CASE("simulator: the battle is fought on a sandbox and the real game never changes") {
    SimWorld w;
    const Rules& r = combatRules();
    const uint64_t before = stateChecksum(w.ar.s);
    for (const bool player : {false, true}) {
        CAPTURE(player);
        combat::SimulatorSetup setup = w.duel(player);
        setup.items.push_back({combat::SimulatorItem::Kind::Planet, {}, homeworld(w.ar.s, w.ar.a).planet, 0, 1, {{w.platform, 3}}, true});
        setup.fleets.push_back({0, "Home Guard", 0, 0});
        setup.items[0].fleet = 0;
        REQUIRE(combat::simulatorProblem(r, w.ar.s, setup).empty());
        combat::Simulation sim = combat::buildSimulation(r, w.ar.s, setup);
        REQUIRE(sim.sides.size() == 2);
        const GameState& sb = sim.state;
        // Virtual empires, at war with each other, copies of the viewer's.
        CHECK(sb.empires.size() == w.ar.s.empires.size() + 2);
        CHECK(sb.empire(sim.sides[0]).name == "Blue");
        CHECK(sb.empire(sim.sides[0]).race.name == w.ar.s.empire(w.ar.a).race.name);
        CHECK(hostile(sb, sim.sides[0], sim.sides[1]));
        CHECK(sim.players == (player ? std::vector<EmpireId>{sim.sides[0]} : std::vector<EmpireId>{}));
        // A new, empty system holds the battle: two ships a side, the planet, the fleet.
        CHECK(sim.where.system.index() == w.ar.s.galaxy.systems.size());
        int ships = 0;
        for (const Vehicle& v : sb.vehicles)
            if (v.location == sim.where) {
                ++ships;
                CHECK((v.owner == sim.sides[0] || v.owner == sim.sides[1]));
                CHECK(sb.design(v.design).owner == v.owner);
                CHECK(v.supply > 0);
            }
        CHECK(ships == 4);
        const auto planets = planetsAt(sb, sim.where);
        REQUIRE(planets.size() == 1);
        CHECK(sb.colony(planets[0])->owner == sim.sides[0]);
        CHECK(sb.colony(planets[0])->cargo.units.size() == 1);
        CHECK(sb.design(sb.colony(planets[0])->cargo.units[0].design).owner == sim.sides[0]);
        REQUIRE(sb.fleets.size() == w.ar.s.fleets.size() + 1);
        CHECK(sb.fleets.back().members.size() == 2);

        combat::TacticalBattle battle = combat::startSimulation(r, std::move(sim));
        REQUIRE(battle.started());
        CHECK(battle.participants().size() == 2);
        if (player) {
            CHECK(battle.awaitingOrders());
            playByHand(battle, battle.phaseEmpire());
        } else {
            CHECK(battle.finished());
            battle.finish();
        }
        CHECK(battle.applied());
        REQUIRE_FALSE(battle.state().combats.empty());
        CHECK_FALSE(battle.record().events.empty());
        CHECK(stateChecksum(w.ar.s) == before);
    }
}

TEST_CASE("simulator: the same setup and seed give the same battle") {
    SimWorld w;
    combat::SimulatorSetup setup = w.duel(false);
    setup.seed = 1234;
    auto run = [&] {
        combat::TacticalBattle b = combat::startSimulation(combatRules(), combat::buildSimulation(combatRules(), w.ar.s, setup));
        b.finish();
        return stateChecksum(b.state());
    };
    CHECK(run() == run());
}

// ---- The client session (client/classic/session.hpp) ----------------------------------------------------

TEST_CASE("client session: a turn-based battle asks, is fought tactically in the client, and the game carries on") {
    // A local turn-based game: A human attacker, B a computer player.
    Arena ar = makeArena(23);
    GameState& s = ar.s;
    s.options.simultaneous = false;
    s.empire(ar.a).kind = PlayerKind::Human;
    s.empire(ar.b).kind = PlayerKind::Computer;
    const Location to = ar.loc, from{to.system, Sector{to.sector.x - 1, to.sector.y}};
    const VehicleId warship = spawn(s, frigate(s, ar.a, "Warship", 3, {"Test Laser", "Test Laser", "CT Big Armor"}), from);
    spawn(s, frigate(s, ar.b, "Picket", 1, {"Test Laser", "Test Armor Plate"}), to);
    for (Empire& e : s.empires) std::fill(e.knowledge.explored.begin(), e.knowledge.explored.end(), 1);
    auto rules = std::make_shared<const Rules>(buildCombatRuleset());
    client::classic::ClassicSession session(rules, std::move(s), ar.a, client::classic::SessionKind::Local);
    REQUIRE(session.player() == ar.a);

    Order o;
    o.kind = OrderKind::MoveTo;
    o.location = to;
    CHECK(session.issue(cmd::SetOrders{warship, {}, {o}}).ok);
    REQUIRE(session.questions().size() == 1);   // Attack Sector?
    const uint64_t before = stateChecksum(session.state());
    session.answer(true);
    REQUIRE(session.battleQuestion().has_value());
    CHECK(stateChecksum(session.state()) == before);   // nothing happened yet
    CHECK(session.issue(cmd::SetOrders{warship, {}, {}}).error == "A battle waits to be fought first.");

    // The player fights it in the Tactical Combat window.
    const BattleQuestion q = *session.battleQuestion();
    REQUIRE(q.humans == std::vector<EmpireId>{ar.a});
    client::classic::TacticalFight fight;
    fight.kind = client::classic::TacticalFight::Kind::Game;
    fight.battle = std::make_unique<TacticalBattle>(*rules, *q.state, TacticalBattle::Setup{q.where, q.entering, q.humans});
    fight.players = q.humans;
    session.startTactical(std::move(fight));
    TacticalBattle& b = *session.tactical()->battle;
    REQUIRE(b.awaitingOrders());
    CHECK(b.submit(order(OK::EndPhase, ar.a)).empty());
    session.endTactical();   // the strategies play what is left; the orders answer the question
    CHECK(session.tactical() == nullptr);
    CHECK_FALSE(session.battleQuestion().has_value());
    REQUIRE(session.state().combats.size() == 1);
    CHECK(session.takeStrategicBattles().empty());   // seen already in the window
    // The order went through: the warship entered the sector (if it survived).
    if (const Vehicle* v = session.state().vehicle(warship)) CHECK(v->location == to);
    CHECK(session.ordersThisTurn().size() == 2);
}

TEST_CASE("client session: simultaneous games show their battles when the Settings flag asks") {
    for (const bool show : {false, true}) {
        CAPTURE(show);
        Arena ar = makeArena(31);
        GameState& s = ar.s;
        s.options.simultaneous = true;
        s.empire(ar.a).kind = PlayerKind::Human;
        s.empire(ar.b).kind = PlayerKind::Computer;
        const Location to = ar.loc, from{to.system, Sector{to.sector.x - 1, to.sector.y}};
        const VehicleId warship = spawn(s, frigate(s, ar.a, "Warship", 3, {"Test Laser", "CT Big Armor"}), from);
        spawn(s, frigate(s, ar.b, "Picket", 1, {"Test Laser"}), to);
        for (Empire& e : s.empires) std::fill(e.knowledge.explored.begin(), e.knowledge.explored.end(), 1);
        ruleset::Ruleset rs = buildCombatRuleset();
        rs.settings.set("Simultaneous Games Show Strategic Combat", show ? "TRUE" : "FALSE");
        client::classic::ClassicSession session(std::make_shared<const Rules>(std::move(rs)), std::move(s), ar.a,
                                                client::classic::SessionKind::Local);
        Order o;
        o.kind = OrderKind::MoveTo;
        o.location = to;
        REQUIRE(session.issue(cmd::SetOrders{warship, {}, {o}}).ok);
        CHECK(session.takeStrategicBattles().empty());   // orders only: nothing is fought yet
        session.endTurn();
        REQUIRE_FALSE(session.state().combats.empty());
        const std::vector<size_t> shown = session.takeStrategicBattles();
        CHECK(shown.empty() != show);
        for (size_t i : shown) CHECK(i < session.state().combats.size());
    }
}

TEST_CASE("client session: a strategic answer, and a game without tactical combat, fight at once") {
    for (const bool offered : {true, false}) {
        CAPTURE(offered);
        Arena ar = makeArena(29);
        GameState& s = ar.s;
        s.options.simultaneous = false;
        s.options.noTacticalCombat = !offered;
        s.empire(ar.a).kind = PlayerKind::Human;
        s.empire(ar.b).kind = PlayerKind::Computer;
        const Location to = ar.loc, from{to.system, Sector{to.sector.x - 1, to.sector.y}};
        const VehicleId warship = spawn(s, frigate(s, ar.a, "Warship", 3, {"Test Laser", "CT Big Armor"}), from);
        spawn(s, frigate(s, ar.b, "Picket", 1, {"Test Laser"}), to);
        for (Empire& e : s.empires) std::fill(e.knowledge.explored.begin(), e.knowledge.explored.end(), 1);
        client::classic::ClassicSession session(std::make_shared<const Rules>(buildCombatRuleset()), std::move(s), ar.a,
                                                client::classic::SessionKind::Local);
        Order o;
        o.kind = OrderKind::MoveTo;
        o.location = to;
        session.issue(cmd::SetOrders{warship, {}, {o}});
        session.answer(true);
        CHECK(session.battleQuestion().has_value() == offered);
        if (offered) session.answerBattle(BattleAnswer{});
        CHECK_FALSE(session.battleQuestion().has_value());
        CHECK(session.state().combats.size() == 1);
        CHECK(session.takeStrategicBattles() == std::vector<size_t>{0});   // watched in the Strategic Combat window
        CHECK(session.takeStrategicBattles().empty());
    }
}
