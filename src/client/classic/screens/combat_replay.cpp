// Combat Replay and Combat Replay Options (docs/spec/06 §1.10.3, §3.4; spec
// 04 §17): a battle of the last processed turn played back one combat turn at
// a time with Next (or Space); Options opens the replay's options, kept with
// the empire; Esc closes. Playback state lives in CombatPlayback
// (replay.hpp), which draws every frame with the waits of spec 06 §1.10.3
// (none with the replay's Fast Tactical Combat); this file draws it. Beside
// the map, an overview of the whole combat grid. The turn's events in words
// and the battle's summary under it are an OpenSE4 extension: the original's
// replay keeps no log of any kind (spec 06 §7 Q39; kept, inferred).

#include "client/classic/replay.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/combat_map.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include "game/combat.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using Kind = game::CombatEvent::Kind;

// Stop Replay in the options window closes the replay.
bool gStopReplay = false;

// Playback pace (spec 06 §1.10.3): the replay's own Fast Tactical Combat and
// "animate ship movement in combat replay" switches, as in the tactical window.
CombatPace replayPace(const game::Rules& r, const game::InterfaceOptions& o) { return combatPace(r, o.replayFast, o.replayAnimate); }

class CombatReplayScreen final : public Screen {
public:
    explicit CombatReplayScreen(int index) : wanted_(index) { gStopReplay = false; }

    bool draw(UiContext& ui) override {
        const auto& combats = ui.state().combats;
        if (combats.empty()) return drawEmpty(ui);
        if (gStopReplay) {
            gStopReplay = false;
            return false;
        }
        if (index_ < 0 || index_ >= int(combats.size()) || loadedTurn_ != ui.state().turn)
            load(ui, wanted_ >= 0 && wanted_ < int(combats.size()) ? wanted_ : int(combats.size()) - 1, combats);
        const game::InterfaceOptions& opts = ui.options();
        if (playback_.pace().beams.empty() || playback_.pace().fast != opts.replayFast || playback_.pace().animateMoves != opts.replayAnimate)
            playback_.setPace(replayPace(ui.rules(), opts));
        // Next plays one combat turn: animated, or at once with animation off.
        const size_t before = playback_.cursor();
        if (playback_.playing() && playback_.cursor() >= stopAt_) playback_.pause();
        playback_.advance(ui.dt);
        if (playback_.cursor() > stopAt_) {
            playback_.pause();
            playback_.seekEvent(stopAt_);
        }
        painter(ui).sounds(before, playback_.cursor());

        Dialog d(ui, "Combat Replay", DialogSize::Full);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        statusBar(ui);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float side = ui.px(250);
        const ImVec2 mapSize{avail.x - side - ImGui::GetStyle().ItemSpacing.x, avail.y};
        drawMap(ui, mapSize, opts);
        ImGui::SameLine();
        ImGui::BeginGroup();
        overview(ui, opts, side);
        eventList(ui, ImVec2(side, avail.y * 0.45f));
        summary(ui, ImVec2(side, 0));
        ImGui::EndGroup();

        d.beginButtons();
        if (d.button("Options")) ui.open(ScreenId::CombatReplayOptions);
        if (d.button("Next", !playback_.atEnd() && !playback_.playing()) ||
            (ImGui::IsKeyPressed(ImGuiKey_Space, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
            next();
        // Esc closes (spec 06 §3.4); there is no Close button.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) d.requestClose();
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
        stopAt_ = 0;
        listCursor_ = SIZE_MAX;
    }

    void next() {
        if (playback_.atEnd()) return;
        const int round = std::max(0, playback_.round()) + 1;
        stopAt_ = round + 1 <= playback_.roundCount() ? playback_.roundStart(round + 1) : playback_.eventCount();
        playback_.play();
    }

    CombatMapPainter painter(UiContext& ui) const { return CombatMapPainter(ui, ui.state(), record_, playback_); }

    // ---- Status bar, overview, event list, summary ---------------------------------------------------

    void statusBar(UiContext& ui) {
        const game::GameState& s = ui.state();
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::Text("Battle at %s", sectorName(s, record_.location, ui.session.player()).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        dimText(std::format("  {}", formatDate(record_.turn)).c_str());
        const std::string round = playback_.roundCount() > 0 ? std::format("Combat Turn {} of {}", std::max(playback_.round(), 0), playback_.roundCount())
                                                             : "No combat turns";
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

    // The whole combat grid, with the dotted Viewing Rectangle when the option is on.
    void overview(UiContext& ui, const game::InterfaceOptions& opts, float width) {
        const float w = width, h = width * float(game::combat::kCombatMapHeight) / float(game::combat::kCombatMapWidth);
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        const ImVec2 o2{o.x + w, o.y + h};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(o, o2, IM_COL32(0, 0, 0, 255));
        const float k = w / float(game::combat::kCombatMapWidth);
        CombatView whole;
        whole.center = o;
        whole.cell = k;
        painter(ui).squares(dl, whole, 2.0f);
        if (opts.replayViewRect && view_.cell > 0) {
            const float halfW = viewSize_.x * 0.5f / view_.cell, halfH = viewSize_.y * 0.5f / view_.cell;
            const ImVec2 a{std::max(o.x, o.x + (view_.cx - halfW) * k), std::max(o.y, o.y + (view_.cy - halfH) * k)};
            const ImVec2 c{std::min(o2.x, o.x + (view_.cx + halfW) * k), std::min(o2.y, o.y + (view_.cy + halfH) * k)};
            for (float x = a.x; x < c.x; x += 4) {
                dl->AddLine({x, a.y}, {std::min(x + 2, c.x), a.y}, IM_COL32_WHITE);
                dl->AddLine({x, c.y}, {std::min(x + 2, c.x), c.y}, IM_COL32_WHITE);
            }
            for (float y = a.y; y < c.y; y += 4) {
                dl->AddLine({a.x, y}, {a.x, std::min(y + 2, c.y)}, IM_COL32_WHITE);
                dl->AddLine({c.x, y}, {c.x, std::min(y + 2, c.y)}, IM_COL32_WHITE);
            }
        }
        dl->AddRect(o, o2, IM_COL32(66, 107, 216, 255));
    }

    void eventList(UiContext& ui, ImVec2 size) {
        heading(ui, playback_.round() > 0 ? std::format("Combat Turn {}", playback_.round()).c_str() : "Before the battle");
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
                case Kind::UnitsLost: ImGui::TextColored(ImVec4(1, 0.55f, 0.45f, 1), "  %s", text.c_str()); break;
                case Kind::Captured: ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "%s", text.c_str()); break;
                default: ImGui::TextUnformatted(text.c_str()); break;
            }
        }
        if (anim) ImGui::TextColored(ImVec4(1, 1, 0.7f, 1), "> %s", painter(ui).eventText(*anim).c_str());
        if (from == to && !anim) dimText(playback_.atStart() ? "Pieces in their starting positions. Next plays a combat turn." : "Nothing happened this turn.");
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
        if (playback_.atEnd())
            for (const std::string& line : record_.summary) ImGui::TextUnformatted(line.c_str());
        else dimText("Shown once the last combat turn has played.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }

    // ---- The map ----------------------------------------------------------------------------------------

    void drawMap(UiContext& ui, ImVec2 size, const game::InterfaceOptions& opts) {
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
        view_ = v;
        viewSize_ = size;
        // Show Grid (Combat Replay Options).
        paint.background(dl, o, o2, v, opts.replayGrid ? IM_COL32(40, 70, 140, 70) : 0);
        const std::optional<uint32_t> hovered = paint.pieces(dl, v, hoveredBox, ImGui::GetIO().MousePos);
        if (hovered) {
            const ImVec2 c = paint.piecePos(v, *hovered);
            const float h = v.cell * paint.pieceExtent(*hovered) * 0.5f;
            dl->AddRect({c.x - h, c.y - h}, {c.x + h, c.y + h}, IM_COL32(255, 255, 255, 220), 0.0f, ui.px(1.5f));
        }
        dl->PopClipRect();
        dl->AddRect(o, o2, IM_COL32(66, 107, 216, 255));

        // Hovering a piece shows its design, not its damaged state (spec 04 §17).
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
            ImGui::EndTooltip();
        }
    }

    int wanted_ = -1;
    int index_ = -1;
    uint32_t loadedTurn_ = 0;
    game::CombatRecord record_;
    CombatPlayback playback_;
    size_t stopAt_ = 0;               // the event Next plays up to
    size_t listCursor_ = SIZE_MAX;
    CombatView view_;
    ImVec2 viewSize_{0, 0};
};

// ---- Combat Replay Options (spec 06 §1.10.3) ---------------------------------------------------------------

// Kept with the empire and saved with the game (game::InterfaceOptions).
class CombatReplayOptionsScreen final : public Screen {
public:
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        Dialog d(ui, screenTitle(ScreenId::CombatReplayOptions), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        game::InterfaceOptions o = ui.options();
        d.beginContent();
        ImGui::TextColored(kLabelBlue, "Options In Use");
        ImGui::BeginChild("##options", ImVec2(0, 0), ImGuiChildFlags_Borders);
        lampToggle(ui, "Animate ship movement in combat replay", &o.replayAnimate);
        lampToggle(ui, "Fast Tactical Combat", &o.replayFast);
        lampToggle(ui, "Show Viewing Rectangle on Map", &o.replayViewRect);
        lampToggle(ui, "Show Grid", &o.replayGrid);
        ImGui::EndChild();
        ui.setOptions(o);
        d.beginButtons();
        if (d.button("Stop Replay")) {
            gStopReplay = true;
            d.requestClose();
        }
        d.close();
        return d.keepOpen();
    }
};

} // namespace

std::unique_ptr<Screen> makeCombatReplay(const ScreenArgs& args) { return std::make_unique<CombatReplayScreen>(args.index); }
std::unique_ptr<Screen> makeCombatReplayOptions(const ScreenArgs&) { return std::make_unique<CombatReplayOptionsScreen>(); }

} // namespace opense4::client::classic
