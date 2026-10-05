// Empires (F9) and the comparison windows it opens: Treaty Grid, Scores,
// Comparisons, History, Race Report and Victory Conditions (docs/spec/06 §1.2,
// §1.5; docs/spec/05 §3, §5, §6). Communicate lives in communicate.cpp.

#include "client/audio.hpp"
#include "client/classic/quadrant_map.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"

#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"
#include "game/score.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace opense4::client::classic {

namespace {

using game::EmpireId;
using game::Treaty;

ImU32 treatyColor(Treaty t) {
    switch (t) {
        case Treaty::War: return IM_COL32(255, 90, 80, 255);
        case Treaty::NonIntercourse: return IM_COL32(255, 160, 60, 255);
        case Treaty::None: return IM_COL32(150, 160, 175, 255);
        case Treaty::NonAggression: return IM_COL32(140, 190, 255, 255);
        case Treaty::Subjugation:
        case Treaty::Protectorate: return IM_COL32(200, 140, 255, 255);
        case Treaty::TradeAlliance: return IM_COL32(150, 220, 150, 255);
        case Treaty::TradeResearchAlliance: return IM_COL32(120, 230, 160, 255);
        case Treaty::MilitaryAlliance: return IM_COL32(90, 240, 110, 255);
        case Treaty::Partnership: return IM_COL32(60, 255, 150, 255);
        case Treaty::Count: break;
    }
    return IM_COL32(255, 255, 255, 255);
}

// "Trade Alliance", with who leads a subjugation or protectorate.
std::string treatyText(const game::Relation& rel) {
    std::string out(game::displayName(rel.treaty));
    if (rel.treaty == Treaty::Subjugation || rel.treaty == Treaty::Protectorate) out += rel.dominant ? " (we lead)" : " (they lead)";
    return out;
}

bool validEmpire(const game::GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size(); }

// Scores and statistics of other empires follow the Score Display option
// (spec 05 §5).
bool statsVisible(const UiContext& ui, EmpireId e) { return game::score::scoreVisible(ui.state(), ui.session.player(), e); }

// Per-page tab buttons over a list: "Empires 1-10", "Empires 11-20", ...
void pageButtons(Dialog& d, int& page, size_t count, int perPage) {
    const int pages = std::max(1, int((count + size_t(perPage) - 1) / size_t(perPage)));
    if (pages <= 1) {
        page = 0;
        return;
    }
    page = std::min(page, pages - 1);
    for (int p = 0; p < pages; ++p) {
        const std::string label = std::format("Empires {}-{}", p * perPage + 1, std::min<int>(int(count), (p + 1) * perPage));
        if (d.tab(label.c_str(), page == p)) page = p;
    }
    d.spacer();
}

void statLine(UiContext& ui, const char* label, const std::string& value, ImVec4 color = ImVec4(0.9f, 0.92f, 0.97f, 1.0f)) {
    ImGui::TextColored(kTextBlue, "%s", label);
    ImGui::SameLine(ui.px(58));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(color, "%s", value.c_str());
    ImGui::PopTextWrapPos();
}

ImVec4 toVec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// 1, 2 or 5 times a power of ten, so that `ticks` steps cover maxValue.
int64_t niceStep(int64_t maxValue, int ticks) {
    const int64_t raw = std::max<int64_t>(1, (maxValue + ticks - 1) / ticks);
    int64_t mag = 1;
    while (mag * 10 <= raw) mag *= 10;
    for (int64_t m : {int64_t{1}, int64_t{2}, int64_t{5}})
        if (m * mag >= raw) return m * mag;
    return 10 * mag;
}

// ---- Empires ------------------------------------------------------------------------------------

class EmpiresScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Empires", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        ImGui::BeginGroup();
        portraits(ui);
        ImGui::EndGroup();
        ui.tagItem("empires:list");

        d.beginButtons();
        if (d.tab("Treaty", tab_ == Tab::Treaty)) select(Tab::Treaty);
        ui.tagTab("treaty", tab_ == Tab::Treaty);
        if (d.tab("Trade", tab_ == Tab::Trade)) select(Tab::Trade);
        ui.tagTab("trade", tab_ == Tab::Trade);
        if (d.tab("Tariff", tab_ == Tab::Tariff)) select(Tab::Tariff);
        ui.tagTab("tariff", tab_ == Tab::Tariff);
        // The original's order (observed, spec 07 session 3): the tabs, a gap,
        // History, Treaty Grid, Intelligence (dim before any contact), Borders,
        // Scores, Victory Conditions, Comparisons, a gap, Our Race in the 13th slot.
        d.spacer();
        if (d.button("History")) ui.open(ScreenId::History);
        if (d.button("Treaty Grid")) ui.open(ScreenId::TreatyGrid);
        ui.tagItem("empires:treaty-grid");
        if (d.button("Intelligence", !knownEmpires(ui).empty())) ui.open(ScreenId::Intelligence);
        ui.tagItem("empires:intelligence");
        // Borders is a window of its own, over this one (spec 06 §7 Q96).
        if (d.button("Borders")) ui.open(ScreenId::Borders);
        if (d.button("Scores")) ui.open(ScreenId::Scores);
        ui.tagItem("empires:scores");
        if (d.button("Victory Conditions")) ui.open(ScreenId::VictoryConditions);
        ui.tagItem("empires:victory-conditions");
        if (d.button("Comparisons")) ui.open(ScreenId::Comparisons);
        d.spacer();
        const bool ourRace = d.button("Our Race");
        ui.tagItem("empires:our-race");
        if (ourRace) {
            ScreenArgs a;
            a.empire = ui.session.player();
            ui.open(ScreenId::RaceReport, a);
        }
        d.close();
        return d.keepOpen();
    }

private:
    enum class Tab { Treaty, Trade, Tariff };
    static constexpr int kPerPage = 4;

    void select(Tab t) { tab_ = t; }

    void header(UiContext& ui, size_t known) {
        const game::Empire& me = ui.me();
        switch (tab_) {
            case Tab::Treaty:
                // No heading here (observed, spec 07 session 3).
                (void)known;
                break;
            case Tab::Trade:
                heading(ui, "Trade income");
                ImGui::SameLine();
                resources(ui, me.economy.trade, true);
                ImGui::SameLine(0, ui.px(18));
                ImGui::TextColored(kTextBlue, "Research");
                ImGui::SameLine();
                ImGui::TextUnformatted(formatNumber(game::diplomacy::researchTradeIncome(ui.rules(), ui.state(), me.id)).c_str());
                break;
            case Tab::Tariff:
                heading(ui, "Tariffs in");
                ImGui::SameLine();
                resources(ui, me.economy.tariffsIn, true);
                ImGui::SameLine(0, ui.px(18));
                heading(ui, "out");
                ImGui::SameLine();
                resources(ui, me.economy.tariffsOut, true);
                break;
        }
    }

    void portraits(UiContext& ui) {
        // The original's strip: a framed row of four portraits between the big page
        // arrows; each empire's details are listed under its portrait.
        const game::GameState& s = ui.state();
        const auto known = knownEmpires(ui);
        ImGui::TextColored(kTextBlue, "%zu Known Empires", known.size());
        ImGui::SameLine();
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        const char* hint = "(click on a portrait to communicate with the empire)";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(hint).x));
        ImGui::TextColored(kTextBlue, "%s", hint);
        ImGui::PopFont();

        const int pages = std::max(1, int((known.size() + kPerPage - 1) / kPerPage));
        page_ = std::clamp(page_, 0, pages - 1);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = ui.px(130);
        const float arrowW = ui.px(16), slotW = (w - 2 * arrowW) / kPerPage;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(a, {a.x + w, a.y + h}, imColor(palette::kFrame));
        for (int c = 0; c <= kPerPage; ++c) {
            const float x = a.x + arrowW + slotW * float(c);
            dl->AddLine({x, a.y}, {x, a.y + h}, imColor(palette::kFrame));
        }
        auto arrow = [&](bool right, bool enabled) {
            const ImVec2 at{right ? a.x + w - arrowW : a.x, a.y};
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(right ? "next" : "prev");
            const bool clicked = ImGui::InvisibleButton("##arrow", ImVec2(arrowW, h)) && enabled;
            const int state = !enabled ? 3 : ImGui::IsItemHovered() ? 1 : 0;
            ImGui::PopID();
            if (Sprite sp = ui.art.region("Pictures/Game/Buttons/BigLeftRightArrows.bmp", right ? 14 : 0, state * 50, 14, 50, false)) {
                const ImVec2 p0{at.x + ui.px(1), at.y + (h - ui.px(50)) * 0.5f};
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(sp.tex.value)), p0, {p0.x + ui.px(14), p0.y + ui.px(50)}, {sp.uv.min.x, sp.uv.min.y},
                             {sp.uv.max.x, sp.uv.max.y});
            }
            return clicked;
        };
        if (arrow(false, page_ > 0)) --page_;
        if (arrow(true, page_ + 1 < pages)) ++page_;
        for (int c = 0; c < kPerPage; ++c) {
            const size_t i = size_t(page_ * kPerPage + c);
            if (i >= known.size()) break;
            const game::Empire& them = s.empire(known[i]);
            const ImVec2 p{a.x + arrowW + slotW * float(c) + (slotW - ui.px(128)) * 0.5f, a.y + ui.px(1)};
            ImGui::SetCursorScreenPos(p);
            ImGui::PushID(int(i));
            ImGui::InvisibleButton("##portrait", ui.size({128, 128}));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (Sprite portrait = ui.art.racePortrait(them.race.style))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(portrait.tex.value)), p, {p.x + ui.px(128), p.y + ui.px(128)},
                             {portrait.uv.min.x, portrait.uv.min.y}, {portrait.uv.max.x, portrait.uv.max.y});
            if (hovered) {
                dl->AddRect(p, ImVec2(p.x + ui.px(128), p.y + ui.px(128)), IM_COL32(255, 208, 64, 255), 0.0f, 2.0f);
                ImGui::SetTooltip("Left-click: Communicate\nRight-click: Race Report");
            }
            ScreenArgs args;
            args.empire = them.id;
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ui.open(ScreenId::Communicate, args);
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ui.open(ScreenId::RaceReport, args);
        }
        ImGui::SetCursorScreenPos({a.x, a.y + h + ui.px(4)});
        // Before any contact the strip stays empty, with no explanation (observed).
        if (known.empty()) {
            ImGui::Dummy(ImVec2(w, 0));
            return;
        }
        header(ui, known.size());
        const float y0 = ImGui::GetCursorScreenPos().y;
        for (int c = 0; c < kPerPage; ++c) {
            const size_t i = size_t(page_ * kPerPage + c);
            if (i >= known.size()) break;
            ImGui::SetCursorScreenPos({a.x + arrowW + slotW * float(c) + ui.px(3), y0});
            ImGui::PushID(int(i));
            ImGui::BeginChild("##col", ImVec2(slotW - ui.px(6), 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
            column(ui, s.empire(known[i]));
            ImGui::EndChild();
            ImGui::PopID();
        }
    }

    void column(UiContext& ui, const game::Empire& them) {
        const game::Relation& rel = ui.me().relation(them.id);
        empireLabel(ui, them.id, true);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kTextDim, "%s %s", them.leaderTitle.c_str(), them.leaderName.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        statLine(ui, "Treaty", treatyText(rel), toVec4(treatyColor(rel.treaty)));
        switch (tab_) {
            case Tab::Treaty: {
                statLine(ui, "Race", them.race.name);
                statLine(ui, "Player", game::isNeutral(them) ? "Neutral" : them.kind == game::PlayerKind::Human ? "Human" : "Computer");
                if (rel.treaty != Treaty::None) statLine(ui, "Since", formatDate(rel.treatyTurn));
                statLine(ui, "Last war", rel.lastWarTurn >= 0 ? formatDate(uint32_t(rel.lastWarTurn)) : "Never");
                if (them.kind != game::PlayerKind::Human) {
                    const int anger = them.relation(ui.session.player()).anger;
                    statLine(ui, "Mood", std::format("{} ({})", moodWord(anger), anger));
                }
                // Only while a reply is still possible: the message is in the Log (issue #4).
                const int waiting = messagesAwaitingReply(ui.state(), ui.session.player(), them.id);
                if (waiting > 0) statLine(ui, "Inbox", std::format("{} waiting", waiting), kTextWarn);
                if (rel.messageSentThisTurn) statLine(ui, "Sent", "Message sent this turn", kTextDim);
                break;
            }
            case Tab::Trade: {
                const int maxPct = int(ui.rules().setting("Maximum Trade Percentage", 20));
                const int tradePct = game::diplomacy::tradePercent(ui.rules(), ui.state(), ui.session.player(), them.id);
                statLine(ui, "Trade", std::format("{}% of {}%", tradePct, maxPct));
                std::string what;
                if (game::treatyTradesResources(rel.treaty)) what = "Resources";
                if (game::treatyTradesResearch(rel.treaty)) what += ", research";
                if (rel.treaty == Treaty::Partnership) what += ", intelligence";
                statLine(ui, "Shares", what.empty() ? "Nothing" : what, what.empty() ? kTextDim : kTextGood);
                if (game::treatyTradesResources(rel.treaty) && tradePct < maxPct) statLine(ui, "Growth", "+1% per turn", kTextDim);
                if (game::treatySharesSight(rel.treaty)) statLine(ui, "Sight", "Shared", kTextGood);
                if (game::treatyAllowsResupply(rel.treaty)) statLine(ui, "Supply", "Our ships may resupply", kTextGood);
                break;
            }
            case Tab::Tariff: {
                const bool subj = rel.treaty == Treaty::Subjugation, prot = rel.treaty == Treaty::Protectorate;
                if (!subj && !prot) {
                    statLine(ui, "Tariff", "None", kTextDim);
                    break;
                }
                const int64_t pct = subj ? ui.rules().setting("Treaty Subjugated Resource Percentage", 0)
                                         : ui.rules().setting("Treaty Protectorate Resource Percentage", 0);
                const std::string share = pct > 0 ? std::format("{}%", pct) : std::string("a share");
                statLine(ui, "Tariff", rel.dominant ? std::format("They pay us {}", share) : std::format("We pay them {}", share),
                         rel.dominant ? kTextGood : kTextBad);
                statLine(ui, "Of", "Resources produced", kTextDim);
                break;
            }
        }
    }

    Tab tab_ = Tab::Treaty;
    int page_ = 0;
};

// ---- Borders ------------------------------------------------------------------------------------

// A window of its own over Empires (spec 06 §7 Q96, confirmed: binary): the
// known empires in a narrow list at (18,52), 70 px wide, each with a check box
// and its flag; the galaxy map at (94,52), 476x329, with the systems the
// checked empires claim in their colours and those several of them claim in
// yellow; a click on a system claims it for the player's empire or gives the
// claim up. Select All, Allies (the player and its allies), Enemies and Us
// (lit at opening: the player alone) check those empires.
class BordersScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Borders", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const auto shown = usAndKnown(ui);
        if (!opened_) {
            apply(ui, Filter::Us);
            opened_ = true;
        }
        d.beginContent(576);
        const ImU32 blue = imColor(palette::kLabel);
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {17, 36}, blue, "Empires");
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {94, 36}, blue, "Systems claimed by Empires");

        // The empires: a check box and the flag on each row.
        ImGui::SetCursorScreenPos(d.at({18, 52}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        beginList(ui, "##empires", ui.size({70, 329}), kRowH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        const Sprite lamp = ui.art.region("Pictures/Game/General.bmp", 190, 0, 13, 13);
        for (EmpireId e : shown) {
            ImGui::PushID(int(e.index()));
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const bool on = std::find(checked_.begin(), checked_.end(), e) != checked_.end();
            if (ImGui::InvisibleButton(s.empire(e).name.c_str(), ImVec2(w, ui.px(kRowH)))) {
                if (on) std::erase(checked_, e);
                else checked_.push_back(e);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.empire(e).name.c_str());
            ImGui::PopID();
            const ImVec2 box{a.x + ui.px(1), a.y + ui.px(1)};
            dl->AddRect(box, {box.x + ui.px(16), box.y + ui.px(17)}, imColor(palette::kButton));
            if (on && lamp) drawSprite(dl, lamp, {box.x + ui.px(2), box.y + ui.px(2)}, {box.x + ui.px(15), box.y + ui.px(15)});
            if (const Sprite flag = ui.art.flag(s.empire(e).race.style, false))
                drawSprite(dl, flag, {a.x + ui.px(20), a.y + ui.px(3)}, {a.x + ui.px(38), a.y + ui.px(16)});
            else
                dl->AddRectFilled({a.x + ui.px(20), a.y + ui.px(3)}, {a.x + ui.px(38), a.y + ui.px(16)}, empireColor(s, e));
        }
        endList(ui);
        ImGui::PopStyleVar(2);

        // The map.
        ImGui::SetCursorScreenPos(d.at({94, 52}));
        QuadrantMapOptions opt;
        opt.claimsOf = checked_;
        opt.frameColor = palette::kFrameLight;
        opt.warpLines = false;
        const QuadrantMapResult r = quadrantMap(ui, "##borders", {476, 329}, opt);
        if (r.clicked) {
            const auto& mine = ui.me().claimedSystems;
            const bool claimed = std::binary_search(mine.begin(), mine.end(), *r.clicked);
            const game::CommandResult res = ui.session.issue(game::cmd::SetSystemFlags{*r.clicked, std::nullopt, !claimed});
            error_ = res.ok ? std::string{} : res.error;
        }
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {569, 390}, blue, "(click on a system to claim it for your empire)");
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {94, 394}, blue, "Legend");
        dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(d.at({100, 418}), ui.px(3.5f), imColor(map_style::kYellow));
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {108, 410}, IM_COL32_WHITE, "Contested");
        if (!error_.empty()) textAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {200, 410}, imColor(0xff8070), error_);
        ImGui::SetCursorScreenPos(d.at({17, 52}));
        ImGui::Dummy(ImVec2(0, 0));

        d.beginButtons();
        static constexpr std::array<std::pair<Filter, const char*>, 4> kFilters{
            {{Filter::All, "Select All"}, {Filter::Allies, "Allies"}, {Filter::Enemies, "Enemies"}, {Filter::Us, "Us"}}};
        for (const auto& [f, label] : kFilters)
            if (d.tab(label, filter_ == f)) apply(ui, f);
        d.close();
        return d.keepOpen();
    }

private:
    enum class Filter { All, Allies, Enemies, Us };
    static constexpr float kRowH = 20.0f;   // the list's rows (inferred)

    void apply(const UiContext& ui, Filter f) {
        filter_ = f;
        const game::GameState& s = ui.state();
        const EmpireId me = ui.session.player();
        checked_.clear();
        for (EmpireId e : usAndKnown(ui)) {
            bool in = false;
            switch (f) {
                case Filter::All: in = true; break;
                case Filter::Us: in = e == me; break;
                case Filter::Allies: in = e == me || game::allied(s, me, e); break;
                case Filter::Enemies: in = e != me && game::hostile(s, me, e); break;
            }
            if (in) checked_.push_back(e);
        }
    }

    bool opened_ = false;
    Filter filter_ = Filter::Us;
    std::vector<EmpireId> checked_;
    std::string error_;
};

// ---- Treaty Grid -----------------------------------------------------------------------------------

// A text in the current table cell, cut short with "…" to the cell (the whole
// text as a tooltip), placed by `align` (0 left, 0.5 centred), and reported
// to assert-fits (input scripts).
void cellFitted(std::string_view text, ImU32 color, float align = 0.0f) {
    const float w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float tw = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    if (align > 0.0f && tw < w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((w - tw) * align));
    fittedText(text, std::min(w, std::max(1.0f, ImGui::GetContentRegionAvail().x)), color);
}

// The first three letters of a name, for a column heading (whole UTF-8 characters).
std::string firstLetters(const std::string& name) {
    size_t end = 0;
    for (int letters = 0; end < name.size() && letters < 3; ++letters) {
        ++end;
        while (end < name.size() && (static_cast<unsigned char>(name[end]) & 0xC0) == 0x80) ++end;
    }
    return name.substr(0, end);
}

// An empire's small flag and name in a table cell, the name cut to the cell.
void empireCell(UiContext& ui, EmpireId e, Vec2 flagSize) {
    const game::GameState& s = ui.state();
    const game::Empire& emp = s.empire(e);
    const float textH = ImGui::GetTextLineHeight();
    const float y = ImGui::GetCursorPosY();
    const float flagY = y + std::max(0.0f, (textH - ui.px(flagSize.y)) * 0.5f);
    ImGui::SetCursorPosY(flagY);
    if (const Sprite flag = ui.art.flag(emp.race.style, flagSize.x > 20)) {
        image(ui, flag, flagSize);
    } else {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ui.px(flagSize.x), p.y + ui.px(flagSize.y)), empireColor(s, e));
        ImGui::Dummy(ui.size(flagSize));
    }
    ImGui::SameLine(0, ui.px(4));
    ImGui::SetCursorPosY(y + std::max(0.0f, (ui.px(flagSize.y) - textH) * 0.5f));
    cellFitted(emp.name, empireColor(s, e));
}

// The grid of the original's large dialog (780x475, spec 06 §1): ten empires
// a page, their columns sharing the width the row names leave, so every page
// keeps the same places. Each column is headed by the empire's flag over the
// first letters of its name (the whole name on hover); every code is drawn
// whole, and a name too long for its place ends in "…" with a tooltip. With
// more empires than fit above the legend, the rows scroll.
class TreatyGridScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Treaty Grid", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const auto all = usAndKnown(ui);
        d.beginContent();
        heading(ui, "Treaties between empires");
        ImGui::TextColored(kTextDim, "?? = unknown: we see the treaties of our allies only.");

        const size_t first = size_t(page_) * kPerPage;
        const size_t cols = std::min(all.size() - std::min(all.size(), first), size_t(kPerPage));
        // The legend's room is kept below the grid.
        const float legendH = ImGui::GetTextLineHeightWithSpacing() * 5.0f + ui.px(16);
        const float rowH = std::max(ImGui::GetTextLineHeight(), ui.px(13)) + ui.px(4);
        const float headH = ui.px(13) + ImGui::GetTextLineHeight() + ui.px(6);
        const float needed = headH + rowH * float(all.size()) + ui.px(8);
        const float gridH = std::max(headH + rowH * 3.0f, std::min(needed, ImGui::GetContentRegionAvail().y - legendH));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(2), ui.px(2)));
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        if (beginListTable(ui, "##grid", kPerPage + 1, flags, ImVec2(0, gridH), rowH)) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("Empire", ImGuiTableColumnFlags_WidthFixed, ui.px(kNameW));
            for (int c = 0; c < kPerPage; ++c) ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            // Header: each empire's flag over the first letters of its name.
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers, headH);
            ImGui::TableSetColumnIndex(0);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui.px(13));
            cellFitted("Empire", ImGui::ColorConvertFloat4ToU32(kTextBlue));
            for (size_t c = 0; c < cols; ++c) {
                ImGui::TableSetColumnIndex(int(c) + 1);
                const game::Empire& e = s.empire(all[first + c]);
                const ImVec2 cellMin = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, std::floor((w - ui.px(18)) * 0.5f)));
                image(ui, ui.art.flag(e.race.style, false), {18, 13});
                cellFitted(firstLetters(e.name), empireColor(s, e.id), 0.5f);
                if (ImGui::IsMouseHoveringRect(cellMin, ImVec2(cellMin.x + w, cellMin.y + headH)) && ImGui::IsWindowHovered())
                    ImGui::SetTooltip("%s", e.name.c_str());
            }
            for (EmpireId row : all) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                ImGui::TableSetColumnIndex(0);
                empireCell(ui, row, {18, 13});
                for (size_t c = 0; c < cols; ++c) {
                    ImGui::TableSetColumnIndex(int(c) + 1);
                    const EmpireId col = all[first + c];
                    if (row == col) continue;
                    if (!treatyVisibleTo(s, ui.session.player(), row, col)) {
                        cellFitted("??", ImGui::ColorConvertFloat4ToU32(kTextDim), 0.5f);
                        continue;
                    }
                    const Treaty t = s.empire(row).relation(col).treaty;
                    cellFitted(treatyCode(t), treatyColor(t), 0.5f);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s - %s: %s", s.empire(row).name.c_str(), s.empire(col).name.c_str(),
                                          std::string(game::displayName(t)).c_str());
                }
            }
            endListTable(ui);
        }
        ImGui::PopStyleVar();
        // Legend: three columns, each as wide as its longest name needs.
        ImGui::Dummy(ui.size({0, 4}));
        heading(ui, "Legend");
        struct Entry {
            std::string_view code;
            ImU32 color;
            std::string_view name;
        };
        std::vector<Entry> entries;
        for (int i = 0; i < int(Treaty::Count); ++i) {
            const auto t = static_cast<Treaty>(i);
            entries.push_back({treatyCode(t), treatyColor(t), game::displayName(t)});
        }
        entries.push_back({"??", ImGui::ColorConvertFloat4ToU32(kTextDim), "Unknown"});
        constexpr int kLegendColumns = 3;
        std::array<float, kLegendColumns> widths{};
        for (size_t i = 0; i < entries.size(); ++i)
            widths[i % kLegendColumns] = std::max(widths[i % kLegendColumns], ImGui::CalcTextSize(entries[i].name.data(), entries[i].name.data() + entries[i].name.size()).x);
        if (ImGui::BeginTable("##legend", kLegendColumns, ImGuiTableFlags_None)) {
            for (int c = 0; c < kLegendColumns; ++c)
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, ui.px(28) + widths[size_t(c)] + ui.px(8));
            for (const Entry& e : entries) {
                ImGui::TableNextColumn();
                fittedText(e.code, ui.px(26), e.color);
                ImGui::SameLine(ui.px(28));   // from the column's start
                fittedText(e.name);
            }
            ImGui::EndTable();
        }
        d.beginButtons();
        pageButtons(d, page_, all.size(), kPerPage);
        d.close();
        return d.keepOpen();
    }

private:
    static constexpr int kPerPage = 10;
    static constexpr float kNameW = 124;   // the row names' column (frame pixels); the ten empire columns share the rest
    int page_ = 0;
};

// ---- Scores ------------------------------------------------------------------------------------------

// A figure as wide as `width` (ImGui units) allows: whole as formatNumber
// writes it, else in thousands, millions or billions ("123k", "4.5M").
std::string numberFitting(int64_t v, float width) {
    std::string full = formatNumber(v);
    auto fits = [&](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x <= width; };
    if (fits(full)) return full;
    const int64_t a = v < 0 ? -v : v;
    const char* sign = v < 0 ? "-" : "";
    std::vector<std::string> shorter;
    for (const auto& [unit, letter] : {std::pair<int64_t, char>{1'000'000'000, 'B'}, {1'000'000, 'M'}, {1'000, 'k'}}) {
        if (a < unit) continue;
        const int64_t tenths = a * 10 / unit;
        if (tenths < 100) shorter.push_back(std::format("{}{}.{}{}", sign, tenths / 10, tenths % 10, letter));
        shorter.push_back(std::format("{}{}{}", sign, a / unit, letter));
    }
    for (const std::string& t : shorter)
        if (fits(t)) return t;
    return shorter.empty() ? full : shorter.back();
}

// The scores as the spec describes the original's window, "flags with
// score, resources, research, ..., rank" (spec 06 §1.5): a column per empire,
// headed by its flag over the first letters of its name (the whole name on
// hover), ten to a page, best rank first; a row per figure, named in full.
// That fits the original's large dialog (780x475, spec 06 §1): v0.9.0 gave
// each empire a row of eleven figures, which had to be cut to fit. A figure
// too wide for its column is given in thousands, millions or billions, with
// the exact value on hover.
class ScoresScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Scores", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        d.beginContent();
        heading(ui, "Scores");
        static constexpr std::array<const char*, 3> kDisplay{"Only our own statistics are shown in this game.",
                                                             "Statistics are shown for us and empires at Non-Aggression or better.",
                                                             "Every empire's statistics are public in this game."};
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kTextDim, "%s", s.gameOver ? "The game is over: every score is shown." : kDisplay[size_t(std::clamp(s.options.scoreDisplay, 0, 2))]);
        ImGui::PopTextWrapPos();

        // Rank among every living empire.
        std::vector<std::pair<int64_t, EmpireId>> ranking;
        for (const game::Empire& e : s.empires)
            if (e.alive) ranking.push_back({game::score::empireScore(r, s, e.id), e.id});
        std::stable_sort(ranking.begin(), ranking.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        auto rankOf = [&](EmpireId e) {
            for (size_t i = 0; i < ranking.size(); ++i)
                if (ranking[i].second == e) return int(i) + 1;
            return 0;
        };
        std::vector<EmpireId> order;
        if (s.options.scoreDisplay == 2 || s.gameOver) {
            for (const auto& [score, e] : ranking) order.push_back(e);
        } else {
            order = usAndKnown(ui);
            std::stable_sort(order.begin(), order.end(), [&](EmpireId a, EmpireId b) {
                return statsVisible(ui, a) != statsVisible(ui, b) ? statsVisible(ui, a) : rankOf(a) < rankOf(b);
            });
        }
        const size_t first = size_t(page_) * kPerPage;
        const size_t cols = std::min(order.size() - std::min(order.size(), first), size_t(kPerPage));

        static constexpr std::array<Metric, 11> kRows{Metric::Score, Metric::Resources, Metric::Research, Metric::Intelligence,
                                                      Metric::TechLevels, Metric::Systems, Metric::Planets, Metric::Population,
                                                      Metric::Units, Metric::Ships, Metric::Bases};
        const float rowH = ImGui::GetTextLineHeight() + ui.px(4);
        const float headH = ui.px(13) + ImGui::GetTextLineHeight() + ui.px(6);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(2), ui.px(2)));
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        const float tableH = std::min(ImGui::GetContentRegionAvail().y, headH + rowH * float(kRows.size() + 1) + ui.px(8));
        if (beginListTable(ui, "##scores", kPerPage + 1, flags, ImVec2(0, tableH), rowH)) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("Statistic", ImGuiTableColumnFlags_WidthFixed, ui.px(kNameW));
            for (int c = 0; c < kPerPage; ++c) ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            // Header: each empire's flag over the first letters of its name.
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers, headH);
            for (size_t c = 0; c < cols; ++c) {
                ImGui::TableSetColumnIndex(int(c) + 1);
                const game::Empire& e = s.empire(order[first + c]);
                const ImVec2 cellMin = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, std::floor((w - ui.px(18)) * 0.5f)));
                image(ui, ui.art.flag(e.race.style, false), {18, 13});
                cellFitted(firstLetters(e.name), empireColor(s, e.id), 0.5f);
                if (ImGui::IsMouseHoveringRect(cellMin, ImVec2(cellMin.x + w, cellMin.y + headH)) && ImGui::IsWindowHovered())
                    ImGui::SetTooltip("%s", e.name.c_str());
            }
            auto row = [&](std::string_view label, auto&& value) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                ImGui::TableSetColumnIndex(0);
                cellFitted(label, ImGui::ColorConvertFloat4ToU32(kTextBlue));
                for (size_t c = 0; c < cols; ++c) {
                    ImGui::TableSetColumnIndex(int(c) + 1);
                    value(order[first + c]);
                }
            };
            for (Metric m : kRows)
                row(m == Metric::Population ? std::string("Population (M)") : std::string(metricName(m)), [&](EmpireId e) {
                    if (!statsVisible(ui, e)) {
                        cellFitted("-", ImGui::ColorConvertFloat4ToU32(kTextDim), 1.0f);
                        return;
                    }
                    const int64_t v = m == Metric::Score ? game::score::empireScore(r, s, e) : metricValue(game::score::currentStats(r, s, e), m);
                    const std::string shown = numberFitting(v, ImGui::GetContentRegionAvail().x);
                    cellFitted(shown, ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
                    if (shown != formatNumber(v) && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", formatNumber(v).c_str());
                });
            row("Rank", [&](EmpireId e) {
                if (statsVisible(ui, e)) cellFitted(std::to_string(rankOf(e)), ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
                else cellFitted("?", ImGui::ColorConvertFloat4ToU32(kTextDim), 1.0f);
            });
            endListTable(ui);
        }
        ImGui::PopStyleVar();
        d.beginButtons();
        pageButtons(d, page_, order.size(), kPerPage);
        if (d.button("Comparisons")) ui.open(ScreenId::Comparisons);
        d.close();
        return d.keepOpen();
    }

private:
    static constexpr int kPerPage = 10;
    static constexpr float kNameW = 100;   // the figures' names (frame pixels); the ten empire columns share the rest
    int page_ = 0;
};

// ---- Comparisons ----------------------------------------------------------------------------------------

class ComparisonsScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Comparisons", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        std::vector<EmpireId> candidates;
        for (EmpireId e : usAndKnown(ui))
            if (statsVisible(ui, e)) candidates.push_back(e);
        if (selected_.empty()) selected_ = candidates;

        d.beginContent();
        heading(ui, std::string(metricName(metric_)).c_str());
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "over time. Tick empires on the right to compare them.");
        const float legendW = ui.px(190);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##graph", ImVec2(avail.x - legendW - ImGui::GetStyle().ItemSpacing.x, 0));
        graph(ui);
        ImGui::EndChild();
        ImGui::SameLine();
        beginList(ui, "##legend", ImVec2(0, 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        for (EmpireId e : candidates) {
            ImGui::PushID(int(e.index()));
            bool on = std::find(selected_.begin(), selected_.end(), e) != selected_.end();
            if (ImGui::Checkbox("##on", &on)) {
                if (on) selected_.push_back(e);
                else std::erase(selected_, e);
            }
            ImGui::SameLine();
            empireLabel(ui, e, false, true);
            ImGui::PopID();
        }
        if (candidates.size() < s.empires.size())
            wrappedText("Other empires' statistics are hidden unless we are allied with them.", kTextDim);
        endList(ui);

        d.beginButtons();
        for (int i = 0; i < int(Metric::Count); ++i) {
            const auto m = static_cast<Metric>(i);
            if (d.tab(std::string(metricName(m)).c_str(), metric_ == m)) metric_ = m;
        }
        d.close();
        return d.keepOpen();
    }

private:
    void graph(UiContext& ui) {
        const game::GameState& s = ui.state();
        struct Line {
            EmpireId empire;
            std::vector<std::pair<uint32_t, int64_t>> points;
        };
        std::vector<Line> lines;
        uint32_t t0 = UINT32_MAX, t1 = 0;
        int64_t vmax = 0;
        for (EmpireId e : selected_) {
            if (!statsVisible(ui, e)) continue;
            Line l{e, {}};
            for (const game::TurnStats& st : statsSeries(ui.rules(), s, e)) {
                const int64_t v = metricValue(st, metric_);
                l.points.push_back({st.turn, v});
                t0 = std::min(t0, st.turn);
                t1 = std::max(t1, st.turn);
                vmax = std::max(vmax, v);
            }
            if (!l.points.empty()) lines.push_back(std::move(l));
        }
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##plot", size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float left = ui.px(64), bottom = ui.px(24), top = ui.px(8), right = ui.px(12);
        const ImVec2 a{p0.x + left, p0.y + top};
        const ImVec2 b{p0.x + size.x - right, p0.y + size.y - bottom};
        dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(2, 5, 12, 255));
        dl->AddRect(a, b, IM_COL32(44, 79, 158, 255));
        if (lines.empty()) {
            dl->AddText(ImVec2(a.x + ui.px(12), a.y + ui.px(12)), IM_COL32(140, 158, 184, 255), "No statistics to show.");
            return;
        }
        if (t1 == t0) t1 = t0 + 1;
        // A rounded vertical scale with about five steps.
        const int64_t step = niceStep(vmax, 5);
        const int64_t top_v = std::max<int64_t>(step, step * ((vmax + step - 1) / step));
        auto px = [&](uint32_t t, int64_t v) {
            const float fx = float(t - t0) / float(t1 - t0);
            const float fy = float(double(v) / double(top_v));
            return ImVec2(a.x + fx * (b.x - a.x), b.y - fy * (b.y - a.y));
        };
        const ImU32 gridC = IM_COL32(16, 34, 74, 255), labelC = IM_COL32(140, 158, 184, 255);
        for (int64_t v = 0; v <= top_v; v += step) {
            const ImVec2 q = px(t0, v);
            dl->AddLine(ImVec2(a.x, q.y), ImVec2(b.x, q.y), gridC);
            const std::string label = formatNumber(v);
            const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            dl->AddText(ImVec2(a.x - ts.x - ui.px(6), q.y - ts.y * 0.5f), labelC, label.c_str());
        }
        float lastRight = -1e9f;
        for (int i = 0; i <= 4; ++i) {
            const uint32_t t = t0 + uint32_t((t1 - t0) * uint32_t(i) / 4u);
            const ImVec2 q = px(t, 0);
            const std::string label = formatDate(t);
            const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            const float x = std::clamp(q.x - ts.x * 0.5f, a.x, b.x - ts.x);
            if (x < lastRight + ui.px(8)) continue;
            lastRight = x + ts.x;
            dl->AddLine(ImVec2(q.x, a.y), ImVec2(q.x, b.y), gridC);
            dl->AddText(ImVec2(x, b.y + ui.px(4)), labelC, label.c_str());
        }
        for (const Line& l : lines) {
            const ImU32 c = empireColor(s, l.empire);
            for (size_t i = 1; i < l.points.size(); ++i) {
                // Gaps: no line across turns with no record.
                if (l.points[i].first > l.points[i - 1].first + 1) continue;
                dl->AddLine(px(l.points[i - 1].first, l.points[i - 1].second), px(l.points[i].first, l.points[i].second), c, 2.0f);
            }
            for (const auto& [t, v] : l.points)
                if (l.points.size() == 1 || t == l.points.back().first) dl->AddCircleFilled(px(t, v), ui.px(3.5f), c);
        }
    }

    Metric metric_ = Metric::Score;
    std::vector<EmpireId> selected_;
};

// ---- History -------------------------------------------------------------------------------------------

class HistoryScreen final : public Screen {
public:
    explicit HistoryScreen(const ScreenArgs& a) : empire_(a.empire) {}

    bool draw(UiContext& ui) override {
        Dialog d(ui, "History", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const EmpireId me = ui.session.player();
        // One list per empire plus the General list (spec 05 §3.4); the lines
        // come from our long record, which outlives the log.
        const auto all = historyEmpires(s, me);
        if (!general_ && (!validEmpire(s, empire_) || std::find(all.begin(), all.end(), empire_) == all.end())) empire_ = me;
        const EmpireId list = general_ ? EmpireId{} : empire_;
        const auto events = historyLines(ui.rules(), s, me, list, !general_ && statsVisible(ui, empire_));

        d.beginContent();
        if (general_) ImGui::TextUnformatted("General");
        else empireLabel(ui, empire_, true);
        ImGui::SameLine();
        ImGui::TextColored(kTextDim, "Timeline, newest first. Select an event to see where it happened.");
        const float mapSize = ui.px(330);
        beginList(ui, "##events", ImVec2(ImGui::GetContentRegionAvail().x - mapSize - ImGui::GetStyle().ItemSpacing.x, 0), kListLineStep,
                  ImGuiChildFlags_AlwaysUseWindowPadding);
        if (events.empty()) ImGui::TextColored(kTextDim, "Nothing recorded yet.");
        else if (ImGui::BeginTable("##timeline", 2, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, ui.px(60));
            ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
            for (size_t i = 0; i < events.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(i));
                if (ImGui::Selectable(formatDate(events[i].turn).c_str(), selected_ == int(i), ImGuiSelectableFlags_SpanAllColumns))
                    selected_ = int(i);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(events[i].text.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndTable();
        }
        endList(ui);
        ImGui::SameLine();
        ImGui::BeginGroup();
        MiniMapStyle style;
        style.owners = false;
        if (!general_)
            for (const auto& c : s.colonies)
                if (c && c->owner == empire_ && (empire_ == me || ui.me().hasExplored(s.galaxy.object(c->planet).system)))
                    style.fills.push_back({s.galaxy.object(c->planet).system, empireColor(s, empire_)});
        if (selected_ >= 0 && size_t(selected_) < events.size() && events[size_t(selected_)].where)
            style.highlight.push_back(events[size_t(selected_)].where->system);
        miniMap(ui, "##historyMap", {330, 330}, style);
        ImGui::TextColored(kTextDim, general_ ? "The selected event's system is highlighted." : "Known colonies in the empire's colour.");
        ImGui::EndGroup();

        d.beginButtons();
        for (EmpireId k : all) {
            ImGui::PushID(int(k.index()));
            if (d.tab(k == me ? "Our Empire" : s.empire(k).name.c_str(), !general_ && k == empire_)) {
                empire_ = k;
                general_ = false;
                selected_ = -1;
            }
            ImGui::PopID();
        }
        if (d.tab("General", general_)) {
            general_ = true;
            selected_ = -1;
        }
        d.close();
        return d.keepOpen();
    }

private:
    EmpireId empire_;
    bool general_ = false;
    int selected_ = -1;
};

// ---- Race Report ---------------------------------------------------------------------------------------

// A report window of its own, like every report opened on its own (spec 06 §1.4,
// §1.10.1, §7 Q87, confirmed: binary): borderless, 310x420, centred; the pages
// 290x327 at (10,10), the four 72x30 tabs Detail, Descr, Race and Tech at
// (10,340), a 153x30 Close button centred under them at (79,380). It is modal
// over the window it was opened from (Empires, Communicate), which it closes
// with; Esc closes it too (inferred, as the Combat Piece Report).
class RaceReportScreen final : public Screen {
public:
    explicit RaceReportScreen(const ScreenArgs& a) : empire_(a.empire) {}
    bool closesWithParent() const override { return true; }

    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        if (!validEmpire(s, empire_)) empire_ = ui.session.player();
        const game::Empire& e = s.empire(empire_);
        const Vec2 size{310, 420};
        const Vec2 min{std::floor((frameW() - size.x) * 0.5f), std::floor((frameH() - size.y) * 0.5f)};
        ImGui::SetNextWindowPos(ui.at(min), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ui.size(size), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const bool open = ImGui::Begin("Race Report", nullptr,
                                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoScrollWithMouse | ui.windowFlags());
        ImGui::PopStyleVar(2);
        bool keep = true;
        if (open) {
            if (ImGui::IsWindowAppearing() && !ui.behind) ImGui::SetWindowFocus();
            ui.tagWindow(ui.at(min), ui.at(min + size));
            drawWindowFrame(ui.painter(), ImGui::GetWindowDrawList(), Rect{min, min + size}, nullptr, 0);
            ImGui::SetCursorScreenPos(ui.at(min + Vec2{10, 10}));
            if (tab_ == Tab::Detail) {
                ImGui::BeginChild("##detail", ui.size({290, kPageH}), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
                detail(ui, e);
                ImGui::EndChild();
            } else {
                // The other pages flow and scroll with the lists' arrow column.
                const Painter list = listPainter(ui);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.size({2, 2}));
                beginList(list, tab_ == Tab::Descr ? "##descr" : tab_ == Tab::Race ? "##race" : "##tech", ui.size({290, kPageH}), kListLineStep,
                          ImGuiChildFlags_AlwaysUseWindowPadding, false);
                ImGui::PopStyleVar();
                switch (tab_) {
                    case Tab::Descr: descr(ui, e); break;
                    case Tab::Race: race(ui, e); break;
                    case Tab::Tech: tech(ui, e); break;
                    case Tab::Detail: break;
                }
                endList(list);
            }
            static constexpr std::array<std::tuple<Tab, int, const char*>, 4> kTabs{
                {{Tab::Detail, 0, "Detail"}, {Tab::Descr, 5, "Descr"}, {Tab::Race, 6, "Race"}, {Tab::Tech, 7, "Tech"}}};
            ImGui::SetCursorScreenPos(ui.at(min + Vec2{10, 340}));
            for (size_t i = 0; i < kTabs.size(); ++i) {
                const auto& [tab, column, label] = kTabs[i];
                if (i > 0) ImGui::SameLine(0, 0);
                ImGui::PushID(int(i));
                if (reportTab(ui, column, label, tab == tab_)) {
                    audio().play("button");
                    tab_ = tab;
                }
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ui.at(min + Vec2{79, 380}));
            if (classicButton(ui, "Close", {153, 30})) keep = false;
            ui.tagItem("race-report:close");
            if (!ui.behind && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput)
                keep = false;
        }
        ImGui::End();
        if (!keep) audio().play("close");
        return keep;
    }

private:
    enum class Tab { Detail, Descr, Race, Tech };
    static constexpr float kPageH = 327.0f;

    // The object report's places (spec 06 §1.10.1, §5.4): the 128x128 picture at
    // the top left with the owner's flag on it; the name in the Button face from x
    // 120 at y 4, right-aligned 10 px from the page's right edge when too long; the
    // lines' labels in label blue at x 130 from y 20 every 30 px, each value in
    // white at x 140 15 px under its label. Each text keeps to its place
    // (drawFitted).
    void detail(UiContext& ui, const game::Empire& e) {
        const Painter p = ui.painter();
        const ImVec2 o = ImGui::GetWindowPos();
        auto at = [&](float x, float y) { return ImVec2(o.x + ui.px(x), o.y + ui.px(y)); };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto put = [&](const Sprite& sp, float x, float y, float w, float h) {
            if (sp) dl->AddImage(ImTextureRef(static_cast<ImTextureID>(sp.tex.value)), at(x, y), at(x + w, y + h), {sp.uv.min.x, sp.uv.min.y}, {sp.uv.max.x, sp.uv.max.y});
        };
        put(ui.art.racePortrait(e.race.style), 0, 0, 128, 128);
        put(ui.art.flag(e.race.style), 2, 2, 26, 18);
        ImFont* button = ui.fonts.bold ? ui.fonts.bold : ImGui::GetFont();
        ImFont* body = ui.fonts.medium ? ui.fonts.medium : ImGui::GetFont();
        const std::string& name = e.race.name.empty() ? e.name : e.race.name;
        if (const TextFit wide = fitText(p, button, kTitleSize, name, ui.px(290 - 10 - 120)); !wide.cut && wide.size >= p.fontPx(kTitleSize) - 0.01f)
            drawFitted(p, dl, button, kTitleSize, at(120, 4 + kTitleLead), ui.px(290 - 10 - 120), IM_COL32_WHITE, name);
        else   // kept clear of the picture
            drawFitted(p, dl, button, kTitleSize, at(130, 4 + kTitleLead), ui.px(290 - 10 - 130), IM_COL32_WHITE, name, 1.0f);
        std::vector<std::pair<const char*, std::string>> lines;
        lines.emplace_back("Empire", e.empireType.empty() ? e.name : std::format("{} {}", e.name, e.empireType));
        lines.emplace_back("Leader", std::format("{} {}", e.leaderTitle, e.leaderName));
        lines.emplace_back("Race", e.race.name);
        lines.emplace_back("Homeworld", std::format("{}, {}", e.race.nativeSurface, e.race.atmosphere));
        if (const ruleset::Culture* c = ui.rules().culture(e.race)) lines.emplace_back("Culture", c->name);
        const auto& happiness = ui.rules().data().happinessModels;
        if (e.race.happinessModel < happiness.size()) lines.emplace_back("Happiness", happiness[e.race.happinessModel].name);
        if (!e.race.demeanor.empty()) lines.emplace_back("Demeanor", e.race.demeanor);
        // The race age for every race, the experience behind it only for our own (spec 02 §9, §11).
        lines.emplace_back("Age", std::string(game::economy::raceAge(e.experience)));
        if (e.id == ui.session.player()) {
            lines.emplace_back("Experience", formatNumber(e.experience));
        } else {
            const game::Relation& rel = ui.me().relation(e.id);
            lines.emplace_back("Treaty", rel.contact ? treatyText(rel) : "No contact");
        }
        std::string player = game::isNeutral(e) ? "Neutral" : e.kind == game::PlayerKind::Human ? "Human" : "Computer";
        if (!e.alive) player += " (destroyed)";
        lines.emplace_back("Player", std::move(player));
        const ImU32 label = ImGui::ColorConvertFloat4ToU32(kLabelBlue);
        float y = 20;
        for (const auto& [l, value] : lines) {
            drawFitted(p, dl, body, kTextSize, at(130, y + kTextLead), ui.px(290 - 130), label, l, 0.0f, ui.px(15));
            drawFitted(p, dl, body, kTextSize, at(140, y + 15 + kTextLead), ui.px(290 - 140), IM_COL32_WHITE, value, 0.0f, ui.px(15));
            script::reportText(l, at(130, y), at(290, y + 30));
            y += 30;
        }
        ImGui::Dummy(ui.size({290, std::min(kPageH, y)}));
    }

    void descr(UiContext& ui, const game::Empire& e) {
        const std::array<std::pair<const char*, const std::string*>, 3> parts{
            {{"Biology", &e.race.biology}, {"Society", &e.race.society}, {"History", &e.race.history}}};
        bool any = false;
        for (const auto& [title, text] : parts) {
            if (text->empty()) continue;
            any = true;
            heading(ui, title);
            const ReadingText reading(ui.painter());   // the page scrolls
            wrappedText(*text);
            ImGui::Spacing();
        }
        if (!any) ImGui::TextColored(kTextDim, "No description.");
    }

    void race(UiContext& ui, const game::Empire& e) {
        heading(ui, "Characteristics");
        if (ImGui::BeginTable("##chars", 2, ImGuiTableFlags_RowBg)) {
            const float value = ImGui::CalcTextSize("000%").x + ImGui::GetStyle().CellPadding.x * 2;
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, value);
            for (size_t i = 0; i < game::kCharacteristics; ++i) {
                const int v = e.race.characteristics[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                fittedText(game::displayName(static_cast<game::Characteristic>(i)));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(v > 100 ? kTextGood : v < 100 ? kTextBad : ImVec4(0.9f, 0.92f, 0.97f, 1), "%d%%", v);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        heading(ui, "Traits");
        const auto& traits = ui.rules().data().racialTraits;
        if (e.race.traits.empty()) ImGui::TextColored(kTextDim, "None");
        for (uint32_t t : e.race.traits) {
            if (t >= traits.size()) continue;
            ImGui::PushTextWrapPos(0.0f);
            ImGui::BulletText("%s", traits[t].name.c_str());
            ImGui::PopTextWrapPos();
            if (!traits[t].description.empty() && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ui.px(320));
                ImGui::TextUnformatted(traits[t].description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }

    void tech(UiContext& ui, const game::Empire& e) {
        const bool known = e.id == ui.session.player() || ui.me().relation(e.id).treaty == Treaty::Partnership;
        if (!known) {
            wrappedText("We know the technology of our own empire and of our partners only.", kTextDim);
            return;
        }
        const game::Rules& r = ui.rules();
        const auto [owned, total] = techProgress(r, ui.state(), e);
        ImGui::TextColored(kLabelBlue, "Tech levels");
        ImGui::SameLine();
        ImGui::Text("%d of %d", owned, total);
        if (ImGui::BeginTable("##tech", 2, ImGuiTableFlags_RowBg)) {
            const float value = ImGui::CalcTextSize("00 / 00").x + ImGui::GetStyle().CellPadding.x * 2;
            ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, value);
            for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
                const ruleset::TechAreaId a{i};
                if (e.techLevel(a) <= 0) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                fittedText(r.tech(a).name);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d / %d", e.techLevel(a), r.tech(a).maxLevel);
            }
            ImGui::EndTable();
        }
    }

    EmpireId empire_;
    Tab tab_ = Tab::Detail;
};

// ---- Victory Conditions ------------------------------------------------------------------------------

class VictoryScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        Dialog d(ui, "Victory Conditions", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const game::VictoryConditions& v = s.options.victory;
        const auto all = usAndKnown(ui);

        d.beginContent();
        heading(ui, "Victory Conditions");
        ImGui::SameLine();
        const bool any = v.score || v.years || v.percentOfSecond || v.techPercent || v.peace;
        ImGui::TextColored(kTextDim, "%s", any ? "Checked at the end of every turn; the first condition met ends the game."
                                             : "None set: nothing ends the game automatically.");
        if (s.gameOver) {
            ImGui::TextColored(kTextWarn, "The game is over; the Scores window shows the final ranking.");
            if (validEmpire(s, s.winner)) {
                ImGui::SameLine();
                ImGui::TextColored(kTextWarn, "Best score:");
                ImGui::SameLine();
                empireLabel(ui, s.winner);
            }
        }

        // Scores of every living empire, for the "% of second place" rule.
        std::vector<std::pair<int64_t, EmpireId>> scores;
        for (const game::Empire& e : s.empires)
            if (e.alive) scores.push_back({game::score::empireScore(r, s, e.id), e.id});
        std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        auto bestOther = [&](EmpireId e) {
            for (const auto& [sc, id] : scores)
                if (id != e) return sc;
            return int64_t{0};
        };
        const uint32_t years10 = s.turn;  // tenths of a year elapsed

        const size_t first = size_t(page_) * kPerPage;
        const size_t cols = std::min(all.size() - std::min(all.size(), first), size_t(kPerPage));
        // Scroll sideways only when the columns do not fit.
        const float cellPad = 2 * ImGui::GetStyle().CellPadding.x + 1;
        const bool scroll = ui.px(280 + 100 * float(cols)) + float(cols + 2) * cellPad > ImGui::GetContentRegionAvail().x;
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                                      (scroll ? ImGuiTableFlags_ScrollX : ImGuiTableFlags_NoHostExtendX);
        const float rowH = ImGui::GetTextLineHeight() + 2 * ImGui::GetStyle().CellPadding.y + 1;
        if (ImGui::BeginTable("##victory", int(cols) + 2, flags, ImVec2(0, scroll ? rowH * 7 + ImGui::GetStyle().ScrollbarSize + ui.px(4) : 0))) {
            ImGui::TableSetupScrollFreeze(1, 0);
            ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthFixed, ui.px(170));
            ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthFixed, ui.px(110));
            for (size_t c = 0; c < cols; ++c)
                ImGui::TableSetupColumn(s.empire(all[first + c]).name.c_str(), ImGuiTableColumnFlags_WidthFixed, ui.px(100));
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kTextBlue, "Condition");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(kTextBlue, "Game");
            for (size_t c = 0; c < cols; ++c) {
                ImGui::TableSetColumnIndex(int(c) + 2);
                empireLabel(ui, all[first + c]);
            }
            auto row = [&](bool on, const std::string& name, const std::string& game, auto&& cell) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(on ? ImVec4(0.92f, 0.94f, 1, 1) : kTextDim, "%s", name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(on ? ImVec4(0.92f, 0.94f, 1, 1) : kTextDim, "%s", on ? game.c_str() : "Off");
                for (size_t c = 0; c < cols; ++c) {
                    ImGui::TableSetColumnIndex(int(c) + 2);
                    const EmpireId e = all[first + c];
                    if (!on) continue;
                    if (!statsVisible(ui, e)) {
                        ImGui::TextColored(kTextDim, "?");
                        continue;
                    }
                    cell(e);
                }
            };
            auto pct = [](int64_t a, int64_t b) { return b > 0 ? a * 100 / b : 0; };
            row(v.score, std::format("Score of {}", formatNumber(v.scoreValue)), "First to reach", [&](EmpireId e) {
                const int64_t sc = game::score::empireScore(r, s, e);
                ImGui::TextColored(sc >= v.scoreValue ? kTextGood : ImVec4(0.9f, 0.92f, 0.97f, 1), "%lld%%",
                                   static_cast<long long>(pct(sc, v.scoreValue)));
            });
            row(v.years, std::format("After {} years", v.yearsValue),
                std::format("{}.{} of {} years", years10 / 10, years10 % 10, v.yearsValue), [&](EmpireId e) {
                    ImGui::Text("Score %s", formatNumber(game::score::empireScore(r, s, e)).c_str());
                });
            row(v.percentOfSecond, std::format("{}% of second place", v.percentOfSecondValue), "Leader vs. next", [&](EmpireId e) {
                const int64_t sc = game::score::empireScore(r, s, e), other = bestOther(e);
                const int64_t p = other > 0 ? sc * 100 / other : (sc > 0 ? 999 : 0);
                ImGui::TextColored(p >= v.percentOfSecondValue ? kTextGood : ImVec4(0.9f, 0.92f, 0.97f, 1), "%lld%%", static_cast<long long>(p));
            });
            row(v.techPercent, std::format("{}% of all technology", v.techPercentValue), "Tech levels owned", [&](EmpireId e) {
                if (e != ui.session.player() && ui.me().relation(e).treaty != Treaty::Partnership) {
                    ImGui::TextColored(kTextDim, "?");
                    return;
                }
                const auto [owned, total] = techProgress(r, s, s.empire(e));
                const int64_t p = pct(owned, total);
                ImGui::TextColored(p >= v.techPercentValue ? kTextGood : ImVec4(0.9f, 0.92f, 0.97f, 1), "%lld%%", static_cast<long long>(p));
            });
            row(v.peace, std::format("{} years of peace", v.peaceYears),
                std::format("{}.{} years so far", s.peacefulTurns / 10, s.peacefulTurns % 10), [&](EmpireId) {});
            row(v.delay, std::format("No victory before {} years", v.delayYears),
                years10 >= uint32_t(v.delayYears) * 10 ? std::string("Passed") : std::format("{}.{} years left", (uint32_t(v.delayYears) * 10 - years10) / 10,
                                                                                          (uint32_t(v.delayYears) * 10 - years10) % 10),
                [&](EmpireId) {});
            ImGui::EndTable();
        }
        ImGui::Spacing();
        wrappedText("Progress toward each condition. Other empires' figures are shown only when their statistics are visible to us.",
                    kTextDim);
        d.beginButtons();
        pageButtons(d, page_, all.size(), kPerPage);
        d.close();
        return d.keepOpen();
    }

private:
    static constexpr int kPerPage = 10;
    int page_ = 0;
};

} // namespace

std::unique_ptr<Screen> makeEmpires(const ScreenArgs&) { return std::make_unique<EmpiresScreen>(); }
std::unique_ptr<Screen> makeBorders(const ScreenArgs&) { return std::make_unique<BordersScreen>(); }
std::unique_ptr<Screen> makeTreatyGrid(const ScreenArgs&) { return std::make_unique<TreatyGridScreen>(); }
std::unique_ptr<Screen> makeScores(const ScreenArgs&) { return std::make_unique<ScoresScreen>(); }
std::unique_ptr<Screen> makeComparisons(const ScreenArgs&) { return std::make_unique<ComparisonsScreen>(); }
std::unique_ptr<Screen> makeHistory(const ScreenArgs& args) { return std::make_unique<HistoryScreen>(args); }
std::unique_ptr<Screen> makeRaceReport(const ScreenArgs& args) { return std::make_unique<RaceReportScreen>(args); }
std::unique_ptr<Screen> makeVictoryConditions(const ScreenArgs&) { return std::make_unique<VictoryScreen>(); }

} // namespace opense4::client::classic
