#include "client/classic/replay.hpp"

#include "game/combat.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace opense4::client::classic {

namespace {

using Kind = game::CombatEvent::Kind;

// Heading of a step (dx, dy) in map squares: 0 = up, clockwise.
float headingOf(int dx, int dy) { return std::atan2(float(dx), float(-dy)); }

bool placesPiece(Kind k) { return k == Kind::Move || k == Kind::Launch || k == Kind::Seeker; }

// Planets and neutral obstacles cover a square of this many map squares.
int footprint(game::CombatPiece::Kind k) {
    return k == game::CombatPiece::Kind::Planet || k == game::CombatPiece::Kind::Obstacle ? game::combat::kBigPieceSize : 1;
}

} // namespace

CombatPlayback::CombatPlayback(const game::CombatRecord& record) : record_(&record) {
    const auto& events = record.events;
    order_.resize(events.size());
    std::iota(order_.begin(), order_.end(), size_t{0});
    std::stable_sort(order_.begin(), order_.end(), [&](size_t a, size_t b) { return events[a].round < events[b].round; });

    // Rounds as shown: the first recorded round is round 1.
    int minRound = 0, maxRound = -1;
    if (!events.empty()) {
        minRound = events[order_.front()].round;
        maxRound = events[order_.back()].round;
    }
    rounds_ = maxRound - minRound + 1;
    roundStarts_.assign(size_t(rounds_) + 2, 0);
    for (int r = 1; r <= rounds_ + 1; ++r) {
        size_t i = roundStarts_[size_t(r - 1)];
        while (i < order_.size() && events[order_[i]].round - minRound + 1 < r) ++i;
        roundStarts_[size_t(r)] = i;
    }
    roundStarts_[0] = 0;

    // Starting positions; seekers and launched pieces enter the map later.
    start_.resize(record.pieces.size());
    std::vector<uint8_t> launched(record.pieces.size(), 0);
    for (const game::CombatEvent& e : events)
        if (e.kind == Kind::Launch && e.piece < launched.size() && record.pieces[e.piece].kind == game::CombatPiece::Kind::UnitGroup)
            launched[e.piece] = 1;
    bool first = true;
    auto extend = [&](int x, int y) {
        if (first) {
            bounds_ = {x, y, x, y};
            first = false;
            return;
        }
        bounds_.minX = std::min(bounds_.minX, x);
        bounds_.minY = std::min(bounds_.minY, y);
        bounds_.maxX = std::max(bounds_.maxX, x);
        bounds_.maxY = std::max(bounds_.maxY, y);
    };
    for (size_t i = 0; i < record.pieces.size(); ++i) {
        const game::CombatPiece& p = record.pieces[i];
        Piece& s = start_[i];
        s.owner = p.owner;
        s.size = footprint(p.kind);
        s.neutral = p.kind == game::CombatPiece::Kind::Obstacle;
        s.x = s.fromX = p.startX;
        s.y = s.fromY = p.startY;
        s.units = std::max(1, int(p.count));
        s.onMap = p.kind != game::CombatPiece::Kind::Seeker && !launched[i];
        if (s.onMap) {
            extend(s.x, s.y);
            extend(s.x + s.size - 1, s.y + s.size - 1);
        }
    }
    for (const game::CombatEvent& e : events)
        if (placesPiece(e.kind)) extend(e.x, e.y);

    // Every piece starts facing the middle of the pieces of the other sides.
    for (size_t i = 0; i < start_.size(); ++i) {
        float sx = 0, sy = 0;
        int n = 0;
        for (const Piece& o : start_)
            if (o.onMap && !o.neutral && o.owner != start_[i].owner) {
                sx += float(o.x);
                sy += float(o.y);
                ++n;
            }
        if (n > 0) start_[i].heading = std::atan2(sx / float(n) - float(start_[i].x), -(sy / float(n) - float(start_[i].y)));
    }
    pieces_ = start_;
}

size_t CombatPlayback::roundStart(int round) const {
    if (roundStarts_.empty()) return 0;
    return roundStarts_[size_t(std::clamp(round, 0, rounds_ + 1))];
}

int CombatPlayback::round() const {
    if (cursor_ == 0 || rounds_ <= 0) return 0;
    // The round whose events include the last applied one.
    int r = 1;
    while (r < rounds_ && roundStarts_[size_t(r + 1)] < cursor_) ++r;
    return r;
}

void CombatPlayback::reset() {
    pieces_ = start_;
    cursor_ = 0;
    frames_.clear();
    frame_ = 0;
}

void CombatPlayback::apply(const game::CombatEvent& e) {
    if (!validPiece(e.piece)) return;
    Piece& p = pieces_[e.piece];
    switch (e.kind) {
        case Kind::Move:
            if (e.x != p.x || e.y != p.y) p.heading = headingOf(e.x - p.x, e.y - p.y);
            p.fromX = p.x;
            p.fromY = p.y;
            p.x = e.x;
            p.y = e.y;
            break;
        case Kind::Fire: ++p.shots; break;
        case Kind::Hit:
            if (validPiece(e.target)) pieces_[e.target].damage += std::max(0, int(e.amount));
            break;
        case Kind::Miss: break;
        case Kind::Destroyed:
            if (record_ && record_->pieces[e.piece].kind == game::CombatPiece::Kind::Planet) {
                // A colony wiped out: the planet stays on the map as an unowned obstacle.
                p.neutral = true;
                p.owner = {};
                break;
            }
            p.destroyed = true;
            p.onMap = false;
            break;
        case Kind::Captured:
            p.captured = true;
            if (validPiece(e.target) && e.target != e.piece) p.owner = pieces_[e.target].owner;
            break;
        case Kind::Launch:
            // `piece` is the new unit group, `target` its carrier. A troop ship
            // landing troops (piece = the ship, target = the planet) places nothing.
            if (record_ && record_->pieces[e.piece].kind == game::CombatPiece::Kind::UnitGroup) {
                p.onMap = true;
                p.destroyed = false;
                p.x = p.fromX = e.x;
                p.y = p.fromY = e.y;
                if (validPiece(e.target)) p.heading = pieces_[e.target].heading;
            }
            break;
        case Kind::UnitsLost: p.units = std::max(0, p.units - std::max(0, int(e.amount))); break;
        case Kind::Seeker:
            if (p.destroyed) break;
            if (!p.onMap) {
                p.onMap = true;
                p.fromX = e.x;
                p.fromY = e.y;
            } else {
                p.fromX = p.x;
                p.fromY = p.y;
            }
            if (validPiece(e.target)) {
                const Piece& t = pieces_[e.target];
                if (t.x != e.x || t.y != e.y) p.heading = headingOf(t.x - e.x, t.y - e.y);
            } else if (e.x != p.fromX || e.y != p.fromY) {
                p.heading = headingOf(e.x - p.fromX, e.y - p.fromY);
            }
            p.x = e.x;
            p.y = e.y;
            break;
    }
}

void CombatPlayback::seekEvent(size_t n) {
    n = std::min(n, order_.size());
    if (n < cursor_) reset();
    while (cursor_ < n) apply(event(cursor_++));
    frames_.clear();
    frame_ = 0;
    spent_ = 0.0f;
    if (playing_) startEvent();
}

void CombatPlayback::seekRound(int r) {
    r = std::clamp(r, 0, rounds_);
    seekEvent(r == 0 ? 0 : roundStart(r + 1));
}

void CombatPlayback::stepRound() {
    // The first round boundary after the cursor (rounds without events are skipped).
    for (int r = 1; r <= rounds_; ++r)
        if (roundStart(r + 1) > cursor_) {
            seekEvent(roundStart(r + 1));
            return;
        }
}

void CombatPlayback::stepBackRound() {
    size_t target = 0;
    for (int r = 1; r <= rounds_; ++r)
        if (roundStart(r + 1) < cursor_) target = roundStart(r + 1);
    seekEvent(target);
}

void CombatPlayback::stepEvent() { seekEvent(cursor_ + 1); }

void CombatPlayback::play() {
    if (atEnd()) rewind();
    playing_ = !order_.empty();
    startEvent();
}

namespace {

// The waits of spec 06 §1.10.3 (confirmed: binary), in seconds.
constexpr float kSlideWait = 0.001f;
constexpr float kJumpWait = 0.1f;
constexpr float kTurnWait = 0.01f;
constexpr float kBeamWait = 0.00001f;
constexpr float kBeamEraseWait = 0.00005f;
constexpr float kTorpedoWait = 0.001f;
constexpr float kHitFrameWait = 0.1f;
constexpr float kAfterHitWait = 0.3f;
constexpr int kHitFrames = 8;
// Frames (spec 06 §1.10.3, §7 Q77, confirmed: binary): 1 px a frame over a
// 36 px square; 5° a turn frame; beam stamps 6 px apart; a torpedo 4 px a
// frame (6 with Fast Tactical Combat).
constexpr int kSlideFramesPerSquare = 36;
constexpr int kTurnFramesPer45 = 9;
constexpr int kBeamStampsPerSquare = 6;
constexpr int kTorpedoFramesPerSquare = 9;
constexpr int kFastTorpedoFramesPerSquare = 6;

// A wait lasts whole steps of the tick counter (observed): any wait at least one.
float ticks(float wait) {
    if (wait <= 0.0f) return 0.0f;
    return std::ceil(wait / kCombatTick - 1e-4f) * kCombatTick;
}

// Eighths of a turn between two headings, the shorter way round.
int turnEighths(float from, float to) {
    constexpr float kPi = 3.14159265358979f;
    float d = std::fmod(to - from, 2.0f * kPi);
    if (d > kPi) d -= 2.0f * kPi;
    if (d < -kPi) d += 2.0f * kPi;
    return int(std::lround(std::fabs(d) / (kPi / 4.0f)));
}

} // namespace

std::vector<AnimationFrame> CombatPlayback::framesOf(size_t i) const {
    using Part = AnimationFrame::Part;
    std::vector<AnimationFrame> out;
    if (i >= order_.size()) return out;
    const game::CombatEvent& e = event(i);
    const bool known = validPiece(e.piece);
    auto add = [&](Part part, int steps, float wait) {
        for (int k = 0; k < steps; ++k) out.push_back(AnimationFrame{part, k, steps, pace_.fast ? 0.0f : ticks(wait)});
    };
    auto inView = [&](int x, int y) { return !pace_.inView || pace_.inView(x, y); };
    auto pieceKind = [&](uint32_t p) {
        return record_ && p < record_->pieces.size() ? record_->pieces[p].kind : game::CombatPiece::Kind::Vehicle;
    };
    switch (e.kind) {
        case Kind::Move:
        case Kind::Seeker: {
            if (!known) break;
            const Piece& p = pieces_[e.piece];
            if (e.x == p.x && e.y == p.y) break;
            // Movement not animated: the piece jumps, then 0.1 s (also off-screen).
            if (!pace_.animateMoves) {
                add(Part::Jump, 1, kJumpWait);
                break;
            }
            // Either square out of the shown part of the map: it just appears.
            if (!inView(p.x, p.y) || !inView(e.x, e.y)) break;
            // A piece turns to its new facing first (seekers never turn), then slides.
            if (e.kind == Kind::Move) add(Part::Turn, kTurnFramesPer45 * turnEighths(p.heading, headingOf(e.x - p.x, e.y - p.y)), kTurnWait);
            const int squares = std::max(std::abs(e.x - p.x), std::abs(e.y - p.y));
            add(Part::Slide, kSlideFramesPerSquare * squares, kSlideWait);
            break;
        }
        case Kind::Fire: {
            if (!known || !validPiece(e.target)) break;
            // A seeker's launch is not animated: the launcher is only redrawn.
            if (e.component < pace_.seekers.size() && pace_.seekers[e.component] != 0) break;
            const Piece& a = pieces_[e.piece];
            const Piece& b = pieces_[e.target];
            const int squares = std::max(1, std::max(std::abs(b.x - a.x), std::abs(b.y - a.y)));
            const bool beam = e.component < pace_.beams.size() && pace_.beams[e.component] != 0;
            if (beam) {
                add(Part::Beam, kBeamStampsPerSquare * squares, kBeamWait);
                add(Part::BeamErase, kBeamStampsPerSquare * squares, kBeamEraseWait);
            } else {
                add(Part::Torpedo, (pace_.fast ? kFastTorpedoFramesPerSquare : kTorpedoFramesPerSquare) * squares, kTorpedoWait);
            }
            break;
        }
        case Kind::Hit: {
            if (!validPiece(e.target)) break;
            if ((e.flags & game::CombatEvent::kStructure) == 0) {
                // The shields took it all: one shield picture, no wait (none with Fast).
                if (!pace_.fast) out.push_back(AnimationFrame{Part::Shield, 0, 1, 0.0f});
                break;
            }
            add(Part::Explosion, kHitFrames, kHitFrameWait);
            add(Part::Wipe, 1, kHitFrameWait);
            // Tactical Combat pauses after a seeker's impact on a piece that survives it.
            if (pace_.tactical && known && pieceKind(e.piece) == game::CombatPiece::Kind::Seeker && (e.flags & game::CombatEvent::kDestroyed) == 0)
                add(Part::AfterHit, 1, kAfterHitWait);
            break;
        }
        case Kind::Destroyed:
        case Kind::Captured:
        case Kind::Launch:
        case Kind::Miss:        // the shot was drawn by its Fire, ending off the target
        case Kind::UnitsLost:   // shown by the Hit before it
            // A loss plays no second explosion: the destroying hit played it.
            // A seeker that struck vanishes; launches, landings and captures
            // are only redrawn (spec 06 §1.10.3).
            break;
    }
    return out;
}

const game::CombatEvent* CombatPlayback::shotOutcome(size_t i) const {
    if (i >= order_.size()) return nullptr;
    const game::CombatEvent& f = event(i);
    if (f.kind != Kind::Fire) return nullptr;
    for (size_t j = i + 1; j < order_.size(); ++j) {
        const game::CombatEvent& e = event(j);
        if (e.round != f.round) break;
        if (e.kind == Kind::Fire) break;
        if ((e.kind == Kind::Hit || e.kind == Kind::Miss) && e.piece == f.piece && e.target == f.target) return &e;
    }
    return nullptr;
}

void CombatPlayback::setPace(CombatPace pace) {
    pace_ = std::move(pace);
    if (!playing_ || atEnd() || frames_.empty()) return;
    frames_ = framesOf(cursor_);
    if (frames_.empty()) startEvent();
    else frame_ = std::min(frame_, frames_.size() - 1);
}

float CombatPlayback::eventWait(size_t i) const {
    float total = 0.0f;
    for (const AnimationFrame& f : framesOf(i)) total += f.wait;
    return total;
}

void CombatPlayback::startEvent() {
    frames_.clear();
    frame_ = 0;
    spent_ = 0.0f;
    hold_ = true;
    if (!playing_) return;
    while (!atEnd()) {
        frames_ = framesOf(cursor_);
        if (!frames_.empty()) return;
        apply(event(cursor_++));
    }
    playing_ = false;
}

void CombatPlayback::advance(float seconds) {
    if (!playing_) return;
    if (frames_.empty()) startEvent();
    if (!playing_) return;
    // The frame current now has not been on screen yet: it is drawn first.
    if (hold_) {
        hold_ = false;
        return;
    }
    spent_ += std::max(0.0f, seconds);
    if (spent_ < frames_[frame_].wait) return;
    // On to the next frame (at most one per refresh, so each one is seen).
    spent_ = 0.0f;
    if (++frame_ < frames_.size()) return;
    apply(event(cursor_++));
    startEvent();
    hold_ = false;   // the new frame is drawn after this call
}

bool CombatPlayback::followsFire(size_t i) const {
    if (i >= order_.size()) return false;
    const game::CombatEvent& e = event(i);
    if (e.kind != Kind::Hit && e.kind != Kind::Miss) return false;
    for (size_t j = i; j-- > 0;) {
        const game::CombatEvent& f = event(j);
        if (f.round != e.round) break;
        if (f.kind == Kind::Fire && f.piece == e.piece && f.target == e.target) return true;
    }
    return false;
}

} // namespace opense4::client::classic
