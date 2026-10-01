#include "client/classic/movement_replay.hpp"

#include "client/classic/map_style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace opense4::client::classic {

namespace {

Vec2 cellCenter(game::Sector s) { return {float(s.x) + 0.5f, float(s.y) + 0.5f}; }

std::map<game::ObjectId, game::EmpireId> coloniesOf(const game::GameState& s) {
    std::map<game::ObjectId, game::EmpireId> out;
    for (const auto& c : s.colonies)
        if (c) out[c->planet] = c->owner;
    return out;
}

} // namespace

// ---- The log ----

std::vector<game::VehicleId> MovementLog::movers(game::EmpireId owner) const {
    std::vector<game::VehicleId> out;
    for (const Day& d : days)
        for (const Move& m : d.moves) {
            if (m.from == m.to) continue;
            const auto it = vehicles.find(m.id);
            if (it == vehicles.end() || it->second.owner != owner) continue;
            if (std::find(out.begin(), out.end(), m.id) == out.end()) out.push_back(m.id);
        }
    return out;
}

MovementRecorder::MovementRecorder(const game::GameState& start) {
    log_.exact = true;
    for (const game::Vehicle& v : start.vehicles) {
        if (v.count <= 0) continue;
        log_.vehicles.emplace(v.id, v);
        log_.start[v.id] = v.location;
    }
    where_ = log_.start;
    colonies_ = coloniesOf(start);
    log_.startColonies = colonies_;
}

void MovementRecorder::day(int day, const game::GameState& s) {
    if (day < 1) return;
    if (int(log_.days.size()) < day) log_.days.resize(size_t(day));
    MovementLog::Day& d = log_.days[size_t(day - 1)];
    std::map<game::VehicleId, game::Location> now;
    for (const game::Vehicle& v : s.vehicles) {
        if (v.count <= 0) continue;
        now[v.id] = v.location;
        const auto was = where_.find(v.id);
        if (was == where_.end()) {
            d.appeared.emplace_back(v.id, v.location);
            log_.vehicles.emplace(v.id, v);
        } else if (was->second != v.location) {
            d.moves.push_back({v.id, was->second, v.location});
        }
    }
    for (const auto& [id, at] : where_)
        if (!now.contains(id)) d.removed.push_back(id);
    where_ = std::move(now);

    const auto colonies = coloniesOf(s);
    for (const auto& [planet, owner] : colonies) {
        const auto was = colonies_.find(planet);
        if (was == colonies_.end() || was->second != owner) d.colonies.emplace_back(planet, owner);
    }
    for (const auto& [planet, owner] : colonies_)
        if (!colonies.contains(planet)) d.coloniesRemoved.push_back(planet);
    colonies_ = colonies;
}

MovementLog MovementRecorder::take(uint32_t turn) {
    log_.turn = turn;
    log_.days.resize(MovementLog::kDays);
    return std::move(log_);
}

MovementLog approximateLog(const std::map<game::VehicleId, game::Location>& before, const game::GameState& now,
                           const std::set<game::VehicleId>& seenNow, uint32_t turn) {
    MovementLog log;
    log.turn = turn;
    log.days.resize(MovementLog::kDays);
    log.startColonies = coloniesOf(now);
    for (const auto& [id, was] : before) {
        const game::Vehicle* v = now.vehicle(id);
        if (!v) continue;   // gone: nothing left to draw it with
        game::Vehicle copy = *v;
        copy.location = was;
        log.vehicles.emplace(id, copy);
        log.start[id] = was;
        if (!seenNow.contains(id) || v->count <= 0) {
            log.days.back().removed.push_back(id);
            continue;
        }
        const game::Location to = v->location;
        if (to.system != was.system) {
            log.days[14].moves.push_back({id, was, to});
        } else if (to.sector != was.sector) {
            // One sector a step along a straight line, the steps spread over the month.
            const int n = std::max(std::abs(to.sector.x - was.sector.x), std::abs(to.sector.y - was.sector.y));
            game::Location at = was;
            for (int k = 1; k <= n; ++k) {
                game::Location next = at;
                next.sector.x = static_cast<decltype(next.sector.x)>(next.sector.x + (to.sector.x > at.sector.x) - (to.sector.x < at.sector.x));
                next.sector.y = static_cast<decltype(next.sector.y)>(next.sector.y + (to.sector.y > at.sector.y) - (to.sector.y < at.sector.y));
                const int day = std::clamp(int(std::lround(double(k) * MovementLog::kDays / n)), 1, MovementLog::kDays);
                log.days[size_t(day - 1)].moves.push_back({id, at, next});
                at = next;
            }
        }
    }
    for (const game::Vehicle& v : now.vehicles)
        if (seenNow.contains(v.id) && !before.contains(v.id) && v.count > 0) {
            log.vehicles.emplace(v.id, v);
            log.days.back().appeared.emplace_back(v.id, v.location);
        }
    return log;
}

// ---- The replay ----

void MovementReplay::setLog(std::shared_ptr<const MovementLog> log) {
    stop();
    log_ = std::move(log);
}

void MovementReplay::reset() {
    day_ = 0;
    where_ = log_ ? log_->start : std::map<game::VehicleId, game::Location>{};
    colonies_ = log_ ? log_->startColonies : std::map<game::ObjectId, game::EmpireId>{};
    headings_.clear();
    anims_.clear();
    animIndex_ = 0;
    started_ = false;
    pending_ = 0;
    rebuildView();
}

void MovementReplay::play() {
    if (!log_) return;
    reset();
    mode_ = Mode::Playing;
}

void MovementReplay::step() {
    if (!log_) return;
    if (mode_ != Mode::Stepping) {
        // The first press shows the start of the turn, Day 0.
        reset();
        mode_ = Mode::Stepping;
        return;
    }
    ++pending_;
}

void MovementReplay::rewind() {
    if (!log_) return;
    reset();
    mode_ = Mode::Stepping;
}

void MovementReplay::playFollowing(std::vector<game::VehicleId> objects) {
    if (!log_ || objects.empty()) return;
    follow_ = std::move(objects);
    followIndex_ = 0;
    reset();
    mode_ = Mode::Following;
}

void MovementReplay::stop() {
    mode_ = Mode::Off;
    anims_.clear();
    animIndex_ = 0;
    view_.clear();
    follow_.clear();
    pending_ = 0;
}

std::optional<game::VehicleId> MovementReplay::following() const {
    if (mode_ != Mode::Following || followIndex_ >= follow_.size()) return std::nullopt;
    return follow_[followIndex_];
}

void MovementReplay::update(const Frame& f) {
    if (mode_ == Mode::Off) return;
    // The day's animations, one after another.
    if (animating()) {
        if (pending_ > 0) {
            animIndex_ = anims_.size();  // a step press finishes the day at once
        } else {
            if (!started_) {
                animStart_ = f.now;
                started_ = true;
            }
            while (animating()) {
                const Animation& a = anims_[animIndex_];
                const double total = a.turnTime + a.slideTime;
                if (f.now - animStart_ < total) break;
                animStart_ += total;
                ++animIndex_;
            }
            if (animating()) return;
        }
        anims_.clear();
        animIndex_ = 0;
        started_ = false;
    }
    if (mode_ == Mode::Stepping) {
        if (pending_ <= 0) return;
        --pending_;
        if (day_ >= kDays) {
            stop();   // the step past day 30 restores the current turn
            return;
        }
        applyDay(f);
        return;
    }
    // Playing: one day per frame (the original redraws the panels after each day, no pause).
    if (day_ < kDays) {
        applyDay(f);
        return;
    }
    if (mode_ == Mode::Following && followIndex_ + 1 < follow_.size()) {
        ++followIndex_;
        reset();
        return;
    }
    stop();
}

void MovementReplay::applyDay(const Frame& f) {
    if (!log_ || day_ >= int(log_->days.size())) {
        ++day_;
        return;
    }
    const MovementLog::Day& d = log_->days[size_t(day_)];
    ++day_;
    for (const MovementLog::Move& m : d.moves) {
        const int before = heading(m.id);
        const bool turns = f.turns && f.turns(m.id);
        const bool inSystem = m.from.system == m.to.system && m.from.sector != m.to.sector;
        // A move within a system turns the mini to its bearing; a warp jump keeps it.
        if (inSystem && turns) headings_[m.id] = map_style::headingStep(m.from.sector, m.to.sector);
        where_[m.id] = m.to;
        const bool animate = f.animate && inSystem && m.from.system == f.shown && (!f.seen || f.seen(m.id));
        if (!animate) continue;
        // A fleet's members make the same move: one animation.
        auto same = std::find_if(anims_.begin(), anims_.end(), [&](const Animation& a) { return a.from == m.from && a.to == m.to; });
        if (same != anims_.end()) {
            same->ids.push_back(m.id);
            continue;
        }
        Animation a;
        a.ids = {m.id};
        a.from = m.from;
        a.to = m.to;
        a.angle0 = turns ? before * 45.0 : 0.0;
        const double target = turns ? heading(m.id) * 45.0 : 0.0;
        const double delta = std::fmod(target - a.angle0 + 540.0, 360.0) - 180.0;  // the shorter way round
        a.angle1 = a.angle0 + delta;
        a.turnTime = std::abs(delta) / 5.0 * kSecondsPerTurnStep;
        const float pixels = float(std::max(std::abs(m.to.sector.x - m.from.sector.x), std::abs(m.to.sector.y - m.from.sector.y))) * f.cellPixels;
        a.slideTime = double(pixels) * kSecondsPerPixel;
        anims_.push_back(std::move(a));
    }
    for (const auto& [id, at] : d.appeared) where_[id] = at;
    for (const game::VehicleId id : d.removed) where_.erase(id);
    for (const auto& [planet, owner] : d.colonies) colonies_[planet] = owner;
    for (const game::ObjectId planet : d.coloniesRemoved) colonies_.erase(planet);
    animIndex_ = 0;
    started_ = false;
    rebuildView();
}

void MovementReplay::rebuildView() {
    view_.clear();
    if (!log_) return;
    for (const auto& [id, at] : where_) {
        const auto it = log_->vehicles.find(id);
        if (it == log_->vehicles.end()) continue;
        game::Vehicle v = it->second;
        v.location = at;
        view_.push_back(std::move(v));
    }
}

std::optional<MovementReplay::Motion> MovementReplay::motion(game::VehicleId v, double now) const {
    for (size_t i = animIndex_; i < anims_.size(); ++i) {
        const Animation& a = anims_[i];
        if (std::find(a.ids.begin(), a.ids.end(), v) == a.ids.end()) continue;
        if (i > animIndex_ || !started_) return Motion{cellCenter(a.from.sector), a.angle0};
        const double e = now - animStart_;
        if (e < a.turnTime) {
            const double steps = std::floor(e / kSecondsPerTurnStep);
            return Motion{cellCenter(a.from.sector), a.angle0 + (a.angle1 >= a.angle0 ? 5.0 : -5.0) * steps};
        }
        const float t = a.slideTime > 0 ? std::clamp(float((e - a.turnTime) / a.slideTime), 0.0f, 1.0f) : 1.0f;
        return Motion{lerp(cellCenter(a.from.sector), cellCenter(a.to.sector), t), a.angle1};
    }
    return std::nullopt;
}

int MovementReplay::heading(game::VehicleId v) const {
    // A mini not yet seen moving in the replay faces up (as a new ship does).
    const auto it = headings_.find(v);
    return it == headings_.end() ? 0 : it->second;
}

std::optional<game::EmpireId> MovementReplay::colonyOwner(game::ObjectId planet) const {
    const auto it = colonies_.find(planet);
    if (it == colonies_.end()) return std::nullopt;
    return it->second;
}

} // namespace opense4::client::classic
