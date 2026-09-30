// Combat Replay (docs/spec/06 §1.6, docs/spec/04 §17): plays back a battle
// of the last processed turn on a tactical-style map. Playback state lives in
// CombatPlayback (replay.hpp); this file draws it.

#include "client/audio.hpp"
#include "client/classic/replay.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
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

const ImVec4 kLabelBlue{0.44f, 0.61f, 1.0f, 1.0f};
constexpr std::array<float, 5> kSpeeds{0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

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

ImU32 withAlpha(ImU32 c, float a) {
    const auto alpha = static_cast<ImU32>(std::clamp(a, 0.0f, 1.0f) * float((c >> IM_COL32_A_SHIFT) & 0xff));
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

// Race art that Art has no helper for: `<style>_<suffix>` with the generic fallback.
Sprite raceCell(Art& art, std::string_view style, std::string_view suffix, int x, int y, int w, int h) {
    if (!style.empty())
        for (std::string_view folder : {"Races", "RaceNeutral"})
            if (Sprite s = art.region(std::format("Pictures/{}/{}/{}_{}", folder, style, style, suffix), x, y, w, h)) return s;
    return art.region(std::format("Pictures/RaceGeneric/Generic_{}", suffix), x, y, w, h);
}

class CombatReplayScreen final : public Screen {
public:
    explicit CombatReplayScreen(int index) : wanted_(index) {}

    bool draw(UiContext& ui) override {
        const auto& combats = ui.state().combats;
        if (combats.empty()) return drawEmpty(ui);
        if (index_ < 0 || index_ >= int(combats.size()) || loadedTurn_ != ui.state().turn) {
            load(ui, wanted_ >= 0 && wanted_ < int(combats.size()) ? wanted_ : int(combats.size()) - 1, combats);
        }
        const size_t before = playback_.cursor();
        playback_.advance(ui.dt);
        playSounds(ui, before, playback_.cursor());

        Dialog d(ui, "Combat Replay", DialogSize::Full);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        statusBar(ui);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float side = ui.px(250);
        const ImVec2 mapSize{avail.x - side - ImGui::GetStyle().ItemSpacing.x, avail.y};
        drawMap(ui, mapSize);
        ImGui::SameLine();
        ImGui::BeginGroup();
        eventList(ui, ImVec2(side, avail.y * 0.58f));
        summary(ui, ImVec2(side, 0));
        ImGui::EndGroup();

        d.beginButtons();
        if (d.button(playback_.playing() ? "Pause" : playback_.atEnd() ? "Play Again" : "Play")) playback_.togglePlay();
        if (d.button("Next Round", !playback_.atEnd())) {
            playback_.pause();
            playback_.stepRound();
        }
        if (d.button("Previous Round", !playback_.atStart())) {
            playback_.pause();
            playback_.stepBackRound();
        }
        if (d.button("Next Event", !playback_.atEnd())) {
            playback_.pause();
            playback_.stepEvent();
        }
        if (d.button("Rewind", !playback_.atStart())) {
            playback_.pause();
            playback_.rewind();
        }
        if (d.button(std::format("Speed: {}x###speed", playback_.speed() < 1.0f ? std::string("1/2") : std::format("{}", int(playback_.speed()))).c_str())) {
            auto it = std::find(kSpeeds.begin(), kSpeeds.end(), playback_.speed());
            const float next = it == kSpeeds.end() || it + 1 == kSpeeds.end() ? kSpeeds.front() : *(it + 1);
            playback_.setSpeed(next);
            settings().replaySpeed = next;
            saveSettings();
        }
        d.spacer();
        const int count = int(combats.size());
        if (d.button("Previous Battle", index_ > 0)) load(ui, index_ - 1, combats);
        if (d.button("Next Battle", index_ + 1 < count)) load(ui, index_ + 1, combats);
        dimText(std::format("Battle {} of {}", index_ + 1, count).c_str());
        keys();
        d.close();
        return d.keepOpen();
    }

private:
    bool drawEmpty(UiContext& ui) {
        Dialog d(ui, "Combat Replay", DialogSize::Prompt, 120);
        if (d.open()) {
            d.beginContent();
            ImGui::Spacing();
            ImGui::TextWrapped("No battles were fought last turn, so there is nothing to replay.");
            d.beginButtons();
            d.close();
        }
        return d.keepOpen();
    }

    void load(UiContext& ui, int index, const std::vector<game::CombatRecord>& combats) {
        index_ = index;
        wanted_ = index;
        loadedTurn_ = ui.state().turn;
        record_ = combats[size_t(index)];
        playback_ = CombatPlayback(record_);
        playback_.setSpeed(settings().replaySpeed);
        playback_.play();
        listCursor_ = SIZE_MAX;
    }

    void keys() {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) playback_.togglePlay();
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            playback_.pause();
            playback_.stepRound();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            playback_.pause();
            playback_.stepBackRound();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            playback_.pause();
            playback_.rewind();
        }
    }

    // ---- Names and pictures ---------------------------------------------------------------------

    std::string baseName(const UiContext& ui, uint32_t i) const {
        const game::CombatPiece& p = record_.pieces[i];
        if (!p.name.empty()) return p.name;
        const game::GameState& s = ui.state();
        if (p.design.valid() && p.design.index() < s.designs.size()) return s.design(p.design).name;
        if (p.planet.valid() && p.planet.index() < s.galaxy.objects.size()) return s.galaxy.object(p.planet).name;
        return std::format("Piece {}", i + 1);
    }

    // The piece's name, with its empire when another side has a piece of the same name.
    std::string pieceName(const UiContext& ui, uint32_t i) const {
        if (i >= record_.pieces.size()) return "?";
        std::string name = baseName(ui, i);
        for (uint32_t j = 0; j < record_.pieces.size(); ++j)
            if (record_.pieces[j].owner != record_.pieces[i].owner && baseName(ui, j) == name)
                return std::format("{} ({})", name, empireName(ui, record_.pieces[i].owner));
        return name;
    }

    std::string empireName(const UiContext& ui, game::EmpireId e) const {
        const game::GameState& s = ui.state();
        return e.valid() && e.index() < s.empires.size() ? s.empire(e).name : std::string("Unknown");
    }

    const std::string& styleOf(const UiContext& ui, game::EmpireId e) const {
        static const std::string none;
        const game::GameState& s = ui.state();
        return e.valid() && e.index() < s.empires.size() ? s.empire(e).race.style : none;
    }

    const ruleset::VehicleSize* hullOf(const UiContext& ui, const game::CombatPiece& p) const {
        const game::GameState& s = ui.state();
        if (!p.design.valid() || p.design.index() >= s.designs.size()) return nullptr;
        const uint32_t hull = s.design(p.design).hull;
        return hull < ui.rules().data().vehicleSizes.size() ? &ui.rules().hull(hull) : nullptr;
    }

    // The picture of a piece and whether it turns to its heading (top-down ships do).
    Sprite pieceSprite(UiContext& ui, const game::CombatPiece& p, game::EmpireId owner, bool& directional) const {
        directional = false;
        const std::string& style = styleOf(ui, owner);
        switch (p.kind) {
            case game::CombatPiece::Kind::Planet:
                if (p.planet.valid() && p.planet.index() < ui.state().galaxy.objects.size()) return objectSprite(ui, ui.state().galaxy.object(p.planet));
                return {};
            case game::CombatPiece::Kind::Seeker:
                directional = true;
                return raceCell(ui.art, style, "Main.bmp", 40, 0, 20, 20);
            case game::CombatPiece::Kind::UnitGroup: {
                const ruleset::VehicleSize* hull = hullOf(ui, p);
                if (!hull) return ui.art.groupMini(style, "FighterGroup");
                const char* group = hull->type == ruleset::VehicleType::Satellite ? "SatelliteGroup"
                                    : hull->type == ruleset::VehicleType::Mine    ? "MineGroup"
                                                                                  : "FighterGroup";
                if (Sprite s = ui.art.groupMini(style, group)) return s;
                directional = hull->type == ruleset::VehicleType::Fighter;
                return ui.art.shipMini(style, *hull);
            }
            case game::CombatPiece::Kind::Vehicle: {
                const ruleset::VehicleSize* hull = hullOf(ui, p);
                if (!hull) return {};
                directional = hull->type == ruleset::VehicleType::Ship || hull->type == ruleset::VehicleType::Fighter ||
                              hull->type == ruleset::VehicleType::Drone;
                return ui.art.shipMini(style, *hull);
            }
        }
        return {};
    }

    std::string weaponName(const UiContext& ui, uint32_t component) const {
        const auto& comps = ui.rules().data().components;
        return component < comps.size() ? comps[component].name : std::string("a weapon");
    }

    std::string eventText(const UiContext& ui, const game::CombatEvent& e) const {
        switch (e.kind) {
            case Kind::Move: return std::format("{} moves to {}, {}", pieceName(ui, e.piece), e.x, e.y);
            case Kind::Fire: return std::format("{} fires {} at {}", pieceName(ui, e.piece), weaponName(ui, e.component), pieceName(ui, e.target));
            case Kind::Hit: return std::format("{} is hit for {} damage", pieceName(ui, e.target), e.amount);
            case Kind::Miss: return std::format("The shot at {} misses", pieceName(ui, e.target));
            case Kind::Destroyed: return std::format("{} is destroyed", pieceName(ui, e.piece));
            case Kind::Captured:
                return e.target < record_.pieces.size() && e.target != e.piece
                           ? std::format("{} is captured by {}", pieceName(ui, e.piece), empireName(ui, playback_.pieces()[e.target].owner))
                           : std::format("{} is captured", pieceName(ui, e.piece));
            case Kind::Launch: return std::format("{} launches {}", pieceName(ui, e.target), pieceName(ui, e.piece));
            case Kind::Seeker: return std::format("{} closes on {}", pieceName(ui, e.piece), pieceName(ui, e.target));
        }
        return {};
    }

    // ---- Status bar, event list, summary -------------------------------------------------------------

    void statusBar(UiContext& ui) {
        const game::GameState& s = ui.state();
        ImGui::PushFont(ui.fonts.bold, ImGui::GetFontSize() * 1.1f);
        ImGui::Text("Battle at %s", sectorName(s, record_.location).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        dimText(std::format("  {}", formatDate(record_.turn)).c_str());
        const std::string round = playback_.roundCount() > 0 ? std::format("Round {} of {}", playback_.round(), playback_.roundCount()) : "No rounds";
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize(round.c_str()).x - ui.px(6));
        ImGui::TextColored(kLabelBlue, "%s", round.c_str());
        bool first = true;
        for (game::EmpireId e : record_.participants) {
            if (!first) ImGui::SameLine(0, ui.px(18));
            first = false;
            if (Sprite flag = ui.art.flag(styleOf(ui, e))) {
                image(ui, flag, {26, 18});
                ImGui::SameLine(0, ui.px(5));
            }
            int lost = 0, total = 0;
            for (size_t i = 0; i < record_.pieces.size(); ++i)
                if (record_.pieces[i].owner == e && record_.pieces[i].kind != game::CombatPiece::Kind::Seeker) {
                    ++total;
                    lost += playback_.pieces()[i].destroyed || playback_.pieces()[i].owner != e ? 1 : 0;
                }
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(s, e)), "%s", empireName(ui, e).c_str());
            ImGui::SameLine(0, ui.px(5));
            dimText(std::format("{} of {} left", total - lost, total).c_str());
        }
        if (record_.participants.empty()) dimText("No participants recorded");
        ImGui::Separator();
    }

    void eventList(UiContext& ui, ImVec2 size) {
        heading(ui, playback_.round() > 0 ? std::format("Round {}", playback_.round()).c_str() : "Before the battle");
        ImGui::BeginChild("##events", ImVec2(size.x, size.y - ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
        const int round = playback_.round();
        const size_t from = round > 0 ? playback_.roundStart(round) : 0;
        const size_t to = playback_.cursor();
        const game::CombatEvent* anim = playback_.animating();
        ImGui::PushTextWrapPos(0.0f);
        for (size_t i = from; i < to; ++i) {
            const game::CombatEvent& e = playback_.event(i);
            const std::string text = eventText(ui, e);
            switch (e.kind) {
                case Kind::Move:
                case Kind::Seeker: dimText(text.c_str()); break;
                case Kind::Hit: ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "  %s", text.c_str()); break;
                case Kind::Miss: ImGui::TextColored(ImVec4(0.6f, 0.62f, 0.68f, 1), "  %s", text.c_str()); break;
                case Kind::Destroyed: ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%s", text.c_str()); break;
                case Kind::Captured: ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "%s", text.c_str()); break;
                default: ImGui::TextUnformatted(text.c_str()); break;
            }
        }
        if (anim) ImGui::TextColored(ImVec4(1, 1, 0.7f, 1), "> %s", eventText(ui, *anim).c_str());
        if (from == to && !anim) dimText(playback_.atStart() ? "Pieces in their starting positions." : "Nothing happened this round.");
        ImGui::PopTextWrapPos();
        if (listCursor_ != to) {
            ImGui::SetScrollHereY(1.0f);
            listCursor_ = to;
        }
        ImGui::EndChild();
    }

    void summary(UiContext& ui, ImVec2 size) {
        heading(ui, "Summary");
        ImGui::BeginChild("##summary", ImVec2(size.x, 0), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.0f);
        for (const std::string& line : record_.summary) ImGui::TextUnformatted(line.c_str());
        if (record_.summary.empty()) dimText("No summary was recorded.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }

    // ---- The map ----------------------------------------------------------------------------------------

    struct View {
        ImVec2 center;
        float cell = 20;
        float cx = 0, cy = 0;  // square coordinates at the centre
        ImVec2 at(float sx, float sy) const { return {center.x + (sx - cx) * cell, center.y + (sy - cy) * cell}; }
    };

    ImVec2 pieceCenter(const View& v, uint32_t i, float x, float y) const {
        const bool planet = i < record_.pieces.size() && record_.pieces[i].kind == game::CombatPiece::Kind::Planet;
        return planet ? v.at(x + 1.0f, y + 1.0f) : v.at(x + 0.5f, y + 0.5f);
    }

    // Where piece i is drawn now (moving pieces are between squares while animating).
    ImVec2 piecePos(const View& v, uint32_t i) const {
        const CombatPlayback::Piece& p = playback_.pieces()[i];
        float x = float(p.x), y = float(p.y);
        if (const game::CombatEvent* e = playback_.animating(); e && e->piece == i && (e->kind == Kind::Move || e->kind == Kind::Seeker) && p.onMap) {
            const float t = smooth(playback_.fraction());
            x += (float(e->x) - x) * t;
            y += (float(e->y) - y) * t;
        }
        return pieceCenter(v, i, x, y);
    }

    float pieceHeading(uint32_t i) const {
        const CombatPlayback::Piece& p = playback_.pieces()[i];
        if (const game::CombatEvent* e = playback_.animating(); e && e->piece == i && e->kind == Kind::Move && (e->x != p.x || e->y != p.y))
            return std::atan2(float(e->x - p.x), float(-(e->y - p.y)));
        return p.heading;
    }

    void drawMap(UiContext& ui, ImVec2 size) {
        const game::GameState& s = ui.state();
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##map", size);
        const bool hoveredBox = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o2{o.x + size.x, o.y + size.y};
        dl->PushClipRect(o, o2, true);
        dl->AddRectFilled(o, o2, IM_COL32(0, 0, 0, 255));
        if (record_.location.system.valid() && record_.location.system.index() < s.galaxy.systems.size()) {
            const game::StarSystem& sys = s.galaxy.system(record_.location.system);
            if (sys.type.index() < ui.rules().data().systemTypes.size())
                drawSprite(dl, ui.art.systemBackground(ui.rules().data().systemTypes[sys.type.index()].backgroundBitmap), o, o2,
                           IM_COL32(255, 255, 255, 120));
        }

        // Fit the squares every piece visits, with a margin.
        const CombatPlayback::Bounds& b = playback_.bounds();
        const float cols = float(std::max(12, b.maxX - b.minX + 1 + 2)), rows = float(std::max(12, b.maxY - b.minY + 1 + 2));
        View v;
        v.cell = std::clamp(std::min(size.x / cols, size.y / rows), ui.px(14), ui.px(56));
        v.center = {o.x + size.x * 0.5f, o.y + size.y * 0.5f};
        v.cx = (float(b.minX) + float(b.maxX + 1)) * 0.5f;
        v.cy = (float(b.minY) + float(b.maxY + 1)) * 0.5f;
        const ImU32 grid = IM_COL32(40, 70, 140, 70);
        for (float x = std::floor(v.cx - size.x * 0.5f / v.cell); x <= v.cx + size.x * 0.5f / v.cell + 1; x += 1.0f)
            dl->AddLine(v.at(x, v.cy - 1000), v.at(x, v.cy + 1000), grid);
        for (float y = std::floor(v.cy - size.y * 0.5f / v.cell); y <= v.cy + size.y * 0.5f / v.cell + 1; y += 1.0f)
            dl->AddLine(v.at(v.cx - 1000, y), v.at(v.cx + 1000, y), grid);

        const auto& pieces = playback_.pieces();
        const game::CombatEvent* anim = playback_.animating();
        const float t = playback_.fraction();

        // Shots of the current round when not animating (a still picture of the round).
        if (!anim && playback_.round() > 0)
            for (size_t i = playback_.roundStart(playback_.round()); i < playback_.cursor(); ++i) {
                const game::CombatEvent& e = playback_.event(i);
                if (e.kind != Kind::Fire || !playback_.validPiece(e.piece) || !playback_.validPiece(e.target)) continue;
                dl->AddLine(piecePos(v, e.piece), piecePos(v, e.target), withAlpha(empireColor(s, pieces[e.piece].owner), 0.45f), ui.px(1.5f));
            }

        // Pieces destroyed in the round shown (marked where they were).
        std::vector<uint8_t> diedThisRound(pieces.size(), 0);
        if (!anim && playback_.round() > 0)
            for (size_t i = playback_.roundStart(playback_.round()); i < playback_.cursor(); ++i)
                if (const game::CombatEvent& e = playback_.event(i); e.kind == Kind::Destroyed && playback_.validPiece(e.piece)) diedThisRound[e.piece] = 1;

        // Pieces.
        std::optional<uint32_t> hovered;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (uint32_t i = 0; i < pieces.size(); ++i) {
            const CombatPlayback::Piece& p = pieces[i];
            const game::CombatPiece& rp = record_.pieces[i];
            const bool planet = rp.kind == game::CombatPiece::Kind::Planet;
            if (!p.onMap) {
                // Destroyed this round: a marker where it was.
                if (diedThisRound[i]) {
                    const ImVec2 c = piecePos(v, i);
                    const float h = v.cell * 0.25f;
                    dl->AddLine({c.x - h, c.y - h}, {c.x + h, c.y + h}, IM_COL32(255, 80, 60, 150), ui.px(2));
                    dl->AddLine({c.x - h, c.y + h}, {c.x + h, c.y - h}, IM_COL32(255, 80, 60, 150), ui.px(2));
                }
                continue;
            }
            const ImVec2 c = piecePos(v, i);
            const float extent = planet ? v.cell * 2.0f : v.cell;
            const float spriteSize = rp.kind == game::CombatPiece::Kind::Seeker ? v.cell * 0.6f : extent * 0.98f;
            const ImU32 owner = empireColor(s, p.owner);
            dl->AddRect({c.x - extent * 0.5f + 1, c.y - extent * 0.5f + 1}, {c.x + extent * 0.5f - 1, c.y + extent * 0.5f - 1}, withAlpha(owner, 0.75f),
                        0.0f, ui.px(1.2f));
            bool directional = false;
            const Sprite sprite = pieceSprite(ui, rp, p.owner, directional);
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
            if (hoveredBox && std::abs(mouse.x - c.x) < extent * 0.5f && std::abs(mouse.y - c.y) < extent * 0.5f) hovered = i;
        }

        if (anim) drawEvent(ui, v, *anim, t);

        if (hovered) {
            const uint32_t i = *hovered;
            const ImVec2 c = piecePos(v, i);
            const bool planet = record_.pieces[i].kind == game::CombatPiece::Kind::Planet;
            const float h = (planet ? v.cell * 2.0f : v.cell) * 0.5f;
            dl->AddRect({c.x - h, c.y - h}, {c.x + h, c.y + h}, IM_COL32(255, 255, 255, 220), 0.0f, ui.px(1.5f));
        }
        dl->PopClipRect();
        dl->AddRect(o, o2, IM_COL32(66, 107, 216, 255));

        if (hovered) {
            const uint32_t i = *hovered;
            const game::CombatPiece& rp = record_.pieces[i];
            const CombatPlayback::Piece& p = pieces[i];
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(pieceName(ui, i).c_str());
            if (rp.design.valid() && rp.design.index() < s.designs.size()) {
                const game::Design& d = s.design(rp.design);
                const ruleset::VehicleSize* hull = hullOf(ui, rp);
                labelValue(ui, "Design", hull ? std::format("{} ({})", d.name, hull->name) : d.name, 80);
            }
            labelValue(ui, "Owner", empireName(ui, p.owner), 80);
            if (p.captured) labelValue(ui, "Captured from", empireName(ui, rp.owner), 80);
            labelValue(ui, "Square", std::format("{}, {}", p.x, p.y), 80);
            if (p.damage > 0) labelValue(ui, "Hits taken", std::format("{} damage so far", p.damage), 80);
            ImGui::EndTooltip();
        }
    }

    void drawEvent(UiContext& ui, const View& v, const game::CombatEvent& e, float t) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const game::GameState& s = ui.state();
        const auto& pieces = playback_.pieces();
        if (!playback_.validPiece(e.piece)) return;
        const ImVec2 from = piecePos(v, e.piece);
        const bool hasTarget = playback_.validPiece(e.target);
        const ImVec2 to = hasTarget ? piecePos(v, e.target) : from;
        const ImU32 shooter = empireColor(s, pieces[e.piece].owner);
        ImFont* font = ui.fonts.bold ? ui.fonts.bold : ImGui::GetFont();
        const float fs = std::max(ui.px(14), 11.0f);
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
                const auto& comps = ui.rules().data().components;
                const ruleset::Weapon* w = e.component < comps.size() ? &comps[e.component].weapon : nullptr;
                const std::string type = w ? w->displayType : std::string();
                const int index = w ? toInt(w->display) : 0;
                if (type == "Beam") {
                    const Sprite beam = ui.art.cell("Pictures/Combat/Beams.bmp", std::max(0, index - 1), 20, 20);
                    const float a = std::sin(std::numbers::pi_v<float> * std::clamp(t, 0.0f, 1.0f));
                    if (beam) drawSpriteAlong(dl, beam, from, to, v.cell * 0.5f, IM_COL32(255, 255, 255, int(255 * a)));
                    else dl->AddLine(from, to, withAlpha(shooter, a), ui.px(2.5f));
                } else {
                    const Sprite shot = type == "Seeker" ? raceCell(ui.art, styleOf(ui, pieces[e.piece].owner), "Main.bmp", 40 + 20 * std::clamp(index, 0, 2), 0, 20, 20)
                                                         : ui.art.cell("Pictures/Combat/Torps.bmp", std::max(0, index), 20, 20);
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
                if (!playback_.followsFire(playback_.cursor())) dl->AddLine(from, to, withAlpha(shooter, 0.6f * (1 - t)), ui.px(1.5f));
                const int row = e.amount <= 5 ? 1 : e.amount <= 20 ? 2 : e.amount <= 60 ? 3 : 4;
                const int frame = std::min(7, int(t * 8.0f));
                const float sz = v.cell * 1.2f;
                if (Sprite boom = ui.art.cell("Pictures/Combat/Explosions.bmp", row * 8 + frame, 36, 36))
                    drawSprite(dl, boom, {to.x - sz * 0.5f, to.y - sz * 0.5f}, {to.x + sz * 0.5f, to.y + sz * 0.5f});
                else dl->AddCircleFilled(to, sz * 0.3f * (1 - t * 0.5f), IM_COL32(255, 160, 60, 200));
                label({to.x, to.y - v.cell * (0.6f + 0.5f * t)}, IM_COL32(255, 110, 90, 255), std::format("-{}", e.amount));
                break;
            }
            case Kind::Miss:
                if (!hasTarget) break;
                if (!playback_.followsFire(playback_.cursor())) dl->AddLine(from, to, withAlpha(shooter, 0.4f * (1 - t)), ui.px(1.0f));
                label({to.x + v.cell * 0.4f, to.y - v.cell * (0.6f + 0.3f * t)}, IM_COL32(190, 195, 210, 230), "miss");
                break;
            case Kind::Destroyed: {
                const int frame = std::min(7, int(t * 8.0f));
                Sprite boom = raceCell(ui.art, styleOf(ui, pieces[e.piece].owner), "BigExplosion.bmp", frame * 72, 0, 72, 72);
                if (!boom) boom = ui.art.cell("Pictures/Combat/BigExplosions.bmp", frame, 72, 72);
                const float sz = v.cell * 2.2f;
                if (boom) drawSprite(dl, boom, {from.x - sz * 0.5f, from.y - sz * 0.5f}, {from.x + sz * 0.5f, from.y + sz * 0.5f});
                else dl->AddCircleFilled(from, sz * 0.4f * t, IM_COL32(255, 200, 80, int(255 * (1 - t))));
                break;
            }
            case Kind::Captured: {
                const ImU32 col = hasTarget && e.target != e.piece ? empireColor(s, pieces[e.target].owner) : IM_COL32(255, 220, 80, 255);
                dl->AddCircle(from, v.cell * (0.6f + 0.4f * t), withAlpha(col, 1 - t * 0.5f), 0, ui.px(2.5f));
                label({from.x, from.y - v.cell * 1.1f}, IM_COL32(255, 220, 80, 255), "Captured");
                break;
            }
            case Kind::Launch: {
                const ImVec2 c = pieceCenter(v, e.piece, float(e.x), float(e.y));
                dl->AddCircle(c, v.cell * (0.2f + 0.6f * t), IM_COL32(200, 230, 255, int(255 * (1 - t))), 0, ui.px(2));
                break;
            }
        }
    }

    // Weapon and explosion sounds for the events played this frame (a few at most).
    void playSounds(UiContext& ui, size_t from, size_t to) {
        if (to <= from || to - from > 8) return;  // skipping around is silent
        for (size_t i = from; i < to; ++i) {
            const game::CombatEvent& e = playback_.event(i);
            if (e.kind == game::CombatEvent::Kind::Fire && e.component < ui.rules().data().components.size())
                audio().play(ui.rules().component(e.component).weapon.sound);
            else if (e.kind == game::CombatEvent::Kind::Destroyed)
                audio().play(std::array<std::string_view, 3>{"boom1", "boom2", "boom3"}[i % 3]);
        }
    }

    int wanted_ = -1;
    int index_ = -1;
    uint32_t loadedTurn_ = 0;
    game::CombatRecord record_;
    CombatPlayback playback_;
    size_t listCursor_ = SIZE_MAX;
};

} // namespace

std::unique_ptr<Screen> makeCombatReplay(const ScreenArgs& args) { return std::make_unique<CombatReplayScreen>(args.index); }

} // namespace opense4::client::classic
