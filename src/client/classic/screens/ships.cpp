// Ships\Units window (F6, docs/spec/06 §1.2, spec 03 §17): every own ship,
// unit group in space and fleet, with statistics, a mini galaxy map and
// column tabs. Left-click a row to select it in the main window, right-click
// for its report.

#include "client/classic/reports.hpp"
#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "game/combat.hpp"
#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

struct Cell {
    std::string text;
};
Cell text(std::string t) { return Cell{std::move(t)}; }
Cell number(int64_t v, std::string t = {}) { return Cell{t.empty() ? formatNumber(v) : std::move(t)}; }

struct ListRow {
    game::VehicleId vehicle;  // ships and unit groups
    game::FleetId fleet;      // fleets
    game::SystemId system;
    std::string name;
    Sprite picture;
    std::vector<Cell> cells;               // the shown tab's columns after Pic and Name
    std::vector<std::string> orderLines;   // the Orders column: one order a line
};

// A column after Pic and Name: what it stands for, its heading, its width
// (spec 06 §1.8.3; 0 takes the rest) and a fixed heading colour.
struct Column {
    ShipColumn id;
    const char* name;
    float width;
    uint32_t color = 0;
};

std::vector<Column> columnsOf(ShipsTab tab) {
    using C = ShipColumn;
    switch (tab) {
        case ShipsTab::General: return {{C::Size, "Size", 99}, {C::Type, "Type", 120}, {C::Movement, "Move", 40}, {C::Damage, "Dmg", 47}, {C::Supplies, "Supplies", 0}};
        case ShipsTab::Orders: return {{C::Class, "Class", 120}, {C::Orders, "Orders", 0}};
        case ShipsTab::Cargo: return {{C::CargoSpace, "Space", 65}, {C::CargoMax, "Max", 65}, {C::CargoList, "Cargo List", 0}};
        case ShipsTab::Fleet: return {{C::Experience, "Experience", 120}, {C::Fleet, "Fleet", 0}};
        case ShipsTab::Maintenance:
            return {{C::MineralsMaintenance, "Minerals", 70, palette::kMinerals},
                    {C::OrganicsMaintenance, "Organics", 70, palette::kOrganics},
                    {C::RadioactivesMaintenance, "Radioactives", 0, palette::kRadioactives}};
        case ShipsTab::Count: break;
    }
    return {};
}

// The same written as one line, the Orders column's sort key.
std::string oneLine(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) out += (out.empty() ? "" : ", ") + l;
    return out;
}

class ShipsScreen final : public Screen {
public:
    // Opened with kViewOnly (Fleet Transfer's Existing Fleets, spec 06 §7
    // Q79) the window is for viewing only: a left-click on a row does nothing.
    explicit ShipsScreen(const ScreenArgs& args) : viewOnly_(args.text == kViewOnly) {}

    bool draw(UiContext& ui) override {
        Dialog d(ui, screenTitle(ScreenId::Ships), DialogSize::Tall);
        if (!d.open()) return d.keepOpen();
        // The tab and the three switches are kept with the empire (spec 06 §7 Q79).
        const game::InterfaceOptions kept = ui.options();
        tab_ = static_cast<ShipsTab>(std::min<int>(kept.shipsTab, int(ShipsTab::Maintenance)));
        ships_ = (kept.shipsShown & 1) != 0;
        units_ = (kept.shipsShown & 2) != 0;
        fleets_ = (kept.shipsShown & 4) != 0;
        refresh(ui);

        d.beginContent();
        statistics(ui);
        table(ui);

        d.beginButtons();
        static constexpr std::array<std::pair<ShipsTab, const char*>, 5> kTabs{{{ShipsTab::General, "General"},
                                                                               {ShipsTab::Orders, "Orders"},
                                                                               {ShipsTab::Cargo, "Cargo"},
                                                                               {ShipsTab::Fleet, "Fleet"},
                                                                               {ShipsTab::Maintenance, "Maintenance"}}};
        static constexpr std::array<const char*, 5> kTabIds{{"general", "orders", "cargo", "fleet", "maintenance"}};
        for (size_t i = 0; i < kTabs.size(); ++i) {
            if (d.tab(kTabs[i].second, tab_ == kTabs[i].first)) tab_ = kTabs[i].first;
            ui.tagTab(kTabIds[i], tab_ == kTabs[i].first);
        }
        d.spacer();
        if (d.check("Show Ships", ships_)) ships_ = !ships_;
        if (d.check("Show Units", units_)) units_ = !units_;
        if (d.check("Show Fleets", fleets_)) fleets_ = !fleets_;
        game::InterfaceOptions now = kept;
        now.shipsTab = static_cast<uint8_t>(tab_);
        now.shipsShown = static_cast<uint8_t>((ships_ ? 1 : 0) | (units_ ? 2 : 0) | (fleets_ ? 4 : 0));
        if (!(now == kept)) ui.setOptions(now);
        if (d.close()) return false;
        report_.draw(ui);
        if (selected_) {
            ui.requests.selectVehicle = *selected_;
            return false;
        }
        return d.keepOpen();
    }

private:
    // Rows and totals are rebuilt only when the game, the tab, the filters or the sort change.
    void refresh(UiContext& ui) {
        const uint64_t key = ui.session.revision() * 64 + static_cast<uint64_t>(tab_) * 8 + (ships_ ? 4u : 0u) + (units_ ? 2u : 0u) + (fleets_ ? 1u : 0u);
        if (built_ && key == cacheKey_ && ui.options().shipsSort == sortedBy_) return;
        built_ = true;
        cacheKey_ = key;
        sortedBy_ = ui.options().shipsSort;
        rows_ = buildRows(ui);

        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        totals_ = {};
        for (const game::Vehicle& v : s.vehicles) {
            if (v.owner != ui.session.player()) continue;
            if (isUnitVehicle(r, s, v)) totals_.units += std::max(1, v.count);
            else ++totals_.ships;
            totals_.maintenance += vehicleMaintenance(r, s, v);
            if (std::find(totals_.present.begin(), totals_.present.end(), v.location.system) == totals_.present.end())
                totals_.present.push_back(v.location.system);
        }
        for (const game::Fleet& f : s.fleets)
            if (f.owner == ui.session.player()) ++totals_.fleets;
        // The engine's own figure once the economy reports it; our estimate until then.
        if (!ui.me().economy.maintenance.isZero()) totals_.maintenance = ui.me().economy.maintenance;
    }

    void statistics(UiContext& ui) {
        const int ships = totals_.ships, units = totals_.units, fleets = totals_.fleets;
        const game::Resources& maintenance = totals_.maintenance;
        const std::vector<game::SystemId>& present = totals_.present;

        const float mapW = ui.px(300), mapH = ui.px(176);
        const float leftW = ImGui::GetContentRegionAvail().x - mapW - ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginChild("##stats", ImVec2(leftW, mapH));
        labelValue(ui, "Total Ships", formatNumber(ships), 190);
        labelValue(ui, "Total Units in Space", formatNumber(units), 190);
        labelValue(ui, "Total Fleets", formatNumber(fleets), 190);
        ImGui::Dummy(ImVec2(0, ui.px(8)));
        ImGui::TextColored(kLabelBlue, "Total Maintenance Cost Per Turn");
        ImGui::Indent(ui.px(12));
        resources(ui, maintenance);
        ImGui::Unindent(ui.px(12));
        ImGui::Dummy(ImVec2(0, ui.px(8)));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kDim, "Left-click a row to select it in the main window; right-click for its report. "
                                 "Click a column heading to sort by it.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::SameLine();
        miniMap(ui, ImVec2(mapW, mapH), hovered_, present);
        hovered_.reset();
    }

    // The vehicle rows in the order of the stored keys, then the fleet rows in
    // the empire's fleet order, never sorted (spec 06 §1.8.3).
    std::vector<ListRow> buildRows(UiContext& ui) const {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        std::vector<ListRow> vehicles;
        std::vector<ShipSortValues> keys;
        for (const game::Vehicle& v : s.vehicles) {
            if (v.owner != ui.session.player()) continue;
            const bool unit = isUnitVehicle(r, s, v);
            if (unit ? !units_ : !ships_) continue;
            ListRow row;
            row.vehicle = v.id;
            row.system = v.location.system;
            row.name = v.count > 1 ? std::format("{} (x{})", v.name, v.count) : v.name;
            row.picture = unit ? unitMini(ui, v) : vehicleMini(ui, v);
            row.orderLines = orderListLines(ui, orderOwner(s, v.id), v.design);
            ShipSortValues k = shipSortValues(r, s, v);
            k.type = roleOf(ui, v);
            k.designName = classOf(ui, v);
            k.orders = oneLine(row.orderLines);
            k.cargo = cargoWords(ui, v);
            row.cells = vehicleCells(ui, v, k);
            vehicles.push_back(std::move(row));
            keys.push_back(std::move(k));
        }
        std::vector<ListRow> rows;
        for (size_t i : shipRowOrder(keys, sortedBy_)) rows.push_back(std::move(vehicles[i]));
        if (fleets_)
            for (const game::Fleet& f : s.fleets) {
                if (f.owner != ui.session.player()) continue;
                ListRow row;
                row.fleet = f.id;
                row.name = f.name;
                row.picture = fleetMini(ui);
                if (const game::Vehicle* lead = s.vehicle(f.leader.valid() ? f.leader : (f.members.empty() ? game::VehicleId{} : f.members.front())))
                    row.system = lead->location.system;
                OrderOwner owner;
                owner.fleet = f.id;
                row.orderLines = orderListLines(ui, owner, {});
                row.cells = fleetCells(ui, f);
                rows.push_back(std::move(row));
            }
        return rows;
    }

    // The design type (a unit group that mixes designs shows its kind, spec 03 §12).
    static std::string roleOf(const UiContext& ui, const game::Vehicle& v) {
        const game::Design& d = ui.state().design(v.design);
        if (!v.mixed.empty()) return std::format("{} designs", v.mixed.size());
        return d.designType.empty() ? std::string(ruleset::displayName(ui.rules().hull(d.hull).type)) : d.designType;
    }
    static std::string classOf(const UiContext& ui, const game::Vehicle& v) {
        return v.mixed.empty() ? ui.state().design(v.design).name : groupDesigns(ui.state(), v, 2);
    }
    static std::string cargoWords(const UiContext& ui, const game::Vehicle& v) {
        return v.cargo.empty() ? std::string("None") : cargoSummary(ui, v.cargo);
    }

    std::vector<Cell> vehicleCells(UiContext& ui, const game::Vehicle& v, const ShipSortValues& k) const {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const game::Design& d = s.design(v.design);
        switch (tab_) {
            case ShipsTab::General: {
                const auto [destroyed, total] = destroyedComponents(r, s, v);
                const int64_t cap = game::vehicleSupplyCapacity(r, s, v);
                // Unlimited supply shows as "Endless" (spec 03 §7).
                const std::string supplyText =
                    game::vehicleHasUnlimitedSupply(r, s, v) ? std::string("Endless") : std::format("{}/{}", formatNumber(v.supply), formatNumber(cap));
                return {text(r.hull(d.hull).name), text(k.type), text(std::format("{}/{}", v.movement, game::vehicleMaxMovement(r, s, v))),
                        text(std::format("{}/{}", destroyed, total)), text(supplyText)};
            }
            case ShipsTab::Orders: return {text(k.designName), text(k.orders)};
            case ShipsTab::Cargo:
                return {text(std::format("{}kT", formatNumber(k.cargoUsed))), text(std::format("{}kT", formatNumber(k.cargoCapacity))), text(k.cargo)};
            case ShipsTab::Fleet: {
                const game::Fleet* f = s.fleet(v.fleet);
                return {text(game::combat::experienceLabel(v.experience, v.experienceTenths)), text(f ? f->name : std::string("None"))};
            }
            case ShipsTab::Maintenance: return {number(k.maintenance.v[0]), number(k.maintenance.v[1]), number(k.maintenance.v[2])};
            case ShipsTab::Count: break;
        }
        return {};
    }

    std::vector<Cell> fleetCells(UiContext& ui, const game::Fleet& f) const {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        int move = 1 << 30, maxMove = 1 << 30, destroyed = 0, total = 0;
        int64_t supply = 0, supplyCap = 0, used = 0, cap = 0;
        game::Resources maintenance;
        int members = 0;
        bool endless = true;
        for (game::VehicleId id : f.members) {
            const game::Vehicle* v = s.vehicle(id);
            if (!v) continue;
            ++members;
            move = std::min(move, v->movement);
            maxMove = std::min(maxMove, game::vehicleMaxMovement(r, s, *v));
            const auto [lost, all] = destroyedComponents(r, s, *v);
            destroyed += lost;
            total += all;
            // Fighter groups and members with unlimited supply are left out (spec 03 §9).
            if (!game::vehicleHasUnlimitedSupply(r, s, *v)) {
                endless = false;
                if (game::vehicleType(r, s, *v) != ruleset::VehicleType::Fighter) {
                    supply += v->supply;
                    supplyCap += game::vehicleSupplyCapacity(r, s, *v);
                }
            }
            used += game::cargoSpaceUsed(r, s, v->cargo);
            cap += game::vehicleCargoCapacity(r, s, *v);
            maintenance += vehicleMaintenance(r, s, *v);
        }
        if (members == 0) move = maxMove = 0;
        switch (tab_) {
            case ShipsTab::General:
                return {text("Fleet"), text(std::format("{} vessel{}", members, members == 1 ? "" : "s")), text(std::format("{}/{}", move, maxMove)),
                        text(std::format("{}/{}", destroyed, total)),
                        text(endless && members > 0 ? std::string("Endless") : std::format("{}/{}", formatNumber(supply), formatNumber(supplyCap)))};
            case ShipsTab::Orders: return {text("Fleet"), text({})};
            case ShipsTab::Cargo:
                return {text(std::format("{}kT", formatNumber(used))), text(std::format("{}kT", formatNumber(cap))),
                        text(std::format("{} vessel{}", members, members == 1 ? "" : "s"))};
            case ShipsTab::Fleet: return {text(game::combat::experienceLabel(f.experience, f.experienceTenths)), text(f.name)};
            case ShipsTab::Maintenance: return {number(maintenance.v[0]), number(maintenance.v[1]), number(maintenance.v[2])};
            case ShipsTab::Count: break;
        }
        return {};
    }

    void table(UiContext& ui) {
        const std::vector<ListRow>& rows = rows_;
        const std::vector<Column> cols = columnsOf(tab_);
        const int ncols = static_cast<int>(cols.size()) + 2;
        const ImGuiTableFlags flags =
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        // The list from y 232 (headings) and 252 (rows); the rows take the window's height less 265 (spec 06 §2.1.1).
        ImGui::SetCursorPos(ui.size({0, 197}));
        // A new ID per tab so each tab keeps its own column setup.
        ImGui::PushID(static_cast<int>(tab_));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(2), ui.px(2)));
        if (!ImGui::BeginTable("##ships", ncols, flags, ImVec2(0, listRowsHeight(ui) + ui.px(20)))) {
            ImGui::PopStyleVar();
            ImGui::PopID();
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        // The widths of spec 06 §1.8.3 less the cells' padding (2 px each side).
        ImGui::TableSetupColumn("Pic", ImGuiTableColumnFlags_WidthFixed, ui.px(36 - 4));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ui.px(134 - 4));
        for (const Column& c : cols) {
            if (c.width > 0) ImGui::TableSetupColumn(c.name, ImGuiTableColumnFlags_WidthFixed, ui.px(c.width - 4));
            else ImGui::TableSetupColumn(c.name, ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        // A heading click, the picture's too, adds a sort key: the column it
        // stands for, in that column's fixed direction (spec 06 §1.8.3, §7 Q24).
        std::vector<ListColumn> headings{{"", 0}, {"Name", 0}};
        std::vector<ShipColumn> ids{ShipColumn::Picture, ShipColumn::Name};
        for (const Column& c : cols) {
            headings.push_back({c.name, 0, c.color});
            ids.push_back(c.id);
        }
        if (const int clicked = tableHeadings(ui, headings); clicked >= 0 && static_cast<size_t>(clicked) < ids.size()) {
            game::InterfaceOptions o = ui.options();
            o.shipsSort = clickSort(o.shipsSort, static_cast<int>(ids[static_cast<size_t>(clicked)]), static_cast<int>(ShipColumn::Name));
            ui.setOptions(o);
        }

        const float rowH = ui.px(kListRowH);
        const float textH = ImGui::GetTextLineHeight();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()), rowH);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const ListRow& row = rows[static_cast<size_t>(i)];
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                // The row is 36 px with the cells' padding (spec 06 §1.8).
                const float inner = rowH - 2.0f * ImGui::GetStyle().CellPadding.y;
                const bool left = ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                                    ImVec2(0, inner));
                if (ImGui::IsItemHovered()) hovered_ = row.system;
                const bool right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                ImGui::PopID();
                if (row.picture) {
                    const float ps = ui.px(32);
                    const ImVec2 a{p0.x + ui.px(3), p0.y + (inner - ps) * 0.5f};
                    ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(row.picture.tex.value)), a, ImVec2(a.x + ps, a.y + ps),
                                                         ImVec2(row.picture.uv.min.x, row.picture.uv.min.y),
                                                         ImVec2(row.picture.uv.max.x, row.picture.uv.max.y));
                }
                auto cell = [&](int column, const std::string& str) {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (inner - textH) * 0.5f);
                    ImGui::TextUnformatted(str.c_str());
                };
                cell(1, row.name);
                for (size_t c = 0; c < row.cells.size() && c < cols.size(); ++c) {
                    if (cols[c].id != ShipColumn::Orders) {
                        cell(static_cast<int>(c) + 2, row.cells[c].text);
                        continue;
                    }
                    // One order per 12 px line.
                    ImGui::TableSetColumnIndex(static_cast<int>(c) + 2);
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
                    for (size_t l = 0; l < row.orderLines.size() && l < 3; ++l)
                        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x, at.y + ui.px(12.0f * float(l) - 1.0f)), IM_COL32_WHITE, row.orderLines[l].c_str());
                    ImGui::PopFont();
                }
                if (left && !viewOnly_) {
                    if (row.vehicle.valid()) selected_ = row.vehicle;
                    else if (const game::Fleet* f = ui.state().fleet(row.fleet); f && !f->members.empty())
                        selected_ = f->leader.valid() ? f->leader : f->members.front();
                }
                if (right) {
                    if (row.vehicle.valid()) report_.vehicle(row.vehicle);
                    else report_.fleet(row.fleet);
                }
            }
        }
        if (rows.empty()) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(kDim, "Nothing to show.");
        }
        ImGui::EndTable();
        ImGui::PopStyleVar();
        ImGui::PopID();
    }

    struct Totals {
        int ships = 0, units = 0, fleets = 0;
        game::Resources maintenance;
        std::vector<game::SystemId> present;
    };

    bool viewOnly_ = false;
    ShipsTab tab_ = ShipsTab::General;
    bool ships_ = true, units_ = true, fleets_ = true;
    uint64_t cacheKey_ = 0;
    bool built_ = false;
    SortSlots sortedBy_{};
    std::vector<ListRow> rows_;
    Totals totals_;
    std::optional<game::SystemId> hovered_;
    std::optional<game::VehicleId> selected_;
    ReportPopup report_;
};

} // namespace

std::unique_ptr<Screen> makeShips(const ScreenArgs& args) { return std::make_unique<ShipsScreen>(args); }

} // namespace opense4::client::classic
