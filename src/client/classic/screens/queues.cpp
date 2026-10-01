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

bool hasTarget(const std::vector<game::cmd::QueueTarget>& list, const game::cmd::QueueTarget& t) {
    return std::any_of(list.begin(), list.end(), [&](const auto& x) { return sameTarget(x, t); });
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
// Construction Queues (spec 06 §1.8.2, confirmed: binary)
// ============================================================================================

enum class QueueTab : uint8_t { Rate, Usage, PlanetValue, Facilities, Cargo };
constexpr std::array<const char*, 5> kQueueTabLabels{{"Rate", "Usage", "Planet Value", "Facilities", "Cargo"}};
// The tab column's header per tab.
constexpr std::array<const char*, 5> kQueueTabHeaders{{"Rate", "Usage Per Turn", "Planet Value", "Number of Facilities", "Cargo Space"}};
// The toggles in the button column's order (slots 7-10).
constexpr std::array<std::pair<QueueKind, const char*>, 4> kQueueToggles{
    {{QueueKind::Ship, "Ships"}, {QueueKind::Planet, "Planets"}, {QueueKind::ShipYard, "Ship SY"}, {QueueKind::PlanetYard, "Planet SY"}}};

enum QueueColumn { QcName, QcTab, QcQueue };
constexpr float kQueueRowH = 40.0f;   // three 12 px item lines and a margin (inferred)
constexpr std::array<uint32_t, 3> kResourceColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};

// Multi-Add hands the tagged queues to the Set Construction Queue window it
// opens, and gets back what was added (one Multi-Add at a time).
std::vector<game::cmd::QueueTarget> gMultiAddTargets;
std::string gMultiAddReport;
bool gMultiAddFailed = false;

struct QueueRow {
    QueueEntry entry;
    game::Resources rate, usage;
    std::vector<std::string> items;   // the first three, with counts
    int turns = -1;                   // the whole queue (-1: never)
    bool empty = true;
    bool onHold = false;
    bool repeat = false;
    std::string note;                 // the build-mode note
    int facilities = 0, slots = 0;    // colonies only
    int64_t cargoUsed = 0, cargoCapacity = 0;
    std::vector<int> icons;           // colonies' status icons
    std::array<int, 3> value{};       // the planet's values (colonies only)
    bool colony = false;
};

// Three amounts in the resource colours, from `x`.
void drawAmounts(UiContext& ui, ImDrawList* dl, ImVec2 at, const std::array<int64_t, 3>& v, const char* suffix = "") {
    float x = at.x;
    for (size_t k = 0; k < 3; ++k) {
        const std::string t = std::format("{}{}", v[k], suffix);
        dl->AddText(ImVec2(x, at.y), imColor(kResourceColors[k]), t.c_str());
        x += std::max(ImGui::CalcTextSize(t.c_str()).x + ui.px(6), ui.px(44));
    }
}

class QueuesScreen final : public Screen {
public:
    bool draw(UiContext& ui) override {
        if (!restored_) {
            // The tab and the toggles come back from the empire (spec 06 §1.8.2).
            tab_ = static_cast<QueueTab>(std::min<int>(ui.options().queuesTab, int(kQueueTabLabels.size()) - 1));
            shown_ = ui.options().queuesShown;
            restored_ = true;
        }
        if (revision_ != ui.session.revision()) refresh(ui);
        if (!gMultiAddReport.empty()) {
            if (gMultiAddFailed) status_.fail(gMultiAddReport);
            else status_.info(gMultiAddReport);
            gMultiAddReport.clear();
        }
        bool leave = false;
        {
            Dialog d(ui, "Construction Queues", DialogSize::Tall);
            if (!d.open()) return d.keepOpen();
            std::vector<const QueueRow*> rows;
            for (const QueueRow& r : rows_)
                if (shown_ & queueKindBit(r.entry.kind)) rows.push_back(&r);
            sortRows(rows);

            d.beginContent();
            statistics(ui);
            ImGui::SetCursorPos(ui.size({290, 3}));
            std::vector<uint8_t> marked(ui.state().galaxy.systems.size(), 0);
            for (const QueueRow* r : rows) marked[r->entry.where.system.index()] = 1;
            quadrantMap(ui, "##map", {262, 190}, marked, hovered_);
            hovered_.reset();
            ImGui::SetCursorPos(ui.size({0, 197}));
            list(ui, rows);
            status_.draw(ui);

            d.beginButtons();
            static constexpr std::array<const char*, 5> kTabIds{{"rate", "usage", "planet-value", "facilities", "cargo"}};
            for (size_t i = 0; i < kQueueTabLabels.size(); ++i) {
                if (d.tab(kQueueTabLabels[i], tab_ == static_cast<QueueTab>(i))) tab_ = static_cast<QueueTab>(i);
                ui.tagTab(kTabIds[i], tab_ == static_cast<QueueTab>(i));
            }
            d.spacer();
            for (const auto& [kind, label] : kQueueToggles)
                if (d.tab(label, (shown_ & queueKindBit(kind)) != 0)) shown_ = uint8_t(shown_ ^ queueKindBit(kind));
            // Always enabled; with nothing tagged it explains how to tag (spec 06 §1.8.2).
            if (d.button("Multi-Add")) {
                if (tagged_.empty()) {
                    noticeText_ = "Shift+click the queues to add to first (a lamp marks each); Multi-Add then adds the same items to every one of them.";
                    notice_ = true;
                } else {
                    gMultiAddTargets = tagged_;
                    ScreenArgs a;
                    a.text = "multiadd";
                    ui.open(ScreenId::SetQueue, a);
                }
            }
            if (d.button("Scrap Facilities")) {
                // A "Select Planet" picker over the listed colonies; the tags do not count.
                pickRows_.clear();
                for (const QueueRow* r : rows)
                    if (r->colony) pickRows_.push_back(r->entry.target.planet);
                picked_.reset();
                picking_ = true;
            }
            if (d.button("Upgrade Facilities")) upgradeAll(ui);
            d.close();
            report_.draw(ui);
            scrapOne_.draw(ui, status_);
            pickerPopup(ui);
            noticePopup(ui);
            leave = !d.keepOpen();
        }
        if (leave) {
            // Stored with the empire when the window closes.
            game::InterfaceOptions o = ui.options();
            o.queuesTab = static_cast<uint8_t>(tab_);
            o.queuesShown = shown_;
            ui.setOptions(o);
            return false;
        }
        return true;
    }

private:
    void refresh(UiContext& ui) {
        const game::Rules& r = ui.rules();
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        rows_.clear();
        planetYards_ = shipYards_ = onHold_ = 0;
        usage_ = {};
        for (QueueEntry& e : empireQueues(r, s, me)) {
            const game::ConstructionQueue* q = queueOf(s, me, e.target);
            if (!q) continue;
            QueueRow row;
            row.rate = game::economy::constructionRate(r, s, me, e.target);
            row.usage = queueUsage(r, s, me, e.target, *q, row.rate);
            row.empty = q->items.empty();
            row.onHold = q->onHold;
            row.repeat = q->repeat;
            row.note = queueModeNote(*q);
            for (size_t i = 0; i < q->items.size() && i < 3; ++i) row.items.push_back(queueItemName(r, s, q->items[i]));
            if (!q->items.empty()) row.turns = estimateQueue(r, s, me, e.target, *q, row.rate).back().doneIn;
            if (const game::Colony* c = e.target.vehicle.valid() ? nullptr : s.colony(e.target.planet)) {
                row.colony = true;
                row.facilities = static_cast<int>(c->facilities.size());
                row.slots = game::facilitySlots(r, s, *c);
                row.cargoUsed = game::cargoSpaceUsed(r, s, c->cargo);
                row.cargoCapacity = game::colonyCargoCapacity(r, s, *c);
                row.icons = colonyStatusIcons(r, s, *c, game::economy::colonyOutput(r, s, *c).connected);
                row.value = s.galaxy.object(c->planet).value;
            } else if (const game::Vehicle* v = s.vehicle(e.target.vehicle)) {
                row.cargoUsed = game::cargoSpaceUsed(r, s, v->cargo);
                row.cargoCapacity = game::vehicleCargoCapacity(r, s, *v);
            }
            planetYards_ += e.kind == QueueKind::PlanetYard;
            shipYards_ += e.kind == QueueKind::ShipYard;
            onHold_ += row.onHold;
            usage_ += row.usage;
            row.entry = std::move(e);
            rows_.push_back(std::move(row));
        }
        std::erase_if(tagged_, [&](const auto& t) { return !queueOf(s, me, t); });
        revision_ = ui.session.revision();
    }

    // The tab column's sort value: the sum of the three amounts, the facility
    // count or the cargo; vehicles count 0 for facilities and cargo.
    int64_t tabValue(const QueueRow& r) const {
        switch (tab_) {
            case QueueTab::Rate: return r.rate.total();
            case QueueTab::Usage: return r.usage.total();
            case QueueTab::PlanetValue: return int64_t{r.value[0]} + r.value[1] + r.value[2];
            case QueueTab::Facilities: return r.colony ? r.facilities : 0;
            case QueueTab::Cargo: return r.colony ? r.cargoUsed : 0;
        }
        return 0;
    }

    void sortRows(std::vector<const QueueRow*>& rows) const {
        // Name A to Z, the tab column highest first, the queue by its first item A to Z (spec 06 §1.8.2).
        sort_.sort(rows, [&](int column, const QueueRow* a, const QueueRow* b) {
            switch (column) {
                case QcTab: {
                    const int64_t x = tabValue(*a), y = tabValue(*b);
                    return x == y ? 0 : x > y ? -1 : 1;
                }
                case QcQueue:
                    return compareNames(a->items.empty() ? std::string_view{} : a->items.front(), b->items.empty() ? std::string_view{} : b->items.front());
                default: return compareNames(a->entry.name, b->entry.name);
            }
        });
    }

    void statistics(UiContext& ui) {
        const game::EconomyReport& eco = ui.me().economy;
        const game::Resources income = eco.colonies + eco.trade + eco.tariffsIn + eco.remoteMining + eco.otherIncome;
        auto line = [&](float y, const char* label) {
            ImGui::SetCursorPos(ui.size({3, y}));
            ImGui::TextColored(kLabelBlue, "%s", label);
        };
        auto value = [&](float y, const std::string& v) {
            ImGui::SetCursorPos(ImVec2(ui.px(274) - ImGui::CalcTextSize(v.c_str()).x, ui.px(y)));
            ImGui::TextUnformatted(v.c_str());
        };
        line(5, "Resources Per Turn");
        ImGui::SetCursorPos(ui.size({12, 21}));
        resources(ui, income, true);
        line(41, "Queue Usage Per Turn");
        ImGui::SetCursorPos(ui.size({12, 57}));
        resources(ui, usage_, true);
        line(85, "Space Yards");
        value(85, std::to_string(planetYards_ + shipYards_));
        line(101, "Planetary Space Yards");
        value(101, std::to_string(planetYards_));
        line(117, "Ship Space Yards");
        value(117, std::to_string(shipYards_));
        line(133, "Queues On Hold");
        value(133, std::to_string(onHold_));
        ImGui::SetCursorPos(ui.size({3, 183}));
        ImGui::TextColored(kTextDim, "Click a queue to change it.");
    }

    void list(UiContext& ui, const std::vector<const QueueRow*>& rows) {
        const game::GameState& s = ui.state();
        const std::array<ListColumn, 3> cols{{{"Name", 170}, {kQueueTabHeaders[size_t(tab_)], 150}, {"Construction Queue", 0}}};
        // The rows' child has no padding; the header leaves room for its scrollbar.
        const float width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - 2.0f;
        if (const int c = listHeader(ui, "##queuehead", cols, width); c >= 0) sort_.click(c);
        const std::vector<float> x = columnEdges(ui, cols, width);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1, 1));
        ImGui::BeginChild("##queuerows", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        const float rowH = ui.px(kQueueRowH);
        const float lh = ImGui::GetTextLineHeight();
        for (const QueueRow* rp : rows) {
            const QueueRow& r = *rp;
            const game::cmd::QueueTarget& t = r.entry.target;
            ImGui::PushID(t.vehicle.valid() ? int(t.vehicle.value) * 2 + 1 : int(t.planet.value) * 2);
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0, rowH - ImGui::GetStyle().ItemSpacing.y));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hovered) hovered_ = r.entry.where.system;
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) openReport(report_, t);
            const bool tagged = hasTarget(tagged_, t);
            if (clicked) {
                if (ImGui::GetIO().KeyShift) {
                    // Shift+click tags or untags a queue for Multi-Add.
                    if (tagged) std::erase_if(tagged_, [&](const auto& y) { return sameTarget(y, t); });
                    else tagged_.push_back(t);
                } else {
                    // Set Construction Queue for that queue, over this window.
                    ScreenArgs args;
                    if (t.vehicle.valid()) args.vehicle = t.vehicle;
                    else args.planet = t.planet;
                    ui.open(ScreenId::SetQueue, args);
                }
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float top = a.y + ui.px(2);
            auto clipText = [&](size_t col, ImVec2 at, ImU32 color, const std::string& text) {
                dl->PushClipRect(ImVec2(a.x + x[col], a.y), ImVec2(a.x + x[col + 1] - ui.px(2), a.y + rowH), true);
                dl->AddText(at, color, text.c_str());
                dl->PopClipRect();
            };
            // Name: picture, name, and the status icons on a second line.
            if (Sprite pic = targetSprite(ui, t))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(pic.tex.value)), ImVec2(a.x + ui.px(2), top), ImVec2(a.x + ui.px(34), top + ui.px(32)),
                             {pic.uv.min.x, pic.uv.min.y}, {pic.uv.max.x, pic.uv.max.y});
            if (tagged)
                if (Sprite lampOn = ui.art.region("Pictures/Game/General.bmp", 191, 0, 13, 13))  // the tag marker (inferred)
                    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(lampOn.tex.value)), ImVec2(a.x, top), ImVec2(a.x + ui.px(13), top + ui.px(13)),
                                 {lampOn.uv.min.x, lampOn.uv.min.y}, {lampOn.uv.max.x, lampOn.uv.max.y});
            clipText(QcName, ImVec2(a.x + ui.px(38), top), IM_COL32_WHITE, r.entry.name);
            for (size_t i = 0; i < r.icons.size() && i < 6; ++i)
                if (Sprite ic = ui.art.statusIcon(r.icons[i])) {
                    const ImVec2 p0(a.x + ui.px(38 + 20 * float(i)), top + lh + ui.px(3));
                    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(ic.tex.value)), p0, ImVec2(p0.x + ui.px(16), p0.y + ui.px(16)),
                                 {ic.uv.min.x, ic.uv.min.y}, {ic.uv.max.x, ic.uv.max.y});
                }
            // The tab column, with the build-mode note under it in yellow.
            const ImVec2 tabAt(a.x + x[QcTab] + ui.px(2), top);
            dl->PushClipRect(ImVec2(a.x + x[QcTab], a.y), ImVec2(a.x + x[QcQueue] - ui.px(2), a.y + rowH), true);
            switch (tab_) {
                case QueueTab::Rate: drawAmounts(ui, dl, tabAt, r.rate.v); break;
                case QueueTab::Usage: drawAmounts(ui, dl, tabAt, r.usage.v); break;
                case QueueTab::PlanetValue:
                    if (r.colony) drawAmounts(ui, dl, tabAt, {r.value[0], r.value[1], r.value[2]}, s.options.finiteResources ? "" : "%");
                    break;
                case QueueTab::Facilities:
                    if (r.colony) dl->AddText(tabAt, IM_COL32_WHITE, std::format("{} / {}", r.facilities, r.slots).c_str());
                    break;
                case QueueTab::Cargo:
                    dl->AddText(tabAt, IM_COL32_WHITE, std::format("{} / {}", formatNumber(r.cargoUsed), formatNumber(r.cargoCapacity)).c_str());
                    break;
            }
            if (!r.note.empty()) dl->AddText(ImVec2(tabAt.x, top + lh + ui.px(2)), IM_COL32(255, 255, 0, 255), r.note.c_str());
            dl->PopClipRect();
            // The first three items, one per 12 px line; the time in years at the right.
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            std::string time = r.onHold ? std::string("On Hold") : r.empty ? std::string{} : queueYearsText(r.turns);
            const bool yellow = r.onHold || time == "Never";
            const float timeW = std::max(ImGui::CalcTextSize(time.c_str()).x, r.repeat ? ImGui::CalcTextSize("(Repeat)").x : 0.0f);
            const float right = a.x + x[QcQueue + 1] - ui.px(4);
            dl->PushClipRect(ImVec2(a.x + x[QcQueue], a.y), ImVec2(right - timeW - ui.px(4), a.y + rowH), true);
            for (size_t i = 0; i < r.items.size(); ++i)
                dl->AddText(ImVec2(a.x + x[QcQueue] + ui.px(2), top + ui.px(12 * float(i))), IM_COL32_WHITE, r.items[i].c_str());
            dl->PopClipRect();
            if (!time.empty())
                dl->AddText(ImVec2(right - ImGui::CalcTextSize(time.c_str()).x, top), yellow ? IM_COL32(255, 255, 0, 255) : IM_COL32_WHITE, time.c_str());
            if (r.repeat) dl->AddText(ImVec2(right - ImGui::CalcTextSize("(Repeat)").x, top + ui.px(12)), IM_COL32_WHITE, "(Repeat)");
            ImGui::PopFont();
            // Rows are separated by a line in #647EC7.
            dl->AddLine(ImVec2(a.x, a.y + rowH - ImGui::GetStyle().ItemSpacing.y * 0.5f),
                        ImVec2(a.x + x.back(), a.y + rowH - ImGui::GetStyle().ItemSpacing.y * 0.5f), imColor(palette::kFrameLight));
        }
        if (rows.empty()) ImGui::TextColored(kTextDim, "%s", shown_ == 0 ? "Every toggle is off." : "No queues of the shown kinds.");
        ImGui::EndChild();
        ui.tagItem("queues:list");
    }

    void upgradeAll(UiContext& ui) {
        // Every possible upgrade on every colony, with no cost limit (spec 06 §1.8.2).
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        std::vector<std::pair<game::ObjectId, game::QueueItem>> work;
        // Facility items already queued move to the newest level too, keeping
        // their counts and what was paid (spec 02 §6.6).
        std::vector<game::cmd::QueueReplaceFacility> switches;
        for (const auto& c : s.colonies) {
            if (!c || c->owner != me) continue;
            for (const auto& [index, facility] : queuedFacilitySwitches(ui.rules(), s, me, *c)) switches.push_back({{c->planet, {}}, index, facility});
            if (c->totalPopulation() == 0) continue;
            for (const game::QueueItem& item : possibleUpgrades(ui.rules(), s, me, *c)) work.emplace_back(c->planet, item);
        }
        if (work.empty() && switches.empty()) {
            status_.info("No facilities can be upgraded right now.");
            return;
        }
        int switched = 0;
        for (const game::cmd::QueueReplaceFacility& c : switches) switched += status_.issue(ui, c) ? 1 : 0;
        int done = 0;
        std::vector<game::ObjectId> planets;
        for (const auto& [planet, item] : work)
            if (status_.issue(ui, game::cmd::QueueAdd{{planet, {}}, item, -1})) {
                ++done;
                if (std::find(planets.begin(), planets.end(), planet) == planets.end()) planets.push_back(planet);
            }
        if (done == static_cast<int>(work.size()) && switched == static_cast<int>(switches.size())) {
            std::string text = std::format("Queued {} upgrade{} on {} colon{}", done, done == 1 ? "" : "s", planets.size(), planets.size() == 1 ? "y" : "ies");
            if (switched > 0) text += std::format("; {} queued facilit{} moved to the newest level", switched, switched == 1 ? "y" : "ies");
            status_.info(text);
        }
    }

    // "Select Planet" for Scrap Facilities, then the scrap checklist of the planet picked.
    void pickerPopup(UiContext& ui) {
        const char* id = "Select Planet###scrappick";
        if (picking_) {
            ImGui::OpenPopup(id);
            picking_ = false;
        }
        if (!beginModal(ui, id, {420, 520})) return;
        const game::GameState& s = ui.state();
        const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
        ImGui::BeginChild("##pick", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        for (game::ObjectId p : pickRows_) {
            const game::SpaceObject& o = s.galaxy.object(p);
            const std::string label = std::format("{}   ({})###p{}", o.name, s.galaxy.system(o.system).name, p.index());
            if (ImGui::Selectable(label.c_str(), picked_ == p)) picked_ = p;
        }
        if (pickRows_.empty()) ImGui::TextColored(kTextDim, "No colonies are listed.");
        ImGui::EndChild();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginDisabled(!picked_);
        const bool ok = ImGui::Button("Select", ImVec2(w, ui.px(26)));
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", ImVec2(w, ui.px(26)));
        if (ok && picked_) {
            ImGui::CloseCurrentPopup();
            scrapOne_.open(*picked_);
        } else if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void noticePopup(UiContext& ui) {
        const char* id = "Multi-Add###multiaddnotice";
        if (notice_) {
            ImGui::OpenPopup(id);
            notice_ = false;
        }
        ImGui::SetNextWindowPos(ui.at({kFrameW * 0.5f, kFrameH * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size({380, 0}), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags))
            return;
        ImGui::TextWrapped("%s", noticeText_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    std::vector<QueueRow> rows_;
    uint64_t revision_ = 0;
    bool restored_ = false;
    QueueTab tab_ = QueueTab::Rate;
    uint8_t shown_ = 0x0f;
    SortHistory sort_{QcName};
    std::vector<game::cmd::QueueTarget> tagged_;
    std::optional<game::SystemId> hovered_;
    int planetYards_ = 0, shipYards_ = 0, onHold_ = 0;
    game::Resources usage_;
    StatusLine status_;
    ReportPopup report_;
    ScrapFacilitiesPopup scrapOne_;
    bool picking_ = false;
    std::vector<game::ObjectId> pickRows_;
    std::optional<game::ObjectId> picked_;
    bool notice_ = false;
    std::string noticeText_;
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
        if (args.text == "multiadd") {
            // Multi-Add (spec 06 §1.8.2): an empty temporary queue whose items go to every tagged queue.
            multi_ = true;
            tagged_ = gMultiAddTargets;
        } else if (args.vehicle.valid()) {
            target_.vehicle = args.vehicle;
        } else if (args.planet.valid()) {
            target_.planet = args.planet;
        }
    }

    bool draw(UiContext& ui) override {
        if (!initialized_) {
            initialize(ui);
        }
        const game::GameState& s = ui.state();
        if (multi_) std::erase_if(tagged_, [&](const auto& t) { return !queueOf(s, ui.session.player(), t); });
        const game::ConstructionQueue* q = multi_ ? &temp_ : queueOf(s, ui.session.player(), target_);
        Dialog d(ui, "Set Construction Queue", DialogSize::Tall);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        if (!q || (multi_ && tagged_.empty())) {
            heading(ui, "No queue");
            ImGui::TextColored(kTextDim, "This queue no longer exists, or we have no colony that can build.");
            d.beginButtons();
            if (d.close()) return false;
            return d.keepOpen();
        }
        rate_ = multi_ ? game::Resources{} : game::economy::constructionRate(ui.rules(), s, ui.session.player(), target_);
        if (multi_) multiHeader(ui);
        else header(ui, *q);
        hoverAvail_.reset();
        hoverQueue_.reset();
        const float detailH = ui.px(150);
        const float listsH = ImGui::GetContentRegionAvail().y - detailH - ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y;
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginChild("##available", ImVec2(half, listsH));
        available(ui);
        ImGui::EndChild();
        ui.tagItem("set-queue:available");
        ImGui::SameLine();
        ImGui::BeginChild("##queue", ImVec2(0, listsH));
        queueList(ui, *q);
        ImGui::EndChild();
        ui.tagItem("set-queue:queue");
        ImGui::BeginChild("##detail", ImVec2(0, detailH), ImGuiChildFlags_Borders);
        detail(ui, *q);
        ImGui::EndChild();
        status_.draw(ui);

        d.beginButtons();
        const bool planet = !multi_ && !target_.vehicle.valid();
        static constexpr std::array<const char*, 4> kBuildTabIds{{"ships", "facilities", "units", "upgrades"}};
        for (const auto& [tab, label] : kBuildTabs) {
            if (lampButton(d, ui, label, tab_ == tab, tabEnabled(tab, planet))) tab_ = tab;
            ui.tagTab(kBuildTabIds[static_cast<size_t>(tab)], tab_ == tab);
        }
        if (lampButton(d, ui, "Only Latest", onlyLatest_, true, "Show only the newest level of each facility and no obsolete designs"))
            onlyLatest_ = !onlyLatest_;
        d.spacer();
        // Multi-Add disables the queue option buttons (spec 06 §1.8.2).
        const bool options = !multi_;
        game::cmd::QueueFlags flags{target_, q->onHold, q->repeat, q->emergency, q->autoWaypoint};
        const bool recovering = !q->emergency && q->slowTurns > 0;
        if (lampButton(d, ui, "Emergency Build", q->emergency, options && !recovering,
                       recovering ? "The yard is recovering from emergency construction"
                                  : "Build at 150% for up to 10 turns; afterwards the yard runs slow for as long")) {
            flags.emergency = !q->emergency;
            status_.issue(ui, flags);
        }
        if (lampButton(d, ui, "Repeat Build", q->repeat, options, "Keep rebuilding the top item")) {
            flags.repeat = !q->repeat;
            status_.issue(ui, flags);
        }
        if (lampButton(d, ui, "Queue On Hold", q->onHold, options, "Freeze the queue; nothing is spent")) {
            flags.onHold = !q->onHold;
            status_.issue(ui, flags);
        }
        d.spacer();
        if (d.button("Set Move To", options)) waypointPending_ = true;
        if (ImGui::IsItemHovered()) itemTooltip("New ships built here get orders to move to a waypoint");
        if (d.button("Clear Move To", options && q->autoWaypoint >= 0)) {
            flags.autoWaypoint = -1;
            status_.issue(ui, flags);
        }
        d.spacer();
        if (d.button("Fill Queue", options)) {
            templatesPending_ = true;
            chosenTemplate_ = -1;
        }
        if (d.button("Clear Queue", !q->items.empty())) {
            // Clearing deletes the first item too: the Empire Options ask first (inferred).
            if (!multi_ && ui.options().confirmDeleteFirstQueueItem) {
                confirmAction_ = Confirm::Clear;
                confirm_.open("Clear the whole queue? Progress on the item under construction is lost.");
            } else {
                clearQueue(ui);
            }
        }
        if (d.close()) {
            if (multi_) finishMultiAdd(ui);
            return false;
        }
        if (confirm_.draw(ui)) {
            if (confirmAction_ == Confirm::Clear) clearQueue(ui);
            else if (confirmAction_ == Confirm::RemoveTop) remove(ui, 0);
        }
        if (!multi_) {
            waypointPopup(ui, flags);
            if (const game::ConstructionQueue* now = queueOf(s, ui.session.player(), target_)) templatePopup(ui, *now);
        }
        return d.keepOpen();
    }

private:
    enum class Confirm { None, Clear, RemoveTop };

    // Multi-Add: Ships only when every tagged queue can build ships, Units
    // likewise; never Facilities or Upgrades (spec 06 §1.8.2).
    bool tabEnabled(BuildTab tab, bool planet) const {
        if (!multi_) return planet || tab == BuildTab::Ships || tab == BuildTab::Units;
        if (tab == BuildTab::Ships) return multiShips_;
        if (tab == BuildTab::Units) return multiUnits_;
        return false;
    }

    void multiHeader(UiContext& ui) {
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        ImGui::TextUnformatted("Multi-Add");
        ImGui::PopFont();
        ImGui::TextColored(kTextDim, "When this window closes, every item placed in the queue is added, in order and with its count,");
        ImGui::TextColored(kTextDim, "to each of the %zu tagged queues.", tagged_.size());
        ImGui::Separator();
    }

    // Appends the temporary queue's items to every tagged queue.
    void finishMultiAdd(UiContext& ui) {
        if (temp_.items.empty()) return;
        int done = 0, failed = 0;
        std::string reason;
        for (const game::cmd::QueueAdd& c : multiAddCommands(tagged_, temp_.items)) {
            const game::CommandResult res = ui.session.issue(c);
            if (res.ok) ++done;
            else {
                ++failed;
                if (reason.empty()) reason = res.error;
            }
        }
        gMultiAddFailed = failed > 0;
        gMultiAddReport = failed == 0 ? std::format("Added {} item{} to {} queue{}.", temp_.items.size(), temp_.items.size() == 1 ? "" : "s",
                                                    tagged_.size(), tagged_.size() == 1 ? "" : "s")
                                      : std::format("Added {} of {} items; {} could not be added: {}", done, done + failed, failed, reason);
    }

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
        // The Empire Options' "only latest items for construction" (spec 06 §1.9).
        onlyLatest_ = ui.options().latestConstructionOnly;
        if (multi_) {
            multiShips_ = multiUnits_ = !tagged_.empty();
            for (const auto& t : tagged_) {
                multiShips_ = multiShips_ && queueCanBuild(ui.rules(), s, me, t, false);
                multiUnits_ = multiUnits_ && queueCanBuild(ui.rules(), s, me, t, true);
            }
            tab_ = multiShips_ || !multiUnits_ ? BuildTab::Ships : BuildTab::Units;
            return;
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
            // Waypoints are numbered 0-9, as in the Waypoints window and the hotkeys.
            moveTo = w.set ? std::format("{} ({})", w.name.empty() ? std::format("Waypoint {}", q.autoWaypoint) : w.name, sectorName(s, w.location, ui.session.player()))
                           : std::format("Waypoint {} (not set)", q.autoWaypoint);
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
            b.problem = itemProblem(ui, it);
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
                cellText(ui, ok && !multi_ ? turnsText(b.turns) : "-", kTextDim);
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
                cellText(ui, multi_ ? std::string("-") : turnsText(est[i].doneIn), kTextDim);
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
                // The Empire Options' "confirm deleting the first item of a construction queue" (spec 06 §1.9).
                if (i == 0 && !multi_ && ui.options().confirmDeleteFirstQueueItem) {
                    confirmAction_ = Confirm::RemoveTop;
                    confirm_.open(topHasProgress ? std::format("Remove {}? Its construction progress is lost.", topName)
                                                 : std::format("Remove {}, the first item of the queue?", topName));
                } else {
                    remove(ui, i);
                }
                break;
            // Batch sizes step by one up to 5, then by five.
            case Act::Fewer: setCount(ui, i, count > 5 ? count - 5 : count - 1); break;
            case Act::More: setCount(ui, i, count >= 5 ? count + 5 : count + 1); break;
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

    // Why the item cannot go in the queue; in Multi-Add, in some tagged queue.
    std::string itemProblem(UiContext& ui, const game::QueueItem& item) const {
        if (!multi_) return game::queueItemProblem(ui.rules(), ui.state(), ui.session.player(), target_, item);
        for (const auto& t : tagged_)
            if (std::string p = game::queueItemProblem(ui.rules(), ui.state(), ui.session.player(), t, item); !p.empty()) return p;
        return {};
    }

    void add(UiContext& ui, const Buildable& b) {
        if (!b.problem.empty()) {
            status_.fail(std::format("{}: {}", b.name, b.problem));
            return;
        }
        if (multi_) {
            temp_.items.push_back(b.item);
            status_.info(std::format("Placed {}", b.name));
            return;
        }
        if (!status_.issue(ui, game::cmd::QueueAdd{target_, b.item, -1})) return;
        status_.info(std::format("Queued {}", b.name));
        // The Empire Options' "note when similar system-wide abilities exist"
        // (spec 06 §1.9): another facility of ours in the system has the same
        // system-wide ability (inferred: what counts as similar).
        if (b.item.kind == game::QueueItem::Kind::Facility && !target_.vehicle.valid() && ui.options().noteSimilarAbilities) {
            const auto same = similarSystemAbilities(ui.rules(), ui.state(), ui.session.player(), target_.planet, b.item.facility);
            if (!same.empty()) {
                std::string list;
                for (const std::string& a : same) list += (list.empty() ? "" : ", ") + a;
                status_.info(std::format("Queued {}. A colony of ours in this system already has: {}.", b.name, list));
            }
        }
    }

    void move(UiContext& ui, size_t from, size_t to) {
        if (multi_) {
            if (from >= temp_.items.size() || to >= temp_.items.size()) return;
            const game::QueueItem item = temp_.items[from];
            temp_.items.erase(temp_.items.begin() + std::ptrdiff_t(from));
            temp_.items.insert(temp_.items.begin() + std::ptrdiff_t(to), item);
            selected_ = to;
            return;
        }
        if (status_.issue(ui, game::cmd::QueueMove{target_, static_cast<uint32_t>(from), static_cast<uint32_t>(to)})) selected_ = to;
    }

    void remove(UiContext& ui, size_t index) {
        if (multi_) {
            if (index < temp_.items.size()) temp_.items.erase(temp_.items.begin() + std::ptrdiff_t(index));
            selected_.reset();
            return;
        }
        if (status_.issue(ui, game::cmd::QueueRemove{target_, static_cast<uint32_t>(index)})) selected_.reset();
    }

    void setCount(UiContext& ui, size_t index, int count) {
        if (multi_) {
            if (index < temp_.items.size()) temp_.items[index].count = std::max(1, count);
            return;
        }
        status_.issue(ui, game::cmd::QueueSetCount{target_, static_cast<uint32_t>(index), count});
    }

    void clearQueue(UiContext& ui) {
        if (multi_) {
            temp_.items.clear();
            selected_.reset();
            return;
        }
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
            // Numbered 0-9, as in the Waypoints window and the hotkeys.
            const std::string label = w.set ? std::format("{}. {} - {}", i, w.name.empty() ? "Waypoint" : w.name, sectorName(ui.state(), w.location, ui.session.player()))
                                             : std::format("{}. (not set)", i);
            if (ImGui::Selectable(label.c_str(), flags.autoWaypoint == static_cast<int>(i), w.set ? 0 : ImGuiSelectableFlags_Disabled)) {
                flags.autoWaypoint = static_cast<int>(i);
                if (status_.issue(ui, flags)) status_.info(std::format("New ships will move to waypoint {}", i));
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
    // Multi-Add: the tagged queues, the temporary queue, and the tabs allowed.
    bool multi_ = false;
    std::vector<game::cmd::QueueTarget> tagged_;
    game::ConstructionQueue temp_;
    bool multiShips_ = false, multiUnits_ = false;
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
