#include "client/classic/replay.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace opense4::client::classic {

namespace {

using Kind = game::CombatEvent::Kind;

// Heading of a step (dx, dy) in map squares: 0 = up, clockwise.
float headingOf(int dx, int dy) { return std::atan2(float(dx), float(-dy)); }

bool placesPiece(Kind k) { return k == Kind::Move || k == Kind::Launch || k == Kind::Seeker; }

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
        s.x = s.fromX = p.startX;
        s.y = s.fromY = p.startY;
        s.onMap = p.kind != game::CombatPiece::Kind::Seeker && !launched[i];
        if (s.onMap) extend(s.x, s.y);
    }
    for (const game::CombatEvent& e : events)
        if (placesPiece(e.kind)) extend(e.x, e.y);

    // Every piece starts facing the middle of the pieces of the other sides.
    for (size_t i = 0; i < start_.size(); ++i) {
        float sx = 0, sy = 0;
        int n = 0;
        for (const Piece& o : start_)
            if (o.onMap && o.owner != start_[i].owner) {
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
    elapsed_ = 0.0f;
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
    elapsed_ = 0.0f;
}

void CombatPlayback::setSpeed(float s) { speed_ = std::clamp(s, 0.25f, 8.0f); }

float CombatPlayback::eventDuration(size_t i) const {
    if (i >= order_.size()) return 1.0f;
    float d = 0.2f;
    switch (event(i).kind) {
        case Kind::Move: d = 0.18f; break;
        case Kind::Fire: d = 0.35f; break;
        case Kind::Hit: d = 0.3f; break;
        case Kind::Miss: d = 0.25f; break;
        case Kind::Destroyed: d = 0.5f; break;
        case Kind::Captured: d = 0.5f; break;
        case Kind::Launch: d = 0.3f; break;
        case Kind::Seeker: d = 0.2f; break;
    }
    // A short pause before each new round.
    if (i > 0 && event(i).round != event(i - 1).round) d += 0.4f;
    return d;
}

void CombatPlayback::advance(float seconds) {
    if (!playing_) return;
    elapsed_ += std::max(0.0f, seconds) * speed_;
    while (!atEnd() && elapsed_ >= eventDuration(cursor_)) {
        elapsed_ -= eventDuration(cursor_);
        apply(event(cursor_++));
    }
    if (atEnd()) {
        playing_ = false;
        elapsed_ = 0.0f;
    }
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
