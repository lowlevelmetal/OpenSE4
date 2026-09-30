// Construction Queues (F7) and Set Construction Queue (Q) windows
// (docs/spec/06 §1.2, spec 02 §6 and §11).

#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/screens.hpp"

#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <sstream>

namespace opense4::client::classic {

namespace {

constexpr Vec2 kMapSize{236, 158};

bool hasTarget(const std::vector<game::cmd::QueueTarget>& list, const game::cmd::QueueTarget& t) {
    return std::any_of(list.begin(), list.end(), [&](const auto& x) { return sameTarget(x, t); });
}

int targetId(const game::cmd::QueueTarget& t) {
    return t.vehicle.valid() ? static_cast<int>(t.vehicle.value) * 2 + 1 : static_cast<int>(t.planet.value) * 2;
}

std::string kindLabel(QueueKind k) {
    switch (k) {
        case QueueKind::Planet: return "Planet";
        case QueueKind::PlanetYard: return "Planet, space yard";
        case QueueKind::Ship: return "Ship, space yard";
        case QueueKind::Base: return "Base, space yard";
    }
    return {};
}

// "On hold, Emergency, Repeat"; `detailed` adds the emergency and slow-mode turn counts.
std::string modeText(const game::ConstructionQueue& q, bool detailed = false) {
    std::string t;
    auto add = [&](const std::string& part) { t += (t.empty() ? "" : ", ") + part; };
    if (q.onHold) add("On hold");
    if (q.emergency) add(detailed ? std::format("Emergency ({} of 10 turns used)", q.emergencyTurns) : "Emergency");
    else if (q.slowTurns > 0) add(detailed ? std::format("Slow ({} turns left)", q.slowTurns) : std::format("Slow ({})", q.slowTurns));
    if (q.repeat) add("Repeat");
    return t;
}

Sprite targetSprite(UiContext& ui, const game::cmd::QueueTarget& t) {
    if (t.vehicle.valid()) {
        const game::Vehicle* v = ui.state().vehicle(t.vehicle);
        return v ? vehicleMini(ui, *v) : Sprite{};
    }
    return objectSprite(ui, ui.state().galaxy.object(t.planet));
}

// Opens the planet or ship report for a queue owner.
void openReport(ReportPopup& report, const game::cmd::QueueTarget& t) {
    if (t.vehicle.valid()) report.openVehicle(t.vehicle);
    else report.openPlanet(t.planet);
}

// ============================================================================================
// Construction Queues
// ============================================================================================

enum class QueueTab { Rate, Usage, PlanetValue, Facilities, Cargo };
constexpr std::array<std::pair<QueueTab, const char*>, 5> kQueueTabs{
    {{QueueTab::Rate, "Rate"}, {QueueTab::Usage, "Usage"}, {QueueTab::PlanetValue, "Planet Value"}, {QueueTab::Facilities, "Facilities"},
     {QueueTab::Cargo, "Cargo"}}};

struct QueueRow {
    QueueEntry entry;
    std::string system;
    game::Resources rate, usage;
    std::string building;
    float progress = 0;
    int topTurns = -1;
    int allTurns = -1;
    int items = 0;
    std::string mode;
    bool onHold = false;
    bool canBuild = true;         // a colony with population, or a working yard ship
    int facilities = 0, slots = 0, freeSlots = 0;  // free: after what is queued
    int64_t cargoUsed = 0, cargoCapacity = 0;
};

enum QueueColumn { QcPicture, QcName, QcSystem, QcBuilding, QcTime, QcTab1, QcTab2, QcTab3, QcTab4 };

class QueuesScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        if (revision_ != ui.session.revision()) refresh(ui);
        Dialog d(ui, "Construction Queues", DialogSize::Tall);
        if (!d.open()) return d.keepOpen();
        std::vector<const QueueRow*> rows;
        for (const QueueRow& r : rows_)
            if (included(r.entry.kind)) rows.push_back(&r);

        d.beginContent();
        statistics(ui, rows);
        ImGui::SameLine();
        std::vector<uint8_t> marked(ui.state().galaxy.systems.size(), 0);
        for (const QueueRow* r : rows) marked[r->entry.where.system.index()] = 1;
        std::optional<game::SystemId> highlight;
        if (hovered_) highlight = *hovered_;
        quadrantMap(ui, "##map", kMapSize, marked, highlight);
        hovered_.reset();
        table(ui, rows);
        status_.draw(ui);

        d.beginButtons();
        for (const auto& [tab, label] : kQueueTabs)
            if (lampButton(d, ui, label, tab_ == tab)) tab_ = tab;
        d.spacer();
        // Inclusion toggles (inferred meanings; docs/spec/06 §7).
        if (lampButton(d, ui, "Ships", showShips_, true, "Show mobile ships with a space yard")) showShips_ = !showShips_;
        if (lampButton(d, ui, "Planets", showPlanets_, true, "Show colonies without a space yard (facilities and units only)"))
            showPlanets_ = !showPlanets_;
        if (lampButton(d, ui, "Ship SY", showBases_, true, "Show bases and other stationary vehicles with a space yard")) showBases_ = !showBases_;
        if (lampButton(d, ui, "Planet SY", showPlanetYards_, true, "Show colonies with a space yard facility")) showPlanetYards_ = !showPlanetYards_;
        d.spacer();
        if (d.button("Multi-Add", !selection_.empty())) {
            multiAddPending_ = true;
            multiItem_.reset();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            itemTooltip(selection_.empty() ? "Ctrl or Shift+click queues to select them first"
                                           : "Add one design or facility to every selected queue");
        const std::vector<game::ObjectId> planets = selectedPlanets();
        if (d.button("Scrap Facilities", !planets.empty())) {
            if (planets.size() == 1) scrapOne_.open(planets.front());
            else scrapTypes_.open(planets);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            itemTooltip(planets.empty() ? "Select one or more planet queues first"
                                        : planets.size() == 1 ? "Pick facilities to scrap on the selected planet"
                                                              : "Scrap one facility type on the selected planets");
        if (d.button("Upgrade Facilities")) upgradeAll(ui);
        if (ImGui::IsItemHovered())
            itemTooltip(planets.empty() ? "Queue every possible facility upgrade on all colonies"
                                        : "Queue every possible facility upgrade on the selected colonies");
        if (d.button(selection_.empty() ? "Select All" : "Select None")) {
            if (selection_.empty())
                for (const QueueRow* r : rows) selection_.push_back(r->entry.target);
            else selection_.clear();
        }
        if (d.close()) return false;
        report_.draw(ui);
        scrapOne_.draw(ui, status_);
        scrapTypes_.draw(ui, status_);
        multiAddPopup(ui);
        return d.keepOpen();
    }

private:
    bool included(QueueKind k) const {
        switch (k) {
            case QueueKind::Planet: return showPlanets_;
            case QueueKind::PlanetYard: return showPlanetYards_;
            case QueueKind::Ship: return showShips_;
            case QueueKind::Base: return showBases_;
        }
        return true;
    }

    std::vector<game::ObjectId> selectedPlanets() const {
        std::vector<game::ObjectId> out;
        for (const auto& t : selection_)
            if (!t.vehicle.valid()) out.push_back(t.planet);
        return out;
    }

    void refresh(UiContext& ui) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        rows_.clear();
        for (QueueEntry& e : empireQueues(r, s, me)) {
            const game::ConstructionQueue* q = queueOf(s, me, e.target);
            if (!q) continue;
            QueueRow row;
            row.system = s.galaxy.system(e.where.system).name;
            row.rate = game::economy::constructionRate(r, s, me, e.target);
            row.usage = queueUsage(r, s, me, e.target, *q, row.rate);
            row.items = static_cast<int>(q->items.size());
            row.mode = modeText(*q);
            row.onHold = q->onHold;
            if (!q->items.empty()) {
                const auto est = estimateQueue(r, s, me, e.target, *q, row.rate);
                row.building = queueItemName(r, s, q->items.front());
                const int64_t total = est.front().cost.total();
                row.progress = total > 0 ? float(q->items.front().spent.total()) / float(total) : 0.0f;
                row.topTurns = est.front().turns;
                row.allTurns = est.back().doneIn;
            }
            if (const game::Colony* c = e.target.vehicle.valid() ? nullptr : s.colony(e.target.planet)) {
                row.canBuild = c->totalPopulation() > 0;
                row.facilities = static_cast<int>(c->facilities.size());
                row.slots = game::facilitySlots(r, s, *c);
                row.freeSlots = freeFacilitySlots(r, s, *c);
                row.cargoUsed = game::cargoSpaceUsed(r, s, c->cargo);
                row.cargoCapacity = game::colonyCargoCapacity(r, s, *c);
            } else if (const game::Vehicle* v = s.vehicle(e.target.vehicle)) {
                row.canBuild = v->status != game::VehicleStatus::Cloaked;
                row.cargoUsed = game::cargoSpaceUsed(r, s, v->cargo);
                row.cargoCapacity = game::vehicleCargoCapacity(r, s, *v);
            }
            row.entry = std::move(e);
            rows_.push_back(std::move(row));
        }
        std::erase_if(selection_, [&](const auto& t) { return !queueOf(s, me, t); });
        revision_ = ui.session.revision();
    }

    void statistics(UiContext& ui, const std::vector<const QueueRow*>& rows) {
        int building = 0, idle = 0, hold = 0;
        game::Resources rate, usage;
        for (const QueueRow* r : rows) {
            building += r->items > 0 && !r->onHold;
            idle += r->items == 0 && r->canBuild;
            hold += r->onHold;
            rate += r->rate;
            usage += r->usage;
        }
        ImGui::BeginChild("##stats", ImVec2(ImGui::GetContentRegionAvail().x - ui.px(kMapSize.x) - ImGui::GetStyle().ItemSpacing.x,
                                            ui.px(kMapSize.y)));
        ImGui::BeginGroup();
        heading(ui, "Statistics");
        labelValue(ui, "Queues", std::format("{} of {}", rows.size(), rows_.size()), 80);
        labelValue(ui, "Building", std::to_string(building), 80);
        labelValue(ui, "Idle", std::to_string(idle), 80);
        labelValue(ui, "On hold", std::to_string(hold), 80);
        ImGui::EndGroup();
        ImGui::SameLine(ui.px(200));
        ImGui::BeginGroup();
        heading(ui, "Resources per turn");
        ImGui::TextColored(kTextLabel, "Total rate");
        ImGui::SameLine(ui.px(96));
        resources(ui, rate, true);
        ImGui::TextColored(kTextLabel, "Usage");
        ImGui::SameLine(ui.px(96));
        resources(ui, usage, true);
        ImGui::TextColored(kTextLabel, "Stockpile");
        ImGui::SameLine(ui.px(96));
        resources(ui, ui.me().stockpile, true);
        ImGui::EndGroup();
        ImGui::Spacing();
        if (!selection_.empty()) ImGui::TextColored(kTextDim, "%zu queues selected (for Multi-Add, Scrap and Upgrade).", selection_.size());
        else ImGui::TextColored(kTextDim, "Click a queue to edit it; Ctrl/Shift+click selects several.");
        ImGui::EndChild();
    }

    void table(UiContext& ui, std::vector<const QueueRow*>& rows) {
        const game::GameState& s = ui.state();
        struct Col {
            const char* name;
            float width;
            bool descending;
        };
        std::vector<Col> extra;
        switch (tab_) {
            case QueueTab::Rate: extra = {{"Rate", 170, true}, {"Mode", 130, false}}; break;
            case QueueTab::Usage: extra = {{"Usage", 170, true}, {"Of Rate", 70, true}}; break;
            case QueueTab::PlanetValue: extra = {{"Cond.", 50, true}, {"Min.", 50, true}, {"Org.", 50, true}, {"Rad.", 50, true}}; break;
            case QueueTab::Facilities: extra = {{"Facilities", 90, true}, {"Free", 60, true}}; break;
            case QueueTab::Cargo: extra = {{"Cargo", 130, true}, {"Free", 80, true}}; break;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(4), ui.px(2)));
        const float footer = ImGui::GetTextLineHeightWithSpacing();
        const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        const std::string id = std::format("##queues{}", static_cast<int>(tab_));
        if (ImGui::BeginTable(id.c_str(), 5 + static_cast<int>(extra.size()), flags, ImVec2(0, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort, ui.px(28), QcPicture);
            ImGui::TableSetupColumn("Queue", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0, QcName);
            ImGui::TableSetupColumn("System", 0, ui.px(84), QcSystem);
            ImGui::TableSetupColumn("Building", 0, ui.px(150), QcBuilding);
            ImGui::TableSetupColumn("Time", 0, ui.px(62), QcTime);
            for (size_t i = 0; i < extra.size(); ++i)
                ImGui::TableSetupColumn(extra[i].name, extra[i].descending ? ImGuiTableColumnFlags_PreferSortDescending : 0, ui.px(extra[i].width),
                                        static_cast<ImGuiID>(QcTab1 + static_cast<int>(i)));
            ImGui::TableHeadersRow();
            readSortSpecs(sortColumn_, ascending_);
            auto value = [&](const QueueRow& r, size_t i) -> int64_t {
                const game::SpaceObject* o = r.entry.target.vehicle.valid() ? nullptr : &s.galaxy.object(r.entry.target.planet);
                switch (tab_) {
                    case QueueTab::Rate: return i == 0 ? r.rate.total() : 0;
                    case QueueTab::Usage: return i == 0 ? r.usage.total() : r.rate.total() > 0 ? r.usage.total() * 100 / r.rate.total() : 0;
                    case QueueTab::PlanetValue: return !o ? -1 : i == 0 ? o->conditions.inHundredths() : o->value[i - 1];
                    case QueueTab::Facilities: return i == 0 ? r.facilities : r.freeSlots;
                    case QueueTab::Cargo: return i == 0 ? r.cargoUsed : r.cargoCapacity - r.cargoUsed;
                }
                return 0;
            };
            auto key = [&](const QueueRow& r) -> SortKey {
                switch (sortColumn_) {
                    case QcSystem: return r.system;
                    case QcBuilding: return r.building;
                    case QcTime: return int64_t{r.topTurns < 0 ? 1 << 30 : r.topTurns};
                    case QcName: return r.entry.name;
                    default:
                        if (tab_ == QueueTab::Rate && sortColumn_ == QcTab2) return r.mode;
                        return value(r, static_cast<size_t>(sortColumn_ - QcTab1));
                }
            };
            std::stable_sort(rows.begin(), rows.end(), [&](const QueueRow* a, const QueueRow* b) {
                return ascending_ ? sortKeyLess(key(*a), key(*b)) : sortKeyLess(key(*b), key(*a));
            });
            for (const QueueRow* r : rows) row(ui, *r, value);
            if (rows.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kTextDim, "No queues of the included kinds.");
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    template <class Value>
    void row(UiContext& ui, const QueueRow& r, Value&& value) {
        const game::cmd::QueueTarget& t = r.entry.target;
        const RowEvents ev = tableRow(ui, targetId(t), hasTarget(selection_, t));
        if (ev.hovered) hovered_ = r.entry.where.system;
        if (ev.rightClicked) openReport(report_, t);
        if (ev.clicked) {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl || io.KeyShift) {
                if (hasTarget(selection_, t)) std::erase_if(selection_, [&](const auto& x) { return sameTarget(x, t); });
                else selection_.push_back(t);
            } else {
                ScreenArgs a;
                if (t.vehicle.valid()) a.vehicle = t.vehicle;
                else a.planet = t.planet;
                ui.open(ScreenId::SetQueue, a);
            }
        }
        cellImage(ui, targetSprite(ui, t), 22);
        ImGui::TableSetColumnIndex(1);
        cellText(ui, r.entry.name, t.vehicle.valid() ? ImVec4(0.75f, 0.85f, 1.0f, 1) : ImVec4(1, 1, 1, 1));
        if (ImGui::IsItemHovered()) itemTooltip(kindLabel(r.entry.kind).c_str());
        ImGui::TableSetColumnIndex(2);
        cellText(ui, r.system, kTextDim);
        ImGui::TableSetColumnIndex(3);
        if (!r.canBuild) cellText(ui, "Cannot build", kTextWarn);
        else if (r.items == 0) cellText(ui, "Idle", kTextWarn);
        else cellText(ui, r.items > 1 ? std::format("{} (+{})", r.building, r.items - 1) : r.building);
        ImGui::TableSetColumnIndex(4);
        if (r.items > 0) cellText(ui, turnsText(r.topTurns), kTextDim);
        if (r.items > 1 && ImGui::IsItemHovered()) ImGui::SetTooltip("Whole queue: %s", turnsText(r.allTurns).c_str());
        const game::SpaceObject* o = t.vehicle.valid() ? nullptr : &ui.state().galaxy.object(t.planet);
        switch (tab_) {
            case QueueTab::Rate:
                ImGui::TableSetColumnIndex(5);
                cellResources(ui, r.rate);
                ImGui::TableSetColumnIndex(6);
                cellText(ui, r.mode, kTextDim);
                break;
            case QueueTab::Usage:
                ImGui::TableSetColumnIndex(5);
                cellResources(ui, r.usage);
                ImGui::TableSetColumnIndex(6);
                cellText(ui, r.rate.total() > 0 ? std::format("{}%", value(r, 1)) : "-", kTextDim);
                break;
            case QueueTab::PlanetValue:
                for (size_t i = 0; i < 4; ++i) {
                    ImGui::TableSetColumnIndex(5 + static_cast<int>(i));
                    cellText(ui, o ? std::format("{}%", value(r, i)) : "-", o ? ImVec4(1, 1, 1, 1) : kTextDim);
                }
                break;
            case QueueTab::Facilities:
                ImGui::TableSetColumnIndex(5);
                cellText(ui, o ? std::format("{} / {}", r.facilities, r.slots) : "-", o ? ImVec4(1, 1, 1, 1) : kTextDim);
                ImGui::TableSetColumnIndex(6);
                cellText(ui, o ? std::to_string(r.freeSlots) : "-", kTextDim);
                break;
            case QueueTab::Cargo:
                ImGui::TableSetColumnIndex(5);
                cellText(ui, std::format("{} / {} kT", formatNumber(r.cargoUsed), formatNumber(r.cargoCapacity)));
                ImGui::TableSetColumnIndex(6);
                cellText(ui, std::format("{} kT", formatNumber(r.cargoCapacity - r.cargoUsed)), kTextDim);
                break;
        }
    }

    void upgradeAll(UiContext& ui) {
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        const std::vector<game::ObjectId> only = selectedPlanets();
        std::vector<std::pair<game::ObjectId, game::QueueItem>> work;
        for (const auto& c : s.colonies) {
            if (!c || c->owner != me || c->totalPopulation() == 0) continue;
            if (!only.empty() && std::find(only.begin(), only.end(), c->planet) == only.end()) continue;
            for (const game::QueueItem& item : possibleUpgrades(ui.rules(), s, me, *c)) work.emplace_back(c->planet, item);
        }
        if (work.empty()) {
            status_.info("No facilities can be upgraded right now");
            return;
        }
        int done = 0;
        std::vector<game::ObjectId> planets;
        for (const auto& [planet, item] : work)
            if (status_.issue(ui, game::cmd::QueueAdd{{planet, {}}, item, -1})) {
                ++done;
                if (std::find(planets.begin(), planets.end(), planet) == planets.end()) planets.push_back(planet);
            }
        if (done == static_cast<int>(work.size()))
            status_.info(std::format("Queued {} upgrade{} on {} colon{}", done, done == 1 ? "" : "s", planets.size(), planets.size() == 1 ? "y" : "ies"));
    }

    void multiAddPopup(UiContext& ui) {
        const char* id = "Multi-Add###multiadd";
        if (multiAddPending_) {
            ImGui::OpenPopup(id);
            multiAddPending_ = false;
        }
        if (!beginModal(ui, id, {520, 600})) return;
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        ImGui::TextColored(kTextDim, "Pick an item to add to the %zu selected queues.", selection_.size());
        const std::array<const char*, 3> tabs{"Ships", "Units", "Facilities"};
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine(0, ui.px(2));
            const bool active = multiTab_ == i;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.36f, 0.75f, 1));
            if (ImGui::Button(tabs[static_cast<size_t>(i)], ImVec2(ui.px(100), ui.px(24)))) {
                multiTab_ = i;
                multiItem_.reset();
            }
            if (active) ImGui::PopStyleColor();
        }
        std::vector<game::QueueItem> items;
        if (multiTab_ == 2) {
            for (uint32_t f : facilityChoices(r, ui.me(), true)) {
                game::QueueItem it;
                it.kind = game::QueueItem::Kind::Facility;
                it.facility = f;
                items.push_back(it);
            }
        } else {
            for (game::DesignId d : designChoices(r, s, me, multiTab_ == 1, true)) {
                game::QueueItem it;
                it.design = d;
                items.push_back(it);
            }
        }
        const float footer = ui.px(26) * 2 + ImGui::GetStyle().ItemSpacing.y * 3;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(4), ui.px(2)));
        if (ImGui::BeginTable("##items", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter, ImVec2(0, -footer))) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ui.px(28));
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Base cost", ImGuiTableColumnFlags_WidthFixed, ui.px(190));
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < items.size(); ++i) {
                const bool sel = multiItem_ && multiItem_->kind == items[i].kind && multiItem_->design == items[i].design &&
                                 multiItem_->facility == items[i].facility;
                const RowEvents ev = tableRow(ui, static_cast<int>(i), sel);
                if (ev.clicked || ev.doubleClicked) multiItem_ = items[i];
                cellImage(ui, queueItemSprite(ui, items[i]), 22);
                ImGui::TableSetColumnIndex(1);
                cellText(ui, queueItemName(r, s, items[i]));
                ImGui::TableSetColumnIndex(2);
                cellResources(ui, displayCost(r, s, me, selection_.empty() ? game::cmd::QueueTarget{} : selection_.front(), items[i]));
            }
            if (items.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kTextDim, "Nothing of this kind is available.");
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        if (multiTab_ == 1) {
            ImGui::SetNextItemWidth(ui.px(120));
            ImGui::InputInt("Units per queue", &multiCount_);
            multiCount_ = std::clamp(multiCount_, 1, 100);
        } else {
            ImGui::TextColored(kTextDim, "Items a queue cannot build are skipped and reported.");
        }
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginDisabled(!multiItem_);
        const std::string addLabel = std::format("Add To {} Queues", selection_.size());
        const bool add = ImGui::Button(addLabel.c_str(), ImVec2(w, ui.px(26)));
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (add && multiItem_) {
            game::QueueItem item = *multiItem_;
            if (multiTab_ == 1) item.count = multiCount_;
            int done = 0, failed = 0;
            std::string reason;
            for (const auto& t : selection_) {
                const game::CommandResult res = ui.session.issue(game::cmd::QueueAdd{t, item, -1});
                if (res.ok) ++done;
                else {
                    ++failed;
                    if (reason.empty()) reason = res.error;
                }
            }
            const std::string name = queueItemName(r, s, item);
            if (failed == 0) status_.info(std::format("Added {} to {} queue{}", name, done, done == 1 ? "" : "s"));
            else status_.fail(std::format("Added {} to {} of {} queues; {} could not: {}", name, done, done + failed, failed, reason));
            ImGui::CloseCurrentPopup();
        } else if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    std::vector<QueueRow> rows_;
    uint64_t revision_ = 0;
    QueueTab tab_ = QueueTab::Rate;
    bool showShips_ = true, showPlanets_ = true, showBases_ = true, showPlanetYards_ = true;
    std::vector<game::cmd::QueueTarget> selection_;
    std::optional<game::SystemId> hovered_;
    int sortColumn_ = QcName;
    bool ascending_ = true;
    StatusLine status_;
    ReportPopup report_;
    ScrapFacilitiesPopup scrapOne_;
    ScrapTypePopup scrapTypes_;
    bool multiAddPending_ = false;
    int multiTab_ = 0;
    int multiCount_ = 1;
    std::optional<game::QueueItem> multiItem_;
};

// ============================================================================================
// Set Construction Queue
// ============================================================================================

enum class BuildTab { Ships, Facilities, Units, Upgrades };
constexpr std::array<std::pair<BuildTab, const char*>, 4> kBuildTabs{
    {{BuildTab::Ships, "Ships"}, {BuildTab::Facilities, "Facilities"}, {BuildTab::Units, "Units"}, {BuildTab::Upgrades, "Upgrades"}}};

// One entry of the buildable-items list.
struct Buildable {
    game::QueueItem item;
    std::string name;
    std::string problem;
    game::Resources cost;
    int turns = -1;
};

std::filesystem::path templatesFile() { return userDataDir() / "queue_types.txt"; }

class SetQueueScreen final : public Screen {
public:
    explicit SetQueueScreen(const ScreenArgs& args) {
        if (args.vehicle.valid()) target_.vehicle = args.vehicle;
        else if (args.planet.valid()) target_.planet = args.planet;
    }

    bool draw(UiContext& ui) override {
        if (!initialized_) {
            initialize(ui);
        }
        const game::GameState& s = ui.state();
        const game::ConstructionQueue* q = queueOf(s, ui.session.player(), target_);
        Dialog d(ui, "Set Construction Queue", DialogSize::Tall);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        if (!q) {
            heading(ui, "No queue");
            ImGui::TextColored(kTextDim, "This queue no longer exists, or we have no colony that can build.");
            d.beginButtons();
            if (d.close()) return false;
            return d.keepOpen();
        }
        rate_ = game::economy::constructionRate(ui.rules(), s, ui.session.player(), target_);
        header(ui, *q);
        hoverAvail_.reset();
        hoverQueue_.reset();
        const float detailH = ui.px(150);
        const float listsH = ImGui::GetContentRegionAvail().y - detailH - ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y;
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginChild("##available", ImVec2(half, listsH));
        available(ui);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##queue", ImVec2(0, listsH));
        queueList(ui, *q);
        ImGui::EndChild();
        ImGui::BeginChild("##detail", ImVec2(0, detailH), ImGuiChildFlags_Borders);
        detail(ui, *q);
        ImGui::EndChild();
        status_.draw(ui);

        d.beginButtons();
        const bool planet = !target_.vehicle.valid();
        for (const auto& [tab, label] : kBuildTabs)
            if (lampButton(d, ui, label, tab_ == tab, planet || tab == BuildTab::Ships || tab == BuildTab::Units)) tab_ = tab;
        if (lampButton(d, ui, "Only Latest", onlyLatest_, true, "Show only the newest level of each facility and no obsolete designs"))
            onlyLatest_ = !onlyLatest_;
        d.spacer();
        game::cmd::QueueFlags flags{target_, q->onHold, q->repeat, q->emergency, q->autoWaypoint};
        const bool recovering = !q->emergency && q->slowTurns > 0;
        if (lampButton(d, ui, "Emergency Build", q->emergency, !recovering,
                       recovering ? "The yard is recovering from emergency construction"
                                  : "Build at 150% for up to 10 turns; afterwards the yard runs slow for as long")) {
            flags.emergency = !q->emergency;
            status_.issue(ui, flags);
        }
        if (lampButton(d, ui, "Repeat Build", q->repeat, true, "Keep rebuilding the top item")) {
            flags.repeat = !q->repeat;
            status_.issue(ui, flags);
        }
        if (lampButton(d, ui, "Queue On Hold", q->onHold, true, "Freeze the queue; nothing is spent")) {
            flags.onHold = !q->onHold;
            status_.issue(ui, flags);
        }
        d.spacer();
        if (d.button("Set Move To")) waypointPending_ = true;
        if (ImGui::IsItemHovered()) itemTooltip("New ships built here get orders to move to a waypoint");
        if (d.button("Clear Move To", q->autoWaypoint >= 0)) {
            flags.autoWaypoint = -1;
            status_.issue(ui, flags);
        }
        d.spacer();
        if (d.button("Fill Queue")) {
            templatesPending_ = true;
            chosenTemplate_ = -1;
        }
        if (d.button("Clear Queue", !q->items.empty())) {
            if (!q->items.front().spent.isZero()) {
                confirmAction_ = Confirm::Clear;
                confirm_.open("Clear the whole queue? Progress on the item under construction is lost.");
            } else {
                clearQueue(ui);
            }
        }
        if (d.close()) return false;
        if (confirm_.draw(ui)) {
            if (confirmAction_ == Confirm::Clear) clearQueue(ui);
            else if (confirmAction_ == Confirm::RemoveTop) remove(ui, 0);
        }
        waypointPopup(ui, flags);
        if (const game::ConstructionQueue* now = queueOf(s, ui.session.player(), target_)) templatePopup(ui, *now);
        return d.keepOpen();
    }

private:
    enum class Confirm { None, Clear, RemoveTop };

    void initialize(UiContext& ui) {
        initialized_ = true;
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        if (!target_.vehicle.valid() && !target_.planet.valid()) {
            // Default: the homeworld, else the most populous colony.
            const game::Colony* best = nullptr;
            for (const auto& c : s.colonies)
                if (c && c->owner == me && (!best || (c->homeworld && !best->homeworld) ||
                                            (c->homeworld == best->homeworld && c->totalPopulation() > best->totalPopulation())))
                    best = &*c;
            if (best) target_.planet = best->planet;
        }
        const bool yard = target_.vehicle.valid() || (s.colony(target_.planet) && game::colonyHasSpaceYard(ui.rules(), *s.colony(target_.planet)));
        tab_ = yard ? BuildTab::Ships : BuildTab::Facilities;
    }

    void header(UiContext& ui, const game::ConstructionQueue& q) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::Vehicle* v = target_.vehicle.valid() ? s.vehicle(target_.vehicle) : nullptr;
        const game::Colony* c = v ? nullptr : s.colony(target_.planet);
        const Sprite pic = v ? vehiclePortrait(ui, *v) : ui.art.planetPortrait(r.data().sectorObjectTypes[s.galaxy.object(target_.planet).sectorType].picture);
        image(ui, pic, {72, 72});
        ImGui::SameLine();
        ImGui::BeginGroup();
        const std::string name = v ? v->name : s.galaxy.object(target_.planet).name;
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopFont();
        const game::Location where = v ? v->location : game::locationOf(s.galaxy, target_.planet);
        if (v) ImGui::TextColored(kTextDim, "%s at %s", s.design(v->design).name.c_str(), sectorName(s, where, ui.session.player()).c_str());
        else ImGui::TextColored(kTextDim, "%s, %s", c ? c->colonyType.c_str() : "", s.galaxy.system(where.system).name.c_str());
        ImGui::TextColored(kTextLabel, "Rate");
        ImGui::SameLine(ui.px(84));
        resources(ui, rate_, true);
        if (rate_.isZero()) {
            ImGui::SameLine();
            ImGui::TextColored(kTextDim, "(no construction rate)");
        }
        ImGui::TextColored(kTextLabel, "Stockpile");
        ImGui::SameLine(ui.px(84));
        resources(ui, ui.me().stockpile, true);
        ImGui::EndGroup();

        ImGui::SameLine(ui.px(470));
        ImGui::BeginGroup();
        if (c) {
            labelValue(ui, "Facilities", std::format("{} / {} ({} free after queue)", c->facilities.size(), game::facilitySlots(r, s, *c),
                                                     freeFacilitySlots(r, s, *c)),
                       90);
            labelValue(ui, "Population", std::format("{}M", formatNumber(c->totalPopulation())), 90);
        }
        const int64_t capacity = v ? game::vehicleCargoCapacity(r, s, *v) : c ? game::colonyCargoCapacity(r, s, *c) : 0;
        const int64_t used = game::cargoSpaceUsed(r, s, v ? v->cargo : c->cargo);
        labelValue(ui, "Cargo", std::format("{} / {} kT", formatNumber(used), formatNumber(capacity)), 90);
        const std::string mode = modeText(q, true);
        labelValue(ui, "Mode", mode.empty() ? "Normal" : mode, 90);
        std::string moveTo = "None";
        if (q.autoWaypoint >= 0 && static_cast<size_t>(q.autoWaypoint) < ui.me().waypoints.size()) {
            const game::Waypoint& w = ui.me().waypoints[static_cast<size_t>(q.autoWaypoint)];
            moveTo = w.set ? std::format("{} ({})", w.name.empty() ? std::format("Waypoint {}", q.autoWaypoint + 1) : w.name, sectorName(s, w.location, ui.session.player()))
                           : std::format("Waypoint {} (not set)", q.autoWaypoint + 1);
        }
        labelValue(ui, "New ships to", moveTo, 90);
        if (c && c->totalPopulation() == 0) ImGui::TextColored(kTextWarn, "A colony without population cannot build.");
        ImGui::EndGroup();
        ImGui::Separator();
    }

    std::vector<Buildable> buildables(UiContext& ui) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        std::vector<game::QueueItem> items;
        switch (tab_) {
            case BuildTab::Ships:
            case BuildTab::Units:
                for (game::DesignId d : designChoices(r, s, me, tab_ == BuildTab::Units, onlyLatest_)) {
                    game::QueueItem it;
                    it.design = d;
                    it.count = tab_ == BuildTab::Units ? batch_ : 1;
                    items.push_back(it);
                }
                break;
            case BuildTab::Facilities:
                for (uint32_t f : facilityChoices(r, ui.me(), onlyLatest_)) {
                    game::QueueItem it;
                    it.kind = game::QueueItem::Kind::Facility;
                    it.facility = f;
                    items.push_back(it);
                }
                break;
            case BuildTab::Upgrades:
                if (const game::Colony* c = target_.vehicle.valid() ? nullptr : s.colony(target_.planet)) items = possibleUpgrades(r, s, me, *c);
                break;
        }
        std::vector<Buildable> out;
        for (const game::QueueItem& it : items) {
            Buildable b;
            b.item = it;
            b.name = queueItemName(r, s, it);
            b.problem = game::queueItemProblem(r, s, me, target_, it);
            b.cost = displayCost(r, s, me, target_, it);
            b.turns = std::max(1, game::economy::turnsToComplete(b.cost, rate_));
            if (game::economy::turnsToComplete(b.cost, rate_) < 0) b.turns = -1;
            out.push_back(std::move(b));
        }
        return out;
    }

    void available(UiContext& ui) {
        heading(ui, "Available");
        if (tab_ == BuildTab::Units) {
            ImGui::SameLine(ui.px(120));
            ImGui::TextColored(kTextDim, "Batch");
            for (int n : {1, 5, 10, 20}) {
                ImGui::SameLine(0, ui.px(3));
                const bool on = batch_ == n;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.36f, 0.75f, 1));
                if (ImGui::Button(std::format("{}##batch", n).c_str(), ImVec2(ui.px(32), 0))) batch_ = n;
                if (on) ImGui::PopStyleColor();
            }
        } else {
            ImGui::SameLine(ui.px(120));
            ImGui::TextColored(kTextDim, "Click to add to the queue");
        }
        avail_ = buildables(ui);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(4), ui.px(2)));
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##avail", 3, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", 0, ui.px(28));
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Time", 0, ui.px(62));
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < avail_.size(); ++i) {
                const Buildable& b = avail_[i];
                const bool ok = b.problem.empty();
                const RowEvents ev = tableRow(ui, static_cast<int>(i), false);
                if (ev.hovered) hoverAvail_ = i;
                if (ev.clicked || ev.doubleClicked) add(ui, b);
                cellImage(ui, queueItemSprite(ui, b.item), 22);
                ImGui::TableSetColumnIndex(1);
                cellText(ui, b.name, ok ? ImVec4(1, 1, 1, 1) : kTextDim);
                ImGui::TableSetColumnIndex(2);
                cellText(ui, ok ? turnsText(b.turns) : "-", kTextDim);
            }
            if (avail_.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                const bool planet = !target_.vehicle.valid();
                ImGui::TextColored(kTextDim, "%s", tab_ == BuildTab::Upgrades ? (planet ? "Nothing to upgrade here." : "Only planets upgrade facilities.")
                                                   : tab_ == BuildTab::Facilities && !planet ? "Only planets build facilities."
                                                                                              : "Nothing available.");
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    void queueList(UiContext& ui, const game::ConstructionQueue& q) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const auto est = estimateQueue(r, s, ui.session.player(), target_, q, rate_);
        heading(ui, "Queue");
        ImGui::SameLine(ui.px(80));
        if (q.items.empty()) ImGui::TextColored(kTextWarn, "Empty");
        else if (est.back().doneIn < 0) ImGui::TextColored(kTextDim, "%zu item%s", q.items.size(), q.items.size() == 1 ? "" : "s");
        else ImGui::TextColored(kTextDim, "%zu item%s, all done in %s", q.items.size(), q.items.size() == 1 ? "" : "s", turnsText(est.back().doneIn).c_str());
        if (selected_ && *selected_ >= q.items.size()) selected_.reset();
        const float controls = ui.px(26) + ImGui::GetStyle().ItemSpacing.y;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui.px(4), ui.px(2)));
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##queue", 4, flags, ImVec2(0, -controls))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", 0, ui.px(28));
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Progress", 0, ui.px(70));
            ImGui::TableSetupColumn("Done", 0, ui.px(62));
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < q.items.size(); ++i) {
                const game::QueueItem& item = q.items[i];
                const RowEvents ev = tableRow(ui, static_cast<int>(i), selected_ == i);
                if (ev.hovered) hoverQueue_ = i;
                if (ev.clicked || ev.doubleClicked) selected_ = i;
                cellImage(ui, queueItemSprite(ui, item), 22);
                ImGui::TableSetColumnIndex(1);
                cellText(ui, queueItemName(r, s, item), i == 0 ? kTextHighlight : ImVec4(1, 1, 1, 1));
                ImGui::TableSetColumnIndex(2);
                const int64_t total = est[i].cost.total();
                const float frac = total > 0 ? float(item.spent.total()) / float(total) : 0.0f;
                cellProgress(ui, frac, std::format("{}%", int(frac * 100)));
                ImGui::TableSetColumnIndex(3);
                cellText(ui, turnsText(est[i].doneIn), kTextDim);
            }
            if (q.items.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kTextDim, "Add items from the list on the left.");
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();

        // Reorder, remove and batch-size controls for the selected item.
        // Everything is read before any button acts: a command changes the items under us.
        const bool sel = selected_.has_value();
        const size_t n = q.items.size();
        const size_t i = selected_.value_or(0);
        const bool unit = sel && q.items[i].kind == game::QueueItem::Kind::Vehicle && q.items[i].design.valid() &&
                          isUnitDesign(r, s.design(q.items[i].design));
        const int count = sel ? q.items[i].count : 1;
        const bool topHasProgress = n > 0 && !q.items.front().spent.isZero();
        const std::string topName = n > 0 ? queueItemName(r, s, q.items.front()) : std::string{};
        const float w = ui.px(52);
        auto small = [&](const char* label, bool enabled, const char* tip) {
            ImGui::BeginDisabled(!enabled);
            const bool c = ImGui::Button(label, ImVec2(w, ui.px(24)));
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) itemTooltip(tip);
            ImGui::SameLine(0, ui.px(3));
            return c;
        };
        enum class Act { None, Top, Up, Down, Bottom, Remove, Fewer, More } act = Act::None;
        if (small("Top", sel && i > 0, "Move the selected item to the top")) act = Act::Top;
        if (small("Up", sel && i > 0, "Move the selected item up")) act = Act::Up;
        if (small("Down", sel && i + 1 < n, "Move the selected item down")) act = Act::Down;
        if (small("Bottom", sel && i + 1 < n, "Move the selected item to the bottom")) act = Act::Bottom;
        if (small("Remove", sel, "Remove the selected item from the queue (Delete)")) act = Act::Remove;
        if (unit) {
            ImGui::SameLine(0, ui.px(10));
            if (ImGui::Button("-", ImVec2(ui.px(24), ui.px(24))) && count > 1) act = Act::Fewer;
            ImGui::SameLine(0, ui.px(3));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("x%d", count);
            ImGui::SameLine(0, ui.px(3));
            if (ImGui::Button("+", ImVec2(ui.px(24), ui.px(24)))) act = Act::More;
        } else {
            ImGui::NewLine();
        }
        if (sel && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) act = Act::Remove;
        switch (act) {
            case Act::None: break;
            case Act::Top: move(ui, i, 0); break;
            case Act::Up: move(ui, i, i - 1); break;
            case Act::Down: move(ui, i, i + 1); break;
            case Act::Bottom: move(ui, i, n - 1); break;
            case Act::Remove:
                if (i == 0 && topHasProgress) {
                    confirmAction_ = Confirm::RemoveTop;
                    confirm_.open(std::format("Remove {}? Its construction progress is lost.", topName));
                } else {
                    remove(ui, i);
                }
                break;
            // Batch sizes step by one up to 5, then by five.
            case Act::Fewer: status_.issue(ui, game::cmd::QueueSetCount{target_, static_cast<uint32_t>(i), count > 5 ? count - 5 : count - 1}); break;
            case Act::More: status_.issue(ui, game::cmd::QueueSetCount{target_, static_cast<uint32_t>(i), count >= 5 ? count + 5 : count + 1}); break;
        }
    }

    void detail(UiContext& ui, const game::ConstructionQueue& q) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        std::optional<game::QueueItem> item;
        std::string problem;
        game::Resources cost;
        int turns = -1;
        std::optional<int> remaining;
        if (hoverAvail_ && *hoverAvail_ < avail_.size()) {
            const Buildable& b = avail_[*hoverAvail_];
            item = b.item;
            problem = b.problem;
            cost = b.cost;
            turns = b.turns;
        } else if (const auto idx = hoverQueue_ ? hoverQueue_ : selected_; idx && *idx < q.items.size()) {
            item = q.items[*idx];
            const auto est = estimateQueue(r, s, ui.session.player(), target_, q, rate_);
            cost = est[*idx].cost;
            turns = std::max(1, game::economy::turnsToComplete(cost, rate_));
            if (game::economy::turnsToComplete(cost, rate_) < 0) turns = -1;
            if (*idx == 0) remaining = est[0].turns;
        }
        if (!item) {
            ImGui::TextColored(kTextDim, "Point at an item to see its details. Left-click an available item to queue it;");
            ImGui::TextColored(kTextDim, "select a queued item to reorder or remove it.");
            return;
        }
        Sprite portrait;
        if (item->kind == game::QueueItem::Kind::Vehicle) {
            const game::Design& d = s.design(item->design);
            portrait = ui.art.shipPortrait(ui.me().race.style, r.hull(d.hull));
        } else if (item->facility < r.data().facilities.size()) {
            portrait = ui.art.facilityPortrait(r.facility(item->facility).picture);
        }
        image(ui, portrait, {96, 96});
        ImGui::SameLine();
        ImGui::BeginChild("##detailtext", ImVec2(0, 0));
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted(queueItemName(r, s, *item).c_str());
        ImGui::PopFont();
        ImGui::TextColored(kTextLabel, "Cost");
        ImGui::SameLine(ui.px(90));
        resources(ui, cost, true);
        std::string time = turnsText(turns);
        // For the item under construction, the time left for what remains (spec 02 §11).
        if (remaining && *remaining >= 0 && *remaining != turns) time += std::format(" ({} left)", turnsText(*remaining));
        labelValue(ui, "Build time", time, 90);
        if (!problem.empty()) ImGui::TextColored(kTextWarn, "Cannot build: %s", problem.c_str());
        ImGui::PushTextWrapPos(0.0f);
        if (item->kind == game::QueueItem::Kind::Vehicle) {
            const game::Design& d = s.design(item->design);
            const game::DesignStats st = game::computeDesignStats(r, nullptr, d);
            const ruleset::VehicleSize& hull = r.hull(d.hull);
            ImGui::TextColored(kTextDim, "%s, %s. %d / %d kT used, speed %d, structure %d, %d weapon%s%s%s.", hull.name.c_str(),
                               std::string(ruleset::displayName(hull.type)).c_str(), st.tonnageUsed, st.tonnageMax, st.movement, st.structure,
                               st.weapons, st.weapons == 1 ? "" : "s", st.cargoCapacity > 0 ? std::format(", cargo {} kT", st.cargoCapacity).c_str() : "",
                               st.spaceYard ? ", space yard" : "");
            if (st.canColonizeRock || st.canColonizeIce || st.canColonizeGas)
                ImGui::TextColored(kTextGood, "Colony module:%s%s%s", st.canColonizeRock ? " Rock" : "", st.canColonizeIce ? " Ice" : "",
                                   st.canColonizeGas ? " Gas Giant" : "");
        } else if (item->facility < r.data().facilities.size()) {
            const ruleset::Facility& f = r.facility(item->facility);
            if (item->kind == game::QueueItem::Kind::Upgrade) {
                const int older = item->count;  // fixed when queued (spec 02 §6.6)
                ImGui::TextColored(kTextGood, "Upgrades %d older facilit%s of this kind here.", older, older == 1 ? "y" : "ies");
            }
            ImGui::TextColored(kTextDim, "%s", f.description.c_str());
            for (const ruleset::Ability& a : f.abilities)
                if (!a.description.empty()) ImGui::BulletText("%s", a.description.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }

    void add(UiContext& ui, const Buildable& b) {
        if (!b.problem.empty()) {
            status_.fail(std::format("{}: {}", b.name, b.problem));
            return;
        }
        if (status_.issue(ui, game::cmd::QueueAdd{target_, b.item, -1})) status_.info(std::format("Queued {}", b.name));
    }

    void move(UiContext& ui, size_t from, size_t to) {
        if (status_.issue(ui, game::cmd::QueueMove{target_, static_cast<uint32_t>(from), static_cast<uint32_t>(to)})) selected_ = to;
    }

    void remove(UiContext& ui, size_t index) {
        if (status_.issue(ui, game::cmd::QueueRemove{target_, static_cast<uint32_t>(index)})) selected_.reset();
    }

    void clearQueue(UiContext& ui) {
        const game::ConstructionQueue* q = queueOf(ui.state(), ui.session.player(), target_);
        if (!q) return;
        for (size_t i = q->items.size(); i-- > 0;)
            if (!status_.issue(ui, game::cmd::QueueRemove{target_, static_cast<uint32_t>(i)})) break;
        selected_.reset();
    }

    void waypointPopup(UiContext& ui, game::cmd::QueueFlags flags) {
        const char* id = "Select Waypoint###queuewaypoint";
        if (waypointPending_) {
            ImGui::OpenPopup(id);
            waypointPending_ = false;
        }
        if (!beginModal(ui, id, {420, 420})) return;
        ImGui::TextColored(kTextDim, "New ships from this queue move to:");
        const auto& wps = ui.me().waypoints;
        const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
        ImGui::BeginChild("##wps", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        bool any = false;
        for (size_t i = 0; i < wps.size(); ++i) {
            const game::Waypoint& w = wps[i];
            const std::string label = w.set ? std::format("{}. {} - {}", i + 1, w.name.empty() ? "Waypoint" : w.name, sectorName(ui.state(), w.location, ui.session.player()))
                                             : std::format("{}. (not set)", i + 1);
            if (ImGui::Selectable(label.c_str(), flags.autoWaypoint == static_cast<int>(i), w.set ? 0 : ImGuiSelectableFlags_Disabled)) {
                flags.autoWaypoint = static_cast<int>(i);
                if (status_.issue(ui, flags)) status_.info(std::format("New ships will move to waypoint {}", i + 1));
                ImGui::CloseCurrentPopup();
            }
            any = any || w.set;
        }
        if (!any) ImGui::TextColored(kTextDim, "No waypoints are set. Set them in Empire Status > Waypoints.");
        ImGui::EndChild();
        if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void loadTemplates() {
        if (templatesLoaded_) return;
        templatesLoaded_ = true;
        userTemplates_.clear();
        std::ifstream in(templatesFile(), std::ios::binary);
        if (!in) return;
        std::stringstream ss;
        ss << in.rdbuf();
        userTemplates_ = parseTemplates(ss.str());
    }

    void saveTemplates() {
        std::ofstream out(templatesFile(), std::ios::binary | std::ios::trunc);
        if (out) out << serializeTemplates(userTemplates_);
        else status_.fail("Could not save the queue types to " + templatesFile().string());
    }

    void templatePopup(UiContext& ui, const game::ConstructionQueue& q) {
        const char* id = "Select Queue Type###queuetypes";
        if (templatesPending_) {
            loadTemplates();
            ImGui::OpenPopup(id);
            templatesPending_ = false;
        }
        if (!beginModal(ui, id, {640, 560})) return;
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        std::vector<QueueTemplate> all = builtInQueueTemplates();
        const int builtIns = static_cast<int>(all.size());
        all.insert(all.end(), userTemplates_.begin(), userTemplates_.end());
        if (chosenTemplate_ >= static_cast<int>(all.size())) chosenTemplate_ = -1;
        const float footer = ui.px(26) * 2 + ImGui::GetStyle().ItemSpacing.y * 3;
        ImGui::BeginChild("##list", ImVec2(ui.px(280), -footer), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < all.size(); ++i) {
            if (static_cast<int>(i) == builtIns) ImGui::Separator();
            if (ImGui::Selectable(std::format("{}##t{}", all[i].name, i).c_str(), chosenTemplate_ == static_cast<int>(i))) chosenTemplate_ = static_cast<int>(i);
        }
        if (userTemplates_.empty()) ImGui::TextColored(kTextDim, "Add Type saves this queue as a new type.");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##preview", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        std::vector<game::QueueItem> items;
        if (chosenTemplate_ >= 0) {
            items = resolveTemplate(r, s, ui.session.player(), target_, all[static_cast<size_t>(chosenTemplate_)]);
            heading(ui, "Adds");
            for (const game::QueueItem& it : items) {
                const std::string problem = game::queueItemProblem(r, s, ui.session.player(), target_, it);
                image(ui, queueItemSprite(ui, it), {18, 18});
                ImGui::SameLine();
                ImGui::TextColored(problem.empty() ? ImVec4(1, 1, 1, 1) : kTextDim, "%s", queueItemName(r, s, it).c_str());
            }
            if (items.empty()) ImGui::TextColored(kTextDim, "Nothing this queue can take (no free slots or no matching technology).");
        } else {
            ImGui::TextColored(kTextDim, "Pick a queue type to see what it adds.");
        }
        ImGui::EndChild();
        ImGui::SetNextItemWidth(ui.px(280));
        ImGui::InputTextWithHint("##newname", "Name for a new type", newTemplateName_, sizeof(newTemplateName_));
        ImGui::SameLine();
        const bool canAdd = newTemplateName_[0] != '\0' && !q.items.empty();
        ImGui::BeginDisabled(!canAdd);
        if (ImGui::Button("Add Type", ImVec2(ui.px(120), 0))) {
            userTemplates_.push_back(templateFromQueue(r, s, newTemplateName_, q));
            saveTemplates();
            newTemplateName_[0] = '\0';
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) itemTooltip("Save the current queue as a new queue type");
        ImGui::SameLine();
        const bool userChosen = chosenTemplate_ >= builtIns;
        ImGui::BeginDisabled(!userChosen);
        if (ImGui::Button("Delete Type", ImVec2(-FLT_MIN, 0)) && userChosen) {
            userTemplates_.erase(userTemplates_.begin() + (chosenTemplate_ - builtIns));
            saveTemplates();
            chosenTemplate_ = -1;
        }
        ImGui::EndDisabled();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginDisabled(items.empty());
        const bool fill = ImGui::Button("Fill Queue", ImVec2(w, ui.px(26)));
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", ImVec2(w, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (fill) {
            int done = 0, skipped = 0;
            for (const game::QueueItem& it : items) {
                if (!game::queueItemProblem(r, s, ui.session.player(), target_, it).empty()) {
                    ++skipped;
                    continue;
                }
                done += ui.session.issue(game::cmd::QueueAdd{target_, it, -1}).ok ? 1 : 0;
            }
            status_.info(skipped > 0 ? std::format("Added {} item{}; {} could not be built here", done, done == 1 ? "" : "s", skipped)
                                     : std::format("Added {} item{}", done, done == 1 ? "" : "s"));
            ImGui::CloseCurrentPopup();
        } else if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    game::cmd::QueueTarget target_;
    bool initialized_ = false;
    BuildTab tab_ = BuildTab::Facilities;
    bool onlyLatest_ = true;
    int batch_ = 1;
    game::Resources rate_;
    std::vector<Buildable> avail_;
    std::optional<size_t> hoverAvail_, hoverQueue_, selected_;
    StatusLine status_;
    ConfirmPopup confirm_;
    Confirm confirmAction_ = Confirm::None;
    bool waypointPending_ = false;
    bool templatesPending_ = false;
    bool templatesLoaded_ = false;
    int chosenTemplate_ = -1;
    std::vector<QueueTemplate> userTemplates_;
    char newTemplateName_[64] = {};
};

} // namespace

std::unique_ptr<Screen> makeQueues(const ScreenArgs&) { return std::make_unique<QueuesScreen>(); }
std::unique_ptr<Screen> makeSetQueue(const ScreenArgs& args) { return std::make_unique<SetQueueScreen>(args); }

} // namespace opense4::client::classic
