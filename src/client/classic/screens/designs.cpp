// Designs (F3) and Create Design (docs/spec/06 §1.2, docs/spec/03 §4).
//
// Designs lists the empire's ship and unit designs and the enemy designs it
// has seen, with a detail pane (figures from computeDesignStats, then the
// components or, with Stats\Strategy on, the service record and default
// strategy). Create / Copy / Edit / Upgrade open Create Design:
//   ScreenArgs::text   "new", "new-unit", "copy", "edit" or "upgrade"
//   ScreenArgs::design the template for copy / edit / upgrade
//   ScreenArgs::index  the hull of a new design (optional)
// Edit is offered only for an own design that is still a prototype (nothing
// built or retrofitted to it); one in a construction queue is refused with
// the "Cannot Edit Design" box. Edit changes that design in place
// (cmd::EditDesign); Copy and Upgrade save a new design (spec 03 §4.1).

#include "client/classic/mod_ui.hpp"
#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/design_pickers.hpp"
#include "client/classic/screens/design_tools.hpp"
#include "client/classic/screens/item_reports.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"
#include "client/classic/screens/type_picker.hpp"
#include "client/script/items.hpp"
#include "client/classic/widgets.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace opense4::client::classic {

namespace {

using Kind = ItemRef::Kind;

std::string shieldsText(const game::DesignStats& st) {
    return st.phasedShields > 0 ? std::format("{} + {} phased", st.shields, st.phasedShields) : std::to_string(st.shields);
}

// Vehicles of a design in service: in space (unit groups count each unit) and carried as cargo.
int inService(const game::GameState& s, game::DesignId id) {
    int n = 0;
    auto cargo = [&](const game::Cargo& c) { n += c.unitCount(id); };
    for (const game::Vehicle& v : s.vehicles) {
        n += game::groupUnits(v, id);   // a unit group that mixes designs counts this one's units
        cargo(v.cargo);
    }
    for (const auto& c : s.colonies)
        if (c) cargo(c->cargo);
    return n;
}

std::string_view mountCode(const game::Rules& r, int32_t mount) {
    if (mount < 0 || static_cast<size_t>(mount) >= r.data().weaponMounts.size()) return {};
    return r.data().weaponMounts[static_cast<size_t>(mount)].code;
}

// The empire's design-name list (Dsgnname/<file> in the install), if any.
// `missing` is set when the race names a file that cannot be opened.
std::vector<std::string> loadNameList(UiContext& ui, bool* missing = nullptr) {
    if (missing) *missing = false;
    const std::string& file = ui.me().race.designNameFile;
    if (file.empty()) return {};
    const ruleset::GameFiles* files = ui.rules().files();
    const auto path = files ? files->path("Dsgnname/" + file) : std::nullopt;
    std::ifstream in;
    if (path) in.open(*path, std::ios::binary);
    if (!in) {
        if (missing) *missing = true;
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseNameList(text);
}

// ---- Designs -----------------------------------------------------------------------------------

enum class DesignTab : uint8_t { Ships, Units, EnemyShips, EnemyUnits };
constexpr std::array<const char*, 4> kDesignTabs{"Ship Designs", "Unit Designs", "Enemy Ship Dsgn", "Enemy Unit Dsgn"};

bool enemyTab(DesignTab t) { return t == DesignTab::EnemyShips || t == DesignTab::EnemyUnits; }
bool unitTab(DesignTab t) { return t == DesignTab::Units || t == DesignTab::EnemyUnits; }

class DesignsScreen final : public Screen {
public:
    explicit DesignsScreen(const ScreenArgs& args) : selected_(args.design) {}

    bool draw(UiContext& ui) override {
        // Designs closes while a tactical simulation is fought and opens again
        // with the simulator afterwards (spec 06 §1.10.4).
        if (tacticalSimulationRunning(ui)) {
            designsClosedForSimulation() = true;
            return false;
        }
        if (!initialized_) init(ui);
        const bool keep = drawDialog(ui);
        popup_.draw(ui);
        notice_.draw(ui);
        return keep;
    }

private:
    void init(UiContext& ui) {
        initialized_ = true;
        // The tab, Hide Obsolete and Stats\Strategy are the empire's options,
        // saved with the game: they come back on every opening (spec 08 §3.6.7).
        hideObsolete_ = ui.options().designsHideObsolete;
        statsView_ = ui.options().designsStatsView;
        tab_ = static_cast<DesignTab>(std::min<int>(ui.options().designsTab, static_cast<int>(kDesignTabs.size()) - 1));
        if (!ui.me().designs.empty()) newest_ = ui.me().designs.back();
        // Opened for a design (ours): on that design's tab.
        if (selected_.valid() && selected_.index() < ui.state().designs.size()) tab_ = tabOf(ui, ui.state().design(selected_));
        if (enemyTab(tab_)) statsView_ = false;   // on an enemy tab: no service record to show
    }

    DesignTab tabOf(const UiContext& ui, const game::Design& d) const {
        const bool unit = isUnitHull(ui.rules().hull(d.hull).type);
        if (d.owner == ui.session.player()) return unit ? DesignTab::Units : DesignTab::Ships;
        return unit ? DesignTab::EnemyUnits : DesignTab::EnemyShips;
    }

    // The tab's designs (spec 06 §7 Q93, confirmed: binary): the own tabs
    // list the empire's designs, without the obsolete ones while Hide Obsolete
    // is on; the enemy tabs list every design of another empire seen at least
    // once, Hide Obsolete or not, leaving out those of eliminated empires.
    std::vector<game::DesignId> listFor(const UiContext& ui, DesignTab tab) const {
        const game::GameState& s = ui.state();
        std::vector<game::DesignId> out;
        const std::vector<game::DesignId> source = enemyTab(tab) ? game::seenDesignIds(ui.me().knowledge) : ui.me().designs;
        for (game::DesignId id : source) {
            if (id.index() >= s.designs.size()) continue;
            const game::Design& d = s.design(id);
            if (isUnitHull(ui.rules().hull(d.hull).type) != unitTab(tab)) continue;
            if (enemyTab(tab)) {
                if (d.owner == ui.session.player()) continue;
                if (d.owner.valid() && d.owner.index() < s.empires.size() && !s.empire(d.owner).alive) continue;
            } else if (hideObsolete_ && d.obsolete) {
                continue;
            }
            out.push_back(id);
        }
        return out;
    }

    const game::Design* selectedOwn(const UiContext& ui) const {
        if (!selected_.valid() || selected_.index() >= ui.state().designs.size()) return nullptr;
        const game::Design& d = ui.state().design(selected_);
        return d.owner == ui.session.player() ? &d : nullptr;
    }

    bool drawDialog(UiContext& ui) {
        // A design made in Create Design (new designs get the highest ids): show it.
        const auto& own = ui.me().designs;
        if (!own.empty() && (!newest_.valid() || own.back() > newest_)) {
            if (newest_.valid()) {
                selected_ = own.back();
                tab_ = tabOf(ui, ui.state().design(selected_));
                note_ = std::format("Created {}.", ui.state().design(selected_).name);
                scrollToSelected_ = true;
                noteIsError_ = false;
            }
            newest_ = own.back();
        }

        const std::vector<game::DesignId> list = listFor(ui, tab_);
        if (std::find(list.begin(), list.end(), selected_) == list.end()) selected_ = list.empty() ? game::DesignId{} : list.front();

        Dialog d(ui, "Designs", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        // Our note (a design made, a refusal) in the title strip.
        if (!note_.empty()) d.titleText(150, noteIsError_ ? imColor(0xff8070) : imColor(0xffdc73), note_);
        // The original's places (spec 06 §7 Q93, confirmed: binary): "Designs"
        // over the list at (16,58), 246x393, "Design Detail" over the detail box
        // (269,58)-(573,461), the note on obsolete designs at (16,450).
        d.beginContent(576);
        const ImU32 blue = imColor(palette::kLabel);
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {16, 39}, blue, "Designs");
        if (!modColumns_) textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {269, 39}, blue, "Design Detail");
        // The mods' columns (docs/sdk/interface.md "List columns"), while Mod Columns is on:
        // the tab's designs in a list across the window; a click shows one's detail again.
        const bool modColumns = !modColumnsFor(ui, sdk::UiList::Designs).empty();
        if (!modColumns) modColumns_ = false;
        if (modColumns_) {
            ImGui::SetCursorScreenPos(d.at({16, 58}));
            std::vector<ModListRow> rows;
            for (game::DesignId id : list) {
                const game::Design& dd = ui.state().design(id);
                rows.push_back({dd.name, shipui::designMini(ui, id), {"design", static_cast<int64_t>(id.value)}});
            }
            const ModListClick click = drawModList(ui, "##designsmods", rows, sdk::UiList::Designs, ImVec2(ui.px(558), ui.px(392)), 170);
            if (click.left) {
                selected_ = list[*click.left];
                modColumns_ = false;
                scrollToSelected_ = true;
            }
        } else {
        ImGui::SetCursorScreenPos(d.at({16, 58}));
        designList(ui, list);
        textAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {16, 450}, blue, "(obsolete designs are deleted automatically)");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(d.at({269, 58}), d.at({574, 462}), imColor(palette::kButton));
        detailBottom_ = 100;
        if (selected_.valid()) detail(ui, d, ui.state().design(selected_));
        else
            textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {276, 66}, imColor(palette::kSecondary),
                   enemyTab(tab_) ? "No enemy designs of this kind seen yet." : "No designs of this kind yet.");
        }
        // For lessons: the selected design's figures, down to what is drawn of them.
        ui.tag("designs:details", d.at({269, 58}), d.at({574, std::min(462.0f, detailBottom_ + 6.0f)}));
        ImGui::SetCursorScreenPos(d.at({16, 40}));
        ImGui::Dummy(ImVec2(0, 0));
        d.beginButtons();
        buttons(ui, d);
        d.close();
        return d.keepOpen();
    }

    // The list (spec 06 §7 Q93, confirmed: binary): headings 25 px high in the
    // large silver type at x 4, resting on the row's foot, the design types'
    // names (the owners' empire names on the enemy tabs), in alphabetical
    // order; under each the designs in alphabetical order, in 36 px rows: the
    // lamp at (4,11) (green for the selected design), the hull's picture at
    // (21,0), the name in white at (61,-2), the hull's name in small type at
    // (71,12) and "Prototype" at (71,23) for a design never built. An obsolete
    // design looks like any other.
    void designList(UiContext& ui, const std::vector<game::DesignId>& list) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        constexpr float kRow = 36.0f, kHeadingRow = 25.0f;
        auto headingOf = [&](const game::Design& d) {
            if (enemyTab(tab_)) return d.owner.valid() ? s.empire(d.owner).name : std::string("Unknown");
            return d.designType.empty() ? std::string("No Type") : d.designType;
        };
        auto before = [](const std::string& x, const std::string& y) {
            return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end(), [](char p, char q) {
                return std::tolower(static_cast<unsigned char>(p)) < std::tolower(static_cast<unsigned char>(q));
            });
        };
        std::vector<std::string> headings;
        for (game::DesignId id : list)
            if (std::find(headings.begin(), headings.end(), headingOf(s.design(id))) == headings.end()) headings.push_back(headingOf(s.design(id)));
        std::stable_sort(headings.begin(), headings.end(), before);
        std::vector<game::DesignId> sorted = list;
        std::stable_sort(sorted.begin(), sorted.end(), [&](game::DesignId x, game::DesignId y) { return before(s.design(x).name, s.design(y).name); });
        const Sprite green = ui.art.region("Pictures/Game/General.bmp", 190, 0, 13, 13);
        const Sprite blue = ui.art.region("Pictures/Game/General.bmp", 177, 0, 13, 13);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        beginList(ui, "##list", ui.size({246, 393}), kRow);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowW = ImGui::GetContentRegionAvail().x;
        int id = 0;
        for (const std::string& h : headings) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
            const float th = ImGui::GetTextLineHeight();
            dl->AddText({a.x + ui.px(4), std::floor(a.y + ui.px(kHeadingRow) - th)}, imColor(palette::kHeading), h.c_str());
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(rowW, ui.px(kHeadingRow)));
            for (game::DesignId designId : sorted) {
                const game::Design& d = s.design(designId);
                if (headingOf(d) != h) continue;
                const ruleset::VehicleSize& hull = r.hull(d.hull);
                const std::string& style = d.owner.valid() ? s.empire(d.owner).race.style : std::string{};
                ImGui::PushID(id++);
                const ImVec2 b = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##row", ImVec2(rowW, ui.px(kRow)))) {
                    selected_ = designId;
                    note_.clear();
                }
                script::reportItem(d.name);   // input scripts find a design by its name
                ImGui::PopID();
                const bool selected = designId == selected_;
                if (selected && scrollToSelected_) {
                    // A design just made comes into view (ours).
                    ImGui::SetScrollHereY(0.5f);
                    scrollToSelected_ = false;
                }
                if (const Sprite& lampSprite = selected ? green : blue)
                    drawSprite(dl, lampSprite, {b.x + ui.px(4), b.y + ui.px(11)}, {b.x + ui.px(17), b.y + ui.px(24)});
                if (const Sprite pic = ui.art.designMini(style, hull, d.picture)) drawSprite(dl, pic, {b.x + ui.px(21), b.y}, {b.x + ui.px(57), b.y + ui.px(36)});
                dl->PushClipRect(b, {b.x + rowW, b.y + ui.px(kRow)}, true);
                dl->AddText({b.x + ui.px(61), b.y + ui.px(-2 + kTextLead)}, IM_COL32_WHITE, d.name.c_str());
                ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
                dl->AddText({b.x + ui.px(71), b.y + ui.px(12 + kSmallLead)}, IM_COL32_WHITE, hull.name.c_str());
                // On every tab, for any design never built (spec 06 §7 Q93,
                // confirmed: binary). A player's view of another empire's
                // design has no statistics, so there "built" is everBuilt.
                const bool prototype = game::designIsPrototype(d) && (!enemyTab(tab_) || !d.everBuilt);
                if (prototype) dl->AddText({b.x + ui.px(71), b.y + ui.px(23 + kSmallLead)}, IM_COL32_WHITE, "Prototype");
                ImGui::PopFont();
                dl->PopClipRect();
            }
        }
        if (list.empty()) ImGui::TextColored(kDimText, enemyTab(tab_) ? "None seen yet." : "None yet.");
        endList(ui);
        ImGui::PopStyleVar(2);
        ui.tagItem("designs:list");
    }

    // The detail (spec 06 §7 Q93, confirmed: binary), in the box's top part
    // (from (270,59)): the picture frame (4,4)-(134,134), the name in the large
    // font at (140,4), Size, Design Type and Date Created with their values
    // under them, "(Obsolete)" in yellow; Cost and Maintenance Cost at (4,135)
    // and (4,150); below (from (270,209)) Movement, Shields, Cargo Space and
    // Supply Capacity, values right-aligned at x 160, 15 px apart; then
    // "Components on Design" at (274,291) over the grid at (276,308), or with
    // Stats\Strategy the service record and the default strategy.
    void detail(UiContext& ui, const Dialog& dlg, const game::Design& d) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const bool own = d.owner == ui.session.player();
        const game::DesignStats st = game::computeDesignStats(r, own ? &ui.me() : nullptr, d);
        const ruleset::VehicleSize& hull = r.hull(d.hull);
        const std::string& style = d.owner.valid() ? s.empire(d.owner).race.style : std::string{};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Vec2 top{270, 59};
        const ImU32 blue = imColor(palette::kLabel), white = IM_COL32_WHITE;
        auto text = [&](Vec2 at, ImU32 color, std::string_view t) { textAt(ui, dlg, ui.fonts.regular, kTextSize, kTextLead, at, color, t); };
        // The picture in its frame; a click opens the hull's report.
        dl->AddRect(dlg.at(top + Vec2{4, 4}), dlg.at(top + Vec2{135, 135}), imColor(palette::kButton));
        ImGui::SetCursorScreenPos(dlg.at(top + Vec2{5, 5}));
        image(ui, ui.art.designPortrait(style, hull, d.picture), {128, 128});
        script::reportItem("design-picture:" + (d.picture.empty() ? std::string("hull") : d.picture));   // input scripts see which picture it shows
        hullReportOnClick(ui, d.hull);
        textAt(ui, dlg, ui.fonts.bold, kTitleSize, kTitleLead, top + Vec2{140, 4}, white, d.name);
        text(top + Vec2{140, 40}, blue, "Size");
        text(top + Vec2{152, 55}, white, std::format("{} ({}kT)", hull.name, hull.tonnage));
        text(top + Vec2{140, 70}, blue, own ? "Design Type" : "Owner");
        text(top + Vec2{152, 85}, white, own ? (d.designType.empty() ? std::string("-") : d.designType) : (d.owner.valid() ? s.empire(d.owner).name : std::string("-")));
        text(top + Vec2{140, 100}, blue, "Date Created");
        text(top + Vec2{152, 115}, white, formatDate(d.createdTurn));
        // Only for an own design: whether another empire made one obsolete is
        // its owner's business (ours; the enemy tabs list it either way).
        if (d.obsolete && own) text(top + Vec2{202, 115}, imColor(0xffff00), "(Obsolete)");
        // The three amounts after a label: right-aligned, each followed by its icon (ours).
        auto amounts = [&](float y, const game::Resources& v) {
            static constexpr std::array<float, 3> kRight{172, 225, 278};
            static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
            static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
            for (size_t i = 0; i < 3; ++i) {
                textRightAt(ui, dlg, ui.fonts.regular, kTextSize, kTextLead, top + Vec2{kRight[i], y}, imColor(kColors[i]), std::to_string(v.v[i]));
                if (const Sprite icon = ui.art.icon16(kIcons[i])) drawSprite(dl, icon, dlg.at(top + Vec2{kRight[i] + 2, y}), dlg.at(top + Vec2{kRight[i] + 18, y + 16}));
            }
        };
        text(top + Vec2{4, 135}, blue, "Cost");
        amounts(135, st.cost);
        text(top + Vec2{4, 150}, blue, "Maintenance Cost");
        amounts(150, game::economy::designMaintenance(r, s, d));
        const Vec2 low{270, 209};
        auto figure = [&](float y, const char* label, const std::string& value) {
            text(low + Vec2{4, y}, blue, label);
            textRightAt(ui, dlg, ui.fonts.regular, kTextSize, kTextLead, low + Vec2{160, y}, white, value);
        };
        figure(15, "Movement", std::to_string(st.movement));
        figure(30, "Shields", shieldsText(st));
        figure(45, "Cargo Space", std::to_string(st.cargoCapacity));
        figure(60, "Supply Capacity", std::to_string(st.supplyCapacity));
        // Components the viewing empire has not researched (the hull too).
        const game::Empire& me = ui.me();
        bool lacking = !r.hullAvailable(me, d.hull);
        for (const game::DesignEntry& e : d.entries) lacking = lacking || !r.componentAvailable(me, e.component);
        if (lacking) text(low + Vec2{4, 75}, imColor(palette::kSecondary), "(Insufficient technology)");
        if (statsView_) serviceRecord(ui, dlg, d, own);
        else components(ui, dlg, d);
    }

    void hullReportOnClick(UiContext& ui, uint32_t hull) {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click for the %s report", ui.rules().hull(hull).name.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) popup_.open({Kind::Hull, hull});
    }

    // "Components on Design" at (274,291) over the grid at (276,308), 290x146:
    // 36x36 cells, 8 to a row, 4 rows, one cell per component (identical ones
    // do not share a cell); the BigUpDownArrows pair at (501,274) when it
    // scrolls (spec 06 §7 Q93, confirmed: binary). A click or right-click opens
    // the component's report.
    void components(UiContext& ui, const Dialog& dlg, const game::Design& d) {
        const game::Rules& r = ui.rules();
        constexpr float kGridCell = 36.0f;
        constexpr int kColumns = 8, kRows = 4;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        textAt(ui, dlg, ui.fonts.regular, kTextSize, kTextLead, {274, 291}, imColor(palette::kLabel), "Components on Design");
        const int rows = (static_cast<int>(d.entries.size()) + kColumns - 1) / kColumns;
        const int hidden = std::max(0, rows - kRows);
        detailBottom_ = 308.0f + kGridCell * float(std::clamp(rows, 1, kRows));
        gridTop_ = std::clamp(gridTop_, 0, hidden);
        if (hidden > 0) {
            // BigUpDownArrows.bmp: the up arrow (column 0) over the down arrow (column 1), 64 x 17 each, a row per state.
            for (int down = 0; down < 2; ++down) {
                const Vec2 at{501, 274.0f + 17.0f * float(down)};
                const bool can = down ? gridTop_ < hidden : gridTop_ > 0;
                ImGui::SetCursorScreenPos(dlg.at(at));
                ImGui::PushID(down ? "##gridDown" : "##gridUp");
                ImGui::BeginDisabled(!can);
                if (ImGui::InvisibleButton("##arrow", ui.size({64, 17})) && can) gridTop_ += down ? 1 : -1;
                ImGui::EndDisabled();
                ImGui::PopID();
                const int look = !can ? 3 : ImGui::IsItemActive() ? 2 : ImGui::IsItemHovered() ? 1 : 0;
                if (const Sprite arrow = ui.art.region("Pictures/Game/Buttons/BigUpDownArrows.bmp", down * 64, look * 17, 64, 17, false))
                    drawSprite(dl, arrow, dlg.at(at), dlg.at(at + Vec2{64, 17}));
            }
        }
        for (size_t i = size_t(gridTop_ * kColumns); i < d.entries.size() && i < size_t((gridTop_ + kRows) * kColumns); ++i) {
            const game::DesignEntry& e = d.entries[i];
            const ruleset::Component& c = r.component(e.component);
            const int slot = static_cast<int>(i) - gridTop_ * kColumns;
            const Vec2 at{276 + kGridCell * float(slot % kColumns), 308 + kGridCell * float(slot / kColumns)};
            const ImVec2 a = dlg.at(at), z = dlg.at(at + Vec2{kGridCell, kGridCell});
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(static_cast<int>(i));
            const bool clicked = ImGui::InvisibleButton("##comp", ImVec2(z.x - a.x, z.y - a.y));
            const bool hovered = ImGui::IsItemHovered();
            const bool right = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            ImGui::PopID();
            if (const Sprite pic = ui.art.component(c.picture)) drawSprite(dl, pic, a, z);
            if (hovered) dl->AddRect(a, z, imColor(palette::kButtonHot));
            if (const std::string_view code = mountCode(r, e.mount); !code.empty()) {
                ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
                dl->AddText({a.x + ui.px(2), a.y + ui.px(1)}, IM_COL32(255, 255, 0, 255), code.data(), code.data() + code.size());
                ImGui::PopFont();
            }
            if (hovered) ImGui::SetTooltip("%s", (mountLabel(r, e.mount).empty() ? c.name : mountLabel(r, e.mount) + " " + c.name).c_str());
            if (clicked || right) popup_.open({Kind::Component, e.component, e.mount});
        }
    }

    // With Stats\Strategy on (spec 06 §7 Q93): Number Constructed, Number In
    // Service, Number Lost, Number Scrapped and Enemy Tonnage Destroyed (labels
    // at x 6, values right-aligned at x 270, 15 px apart from y 97 of the part
    // below), then "Default Strategy" with its picker.
    void serviceRecord(UiContext& ui, const Dialog& dlg, const game::Design& d, bool own) {
        const game::GameState& s = ui.state();
        const Vec2 low{270, 209};
        const ImU32 blue = imColor(palette::kLabel), white = IM_COL32_WHITE;
        auto row = [&](float y, const char* label, const std::string& value) {
            textAt(ui, dlg, ui.fonts.regular, kTextSize, kTextLead, low + Vec2{6, y}, blue, label);
            textRightAt(ui, dlg, ui.fonts.regular, kTextSize, kTextLead, low + Vec2{270, y}, white, own ? value : std::string("-"));
        };
        row(97, "Number Constructed", std::to_string(d.built));
        row(112, "Number In Service", std::to_string(inService(s, d.id)));
        row(127, "Number Lost", std::to_string(d.lost));
        row(142, "Number Scrapped", std::to_string(d.scrapped));
        row(157, "Enemy Tonnage Destroyed", std::to_string(d.enemyTonnageDestroyed));
        detailBottom_ = low.y + 172;
        if (!own) return;
        detailBottom_ = low.y + 216;
        textAt(ui, dlg, ui.fonts.regular, kTextSize, kTextLead, low + Vec2{6, 177}, blue, "Default Strategy");
        const auto& strategies = ui.me().strategies;
        const std::string current = d.strategy < strategies.size() ? strategies[d.strategy].name : std::string("Default");
        ImGui::SetCursorScreenPos(dlg.at(low + Vec2{6, 194}));
        ImGui::SetNextItemWidth(ui.px(264));
        if (ImGui::BeginCombo("##strategy", current.c_str())) {
            for (uint32_t i = 0; i < strategies.size(); ++i)
                if (ImGui::Selectable(std::format("{}##{}", strategies[i].name, i).c_str(), i == d.strategy) && i != d.strategy) {
                    const game::CommandResult res = ui.session.issue(game::cmd::SetVehicleStrategy{d.id, i});
                    if (!res.ok) {
                        note_ = res.error;
                        noteIsError_ = true;
                    }
                }
            ImGui::EndCombo();
        }
    }

    void buttons(UiContext& ui, Dialog& d) {
        static constexpr std::array<const char*, 4> kTabIds{"ship-designs", "unit-designs", "enemy-ship-designs", "enemy-unit-designs"};
        for (size_t i = 0; i < kDesignTabs.size(); ++i) {
            if (lampButton(ui, d, kDesignTabs[i], tab_ == static_cast<DesignTab>(i))) {
                tab_ = static_cast<DesignTab>(i);
                note_.clear();
                // The enemy tabs switch Stats\Strategy off (spec 06 §7 Q93).
                if (enemyTab(tab_)) setStatsView(ui, false);
                setTab(ui, tab_);
            }
            ui.tagTab(kTabIds[i], tab_ == static_cast<DesignTab>(i));
        }
        if (!modColumnsFor(ui, sdk::UiList::Designs).empty()) {
            // OpenSE4's own, in the free slot: the mods' columns over the tab's designs.
            if (d.check("Mod Columns", modColumns_)) modColumns_ = !modColumns_;
        } else {
            d.spacer();
        }
        const game::Design* own = selectedOwn(ui);
        auto openDesigner = [&](const char* mode) {
            ScreenArgs a;
            a.text = mode;
            if (own) a.design = own->id;
            ui.open(ScreenId::CreateDesign, a);
            note_.clear();
        };
        // Create asks for the vehicle type first (observed, spec 07 session 3).
        // Create and Stats\Strategy are dim on the enemy tabs; Copy, Edit,
        // Upgrade and Make Obsolete there and with nothing selected (spec 06
        // §7 Q93, confirmed: binary).
        const bool enemy = enemyTab(tab_);
        const bool create = d.button("Create", !enemy);
        ui.tagItem("designs:create");
        if (create) {
            ImGui::OpenPopup(kTypePicker);
            note_.clear();
        }
        typePicker(ui);
        if (d.button("Copy", own != nullptr)) openDesigner("copy");
        ui.tagItem("designs:copy");
        // Edit is lit for a design never built; one in a construction queue
        // is refused with a message (confirmed: binary).
        const bool editable = own && game::designIsPrototype(*own);
        if (d.button("Edit", editable)) {
            if (game::designInQueue(ui.state(), ui.me().id, own->id)) notice_.open("A design cannot be edited once it is in a construction queue.", "Cannot Edit Design");
            else openDesigner("edit");
        }
        ui.tagItem("designs:edit");
        const bool upgrade = d.button("Upgrade", own != nullptr);
        ui.tagItem("designs:upgrade");
        // The candidate opens in the designer even when nothing changed (spec 03 §4.1).
        if (upgrade) openDesigner("upgrade");
        const bool obsolete = own && own->obsolete;
        if (d.button(obsolete ? "Make Current" : "Make Obsolete", own != nullptr)) {
            const game::CommandResult res = ui.session.issue(game::cmd::SetDesignObsolete{own->id, !obsolete});
            note_ = res.ok ? std::string{} : res.error;
            noteIsError_ = !res.ok;
        }
        // On/off settings: check boxes (observed, spec 07 session 3).
        if (d.check("Hide Obsolete", hideObsolete_)) setHideObsolete(ui, !hideObsolete_);
        if (d.check("Stats\\Strategy", statsView_, !enemy)) setStatsView(ui, !statsView_);
        if (d.button("Simulator")) ui.open(ScreenId::CombatSimulator);
        ui.tagItem("designs:simulator");
    }

    void setHideObsolete(UiContext& ui, bool on) {
        hideObsolete_ = on;
        game::InterfaceOptions o = ui.options();
        o.designsHideObsolete = on;
        ui.setOptions(o);
    }

    void setStatsView(UiContext& ui, bool on) {
        statsView_ = on;
        game::InterfaceOptions o = ui.options();
        o.designsStatsView = on;
        ui.setOptions(o);
    }

    static void setTab(UiContext& ui, DesignTab tab) {
        game::InterfaceOptions o = ui.options();
        o.designsTab = static_cast<uint8_t>(tab);
        ui.setOptions(o);
    }

    // "Select Vehicle Type": a list window with one "Name" column, the vehicle
    // types the empire has a researched hull size of; picking one opens the
    // designer for it, the same from every tab (spec 06 §7 Q94, confirmed: binary).
    static constexpr const char* kTypePicker = "Select Vehicle Type###vehicletype";
    void typePicker(UiContext& ui) {
        if (!beginModal(ui, kTypePicker, {340, 370})) return;
        const std::vector<ruleset::VehicleType> types = designableTypes(ui.rules(), ui.me());
        const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
        static constexpr ListColumn kName[] = {{"Name", 0, 0, false}};
        listHeader(ui, "##typehead", kName, listRowsWidth(ui, ImGui::GetContentRegionAvail().x));
        beginList(ui, "##types", ImVec2(0, -footer), kListLineStep);
        std::optional<ruleset::VehicleType> picked;
        for (ruleset::VehicleType t : types) {
            if (ImGui::Selectable(std::string(ruleset::displayName(t)).c_str(), false)) picked = t;
            ui.tagOption("designs:create", learn::vehicleTypeId(t));   // a lesson may let one kind through
        }
        if (types.empty()) ImGui::TextColored(kDimText, "No hull is available yet.");
        endList(ui);
        const bool cancel = ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (picked) {
            ScreenArgs a;
            a.text = "new";
            a.sub = static_cast<int>(*picked);
            ui.open(ScreenId::CreateDesign, a);
        }
        if (picked || cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    bool initialized_ = false;
    DesignTab tab_ = DesignTab::Ships;
    game::DesignId selected_;
    game::DesignId newest_;  // the newest own design seen, to notice designs just created
    bool hideObsolete_ = false;
    bool modColumns_ = false;   // the mods' columns shown (OpenSE4's own)
    bool statsView_ = false;
    int gridTop_ = 0;   // the component grid's first row shown
    float detailBottom_ = 462;   // where the detail's content ends (window frame pixels), for lessons
    bool scrollToSelected_ = false;
    std::string note_;
    bool noteIsError_ = false;
    ItemReportPopup popup_;
    NoticePopup notice_;
};

// ---- Create Design -------------------------------------------------------------------------------
//
// The original's designer (spec 06 §7 Q94, confirmed: binary; places in the
// 780x475 window): titled after the vehicle type chosen first ("Ship
// Design"), it starts with no size, no design type and no name. The picture
// 131x131 at (19,40); Size, Design Type and Design Name labelled at x 155
// over 185x20 drop-down boxes at x 160 (only the name can be typed in); the
// figures box 223x140 at (349,40); "Components on Design" at (19,181) over
// the strip at (25,198), 536x38; "Components Available (<type>)" at
// (19,238) over the tile grid at (19,254), 550x114, tiles 173x38 three to a
// row; Warnings at (19,369) over a list at (19,385), 283x76, and Component
// Details at (309,369) over a box at (309,385), 264x76. Buttons: Comp Type,
// Weap Mount, To Hit Modifiers, Condensed View and Only Latest (slots 8 to
// 10), Weapons Report (11), Create Design (13), Cancel.
//
// Our client differs (GitHub issue #14): at 1024x768 the window is as tall as
// the list windows (the frame's height less 100, 668 px), and the height it
// gains goes to the Warnings and Component Details boxes, so a component's
// details show without scrolling; at 800x600 it keeps the original's 475 px.

namespace cd {
// Sums of ImGui positions (ImGui's own operators are not enabled here).
inline ImVec2 operator+(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 kPicture{19, 40};
constexpr float kLabelX = 155, kBoxX = 160, kBoxW = 185, kBoxH = 20;
constexpr Vec2 kFigures{349, 40}, kFiguresSize{223, 140};
constexpr Vec2 kStrip{25, 198}, kStripSize{536, 38};
constexpr float kCell = 36;            // a component cell in the strip
constexpr Vec2 kGrid{19, 254}, kGridSize{550, 114};
constexpr float kTileW = 173, kTileH = 38;
constexpr Vec2 kWarnings{19, 385}, kWarningsSize{283, 76};
constexpr Vec2 kDetails{309, 385}, kDetailsSize{264, 76};
} // namespace cd

using cd::operator+;

class CreateDesignScreen final : public Screen {
public:
    explicit CreateDesignScreen(const ScreenArgs& args) : args_(args) {}

    bool draw(UiContext& ui) override {
        // Designs closes while a tactical simulation is fought and opens again
        // with the simulator afterwards (spec 06 §1.10.4).
        if (tacticalSimulationRunning(ui)) {
            designsClosedForSimulation() = true;
            return false;
        }
        if (!initialized_) init(ui);
        const bool keep = drawDialog(ui);
        popup_.draw(ui);
        return keep && !done_;
    }

private:
    void setName(std::string_view name) {
        name_.fill(0);
        std::copy_n(name.begin(), std::min(name.size(), name_.size() - 1), name_.begin());
    }

    std::string name() const {
        std::string n(name_.data());
        while (!n.empty() && n.back() == ' ') n.pop_back();
        const size_t first = n.find_first_not_of(' ');
        return first == std::string::npos ? std::string{} : n.substr(first);
    }

    // The names the Design Name list offers (spec 06 §6, §7 Q94, confirmed:
    // binary): every line of the race's name file in file order, names in use
    // included; without a file, none. Blank lines are left out (ours).
    const std::vector<std::string>& nameChoices() const { return names_; }

    void pickDesignType(const UiContext& ui) {
        // Prefer a design type named after the vehicle type (fighters, satellites, ...).
        const auto& types = ui.me().designTypes;
        designType_ = types.empty() ? std::string{} : types.front();
        if (type_ == ruleset::VehicleType::Ship) return;
        const std::string_view cls = ruleset::displayName(type_);
        for (const std::string& t : types)
            if (containsNoCase(t, cls)) {
                designType_ = t;
                return;
            }
    }

    void init(UiContext& ui) {
        initialized_ = true;
        // Only Latest starts as the Empire Options' Latest Items row says (spec 06 §1.9).
        onlyLatest_ = ui.options().latestComponentsOnly;
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        // A name file the race names that cannot be opened: a "Load Error" box
        // naming it, and an empty list (spec 06 §7 Q94, confirmed: binary; the
        // words are ours).
        bool missing = false;
        names_ = loadNameList(ui, &missing);
        if (missing) notice_.open(std::format("Your design names file could not be opened: {}.", me.race.designNameFile), "Load Error");

        if (args_.design.valid() && args_.design.index() < s.designs.size()) {
            const game::Design& t = s.design(args_.design);
            hull_ = t.hull;
            type_ = r.hull(t.hull).type;
            entries_ = t.entries;
            designType_ = t.designType;
            strategy_ = t.strategy;
            picture_ = t.picture;   // a copy, an upgrade and an edit keep the design's own picture
            if (args_.text == "upgrade") {
                upgradeEntries(r, me, entries_);
                setName(nextVersionName(s, me, t.name));
            } else if (args_.text == "edit") {
                setName(t.name);
                editing_ = t.id;
            }
            // Copy clones the design with an empty name (spec 03 §4.1).
            if (t.owner != me.id) {
                pickDesignType(ui);
                strategy_ = 0;
            }
        } else {
            // The vehicle type picked in Designs; it starts with no size, no
            // design type and no name (observed). A hull given by automation is
            // taken as the size.
            if (args_.sub >= 0 && args_.sub < static_cast<int>(ruleset::VehicleType::Count)) type_ = static_cast<ruleset::VehicleType>(args_.sub);
            else if (args_.text == "new-unit") {
                for (ruleset::VehicleType t : designableTypes(r, me))
                    if (isUnitHull(t)) {
                        type_ = t;
                        break;
                    }
            }
            if (args_.index >= 0 && static_cast<size_t>(args_.index) < r.data().vehicleSizes.size()) {
                hull_ = static_cast<uint32_t>(args_.index);
                type_ = r.hull(*hull_).type;
            }
            // No design type yet: its box reads "Design Type" (observed, spec 06 §7 Q94).
            designType_.clear();
        }
        mounts_ = hull_ ? hullMounts(r, me, *hull_) : std::vector<uint32_t>{};
        title_ = designWindowTitle(type_);
    }

    // A hull of the vehicle type for what does not need the size chosen (the
    // components that suit the type).
    uint32_t probeHull(const UiContext& ui) const {
        if (hull_) return *hull_;
        const auto hulls = hullsOfType(ui.rules(), ui.me(), type_);
        if (!hulls.empty()) return hulls.front();
        for (uint32_t i = 0; i < ui.rules().data().vehicleSizes.size(); ++i)
            if (ui.rules().hull(i).type == type_) return i;
        return 0;
    }

    // The pictures the design may show (OpenSE4's own, docs/sdk/packages-and-data.md
    // "A design's own picture"): its hull's, the hull's other picture, and the
    // ship pictures mods add for the race. "" stands for the hull's.
    std::vector<std::string> pictureChoices(UiContext& ui) const {
        std::vector<std::string> out{std::string{}};
        if (!hull_) return out;
        const ruleset::VehicleSize& h = ui.rules().hull(*hull_);
        const std::string& style = ui.me().race.style;
        auto offered = [&](std::string_view name) {
            return std::any_of(out.begin(), out.end(), [&](const std::string& o) { return datafile::keysEqual(o, name); }) ||
                   datafile::keysEqual(name, h.primaryBitmap);
        };
        if (!h.alternateBitmap.empty() && !offered(h.alternateBitmap) && ui.art.hasShipPicture(style, h.alternateBitmap)) out.push_back(h.alternateBitmap);
        for (const std::string& name : ui.art.modShipPictures(style))
            if (!offered(name)) out.push_back(name);
        // A picture the design shows that is offered no more (a mod's not here) stays one to keep.
        if (!picture_.empty() && !offered(picture_)) out.push_back(picture_);
        return out;
    }

    bool hasPictureChoice(UiContext& ui) {
        if (choicesFor_ != hull_) {
            pictures_ = pictureChoices(ui);
            choicesFor_ = hull_;
        }
        return pictures_.size() >= 2;
    }

    // A small arrow in the picture's corner opens the choice, when there is one.
    static constexpr Vec2 kPictureArrow = cd::kPicture + Vec2{114, 114};
    void pictureChoice(UiContext& ui, const Dialog& d) {
        if (!hasPictureChoice(ui)) return;
        const ImVec2 back = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(d.at(kPictureArrow));
        if (arrowButton(ui, "##pictures", ArrowGlyph::Down, {16, 16}, true)) ImGui::OpenPopup("##picturelist");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The design's picture");
        ImGui::SetCursorScreenPos(back);
        if (!ImGui::BeginPopup("##picturelist")) return;
        const ruleset::VehicleSize& h = ui.rules().hull(*hull_);
        const std::string& style = ui.me().race.style;
        for (size_t i = 0; i < pictures_.size(); ++i) {
            const std::string& name = pictures_[i];
            const std::string caption = name.empty() ? std::format("{} (the hull's)", h.primaryBitmap) : name;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable(std::format("##picture{}", i).c_str(), datafile::keysEqual(picture_, name), 0, ui.size({200, 38}))) picture_ = name;
            script::reportItem("picture:" + (name.empty() ? std::string("hull") : name));   // input scripts pick a picture by its name
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            if (const Sprite mini = ui.art.designMini(style, h, name)) drawSprite(pdl, mini, {at.x + ui.px(1), at.y + ui.px(1)}, {at.x + ui.px(37), at.y + ui.px(37)});
            pdl->AddText({at.x + ui.px(44), at.y + ui.px(11)}, IM_COL32_WHITE, caption.c_str());
        }
        ImGui::EndPopup();
    }

    void setHull(const UiContext& ui, uint32_t hull) {
        hullChosen_ = true;
        if (hull_ == hull) return;
        // The other hull's own second picture is no choice for this one.
        if (hull_ && datafile::keysEqual(picture_, ui.rules().hull(*hull_).alternateBitmap)) picture_.clear();
        hull_ = hull;
        mounts_ = hullMounts(ui.rules(), ui.me(), hull);
        if (mount_ >= 0 && std::find(mounts_.begin(), mounts_.end(), static_cast<uint32_t>(mount_)) == mounts_.end()) mount_ = -1;
    }

    // Cancel in "Select Vehicle Size" leaves the design without a size
    // (spec 06 §7 Q94); its components stay.
    void clearHull(const UiContext& ui) {
        hullChosen_ = false;
        if (!hull_) return;
        if (datafile::keysEqual(picture_, ui.rules().hull(*hull_).alternateBitmap)) picture_.clear();
        hull_.reset();
        mounts_.clear();
        mount_ = -1;
    }

    // The warnings (spec 06 §7 Q94, confirmed: binary): the requirements the
    // design does not meet yet, a size first. The name is never one of them:
    // Create Design checks it after them.
    static std::vector<std::string> problems(const std::optional<game::DesignStats>& st) {
        std::vector<std::string> out;
        if (!st) out.emplace_back("Choose a size for the design");
        else out.insert(out.end(), st->problems.begin(), st->problems.end());
        return out;
    }

    bool drawDialog(UiContext& ui) {
        Dialog d(ui, title_.c_str(), DialogSize::LargeTall);
        if (!d.open()) return d.keepOpen();
        // The height beyond the original's 475 px, for the lower boxes.
        lowerExtra_ = std::max(0.0f, d.size().y - 475.0f);
        // For lessons: the design being built.
        ui.facts.designComponents = static_cast<int64_t>(entries_.size());
        ui.facts.designHullChosen = hullChosen_;
        ui.facts.designType = designType_;
        ui.facts.designNamed = !name().empty() && !designNameTaken(ui.state(), ui.me(), name());
        ui.facts.designVehicle = learn::vehicleTypeId(type_);
        std::optional<game::DesignStats> st;
        if (hull_) st = game::computeDesignStats(ui.rules(), &ui.me(), *hull_, entries_);
        hovered_.reset();
        toHit_ = ui.options().designToHit;
        condensed_ = ui.options().designCondensed;
        d.beginContent(576);
        topRow(ui, d, st);
        strip(ui, d);
        grid(ui, d);
        sideBoxes(ui, d, st);
        ImGui::SetCursorScreenPos(d.at({19, 40}));
        ImGui::Dummy(ImVec2(0, 0));
        if (hovered_) hover_ = hovered_;
        d.beginButtons();
        buttons(ui, d, st);
        if (d.close(true, "Cancel")) return false;
        return d.keepOpen();
    }

    // A label in label blue at a place of the window.
    static void label(UiContext& ui, const Dialog& d, Vec2 at, std::string_view text) {
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, at, imColor(palette::kLabel), text);
    }

    // A 185x20 drop-down box at (160, y) showing `value` (else `empty` in grey)
    // with its arrow at the right; returns true when it is pressed.
    bool dropBox(UiContext& ui, const Dialog& d, const char* id, float y, const std::string& value, const char* empty, bool enabled = true) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = d.at({cd::kBoxX, y}), b = d.at({cd::kBoxX + cd::kBoxW, y + cd::kBoxH});
        dl->AddRect(a, b, imColor(palette::kFrameLight));
        dl->PushClipRect(a, {b.x - ui.px(cd::kBoxH), b.y}, true);
        dl->AddText({a.x + ui.px(4), a.y + ui.px(2)}, value.empty() ? imColor(palette::kSecondary) : IM_COL32_WHITE, value.empty() ? empty : value.c_str());
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(d.at({cd::kBoxX + cd::kBoxW - cd::kBoxH, y}));
        return arrowButton(ui, id, ArrowGlyph::Down, {cd::kBoxH, cd::kBoxH}, enabled);
    }

    // "Select Vehicle Size" (spec 06 §7 Q94, confirmed: binary): a list
    // window of the empire's researched hulls of the vehicle type in data
    // order (ours: the hull of a design being edited stays listed), columns
    // Pic, Name, Size (the tonnage) and Cost (minerals, organics and
    // radioactives in their colours), and under the list an "Abilities" box
    // for the hull under the pointer, else the design's (inferred). A click
    // on a row picks that size; Cancel leaves the design without one, even
    // one chosen before (inferred, as Cancel in Design Type's list).
    static constexpr const char* kSizePicker = "Select Vehicle Size###vehiclesize";
    void sizePicker(UiContext& ui) {
        if (!beginModal(ui, kSizePicker, {480, 450})) return;
        const game::Rules& r = ui.rules();
        const std::vector<uint32_t> hulls = hullsOfType(r, ui.me(), type_, hull_);
        // For lessons: the smallest hull (the first of equals) and the others.
        const auto smallest = std::min_element(hulls.begin(), hulls.end(), [&](uint32_t a, uint32_t b) { return r.hull(a).tonnage < r.hull(b).tonnage; });
        static constexpr ListColumn kColumns[] = {{kPicHeading, 40, 0, false}, {"Name", 0, 0, false}, {"Size", 60, 0, false}, {"Cost", 150, 0, false}};
        constexpr float kAbilitiesH = 84;
        const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
        const float below = ui.px(kAbilitiesH + 18) + ImGui::GetStyle().ItemSpacing.y * 2;
        const float rowsW = listRowsWidth(ui, ImGui::GetContentRegionAvail().x);
        listHeader(ui, "##sizehead", kColumns, rowsW);
        const std::vector<float> edges = columnEdges(ui, kColumns, rowsW);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        beginList(ui, "##sizes", ImVec2(0, -(footer + below)), kListRowH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        std::optional<uint32_t> picked, hovered;
        for (auto h = hulls.begin(); h != hulls.end(); ++h) {
            const ruleset::VehicleSize& v = r.hull(*h);
            const ImVec2 a = ImGui::GetCursorScreenPos();
            // Named after the hull (input scripts find it by "##<index>"); the text is drawn below.
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
            if (ImGui::Selectable(std::format("{}##{}", v.name, *h).c_str(), hull_ == *h, ImGuiSelectableFlags_None, ImVec2(w, ui.px(kListRowH)))) picked = *h;
            ImGui::PopStyleColor();
            ui.tagOption("create-design:hull", h == smallest ? "smallest" : "other");
            if (ImGui::IsItemHovered()) hovered = *h;
            if (const Sprite pic = ui.art.designMini(ui.me().race.style, v, {})) drawSprite(dl, pic, {a.x + edges[0] + ui.px(2), a.y}, {a.x + edges[0] + ui.px(38), a.y + ui.px(36)});
            const float textY = a.y + ui.px(10);
            dl->PushClipRect({a.x + edges[1], a.y}, {a.x + edges[2], a.y + ui.px(kListRowH)}, true);
            dl->AddText({a.x + edges[1] + ui.px(4), textY}, IM_COL32_WHITE, v.name.c_str());
            dl->PopClipRect();
            dl->AddText({a.x + edges[2] + ui.px(4), textY}, IM_COL32_WHITE, std::format("{}kT", v.tonnage).c_str());
            const std::array<int64_t, 3> cost{v.cost.minerals, v.cost.organics, v.cost.radioactives};
            static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
            for (size_t k = 0; k < 3; ++k)
                dl->AddText({a.x + edges[3] + ui.px(4 + 48 * float(k)), textY}, imColor(kColors[k]), formatNumber(cost[k]).c_str());
        }
        endList(ui);
        ImGui::PopStyleVar(2);
        // The Abilities box.
        ImGui::TextColored(kLabelBlue, "Abilities");
        const std::optional<uint32_t> shown = hovered ? hovered : hull_;
        ImGui::PushStyleColor(ImGuiCol_Border, imColorV(palette::kFrameLight));
        ImGui::BeginChild("##sizeabilities", ImVec2(0, ui.px(kAbilitiesH)), ImGuiChildFlags_Borders);
        ImGui::PopStyleColor();
        if (shown) {
            ImGui::PushTextWrapPos(0.0f);
            for (const ruleset::Ability& ab : r.hull(*shown).abilities) ImGui::TextUnformatted(abilityText(ab).c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        const bool cancel = ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (picked) setHull(ui, *picked);
        else if (cancel) clearHull(ui);
        if (picked || cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void topRow(UiContext& ui, const Dialog& d, const std::optional<game::DesignStats>& st) {
        const game::Rules& r = ui.rules();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // The picture: 131x131, the hull's picture drawn from (1,1); a click opens the hull's report.
        dl->AddRect(d.at(cd::kPicture), d.at(cd::kPicture + Vec2{131, 131}), imColor(palette::kButton));
        ImGui::SetCursorScreenPos(d.at(cd::kPicture + Vec2{1, 1}));
        if (hull_) {
            image(ui, ui.art.designPortrait(ui.me().race.style, r.hull(*hull_), picture_), {128, 128});
            script::reportItem("design-picture:" + (picture_.empty() ? std::string("hull") : picture_));
            // The picture's arrow (ours, over its corner) is not a click on the picture.
            const bool overArrow = hasPictureChoice(ui) && ImGui::IsMouseHoveringRect(d.at(kPictureArrow), d.at(kPictureArrow + Vec2{16, 16}));
            if (!overArrow && (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))) popup_.open({Kind::Hull, *hull_});
            pictureChoice(ui, d);
        } else {
            ImGui::Dummy(ui.size({128, 128}));
        }

        // Size: without one the box reads "Vehicle Size" (confirmed: binary).
        label(ui, d, {cd::kLabelX, 39}, "Size");
        const std::string size = hull_ ? std::format("{} ({}kT)", r.hull(*hull_).name, r.hull(*hull_).tonnage) : std::string{};
        if (dropBox(ui, d, "##size", 55, size, "Vehicle Size")) ImGui::OpenPopup(kSizePicker);
        ui.tag("create-design:hull", d.at({cd::kBoxX, 55}), d.at({cd::kBoxX + cd::kBoxW, 55 + cd::kBoxH}));
        sizePicker(ui);

        // Design Type: a new design has none, its box reading "Design Type".
        // The box opens the type list, Select Design Type (spec 06 §7 Q94,
        // confirmed: binary), whose Add Type and Delete Type change the
        // empire's design types at once.
        label(ui, d, {cd::kLabelX, 79}, "Design Type");
        if (dropBox(ui, d, "##type", 95, designType_, "Design Type")) {
            TypeListPicker::Config config;
            config.title = "Select Design Type";
            config.addTitle = "Design Type Name";
            config.limit = kMaxDesignTypes;
            config.tagChooser = "create-design:type";
            config.tagOption = [](const std::string& t) { return learn::optionId(t); };
            typePicker_.open(std::move(config), designType_.empty() ? std::nullopt : std::optional<std::string>(designType_));
        }
        ui.tag("create-design:type", d.at({cd::kBoxX, 95}), d.at({cd::kBoxX + cd::kBoxW, 95 + cd::kBoxH}));
        {
            const std::vector<std::string>& types = ui.me().designTypes;
            const TypeListPicker::Result res = typePicker_.drawPopup(ui, types);
            if (res.added || res.deleted) {
                std::vector<std::string> list = types;
                if (res.added) list.push_back(*res.added);
                if (res.deleted) std::erase(list, *res.deleted);
                ui.session.issue(game::cmd::SetDesignTypes{std::move(list)});
            }
            // Our client differs: the original's Cancel leaves the design
            // with no type; ours keeps the one it had (spec 06 §7 Q94).
            if (res.chosen) designType_ = *res.chosen;
        }

        // Design Name: typed, or picked from the race's list with its arrow;
        // without a name the box reads "Design Name".
        label(ui, d, {cd::kLabelX, 119}, "Design Name");
        dl->AddRect(d.at({cd::kBoxX, 135}), d.at({cd::kBoxX + cd::kBoxW, 135 + cd::kBoxH}), imColor(palette::kFrameLight));
        ImGui::SetCursorScreenPos(d.at({cd::kBoxX + 1, 136}));
        ImGui::SetNextItemWidth(ui.px(cd::kBoxW - cd::kBoxH - 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui.px(3), ui.px(1)));
        ImGui::InputTextWithHint("##name", "Design Name", name_.data(), name_.size());
        ImGui::PopStyleVar(2);
        ui.tag("create-design:name", d.at({cd::kBoxX, 135}), d.at({cd::kBoxX + cd::kBoxW - cd::kBoxH, 135 + cd::kBoxH}));
        ImGui::SetCursorScreenPos(d.at({cd::kBoxX + cd::kBoxW - cd::kBoxH, 135}));
        if (arrowButton(ui, "##names", ArrowGlyph::Down, {cd::kBoxH, cd::kBoxH}, true)) ImGui::OpenPopup("##namelist");
        ui.tagItem("create-design:suggest");
        if (ImGui::BeginPopup("##namelist")) {
            const std::vector<std::string>& choices = nameChoices();
            for (size_t i = 0; i < choices.size(); ++i)
                if (ImGui::Selectable(std::format("{}##{}", choices[i], i).c_str())) setName(choices[i]);
            if (choices.empty()) ImGui::Dummy(ui.size({cd::kBoxW, 1}));   // an empty drop-down list
            ImGui::EndPopup();
        }

        // The figures box: lines 16 px apart from y 4, labels at x 4, values at x 120.
        dl->AddRect(d.at(cd::kFigures), d.at(cd::kFigures + cd::kFiguresSize), imColor(palette::kButton));
        ui.tag("create-design:figures", d.at(cd::kFigures), d.at(cd::kFigures + cd::kFiguresSize));
        auto row = [&](float y, const char* name, const std::string& value, ImU32 color = IM_COL32_WHITE) {
            if (name) label(ui, d, cd::kFigures + Vec2{4, y}, name);
            if (!value.empty()) textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, cd::kFigures + Vec2{120, y}, color, value);
        };
        const bool over = st && st->tonnageUsed > st->tonnageMax;
        row(4, "Space Used", st ? std::format("{}/{}", st->tonnageUsed, st->tonnageMax) : std::string("0/0"),
            over ? ImGui::GetColorU32(kWarnText) : IM_COL32_WHITE);
        row(20, "Total Cost", {});
        static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
        static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
        for (size_t k = 0; k < 3; ++k) {
            const float y = 20.0f + 16.0f * float(k);
            row(y, nullptr, st ? formatNumber(st->cost.v[k]) : std::string("0"), imColor(kColors[k]));
            if (const Sprite icon = ui.art.icon16(kIcons[k])) drawSprite(dl, icon, d.at(cd::kFigures + Vec2{174, y}), d.at(cd::kFigures + Vec2{190, y + 16}));
        }
        row(68, "Movement", st ? std::to_string(st->movement) : std::string("0"));
        if (toHit_) {
            // To Hit Modifiers: the design's combat to-hit bonuses in place of
            // Shields, Cargo Space and Supply Capacity (spec 06 §7 Q94).
            const auto [offense, defense] = hull_ ? game::designToHit(r, *hull_, entries_) : std::pair<int64_t, int64_t>{0, 0};
            row(84, "Offense Bonus", std::format("{:+}%", offense));
            row(100, "Defense Bonus", std::format("{:+}%", defense));
        } else {
            row(84, "Shields", st ? shieldsText(*st) : std::string("0"));
            row(100, "Cargo Space", st ? std::to_string(st->cargoCapacity) : std::string("0"));
            row(116, "Supply Capacity", st ? formatNumber(st->supplyCapacity) : std::string("0"));
        }
    }

    // "Components on Design" at (19,181), the hint right-aligned to x 573, the
    // strip at (25,198), 536x38: 36 px cells between two arrows (the small
    // left and right arrows, ours); a click removes the component, a
    // right-click opens its report. Condensed View merges identical components
    // (same component and mount) into one cell with the count on it when above 1.
    void strip(UiContext& ui, const Dialog& d) {
        const game::Rules& r = ui.rules();
        label(ui, d, {19, 181}, "Components on Design");
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {573, 184}, imColor(palette::kLabel), "(click to remove component)");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 boxA = d.at(cd::kStrip), boxB = d.at(cd::kStrip + cd::kStripSize);
        dl->AddRect(boxA, boxB, imColor(palette::kFrameLight));
        struct Cell {
            game::DesignEntry entry;
            int count = 1;
            size_t remove = 0;
        };
        std::vector<Cell> cells;
        if (condensed_)
            for (const EntryGroup& g : groupEntries(entries_)) cells.push_back({g.entry, g.count, g.last});
        else
            for (size_t i = 0; i < entries_.size(); ++i) cells.push_back({entries_[i], 1, i});
        const int visible = static_cast<int>((cd::kStripSize.x - 2 * 17) / cd::kCell);
        stripFirst_ = std::clamp(stripFirst_, 0, std::max(0, static_cast<int>(cells.size()) - visible));
        auto arrow = [&](const char* id, bool left, float x, bool enabled) {
            const Vec2 at{x, cd::kStrip.y};
            ImGui::SetCursorScreenPos(d.at(at));
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::InvisibleButton(id, ui.size({16, 38}));
            ImGui::EndDisabled();
            const int look = !enabled ? 3 : ImGui::IsItemActive() ? 2 : ImGui::IsItemHovered() ? 1 : 0;
            if (const Sprite sp = ui.art.region("Pictures/Game/Buttons/SmallLeftRightArrows.bmp", left ? 0 : 16, look * 38, 16, 38, false))
                drawSprite(dl, sp, d.at(at), d.at(at + Vec2{16, 38}));
            return clicked && enabled;
        };
        if (arrow("##stripLeft", true, cd::kStrip.x, stripFirst_ > 0)) --stripFirst_;
        if (arrow("##stripRight", false, cd::kStrip.x + cd::kStripSize.x - 16, stripFirst_ + visible < static_cast<int>(cells.size()))) ++stripFirst_;
        std::optional<size_t> remove;
        for (int i = 0; i < visible && stripFirst_ + i < static_cast<int>(cells.size()); ++i) {
            const Cell& c = cells[static_cast<size_t>(stripFirst_ + i)];
            const ruleset::Component& comp = r.component(c.entry.component);
            const Vec2 at{cd::kStrip.x + 17 + cd::kCell * float(i), cd::kStrip.y + 1};
            const ImVec2 a = d.at(at), z = d.at(at + Vec2{cd::kCell, cd::kCell});
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(stripFirst_ + i);
            const bool clicked = ImGui::InvisibleButton("##cell", ImVec2(z.x - a.x, z.y - a.y));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (const Sprite pic = ui.art.component(comp.picture)) drawSprite(dl, pic, a, z);
            if (hovered) dl->AddRect(a, z, imColor(palette::kButtonHot));
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            if (const std::string_view code = mountCode(r, c.entry.mount); !code.empty())
                dl->AddText(a + ui.size({2, 1}), IM_COL32(255, 255, 0, 255), code.data(), code.data() + code.size());
            if (c.count > 1) {
                const std::string n = std::to_string(c.count);
                dl->AddText(a + ImVec2(ui.px(34) - ImGui::CalcTextSize(n.c_str()).x, ui.px(24)), IM_COL32_WHITE, n.c_str());
            }
            ImGui::PopFont();
            if (hovered) hovered_ = ItemRef{Kind::Component, c.entry.component, c.entry.mount};
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) popup_.open({Kind::Component, c.entry.component, c.entry.mount});
            if (clicked) remove = c.remove;
        }
        ui.tag("create-design:on-design", boxA, boxB);
        if (remove && *remove < entries_.size()) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*remove));
    }

    // "Components Available (<group>)" at (19,238) with the weapon mount's
    // name drawn apart at x 319 (spec 06 §7 Q94, confirmed: binary; "Normal
    // Mount" without one and white, inferred), the hint right-aligned to
    // x 573, the tiles in a list at (19,254), 550x114: 173x38 tiles of icon,
    // name and kT, three to a row; a click adds the component (with the chosen
    // mount where it applies), and does nothing before a size is chosen
    // (confirmed: binary); a right-click opens its report.
    void grid(UiContext& ui, const Dialog& d) {
        const game::Rules& r = ui.rules();
        const uint32_t hull = probeHull(ui);
        label(ui, d, {19, 238}, std::format("Components Available ({})", group_.empty() ? std::string("All") : group_));
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {319, 238}, IM_COL32_WHITE,
               mount_ >= 0 ? r.data().weaponMounts[static_cast<size_t>(mount_)].longName : std::string("Normal Mount"));
        textRightAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {573, 240}, imColor(palette::kLabel), "(click to add component)");
        ImGui::SetCursorScreenPos(d.at(cd::kGrid));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        beginList(ui, "##available", ui.size(cd::kGridSize), cd::kTileH);
        const std::vector<uint32_t> comps = designerComponents(r, ui.me(), hull, group_, onlyLatest_);
        const float tileW = ui.px(cd::kTileW), tileH = ui.px(cd::kTileH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        // Only the rows in view are submitted (input scripts count the tiles
        // shown); a row less than a pixel in view counts as out of it (scroll
        // positions are whole pixels, rows need not be).
        const float viewTop = ImGui::GetScrollY(), viewH = ImGui::GetWindowHeight();
        for (size_t i = 0; i < comps.size(); ++i) {
            const float rowTop = tileH * float(i / 3);
            if (rowTop + tileH <= viewTop + 1.0f || rowTop >= viewTop + viewH - 1.0f) continue;
            const uint32_t ci = comps[i];
            const ruleset::Component& c = r.component(ci);
            const game::DesignEntry entry{ci, hull_ ? mountFor(r, *hull_, ci, mount_) : -1};
            const game::MountedComponent m = game::mounted(r, entry);
            const ImVec2 a(origin.x + tileW * float(i % 3), origin.y + tileH * float(i / 3));
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(static_cast<int>(i));
            const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImVec2 b(a.x + tileW, a.y + tileH);
            if (hovered) dl->AddRect(a, b, imColor(palette::kButtonHot));
            // Every tile is drawn alike, whether the component fits or not,
            // with a thin line between the picture and the text (spec 06 §7
            // Q94, confirmed: binary; the line's colour is ours).
            if (const Sprite pic = ui.art.component(c.picture)) drawSprite(dl, pic, a + ui.size({1, 1}), a + ui.size({37, 37}), IM_COL32_WHITE);
            dl->AddLine(a + ui.size({38, 1}), a + ui.size({38, 37}), imColor(palette::kButton), std::max(1.0f, ui.px(1)));
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            dl->PushClipRect(a + ui.size({39, 0}), b, true);
            dl->AddText(a + ui.size({40, 3}), IM_COL32_WHITE, c.name.c_str());
            dl->AddText(a + ui.size({40, 21}), imColor(palette::kSecondary), std::format("{}kT", m.tonnage).c_str());
            dl->PopClipRect();
            if (const std::string_view code = mountCode(r, entry.mount); !code.empty())
                dl->AddText(a + ui.size({2, 1}), IM_COL32(255, 255, 0, 255), code.data(), code.data() + code.size());
            ImGui::PopFont();
            if (hovered) hovered_ = ItemRef{Kind::Component, ci, entry.mount};
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) popup_.open({Kind::Component, ci, entry.mount});
            if (clicked && hull_) {
                entries_.push_back(entry);
                // Condensed View on: the new entry joins the first of its kind.
                if (condensed_) condenseEntries(entries_);
            }
        }
        const float rows = std::ceil(float(comps.size()) / 3.0f);
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(tileW * 3.0f, std::max(1.0f, tileH * rows)));
        if (comps.empty()) {
            ImGui::SetCursorScreenPos(origin + ui.size({4, 4}));
            ImGui::TextColored(kDimText, "Nothing of this type.");
        }
        endList(ui);
        ImGui::PopStyleVar(2);
        ui.tagItem("create-design:components");
    }

    // Warnings at (19,369) over a list at (19,385), 283x76: one line of white
    // text after a small red ball per requirement the design does not meet,
    // 18 px apart (observed, spec 07 session 5); Component Details at (309,369)
    // over a box at (309,385), 264x76 (the component under the pointer, else the hull).
    // Both are taller by the window's height beyond 475 px (ours).
    void sideBoxes(UiContext& ui, const Dialog& d, const std::optional<game::DesignStats>& st) {
        const Vec2 grow{0, lowerExtra_};
        label(ui, d, {19, 369}, "Warnings");
        ImGui::SetCursorScreenPos(d.at(cd::kWarnings));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        beginList(ui, "##warnings", ui.size(cd::kWarningsSize + grow), 18);
        const std::vector<std::string> lines = problems(st);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        const float w = ImGui::GetContentRegionAvail().x;
        for (const std::string& line : lines) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled({a.x + ui.px(7), a.y + ui.px(9)}, ui.px(3.5f), IM_COL32(255, 0, 0, 255));
            dl->PushClipRect(a, {a.x + w, a.y + ui.px(18)}, true);
            dl->AddText({a.x + ui.px(14), a.y + ui.px(3 + kSmallLead)}, IM_COL32_WHITE, line.c_str());
            dl->PopClipRect();
            ImGui::Dummy(ImVec2(w, ui.px(18)));
            if (ImGui::IsItemHovered() && ImGui::CalcTextSize(line.c_str()).x > w - ui.px(14)) ImGui::SetTooltip("%s", line.c_str());
        }
        ImGui::PopFont();
        endList(ui);
        ImGui::PopStyleVar(2);
        ui.tagItem("create-design:warnings");

        label(ui, d, {309, 369}, "Component Details");
        ImGui::SetCursorScreenPos(d.at(cd::kDetails));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.size({3, 2}));
        ImGui::PushStyleColor(ImGuiCol_Border, imColorV(palette::kFrameLight));
        ImGui::BeginChild("##details", ui.size(cd::kDetailsSize + grow), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleColor();
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        ImGui::PushTextWrapPos(0.0f);
        if (hover_ && hover_->kind == Kind::Component) itemDetail(ui, *hover_, DetailStyle::Compact);
        else if (hull_) itemDetail(ui, ItemRef{Kind::Hull, *hull_}, DetailStyle::Compact);
        else ImGui::TextColored(kDimText, "Point at a component to see it here.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ui.tagItem("create-design:details");
    }

    void buttons(UiContext& ui, Dialog& d, const std::optional<game::DesignStats>& st) {
        const game::Rules& r = ui.rules();
        const uint32_t hull = probeHull(ui);
        // Comp Type: "Select Component Group", the General Groups of the
        // components available and "All", the letter case ignored; Cancel
        // picks All (spec 06 §7 Q94, confirmed: binary).
        static constexpr const char* kGroupPicker = "Select Component Group###componentgroup";
        if (d.button("Comp Type")) {
            groupRows_ = componentGroupRows(r, ui.me(), hull);
            ImGui::OpenPopup(kGroupPicker);
        }
        if (const ListPick pick = nameListWindow(ui, kGroupPicker, groupRows_); pick.done) {
            group_.clear();
            if (pick.row && !datafile::keysEqual(groupRows_[*pick.row], "All")) group_ = groupRows_[*pick.row];
        }
        // Weap Mount: "Select Weapon Mount", Normal Mount first, then the
        // mounts the hull can use; Cancel picks Normal Mount (inferred, as the
        // other list windows' Cancel picks their empty choice).
        static constexpr const char* kMountPicker = "Select Weapon Mount###weaponmount";
        if (d.button("Weap Mount", !mounts_.empty())) ImGui::OpenPopup(kMountPicker);
        if (const ListPick pick = weaponMountWindow(ui, kMountPicker, mounts_, mount_); pick.done)
            mount_ = pick.row && *pick.row > 0 && *pick.row <= mounts_.size() ? static_cast<int32_t>(mounts_[*pick.row - 1]) : -1;
        // Slots 3 to 7 empty; the check boxes in 8 to 10, Weapons Report in 11,
        // Create Design in 13 (spec 06 §7 Q94). To Hit Modifiers and Condensed
        // View are the empire's options.
        for (int gap = 0; gap < 5; ++gap) d.spacer();
        if (d.check("To Hit Modifiers", toHit_)) setToHit(ui, !toHit_);
        if (d.check("Condensed View", condensed_)) {
            // Ticking it moves identical components together on the design
            // itself (spec 06 §7 Q94, confirmed: binary).
            const bool on = !condensed_;
            setCondensed(ui, on);
            condensed_ = on;
            if (on) condenseEntries(entries_);
        }
        if (d.check("Only Latest", onlyLatest_)) {
            // The box is the Empire Options row "only latest components for
            // designs": a click changes the option (spec 02 §6.4).
            onlyLatest_ = !onlyLatest_;
            game::InterfaceOptions o = ui.options();
            o.latestComponentsOnly = onlyLatest_;
            ui.setOptions(o);
        }
        // The report offers the mounts of this design's hull and starts on
        // Normal Mount (spec 06 §1.2, confirmed: binary).
        if (d.button("Weapons Report")) {
            ScreenArgs a;
            a.text = "weapons";
            a.index = hull_ ? static_cast<int>(*hull_) : -1;
            ui.open(ScreenId::Help, a);
        }
        d.spacer();
        // "Create Design" for an edit too (confirmed: binary).
        if (d.button("Create Design")) create(ui, st);
        ui.tagItem("create-design:save");
        notice_.draw(ui);
    }

    static void setToHit(UiContext& ui, bool on) {
        game::InterfaceOptions o = ui.options();
        o.designToHit = on;
        ui.setOptions(o);
    }

    static void setCondensed(UiContext& ui, bool on) {
        game::InterfaceOptions o = ui.options();
        o.designCondensed = on;
        ui.setOptions(o);
    }

    // Create Design (spec 06 §7 Q94, spec 03 §4.1, confirmed: binary): with
    // any warning the "Design Warnings" box says the design cannot be made
    // while it has warnings; then the name is checked, exactly, against every
    // design in the game, and a clash gets the same box. Our client differs:
    // an empty name is refused too (the original accepts it).
    void create(UiContext& ui, const std::optional<game::DesignStats>& st) {
        if (!hull_ || !problems(st).empty()) {
            notice_.open("The design cannot be created while it has warnings.", "Design Warnings");
            return;
        }
        const std::string n = name();
        if (n.empty()) {
            notice_.open("The design needs a name.", "Design Warnings");
            return;
        }
        if (designNameClashes(ui.state(), ui.me(), n, editing_)) {
            notice_.open("That design name is already in use.", "Design Warnings");
            return;
        }
        game::Design d;
        d.name = n;
        d.designType = designType_;
        d.hull = *hull_;
        d.entries = entries_;
        d.strategy = strategy_;
        d.picture = picture_;
        const game::CommandResult res =
            editing_.valid() ? ui.session.issue(game::cmd::EditDesign{editing_, std::move(d)}) : ui.session.issue(game::cmd::CreateDesign{std::move(d)});
        if (res.ok) done_ = true;
        else notice_.open(res.error, "Design Warnings");   // a refusal of the engine (ours)
    }

    ScreenArgs args_;
    bool initialized_ = false;
    bool done_ = false;
    game::DesignId editing_;  // Edit: the prototype changed in place
    ruleset::VehicleType type_ = ruleset::VehicleType::Ship;
    std::string title_ = "Ship Design";
    std::optional<uint32_t> hull_;  // none until a size is chosen
    bool hullChosen_ = false;   // the player picked a hull in the Size list
    std::string designType_;
    TypeListPicker typePicker_;   // Select Design Type
    std::array<char, 64> name_{};
    std::vector<game::DesignEntry> entries_;
    uint32_t strategy_ = 0;
    std::string picture_;   // the design's own picture; empty: the hull's
    std::vector<std::string> pictures_;   // the choices for the hull below
    std::optional<std::optional<uint32_t>> choicesFor_;
    NoticePopup notice_;

    std::string group_;  // Comp Type filter; empty = all
    std::vector<std::string> groupRows_;   // the rows of "Select Component Group" while it is open
    int32_t mount_ = -1;
    std::vector<uint32_t> mounts_;
    bool condensed_ = false;   // the empire's options, read each frame (drawDialog)
    bool onlyLatest_ = true;
    bool toHit_ = false;
    int stripFirst_ = 0;
    float lowerExtra_ = 0;   // the window's height beyond 475 px, given to the lower boxes
    std::optional<ItemRef> hover_, hovered_;

    std::vector<std::string> names_;
    ItemReportPopup popup_;
};

} // namespace

std::unique_ptr<Screen> makeDesigns(const ScreenArgs& args) { return std::make_unique<DesignsScreen>(args); }
std::unique_ptr<Screen> makeCreateDesign(const ScreenArgs& args) { return std::make_unique<CreateDesignScreen>(args); }

} // namespace opense4::client::classic
