// The combat map shared by the Combat Replay, Tactical Combat and Strategic
// Combat windows (docs/spec/06 §1.6). See combat_map.hpp.

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
        case Kind::UnitsLost: return std::format("{} loses {} unit{}", pieceName(e.piece), e.amount, e.amount == 1 ? "" : "s");
    }
    return {};
}

// ---- Drawing -----------------------------------------------------------------------------------------------

void CombatMapPainter::background(ImDrawList* dl, ImVec2 min, ImVec2 max, const CombatView& v, ImU32 gridColor) const {
    dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 255));
    // The tiled background of docs/spec/06 §5.3, opaque, repeating every 12 squares:
    // the system's own tiles when its type masks black and has them, else the
    // Combat Tile of a storm or asteroid field in the sector, else the star field.
    const game::Location where = record_.location;
    std::string tiles;
    if (where.system.valid() && where.system.index() < s_.galaxy.systems.size()) {
        const game::StarSystem& sys = s_.galaxy.system(where.system);
        const auto& types = ui_.rules().data().systemTypes;
        if (sys.type.index() < types.size() && types[sys.type.index()].maskBackgroundObjects) {
            std::string name = types[sys.type.index()].backgroundBitmap;
            if (name.size() > 4 && (name.ends_with(".bmp") || name.ends_with(".BMP"))) name.resize(name.size() - 4);
            if (ui_.art.hasCombatTiles(name)) tiles = name;
        }
        for (game::ObjectId id : sys.objects) {
            const game::SpaceObject& o = s_.galaxy.object(id);
            if (!tiles.empty() || o.sector != where.sector || (o.kind != game::ObjectKind::Storm && o.kind != game::ObjectKind::Asteroids)) continue;
            if (o.sectorType < ui_.rules().data().sectorObjectTypes.size()) tiles = ui_.rules().data().sectorObjectTypes[o.sectorType].combatTile;
        }
    }
    if (const Sprite pic = ui_.art.combatBackground(tiles, (uint64_t(where.system.value) << 16) ^ uint64_t(where.sector.x * 13 + where.sector.y))) {
        dl->PushClipRect(min, max, true);
        const float x0 = std::floor((v.cx - (max.x - min.x) * 0.5f / v.cell) / 12.0f) * 12.0f;
        const float y0 = std::floor((v.cy - (max.y - min.y) * 0.5f / v.cell) / 12.0f) * 12.0f;
        for (float y = y0; v.at(0, y).y < max.y; y += 12.0f)
            for (float x = x0; v.at(x, 0).x < max.x; x += 12.0f) drawSprite(dl, pic, v.at(x, y), v.at(x + 12.0f, y + 12.0f));
        dl->PopClipRect();
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
    const game::CombatEvent* e = playback_.animating();
    const AnimationFrame* f = playback_.frame();
    if (e && f && e->piece == i && (e->kind == Kind::Move || e->kind == Kind::Seeker) && p.onMap) {
        // A slide is (step + 1) of its frames along; a jump stands on the new square; a turn on the old one.
        float t = 0.0f;
        if (f->part == AnimationFrame::Part::Slide) t = float(f->step + 1) / float(std::max(1, f->steps));
        else if (f->part == AnimationFrame::Part::Jump) t = 1.0f;
        x += (float(e->x) - x) * t;
        y += (float(e->y) - y) * t;
    }
    const float half = pieceExtent(i) * 0.5f;
    return v.at(x + half, y + half);
}

float CombatMapPainter::pieceHeading(uint32_t i) const {
    const CombatPlayback::Piece& p = playback_.pieces()[i];
    const game::CombatEvent* e = playback_.animating();
    const AnimationFrame* f = playback_.frame();
    if (!e || !f || e->piece != i || e->kind != Kind::Move || (e->x == p.x && e->y == p.y)) return p.heading;
    const float to = std::atan2(float(e->x - p.x), float(-(e->y - p.y)));
    if (f->part != AnimationFrame::Part::Turn) return to;
    // Turning: 45 degrees a frame toward the new facing, the shorter way round.
    constexpr float pi = std::numbers::pi_v<float>;
    float d = std::fmod(to - p.heading, 2.0f * pi);
    if (d > pi) d -= 2.0f * pi;
    if (d < -pi) d += 2.0f * pi;
    return p.heading + d * float(f->step + 1) / float(std::max(1, f->steps));
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
    if (anim)
        if (const AnimationFrame* f = playback_.frame()) event(dl, v, *anim, *f);
    return hovered;
}

std::optional<uint32_t> CombatMapPainter::squares(ImDrawList* dl, const CombatView& v, float minSize, bool hover, ImVec2 mouse) const {
    const auto& pieces = playback_.pieces();
    std::optional<uint32_t> hovered;
    // Big pieces first, so the small ones stay visible on top of them.
    for (const bool big : {true, false})
        for (uint32_t i = 0; i < pieces.size() && i < record_.pieces.size(); ++i) {
            const CombatPlayback::Piece& p = pieces[i];
            if (!p.onMap || (p.size > 1) != big) continue;
            const ImVec2 c = piecePos(v, i);
            const bool seeker = record_.pieces[i].kind == game::CombatPiece::Kind::Seeker;
            const ImU32 col = p.neutral ? IM_COL32(120, 120, 120, 255) : empireColor(s_, p.owner);
            if (seeker) {
                dl->AddCircleFilled(c, std::max(1.5f, v.cell * 0.25f), withAlpha(col, 0.85f));
                continue;
            }
            const float h = std::max(minSize, v.cell * float(p.size)) * 0.5f;
            dl->AddRectFilled({c.x - h, c.y - h}, {c.x + h, c.y + h}, big ? withAlpha(col, 0.55f) : col);
            if (p.captured) dl->AddRect({c.x - h - 1, c.y - h - 1}, {c.x + h + 1, c.y + h + 1}, IM_COL32(255, 220, 80, 255));
            if (hover && std::abs(mouse.x - c.x) <= h + 1 && std::abs(mouse.y - c.y) <= h + 1 && (!hovered || pieces[*hovered].size > p.size)) hovered = i;
        }
    return hovered;
}

void CombatMapPainter::event(ImDrawList* dl, const CombatView& v, const game::CombatEvent& e, const AnimationFrame& f) const {
    using Part = AnimationFrame::Part;
    // How far along its part the frame is (0..1), and the animation frame of a hit.
    const float t = float(f.step + 1) / float(std::max(1, f.steps));
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
    // A square of the original's map is 36 px: its pixel distances scale with our cells.
    const float px = v.cell / 36.0f;
    // Where a shot ends (spec 06 §1.10.3, §7 Q77, confirmed: binary): the
    // target's centre on a hit (a planet's: a point within its 4x4 block),
    // 17 px short for a hit the shields took, 18 px off in x and y on a miss.
    // The original draws the signs and the planet's point from the battle's
    // random sequence; ours derive them from the event's place, so the battle
    // is not touched (spec 04 §19.1).
    auto shotEnd = [&](const game::CombatEvent* outcome) {
        if (!hasTarget || !outcome) return to;
        const uint32_t h = static_cast<uint32_t>(playback_.cursor()) * 2654435761u ^ (e.piece * 40503u + e.target * 9973u);
        if (outcome->kind == Kind::Miss)
            return ImVec2{to.x + ((h & 1) ? 18.0f : -18.0f) * px, to.y + ((h & 2) ? 18.0f : -18.0f) * px};
        if ((outcome->flags & game::CombatEvent::kStructure) == 0) {
            const float dx = to.x - from.x, dy = to.y - from.y, len = std::sqrt(dx * dx + dy * dy);
            if (len <= 17.0f * px) return from;
            return ImVec2{to.x - dx / len * 17.0f * px, to.y - dy / len * 17.0f * px};
        }
        if (record_.pieces.size() > e.target && record_.pieces[e.target].kind == game::CombatPiece::Kind::Planet) {
            const float half = pieceExtent(e.target) * 0.5f;
            const float ox = float(h % 4) + 0.5f - half, oy = float((h >> 2) % 4) + 0.5f - half;
            return ImVec2{to.x + ox * v.cell, to.y + oy * v.cell};
        }
        return to;
    };
    // The target's own shield-hit pictures: 8 frames of 36x36 (the race's, else the generic ones).
    auto shieldRing = [&](int frame) {
        if (!hasTarget) return;
        const float sz = v.cell * pieceExtent(e.target);
        if (Sprite ring = raceCell(ui_.art, styleOf(pieces[e.target].owner), "Shields.bmp", 36 * std::clamp(frame, 0, 7), 0, 36, 36))
            drawSprite(dl, ring, {to.x - sz * 0.5f, to.y - sz * 0.5f}, {to.x + sz * 0.5f, to.y + sz * 0.5f});
        else dl->AddCircle(to, sz * 0.55f, IM_COL32(80, 230, 255, 220), 0, ui_.px(1.5f));
    };
    switch (e.kind) {
        case Kind::Move:
        case Kind::Seeker: break;  // drawn as piece movement
        case Kind::UnitsLost: break;  // shown by the Hit before it
        case Kind::Fire: {
            if (!hasTarget) break;
            const auto& comps = ui_.rules().data().components;
            const ruleset::Weapon* w = e.component < comps.size() ? &comps[e.component].weapon : nullptr;
            const std::string type = w ? w->displayType : std::string();
            const int index = w ? toInt(w->display) : 0;
            const game::CombatEvent* outcome = playback_.shotOutcome(playback_.cursor());
            const ImVec2 end = shotEnd(outcome);
            const float bearing = std::atan2(end.x - from.x, -(end.y - from.y));
            // A one-square target with its shields up shimmers while the shot is
            // drawn, one picture every 8 shot frames (ours: when the shields took the hit).
            const bool shimmer = outcome && outcome->kind == Kind::Hit && (outcome->flags & game::CombatEvent::kStructure) == 0 &&
                                 pieceExtent(e.target) <= 1.0f;
            if (shimmer) shieldRing((f.step / 8) % 8);
            if (type == "Beam") {
                // Stamps of the 10x10 centre of the 20x20 beam cell, turned to
                // the bearing, one every 6 px: drawn one by one outward, then
                // erased one by one in the same order (§5.3).
                // Beams and torpedoes are 1-based: cell = Weapon Display - 1, 0 = no picture (§5.2).
                const Sprite beam = index > 0 ? ui_.art.cell("Pictures/Combat/Beams.bmp", index - 1, 20, 20) : Sprite{};
                const int stamps = std::max(1, f.steps);
                const int first = f.part == Part::BeamErase ? f.step + 1 : 0;
                const int last = f.part == Part::BeamErase ? stamps - 1 : f.step;
                for (int k = first; k <= last; ++k) {
                    const float t0 = float(k + 1) / float(stamps);
                    const ImVec2 at{from.x + (end.x - from.x) * t0, from.y + (end.y - from.y) * t0};
                    if (beam) {
                        Sprite centre = beam;
                        const Vec2 uv = beam.uv.max - beam.uv.min;
                        centre.uv = Rect{beam.uv.min + Vec2{uv.x * 0.25f, uv.y * 0.25f}, beam.uv.min + Vec2{uv.x * 0.75f, uv.y * 0.75f}};
                        drawSpriteRotated(dl, centre, at, 10.0f * px, 10.0f * px, bearing);
                    } else {
                        dl->AddCircleFilled(at, 2.0f * px + 1.0f, shooter);
                    }
                }
            } else if (f.part == Part::Torpedo) {
                // A 40x40 picture of the 20x20 cell turned to the bearing, moving along the shot.
                const Sprite shot = type == "Seeker" ? raceCell(ui_.art, styleOf(pieces[e.piece].owner), "Main.bmp", 40 + 20 * std::clamp(index, 0, 2), 0, 20, 20)
                                                     : index > 0 ? ui_.art.cell("Pictures/Combat/Torps.bmp", index - 1, 20, 20) : Sprite{};
                const float k = float(f.step + 1) / float(std::max(1, f.steps));
                const ImVec2 at{from.x + (end.x - from.x) * k, from.y + (end.y - from.y) * k};
                if (shot) drawSpriteRotated(dl, shot, at, 40.0f * px, 40.0f * px, bearing);
                else dl->AddCircleFilled(at, 5.0f * px, shooter);
            }
            break;
        }
        case Kind::Hit: {
            if (!hasTarget) break;
            if (f.part == Part::Shield) {
                shieldRing(0);   // a single shield picture; the next redraw removes it
                break;
            }
            if (f.part != Part::Explosion) break;   // wiped, then the pause after a seeker's impact
            if (!playback_.followsFire(playback_.cursor())) dl->AddLine(from, to, shooter, ui_.px(1.5f));
            const int frame = std::clamp(f.step, 0, 7);
            // A loss uses the 72x72 explosion of the lost piece's race (every ship
            // and base loss observed); other damaging hits the 36x36 pictures.
            const bool loss = (e.flags & game::CombatEvent::kDestroyed) != 0 && record_.pieces.size() > e.target &&
                              record_.pieces[e.target].kind == game::CombatPiece::Kind::Vehicle;
            if (loss) {
                Sprite boom = raceCell(ui_.art, styleOf(pieces[e.target].owner), "BigExplosion.bmp", frame * 72, 0, 72, 72);
                const float sz = 72.0f * px;
                if (boom) drawSprite(dl, boom, {to.x - sz * 0.5f, to.y - sz * 0.5f}, {to.x + sz * 0.5f, to.y + sz * 0.5f});
                else dl->AddCircleFilled(to, sz * 0.4f * t, IM_COL32(255, 200, 80, int(255 * (1 - t))));
            } else {
                const int row = e.amount <= 5 ? 1 : e.amount <= 20 ? 2 : e.amount <= 60 ? 3 : 4;
                const float sz = 36.0f * px;
                if (Sprite boom = ui_.art.cell("Pictures/Combat/Explosions.bmp", row * 8 + frame, 36, 36))
                    drawSprite(dl, boom, {to.x - sz * 0.5f, to.y - sz * 0.5f}, {to.x + sz * 0.5f, to.y + sz * 0.5f});
                else dl->AddCircleFilled(to, sz * 0.3f * (1 - t * 0.5f), IM_COL32(255, 160, 60, 200));
            }
            label({to.x, to.y - v.cell * (0.6f + 0.5f * t)}, IM_COL32(255, 110, 90, 255), std::format("-{}", e.amount));
            break;
        }
        // A miss adds nothing to its shot; a loss, a capture, a launch or a
        // landing is only redrawn (spec 06 §1.10.3, §7 Q77).
        case Kind::Miss:
        case Kind::Destroyed:
        case Kind::Captured:
        case Kind::Launch: break;
    }
}

// ---- Combat simulator sides -------------------------------------------------------------------------------

namespace {

struct SimulationSides {
    const game::GameState* sandbox = nullptr;
    std::vector<game::EmpireId> sides;
};

SimulationSides& simulationSides() {
    static SimulationSides s;
    return s;
}

} // namespace

void setSimulationSides(const game::GameState* sandbox, std::vector<game::EmpireId> sides) {
    simulationSides() = SimulationSides{sandbox, std::move(sides)};
}

int simulationSide(const game::GameState& state, game::EmpireId e) {
    const SimulationSides& sim = simulationSides();
    if (sim.sandbox != &state || !e.valid()) return 0;
    for (size_t k = 0; k < sim.sides.size(); ++k)
        if (sim.sides[k] == e) return int(k) + 1;
    return 0;
}

ImU32 sideBoxColor(int side) {
    // The standard colours of those names (inferred: the values of the named colours).
    static constexpr std::array<ImU32, 10> kColors{
        IM_COL32(255, 0, 0, 255),   IM_COL32(0, 0, 255, 255),     IM_COL32(0, 128, 0, 255),   IM_COL32(255, 255, 0, 255),
        IM_COL32(128, 0, 128, 255), IM_COL32(255, 255, 255, 255), IM_COL32(0, 255, 255, 255), IM_COL32(0, 255, 0, 255),
        IM_COL32(128, 0, 0, 255),   IM_COL32(128, 128, 0, 255)};
    return side >= 1 && side <= int(kColors.size()) ? kColors[size_t(side - 1)] : IM_COL32(128, 128, 128, 255);
}

ImU32 sideNumberColor(int side) {
    const bool dark = side == 1 || side == 2 || side == 3 || side == 5 || side == 9 || side == 10;
    return dark ? IM_COL32_WHITE : IM_COL32_BLACK;
}

// A plain filled box with no outline, the number centred both ways in the
// text font the window is drawing with (spec 06 §7 Q82, confirmed: binary).
// OpenSE4 keeps the number within the box's height (inferred).
void drawSideBox(UiContext& ui, ImDrawList* dl, ImVec2 min, ImVec2 max, int side) {
    (void)ui;
    dl->AddRectFilled(min, max, sideBoxColor(side));
    const std::string number = std::to_string(side);
    ImFont* font = ImGui::GetFont();
    const float size = std::min(ImGui::GetFontSize(), max.y - min.y);
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, number.c_str());
    dl->AddText(font, size, {std::floor((min.x + max.x - ts.x) * 0.5f), std::floor((min.y + max.y - ts.y) * 0.5f)}, sideNumberColor(side),
                number.c_str());
}

void sideBox(UiContext& ui, int side, Vec2 size) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ui.size(size));
    drawSideBox(ui, ImGui::GetWindowDrawList(), a, {a.x + ui.px(size.x), a.y + ui.px(size.y)}, side);
}

bool ownerMark(UiContext& ui, const game::GameState& s, game::EmpireId e, Vec2 size, bool small) {
    if (const int side = simulationSide(s, e); side > 0) {
        sideBox(ui, side, size);
        return true;
    }
    if (!e.valid() || e.index() >= s.empires.size()) return false;
    if (Sprite flag = ui.art.flag(s.empire(e).race.style, !small)) {
        image(ui, flag, size);
        return true;
    }
    return false;
}

CombatPace combatPace(const game::Rules& r, bool fast, bool animateMoves) {
    CombatPace pace;
    pace.fast = fast;
    pace.animateMoves = animateMoves;
    const auto& comps = r.data().components;
    pace.beams.resize(comps.size(), 0);
    pace.seekers.resize(comps.size(), 0);
    for (size_t k = 0; k < comps.size(); ++k) {
        pace.beams[k] = comps[k].weapon.displayType == "Beam" ? 1 : 0;
        pace.seekers[k] = comps[k].isWeapon() && comps[k].weapon.kind == ruleset::WeaponKind::Seeking ? 1 : 0;
    }
    return pace;
}

bool squareInView(const CombatView& v, ImVec2 mapMin, ImVec2 mapMax, int x, int y) {
    const ImVec2 a = v.at(float(x), float(y)), b = v.at(float(x + 1), float(y + 1));
    return a.x >= mapMin.x - 0.5f && a.y >= mapMin.y - 0.5f && b.x <= mapMax.x + 0.5f && b.y <= mapMax.y + 0.5f;
}

void CombatMapPainter::sounds(size_t from, size_t to) const {
    if (to <= from || to - from > 8) return;  // skipping around is silent
    for (size_t i = from; i < to; ++i) {
        const game::CombatEvent& e = playback_.event(i);
        if (e.kind == Kind::Fire && e.component < ui_.rules().data().components.size()) audio().play(ui_.rules().component(e.component).weapon.sound);
        // boom3 for a destroyed piece; a damaging hit boom1 under 4 damage, else boom2 (§5.5).
        else if (e.kind == Kind::Destroyed) audio().play("boom3");
        else if (e.kind == Kind::Hit && e.amount > 0) audio().play(e.amount < 4 ? "boom1" : "boom2");
    }
}

} // namespace opense4::client::classic
