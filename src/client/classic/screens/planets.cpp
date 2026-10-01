// Planets (F4) and Colonies (F5) windows (docs/spec/06 §1.2, §1.8.1, spec 02 §11).

#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <functional>

namespace opense4::client::classic {

namespace {

constexpr Vec2 kMapSize{236, 158};

std::string percent(int64_t v) { return std::format("{}%", v); }
// Conditions show as their band (spec 02 §2).
std::string conditionsText(game::Conditions conditions) { return std::string(game::economy::conditionsName(game::economy::conditionsBand(conditions))); }

std::string surfaceAndSize(const game::SpaceObject& o) {
    if (o.kind == game::ObjectKind::Asteroids) return "Asteroids";
    return o.size.empty() ? o.surface : std::format("{} {}", o.size, o.surface);
}

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

// A labelled statistics line in two columns.
void stat(UiContext& ui, const char* label, const std::string& value, float column = 118.0f) { labelValue(ui, label, value, column); }

// ============================================================================================
// Planets (spec 06 §1.8.1, confirmed: binary)
// ============================================================================================

constexpr std::array<const char*, 10> kFilterLabels{
    {"All", "Colonizable", "All Colonies", "Enemy Colonies", "Ally Colonies", "Coloniz\\Empty", "Coloniz\\Breathe", "Ship Enroute", "Asteroids",
     "Special"}};

enum PlanetColumn { PcPicture, PcName, PcAtmosphere, PcMinerals, PcOrganics, PcRadioactives, PcEnroute };
constexpr std::array<ListColumn, 7> kPlanetColumns{
    {{"", 40}, {"Name", 145}, {"Atmosphere", 95}, {"Min.", 58}, {"Org.", 58}, {"Rad.", 58}, {"Ship Enroute", 0}}};
constexpr float kPlanetRowH = 36.0f;
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
            status_.draw(ui);

            d.beginButtons();
            for (size_t i = 0; i < kFilterLabels.size(); ++i)
                if (d.tab(kFilterLabels[i], filter_ == static_cast<PlanetFilter>(i))) filter_ = static_cast<PlanetFilter>(i);
            d.spacer();
            // Stored with the empire when clicked (spec 06 §1.8.1).
            if (d.tab("No Sys To Avoid", noAvoid)) {
                game::InterfaceOptions o = ui.options();
                o.planetsNoSysToAvoid = !noAvoid;
                if (!ui.setOptions(o)) status_.fail("The option cannot be changed now.");
            }
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
        stats_ = planetStatistics(ui.state(), ui.session.player(), all_, colonyShips(ui.rules(), ui.state(), ui.session.player()));
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
        sort_.sort(rows, [&](int column, const PlanetInfo* a, const PlanetInfo* b) {
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
        const std::array<std::pair<const char*, int>, 10> lines{{{"Known Systems", st.systems},
                                                                 {"Planets", st.planets},
                                                                 {"Colonizable Planets", st.colonizable},
                                                                 {"Owned By Enemies", st.enemy},
                                                                 {"Owned By Allies", st.ally},
                                                                 {"Owned By Non-Aligned", st.nonAligned},
                                                                 {"Not Colonized", st.free},
                                                                 {"Not Colonized, Breathable", st.freeBreathable},
                                                                 {"Colonizing Ships", st.colonyShips},
                                                                 {"Available Colonizing Ships", st.available}}};
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
        // The rows' child has no padding; the header leaves room for its scrollbar.
        const float width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - 2.0f;
        if (const int c = listHeader(ui, "##planethead", kPlanetColumns, width); c >= 0) sort_.click(c);
        const std::vector<float> x = columnEdges(ui, kPlanetColumns, width);
        bool leave = false;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1, 1));
        ImGui::BeginChild("##planetrows", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        const float rowH = ui.px(kPlanetRowH);
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
                text(x[PcName], top, p.own ? imColor(0xffdb59) : white, o.name, x[PcName + 1] - x[PcName]);
                ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
                text(x[PcName], top + lh + ui.px(1), imColor(palette::kSecondary), typeLine(o), x[PcName + 1] - x[PcName]);
                ImGui::PopFont();
                const float mid = a.y + (rowH - lh) * 0.5f;
                text(x[PcAtmosphere], mid, white, p.asteroids ? std::string("None") : o.atmosphere, x[PcAtmosphere + 1] - x[PcAtmosphere]);
                for (size_t k = 0; k < 3; ++k) {
                    // A percentage, or the amount left when resources are finite.
                    const std::string v = s.options.finiteResources ? formatNumber(o.value[k]) : std::format("{}%", o.value[k]);
                    const int col = PcMinerals + int(k);
                    text(x[size_t(col)], mid, imColor(kValueColors[k]), v, x[size_t(col) + 1] - x[size_t(col)]);
                }
                if (p.enroute) text(x[PcEnroute], mid, white, p.enrouteShip, x[PcEnroute + 1] - x[PcEnroute]);
            }
        if (rows.empty()) ImGui::TextColored(kTextDim, "No planets on this tab.");
        ImGui::EndChild();
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
        ImGui::BeginChild("##pick", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        for (game::ObjectId id2 : pickRows_) {
            const game::SpaceObject& o = s.galaxy.object(id2);
            const std::string label = std::format("{}   ({}, {})###p{}", o.name, typeLine(o), s.galaxy.system(o.system).name, id2.index());
            if (ImGui::Selectable(label.c_str(), picked_ == id2, ImGuiSelectableFlags_AllowDoubleClick)) picked_ = id2;
        }
        if (pickRows_.empty()) ImGui::TextColored(kTextDim, "No planets on this tab.");
        ImGui::EndChild();
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
        const game::GameState& s = ui.state();
        const std::string name = s.galaxy.object(planet).name;
        if (s.colony(planet)) {
            status_.fail(std::format("{} is already colonized.", name));  // (inferred: the original's reply is not known)
            return false;
        }
        const auto ship = chooseColonyShip(ui.rules(), s, ui.session.player(), planet);
        if (!ship) {
            status_.fail(std::format("No available colony ship can colonize {} ({}).", name, s.galaxy.object(planet).surface));
            return false;
        }
        const std::string shipName = s.vehicle(*ship)->name;
        if (!status_.issue(ui, sendColonyShipOrders(s, *ship, planet))) return false;
        status_.info(std::format("{} is on its way to colonize {}.", shipName, name));
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
    SortHistory sort_{PcName};
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

enum class ColonyTab { General, Value, Production, Facilities, Cargo, Construction, Status, Races, Orders, Count };

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

// Everything a Colonies row shows, computed once per state revision.
struct ColonyRow {
    game::ObjectId planet;
    std::string name, system, type, colonyType;
    int64_t population = 0, maxPopulation = 0;
    int facilities = 0, slots = 0;
    game::Mood mood = game::Mood::Indifferent;
    std::string moodName;
    int anger = 0;
    game::economy::ColonyOutput out;
    int64_t cargoUsed = 0, cargoCapacity = 0;
    game::Resources rate;
    std::string building;
    float progress = 0;
    int turns = -1;
    int queueLength = 0;
    std::string queueMode;
    std::vector<int> icons;
    std::vector<std::string> orders;
    bool breathable = true;
    bool homeworld = false;
};

struct ColonyColumn {
    const char* name;
    float width;  // 0 = stretch
    std::function<SortKey(const ColonyRow&)> key;
    std::function<void(UiContext&, const ColonyRow&)> draw;
    bool descendingFirst = false;
};

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
            ImGui::SameLine();
            std::vector<uint8_t> marked(ui.state().galaxy.systems.size(), 0);
            for (const ColonyRow& r : rows_) marked[ui.state().galaxy.object(r.planet).system.index()] = 1;
            std::optional<game::SystemId> highlight;
            if (hovered_) highlight = ui.state().galaxy.object(*hovered_).system;
            else if (!selection_.empty()) highlight = ui.state().galaxy.object(selection_.front()).system;
            quadrantMap(ui, "##map", kMapSize, marked, highlight);
            hovered_.reset();
            table(ui);
            status_.draw(ui);

            d.beginButtons();
            for (const auto& [tab, label] : kColonyTabs)
                if (lampButton(d, ui, label, tab_ == tab)) setTab(tab);
            d.spacer();
            if (d.button("Scrap Facil Types")) scrapTypes_.open(selection_.size() > 1 ? selection_ : std::vector<game::ObjectId>{});
            if (ImGui::IsItemHovered())
                itemTooltip(selection_.size() > 1 ? "Scrap every facility of one type on the selected colonies"
                                                  : "Scrap every facility of one type on all colonies");
            if (d.button("Set Colony Type", !selection_.empty())) {
                pendingColonyType_ = true;
                chosenType_ = -1;
            }
            const bool one = selection_.size() == 1;
            if (d.button("Constr. Queue", one)) {
                ScreenArgs a;
                a.planet = selection_.front();
                ui.open(ScreenId::SetQueue, a);
            }
            ui.tagItem("colonies:queue");
            if (d.button("Goto", one)) goto_ = selection_.front();
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
        sortColumn_ = 1;
        ascending_ = true;
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
            row.name = o.name;
            row.system = s.galaxy.system(o.system).name;
            row.type = surfaceAndSize(o);
            row.colonyType = c->colonyType;
            row.population = c->totalPopulation();
            row.maxPopulation = game::maxPopulation(r, s, *c);
            row.facilities = static_cast<int>(c->facilities.size());
            row.slots = game::facilitySlots(r, s, *c);
            row.anger = c->anger;
            row.mood = game::moodFromAnger(c->anger);
            row.moodName = std::string(game::economy::moodName(r, s, *c));
            row.out = game::economy::colonyOutput(r, s, *c);
            row.cargoUsed = game::cargoSpaceUsed(r, s, c->cargo);
            row.cargoCapacity = game::colonyCargoCapacity(r, s, *c);
            const game::cmd::QueueTarget target{c->planet, {}};
            row.rate = game::economy::constructionRate(r, s, me, target);
            row.queueLength = static_cast<int>(c->queue.items.size());
            if (!c->queue.items.empty()) {
                const game::QueueItem& top = c->queue.items.front();
                row.building = queueItemName(r, s, top);
                const auto est = estimateQueue(r, s, me, target, c->queue, row.rate);
                const int64_t total = est.front().cost.total();
                row.progress = total > 0 ? float(top.spent.total()) / float(total) : 0.0f;
                row.turns = est.front().turns;
            }
            if (c->queue.onHold) row.queueMode = "On hold";
            else if (c->queue.emergency) row.queueMode = "Emergency";
            else if (c->queue.slowTurns > 0) row.queueMode = std::format("Slow ({})", c->queue.slowTurns);
            else if (c->queue.repeat) row.queueMode = "Repeat";
            row.icons = colonyStatusIcons(r, s, *c, row.out.connected);
            row.orders = planetOrders(ui.session.ordersThisTurn(), c->planet);
            row.breathable = game::breathable(s, *c);
            row.homeworld = c->homeworld;
            rows_.push_back(std::move(row));
        }
        std::erase_if(selection_, [&](game::ObjectId id) { return !s.colony(id) || s.colony(id)->owner != me; });
        revision_ = ui.session.revision();
    }

    void statistics(UiContext& ui) {
        int64_t population = 0, maxPop = 0, research = 0, intel = 0;
        int facilities = 0, slots = 0, yards = 0, idle = 0, unhappy = 0;
        game::Resources production;
        for (const ColonyRow& r : rows_) {
            population += r.population;
            maxPop += r.maxPopulation;
            production += r.out.production;
            research += r.out.research;
            intel += r.out.intelligence;
            facilities += r.facilities;
            slots += r.slots;
            idle += r.queueLength == 0 && r.population > 0;
            unhappy += r.mood >= game::Mood::Unhappy;
            yards += std::find(r.icons.begin(), r.icons.end(), 1) != r.icons.end();
        }
        ImGui::BeginChild("##stats", ImVec2(ImGui::GetContentRegionAvail().x - ui.px(kMapSize.x) - ImGui::GetStyle().ItemSpacing.x,
                                            ui.px(kMapSize.y)));
        ImGui::BeginGroup();
        heading(ui, "Statistics");
        stat(ui, "Colonies", std::to_string(rows_.size()), 96);
        stat(ui, "Population", std::format("{}M / {}M", formatNumber(population), formatNumber(maxPop)), 96);
        stat(ui, "Facilities", std::format("{} / {} slots", facilities, slots), 96);
        stat(ui, "Space yards", std::to_string(yards), 96);
        stat(ui, "Idle queues", std::to_string(idle), 96);
        stat(ui, "Unhappy", std::to_string(unhappy), 96);
        ImGui::EndGroup();
        ImGui::SameLine(ui.px(256));
        ImGui::BeginGroup();
        heading(ui, "Output per turn");
        ImGui::TextColored(kTextLabel, "Production");
        ImGui::SameLine(ui.px(84));
        resources(ui, production, true);
        stat(ui, "Research", formatNumber(research), 84);
        stat(ui, "Intelligence", formatNumber(intel), 84);
        ImGui::Spacing();
        if (selection_.empty()) ImGui::TextColored(kTextDim, "Ctrl/Shift+click selects several.");
        else ImGui::TextColored(kTextDim, "%zu selected (Ctrl/Shift+click)", selection_.size());
        ImGui::EndGroup();
        ImGui::EndChild();
    }

    std::vector<ColonyColumn> columns(UiContext& ui) const {
        const game::GameState& s = ui.state();
        using R = const ColonyRow&;
        auto num = [](int64_t v) { return SortKey{v}; };
        auto text = [](UiContext& u, const std::string& t) { cellText(u, t); };
        std::vector<ColonyColumn> c;
        switch (tab_) {
            case ColonyTab::General:
                c.push_back({"System", 90, [](R r) { return SortKey{r.system}; }, [=](UiContext& u, R r) { cellText(u, r.system, kTextDim); }});
                c.push_back({"Type", 104, [](R r) { return SortKey{r.type}; }, [=](UiContext& u, R r) { text(u, r.type); }});
                c.push_back({"Colony Type", 100, [](R r) { return SortKey{r.colonyType}; }, [=](UiContext& u, R r) { text(u, r.colonyType); }});
                c.push_back({"Population", 118, [=](R r) { return num(r.population); },
                             [=](UiContext& u, R r) {
                                 text(u, std::format("{}M / {}M", formatNumber(r.population), formatNumber(r.maxPopulation)));
                             },
                             true});
                c.push_back({"Mood", 78, [=](R r) { return num(r.anger); },
                             [=](UiContext& u, R r) {
                                 cellText(u, r.moodName, r.mood >= game::Mood::Unhappy ? kTextWarn : ImVec4(1, 1, 1, 1));
                             }});
                c.push_back({"Facil.", 60, [=](R r) { return num(r.facilities); },
                             [=](UiContext& u, R r) { text(u, std::format("{} / {}", r.facilities, r.slots)); }, true});
                break;
            case ColonyTab::Value:
                c.push_back({"Type", 104, [](R r) { return SortKey{r.type}; }, [=](UiContext& u, R r) { text(u, r.type); }});
                c.push_back({"Atmosphere", 84, [&s](R r) { return SortKey{s.galaxy.object(r.planet).atmosphere}; },
                             [&s](UiContext& u, R r) {
                                 cellText(u, s.galaxy.object(r.planet).atmosphere + (r.breathable ? "" : " (dome)"),
                                          r.breathable ? ImVec4(1, 1, 1, 1) : kTextWarn);
                             }});
                c.push_back({"Conditions", 76, [&s](R r) { return SortKey{static_cast<int64_t>(s.galaxy.object(r.planet).conditions.bits)}; },
                             [&s](UiContext& u, R r) { cellText(u, conditionsText(s.galaxy.object(r.planet).conditions)); }, true});
                for (size_t i = 0; i < 3; ++i)
                    c.push_back({i == 0 ? "Minerals" : i == 1 ? "Organics" : "Radioact.", 72,
                                 [&s, i](R r) { return SortKey{int64_t{s.galaxy.object(r.planet).value[i]}}; },
                                 [&s, i](UiContext& u, R r) { cellText(u, percent(s.galaxy.object(r.planet).value[i])); }, true});
                break;
            case ColonyTab::Production:
                for (size_t i = 0; i < 3; ++i)
                    c.push_back({i == 0 ? "Minerals" : i == 1 ? "Organics" : "Radioact.", 76, [i](R r) { return SortKey{r.out.production.v[i]}; },
                                 [i](UiContext& u, R r) { cellText(u, formatNumber(r.out.production.v[i])); }, true});
                c.push_back({"Research", 72, [](R r) { return SortKey{r.out.research}; },
                             [](UiContext& u, R r) { cellText(u, formatNumber(r.out.research)); }, true});
                c.push_back({"Intel", 60, [](R r) { return SortKey{r.out.intelligence}; },
                             [](UiContext& u, R r) { cellText(u, formatNumber(r.out.intelligence)); }, true});
                c.push_back({"Delivery", 104, [](R r) { return SortKey{int64_t{r.out.connected && !r.out.blockaded}}; },
                             [](UiContext& u, R r) {
                                 cellText(u, r.out.blockaded ? "Blockaded" : r.out.connected ? "Delivered" : "No spaceport",
                                          r.out.connected && !r.out.blockaded ? kTextDim : kTextWarn);
                             }});
                break;
            case ColonyTab::Facilities:
                c.push_back({"Used", 64, [](R r) { return SortKey{int64_t{r.facilities}}; },
                             [](UiContext& u, R r) { cellText(u, std::format("{} / {}", r.facilities, r.slots)); }, true});
                c.push_back({"Facilities", 0, [](R r) { return SortKey{int64_t{r.facilities}}; },
                             [&s](UiContext& u, R r) { facilityIcons(u, s, r.planet); }, true});
                break;
            case ColonyTab::Cargo:
                c.push_back({"Space", 110, [](R r) { return SortKey{r.cargoUsed}; },
                             [](UiContext& u, R r) { cellText(u, std::format("{} / {} kT", formatNumber(r.cargoUsed), formatNumber(r.cargoCapacity))); },
                             true});
                c.push_back({"Contents", 0, [&s](R r) { return SortKey{int64_t(s.colony(r.planet) ? s.colony(r.planet)->cargo.units.size() : 0)}; },
                             [&s](UiContext& u, R r) { cellText(u, cargoText(s, r.planet), kTextDim); }, true});
                break;
            case ColonyTab::Construction:
                c.push_back({"Building", 0, [](R r) { return SortKey{r.building}; },
                             [](UiContext& u, R r) { cellText(u, r.building.empty() ? "Nothing" : r.building, r.building.empty() ? kTextWarn : ImVec4(1, 1, 1, 1)); }});
                c.push_back({"Progress", 90, [](R r) { return SortKey{int64_t(r.progress * 1000)}; },
                             [](UiContext& u, R r) {
                                 if (r.queueLength > 0) cellProgress(u, r.progress, std::format("{}%", int(r.progress * 100)));
                             },
                             true});
                c.push_back({"Time", 64, [](R r) { return SortKey{int64_t{r.turns < 0 ? 1 << 30 : r.turns}}; },
                             [](UiContext& u, R r) { cellText(u, r.queueLength > 0 ? turnsText(r.turns) : "", kTextDim); }});
                c.push_back({"Items", 46, [](R r) { return SortKey{int64_t{r.queueLength}}; },
                             [](UiContext& u, R r) { cellText(u, std::to_string(r.queueLength)); }, true});
                c.push_back({"Mode", 84, [](R r) { return SortKey{r.queueMode}; }, [](UiContext& u, R r) { cellText(u, r.queueMode, kTextDim); }});
                break;
            case ColonyTab::Status:
                c.push_back({"Status", 0, [](R r) { return SortKey{int64_t(r.icons.size())}; },
                             [](UiContext& u, R r) { statusIconRow(u, r.icons); }, true});
                c.push_back({"Mood", 90, [](R r) { return SortKey{int64_t{r.anger}}; },
                             [](UiContext& u, R r) {
                                 cellText(u, r.moodName, r.mood >= game::Mood::Unhappy ? kTextWarn : ImVec4(1, 1, 1, 1));
                             }});
                c.push_back({"Anger", 60, [](R r) { return SortKey{int64_t{r.anger}}; },
                             [](UiContext& u, R r) { cellText(u, std::format("{}%", r.anger), kTextDim); }, true});
                break;
            case ColonyTab::Races:
                c.push_back({"Races", 0, [&s](R r) { return SortKey{int64_t(s.colony(r.planet) ? s.colony(r.planet)->population.size() : 0)}; },
                             [&s](UiContext& u, R r) { raceList(u, s, r.planet); }, true});
                break;
            case ColonyTab::Orders:
                c.push_back({"Orders given this turn", 0, [](R r) { return SortKey{int64_t(r.orders.size())}; },
                             [](UiContext& u, R r) {
                                 std::string t;
                                 for (const auto& o : r.orders) t += (t.empty() ? "" : ", ") + o;
                                 cellText(u, t.empty() ? "None" : t, t.empty() ? kTextDim : ImVec4(1, 1, 1, 1));
                             },
                             true});
                break;
            case ColonyTab::Count: break;
        }
        return c;
    }

    static void facilityIcons(UiContext& ui, const game::GameState& s, game::ObjectId planet) {
        const game::Colony* c = s.colony(planet);
        if (!c) return;
        // One icon per facility type, with a count when there are several.
        std::vector<std::pair<uint32_t, int>> types;
        for (uint32_t f : c->facilities) {
            auto it = std::find_if(types.begin(), types.end(), [&](const auto& t) { return t.first == f; });
            if (it == types.end()) types.emplace_back(f, 1);
            else ++it->second;
        }
        const float y = ImGui::GetCursorPosY();
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) ImGui::SameLine(0, ui.px(6));
            ImGui::SetCursorPosY(y);
            cellImage(ui, ui.art.facility(ui.rules().facility(types[i].first).picture), 20);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s x%d", ui.rules().facility(types[i].first).name.c_str(), types[i].second);
            if (types[i].second > 1) {
                ImGui::SameLine(0, ui.px(1));
                ImGui::SetCursorPosY(y);
                cellText(ui, std::format("x{}", types[i].second), kTextDim);
            }
        }
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

    static void raceList(UiContext& ui, const game::GameState& s, game::ObjectId planet) {
        const game::Colony* c = s.colony(planet);
        if (!c) return;
        const float y = ImGui::GetCursorPosY();
        for (size_t i = 0; i < c->population.size(); ++i) {
            const game::PopulationGroup& p = c->population[i];
            if (i > 0) ImGui::SameLine(0, ui.px(14));
            ImGui::SetCursorPosY(y);
            cellImage(ui, ui.art.populationMini(p.race.valid() ? s.empire(p.race).race.style : ""), 20);
            ImGui::SameLine(0, ui.px(4));
            ImGui::SetCursorPosY(y);
            cellText(ui, std::format("{} {}M", p.race.valid() ? s.empire(p.race).race.name : "Unknown", formatNumber(p.millions)));
        }
    }

    void table(UiContext& ui) {
        const game::GameState& s = ui.state();
        const std::vector<ColonyColumn> cols = columns(ui);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(4), ui.px(2)));
        const float footer = ImGui::GetTextLineHeightWithSpacing();
        const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        const bool nameStretches = std::none_of(cols.begin(), cols.end(), [](const ColonyColumn& c) { return c.width == 0; });
        if (ImGui::BeginTable(tableId_.c_str(), static_cast<int>(cols.size()) + 2, flags, ImVec2(0, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort, ui.px(28), 0);
            ImGui::TableSetupColumn("Name", (nameStretches ? ImGuiTableColumnFlags_WidthStretch : ImGuiTableColumnFlags_WidthFixed) |
                                               ImGuiTableColumnFlags_DefaultSort,
                                    nameStretches ? 0.0f : ui.px(150), 1);
            for (size_t i = 0; i < cols.size(); ++i)
                ImGui::TableSetupColumn(cols[i].name,
                                        (cols[i].width == 0 ? ImGuiTableColumnFlags_WidthStretch : ImGuiTableColumnFlags_WidthFixed) |
                                            (cols[i].descendingFirst ? ImGuiTableColumnFlags_PreferSortDescending : 0),
                                        cols[i].width == 0 ? 0.0f : ui.px(cols[i].width), static_cast<ImGuiID>(i + 2));
            ImGui::TableHeadersRow();
            readSortSpecs(sortColumn_, ascending_);
            std::vector<const ColonyRow*> rows;
            for (const ColonyRow& r : rows_) rows.push_back(&r);
            auto key = [&](const ColonyRow& r) -> SortKey {
                if (sortColumn_ >= 2 && static_cast<size_t>(sortColumn_ - 2) < cols.size()) return cols[static_cast<size_t>(sortColumn_ - 2)].key(r);
                return r.name;
            };
            std::stable_sort(rows.begin(), rows.end(), [&](const ColonyRow* a, const ColonyRow* b) {
                return ascending_ ? sortKeyLess(key(*a), key(*b)) : sortKeyLess(key(*b), key(*a));
            });
            for (const ColonyRow* r : rows) {
                const RowEvents ev = tableRow(ui, static_cast<int>(r->planet.index()), contains(selection_, r->planet));
                if (ev.hovered) hovered_ = r->planet;
                if (ev.clicked) clickSelect(selection_, r->planet);
                if (ev.rightClicked) report_.openPlanet(r->planet);
                if (ev.doubleClicked) goto_ = r->planet;
                cellImage(ui, objectSprite(ui, s.galaxy.object(r->planet)), 22);
                ImGui::TableSetColumnIndex(1);
                cellText(ui, r->homeworld ? r->name + " (home)" : r->name, r->homeworld ? kTextHighlight : ImVec4(1, 1, 1, 1));
                for (size_t i = 0; i < cols.size(); ++i) {
                    ImGui::TableSetColumnIndex(static_cast<int>(i) + 2);
                    cols[i].draw(ui, *r);
                }
            }
            if (rows_.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kTextDim, "No colonies.");
            }
            ImGui::EndTable();
            ui.tagItem("colonies:list");
        }
        ImGui::PopStyleVar();
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
        ImGui::BeginChild("##types", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < types.size(); ++i)
            if (ImGui::Selectable(types[i].c_str(), chosenType_ == static_cast<int>(i), ImGuiSelectableFlags_AllowDoubleClick))
                chosenType_ = static_cast<int>(i);
        if (types.empty()) ImGui::TextColored(kTextDim, "No colony types are defined.");
        ImGui::EndChild();
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
    int sortColumn_ = 1;
    bool ascending_ = true;
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
