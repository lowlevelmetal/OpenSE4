// Strategic Combat and Ground Combat (docs/spec/06 §1.6; the rules are
// docs/spec/04 §2, §3, §11, §13).
//
// Strategic Combat is watch-only: a battle fought by the strategies, played
// back on a small map of coloured squares (the combat map's squares, shared
// with the Tactical Combat overview) beside the forces of each side, counted
// per vehicle size: how many are left and how many were lost so far. Begin
// plays it; Close closes it, for the battle's results are already in the
// game. It shows a battle of the game (GameState::combats: one the player's
// orders started or answered Strategic in a turn-based game, or one of a
// simultaneous game's processed turn when the Settings flag `Simultaneous
// Games Show Strategic Combat` is on), or a simulation that the strategies
// fight (the session's fight without player sides, from the Combat Simulator).
//
// Ground Combat shows a ground fight recorded in a battle (CombatRecord::
// grounds): the planet with its facilities, and the defenders and attackers
// as the fight began; Begin shows how it ended. It opens by itself when the
// troops land, in the Tactical Combat window and when the playback here
// reaches the landing.

#include "client/classic/replay.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/combat_map.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"

#include "game/combat.hpp"
#include "game/tactical.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using PieceKind = game::CombatPiece::Kind;

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

std::string designName(const game::GameState& s, game::DesignId d) {
    return d.valid() && d.index() < s.designs.size() ? s.design(d).name : std::string("Unknown design");
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
        BattleSource src = index_ < 0 ? fightBattle(ui) : gameBattle(ui, index_);
        if (src.fight && !src.fight->battle->applied()) {
            // The strategies fight a simulation to its end at once; the window plays it back.
            src.fight->battle->finish();
            src = fightBattle(ui);
        }
        if (!src.record) return false;
        sync(*src.record);
        const CombatMapPainter paint(ui, *src.state, *src.record, playback_);
        const size_t before = playback_.cursor();
        playback_.advance(ui.dt);
        paint.sounds(before, playback_.cursor());
        troopsLanded(ui, *src.record);

        Dialog d(ui, screenTitle(ScreenId::StrategicCombat), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        header(ui, *src.state, *src.record);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float side = ui.px(250);
        drawMap(ui, paint, ImVec2(avail.x - side - ImGui::GetStyle().ItemSpacing.x, avail.y));
        ImGui::SameLine();
        forces(ui, paint, *src.state, *src.record, ImVec2(side, avail.y));

        d.beginButtons();
        const char* play = playback_.playing() ? "Pause" : playback_.atEnd() ? "Watch Again" : playback_.atStart() ? "Begin" : "Continue";
        if (d.button(play, playback_.eventCount() > 0)) playback_.togglePlay();
        if (d.button("Skip to End", !playback_.atEnd())) {
            playback_.pause();
            playback_.seekEvent(playback_.eventCount());
        }
        d.spacer();
        for (size_t k = 0; k < src.record->grounds.size(); ++k) {
            const std::string label = src.record->grounds.size() == 1 ? std::string("Ground Combat") : std::format("Ground Combat {}", k + 1);
            if (d.button(label.c_str())) openGround(ui, int(k));
        }
        const bool closing = d.close();
        if (closing || !d.keepOpen()) {
            // A simulation is dropped; a game battle is already over.
            if (src.fight && src.fight->kind == TacticalFight::Kind::Simulation) ui.session.endTactical();
            return false;
        }
        return true;
    }

private:
    void sync(const game::CombatRecord& rec) {
        if (&rec == record_ && rec.events.size() == events_) return;
        const size_t cursor = record_ ? playback_.cursor() : 0;
        const bool playing = record_ && playback_.playing();
        playback_ = CombatPlayback(rec);
        playback_.setSpeed(settings().replaySpeed);
        playback_.seekEvent(std::min(cursor, playback_.eventCount()));
        if (playing) playback_.play();
        record_ = &rec;
        events_ = rec.events.size();
        shown_.assign(rec.grounds.size(), 0);
    }

    void openGround(UiContext& ui, int k) {
        ScreenArgs a;
        a.index = index_;
        a.sub = k;
        ui.open(ScreenId::GroundCombat, std::move(a));
    }

    // "After troops land" (spec 06 §1.6): the Ground Combat window opens as the playback reaches a landing.
    void troopsLanded(UiContext& ui, const game::CombatRecord& rec) {
        if (!playback_.playing() || playback_.round() <= 0) return;
        for (size_t k = 0; k < rec.grounds.size() && k < shown_.size(); ++k) {
            if (shown_[k] || int(rec.grounds[k].round) - firstRound(rec) + 1 > playback_.round()) continue;
            shown_[k] = 1;
            playback_.pause();
            openGround(ui, int(k));
        }
    }

    static int firstRound(const game::CombatRecord& rec) {
        int first = 255;
        for (const game::CombatEvent& e : rec.events) first = std::min(first, int(e.round));
        return rec.events.empty() ? 1 : first;
    }

    void header(UiContext& ui, const game::GameState& s, const game::CombatRecord& rec) {
        ImGui::TextColored(kLabelBlue, "System");
        ImGui::SameLine();
        ImGui::TextUnformatted(systemName(s, rec.location).c_str());
        ImGui::SameLine(0, ui.px(24));
        ImGui::TextColored(kLabelBlue, "Sector");
        ImGui::SameLine();
        ImGui::Text("%d, %d", rec.location.sector.x, rec.location.sector.y);
        ImGui::SameLine(0, ui.px(24));
        ImGui::TextColored(kLabelBlue, "Combat turn");
        ImGui::SameLine();
        if (playback_.roundCount() <= 0) ImGui::TextUnformatted("-");
        else if (playback_.atStart()) ImGui::Text("before the first of %d", playback_.roundCount());
        else ImGui::Text("%d of %d", std::max(playback_.round(), 1), playback_.roundCount());
        ImGui::Separator();
    }

    // The small map: the whole combat grid, every piece a square of its owner's colour.
    void drawMap(UiContext& ui, const CombatMapPainter& paint, ImVec2 size) {
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##strategicmap", size);
        const bool hoveredBox = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o2{o.x + size.x, o.y + size.y};
        constexpr float kW = float(game::combat::kCombatMapWidth), kH = float(game::combat::kCombatMapHeight);
        CombatView v;
        v.cell = std::max(1.0f, std::min(size.x / kW, size.y / kH));
        v.cx = kW * 0.5f;
        v.cy = kH * 0.5f;
        v.center = {o.x + size.x * 0.5f, o.y + size.y * 0.5f};
        dl->PushClipRect(o, o2, true);
        dl->AddRectFilled(o, o2, IM_COL32(0, 0, 0, 255));
        const ImVec2 m0 = v.at(0, 0), m1 = v.at(kW, kH);
        dl->AddRectFilled(m0, m1, IM_COL32(6, 10, 24, 255));
        if (v.cell >= 4.0f) {
            for (int x = 0; x <= int(kW); ++x) dl->AddLine(v.at(float(x), 0), v.at(float(x), kH), IM_COL32(40, 70, 140, 28));
            for (int y = 0; y <= int(kH); ++y) dl->AddLine(v.at(0, float(y)), v.at(kW, float(y)), IM_COL32(40, 70, 140, 28));
        }
        dl->AddRect(m0, m1, IM_COL32(90, 120, 200, 200));
        const std::optional<uint32_t> hovered = paint.squares(dl, v, 3.0f, hoveredBox, ImGui::GetIO().MousePos);
        if (const game::CombatEvent* e = playback_.animating()) paint.event(dl, v, *e, playback_.fraction());
        dl->PopClipRect();
        dl->AddRect(o, o2, IM_COL32(66, 107, 216, 255));
        if (hovered && *hovered < playback_.pieces().size()) {
            const CombatPlayback::Piece& p = playback_.pieces()[*hovered];
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(paint.pieceName(*hovered).c_str());
            labelValue(ui, "Owner", p.neutral ? std::string("None") : paint.empireName(p.owner), 70);
            if (record_ && record_->pieces[*hovered].kind == PieceKind::UnitGroup)
                labelValue(ui, "Units", std::format("{} of {} left", p.units, record_->pieces[*hovered].count), 70);
            ImGui::EndTooltip();
        }
    }

    // Forces: for each side, per vehicle size, the pieces left now and those lost so far
    // (unit groups count their units; planets count as one).
    void forces(UiContext& ui, const CombatMapPainter& paint, const game::GameState& s, const game::CombatRecord& rec, ImVec2 size) {
        ImGui::BeginChild("##forces", size, ImGuiChildFlags_Borders);
        heading(ui, "Forces");
        for (game::EmpireId e : rec.participants) {
            struct Row {
                std::string size;
                int now = 0, lost = 0;
            };
            std::vector<Row> rows;
            auto rowFor = [&](const std::string& name) -> Row& {
                for (Row& r : rows)
                    if (r.size == name) return r;
                rows.push_back({name});
                return rows.back();
            };
            for (size_t i = 0; i < rec.pieces.size() && i < playback_.pieces().size(); ++i) {
                const game::CombatPiece& rp = rec.pieces[i];
                const CombatPlayback::Piece& p = playback_.pieces()[i];
                if (rp.kind == PieceKind::Seeker || rp.kind == PieceKind::Obstacle) continue;
                if (!p.onMap && !p.destroyed && !p.captured) continue;   // units not launched yet
                const bool group = rp.kind == PieceKind::UnitGroup;
                std::string name = "Planets";
                if (rp.kind != PieceKind::Planet) {
                    const ruleset::VehicleSize* hull = paint.hullOf(rp);
                    name = hull ? hull->name : std::string("Unknown");
                }
                const int start = group ? std::max(1, int(rp.count)) : 1;
                const int left = p.destroyed || p.neutral ? 0 : group ? std::clamp(p.units, 0, start) : 1;
                if (rp.owner == e) rowFor(name).lost += p.owner != e && left > 0 ? start : start - left;
                if (p.owner == e && left > 0) rowFor(name).now += left;
            }
            ImGui::Spacing();
            flagAndName(ui, s, e);
            if (rows.empty()) {
                dimText("  Nothing on the field");
                continue;
            }
            for (const Row& r : rows) {
                ImGui::TextUnformatted(std::format("  {}", r.size).c_str());
                ImGui::SameLine(ui.px(150));
                ImGui::Text("%d", r.now);
                ImGui::SameLine(ui.px(185));
                if (r.lost > 0) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "lost %d", r.lost);
                else dimText("");
            }
        }
        if (playback_.atEnd() && !rec.summary.empty()) {
            ImGui::Spacing();
            heading(ui, "Result");
            ImGui::PushTextWrapPos(0.0f);
            for (const std::string& line : rec.summary)
                if (!line.starts_with("Turn ")) ImGui::TextUnformatted(line.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
    }

    int index_ = -1;
    const game::CombatRecord* record_ = nullptr;
    size_t events_ = 0;
    CombatPlayback playback_;
    std::vector<uint8_t> shown_;   // ground combats already opened by the playback
};

// ---- Ground Combat ---------------------------------------------------------------------------------------

class GroundCombatScreen final : public Screen {
public:
    GroundCombatScreen(int index, int sub) : index_(index), sub_(std::max(0, sub)) {}
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        BattleSource src = find(ui);
        Dialog d(ui, screenTitle(ScreenId::GroundCombat), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        const game::GroundCombat* g = src.record && size_t(sub_) < src.record->grounds.size() ? &src.record->grounds[size_t(sub_)] : nullptr;
        if (!g) {
            ImGui::Spacing();
            ImGui::TextWrapped("No ground combat was fought in the battles of the last turn.");
        } else {
            draw(ui, *src.state, *src.record, *g);
        }
        d.beginButtons();
        if (d.button(done_ ? "Fought" : "Begin", g && !done_)) done_ = true;
        const size_t count = src.record ? src.record->grounds.size() : 0;
        if (count > 1) {
            d.spacer();
            if (d.button("Previous", sub_ > 0)) {
                --sub_;
                done_ = false;
            }
            if (d.button("Next", size_t(sub_) + 1 < count)) {
                ++sub_;
                done_ = false;
            }
        }
        if (d.close()) return false;
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

    void draw(UiContext& ui, const game::GameState& s, const game::CombatRecord& rec, const game::GroundCombat& g) {
        const game::Rules& r = ui.rules();
        const bool planetKnown = g.planet.valid() && g.planet.index() < s.galaxy.objects.size();
        // The planet: picture, name, owner, population, where, and its facilities.
        ImGui::BeginChild("##planet", ImVec2(ui.px(250), 0), ImGuiChildFlags_Borders);
        if (planetKnown) image(ui, objectSprite(ui, s.galaxy.object(g.planet)), {96, 96});
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTextSize));
        ImGui::TextUnformatted(planetKnown ? s.galaxy.object(g.planet).name.c_str() : "Planet");
        ImGui::PopFont();
        labelValue(ui, "Owner", g.defender.valid() && g.defender.index() < s.empires.size() ? s.empire(g.defender).name : std::string("Nobody"), 80);
        labelValue(ui, "Population", std::format("{}M", formatNumber(g.population)), 80);
        labelValue(ui, "Location", sectorName(s, rec.location, ui.session.player()), 80);
        labelValue(ui, "Landed", std::format("combat turn {}", int(g.round)), 80);
        heading(ui, std::format("Facilities ({})", g.facilities.size()).c_str());
        for (uint32_t f : g.facilities)
            if (f < r.data().facilities.size()) ImGui::BulletText("%s", r.facility(f).name.c_str());
        if (g.facilities.empty()) dimText("None");
        ImGui::EndChild();
        ImGui::SameLine();
        // The two sides: as the fight began, and after Begin, what is left of them.
        ImGui::BeginGroup();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = (ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.5f) * 0.5f;
        side(ui, s, "Defenders", g.defender, g.defenders, g.defendersLeft, g.militia, g.militiaLeft, ImVec2(w, h));
        side(ui, s, "Attackers", g.attacker, g.attackers, g.attackersLeft, -1, -1, ImVec2(w, h));
        if (done_) {
            const std::string attacker = g.attacker.valid() && g.attacker.index() < s.empires.size() ? s.empire(g.attacker).name : "the invaders";
            int attackersLeft = 0;
            for (const game::UnitStack& st : g.attackersLeft) attackersLeft += st.count;
            std::string result = g.captured ? std::format("The planet fell to the {}.", attacker)
                                 : attackersLeft == 0 ? std::string("The invasion failed.")
                                                      : std::string("Both sides hold on: the fight goes on next game turn.");
            ImGui::TextColored(ImVec4(1, 0.85f, 0.45f, 1), "%s", std::format("{} round{}. {}", g.rounds, g.rounds == 1 ? "" : "s", result).c_str());
        } else {
            dimText("Begin to see how the fight went.");
        }
        ImGui::EndGroup();
    }

    void side(UiContext& ui, const game::GameState& s, const char* title, game::EmpireId who, const std::vector<game::UnitStack>& start,
              const std::vector<game::UnitStack>& left, int militia, int militiaLeft, ImVec2 size) {
        ImGui::BeginChild(title, size, ImGuiChildFlags_Borders);
        heading(ui, title);
        flagAndName(ui, s, who);
        for (size_t k = 0; k < start.size(); ++k) {
            const int after = k < left.size() ? left[k].count : start[k].count;
            ImGui::TextUnformatted(std::format("  {}", designName(s, start[k].design)).c_str());
            ImGui::SameLine(ui.px(260));
            if (done_) ImGui::Text("%d of %d", after, start[k].count);
            else ImGui::Text("%d", start[k].count);
        }
        if (militia >= 0) {
            ImGui::TextUnformatted("  Militia");
            ImGui::SameLine(ui.px(260));
            if (done_) ImGui::Text("%d of %d", militiaLeft, militia);
            else ImGui::Text("%d", militia);
        }
        if (start.empty() && militia <= 0) dimText("  None");
        ImGui::EndChild();
    }

    int index_ = -1;
    int sub_ = 0;
    bool done_ = false;
};

} // namespace

std::unique_ptr<Screen> makeStrategicCombat(const ScreenArgs& args) { return std::make_unique<StrategicCombatScreen>(args.index); }
std::unique_ptr<Screen> makeGroundCombat(const ScreenArgs& args) { return std::make_unique<GroundCombatScreen>(args.index, args.sub); }

} // namespace opense4::client::classic
