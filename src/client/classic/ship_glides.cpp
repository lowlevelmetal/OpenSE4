#include "client/classic/ship_glides.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace opense4::client::classic {

namespace {

Vec2 cellCenter(game::Sector s) { return {float(s.x) + 0.5f, float(s.y) + 0.5f}; }

int squaresBetween(game::Sector a, game::Sector b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

} // namespace

void ShipGlides::enqueue(std::span<const game::MovementStep> steps) {
    for (const game::MovementStep& m : steps) {
        Step st;
        st.members = m.members;
        st.lead = m.lead;
        st.from = m.from;
        st.to = m.to;
        st.heading = m.heading;
        hold(st);
        pending_.push_back(std::move(st));
    }
}

void ShipGlides::enqueueMarker(uint64_t id) {
    Step st;
    st.marker = id;
    pending_.push_back(std::move(st));
}

void ShipGlides::hold(const Step& st) {
    // A vehicle waits for its steps where the last frame drew it; one that
    // was not in the game then (or a call before any frame) is simply drawn
    // where it is.
    for (const game::VehicleId id : st.members) {
        auto held = held_.find(id);
        if (held == held_.end()) {
            const auto was = lastAll_.find(id);
            if (was == lastAll_.end()) continue;
            held = held_.emplace(id, Held{was->second.location, was->second.heading, lastSeen_.contains(id), 0}).first;
        }
        ++held->second.steps;
        queuedTo_[id] = st.to;
    }
}

void ShipGlides::complete(const Step& st) {
    for (const game::VehicleId id : st.members) {
        const auto held = held_.find(id);
        if (held == held_.end()) continue;
        held->second.at = st.to;
        // A step within a system turns the vehicle to its bearing; a warp keeps its heading.
        if (st.from.system == st.to.system && st.from.sector != st.to.sector) held->second.heading = st.heading;
        if (--held->second.steps <= 0) {
            held_.erase(held);
            queuedTo_.erase(id);
        }
    }
}

void ShipGlides::finish() {
    for (const Step& st : pending_)
        if (st.marker) reached_.push_back(*st.marker);
    pending_.clear();
    running_.clear();
    glides_.clear();
    held_.clear();
    queuedTo_.clear();
    freeAt_.clear();
}

void ShipGlides::begin(Step st, double start, game::SystemId shown, bool enabled, const SeenMap& seen, float cellPixels, double pauseSeconds,
                       MovementPace pace) {
    // The members the player sees now and saw before the moves (or its own);
    // the group's first vehicle is drawn, or the first of them it sees.
    Glide g;
    const Seen* lead = nullptr;
    for (const game::VehicleId id : st.members) {
        const auto v = seen.find(id);
        const auto held = held_.find(id);
        if (v == seen.end() || held == held_.end() || !v->second->visible || !(held->second.seenBefore || v->second->own)) continue;
        g.members.push_back(id);
        if (!lead || id == st.lead) lead = v->second;
    }
    const bool inSystem = st.from.system == st.to.system && st.from.sector != st.to.sector;
    if (!enabled || !inSystem || st.from.system != shown || !lead) {
        // Not shown: done at once, in its turn.
        complete(st);
        for (const game::VehicleId id : st.members) freeAt_[id] = start;
        lastEnd_ = std::max(lastEnd_, start);
        return;
    }
    g.lead = lead->id;
    g.from = cellCenter(st.from.sector);
    g.to = cellCenter(st.to.sector);
    g.angle0 = lead->turns ? double(held_.at(lead->id).heading % 8) * 45.0 : 0.0;
    // The shorter way round to the new heading, clockwise for a half-turn.
    const double target = lead->turns ? double(st.heading % 8) * 45.0 : g.angle0;
    double delta = std::fmod(target - g.angle0, 360.0);
    if (delta < 0.0) delta += 360.0;
    if (delta > 180.0) delta -= 360.0;
    g.angle1 = g.angle0 + delta;
    g.turnFrames = int(std::lround(std::abs(delta) / kDegreesPerTurnFrame));
    const int squares = squaresBetween(st.from.sector, st.to.sector);
    g.slideFrames = std::max(1, int(std::lround(double(squares) * double(cellPixels))));
    // The pause comes after every step; a move seen without its steps stands for one a square.
    g.pause = std::max(0.0, pauseSeconds) * double(st.straight ? std::max(1, squares) : 1);
    g.start = start;
    g.pace = pace;
    g.frame = 0;
    const double end = start + pace.duration(g.turnFrames, g.slideFrames) + g.pause;
    running_.push_back(Running{std::move(st), std::move(g), end});
}

bool ShipGlides::startDue(game::SystemId shown, bool enabled, const SeenMap& seen, float cellPixels, double pauseSeconds, MovementPace pace,
                          Order order, bool& stop) {
    bool any = false;
    if (order == Order::OneGroupAtATime) {
        // Each step when the one before is over (or at once when the queue was idle).
        while (running_.empty() && !pending_.empty()) {
            Step st = std::move(pending_.front());
            pending_.pop_front();
            const double start = std::max(lastEnd_, st.queued);
            any = true;
            if (st.marker) {
                // The view follows now, before the next step is shown.
                reached_.push_back(*st.marker);
                lastEnd_ = start;
                stop = true;
                return any;
            }
            begin(std::move(st), start, shown, enabled, seen, cellPixels, pauseSeconds, pace);
        }
        return any;
    }
    // All at once: the markers are reached at once, first, so that the view
    // has followed before any step is shown; then a step starts as soon as its
    // members' steps before it are over.
    if (std::any_of(pending_.begin(), pending_.end(), [](const Step& st) { return st.marker.has_value(); })) {
        for (const Step& st : pending_)
            if (st.marker) reached_.push_back(*st.marker);
        std::erase_if(pending_, [](const Step& st) { return st.marker.has_value(); });
        stop = true;
        return true;
    }
    std::set<game::VehicleId> busy;
    for (const Running& r : running_) busy.insert(r.step.members.begin(), r.step.members.end());
    for (auto it = pending_.begin(); it != pending_.end();) {
        const bool blocked = std::any_of(it->members.begin(), it->members.end(), [&](game::VehicleId id) { return busy.contains(id); });
        busy.insert(it->members.begin(), it->members.end());
        if (blocked) {
            ++it;
            continue;
        }
        Step st = std::move(*it);
        it = pending_.erase(it);
        double start = st.queued;
        for (const game::VehicleId id : st.members)
            if (const auto f = freeAt_.find(id); f != freeAt_.end()) start = std::max(start, f->second);
        begin(std::move(st), start, shown, enabled, seen, cellPixels, pauseSeconds, pace);
        any = true;
    }
    return any;
}

void ShipGlides::track(double now, game::SystemId shown, bool enabled, std::span<const Seen> vehicles, float cellPixels, double pauseSeconds,
                       MovementPace pace, Order order) {
    SeenMap seen;
    for (const Seen& v : vehicles) seen.emplace(v.id, &v);
    for (Step& st : pending_)
        if (st.queued < 0.0) st.queued = now;

    if (!enabled) {
        // The setting off: every ship is simply where it is, and the view follows at once.
        finish();
    } else {
        if (shown != lastShown_) {
            // Another system shown: the glides under way end there; the queue goes on.
            for (const Running& r : running_) {
                complete(r.step);
                for (const game::VehicleId id : r.step.members) freeAt_[id] = now;
                lastEnd_ = std::max(lastEnd_, now);
            }
            running_.clear();
        }
        // Moves of vehicles seen before and now found without their steps:
        // one straight glide per group of vehicles that started and ended
        // together, queued in the game's vehicle order.
        std::vector<Step> found;
        for (const Seen& v : vehicles) {
            if (!v.visible) continue;
            const auto was = lastSeen_.find(v.id);
            if (was == lastSeen_.end()) continue;   // first sight: nothing to animate
            const auto queued = queuedTo_.find(v.id);
            const game::Location expected = queued != queuedTo_.end() ? queued->second : was->second;
            if (expected == v.location) continue;
            auto same = std::find_if(found.begin(), found.end(), [&](const Step& st) { return st.from == expected && st.to == v.location; });
            if (same == found.end()) {
                Step st;
                st.lead = v.id;
                st.from = expected;
                st.to = v.location;
                st.heading = v.turns ? v.heading % 8 : 0;
                st.straight = true;
                st.queued = now;
                found.push_back(std::move(st));
                same = std::prev(found.end());
            }
            same->members.push_back(v.id);
        }
        for (Step& st : found) {
            hold(st);
            pending_.push_back(std::move(st));
        }
    }

    // The queue to `now`: the steps that are over end, and the next begin
    // from where the ones before ended, several in one call when they come
    // faster than the display's frames; it stops at a marker.
    bool stop = false;
    for (bool changed = true; changed && !stop;) {
        changed = false;
        for (auto it = running_.begin(); it != running_.end();) {
            if (now < it->end) {
                ++it;
                continue;
            }
            lastEnd_ = std::max(lastEnd_, it->end);
            for (const game::VehicleId id : it->step.members) freeAt_[id] = it->end;
            complete(it->step);
            it = running_.erase(it);
            changed = true;
        }
        changed = startDue(shown, enabled, seen, cellPixels, pauseSeconds, pace, order, stop) || changed;
    }
    for (Running& r : running_) r.glide.frame = r.glide.pace.framesDrawn(r.glide.turnFrames, r.glide.slideFrames, now - r.glide.start);
    if (!active()) freeAt_.clear();
    rebuildGlides();

    lastSeen_.clear();
    lastHeadings_.clear();
    lastAll_.clear();
    for (const Seen& v : vehicles) {
        const int heading = v.turns ? v.heading % 8 : 0;
        lastAll_.emplace(v.id, Was{v.location, heading});
        if (!v.visible) continue;
        lastSeen_.emplace(v.id, v.location);
        lastHeadings_.emplace(v.id, heading);
    }
    lastShown_ = shown;
}

void ShipGlides::rebuildGlides() {
    glides_.clear();
    for (const Running& r : running_) glides_.push_back(r.glide);
}

const ShipGlides::Glide* ShipGlides::find(game::VehicleId v) const {
    for (const Glide& g : glides_)
        if (std::find(g.members.begin(), g.members.end(), v) != g.members.end()) return &g;
    return nullptr;
}

std::optional<game::Location> ShipGlides::heldAt(game::VehicleId v) const {
    const auto it = held_.find(v);
    if (it == held_.end()) return std::nullopt;
    return it->second.at;
}

std::optional<int> ShipGlides::heldHeading(game::VehicleId v) const {
    const auto it = held_.find(v);
    if (it == held_.end()) return std::nullopt;
    return it->second.heading;
}

Vec2 ShipGlides::position(const Glide& g) {
    // Turning on the old square, then 1 px a frame toward the new one.
    if (g.frame <= g.turnFrames) return g.from;
    const int slid = std::min(g.frame - g.turnFrames, g.slideFrames);
    const float t = g.slideFrames > 0 ? float(slid) / float(g.slideFrames) : 1.0f;
    return lerp(g.from, g.to, std::clamp(t, 0.0f, 1.0f));
}

double ShipGlides::angle(const Glide& g) {
    if (g.frame >= g.turnFrames) return g.angle1;
    const double step = g.angle1 >= g.angle0 ? kDegreesPerTurnFrame : -kDegreesPerTurnFrame;
    return g.angle0 + step * std::max(0, g.frame);
}

} // namespace opense4::client::classic
