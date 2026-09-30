// Tactical Combat, Tactical Combat Orders and Tactical Combat Options
// (docs/spec/06 §1.6, §3.3; the rules are docs/spec/04 §4-§12): the player's
// side of the combat::TacticalBattle the session holds (ClassicSession::
// tactical()): a turn-based game's battle or a combat simulation.
//
// The map is the Combat Replay's (combat_map.hpp): the battle's record plays
// back as it grows, so every move and shot, the computer's too, is seen.
// Left-click selects an own piece, moves it to an empty square or fires its
// enabled weapons at an enemy; right-click reports on a piece. The pointer
// shows the move direction or crosshairs. Hotkeys follow spec 06 §3.3.

#include "client/audio.hpp"
#include "client/classic/replay.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/combat_map.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"

#include "game/combat.hpp"
#include "game/tactical.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace opense4::client::classic {

namespace {

using game::combat::Square;
using game::combat::TacticalBattle;
using game::combat::TacticalOrder;
using game::combat::TacticalPiece;
using game::combat::TacticalWeapon;
using OK = TacticalOrder::Kind;
using PieceKind = game::CombatPiece::Kind;

constexpr float kStatusH = 46;       // frame pixels
constexpr float kMapW = 676;
constexpr float kSideX = 684;
constexpr float kSideW = 310;
constexpr std::array<float, 5> kSpeeds{0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

constexpr const char* kPausedHint = "Auto is on: every empire follows its strategies. End Turn goes on; switch Auto off to give orders again.";

// A target picked on the map for an order armed in the Orders window (or R, C, T).
enum class Aim { None, Ram, Capture, DropTroops };

// What the three windows share for the battle in progress.
struct TacticalUi {
    const TacticalFight* fight = nullptr;
    int selected = -1;
    int target = -1;             // the enemy last hovered or fired at
    Aim aim = Aim::None;
    int groupSize = 10;          // Launch Fighters in Groups (one of kFighterGroupSizes)
    int groupNumber = 1;         // for Set Group Leader / Member
    int formation = 0;           // Formations.txt index for Set Group Leader
    int launchWindow = 0;        // the Launch Units window's session (spec 04 §10.4)
    std::string message;         // the last refusal or hint
    float cx = 36, cy = 31;      // squares at the map's centre
    float cellFrame = 30;        // zoom: frame pixels per square
    bool placed = false;         // the view has been centred on the player's pieces
    // Orders given while a window draws, carried out when it has drawn (they
    // change the pieces the window is drawing), and whether to work out the
    // results then.
    std::vector<TacticalOrder> queue;
    bool finishNow = false;
};

TacticalUi& state() {
    static TacticalUi u;
    return u;
}

// Resets the shared state when a new battle starts.
TacticalUi& stateFor(const TacticalFight* fight) {
    TacticalUi& u = state();
    if (u.fight != fight) {
        u = TacticalUi{};
        u.fight = fight;
    }
    return u;
}

void submit(TacticalFight&, TacticalOrder o) { state().queue.push_back(std::move(o)); }

// Carries out the orders given this frame, in order.
void flush(TacticalFight& f) {
    TacticalUi& u = state();
    std::vector<TacticalOrder> queue = std::move(u.queue);
    u.queue.clear();
    for (const TacticalOrder& o : queue) {
        u.message = f.battle->submit(o);
    }
    if (u.finishNow && f.battle->finished() && !f.battle->applied()) f.battle->finish();
    u.finishNow = false;
}

bool isDrone(const TacticalPiece& p) { return p.kind == PieceKind::UnitGroup && p.type == ruleset::VehicleType::Drone; }

// An own piece that takes orders.
bool commandable(const TacticalPiece& p, game::EmpireId side) {
    return p.alive && p.owner == side && p.kind != PieceKind::Seeker && p.kind != PieceKind::Obstacle && !isDrone(p) && !p.mothballed;
}
bool canMove(const TacticalPiece& p, game::EmpireId side) {
    return commandable(p, side) && (p.kind == PieceKind::Vehicle || p.kind == PieceKind::UnitGroup) && p.movement > 0;
}
bool canFire(const TacticalPiece& p, game::EmpireId side) {
    if (!commandable(p, side) || !p.hasSupply) return false;
    for (const TacticalWeapon& w : p.weapons)
        for (int k = 0; k < w.instances && k < int(w.reload.size()); ++k)
            if (w.reload[size_t(k)] == 0) return true;
    return false;
}

// The next own piece (after `from`, backwards with `step` -1) that passes `test`.
template <class Test>
int cycle(const TacticalBattle& b, int from, int step, Test&& test) {
    const int n = int(b.pieces().size());
    for (int k = 1; k <= n; ++k) {
        const int i = ((from < 0 ? (step > 0 ? -1 : 0) : from) + step * k % n + n * 2) % n;
        if (test(b.pieces()[size_t(i)])) return i;
    }
    return -1;
}

// Facing 0..7 of a direction (spec 03 §10 order), for the move pointer.
int directionOf(int dx, int dy) {
    constexpr std::array<std::pair<int, int>, 8> kFacing{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {-1, -1}, {1, 1}, {-1, 1}}};
    const int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
    for (size_t f = 0; f < kFacing.size(); ++f)
        if (kFacing[f].first == sx && kFacing[f].second == sy) return int(f);
    return 0;
}

float angleOf(int dx, int dy) { return std::atan2(float(dx), float(-dy)); }

// A unit group's units: "Wasp x5", or each design of a group that mixes them (spec 03 §12).
std::string unitsText(const game::GameState& s, const TacticalPiece& p) {
    if (p.units.size() < 2) return std::format("{} x{}", p.design.index() < s.designs.size() ? s.design(p.design).name : std::string("?"), p.count);
    std::string out;
    for (const game::UnitStack& st : p.units)
        out += std::format("{}{} x{}", out.empty() ? "" : ", ", st.design.index() < s.designs.size() ? s.design(st.design).name : "?", st.count);
    return out;
}

// ---- The Tactical Combat window --------------------------------------------------------------------------

class TacticalCombatScreen final : public Screen {
public:
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        TacticalFight* f = ui.session.tactical();
        if (!f || !f->battle) return false;
        TacticalUi& u = stateFor(f);
        TacticalBattle& b = *f->battle;
        sync(ui, b);
        automatic(ui, *f);
        // Drawing reads the battle; orders wait for flush() at the end of the frame.

        Dialog d(ui, f->title.c_str(), DialogSize::Full, 0);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        const game::GameState& s = b.state();
        const CombatMapPainter paint(ui, s, b.record(), playback_);
        statusBar(ui, b, paint);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        drawMap(ui, *f, paint, ImVec2(ui.px(kMapW), ImGui::GetContentRegionAvail().y));
        ImGui::SetCursorScreenPos({origin.x + ui.px(kSideX), origin.y});
        ImGui::BeginGroup();
        currentPanel(ui, *f);
        targetPanel(ui, *f, paint);
        overviewAndButtons(ui, *f, paint);
        ImGui::EndGroup();
        keys(ui, *f);
        if (!u.message.empty()) {
            ImGui::SetCursorScreenPos({origin.x + ui.px(kSideX), origin.y + ui.px(636)});
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui.px(kSideW));
            ImGui::TextColored(ImVec4(1, 0.72f, 0.45f, 1), "%s", u.message.c_str());
            ImGui::PopTextWrapPos();
        }
        reportPopup(ui, b, paint);
        flush(*f);
        // Troops of a player side landed, or landed on one: the Ground Combat window,
        // once the landing has played (spec 06 §1.6).
        const std::vector<game::GroundCombat>& grounds = b.record().grounds;
        if (grounds.size() > groundsSeen_ && !animating()) {
            for (size_t k = groundsSeen_; k < grounds.size(); ++k)
                if (b.isPlayer(grounds[k].attacker) || b.isPlayer(grounds[k].defender)) {
                    ScreenArgs a;
                    a.sub = int(k);
                    ui.open(ScreenId::GroundCombat, std::move(a));
                }
            groundsSeen_ = grounds.size();
        }
        // No Close: a battle is fought to its end (Resolve Combat hands it to the strategies).
        if (done_) {
            // Last, as it ends the battle this frame drew: a game battle's orders
            // answer its question and the game carries on.
            ui.session.endTactical();
            return false;
        }
        return true;
    }

private:
    // ---- The record as it grows ------------------------------------------------------------------

    void sync(UiContext& ui, TacticalBattle& b) {
        const game::CombatRecord& rec = b.record();
        if (&rec != record_ || rec.events.size() != events_ || rec.pieces.size() != pieces_) {
            const size_t cursor = record_ ? playback_.cursor() : std::min(ui.session.tactical()->seen, rec.events.size());
            playback_ = CombatPlayback(rec);
            playback_.setSpeed(settings().tacticalSpeed);
            playback_.seekEvent(std::min(cursor, playback_.eventCount()));
            if (settings().tacticalAnimate && !playback_.atEnd()) playback_.play();
            else playback_.seekEvent(playback_.eventCount());
            record_ = &rec;
            events_ = rec.events.size();
            pieces_ = rec.pieces.size();
        }
        const size_t before = playback_.cursor();
        playback_.advance(ui.dt);
        CombatMapPainter(ui, b.state(), rec, playback_).sounds(before, playback_.cursor());
    }

    bool animating() const { return playback_.playing() && !playback_.atEnd(); }
    void skipAnimation() { playback_.seekEvent(playback_.eventCount()); }

    // The steps nobody needs to click: a phase with nothing left to fight ends.
    // The side's drones and seekers have moved before the player gets control (spec 04 §4).
    void automatic(UiContext& ui, TacticalFight& f) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        // Once the last phase has played out, the results are worked out (on the
        // battle's copy of the game) for the result panel.
        if (b.finished() && !b.applied() && !animating()) u.finishNow = true;
        if (!b.awaitingOrders() || animating()) return;
        const game::EmpireId side = b.phaseEmpire();
        if (b.paused()) {
            if (u.message.empty()) u.message = kPausedHint;
            return;
        }
        if (u.message == kPausedHint) u.message.clear();
        bool any = false;
        for (const TacticalPiece& p : b.pieces()) any = any || commandable(p, side);
        if (!any || (b.over() && settings().tacticalAutoEnd)) {
            submit(f, TacticalOrder{OK::EndPhase, side});
            u.message = any ? "No enemy is left to fight." : std::string{};
            return;
        }
        // Keep a piece of the side in phase selected, and an enemy in the target panel.
        if (u.selected < 0 || size_t(u.selected) >= b.pieces().size() || !commandable(b.pieces()[size_t(u.selected)], side)) {
            u.selected = cycle(b, -1, 1, [&](const TacticalPiece& p) { return canMove(p, side); });
            if (u.selected < 0) u.selected = cycle(b, -1, 1, [&](const TacticalPiece& p) { return commandable(p, side); });
            centreOn(b, u.selected);
        }
        if (u.selected >= 0 && (u.target < 0 || size_t(u.target) >= b.pieces().size() || !b.pieces()[size_t(u.target)].alive ||
                                !b.hostile(side, b.pieces()[size_t(u.target)].owner))) {
            int best = -1;
            for (size_t j = 0; j < b.pieces().size(); ++j) {
                const TacticalPiece& q = b.pieces()[j];
                if (!q.alive || q.kind == PieceKind::Seeker || q.kind == PieceKind::Obstacle || !b.hostile(side, q.owner)) continue;
                if (best < 0 || b.distance(u.selected, int(j)) < b.distance(u.selected, best)) best = int(j);
            }
            u.target = best;
        }
        (void)ui;
    }

    void centreOn(const TacticalBattle& b, int piece) {
        if (piece < 0 || size_t(piece) >= b.pieces().size()) return;
        TacticalUi& u = state();
        const TacticalPiece& p = b.pieces()[size_t(piece)];
        u.cx = float(p.x) + float(p.size) * 0.5f;
        u.cy = float(p.y) + float(p.size) * 0.5f;
        u.placed = true;
    }

    // ---- Status bar ---------------------------------------------------------------------------------

    void statusBar(UiContext& ui, TacticalBattle& b, const CombatMapPainter& paint) {
        const game::GameState& s = b.state();
        const TacticalUi& u = state();
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::Text("Battle at %s", sectorName(s, b.record().location, ui.session.player()).c_str());
        ImGui::PopFont();
        const std::string turn = b.finished() ? std::string("The battle is over")
                                              : std::format("Combat turn {} of {}", std::min(b.round(), b.lastRound()), b.lastRound());
        ImGui::SameLine(ui.px(kMapW) - ImGui::CalcTextSize(turn.c_str()).x);
        ImGui::TextColored(kLabelBlue, "%s", turn.c_str());
        ImGui::SameLine(ui.px(kSideX));
        if (b.awaitingOrders()) {
            const std::string who = paint.empireName(b.phaseEmpire());
            const std::string what = animating() ? "wait for orders" : b.paused() ? "paused (Auto)" : "give orders";
            ImGui::TextColored(ImVec4(0.55f, 1, 0.55f, 1), "%s", std::format("{}: {}", who, what).c_str());
        } else {
            dimText(b.finished() ? "Results are ready" : "Watching");
        }
        // Participants: flag, name, pieces left.
        bool first = true;
        for (game::EmpireId e : b.participants()) {
            if (!first) ImGui::SameLine(0, ui.px(14));
            first = false;
            if (Sprite flag = ui.art.flag(paint.styleOf(e), false)) {
                image(ui, flag, {20, 14});
                ImGui::SameLine(0, ui.px(4));
            }
            int left = 0;
            for (const TacticalPiece& p : b.pieces())
                if (p.alive && p.owner == e && p.kind != PieceKind::Seeker && p.kind != PieceKind::Obstacle) ++left;
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(s, e)), "%s", paint.empireName(e).c_str());
            ImGui::SameLine(0, ui.px(4));
            dimText(std::format("{} left{}", left, b.phaseEmpire() == e ? " *" : "").c_str());
        }
        // Next / previous selectors: pieces that can move, pieces that can fire.
        const game::EmpireId side = b.phaseEmpire();
        if (side.valid()) {
            ImGui::SameLine(ui.px(kSideX));
            selector(ui, b, "Move", [&](const TacticalPiece& p) { return canMove(p, side); });
            ImGui::SameLine(0, ui.px(10));
            selector(ui, b, "Fire", [&](const TacticalPiece& p) { return canFire(p, side); });
        }
        (void)u;
        ImGui::Dummy(ui.size({1, 2}));
    }

    template <class Test>
    void selector(UiContext& ui, TacticalBattle& b, const char* label, Test&& test) {
        TacticalUi& u = state();
        ImGui::PushID(label);
        if (ImGui::SmallButton("<")) {
            u.selected = cycle(b, u.selected, -1, test);
            centreOn(b, u.selected);
        }
        ImGui::SameLine(0, ui.px(3));
        ImGui::TextUnformatted(label);
        ImGui::SameLine(0, ui.px(3));
        if (ImGui::SmallButton(">")) {
            u.selected = cycle(b, u.selected, 1, test);
            centreOn(b, u.selected);
        }
        ImGui::PopID();
    }

    // ---- The tactical map -------------------------------------------------------------------------------

    void drawMap(UiContext& ui, TacticalFight& f, const CombatMapPainter& paint, ImVec2 size) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const game::EmpireId side = b.phaseEmpire();
        if (!u.placed) {
            // Centre on the player's pieces.
            float sx = 0, sy = 0;
            int n = 0;
            for (const TacticalPiece& p : b.pieces())
                if (p.alive && b.isPlayer(p.owner) && p.kind != PieceKind::Seeker) {
                    sx += float(p.x);
                    sy += float(p.y);
                    ++n;
                }
            if (n > 0) {
                u.cx = sx / float(n) + 0.5f;
                u.cy = sy / float(n) + 0.5f;
            }
            u.placed = true;
        }
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##tacticalmap", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hoveredBox = ImGui::IsItemHovered();
        const ImVec2 o2{o.x + size.x, o.y + size.y};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();

        // Zoom with the wheel around the pointer; drag with the middle button.
        if (hoveredBox && io.MouseWheel != 0.0f) {
            const float old = u.cellFrame;
            u.cellFrame = std::clamp(u.cellFrame * (io.MouseWheel > 0 ? 1.25f : 0.8f), 6.0f, 48.0f);
            const float k = 1.0f / ui.px(old) - 1.0f / ui.px(u.cellFrame);
            u.cx += (io.MousePos.x - (o.x + size.x * 0.5f)) * k;
            u.cy += (io.MousePos.y - (o.y + size.y * 0.5f)) * k;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            u.cx -= io.MouseDelta.x / ui.px(u.cellFrame);
            u.cy -= io.MouseDelta.y / ui.px(u.cellFrame);
        }
        u.cx = std::clamp(u.cx, 0.0f, float(game::combat::kCombatMapWidth));
        u.cy = std::clamp(u.cy, 0.0f, float(game::combat::kCombatMapHeight));

        CombatView v;
        v.cell = ui.px(u.cellFrame);
        v.center = {o.x + size.x * 0.5f, o.y + size.y * 0.5f};
        v.cx = u.cx;
        v.cy = u.cy;
        view_ = v;
        viewSize_ = size;
        dl->PushClipRect(o, o2, true);
        paint.background(dl, o, o2, v, settings().tacticalGrid ? IM_COL32(40, 70, 140, 60) : 0);
        // The map's edges.
        const ImVec2 m0 = v.at(0, 0), m1 = v.at(float(game::combat::kCombatMapWidth), float(game::combat::kCombatMapHeight));
        dl->AddRect(m0, m1, IM_COL32(90, 120, 200, 200), 0.0f, ui.px(1.5f));

        const TacticalPiece* sel = u.selected >= 0 && size_t(u.selected) < b.pieces().size() ? &b.pieces()[size_t(u.selected)] : nullptr;
        if (sel && !sel->alive) sel = nullptr;
        // Movement and weapon reach of the selected piece.
        if (sel && settings().tacticalRanges && !animating()) {
            const float x0 = float(sel->x), y0 = float(sel->y), ext = float(sel->size);
            if (sel->movement > 0 && b.isPlayer(sel->owner))
                dl->AddRectFilled(v.at(x0 - float(sel->movement), y0 - float(sel->movement)),
                                  v.at(x0 + ext + float(sel->movement), y0 + ext + float(sel->movement)), IM_COL32(60, 160, 255, 26));
            int reach = 0;
            for (const TacticalWeapon& w : sel->weapons)
                if (w.enabled && w.instances > 0 && w.kind != ruleset::WeaponKind::Seeking) reach = std::max(reach, w.reach);
            if (reach > 0)
                dl->AddRect(v.at(x0 - float(reach), y0 - float(reach)), v.at(x0 + ext + float(reach), y0 + ext + float(reach)),
                            IM_COL32(255, 120, 90, 110), 0.0f, ui.px(1.2f));
        }

        const std::optional<uint32_t> hovered = paint.pieces(dl, v, hoveredBox, io.MousePos);
        const auto [hx, hy] = v.square(io.MousePos);

        // Selection and group badges.
        for (size_t i = 0; i < b.pieces().size() && i < playback_.pieces().size(); ++i) {
            const TacticalPiece& p = b.pieces()[i];
            if (!p.alive || !playback_.pieces()[i].onMap) continue;
            const ImVec2 c = paint.piecePos(v, uint32_t(i));
            const float h = v.cell * float(p.size) * 0.5f;
            if (int(i) == u.selected) dl->AddRect({c.x - h - 1, c.y - h - 1}, {c.x + h + 1, c.y + h + 1}, IM_COL32(255, 230, 80, 255), 0.0f, ui.px(2));
            if ((p.isLeader || p.leader >= 0 || p.group >= 0) && b.isPlayer(p.owner)) {
                const ImU32 badge = p.isLeader ? IM_COL32(70, 120, 255, 255) : IM_COL32(230, 60, 60, 255);
                const float r = std::max(3.0f, v.cell * 0.18f);
                dl->AddRectFilled({c.x + h - 2 * r, c.y - h}, {c.x + h, c.y - h + 2 * r}, badge);
                if (p.group >= 0 && v.cell >= 14) dl->AddText({c.x + h - 2 * r + 1, c.y - h - 2}, IM_COL32_WHITE, std::format("{}", p.group).c_str());
            }
            if (settings().tacticalNames && p.kind != PieceKind::Seeker && v.cell >= 10) {
                const std::string name = paint.pieceName(uint32_t(i));
                const ImVec2 ts = ImGui::CalcTextSize(name.c_str());
                dl->AddText({c.x - ts.x * 0.5f, c.y + h}, IM_COL32(200, 210, 230, 200), name.c_str());
            }
        }

        // What a click would do, shown by the pointer.
        const bool ours = sel && side.valid() && sel->owner == side && b.awaitingOrders();
        const TacticalPiece* over = hovered && *hovered < b.pieces().size() ? &b.pieces()[*hovered] : nullptr;
        if (over && !over->alive) over = nullptr;
        const bool enemy = over && side.valid() && b.hostile(side, over->owner) && over->kind != PieceKind::Obstacle;
        if (enemy) u.target = int(*hovered);
        bool hidePointer = false;
        if (hoveredBox && ours && !animating()) {
            if (u.aim != Aim::None) {
                pointerCross(ui, dl, io.MousePos, enemy ? IM_COL32(255, 200, 60, 255) : IM_COL32(150, 150, 150, 200));
                hidePointer = true;
            } else if (enemy) {
                const std::string why = b.check(TacticalOrder{OK::Fire, side, u.selected, int(*hovered)});
                pointerCross(ui, dl, io.MousePos, why.empty() ? IM_COL32(80, 255, 90, 255) : IM_COL32(255, 70, 60, 255));
                hidePointer = true;
                if (!why.empty()) ImGui::SetTooltip("%s", why.c_str());
            } else if (!over && canMove(*sel, side)) {
                const std::vector<Square> path = b.pathTo(u.selected, hx, hy);
                for (const Square& sq : path) dl->AddCircleFilled(v.at(float(sq.x) + 0.5f, float(sq.y) + 0.5f), std::max(2.0f, v.cell * 0.12f), IM_COL32(120, 220, 255, 200));
                if (!path.empty()) {
                    pointerArrow(ui, dl, io.MousePos, directionOf(hx - sel->x, hy - sel->y), IM_COL32(120, 220, 255, 255));
                    hidePointer = true;
                }
            }
        }
        if (hidePointer) ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        dl->PopClipRect();
        dl->AddRect(o, o2, IM_COL32(66, 107, 216, 255));

        if (b.finished() && !animating()) resultPanel(ui, f, o, size);

        // Clicks.
        if (hoveredBox && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (animating()) {
                skipAnimation();   // a click while the battle plays skips to its end
            } else {
                click(ui, f, hovered ? int(*hovered) : -1, hx, hy);
            }
        }
        if (hoveredBox && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered) {
            report_ = int(*hovered);
            ImGui::OpenPopup("##piecereport");
        }
    }

    void pointerArrow(UiContext& ui, ImDrawList* dl, ImVec2 at, int facing, ImU32 color) {
        constexpr std::array<std::pair<int, int>, 8> kFacing{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {-1, -1}, {1, 1}, {-1, 1}}};
        const float a = angleOf(kFacing[size_t(facing)].first, kFacing[size_t(facing)].second);
        const float r = ui.px(11);
        auto rot = [&](float x, float y) { return ImVec2{at.x + x * std::cos(a) - y * std::sin(a), at.y + x * std::sin(a) + y * std::cos(a)}; };
        dl->AddTriangleFilled(rot(0, -r), rot(-r * 0.7f, 0), rot(r * 0.7f, 0), color);
        dl->AddRectFilled(rot(-r * 0.25f, 0), rot(r * 0.25f, r * 0.8f), color);
        dl->AddQuadFilled(rot(-r * 0.25f, 0), rot(r * 0.25f, 0), rot(r * 0.25f, r * 0.8f), rot(-r * 0.25f, r * 0.8f), color);
    }

    void pointerCross(UiContext& ui, ImDrawList* dl, ImVec2 at, ImU32 color) {
        const float r = ui.px(10);
        dl->AddCircle(at, r, color, 0, ui.px(1.6f));
        dl->AddLine({at.x - r * 1.5f, at.y}, {at.x - r * 0.4f, at.y}, color, ui.px(1.6f));
        dl->AddLine({at.x + r * 0.4f, at.y}, {at.x + r * 1.5f, at.y}, color, ui.px(1.6f));
        dl->AddLine({at.x, at.y - r * 1.5f}, {at.x, at.y - r * 0.4f}, color, ui.px(1.6f));
        dl->AddLine({at.x, at.y + r * 0.4f}, {at.x, at.y + r * 1.5f}, color, ui.px(1.6f));
    }

    void click(UiContext& ui, TacticalFight& f, int piece, int x, int y) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const game::EmpireId side = b.phaseEmpire();
        const TacticalPiece* over = piece >= 0 && size_t(piece) < b.pieces().size() && b.pieces()[size_t(piece)].alive ? &b.pieces()[size_t(piece)] : nullptr;
        if (!side.valid()) {
            if (over) u.selected = piece;
            return;
        }
        if (u.aim != Aim::None) {
            const OK kind = u.aim == Aim::Ram ? OK::Ram : u.aim == Aim::Capture ? OK::Capture : OK::DropTroops;
            u.aim = Aim::None;
            if (over) {
                submit(f, TacticalOrder{kind, side, u.selected, piece});
                audio().play("button");
            }
            return;
        }
        if (over && commandable(*over, side)) {
            u.selected = piece;
            u.message.clear();
            return;
        }
        if (u.selected < 0) {
            if (over) u.selected = piece;
            return;
        }
        if (over && b.hostile(side, over->owner)) {
            u.target = piece;
            submit(f, TacticalOrder{OK::Fire, side, u.selected, piece});
            return;
        }
        if (!over) {
            TacticalOrder mv{OK::Move, side, u.selected};
            mv.x = x;
            mv.y = y;
            submit(f, mv);
            return;
        }
        u.selected = piece;   // someone else's piece: look at it
        (void)ui;
    }

    void resultPanel(UiContext& ui, TacticalFight& f, ImVec2 o, ImVec2 size) {
        TacticalBattle& b = *f.battle;
        const float w = ui.px(420), h = ui.px(230);
        ImGui::SetCursorScreenPos({o.x + (size.x - w) * 0.5f, o.y + (size.y - h) * 0.5f});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.03f, 0.08f, 0.93f));
        ImGui::BeginChild("##result", ImVec2(w, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        heading(ui, "The battle is over");
        ImGui::PushTextWrapPos(0.0f);
        // The outcome lines (the turn notes are in the replay).
        for (const std::string& line : b.record().summary)
            if (!line.starts_with("Turn ")) ImGui::TextUnformatted(line.c_str());
        if (f.kind == TacticalFight::Kind::Simulation) dimText("A simulation: nothing in the game has changed.");
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPosY(h - ui.px(40));
        if (classicButton(ui, "Done", {120, 26}, 0, false, b.applied())) done_ = true;
        ImGui::SameLine(0, ui.px(10));
        if (classicButton(ui, "Replay", {120, 26})) {
            // The whole battle again from the start.
            playback_.rewind();
            playback_.setSpeed(settings().tacticalSpeed);
            playback_.play();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ---- The current piece ----------------------------------------------------------------------------

    void currentPanel(UiContext& ui, TacticalFight& f) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const game::GameState& s = b.state();
        ImGui::BeginChild("##current", ui.size({kSideW, 300}), ImGuiChildFlags_Borders);
        if (u.selected < 0 || size_t(u.selected) >= b.pieces().size()) {
            dimText("Click one of your pieces on the map.");
            ImGui::EndChild();
            return;
        }
        const TacticalPiece& p = b.pieces()[size_t(u.selected)];
        const CombatMapPainter paint(ui, s, b.record(), playback_);
        // Portrait, flag, name, group badge.
        Sprite pic;
        if (p.design.valid() && p.design.index() < s.designs.size()) {
            const ruleset::VehicleSize& hull = ui.rules().hull(s.design(p.design).hull);
            pic = ui.art.shipPortrait(paint.styleOf(p.owner), hull);
            if (!pic) pic = ui.art.shipMini(paint.styleOf(p.owner), hull);
        } else if (p.planet.valid() && p.planet.index() < s.galaxy.objects.size()) {
            pic = objectSprite(ui, s.galaxy.object(p.planet));
        }
        image(ui, pic, {64, 64});
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (Sprite flag = ui.art.flag(paint.styleOf(p.owner), false)) {
            image(ui, flag, {20, 14});
            ImGui::SameLine();
        }
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTextSize));
        ImGui::TextUnformatted(paint.pieceName(uint32_t(u.selected)).c_str());
        ImGui::PopFont();
        if (p.design.valid() && p.design.index() < s.designs.size()) {
            const game::Design& d = s.design(p.design);
            dimText(p.kind == PieceKind::UnitGroup ? unitsText(s, p).c_str() : std::format("{} ({})", d.name, ui.rules().hull(d.hull).name).c_str());
        } else {
            dimText(p.kind == PieceKind::Planet ? "Planet" : p.kind == PieceKind::Seeker ? "Seeker" : "Obstacle");
        }
        if (p.isLeader) ImGui::TextColored(ImVec4(0.4f, 0.55f, 1, 1), "%s", p.group >= 0 ? std::format("Leader of group {}", p.group).c_str() : "Fleet leader");
        else if (p.leader >= 0 || p.group >= 0)
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", p.group >= 0 ? std::format("Member of group {}{}", p.group, p.leader < 0 ? " (no leader)" : "").c_str() : "In formation");
        if (!p.alive) ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Destroyed");
        ImGui::EndGroup();
        // Shields and damage bars.
        auto bar = [&](const char* label, float fraction, ImU32 color, const std::string& text) {
            ImGui::TextColored(kLabelBlue, "%s", label);
            ImGui::SameLine(ui.px(70));
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const ImVec2 sz = ui.size({kSideW - 90, 12});
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, {a.x + sz.x, a.y + sz.y}, IM_COL32(20, 25, 40, 255));
            dl->AddRectFilled(a, {a.x + sz.x * std::clamp(fraction, 0.0f, 1.0f), a.y + sz.y}, color);
            dl->AddRect(a, {a.x + sz.x, a.y + sz.y}, IM_COL32(70, 90, 150, 255));
            dl->AddText({a.x + ui.px(4), a.y - ui.px(1)}, IM_COL32_WHITE, text.c_str());
            ImGui::Dummy(sz);
        };
        if (p.kind != PieceKind::Obstacle && p.kind != PieceKind::Seeker) {
            bar("Shields", p.shieldsMax > 0 ? float(p.shields) / float(p.shieldsMax) : 0.0f, IM_COL32(60, 120, 255, 255),
                std::format("{} / {}", p.shields, p.shieldsMax));
            bar("Damage", float(p.damagePercent) / 100.0f, IM_COL32(220, 60, 50, 255), std::format("{}%", p.damagePercent));
            labelValue(ui, "Movement", std::format("{} of {}", p.movement, p.movementMax), 70);
            labelValue(ui, "Supply", p.kind == PieceKind::Planet || !p.hasSupply ? (p.hasSupply ? std::string("Not needed") : std::string("None left"))
                                                                                  : formatNumber(p.supply),
                       70);
            labelValue(ui, "Targets", std::format("{} of {} this turn", p.engaged, p.budget), 70);
        }
        // The weapon grid: click a weapon to switch it on or off; the pips are its reload.
        if (!p.weapons.empty()) {
            ImGui::Spacing();
            const bool ours = b.phaseEmpire().valid() && p.owner == b.phaseEmpire() && commandable(p, p.owner);
            const float cellW = 44;
            int col = 0;
            for (size_t w = 0; w < p.weapons.size(); ++w) {
                const TacticalWeapon& tw = p.weapons[w];
                if (col > 0) ImGui::SameLine(0, ui.px(2));
                ImGui::PushID(int(w));
                const ImVec2 a = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##weapon", ui.size({cellW, 46})) && ours) {
                    TacticalOrder t{OK::ToggleWeapon, p.owner, u.selected};
                    t.weapon = int(w);
                    t.on = !tw.enabled;
                    submit(f, t);
                }
                weaponCell(ui, b, p, tw, a, ImGui::IsItemHovered());
                if (ImGui::IsItemHovered()) weaponTooltip(ui, b, int(w), tw);
                ImGui::PopID();
                col = (col + 1) % 6;
            }
        }
        ImGui::EndChild();
    }

    void weaponCell(UiContext& ui, TacticalBattle& b, const TacticalPiece& p, const TacticalWeapon& tw, ImVec2 a, bool hovered) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 sz = ui.size({44, 46});
        const bool destroyed = tw.instances <= 0;
        dl->AddRectFilled(a, {a.x + sz.x, a.y + sz.y}, tw.enabled ? IM_COL32(10, 30, 70, 255) : IM_COL32(15, 15, 18, 255));
        const ruleset::Component& c = ui.rules().component(tw.component);
        if (Sprite icon = ui.art.component(c.picture))
            drawSprite(dl, icon, {a.x + ui.px(6), a.y + ui.px(2)}, {a.x + ui.px(38), a.y + ui.px(34)},
                       destroyed ? IM_COL32(255, 80, 80, 150) : tw.enabled ? IM_COL32_WHITE : IM_COL32(120, 120, 120, 200));
        // Reload pips: one per turn of the reload; lit while it counts down, all dark when ready.
        int counter = 0, ready = 0;
        for (int k = 0; k < tw.instances && k < int(tw.reload.size()); ++k) {
            if (tw.reload[size_t(k)] == 0) ++ready;
            counter = k == 0 ? tw.reload[size_t(k)] : std::min(counter, tw.reload[size_t(k)]);
        }
        const int pips = std::clamp(tw.reloadRate, 1, 5);
        for (int k = 0; k < pips; ++k) {
            const ImVec2 q{a.x + ui.px(4 + float(k) * 8), a.y + ui.px(37)};
            const bool lit = k < std::min(counter, pips);
            dl->AddRectFilled(q, {q.x + ui.px(6), q.y + ui.px(6)}, destroyed ? IM_COL32(90, 30, 30, 255)
                                                                  : lit       ? IM_COL32(230, 150, 40, 255)
                                                                              : IM_COL32(60, 200, 80, 255));
        }
        if (tw.instances > 1) dl->AddText({a.x + ui.px(28), a.y + ui.px(30)}, IM_COL32(220, 220, 220, 255), std::format("{}", ready).c_str());
        dl->AddRect(a, {a.x + sz.x, a.y + sz.y}, hovered ? IM_COL32(168, 188, 255, 255) : tw.enabled ? IM_COL32(97, 123, 194, 255) : IM_COL32(50, 50, 60, 255));
        (void)b;
        (void)p;
    }

    void weaponTooltip(UiContext& ui, TacticalBattle& b, int w, const TacticalWeapon& tw) {
        TacticalUi& u = state();
        const ruleset::Component& c = ui.rules().component(tw.component);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(c.name.c_str());
        dimText(std::format("{} · reload {} · reach {}{}", tw.kind == ruleset::WeaponKind::Seeking ? "Seeking" : tw.kind == ruleset::WeaponKind::PointDefense ? "Point-defense" : "Direct fire",
                            tw.reloadRate, tw.reach, tw.instances > 1 ? std::format(" · {} weapons", tw.instances) : std::string{})
                    .c_str());
        dimText(tw.enabled ? "On: fires with the piece. Click to switch off." : "Off. Click to switch on.");
        if (u.target >= 0 && size_t(u.target) < b.pieces().size() && b.pieces()[size_t(u.target)].alive) {
            const std::string why = b.fireProblem(u.selected, w, u.target);
            if (why.empty()) ImGui::Text("At the target: %d%% to hit, %d damage", b.hitChance(u.selected, w, u.target), b.damageAt(u.selected, w, u.target));
            else ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", why.c_str());
        }
        ImGui::EndTooltip();
    }

    // ---- The target piece -------------------------------------------------------------------------------

    void targetPanel(UiContext& ui, TacticalFight& f, const CombatMapPainter& paint) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        ImGui::BeginChild("##target", ui.size({kSideW, 112}), ImGuiChildFlags_Borders);
        if (u.target < 0 || size_t(u.target) >= b.pieces().size() || !b.pieces()[size_t(u.target)].alive) {
            dimText("Point at an enemy to see it here.");
            ImGui::EndChild();
            return;
        }
        const TacticalPiece& t = b.pieces()[size_t(u.target)];
        if (Sprite flag = ui.art.flag(paint.styleOf(t.owner), false)) {
            image(ui, flag, {20, 14});
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(paint.pieceName(uint32_t(u.target)).c_str());
        // Shields in blue, internal damage in red (spec 06 §1.6).
        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1, 1), "Shields %d", t.shields);
        ImGui::SameLine(ui.px(120));
        ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "Damage %d%%", t.damagePercent);
        if (u.selected >= 0 && size_t(u.selected) < b.pieces().size() && b.pieces()[size_t(u.selected)].alive) {
            const TacticalPiece& p = b.pieces()[size_t(u.selected)];
            labelValue(ui, "Distance", std::format("{} squares", b.distance(u.selected, u.target)), 70);
            int best = 0, total = 0;
            for (size_t w = 0; w < p.weapons.size(); ++w)
                if (p.weapons[w].enabled && b.fireProblem(u.selected, int(w), u.target).empty()) {
                    best = std::max(best, b.hitChance(u.selected, int(w), u.target));
                    total += b.damageAt(u.selected, int(w), u.target);
                }
            if (total > 0) labelValue(ui, "Your fire", std::format("{} damage, up to {}% to hit", total, best), 70);
            else dimText("No selected weapon can fire at it now.");
        }
        ImGui::EndChild();
    }

    // ---- Overview map and buttons -----------------------------------------------------------------------

    void overviewAndButtons(UiContext& ui, TacticalFight& f, const CombatMapPainter& paint) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        // The whole map with a dotted view box (spec 06 §1.6); a click moves the view there.
        const float w = 150, h = 150.0f * float(game::combat::kCombatMapHeight) / float(game::combat::kCombatMapWidth);
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##overview", ui.size({w, h}));
        const ImVec2 o2{o.x + ui.px(w), o.y + ui.px(h)};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(o, o2, IM_COL32(0, 0, 0, 255));
        const float k = ui.px(w) / float(game::combat::kCombatMapWidth);
        CombatView whole;   // square (x, y) at o + (x, y) × k
        whole.center = o;
        whole.cell = k;
        paint.squares(dl, whole, 2.0f);
        if (viewSize_.x > 0) {
            const float halfW = viewSize_.x * 0.5f / view_.cell, halfH = viewSize_.y * 0.5f / view_.cell;
            const ImVec2 a{o.x + (u.cx - halfW) * k, o.y + (u.cy - halfH) * k}, c{o.x + (u.cx + halfW) * k, o.y + (u.cy + halfH) * k};
            // Dotted box.
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
        if (ImGui::IsItemActive()) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            u.cx = (m.x - o.x) / k;
            u.cy = (m.y - o.y) / k;
        }
        // Options, Orders, Auto, End Turn.
        ImGui::SameLine(0, ui.px(8));
        ImGui::BeginGroup();
        const game::EmpireId side = b.phaseEmpire();
        const bool orders = side.valid() && !animating();
        if (classicButton(ui, "Options", {150, 28})) ui.open(ScreenId::TacticalOptions);
        if (classicButton(ui, "Orders", {150, 28}, 0, false, orders && !b.paused())) ui.open(ScreenId::TacticalOrders);
        // Auto: one toggle for every empire, from the next phase on (spec 04 §4).
        TacticalOrder toggle{OK::Auto, side};
        toggle.on = !b.autoOn();
        if (classicButton(ui, b.autoOn() ? "Auto: On" : "Auto: Off", {150, 28}, 0, false, orders)) submit(f, toggle);
        if (classicButton(ui, b.over() ? "End Battle" : "End Turn", {150, 28}, 0, false, orders)) submit(f, TacticalOrder{OK::EndPhase, side});
        ImGui::EndGroup();
        ImGui::Spacing();
        if (u.aim != Aim::None) {
            ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s", u.aim == Aim::Ram       ? "Click the enemy to ram."
                                                                 : u.aim == Aim::Capture ? "Click the enemy ship to board."
                                                                                         : "Click the enemy planet to invade.");
        }
    }

    // ---- Hotkeys (spec 06 §3.3) ------------------------------------------------------------------------

    void keys(UiContext& ui, TacticalFight& f) {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const ImGuiIO& io = ImGui::GetIO();
        // Scrolling the map.
        const float step = 2.0f;
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) u.cx -= step * 0.25f;
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) u.cx += step * 0.25f;
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) u.cy -= step * 0.25f;
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) u.cy += step * 0.25f;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            u.aim = Aim::None;
            u.message.clear();
        }
        if (animating()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) skipAnimation();
            return;
        }
        const game::EmpireId side = b.phaseEmpire();
        if (!side.valid()) return;
        // Alt+0..9 / Ctrl+0..9: leader / member of a group.
        for (int n = 0; n <= 9; ++n) {
            if (!ImGui::IsKeyPressed(ImGuiKey(int(ImGuiKey_0) + n), false) || u.selected < 0) continue;
            if (io.KeyAlt || io.KeyCtrl) {
                TacticalOrder o{io.KeyAlt ? OK::SetLeader : OK::SetMember, side, u.selected};
                o.group = n;
                if (io.KeyAlt) o.formation = ui.rules().data().formations.empty() ? -1 : std::clamp(u.formation, 0, int(ui.rules().data().formations.size()) - 1);
                submit(f, o);
            }
        }
        const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeyShift;
        if (plain && ImGui::IsKeyPressed(ImGuiKey_L, false)) ui.open(ScreenId::TacticalOrders);
        if (plain && ImGui::IsKeyPressed(ImGuiKey_T, false)) u.aim = Aim::DropTroops;
        if (plain && ImGui::IsKeyPressed(ImGuiKey_R, false)) u.aim = Aim::Ram;
        if (plain && ImGui::IsKeyPressed(ImGuiKey_C, false)) u.aim = Aim::Capture;
        if (plain && ImGui::IsKeyPressed(ImGuiKey_E, false)) submit(f, TacticalOrder{OK::EndPhase, side});
        auto pick = [&](int direction, auto test) {
            u.selected = cycle(b, u.selected, direction, test);
            centreOn(b, u.selected);
        };
        auto movable = [&](const TacticalPiece& p) { return canMove(p, side); };
        auto armed = [&](const TacticalPiece& p) { return canFire(p, side); };
        if ((plain && ImGui::IsKeyPressed(ImGuiKey_Space, false)) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false))) pick(1, movable);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) pick(-1, movable);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) pick(1, armed);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) pick(-1, armed);
        // Shift+A / Shift+C: every weapon of the piece on / off.
        if (io.KeyShift && u.selected >= 0 && (ImGui::IsKeyPressed(ImGuiKey_A, false) || ImGui::IsKeyPressed(ImGuiKey_C, false))) {
            TacticalOrder o{OK::ToggleWeapon, side, u.selected};
            o.on = ImGui::IsKeyPressed(ImGuiKey_A, false);
            submit(f, o);
        }
    }

    // ---- Piece report (right-click) ---------------------------------------------------------------------

    void reportPopup(UiContext& ui, TacticalBattle& b, const CombatMapPainter& paint) {
        if (!ImGui::BeginPopup("##piecereport")) return;
        if (report_ >= 0 && size_t(report_) < b.pieces().size()) {
            const TacticalPiece& p = b.pieces()[size_t(report_)];
            const game::GameState& s = b.state();
            ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTextSize));
            ImGui::TextUnformatted(paint.pieceName(uint32_t(report_)).c_str());
            ImGui::PopFont();
            labelValue(ui, "Owner", p.owner.valid() ? paint.empireName(p.owner) : std::string("None"), 90);
            if (p.design.valid() && p.design.index() < s.designs.size()) labelValue(ui, "Design", s.design(p.design).name, 90);
            if (p.kind == PieceKind::Seeker) {
                labelValue(ui, "Aimed at", p.seekTarget >= 0 ? paint.pieceName(uint32_t(p.seekTarget)) : std::string("-"), 90);
                labelValue(ui, "Speed", std::format("{} squares a turn", p.movementMax), 90);
                labelValue(ui, "Seekers", std::format("{}", p.count), 90);
            } else if (p.kind != PieceKind::Obstacle) {
                labelValue(ui, "Movement", std::format("{} of {}", p.movement, p.movementMax), 90);
                labelValue(ui, "Shields", std::format("{} of {}", p.shields, p.shieldsMax), 90);
                labelValue(ui, "Damage", std::format("{}%", p.damagePercent), 90);
                if (p.kind != PieceKind::Planet) labelValue(ui, "Supply", p.hasSupply ? formatNumber(p.supply) : std::string("None"), 90);
                labelValue(ui, "Max targets", std::format("{}", p.budget), 90);
                labelValue(ui, "Combat group", p.isLeader ? (p.group >= 0 ? std::format("Leads group {}", p.group) : std::string("Leads its fleet"))
                                                : p.leader >= 0 ? std::format("Follows {}", paint.pieceName(uint32_t(p.leader)))
                                                                : std::string("None"),
                           90);
                if (const game::Vehicle* v = p.vehicle.valid() ? s.vehicle(p.vehicle) : nullptr)
                    if (const game::Fleet* fleet = s.fleet(v->fleet); fleet && fleet->formation < ui.rules().data().formations.size())
                        labelValue(ui, "Formation", std::format("{} ({})", ui.rules().data().formations[fleet->formation].name, fleet->name), 90);
                if (p.kind == PieceKind::UnitGroup) labelValue(ui, "Units", unitsText(s, p), 90);
                if (!p.cargo.empty()) {
                    std::string cargo;
                    for (const game::UnitStack& st : p.cargo)
                        cargo += std::format("{}{} x{}", cargo.empty() ? "" : ", ", st.design.index() < s.designs.size() ? s.design(st.design).name : "?", st.count);
                    labelValue(ui, "Cargo", cargo, 90);
                }
                if (!p.landed.empty()) {
                    // Troops landed by another empire, fighting on the ground (spec 04 §13).
                    int troops = 0;
                    for (const game::UnitStack& st : p.landed) troops += st.count;
                    labelValue(ui, "Invaders", std::format("{} troops of the {}", troops, paint.empireName(p.invader)), 90);
                }
            }
            labelValue(ui, "Square", std::format("{}, {}", p.x, p.y), 90);
        }
        dimText("Click outside to close.");
        ImGui::EndPopup();
    }

    CombatPlayback playback_;
    const game::CombatRecord* record_ = nullptr;
    size_t events_ = 0, pieces_ = 0;
    CombatView view_;
    ImVec2 viewSize_{0, 0};
    int report_ = -1;
    bool done_ = false;
    size_t groundsSeen_ = 0;   // ground combats of the record already shown
};

// ---- Tactical Combat Orders ---------------------------------------------------------------------------------

class TacticalOrdersScreen final : public Screen {
public:
    TacticalOrdersScreen() { window_ = ++state().launchWindow; }   // each opening is a new Launch Units session
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        TacticalFight* f = ui.session.tactical();
        if (!f || !f->battle) return false;
        TacticalBattle& b = *f->battle;
        TacticalUi& u = stateFor(f);
        const game::EmpireId side = b.phaseEmpire();
        Dialog d(ui, "Tactical Combat Orders", DialogSize::Picker);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        const game::GameState& s = b.state();
        const TacticalPiece* p = u.selected >= 0 && size_t(u.selected) < b.pieces().size() ? &b.pieces()[size_t(u.selected)] : nullptr;
        const bool ours = p && side.valid() && commandable(*p, side) && !b.paused();
        heading(ui, ours ? std::format("Orders for {}", p->name).c_str() : "Select one of your pieces first");

        // Launch Units (spec 04 §10.4): 1, 5, 10 or all units of a stack at a time.
        // While this window stays open, units of one kind launched from the piece
        // join the group made first in it, whatever their design; drones are one
        // per group. Drones you launch first act at your next phase.
        ImGui::Spacing();
        heading(ui, "Launch Units");
        const std::array<const char*, 3> kinds{"fighters", "satellites", "drones"};
        if (ours) {
            wrappedDim(std::format("It may launch {} fighters, {} satellites and {} drones more this turn.", p->launchLeft[0], p->launchLeft[1],
                                   p->launchLeft[2]));
            bool any = false;
            for (const game::UnitStack& st : p->cargo) {
                if (st.design.index() >= s.designs.size()) continue;
                const ruleset::VehicleType t = ui.rules().hull(s.design(st.design).hull).type;
                if (t != ruleset::VehicleType::Fighter && t != ruleset::VehicleType::Satellite && t != ruleset::VehicleType::Drone) continue;
                any = true;
                ImGui::PushID(int(st.design.value));
                ImGui::TextUnformatted(std::format("{} x{}", s.design(st.design).name, st.count).c_str());
                ImGui::SameLine(ui.px(230));
                for (const int n : {1, 5, 10, 0}) {
                    TacticalOrder o{OK::Launch, side, u.selected};
                    o.design = st.design;
                    o.count = n > 0 ? n : st.count;
                    o.group = window_;
                    ImGui::PushID(n);
                    if (classicButton(ui, n > 0 ? std::format("{}", n).c_str() : "All", {40, 22}, 0, false, b.check(o).empty())) submit(*f, o);
                    ImGui::PopID();
                    ImGui::SameLine(0, ui.px(3));
                }
                ImGui::NewLine();
                ImGui::PopID();
            }
            if (!any) wrappedDim(std::format("It carries no {}, {} or {}.", kinds[0], kinds[1], kinds[2]));
        }
        // Launch Fighters in Groups: fighters only, single-design groups of 5 to 50.
        ImGui::Spacing();
        heading(ui, "Launch Fighters in Groups");
        ImGui::SetNextItemWidth(ui.px(80));
        if (ImGui::BeginCombo("fighters a group", std::format("{}", u.groupSize).c_str())) {
            for (int size : game::combat::kFighterGroupSizes)
                if (ImGui::Selectable(std::format("{}", size).c_str(), size == u.groupSize)) u.groupSize = size;
            ImGui::EndCombo();
        }
        if (ours)
            for (const game::UnitStack& st : p->cargo) {
                if (st.design.index() >= s.designs.size() || ui.rules().hull(s.design(st.design).hull).type != ruleset::VehicleType::Fighter) continue;
                TacticalOrder o{OK::LaunchFighters, side, u.selected};
                o.design = st.design;
                o.count = st.count;
                o.group = u.groupSize;
                ImGui::PushID(int(st.design.value) + 100000);
                if (classicButton(ui, std::format("Launch {} in groups", s.design(st.design).name).c_str(), {230, 22}, 0, false, b.check(o).empty()))
                    submit(*f, o);
                ImGui::PopID();
            }
        // Combat groups (spec 04 §5): a leader picks the formation its members take places in.
        ImGui::Spacing();
        heading(ui, "Combat Groups");
        ImGui::SetNextItemWidth(ui.px(120));
        ImGui::SliderInt("group number", &u.groupNumber, 0, 9);
        const auto& formations = ui.rules().data().formations;
        if (!formations.empty()) {
            u.formation = std::clamp(u.formation, 0, int(formations.size()) - 1);
            ImGui::SetNextItemWidth(ui.px(200));
            if (ImGui::BeginCombo("formation", formations[size_t(u.formation)].name.c_str())) {
                for (size_t k = 0; k < formations.size(); ++k)
                    if (ImGui::Selectable(formations[k].name.c_str(), int(k) == u.formation)) u.formation = int(k);
                ImGui::EndCombo();
            }
        }
        wrappedDim("Members take the next place of their leader's formation and follow it when it moves. On the map, Alt+number makes the "
                   "selected piece a leader, Ctrl+number a member.");
        if (!u.message.empty()) ImGui::TextColored(ImVec4(1, 0.72f, 0.45f, 1), "%s", u.message.c_str());

        d.beginButtons();
        const bool can = ours && b.awaitingOrders();
        if (d.button("Drop Troops", can && p->troops)) aim(u, Aim::DropTroops, d);
        if (d.button("Ram Ship", can && p->movement > 0)) aim(u, Aim::Ram, d);
        if (d.button("Capture Ship", can && p->boardingAttack > 0)) aim(u, Aim::Capture, d);
        d.spacer();
        TacticalOrder leader{OK::SetLeader, side, u.selected};
        leader.group = u.groupNumber;
        leader.formation = formations.empty() ? -1 : u.formation;
        TacticalOrder member{OK::SetMember, side, u.selected};
        member.group = u.groupNumber;
        if (d.button(std::format("Group {} Leader", u.groupNumber).c_str(), can && b.check(leader).empty())) submit(*f, leader);
        if (d.button(std::format("Group {} Member", u.groupNumber).c_str(), can && b.check(member).empty())) submit(*f, member);
        if (d.button("Clear Group", can && b.check(TacticalOrder{OK::ClearGroup, side, u.selected}).empty()))
            submit(*f, TacticalOrder{OK::ClearGroup, side, u.selected});
        if (d.button("Clear All Groups", side.valid() && b.awaitingOrders() && !b.paused())) submit(*f, TacticalOrder{OK::ClearAllGroups, side});
        d.spacer();
        // An OpenSE4 extension: the strategies play the rest of this phase.
        if (d.button("Auto This Phase", side.valid() && b.awaitingOrders() && !b.paused())) {
            submit(*f, TacticalOrder{OK::AutoPhase, side});
            d.requestClose();
        }
        if (d.button("Resolve Combat", side.valid() && b.awaitingOrders())) ImGui::OpenPopup("##resolve");
        if (ImGui::BeginPopup("##resolve")) {
            // Every empire follows its strategies until the battle ends (spec 04 §4).
            ImGui::TextUnformatted("Let every empire's strategies fight the rest of the battle?");
            if (classicButton(ui, "Resolve", {100, 24})) {
                submit(*f, TacticalOrder{OK::ResolveCombat, side});
                ImGui::CloseCurrentPopup();
                d.requestClose();
            }
            ImGui::SameLine();
            if (classicButton(ui, "Cancel", {100, 24})) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        d.close();
        flush(*f);
        return d.keepOpen();
    }

private:
    static void aim(TacticalUi& u, Aim a, Dialog& d) {
        u.aim = a;
        d.requestClose();
    }

    int window_ = 0;
};

// ---- Tactical Combat Options ----------------------------------------------------------------------------------

class TacticalOptionsScreen final : public Screen {
public:
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        Dialog d(ui, "Tactical Combat Options", DialogSize::Picker);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        ClassicSettings& prefs = settings();
        bool changed = false;
        heading(ui, "Display");
        changed |= lampToggle(ui, "Animate moves and shots", &prefs.tacticalAnimate);
        changed |= lampToggle(ui, "Show the square grid", &prefs.tacticalGrid);
        changed |= lampToggle(ui, "Show the selected piece's reach", &prefs.tacticalRanges);
        changed |= lampToggle(ui, "Show piece names", &prefs.tacticalNames);
        heading(ui, "Turns");
        changed |= lampToggle(ui, "End my phase when no enemy is left", &prefs.tacticalAutoEnd);
        heading(ui, "Animation speed");
        for (float sp : kSpeeds) {
            bool on = prefs.tacticalSpeed == sp;
            if (lampToggle(ui, sp < 1.0f ? "Half speed" : std::format("{}x", int(sp)).c_str(), &on) && on) {
                prefs.tacticalSpeed = sp;
                changed = true;
            }
        }
        if (changed) saveSettings();
        d.beginButtons();
        d.close();
        return d.keepOpen();
    }
};

} // namespace

std::unique_ptr<Screen> makeTacticalCombat(const ScreenArgs&) { return std::make_unique<TacticalCombatScreen>(); }
std::unique_ptr<Screen> makeTacticalOrders(const ScreenArgs&) { return std::make_unique<TacticalOrdersScreen>(); }
std::unique_ptr<Screen> makeTacticalOptions(const ScreenArgs&) { return std::make_unique<TacticalOptionsScreen>(); }

} // namespace opense4::client::classic
