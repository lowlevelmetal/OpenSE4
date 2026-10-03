// Combat Replay and Combat Replay Options (docs/spec/06 §1.6, §1.10.3, §3.4;
// spec 04 §17): a battle of the last processed turn played back one combat
// turn at a time with Next (or Space); Options opens the replay's options,
// kept with the empire; Esc closes. Playback state lives in CombatPlayback
// (replay.hpp), which draws every frame with the waits of spec 06 §1.10.3
// (none with the replay's Fast Tactical Combat); this file draws it.
//
// The window is the Tactical Combat window's frame (combat_frame,
// combat_map.hpp; spec 06 §1.6, observed in spec 07 session 5): the title
// strip with "Combat Replay", the location, the combat turn and the empires'
// flags, no navigation buttons; the map in the same place; in the right
// column an empty weapon grid of eight rows at (W-232,226), Options and Next
// side by side under it, and the overview map at the bottom. The view does
// not follow the action. Hovering a piece shows its design in the piece
// panel's place (spec 04 §17). OpenSE4's own, off unless Combat Replay
// Options switches it on: each combat turn's events in words and the battle's
// summary, listed in the weapon grid's place (spec 06 §7 Q39).

#include "client/classic/replay.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/combat_map.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"

#include "game/combat.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using Kind = game::CombatEvent::Kind;
using namespace combat_frame;

// Stop Replay in the options window closes the replay.
bool gStopReplay = false;

// Playback pace (spec 06 §1.10.3): the replay's own Fast Tactical Combat and
// "animate ship movement in combat replay" switches, as in the tactical window.
CombatPace replayPace(const game::Rules& r, const game::InterfaceOptions& o) { return combatPace(r, o.replayFast, o.replayAnimate); }

// The right column below the panel (observed at 1024 x 768, spec 07 session
// 5): the grid of eight rows, 216 x 289 at (W-232,226), and the 109 x 26
// buttons Options at (W-232,534) and Next at (W-119,534). Placed from the
// frame's bottom, so they keep their distance to the overview map on every
// frame; on a shorter frame the grid has the rows that fit under the piece
// panel (inferred for 800 x 600: six).
float buttonsY() { return frameH() - 234; }
int gridRows() { return std::clamp(int((buttonsY() - 19 - 1 - (kPanelY + kPanelH + 2)) / kCell), 1, 8); }
float gridY() { return buttonsY() - 19 - kCell * float(gridRows()) - 1; }

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
        if (playback_.pace().beams.empty() || playback_.pace().fast != opts.replayFast || playback_.pace().animateMoves != opts.replayAnimate) {
            CombatPace pace = replayPace(ui.rules(), opts);
            // No move animated across the edge of the shown map (§1.10.3).
            pace.inView = [this](int x, int y) {
                if (viewSize_.x <= 0.0f || viewSize_.y <= 0.0f) return true;
                const ImVec2 half{viewSize_.x * 0.5f, viewSize_.y * 0.5f};
                return squareInView(view_, {view_.center.x - half.x, view_.center.y - half.y}, {view_.center.x + half.x, view_.center.y + half.y}, x, y);
            };
            playback_.setPace(std::move(pace));
        }
        // Next plays one combat turn: animated, or at once with animation off.
        const size_t before = playback_.cursor();
        if (playback_.playing() && playback_.cursor() >= stopAt_) playback_.pause();
        playback_.advance(ui.dt);
        if (playback_.cursor() > stopAt_) {
            playback_.pause();
            playback_.seekEvent(stopAt_);
        }
        painter(ui).sounds(before, playback_.cursor());

        Dialog d(ui, "Combat Replay", DialogSize::Full, 0);
        if (!d.open()) return d.keepOpen();
        const CombatMapPainter paint = painter(ui);
        {
            // The title strip at the layout's places (spec 06 §2.1.1).
            std::vector<std::string> flags;
            for (game::EmpireId e : record_.participants) flags.push_back(paint.styleOf(e));
            combatTitleStrip(ui, d, sectorName(ui.state(), record_.location, ui.session.player()), std::to_string(std::max(playback_.round(), 0)), flags, -1);
        }
        ImGui::SetCursorScreenPos(d.at({10, 38}));
        const std::optional<uint32_t> hovered = drawMap(ui, paint, ui.size({mapWidth(), mapHeight()}), opts);
        if (hovered) piecePanel(ui, d, paint, *hovered);
        const Vec2 grid{sideX(), gridY()};
        if (settings().replayEvents) eventList(ui, d, paint, grid);
        else weaponGridLines(ui, d, grid, gridRows());
        // Options and Next side by side; Next dims after the last combat turn.
        ImGui::SetCursorScreenPos(d.at({sideX(), buttonsY()}));
        if (classicButton(ui, "Options", {109, 26})) ui.open(ScreenId::CombatReplayOptions);
        ImGui::SetCursorScreenPos(d.at({frameW() - 119, buttonsY()}));
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (classicButton(ui, "Next", {109, 26}, 0, false, !playback_.atEnd() && !playback_.playing()) ||
            (ImGui::IsKeyPressed(ImGuiKey_Space, false) && focused))
            next();
        combatOverview(ui, d, paint, view_, viewSize_, opts.replayViewRect, cx_, cy_);
        if (focused && !ImGui::GetIO().WantTextInput) combatViewKeys(ui, cx_, cy_);
        // Esc closes (spec 06 §3.4); there is no Close button.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && focused) d.requestClose();
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
        // The view opens at 36 px a square on the viewer's pieces as the battle
        // began, as the Tactical Combat window does; without any, on the squares
        // the pieces visit (inferred).
        const CombatPlayback::Bounds& b = playback_.bounds();
        cx_ = (float(b.minX) + float(b.maxX + 1)) * 0.5f;
        cy_ = (float(b.minY) + float(b.maxY + 1)) * 0.5f;
        float sx = 0, sy = 0;
        int n = 0;
        for (const game::CombatPiece& p : record_.pieces)
            if (p.owner == ui.session.player() && p.kind != game::CombatPiece::Kind::Seeker) {
                sx += float(p.startX);
                sy += float(p.startY);
                ++n;
            }
        if (n > 0) {
            cx_ = sx / float(n) + 0.5f;
            cy_ = sy / float(n) + 0.5f;
        }
        cellFrame_ = kCell;
    }

    void next() {
        if (playback_.atEnd()) return;
        const int round = std::max(0, playback_.round()) + 1;
        stopAt_ = round + 1 <= playback_.roundCount() ? playback_.roundStart(round + 1) : playback_.eventCount();
        playback_.play();
    }

    CombatMapPainter painter(UiContext& ui) const { return CombatMapPainter(ui, ui.state(), record_, playback_); }

    // ---- The map ----------------------------------------------------------------------------------------

    // The map at the cursor, `size` ImGui units: the background, the grid with
    // Show Grid, every piece, a frame round the one under the pointer, which
    // is returned. The wheel zooms and the middle button drags (ours).
    std::optional<uint32_t> drawMap(UiContext& ui, const CombatMapPainter& paint, ImVec2 size, const game::InterfaceOptions& opts) {
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##map", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hoveredBox = ImGui::IsItemHovered();
        combatViewInput(ui, o, size, hoveredBox, ImGui::IsItemActive(), cx_, cy_, cellFrame_);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o2{o.x + size.x, o.y + size.y};
        CombatView v;
        v.cell = ui.px(cellFrame_);
        v.center = {o.x + size.x * 0.5f, o.y + size.y * 0.5f};
        v.cx = cx_;
        v.cy = cy_;
        view_ = v;
        viewSize_ = size;
        dl->PushClipRect(o, o2, true);
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
        return hovered;
    }

    // Hovering a piece shows its design, not its damaged state (spec 04 §17):
    // in the piece panel's place, its picture, name, hull size and owner's flag.
    void piecePanel(UiContext& ui, const Dialog& d, const CombatMapPainter& paint, uint32_t i) {
        if (i >= record_.pieces.size() || i >= playback_.pieces().size()) return;
        const game::GameState& s = ui.state();
        const game::CombatPiece& rp = record_.pieces[i];
        const CombatPlayback::Piece& p = playback_.pieces()[i];
        const game::EmpireId owner = p.neutral ? game::EmpireId{} : p.owner;
        const ruleset::VehicleSize* hull = rp.kind == game::CombatPiece::Kind::Planet ? nullptr : paint.hullOf(rp);
        Sprite picture;
        if (hull) {
            const std::string& style = paint.styleOf(owner);
            picture = ui.art.shipPortrait(style, *hull);
            if (!picture) picture = ui.art.shipMini(style, *hull);
        } else if (rp.planet.valid() && rp.planet.index() < s.galaxy.objects.size()) {
            picture = objectSprite(ui, s.galaxy.object(rp.planet));
        }
        std::string size = hull ? hull->name : std::string{};
        if (rp.design.valid() && rp.design.index() < s.designs.size() && rp.kind != game::CombatPiece::Kind::Planet)
            size = hull ? std::format("{} ({})", hull->name, s.design(rp.design).name) : s.design(rp.design).name;
        combatPanelHead(ui, d, {sideX(), kPanelY}, s, picture, paint.pieceName(i), IM_COL32_WHITE, size, nullptr, {}, owner);
    }

    // ---- OpenSE4's event list ---------------------------------------------------------------------------

    // The combat turn's events in words and, once the last one has played,
    // the battle's summary, in the weapon grid's place (spec 06 §7 Q39).
    void eventList(UiContext& ui, const Dialog& d, const CombatMapPainter& paint, Vec2 at) {
        ImGui::SetCursorScreenPos(d.at(at));
        beginList(ui, "##events", ui.size({kSideW, kCell * float(gridRows()) + 1}), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kLabelBlue, "%s", playback_.round() > 0 ? std::format("Combat Turn {}", playback_.round()).c_str() : "Before the battle");
        const int round = playback_.round();
        const size_t from = round > 0 ? playback_.roundStart(round) : 0;
        const size_t to = playback_.cursor();
        const game::CombatEvent* anim = playback_.animating();
        for (size_t i = from; i < to; ++i) {
            const game::CombatEvent& e = playback_.event(i);
            const std::string text = paint.eventText(e);
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
        if (anim) ImGui::TextColored(ImVec4(1, 1, 0.7f, 1), "> %s", paint.eventText(*anim).c_str());
        if (from == to && !anim) dimText(playback_.atStart() ? "Pieces in their starting positions. Next plays a combat turn." : "Nothing happened this turn.");
        if (playback_.atEnd() && !record_.summary.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kLabelBlue, "Summary");
            for (const std::string& line : record_.summary) ImGui::TextUnformatted(line.c_str());
        }
        ImGui::PopTextWrapPos();
        if (listCursor_ != to) {
            ImGui::SetScrollHereY(1.0f);
            listCursor_ = to;
        }
        endList(ui);
    }

    int wanted_ = -1;
    int index_ = -1;
    uint32_t loadedTurn_ = 0;
    game::CombatRecord record_;
    CombatPlayback playback_;
    size_t stopAt_ = 0;               // the event Next plays up to
    size_t listCursor_ = SIZE_MAX;
    float cx_ = 0, cy_ = 0;           // squares at the map's centre
    float cellFrame_ = kCell;         // frame pixels a square (the wheel zooms, ours)
    CombatView view_;
    ImVec2 viewSize_{0, 0};
};

// ---- Combat Replay Options (spec 06 §1.10.3) ---------------------------------------------------------------

// Kept with the empire and saved with the game (game::InterfaceOptions). As
// observed (spec 07 session 5): the 780 x 475 window, "Options In Use" over a
// list (15,57) 560 x 400 of the window, headings "Animation" and "Tactical
// Combat" in the large silver type with check boxes under them, Stop Replay
// in slot 1 and Close in slot 14. Under an "OpenSE4" heading, our own switch
// for the replay's list of events (ClassicSettings::replayEvents, per computer).
class CombatReplayOptionsScreen final : public Screen {
public:
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        Dialog d(ui, screenTitle(ScreenId::CombatReplayOptions), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        game::InterfaceOptions o = ui.options();
        ImGui::SetCursorScreenPos(d.at({15, 38}));
        ImGui::TextColored(kLabelBlue, "Options In Use");
        ImGui::SetCursorScreenPos(d.at({15, 57}));
        beginList(ui, "##options", ui.size({560, 400}), 18);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0));
        heading(ui, "Animation");
        checkRow(ui, "Animate ship movement in combat replay", &o.replayAnimate);
        heading(ui, "Tactical Combat");
        checkRow(ui, "Fast Tactical Combat", &o.replayFast);
        checkRow(ui, "Show Viewing Rectangle on Map", &o.replayViewRect);
        checkRow(ui, "Show Grid", &o.replayGrid);
        heading(ui, "OpenSE4");
        if (checkRow(ui, "List each combat turn's events and the summary", &settings().replayEvents)) saveSettings();
        ImGui::PopStyleVar();
        endList(ui);
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
