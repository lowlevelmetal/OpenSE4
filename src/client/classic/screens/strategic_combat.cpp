// Strategic Combat and Ground Combat (docs/spec/06 §1.10.5, §1.10.6; the
// rules are docs/spec/04 §2, §3, §11, §13).
//
// Strategic Combat shows a battle fought by the strategies: a list of each
// empire's forces per hull on the left (current and lost), a small map of
// coloured squares on the right, and "Combat Turn N" in the title strip once
// it runs. It plays no sounds. It is opened
//   - when a battle of a local or hotseat game stops the engine call
//     (ScreenArgs::index kStrategicQuestion; game/turn.hpp): set up, before
//     combat turn 1. When tactical combat is offered it asks first (Strategic
//     and Tactical buttons; Tactical opens the Tactical Combat window
//     instead), otherwise it has Begin and Close. Strategic or Begin hands the
//     battle (a stepped combat::TacticalBattle on the question's copy of the
//     game, without player sides) to the session and fights it here;
//   - for the session's fight without player sides (index -1): such a game
//     battle, or a simulation the strategies fight;
//   - for a battle of the game (index into GameState::combats) that the host
//     of a network or PBEM game fought, which is played back.
// A battle fought here advances one empire phase per displayed frame, the
// map drawn after the steps of each phase and the forces list and "Combat
// Turn N" after each combat turn, with no added delay; the battle's results
// come when Close answers the question. A computer side's landing opens
// Ground Combat at once, the playback stopped at the landing, and the battle
// waits until that window is closed.
//
// Ground Combat shows a ground fight: the planet, its facilities, the
// defending troops and militia and the attacking troops. Begin fights it
// round by round from its record (game::GroundCombat): after every round the
// round counter and the counts change, an explosion plays over the planet for
// 0.9 s and a boom sounds. "Victorious!" marks the winner; Close is dim until
// then. It opens by itself when troops land in the Tactical Combat window, and
// when a computer side lands troops during a Strategic Combat battle, unless
// both empires are computer-controlled (the simulator always shows it); and
// for the colony owner's end-of-turn fight that stops a turn-based game.

#include "client/audio.hpp"
#include "client/classic/replay.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/combat_logic.hpp"
#include "client/classic/screens/combat_map.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include "game/combat.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"
#include "game/tactical.hpp"

#include <algorithm>
#include <format>
#include <random>

namespace opense4::client::classic {

namespace {

using PieceKind = game::CombatPiece::Kind;

// The Ground Combat window's explosion: 8 frames of 0.1 s, then 0.1 s wiped,
// 0.9 s a round (spec 06 §1.10.6, confirmed: binary).
constexpr float kExplosionFrame = 0.1f;
constexpr int kExplosionFrames = 8;
constexpr float kGroundRound = kExplosionFrame * float(kExplosionFrames + 1);

// The Large dialog's top-left corner, for the positions spec 06 gives in window pixels.
Vec2 largeOrigin() { return {(kFrameW - 780.0f) * 0.5f, (kFrameH - 475.0f) * 0.5f}; }

// Ground Combat windows open now: a Strategic Combat battle waits while one is.
int gGroundWindows = 0;

// The battle a window shows: a battle of the game, the session's fight, or the question's.
struct BattleSource {
    const game::GameState* state = nullptr;
    const game::CombatRecord* record = nullptr;
    TacticalFight* fight = nullptr;
    game::combat::TacticalBattle* live = nullptr;   // a stepped battle still being fought
    int index = -1;                                  // into GameState::combats (-1: the fight)
};

BattleSource gameBattle(UiContext& ui, int index) {
    BattleSource out;
    const auto& combats = ui.state().combats;
    if (index < 0 || size_t(index) >= combats.size()) return out;
    out.state = &ui.state();
    out.record = &combats[size_t(index)];
    out.index = index;
    return out;
}

BattleSource fightBattle(UiContext& ui) {
    BattleSource out;
    TacticalFight* f = ui.session.tactical();
    if (!f || !f->battle || !f->battle->started()) return out;
    out.fight = f;
    out.state = &f->battle->state();
    out.record = &f->battle->record();
    if (f->battle->setup().stepped && !f->battle->applied()) out.live = f->battle.get();
    return out;
}

std::string systemName(const game::GameState& s, game::Location where) {
    return where.system.valid() && where.system.index() < s.galaxy.systems.size() ? s.galaxy.system(where.system).name : std::string("?");
}

bool computerControlled(const game::GameState& s, game::EmpireId e) {
    return e.valid() && e.index() < s.empires.size() && s.empire(e).kind != game::PlayerKind::Human;
}

void flagAndName(UiContext& ui, const game::GameState& s, game::EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size()) {
        dimText("Nobody");
        return;
    }
    if (Sprite flag = ui.art.flag(s.empire(e).race.style, false)) {
        image(ui, flag, {20, 14});
        ImGui::SameLine(0, ui.px(5));
    }
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(s, e)), "%s", s.empire(e).name.c_str());
}

// A title strip label in #7D9FFF at x, its value right after it in white (spec 06 §1.10.5).
void titleLabel(UiContext& ui, Dialog& d, float x, std::string_view label, const std::string& value) {
    d.titleText(x, ImGui::ColorConvertFloat4ToU32(kLabelBlue), label);
    const float width = ImGui::CalcTextSize(label.data(), label.data() + label.size()).x / std::max(0.01f, ui.px(1));
    d.titleText(x + width + 5, IM_COL32_WHITE, value);
}

// ---- Strategic Combat ------------------------------------------------------------------------------------

class StrategicCombatScreen final : public Screen {
public:
    explicit StrategicCombatScreen(int index) : index_(index) {}
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        BattleSource src;
        bool asks = false;   // the question form: Strategic and Tactical
        if (index_ == kStrategicQuestion) {
            TacticalFight* f = ui.session.tactical();
            if (f && f->kind == TacticalFight::Kind::Game && f->players.empty()) {
                index_ = -1;   // begun: the fight is the session's to show
            } else if (!ui.session.battleQuestion() || ui.session.battleQuestion()->kind == game::BattleQuestion::Kind::Ground) {
                return false;
            } else {
                if (!preview(ui)) return false;
                asks = ui.session.battleQuestion()->kind == game::BattleQuestion::Kind::Choose;
                src.state = &preview_->state();
                src.record = &preview_->record();
            }
        }
        if (index_ != kStrategicQuestion) src = index_ < 0 ? fightBattle(ui) : gameBattle(ui, index_);
        if (!src.record) return false;
        // A fight the strategies fought at once (not stepped): its record is complete.
        if (index_ < 0 && !begun_ && src.fight && !src.live && src.fight->battle->finished()) begun_ = true;
        sync(*src.record);
        if (!forces_.ready()) setupForces(ui, src);
        update(ui, src);

        Dialog d(ui, screenTitle(ScreenId::StrategicCombat), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Location where = src.record->location;
        titleLabel(ui, d, 270, "System", systemName(*src.state, where));
        titleLabel(ui, d, 460, "Coordinates", std::format("({},{})", where.sector.x, where.sector.y));
        if (begun_) titleLabel(ui, d, 630, "Combat Turn", std::to_string(std::max(1, turn_)));
        d.beginContent();
        forcesList(ui, *src.state);
        drawMap(ui, *src.state, *src.record);

        d.beginButtons();
        const bool over = begun_ && done(src);
        if (asks) {
            // T and S pick them (spec 06 §3.4).
            const std::optional<bool> key = tacticalStrategicKey();
            const bool strategic = d.button("Strategic") || key == false;
            const bool tactical = d.button("Tactical") || key == true;
            if (strategic) start(ui);
            else if (tactical) return answerTactical(ui);
        } else if (d.button("Begin", !begun_)) {
            if (index_ == kStrategicQuestion) start(ui);
            else begun_ = true;
        }
        if (d.close(over) || !d.keepOpen()) {
            // A simulation is dropped; a game battle fought here answers its question.
            if (src.fight) ui.session.endTactical();
            return false;
        }
        return true;
    }

private:
    // The battle that waits to be shown, set up on the question's copy of the game, before combat turn 1.
    bool preview(UiContext& ui) {
        const game::BattleQuestion& q = *ui.session.battleQuestion();
        const size_t key = q.index * 100003u + size_t(q.where.system.value) * 1009u + size_t(q.where.sector.x * 13 + q.where.sector.y);
        if (preview_ && previewKey_ == key) return true;
        previewKey_ = key;
        begun_ = false;
        record_ = nullptr;
        forces_.reset();
        game::combat::TacticalBattle::Setup setup{q.where, q.entering, {}, std::nullopt, std::nullopt, std::nullopt, q.check};
        setup.stepped = true;
        preview_ = std::make_unique<game::combat::TacticalBattle>(ui.rules(), *q.state, std::move(setup));
        if (!preview_->started()) {
            preview_.reset();
            ui.session.answerBattle(game::BattleAnswer{});
            return false;
        }
        return true;
    }

    // Strategic (or Begin): the strategies fight it here, every side handed to them (spec 06 §1.10.5).
    void start(UiContext& ui) {
        if (!preview_) return;
        TacticalFight f;
        f.kind = TacticalFight::Kind::Game;
        f.battle = std::move(preview_);
        f.title = screenTitle(ScreenId::StrategicCombat);
        ui.session.startTactical(std::move(f));
        index_ = -1;
        begun_ = true;
    }

    bool answerTactical(UiContext& ui) {
        const game::BattleQuestion& q = *ui.session.battleQuestion();
        auto battle = std::make_unique<game::combat::TacticalBattle>(
            ui.rules(), *q.state, game::combat::TacticalBattle::Setup{q.where, q.entering, q.humans, std::nullopt, std::nullopt, std::nullopt, q.check});
        if (!battle->started()) {
            ui.session.answerBattle(game::BattleAnswer{});
            return false;
        }
        TacticalFight f;
        f.kind = TacticalFight::Kind::Game;
        f.battle = std::move(battle);
        f.players = q.humans;
        f.title = screenTitle(ScreenId::TacticalCombat);
        ui.session.startTactical(std::move(f));
        ui.open(ScreenId::TacticalCombat);
        return false;
    }

    // The playback follows the record; a record that grew (the battle stepped) or moved is taken again.
    void sync(const game::CombatRecord& rec) {
        if (&rec == record_ && rec.events.size() == events_) return;
        const size_t cursor = record_ ? playback_.cursor() : 0;
        playback_ = CombatPlayback(rec);
        playback_.seekEvent(std::min(cursor, playback_.eventCount()));
        if (!record_) shown_.clear();
        record_ = &rec;
        events_ = rec.events.size();
        shown_.resize(rec.grounds.size(), 0);
    }

    // The forces list's rows, made once from the battle as set up.
    void setupForces(UiContext& ui, const BattleSource& src) {
        if (src.fight || preview_) {
            const game::combat::TacticalBattle& b = src.fight ? *src.fight->battle : *preview_;
            if (!b.finished() || b.setup().stepped) {
                forces_.setup(ui.rules(), *src.state, b.pieces());
                return;
            }
        }
        const CombatPlayback start(*src.record);
        forces_.setup(ui.rules(), *src.state, *src.record, start.pieces());
    }

    void recount(UiContext& ui, const BattleSource& src) {
        if (src.live) forces_.count(ui.rules(), *src.state, src.live->pieces());
        else forces_.count(ui.rules(), *src.state, *src.record, playback_.pieces());
    }

    bool done(const BattleSource& src) const {
        const bool pending = std::any_of(shown_.begin(), shown_.end(), [](uint8_t v) { return v == 0; });
        if (src.live) return src.live->finished() && playback_.atEnd() && !pending;
        return playback_.atEnd();
    }

    // Once begun: one empire phase per frame for a battle fought here, one
    // combat turn per frame for one played back; nothing while a Ground
    // Combat window is open. No delay is added (spec 06 §1.10.5 "Pace").
    void update(UiContext& ui, BattleSource& src) {
        if (!begun_ || gGroundWindows > 0) return;
        if (src.live && playback_.atEnd() && !src.live->finished()) {
            const int before = src.live->round();
            src.live->step();
            sync(*src.record);
            if (src.live->round() != before || src.live->finished()) {
                countAfter_ = true;
                turn_ = src.live->round();
            }
        }
        size_t target = playback_.eventCount();
        if (!src.live) {
            const int next = std::max(0, playback_.round()) + 1;
            target = next > playback_.roundCount() ? playback_.eventCount() : playback_.roundStart(next + 1);
        }
        // A landing on the way: the playback stops just after it and Ground Combat opens.
        const game::CombatRecord& rec = *src.record;
        const bool simulation = src.fight && src.fight->kind == TacticalFight::Kind::Simulation;
        for (size_t k = 0; k < rec.grounds.size() && k < shown_.size(); ++k) {
            const game::GroundCombat& g = rec.grounds[k];
            if (shown_[k]) continue;
            const size_t at = std::min<size_t>(size_t(g.event) + 1, playback_.eventCount());
            if (at > target) break;
            shown_[k] = 1;
            playback_.seekEvent(std::max(playback_.cursor(), at));
            // Shown unless both empires are computer-controlled; in the simulator always (spec 06 §1.10.6).
            if (!simulation && computerControlled(*src.state, g.attacker) && computerControlled(*src.state, g.defender)) continue;
            ScreenArgs a;
            a.index = src.index;
            a.sub = int(k);
            ui.open(ScreenId::GroundCombat, std::move(a));
            return;
        }
        if (playback_.cursor() < target) playback_.seekEvent(target);
        if (src.live) {
            if (countAfter_) {
                recount(ui, src);
                countAfter_ = false;
            }
        } else {
            turn_ = std::max(1, playback_.round());
            recount(ui, src);
        }
    }

    // "Combat Forces" (15,38), Current and Lost at +220 and +270 in #7D9FFF; the list at (15,55), 329x400, 18 px rows.
    void forcesList(UiContext& ui, const game::GameState& s) {
        const Vec2 o = largeOrigin();
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{15, 38}));
        ImGui::TextColored(kLabelBlue, "Combat Forces");
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{15 + 220, 38}));
        ImGui::TextColored(kLabelBlue, "Current");
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{15 + 270, 38}));
        ImGui::TextColored(kLabelBlue, "Lost");
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{15, 55}));
        ImGui::BeginChild("##forces", ui.size({329, 400}), ImGuiChildFlags_Borders);
        // 18 px rows: the text drawn in place, then a dummy item that takes the row.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowH = ui.px(18);
        auto text = [&](ImVec2 at, ImU32 color, const std::string& t) { dl->AddText(at, color, t.c_str()); };
        auto row = [&](const std::string& name, int current, int lost) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float y = p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            text({p.x + ui.px(16), y}, IM_COL32_WHITE, name);
            text({p.x + ui.px(218), y}, IM_COL32_WHITE, std::to_string(current));
            text({p.x + ui.px(268), y}, lost > 0 ? IM_COL32(255, 115, 102, 255) : IM_COL32_WHITE, std::to_string(lost));
            ImGui::Dummy(ImVec2(ui.px(300), rowH));
        };
        for (const ForceSide& side : forces_.sides()) {
            flagAndName(ui, s, side.empire);
            for (const ForceRow& r : side.rows) row(r.name, r.current, r.lost);
        }
        ImGui::EndChild();
    }

    // The map at (354,55), 218x191: the whole combat grid in 3 px squares, framed by a #647EC7 line just outside it.
    void drawMap(UiContext& ui, const game::GameState& s, const game::CombatRecord& rec) {
        const Vec2 o = largeOrigin();
        const ImVec2 a = ui.at(o + Vec2{354, 55}), b = ui.at(o + Vec2{354 + 218, 55 + 191});
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(a, b, false);
        dl->AddRectFilled(a, b, IM_COL32_BLACK);
        CombatView v;
        v.cell = ui.px(3);
        v.cx = float(game::combat::kCombatMapWidth) * 0.5f;
        v.cy = float(game::combat::kCombatMapHeight) * 0.5f;
        v.center = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
        const CombatMapPainter paint(ui, s, rec, playback_);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool inside = mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y;
        const std::optional<uint32_t> hovered = paint.squares(dl, v, ui.px(3), inside, mouse);
        dl->PopClipRect();
        const float px = ui.px(1);
        dl->AddRect(ImVec2(a.x - px, a.y - px), ImVec2(b.x + px, b.y + px), imColor(palette::kFrameLight));
        if (hovered && *hovered < playback_.pieces().size()) {
            const CombatPlayback::Piece& p = playback_.pieces()[*hovered];
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(paint.pieceName(*hovered).c_str());
            labelValue(ui, "Owner", p.neutral ? std::string("None") : paint.empireName(p.owner), 70);
            if (rec.pieces[*hovered].kind == PieceKind::UnitGroup)
                labelValue(ui, "Units", std::format("{} of {} left", p.units, rec.pieces[*hovered].count), 70);
            ImGui::EndTooltip();
        }
    }

    int index_ = -1;
    std::unique_ptr<game::combat::TacticalBattle> preview_;   // the question's battle, set up, not begun
    size_t previewKey_ = SIZE_MAX;
    const game::CombatRecord* record_ = nullptr;
    size_t events_ = 0;
    CombatPlayback playback_;
    CombatForces forces_;
    bool begun_ = false;
    bool countAfter_ = false;      // a combat turn ended: recount once the map shows it
    int turn_ = 1;                 // "Combat Turn N"
    std::vector<uint8_t> shown_;   // ground combats already reached
};

// ---- Ground Combat ---------------------------------------------------------------------------------------

class GroundCombatScreen final : public Screen {
public:
    GroundCombatScreen(int index, int sub) : index_(index), sub_(std::max(0, sub)), rng_(std::random_device{}()) { ++gGroundWindows; }
    ~GroundCombatScreen() override { --gGroundWindows; }
    GroundCombatScreen(const GroundCombatScreen&) = delete;
    GroundCombatScreen& operator=(const GroundCombatScreen&) = delete;
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        const game::GameState* state = nullptr;
        const game::GroundCombat* g = nullptr;
        game::Location where;
        if (index_ == kGroundQuestion) {
            const auto& q = ui.session.battleQuestion();
            if (!q || q->kind != game::BattleQuestion::Kind::Ground || !q->ground || !q->state) return false;
            state = q->state.get();
            g = &*q->ground;
            where = q->where;
        } else {
            const BattleSource src = find(ui);
            if (src.record && size_t(sub_) < src.record->grounds.size()) {
                state = src.state;
                g = &src.record->grounds[size_t(sub_)];
                where = src.record->location;
            }
        }
        if (!g || !state) return false;
        advance(ui, *g);
        const bool over = begun_ && round_ >= g->rounds && !exploding_;
        Dialog d(ui, screenTitle(ScreenId::GroundCombat), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = *state;
        d.titleText(270, IM_COL32_WHITE, std::format("System {}", systemName(s, where)));
        d.titleText(450, IM_COL32_WHITE, std::format("Coordinates ({},{})", where.sector.x, where.sector.y));
        // "Ground Combat Turn" at (580,10), "N/M" right-aligned to x 760, drawn from the start (spec 06 §1.10.6).
        d.titleText(580, ImGui::ColorConvertFloat4ToU32(kLabelBlue), "Ground Combat Turn");
        {
            const std::string count = std::format("{}/{}", round_, game::combat::loadSettings(ui.rules()).groundTurns);
            const float width = ImGui::CalcTextSize(count.c_str()).x / std::max(0.01f, ui.px(1));
            d.titleText(760 - width, IM_COL32_WHITE, count);
        }
        d.beginContent();
        drawPlanet(ui, s, *g);
        // The counts after the round shown (before Begin, as the fight starts).
        const game::GroundRound* now = round_ > 0 && size_t(round_) <= g->perRound.size() ? &g->perRound[size_t(round_ - 1)] : nullptr;
        std::vector<int> attackers, defenders;
        for (size_t k = 0; k < g->attackers.size(); ++k)
            attackers.push_back(now && k < now->attackers.size() ? now->attackers[k] : g->attackers[k].count);
        for (size_t k = 0; k < g->defenders.size(); ++k)
            defenders.push_back(now && k < now->defenders.size() ? now->defenders[k] : g->defenders[k].count);
        const int militia = now ? now->militia : g->militia;
        sideRow(ui, s, "Defender", 264, g->defender, g->defenders, defenders, militia);
        sideRow(ui, s, "Attacker", 378, g->attacker, g->attackers, attackers, -1);
        // "Victorious!" in yellow beside the winner's label; nothing after a stalemate.
        if (over) {
            int left = 0;
            for (int n : attackers) left += n;
            const bool defenderWon = !g->captured && left == 0;
            if (defenderWon || g->captured) {
                ImGui::SetCursorScreenPos(ui.at(largeOrigin() + Vec2{190, g->captured ? 378.0f : 264.0f}));
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Victorious!");
            }
        }
        d.beginButtons();
        if (d.button("Begin", !begun_)) {
            begun_ = true;
            nextRound(*g);
        }
        if (d.close(over)) {
            // The colony owner's fight: the engine goes on (the answer only says it was shown).
            if (index_ == kGroundQuestion) ui.session.answerBattle(game::BattleAnswer{});
            return false;
        }
        return d.keepOpen();
    }

private:
    // The battle: the one named; the session's fight; else the game's last battle with a ground combat.
    BattleSource find(UiContext& ui) const {
        if (index_ >= 0) return gameBattle(ui, index_);
        if (BattleSource f = fightBattle(ui); f.record) return f;
        const auto& combats = ui.state().combats;
        for (size_t i = combats.size(); i-- > 0;)
            if (!combats[i].grounds.empty()) return gameBattle(ui, int(i));
        return {};
    }

    // The next round's counts, its explosion over a random spot of the planet picture.
    void nextRound(const game::GroundCombat& g) {
        if (round_ >= g.rounds) return;
        ++round_;
        exploding_ = true;
        elapsed_ = 0.0f;
        std::uniform_int_distribution<int> x(0, 128 - 36), y(0, 128 - 36);
        spot_ = {float(x(rng_)), float(y(rng_))};
    }

    // 0.9 s a round, the Fast Tactical Combat switches notwithstanding; then a boom and the next round.
    void advance(UiContext& ui, const game::GroundCombat& g) {
        if (!begun_ || !exploding_) return;
        elapsed_ += ui.dt;
        if (elapsed_ < kGroundRound) return;
        exploding_ = false;
        audio().play(std::uniform_int_distribution<int>(0, 1)(rng_) == 0 ? "boom1" : "boom2");
        nextRound(g);
    }

    // Picture (12,40), name (160,40), labels at x 170 from y 60 every 30 px with
    // their values under them at x 180; Facilities at (350,60) over its grid.
    void drawPlanet(UiContext& ui, const game::GameState& s, const game::GroundCombat& g) {
        const game::Rules& r = ui.rules();
        const Vec2 o = largeOrigin();
        const bool known = g.planet.valid() && g.planet.index() < s.galaxy.objects.size();
        const game::SpaceObject* obj = known ? &s.galaxy.object(g.planet) : nullptr;
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{12, 40}));
        Sprite pic;
        if (obj && obj->sectorType < r.data().sectorObjectTypes.size()) pic = ui.art.planetPortrait(r.data().sectorObjectTypes[obj->sectorType].picture);
        if (!pic && obj) pic = objectSprite(ui, *obj);
        image(ui, pic, {128, 128});
        // The round's explosion: the 8-frame 36 px strip of General.bmp at y 64, 0.1 s a frame, then wiped.
        if (exploding_) {
            const int frame = int(elapsed_ / kExplosionFrame);
            if (frame < kExplosionFrames)
                if (Sprite boom = ui.art.region("Pictures/Game/General.bmp", frame * 36, 64, 36, 36)) {
                    ImGui::SetCursorScreenPos(ui.at(o + Vec2{12, 40} + spot_));
                    image(ui, boom, {36, 36});
                }
        }
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{160, 40}));
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(obj ? obj->name.c_str() : "Planet");
        ImGui::PopFont();
        const game::Colony* colony = known ? s.colony(g.planet) : nullptr;
        const std::string population = std::format("{}M{}", formatNumber(g.population), colony && !game::breathable(s, *colony) ? " (Domed)" : "");
        const std::array<std::pair<const char*, std::string>, 5> lines{{
            {"Type", obj ? std::format("{} {}", obj->size, obj->surface) : std::string("-")},
            {"Atmosphere", obj ? obj->atmosphere : std::string("-")},
            {"Conditions", obj ? std::string(game::economy::conditionsName(game::economy::conditionsBand(obj->conditions))) : std::string("-")},
            {"Value", obj ? std::format("{}% / {}% / {}%", obj->value[0], obj->value[1], obj->value[2]) : std::string("-")},
            {"Population", population},
        }};
        for (size_t k = 0; k < lines.size(); ++k) {
            const float y = 60.0f + 30.0f * float(k);
            ImGui::SetCursorScreenPos(ui.at(o + Vec2{170, y}));
            ImGui::TextColored(kLabelBlue, "%s", lines[k].first);
            ImGui::SetCursorScreenPos(ui.at(o + Vec2{180, y + 14}));
            ImGui::TextUnformatted(lines[k].second.c_str());
        }
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{350, 60}));
        ImGui::TextColored(kLabelBlue, "Facilities");
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{350, 75}));
        ImGui::BeginChild("##facilities", ui.size({218, 146}), ImGuiChildFlags_Borders);
        int col = 0;
        for (uint32_t f : g.facilities) {
            if (f >= r.data().facilities.size()) continue;
            if (col > 0) ImGui::SameLine(0, ui.px(2));
            image(ui, ui.art.facility(r.facility(f).picture), {32, 32});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.facility(f).name.c_str());
            col = (col + 1) % 6;
        }
        if (g.facilities.empty()) dimText("None");
        ImGui::EndChild();
    }

    // "Defender" / "Attacker" at (40,y) over its units at (40,y+15), 504x72 (ours 66 high, to stay inside
    // our content panel). The grids list troop and militia stacks only (spec 06 §1.10.6).
    void sideRow(UiContext& ui, const game::GameState& s, const char* label, float y, game::EmpireId who, const std::vector<game::UnitStack>& start,
                 const std::vector<int>& now, int militia) {
        const game::Rules& r = ui.rules();
        const Vec2 o = largeOrigin();
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{40, y}));
        ImGui::TextColored(kLabelBlue, "%s", label);
        ImGui::SameLine(0, ui.px(10));
        flagAndName(ui, s, who);
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{40, y + 15}));
        ImGui::BeginChild(label, ui.size({504, 66}), ImGuiChildFlags_Borders);
        const std::string style = who.valid() && who.index() < s.empires.size() ? s.empire(who).race.style : std::string{};
        int col = 0;
        bool any = false;
        for (size_t k = 0; k < start.size(); ++k) {
            const game::UnitStack& st = start[k];
            if (!st.design.valid() || st.design.index() >= s.designs.size() || !game::combat::isTroopDesign(r, s, st.design)) continue;
            any = true;
            if (col > 0) ImGui::SameLine(0, ui.px(6));
            ImGui::BeginGroup();
            image(ui, ui.art.shipMini(style, r.hull(s.design(st.design).hull)), {36, 36});
            ImGui::Text("%d", k < now.size() ? now[k] : st.count);
            ImGui::EndGroup();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.design(st.design).name.c_str());
            col = (col + 1) % 12;
        }
        if (militia >= 0) {
            any = true;
            if (col > 0) ImGui::SameLine(0, ui.px(6));
            ImGui::BeginGroup();
            dimText("Militia");
            ImGui::Text("%d", militia);
            ImGui::EndGroup();
        }
        if (!any) dimText("None");
        ImGui::EndChild();
    }

    int index_ = -1;
    int sub_ = 0;
    bool begun_ = false;
    bool exploding_ = false;   // the round's 0.9 s
    int round_ = 0;            // rounds shown so far
    float elapsed_ = 0.0f;
    Vec2 spot_{0, 0};
    std::minstd_rand rng_;     // where the explosion plays and which boom sounds (not the game's random numbers)
};

} // namespace

std::unique_ptr<Screen> makeStrategicCombat(const ScreenArgs& args) { return std::make_unique<StrategicCombatScreen>(args.index); }
std::unique_ptr<Screen> makeGroundCombat(const ScreenArgs& args) { return std::make_unique<GroundCombatScreen>(args.index, args.sub); }

} // namespace opense4::client::classic
