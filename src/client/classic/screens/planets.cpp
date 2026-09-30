// Planets (F4) and Colonies (F5) windows (docs/spec/06 §1.2, spec 02 §11).

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

// Picture with the colonization hint of the system panel: a green star when
// colonizable and breathable, red when a colony would be domed.
void planetPicture(UiContext& ui, const game::SpaceObject& o, bool colonizable, bool breathable) {
    cellImage(ui, objectSprite(ui, o), 22);
    if (!colonizable) return;
    const ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
    const float s = ui.px(8);
    if (Sprite star = ui.art.region("Pictures/Game/General.bmp", breathable ? 237 : 261, 16, 7, 7))
        ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(star.tex.value)), ImVec2(mx.x - s + ui.px(3), mn.y - ui.px(1)),
                                             ImVec2(mx.x + ui.px(3), mn.y - ui.px(1) + s), ImVec2(star.uv.min.x, star.uv.min.y),
                                             ImVec2(star.uv.max.x, star.uv.max.y));
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
// Planets
// ============================================================================================

struct FilterButton {
    PlanetFilter filter;
    const char* label;
    const char* tooltip;
};
constexpr std::array<FilterButton, 10> kFilters{{
    {PlanetFilter::All, "All", "Every planet and asteroid field in the systems we have explored"},
    {PlanetFilter::Colonizable, "Colonizable", "Free planets we have a colony module for"},
    {PlanetFilter::AllColonies, "All Colonies", "Planets anyone has colonized, ours included"},
    {PlanetFilter::EnemyColonies, "Enemy Colonies", "Colonies of empires we fight on contact"},
    {PlanetFilter::AllyColonies, "Ally Colonies", "Colonies of empires with a treaty of Non-Aggression or better"},
    {PlanetFilter::ColonizableEmpty, "Coloniz\\Empty", "Colonizable planets in systems no other empire has settled"},
    {PlanetFilter::ColonizableBreathable, "Coloniz\\Breathe", "Colonizable planets whose atmosphere our race breathes (no domes)"},
    {PlanetFilter::ShipEnroute, "Ship Enroute", "Planets one of our ships has orders to colonize"},
    {PlanetFilter::Asteroids, "Asteroids", "Asteroid fields"},
    {PlanetFilter::Special, "Special", "Planets with special features such as ruins or rich deposits"},
}};

enum PlanetColumn { PcPicture, PcName, PcSystem, PcType, PcAtmosphere, PcConditions, PcMinerals, PcOrganics, PcRadioactives, PcStatus };

class PlanetsScreen final : public Screen {
public:
    explicit PlanetsScreen(const ScreenArgs& args) {
        if (args.planet.valid()) selected_ = args.planet;
    }

    bool draw(UiContext& ui) override {
        if (revision_ != ui.session.revision()) refresh(ui);
        bool keep = true;
        {
            Dialog d(ui, "Planets", DialogSize::Tall);
            if (!d.open()) return d.keepOpen();
            const game::GameState& s = ui.state();

            std::vector<const PlanetInfo*> rows;
            for (const PlanetInfo& p : all_)
                if (matches(filter_, p) && !(noAvoid_ && p.avoided)) rows.push_back(&p);
            const PlanetInfo* selected = nullptr;
            for (const PlanetInfo& p : all_)
                if (selected_ && p.id == *selected_) selected = &p;

            d.beginContent();
            statistics(ui, selected);
            ImGui::SameLine();
            std::vector<uint8_t> marked(s.galaxy.systems.size(), 0);
            for (const PlanetInfo* p : rows) marked[p->system.index()] = 1;
            std::optional<game::SystemId> highlight;
            if (hovered_) highlight = s.galaxy.object(*hovered_).system;
            else if (selected) highlight = selected->system;
            quadrantMap(ui, "##map", kMapSize, marked, highlight);

            hovered_.reset();
            table(ui, rows);
            status_.draw(ui);

            d.beginButtons();
            for (const FilterButton& f : kFilters)
                if (lampButton(d, ui, f.label, filter_ == f.filter, true, f.tooltip)) filter_ = f.filter;
            d.spacer();
            if (lampButton(d, ui, "No Sys To Avoid", noAvoid_, true, "Hide planets in the systems on our Systems To Avoid list"))
                noAvoid_ = !noAvoid_;
            d.spacer();
            const bool canSend = selected && selected->colonizable && !selected->enroute;
            if (d.button("Send Colony Ship", canSend)) sendColonyShip(ui, *selected);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                itemTooltip(!selected                 ? "Select a planet first"
                            : selected->enroute       ? "A ship is already on its way"
                            : !selected->colonizable ? selected->problem.c_str()
                                                      : "Order the nearest idle colony ship to colonize the selected planet");
            if (d.button("Goto", selected != nullptr)) goto_ = selected->id;
            if (d.close()) return false;
            report_.draw(ui);
            keep = d.keepOpen();
        }
        if (goto_) {
            ui.requests.selectPlanet = *goto_;
            return false;
        }
        return keep;
    }

private:
    void refresh(UiContext& ui) {
        all_ = surveyPlanets(ui.rules(), ui.state(), ui.session.player());
        revision_ = ui.session.revision();
    }

    void statistics(UiContext& ui, const PlanetInfo* selected) {
        int planets = 0, asteroids = 0, colonizable = 0, breathe = 0, own = 0, ally = 0, enemy = 0, enroute = 0, systems = 0;
        for (const PlanetInfo& p : all_) {
            (p.asteroids ? asteroids : planets)++;
            colonizable += p.colonizable;
            breathe += p.colonizable && p.breathable;
            own += p.own;
            ally += p.ally;
            enemy += p.enemy;
            enroute += p.enroute;
        }
        for (const game::StarSystem& sys : ui.state().galaxy.systems) systems += ui.me().hasExplored(sys.id);
        const float h = ui.px(kMapSize.y);
        ImGui::BeginChild("##stats", ImVec2(ImGui::GetContentRegionAvail().x - ui.px(kMapSize.x) - ImGui::GetStyle().ItemSpacing.x, h));
        ImGui::BeginGroup();
        heading(ui, "Statistics");
        stat(ui, "Explored systems", std::to_string(systems));
        stat(ui, "Planets", std::format("{} (+{} asteroid fields)", planets, asteroids));
        stat(ui, "Colonizable", std::format("{} ({} breathable)", colonizable, breathe));
        stat(ui, "Our colonies", std::to_string(own));
        stat(ui, "Ally / enemy", std::format("{} / {}", ally, enemy));
        stat(ui, "Ships en route", std::to_string(enroute));
        ImGui::EndGroup();
        ImGui::SameLine(ui.px(300));
        ImGui::BeginGroup();
        if (selected) {
            const game::GameState& s = ui.state();
            const game::SpaceObject& o = s.galaxy.object(selected->id);
            heading(ui, o.name.c_str());
            stat(ui, "Type", surfaceAndSize(o), 90);
            stat(ui, "Atmosphere", o.atmosphere, 90);
            stat(ui, "Conditions", conditionsText(o.conditions), 90);
            stat(ui, "Value", std::format("{}% / {}% / {}%", o.value[0], o.value[1], o.value[2]), 90);
            if (selected->colonized) {
                ImGui::TextColored(kTextLabel, "Owner");
                ImGui::SameLine(ui.px(90));
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(s, selected->owner)), "%s", s.empire(selected->owner).name.c_str());
            } else if (selected->colonizable) {
                ImGui::TextColored(kTextGood, "%s", selected->breathable ? "Colonizable" : "Colonizable (domed)");
            } else {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(kTextDim, "%s", selected->problem.c_str());
                ImGui::PopTextWrapPos();
            }
        } else {
            ImGui::TextColored(kTextDim, "Click a planet to select it.");
            ImGui::TextColored(kTextDim, "Double-click (or Goto) shows it");
            ImGui::TextColored(kTextDim, "in the main window; right-click");
            ImGui::TextColored(kTextDim, "opens its report.");
        }
        ImGui::EndGroup();
        ImGui::EndChild();
    }

    void table(UiContext& ui, std::vector<const PlanetInfo*>& rows) {
        const game::GameState& s = ui.state();
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(4), ui.px(2)));
        const float footer = ImGui::GetTextLineHeightWithSpacing();
        const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##planets", 10, flags, ImVec2(0, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort, ui.px(28), PcPicture);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0, PcName);
            ImGui::TableSetupColumn("System", 0, ui.px(76), PcSystem);
            ImGui::TableSetupColumn("Type", 0, ui.px(96), PcType);
            ImGui::TableSetupColumn("Atmosphere", 0, ui.px(92), PcAtmosphere);
            ImGui::TableSetupColumn("Cond.", ImGuiTableColumnFlags_PreferSortDescending, ui.px(44), PcConditions);
            ImGui::TableSetupColumn("Min.", ImGuiTableColumnFlags_PreferSortDescending, ui.px(40), PcMinerals);
            ImGui::TableSetupColumn("Org.", ImGuiTableColumnFlags_PreferSortDescending, ui.px(40), PcOrganics);
            ImGui::TableSetupColumn("Rad.", ImGuiTableColumnFlags_PreferSortDescending, ui.px(40), PcRadioactives);
            ImGui::TableSetupColumn("Status", 0, ui.px(104), PcStatus);
            ImGui::TableHeadersRow();
            readSortSpecs(sortColumn_, ascending_);
            auto key = [&](const PlanetInfo& p) -> SortKey {
                const game::SpaceObject& o = s.galaxy.object(p.id);
                switch (sortColumn_) {
                    case PcSystem: return s.galaxy.system(p.system).name;
                    case PcType: return surfaceAndSize(o);
                    case PcAtmosphere: return o.atmosphere;
                    case PcConditions: return static_cast<int64_t>(o.conditions.bits);  // ordered like the values (never negative)
                    case PcMinerals: return int64_t{o.value[0]};
                    case PcOrganics: return int64_t{o.value[1]};
                    case PcRadioactives: return int64_t{o.value[2]};
                    case PcStatus: return statusText(ui, p);
                    default: return o.name;
                }
            };
            std::stable_sort(rows.begin(), rows.end(), [&](const PlanetInfo* a, const PlanetInfo* b) {
                return ascending_ ? sortKeyLess(key(*a), key(*b)) : sortKeyLess(key(*b), key(*a));
            });
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()), ui.px(kRowHeight));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) row(ui, *rows[static_cast<size_t>(i)]);
            if (rows.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kTextDim, "No planets match this filter.");
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    std::string statusText(UiContext& ui, const PlanetInfo& p) const {
        if (p.colonized) return p.own ? "Ours" : ui.state().empire(p.owner).name;
        if (p.enroute) return "Ship en route";
        if (p.colonizable) return p.breathable ? "Colonizable" : "Needs a dome";
        return "-";
    }

    void row(UiContext& ui, const PlanetInfo& p) {
        const game::GameState& s = ui.state();
        const game::SpaceObject& o = s.galaxy.object(p.id);
        const RowEvents ev = tableRow(ui, static_cast<int>(p.id.index()), selected_ == p.id);
        if (ev.hovered) hovered_ = p.id;
        if (ev.clicked) selected_ = p.id;
        if (ev.rightClicked) report_.openPlanet(p.id);
        if (ev.doubleClicked) goto_ = p.id;
        planetPicture(ui, o, p.colonizable, p.breathable);
        ImGui::TableSetColumnIndex(1);
        cellText(ui, o.name, p.own ? kTextHighlight : ImVec4(1, 1, 1, 1));
        ImGui::TableSetColumnIndex(2);
        cellText(ui, s.galaxy.system(p.system).name, kTextDim);
        ImGui::TableSetColumnIndex(3);
        cellText(ui, surfaceAndSize(o));
        ImGui::TableSetColumnIndex(4);
        cellText(ui, p.asteroids ? "-" : o.atmosphere, p.breathable ? kTextGood : ImVec4(1, 1, 1, 1));
        ImGui::TableSetColumnIndex(5);
        cellText(ui, conditionsText(o.conditions));
        for (int r = 0; r < 3; ++r) {
            ImGui::TableSetColumnIndex(6 + r);
            cellText(ui, percent(o.value[static_cast<size_t>(r)]));
        }
        ImGui::TableSetColumnIndex(9);
        const std::string st = statusText(ui, p);
        if (p.colonized && !p.own) cellText(ui, st, ImGui::ColorConvertU32ToFloat4(empireColor(s, p.owner)));
        else cellText(ui, st, p.own ? kTextHighlight : p.colonizable || p.enroute ? kTextGood : kTextDim);
        if (!p.colonizable && !p.colonized && ImGui::IsItemHovered()) itemTooltip(p.problem.c_str());
    }

    void sendColonyShip(UiContext& ui, const PlanetInfo& p) {
        const game::GameState& s = ui.state();
        const std::string& name = s.galaxy.object(p.id).name;
        const auto ship = findColonyShip(ui.rules(), s, ui.session.player(), p.id);
        if (!ship) {
            status_.fail(std::format("No idle ship outside a fleet can colonize {} ({} planets need a matching colony module)", name,
                                     s.galaxy.object(p.id).surface));
            return;
        }
        const std::string shipName = s.vehicle(*ship)->name;
        if (status_.issue(ui, colonizeOrders(s, *ship, p.id))) status_.info(std::format("{} is on its way to colonize {}", shipName, name));
    }

    std::vector<PlanetInfo> all_;
    uint64_t revision_ = 0;
    PlanetFilter filter_ = PlanetFilter::All;
    bool noAvoid_ = false;
    std::optional<game::ObjectId> selected_;
    std::optional<game::ObjectId> hovered_;
    std::optional<game::ObjectId> goto_;
    int sortColumn_ = PcName;
    bool ascending_ = true;
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
