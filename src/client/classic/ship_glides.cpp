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

void ShipGlides::enqueue(std::span<const game::MovementStep> steps, bool originals, bool provisional) {
    for (const game::MovementStep& m : steps) {
        Step st;
        st.provisional = provisional;
        st.members = m.members;
        st.lead = m.lead;
        st.from = m.from;
        st.to = m.to;
        st.heading = m.heading;
        st.others = !originals;
        st.mines = m.kind == game::MovementStep::Kind::MinesStruck;
        st.lost = st.mines && m.destroyed > 0;
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
        if (st.provisional) held->second.provisional = true;
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
            queuedTo_.erase(id);
            // A step the game does not hold yet stays where it took the
            // vehicle, if it was the player's to see (release()).
            if (st.provisional && (!st.others || st.shown)) held->second.pinned = true;
            else held_.erase(held);
        }
    }
}

void ShipGlides::release() {
    for (auto it = held_.begin(); it != held_.end();) {
        Held& h = it->second;
        if (!h.pinned) {
            ++it;
            continue;
        }
        h.pinned = false;
        h.provisional = false;
        if (h.steps > 0) {   // its next steps take it on from there
            ++it;
            continue;
        }
        // Seen there last: the game's place for it now, so no move is found.
        lastAll_[it->first] = Was{h.at, h.heading};
        if (lastSeen_.contains(it->first)) {
            lastSeen_[it->first] = h.at;
            lastHeadings_[it->first] = h.heading;
        }
        it = held_.erase(it);
    }
}

void ShipGlides::finish() {
    // Every step done in its order: the vehicles on their squares (those of
    // steps the game does not hold yet pinned there, release()).
    for (const Running& r : running_) complete(r.step);
    for (const Step& st : pending_) {
        if (st.marker) reached_.push_back(*st.marker);
        else complete(st);
    }
    pending_.clear();
    running_.clear();
    glides_.clear();
    blasts_.clear();
    std::erase_if(held_, [](const auto& h) { return !h.second.pinned; });
    queuedTo_.clear();
    freeAt_.clear();
}

void ShipGlides::begin(Step st, double start, const Look& look, const SeenMap& seen) {
    auto doneAtOnce = [&] {
        complete(st);
        for (const game::VehicleId id : st.members) freeAt_[id] = start;
        lastEnd_ = std::max(lastEnd_, start);
    };
    if (st.mines) {
        // Mines strike the group: the explosion over its square, with the
        // animation or without, for the moves the original shows.
        if (st.others || st.to.system != look.shown) return doneAtOnce();
        Running r;
        st.shown = true;
        r.blast = Blast{st.to, st.lost, start, 0};
        r.end = start + MineBlast::duration();
        r.step = std::move(st);
        running_.push_back(std::move(r));
        return;
    }
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
    // Not shown: done at once, in its turn.
    if (!look.enabled || (st.others && !look.animated.others) || !inSystem || st.from.system != look.shown || !lead) return doneAtOnce();
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
    g.slideFrames = std::max(1, int(std::lround(double(squares) * double(look.cellPixels))));
    // The pause comes after every step; a move seen without its steps stands for one a square.
    g.pause = std::max(0.0, look.pauseSeconds) * double(st.straight ? std::max(1, squares) : 1);
    g.start = start;
    g.pace = look.pace;
    g.frame = 0;
    Running r;
    r.end = start + look.pace.duration(g.turnFrames, g.slideFrames) + g.pause;
    st.shown = true;
    r.step = std::move(st);
    r.glide = std::move(g);
    running_.push_back(std::move(r));
}

void ShipGlides::boom(Running& r) {
    if (!r.blast || r.boomed) return;
    r.boomed = true;
    booms_.push_back(r.blast->at);
}

bool ShipGlides::startDue(const Look& look, const SeenMap& seen, Order order, bool& stop) {
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
            begin(std::move(st), start, look, seen);
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
        begin(std::move(st), start, look, seen);
        any = true;
    }
    return any;
}

void ShipGlides::track(double now, game::SystemId shown, bool enabled, std::span<const Seen> vehicles, float cellPixels, double pauseSeconds,
                       MovementPace pace, Order order, Animated animated) {
    SeenMap seen;
    for (const Seen& v : vehicles) seen.emplace(v.id, &v);
    for (Step& st : pending_)
        if (st.queued < 0.0) st.queued = now;
    const Look look{shown, enabled, cellPixels, pauseSeconds, pace, animated};

    // Another system shown: the glides under way end there; the queue goes
    // on. The setting off: they end too, and every move after them takes no
    // time, in its turn (mines' explosions take theirs).
    if (shown != lastShown_ || !enabled) {
        for (auto it = running_.begin(); it != running_.end();) {
            if (it->blast && shown == lastShown_) {
                ++it;
                continue;
            }
            complete(it->step);
            for (const game::VehicleId id : it->step.members) freeAt_[id] = now;
            lastEnd_ = std::max(lastEnd_, now);
            it = running_.erase(it);
        }
    }
    if (enabled) {
        // Moves of vehicles seen before and now found without their steps:
        // one straight glide per group of vehicles that started and ended
        // together, queued in the game's vehicle order.
        std::vector<Step> found;
        for (const Seen& v : vehicles) {
            if (!v.visible) continue;
            const auto was = lastSeen_.find(v.id);
            if (was == lastSeen_.end()) continue;   // first sight: nothing to animate
            // Steps the game does not hold yet: it is still where it was (release()).
            if (const auto h = held_.find(v.id); h != held_.end() && h->second.provisional) continue;
            const auto queued = queuedTo_.find(v.id);
            const game::Location expected = queued != queuedTo_.end() ? queued->second : was->second;
            if (expected == v.location) continue;
            // The original animates only the player's own moves, in a turn-based game.
            const bool others = !(v.own && animated.ownFound);
            if (others && !animated.others) continue;   // simply drawn on its new square
            auto same = std::find_if(found.begin(), found.end(),
                                     [&](const Step& st) { return st.from == expected && st.to == v.location && st.others == others; });
            if (same == found.end()) {
                Step st;
                st.lead = v.id;
                st.from = expected;
                st.to = v.location;
                st.heading = v.turns ? v.heading % 8 : 0;
                st.straight = true;
                st.others = others;
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
            boom(*it);   // an explosion over before it was drawn still sounds
            complete(it->step);
            it = running_.erase(it);
            changed = true;
        }
        changed = startDue(look, seen, order, stop) || changed;
    }
    for (Running& r : running_) {
        if (r.blast) {
            if (now - r.blast->start + 1e-9 >= MineBlast::soundAt()) boom(r);
            continue;
        }
        r.glide.frame = r.glide.pace.framesDrawn(r.glide.turnFrames, r.glide.slideFrames, now - r.glide.start);
    }
    if (!active()) freeAt_.clear();
    rebuildGlides(now);

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

void ShipGlides::rebuildGlides(double now) {
    glides_.clear();
    blasts_.clear();
    for (const Running& r : running_) {
        if (!r.blast) {
            glides_.push_back(r.glide);
            continue;
        }
        Blast b = *r.blast;
        b.frame = MineBlast::frameAt(now - b.start);
        blasts_.push_back(b);
    }
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
    // On the old square while it turns (where nothing of it is drawn), then
    // 1 px a frame toward the new one.
    if (g.frame <= g.turnFrames) return g.from;
    const int slid = std::min(g.frame - g.turnFrames, g.slideFrames);
    const float t = g.slideFrames > 0 ? float(slid) / float(g.slideFrames) : 1.0f;
    return lerp(g.from, g.to, std::clamp(t, 0.0f, 1.0f));
}

double ShipGlides::angle(const Glide& g) {
    // Drawn only while it slides, at its new heading from the slide's first
    // frame: nothing of the turn is drawn (spec 06 §2.4).
    return g.angle1;
}

} // namespace opense4::client::classic
