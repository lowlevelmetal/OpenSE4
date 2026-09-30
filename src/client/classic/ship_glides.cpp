#include "client/classic/ship_glides.hpp"

#include <algorithm>
#include <cmath>

namespace opense4::client::classic {

namespace {

Vec2 cellCenter(game::Sector s) { return {float(s.x) + 0.5f, float(s.y) + 0.5f}; }

} // namespace

void ShipGlides::track(double now, game::SystemId shown, bool enabled, std::span<const Seen> visible) {
    std::erase_if(glides_, [&](const auto& g) { return now >= g.second.start + g.second.duration; });
    if (!enabled) glides_.clear();
    const bool animate = enabled && shown == lastShown_;

    std::map<game::VehicleId, game::Location> seen;
    for (const Seen& v : visible) {
        seen.emplace(v.id, v.location);
        if (!animate) continue;
        const auto was = lastSeen_.find(v.id);
        if (was == lastSeen_.end()) continue;
        if (was->second.system != shown || v.location.system != shown || was->second.sector == v.location.sector) continue;
        // Moved again while still gliding: carry on from where it is drawn.
        const Glide* current = find(v.id, now);
        const Vec2 from = current ? position(*current, now) : cellCenter(was->second.sector);
        const Vec2 to = cellCenter(v.location.sector);
        const double squares = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
        glides_[v.id] = Glide{from, to, now, std::clamp(kSecondsPerSquare * squares, kMinSeconds, kMaxSeconds)};
    }
    lastSeen_ = std::move(seen);
    lastShown_ = shown;
}

const ShipGlides::Glide* ShipGlides::find(game::VehicleId v, double now) const {
    const auto it = glides_.find(v);
    if (it == glides_.end() || now >= it->second.start + it->second.duration) return nullptr;
    return &it->second;
}

Vec2 ShipGlides::position(const Glide& g, double now) {
    const float t = std::clamp(static_cast<float>((now - g.start) / g.duration), 0.0f, 1.0f);
    return lerp(g.from, g.to, t * t * (3.0f - 2.0f * t));
}

} // namespace opense4::client::classic
