#include "client/classic/quadrant_map.hpp"

#include "client/classic/reports.hpp"
#include "game/abilities.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace opense4::client::classic {

namespace {

constexpr ImU32 kUnexplored = IM_COL32(80, 88, 102, 255);
constexpr ImU32 kExplored = IM_COL32(200, 208, 220, 255);
constexpr ImU32 kDim = IM_COL32(120, 128, 142, 255);
constexpr ImU32 kYellow = IM_COL32(255, 208, 64, 255);
constexpr ImU32 kGreen = IM_COL32(80, 220, 90, 255);
constexpr ImU32 kLink = IM_COL32(72, 104, 168, 230);
constexpr ImU32 kGrid = IM_COL32(16, 34, 74, 200);
constexpr ImU32 kHighlight = IM_COL32(64, 208, 255, 255);

bool colonyHas(const game::Rules& r, const game::Colony& c, game::AbilityKind k) {
    for (uint32_t f : c.facilities)
        if (game::hasAbility(r.facilityAbilities(f), k)) return true;
    return false;
}

// Empires claiming a system that the overlay shows.
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

void triangle(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddTriangleFilled({c.x, c.y - r * 1.25f}, {c.x - r * 1.1f, c.y + r * 0.85f}, {c.x + r * 1.1f, c.y + r * 0.85f}, col);
}

} // namespace

std::vector<std::vector<game::EmpireId>> systemPresence(const UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::Galaxy& g = s.galaxy;
    const game::Empire& me = ui.me();
    std::vector<std::vector<game::EmpireId>> presence(g.systems.size());
    auto mark = [&](game::SystemId sys, game::EmpireId e) {
        if (!sys.valid() || sys.index() >= presence.size()) return;
        auto& list = presence[sys.index()];
        if (std::find(list.begin(), list.end(), e) == list.end()) list.push_back(e);
    };
    for (const game::Vehicle& v : s.vehicles)
        if (v.owner.valid() && knownVehicle(ui, v)) mark(v.location.system, v.owner);
    for (const auto& c : s.colonies)
        if (c && (c->owner == me.id || me.hasExplored(g.object(c->planet).system))) mark(g.object(c->planet).system, c->owner);
    return presence;
}

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
    std::sort(out.begin(), out.end(), [&](game::SystemId a, game::SystemId b) { return g.system(a).name < g.system(b).name; });
    return out;
}

QuadrantMapResult quadrantMap(UiContext& ui, const char* id, Vec2 frameSize, const QuadrantMapOptions& opt) {
    const game::GameState& s = ui.state();
    const game::Galaxy& g = s.galaxy;
    const game::Empire& me = ui.me();
    const game::Rules& rules = ui.rules();
    QuadrantMapResult out;

    const ImVec2 size = ui.size(frameSize);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool boxHovered = ImGui::IsItemHovered();
    const bool boxClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(o, {o.x + size.x, o.y + size.y}, true);
    dl->AddRectFilled(o, {o.x + size.x, o.y + size.y}, IM_COL32(2, 5, 12, 255));

    // Square cells, centred in the box.
    const float pad = ui.px(10);
    const int gw = std::max(1, g.width), gh = std::max(1, g.height);
    const float cell = std::max(1.0f, std::min((size.x - 2 * pad) / float(gw), (size.y - 2 * pad) / float(gh)));
    const ImVec2 m{o.x + (size.x - cell * float(gw)) * 0.5f, o.y + (size.y - cell * float(gh)) * 0.5f};
    auto pos = [&](game::SystemId id_) {
        const game::GalaxyPos p = g.system(id_).position;
        return ImVec2{m.x + (float(p.x) + 0.5f) * cell, m.y + (float(p.y) + 0.5f) * cell};
    };
    for (int x = 0; x <= gw; ++x) dl->AddLine({m.x + float(x) * cell, m.y}, {m.x + float(x) * cell, m.y + float(gh) * cell}, kGrid);
    for (int y = 0; y <= gh; ++y) dl->AddLine({m.x, m.y + float(y) * cell}, {m.x + float(gw) * cell, m.y + float(y) * cell}, kGrid);

    // Warp links: known ones as lines, unknown ones as a stub from explored systems.
    const auto& known = me.knowledge.knownWarpLink;
    for (const game::SpaceObject& w : g.objects) {
        if (w.kind != game::ObjectKind::WarpPoint || !w.destination.valid()) continue;
        const bool linkKnown = w.id.index() < known.size() && known[w.id.index()];
        const ImVec2 a = pos(w.system);
        const ImVec2 b = pos(g.object(w.destination).system);
        if (linkKnown) {
            if (!(w.destination < w.id) || !(w.destination.index() < known.size() && known[w.destination.index()]))
                dl->AddLine(a, b, kLink, ui.px(1.2f));
        } else if (me.hasExplored(w.system)) {
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            const float stub = std::min(len * 0.5f, ui.px(12));
            dl->AddLine(a, {a.x + dx * stub / len, a.y + dy * stub / len}, kLink, ui.px(1.2f));
        }
    }

    // Hover: the nearest system within reach of the mouse.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (boxHovered) {
        float best = std::max(cell * 0.7f, ui.px(9));
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
    const float r = std::clamp(cell * 0.24f, ui.px(3.0f), ui.px(7.0f));
    const float thick = ui.px(1.5f);
    ImFont* font = ui.fonts.medium ? ui.fonts.medium : ImGui::GetFont();
    const float fs = std::max(ui.px(11), 9.0f);
    auto centeredText = [&](ImVec2 c, float dy, ImU32 col, const std::string& str) {
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, str.c_str());
        dl->AddText(font, fs, {c.x - ts.x * 0.5f, c.y + dy}, col, str.c_str());
    };

    for (const game::StarSystem& sys : g.systems) {
        const ImVec2 p = pos(sys.id);
        const bool explored = me.hasExplored(sys.id);
        const ImU32 base = explored ? kExplored : kUnexplored;
        const auto& who = presence[sys.id.index()];
        const bool avoided = std::binary_search(me.systemsToAvoid.begin(), me.systemsToAvoid.end(), sys.id);
        switch (opt.overlay) {
            case MapOverlay::Presence:
                if (who.size() > 1) triangle(dl, p, r, IM_COL32_WHITE);
                else dl->AddCircle(p, r, who.size() == 1 ? empireColor(s, who.front()) : base, 0, thick);
                break;
            case MapOverlay::Avoid:
                if (avoided) {
                    dl->AddCircleFilled(p, r, kYellow);
                } else {
                    dl->AddCircle(p, r, explored ? kDim : kUnexplored, 0, thick);
                }
                break;
            case MapOverlay::AllyClaimed:
            case MapOverlay::EnemyClaimed: {
                const auto list = claimants(ui, sys.id, opt.overlay == MapOverlay::AllyClaimed);
                if (list.size() > 1) triangle(dl, p, r, IM_COL32_WHITE);
                else if (list.size() == 1) dl->AddCircleFilled(p, r, empireColor(s, list.front()));
                else dl->AddCircle(p, r, explored ? kDim : kUnexplored, 0, thick);
                break;
            }
            case MapOverlay::Spaceports:
            case MapOverlay::ResupplyDepots: {
                const game::AbilityKind k = opt.overlay == MapOverlay::Spaceports ? game::AbilityKind::Spaceport : game::AbilityKind::SupplyGeneration;
                bool colonized = false, has = false;
                for (game::ObjectId id_ : sys.objects)
                    if (const game::Colony* c = s.colony(id_); c && c->owner == me.id) {
                        colonized = true;
                        has = has || colonyHas(rules, *c, k);
                    }
                if (colonized) dl->AddCircleFilled(p, r, has ? kGreen : kYellow);
                else dl->AddCircle(p, r, explored ? kDim : kUnexplored, 0, thick);
                break;
            }
        }
        if (opt.avoidRings && avoided && opt.overlay != MapOverlay::Avoid) dl->AddCircle(p, r + ui.px(3), kYellow, 0, ui.px(1));
        if (std::find(opt.highlight.begin(), opt.highlight.end(), sys.id) != opt.highlight.end())
            dl->AddCircle(p, r + ui.px(4), kHighlight, 0, thick);
        if (opt.current && *opt.current == sys.id) {
            dl->AddCircle(p, r + ui.px(3.5f), kYellow, 0, thick);
            dl->AddCircleFilled(p, r * 0.45f, kYellow);
        }
        if (out.hovered && *out.hovered == sys.id) dl->AddCircle(p, r + ui.px(6), IM_COL32(255, 255, 255, 160), 0, ui.px(1));
        if (opt.names && explored) centeredText(p, r + ui.px(1), IM_COL32(170, 190, 225, 255), sys.name);
        for (const auto& [tagged, tag] : opt.tags)
            if (tagged == sys.id) centeredText(p, -r - fs - ui.px(1), kHighlight, tag);
    }

    // Distances from the hovered system.
    if (opt.distances && out.hovered)
        for (const game::StarSystem& sys : g.systems) {
            if (sys.id == *out.hovered) continue;
            const float dy = opt.names && me.hasExplored(sys.id) ? r + fs + ui.px(1) : r + ui.px(1);
            centeredText(pos(sys.id), dy, IM_COL32(255, 220, 120, 255), std::format("{} ly", lightYears(g, *out.hovered, sys.id)));
        }

    // The hovered system's name next to it.
    if (out.hovered) {
        const game::StarSystem& sys = g.system(*out.hovered);
        const std::string name = me.hasExplored(sys.id) ? sys.name : sys.name + " (unexplored)";
        const ImVec2 p = pos(sys.id);
        const ImVec2 ts = font->CalcTextSizeA(fs * 1.1f, FLT_MAX, 0.0f, name.c_str());
        ImVec2 at{p.x + r + ui.px(8), p.y - ts.y * 0.5f};
        if (at.x + ts.x > o.x + size.x - ui.px(4)) at.x = p.x - r - ui.px(8) - ts.x;
        dl->AddRectFilled({at.x - ui.px(3), at.y - ui.px(1)}, {at.x + ts.x + ui.px(3), at.y + ts.y + ui.px(1)}, IM_COL32(4, 10, 28, 220));
        dl->AddText(font, fs * 1.1f, at, IM_COL32(230, 236, 255, 255), name.c_str());
    }

    dl->PopClipRect();
    dl->AddRect(o, {o.x + size.x, o.y + size.y}, IM_COL32(66, 107, 216, 255), 0.0f, ui.px(1));
    return out;
}

} // namespace opense4::client::classic
