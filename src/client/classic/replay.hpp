#pragma once

// Playback of a recorded battle (game::CombatRecord) for the Combat Replay
// window (docs/spec/06 §1.6, docs/spec/04 §17). Headless and testable: it
// tracks where every piece is after any number of events and drives timed
// playback; drawing lives in screens/combat_replay.cpp.
//
// How events are read (the recording side is the combat engine):
//   Move       `piece` moves to square (x, y).
//   Fire       `piece` fires weapon `component` (Components.txt index) at `target`.
//   Hit        `piece`'s shot hits `target` for `amount` damage.
//   Miss       `piece`'s shot misses `target`.
//   Destroyed  `piece` is destroyed (and leaves the map).
//   Captured   `piece` is captured; its new owner is the owner of piece `target`
//              (when `target` names another piece).
//   Launch     `piece` launches piece `target`, which appears at (x, y).
//   Seeker     seeker piece `piece` flies to (x, y) toward `target`; it appears
//              on its first event.
// Pieces of kind Seeker, and pieces launched by a Launch event, start off the
// map. Events are played in round order (stable for equal rounds).

#include "game/state.hpp"

#include <cstddef>
#include <vector>

namespace opense4::client::classic {

class CombatPlayback {
public:
    struct Piece {
        bool onMap = true;
        bool destroyed = false;
        bool captured = false;
        game::EmpireId owner;
        int x = 0, y = 0;
        int fromX = 0, fromY = 0;  // square before the latest move (for animation)
        float heading = 0.0f;      // radians, 0 = facing up (map y decreasing)
        int damage = 0;            // total damage taken so far
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
    float speed() const { return speed_; }
    void setSpeed(float s);        // clamped to 0.25..8
    // Advances playback by wall-clock seconds (times speed); stops at the end.
    void advance(float seconds);
    // While playing: the event being animated (event(cursor())) and how far along it is.
    const game::CombatEvent* animating() const { return playing_ && !atEnd() ? &event(cursor_) : nullptr; }
    float fraction() const { return playing_ ? elapsed_ / eventDuration(cursor_) : 0.0f; }
    // Seconds an event takes at speed 1 (moves are quick, shots and explosions slower).
    float eventDuration(size_t i) const;

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

    const game::CombatRecord* record_ = nullptr;
    std::vector<size_t> order_;       // record event indices in playback order
    std::vector<size_t> roundStarts_; // index into order_ per round (1-based, plus end)
    int rounds_ = 0;
    std::vector<Piece> start_;
    std::vector<Piece> pieces_;
    Bounds bounds_;
    size_t cursor_ = 0;
    bool playing_ = false;
    float speed_ = 1.0f;
    float elapsed_ = 0.0f;            // seconds into event(cursor_)
};

} // namespace opense4::client::classic
