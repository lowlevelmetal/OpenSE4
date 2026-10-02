#include "client/classic/ship_glides.hpp"

#include "client/classic/map_style.hpp"

#include <algorithm>
#include <cmath>

namespace opense4::client::classic {

namespace {

Vec2 cellCenter(game::Sector s) { return {float(s.x) + 0.5f, float(s.y) + 0.5f}; }

} // namespace

void ShipGlides::track(double now, game::SystemId shown, bool enabled, std::span<const Seen> visible, double pauseSeconds) {
    std::erase_if(glides_, [&](const auto& g) { return now >= g.second.start + g.second.duration; });
    if (!enabled) glides_.clear();
    const bool animate = enabled && shown == lastShown_;

    std::map<game::VehicleId, game::Location> seen;
    std::map<game::VehicleId, int> headings;
    for (const Seen& v : visible) {
        seen.emplace(v.id, v.location);
        const auto was = lastSeen_.find(v.id);
        // A move within a system turns the mini; a warp keeps its heading.
        if (const auto h = headings_.find(v.id); h != headings_.end()) headings.emplace(v.id, h->second);
        if (was != lastSeen_.end() && was->second.system == v.location.system && was->second.sector != v.location.sector)
            headings[v.id] = map_style::headingStep(was->second.sector, v.location.sector);
        if (!animate) continue;
        if (was == lastSeen_.end()) continue;
        if (was->second.system != shown || v.location.system != shown || was->second.sector == v.location.sector) continue;
        // Moved again while still gliding: carry on from where it is drawn.
        const Glide* current = find(v.id, now);
        const Vec2 from = current ? position(*current, now) : cellCenter(was->second.sector);
        const Vec2 to = cellCenter(v.location.sector);
        const double squares = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
        const double move = std::clamp(kSecondsPerSquare * squares, kMinSeconds, kMaxSeconds);
        // The pause after each one-square step (Settings.txt `System Ship
        // Movement Delay Milliseconds`, spec 06 §1.9): a glide resumed half way
        // counts its part of a square as a step.
        const int steps = std::max(1, static_cast<int>(std::ceil(squares - 1e-6)));
        const double pause = std::max(0.0, pauseSeconds);
        glides_[v.id] = Glide{from, to, now, move + pause * steps, steps, pause};
    }
    lastSeen_ = std::move(seen);
    headings_ = std::move(headings);
    lastShown_ = shown;
}

const ShipGlides::Glide* ShipGlides::find(game::VehicleId v, double now) const {
    const auto it = glides_.find(v);
    if (it == glides_.end() || now >= it->second.start + it->second.duration) return nullptr;
    return &it->second;
}

Vec2 ShipGlides::position(const Glide& g, double now) {
    auto ease = [](float t) { return t * t * (3.0f - 2.0f * t); };
    if (g.pause <= 0.0 || g.squares <= 0) {
        const float t = std::clamp(static_cast<float>((now - g.start) / g.duration), 0.0f, 1.0f);
        return lerp(g.from, g.to, ease(t));
    }
    // Square by square: each step's share of the move, then the pause.
    const double step = (g.duration - g.pause * g.squares) / g.squares;
    const double elapsed = std::max(0.0, now - g.start);
    const double segment = step + g.pause;
    const int done = segment > 0.0 ? std::min(g.squares - 1, static_cast<int>(elapsed / segment)) : g.squares - 1;
    const double within = elapsed - segment * done;
    const float t = step > 0.0 ? std::clamp(static_cast<float>(within / step), 0.0f, 1.0f) : 1.0f;
    return lerp(g.from, g.to, (static_cast<float>(done) + ease(t)) / static_cast<float>(g.squares));
}

int ShipGlides::heading(game::VehicleId v) const {
    const auto it = headings_.find(v);
    return it == headings_.end() ? 0 : it->second;
}

} // namespace opense4::client::classic
