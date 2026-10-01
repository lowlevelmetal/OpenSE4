// Strategic Combat and Ground Combat (docs/spec/06 §1.10.5, §1.10.6; the
// rules are docs/spec/04 §2, §3, §11, §13).
//
// Strategic Combat shows a battle fought by the strategies, one combat turn
// at a time: a list of each empire's forces per hull on the left (current and
// lost), a small map of coloured squares on the right, and "Combat Turn N" in
// the title strip. Begin (or Strategic) starts it; Close stays dim until the
// battle is over. It plays no sounds. It is opened
//   - as the question of a turn-based game on this machine (ScreenArgs::index
//     kStrategicQuestion): the battle that waits for Tactical or Strategic, with
//     Strategic and Tactical buttons. Strategic fights it here: the battle is
//     fought by the strategies on the question's copy of the game (a
//     combat::TacticalBattle without player sides), shown, and its orders
//     answer the question when the window closes; Tactical opens the Tactical
//     Combat window instead;
//   - for the session's fight without player sides (index -1): such a game
//     battle, or a simulation the strategies fight;
//   - for a battle of the game (index into GameState::combats): one the
//     player's orders started, one of a game without tactical combat, or one
//     of a simultaneous game's turn when the Settings flag `Simultaneous Games
//     Show Strategic Combat` is on.
// The engine fights a battle at once, so the window plays the recorded battle
// back one combat turn at a time (our way of fighting it "live").
//
// Ground Combat shows a ground fight recorded in a battle (CombatRecord::
// grounds): the planet, its facilities, the defenders and the attackers.
// Begin fights it round by round and "Victorious!" marks the winner; Close is
// dim until then. It opens by itself when troops land in the Tactical Combat
// window, and when a computer side lands troops during a Strategic Combat
// battle, unless both empires are computer-controlled (the simulator always
// shows it).

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

namespace opense4::client::classic {

namespace {

using PieceKind = game::CombatPiece::Kind;

// Seconds a combat turn or a ground combat round stays on screen (inferred:
// the original has no coded delay, so it runs as fast as it can draw).
constexpr float kTurnSeconds = 0.45f;

// The Large dialog's top-left corner, for the positions spec 06 gives in window pixels.
Vec2 largeOrigin() { return {(kFrameW - 780.0f) * 0.5f, (kFrameH - 475.0f) * 0.5f}; }

// Ground Combat windows open now: a Strategic Combat battle waits while one is.
int gGroundWindows = 0;

// The battle a window shows: a battle of the game, or the session's fight.
struct BattleSource {
    const game::GameState* state = nullptr;
    const game::CombatRecord* record = nullptr;
    TacticalFight* fight = nullptr;
    int index = -1;   // into GameState::combats (-1: the fight)
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

// ---- Strategic Combat ------------------------------------------------------------------------------------

class StrategicCombatScreen final : public Screen {
public:
    explicit StrategicCombatScreen(int index) : index_(index) {}
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        BattleSource src;
        bool question = false;
        if (index_ == kStrategicQuestion) {
            TacticalFight* f = ui.session.tactical();
            if (f && f->kind == TacticalFight::Kind::Game && f->players.empty()) {
                index_ = -1;   // answered Strategic: the fight is ours to show
            } else if (!ui.session.battleQuestion()) {
                return false;
            } else {
                if (!preview(ui)) return false;
                question = true;
                src.state = &preview_->state();
                src.record = &preview_->record();
            }
        }
        if (!question) src = index_ < 0 ? fightBattle(ui) : gameBattle(ui, index_);
        if (src.fight && !src.fight->battle->applied()) {
            // The strategies have fought it to its end already; work out the results.
            src.fight->battle->finish();
            src = fightBattle(ui);
        }
        if (!src.record) return false;
        sync(*src.record);
        step(ui, src);
        forces_.update(ui.rules(), *src.state, *src.record, playback_.pieces());

        Dialog d(ui, screenTitle(ScreenId::StrategicCombat), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Location where = src.record->location;
        d.titleText(270, IM_COL32_WHITE, std::format("System {}", systemName(*src.state, where)));
        d.titleText(450, IM_COL32_WHITE, std::format("Coordinates ({},{})", where.sector.x, where.sector.y));
        if (begun_) d.titleText(630, IM_COL32_WHITE, std::format("Combat Turn {}", std::max(1, playback_.round())));
        d.beginContent();
        forcesList(ui, *src.state);
        drawMap(ui, *src.state, *src.record);

        d.beginButtons();
        const bool over = begun_ && playback_.atEnd();
        if (question) {
            // T and S pick them (spec 06 §3.4).
            const std::optional<bool> key = tacticalStrategicKey();
            const bool strategic = d.button("Strategic") || key == false;
            const bool tactical = d.button("Tactical") || key == true;
            if (strategic) answerStrategic(ui);
            else if (tactical) return answerTactical(ui);
        } else if (d.button("Begin", !begun_)) {
            begin();
        }
        if (d.close(over) || !d.keepOpen()) {
            // A simulation is dropped; a game battle fought here answers its question.
            if (src.fight) ui.session.endTactical();
            return false;
        }
        return true;
    }

private:
    // The battle that waits for its answer, fought by the strategies on the question's copy of the game.
    bool preview(UiContext& ui) {
        const game::BattleQuestion& q = *ui.session.battleQuestion();
        const size_t key = q.index * 100003u + size_t(q.where.system.value) * 1009u + size_t(q.where.sector.x * 13 + q.where.sector.y);
        if (preview_ && previewKey_ == key) return true;
        previewKey_ = key;
        begun_ = false;
        record_ = nullptr;
        preview_ = std::make_unique<game::combat::TacticalBattle>(
            ui.rules(), *q.state, game::combat::TacticalBattle::Setup{q.where, q.entering, {}, std::nullopt, std::nullopt, std::nullopt, q.check});
        if (!preview_->started()) {
            preview_.reset();
            ui.session.answerBattle(game::BattleAnswer{});
            return false;
        }
        return true;
    }

    void answerStrategic(UiContext& ui) {
        if (!preview_) return;
        TacticalFight f;
        f.kind = TacticalFight::Kind::Game;
        f.battle = std::move(preview_);
        f.title = screenTitle(ScreenId::StrategicCombat);
        ui.session.startTactical(std::move(f));
        index_ = -1;
        begin();
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

    void begin() {
        begun_ = true;
        elapsed_ = kTurnSeconds;   // the first combat turn at once
    }

    void sync(const game::CombatRecord& rec) {
        if (&rec == record_ && rec.events.size() == events_) return;
        const size_t cursor = record_ ? playback_.cursor() : 0;
        playback_ = CombatPlayback(rec);
        playback_.seekEvent(std::min(cursor, playback_.eventCount()));
        record_ = &rec;
        events_ = rec.events.size();
        shown_.assign(rec.grounds.size(), 0);
        forces_.reset();
    }

    // One combat turn at a time while the battle runs; it waits while a Ground Combat window is open.
    void step(UiContext& ui, const BattleSource& src) {
        if (!begun_ || playback_.atEnd() || gGroundWindows > 0) return;
        elapsed_ += ui.dt;
        if (elapsed_ < kTurnSeconds) return;
        elapsed_ = 0.0f;
        playback_.seekRound(std::max(0, playback_.round()) + 1);
        troopsLanded(ui, src);
    }

    // A computer side's troops landed in the turn just shown: the Ground Combat
    // window, unless both empires are computer-controlled (the simulator always
    // shows it) (spec 06 §1.10.6).
    void troopsLanded(UiContext& ui, const BattleSource& src) {
        const game::CombatRecord& rec = *src.record;
        const int first = firstRound(rec);
        const bool simulation = src.fight && src.fight->kind == TacticalFight::Kind::Simulation;
        for (size_t k = 0; k < rec.grounds.size() && k < shown_.size(); ++k) {
            const game::GroundCombat& g = rec.grounds[k];
            if (shown_[k] || int(g.round) - first + 1 > playback_.round()) continue;
            shown_[k] = 1;
            if (!computerControlled(*src.state, g.attacker)) continue;
            if (!simulation && computerControlled(*src.state, g.defender)) continue;
            ScreenArgs a;
            a.index = src.index;
            a.sub = int(k);
            ui.open(ScreenId::GroundCombat, std::move(a));
        }
    }

    static int firstRound(const game::CombatRecord& rec) {
        int first = 255;
        for (const game::CombatEvent& e : rec.events) first = std::min(first, int(e.round));
        return rec.events.empty() ? 1 : first;
    }

    // "Combat Forces" (15,55), 329x400, 18 px rows; Current and Lost at +220 and +270.
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

    // The map at (354,55), 218x191: the whole combat grid in 3 px squares, framed in #647EC7.
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
        dl->AddRect(a, b, imColor(palette::kFrameLight));
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
    std::unique_ptr<game::combat::TacticalBattle> preview_;   // the question's battle, fought by the strategies
    size_t previewKey_ = SIZE_MAX;
    const game::CombatRecord* record_ = nullptr;
    size_t events_ = 0;
    CombatPlayback playback_;
    CombatForces forces_;
    bool begun_ = false;
    float elapsed_ = 0.0f;
    std::vector<uint8_t> shown_;   // ground combats already opened
};

// ---- Ground Combat ---------------------------------------------------------------------------------------

class GroundCombatScreen final : public Screen {
public:
    GroundCombatScreen(int index, int sub) : index_(index), sub_(std::max(0, sub)) { ++gGroundWindows; }
    ~GroundCombatScreen() override { --gGroundWindows; }
    GroundCombatScreen(const GroundCombatScreen&) = delete;
    GroundCombatScreen& operator=(const GroundCombatScreen&) = delete;
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        BattleSource src = find(ui);
        const game::GroundCombat* g = src.record && size_t(sub_) < src.record->grounds.size() ? &src.record->grounds[size_t(sub_)] : nullptr;
        if (!g) return false;
        // Round by round once begun (our record keeps the start and the end of the fight).
        if (begun_ && round_ < g->rounds) {
            elapsed_ += ui.dt;
            if (elapsed_ >= kTurnSeconds) {
                elapsed_ = 0.0f;
                ++round_;
            }
        }
        const bool over = begun_ && round_ >= g->rounds;
        Dialog d(ui, screenTitle(ScreenId::GroundCombat), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = *src.state;
        d.titleText(270, IM_COL32_WHITE, std::format("System {}", systemName(s, src.record->location)));
        d.titleText(450, IM_COL32_WHITE, std::format("Coordinates ({},{})", src.record->location.sector.x, src.record->location.sector.y));
        d.beginContent();
        drawPlanet(ui, s, *g);
        const bool defenderWon = over && !g->captured && survivors(g->attackersLeft) == 0;
        const bool attackerWon = over && g->captured;
        sideRow(ui, s, "Defender", 264, g->defender, over ? g->defendersLeft : g->defenders, g->defenders, over ? g->militiaLeft : g->militia, defenderWon);
        sideRow(ui, s, "Attacker", 378, g->attacker, over ? g->attackersLeft : g->attackers, g->attackers, -1, attackerWon);
        if (begun_ && !over) {
            ImGui::SetCursorScreenPos(ui.at(largeOrigin() + Vec2{350, 230}));
            ImGui::TextColored(kLabelBlue, "%s", std::format("Round {} of {}", std::max(1, round_), g->rounds).c_str());
        }
        d.beginButtons();
        if (d.button("Begin", !begun_)) {
            begun_ = true;
            elapsed_ = 0.0f;
            round_ = g->rounds > 0 ? 1 : 0;
        }
        if (d.close(over)) return false;
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

    static int survivors(const std::vector<game::UnitStack>& stacks) {
        int n = 0;
        for (const game::UnitStack& st : stacks) n += st.count;
        return n;
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
    // our content panel); "Victorious!" beside the winner.
    void sideRow(UiContext& ui, const game::GameState& s, const char* label, float y, game::EmpireId who, const std::vector<game::UnitStack>& units,
                 const std::vector<game::UnitStack>& start, int militia, bool won) {
        const Vec2 o = largeOrigin();
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{40, y}));
        ImGui::TextColored(kLabelBlue, "%s", label);
        ImGui::SameLine(0, ui.px(10));
        flagAndName(ui, s, who);
        if (won) {
            ImGui::SameLine(0, ui.px(12));
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Victorious!");
        }
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{40, y + 15}));
        ImGui::BeginChild(label, ui.size({504, 66}), ImGuiChildFlags_Borders);
        const std::string style = who.valid() && who.index() < s.empires.size() ? s.empire(who).race.style : std::string{};
        int col = 0;
        for (size_t k = 0; k < start.size(); ++k) {
            const game::UnitStack& st = start[k];
            const int now = k < units.size() ? units[k].count : st.count;
            if (col > 0) ImGui::SameLine(0, ui.px(6));
            ImGui::BeginGroup();
            Sprite pic;
            if (st.design.valid() && st.design.index() < s.designs.size()) pic = ui.art.shipMini(style, ui.rules().hull(s.design(st.design).hull));
            image(ui, pic, {36, 36});
            ImGui::Text("%d", now);
            ImGui::EndGroup();
            if (ImGui::IsItemHovered() && st.design.valid() && st.design.index() < s.designs.size())
                ImGui::SetTooltip("%s", s.design(st.design).name.c_str());
            col = (col + 1) % 12;
        }
        if (militia >= 0) {
            if (col > 0) ImGui::SameLine(0, ui.px(6));
            ImGui::BeginGroup();
            dimText("Militia");
            ImGui::Text("%d", militia);
            ImGui::EndGroup();
        }
        if (start.empty() && militia < 0) dimText("None");
        ImGui::EndChild();
    }

    int index_ = -1;
    int sub_ = 0;
    bool begun_ = false;
    int round_ = 0;
    float elapsed_ = 0.0f;
};

} // namespace

std::unique_ptr<Screen> makeStrategicCombat(const ScreenArgs& args) { return std::make_unique<StrategicCombatScreen>(args.index); }
std::unique_ptr<Screen> makeGroundCombat(const ScreenArgs& args) { return std::make_unique<GroundCombatScreen>(args.index, args.sub); }

} // namespace opense4::client::classic
