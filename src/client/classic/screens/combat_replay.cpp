// Combat Replay (docs/spec/06 §1.6, docs/spec/04 §17): plays back a battle
// of the last processed turn on a tactical-style map. Playback state lives in
// CombatPlayback (replay.hpp); this file draws it.

#include "client/classic/replay.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/combat_map.hpp"
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

constexpr std::array<float, 5> kSpeeds{0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

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
        painter(ui).sounds(before, playback_.cursor());

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

    CombatMapPainter painter(UiContext& ui) const { return CombatMapPainter(ui, ui.state(), record_, playback_); }

    // ---- Status bar, event list, summary -------------------------------------------------------------

    void statusBar(UiContext& ui) {
        const game::GameState& s = ui.state();
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::Text("Battle at %s", sectorName(s, record_.location, ui.session.player()).c_str());
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
            if (Sprite flag = ui.art.flag(painter(ui).styleOf(e))) {
                image(ui, flag, {26, 18});
                ImGui::SameLine(0, ui.px(5));
            }
            int lost = 0, total = 0;
            for (size_t i = 0; i < record_.pieces.size(); ++i)
                if (record_.pieces[i].owner == e && record_.pieces[i].kind != game::CombatPiece::Kind::Seeker) {
                    ++total;
                    lost += playback_.pieces()[i].destroyed || playback_.pieces()[i].owner != e ? 1 : 0;
                }
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(s, e)), "%s", painter(ui).empireName(e).c_str());
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
            const std::string text = painter(ui).eventText(e);
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
        if (anim) ImGui::TextColored(ImVec4(1, 1, 0.7f, 1), "> %s", painter(ui).eventText(*anim).c_str());
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

    void drawMap(UiContext& ui, ImVec2 size) {
        const game::GameState& s = ui.state();
        const CombatMapPainter paint = painter(ui);
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##map", size);
        const bool hoveredBox = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o2{o.x + size.x, o.y + size.y};
        dl->PushClipRect(o, o2, true);

        // Fit the squares every piece visits, with a margin.
        const CombatPlayback::Bounds& b = playback_.bounds();
        const float cols = float(std::max(12, b.maxX - b.minX + 1 + 2)), rows = float(std::max(12, b.maxY - b.minY + 1 + 2));
        CombatView v;
        v.cell = std::clamp(std::min(size.x / cols, size.y / rows), ui.px(14), ui.px(56));
        v.center = {o.x + size.x * 0.5f, o.y + size.y * 0.5f};
        v.cx = (float(b.minX) + float(b.maxX + 1)) * 0.5f;
        v.cy = (float(b.minY) + float(b.maxY + 1)) * 0.5f;
        paint.background(dl, o, o2, v, IM_COL32(40, 70, 140, 70));
        const std::optional<uint32_t> hovered = paint.pieces(dl, v, hoveredBox, ImGui::GetIO().MousePos);
        if (hovered) {
            const ImVec2 c = paint.piecePos(v, *hovered);
            const float h = v.cell * paint.pieceExtent(*hovered) * 0.5f;
            dl->AddRect({c.x - h, c.y - h}, {c.x + h, c.y + h}, IM_COL32(255, 255, 255, 220), 0.0f, ui.px(1.5f));
        }
        dl->PopClipRect();
        dl->AddRect(o, o2, IM_COL32(66, 107, 216, 255));

        if (hovered) {
            const uint32_t i = *hovered;
            const game::CombatPiece& rp = record_.pieces[i];
            const CombatPlayback::Piece& p = playback_.pieces()[i];
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(paint.pieceName(i).c_str());
            if (rp.design.valid() && rp.design.index() < s.designs.size()) {
                const game::Design& d = s.design(rp.design);
                const ruleset::VehicleSize* hull = paint.hullOf(rp);
                labelValue(ui, "Design", hull ? std::format("{} ({})", d.name, hull->name) : d.name, 80);
            }
            labelValue(ui, "Owner", p.neutral ? std::string("None") : paint.empireName(p.owner), 80);
            if (p.captured) labelValue(ui, "Captured from", paint.empireName(rp.owner), 80);
            labelValue(ui, "Square", std::format("{}, {}", p.x, p.y), 80);
            if (p.damage > 0) labelValue(ui, "Hits taken", std::format("{} damage so far", p.damage), 80);
            ImGui::EndTooltip();
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
