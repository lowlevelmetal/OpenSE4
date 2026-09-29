#pragma once

#include "client/camera.hpp"
#include "client/view_context.hpp"

#include <optional>

namespace opense4::client {

// Tactical map of one star system: the sector grid with the star, planets,
// warp points and ships. Ships are ordered around sector by sector.
class SystemView {
public:
    static constexpr float kSector = 64.0f;  // world units per sector

    void open(const sim::GameState& s, sim::SystemId system, Vec2 viewport);
    sim::SystemId system() const { return system_; }
    void centerOn(sim::SectorPos sector) { camera.flyTo(sectorCenter(sector)); }

    void update(ViewContext& ctx, NavRequest& nav);
    void draw(ViewContext& ctx, gfx::Renderer2D& r) const;
    void drawLabels(ViewContext& ctx) const;

    Camera2D camera;

private:
    static Vec2 sectorCenter(sim::SectorPos p) { return {static_cast<float>(p.x) * kSector, static_cast<float>(p.y) * kSector}; }
    std::optional<sim::SectorPos> sectorAt(const sim::GameState& s, Vec2 world) const;
    std::vector<Selection> selectablesAt(const GameSession& session, sim::SectorPos sector) const;
    void rightClick(ViewContext& ctx, sim::SectorPos sector);

    sim::SystemId system_;
    std::optional<sim::SectorPos> hovered_;
    bool pressedOnMap_ = false;
};

} // namespace opense4::client
