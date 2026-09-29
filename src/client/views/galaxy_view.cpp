#include "client/views/galaxy_view.hpp"

#include "client/palette.hpp"
#include "core/rng.hpp"
#include "sim/pathfinding.hpp"
#include "sim/rules.hpp"

#include <imgui.h>

#include <cmath>
#include <map>

namespace opense4::client {

namespace {

constexpr float kStarBaseRadius = 7.0f;  // world units for a sun-like star

// Visible ships in a system grouped by owner, in empire order.
std::map<sim::EmpireId, int> visibleFleets(const GameSession& session, sim::SystemId sys) {
    std::map<sim::EmpireId, int> fleets;
    for (const sim::Ship& ship : session.state().ships)
        if (ship.location.system == sys && session.canSee(ship)) ++fleets[ship.owner];
    return fleets;
}

std::vector<sim::EmpireId> colonyOwners(const sim::GameState& s, sim::SystemId sys) {
    std::vector<sim::EmpireId> owners;
    for (sim::PlanetId pid : s.system(sys).planets) {
        const sim::Planet& p = s.planet(pid);
        if (p.colony && std::find(owners.begin(), owners.end(), p.colony->owner) == owners.end()) owners.push_back(p.colony->owner);
    }
    std::sort(owners.begin(), owners.end());
    return owners;
}

} // namespace

void GalaxyView::fitToGalaxy(const sim::GameState& s, Vec2 viewport) {
    Rect bounds{s.systems.front().position, s.systems.front().position};
    for (const sim::StarSystem& sys : s.systems) {
        bounds.min = {std::min(bounds.min.x, sys.position.x), std::min(bounds.min.y, sys.position.y)};
        bounds.max = {std::max(bounds.max.x, sys.position.x), std::max(bounds.max.y, sys.position.y)};
    }
    // Decorative nebulae, seeded by the galaxy so they are stable for a game.
    nebulae_.clear();
    Rng rng(s.galaxy.seed ^ 0x6e6562756c6165ull);
    static constexpr Color kNebulaColors[] = {Color::hex(0x5b3fa8), Color::hex(0x2f5fa8), Color::hex(0x1f8a8a),
                                              Color::hex(0x8a3f7a), Color::hex(0x3f4fb8)};
    const int count = 5 + static_cast<int>(s.systems.size() / 8);
    for (int i = 0; i < count; ++i) {
        const sim::StarSystem& anchor = s.systems[rng.below(s.systems.size())];
        const Vec2 offset = fromAngle(rng.unit() * kTau) * rng.rangeF(0.0f, 120.0f);
        nebulae_.push_back({anchor.position + offset, rng.rangeF(140.0f, 320.0f),
                            kNebulaColors[rng.below(std::size(kNebulaColors))].withAlpha(rng.rangeF(0.05f, 0.10f))});
    }
    camera.viewport = viewport;
    camera.minZoom = 0.05f;
    camera.maxZoom = 8.0f;
    camera.fit(bounds.expanded(40.0f), 60.0f);
}

float GalaxyView::starRadius(sim::StarClass c) const {
    return std::max(kStarBaseRadius * palette::starScale(c), 2.5f / camera.zoom);
}

std::optional<sim::SystemId> GalaxyView::pick(const sim::GameState& s, Vec2 screen) const {
    std::optional<sim::SystemId> best;
    float bestDist = 18.0f;  // pixels
    for (const sim::StarSystem& sys : s.systems) {
        const float d = distance(camera.worldToScreen(sys.position), screen);
        if (d < bestDist) {
            bestDist = d;
            best = sys.id;
        }
    }
    return best;
}

void GalaxyView::update(ViewContext& ctx, NavRequest& nav) {
    const ImGuiIO& io = ImGui::GetIO();
    const sim::GameState& s = ctx.session.state();
    camera.viewport = {static_cast<float>(ctx.frame.width), static_cast<float>(ctx.frame.height)};
    const Vec2 mouse = Vec2{io.MousePos.x, io.MousePos.y} * ctx.fbScale;
    const bool mouseFree = !io.WantCaptureMouse && ImGui::IsMousePosValid();

    hovered_.reset();
    if (mouseFree) {
        if (io.MouseWheel != 0.0f) camera.zoomAt(mouse, std::pow(1.2f, io.MouseWheel));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) pressedOnMap_ = true;
        hovered_ = pick(s, mouse);
    }
    if (pressedOnMap_ && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
        camera.pan(Vec2{io.MouseDelta.x, io.MouseDelta.y} * ctx.fbScale);

    if (pressedOnMap_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const bool wasClick = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < io.MouseDragThreshold * io.MouseDragThreshold;
        if (wasClick) {
            if (hovered_) ctx.selection = Selection::ofSystem(*hovered_);
            else ctx.selection = {};
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) pressedOnMap_ = false;

    if (mouseFree && hovered_ && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) nav.openSystem = *hovered_;

    // Right click on a system: send the selected ship there.
    if (mouseFree && hovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ctx.selection.kind == Selection::Kind::Ship) {
        const sim::Ship* ship = s.findShip(ctx.selection.ship);
        if (ship && ship->owner == ctx.session.player()) {
            const sim::SystemId target = *hovered_;
            if (target == ship->location.system) {
                ctx.session.setStatus("The ship is already in that system. Open the system map to move it within the system.");
            } else if (auto path = sim::findPathTo(s, ship->location, [&](sim::Location l) { return l.system == target; },
                                                   &ctx.session.playerEmpire())) {
                ctx.session.issue(sim::cmd::MoveShip{ship->id, path->back()});
            } else {
                ctx.session.setStatus("No known route to that system.");
            }
        }
    }

    if (!io.WantCaptureKeyboard) {
        Vec2 dir;
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) dir.y -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) dir.y += 1;
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) dir.x -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) dir.x += 1;
        if (dir.x != 0 || dir.y != 0) camera.pan(-dir * (700.0f * ctx.dt * ctx.fbScale));
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) fitToGalaxy(s, camera.viewport);
    }
    camera.update(ctx.dt);
}

void GalaxyView::draw(ViewContext& ctx, gfx::Renderer2D& r) const {
    const GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    const float px = 1.0f / camera.zoom;
    const float t = static_cast<float>(ctx.time);
    r.begin(camera.transform(), ctx.frame, camera.zoom);

    for (const Nebula& n : nebulae_) {
        r.glow(n.center, n.radius, n.color, 1.3f);
        r.glow(n.center + Vec2{n.radius * 0.3f, -n.radius * 0.2f}, n.radius * 0.6f, n.color, 1.6f);
    }

    // Faint glow around colonized space, tinted by owner.
    for (const sim::Planet& p : s.planets) {
        if (!p.colony || !session.explored(p.system)) continue;
        const Color c = palette::empireColor(s.empire(p.colony->owner).color, 0.10f);
        r.glow(s.system(p.system).position, 95.0f, c, 1.6f);
    }

    // Warp lanes. Known when either end has been explored.
    for (const sim::WarpPoint& wp : s.warpPoints) {
        if (wp.id > wp.exit) continue;
        const sim::SystemId a = wp.system;
        const sim::SystemId b = s.warpPoint(wp.exit).system;
        const bool knownA = session.explored(a), knownB = session.explored(b);
        if (!knownA && !knownB) continue;
        const Vec2 pa = s.system(a).position, pb = s.system(b).position;
        const Vec2 dir = normalize(pb - pa);
        const Vec2 from = pa + dir * starRadius(s.system(a).star) * 1.8f;
        const Vec2 to = pb - dir * starRadius(s.system(b).star) * 1.8f;
        if (knownA && knownB) r.line(from, to, 1.5f, palette::kLane);
        else r.dashedLine(from, to, 1.2f, 6.0f, 5.0f, palette::kLaneUnknown);
    }

    // Planned route of the selected ship.
    if (ctx.selection.kind == Selection::Kind::Ship) {
        if (const sim::Ship* ship = s.findShip(ctx.selection.ship); ship && session.canSee(*ship)) {
            sim::SystemId prev = ship->location.system;
            for (const sim::Location& step : ship->path) {
                if (step.system == prev) continue;
                r.dashedLine(s.system(prev).position, s.system(step.system).position, 2.5f, 8.0f, 5.0f, palette::kPath);
                prev = step.system;
            }
            if (prev != ship->location.system) r.ring(s.system(prev).position, starRadius(s.system(prev).star) + 10.0f * px, 2.0f, palette::kPath);
        }
    }

    for (const sim::StarSystem& sys : s.systems) {
        const bool known = session.explored(sys.id);
        const float radius = starRadius(sys.star);
        const Color base = palette::starColor(sys.star);
        if (known) {
            r.glow(sys.position, radius * 4.5f, base.withAlpha(0.55f), 2.2f);
            r.disc(sys.position, radius, base);
            r.disc(sys.position, radius * 0.55f, lerp(base, Color{}, 0.6f));
        } else {
            // Unexplored: the star is visible from afar, but dimmer and without detail.
            r.glow(sys.position, radius * 3.0f, base.withAlpha(0.22f), 2.2f);
            r.disc(sys.position, radius * 0.75f, lerp(palette::kUnexplored, base, 0.45f));
        }

        // Ownership rings.
        float ringRadius = radius + 5.0f * px;
        for (sim::EmpireId owner : colonyOwners(s, sys.id)) {
            if (!known) break;
            r.ring(sys.position, ringRadius, 2.0f, palette::empireColor(s.empire(owner).color));
            ringRadius += 4.0f * px;
        }

        // Fleet markers: one small arrow per empire present.
        int slot = 0;
        for (const auto& [owner, count] : visibleFleets(session, sys.id)) {
            const Vec2 p = sys.position + Vec2{radius + 9.0f * px, -radius - 4.0f * px + static_cast<float>(slot) * 11.0f * px};
            const float sz = 5.0f * px;
            r.triangle(p + Vec2{-sz, -sz}, p + Vec2{sz * 1.2f, 0}, p + Vec2{-sz, sz}, palette::empireColor(s.empire(owner).color));
            ++slot;
        }
    }

    // Hover and selection highlights.
    if (hovered_) {
        const sim::StarSystem& sys = s.system(*hovered_);
        r.ring(sys.position, starRadius(sys.star) + 12.0f * px, 1.5f, palette::kHover);
    }
    if (ctx.selection.kind != Selection::Kind::None && ctx.selection.system.valid()) {
        const sim::StarSystem& sys = s.system(ctx.selection.system);
        const float pulse = 0.75f + 0.25f * std::sin(t * 4.0f);
        r.ring(sys.position, starRadius(sys.star) + 14.0f * px, 2.0f, palette::kSelection.withAlpha(pulse));
    }
    r.flush();
}

void GalaxyView::drawLabels(ViewContext& ctx) const {
    const GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float spacingPx = 70.0f * camera.zoom / ctx.fbScale;
    if (spacingPx < 38.0f) return;  // too crowded to label
    const float fontSize = std::clamp(spacingPx * 0.2f, 12.0f, 16.0f);

    for (const sim::StarSystem& sys : s.systems) {
        const bool known = session.explored(sys.id);
        const Vec2 screen = camera.worldToScreen(sys.position) / ctx.fbScale;
        const float radiusPx = starRadius(sys.star) * camera.zoom / ctx.fbScale;
        const auto owners = known ? colonyOwners(s, sys.id) : std::vector<sim::EmpireId>{};
        Color c = known ? Color::hex(0xd6e2ee) : Color::hex(0x6b7888);
        if (!owners.empty()) c = lerp(palette::empireColor(s.empire(owners.front()).color), Color{}, 0.35f);
        const ImVec2 size = ctx.fonts.medium->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, sys.name.c_str());
        const ImVec2 pos{screen.x - size.x * 0.5f, screen.y + radiusPx + 12.0f + static_cast<float>(owners.size()) * 4.0f};
        dl->AddText(ctx.fonts.medium, fontSize, ImVec2{pos.x + 1, pos.y + 1}, IM_COL32(0, 0, 0, 160), sys.name.c_str());
        dl->AddText(ctx.fonts.medium, fontSize, pos, c.toRgba8(), sys.name.c_str());
    }

    // Tooltip for the hovered system.
    if (hovered_ && !ImGui::GetIO().WantCaptureMouse && ImGui::IsMousePosValid()) {
        const sim::StarSystem& sys = s.system(*hovered_);
        ImGui::BeginTooltip();
        ImGui::PushFont(ctx.fonts.bold, 0.0f);
        ImGui::TextUnformatted(sys.name.c_str());
        ImGui::PopFont();
        if (session.explored(sys.id)) {
            ImGui::TextDisabled("%s, %zu planets, %zu warp points", std::string(sim::displayName(sys.star)).c_str(), sys.planets.size(),
                                sys.warpPoints.size());
            for (sim::EmpireId owner : colonyOwners(s, sys.id))
                ImGui::TextColored(ImColor(palette::empireColor(s.empire(owner).color).toRgba8()), "%s colony", s.empire(owner).name.c_str());
        } else {
            ImGui::TextDisabled("Unexplored");
        }
        for (const auto& [owner, count] : visibleFleets(session, sys.id))
            ImGui::TextColored(ImColor(palette::empireColor(s.empire(owner).color).toRgba8()), "%d %s ship%s", count,
                               s.empire(owner).name.c_str(), count == 1 ? "" : "s");
        if (ctx.selection.kind == Selection::Kind::Ship) ImGui::TextDisabled("Right-click to send the selected ship here");
        else ImGui::TextDisabled("Double-click to open");
        ImGui::EndTooltip();
    }
}

} // namespace opense4::client
