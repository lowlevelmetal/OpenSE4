// The combat map shared by the Combat Replay and Tactical Combat windows
// (docs/spec/06 §1.6). See combat_map.hpp.

#include "client/classic/screens/combat_map.hpp"

#include "client/audio.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/widgets.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace opense4::client::classic {

namespace {

using Kind = game::CombatEvent::Kind;

int toInt(std::string_view v) {
    int out = 0;
    while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
    std::from_chars(v.data(), v.data() + v.size(), out);
    return out;
}

float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Race art that Art has no helper for: `<style>_<suffix>` with the generic fallback.
Sprite raceCell(Art& art, std::string_view style, std::string_view suffix, int x, int y, int w, int h) {
    if (!style.empty())
        for (std::string_view folder : {"Races", "RaceNeutral"})
            if (Sprite s = art.region(std::format("Pictures/{}/{}/{}_{}", folder, style, style, suffix), x, y, w, h)) return s;
    return art.region(std::format("Pictures/RaceGeneric/Generic_{}", suffix), x, y, w, h);
}

} // namespace

ImU32 withAlpha(ImU32 c, float a) {
    const auto alpha = static_cast<ImU32>(std::clamp(a, 0.0f, 1.0f) * float((c >> IM_COL32_A_SHIFT) & 0xff));
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

std::pair<int, int> CombatView::square(ImVec2 p) const {
    return {int(std::floor((p.x - center.x) / cell + cx)), int(std::floor((p.y - center.y) / cell + cy))};
}

// ---- Names and pictures ---------------------------------------------------------------------------------

std::string CombatMapPainter::pieceName(uint32_t i) const {
    if (i >= record_.pieces.size()) return "?";
    auto base = [&](uint32_t k) {
        const game::CombatPiece& p = record_.pieces[k];
        if (!p.name.empty()) return p.name;
        if (p.design.valid() && p.design.index() < s_.designs.size()) return s_.design(p.design).name;
        if (p.planet.valid() && p.planet.index() < s_.galaxy.objects.size()) return s_.galaxy.object(p.planet).name;
        return std::format("Piece {}", k + 1);
    };
    std::string name = base(i);
    for (uint32_t j = 0; j < record_.pieces.size(); ++j)
        if (record_.pieces[j].owner != record_.pieces[i].owner && base(j) == name) return std::format("{} ({})", name, empireName(record_.pieces[i].owner));
    return name;
}

std::string CombatMapPainter::empireName(game::EmpireId e) const {
    return e.valid() && e.index() < s_.empires.size() ? s_.empire(e).name : std::string("Unknown");
}

const std::string& CombatMapPainter::styleOf(game::EmpireId e) const {
    static const std::string none;
    return e.valid() && e.index() < s_.empires.size() ? s_.empire(e).race.style : none;
}

const ruleset::VehicleSize* CombatMapPainter::hullOf(const game::CombatPiece& p) const {
    if (!p.design.valid() || p.design.index() >= s_.designs.size()) return nullptr;
    const uint32_t hull = s_.design(p.design).hull;
    return hull < ui_.rules().data().vehicleSizes.size() ? &ui_.rules().hull(hull) : nullptr;
}

Sprite CombatMapPainter::pieceSprite(const game::CombatPiece& p, game::EmpireId owner, bool& directional) const {
    directional = false;
    const std::string& style = styleOf(owner);
    switch (p.kind) {
        case game::CombatPiece::Kind::Planet:
        case game::CombatPiece::Kind::Obstacle:
            if (p.planet.valid() && p.planet.index() < s_.galaxy.objects.size()) return objectSprite(ui_, s_.galaxy.object(p.planet));
            return {};
        case game::CombatPiece::Kind::Seeker:
            directional = true;
            return raceCell(ui_.art, style, "Main.bmp", 40, 0, 20, 20);
        case game::CombatPiece::Kind::UnitGroup: {
            const ruleset::VehicleSize* hull = hullOf(p);
            if (!hull) return ui_.art.groupMini(style, "FighterGroup");
            const char* group = hull->type == ruleset::VehicleType::Satellite ? "SatelliteGroup"
                                : hull->type == ruleset::VehicleType::Mine    ? "MineGroup"
                                                                              : "FighterGroup";
            if (Sprite s = ui_.art.groupMini(style, group)) return s;
            directional = hull->type == ruleset::VehicleType::Fighter;
            return ui_.art.shipMini(style, *hull);
        }
        case game::CombatPiece::Kind::Vehicle: {
            const ruleset::VehicleSize* hull = hullOf(p);
            if (!hull) return {};
            directional = hull->type == ruleset::VehicleType::Ship || hull->type == ruleset::VehicleType::Fighter ||
                          hull->type == ruleset::VehicleType::Drone;
            return ui_.art.shipMini(style, *hull);
        }
    }
    return {};
}

std::string CombatMapPainter::weaponName(uint32_t component) const {
    const auto& comps = ui_.rules().data().components;
    return component < comps.size() ? comps[component].name : std::string("a weapon");
}

std::string CombatMapPainter::eventText(const game::CombatEvent& e) const {
    switch (e.kind) {
        case Kind::Move: return std::format("{} moves to {}, {}", pieceName(e.piece), e.x, e.y);
        case Kind::Fire: return std::format("{} fires {} at {}", pieceName(e.piece), weaponName(e.component), pieceName(e.target));
        case Kind::Hit: return std::format("{} is hit for {} damage", pieceName(e.target), e.amount);
        case Kind::Miss: return std::format("The shot at {} misses", pieceName(e.target));
        case Kind::Destroyed: return std::format("{} is destroyed", pieceName(e.piece));
        case Kind::Captured:
            return e.target < record_.pieces.size() && e.target != e.piece && playback_.validPiece(e.target)
                       ? std::format("{} is captured by {}", pieceName(e.piece), empireName(playback_.pieces()[e.target].owner))
                       : std::format("{} is captured", pieceName(e.piece));
        case Kind::Launch: return std::format("{} launches {}", pieceName(e.target), pieceName(e.piece));
        case Kind::Seeker: return std::format("{} closes on {}", pieceName(e.piece), pieceName(e.target));
    }
    return {};
}

// ---- Drawing -----------------------------------------------------------------------------------------------

void CombatMapPainter::background(ImDrawList* dl, ImVec2 min, ImVec2 max, const CombatView& v, ImU32 gridColor) const {
    dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 255));
    const game::Location where = record_.location;
    if (where.system.valid() && where.system.index() < s_.galaxy.systems.size()) {
        const game::StarSystem& sys = s_.galaxy.system(where.system);
        if (sys.type.index() < ui_.rules().data().systemTypes.size())
            drawSprite(dl, ui_.art.systemBackground(ui_.rules().data().systemTypes[sys.type.index()].backgroundBitmap), min, max,
                       IM_COL32(255, 255, 255, 120));
    }
    if (gridColor == 0) return;
    const float w = max.x - min.x, h = max.y - min.y;
    for (float x = std::floor(v.cx - w * 0.5f / v.cell); x <= v.cx + w * 0.5f / v.cell + 1; x += 1.0f)
        dl->AddLine(v.at(x, v.cy - 1000), v.at(x, v.cy + 1000), gridColor);
    for (float y = std::floor(v.cy - h * 0.5f / v.cell); y <= v.cy + h * 0.5f / v.cell + 1; y += 1.0f)
        dl->AddLine(v.at(v.cx - 1000, y), v.at(v.cx + 1000, y), gridColor);
}

float CombatMapPainter::pieceExtent(uint32_t i) const { return i < playback_.pieces().size() ? float(playback_.pieces()[i].size) : 1.0f; }

ImVec2 CombatMapPainter::piecePos(const CombatView& v, uint32_t i) const {
    const CombatPlayback::Piece& p = playback_.pieces()[i];
    float x = float(p.x), y = float(p.y);
    if (const game::CombatEvent* e = playback_.animating(); e && e->piece == i && (e->kind == Kind::Move || e->kind == Kind::Seeker) && p.onMap) {
        const float t = smooth(playback_.fraction());
        x += (float(e->x) - x) * t;
        y += (float(e->y) - y) * t;
    }
    const float half = pieceExtent(i) * 0.5f;
    return v.at(x + half, y + half);
}

float CombatMapPainter::pieceHeading(uint32_t i) const {
    const CombatPlayback::Piece& p = playback_.pieces()[i];
    if (const game::CombatEvent* e = playback_.animating(); e && e->piece == i && e->kind == Kind::Move && (e->x != p.x || e->y != p.y))
        return std::atan2(float(e->x - p.x), float(-(e->y - p.y)));
    return p.heading;
}

std::optional<uint32_t> CombatMapPainter::pieces(ImDrawList* dl, const CombatView& v, bool hover, ImVec2 mouse) const {
    const auto& pieces = playback_.pieces();
    const game::CombatEvent* anim = playback_.animating();

    // Shots of the current round when not animating (a still picture of the round).
    if (!anim && playback_.round() > 0)
        for (size_t i = playback_.roundStart(playback_.round()); i < playback_.cursor(); ++i) {
            const game::CombatEvent& e = playback_.event(i);
            if (e.kind != Kind::Fire || !playback_.validPiece(e.piece) || !playback_.validPiece(e.target)) continue;
            dl->AddLine(piecePos(v, e.piece), piecePos(v, e.target), withAlpha(empireColor(s_, pieces[e.piece].owner), 0.45f), ui_.px(1.5f));
        }

    // Pieces destroyed in the round shown (marked where they were).
    std::vector<uint8_t> diedThisRound(pieces.size(), 0);
    if (!anim && playback_.round() > 0)
        for (size_t i = playback_.roundStart(playback_.round()); i < playback_.cursor(); ++i)
            if (const game::CombatEvent& e = playback_.event(i); e.kind == Kind::Destroyed && playback_.validPiece(e.piece)) diedThisRound[e.piece] = 1;

    std::optional<uint32_t> hovered;
    for (uint32_t i = 0; i < pieces.size() && i < record_.pieces.size(); ++i) {
        const CombatPlayback::Piece& p = pieces[i];
        const game::CombatPiece& rp = record_.pieces[i];
        const bool planet = rp.kind == game::CombatPiece::Kind::Planet || rp.kind == game::CombatPiece::Kind::Obstacle;
        if (!p.onMap) {
            if (diedThisRound[i]) {
                const ImVec2 c = piecePos(v, i);
                const float h = v.cell * 0.25f;
                dl->AddLine({c.x - h, c.y - h}, {c.x + h, c.y + h}, IM_COL32(255, 80, 60, 150), ui_.px(2));
                dl->AddLine({c.x - h, c.y + h}, {c.x + h, c.y - h}, IM_COL32(255, 80, 60, 150), ui_.px(2));
            }
            continue;
        }
        const ImVec2 c = piecePos(v, i);
        const float extent = v.cell * pieceExtent(i);
        const float spriteSize = rp.kind == game::CombatPiece::Kind::Seeker ? v.cell * 0.6f : extent * 0.98f;
        const ImU32 owner = empireColor(s_, p.owner);
        if (!p.neutral)
            dl->AddRect({c.x - extent * 0.5f + 1, c.y - extent * 0.5f + 1}, {c.x + extent * 0.5f - 1, c.y + extent * 0.5f - 1},
                        withAlpha(owner, 0.75f), 0.0f, ui_.px(1.2f));
        bool directional = false;
        const Sprite sprite = pieceSprite(rp, p.owner, directional);
        if (sprite) {
            if (directional) drawSpriteRotated(dl, sprite, c, spriteSize, spriteSize, pieceHeading(i));
            else drawSprite(dl, sprite, {c.x - spriteSize * 0.5f, c.y - spriteSize * 0.5f}, {c.x + spriteSize * 0.5f, c.y + spriteSize * 0.5f});
        } else if (planet) {
            dl->AddCircleFilled(c, spriteSize * 0.4f, IM_COL32(110, 130, 150, 255));
        } else {
            const float r = spriteSize * 0.35f, a = pieceHeading(i);
            auto rot = [&](float x, float y) { return ImVec2{c.x + x * std::cos(a) - y * std::sin(a), c.y + x * std::sin(a) + y * std::cos(a)}; };
            dl->AddTriangleFilled(rot(0, -r), rot(-r * 0.75f, r), rot(r * 0.75f, r), owner);
        }
        if (p.captured) dl->AddText({c.x - extent * 0.5f + 2, c.y - extent * 0.5f}, IM_COL32(255, 220, 80, 255), "C");
        if (hover && std::abs(mouse.x - c.x) < extent * 0.5f && std::abs(mouse.y - c.y) < extent * 0.5f) {
            // Small pieces win over the big ones they overlap.
            if (!hovered || pieceExtent(*hovered) > pieceExtent(i)) hovered = i;
        }
    }
    if (anim) event(dl, v, *anim, playback_.fraction());
    return hovered;
}

void CombatMapPainter::event(ImDrawList* dl, const CombatView& v, const game::CombatEvent& e, float t) const {
    const auto& pieces = playback_.pieces();
    if (!playback_.validPiece(e.piece)) return;
    const ImVec2 from = piecePos(v, e.piece);
    const bool hasTarget = playback_.validPiece(e.target);
    const ImVec2 to = hasTarget ? piecePos(v, e.target) : from;
    const ImU32 shooter = empireColor(s_, pieces[e.piece].owner);
    ImFont* font = ui_.fonts.bold ? ui_.fonts.bold : ImGui::GetFont();
    const float fs = std::max(ui_.px(14), 11.0f);
    auto label = [&](ImVec2 c, ImU32 col, const std::string& text) {
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text.c_str());
        dl->AddText(font, fs, {c.x - ts.x * 0.5f + 1, c.y + 1}, IM_COL32(0, 0, 0, 200), text.c_str());
        dl->AddText(font, fs, {c.x - ts.x * 0.5f, c.y}, col, text.c_str());
    };
    switch (e.kind) {
        case Kind::Move:
        case Kind::Seeker: break;  // drawn as piece movement
        case Kind::Fire: {
            if (!hasTarget) break;
            const auto& comps = ui_.rules().data().components;
            const ruleset::Weapon* w = e.component < comps.size() ? &comps[e.component].weapon : nullptr;
            const std::string type = w ? w->displayType : std::string();
            const int index = w ? toInt(w->display) : 0;
            if (type == "Beam") {
                const Sprite beam = ui_.art.cell("Pictures/Combat/Beams.bmp", std::max(0, index - 1), 20, 20);
                const float a = std::sin(std::numbers::pi_v<float> * std::clamp(t, 0.0f, 1.0f));
                if (beam) drawSpriteAlong(dl, beam, from, to, v.cell * 0.5f, IM_COL32(255, 255, 255, int(255 * a)));
                else dl->AddLine(from, to, withAlpha(shooter, a), ui_.px(2.5f));
            } else {
                const Sprite shot = type == "Seeker" ? raceCell(ui_.art, styleOf(pieces[e.piece].owner), "Main.bmp", 40 + 20 * std::clamp(index, 0, 2), 0, 20, 20)
                                                     : ui_.art.cell("Pictures/Combat/Torps.bmp", std::max(0, index), 20, 20);
                const float k = smooth(t);
                const ImVec2 p{from.x + (to.x - from.x) * k, from.y + (to.y - from.y) * k};
                const float sz = v.cell * 0.6f;
                if (shot) drawSpriteRotated(dl, shot, p, sz, sz, std::atan2(to.x - from.x, -(to.y - from.y)));
                else dl->AddCircleFilled(p, sz * 0.25f, shooter);
            }
            break;
        }
        case Kind::Hit: {
            if (!hasTarget) break;
            if (!playback_.followsFire(playback_.cursor())) dl->AddLine(from, to, withAlpha(shooter, 0.6f * (1 - t)), ui_.px(1.5f));
            const int row = e.amount <= 5 ? 1 : e.amount <= 20 ? 2 : e.amount <= 60 ? 3 : 4;
            const int frame = std::min(7, int(t * 8.0f));
            const float sz = v.cell * 1.2f;
            if (Sprite boom = ui_.art.cell("Pictures/Combat/Explosions.bmp", row * 8 + frame, 36, 36))
                drawSprite(dl, boom, {to.x - sz * 0.5f, to.y - sz * 0.5f}, {to.x + sz * 0.5f, to.y + sz * 0.5f});
            else dl->AddCircleFilled(to, sz * 0.3f * (1 - t * 0.5f), IM_COL32(255, 160, 60, 200));
            label({to.x, to.y - v.cell * (0.6f + 0.5f * t)}, IM_COL32(255, 110, 90, 255), std::format("-{}", e.amount));
            break;
        }
        case Kind::Miss:
            if (!hasTarget) break;
            if (!playback_.followsFire(playback_.cursor())) dl->AddLine(from, to, withAlpha(shooter, 0.4f * (1 - t)), ui_.px(1.0f));
            label({to.x + v.cell * 0.4f, to.y - v.cell * (0.6f + 0.3f * t)}, IM_COL32(190, 195, 210, 230), "miss");
            break;
        case Kind::Destroyed: {
            const int frame = std::min(7, int(t * 8.0f));
            Sprite boom = raceCell(ui_.art, styleOf(pieces[e.piece].owner), "BigExplosion.bmp", frame * 72, 0, 72, 72);
            if (!boom) boom = ui_.art.cell("Pictures/Combat/BigExplosions.bmp", frame, 72, 72);
            const float sz = v.cell * 2.2f;
            if (boom) drawSprite(dl, boom, {from.x - sz * 0.5f, from.y - sz * 0.5f}, {from.x + sz * 0.5f, from.y + sz * 0.5f});
            else dl->AddCircleFilled(from, sz * 0.4f * t, IM_COL32(255, 200, 80, int(255 * (1 - t))));
            break;
        }
        case Kind::Captured: {
            const ImU32 col = hasTarget && e.target != e.piece ? empireColor(s_, pieces[e.target].owner) : IM_COL32(255, 220, 80, 255);
            dl->AddCircle(from, v.cell * (0.6f + 0.4f * t), withAlpha(col, 1 - t * 0.5f), 0, ui_.px(2.5f));
            label({from.x, from.y - v.cell * 1.1f}, IM_COL32(255, 220, 80, 255), "Captured");
            break;
        }
        case Kind::Launch: {
            const float half = pieceExtent(e.piece) * 0.5f;
            const ImVec2 c = v.at(float(e.x) + half, float(e.y) + half);
            dl->AddCircle(c, v.cell * (0.2f + 0.6f * t), IM_COL32(200, 230, 255, int(255 * (1 - t))), 0, ui_.px(2));
            break;
        }
    }
}

void CombatMapPainter::sounds(size_t from, size_t to) const {
    if (to <= from || to - from > 8) return;  // skipping around is silent
    for (size_t i = from; i < to; ++i) {
        const game::CombatEvent& e = playback_.event(i);
        if (e.kind == Kind::Fire && e.component < ui_.rules().data().components.size()) audio().play(ui_.rules().component(e.component).weapon.sound);
        else if (e.kind == Kind::Destroyed) audio().play(std::array<std::string_view, 3>{"boom1", "boom2", "boom3"}[i % 3]);
    }
}

} // namespace opense4::client::classic
