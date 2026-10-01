// Tactical Combat and the windows opened from it: Tactical Combat Orders,
// Launch Units, Combat Options and the Combat Piece Report (docs/spec/06
// §1.10.1-§1.10.3, §3.3; the rules are docs/spec/04 §4-§12): the player's
// side of the combat::TacticalBattle the session holds (ClassicSession::
// tactical()): a turn-based game's battle or a combat simulation.
//
// The map is the Combat Replay's (combat_map.hpp): the battle's record plays
// back as it grows, so every move and shot, the computer's too, is seen.
// The window opens with Begin, Orders dim; Begin starts the battle and
// becomes End Turn. Left-click selects an own piece, moves it to an empty
// square or fires its enabled weapons at an enemy; right-click opens the
// Combat Piece Report. When the battle is over a "Combat Complete" message
// shows and the window closes. Hotkeys follow spec 06 §3.3; Esc does nothing
// here but cancel a Ram or Capture target.

#include "client/audio.hpp"
#include "client/classic/replay.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/combat_logic.hpp"
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

// Animation pace: Fast Tactical Combat drops the pauses between steps (spec 06
// §1.10.3); our playback runs faster instead (inferred).
float animationSpeed() { return settings().fastTacticalCombat ? 6.0f : 2.0f; }

// A target picked on the map for Ram or Capture (the Orders window, R or C).
enum class Aim { None, Ram, Capture };

// The pickers the Orders window's items open (spec 06 §1.10.2).
enum class OrdersMode { Menu, GroupSize, LeaderNumber, LeaderFormation, MemberNumber, Resolve };

// What the windows share for the battle in progress.
struct TacticalUi {
    const TacticalFight* fight = nullptr;
    bool begun = false;          // Begin was pressed (spec 06 §1.10.1)
    int selected = -1;
    int target = -1;             // the enemy last hovered or fired at
    bool hoverEnemy = false;     // the pointer is over an enemy now (to-hit chances)
    Aim aim = Aim::None;
    OrdersMode ordersMode = OrdersMode::Menu;
    int ordersGroup = 1;         // the group number picked for Set Group Leader
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
    bool stop = false;           // Stop Combat (simulator only)
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

// The selected piece when it is one the player may give orders to now.
const TacticalPiece* ownSelected(const TacticalBattle& b, const TacticalUi& u) {
    const game::EmpireId side = b.phaseEmpire();
    if (!side.valid() || !b.awaitingOrders() || b.paused() || u.selected < 0 || size_t(u.selected) >= b.pieces().size()) return nullptr;
    const TacticalPiece& p = b.pieces()[size_t(u.selected)];
    return commandable(p, side) ? &p : nullptr;
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

// Drop Troops (the Orders item and T): at once on the adjacent colony of another empire (spec 06 §1.10.2).
void dropTroops(TacticalFight& f) {
    TacticalUi& u = state();
    const TacticalBattle& b = *f.battle;
    if (!ownSelected(b, u)) {
        u.message = "Select one of your ships first.";
        return;
    }
    const DropTarget t = dropTroopsTarget(b, u.selected);
    if (t.planet < 0) {
        u.message = t.problem;
        return;
    }
    submit(f, TacticalOrder{OK::DropTroops, b.phaseEmpire(), u.selected, t.planet});
}

// Alt/Ctrl+0 and Clear Group Assignment: the selected piece's group marks go (spec 06 §3.3).
void clearGroup(TacticalFight& f) {
    TacticalUi& u = state();
    if (ownSelected(*f.battle, u)) submit(f, TacticalOrder{OK::ClearGroup, f.battle->phaseEmpire(), u.selected});
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
        if (u.stop) {
            // Stop Combat (simulator only): the battle ends and the window closes.
            u.stop = false;
            return finishWindow(ui, *f);
        }
        sync(ui, b);
        if (u.begun) automatic(ui, *f);
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
        flush(*f);
        // Troops landed: the Ground Combat window at once, once the landing has
        // played, unless both empires are computer-controlled (the simulator
        // always shows it) (spec 06 §1.10.6).
        const std::vector<game::GroundCombat>& grounds = b.record().grounds;
        if (grounds.size() > groundsSeen_ && !animating()) {
            const bool simulation = f->kind == TacticalFight::Kind::Simulation;
            auto computer = [&](game::EmpireId e) { return !b.isPlayer(e) && (!e.valid() || e.index() >= s.empires.size() || s.empire(e).kind != game::PlayerKind::Human); };
            for (size_t k = groundsSeen_; k < grounds.size(); ++k)
                if (simulation || !(computer(grounds[k].attacker) && computer(grounds[k].defender))) {
                    ScreenArgs a;
                    a.sub = int(k);
                    ui.open(ScreenId::GroundCombat, std::move(a));
                }
            groundsSeen_ = grounds.size();
        }
        // The end: "Combat Complete", then the window closes by itself.
        if (b.finished() && b.applied() && !animating() && completePrompt(ui)) return finishWindow(ui, *f);
        return true;
    }

private:
    // Closes the window: a game battle's orders answer its question and the game
    // carries on; after a simulation the Combat Simulator opens again with the
    // same setup (spec 06 §1.10.4).
    bool finishWindow(UiContext& ui, TacticalFight& f) {
        const bool simulation = f.kind == TacticalFight::Kind::Simulation;
        ui.session.endTactical();
        if (simulation) {
            ScreenArgs a;
            a.text = "again";
            ui.open(ScreenId::CombatSimulator, std::move(a));
        }
        return false;
    }

    // A message box; OK, Esc or Enter close it.
    bool completePrompt(UiContext& ui) {
        constexpr const char* kId = "Combat Complete##tactical";
        if (!ImGui::IsPopupOpen(kId)) ImGui::OpenPopup(kId);
        ImGui::SetNextWindowPos(ui.at({kFrameW * 0.5f, kFrameH * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size({260, 110}), ImGuiCond_Always);
        bool done = false;
        if (ImGui::BeginPopupModal(kId, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | kPromptFlags)) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Combat Complete");
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ui.px(36));
            if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) {
                done = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return done;
    }

    // ---- The record as it grows ------------------------------------------------------------------

    void sync(UiContext& ui, TacticalBattle& b) {
        const game::CombatRecord& rec = b.record();
        if (&rec != record_ || rec.events.size() != events_ || rec.pieces.size() != pieces_) {
            const size_t cursor = record_ ? playback_.cursor() : std::min(ui.session.tactical()->seen, rec.events.size());
            playback_ = CombatPlayback(rec);
            playback_.setSpeed(animationSpeed());
            playback_.seekEvent(std::min(cursor, playback_.eventCount()));
            // Before Begin the map stays as the battle starts.
            if (state().begun) {
                if (settings().animateCombatMovement && !playback_.atEnd()) playback_.play();
                else playback_.seekEvent(playback_.eventCount());
            }
            record_ = &rec;
            events_ = rec.events.size();
            pieces_ = rec.pieces.size();
        }
        if (!state().begun) return;
        if (!playback_.playing() && !playback_.atEnd()) {
            if (settings().animateCombatMovement) playback_.play();
            else playback_.seekEvent(playback_.eventCount());
        }
        playback_.setSpeed(animationSpeed());
        const size_t before = playback_.cursor();
        playback_.advance(ui.dt);
        CombatMapPainter(ui, b.state(), rec, playback_).sounds(before, playback_.cursor());
    }

    bool animating() const { return playback_.playing() && !playback_.atEnd(); }
    void skipAnimation() { playback_.seekEvent(playback_.eventCount()); }

    // The steps nobody needs to click: a phase where the side has nothing left
    // that takes orders ends. The side's drones and seekers have moved before
    // the player gets control (spec 04 §4).
    void automatic(UiContext& ui, TacticalFight& f) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        // Once the last phase has played out, the results are worked out.
        if (b.finished() && !b.applied() && !animating()) u.finishNow = true;
        if (!b.awaitingOrders() || animating()) return;
        const game::EmpireId side = b.phaseEmpire();
        if (b.paused()) return;
        bool any = false;
        for (const TacticalPiece& p : b.pieces()) any = any || commandable(p, side);
        if (!any) {
            submit(f, TacticalOrder{OK::EndPhase, side});
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

    // Center Map on Current Ship (spec 06 §1.10.3): the view follows the selection when it is on.
    void centreOn(const TacticalBattle& b, int piece, bool always = false) {
        if (piece < 0 || size_t(piece) >= b.pieces().size()) return;
        if (!always && !settings().centerOnCurrentShip) return;
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
        const std::string turn = !u.begun     ? std::string("Press Begin to start the battle")
                                 : b.finished() ? std::string("The battle is over")
                                                : std::format("Combat turn {} of {}", std::min(b.round(), b.lastRound()), b.lastRound());
        ImGui::SameLine(ui.px(kMapW) - ImGui::CalcTextSize(turn.c_str()).x);
        ImGui::TextColored(kLabelBlue, "%s", turn.c_str());
        ImGui::SameLine(ui.px(kSideX));
        if (u.begun && b.awaitingOrders()) {
            const std::string who = paint.empireName(b.phaseEmpire());
            const std::string what = animating() ? "wait for orders" : b.paused() ? "paused (Auto)" : "give orders";
            ImGui::TextColored(ImVec4(0.55f, 1, 0.55f, 1), "%s", std::format("{}: {}", who, what).c_str());
        } else {
            dimText(!u.begun ? "" : b.finished() ? "Combat complete" : "Watching");
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
            dimText(std::format("({} left){}", left, b.phaseEmpire() == e ? " *" : "").c_str());
        }
        // Next / previous selectors: pieces that can move, pieces that can fire.
        const game::EmpireId side = b.phaseEmpire();
        if (side.valid() && u.begun) {
            ImGui::SameLine(ui.px(kSideX));
            selector(ui, b, "Move", [&](const TacticalPiece& p) { return canMove(p, side); });
            ImGui::SameLine(0, ui.px(10));
            selector(ui, b, "Fire", [&](const TacticalPiece& p) { return canFire(p, side); });
        }
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
        // Show Grid (spec 06 §1.10.3, off on a fresh install).
        paint.background(dl, o, o2, v, settings().tacticalGrid ? IM_COL32(40, 70, 140, 60) : 0);
        // The map's edges.
        const ImVec2 m0 = v.at(0, 0), m1 = v.at(float(game::combat::kCombatMapWidth), float(game::combat::kCombatMapHeight));
        dl->AddRect(m0, m1, IM_COL32(90, 120, 200, 200), 0.0f, ui.px(1.5f));

        const TacticalPiece* sel = u.selected >= 0 && size_t(u.selected) < b.pieces().size() ? &b.pieces()[size_t(u.selected)] : nullptr;
        if (sel && !sel->alive) sel = nullptr;
        const std::optional<uint32_t> hovered = paint.pieces(dl, v, hoveredBox, io.MousePos);
        const auto [hx, hy] = v.square(io.MousePos);

        // The selection, and the group identifiers when they are on.
        for (size_t i = 0; i < b.pieces().size() && i < playback_.pieces().size(); ++i) {
            const TacticalPiece& p = b.pieces()[i];
            if (!p.alive || !playback_.pieces()[i].onMap) continue;
            const ImVec2 c = paint.piecePos(v, uint32_t(i));
            const float h = v.cell * float(p.size) * 0.5f;
            if (int(i) == u.selected) dl->AddRect({c.x - h - 1, c.y - h - 1}, {c.x + h + 1, c.y + h + 1}, IM_COL32(255, 230, 80, 255), 0.0f, ui.px(2));
            if (settings().showGroupIdentifiers && (p.isLeader || p.leader >= 0 || p.group >= 0) && b.isPlayer(p.owner)) {
                const ImU32 badge = p.isLeader ? IM_COL32(70, 120, 255, 255) : IM_COL32(230, 60, 60, 255);
                const float r = std::max(3.0f, v.cell * 0.18f);
                dl->AddRectFilled({c.x + h - 2 * r, c.y - h}, {c.x + h, c.y - h + 2 * r}, badge);
                if (p.group >= 0 && v.cell >= 14) dl->AddText({c.x + h - 2 * r + 1, c.y - h - 2}, IM_COL32_WHITE, std::format("{}", p.group).c_str());
            }
        }

        // What a click would do, shown by the pointer.
        const bool ours = u.begun && sel && side.valid() && sel->owner == side && b.awaitingOrders();
        const TacticalPiece* over = hovered && *hovered < b.pieces().size() ? &b.pieces()[*hovered] : nullptr;
        if (over && !over->alive) over = nullptr;
        const bool enemy = over && side.valid() && b.hostile(side, over->owner) && over->kind != PieceKind::Obstacle;
        u.hoverEnemy = enemy;
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

        // Clicks.
        if (hoveredBox && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && u.begun) {
            if (animating()) {
                skipAnimation();   // a click while the battle plays skips to its end
            } else {
                click(ui, f, hovered ? int(*hovered) : -1, hx, hy);
            }
        }
        // Right-click: the Combat Piece Report (spec 06 §1.10.1).
        if (hoveredBox && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered) openReport(ui, int(*hovered));
    }

    static void openReport(UiContext& ui, int piece) {
        ScreenArgs a;
        a.index = piece;
        ui.open(ScreenId::CombatPieceReport, std::move(a));
    }

    void pointerArrow(UiContext& ui, ImDrawList* dl, ImVec2 at, int facing, ImU32 color) {
        constexpr std::array<std::pair<int, int>, 8> kFacing{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {-1, -1}, {1, 1}, {-1, 1}}};
        const float a = angleOf(kFacing[size_t(facing)].first, kFacing[size_t(facing)].second);
        const float r = ui.px(11);
        auto rot = [&](float x, float y) { return ImVec2{at.x + x * std::cos(a) - y * std::sin(a), at.y + x * std::sin(a) + y * std::cos(a)}; };
        dl->AddTriangleFilled(rot(0, -r), rot(-r * 0.7f, 0), rot(r * 0.7f, 0), color);
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
            // Ram or Capture: a click on a piece aims it, one on an empty square cancels.
            const OK kind = u.aim == Aim::Ram ? OK::Ram : OK::Capture;
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
        // Portrait, flag, name; clicking this header opens the piece's report (spec 06 §1.10.1).
        const ImVec2 header = ImGui::GetCursorScreenPos();
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
        const ImVec2 headerEnd{header.x + ui.px(kSideW), ImGui::GetItemRectMax().y};
        if (ImGui::IsMouseHoveringRect(header, headerEnd) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
            openReport(ui, u.selected);
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
        // The weapon list: click a weapon to switch it on or off; the pips are its
        // reload. With Show Weapon To Hit Chances on, hovering an enemy shows each
        // weapon's chance here (spec 06 §1.10.3).
        if (!p.weapons.empty()) {
            ImGui::Spacing();
            const bool ours = u.begun && b.phaseEmpire().valid() && p.owner == b.phaseEmpire() && commandable(p, p.owner);
            const int chancesAt = settings().showToHitChances && u.hoverEnemy && ours ? u.target : -1;
            int col = 0;
            for (size_t w = 0; w < p.weapons.size(); ++w) {
                const TacticalWeapon& tw = p.weapons[w];
                if (col > 0) ImGui::SameLine(0, ui.px(2));
                ImGui::PushID(int(w));
                const ImVec2 a = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##weapon", ui.size({44, 46})) && ours) {
                    TacticalOrder t{OK::ToggleWeapon, p.owner, u.selected};
                    t.weapon = int(w);
                    t.on = !tw.enabled;
                    submit(f, t);
                }
                weaponCell(ui, tw, a, ImGui::IsItemHovered());
                if (chancesAt >= 0 && b.fireProblem(u.selected, int(w), chancesAt).empty()) {
                    const std::string chance = std::format("{}%", b.hitChance(u.selected, int(w), chancesAt));
                    ImGui::GetWindowDrawList()->AddText({a.x + ui.px(3), a.y + ui.px(1)}, IM_COL32(255, 255, 0, 255), chance.c_str());
                }
                if (ImGui::IsItemHovered()) weaponTooltip(ui, b, int(w), tw);
                ImGui::PopID();
                col = (col + 1) % 6;
            }
        }
        ImGui::EndChild();
    }

    void weaponCell(UiContext& ui, const TacticalWeapon& tw, ImVec2 a, bool hovered) {
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
        // Shields in blue, internal damage in red.
        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1, 1), "Shields %d", t.shields);
        ImGui::SameLine(ui.px(120));
        ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "Damage %d%%", t.damagePercent);
        if (u.selected >= 0 && size_t(u.selected) < b.pieces().size() && b.pieces()[size_t(u.selected)].alive)
            labelValue(ui, "Distance", std::format("{} squares", b.distance(u.selected, u.target)), 70);
        ImGui::EndChild();
    }

    // ---- Overview map and buttons -----------------------------------------------------------------------

    void overviewAndButtons(UiContext& ui, TacticalFight& f, const CombatMapPainter& paint) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        // The whole map, with the dotted Viewing Rectangle when it is on; a click moves the view there.
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
        if (viewSize_.x > 0 && settings().showViewingRectangle) {
            const float halfW = viewSize_.x * 0.5f / view_.cell, halfH = viewSize_.y * 0.5f / view_.cell;
            const ImVec2 a{o.x + (u.cx - halfW) * k, o.y + (u.cy - halfH) * k}, c{o.x + (u.cx + halfW) * k, o.y + (u.cy + halfH) * k};
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
        // Options and Orders, then Auto and Begin / End Turn (spec 06 §1.10.1).
        ImGui::SameLine(0, ui.px(8));
        ImGui::BeginGroup();
        const game::EmpireId side = b.phaseEmpire();
        const bool orders = u.begun && side.valid() && !animating();
        if (classicButton(ui, "Options", {150, 28})) ui.open(ScreenId::TacticalOptions);
        if (classicButton(ui, "Orders", {150, 28}, 0, false, orders && !b.paused())) {
            u.ordersMode = OrdersMode::Menu;
            ui.open(ScreenId::TacticalOrders);
        }
        // Auto: one toggle for every empire, from the next phase on (spec 04 §4).
        TacticalOrder toggle{OK::Auto, side};
        toggle.on = !b.autoOn();
        if (classicButton(ui, b.autoOn() ? "Auto: On" : "Auto: Off", {150, 28}, 0, false, orders)) submit(f, toggle);
        if (!u.begun) {
            if (classicButton(ui, "Begin", {150, 28})) begin(b);
        } else if (classicButton(ui, "End Turn", {150, 28}, 0, false, orders)) {
            submit(f, TacticalOrder{OK::EndPhase, side});
        }
        ImGui::EndGroup();
        ImGui::Spacing();
        if (u.aim != Aim::None) ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s", u.aim == Aim::Ram ? "Click the enemy to ram." : "Click the enemy ship to board.");
    }

    void begin(const TacticalBattle& b) {
        TacticalUi& u = state();
        u.begun = true;
        if (settings().animateCombatMovement && !playback_.atEnd()) playback_.play();
        else skipAnimation();
        centreOn(b, u.selected);
    }

    // ---- Hotkeys (spec 06 §3.3) ------------------------------------------------------------------------

    void keys(UiContext& ui, TacticalFight& f) {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const ImGuiIO& io = ImGui::GetIO();
        // Scrolling the map.
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) u.cx -= 0.5f;
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) u.cx += 0.5f;
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) u.cy -= 0.5f;
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) u.cy += 0.5f;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) u.aim = Aim::None;   // Esc closes nothing here
        if (!u.begun) return;
        if (animating()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) skipAnimation();
            return;
        }
        const game::EmpireId side = b.phaseEmpire();
        if (!side.valid()) return;
        // Alt+1..9 / Ctrl+1..9: leader / member of a group; Alt or Ctrl+0 clears the marks.
        for (int n = 0; n <= 9; ++n) {
            if (!ImGui::IsKeyPressed(ImGuiKey(int(ImGuiKey_0) + n), false) || !(io.KeyAlt || io.KeyCtrl)) continue;
            if (!ownSelected(b, u)) continue;
            if (n == 0) {
                clearGroup(f);
            } else if (io.KeyAlt) {
                // A new leader then picks its formation.
                u.ordersGroup = n;
                u.ordersMode = OrdersMode::LeaderFormation;
                ui.open(ScreenId::TacticalOrders);
            } else {
                TacticalOrder o{OK::SetMember, side, u.selected};
                o.group = n;
                submit(f, o);
            }
        }
        const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeyShift;
        if (plain && ImGui::IsKeyPressed(ImGuiKey_L, false) && ownSelected(b, u)) ui.open(ScreenId::TacticalLaunch);
        if (plain && ImGui::IsKeyPressed(ImGuiKey_T, false)) dropTroops(f);
        if (plain && ImGui::IsKeyPressed(ImGuiKey_R, false) && ownSelected(b, u)) u.aim = Aim::Ram;
        if (plain && ImGui::IsKeyPressed(ImGuiKey_C, false) && ownSelected(b, u)) u.aim = Aim::Capture;
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

    CombatPlayback playback_;
    const game::CombatRecord* record_ = nullptr;
    size_t events_ = 0, pieces_ = 0;
    CombatView view_;
    ImVec2 viewSize_{0, 0};
    size_t groundsSeen_ = 0;   // ground combats of the record already shown
};

// ---- Tactical Combat Orders (spec 06 §1.10.2) ------------------------------------------------------------------

// A 326x350 menu of 11 stacked 306x30 buttons, without a title; the items that
// need a choice open their pickers in its place.
class TacticalOrdersScreen final : public Screen {
public:
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        TacticalFight* f = ui.session.tactical();
        if (!f || !f->battle) return false;
        TacticalBattle& b = *f->battle;
        TacticalUi& u = stateFor(f);
        const game::EmpireId side = b.phaseEmpire();
        const TacticalPiece* p = ownSelected(b, u);
        const Vec2 size{326, 350};
        const Vec2 min{(kFrameW - size.x) * 0.5f, (kFrameH - size.y) * 0.5f};
        ImGui::SetNextWindowPos(ui.at(min), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        bool keep = true;
        const bool open = ImGui::Begin("Tactical Combat Orders", nullptr,
                                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | kPromptFlags);
        ImGui::PopStyleVar(2);
        if (open) {
            if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
            drawWindowFrame(ui.painter(), ImGui::GetWindowDrawList(), Rect{min, min + size}, nullptr, 0);
            int row = 0;
            auto button = [&](const std::string& label, bool enabled = true) {
                ImGui::SetCursorPos(ImVec2(ui.px(10), ui.px(10 + 30 * float(row++))));
                const bool clicked = classicButton(ui, label.c_str(), {306, 30}, 0, false, enabled);
                if (clicked) audio().play("button");
                return clicked;
            };
            switch (u.ordersMode) {
                case OrdersMode::Menu: keep = menu(ui, *f, p, button); break;
                case OrdersMode::GroupSize:
                    for (int n : game::combat::kFighterGroupSizes)
                        if (button(std::format("Groups of {}", n))) {
                            launchInGroups(ui, *f, n);
                            keep = false;
                        }
                    break;
                case OrdersMode::LeaderNumber:
                case OrdersMode::MemberNumber:
                    for (int n = 1; n <= 9; ++n)
                        if (button(std::format("Group {}", n), p != nullptr)) {
                            if (u.ordersMode == OrdersMode::LeaderNumber) {
                                u.ordersGroup = n;
                                u.ordersMode = OrdersMode::LeaderFormation;
                            } else {
                                TacticalOrder o{OK::SetMember, side, u.selected};
                                o.group = n;
                                submit(*f, o);
                                keep = false;
                            }
                        }
                    break;
                case OrdersMode::LeaderFormation: {
                    const auto& formations = ui.rules().data().formations;
                    for (size_t k = 0; k < formations.size() && k < 10; ++k)
                        if (button(formations[k].name, p != nullptr)) {
                            TacticalOrder o{OK::SetLeader, side, u.selected};
                            o.group = u.ordersGroup;
                            o.formation = int(k);
                            submit(*f, o);
                            keep = false;
                        }
                    if (formations.empty() && button(std::format("Lead group {}", u.ordersGroup), p != nullptr)) {
                        TacticalOrder o{OK::SetLeader, side, u.selected};
                        o.group = u.ordersGroup;
                        submit(*f, o);
                        keep = false;
                    }
                    break;
                }
                case OrdersMode::Resolve: {
                    // A Yes/No prompt: Y means Yes; N, Esc and Enter mean No (spec 06 §3.4).
                    ImGui::SetCursorPos(ImVec2(ui.px(14), ui.px(14)));
                    ImGui::PushTextWrapPos(ui.px(size.x - 14));
                    ImGui::TextUnformatted("Let every empire's strategies fight the rest of the battle?");
                    ImGui::PopTextWrapPos();
                    row = 3;
                    const std::optional<bool> answer = yesNoKey();
                    if (button("Yes") || answer == true) {
                        submit(*f, TacticalOrder{OK::ResolveCombat, side});
                        keep = false;
                    } else if (button("No") || answer == false) {
                        u.ordersMode = OrdersMode::Menu;
                    }
                    break;
                }
            }
            if (u.ordersMode != OrdersMode::Menu && u.ordersMode != OrdersMode::Resolve) {
                row = 10;
                if (button("Cancel")) keep = false;
            }
        }
        ImGui::End();
        flush(*f);
        if (!keep) u.ordersMode = OrdersMode::Menu;
        return keep;
    }

private:
    template <class Button>
    bool menu(UiContext& ui, TacticalFight& f, const TacticalPiece* p, Button&& button) {
        TacticalUi& u = state();
        const TacticalBattle& b = *f.battle;
        const game::EmpireId side = b.phaseEmpire();
        const bool can = p != nullptr;
        bool keep = true;
        if (button("Launch Units", can)) {
            ui.open(ScreenId::TacticalLaunch);
            keep = false;
        }
        if (button("Launch Fighters in Groups", can)) u.ordersMode = OrdersMode::GroupSize;
        if (button("Drop Troops", can && p->troops)) {
            dropTroops(f);
            keep = false;
        }
        if (button("Ram Ship", can && p->movement > 0)) {
            u.aim = Aim::Ram;
            keep = false;
        }
        if (button("Capture Ship", can && p->boardingAttack > 0)) {
            u.aim = Aim::Capture;
            keep = false;
        }
        if (button("Resolve Combat", side.valid() && b.awaitingOrders())) u.ordersMode = OrdersMode::Resolve;
        if (button("Set Group Leader", can)) u.ordersMode = OrdersMode::LeaderNumber;
        if (button("Set Group Member", can)) u.ordersMode = OrdersMode::MemberNumber;
        if (button("Clear Group Assignment", can)) {
            clearGroup(f);
            keep = false;
        }
        if (button("Clear All Group Assignments", side.valid() && b.awaitingOrders() && !b.paused())) {
            submit(f, TacticalOrder{OK::ClearAllGroups, side});
            keep = false;
        }
        if (button("Cancel")) keep = false;
        return keep;
    }

    // Every fighter the selected piece carries, in single-design groups of `size` (spec 04 §10.4).
    static void launchInGroups(UiContext& ui, TacticalFight& f, int size) {
        const TacticalUi& u = state();
        const TacticalBattle& b = *f.battle;
        const TacticalPiece* p = ownSelected(b, u);
        if (!p) return;
        const game::GameState& s = b.state();
        for (const game::UnitStack& st : p->cargo) {
            if (st.design.index() >= s.designs.size() || ui.rules().hull(s.design(st.design).hull).type != ruleset::VehicleType::Fighter) continue;
            TacticalOrder o{OK::LaunchFighters, b.phaseEmpire(), u.selected};
            o.design = st.design;
            o.count = st.count;
            o.group = size;
            if (b.check(o).empty()) submit(f, o);
        }
    }
};

// ---- Launch Units (L; spec 04 §10.4) ---------------------------------------------------------------------------

// The selected piece's fighters, satellites and drones, launched 1, 5, 10 or
// all at a time. While this window stays open, units of one kind launched
// from the piece join the group made first in it, whatever their design;
// drones are one per group.
class TacticalLaunchScreen final : public Screen {
public:
    TacticalLaunchScreen() { window_ = ++state().launchWindow; }   // each opening is a new Launch Units session
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        TacticalFight* f = ui.session.tactical();
        if (!f || !f->battle) return false;
        TacticalBattle& b = *f->battle;
        TacticalUi& u = stateFor(f);
        const game::EmpireId side = b.phaseEmpire();
        const TacticalPiece* p = ownSelected(b, u);
        Dialog d(ui, screenTitle(ScreenId::TacticalLaunch), DialogSize::Prompt, 120);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        if (!p) {
            dimText("Select one of your pieces first.");
        } else {
            const game::GameState& s = b.state();
            ImGui::TextUnformatted(p->name.c_str());
            dimText(std::format("It may launch {} fighters, {} satellites and {} drones more this turn.", p->launchLeft[0], p->launchLeft[1],
                                p->launchLeft[2])
                        .c_str());
            bool any = false;
            for (const game::UnitStack& st : p->cargo) {
                if (st.design.index() >= s.designs.size()) continue;
                const ruleset::VehicleType t = ui.rules().hull(s.design(st.design).hull).type;
                if (t != ruleset::VehicleType::Fighter && t != ruleset::VehicleType::Satellite && t != ruleset::VehicleType::Drone) continue;
                any = true;
                ImGui::PushID(int(st.design.value));
                ImGui::TextUnformatted(std::format("{} x{}", s.design(st.design).name, st.count).c_str());
                ImGui::SameLine(ui.px(170));
                for (const int n : {1, 5, 10, 0}) {
                    TacticalOrder o{OK::Launch, side, u.selected};
                    o.design = st.design;
                    o.count = n > 0 ? n : st.count;
                    o.group = window_;
                    ImGui::PushID(n);
                    if (classicButton(ui, n > 0 ? std::format("{}", n).c_str() : "All", {40, 20}, 0, false, b.check(o).empty())) submit(*f, o);
                    ImGui::PopID();
                    ImGui::SameLine(0, ui.px(3));
                }
                ImGui::NewLine();
                ImGui::PopID();
            }
            if (!any) dimText("It carries no fighters, satellites or drones.");
        }
        if (!u.message.empty()) ImGui::TextColored(ImVec4(1, 0.72f, 0.45f, 1), "%s", u.message.c_str());
        d.beginButtons();
        d.close();
        flush(*f);
        return d.keepOpen();
    }

private:
    int window_ = 0;
};

// ---- Combat Options (spec 06 §1.10.3) ---------------------------------------------------------------------------

// Per computer, shared with the Options window where the rows are the same;
// Stop Combat only in the simulator.
class TacticalOptionsScreen final : public Screen {
public:
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        Dialog d(ui, screenTitle(ScreenId::TacticalOptions), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        ClassicSettings& prefs = settings();
        bool changed = false;
        ImGui::TextColored(kLabelBlue, "Options In Use");
        ImGui::BeginChild("##options", ImVec2(0, 0), ImGuiChildFlags_Borders);
        heading(ui, "Animation");
        changed |= lampToggle(ui, "Animate ship movement in combat", &prefs.animateCombatMovement);
        ImGui::Spacing();
        heading(ui, "Sound");
        changed |= lampToggle(ui, "Sound On", &prefs.soundOn);
        changed |= lampToggle(ui, "Music On", &prefs.musicOn);
        ImGui::Spacing();
        heading(ui, "Tactical Combat");
        changed |= lampToggle(ui, "Fast Tactical Combat", &prefs.fastTacticalCombat);
        changed |= lampToggle(ui, "Show Group Identifiers", &prefs.showGroupIdentifiers);
        changed |= lampToggle(ui, "Show Viewing Rectangle on Map", &prefs.showViewingRectangle);
        changed |= lampToggle(ui, "Center Map on Current Ship", &prefs.centerOnCurrentShip);
        changed |= lampToggle(ui, "Show Weapon To Hit Chances", &prefs.showToHitChances);
        changed |= lampToggle(ui, "Show Grid", &prefs.tacticalGrid);
        ImGui::EndChild();
        if (changed) saveSettings();
        d.beginButtons();
        const TacticalFight* f = ui.session.tactical();
        if (f && f->kind == TacticalFight::Kind::Simulation && d.button("Stop Combat")) {
            stateFor(f).stop = true;
            d.requestClose();
        }
        d.close();
        return d.keepOpen();
    }
};

// ---- Combat Piece Report (spec 06 §1.10.1) ----------------------------------------------------------------------

// A 353x422 report: a 128 px picture, the owner's flag, the name and the Detail lines.
class CombatPieceReportScreen final : public Screen {
public:
    explicit CombatPieceReportScreen(int piece) : piece_(piece) {}
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        const TacticalFight* f = ui.session.tactical();
        if (!f || !f->battle || piece_ < 0 || size_t(piece_) >= f->battle->pieces().size()) return false;
        const TacticalBattle& b = *f->battle;
        const game::GameState& s = b.state();
        const TacticalPiece& p = b.pieces()[size_t(piece_)];
        Dialog d(ui, screenTitle(ScreenId::CombatPieceReport), Vec2{353, 422}, 0);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        Sprite pic;
        const std::string style = p.owner.valid() && p.owner.index() < s.empires.size() ? s.empire(p.owner).race.style : std::string{};
        if (p.design.valid() && p.design.index() < s.designs.size()) {
            const ruleset::VehicleSize& hull = ui.rules().hull(s.design(p.design).hull);
            pic = ui.art.shipPortrait(style, hull);
            if (!pic) pic = ui.art.shipMini(style, hull);
        } else if (p.planet.valid() && p.planet.index() < s.galaxy.objects.size()) {
            const game::SpaceObject& o = s.galaxy.object(p.planet);
            if (o.sectorType < ui.rules().data().sectorObjectTypes.size()) pic = ui.art.planetPortrait(ui.rules().data().sectorObjectTypes[o.sectorType].picture);
            if (!pic) pic = objectSprite(ui, o);
        }
        image(ui, pic, {128, 128});
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (!style.empty())
            if (Sprite flag = ui.art.flag(style)) image(ui, flag, {39, 27});
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        if (p.design.valid() && p.design.index() < s.designs.size()) dimText(s.design(p.design).name.c_str());
        ImGui::EndGroup();
        ImGui::Spacing();
        heading(ui, "Detail");
        if (p.kind == PieceKind::Obstacle) {
            // A neutral obstacle (inferred: its name and kind; the original opens its ordinary report).
            dimText("A neutral obstacle: it takes no part in the fighting.");
        } else {
            for (const auto& [label, value] : pieceReportLines(ui.rules(), s, b.pieces(), piece_)) labelValue(ui, label.c_str(), value, 110);
        }
        // Esc closes a report window (spec 06 §3.4).
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) d.requestClose();
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ui.px(34));
        if (classicButton(ui, "Close", {100, 26})) d.requestClose();
        return d.keepOpen();
    }

private:
    int piece_ = -1;
};

} // namespace

std::unique_ptr<Screen> makeTacticalCombat(const ScreenArgs&) { return std::make_unique<TacticalCombatScreen>(); }
std::unique_ptr<Screen> makeTacticalOrders(const ScreenArgs&) { return std::make_unique<TacticalOrdersScreen>(); }
std::unique_ptr<Screen> makeTacticalOptions(const ScreenArgs&) { return std::make_unique<TacticalOptionsScreen>(); }
std::unique_ptr<Screen> makeTacticalLaunch(const ScreenArgs&) { return std::make_unique<TacticalLaunchScreen>(); }
std::unique_ptr<Screen> makeCombatPieceReport(const ScreenArgs& args) { return std::make_unique<CombatPieceReportScreen>(args.index); }

} // namespace opense4::client::classic
