#pragma once

#include "client/camera.hpp"
#include "client/view_context.hpp"

#include <optional>
#include <vector>

namespace opense4::client {

// Strategic map: star systems, warp lanes, territory and fleets.
class GalaxyView {
public:
    void fitToGalaxy(const sim::GameState& s, Vec2 viewport);
    void focus(Vec2 galaxyPos) { camera.flyTo(galaxyPos); }

    void update(ViewContext& ctx, NavRequest& nav);
    void draw(ViewContext& ctx, gfx::Renderer2D& r) const;
    void drawLabels(ViewContext& ctx) const;

    Camera2D camera;

private:
    std::optional<sim::SystemId> pick(const sim::GameState& s, Vec2 screen) const;
    float starRadius(sim::StarClass c) const;  // world units, never below a few pixels

    struct Nebula {
        Vec2 center;
        float radius;
        Color color;
    };
    std::vector<Nebula> nebulae_;
    std::optional<sim::SystemId> hovered_;
    bool pressedOnMap_ = false;
};

} // namespace opense4::client
