#include "client/classic/quadrant_map.hpp"

#include "client/classic/map_style.hpp"
#include "client/classic/reports.hpp"
#include "game/abilities.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace opense4::client::classic {

namespace {

ImU32 col(uint32_t rgb) { return imColor(rgb); }

bool colonyHas(const game::Rules& r, const game::Colony& c, game::AbilityKind k) {
    for (uint32_t f : c.facilities)
        if (game::hasAbility(r.facilityAbilities(f), k)) return true;
    return false;
}

// Empires claiming a system that the overlay counts: the viewer and its allies, or its enemies (§2.6).
std::vector<game::EmpireId> claimants(const UiContext& ui, game::SystemId sys, bool allies) {
    const game::GameState& s = ui.state();
    const game::Empire& me = ui.me();
    std::vector<game::EmpireId> out;
    for (const game::Empire& e : s.empires) {
        if (!e.alive || !std::binary_search(e.claimedSystems.begin(), e.claimedSystems.end(), sys)) continue;
        if (allies) {
            if (e.id == me.id || game::allied(s, me.id, e.id)) out.push_back(e.id);
        } else if (e.id != me.id && me.relation(e.id).contact && game::hostile(s, me.id, e.id)) {
            out.push_back(e.id);
        }
    }
    return out;
}

map_style::Overlay styleOf(MapOverlay o) {
    switch (o) {
        case MapOverlay::Presence: return map_style::Overlay::Presence;
        case MapOverlay::Avoid: return map_style::Overlay::Avoid;
        case MapOverlay::AllyClaimed: return map_style::Overlay::AllyClaimed;
        case MapOverlay::EnemyClaimed: return map_style::Overlay::EnemyClaimed;
        case MapOverlay::Spaceports: return map_style::Overlay::Spaceports;
        case MapOverlay::ResupplyDepots: return map_style::Overlay::ResupplyDepots;
    }
    return map_style::Overlay::Presence;
}

} // namespace

std::vector<std::vector<game::EmpireId>> systemPresence(const UiContext& ui) { return map_style::presence(ui.rules(), ui.state(), ui.session.player()); }

int lightYears(const game::Galaxy& g, game::SystemId a, game::SystemId b) {
    const game::GalaxyPos pa = g.system(a).position, pb = g.system(b).position;
    const double dx = pa.x - pb.x, dy = pa.y - pb.y;
    return static_cast<int>(std::lround(std::sqrt(dx * dx + dy * dy) * 10.0));
}

std::vector<game::SystemId> exploredSystems(const UiContext& ui) {
    const game::Galaxy& g = ui.state().galaxy;
    std::vector<game::SystemId> out;
    for (const game::StarSystem& sys : g.systems)
        if (ui.me().hasExplored(sys.id)) out.push_back(sys.id);
    std::sort(out.begin(), out.end(), [&](game::SystemId a, game::SystemId b) { return std::pair(g.system(a).name, a) < std::pair(g.system(b).name, b); });
    return out;
}

map_style::Symbol systemSymbol(const UiContext& ui, game::SystemId sys, MapOverlay overlay,
                               const std::vector<std::vector<game::EmpireId>>& presence) {
    const game::GameState& s = ui.state();
    const game::Empire& me = ui.me();
    const bool explored = me.hasExplored(sys);
    switch (styleOf(overlay)) {
        case map_style::Overlay::Presence: return map_style::presenceSymbol(explored, presence[sys.index()], me.id);
        case map_style::Overlay::Avoid:
            return map_style::avoidSymbol(explored, std::binary_search(me.systemsToAvoid.begin(), me.systemsToAvoid.end(), sys), me.id);
        case map_style::Overlay::AllyClaimed:
        case map_style::Overlay::EnemyClaimed: return map_style::claimedSymbol(explored, claimants(ui, sys, overlay == MapOverlay::AllyClaimed));
        case map_style::Overlay::Spaceports:
        case map_style::Overlay::ResupplyDepots: {
            const game::AbilityKind k = overlay == MapOverlay::Spaceports ? game::AbilityKind::Spaceport : game::AbilityKind::SupplyGeneration;
            bool colonized = false, has = false;
            for (game::ObjectId id : s.galaxy.system(sys).objects)
                if (const game::Colony* c = s.colony(id); c && c->owner == me.id) {
                    colonized = true;
                    has = has || colonyHas(ui.rules(), *c, k);
                }
            return map_style::facilitySymbol(explored, colonized, has);
        }
    }
    return map_style::baseSymbol(explored);
}

QuadrantMapResult quadrantMap(UiContext& ui, const char* id, Vec2 frameSize, const QuadrantMapOptions& opt) {
    const game::GameState& s = ui.state();
    const game::Galaxy& g = s.galaxy;
    const game::Empire& me = ui.me();
    QuadrantMapResult out;

    const ImVec2 size = ui.size(frameSize);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool boxHovered = ImGui::IsItemHovered();
    const bool boxClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(o, {o.x + size.x, o.y + size.y}, true);
    dl->AddRectFilled(o, {o.x + size.x, o.y + size.y}, IM_COL32_BLACK);

    // Always 68 × 47 cells (§2.6), whole frame pixels each, centred in the box.
    const map_style::GridCell gc = map_style::gridCell(int(frameSize.x), int(frameSize.y));
    const float cw = ui.px(float(gc.w)), ch = ui.px(float(gc.h));
    const ImVec2 m{std::floor(o.x + (size.x - cw * map_style::kGridColumns) * 0.5f), std::floor(o.y + (size.y - ch * map_style::kGridRows) * 0.5f)};
    auto cellOf = [&](game::SystemId sys) {
        const game::GalaxyPos p = g.system(sys).position;
        return ImVec2{m.x + float(p.x) * cw, m.y + float(p.y) * ch};
    };
    auto pos = [&](game::SystemId sys) {
        const ImVec2 c = cellOf(sys);
        return ImVec2{c.x + cw * 0.5f, c.y + ch * 0.5f};
    };
    const ImU32 grid = col(map_style::kGrid);
    for (int x = 0; x <= map_style::kGridColumns; ++x)
        dl->AddLine({m.x + float(x) * cw, m.y}, {m.x + float(x) * cw, m.y + float(map_style::kGridRows) * ch}, grid);
    for (int y = 0; y <= map_style::kGridRows; ++y)
        dl->AddLine({m.x, m.y + float(y) * ch}, {m.x + float(map_style::kGridColumns) * cw, m.y + float(y) * ch}, grid);

    // Warp lines from explored systems; a link to an unexplored one is a stub two cells long.
    const ImU32 warp = col(map_style::kWarpLine);
    for (const game::SpaceObject& w : g.objects) {
        if (!opt.warpLines || w.kind != game::ObjectKind::WarpPoint || !w.destination.valid() || !me.hasExplored(w.system)) continue;
        const game::SystemId to = g.object(w.destination).system;
        const ImVec2 a = pos(w.system), b = pos(to);
        if (me.hasExplored(to)) {
            dl->AddLine(a, b, warp, ui.px(1));
        } else {
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            const float stub = std::min(len, 2.0f * cw);
            dl->AddLine(a, {a.x + dx * stub / len, a.y + dy * stub / len}, warp, ui.px(1));
        }
    }

    // Hover: the nearest system within reach of the mouse.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (boxHovered) {
        float best = std::max(std::max(cw, ch) * 2.0f, ui.px(8));
        for (const game::StarSystem& sys : g.systems) {
            const ImVec2 p = pos(sys.id);
            const float d = std::hypot(p.x - mouse.x, p.y - mouse.y);
            if (d < best) {
                best = d;
                out.hovered = sys.id;
            }
        }
        if (boxClicked) out.clicked = out.hovered;
    }

    const auto presence = systemPresence(ui);
    const float r = std::max(ui.px(1.5f), std::min(cw, ch) * 0.5f);
    const float thick = std::max(1.0f, ui.px(1));
    ImFont* font = ui.fonts.medium ? ui.fonts.medium : ImGui::GetFont();
    const float fs = std::max(ui.px(10), 9.0f);
    const ImU32 cyan = col(map_style::kHover);
    auto centeredText = [&](ImVec2 c, float dy, ImU32 color, const std::string& str) {
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, str.c_str());
        dl->AddText(font, fs, {c.x - ts.x * 0.5f, c.y + dy}, color, str.c_str());
    };

    for (const game::StarSystem& sys : g.systems) {
        const ImVec2 cell = cellOf(sys.id);
        const ImVec2 p = pos(sys.id);
        const bool current = opt.current && *opt.current == sys.id;
        map_style::Symbol sym = systemSymbol(ui, sys.id, opt.overlay, presence);
        if (opt.claimsOf) {
            std::vector<game::EmpireId> by;
            for (game::EmpireId e : *opt.claimsOf)
                if (e.index() < s.empires.size() &&
                    std::binary_search(s.empire(e).claimedSystems.begin(), s.empire(e).claimedSystems.end(), sys.id))
                    by.push_back(e);
            sym = map_style::claimedSymbol(me.hasExplored(sys.id), by);
        }
        const ImU32 c = sym.empire ? empireColor(s, *sym.empire) : col(sym.rgb);
        switch (sym.shape) {
            case map_style::Shape::Triangle:
                dl->AddTriangleFilled({cell.x, cell.y + ch}, {cell.x + cw, cell.y + ch}, {cell.x + cw * 0.5f, cell.y}, c);
                break;
            case map_style::Shape::Disc: dl->AddCircleFilled(p, r, c); break;
            case map_style::Shape::Ring:
                dl->AddCircleFilled(p, r, current ? c : IM_COL32_BLACK);
                dl->AddCircle(p, r, c, 0, thick);
                break;
        }
        if (sym.outerRing) dl->AddCircle(p, r + ui.px(2), c, 0, thick);
        // Ours: the systems to avoid on the other tabs of Systems To Avoid, in the viewer's colour.
        if (opt.avoidRings && std::binary_search(me.systemsToAvoid.begin(), me.systemsToAvoid.end(), sys.id))
            dl->AddCircle(p, r + ui.px(4), empireColor(s, me.id), 0, thick);
        if (std::find(opt.highlight.begin(), opt.highlight.end(), sys.id) != opt.highlight.end())
            dl->AddCircle(p, r + ui.px(4), cyan, 0, thick);
        if (current) dl->AddCircle(p, r + ui.px(2), c, 0, thick);
        if (out.hovered && *out.hovered == sys.id) dl->AddCircle(p, r + ui.px(2), cyan, 0, thick);
        if (opt.names && me.hasExplored(sys.id)) centeredText(p, r + ui.px(1), cyan, sys.name);
        for (const auto& [tagged, tag] : opt.tags)
            if (tagged == sys.id) centeredText(p, -r - fs - ui.px(1), cyan, tag);
    }

    // Distances from the hovered system.
    if (opt.distances && out.hovered)
        for (const game::StarSystem& sys : g.systems) {
            if (sys.id == *out.hovered) continue;
            const float dy = opt.names && me.hasExplored(sys.id) ? r + fs + ui.px(1) : r + ui.px(1);
            centeredText(pos(sys.id), dy, IM_COL32(255, 220, 120, 255), std::format("{} ly", lightYears(g, *out.hovered, sys.id)));
        }

    // The hovered system's name in cyan, at the first corner that fits (§2.6).
    // An unexplored system is never named (spec 01 §6.1).
    if (out.hovered && !opt.names && me.hasExplored(*out.hovered)) {
        const game::StarSystem& sys = g.system(*out.hovered);
        const std::string& name = sys.name;
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name.c_str());
        const ImVec2 cell = cellOf(sys.id);
        const map_style::Point at = map_style::nameCorner(cell.x - o.x, cell.y - o.y, cw, ch, ts.x + ui.px(2), ts.y, size.x, size.y);
        dl->AddText(font, fs, {o.x + at.x + ui.px(1), o.y + at.y}, cyan, name.c_str());
    }

    dl->PopClipRect();
    dl->AddRect(o, {o.x + size.x, o.y + size.y}, imColor(opt.frameColor), 0.0f, ui.px(1));
    return out;
}

} // namespace opense4::client::classic
