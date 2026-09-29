#include "client/views/system_view.hpp"

#include "client/palette.hpp"
#include "sim/pathfinding.hpp"
#include "sim/rules.hpp"

#include <imgui.h>

#include <cmath>
#include <format>
#include <map>

namespace opense4::client {

namespace {

// Visible ships in one sector, grouped by owner (player first).
std::vector<std::pair<sim::EmpireId, std::vector<const sim::Ship*>>> shipGroups(const GameSession& session, sim::Location loc) {
    std::map<sim::EmpireId, std::vector<const sim::Ship*>> byOwner;
    for (const sim::Ship* ship : sim::shipsAt(session.state(), loc))
        if (session.canSee(*ship)) byOwner[ship->owner].push_back(ship);
    std::vector<std::pair<sim::EmpireId, std::vector<const sim::Ship*>>> groups;
    if (auto it = byOwner.find(session.player()); it != byOwner.end()) groups.emplace_back(it->first, it->second);
    for (auto& [owner, ships] : byOwner)
        if (owner != session.player()) groups.emplace_back(owner, ships);
    return groups;
}

Vec2 shipIconPos(Vec2 sectorCenter, size_t group) {
    return sectorCenter + Vec2{-0.34f + 0.2f * static_cast<float>(group), -0.34f} * SystemView::kSector;
}

} // namespace

void SystemView::open(const sim::GameState& s, sim::SystemId system, Vec2 viewport) {
    system_ = system;
    hovered_.reset();
    camera.viewport = viewport;
    camera.minZoom = 0.15f;
    camera.maxZoom = 6.0f;
    const float half = (static_cast<float>(s.sectorRadius()) + 0.5f) * kSector;
    camera.fit(Rect{{-half, -half}, {half, half}}, 70.0f);
}

std::optional<sim::SectorPos> SystemView::sectorAt(const sim::GameState& s, Vec2 world) const {
    const sim::SectorPos p{static_cast<int>(std::floor(world.x / kSector + 0.5f)), static_cast<int>(std::floor(world.y / kSector + 0.5f))};
    if (!s.inBounds(p)) return std::nullopt;
    return p;
}

std::vector<Selection> SystemView::selectablesAt(const GameSession& session, sim::SectorPos sector) const {
    const sim::GameState& s = session.state();
    const sim::Location loc{system_, sector};
    std::vector<Selection> out;
    for (const auto& [owner, ships] : shipGroups(session, loc))
        for (const sim::Ship* ship : ships) out.push_back(Selection::ofShip(*ship));
    if (session.explored(system_)) {
        if (const sim::Planet* p = sim::planetAt(s, loc)) out.push_back(Selection::ofPlanet(*p));
        if (const sim::WarpPoint* w = sim::warpPointAt(s, loc)) out.push_back(Selection::ofWarpPoint(*w));
    }
    return out;
}

void SystemView::rightClick(ViewContext& ctx, sim::SectorPos sector) {
    GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    if (ctx.selection.kind != Selection::Kind::Ship) return;
    const sim::Ship* ship = s.findShip(ctx.selection.ship);
    if (!ship) return;
    if (ship->owner != session.player()) {
        session.setStatus("You can only give orders to your own ships.");
        return;
    }
    const sim::Location target{system_, sector};
    if (const sim::Planet* planet = sim::planetAt(s, target)) {
        if (!s.statsOf(*ship).colonizes.empty() && sim::colonizeProblem(s, *ship, *planet).empty()) {
            session.issue(sim::cmd::Colonize{ship->id, planet->id});
            return;
        }
    }
    session.issue(sim::cmd::MoveShip{ship->id, target});
}

void SystemView::update(ViewContext& ctx, NavRequest& nav) {
    const ImGuiIO& io = ImGui::GetIO();
    const sim::GameState& s = ctx.session.state();
    camera.viewport = {static_cast<float>(ctx.frame.width), static_cast<float>(ctx.frame.height)};
    const Vec2 mouse = Vec2{io.MousePos.x, io.MousePos.y} * ctx.fbScale;
    const bool mouseFree = !io.WantCaptureMouse && ImGui::IsMousePosValid();

    hovered_.reset();
    if (mouseFree) {
        if (io.MouseWheel != 0.0f) camera.zoomAt(mouse, std::pow(1.2f, io.MouseWheel));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) pressedOnMap_ = true;
        hovered_ = sectorAt(s, camera.screenToWorld(mouse));
    }
    if (pressedOnMap_ && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
        camera.pan(Vec2{io.MouseDelta.x, io.MouseDelta.y} * ctx.fbScale);

    if (pressedOnMap_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const bool wasClick = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < io.MouseDragThreshold * io.MouseDragThreshold;
        if (wasClick && hovered_) {
            // Clicking the same sector again cycles through everything in it.
            const std::vector<Selection> items = selectablesAt(ctx.session, *hovered_);
            if (items.empty()) {
                ctx.selection = Selection::ofSystem(system_);
            } else {
                auto it = std::find(items.begin(), items.end(), ctx.selection);
                ctx.selection = (it == items.end() || std::next(it) == items.end()) ? items.front() : *std::next(it);
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) pressedOnMap_ = false;

    if (mouseFree && hovered_ && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (const sim::WarpPoint* wp = sim::warpPointAt(s, {system_, *hovered_}); wp && ctx.session.explored(system_)) {
            const sim::WarpPoint& exit = s.warpPoint(wp->exit);
            nav.focus = sim::Location{exit.system, exit.sector};
        }
    }
    if (mouseFree && hovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) rightClick(ctx, *hovered_);

    if (!io.WantCaptureKeyboard) {
        Vec2 dir;
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) dir.y -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) dir.y += 1;
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) dir.x -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) dir.x += 1;
        if (dir.x != 0 || dir.y != 0) camera.pan(-dir * (700.0f * ctx.dt * ctx.fbScale));
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) open(s, system_, camera.viewport);
    }
    camera.update(ctx.dt);
}

void SystemView::draw(ViewContext& ctx, gfx::Renderer2D& r) const {
    const GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    const sim::StarSystem& sys = s.system(system_);
    const bool known = session.explored(system_);
    const float px = 1.0f / camera.zoom;
    const float t = static_cast<float>(ctx.time);
    const int radius = s.sectorRadius();
    const float half = (static_cast<float>(radius) + 0.5f) * kSector;
    r.begin(camera.transform(), ctx.frame, camera.zoom);

    // Sector grid.
    r.rect(Rect{{-half, -half}, {half, half}}, Color::hex(0x0b1320, 0.55f));
    for (int i = -radius; i <= radius + 1; ++i) {
        const float x = (static_cast<float>(i) - 0.5f) * kSector;
        r.line({x, -half}, {x, half}, 1.0f, palette::kGrid);
        r.line({-half, x}, {half, x}, 1.0f, palette::kGrid);
    }
    r.rectOutline(Rect{{-half, -half}, {half, half}}, 1.5f, palette::kGridBorder);

    if (hovered_) {
        const Vec2 c = sectorCenter(*hovered_);
        r.rect(Rect::fromCenter(c, {kSector * 0.5f, kSector * 0.5f}), Color::hex(0xffffff, 0.05f));
    }

    // Star.
    const Color starCol = palette::starColor(sys.star);
    const float starR = kSector * 0.3f * palette::starScale(sys.star);
    r.glow({}, kSector * 2.2f * palette::starScale(sys.star), starCol.withAlpha(0.8f), 2.4f);
    r.disc({}, starR, starCol);
    r.disc({}, starR * 0.65f, lerp(starCol, Color{}, 0.7f));

    if (known) {
        for (sim::PlanetId pid : sys.planets) r.ring({}, length(sectorCenter(s.planet(pid).sector)), 1.0f, palette::kOrbit);

        for (sim::WarpPointId wid : sys.warpPoints) {
            const Vec2 c = sectorCenter(s.warpPoint(wid).sector);
            r.glow(c, kSector * 0.55f, palette::kWarp.withAlpha(0.55f), 1.8f);
            r.ring(c, kSector * 0.3f, 2.0f, palette::kWarp);
            r.ring(c, kSector * 0.17f, 1.5f, lerp(palette::kWarp, Color{}, 0.4f));
            for (int k = 0; k < 3; ++k) {
                const float a = t * 1.4f + static_cast<float>(k) * kTau / 3.0f;
                r.disc(c + fromAngle(a) * (kSector * 0.3f), 2.5f * px, Color::hex(0xe8d8ff));
            }
        }

        for (sim::PlanetId pid : sys.planets) {
            const sim::Planet& p = s.planet(pid);
            const Vec2 c = sectorCenter(p.sector);
            const float pr = kSector * palette::planetScale(p.size);
            const Vec2 toStar = normalize(-c);
            const Color atmo = palette::atmosphereColor(p.atmosphere);
            if (atmo.a > 0.0f) r.glow(c, pr * 1.45f, atmo, 1.4f);
            r.disc(c, pr, palette::planetColor(p.surface, p.atmosphere));
            r.glow(c + toStar * (pr * 0.35f), pr * 0.85f, Color::hex(0xffffff, 0.22f), 1.6f);
            if (p.colony) {
                const Color owner = palette::empireColor(s.empire(p.colony->owner).color);
                r.ring(c, pr + 5.0f * px, 2.0f, owner);
                if (s.empire(p.colony->owner).homeworld == p.id) r.ring(c, pr + 9.0f * px, 1.5f, owner.withAlpha(0.7f));
            }
        }
    }

    // Planned path of the selected ship, with a tick at each turn's end.
    if (ctx.selection.kind == Selection::Kind::Ship) {
        if (const sim::Ship* ship = s.findShip(ctx.selection.ship); ship && session.canSee(*ship) && !ship->path.empty()) {
            const int speed = std::max(1, s.statsOf(*ship).speed);
            sim::Location prev = ship->location;
            int step = 0;
            for (const sim::Location& loc : ship->path) {
                ++step;
                if (loc.system != system_ || prev.system != system_) {
                    prev = loc;
                    continue;
                }
                r.line(sectorCenter(prev.sector), sectorCenter(loc.sector), 2.0f, palette::kPath.withAlpha(0.7f));
                if (step % speed == 0) r.ring(sectorCenter(loc.sector), 5.0f * px, 2.0f, palette::kPath);
                prev = loc;
            }
            const sim::Location& dest = ship->path.back();
            if (dest.system == system_) r.ring(sectorCenter(dest.sector), kSector * 0.4f, 2.0f, palette::kPath);
        }
    }

    // Ships.
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            const sim::SectorPos sp{x, y};
            const auto groups = shipGroups(session, {system_, sp});
            for (size_t g = 0; g < groups.size(); ++g) {
                const Color col = palette::empireColor(s.empire(groups[g].first).color);
                const Vec2 p = shipIconPos(sectorCenter(sp), g);
                const float sz = kSector * 0.09f;
                const Vec2 a = p + Vec2{sz * 1.3f, -sz * 1.3f}, b = p + Vec2{-sz, -sz * 0.4f}, c = p + Vec2{sz * 0.4f, sz};
                r.triangle(a, b, c, col);
                const bool selected = std::any_of(groups[g].second.begin(), groups[g].second.end(), [&](const sim::Ship* sh) {
                    return ctx.selection.kind == Selection::Kind::Ship && ctx.selection.ship == sh->id;
                });
                if (selected) r.ring(p, sz * 2.0f, 1.5f, palette::kSelection);
            }
        }
    }

    // Selection highlight around the selected object's sector.
    std::optional<sim::SectorPos> selectedSector;
    if (ctx.selection.kind == Selection::Kind::Planet) selectedSector = s.planet(ctx.selection.planet).sector;
    if (ctx.selection.kind == Selection::Kind::WarpPoint) selectedSector = s.warpPoint(ctx.selection.warpPoint).sector;
    if (ctx.selection.kind == Selection::Kind::Ship)
        if (const sim::Ship* ship = s.findShip(ctx.selection.ship); ship && ship->location.system == system_) selectedSector = ship->location.sector;
    if (selectedSector && ctx.selection.system == system_) {
        const float pulse = 0.7f + 0.3f * std::sin(t * 4.0f);
        r.rectOutline(Rect::fromCenter(sectorCenter(*selectedSector), {kSector * 0.5f, kSector * 0.5f}), 2.0f,
                      palette::kSelection.withAlpha(pulse));
    }
    r.flush();
}

void SystemView::drawLabels(ViewContext& ctx) const {
    const GameSession& session = ctx.session;
    const sim::GameState& s = session.state();
    const sim::StarSystem& sys = s.system(system_);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float sectorPx = kSector * camera.zoom / ctx.fbScale;
    auto toUi = [&](Vec2 world) {
        const Vec2 p = camera.worldToScreen(world) / ctx.fbScale;
        return ImVec2{p.x, p.y};
    };
    auto centered = [&](ImFont* font, float size, ImVec2 at, ImU32 color, const std::string& text) {
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
        const ImVec2 pos{at.x - sz.x * 0.5f, at.y};
        dl->AddText(font, size, ImVec2{pos.x + 1, pos.y + 1}, IM_COL32(0, 0, 0, 170), text.c_str());
        dl->AddText(font, size, pos, color, text.c_str());
    };

    // Title above the grid.
    const float half = (static_cast<float>(s.sectorRadius()) + 0.5f) * kSector;
    const ImVec2 title = toUi({0.0f, -half});
    centered(ctx.fonts.bold, 22.0f, ImVec2{title.x, title.y - 34.0f}, IM_COL32(220, 232, 245, 255),
             std::format("{} - {}", sys.name, sim::displayName(sys.star)));

    if (!session.explored(system_)) {
        const ImVec2 c = toUi({0.0f, kSector * 0.8f});
        centered(ctx.fonts.medium, 16.0f, c, IM_COL32(150, 165, 185, 255), "Unexplored system - send a ship to survey it");
        return;
    }
    if (sectorPx < 26.0f) return;
    const float fontSize = std::clamp(sectorPx * 0.2f, 11.0f, 15.0f);

    for (sim::PlanetId pid : sys.planets) {
        const sim::Planet& p = s.planet(pid);
        const float pr = kSector * palette::planetScale(p.size);
        const ImVec2 at = toUi(sectorCenter(p.sector) + Vec2{0.0f, pr});
        const ImU32 col = p.colony ? lerp(palette::empireColor(s.empire(p.colony->owner).color), Color{}, 0.35f).toRgba8()
                                   : IM_COL32(200, 212, 225, 255);
        centered(ctx.fonts.regular, fontSize, ImVec2{at.x, at.y + (p.colony ? 11.0f : 4.0f)}, col, p.name);
    }
    for (sim::WarpPointId wid : sys.warpPoints) {
        const sim::WarpPoint& wp = s.warpPoint(wid);
        const sim::SystemId dest = s.warpPoint(wp.exit).system;
        const std::string label = "> " + (session.explored(dest) ? s.system(dest).name : std::string("Unexplored"));
        const ImVec2 at = toUi(sectorCenter(wp.sector) + Vec2{0.0f, kSector * 0.32f});
        centered(ctx.fonts.regular, fontSize, ImVec2{at.x, at.y + 3.0f}, IM_COL32(200, 170, 255, 255), label);
    }

    // Ship counts for stacked groups.
    const int radius = s.sectorRadius();
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            const auto groups = shipGroups(session, {system_, sim::SectorPos{x, y}});
            for (size_t g = 0; g < groups.size(); ++g) {
                if (groups[g].second.size() < 2) continue;
                const ImVec2 at = toUi(shipIconPos(sectorCenter(sim::SectorPos{x, y}), g) + Vec2{0.0f, kSector * 0.1f});
                centered(ctx.fonts.bold, fontSize - 1.0f, at, IM_COL32(235, 240, 250, 255), std::to_string(groups[g].second.size()));
            }
        }

    // Tooltip for the hovered sector.
    if (hovered_ && !ImGui::GetIO().WantCaptureMouse && ImGui::IsMousePosValid()) {
        const sim::Location loc{system_, *hovered_};
        const auto items = selectablesAt(session, *hovered_);
        if (items.empty() && *hovered_ != sim::SectorPos{}) return;
        ImGui::BeginTooltip();
        ImGui::TextDisabled("Sector (%d, %d)", hovered_->x, hovered_->y);
        if (*hovered_ == sim::SectorPos{}) ImGui::Text("%s (%s)", sys.name.c_str(), std::string(sim::displayName(sys.star)).c_str());
        for (const Selection& item : items) {
            switch (item.kind) {
                case Selection::Kind::Ship: {
                    const sim::Ship& ship = *s.findShip(item.ship);
                    ImGui::TextColored(ImColor(palette::empireColor(s.empire(ship.owner).color).toRgba8()), "%s", ship.name.c_str());
                    break;
                }
                case Selection::Kind::Planet: {
                    const sim::Planet& p = s.planet(item.planet);
                    ImGui::Text("%s - %s %s, %s atmosphere", p.name.c_str(), std::string(sim::displayName(p.size)).c_str(),
                                std::string(sim::displayName(p.surface)).c_str(), std::string(sim::displayName(p.atmosphere)).c_str());
                    break;
                }
                case Selection::Kind::WarpPoint:
                    ImGui::Text("Warp point to %s", session.explored(s.warpPoint(s.warpPoint(item.warpPoint).exit).system)
                                                        ? s.system(s.warpPoint(s.warpPoint(item.warpPoint).exit).system).name.c_str()
                                                        : "an unexplored system");
                    break;
                default: break;
            }
        }
        if (ctx.selection.kind == Selection::Kind::Ship) {
            const sim::Ship* ship = s.findShip(ctx.selection.ship);
            if (ship && ship->owner == session.player()) {
                const sim::Planet* planet = sim::planetAt(s, loc);
                if (planet && !s.statsOf(*ship).colonizes.empty() && sim::colonizeProblem(s, *ship, *planet).empty())
                    ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.6f, 1.0f), "Right-click: colonize %s", planet->name.c_str());
                else if (auto path = sim::findPath(s, ship->location, loc, &session.playerEmpire())) {
                    const int speed = std::max(1, s.statsOf(*ship).speed);
                    const int turns = static_cast<int>((path->size() + static_cast<size_t>(speed) - 1) / static_cast<size_t>(speed));
                    ImGui::TextColored(ImVec4(0.5f, 0.89f, 1.0f, 1.0f), "Right-click: move here (%zu moves, %d turn%s)", path->size(),
                                       turns, turns == 1 ? "" : "s");
                }
            }
        }
        if (std::any_of(items.begin(), items.end(), [](const Selection& i) { return i.kind == Selection::Kind::WarpPoint; }))
            ImGui::TextDisabled("Double-click to follow the warp point");
        ImGui::EndTooltip();
    }
}

} // namespace opense4::client
