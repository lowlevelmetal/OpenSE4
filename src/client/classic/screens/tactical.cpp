// Tactical Combat and the windows opened from it: Tactical Combat Orders,
// Launch Units, Combat Options and the Combat Piece Report (docs/spec/06
// §1.10.1-§1.10.3, §3.3; the rules are docs/spec/04 §4-§12): the player's
// side of the combat::TacticalBattle the session holds (ClassicSession::
// tactical()): a turn-based game's battle or a combat simulation.
//
// The map is the Combat Replay's (combat_map.hpp): the battle's record plays
// back as it grows, so every move and shot, the computer's too, is seen, each
// frame followed by the original's wait unless Fast Tactical Combat is on
// (spec 06 §1.10.3; replay.hpp). In a combat simulation the sides show as
// numbered colour boxes instead of flags (spec 04 §17).
// The window opens with Begin, Orders dim (a click shows any piece, E and
// Auto work); Begin starts the battle and becomes End Turn. Left-click
// selects an own piece, moves it to an empty square or fires the weapons
// ticked in the grid at an enemy (the ticks are the window's, spec 06
// §1.10.1); right-click opens the Combat Piece Report. Refusals the original
// explains in message boxes, and the outcome of a boarding, show as boxes
// that hold the playback. When the battle is over a "Combat Complete"
// message shows and the window closes. Hotkeys follow spec 06 §3.3; Esc does
// nothing here but cancel a Ram or Capture target.

#include "client/audio.hpp"
#include "client/classic/replay.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/combat_logic.hpp"
#include "client/classic/pointers.hpp"
#include "client/classic/screens/combat_map.hpp"
#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/design_tools.hpp"
#include "client/classic/screens/launch_window.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"

#include "game/abilities.hpp"
#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/tactical.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <numbers>
#include <optional>
#include <utility>

namespace opense4::client::classic {

namespace {

using game::combat::Square;
using game::combat::TacticalBattle;
using game::combat::TacticalOrder;
using game::combat::TacticalPiece;
using game::combat::TacticalWeapon;
using OK = TacticalOrder::Kind;
using PieceKind = game::CombatPiece::Kind;

// The layout of spec 06 §1.10.1 and §7 Q97 (confirmed: binary), W x H being
// the window, which covers the whole frame in both layouts (§2.1.1): the map
// at (10,38), (W-256) x (H-44); the current-piece panel 216 x 64 at (W-232,36);
// the weapon grid of 36 px cells, 6 to a row, at (W-232,102), 6 rows (5 on a
// frame narrower than 1024) with its up and down arrows just under it; the
// target panel 216 x 100 at (W-232,373), only on a frame at least 1024 wide;
// the 2 x 2 group of 113 x 30 buttons at (W-234,H-266), 70 px above the
// overview map, 218 x 190 at (W-230,H-196).
using combat_frame::kSideW;
using combat_frame::kPanelH;
using combat_frame::mapWidth;
using combat_frame::mapHeight;
using combat_frame::sideX;
constexpr float kTargetH = 100;
constexpr float kWeaponCell = combat_frame::kCell;
constexpr int kWeaponColumns = combat_frame::kColumns;
constexpr float kWeaponsY = 102;
constexpr float kTargetY = 373;
int weaponRows() { return frameW() < 1024.0f ? 5 : 6; }
// The grid's height: its rows of cells and the closing line.
float weaponGridH() { return kWeaponCell * float(weaponRows()) + 1; }

// Animation pace (spec 06 §1.10.3): the original's waits after every frame,
// none with Fast Tactical Combat; moves slide, or jump with "animate ship
// movement in combat" off. Every frame is drawn either way (replay.hpp).
CombatPace animationPace(const game::Rules& r) { return combatPace(r, settings().fastTacticalCombat, settings().animateCombatMovement); }

// A target picked on the map for Ram or Capture (the Orders window, R or C).
enum class Aim { None, Ram, Capture };

// The pickers the Orders window's items open, each a modal window of its own
// once the menu has closed (spec 06 §1.10.2).
enum class Picker { None, GroupSize, LeaderNumber, MemberNumber, Formation, Resolve };

// An order given while a window draws, carried out when it has drawn.
struct Queued {
    TacticalOrder order;
    // The shots of one click on an enemy: one Fire order per ticked weapon, in
    // design order; a weapon that fires is unticked, and a refusal is told only
    // when none of them fired (0: an order on its own).
    int volley = 0;
};

// What the windows share for the battle in progress.
struct TacticalUi {
    const TacticalFight* fight = nullptr;
    bool begun = false;          // Begin was pressed (spec 06 §1.10.1)
    bool resolved = false;       // Resolve Combat was confirmed: the four buttons dim, Auto shows pressed (§1.10.1)
    int selected = -1;
    // The weapons ticked in the grid, the window's and not the piece's (spec 06
    // §1.10.1): reset to every intact ready weapon when a piece becomes
    // selected, at the start of each phase and after the piece moves.
    std::vector<char> ticks;
    int ticksOf = -1;            // the piece they are for
    int target = -1;             // the piece the target panel shows (-1: none)
    int hoverEnemy = -1;         // the enemy under the pointer now (to-hit chances)
    // The target panel's damage line: the shields' and the structure's loss in
    // the last hit drawn while the panel showed a piece (spec 06 §1.10.1).
    std::optional<std::pair<int32_t, int32_t>> damage;
    Aim aim = Aim::None;
    Picker picker = Picker::None;
    int ordersGroup = 1;         // the group number picked for Set Group Leader
    int launchWindow = 0;        // the Launch Units window's session (spec 04 §10.4)
    std::optional<uint8_t> unitsStep;   // its Move One .. Move All during this battle (InterfaceOptions::unitsStep)
    std::string message;         // the last refusal or hint on the map (ours)
    std::vector<CombatNotice> notices;   // message boxes waiting, one shown at a time
    float cx = 36, cy = 31;      // squares at the map's centre
    float cellFrame = 36;        // zoom: frame pixels per square (36 in the original; the wheel zooms, ours)
    bool placed = false;         // the view has been centred on the player's pieces
    std::pair<int, uint32_t> phaseSeen{-1, 0};   // the combat turn and empire of the last phase that began here
    // Orders given while a window draws, carried out when it has drawn (they
    // change the pieces the window is drawing), and whether to work out the
    // results then.
    std::vector<Queued> queue;
    int volleys = 0;
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

void submit(TacticalFight&, TacticalOrder o) { state().queue.push_back(Queued{std::move(o)}); }

// The selection, with the weapons ticked afresh (spec 06 §1.10.1).
void selectPiece(const TacticalBattle& b, int piece) {
    TacticalUi& u = state();
    u.selected = piece;
    u.ticksOf = piece;
    u.ticks = piece >= 0 && size_t(piece) < b.pieces().size() ? readyTicks(b.pieces()[size_t(piece)]) : std::vector<char>{};
}

// The selected piece's ticks, sized to its weapons.
std::vector<char>& ticksFor(const TacticalBattle& b) {
    TacticalUi& u = state();
    if (u.ticksOf != u.selected) selectPiece(b, u.selected);
    if (u.selected >= 0 && size_t(u.selected) < b.pieces().size()) u.ticks.resize(b.pieces()[size_t(u.selected)].weapons.size(), 0);
    return u.ticks;
}

} // namespace

std::vector<std::string>& tacticalOrderLog() {
    static std::vector<std::string> log;
    return log;
}

namespace {

// Carries out the orders given this frame, in order.
void flush(TacticalFight& f) {
    TacticalUi& u = state();
    std::vector<Queued> queue = std::move(u.queue);
    u.queue.clear();
    TacticalBattle& b = *f.battle;
    // Per volley: whether a weapon fired, and the first refusal.
    std::map<int, std::pair<bool, std::string>> volleys;
    for (const Queued& q : queue) {
        const TacticalOrder& o = q.order;
        std::string why = b.submit(o);
        // Every order the battle took, for the lessons (battle_order).
        if (why.empty()) tacticalOrderLog().emplace_back(learn::battleOrderId(o.kind));
        if (q.volley != 0) {
            auto& [fired, first] = volleys[q.volley];
            if (why.empty()) {
                fired = true;
                // A weapon that fired is unticked (spec 06 §1.10.1).
                if (u.ticksOf == o.piece && o.weapon >= 0 && size_t(o.weapon) < u.ticks.size()) u.ticks[size_t(o.weapon)] = 0;
            } else if (first.empty()) {
                first = std::move(why);
            }
            continue;
        }
        // After every move of the selected piece its weapons are ticked afresh.
        if (why.empty() && o.kind == OK::Move && o.piece == u.selected) selectPiece(b, u.selected);
        // Ram and Capture explain a refusal in a message box (spec 06 §1.10.2).
        if (o.kind == OK::Ram || o.kind == OK::Capture) {
            if (const std::optional<CombatNotice> n = ramCaptureNotice(o.kind, why, b.autoOn())) u.notices.push_back(*n);
            continue;
        }
        // The group orders refuse silently (spec 06 §1.10.2), and so does a
        // landing from a piece with units but no troops (spec 04 §19.4 Q88).
        const bool group = o.kind == OK::SetLeader || o.kind == OK::SetMember || o.kind == OK::ClearGroup || o.kind == OK::ClearAllGroups;
        u.message = group || why == game::combat::kSilentRefusal ? std::string{} : std::move(why);
    }
    // A volley's refusal shows only when no weapon fired (ours: on the map).
    for (auto& [id, v] : volleys) u.message = v.first ? std::string{} : std::move(v.second);
    if (u.finishNow && b.finished() && !b.applied()) b.finish();
    u.finishNow = false;
}

bool isDrone(const TacticalPiece& p) { return p.kind == PieceKind::UnitGroup && p.type == ruleset::VehicleType::Drone; }

// An own piece that takes orders, and those that can still move.
bool commandable(const TacticalPiece& p, game::EmpireId side) { return tacticalCommandable(p, side); }
bool canMove(const TacticalPiece& p, game::EmpireId side) { return tacticalCanMove(p, side); }

// The selected piece when it is one the player may give orders to now.
const TacticalPiece* ownSelected(const TacticalBattle& b, const TacticalUi& u) {
    const game::EmpireId side = b.phaseEmpire();
    if (!side.valid() || !b.awaitingOrders() || b.paused() || u.selected < 0 || size_t(u.selected) >= b.pieces().size()) return nullptr;
    const TacticalPiece& p = b.pieces()[size_t(u.selected)];
    return commandable(p, side) ? &p : nullptr;
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

// Drop Troops (the Orders item and T): at once, no target click, on the
// adjacent colony of another side that comes last in piece order, whatever
// the treaty. Any own piece may give it (a planet lands its colony's troops).
// A refusal is explained in a message box titled "Drop Troops", except the
// one the original makes silently (spec 06 §1.10.2, spec 04 §11, §19.4 Q88).
void dropTroops(TacticalFight& f) {
    TacticalUi& u = state();
    const TacticalBattle& b = *f.battle;
    if (!ownSelected(b, u)) {
        u.message = "Select one of your ships first.";
        return;
    }
    const TacticalOrder o = dropTroopsOrder(b, u.selected);
    if (std::string why = b.check(o); !why.empty()) {
        if (why != game::combat::kSilentRefusal) u.notices.push_back({"Drop Troops", std::move(why)});
        return;
    }
    submit(f, o);
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
        ui.facts.battleBegun = u.begun;   // for the lessons (battle_begun, battle_turn)
        ui.facts.battleTurn = u.begun ? b.round() : 0;
        if (u.stop) {
            // Stop Combat (simulator only): the battle ends and the window closes.
            u.stop = false;
            return finishWindow(ui, *f);
        }
        sync(ui, b);
        if (u.begun) automatic(ui, *f);
        // Before Begin nothing is selected: the panel is empty (spec 06 §7 Q97, observed).
        // Drawing reads the battle; orders wait for flush() at the end of the frame.

        // Titled "Tactical Combat", a simulation's battle too (spec 06 §7 Q97, observed).
        Dialog d(ui, screenTitle(ScreenId::TacticalCombat), DialogSize::Full, 0);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = b.state();
        const CombatMapPainter paint(ui, s, b.record(), playback_);
        {
            // The title strip at the layout's places (spec 06 §2.1.1).
            std::vector<std::string> flags;
            std::vector<int> sides;
            int phase = -1;
            for (game::EmpireId e : b.participants()) {
                if (e == b.phaseEmpire()) phase = int(flags.size());
                sides.push_back(simulationSide(s, e));
                flags.push_back(sides.back() > 0 ? std::string{} : paint.styleOf(e));
            }
            combatTitleStrip(ui, d, sectorName(s, b.record().location, ui.session.player()), std::to_string(std::max(b.round(), 0)), flags, phase);
            // For lessons: the title strip, with the combat turn and whose phase it is.
            const auto window = std::find_if(ui.tags.begin(), ui.tags.end(), [](const UiTag& t) { return t.name == "window:tactical-combat"; });
            if (window != ui.tags.end()) {
                const ImVec2 min = window->min, max(window->max.x, window->min.y + ui.px(36));
                ui.tag("tactical-combat:title", min, max);
            }
            // A simulation's sides: their numbered boxes where a real battle shows flags (spec 04 §17).
            const float flagsX = layoutGeometry().tacticalTitle.flags;
            for (size_t i = 0; i < sides.size() && i < (frameW() < 1024.0f ? 6u : 10u); ++i)
                if (sides[i] > 0) {
                    const Vec2 at{flagsX + 28.0f * float(i), 7};
                    drawSideBox(ui, ImGui::GetWindowDrawList(), d.at(at), d.at(at + Vec2{26, 18}), sides[i]);
                }
            if (navigation(ui, d, b)) return finishWindow(ui, *f);
        }
        // The map fills the left part, straight under the title strip.
        ImGui::SetCursorScreenPos(d.at({10, 38}));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 mapSize = ui.size({mapWidth(), mapHeight()});
        drawMap(ui, *f, paint, mapSize);
        ui.tag("tactical-combat:map", origin, ImVec2(origin.x + mapSize.x, origin.y + mapSize.y));
        currentPanel(ui, d, *f);
        // The target-piece panel only on a window at least 1024 wide (§2.1.1).
        if (frameW() >= 1024.0f) targetPanel(ui, d, *f, paint);
        buttons(ui, d, *f);
        overview(ui, d, paint);
        keys(ui, *f);
        pickers(ui, *f);
        if (u.picker == Picker::None) noticeBox(ui);
        // A refusal, or the Ram / Capture aim, over the map's top left (ours).
        const std::string note = u.aim != Aim::None ? std::string(u.aim == Aim::Ram ? "Click the enemy to ram." : "Click the enemy ship to board.")
                                                    : u.message;
        if (!note.empty()) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 at{origin.x + ui.px(6), origin.y + ui.px(4)};
            dl->PushClipRect(origin, ImVec2(origin.x + mapSize.x, origin.y + mapSize.y), true);
            dl->AddText(nullptr, 0.0f, at, IM_COL32(255, 255, 0, 255), note.c_str(), nullptr, mapSize.x - ui.px(12));
            dl->PopClipRect();
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
        if (b.finished() && b.applied() && !animating() && u.notices.empty() && completePrompt(ui)) return finishWindow(ui, *f);
        return true;
    }

private:
    // The first message box waiting (refusals, boarding outcomes; spec 06
    // §1.10.2): titled, with OK; Esc and Enter close it too (§3.4). Play waits
    // while one is open.
    void noticeBox(UiContext& ui) {
        TacticalUi& u = state();
        if (u.notices.empty()) return;
        const CombatNotice& n = u.notices.front();
        const std::string id = n.title + "##tacticalnotice";
        if (!ImGui::IsPopupOpen(id.c_str())) ImGui::OpenPopup(id.c_str());
        ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size({320, 130}), ImGuiCond_Always);
        bool done = false;
        if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | kPromptFlags)) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(n.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ui.px(36));
            if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || (okKey() && !ImGui::IsWindowAppearing())) {
                done = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (done) u.notices.erase(u.notices.begin());
    }

    // What the window does as the playback passes events (spec 06 §1.10.1,
    // §1.10.2): a hit drawn writes the target panel's damage line, a seeker's
    // impact also puts the struck piece in the panel (and in view, with Center
    // Map on Current Ship), and a boarding gives its message boxes to the
    // sides played by hand.
    void passEvents(TacticalBattle& b) {
        TacticalUi& u = state();
        const size_t to = std::min(playback_.cursor(), playback_.eventCount());
        if (eventsSeen_ > to) eventsSeen_ = to;
        const game::CombatRecord& rec = b.record();
        auto nameOf = [&](uint32_t i) {
            if (i < b.pieces().size()) return b.pieces()[i].name;
            return i < rec.pieces.size() ? rec.pieces[i].name : std::string("?");
        };
        for (; eventsSeen_ < to; ++eventsSeen_) {
            const game::CombatEvent& e = playback_.event(eventsSeen_);
            if (e.kind == game::CombatEvent::Kind::Hit) {
                if (e.piece < rec.pieces.size() && rec.pieces[e.piece].kind == PieceKind::Seeker && e.target < b.pieces().size()) {
                    u.target = int(e.target);
                    centreOn(b, int(e.target));
                }
                if (u.target >= 0) u.damage = std::pair{e.shieldDamage, e.structureDamage};
            } else if (e.kind == game::CombatEvent::Kind::Boarding) {
                const game::EmpireId attacker = playback_.validPiece(e.piece) ? playback_.pieces()[e.piece].owner
                                                : e.piece < b.pieces().size() ? b.pieces()[e.piece].owner : game::EmpireId{};
                auto byHand = [&](game::EmpireId x) { return b.isPlayer(x) && !b.autoOn(); };
                for (CombatNotice& n : boardingNotices(e, nameOf(e.piece), nameOf(e.target), attacker, byHand)) u.notices.push_back(std::move(n));
            }
        }
    }

    // Closes the window: a game battle's orders answer its question and the game
    // carries on; after a simulation Designs and the Combat Simulator open
    // again with the same setup (spec 06 §1.10.4).
    bool finishWindow(UiContext& ui, TacticalFight& f) {
        const bool simulation = f.kind == TacticalFight::Kind::Simulation;
        ui.session.endTactical();
        if (simulation) {
            // Designs (when it closed for the battle) and the simulator open again.
            if (std::exchange(designsClosedForSimulation(), false)) ui.open(ScreenId::Designs);
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
        ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
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
            playback_.setPace(windowPace(ui));
            playback_.seekEvent(std::min(cursor, playback_.eventCount()));
            // Before Begin the map stays as the battle starts.
            if (state().begun && !playback_.atEnd()) playback_.play();
            if (!record_) eventsSeen_ = playback_.cursor();   // what was shown before this window opened
            record_ = &rec;
            events_ = rec.events.size();
            pieces_ = rec.pieces.size();
        }
        if (!state().begun) return;
        // The Combat Options may change the pace.
        if (playback_.pace().fast != settings().fastTacticalCombat || playback_.pace().animateMoves != settings().animateCombatMovement)
            playback_.setPace(windowPace(ui));
        if (!playback_.playing() && !playback_.atEnd()) playback_.play();
        // Play waits while a message box is open.
        if (!state().notices.empty()) return;
        const size_t before = playback_.cursor();
        playback_.advance(ui.dt);
        CombatMapPainter(ui, b.state(), rec, playback_).sounds(before, playback_.cursor());
        passEvents(b);
    }

    // The Tactical Combat window's pace: the 0.3 s pause after a seeker's
    // impact, and no move animated across the edge of the shown map (§1.10.3).
    CombatPace windowPace(UiContext& ui) {
        CombatPace pace = animationPace(ui.rules());
        pace.tactical = true;
        pace.inView = [this](int x, int y) {
            if (viewSize_.x <= 0.0f || viewSize_.y <= 0.0f) return true;
            const ImVec2 half{viewSize_.x * 0.5f, viewSize_.y * 0.5f};
            return squareInView(view_, {view_.center.x - half.x, view_.center.y - half.y}, {view_.center.x + half.x, view_.center.y + half.y}, x, y);
        };
        return pace;
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
        // At the start of each phase played by hand the selection is made by the
        // search forward from the first piece: a group leader that can move first
        // (spec 06 §1.10.1, §3.3, confirmed: binary), its weapons ticked afresh.
        const std::pair<int, uint32_t> phase{b.round(), side.value};
        if (phase != u.phaseSeen) {
            u.phaseSeen = phase;
            int first = nextTacticalPiece(b.pieces(), side, -1, PieceSearch::Move, true);
            if (first < 0)
                for (size_t i = 0; i < b.pieces().size() && first < 0; ++i)
                    if (commandable(b.pieces()[i], side)) first = int(i);
            selectPiece(b, first);
            centreOn(b, first);
        }
        (void)ui;
    }

    // Center Map on Current Ship (spec 06 §1.10.3, confirmed: binary): with it
    // on, the view moves to a newly selected piece only when its square is
    // outside the part of the map shown.
    void centreOn(const TacticalBattle& b, int piece, bool always = false) {
        if (piece < 0 || size_t(piece) >= b.pieces().size()) return;
        if (!always && !settings().centerOnCurrentShip) return;
        TacticalUi& u = state();
        const TacticalPiece& p = b.pieces()[size_t(piece)];
        if (!always && viewSize_.x > 0.0f && viewSize_.y > 0.0f) {
            const ImVec2 half{viewSize_.x * 0.5f, viewSize_.y * 0.5f};
            if (squareInView(view_, {view_.center.x - half.x, view_.center.y - half.y}, {view_.center.x + half.x, view_.center.y + half.y}, p.x, p.y))
                return;
        }
        u.cx = float(p.x) + float(p.size) * 0.5f;
        u.cy = float(p.y) + float(p.size) * 0.5f;
        u.placed = true;
    }

    // ---- Navigation -------------------------------------------------------------------------------------

    // The title strip's buttons at its right (spec 06 §7 Q97, confirmed:
    // binary): four 24 x 24 buttons in a row from (W-146,5), left to right the
    // next piece of the side that can still move (as Space and Ctrl+N), the
    // previous one (Ctrl+B), the next that can still fire (Ctrl+F) and the
    // previous one (Ctrl+D), drawn as two Nextprev.bmp selectors (the ship and
    // the crosshair) of 48 x 24; then the 20 x 20 Close.bmp box with a bar at
    // (W-34,7), which asks the window to close: refused while the battle is not
    // over. None of them does anything while an action is animated. True when
    // the window should close.
    bool navigation(UiContext& ui, const Dialog& d, TacticalBattle& b) {
        TacticalUi& u = state();
        const game::EmpireId side = b.phaseEmpire();
        // Dim before Begin and while no side of the player is in its phase (inferred).
        const bool live = side.valid() && u.begun && b.awaitingOrders();
        const bool inert = animating();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int group = 0; group < 2; ++group) {
            const Vec2 at{frameW() - 146 + 48.0f * float(group), 5};
            bool hovered = false, held = false;
            for (int half = 0; half < 2; ++half) {
                ImGui::SetCursorScreenPos(d.at(at + Vec2{24.0f * float(half), 0}));
                ImGui::PushID(group * 2 + half);
                ImGui::BeginDisabled(!live);
                const bool clicked = ImGui::InvisibleButton("##navigate", ui.size({24, 24}));
                ImGui::EndDisabled();
                ImGui::PopID();
                hovered = hovered || (live && ImGui::IsItemHovered());
                held = held || (live && ImGui::IsItemActive());
                if (!clicked || !live || inert) continue;
                // The left half picks the next piece, the right half the previous one
                // (the search of spec 06 §3.3: "next" walks the list backwards).
                pick(b, group == 0 ? PieceSearch::Move : PieceSearch::Fire, half == 1);
            }
            const int row = !live ? 3 : held ? 2 : hovered ? 1 : 0;
            // Nextprev.bmp: the ship selector (column 0) and the crosshair (column 3).
            if (const Sprite cell = ui.art.region("Pictures/Game/Buttons/Nextprev.bmp", group == 0 ? 0 : 3 * 48, row * 24, 48, 24, false))
                drawSprite(dl, cell, d.at(at), d.at(at + Vec2{48, 24}));
        }
        const Vec2 closeAt{frameW() - 34, 7};
        ImGui::SetCursorScreenPos(d.at(closeAt));
        const bool close = ImGui::InvisibleButton("##closeBattle", ui.size({20, 20}));
        const int row = ImGui::IsItemActive() ? 2 : ImGui::IsItemHovered() ? 1 : 0;
        if (const Sprite cell = ui.art.region("Pictures/Game/Buttons/Close.bmp", 0, row * 20, 20, 20, false))
            drawSprite(dl, cell, d.at(closeAt), d.at(closeAt + Vec2{20, 20}));
        return close && !inert && b.finished() && b.applied();
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
        combatViewInput(ui, o, size, hoveredBox, ImGui::IsItemActive(), u.cx, u.cy, u.cellFrame);

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
            // Show Group Identifiers (spec 06 §1.10.3, confirmed: binary): on every
            // piece with a group number, of any empire, a 14 x 12 box at the top
            // right of its square, blue for a leader and red for a member, with the
            // number in white in its middle (scaled with the zoom, ours).
            if (settings().showGroupIdentifiers && p.group >= 0) {
                const float k = v.cell / 36.0f;
                const ImVec2 b0{c.x + h - 14 * k, c.y - h}, b1{c.x + h, c.y - h + 12 * k};
                dl->AddRectFilled(b0, b1, p.isLeader ? IM_COL32(0, 0, 255, 255) : IM_COL32(255, 0, 0, 255));
                if (k >= 0.6f) {
                    ImFont* small = ui.fonts.small ? ui.fonts.small : ImGui::GetFont();
                    const float textSize = std::min(ui.fontPx(kSmallSize), 12 * k);
                    const std::string n = std::to_string(p.group);
                    const ImVec2 t = small->CalcTextSizeA(textSize, FLT_MAX, 0.0f, n.c_str());
                    dl->AddText(small, textSize, {(b0.x + b1.x - t.x) * 0.5f, (b0.y + b1.y - t.y) * 0.5f}, IM_COL32_WHITE, n.c_str());
                }
            }
        }

        if (script::collectingItems()) reportPieces(b, v, paint, sel);

        // What a click would do, shown by the pointer.
        const bool ours = u.begun && sel && side.valid() && sel->owner == side && b.awaitingOrders();
        const TacticalPiece* over = hovered && *hovered < b.pieces().size() ? &b.pieces()[*hovered] : nullptr;
        if (over && !over->alive) over = nullptr;
        const bool enemy = over && side.valid() && b.hostile(side, over->owner) && over->kind != PieceKind::Obstacle;
        u.hoverEnemy = enemy && ours ? int(*hovered) : -1;
        // The target panel follows the pointer (spec 06 §1.10.1): empty before Begin.
        if (u.begun && (hoveredBox || u.aim != Aim::None)) {
            const int own = sel && b.isPlayer(sel->owner) ? u.selected : -1;
            const int next = targetPanelPiece(b.pieces(), own, over ? int(*hovered) : -1, hoveredBox, u.aim != Aim::None, u.target);
            if (next != u.target) {
                u.target = next;
                u.damage.reset();
            }
        }
        bool hidePointer = false;
        // The install's pointers (spec 06 §5.8, §1.10.1): Target, Select and the
        // eight arrows over the map; our drawn ones only when a file is missing.
        TacticalPointerFacts pf;
        pf.begun = u.begun;
        pf.busy = animating();
        pf.overMap = hoveredBox;
        pf.aiming = u.aim != Aim::None;
        pf.selected = sel && side.valid() && sel->owner == side;
        pf.selectedIsDrone = sel && isDrone(*sel);
        pf.overOtherEmpire = over && side.valid() && over->owner != side && over->kind != PieceKind::Obstacle;
        if (sel) {
            pf.dx = hx - sel->x;
            pf.dy = hy - sel->y;
        }
        const Pointer classic = tacticalPointer(pf);
        const bool filePointer = pointers().has(classic);
        if (filePointer) pointers().request(classic);
        if (hoveredBox && ours && !animating()) {
            if (u.aim != Aim::None) {
                if (!filePointer) pointerCross(ui, dl, io.MousePos, enemy ? IM_COL32(255, 200, 60, 255) : IM_COL32(150, 150, 150, 200));
                hidePointer = !filePointer;
            } else if (enemy) {
                const bool can = !volleyOf(b, int(*hovered)).empty() && volleyProblem(b, int(*hovered)).empty();
                if (!filePointer) pointerCross(ui, dl, io.MousePos, can ? IM_COL32(80, 255, 90, 255) : IM_COL32(255, 70, 60, 255));
                hidePointer = !filePointer;
            } else if (!over && canMove(*sel, side)) {
                const std::vector<Square> path = b.pathTo(u.selected, hx, hy);
                for (const Square& sq : path) dl->AddCircleFilled(v.at(float(sq.x) + 0.5f, float(sq.y) + 0.5f), std::max(2.0f, v.cell * 0.12f), IM_COL32(120, 220, 255, 200));
                if (!path.empty() && !filePointer) {
                    pointerArrow(ui, dl, io.MousePos, directionOf(hx - sel->x, hy - sel->y), IM_COL32(120, 220, 255, 255));
                    hidePointer = true;
                }
            }
        }
        if (hidePointer) ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        dl->PopClipRect();
        dl->AddRect(o, o2, IM_COL32(66, 107, 216, 255));

        // Clicks. Before Begin a click on any piece, an enemy's too, shows it in the
        // current-piece panel and the weapon grid; an empty square does nothing (spec 06 §1.10.1).
        if (hoveredBox && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!u.begun) {
                if (over) selectPiece(b, int(*hovered));
            } else if (animating()) {
                skipAnimation();   // a click while the battle plays skips to its end
            } else if (!u.resolved) {
                click(ui, f, hovered ? int(*hovered) : -1, hx, hy);
            }
        }
        // Right-click: the Combat Piece Report (spec 06 §1.10.1).
        if (hoveredBox && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered) openReport(ui, int(*hovered));
    }

    // Input scripts (docs/BUILDING.md "Input scripts"): the pieces on the map as
    // piece:own (the player's), piece:enemy and piece:other (obstacles), and the
    // squares around the selected piece as square:dx,dy.
    void reportPieces(const TacticalBattle& b, const CombatView& v, const CombatMapPainter& paint, const TacticalPiece* sel) const {
        for (size_t i = 0; i < b.pieces().size() && i < playback_.pieces().size(); ++i) {
            const TacticalPiece& p = b.pieces()[i];
            if (!p.alive || !playback_.pieces()[i].onMap) continue;
            const ImVec2 c = paint.piecePos(v, uint32_t(i));
            const float h = std::max(2.0f, v.cell * float(p.size) * 0.5f);
            const char* kind = b.isPlayer(p.owner) ? "piece:own" : p.kind == PieceKind::Obstacle ? "piece:other" : "piece:enemy";
            script::reportItem(kind, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h));
        }
        if (!sel) return;
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                const int x = sel->x + dx, y = sel->y + dy;
                if ((dx == 0 && dy == 0) || x < 0 || y < 0 || x >= game::combat::kCombatMapWidth || y >= game::combat::kCombatMapHeight) continue;
                script::reportItem(std::format("square:{},{}", dx, dy), v.at(float(x), float(y)), v.at(float(x + 1), float(y + 1)));
            }
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

    // The Fire orders of a click on `target`: one per weapon ticked, in design order (spec 04 §6, spec 06 §1.10.1).
    std::vector<TacticalOrder> volleyOf(const TacticalBattle& b, int target) const {
        std::vector<TacticalOrder> out;
        const TacticalUi& u = state();
        if (u.ticksOf != u.selected) return out;
        for (size_t w = 0; w < u.ticks.size(); ++w) {
            if (!u.ticks[w]) continue;
            TacticalOrder o{OK::Fire, b.phaseEmpire(), u.selected, target};
            o.weapon = int(w);
            out.push_back(o);
        }
        return out;
    }
    // Why none of the ticked weapons can fire at `target` now (empty: one can).
    std::string volleyProblem(const TacticalBattle& b, int target) const {
        std::string first;
        for (const TacticalOrder& o : volleyOf(b, target)) {
            std::string why = b.check(o);
            if (why.empty()) return {};
            if (first.empty()) first = std::move(why);
        }
        return first.empty() ? std::string("No weapon is ticked.") : first;
    }

    void click(UiContext& ui, TacticalFight& f, int piece, int x, int y) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const game::EmpireId side = b.phaseEmpire();
        const TacticalPiece* over = piece >= 0 && size_t(piece) < b.pieces().size() && b.pieces()[size_t(piece)].alive ? &b.pieces()[size_t(piece)] : nullptr;
        if (!side.valid()) {
            if (over) selectPiece(b, piece);
            return;
        }
        if (u.aim != Aim::None) {
            // Ram or Capture: a click on a piece aims it, one on an empty square
            // cancels. With nothing of ours selected the order is refused (spec 06 §1.10.2).
            const OK kind = u.aim == Aim::Ram ? OK::Ram : OK::Capture;
            u.aim = Aim::None;
            if (over) {
                submit(f, TacticalOrder{kind, side, ownSelected(b, u) ? u.selected : -1, piece});
                audio().play("button");
            }
            return;
        }
        if (over && commandable(*over, side)) {
            selectPiece(b, piece);   // its weapons ticked afresh, the same piece clicked again too
            u.message.clear();
            return;
        }
        if (!ownSelected(b, u)) {
            if (over) selectPiece(b, piece);   // nothing of ours selected: look at it (ours)
            return;
        }
        if (over) {
            // Another empire's piece: the ticked weapons fire at it. A piece that is
            // not hostile (an ally, our own drones or seekers, a neutral obstacle) is
            // refused in a message box (spec 04 §6, spec 06 §1.10.2).
            if (!b.hostile(side, over->owner) || over->kind == PieceKind::Obstacle) {
                u.notices.push_back({"Cannot Fire Weapons", "You cannot fire on that: it is not an enemy."});
                return;
            }
            const std::vector<TacticalOrder> shots = volleyOf(b, piece);
            if (shots.empty()) {
                u.message = "No weapon is ticked.";
                return;
            }
            const int volley = ++u.volleys;
            for (const TacticalOrder& o : shots) u.queue.push_back(Queued{o, volley});
            return;
        }
        TacticalOrder mv{OK::Move, side, u.selected};
        mv.x = x;
        mv.y = y;
        submit(f, mv);
        (void)ui;
    }

    // ---- The current piece ----------------------------------------------------------------------------

    // The picture of a piece for the panels.
    Sprite piecePicture(UiContext& ui, const game::GameState& s, const TacticalPiece& p, const CombatMapPainter& paint) const {
        if (p.design.valid() && p.design.index() < s.designs.size()) {
            const game::Design& design = s.design(p.design);
            const ruleset::VehicleSize& hull = ui.rules().hull(design.hull);
            if (Sprite pic = ui.art.designPortrait(paint.styleOf(p.owner), hull, design.picture)) return pic;
            return ui.art.designMini(paint.styleOf(p.owner), hull, design.picture);
        }
        if (p.planet.valid() && p.planet.index() < s.galaxy.objects.size()) return objectSprite(ui, s.galaxy.object(p.planet));
        return {};
    }

    // What the Size line of a panel says: the hull size's name; nothing for a planet (spec 06 §7 Q97, observed).
    static std::string sizeText(UiContext& ui, const game::GameState& s, const TacticalPiece& p) {
        if (p.design.valid() && p.design.index() < s.designs.size() && p.kind != PieceKind::Planet)
            return p.kind == PieceKind::UnitGroup ? unitsText(s, p) : ui.rules().hull(s.design(p.design).hull).name;
        return p.kind == PieceKind::Seeker ? "Seeker" : p.kind == PieceKind::Obstacle ? "Obstacle" : std::string{};
    }

    // A piece's panel at `at` (frame pixels from the window), as the original
    // lays out both panels (spec 06 §7 Q97, observed): the picture in a 38 x 38
    // frame at (2,2); the name in white at (42,4); "Size" and a second line
    // ("Move" for the current piece, "Dist" for the target) in #7D9FFF small
    // type at (47,23) and (47,34) with the values in white at x 91; the owner's
    // flag (a simulation's numbered box) 26 x 18 at (190,3) (the texts' places
    // are their letters' tops, as measured); and the bar frame
    // in #4F65A2 from (2,42) to (204,61) holding the shield row in blue (y 44-50)
    // over the structure row in red (y 53-59), blocks 3 px wide every 4 px from
    // x 4, no numbers; a planet's structure in thousands in red at the bar's
    // right end. The group badge (blue a leader, red a member, spec 06 §1.6) on
    // the picture's top right corner is ours (inferred).
    void piecePanel(UiContext& ui, const Dialog& d, Vec2 at, const TacticalBattle& b, int index, const char* second, const std::string& secondValue,
                    const CombatMapPainter& paint) {
        const game::GameState& s = b.state();
        const TacticalPiece& p = b.pieces()[size_t(index)];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 frame = imColor(palette::kFrame);
        auto box = [&](Vec2 a, Vec2 z, ImU32 c) { dl->AddRect(d.at(at + a), d.at(at + z + Vec2{1, 1}), c, 0.0f, std::max(1.0f, ui.px(1))); };
        combatPanelHead(ui, d, at, s, piecePicture(ui, s, p, paint), paint.pieceName(uint32_t(index)), p.alive ? IM_COL32_WHITE : IM_COL32(255, 0, 0, 255),
                        sizeText(ui, s, p), second, secondValue, p.owner);
        if (p.isLeader || p.leader >= 0 || p.group >= 0)
            dl->AddRectFilled(d.at(at + Vec2{31, 3}), d.at(at + Vec2{39, 11}), p.isLeader ? IM_COL32(0, 0, 255, 255) : IM_COL32(255, 0, 0, 255));
        ImFont* small = ui.fonts.small ? ui.fonts.small : ImGui::GetFont();
        const float smallSize = ui.fontPx(kSmallSize);
        if (p.kind == PieceKind::Obstacle || p.kind == PieceKind::Seeker) return;
        box({2, 42}, {204, 61}, frame);
        // Each row: 50 blocks of 3 x 7 px, as many lit as the share left (a part rounds up; inferred).
        auto row = [&](float y, int64_t now, int64_t full, ImU32 color) {
            if (full <= 0 || now <= 0) return;
            const int64_t lit = std::min<int64_t>(50, (now * 50 + full - 1) / full);
            for (int64_t k = 0; k < lit; ++k) {
                const Vec2 a = at + Vec2{4 + 4 * float(k), y};
                dl->AddRectFilled(d.at(a), d.at(a + Vec2{3, 7}), color);
            }
        };
        row(44, p.shields, p.shieldsMax, IM_COL32(0, 0, 255, 255));
        row(53, 100 - p.damagePercent, 100, IM_COL32(255, 0, 0, 255));
        if (p.kind == PieceKind::Planet) {
            // "21k": thousands, truncated (inferred), right-aligned at the bar's end.
            const std::string hp = p.hitPoints >= 1000 ? std::format("{}k", p.hitPoints / 1000) : std::to_string(std::max<int64_t>(0, p.hitPoints));
            const ImVec2 t = small->CalcTextSizeA(smallSize, FLT_MAX, 0.0f, hp.c_str());
            const ImVec2 c{d.at(at + Vec2{202, 0}).x - t.x, d.at(at + Vec2{0, 52}).y};
            // On black, so that it reads over the blocks (ours).
            dl->AddRectFilled({c.x - ui.px(2), c.y}, {c.x + t.x, c.y + ui.px(9)}, IM_COL32_BLACK);
            dl->AddText(small, smallSize, c, IM_COL32(255, 0, 0, 255), hp.c_str());
        }
    }

    void currentPanel(UiContext& ui, const Dialog& d, TacticalFight& f) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const CombatMapPainter paint(ui, b.state(), b.record(), playback_);
        const Vec2 at{sideX(), 36};
        ImGui::SetCursorScreenPos(d.at(at));
        // The panel is a button: clicking the current piece's header opens its report (§1.10.1).
        const bool clicked = ImGui::InvisibleButton("##currentPanel", ui.size({kSideW, kPanelH}));
        ui.tagItem("tactical-combat:piece");
        const bool have = u.selected >= 0 && size_t(u.selected) < b.pieces().size();
        if (have) {
            const TacticalPiece& p = b.pieces()[size_t(u.selected)];
            piecePanel(ui, d, at, b, u.selected, "Move", std::format("{}/{}", p.movement, p.movementMax), paint);
            if (clicked) openReport(ui, u.selected);
        }
        // A new piece shows its weapons from the top.
        if (u.selected != weaponsOf_) {
            weaponsOf_ = u.selected;
            weaponTop_ = 0;
        }
        // The weapon grid of 36 px cells, 6 to a row, empty cells drawn too:
        // click a weapon to tick or untick it; a click on an enemy fires the
        // ticked ones (spec 06 §1.10.1). A reloading weapon may be ticked by hand
        // too (inferred, spec 04 §19.6 Q91). With Show Weapon To Hit Chances on,
        // hovering an enemy shows each ready weapon's chance (spec 06 §1.10.3).
        // When the piece has more weapons than cells the arrows under the grid
        // scroll it a row at a time (shown only then, inferred).
        const Vec2 grid{sideX(), kWeaponsY};
        const int rows = weaponRows();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos(d.at(grid));
        ImGui::BeginGroup();
        ImGui::Dummy(ui.size({kSideW, weaponGridH()}));
        size_t weapons = 0;
        if (have) {
            const TacticalPiece& p = b.pieces()[size_t(u.selected)];
            weapons = p.weapons.size();
            const bool ours = u.begun && !u.resolved && b.phaseEmpire().valid() && p.owner == b.phaseEmpire() && commandable(p, p.owner);
            const int chancesAt = settings().showToHitChances && ours ? u.hoverEnemy : -1;
            std::vector<char>& ticks = ticksFor(b);
            const size_t first = size_t(weaponTop_) * size_t(kWeaponColumns);
            for (size_t w = first; w < p.weapons.size() && w < first + size_t(kWeaponColumns * rows); ++w) {
                const TacticalWeapon& tw = p.weapons[w];
                const size_t slot = w - first;
                const Vec2 cell = grid + Vec2{kWeaponCell * float(slot % kWeaponColumns), kWeaponCell * float(slot / kWeaponColumns)};
                ImGui::SetCursorScreenPos(d.at(cell));
                ImGui::PushID(int(w));
                if (ImGui::InvisibleButton("##weapon", ui.size({kWeaponCell, kWeaponCell})) && ours && w < ticks.size()) {
                    ticks[w] = ticks[w] ? 0 : 1;
                    tacticalOrderLog().emplace_back(learn::battleOrderId(OK::ToggleWeapon));   // for the lessons
                }
                const ImVec2 a = d.at(cell);
                const bool ticked = w < ticks.size() && ticks[w] != 0;
                // The to-hit chance of a ready weapon, right-aligned at the top right (spec 06 §1.10.1).
                std::optional<int> chance;
                if (chancesAt >= 0 && b.fireProblem(u.selected, int(w), chancesAt).empty()) chance = b.hitChance(u.selected, int(w), chancesAt);
                weaponCell(ui, b.state(), p, tw, a, ImGui::IsItemHovered(), ticked, chance);
                if (ImGui::IsItemHovered()) weaponTooltip(ui, b, int(w), tw, ticked);
                ImGui::PopID();
            }
        }
        weaponGridLines(ui, d, grid, rows);
        ImGui::EndGroup();
        ui.tagItem("tactical-combat:weapons");
        const int hiddenRows = int((weapons + size_t(kWeaponColumns) - 1) / size_t(kWeaponColumns)) - rows;
        weaponTop_ = std::clamp(weaponTop_, 0, std::max(0, hiddenRows));
        if (hiddenRows > 0) {
            // BigUpDownArrows.bmp: the up arrow (column 0) over the down arrow (column 1), 64 x 17 each.
            for (int down = 0; down < 2; ++down) {
                const Vec2 aAt = grid + Vec2{0, weaponGridH() + 17.0f * float(down)};
                const bool can = down ? weaponTop_ < hiddenRows : weaponTop_ > 0;
                ImGui::SetCursorScreenPos(d.at(aAt));
                ImGui::PushID(down ? "##weaponsDown" : "##weaponsUp");
                ImGui::BeginDisabled(!can);
                if (ImGui::InvisibleButton("##arrow", ui.size({64, 17})) && can) weaponTop_ += down ? 1 : -1;
                ImGui::EndDisabled();
                ImGui::PopID();
                const int look = !can ? 3 : ImGui::IsItemActive() ? 2 : ImGui::IsItemHovered() ? 1 : 0;
                if (const Sprite arrow = ui.art.region("Pictures/Game/Buttons/BigUpDownArrows.bmp", down * 64, look * 17, 64, 17, false))
                    drawSprite(dl, arrow, d.at(aAt), d.at(aAt + Vec2{64, 17}));
            }
        }
    }

    // A weapon's cell (spec 06 §1.10.1, §7 Q97, confirmed: binary and
    // observed): the component's picture on a teal background while the
    // weapon is ticked, the mount's code in white at the top left and the
    // level's numeral at the bottom right; one red 3 x 6 bar per combat turn of
    // reload left, up to 5, drawn leftward from x 32, 4 px apart; with Show
    // Weapon To Hit Chances, a ready weapon's chance right-aligned at the top
    // right; and a unit group's count of the weapon at the top right when it is
    // more than 1 (under the chance when both show, inferred). A destroyed
    // weapon is drawn in red (ours).
    void weaponCell(UiContext& ui, const game::GameState& s, const TacticalPiece& p, const TacticalWeapon& tw, ImVec2 a, bool hovered, bool ticked,
                    std::optional<int> chance) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const game::Rules& r = ui.rules();
        const ImVec2 sz = ui.size({kWeaponCell, kWeaponCell});
        const bool destroyed = tw.instances <= 0;
        if (ticked) dl->AddRectFilled(a, {a.x + sz.x, a.y + sz.y}, IM_COL32(0, 128, 128, 255));
        const ruleset::Component& c = r.component(tw.component);
        if (Sprite icon = ui.art.component(c.picture))
            drawSprite(dl, icon, {a.x + ui.px(2), a.y + ui.px(2)}, {a.x + ui.px(34), a.y + ui.px(34)}, destroyed ? IM_COL32(255, 80, 80, 150) : IM_COL32_WHITE);
        // The reload bars, the first one with its left edge at x 32 (inferred: from the cell's top, y 2 to 8).
        const int bars = reloadBars(tw);
        for (int k = 0; k < bars; ++k) {
            const float x = 32.0f - 4.0f * float(k);
            dl->AddRectFilled({a.x + ui.px(x), a.y + ui.px(2)}, {a.x + ui.px(x + 3), a.y + ui.px(8)}, IM_COL32(255, 0, 0, 255));
        }
        ImFont* small = ui.fonts.small ? ui.fonts.small : ImGui::GetFont();
        const float size = ui.fontPx(kSmallSize);
        if (p.design.valid() && p.design.index() < s.designs.size()) {
            const game::Design& design = s.design(p.design);
            if (tw.entry < design.entries.size() && design.entries[tw.entry].component == tw.component) {
                const int mount = design.entries[tw.entry].mount;
                if (mount >= 0 && size_t(mount) < r.data().weaponMounts.size())
                    dl->AddText(small, size, {a.x + ui.px(2), a.y + ui.px(1)}, IM_COL32_WHITE, r.data().weaponMounts[size_t(mount)].shortName.c_str());
            }
        }
        const std::string level = romanNumeral(c.romanNumeral);
        if (!level.empty()) {
            const ImVec2 t = small->CalcTextSizeA(size, FLT_MAX, 0.0f, level.c_str());
            dl->AddText(small, size, {a.x + sz.x - t.x - ui.px(2), a.y + sz.y - t.y}, IM_COL32_WHITE, level.c_str());
        }
        float topRight = ui.px(1);
        auto rightAligned = [&](const std::string& text, ImU32 color) {
            const ImVec2 t = small->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
            const ImVec2 at{a.x + sz.x - t.x - ui.px(2), a.y + topRight};
            dl->AddRectFilled({at.x - 1, at.y}, {at.x + t.x + 1, at.y + t.y}, IM_COL32(0, 0, 0, 160));
            dl->AddText(small, size, at, color, text.c_str());
            topRight += t.y;
        };
        if (chance) rightAligned(std::format("{}%", *chance), IM_COL32(255, 255, 0, 255));
        if (tw.instances > 1) rightAligned(std::to_string(tw.instances), IM_COL32_WHITE);
        if (hovered) dl->AddRect(a, {a.x + sz.x, a.y + sz.y}, IM_COL32(168, 188, 255, 255));
    }

    void weaponTooltip(UiContext& ui, TacticalBattle& b, int w, const TacticalWeapon& tw, bool ticked) {
        TacticalUi& u = state();
        const ruleset::Component& c = ui.rules().component(tw.component);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(c.name.c_str());
        dimText(std::format("{} · reload {} · reach {}{}", tw.kind == ruleset::WeaponKind::Seeking ? "Seeking" : tw.kind == ruleset::WeaponKind::PointDefense ? "Point-defense" : "Direct fire",
                            tw.reloadRate, tw.reach, tw.instances > 1 ? std::format(" · {} weapons", tw.instances) : std::string{})
                    .c_str());
        dimText(ticked ? "Ticked: fires when you click an enemy. Click to untick." : "Not ticked. Click to tick.");
        if (u.target >= 0 && size_t(u.target) < b.pieces().size() && b.pieces()[size_t(u.target)].alive) {
            const std::string why = b.fireProblem(u.selected, w, u.target);
            if (why.empty()) ImGui::Text("At the target: %d%% to hit, %d damage", b.hitChance(u.selected, w, u.target), b.damageAt(u.selected, w, u.target));
            else ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", why.c_str());
        }
        ImGui::EndTooltip();
    }

    // ---- The target piece -------------------------------------------------------------------------------

    // The target's panel, 216 x 100 at (W-232,373): "Dist" its distance in
    // squares from the current piece (spec 06 §7 Q97); empty without a target.
    // After a hit is drawn while it shows a piece, a 220 x 20 line 66 px below
    // its top says "Damage: " in white, the shields' loss in blue, "/" in
    // white and the structure's loss in red (spec 06 §1.10.1, confirmed: binary).
    void targetPanel(UiContext& ui, const Dialog& d, TacticalFight& f, const CombatMapPainter& paint) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const Vec2 at{sideX(), kTargetY};
        ImGui::SetCursorScreenPos(d.at(at));
        ImGui::Dummy(ui.size({kSideW, kTargetH}));
        ui.tagItem("tactical-combat:target");
        if (u.target < 0 || size_t(u.target) >= b.pieces().size()) return;
        const bool fromSelected = u.selected >= 0 && size_t(u.selected) < b.pieces().size() && b.pieces()[size_t(u.selected)].alive;
        piecePanel(ui, d, at, b, u.target, "Dist", fromSelected ? std::to_string(b.distance(u.selected, u.target)) : std::string{}, paint);
        if (!u.damage) return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 c = d.at(at + Vec2{2, 66});
        dl->PushClipRect(c, d.at(at + Vec2{222, 86}), true);
        auto part = [&](const std::string& text, ImU32 color) {
            dl->AddText(c, color, text.c_str());
            c.x += ImGui::CalcTextSize(text.c_str()).x;
        };
        part("Damage: ", IM_COL32_WHITE);
        part(std::to_string(u.damage->first), IM_COL32(0, 0, 255, 255));
        part("/", IM_COL32_WHITE);
        part(std::to_string(u.damage->second), IM_COL32(255, 0, 0, 255));
        dl->PopClipRect();
    }

    // ---- Buttons and the overview map ------------------------------------------------------------------

    // Options and Orders, then the Auto check box and Begin / End Turn, a 2 x 2
    // group of 113 x 30 buttons 70 px above the overview map (§1.10.1).
    void buttons(UiContext& ui, const Dialog& d, TacticalFight& f) {
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const game::EmpireId side = b.phaseEmpire();
        // After Resolve Combat all four dim and Auto shows pressed (spec 06 §1.10.1, confirmed: binary).
        const bool live = !u.resolved;
        const bool orders = live && u.begun && side.valid() && !animating();
        const float x = frameW() - 234, y = frameH() - 266;
        ImGui::SetCursorScreenPos(d.at({x, y}));
        if (classicButton(ui, "Options", {113, 30}, 0, false, live)) ui.open(ScreenId::TacticalOptions);
        ui.tagItem("tactical-combat:options");
        ImGui::SetCursorScreenPos(d.at({x + 113, y}));
        if (classicButton(ui, "Orders", {113, 30}, 0, false, orders && !b.paused())) ui.open(ScreenId::TacticalOrders);
        ui.tagItem("tactical-combat:orders");
        // Auto: one toggle for every empire, from the next phase on (spec 04 §4);
        // a check box (observed, spec 07 session 3), live before Begin too (spec 06 §1.10.1).
        TacticalOrder toggle{OK::Auto, side};
        toggle.on = !b.autoOn();
        ImGui::SetCursorScreenPos(d.at({x, y + 30}));
        if (classicButton(ui, "Auto", {113, 30}, 2, b.autoOn() || u.resolved, live && side.valid() && !animating())) submit(f, toggle);
        ui.tagItem("tactical-combat:auto");
        // Begin, then End Turn in the same place: one tag.
        ImGui::SetCursorScreenPos(d.at({x + 113, y + 30}));
        if (!u.begun) {
            if (classicButton(ui, "Begin", {113, 30})) begin(b);
        } else if (classicButton(ui, "End Turn", {113, 30}, 0, false, orders)) {
            submit(f, TacticalOrder{OK::EndPhase, side});
        }
        ui.tagItem("tactical-combat:end-turn");
    }

    // The whole map, 218 x 190 at (W-230, H-196), 3 px a square, with the dotted
    // Viewing Rectangle when it is on; a click moves the view there (§1.10.1).
    void overview(UiContext& ui, const Dialog& d, const CombatMapPainter& paint) {
        TacticalUi& u = state();
        combatOverview(ui, d, paint, view_, viewSize_, settings().showViewingRectangle, u.cx, u.cy);
    }

    void begin(const TacticalBattle& b) {
        TacticalUi& u = state();
        u.begun = true;
        if (!playback_.atEnd()) playback_.play();
        centreOn(b, u.selected);
    }

    // ---- The Orders window's pickers (spec 06 §1.10.2) --------------------------------------------------

    // Whether Set Group Leader with number `group` goes on to the formation
    // list: a piece in no group, or the leader of that very number (which picks
    // a new formation); a member, or the leader of another number, gets nothing
    // (spec 04 §5, spec 06 §1.10.2, confirmed: binary).
    static bool leaderMayPick(UiContext& ui, const TacticalBattle& b, int piece, int group) {
        TacticalOrder o{OK::SetLeader, b.phaseEmpire(), piece};
        o.group = group;
        o.formation = ui.rules().data().formations.empty() ? -1 : 0;
        return b.check(o).empty();
    }

    // Each picker is a modal list window of its own, opened once the Orders
    // menu has closed: "Select Fighters Per group" (Amount 5 to 50), "Select
    // Combat Group" (1 to 9) and, for a new leader, "Select Formation" (closed
    // without a choice, the piece's group marks are cleared); and the
    // "Resolve Combat" confirmation.
    void pickers(UiContext& ui, TacticalFight& f) {
        TacticalUi& u = state();
        if (u.picker == Picker::None) return;
        TacticalBattle& b = *f.battle;
        const game::EmpireId side = b.phaseEmpire();
        const TacticalPiece* p = ownSelected(b, u);
        if (u.picker != Picker::Resolve && !p) {
            u.picker = Picker::None;
            return;
        }
        std::string title, column;
        std::vector<std::string> rows;
        const auto& formations = ui.rules().data().formations;
        switch (u.picker) {
            case Picker::GroupSize:
                title = "Select Fighters Per group";
                column = "Amount";
                for (int n : game::combat::kFighterGroupSizes) rows.push_back(std::to_string(n));
                break;
            case Picker::LeaderNumber:
            case Picker::MemberNumber:
                title = "Select Combat Group";
                column = "Group";
                for (int n = 1; n <= 9; ++n) rows.push_back(std::to_string(n));
                break;
            case Picker::Formation:
                if (formations.empty()) {
                    // Without formations the leader takes none.
                    TacticalOrder o{OK::SetLeader, side, u.selected};
                    o.group = u.ordersGroup;
                    submit(f, o);
                    u.picker = Picker::None;
                    return;
                }
                title = "Select Formation";
                column = "Formation";
                for (const ruleset::Formation& fm : formations) rows.push_back(fm.name);
                break;
            case Picker::Resolve: title = "Resolve Combat"; break;
            case Picker::None: return;
        }
        const std::string id = title + "##tacticalpicker";
        if (!ImGui::IsPopupOpen(id.c_str())) ImGui::OpenPopup(id.c_str());
        const bool box = u.picker == Picker::Resolve;
        const Vec2 size = box ? Vec2{320, 120} : Vec2{240, 60 + 22 * float(std::min<size_t>(rows.size(), 10)) + 40};
        ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
        int picked = -1;
        bool cancelled = false, yes = false;
        if (ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | kPromptFlags)) {
            if (u.picker == Picker::Resolve) {
                // A Yes/No message box: Y means Yes; N, Esc and Enter mean No (spec 06 §3.4).
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted("Should the rest of the battle be fought automatically?");
                ImGui::PopTextWrapPos();
                const std::optional<bool> key = yesNoKey();
                ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ui.px(36));
                yes = ImGui::Button("Yes", ui.size({140, 26})) || key == true;
                ImGui::SameLine();
                cancelled = ImGui::Button("No", ui.size({140, 26})) || key == false;
            } else {
                ImGui::TextColored(kLabelBlue, "%s", column.c_str());
                beginList(ui, "##rows", ImVec2(0, -ui.px(34)), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
                for (size_t k = 0; k < rows.size(); ++k)
                    if (ImGui::Selectable(rows[k].c_str(), false)) picked = int(k);
                endList(ui);
                cancelled = ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) ||
                            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsWindowAppearing());
            }
            if (picked >= 0 || cancelled || yes) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (picked < 0 && !cancelled && !yes) return;
        const Picker was = u.picker;
        u.picker = Picker::None;
        switch (was) {
            case Picker::GroupSize:
                if (picked >= 0) launchInGroups(ui, f, game::combat::kFighterGroupSizes[size_t(picked)]);
                break;
            case Picker::LeaderNumber:
                if (picked >= 0) {
                    u.ordersGroup = picked + 1;
                    if (leaderMayPick(ui, b, u.selected, u.ordersGroup)) u.picker = Picker::Formation;
                }
                break;
            case Picker::MemberNumber:
                if (picked >= 0) {
                    TacticalOrder o{OK::SetMember, side, u.selected};
                    o.group = picked + 1;
                    submit(f, o);
                }
                break;
            case Picker::Formation:
                if (picked >= 0) {
                    TacticalOrder o{OK::SetLeader, side, u.selected};
                    o.group = u.ordersGroup;
                    o.formation = picked;
                    submit(f, o);
                } else if (p && (p->isLeader || p->group >= 0)) {
                    submit(f, TacticalOrder{OK::ClearGroup, side, u.selected});   // closed without a choice
                }
                break;
            case Picker::Resolve:
                if (yes) {
                    submit(f, TacticalOrder{OK::ResolveCombat, side});
                    u.resolved = true;   // the four buttons dim, Auto shows pressed (spec 06 §1.10.1)
                }
                break;
            case Picker::None: break;
        }
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

    // ---- Hotkeys (spec 06 §3.3) ------------------------------------------------------------------------

    void keys(UiContext& ui, TacticalFight& f) {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
        TacticalBattle& b = *f.battle;
        TacticalUi& u = state();
        const ImGuiIO& io = ImGui::GetIO();
        combatViewKeys(ui, u.cx, u.cy);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) u.aim = Aim::None;   // Esc closes nothing here
        const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeyShift;
        // E presses the fourth button whatever it says: Begin before the battle begins (spec 06 §3.3).
        if (!u.begun) {
            if (plain && ImGui::IsKeyPressed(ImGuiKey_E, false)) begin(b);
            return;
        }
        if (animating()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) skipAnimation();
            return;
        }
        const game::EmpireId side = b.phaseEmpire();
        if (!side.valid() || u.resolved) return;
        // Alt+1..9 / Ctrl+1..9: leader / member of a group; Alt or Ctrl+0 clears the marks.
        for (int n = 0; n <= 9; ++n) {
            if (!ImGui::IsKeyPressed(ImGuiKey(int(ImGuiKey_0) + n), false) || !(io.KeyAlt || io.KeyCtrl)) continue;
            if (!ownSelected(b, u)) continue;
            if (n == 0) {
                clearGroup(f);
            } else if (io.KeyAlt) {
                // A new leader, or the leader of that number, then picks its formation.
                u.ordersGroup = n;
                if (leaderMayPick(ui, b, u.selected, n)) u.picker = Picker::Formation;
            } else {
                TacticalOrder o{OK::SetMember, side, u.selected};
                o.group = n;
                submit(f, o);
            }
        }
        if (plain && ImGui::IsKeyPressed(ImGuiKey_L, false) && ownSelected(b, u)) ui.open(ScreenId::TacticalLaunch);
        if (plain && ImGui::IsKeyPressed(ImGuiKey_T, false)) dropTroops(f);
        // R and C arm the aim even with nothing selected: the click on the target is refused then (spec 06 §1.10.2).
        if (plain && ImGui::IsKeyPressed(ImGuiKey_R, false)) u.aim = Aim::Ram;
        if (plain && ImGui::IsKeyPressed(ImGuiKey_C, false)) u.aim = Aim::Capture;
        if (plain && ImGui::IsKeyPressed(ImGuiKey_E, false)) submit(f, TacticalOrder{OK::EndPhase, side});
        // Space and Ctrl+N: the next piece that can move; Ctrl+B the previous one;
        // Ctrl+F / Ctrl+D the same for firing ("next" walks the list backwards).
        if ((plain && ImGui::IsKeyPressed(ImGuiKey_Space, false)) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false))) pick(b, PieceSearch::Move, false);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) pick(b, PieceSearch::Move, true);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) pick(b, PieceSearch::Fire, false);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) pick(b, PieceSearch::Fire, true);
        // Shift+A ticks the intact weapons that are ready, Shift+C unticks all (spec 06 §1.10.1).
        if (io.KeyShift && ownSelected(b, u) && (ImGui::IsKeyPressed(ImGuiKey_A, false) || ImGui::IsKeyPressed(ImGuiKey_C, false))) {
            std::vector<char>& ticks = ticksFor(b);
            const std::vector<char> was = ticks;
            if (ImGui::IsKeyPressed(ImGuiKey_A, false)) ticks = readyTicks(b.pieces()[size_t(u.selected)]);
            else std::fill(ticks.begin(), ticks.end(), 0);
            if (ticks != was) tacticalOrderLog().emplace_back(learn::battleOrderId(OK::ToggleWeapon));   // for the lessons
        }
    }

    // The next (or previous, `forward`) piece of the side in phase, by the search of spec 06 §3.3.
    void pick(const TacticalBattle& b, PieceSearch kind, bool forward) {
        TacticalUi& u = state();
        const int next = nextTacticalPiece(b.pieces(), b.phaseEmpire(), u.selected, kind, forward);
        if (next < 0) return;
        selectPiece(b, next);
        centreOn(b, next);
    }

    CombatPlayback playback_;
    const game::CombatRecord* record_ = nullptr;
    size_t events_ = 0, pieces_ = 0;
    CombatView view_;
    ImVec2 viewSize_{0, 0};
    size_t groundsSeen_ = 0;   // ground combats of the record already shown
    size_t eventsSeen_ = 0;    // playback events the window has passed (passEvents)
    int weaponsOf_ = -1;       // the piece whose weapons the grid shows
    int weaponTop_ = 0;        // its first row shown
};

// ---- Tactical Combat Orders (spec 06 §1.10.2) ------------------------------------------------------------------

// A 326x350 modal menu without border or title bar: 11 stacked 306x30
// buttons with no gap, from (10,10). A click closes it, and only then does
// the order run: the items that need a choice open their pickers as windows
// of their own (TacticalCombatScreen::pickers).
class TacticalOrdersScreen final : public Screen {
public:
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        TacticalFight* f = ui.session.tactical();
        if (!f || !f->battle) return false;
        TacticalBattle& b = *f->battle;
        TacticalUi& u = stateFor(f);
        const game::EmpireId side = b.phaseEmpire();
        const Vec2 size{326, 350};
        const Vec2 min{(frameW() - size.x) * 0.5f, (frameH() - size.y) * 0.5f};
        ImGui::SetNextWindowPos(ui.at(min), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        std::function<void()> chosen;   // runs once the menu has gone
        bool close = false;
        const bool open = ImGui::Begin("Tactical Combat Orders", nullptr,
                                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | kPromptFlags |
                                           ui.windowFlags());
        ImGui::PopStyleVar(2);
        if (open) {
            if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
            ui.tagWindow(ui.at(min), ui.at(min + size));
            drawWindowFrame(ui.painter(), ImGui::GetWindowDrawList(), Rect{min, min + size}, nullptr, 0);
            int row = 0;
            auto item = [&](const char* label, bool enabled, std::function<void()> action) {
                ImGui::SetCursorPos(ImVec2(ui.px(10), ui.px(10 + 30 * float(row++))));
                if (classicButton(ui, label, {306, 30}, 0, false, enabled)) {
                    audio().play("button");
                    chosen = std::move(action);
                    close = true;
                }
            };
            // Every item stays lit (spec 06 §1.10.2, confirmed: binary): an order
            // that cannot be given is refused once chosen, by a message box where the
            // original shows one.
            item("Launch Units", true, [&ui] { ui.open(ScreenId::TacticalLaunch); });
            item("Launch Fighters in Groups", true, [&u] { u.picker = Picker::GroupSize; });
            item("Drop Troops", true, [f] { dropTroops(*f); });
            item("Ram Ship", true, [&u] { u.aim = Aim::Ram; });
            item("Capture Ship", true, [&u] { u.aim = Aim::Capture; });
            item("Resolve Combat", true, [&u] { u.picker = Picker::Resolve; });
            item("Set Group Leader", true, [&u] { u.picker = Picker::LeaderNumber; });
            item("Set Group Member", true, [&u] { u.picker = Picker::MemberNumber; });
            item("Clear Group Assignment", true, [f] { clearGroup(*f); });
            item("Clear All Group Assignments", true, [f, side] { submit(*f, TacticalOrder{OK::ClearAllGroups, side}); });
            item("Cancel", true, [] {});
        }
        ImGui::End();
        if (close && chosen) chosen();
        flush(*f);
        return !close;
    }
};

// ---- Launch \ Recover Units in a battle (L; spec 04 §10.4, spec 06 §1.10.2) -----------------------------------

// The main window's Launch \ Recover Units (launch_window.hpp) in battle
// mode (confirmed: binary): the left list holds only the selected piece, with
// "Can Launch:" and what it may still launch this combat turn of each kind,
// and its units; a click on a unit launches 1, 5, 10 or all of it (Move One
// .. Move All, kept with the empire's options). While this window stays open,
// units of one kind launched from the piece join the group made first in it,
// whatever their design; drones are one per group, and mines join the
// sector's minefield. The right list holds only the groups launched while
// this window has been open; a click on one takes it back aboard (the
// full-movement test of the map does not apply). A refusal shows in a message
// box titled "Launch Warning" or "Recover Warning".
class TacticalLaunchScreen final : public Screen {
public:
    TacticalLaunchScreen() { window_ = ++state().launchWindow; }   // each opening is a new Launch Units session
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        TacticalFight* f = ui.session.tactical();
        if (!f || !f->battle) return false;
        TacticalBattle& b = *f->battle;
        TacticalUi& u = stateFor(f);
        if (!u.unitsStep) u.unitsStep = launchui::pendingStep().value_or(std::min<uint8_t>(ui.options().unitsStep, 3));
        const game::EmpireId side = b.phaseEmpire();
        const TacticalPiece* p = ownSelected(b, u);
        Dialog d(ui, screenTitle(ScreenId::TacticalLaunch), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        const game::GameState& s = b.state();
        launchui::Lists lists;
        lists.carriersCaption = "Units Aboard";
        lists.groupsCaption = "Launched";
        lists.noCarriers = "Select one of your ships or planets first.";
        lists.noGroups = "None launched yet.";
        std::vector<int> groups;
        if (p) {
            launchui::CarrierRow row;
            row.name = p->name;
            row.picture = picture(ui, s, *p);
            static constexpr std::array<const char*, 4> kKinds{"Fighters", "Satellites", "Mines", "Drones"};
            static constexpr std::array<size_t, 4> kLeft{0, 1, 3, 2};   // TacticalPiece::launchLeft: fighters, satellites, drones, mines
            for (size_t k = 0; k < kKinds.size(); ++k) row.canLaunch[k] = launchui::canLaunchLine(p->launchLeft[kLeft[k]], 0, kKinds[k], true);
            for (const game::UnitStack& st : p->cargo)
                if (st.design.index() < s.designs.size()) row.units.push_back({st.design, s.design(st.design).name, unitPicture(ui, s, st.design), st.count});
            row.selected = true;
            lists.carriers.push_back(std::move(row));
            for (size_t i = 0; i < b.pieces().size(); ++i) {
                const TacticalPiece& g = b.pieces()[i];
                if (!g.alive || g.kind != PieceKind::UnitGroup || g.carrier != u.selected || g.launchWindow != window_) continue;
                launchui::GroupRow gr;
                gr.name = g.name;
                gr.picture = picture(ui, s, g);
                gr.detail = std::format("{} unit{}", g.count, g.count == 1 ? "" : "s");
                lists.groups.push_back(std::move(gr));
                groups.push_back(static_cast<int>(i));
            }
        }
        const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f;
        const launchui::Click click = launchui::drawLists(ui, lists, ImVec2(ImGui::GetContentRegionAvail().x, h));
        if (!u.message.empty()) ImGui::TextColored(ImVec4(1, 0.72f, 0.45f, 1), "%s", u.message.c_str());
        d.beginButtons();
        uint8_t step = *u.unitsStep;
        if (launchui::stepRadios(d, step)) {
            u.unitsStep = step;
            // Kept with the empire's options when a command can be given now, else the next window on the map keeps it.
            game::InterfaceOptions o = ui.options();
            o.unitsStep = step;
            if (ui.setOptions(o)) launchui::pendingStep().reset();
            else launchui::pendingStep() = step;
        }
        d.spacer();
        const bool close = d.close();
        notice_.draw(ui);
        if (p && click.launch && click.launch->second < p->cargo.size()) {
            const game::UnitStack& st = p->cargo[click.launch->second];
            TacticalOrder o{OK::Launch, side, u.selected};
            o.design = st.design;
            o.count = static_cast<int>(launchui::stepCount(*u.unitsStep, st.count));
            o.group = window_;
            if (std::string why = b.check(o); !why.empty()) notice_.open(why, "Launch Warning");
            else submit(*f, o);
        }
        std::optional<size_t> back = click.recover;
        if (click.recoverDesign) back = click.recoverDesign->first;
        if (p && back && *back < groups.size()) {
            TacticalOrder o{OK::Recover, side, u.selected, groups[*back]};
            o.group = window_;
            if (std::string why = b.check(o); !why.empty()) notice_.open(why, "Recover Warning");
            else submit(*f, o);
        }
        flush(*f);
        if (close) return false;
        return d.keepOpen();
    }

private:
    static Sprite unitPicture(UiContext& ui, const game::GameState& s, game::DesignId d) {
        const game::Design& design = s.design(d);
        const std::string style = design.owner.valid() && design.owner.index() < s.empires.size() ? s.empire(design.owner).race.style : std::string{};
        return ui.art.designMini(style, ui.rules().hull(design.hull), design.picture);
    }
    static Sprite picture(UiContext& ui, const game::GameState& s, const TacticalPiece& p) {
        if (p.design.valid() && p.design.index() < s.designs.size()) return unitPicture(ui, s, p.design);
        if (p.planet.valid() && p.planet.index() < s.galaxy.objects.size()) return objectSprite(ui, s.galaxy.object(p.planet));
        return {};
    }

    int window_ = 0;
    NoticePopup notice_;
};

// ---- Combat Options (spec 06 §1.10.3) ---------------------------------------------------------------------------

// Per computer, shared with the Options window where the rows are the same;
// Stop Combat only in the simulator. "Options In Use" at (15,38) over the
// list at (15,56), 560 x 400 (spec 06 §1.10.3); its headings and check boxes
// as Combat Replay Options shows them (spec 07 session 5; inferred: the same
// rows in this window).
class TacticalOptionsScreen final : public Screen {
public:
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        Dialog d(ui, screenTitle(ScreenId::TacticalOptions), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        ClassicSettings& prefs = settings();
        bool changed = false;
        ImGui::SetCursorScreenPos(d.at({15, 38}));
        ImGui::TextColored(kLabelBlue, "Options In Use");
        ImGui::SetCursorScreenPos(d.at({15, 56}));
        beginList(ui, "##options", ui.size({560, 400}), 18);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0));
        heading(ui, "Animation");
        changed |= checkRow(ui, "Animate ship movement in combat", &prefs.animateCombatMovement);
        heading(ui, "Sound");
        changed |= checkRow(ui, "Sound On", &prefs.soundOn);
        // Lit only when music is on and Settings.txt `Allow CD Music` allows it (spec 06 §1.10.3).
        bool music = musicLampLit(prefs, musicAllowed(ui.rules().data().settings));
        if (checkRow(ui, "Music On", &music)) {
            prefs.musicOn = music;
            changed = true;
        }
        heading(ui, "Tactical Combat");
        changed |= checkRow(ui, "Fast Tactical Combat", &prefs.fastTacticalCombat);
        changed |= checkRow(ui, "Show Group Identifiers", &prefs.showGroupIdentifiers);
        changed |= checkRow(ui, "Show Viewing Rectangle on Map", &prefs.showViewingRectangle);
        changed |= checkRow(ui, "Center Map on Current Ship", &prefs.centerOnCurrentShip);
        changed |= checkRow(ui, "Show Weapon To Hit Chances", &prefs.showToHitChances);
        changed |= checkRow(ui, "Show Grid", &prefs.tacticalGrid);
        ImGui::PopStyleVar();
        endList(ui);
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

// A report window built like the object reports: a 290x361 Detail picture
// (the 128x128 picture at the top left with the owner's flag on it, the name
// at the top from x 120, or right-aligned 10 px from the picture's right edge
// when too long; labels in #7D9FFF at x 130 from y 20, 30 px apart, each value
// in white at x 140, 15 px under its label) and the object's tabs along the
// bottom: a ship or base Detail, Comps, Cargo (with cargo space) and Ability;
// a planet Detail, Facil, Cargo (likewise) and Ability. A unit group has no
// tabs: its page is cut to 249 px and its units are listed in a 108 px grid at
// y 263 of the window. A seeker has no tabs and shows the picture of the
// weapon that launched it. A neutral obstacle opens its ordinary object
// report. In a simulation the owner's flag is the side's numbered box.
// The report window's pages: 290x327 at (10,10) (spec 06 §7 Q87, confirmed: binary).
constexpr float kReportPageH = 327;

class CombatPieceReportScreen final : public Screen {
public:
    explicit CombatPieceReportScreen(int piece) : piece_(piece) {}
    bool modal() const override { return true; }

    // A borderless 310x420 report window, as every report opened on its own
    // (spec 06 §1.10.1, §7 Q78, Q87, confirmed: binary): the pages, 290x327
    // areas at (10,10) with or without tabs, end 3 px above the four 72x30
    // tabs at (10,340); a 153x30 Close button centred under them at (79,380).
    bool draw(UiContext& ui) override {
        const TacticalFight* f = ui.session.tactical();
        if (!f || !f->battle || piece_ < 0 || size_t(piece_) >= f->battle->pieces().size()) return false;
        const TacticalBattle& b = *f->battle;
        const game::GameState& s = b.state();
        const TacticalPiece& p = b.pieces()[size_t(piece_)];
        const Vec2 size{310, 420};
        const Vec2 min{(frameW() - size.x) * 0.5f, (frameH() - size.y) * 0.5f};
        ImGui::SetNextWindowPos(ui.at(min), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const bool open = ImGui::Begin("Combat Piece Report", nullptr,
                                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | kPromptFlags |
                                           ui.windowFlags());
        ImGui::PopStyleVar(2);
        bool keep = true;
        if (open) {
            if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
            ui.tagWindow(ui.at(min), ui.at(min + size));
            drawWindowFrame(ui.painter(), ImGui::GetWindowDrawList(), Rect{min, min + size}, nullptr, 0);
            const ImVec2 page = ui.at(min + Vec2{10, 10});
            if (p.kind == PieceKind::Obstacle) {
                // The object's ordinary report (stars, warp points, comets, empty planets).
                ImGui::SetCursorScreenPos(page);
                ImGui::BeginChild("##page", ui.size({290, kReportPageH}));
                if (p.planet.valid() && p.planet.index() < s.galaxy.objects.size()) objectReport(ui, p.planet, &s);
                ImGui::EndChild();
            } else {
                const bool group = p.kind == PieceKind::UnitGroup;
                const bool seeker = p.kind == PieceKind::Seeker;
                const bool planet = p.kind == PieceKind::Planet;
                const bool tabs = !group && !seeker;
                if (!tabs) tab_ = ReportTab::Detail;
                // A unit group's page is cut to 249 px, its unit grid (108 px) at y 263 of the window.
                const float detailH = group ? 249.0f : kReportPageH;
                ImGui::SetCursorScreenPos(page);
                ImGui::BeginChild("##page", ui.size({290, detailH}), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
                switch (tab_) {
                    case ReportTab::Detail: detail(ui, b, p); break;
                    case ReportTab::Components: components(ui, s, p); break;
                    case ReportTab::Facilities: facilities(ui, s, p); break;
                    case ReportTab::Cargo: cargo(ui, s, p); break;
                    case ReportTab::Abilities: abilities(ui, s, p); break;
                }
                ImGui::EndChild();
                if (group) unitGrid(ui, s, p, ui.at(min + Vec2{10, 10 + 253}), 108);
                if (tabs) {
                    ImGui::SetCursorScreenPos(ui.at(min + Vec2{10, 340}));
                    tab_ = reportTabs(ui, tab_, planet, cargoSpace(ui.rules(), s, p) > 0);
                }
            }
            ImGui::SetCursorScreenPos(ui.at(min + Vec2{79, 380}));
            if (classicButton(ui, "Close", {153, 30})) keep = false;
            // Esc closes a report window (spec 06 §3.4).
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) keep = false;
        }
        ImGui::End();
        item_.draw(ui);
        return keep;
    }

private:
    static int64_t cargoSpace(const game::Rules& r, const game::GameState& s, const TacticalPiece& p) {
        if (p.kind == PieceKind::Vehicle && p.vehicle.valid())
            if (const game::Vehicle* v = s.vehicle(p.vehicle)) return game::vehicleCargoCapacity(r, s, *v);
        if (p.kind == PieceKind::Planet && p.planet.valid() && p.planet.index() < s.galaxy.objects.size())
            if (const game::Colony* c = s.colony(p.planet)) return game::colonyCargoCapacity(r, s, *c);
        return 0;
    }

    // The Detail picture, 290x361, is drawn 10 px right and 10 px down of the
    // page's corner and cut off by the page: its last 10 columns and its
    // bottom rows never show (spec 06 §7 Q87, confirmed: binary).
    void detail(UiContext& ui, const TacticalBattle& b, const TacticalPiece& p) {
        const game::GameState& s = b.state();
        const game::Rules& r = ui.rules();
        const ImVec2 corner = ImGui::GetCursorScreenPos();
        const ImVec2 o{corner.x + ui.px(10), corner.y + ui.px(10)};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // The picture: the hull's portrait, the planet's, or the launching weapon's for a seeker.
        Sprite pic;
        const std::string style = p.owner.valid() && p.owner.index() < s.empires.size() ? s.empire(p.owner).race.style : std::string{};
        if (p.kind == PieceKind::Seeker) {
            if (p.seekComponent >= 0 && size_t(p.seekComponent) < r.data().components.size()) pic = ui.art.component(r.component(uint32_t(p.seekComponent)).picture);
        } else if (p.design.valid() && p.design.index() < s.designs.size()) {
            const game::Design& design = s.design(p.design);
            const ruleset::VehicleSize& hull = r.hull(design.hull);
            pic = ui.art.designPortrait(style, hull, design.picture);
            if (!pic) pic = ui.art.designMini(style, hull, design.picture);
        } else if (p.planet.valid() && p.planet.index() < s.galaxy.objects.size()) {
            const game::SpaceObject& obj = s.galaxy.object(p.planet);
            if (obj.sectorType < r.data().sectorObjectTypes.size()) pic = ui.art.planetPortrait(r.data().sectorObjectTypes[obj.sectorType].picture);
            if (!pic) pic = objectSprite(ui, obj);
        }
        ImGui::SetCursorScreenPos(o);
        image(ui, pic, {128, 128});
        // The owner's flag on the picture (a simulation's side box).
        ImGui::SetCursorScreenPos(o);
        ownerMark(ui, s, p.owner, {26, 18}, false);
        // The name at the top from x 120; right-aligned 10 px from the right edge when too long.
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        const float w = ImGui::CalcTextSize(p.name.c_str()).x;
        const float x = w > ui.px(290 - 10 - 120) ? o.x + ui.px(290 - 10) - w : o.x + ui.px(120);
        dl->AddText({x, o.y}, IM_COL32_WHITE, p.name.c_str());
        ImGui::PopFont();
        // The lines: labels at x 130 from y 20 every 30 px, values at x 140 15 px under them; Conditions at y 230.
        float y = 20;
        for (const auto& [label, value] : pieceReportLines(r, s, b.pieces(), piece_)) {
            if (label == "Conditions") y = 230;
            dl->AddText({o.x + ui.px(130), o.y + ui.px(y)}, ImGui::ColorConvertFloat4ToU32(kLabelBlue), label.c_str());
            dl->AddText({o.x + ui.px(140), o.y + ui.px(y + 15)}, IM_COL32_WHITE, value.c_str());
            y += 30;
        }
        ImGui::SetCursorScreenPos({o.x, o.y + ui.px(y + 30)});
        ImGui::Dummy({1, 1});
    }

    // Comps: the design's components, those destroyed in red; a right-click
    // opens the component's report with its mount (spec 06 §1.4, §7 Q105).
    void components(UiContext& ui, const game::GameState& s, const TacticalPiece& p) {
        const game::Rules& r = ui.rules();
        if (!p.design.valid() || p.design.index() >= s.designs.size()) return;
        const game::Design& d = s.design(p.design);
        for (size_t e = 0; e < d.entries.size(); ++e) {
            const ruleset::Component& c = r.component(d.entries[e].component);
            const bool intact = e >= p.intact.size() || p.intact[e] != 0;
            ImGui::PushID(int(e));
            ImGui::BeginGroup();
            image(ui, ui.art.component(c.picture), {24, 24}, intact ? Color{1, 1, 1, 1} : Color{1, 0.3f, 0.3f, 0.8f});
            ImGui::SameLine();
            std::string label = c.name;
            if (d.entries[e].mount >= 0 && size_t(d.entries[e].mount) < r.data().weaponMounts.size())
                label = r.data().weaponMounts[size_t(d.entries[e].mount)].shortName + " " + label;
            if (intact) ImGui::TextUnformatted(label.c_str());
            else ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s (destroyed)", label.c_str());
            ImGui::EndGroup();
            ImGui::PopID();
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                item_.open(ItemRef{ItemRef::Kind::Component, d.entries[e].component, d.entries[e].mount});
        }
    }

    // Facil: the colony's facilities as the battle began.
    void facilities(UiContext& ui, const game::GameState& s, const TacticalPiece& p) {
        const game::Colony* c = p.planet.valid() && p.planet.index() < s.galaxy.objects.size() ? s.colony(p.planet) : nullptr;
        if (!c) {
            dimText("Not colonized");
            return;
        }
        for (size_t i = 0; i < c->facilities.size(); ++i) {
            const uint32_t f = c->facilities[i];
            ImGui::PushID(int(i));
            ImGui::BeginGroup();
            image(ui, ui.art.facility(ui.rules().facility(f).picture), {24, 24});
            ImGui::SameLine();
            ImGui::TextUnformatted(ui.rules().facility(f).name.c_str());
            ImGui::EndGroup();
            ImGui::PopID();
            // A right-click opens the facility's report (spec 06 §1.4, §7 Q105).
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) item_.open(ItemRef{ItemRef::Kind::Facility, f});
        }
    }

    // Cargo: the units it carries now.
    void cargo(UiContext& ui, const game::GameState& s, const TacticalPiece& p) {
        game::Cargo c;
        c.units = p.cargo;
        labelValue(ui, "Capacity", std::format("{} / {} kT", game::cargoSpaceUsed(ui.rules(), s, c), cargoSpace(ui.rules(), s, p)));
        if (p.cargo.empty()) dimText("Empty");
        for (const game::UnitStack& u : p.cargo)
            ImGui::Text("%d x %s", u.count, u.design.index() < s.designs.size() ? s.design(u.design).name.c_str() : "?");
    }

    // Ability: a ship's or base's hull entries, not its components; a
    // planet's own abilities only; each entry's Descr, with the lamp of the
    // main window's page (spec 06 §1.4, §1.10.1, §7 Q78, Q106).
    void abilities(UiContext& ui, const game::GameState& s, const TacticalPiece& p) { abilityPage(ui, pieceReportAbilities(ui.rules(), s, p)); }

    // A unit group's units in a 108 px grid under its cut Detail page.
    void unitGrid(UiContext& ui, const game::GameState& s, const TacticalPiece& p, ImVec2 at, float height) {
        ImGui::SetCursorScreenPos(at);
        ImGui::BeginChild("##units", ui.size({290, std::max(40.0f, height)}), ImGuiChildFlags_Borders);
        const std::string style = p.owner.valid() && p.owner.index() < s.empires.size() ? s.empire(p.owner).race.style : std::string{};
        int col = 0;
        for (const game::UnitStack& st : p.units) {
            if (st.design.index() >= s.designs.size()) continue;
            if (col > 0) ImGui::SameLine(0, ui.px(4));
            ImGui::BeginGroup();
            image(ui, ui.art.designMini(style, ui.rules().hull(s.design(st.design).hull), s.design(st.design).picture), {36, 36});
            ImGui::Text("%d", st.count);
            ImGui::EndGroup();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.design(st.design).name.c_str());
            col = (col + 1) % 6;
        }
        ImGui::EndChild();
    }

    int piece_ = -1;
    ItemReportPopup item_;   // a facility's or component's report, from a right-click on its page
    ReportTab tab_ = ReportTab::Detail;
};

} // namespace

std::unique_ptr<Screen> makeTacticalCombat(const ScreenArgs&) { return std::make_unique<TacticalCombatScreen>(); }
std::unique_ptr<Screen> makeTacticalOrders(const ScreenArgs&) { return std::make_unique<TacticalOrdersScreen>(); }
std::unique_ptr<Screen> makeTacticalOptions(const ScreenArgs&) { return std::make_unique<TacticalOptionsScreen>(); }
std::unique_ptr<Screen> makeTacticalLaunch(const ScreenArgs&) { return std::make_unique<TacticalLaunchScreen>(); }
std::unique_ptr<Screen> makeCombatPieceReport(const ScreenArgs& args) { return std::make_unique<CombatPieceReportScreen>(args.index); }

} // namespace opense4::client::classic
