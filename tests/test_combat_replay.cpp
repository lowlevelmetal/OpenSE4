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

// A Hit marked with what it did (CombatEvent::flags).
CombatEvent hitting(CombatEvent e, uint8_t flags) {
    e.flags = flags;
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
        hitting(ev(K::Hit, 1, 0, 1, 0, 0, 12), CombatEvent::kStructure),
        ev(K::Destroyed, 5, 2),  // out of order on purpose
        ev(K::Launch, 2, 2, 0, 5, 4),
        ev(K::Seeker, 2, 3, 0, 7, 5),
        ev(K::Fire, 2, 1, 0),
        ev(K::Miss, 2, 1, 0),
        ev(K::Seeker, 3, 3, 0, 5, 5),
        hitting(ev(K::Hit, 3, 3, 0, 0, 0, 20), CombatEvent::kStructure),
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

TEST_CASE("replay: timed playback draws every frame and waits as the original does (spec 06 §1.10.3, §7 Q77)") {
    using opense4::client::classic::AnimationFrame;
    using opense4::client::classic::CombatPace;
    using opense4::client::classic::kCombatTick;
    using Part = AnimationFrame::Part;
    const CombatRecord r = battle();
    CombatPlayback p(r);
    CombatPace pace;
    pace.beams = {0};   // component 0 is a torpedo
    p.setPace(pace);
    CHECK_FALSE(p.playing());
    p.advance(100.0f);  // paused: nothing happens
    CHECK(p.atStart());

    // Ship 0 faces its enemy (east) and moves two squares east: no turn, then
    // 36 frames a square of 1 px.
    const std::vector<AnimationFrame> move = p.framesOf(0);
    REQUIRE(move.size() == 72);
    CHECK(move[0].part == Part::Slide);
    // Every 1 ms or 10 ms wait lasts one 16 ms step of the tick counter.
    CHECK(move[0].wait == doctest::Approx(kCombatTick));
    CHECK(p.eventWait(0) == doctest::Approx(72 * kCombatTick));

    p.play();
    CHECK(p.playing());
    REQUIRE(p.animating() != nullptr);
    CHECK(p.animating()->kind == K::Move);
    REQUIRE(p.frame() != nullptr);
    CHECK(p.frame()->part == Part::Slide);
    CHECK(p.frame()->step == 0);
    // The first frame is drawn before anything moves on, and then one frame a
    // refresh at most, however long the refresh took.
    p.advance(100.0f);
    CHECK(p.frame()->step == 0);
    p.advance(100.0f);
    CHECK(p.frame()->step == 1);
    // A frame waits a tick: a shorter refresh keeps it on screen.
    p.advance(0.010f);
    CHECK(p.frame()->step == 1);
    p.advance(0.007f);
    CHECK(p.frame()->step == 2);
    for (int k = 3; k <= int(move.size()); ++k) p.advance(0.02f);
    CHECK(p.cursor() == 1);   // the last frame done: the move is applied
    CHECK(p.pieces()[0].x == 4);

    // A hit on structure: 8 explosion frames and the wipe, 0.1 s each, so 7 ticks each.
    std::vector<AnimationFrame> hit = p.framesOf(3);
    REQUIRE(hit.size() == 9);
    CHECK(hit[0].part == Part::Explosion);
    CHECK(hit[7].step == 7);
    CHECK(hit[8].part == Part::Wipe);
    CHECK(p.eventWait(3) == doctest::Approx(9 * 7 * kCombatTick));
    // A torpedo flies 9 frames a square; a beam is 6 stamps a square drawn, then erased.
    while (p.cursor() < 2) p.advance(1.0f);
    std::vector<AnimationFrame> torpedo = p.framesOf(2);
    REQUIRE_FALSE(torpedo.empty());
    CHECK(torpedo.front().part == Part::Torpedo);
    CHECK(int(torpedo.size()) == 4 * 9);   // ships at x 4 and 8
    CHECK(torpedo.front().wait == doctest::Approx(kCombatTick));
    pace.beams = {1};
    p.setPace(pace);
    std::vector<AnimationFrame> beam = p.framesOf(2);
    REQUIRE(beam.size() == 2 * 4 * 6);
    CHECK(beam[0].part == Part::Beam);
    CHECK(beam[23].part == Part::Beam);
    CHECK(beam[24].part == Part::BeamErase);
    // A seeking weapon's launch is not animated.
    pace.seekers = {1};
    p.setPace(pace);
    CHECK(p.framesOf(2).empty());
    pace.seekers = {0};
    p.setPace(pace);
    // Misses, losses, captures, launches and lost units have no frames of their own.
    CHECK(p.event(7).kind == K::Miss);
    CHECK(p.framesOf(7).empty());
    CHECK(p.event(9).kind == K::Hit);
    CHECK(p.event(10).kind == K::Destroyed);
    CHECK(p.framesOf(10).empty());   // the seeker's own end
    CHECK(p.event(11).kind == K::Captured);
    CHECK(p.framesOf(11).empty());
    CHECK(p.event(4).kind == K::Launch);
    CHECK(p.framesOf(4).empty());
    // The seeker's impact on a survivor: the 0.3 s pause, in Tactical Combat only.
    CHECK(p.framesOf(9).size() == 9);
    pace.tactical = true;
    p.setPace(pace);
    REQUIRE(p.framesOf(9).size() == 10);
    CHECK(p.framesOf(9).back().part == Part::AfterHit);
    CHECK(p.framesOf(9).back().wait == doctest::Approx(19 * kCombatTick));
    CHECK(p.framesOf(3).size() == 9);   // a direct-fire hit has no pause

    // Movement not animated: the piece jumps, then 0.1 s, also off-screen;
    // animated, a move across the edge of the shown map has no frame.
    {
        CombatPlayback q(r);
        CombatPace still;
        still.animateMoves = false;
        still.inView = [](int, int) { return false; };
        q.setPace(still);
        const std::vector<AnimationFrame> jump = q.framesOf(0);
        REQUIRE(jump.size() == 1);
        CHECK(jump[0].part == Part::Jump);
        CHECK(q.eventWait(0) == doctest::Approx(7 * kCombatTick));
        CombatPace off;
        off.inView = [](int x, int) { return x < 3; };
        q.setPace(off);
        CHECK(q.framesOf(0).empty());
    }
    // A hit the shields took: one shield picture, no wait; none with Fast.
    {
        CombatRecord shields = battle();
        shields.events[3].flags = 0;
        CombatPlayback q(shields);
        q.setPace(CombatPace{});
        const std::vector<AnimationFrame> f = q.framesOf(3);
        REQUIRE(f.size() == 1);
        CHECK(f[0].part == Part::Shield);
        CHECK(f[0].wait == 0.0f);
        CombatPace fast;
        fast.fast = true;
        q.setPace(fast);
        CHECK(q.framesOf(3).empty());
    }
    // Fast Tactical Combat: no wait at all, every frame still there.
    pace.fast = true;
    pace.animateMoves = true;
    pace.tactical = false;
    p.setPace(pace);
    CHECK(p.eventWait(3) == 0.0f);
    CHECK(p.framesOf(3).size() == 9);

    // To the end: one frame a refresh with no waits.
    for (int k = 0; k < 2000 && p.playing(); ++k) p.advance(0.0f);
    CHECK(p.atEnd());
    CHECK_FALSE(p.playing());
    CHECK(p.animating() == nullptr);

    // Play at the end starts again from the beginning.
    p.play();
    CHECK(p.atStart());
    CHECK(p.playing());
    p.pause();
    CHECK_FALSE(p.playing());
}

TEST_CASE("replay: a piece turns to its new facing 5 degrees a frame, 9 frames per 45, before it moves") {
    using opense4::client::classic::AnimationFrame;
    using opense4::client::classic::kCombatTick;
    CombatRecord r;
    r.participants = {EmpireId{0u}, EmpireId{1u}};
    r.pieces = {piece(CombatPiece::Kind::Vehicle, 0, 2, 5), piece(CombatPiece::Kind::Vehicle, 1, 10, 5)};
    // Facing east, it goes north (a quarter turn), then back south (a half-turn).
    r.events = {ev(K::Move, 1, 0, 0, 2, 4), ev(K::Move, 1, 0, 0, 2, 5)};
    CombatPlayback p(r);
    const std::vector<AnimationFrame> f = p.framesOf(0);
    REQUIRE(f.size() == 18 + 36);
    CHECK(f[0].part == AnimationFrame::Part::Turn);
    CHECK(f[0].steps == 18);
    CHECK(f[0].wait == doctest::Approx(kCombatTick));
    CHECK(f[18].part == AnimationFrame::Part::Slide);
    p.stepEvent();
    CHECK(p.framesOf(1).size() == 36 + 36);   // a half-turn: 36 frames
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
