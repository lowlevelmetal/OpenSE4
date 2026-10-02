// Planets (F4) and Colonies (F5) windows (docs/spec/06 §1.2, §1.8.1, spec 02 §11).

#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opense4::client::classic {

namespace {

std::string percent(int64_t v) { return std::format("{}%", v); }
// Conditions show as their band (spec 02 §2).
std::string conditionsText(game::Conditions conditions) { return std::string(game::economy::conditionsName(game::economy::conditionsBand(conditions))); }

// Selection with Ctrl/Shift toggling (multi-select), plain click selecting one.
void clickSelect(std::vector<game::ObjectId>& sel, game::ObjectId id) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl || io.KeyShift) {
        if (auto it = std::find(sel.begin(), sel.end(), id); it != sel.end()) sel.erase(it);
        else sel.push_back(id);
    } else {
        sel.assign(1, id);
    }
}

bool contains(const std::vector<game::ObjectId>& v, game::ObjectId id) { return std::find(v.begin(), v.end(), id) != v.end(); }

// ============================================================================================
// Planets (spec 06 §1.8.1, confirmed: binary)
// ============================================================================================

constexpr std::array<const char*, 10> kFilterLabels{
    {"All", "Colonizable", "All Colonies", "Enemy Colonies", "Ally Colonies", "Coloniz\\Empty", "Coloniz\\Breathe", "Ship Enroute", "Asteroids",
     "Special"}};
// The filters' ids for lessons (learn/ids.hpp windowTabs).
constexpr std::array<const char*, 10> kFilterIds{{"all", "colonizable", "all-colonies", "enemy-colonies", "ally-colonies", "colonizable-empty",
                                                  "colonizable-breathable", "ship-enroute", "asteroids", "special"}};

enum PlanetColumn { PcPicture, PcName, PcAtmosphere, PcMinerals, PcOrganics, PcRadioactives, PcEnroute };
// "Pic" over the picture; three "Value" headings, each with its resource's
// icon and always in its resource colour (spec 06 §5.4; observed, spec 07
// session 3).
constexpr std::array<ListColumn, 7> kPlanetColumns{{{kPicHeading, 40},
                                                    {"Name", 145},
                                                    {"Atmosphere", 95},
                                                    {"Value", 58, palette::kMinerals, true, static_cast<int>(Icon::Minerals)},
                                                    {"Value", 58, palette::kOrganics, true, static_cast<int>(Icon::Organics)},
                                                    {"Value", 58, palette::kRadioactives, true, static_cast<int>(Icon::Radioactives)},
                                                    {"Ship Enroute", 0}}};
constexpr std::array<uint32_t, 3> kValueColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};

// "Rock - Medium" (the grey second line of the Name column).
std::string typeLine(const game::SpaceObject& o) {
    const std::string kind = o.kind == game::ObjectKind::Asteroids ? std::string("Asteroids") : o.surface;
    return o.size.empty() ? kind : std::format("{} - {}", kind, o.size);
}

class PlanetsScreen final : public Screen {
public:
    explicit PlanetsScreen(const ScreenArgs&) {}

    bool draw(UiContext& ui) override {
        if (!restored_) {
            // The tab comes back from the empire (spec 06 §1.8.1).
            filter_ = static_cast<PlanetFilter>(std::min<int>(ui.options().planetsTab, int(PlanetFilter::Count) - 1));
            restored_ = true;
        }
        if (revision_ != ui.session.revision()) refresh(ui);
        bool leave = false;
        {
            Dialog d(ui, "Planets", DialogSize::Tall);
            if (!d.open()) return d.keepOpen();
            const game::GameState& s = ui.state();
            const bool noAvoid = ui.options().planetsNoSysToAvoid;
            const std::vector<const PlanetInfo*> rows = shown(ui, noAvoid);

            d.beginContent();
            statistics(ui);
            ImGui::SetCursorPos(ui.size({290, 3}));
            std::vector<uint8_t> marked(s.galaxy.systems.size(), 0);
            for (const PlanetInfo* p : rows) marked[p->system.index()] = 1;
            std::optional<game::SystemId> highlight;
            if (hovered_) highlight = s.galaxy.object(*hovered_).system;
            quadrantMap(ui, "##map", {262, 190}, marked, highlight);
            hovered_.reset();
            ImGui::SetCursorPos(ui.size({0, 197}));
            leave = list(ui, rows);
            // Our command results, under the statistics (the list fills the window below).
            status_.drawAt(ui, {3, 183}, 280);

            d.beginButtons();
            ImVec2 filtersMin, filtersMax;
            for (size_t i = 0; i < kFilterLabels.size(); ++i) {
                if (d.tab(kFilterLabels[i], filter_ == static_cast<PlanetFilter>(i))) filter_ = static_cast<PlanetFilter>(i);
                ui.tagTab(kFilterIds[i], filter_ == static_cast<PlanetFilter>(i));
                if (i == 0) filtersMin = ImGui::GetItemRectMin();
                filtersMax = ImGui::GetItemRectMax();
            }
            ui.tag("planets:filters", filtersMin, filtersMax);
            d.spacer();
            // An on/off setting: a check box (observed, spec 07 session 3), stored
            // with the empire when clicked (spec 06 §1.8.1).
            if (d.check("No Sys To Avoid", noAvoid)) {
                game::InterfaceOptions o = ui.options();
                o.planetsNoSysToAvoid = !noAvoid;
                if (!ui.setOptions(o)) status_.fail("The option cannot be changed now.");
            }
            ui.tagItem("planets:no-sys-to-avoid");
            const bool send = d.button("Send Colony Ship", stats_.available > 0);
            ui.tagItem("planets:send-colony-ship");
            if (send) {
                picking_ = true;
                pickRows_.clear();
                for (const PlanetInfo* p : rows) pickRows_.push_back(p->id);
                picked_.reset();
            }
            d.close();
            report_.draw(ui);
            if (pickerPopup(ui)) leave = true;
            leave = leave || !d.keepOpen();
        }
        if (leave) {
            // The tab is stored with the empire when the window closes.
            game::InterfaceOptions o = ui.options();
            o.planetsTab = static_cast<uint8_t>(filter_);
            ui.setOptions(o);
            return false;
        }
        return true;
    }

private:
    void refresh(UiContext& ui) {
        all_ = surveyPlanets(ui.rules(), ui.state(), ui.session.player());
        stats_ = planetStatistics(ui.rules(), ui.state(), ui.session.player(), colonyShips(ui.rules(), ui.state(), ui.session.player()));
        revision_ = ui.session.revision();
    }

    // The current tab's planets, in the sort order.
    std::vector<const PlanetInfo*> shown(UiContext& ui, bool noAvoid) const {
        const game::GameState& s = ui.state();
        std::vector<const PlanetInfo*> rows;
        for (const PlanetInfo& p : all_)
            if (matches(filter_, p) && !(noAvoid && p.avoided)) rows.push_back(&p);
        // Fixed directions (spec 06 §1.8.1): names A to Z, values highest first,
        // the picture and Ship Enroute by planet size, smallest first.
        sortByKeys(rows, sortKeys(ui.options().planetsSort, PcName), [&](int column, const PlanetInfo* a, const PlanetInfo* b) {
            const game::SpaceObject& oa = s.galaxy.object(a->id);
            const game::SpaceObject& ob = s.galaxy.object(b->id);
            switch (column) {
                case PcName: return compareNames(oa.name, ob.name);
                case PcAtmosphere: return compareNames(oa.atmosphere, ob.atmosphere);
                case PcMinerals:
                case PcOrganics:
                case PcRadioactives: {
                    const size_t k = size_t(column - PcMinerals);
                    return oa.value[k] == ob.value[k] ? 0 : oa.value[k] > ob.value[k] ? -1 : 1;
                }
                default: return a->sizeRank == b->sizeRank ? 0 : a->sizeRank < b->sizeRank ? -1 : 1;
            }
        });
        return rows;
    }

    void statistics(UiContext& ui) {
        // Labels at x 18 one every 16 px from y 40, values right-aligned at x 289 (window pixels).
        const PlanetStatistics& st = stats_;
        // The original's labels (observed, spec 07 session 3): the lines under a
        // count start with "..." and narrow it.
        const std::array<std::pair<const char*, int>, 10> lines{{{"Known Systems", st.systems},
                                                                 {"Number of Planets", st.planets},
                                                                 {"Number of Colonizable Planets", st.colonizable},
                                                                 {"... which are owned by Enemies", st.enemy},
                                                                 {"... which are owned by Allies", st.ally},
                                                                 {"... which are owned by Non-Aligned", st.nonAligned},
                                                                 {"... which are not Colonized", st.free},
                                                                 {"... which are Breathable", st.freeBreathable},
                                                                 {"Colonizing Ships", st.colonyShips},
                                                                 {"... which are available", st.available}}};
        for (size_t i = 0; i < lines.size(); ++i) {
            const float y = 5.0f + 16.0f * float(i) + (i >= 8 ? 16.0f : 0.0f);
            ImGui::SetCursorPos(ui.size({3, y}));
            ImGui::TextColored(kLabelBlue, "%s", lines[i].first);
            const std::string value = std::to_string(lines[i].second);
            ImGui::SetCursorPos(ImVec2(ui.px(274) - ImGui::CalcTextSize(value.c_str()).x, ui.px(y)));
            ImGui::TextUnformatted(value.c_str());
        }
    }

    // The list; returns true when a row was left-clicked (the window closes and shows that planet).
    bool list(UiContext& ui, const std::vector<const PlanetInfo*>& rows) {
        const game::GameState& s = ui.state();
        // The headings span the rows, not the arrow column.
        const float width = listRowsWidth(ui, ImGui::GetContentRegionAvail().x);
        if (const int c = listHeader(ui, "##planethead", kPlanetColumns, width); c >= 0) {
            // The sort keys are stored with the empire at once (spec 06 §7 Q24).
            game::InterfaceOptions o = ui.options();
            o.planetsSort = clickSort(o.planetsSort, c, PcName);
            ui.setOptions(o);
        }
        const std::vector<float> x = columnEdges(ui, kPlanetColumns, width);
        bool leave = false;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        beginList(ui, "##planetrows", ImVec2(0, listRowsHeight(ui)));
        ImGui::PopStyleVar();
        const float rowH = ui.px(kListRowH);
        ImGuiListClipper clipper;
        clipper.Begin(int(rows.size()), rowH);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const PlanetInfo& p = *rows[size_t(i)];
                const game::SpaceObject& o = s.galaxy.object(p.id);
                ImGui::PushID(int(p.id.index()));
                const ImVec2 a = ImGui::GetCursorScreenPos();
                const bool clicked = ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0, rowH - ImGui::GetStyle().ItemSpacing.y));
                const bool hovered = ImGui::IsItemHovered();
                if (hovered) hovered_ = p.id;
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) report_.openPlanet(p.id);
                // Left-click: the window closes and the main window shows the planet.
                if (clicked) {
                    ui.requests.selectPlanet = p.id;
                    leave = true;
                }
                ImGui::PopID();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float top = a.y + ui.px(2);
                if (Sprite pic = objectSprite(ui, o))
                    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(pic.tex.value)), ImVec2(a.x + x[0] + ui.px(2), top),
                                 ImVec2(a.x + x[0] + ui.px(32), top + ui.px(30)), {pic.uv.min.x, pic.uv.min.y}, {pic.uv.max.x, pic.uv.max.y});
                const ImU32 white = IM_COL32_WHITE;
                const float lh = ImGui::GetTextLineHeight();
                auto text = [&](float cx, float cy, ImU32 color, const std::string& t, float cw) {
                    dl->PushClipRect(ImVec2(a.x + cx, a.y), ImVec2(a.x + cx + cw, a.y + rowH), true);
                    dl->AddText(ImVec2(a.x + cx + ui.px(2), cy), color, t.c_str());
                    dl->PopClipRect();
                };
                // Every name in white, our own colonies' too; the atmosphere on the
                // name's line (observed, spec 07 session 3).
                text(x[PcName], top, white, o.name, x[PcName + 1] - x[PcName]);
                ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
                text(x[PcName], top + lh + ui.px(1), imColor(palette::kSecondary), typeLine(o), x[PcName + 1] - x[PcName]);
                ImGui::PopFont();
                const float mid = a.y + (rowH - lh) * 0.5f;
                text(x[PcAtmosphere], top, white, p.asteroids ? std::string("None") : o.atmosphere, x[PcAtmosphere + 1] - x[PcAtmosphere]);
                for (size_t k = 0; k < 3; ++k) {
                    // A percentage, or the amount left when resources are finite.
                    const std::string v = s.options.finiteResources ? formatNumber(o.value[k]) : std::format("{}%", o.value[k]);
                    const int col = PcMinerals + int(k);
                    text(x[size_t(col)], mid, imColor(kValueColors[k]), v, x[size_t(col) + 1] - x[size_t(col)]);
                }
                if (p.enroute) text(x[PcEnroute], mid, white, p.enrouteShip, x[PcEnroute + 1] - x[PcEnroute]);
            }
        if (rows.empty()) ImGui::TextColored(kTextDim, "No planets on this tab.");
        endList(ui);
        ui.tagItem("planets:list");
        return leave;
    }

    // "Select Planet to Colonize" over the current tab's planets; returns true
    // when the window should close (turn-based: it shows the ship that goes).
    bool pickerPopup(UiContext& ui) {
        const char* id = "Select Planet to Colonize###colonizepick";
        if (picking_) {
            ImGui::OpenPopup(id);
            picking_ = false;
        }
        if (!beginModal(ui, id, {420, 520})) return false;
        const game::GameState& s = ui.state();
        bool leave = false;
        const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
        beginList(ui, "##pick", ImVec2(0, -footer), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        for (game::ObjectId id2 : pickRows_) {
            const game::SpaceObject& o = s.galaxy.object(id2);
            const std::string label = std::format("{}   ({}, {})###p{}", o.name, typeLine(o), s.galaxy.system(o.system).name, id2.index());
            if (ImGui::Selectable(label.c_str(), picked_ == id2, ImGuiSelectableFlags_AllowDoubleClick)) picked_ = id2;
        }
        if (pickRows_.empty()) ImGui::TextColored(kTextDim, "No planets on this tab.");
        endList(ui);
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginDisabled(!picked_);
        const bool go = ImGui::Button("Colonize", ImVec2(w, ui.px(26)));
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", ImVec2(w, ui.px(26)));
        if (go && picked_) {
            ImGui::CloseCurrentPopup();
            leave = send(ui, *picked_);
        } else if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return leave;
    }

    bool send(UiContext& ui, game::ObjectId planet) {
        // The planet is not checked (an already colonized one gets the orders
        // anyway: Colonize fails when the ship arrives), and nothing is said
        // when no ship can go (spec 06 §7 Q25, confirmed: binary).
        const game::GameState& s = ui.state();
        const auto ship = chooseColonyShip(ui.rules(), s, ui.session.player(), planet);
        if (!ship) return false;
        if (!status_.issue(ui, sendColonyShipOrders(ui.rules(), s, *ship, planet))) return false;
        // Turn-based: the window closes and the main window shows the ship; a
        // simultaneous game keeps it open (spec 06 §1.8.1).
        if (ui.session.turnBased()) {
            ui.requests.selectVehicle = *ship;
            return true;
        }
        return false;
    }

    std::vector<PlanetInfo> all_;
    PlanetStatistics stats_;
    uint64_t revision_ = 0;
    bool restored_ = false;
    PlanetFilter filter_ = PlanetFilter::All;
    std::optional<game::ObjectId> hovered_;
    bool picking_ = false;
    std::vector<game::ObjectId> pickRows_;
    std::optional<game::ObjectId> picked_;
    StatusLine status_;
    ReportPopup report_;
};

// ============================================================================================
// Colonies
// ============================================================================================

constexpr std::array<std::pair<ColonyTab, const char*>, 9> kColonyTabs{{
    {ColonyTab::General, "General"},
    {ColonyTab::Value, "Value"},
    {ColonyTab::Production, "Production"},
    {ColonyTab::Facilities, "Facilities"},
    {ColonyTab::Cargo, "Cargo"},
    {ColonyTab::Construction, "Construction"},
    {ColonyTab::Status, "Status"},
    {ColonyTab::Races, "Races"},
    {ColonyTab::Orders, "Orders"},
}};

constexpr std::array<const char*, 9> kColonyTabIds{
    {"general", "value", "production", "facilities", "cargo", "construction", "status", "races", "orders"}};

// Everything a Colonies row shows, computed once per state revision: what it
// is sorted by in every tab (colony_logic.hpp) and what only the eye needs.
struct ColonyRow {
    game::ObjectId planet;
    ColonySortValues keys;
    std::string secondLine;           // "type - size" (grey), or "Blockaded" (red)
    bool blockaded = false;
    bool delivered = true;            // its output reaches the empire (no brackets)
    int64_t maxPopulation = 0;
    game::Mood mood = game::Mood::Indifferent;
    std::string conditions;           // the band's name
    std::string cargo;                // the cargo in words
    int queueLength = 0;
    std::vector<int> icons;
    std::vector<std::string> orders;  // the colony's order list, one a line
};

// A heading of the Colonies list: the column it stands for, its label and
// width (spec 06 §1.8.3; 0 takes the rest) and a fixed colour.
struct ColonyHeading {
    ColonyColumn id;
    const char* label;
    float width;
    uint32_t color = 0;
};

std::vector<ColonyHeading> colonyHeadings(ColonyTab tab) {
    using C = ColonyColumn;
    std::vector<ColonyHeading> h{{C::Picture, kPicHeading, 40}, {C::Name, "Name", 150}};
    auto add = [&](std::initializer_list<ColonyHeading> more) { h.insert(h.end(), more); };
    switch (tab) {
        case ColonyTab::General: add({{C::Atmosphere, "Atmosphere", 90}, {C::Conditions, "Conditions", 90}, {C::Population, "Pop", 65}, {C::Mood, "Mood", 0}}); break;
        case ColonyTab::Value:
            add({{C::ColonyType, "Type", 120},
                 {C::MineralsValue, "Min.", 70, palette::kMinerals},
                 {C::OrganicsValue, "Org.", 70, palette::kOrganics},
                 {C::RadioactivesValue, "Rad.", 0, palette::kRadioactives}});
            break;
        case ColonyTab::Production:
            add({{C::Minerals, "Min.", 60}, {C::Organics, "Org.", 60}, {C::Radioactives, "Rad.", 60}, {C::Research, "Res", 60}, {C::Intelligence, "Intel", 0}});
            break;
        case ColonyTab::Facilities: add({{C::FacilitiesBuilt, "Num", 50}, {C::FacilitySlots, "Max", 50}, {C::FacilityList, "Facilities", 0}}); break;
        case ColonyTab::Cargo: add({{C::CargoUsed, "Space", 65}, {C::CargoCapacity, "Max", 65}, {C::CargoItems, "Cargo Items", 0}}); break;
        case ColonyTab::Construction: add({{C::UnderConstruction, "Under Construction", 200}, {C::TimeRemaining, "Time Remaining", 0}}); break;
        case ColonyTab::Status: add({{C::Status, "Status", 0}}); break;
        case ColonyTab::Races: add({{C::RacePopulation, "Population", 0}}); break;
        case ColonyTab::Orders: add({{C::Orders, "Orders", 0}}); break;
        case ColonyTab::Count: break;
    }
    return h;
}

class ColoniesScreen final : public Screen {
public:
    explicit ColoniesScreen(const ScreenArgs& args) {
        if (args.planet.valid()) selection_.assign(1, args.planet);
    }

    bool draw(UiContext& ui) override {
        if (revision_ != ui.session.revision()) refresh(ui);
        bool keep = true;
        {
            Dialog d(ui, "Colonies", DialogSize::Tall);
            if (!d.open()) return d.keepOpen();
            d.beginContent();
            statistics(ui);
            // The mini-map where Planets has it (inferred, spec 06 §7 Q90).
            ImGui::SetCursorPos(ui.size({290, 3}));
            std::vector<uint8_t> marked(ui.state().galaxy.systems.size(), 0);
            for (const ColonyRow& r : rows_) marked[ui.state().galaxy.object(r.planet).system.index()] = 1;
            std::optional<game::SystemId> highlight;
            if (hovered_) highlight = ui.state().galaxy.object(*hovered_).system;
            else if (!selection_.empty()) highlight = ui.state().galaxy.object(selection_.front()).system;
            quadrantMap(ui, "##map", {262, 190}, marked, highlight);
            hovered_.reset();
            status_.drawAt(ui, {3, 183}, 280);
            ImGui::SetCursorPos(ui.size({0, 197}));
            table(ui);

            d.beginButtons();
            for (const auto& [tab, label] : kColonyTabs) {
                if (lampButton(d, ui, label, tab_ == tab)) setTab(tab);
                ui.tagTab(kColonyTabIds[static_cast<size_t>(tab)], tab_ == tab);
            }
            // The original's column: the nine tabs, two empty slots, the two
            // actions just above Close in the 14th slot; no Constr. Queue or Goto
            // (observed, spec 07 session 3; the actions' slots inferred, Q90).
            d.spacer();
            d.spacer();
            if (d.button("Scrap Facil Types")) scrapTypes_.open(selection_.size() > 1 ? selection_ : std::vector<game::ObjectId>{});
            if (ImGui::IsItemHovered())
                itemTooltip(selection_.size() > 1 ? "Scrap every facility of one type on the selected colonies"
                                                  : "Scrap every facility of one type on all colonies");
            if (d.button("Set Colony Type", !selection_.empty())) {
                pendingColonyType_ = true;
                chosenType_ = -1;
            }
            if (d.close()) return false;
            report_.draw(ui);
            scrapTypes_.draw(ui, status_);
            colonyTypePopup(ui);
            keep = d.keepOpen();
        }
        if (goto_) {
            ui.requests.selectPlanet = *goto_;
            return false;
        }
        return keep;
    }

private:
    void setTab(ColonyTab t) {
        if (t == tab_) return;
        tab_ = t;
        tableId_ = std::format("##colonies{}", static_cast<int>(t));
    }

    void refresh(UiContext& ui) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        rows_.clear();
        for (const auto& c : s.colonies) {
            if (!c || c->owner != me) continue;
            const game::SpaceObject& o = s.galaxy.object(c->planet);
            ColonyRow row;
            row.planet = c->planet;
            row.keys = colonySortValues(r, s, *c);
            const game::economy::ColonyOutput out = game::economy::colonyOutput(r, s, *c);
            row.blockaded = out.blockaded;
            row.delivered = out.connected && !out.blockaded;
            row.secondLine = out.blockaded ? std::string("Blockaded") : typeLine(o);
            row.maxPopulation = game::maxPopulation(r, s, *c);
            row.mood = game::moodFromAnger(c->anger);
            row.conditions = conditionsText(o.conditions);
            row.cargo = cargoText(s, c->planet);
            row.queueLength = static_cast<int>(c->queue.items.size());
            row.icons = colonyStatusIcons(r, s, *c, out.connected);
            shipui::OrderOwner owner;
            owner.planet = c->planet;
            row.orders = shipui::orderListLines(ui, owner);
            rows_.push_back(std::move(row));
        }
        std::erase_if(selection_, [&](game::ObjectId id) { return !s.colony(id) || s.colony(id)->owner != me; });
        revision_ = ui.session.revision();
    }

    void statistics(UiContext& ui) {
        // The original's summary (observed, spec 07 session 3), laid out as the
        // Planets statistics: labels at x 18 one every 16 px, values right-aligned
        // at x 289, the resource amounts on the line under their label (inferred,
        // spec 06 §7 Q90).
        const game::GameState& s = ui.state();
        int64_t population = 0, research = 0, intel = 0;
        int blockaded = 0;
        game::Resources production;
        std::vector<game::SystemId> systems;
        for (const ColonyRow& r : rows_) {
            population += r.keys.population;
            production += r.keys.production;
            research += r.keys.research;
            intel += r.keys.intelligence;
            blockaded += r.blockaded ? 1 : 0;
            const game::SystemId sys = s.galaxy.object(r.planet).system;
            if (std::find(systems.begin(), systems.end(), sys) == systems.end()) systems.push_back(sys);
        }
        const game::Resources storage = game::economy::storageCapacity(ui.rules(), s, ui.session.player());
        float y = 5.0f;
        auto line = [&](const char* label, const std::string& value, std::optional<Icon> icon = std::nullopt) {
            ImGui::SetCursorPos(ui.size({3, y}));
            ImGui::TextColored(kLabelBlue, "%s", label);
            const float iconW = icon ? 18.0f : 0.0f;
            ImGui::SetCursorPos(ImVec2(ui.px(274 - iconW) - ImGui::CalcTextSize(value.c_str()).x, ui.px(y)));
            ImGui::TextUnformatted(value.c_str());
            if (icon) {
                ImGui::SetCursorPos(ui.size({274 - 16, y}));
                image(ui, ui.art.icon16(*icon), {16, 16});
            }
            y += 16.0f;
        };
        // Three amounts, each in its resource colour with its icon.
        auto amounts = [&](const std::array<std::string, 3>& v) {
            static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
            static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
            float x = 274.0f;
            for (size_t i = 3; i-- > 0;) {
                x -= 16.0f;
                ImGui::SetCursorPos(ui.size({x, y}));
                image(ui, ui.art.icon16(kIcons[i]), {16, 16});
                x -= 2.0f + ImGui::CalcTextSize(v[i].c_str()).x / ui.k();
                ImGui::SetCursorPos(ui.size({x, y}));
                ImGui::TextColored(imColorV(kColors[i]), "%s", v[i].c_str());
                x -= 10.0f;
            }
            y += 16.0f;
        };
        line("Systems with Colonies", std::to_string(systems.size()));
        line("Number of Colonies", std::to_string(rows_.size()));
        line("Number of Blockaded Colonies", std::to_string(blockaded));
        line("Total Population", std::format("{}M", formatNumber(population)), Icon::Population);
        line("Research Points Produced", formatNumber(research), Icon::Research);
        line("Intelligence Points Produced", formatNumber(intel), Icon::Intelligence);
        line("Total Resources Produced", {});
        amounts({formatNumber(production.v[0]), formatNumber(production.v[1]), formatNumber(production.v[2])});
        line("Maximum Resource Storage", {});
        // Storage in thousands, as "50kT" (observed; whole thousands, rounded down: inferred, Q90).
        amounts({std::format("{}kT", storage.v[0] / 1000), std::format("{}kT", storage.v[1] / 1000), std::format("{}kT", storage.v[2] / 1000)});
    }

    static std::string cargoText(const game::GameState& s, game::ObjectId planet) {
        const game::Colony* c = s.colony(planet);
        if (!c || c->cargo.empty()) return "Empty";
        std::string t;
        for (const auto& p : c->cargo.population)
            t += std::format("{}{} {}M", t.empty() ? "" : ", ", p.race.valid() ? s.empire(p.race).race.name : "Unknown", formatNumber(p.millions));
        for (const auto& u : c->cargo.units) t += std::format("{}{} x{}", t.empty() ? "" : ", ", s.design(u.design).name, u.count);
        return t;
    }

    // The list (spec 06 §1.8.3, confirmed: binary): the headings, every one a
    // sort key by the column it stands for, and rows of 36 px.
    void table(UiContext& ui) {
        const game::GameState& s = ui.state();
        const std::vector<ColonyHeading> heads = colonyHeadings(tab_);
        std::vector<ListColumn> cols;
        for (const ColonyHeading& h : heads) cols.push_back({h.label, h.width, h.color});
        // The headings span the rows, not the arrow column.
        const float width = listRowsWidth(ui, ImGui::GetContentRegionAvail().x);
        if (const int c = listHeader(ui, tableId_.c_str(), cols, width); c >= 0 && static_cast<size_t>(c) < heads.size()) {
            // A click on any heading takes the first slot, a column without a
            // key too; the keys are stored with the empire at once (§7 Q24).
            game::InterfaceOptions o = ui.options();
            o.coloniesSort = clickSort(o.coloniesSort, static_cast<int>(heads[static_cast<size_t>(c)].id), static_cast<int>(ColonyColumn::Name));
            ui.setOptions(o);
        }
        const std::vector<float> x = columnEdges(ui, cols, width);
        std::vector<ColonySortValues> keys;
        for (const ColonyRow& r : rows_) keys.push_back(r.keys);
        const std::vector<size_t> order = colonyRowOrder(keys, ui.options().coloniesSort);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        beginList(ui, "##colonyrows", ImVec2(0, listRowsHeight(ui)));
        ImGui::PopStyleVar();
        const float rowH = ui.px(kListRowH);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(order.size()), rowH);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const ColonyRow& row = rows_[order[static_cast<size_t>(i)]];
                ImGui::PushID(static_cast<int>(row.planet.index()));
                const ImVec2 a = ImGui::GetCursorScreenPos();
                const bool clicked = ImGui::Selectable("##row", contains(selection_, row.planet), ImGuiSelectableFlags_AllowDoubleClick,
                                                       ImVec2(0, rowH - ImGui::GetStyle().ItemSpacing.y));
                const bool hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                if (hovered) hovered_ = row.planet;
                if (clicked) clickSelect(selection_, row.planet);
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) report_.openPlanet(row.planet);
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) goto_ = row.planet;
                for (size_t k = 0; k < heads.size(); ++k) drawCell(ui, s, row, heads[k].id, a, x[k], x[k + 1] - x[k]);
            }
        if (rows_.empty()) ImGui::TextColored(kTextDim, "No colonies.");
        endList(ui);
        ui.tagItem("colonies:list");
    }

    // One cell of a row whose top left is `a`, the column from `cx` (`cw` wide).
    void drawCell(UiContext& ui, const game::GameState& s, const ColonyRow& row, ColonyColumn id, ImVec2 a, float cx, float cw) const {
        using C = ColonyColumn;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowH = ui.px(kListRowH);
        const float lh = ImGui::GetTextLineHeight();
        const float top = a.y + ui.px(2);
        const float mid = a.y + (rowH - lh) * 0.5f;
        const ImU32 white = IM_COL32_WHITE;
        const ColonySortValues& k = row.keys;
        auto text = [&](float cy, ImU32 color, const std::string& t) {
            dl->PushClipRect(ImVec2(a.x + cx, a.y), ImVec2(a.x + cx + cw, a.y + rowH), true);
            dl->AddText(ImVec2(a.x + cx + ui.px(2), cy), color, t.c_str());
            dl->PopClipRect();
        };
        auto small = [&](float cy, ImU32 color, const std::string& t) {
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            text(cy, color, t);
            ImGui::PopFont();
        };
        auto picture = [&](const Sprite& sp, float px, float size) {
            if (!sp) return;
            const ImVec2 p0{a.x + px, a.y + (rowH - size) * 0.5f};
            dl->PushClipRect(ImVec2(a.x + cx, a.y), ImVec2(a.x + cx + cw, a.y + rowH), true);
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(sp.tex.value)), p0, ImVec2(p0.x + size, p0.y + size), {sp.uv.min.x, sp.uv.min.y},
                         {sp.uv.max.x, sp.uv.max.y});
            dl->PopClipRect();
        };
        // Output that does not reach the empire is shown in brackets.
        auto output = [&](int64_t v) { return row.delivered ? formatNumber(v) : std::format("({})", formatNumber(v)); };
        switch (id) {
            case C::Picture:
                if (Sprite pic = objectSprite(ui, s.galaxy.object(row.planet)))
                    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(pic.tex.value)), ImVec2(a.x + cx + ui.px(2), top),
                                 ImVec2(a.x + cx + ui.px(32), top + ui.px(30)), {pic.uv.min.x, pic.uv.min.y}, {pic.uv.max.x, pic.uv.max.y});
                break;
            case C::Name:
                text(top, white, k.name);
                small(top + lh + ui.px(1), row.blockaded ? imColor(0xff0000) : imColor(palette::kSecondary), row.secondLine);
                break;
            case C::Atmosphere: text(mid, white, k.atmosphere); break;
            case C::Conditions: text(mid, white, row.conditions); break;
            case C::Population:
                text(top, white, std::format("{}M", formatNumber(k.population)));
                small(top + lh + ui.px(1), imColor(palette::kSecondary), std::format("{}M", formatNumber(row.maxPopulation)));
                break;
            case C::Mood: text(mid, row.mood >= game::Mood::Unhappy ? ImGui::ColorConvertFloat4ToU32(kTextWarn) : white, k.mood); break;
            case C::ColonyType: text(mid, white, k.colonyType); break;
            case C::MineralsValue:
            case C::OrganicsValue:
            case C::RadioactivesValue: {
                const size_t i = static_cast<size_t>(id) - static_cast<size_t>(C::MineralsValue);
                constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
                text(mid, imColor(kColors[i]), percent(k.value[i]));
                break;
            }
            case C::Minerals:
            case C::Organics:
            case C::Radioactives: text(mid, white, output(k.production.v[static_cast<size_t>(id) - static_cast<size_t>(C::Minerals)])); break;
            case C::Research: text(mid, white, output(k.research)); break;
            case C::Intelligence: text(mid, white, output(k.intelligence)); break;
            case C::FacilitiesBuilt: text(mid, white, std::to_string(k.facilities)); break;
            case C::FacilitySlots: text(mid, white, std::to_string(k.slots)); break;
            case C::FacilityList: {
                const game::Colony* c = s.colony(row.planet);
                if (!c) break;
                // One icon per facility type, with a count when there are several.
                std::vector<std::pair<uint32_t, int>> types;
                for (uint32_t f : c->facilities) {
                    auto it = std::find_if(types.begin(), types.end(), [&](const auto& t) { return t.first == f; });
                    if (it == types.end()) types.emplace_back(f, 1);
                    else ++it->second;
                }
                float px = cx + ui.px(2);
                for (const auto& [f, n] : types) {
                    picture(ui.art.facility(ui.rules().facility(f).picture), px, ui.px(20));
                    px += ui.px(21);
                    if (n > 1) {
                        const std::string count = std::format("x{}", n);
                        dl->PushClipRect(ImVec2(a.x + cx, a.y), ImVec2(a.x + cx + cw, a.y + rowH), true);
                        dl->AddText(ImVec2(a.x + px, mid), ImGui::ColorConvertFloat4ToU32(kTextDim), count.c_str());
                        dl->PopClipRect();
                        px += ImGui::CalcTextSize(count.c_str()).x;
                    }
                    px += ui.px(5);
                }
                break;
            }
            case C::CargoUsed: text(mid, white, formatNumber(k.cargoUsed)); break;
            case C::CargoCapacity: text(mid, white, formatNumber(k.cargoCapacity)); break;
            case C::CargoItems: text(mid, ImGui::ColorConvertFloat4ToU32(kTextDim), row.cargo); break;
            case C::UnderConstruction: text(mid, row.queueLength == 0 ? ImGui::ColorConvertFloat4ToU32(kTextWarn) : white, k.underConstruction); break;
            case C::TimeRemaining: text(mid, white, k.timeRemaining); break;
            case C::Status: {
                float px = cx + ui.px(2);
                for (int icon : row.icons) {
                    picture(ui.art.statusIcon(icon), px, ui.px(18));
                    px += ui.px(20);
                }
                break;
            }
            case C::RacePopulation: {
                const game::Colony* c = s.colony(row.planet);
                if (!c) break;
                float px = cx + ui.px(2);
                for (const game::PopulationGroup& p : c->population) {
                    picture(ui.art.populationMini(p.race.valid() ? s.empire(p.race).race.style : ""), px, ui.px(20));
                    px += ui.px(24);
                    const std::string t = std::format("{} {}M", p.race.valid() ? s.empire(p.race).race.name : "Unknown", formatNumber(p.millions));
                    dl->PushClipRect(ImVec2(a.x + cx, a.y), ImVec2(a.x + cx + cw, a.y + rowH), true);
                    dl->AddText(ImVec2(a.x + px, mid), white, t.c_str());
                    dl->PopClipRect();
                    px += ImGui::CalcTextSize(t.c_str()).x + ui.px(14);
                }
                break;
            }
            case C::Orders:
                // The colony's own order list (launch, recover, facilities,
                // conversions), one order per 12 px line.
                for (size_t l = 0; l < row.orders.size() && l < 3; ++l) small(a.y + ui.px(12.0f * float(l) - 1.0f), white, row.orders[l]);
                break;
            case C::Count: break;
        }
    }

    void colonyTypePopup(UiContext& ui) {
        const char* id = "Set Colony Type###colonytype";
        if (pendingColonyType_) {
            ImGui::OpenPopup(id);
            pendingColonyType_ = false;
        }
        if (!beginModal(ui, id, {380, 480})) return;
        const auto& types = ui.me().colonyTypes;
        ImGui::TextColored(kTextDim, "Colony type for %zu selected colon%s:", selection_.size(), selection_.size() == 1 ? "y" : "ies");
        const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
        beginList(ui, "##types", ImVec2(0, -footer), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
        for (size_t i = 0; i < types.size(); ++i)
            if (ImGui::Selectable(types[i].c_str(), chosenType_ == static_cast<int>(i), ImGuiSelectableFlags_AllowDoubleClick))
                chosenType_ = static_cast<int>(i);
        if (types.empty()) ImGui::TextColored(kTextDim, "No colony types are defined.");
        endList(ui);
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        bool apply = chosenType_ >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        ImGui::BeginDisabled(chosenType_ < 0);
        apply = ImGui::Button("Set Type", ImVec2(w, ui.px(26))) || apply;
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (apply && chosenType_ >= 0 && static_cast<size_t>(chosenType_) < types.size()) {
            const std::string type = types[static_cast<size_t>(chosenType_)];
            int done = 0;
            for (game::ObjectId p : selection_) done += status_.issue(ui, game::cmd::SetColonyType{p, type}) ? 1 : 0;
            if (done == static_cast<int>(selection_.size())) status_.info(std::format("{} colon{} set to {}", done, done == 1 ? "y" : "ies", type));
            ImGui::CloseCurrentPopup();
        } else if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    std::vector<ColonyRow> rows_;
    uint64_t revision_ = 0;
    ColonyTab tab_ = ColonyTab::General;
    std::string tableId_ = "##colonies0";
    std::vector<game::ObjectId> selection_;
    std::optional<game::ObjectId> hovered_;
    std::optional<game::ObjectId> goto_;
    StatusLine status_;
    ReportPopup report_;
    ScrapTypePopup scrapTypes_;
    bool pendingColonyType_ = false;
    int chosenType_ = -1;
};

} // namespace

std::unique_ptr<Screen> makePlanets(const ScreenArgs& args) { return std::make_unique<PlanetsScreen>(args); }
std::unique_ptr<Screen> makeColonies(const ScreenArgs& args) { return std::make_unique<ColoniesScreen>(args); }

} // namespace opense4::client::classic
