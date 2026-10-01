#pragma once

// Playback of a recorded battle (game::CombatRecord) for the Combat Replay
// window (docs/spec/06 §1.6, docs/spec/04 §17). Headless and testable: it
// tracks where every piece is after any number of events and drives timed
// playback; drawing lives in screens/combat_replay.cpp.
//
// How events are read (the recording side is the combat engine; see the
// encoding in game/combat.hpp):
//   Move       `piece` moves to square (x, y).
//   Fire       `piece` fires weapon `component` (Components.txt index) at `target`.
//   Hit        `piece`'s shot hits `target` for `amount` damage.
//   Miss       `piece`'s shot misses `target`.
//   Destroyed  `piece` is destroyed (and leaves the map); a planet whose colony
//              died stays on the map as an unowned obstacle.
//   Captured   `piece` is captured; its new owner is the owner of piece `target`
//              (when `target` names another piece).
//   Launch     unit group `piece` is launched by carrier `target` and appears at
//              (x, y); or troop ship `piece` lands troops on planet `target`.
//   Seeker     seeker piece `piece` flies to (x, y) toward `target`; it appears
//              on its first event.
//   UnitsLost  unit group `piece` loses `amount` units to a hit by `target`.
// Pieces of kind Seeker, and pieces launched by a Launch event, start off the
// map. Planets and obstacles cover 4x4 squares from their top-left square.
// Events are played in round order (stable for equal rounds).
//
// Timed playback (spec 06 §1.10.3, confirmed: binary): every event is an
// animation of frames, and every frame is drawn. Without Fast Tactical Combat
// each frame is followed by the original's wait: a one-square slide 1 ms per
// frame (a jump and 0.1 s with "animate ship movement" off), a turn to a new
// facing 0.01 s per frame, a beam 0.00001 s per stamp drawn and 0.00005 s per
// stamp erased, a torpedo 1 ms per frame of its flight, a hit's 8-frame
// animation 0.1 s per frame and after it is wiped (0.9 s), then 0.3 s after
// the hit. With Fast Tactical Combat nothing waits and there is no speed
// factor. A frame is shown for at least one display refresh, whatever its
// wait, so that it is seen (an OpenSE4 choice, inferred: the original draws
// it and moves on at once). How many frames our own drawings take (a slide,
// a turn, a torpedo's flight) is ours too (inferred).

#include "game/state.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace opense4::client::classic {

// How a window animates a battle (spec 06 §1.10.3).
struct CombatPace {
    bool fast = false;             // Fast Tactical Combat: no waits
    bool animateMoves = true;      // "animate ship movement in combat": slides, else jumps
    std::vector<uint8_t> beams;    // per Components.txt index: 1 when the weapon is drawn as a beam
};

// One frame of an event's animation and the wait after it.
struct AnimationFrame {
    enum class Part : uint8_t {
        Turn,       // the piece turns 45 degrees toward its move (0.01 s)
        Slide,      // the piece slides toward the next square (1 ms)
        Jump,       // the piece stands on the next square (0.1 s; movement not animated)
        Beam,       // a beam drawn from shooter to target (0.00001 s per stamp)
        BeamErase,  // the beam erased (0.00005 s per stamp)
        Torpedo,    // a torpedo in flight, `step` of `steps` of the way (1 ms)
        Explosion,  // frame `step` of a hit's or a loss's 8-frame animation (0.1 s)
        Wipe,       // the animation wiped (0.1 s)
        AfterHit,   // the pause after a hit (0.3 s)
        Flash,      // a launch, a landing or a capture marked (no wait)
    };
    Part part = Part::Slide;
    int step = 0, steps = 1;       // this frame's place in its part
    float wait = 0.0f;             // seconds after it (0 with Fast Tactical Combat)
};

class CombatPlayback {
public:
    struct Piece {
        bool onMap = true;
        bool destroyed = false;
        bool captured = false;
        bool neutral = false;      // an obstacle, or a planet whose colony died
        int size = 1;              // squares covered on each side (planets and obstacles: 4)
        game::EmpireId owner;
        int x = 0, y = 0;
        int fromX = 0, fromY = 0;  // square before the latest move (for animation)
        float heading = 0.0f;      // radians, 0 = facing up (map y decreasing)
        int damage = 0;            // total damage taken so far
        int units = 1;             // units left in a group (seekers: members at the start)
        int shots = 0;             // Fire events so far
    };

    CombatPlayback() = default;
    explicit CombatPlayback(const game::CombatRecord& record);

    // ---- The record, in playback order ---------------------------------------------
    size_t eventCount() const { return order_.size(); }
    const game::CombatEvent& event(size_t i) const { return record_->events[order_[i]]; }
    int roundCount() const { return rounds_; }
    // First event index of a round (1-based rounds; roundCount()+1 = eventCount()).
    size_t roundStart(int round) const;

    // ---- Cursor: the number of events applied ----------------------------------------
    size_t cursor() const { return cursor_; }
    // The round being shown: the round of the last applied event (0 = setup).
    int round() const;
    bool atStart() const { return cursor_ == 0; }
    bool atEnd() const { return cursor_ >= order_.size(); }

    void seekEvent(size_t n);      // state after the first n events
    void seekRound(int round);     // state after every event of rounds <= round
    void rewind() { seekEvent(0); }
    void stepRound();              // to the end of the next round
    void stepBackRound();          // to the end of the previous round
    void stepEvent();              // one event forward

    // ---- Timed playback ----------------------------------------------------------------
    bool playing() const { return playing_; }
    void play();                   // from the start again when at the end
    void pause() { playing_ = false; }
    void togglePlay() { playing_ ? pause() : play(); }
    const CombatPace& pace() const { return pace_; }
    // A new pace applies at once, to the frames of the event being animated too.
    void setPace(CombatPace pace);
    // Called once per display refresh, before drawing, with the seconds since
    // the last one: moves on by at most one animation frame, once the frame
    // shown has been on screen for its wait (see the file comment). An event
    // is applied when its last frame is done; events without frames at once.
    // Stops at the end.
    void advance(float seconds);
    // While playing: the event being animated (event(cursor())) and its frame.
    const game::CombatEvent* animating() const { return playing_ && !atEnd() ? &event(cursor_) : nullptr; }
    const AnimationFrame* frame() const { return animating() && frame_ < frames_.size() ? &frames_[frame_] : nullptr; }
    // The frames of event i if it were played now, from the pieces' state after
    // the applied events (the turn before a move depends on the heading then).
    std::vector<AnimationFrame> framesOf(size_t i) const;
    // Their waits added up: how long event i takes at least.
    float eventWait(size_t i) const;

    // ---- State after the applied events ----------------------------------------------
    const std::vector<Piece>& pieces() const { return pieces_; }
    bool validPiece(uint32_t i) const { return i < pieces_.size(); }
    // Squares covered by every position in the record (for fitting the map).
    struct Bounds {
        int minX = 0, minY = 0, maxX = 0, maxY = 0;
    };
    const Bounds& bounds() const { return bounds_; }
    // Whether event i is a Hit or Miss that follows a Fire of the same shot.
    bool followsFire(size_t i) const;

private:
    void reset();
    void apply(const game::CombatEvent& e);
    // Makes event(cursor_) the one being animated; applies events without frames on the way.
    void startEvent();

    const game::CombatRecord* record_ = nullptr;
    std::vector<size_t> order_;       // record event indices in playback order
    std::vector<size_t> roundStarts_; // index into order_ per round (1-based, plus end)
    int rounds_ = 0;
    std::vector<Piece> start_;
    std::vector<Piece> pieces_;
    Bounds bounds_;
    size_t cursor_ = 0;
    bool playing_ = false;
    CombatPace pace_;
    std::vector<AnimationFrame> frames_;   // of event(cursor_) while playing
    size_t frame_ = 0;
    float spent_ = 0.0f;              // seconds the current frame has been on screen
    bool hold_ = false;               // the current frame has not been drawn yet
};

} // namespace opense4::client::classic
