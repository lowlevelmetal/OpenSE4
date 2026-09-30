// Combat Replay playback (client/classic/replay.hpp) on a synthetic battle.

#include "client/classic/replay.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using namespace opense4;
using namespace opense4::game;
using opense4::client::classic::CombatPlayback;

namespace {

using K = CombatEvent::Kind;

CombatEvent ev(K kind, uint8_t round, uint32_t piece, uint32_t target = 0, int x = 0, int y = 0, int amount = 0) {
    CombatEvent e;
    e.kind = kind;
    e.round = round;
    e.piece = piece;
    e.target = target;
    e.x = static_cast<int16_t>(x);
    e.y = static_cast<int16_t>(y);
    e.amount = amount;
    return e;
}

CombatPiece piece(CombatPiece::Kind kind, uint32_t owner, int x, int y) {
    CombatPiece p;
    p.kind = kind;
    p.owner = EmpireId{owner};
    p.startX = static_cast<int16_t>(x);
    p.startY = static_cast<int16_t>(y);
    return p;
}

// Two ships (empires 0 and 1), a fighter group launched by ship 0 and a seeker
// of empire 1. Rounds 1, 2, 3 and 5 have events; round 4 has none. One event of
// round 5 is recorded out of order to check that playback sorts by round.
CombatRecord battle() {
    CombatRecord r;
    r.participants = {EmpireId{0u}, EmpireId{1u}};
    r.pieces = {piece(CombatPiece::Kind::Vehicle, 0, 2, 5), piece(CombatPiece::Kind::Vehicle, 1, 10, 5),
                piece(CombatPiece::Kind::UnitGroup, 0, 0, 0), piece(CombatPiece::Kind::Seeker, 1, 0, 0)};
    r.events = {
        ev(K::Move, 1, 0, 0, 4, 5),
        ev(K::Move, 1, 1, 0, 8, 5),
        ev(K::Fire, 1, 0, 1),
        ev(K::Hit, 1, 0, 1, 0, 0, 12),
        ev(K::Destroyed, 5, 2),  // out of order on purpose
        ev(K::Launch, 2, 2, 0, 5, 4),
        ev(K::Seeker, 2, 3, 0, 7, 5),
        ev(K::Fire, 2, 1, 0),
        ev(K::Miss, 2, 1, 0),
        ev(K::Seeker, 3, 3, 0, 5, 5),
        ev(K::Hit, 3, 3, 0, 0, 0, 20),
        ev(K::Destroyed, 3, 3),
        ev(K::Captured, 3, 1, 0),
    };
    return r;
}

} // namespace

TEST_CASE("replay: start state, rounds and bounds") {
    const CombatRecord r = battle();
    CombatPlayback p(r);
    CHECK(p.eventCount() == 13);
    CHECK(p.roundCount() == 5);
    CHECK(p.round() == 0);
    CHECK(p.atStart());
    CHECK_FALSE(p.atEnd());
    // Events play in round order; the out-of-order round 5 event comes last.
    CHECK(p.event(4).kind == K::Launch);
    CHECK(p.event(12).kind == K::Destroyed);
    CHECK(p.event(12).round == 5);
    CHECK(p.roundStart(1) == 0);
    CHECK(p.roundStart(2) == 4);
    CHECK(p.roundStart(3) == 8);
    CHECK(p.roundStart(4) == 12);
    CHECK(p.roundStart(5) == 12);
    CHECK(p.roundStart(6) == 13);

    const auto& pc = p.pieces();
    REQUIRE(pc.size() == 4);
    CHECK(pc[0].onMap);
    CHECK(pc[0].x == 2);
    CHECK(pc[1].onMap);
    CHECK_FALSE(pc[2].onMap);  // launched later
    CHECK_FALSE(pc[3].onMap);  // seekers enter with their first event
    // Ships start facing the other side: 0 faces right, 1 faces left.
    CHECK(pc[0].heading == doctest::Approx(std::numbers::pi / 2));
    CHECK(pc[1].heading == doctest::Approx(-std::numbers::pi / 2));

    CHECK(p.bounds().minX == 2);
    CHECK(p.bounds().maxX == 10);
    CHECK(p.bounds().minY == 4);
    CHECK(p.bounds().maxY == 5);
}

TEST_CASE("replay: stepping by round forward and back") {
    const CombatRecord r = battle();
    CombatPlayback p(r);
    const auto& pc = p.pieces();

    p.stepRound();
    CHECK(p.round() == 1);
    CHECK(p.cursor() == 4);
    CHECK(pc[0].x == 4);
    CHECK(pc[0].fromX == 2);
    CHECK(pc[1].x == 8);
    CHECK(pc[1].damage == 12);
    CHECK(pc[0].shots == 1);

    p.stepRound();
    CHECK(p.round() == 2);
    CHECK(pc[2].onMap);
    CHECK(pc[2].x == 5);
    CHECK(pc[2].y == 4);
    CHECK(pc[3].onMap);
    CHECK(pc[3].x == 7);
    CHECK(pc[0].damage == 0);  // the shot missed

    p.stepRound();
    CHECK(p.round() == 3);
    CHECK(pc[3].destroyed);
    CHECK_FALSE(pc[3].onMap);
    CHECK(pc[0].damage == 20);
    CHECK(pc[1].captured);
    CHECK(pc[1].owner == EmpireId{0u});

    // Round 4 has no events: the next step ends round 5 and the battle.
    p.stepRound();
    CHECK(p.round() == 5);
    CHECK(p.atEnd());
    CHECK(pc[2].destroyed);
    p.stepRound();  // no-op at the end
    CHECK(p.atEnd());

    p.stepBackRound();
    CHECK(p.round() == 3);
    CHECK(p.cursor() == 12);
    CHECK(pc[2].onMap);
    CHECK(pc[1].owner == EmpireId{0u});

    p.stepBackRound();
    p.stepBackRound();
    CHECK(p.round() == 1);
    p.stepBackRound();
    CHECK(p.atStart());
    CHECK(pc[1].owner == EmpireId{1u});
    CHECK(pc[1].damage == 0);

    // Seeking and single events give the same states as stepping.
    p.seekRound(2);
    CHECK(p.cursor() == 8);
    CHECK(pc[3].onMap);
    p.rewind();
    for (int i = 0; i < 5; ++i) p.stepEvent();
    CHECK(p.cursor() == 5);
    CHECK(p.round() == 2);
    CHECK(pc[2].onMap);
    CHECK_FALSE(pc[3].onMap);
}

TEST_CASE("replay: timed playback") {
    const CombatRecord r = battle();
    CombatPlayback p(r);
    CHECK_FALSE(p.playing());
    p.advance(100.0f);  // paused: nothing happens
    CHECK(p.atStart());

    p.play();
    CHECK(p.playing());
    REQUIRE(p.animating() != nullptr);
    CHECK(p.animating()->kind == K::Move);
    p.advance(p.eventDuration(0) * 0.5f);
    CHECK(p.cursor() == 0);
    CHECK(p.fraction() == doctest::Approx(0.5f));

    // Exactly the first round's events.
    float round1 = 0;
    for (size_t i = 0; i < 4; ++i) round1 += p.eventDuration(i);
    p.advance(round1 - p.eventDuration(0) * 0.5f + 0.001f);
    CHECK(p.cursor() == 4);
    CHECK(p.round() == 1);
    // A new round starts with a short pause.
    CHECK(p.eventDuration(4) > p.eventDuration(3) - 0.2f);

    // Double speed halves the time.
    p.setSpeed(2.0f);
    const float next = p.eventDuration(4);
    p.advance(next * 0.5f + 0.001f);
    CHECK(p.cursor() == 5);

    p.advance(1000.0f);
    CHECK(p.atEnd());
    CHECK_FALSE(p.playing());
    CHECK(p.animating() == nullptr);

    // Play at the end starts again from the beginning.
    p.play();
    CHECK(p.atStart());
    CHECK(p.playing());
    p.pause();
    CHECK_FALSE(p.playing());

    p.setSpeed(100.0f);
    CHECK(p.speed() == doctest::Approx(8.0f));
}

TEST_CASE("replay: shots, hits and odd records") {
    const CombatRecord r = battle();
    CombatPlayback p(r);
    CHECK(p.followsFire(3));        // round 1 hit follows its fire
    CHECK(p.followsFire(7));        // round 2 miss follows its fire
    CHECK_FALSE(p.followsFire(9));  // the seeker's hit has no fire event
    CHECK_FALSE(p.followsFire(0));

    // An empty record and events naming missing pieces are harmless.
    CombatRecord empty;
    CombatPlayback e(empty);
    CHECK(e.roundCount() == 0);
    CHECK(e.atEnd());
    e.play();
    CHECK_FALSE(e.playing());
    e.stepRound();
    e.stepBackRound();
    CHECK(e.round() == 0);

    CombatRecord bad = battle();
    bad.events.push_back(ev(K::Hit, 6, 99, 42, 0, 0, 5));
    bad.events.push_back(ev(K::Launch, 6, 0, 77, 1, 1));
    CombatPlayback b(bad);
    b.seekEvent(b.eventCount());
    CHECK(b.atEnd());
    CHECK(b.round() == 6);
}

TEST_CASE("replay: planets and obstacles cover 4x4 squares; a lost colony stays on the map") {
    CombatRecord r;
    r.participants = {EmpireId{0u}, EmpireId{1u}};
    CombatPiece star = piece(CombatPiece::Kind::Obstacle, 0, 40, 40);
    star.owner = EmpireId{};
    r.pieces = {piece(CombatPiece::Kind::Vehicle, 0, 30, 31), piece(CombatPiece::Kind::Planet, 1, 37, 29), star};
    r.events = {ev(K::Fire, 1, 0, 1), ev(K::Hit, 1, 0, 1, 30, 31, 100), ev(K::Destroyed, 1, 1, 0, 37, 29)};
    CombatPlayback p(r);
    const auto& pc = p.pieces();
    CHECK(pc[0].size == 1);
    CHECK(pc[1].size == 4);
    CHECK(pc[2].size == 4);
    CHECK(pc[2].neutral);
    CHECK_FALSE(pc[1].neutral);
    // The bounds hold the whole footprint of the big pieces.
    CHECK(p.bounds().minX == 30);
    CHECK(p.bounds().maxX == 43);
    CHECK(p.bounds().maxY == 43);
    // The ship faces the planet, not the neutral star.
    CHECK(pc[0].heading == doctest::Approx(std::atan2(7.0f, 2.0f)));
    p.seekEvent(p.eventCount());
    CHECK(pc[1].onMap);   // the planet stays as an unowned obstacle
    CHECK(pc[1].neutral);
    CHECK_FALSE(pc[1].owner.valid());
    CHECK_FALSE(pc[1].destroyed);
}
