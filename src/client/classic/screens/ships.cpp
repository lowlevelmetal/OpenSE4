// Ships\Units window (F6, docs/spec/06 §1.2, spec 03 §17): every own ship,
// unit group in space and fleet, with statistics, a mini galaxy map and
// column tabs. Left-click a row to select it in the main window, right-click
// for its report.

#include "client/classic/reports.hpp"
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

enum class ShipsTab { General, Orders, Cargo, Fleet, Maintenance };

struct Cell {
    std::string text;
    int64_t key = 0;
    bool numeric = false;
};
Cell text(std::string t) { return Cell{std::move(t), 0, false}; }
Cell number(int64_t v, std::string t = {}) { return Cell{t.empty() ? formatNumber(v) : std::move(t), v, true}; }

struct ListRow {
    game::VehicleId vehicle;  // ships and unit groups
    game::FleetId fleet;      // fleets
    game::SystemId system;
    int group = 0;            // 0 ships, 1 units, 2 fleets: the unsorted order
    std::string name;
    Sprite picture;
    std::vector<Cell> cells;  // the tab's columns after Pic and Name
};

struct Column {
    const char* name;
    float width;  // frame pixels; 0 = stretch
};

std::vector<Column> columnsOf(ShipsTab tab) {
    switch (tab) {
        case ShipsTab::General: return {{"Size", 118}, {"Type", 118}, {"Move", 56}, {"Dmg", 70}, {"Supplies", 104}};
        case ShipsTab::Orders: return {{"Class", 150}, {"Orders", 0}};
        case ShipsTab::Cargo: return {{"Space", 64}, {"Max", 64}, {"Cargo", 0}};
        case ShipsTab::Fleet: return {{"Experience", 90}, {"Fleet", 0}};
        case ShipsTab::Maintenance: return {{"Minerals", 96}, {"Organics", 96}, {"Radioactives", 104}};
    }
    return {};
}

std::string ordersCell(const UiContext& ui, OrderOwner owner, game::DesignId design) {
    const auto* orders = ordersOf(ui.state(), owner);
    if (!orders || orders->empty()) return "None";
    std::string out = describeOrder(ui, orders->front(), design);
    if (orders->size() > 1) out += std::format(" (+{})", orders->size() - 1);
    if (repeatOf(ui.state(), owner)) out += " [repeat]";
    return out;
}

class ShipsScreen final : public Screen {
public:
    explicit ShipsScreen(const ScreenArgs& args) {
        if (args.text == "fleets") ships_ = units_ = false;
    }

    bool draw(UiContext& ui) override {
        Dialog d(ui, screenTitle(ScreenId::Ships), DialogSize::Tall);
        if (!d.open()) return d.keepOpen();
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
        for (const auto& [tab, label] : kTabs)
            if (d.tab(label, tab_ == tab)) tab_ = tab;
        d.spacer();
        if (d.check("Show Ships", ships_)) ships_ = !ships_;
        if (d.check("Show Units", units_)) units_ = !units_;
        if (d.check("Show Fleets", fleets_)) fleets_ = !fleets_;
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
        if (key == cacheKey_ && !sortDirty_) return;
        cacheKey_ = key;
        sortDirty_ = false;
        rows_ = buildRows(ui);
        sortRows(rows_);

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
                                 "Click a column header to sort.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::SameLine();
        miniMap(ui, ImVec2(mapW, mapH), hovered_, present);
        hovered_.reset();
    }

    std::vector<ListRow> buildRows(UiContext& ui) const {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        std::vector<ListRow> rows;
        for (const game::Vehicle& v : s.vehicles) {
            if (v.owner != ui.session.player()) continue;
            const bool unit = isUnitVehicle(r, s, v);
            if (unit ? !units_ : !ships_) continue;
            ListRow row;
            row.vehicle = v.id;
            row.system = v.location.system;
            row.group = unit ? 1 : 0;
            row.name = v.count > 1 ? std::format("{} (x{})", v.name, v.count) : v.name;
            row.picture = unit ? unitMini(ui, v) : vehicleMini(ui, v);
            row.cells = vehicleCells(ui, v);
            rows.push_back(std::move(row));
        }
        if (fleets_)
            for (const game::Fleet& f : s.fleets) {
                if (f.owner != ui.session.player()) continue;
                ListRow row;
                row.fleet = f.id;
                row.group = 2;
                row.name = f.name;
                row.picture = fleetMini(ui);
                if (const game::Vehicle* lead = s.vehicle(f.leader.valid() ? f.leader : (f.members.empty() ? game::VehicleId{} : f.members.front())))
                    row.system = lead->location.system;
                row.cells = fleetCells(ui, f);
                rows.push_back(std::move(row));
            }
        return rows;
    }

    std::vector<Cell> vehicleCells(UiContext& ui, const game::Vehicle& v) const {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const game::Design& d = s.design(v.design);
        const ruleset::VehicleSize& hull = r.hull(d.hull);
        switch (tab_) {
            case ShipsTab::General: {
                const int structure = game::vehicleStructure(r, s, v);
                const int damage = std::min(structure, game::vehicleDamageTaken(s, v));
                const int64_t cap = game::vehicleSupplyCapacity(r, s, v);
                // Unlimited supply shows as "Endless" (spec 03 §7).
                const std::string supplyText =
                    game::vehicleHasUnlimitedSupply(r, s, v) ? std::string("Endless") : std::format("{} / {}", formatNumber(v.supply), formatNumber(cap));
                // A unit group that mixes designs shows its kind (spec 03 §12).
                const std::string role = !v.mixed.empty() ? std::format("{} designs", v.mixed.size())
                                         : d.designType.empty() ? std::string(ruleset::displayName(hull.type)) : d.designType;
                return {text(hull.name), text(role),
                        number(v.movement, std::format("{}/{}", v.movement, game::vehicleMaxMovement(r, s, v))),
                        number(damage, std::format("{}/{}", damage, structure)), number(v.supply, supplyText)};
            }
            case ShipsTab::Orders: return {text(v.mixed.empty() ? d.name : groupDesigns(s, v, 2)), text(ordersCell(ui, orderOwner(s, v.id), v.design))};
            case ShipsTab::Cargo: {
                const int64_t used = game::cargoSpaceUsed(r, s, v.cargo);
                const int64_t cap = game::vehicleCargoCapacity(r, s, v);
                return {number(used, std::format("{}kT", formatNumber(used))), number(cap, std::format("{}kT", formatNumber(cap))),
                        text(cap > 0 || !v.cargo.empty() ? cargoSummary(ui, v.cargo) : std::string("-"))};
            }
            case ShipsTab::Fleet: {
                const game::Fleet* f = s.fleet(v.fleet);
                return {number(v.experience, game::combat::experienceLabel(v.experience, v.experienceTenths)), text(f ? f->name : std::string("-"))};
            }
            case ShipsTab::Maintenance: {
                const game::Resources m = vehicleMaintenance(r, s, v);
                return {number(m.v[0]), number(m.v[1]), number(m.v[2])};
            }
        }
        return {};
    }

    std::vector<Cell> fleetCells(UiContext& ui, const game::Fleet& f) const {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        int move = 1 << 30, maxMove = 1 << 30, damage = 0, structure = 0;
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
            const int st = game::vehicleStructure(r, s, *v);
            structure += st;
            damage += std::min(st, game::vehicleDamageTaken(s, *v));
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
                return {text("Fleet"), text(std::format("{} vessel{}", members, members == 1 ? "" : "s")),
                        number(move, std::format("{}/{}", move, maxMove)), number(damage, std::format("{}/{}", damage, structure)),
                        number(supply, endless && members > 0 ? std::string("Endless")
                                                              : std::format("{} / {}", formatNumber(supply), formatNumber(supplyCap)))};
            case ShipsTab::Orders: {
                OrderOwner owner;
                owner.fleet = f.id;
                return {text("Fleet"), text(ordersCell(ui, owner, {}))};
            }
            case ShipsTab::Cargo:
                return {number(used, std::format("{}kT", formatNumber(used))), number(cap, std::format("{}kT", formatNumber(cap))),
                        text(std::format("{} vessel{}", members, members == 1 ? "" : "s"))};
            case ShipsTab::Fleet: return {number(f.experience, game::combat::experienceLabel(f.experience, f.experienceTenths)), text(f.name)};
            case ShipsTab::Maintenance: return {number(maintenance.v[0]), number(maintenance.v[1]), number(maintenance.v[2])};
        }
        return {};
    }

    void sortRows(std::vector<ListRow>& rows) const {
        if (sortColumn_ < 1) return;  // unsorted: ships, units, fleets as the engine lists them
        const int col = sortColumn_;
        const bool desc = sortDescending_;
        std::stable_sort(rows.begin(), rows.end(), [&](const ListRow& a, const ListRow& b) {
            int c = 0;
            if (col == 1) {
                c = a.name.compare(b.name);
            } else {
                const size_t i = static_cast<size_t>(col - 2);
                if (i >= a.cells.size() || i >= b.cells.size()) return false;
                const Cell& x = a.cells[i];
                const Cell& y = b.cells[i];
                c = x.numeric && y.numeric ? (x.key < y.key ? -1 : x.key > y.key ? 1 : 0) : x.text.compare(y.text);
            }
            return desc ? c > 0 : c < 0;
        });
    }

    void table(UiContext& ui) {
        const std::vector<ListRow>& rows = rows_;
        const std::vector<Column> cols = columnsOf(tab_);
        const int ncols = static_cast<int>(cols.size()) + 2;
        const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        ImGui::Dummy(ImVec2(0, ui.px(4)));
        // A new ID per tab so each tab keeps its own column setup and sort.
        ImGui::PushID(static_cast<int>(tab_));
        if (!ImGui::BeginTable("##ships", ncols, flags, ImVec2(0, 0))) {
            ImGui::PopID();
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Pic", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, ui.px(42));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        for (const Column& c : cols) {
            if (c.width > 0) ImGui::TableSetupColumn(c.name, ImGuiTableColumnFlags_WidthFixed, ui.px(c.width));
            else ImGui::TableSetupColumn(c.name, ImGuiTableColumnFlags_WidthStretch, 1.4f);
        }
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                sortColumn_ = specs->Specs[0].ColumnIndex;
                sortDescending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            } else {
                sortColumn_ = -1;
            }
            specs->SpecsDirty = false;
            sortDirty_ = true;  // re-sorted from the engine's order next frame
        }

        const float rowH = ui.px(38);
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
                const bool left = ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                                    ImVec2(0, rowH));
                if (ImGui::IsItemHovered()) hovered_ = row.system;
                const bool right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                ImGui::PopID();
                if (row.picture) {
                    const float ps = ui.px(32);
                    const ImVec2 a{p0.x + ui.px(3), p0.y + (rowH - ps) * 0.5f};
                    ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(row.picture.tex.value)), a, ImVec2(a.x + ps, a.y + ps),
                                                         ImVec2(row.picture.uv.min.x, row.picture.uv.min.y),
                                                         ImVec2(row.picture.uv.max.x, row.picture.uv.max.y));
                }
                auto cell = [&](int column, const std::string& str) {
                    ImGui::TableSetColumnIndex(column);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - textH) * 0.5f);
                    ImGui::TextUnformatted(str.c_str());
                };
                cell(1, row.name);
                for (size_t c = 0; c < row.cells.size(); ++c) cell(static_cast<int>(c) + 2, row.cells[c].text);
                if (left) {
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
        ImGui::PopID();
    }

    struct Totals {
        int ships = 0, units = 0, fleets = 0;
        game::Resources maintenance;
        std::vector<game::SystemId> present;
    };

    ShipsTab tab_ = ShipsTab::General;
    bool ships_ = true, units_ = true, fleets_ = true;
    uint64_t cacheKey_ = 0;
    bool sortDirty_ = false;
    std::vector<ListRow> rows_;
    Totals totals_;
    int sortColumn_ = -1;
    bool sortDescending_ = false;
    std::optional<game::SystemId> hovered_;
    std::optional<game::VehicleId> selected_;
    ReportPopup report_;
};

} // namespace

std::unique_ptr<Screen> makeShips(const ScreenArgs& args) { return std::make_unique<ShipsScreen>(args); }

} // namespace opense4::client::classic
