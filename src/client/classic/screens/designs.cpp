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
// built or retrofitted to it) and in no construction queue, and changes that
// design in place (cmd::EditDesign); Copy and Upgrade save a new design
// (spec 03 §4.1).

#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/design_tools.hpp"
#include "client/classic/screens/item_reports.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/script/items.hpp"
#include "client/classic/widgets.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/economy.hpp"

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

// A 128 px hull portrait on a solid, framed backdrop (the item is the picture, for click tests).
void portrait(UiContext& ui, const Sprite& s) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 q(p.x + ui.px(128), p.y + ui.px(128));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, q, IM_COL32_BLACK);
    dl->AddRect(p, q, imColor(palette::kFrame));
    image(ui, s, {128, 128});
}

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
std::vector<std::string> loadNameList(UiContext& ui) {
    const std::string& file = ui.me().race.designNameFile;
    if (file.empty()) return {};
    const auto path = ui.art.files().find("Dsgnname/" + file);
    if (!path) return {};
    std::ifstream in(*path, std::ios::binary);
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
        return keep;
    }

private:
    void init(UiContext& ui) {
        initialized_ = true;
        if (!ui.me().designs.empty()) newest_ = ui.me().designs.back();
        if (selected_.valid() && selected_.index() < ui.state().designs.size()) tab_ = tabOf(ui, ui.state().design(selected_));
    }

    DesignTab tabOf(const UiContext& ui, const game::Design& d) const {
        const bool unit = isUnitHull(ui.rules().hull(d.hull).type);
        if (d.owner == ui.session.player()) return unit ? DesignTab::Units : DesignTab::Ships;
        return unit ? DesignTab::EnemyUnits : DesignTab::EnemyShips;
    }

    std::vector<game::DesignId> listFor(const UiContext& ui, DesignTab tab) const {
        const game::GameState& s = ui.state();
        std::vector<game::DesignId> out;
        const std::vector<game::DesignId> source = enemyTab(tab) ? game::seenDesignIds(ui.me().knowledge) : ui.me().designs;
        for (game::DesignId id : source) {
            if (id.index() >= s.designs.size()) continue;
            const game::Design& d = s.design(id);
            if (enemyTab(tab) && d.owner == ui.session.player()) continue;
            if (isUnitHull(ui.rules().hull(d.hull).type) != unitTab(tab)) continue;
            if (hideObsolete_ && d.obsolete) continue;
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
        textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {269, 39}, blue, "Design Detail");
        ImGui::SetCursorScreenPos(d.at({16, 58}));
        designList(ui, list);
        textAt(ui, d, ui.fonts.small, kSmallSize, kSmallLead, {16, 450}, blue, "(obsolete designs are deleted automatically)");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(d.at({269, 58}), d.at({574, 462}), imColor(palette::kButton));
        if (selected_.valid()) detail(ui, d, ui.state().design(selected_));
        else
            textAt(ui, d, ui.fonts.regular, kTextSize, kTextLead, {276, 66}, imColor(palette::kSecondary),
                   enemyTab(tab_) ? "No enemy designs of this kind seen yet." : "No designs of this kind yet.");
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
                if (const Sprite& lampSprite = selected ? green : blue)
                    drawSprite(dl, lampSprite, {b.x + ui.px(4), b.y + ui.px(11)}, {b.x + ui.px(17), b.y + ui.px(24)});
                if (const Sprite pic = ui.art.shipMini(style, hull)) drawSprite(dl, pic, {b.x + ui.px(21), b.y}, {b.x + ui.px(57), b.y + ui.px(36)});
                dl->PushClipRect(b, {b.x + rowW, b.y + ui.px(kRow)}, true);
                dl->AddText({b.x + ui.px(61), b.y + ui.px(-2 + kTextLead)}, IM_COL32_WHITE, d.name.c_str());
                ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
                dl->AddText({b.x + ui.px(71), b.y + ui.px(12 + kSmallLead)}, IM_COL32_WHITE, hull.name.c_str());
                if (!enemyTab(tab_) && game::designIsPrototype(d)) dl->AddText({b.x + ui.px(71), b.y + ui.px(23 + kSmallLead)}, IM_COL32_WHITE, "Prototype");
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
        image(ui, ui.art.shipPortrait(style, hull), {128, 128});
        hullReportOnClick(ui, d.hull);
        textAt(ui, dlg, ui.fonts.bold, kTitleSize, kTitleLead, top + Vec2{140, 4}, white, d.name);
        text(top + Vec2{140, 40}, blue, "Size");
        text(top + Vec2{152, 55}, white, std::format("{} ({}kT)", hull.name, hull.tonnage));
        text(top + Vec2{140, 70}, blue, own ? "Design Type" : "Owner");
        text(top + Vec2{152, 85}, white, own ? (d.designType.empty() ? std::string("-") : d.designType) : (d.owner.valid() ? s.empire(d.owner).name : std::string("-")));
        text(top + Vec2{140, 100}, blue, "Date Created");
        text(top + Vec2{152, 115}, white, formatDate(d.createdTurn));
        if (d.obsolete) text(top + Vec2{202, 115}, imColor(0xffff00), "(Obsolete)");
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
        if (!own) return;
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
                if (enemyTab(tab_)) statsView_ = false;
            }
            ui.tagTab(kTabIds[i], tab_ == static_cast<DesignTab>(i));
        }
        d.spacer();
        const game::Design* own = selectedOwn(ui);
        auto openDesigner = [&](const char* mode) {
            ScreenArgs a;
            a.text = mode;
            if (own) a.design = own->id;
            ui.open(ScreenId::CreateDesign, a);
            note_.clear();
        };
        // Create asks for the vehicle type first (observed, spec 07 session 3).
        const bool create = d.button("Create");
        ui.tagItem("designs:create");
        if (create) {
            ImGui::OpenPopup(kTypePicker);
            note_.clear();
        }
        typePicker(ui);
        if (d.button("Copy", own != nullptr)) openDesigner("copy");
        const bool editable = own && game::designIsPrototype(*own) && !game::designInQueue(ui.state(), ui.me().id, own->id);
        if (d.button("Edit", editable)) openDesigner("edit");
        if (d.button("Upgrade", own != nullptr)) {
            std::vector<game::DesignEntry> entries = own->entries;
            if (upgradeEntries(ui.rules(), ui.me(), entries)) {
                openDesigner("upgrade");
            } else {
                note_ = std::format("{} already uses the newest components.", own->name);
                noteIsError_ = false;
            }
        }
        const bool obsolete = own && own->obsolete;
        if (d.button(obsolete ? "Make Current" : "Make Obsolete", own != nullptr)) {
            const game::CommandResult res = ui.session.issue(game::cmd::SetDesignObsolete{own->id, !obsolete});
            note_ = res.ok ? std::string{} : res.error;
            noteIsError_ = !res.ok;
        }
        // On/off settings: check boxes (observed, spec 07 session 3).
        if (d.check("Hide Obsolete", hideObsolete_)) hideObsolete_ = !hideObsolete_;
        if (d.check("Stats\\Strategy", statsView_)) statsView_ = !statsView_;
        if (d.button("Simulator")) ui.open(ScreenId::CombatSimulator);
        ui.tagItem("designs:simulator");
    }

    // "Select Vehicle Type" (our title): the vehicle types the empire has a hull
    // of; picking one opens the designer for it (spec 06 §7 Q94).
    static constexpr const char* kTypePicker = "Select Vehicle Type###vehicletype";
    void typePicker(UiContext& ui) {
        if (!beginModal(ui, kTypePicker, {340, 370})) return;
        const std::vector<ruleset::VehicleType> types = designableTypes(ui.rules(), ui.me());
        const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
        beginList(ui, "##types", ImVec2(0, -footer), kListLineStep);
        std::optional<ruleset::VehicleType> picked;
        for (ruleset::VehicleType t : types)
            if (ImGui::Selectable(std::string(ruleset::displayName(t)).c_str(), false)) picked = t;
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
    bool statsView_ = false;
    int gridTop_ = 0;   // the component grid's first row shown
    std::string note_;
    bool noteIsError_ = false;
    ItemReportPopup popup_;
};

// ---- Create Design -------------------------------------------------------------------------------
//
// The original's designer as observed (spec 07 session 3): titled after the
// vehicle type chosen first ("Ship Design"), it starts with no size. Top row:
// the picture; Size, Design Type and Design Name, each with a down-arrow button
// that opens its list; the figures box (Space Used, Total Cost on three lines,
// Movement, Shields, Cargo Space, Supply Capacity). Then "Components on Design"
// as a strip of icons with arrows, and "Components Available" as a 3-column grid
// of tiles (icon, name, kT) with the Warnings and Component Details boxes beside
// it. Buttons: Comp Type, Weap Mount, the To Hit Modifiers, Condensed View and
// Only Latest check boxes, Weapons Report, and Create Design and Cancel in the
// 13th and 14th slots. Every place and size not given there is ours (inferred,
// spec 06 §7 Q94).

namespace cd {
// Sums of ImGui positions (ImGui's own operators are not enabled here).
inline ImVec2 operator+(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
// Places in the content area (frame pixels from its top left).
constexpr Vec2 kPicture{3, 3};
constexpr float kPickerX = 140, kPickerBoxW = 190, kPickerArrow = 20;
constexpr float kFiguresX = 358, kFiguresR = 553, kFiguresB = 131;
constexpr float kStripY = 137;         // its label; the strip 16 px below, 38 tall
constexpr float kCell = 36;            // a component cell in the strip
constexpr float kGridY = 197;          // the lower labels; boxes 16 px below
constexpr float kGridW = 366, kBottom = 427;
constexpr float kSideX = 375, kWarningsB = 303;
constexpr float kTileH = 36;
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

    // The names the Design Name list offers: the race's list without the names
    // in use, or with no list in the install "<Hull> 1", "<Hull> 2"... (ours).
    std::vector<std::string> nameChoices(const UiContext& ui) const {
        std::vector<std::string> out;
        for (const std::string& n : names_)
            if (!designNameTaken(ui.state(), ui.me(), n)) out.push_back(n);
        if (!out.empty() || !names_.empty()) return out;
        const std::string base = std::string(ruleset::displayName(type_));
        for (int i = 1; out.size() < 10; ++i) {
            const std::string n = std::format("{} {}", hull_ ? ui.rules().hull(*hull_).name : base, i);
            if (!designNameTaken(ui.state(), ui.me(), n)) out.push_back(n);
        }
        return out;
    }

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
        names_ = loadNameList(ui);

        if (args_.design.valid() && args_.design.index() < s.designs.size()) {
            const game::Design& t = s.design(args_.design);
            hull_ = t.hull;
            type_ = r.hull(t.hull).type;
            entries_ = t.entries;
            designType_ = t.designType;
            strategy_ = t.strategy;
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
            // The vehicle type picked in Designs; it starts with no size and no
            // name (observed). A hull given by automation is taken as the size.
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
            pickDesignType(ui);
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

    void setHull(const UiContext& ui, uint32_t hull) {
        hullChosen_ = true;
        if (hull_ == hull) return;
        hull_ = hull;
        mounts_ = hullMounts(ui.rules(), ui.me(), hull);
        if (mount_ >= 0 && std::find(mounts_.begin(), mounts_.end(), static_cast<uint32_t>(mount_)) == mounts_.end()) mount_ = -1;
        error_.clear();
    }

    std::vector<std::string> problems(const UiContext& ui, const std::optional<game::DesignStats>& st) const {
        std::vector<std::string> out;
        if (!hull_) out.emplace_back("Choose a size for the design");
        const std::string n = name();
        if (n.empty()) out.emplace_back("The design needs a name");
        else if (designNameTaken(ui.state(), ui.me(), n)) out.emplace_back("Another design already has this name");
        if (st) out.insert(out.end(), st->problems.begin(), st->problems.end());
        return out;
    }

    bool drawDialog(UiContext& ui) {
        Dialog d(ui, title_.c_str(), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        // For lessons: the design being built.
        ui.facts.designComponents = static_cast<int64_t>(entries_.size());
        ui.facts.designHullChosen = hullChosen_;
        std::optional<game::DesignStats> st;
        if (hull_) st = game::computeDesignStats(ui.rules(), &ui.me(), *hull_, entries_);
        hovered_.reset();
        d.beginContent();
        topRow(ui, st);
        strip(ui);
        grid(ui, st);
        sideBoxes(ui, st);
        if (hovered_) hover_ = hovered_;
        d.beginButtons();
        buttons(ui, d, st);
        if (d.close(true, "Cancel")) return false;
        return d.keepOpen();
    }

    // A label in label blue at a place of the content area.
    static void label(UiContext& ui, Vec2 at, const char* text) {
        ImGui::GetWindowDrawList()->AddText(ImGui::GetWindowPos() + ui.size(at + Vec2{0, kTextLead - 3}), imColor(palette::kLabel), text);
    }

    // A value box with its down-arrow button; returns true when the button is pressed.
    bool pickerBox(UiContext& ui, const char* id, float y, const std::string& value, bool enabled = true) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        const ImVec2 a = o + ui.size({cd::kPickerX, y}), b = o + ui.size({cd::kPickerX + cd::kPickerBoxW, y + 20});
        dl->AddRect(a, b, imColor(palette::kFrame));
        dl->PushClipRect(a, b, true);
        dl->AddText({a.x + ui.px(4), a.y + ui.px(2)}, value.empty() ? imColor(palette::kSecondary) : IM_COL32_WHITE, value.empty() ? "None" : value.c_str());
        dl->PopClipRect();
        ImGui::SetCursorPos(ui.size({cd::kPickerX + cd::kPickerBoxW + 2, y}));
        return arrowButton(ui, id, ArrowGlyph::Down, {cd::kPickerArrow, 20}, enabled);
    }

    void topRow(UiContext& ui, const std::optional<game::DesignStats>& st) {
        const game::Rules& r = ui.rules();
        // The picture: the hull's portrait once a size is chosen.
        ImGui::SetCursorPos(ui.size(cd::kPicture));
        if (hull_) {
            portrait(ui, ui.art.shipPortrait(ui.me().race.style, r.hull(*hull_)));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) popup_.open({Kind::Hull, *hull_});
        } else {
            portrait(ui, Sprite{});
        }

        // Size.
        label(ui, {cd::kPickerX, 3}, "Size");
        const std::string size = hull_ ? std::format("{} ({}kT)", r.hull(*hull_).name, r.hull(*hull_).tonnage) : std::string{};
        if (pickerBox(ui, "##size", 19, size)) ImGui::OpenPopup("##sizes");
        ui.tag("create-design:hull", ImGui::GetWindowPos() + ui.size({cd::kPickerX, 19}), ImGui::GetItemRectMax());
        if (ImGui::BeginPopup("##sizes")) {
            for (uint32_t h : hullsOfType(r, ui.me(), type_, hull_))
                if (ImGui::Selectable(std::format("{} ({} kT)##{}", r.hull(h).name, r.hull(h).tonnage, h).c_str(), hull_ == h)) setHull(ui, h);
            ImGui::EndPopup();
        }

        // Design Type.
        label(ui, {cd::kPickerX, 45}, "Design Type");
        if (pickerBox(ui, "##type", 61, designType_)) ImGui::OpenPopup("##types");
        if (ImGui::BeginPopup("##types")) {
            const auto& types = ui.me().designTypes;
            for (size_t i = 0; i < types.size(); ++i)
                if (ImGui::Selectable(std::format("{}##{}", types[i], i).c_str(), types[i] == designType_)) designType_ = types[i];
            ImGui::EndPopup();
        }

        // Design Name: typed, or picked from the race's list with its button.
        label(ui, {cd::kPickerX, 87}, "Design Name");
        ImGui::SetCursorPos(ui.size({cd::kPickerX, 103}));
        ImGui::SetNextItemWidth(ui.px(cd::kPickerBoxW));
        if (ImGui::InputText("##name", name_.data(), name_.size())) error_.clear();
        ui.tagItem("create-design:name");
        ImGui::SetCursorPos(ui.size({cd::kPickerX + cd::kPickerBoxW + 2, 103}));
        if (arrowButton(ui, "##names", ArrowGlyph::Down, {cd::kPickerArrow, 20}, true)) ImGui::OpenPopup("##namelist");
        ui.tagItem("create-design:suggest");
        if (ImGui::BeginPopup("##namelist")) {
            const std::vector<std::string> choices = nameChoices(ui);
            for (size_t i = 0; i < choices.size(); ++i)
                if (ImGui::Selectable(std::format("{}##{}", choices[i], i).c_str())) {
                    setName(choices[i]);
                    error_.clear();
                }
            if (choices.empty()) ImGui::TextColored(kDimText, "Every name of the list is in use.");
            ImGui::EndPopup();
        }

        // The figures box.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        dl->AddRect(o + ui.size({cd::kFiguresX, 3}), o + ui.size({cd::kFiguresR, cd::kFiguresB}), imColor(palette::kFrame));
        int line = 0;
        auto row = [&](const char* name, const std::string& value, ImU32 color = IM_COL32_WHITE) {
            const float y = 5.0f + 14.0f * float(line++);
            if (name) label(ui, {cd::kFiguresX + 4, y}, name);
            if (value.empty()) return;
            const float w = ImGui::CalcTextSize(value.c_str()).x;
            dl->AddText(o + ImVec2(ui.px(cd::kFiguresR - 4) - w, ui.px(y + kTextLead - 3)), color, value.c_str());
        };
        const bool over = st && st->tonnageUsed > st->tonnageMax;
        row("Space Used", st ? std::format("{}/{}kT", st->tonnageUsed, st->tonnageMax) : std::string("-"),
            over ? ImGui::GetColorU32(kWarnText) : IM_COL32_WHITE);
        row("Total Cost", {});
        static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
        static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
        for (size_t k = 0; k < 3; ++k) {
            const float y = 5.0f + 14.0f * float(line++);
            const std::string v = st ? formatNumber(st->cost.v[k]) : std::string("-");
            if (const Sprite icon = ui.art.icon16(kIcons[k]))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(icon.tex.value)), o + ui.size({cd::kFiguresR - 18, y}),
                             o + ui.size({cd::kFiguresR - 4, y + 14}), {icon.uv.min.x, icon.uv.min.y}, {icon.uv.max.x, icon.uv.max.y});
            const float w = ImGui::CalcTextSize(v.c_str()).x;
            dl->AddText(o + ImVec2(ui.px(cd::kFiguresR - 20) - w, ui.px(y + kTextLead - 3)), imColor(kColors[k]), v.c_str());
        }
        row("Movement", st ? std::to_string(st->movement) : std::string("-"));
        row("Shields", st ? shieldsText(*st) : std::string("-"));
        row("Cargo Space", st ? std::format("{}kT", st->cargoCapacity) : std::string("-"));
        row("Supply Capacity", st ? formatNumber(st->supplyCapacity) : std::string("-"));
    }

    // "Components on Design": a strip of 36 px cells between two arrows; a click
    // removes the component, a right-click opens its report. Condensed View
    // shows each component once with its count (inferred).
    void strip(UiContext& ui) {
        const game::Rules& r = ui.rules();
        label(ui, {3, cd::kStripY}, std::format("Components on Design ({})", entries_.size()).c_str());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        const float top = cd::kStripY + 16;
        const ImVec2 boxA = o + ui.size({3, top}), boxB = o + ui.size({cd::kFiguresR, top + 38});
        dl->AddRect(boxA, boxB, imColor(palette::kFrame));
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
        const int visible = static_cast<int>((cd::kFiguresR - 2 - 20 - 22) / cd::kCell);
        stripFirst_ = std::clamp(stripFirst_, 0, std::max(0, static_cast<int>(cells.size()) - visible));
        ImGui::SetCursorPos(ui.size({4, top + 1}));
        if (arrowButton(ui, "##stripLeft", ArrowGlyph::Left, {16, 36}, stripFirst_ > 0)) --stripFirst_;
        ImGui::SetCursorPos(ui.size({cd::kFiguresR - 17, top + 1}));
        if (arrowButton(ui, "##stripRight", ArrowGlyph::Right, {16, 36}, stripFirst_ + visible < static_cast<int>(cells.size()))) ++stripFirst_;
        std::optional<size_t> remove;
        for (int i = 0; i < visible && stripFirst_ + i < static_cast<int>(cells.size()); ++i) {
            const Cell& c = cells[static_cast<size_t>(stripFirst_ + i)];
            const ruleset::Component& comp = r.component(c.entry.component);
            const Vec2 at{22 + cd::kCell * float(i), top + 1};
            ImGui::SetCursorPos(ui.size(at));
            ImGui::PushID(stripFirst_ + i);
            const bool clicked = ImGui::InvisibleButton("##cell", ui.size({cd::kCell, cd::kCell}));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImVec2 a = o + ui.size(at);
            if (hovered) dl->AddRect(a, a + ui.size({cd::kCell, cd::kCell}), imColor(palette::kButtonHot));
            if (const Sprite pic = ui.art.component(comp.picture)) drawSprite(dl, pic, a + ui.size({2, 2}), a + ui.size({34, 34}));
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            if (const std::string_view code = mountCode(r, c.entry.mount); !code.empty())
                dl->AddText(a + ui.size({3, 1}), IM_COL32(255, 255, 0, 255), code.data(), code.data() + code.size());
            if (c.count > 1) {
                const std::string n = std::format("x{}", c.count);
                dl->AddText(a + ImVec2(ui.px(34) - ImGui::CalcTextSize(n.c_str()).x, ui.px(24)), IM_COL32_WHITE, n.c_str());
            }
            ImGui::PopFont();
            if (hovered) hovered_ = ItemRef{Kind::Component, c.entry.component, c.entry.mount};
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) popup_.open({Kind::Component, c.entry.component, c.entry.mount});
            if (clicked) remove = c.remove;
        }
        if (cells.empty()) {
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            dl->AddText(o + ui.size({24, top + 13}), imColor(palette::kSecondary), "(click a component below to add it to the design)");
            ImGui::PopFont();
        }
        ui.tag("create-design:on-design", boxA, boxB);
        if (remove && *remove < entries_.size()) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*remove));
            error_.clear();
        }
    }

    // "Components Available": tiles of icon, name and kT, three to a row, in a
    // list with the arrow column; a click adds the component (with the chosen
    // mount where it applies), a right-click opens its report.
    void grid(UiContext& ui, const std::optional<game::DesignStats>& st) {
        const game::Rules& r = ui.rules();
        const uint32_t hull = probeHull(ui);
        std::string filter = group_.empty() ? std::string("All types") : group_;
        if (mount_ >= 0) filter += " - " + r.data().weaponMounts[static_cast<size_t>(mount_)].longName;
        label(ui, {3, cd::kGridY}, "Components Available");
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        ImGui::GetWindowDrawList()->AddText(ImGui::GetWindowPos() + ui.size({150, cd::kGridY + 2}), imColor(palette::kSecondary), filter.c_str());
        ImGui::PopFont();
        ImGui::SetCursorPos(ui.size({3, cd::kGridY + 16}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        beginList(ui, "##available", ui.size({cd::kGridW, cd::kBottom - cd::kGridY - 16}), cd::kTileH);
        const std::vector<uint32_t> comps = designerComponents(r, ui.me(), hull, group_, onlyLatest_);
        const float tileW = std::floor(ImGui::GetContentRegionAvail().x / 3.0f);
        const float tileH = ui.px(cd::kTileH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        for (size_t i = 0; i < comps.size(); ++i) {
            const uint32_t ci = comps[i];
            const ruleset::Component& c = r.component(ci);
            const game::DesignEntry entry{ci, hull_ ? mountFor(r, *hull_, ci, mount_) : -1};
            const game::MountedComponent m = game::mounted(r, entry);
            const int onDesign = static_cast<int>(std::count_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e.component == ci; }));
            const bool fits = !st || (st->tonnageUsed + m.tonnage <= st->tonnageMax && (c.maxPerVehicle == 0 || onDesign < c.maxPerVehicle));
            const ImVec2 a(origin.x + tileW * float(i % 3), origin.y + tileH * float(i / 3));
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(static_cast<int>(i));
            const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImVec2 b(a.x + tileW, a.y + tileH);
            dl->AddRect(a, b, imColor(hovered ? palette::kButtonHot : palette::kFrame, hovered ? 1.0f : 0.6f));
            if (const Sprite pic = ui.art.component(c.picture)) drawSprite(dl, pic, a + ui.size({2, 2}), a + ui.size({34, 34}), fits ? IM_COL32_WHITE : IM_COL32(120, 120, 120, 255));
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            dl->PushClipRect(a + ui.size({37, 0}), b, true);
            dl->AddText(a + ui.size({37, 3}), fits ? IM_COL32_WHITE : imColor(palette::kDim), c.name.c_str());
            // With To Hit Modifiers on, a weapon shows its to-hit modifier with the
            // chosen mount in place of its size (inferred, Q94).
            std::string second = std::format("{}kT", m.tonnage);
            if (toHit_ && c.isWeapon()) {
                const int mod = c.weapon.modifier + (entry.mount >= 0 ? r.data().weaponMounts[static_cast<size_t>(entry.mount)].toHitModifier : 0);
                second = std::format("To Hit {:+}", mod);
            }
            dl->AddText(a + ui.size({37, 19}), imColor(palette::kSecondary), second.c_str());
            if (const std::string_view code = mountCode(r, entry.mount); !code.empty())
                dl->AddText(a + ui.size({3, 1}), IM_COL32(255, 255, 0, 255), code.data(), code.data() + code.size());
            dl->PopClipRect();
            ImGui::PopFont();
            if (hovered) hovered_ = ItemRef{Kind::Component, ci, entry.mount};
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) popup_.open({Kind::Component, ci, entry.mount});
            if (clicked) {
                entries_.push_back(entry);
                error_.clear();
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

    // The Warnings box (every rule the design breaks, in yellow, spec 06 §5.4)
    // and the Component Details box (the component under the pointer, else the hull).
    void sideBoxes(UiContext& ui, const std::optional<game::DesignStats>& st) {
        label(ui, {cd::kSideX, cd::kGridY}, "Warnings");
        ImGui::SetCursorPos(ui.size({cd::kSideX, cd::kGridY + 16}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.size({3, 2}));
        beginList(ui, "##warnings", ui.size({cd::kFiguresR - cd::kSideX, cd::kWarningsB - cd::kGridY - 16}), kListLineStep,
                  ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        ImGui::PushTextWrapPos(0.0f);
        if (!error_.empty()) ImGui::TextColored(ImVec4(1, 1, 0, 1), "Not created: %s", error_.c_str());
        const std::vector<std::string> list = problems(ui, st);
        for (const std::string& p : list) ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", p.c_str());
        if (list.empty() && error_.empty()) ImGui::TextColored(kGoodText, "None: the design can be created.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        endList(ui);
        ImGui::PopStyleVar();
        ui.tagItem("create-design:warnings");

        label(ui, {cd::kSideX, cd::kWarningsB + 6}, "Component Details");
        ImGui::SetCursorPos(ui.size({cd::kSideX, cd::kWarningsB + 22}));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.size({3, 2}));
        beginList(ui, "##details", ui.size({cd::kFiguresR - cd::kSideX, cd::kBottom - cd::kWarningsB - 22}), kListLineStep,
                  ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        ImGui::PushTextWrapPos(0.0f);
        if (hover_ && hover_->kind == Kind::Component) itemDetail(ui, *hover_, DetailStyle::Compact);
        else if (hull_) itemDetail(ui, ItemRef{Kind::Hull, *hull_}, DetailStyle::Compact);
        else ImGui::TextColored(kDimText, "Point at a component to see it here.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        endList(ui);
        ImGui::PopStyleVar();
    }

    void buttons(UiContext& ui, Dialog& d, const std::optional<game::DesignStats>& st) {
        (void)st;
        const game::Rules& r = ui.rules();
        const uint32_t hull = probeHull(ui);
        if (d.button("Comp Type")) ImGui::OpenPopup("##groups");
        if (ImGui::BeginPopup("##groups")) {
            if (ImGui::Selectable("All Types", group_.empty())) group_.clear();
            ImGui::Separator();
            for (const std::string& g : componentGroups(r, ui.me(), hull))
                if (ImGui::Selectable(g.c_str(), datafile::keysEqual(g, group_))) group_ = g;
            ImGui::EndPopup();
        }
        if (d.button("Weap Mount", !mounts_.empty())) ImGui::OpenPopup("##mounts");
        if (ImGui::BeginPopup("##mounts")) {
            if (ImGui::Selectable("No Mount", mount_ < 0)) mount_ = -1;
            for (uint32_t m : mounts_) {
                const auto& wm = r.data().weaponMounts[m];
                if (ImGui::Selectable(std::format("{}##{}", wm.longName, m).c_str(), mount_ == static_cast<int32_t>(m)))
                    mount_ = static_cast<int32_t>(m);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Damage %d%%, size %d%%, cost %d%%, range %+d, to hit %+d%%", wm.damagePercent, wm.tonnagePercent,
                                      wm.costPercent, wm.rangeModifier, wm.toHitModifier);
            }
            ImGui::EndPopup();
        }
        // On/off settings: check boxes (observed, spec 07 session 3).
        if (d.check("To Hit Modifiers", toHit_)) toHit_ = !toHit_;
        if (d.check("Condensed View", condensed_)) condensed_ = !condensed_;
        if (d.check("Only Latest", onlyLatest_)) onlyLatest_ = !onlyLatest_;
        d.spacer();
        if (d.button("Weapons Report")) {
            ScreenArgs a;
            a.text = "weapons";
            a.index = mount_;
            ui.open(ScreenId::Help, a);
        }
        for (int gap = 0; gap < 5; ++gap) d.spacer();
        if (d.button(editing_.valid() ? "Save Design" : "Create Design")) create(ui);
        ui.tagItem("create-design:save");
    }

    void create(UiContext& ui) {
        if (!hull_) {
            error_ = "the design has no size";
            return;
        }
        game::Design d;
        d.name = name();
        d.designType = designType_;
        d.hull = *hull_;
        d.entries = entries_;
        d.strategy = strategy_;
        const game::CommandResult res =
            editing_.valid() ? ui.session.issue(game::cmd::EditDesign{editing_, std::move(d)}) : ui.session.issue(game::cmd::CreateDesign{std::move(d)});
        if (res.ok) done_ = true;
        else error_ = res.error;
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
    std::array<char, 64> name_{};
    std::vector<game::DesignEntry> entries_;
    uint32_t strategy_ = 0;
    std::string error_;

    std::string group_;  // Comp Type filter; empty = all
    int32_t mount_ = -1;
    std::vector<uint32_t> mounts_;
    bool condensed_ = false;
    bool onlyLatest_ = true;
    bool toHit_ = false;
    int stripFirst_ = 0;
    std::optional<ItemRef> hover_, hovered_;

    std::vector<std::string> names_;
    ItemReportPopup popup_;
};

} // namespace

std::unique_ptr<Screen> makeDesigns(const ScreenArgs& args) { return std::make_unique<DesignsScreen>(args); }
std::unique_ptr<Screen> makeCreateDesign(const ScreenArgs& args) { return std::make_unique<CreateDesignScreen>(args); }

} // namespace opense4::client::classic
