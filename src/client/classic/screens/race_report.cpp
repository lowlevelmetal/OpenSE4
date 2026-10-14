// The Race Report (race_report.hpp; docs/spec/06 §1.4, §7 Q87, Q120).

#include "client/classic/screens/race_report.hpp"

#include "client/audio.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/script/items.hpp"

#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <tuple>

namespace opense4::client::classic {

namespace {

constexpr float kPageH = 327.0f;

bool validEmpire(const game::GameState& s, game::EmpireId e) { return e.valid() && e.index() < s.empires.size(); }

// The Treaty line's colour (spec 06 §1.4, confirmed: binary): red for war,
// non-intercourse and no contact; yellow for none, non-aggression,
// subjugation and protectorate; green from trade alliance up. The colours'
// values are the named ones (inferred).
ImU32 treatyLineColor(bool contact, game::Treaty t) {
    using game::Treaty;
    if (!contact) return IM_COL32(255, 0, 0, 255);
    switch (t) {
        case Treaty::War:
        case Treaty::NonIntercourse: return IM_COL32(255, 0, 0, 255);
        case Treaty::None:
        case Treaty::NonAggression:
        case Treaty::Subjugation:
        case Treaty::Protectorate: return IM_COL32(255, 255, 0, 255);
        case Treaty::TradeAlliance:
        case Treaty::TradeResearchAlliance:
        case Treaty::MilitaryAlliance:
        case Treaty::Partnership: return IM_COL32(0, 255, 0, 255);
        case Treaty::Count: break;
    }
    return IM_COL32_WHITE;
}

} // namespace

bool RaceReportView::draw(UiContext& ui, Vec2 min) {
    const game::GameState& s = ui.state();
    if (!validEmpire(s, empire_)) empire_ = ui.session.player();
    if (!validEmpire(s, empire_)) return false;
    const game::Empire& e = s.empire(empire_);
    drawWindowFrame(ui.painter(), ImGui::GetWindowDrawList(), Rect{min, min + kSize}, nullptr, 0);
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
    bool keep = !classicButton(ui, "Close", {153, 30});
    ui.tagItem("race-report:close");
    if (!ui.behind && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput)
        keep = false;
    if (!keep) audio().play("close");
    return keep;
}

// The original's Detail page (spec 06 §1.4, §7 Q120, confirmed: binary): the
// race's 128x128 portrait at the top left, no flag on it; the empire's name in
// the Button face at y 4 from x 120, further left when it is long. For another
// empire, labels in label blue at x 130 every 30 px from y 20 (Treaty, Trade,
// Mood), each value in white at x 140 15 px under it, the treaty in its
// colour. For every empire, labels at x 4 with the values at x 140 on the same
// line: Age, Culture, Demeanor at y 134, 148 and 162, Home Atmosphere and Home
// Planet Type at 190 and 204, Race Directory at 232; for our own, Experience
// Points at 260. Each text keeps to its place (drawFitted).
void RaceReportView::detail(UiContext& ui, const game::Empire& e) const {
    const game::GameState& s = ui.state();
    const Painter p = ui.painter();
    const ImVec2 o = ImGui::GetWindowPos();
    auto at = [&](float x, float y) { return ImVec2(o.x + ui.px(x), o.y + ui.px(y)); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (const Sprite portrait = ui.art.racePortrait(e.race.style))
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(portrait.tex.value)), at(0, 0), at(128, 128), {portrait.uv.min.x, portrait.uv.min.y},
                     {portrait.uv.max.x, portrait.uv.max.y});
    ImFont* button = ui.fonts.bold ? ui.fonts.bold : ImGui::GetFont();
    ImFont* body = ui.fonts.medium ? ui.fonts.medium : ImGui::GetFont();
    constexpr float kRight = 290 - 10;
    if (const TextFit wide = fitText(p, button, kTitleSize, e.name, ui.px(kRight - 120)); !wide.cut && wide.size >= p.fontPx(kTitleSize) - 0.01f)
        drawFitted(p, dl, button, kTitleSize, at(120, 4 + kTitleLead), ui.px(kRight - 120), IM_COL32_WHITE, e.name);
    else   // longer: it starts further left, ending 10 px from the page's right edge
        drawFitted(p, dl, button, kTitleSize, at(0, 4 + kTitleLead), ui.px(kRight), IM_COL32_WHITE, e.name, 1.0f);
    const ImU32 label = ImGui::ColorConvertFloat4ToU32(kLabelBlue);
    const game::EmpireId me = ui.session.player();
    if (e.id != me) {
        // Treaty, Trade and Mood toward us; a conquered empire's "Conquered", "None", "None".
        std::string treaty, trade, mood;
        ImU32 treatyColor = IM_COL32_WHITE;
        if (!e.alive) {
            treaty = "Conquered";
            trade = mood = "None";
        } else {
            const game::Relation& rel = ui.me().relation(e.id);
            treaty = rel.contact ? std::string(game::displayName(rel.treaty)) : std::string("Unknown");
            treatyColor = treatyLineColor(rel.contact, rel.treaty);
            trade = std::format("{}%", game::diplomacy::tradePercent(ui.rules(), s, me, e.id));
            mood = std::string(moodWord(e.relation(me).anger));
        }
        const std::array<std::tuple<const char*, std::string, ImU32>, 3> lines{
            {{"Treaty", treaty, treatyColor}, {"Trade", trade, IM_COL32_WHITE}, {"Mood", mood, IM_COL32_WHITE}}};
        float y = 20;
        for (const auto& [l, value, color] : lines) {
            drawFitted(p, dl, body, kTextSize, at(130, y + kTextLead), ui.px(290 - 130), label, l, 0.0f, ui.px(15));
            drawFitted(p, dl, body, kTextSize, at(140, y + 15 + kTextLead), ui.px(290 - 140), color, value, 0.0f, ui.px(15));
            script::reportText(l, at(130, y), at(290, y + 30));
            y += 30;
        }
    }
    std::string directory = e.race.style;
    if (!directory.empty()) directory[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(directory[0])));
    const ruleset::Culture* culture = ui.rules().culture(e.race);
    std::vector<std::pair<float, std::pair<const char*, std::string>>> lines{
        {134.0f, {"Age", std::string(game::economy::raceAge(e.experience))}},
        {148.0f, {"Culture", culture ? culture->name : std::string()}},
        {162.0f, {"Demeanor", e.race.demeanor}},
        {190.0f, {"Home Atmosphere", e.race.atmosphere}},
        {204.0f, {"Home Planet Type", e.race.nativeSurface}},
        {232.0f, {"Race Directory", directory}},
    };
    if (e.id == me) lines.push_back({260.0f, {"Experience Points", formatNumber(e.experience)}});
    for (const auto& [y, line] : lines) {
        const auto& [l, value] = line;
        drawFitted(p, dl, body, kTextSize, at(4, y + kTextLead), ui.px(140 - 4 - 2), label, l, 0.0f, ui.px(14));
        drawFitted(p, dl, body, kTextSize, at(140, y + kTextLead), ui.px(290 - 140), IM_COL32_WHITE, value, 0.0f, ui.px(14));
        script::reportText(l, at(4, y), at(290, y + 14));
    }
    ImGui::Dummy(ui.size({290, kPageH}));
}

void RaceReportView::descr(UiContext& ui, const game::Empire& e) const {
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

void RaceReportView::race(UiContext& ui, const game::Empire& e) const {
    heading(ui, "Characteristics");
    // Each value with its level word, as Empire Setup shows it (confirmed: binary).
    if (ImGui::BeginTable("##chars", 3, ImGuiTableFlags_RowBg)) {
        const float value = ImGui::CalcTextSize("000%").x + ImGui::GetStyle().CellPadding.x * 2;
        const float word = ImGui::CalcTextSize("Non-Existant").x + ImGui::GetStyle().CellPadding.x * 2;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, value);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, word);
        for (size_t i = 0; i < game::kCharacteristics; ++i) {
            const int v = e.race.characteristics[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            fittedText(game::displayName(static_cast<game::Characteristic>(i)));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(v > 100 ? kTextGood : v < 100 ? kTextBad : ImVec4(0.9f, 0.92f, 0.97f, 1), "%d%%", v);
            ImGui::TableSetColumnIndex(2);
            const std::string_view level = game::economy::characteristicLevel(v);
            ImGui::TextUnformatted(level.data(), level.data() + level.size());
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

void RaceReportView::tech(UiContext& ui, const game::Empire& e) const {
    const bool known = e.id == ui.session.player() || ui.me().relation(e.id).treaty == game::Treaty::Partnership;
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

void RaceReportPopup::draw(UiContext& ui) {
    if (request_) {
        ImGui::OpenPopup(kRaceReportPopup);
        request_ = false;
    }
    const Vec2 min{std::floor((frameW() - RaceReportView::kSize.x) * 0.5f), std::floor((frameH() - RaceReportView::kSize.y) * 0.5f)};
    ImGui::SetNextWindowPos(ui.at(min), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ui.size(RaceReportView::kSize), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool open = ImGui::BeginPopupModal(kRaceReportPopup, nullptr,
                                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                 ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    if (!open) return;
    // Input scripts name the popup by its id (Dear ImGui names popup windows by a number).
    script::reportItem(std::string("window:") + kRaceReportPopup, ui.at(min), ui.at(min + RaceReportView::kSize));
    if (!view_.draw(ui, min)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

namespace {

// The Race Report window, modal over the window it was opened from (Empires,
// Communicate, the Combat Piece Report), which it closes with; over the main
// window when its report panel's flag opened it.
class RaceReportScreen final : public Screen {
public:
    explicit RaceReportScreen(const ScreenArgs& a) { view_.show(a.empire); }
    bool closesWithParent() const override { return true; }

    bool draw(UiContext& ui) override {
        const Vec2 min{std::floor((frameW() - RaceReportView::kSize.x) * 0.5f), std::floor((frameH() - RaceReportView::kSize.y) * 0.5f)};
        ImGui::SetNextWindowPos(ui.at(min), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ui.size(RaceReportView::kSize), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const bool open = ImGui::Begin("Race Report", nullptr,
                                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoScrollWithMouse | ui.windowFlags());
        ImGui::PopStyleVar(2);
        bool keep = true;
        if (open) {
            if (ImGui::IsWindowAppearing() && !ui.behind) ImGui::SetWindowFocus();
            ui.tagWindow(ui.at(min), ui.at(min + RaceReportView::kSize));
            keep = view_.draw(ui, min);
        }
        ImGui::End();
        return keep;
    }

private:
    RaceReportView view_;
};

} // namespace

std::unique_ptr<Screen> makeRaceReport(const ScreenArgs& args) { return std::make_unique<RaceReportScreen>(args); }

} // namespace opense4::client::classic
