#include "client/classic/ship_glides.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace opense4::client::classic {

namespace {

Vec2 cellCenter(game::Sector s) { return {float(s.x) + 0.5f, float(s.y) + 0.5f}; }

// One frame on, once its wait is over; true when the glide is over: its last
// frame shows the vehicle on its new square, where it is then drawn as usual,
// after the pause when there is one.
bool advance(ShipGlides::Glide& g, double now) {
    if (g.done) return true;
    const int frames = g.turnFrames + g.slideFrames;
    if (g.frame + 1 >= frames) {
        // The pause after the step, on the new square.
        if (g.pauseUntil < 0.0) g.pauseUntil = now + g.pause;
        g.frame = frames;
        g.done = now >= g.pauseUntil;
        return g.done;
    }
    const double wait = g.frame < g.turnFrames ? ShipGlides::kSecondsAfterTurnFrame : ShipGlides::kSecondsAfterSlideFrame;
    if (now - g.lastFrameAt < wait) return false;
    g.lastFrameAt = now;
    ++g.frame;
    return false;
}

} // namespace

void ShipGlides::track(double now, game::SystemId shown, bool enabled, std::span<const Seen> visible, float cellPixels, double pauseSeconds) {
    if (!enabled) glides_.clear();
    const bool animate = enabled && shown == lastShown_;
    // The glides under way play one frame on, after the moves of this frame
    // have started theirs from where the vehicles were drawn.
    std::map<game::VehicleId, Glide> running = std::move(glides_);
    glides_.clear();

    std::map<game::VehicleId, game::Location> seen;
    std::map<game::VehicleId, int> headings;
    for (const Seen& v : visible) {
        seen.emplace(v.id, v.location);
        headings.emplace(v.id, v.turns ? v.heading % 8 : 0);
        if (!animate) continue;
        const auto was = lastSeen_.find(v.id);
        if (was == lastSeen_.end()) continue;
        if (was->second.system != shown || v.location.system != shown || was->second.sector == v.location.sector) continue;
        // Moved again while still gliding: carry on from where it is drawn.
        const auto current = running.find(v.id);
        const Vec2 from = current != running.end() ? position(current->second) : cellCenter(was->second.sector);
        const auto oldHeading = lastHeadings_.find(v.id);
        const double angle0 = current != running.end() ? angle(current->second)
                                                        : (oldHeading != lastHeadings_.end() ? oldHeading->second * 45.0 : 0.0);
        if (current != running.end()) running.erase(current);
        Glide g;
        g.from = from;
        g.to = cellCenter(v.location.sector);
        g.angle0 = angle0;
        // The shorter way round to the new heading, clockwise for a half-turn.
        const double target = v.turns ? double(v.heading % 8) * 45.0 : 0.0;
        double delta = std::fmod(target - angle0, 360.0);
        if (delta < 0.0) delta += 360.0;
        if (delta > 180.0) delta -= 360.0;
        g.angle1 = angle0 + delta;
        g.turnFrames = int(std::lround(std::abs(delta) / kDegreesPerTurnFrame));
        const float squares = std::max(std::abs(g.to.x - g.from.x), std::abs(g.to.y - g.from.y));
        g.slideFrames = std::max(1, int(std::lround(double(squares) * double(cellPixels))));
        // The pause comes after every step the move stands for.
        const int steps = std::max(std::abs(v.location.sector.x - was->second.sector.x), std::abs(v.location.sector.y - was->second.sector.y));
        g.pause = std::max(0.0, pauseSeconds) * double(std::max(1, steps));
        g.start = now;
        g.lastFrameAt = now;
        glides_[v.id] = g;
    }
    for (auto& [id, g] : running)
        if (!advance(g, now)) glides_.emplace(id, g);
    lastSeen_ = std::move(seen);
    lastHeadings_ = std::move(headings);
    lastShown_ = shown;
}

const ShipGlides::Glide* ShipGlides::find(game::VehicleId v) const {
    const auto it = glides_.find(v);
    return it == glides_.end() ? nullptr : &it->second;
}

Vec2 ShipGlides::position(const Glide& g) {
    if (g.frame < g.turnFrames) return g.from;
    const int slid = std::min(g.frame - g.turnFrames + 1, g.slideFrames);
    const float t = g.slideFrames > 0 ? float(slid) / float(g.slideFrames) : 1.0f;
    return lerp(g.from, g.to, std::clamp(t, 0.0f, 1.0f));
}

double ShipGlides::angle(const Glide& g) {
    if (g.frame >= g.turnFrames) return g.angle1;
    const double step = g.angle1 >= g.angle0 ? kDegreesPerTurnFrame : -kDegreesPerTurnFrame;
    return g.angle0 + step * (g.frame + 1);
}

} // namespace opense4::client::classic
