// Scrap \ Analyze \ Mothball \ Self-Destruct (order G, docs/spec/06 §1.3,
// spec 03 §14-15, spec 02 §6): the own vehicles in a sector, toggled into a
// selection, with what scrapping, unmothballing and the other actions would
// give or cost. With an own colony in the sector a Facilities tab lists its
// facilities for Scrap Facilities (OpenSE4's own; the window opens on its
// vehicles, and on that tab when opened with kFacilitiesTab).

#include "client/classic/reports.hpp"
#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"
#include "client/classic/status_icons.hpp"

#include "game/design.hpp"
#include "game/query.hpp"
#include "game/scrap.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

std::string plural(int n, std::string_view one, std::string_view many) { return std::format("{} {}", n, n == 1 ? one : many); }

// The Retrofit picker, "Select New Design" (spec 03 §14, confirmed: binary):
// a row per design with its picture, Name, Size and the retrofit's cost in
// three resource columns; a left-click picks it, a right-click opens its
// Design Report. A row the retrofit would refuse now says why when hovered
// (OpenSE4's); it can be picked all the same, as in the original.
class RetrofitPicker {
public:
    struct Row {
        game::DesignId design;
        std::string size;
        game::Resources cost;
        std::string problem;
    };
    void open(std::vector<Row> rows) {
        rows_ = std::move(rows);
        request_ = true;
    }
    std::optional<game::DesignId> draw(UiContext& ui) {
        const char* id = "Select New Design###retrofitpicker";
        if (request_) {
            ImGui::OpenPopup(id);
            request_ = false;
        }
        if (!beginModal(ui, id, {520, 440})) return std::nullopt;
        const game::GameState& s = ui.state();
        std::optional<game::DesignId> chosen;
        const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("##designs", 6, flags, ImVec2(0, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ui.px(36));
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ui.px(96));
            for (const char* res : {"Min", "Org", "Rad"}) ImGui::TableSetupColumn(res, ImGuiTableColumnFlags_WidthFixed, ui.px(56));
            ImGui::TableHeadersRow();
            for (const Row& row : rows_) {
                if (!row.design.valid() || row.design.index() >= s.designs.size()) continue;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, ui.px(36));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(row.design.value));
                if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, ui.px(34)))) chosen = row.design;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) report_.open(row.design);
                if (ImGui::IsItemHovered() && !row.problem.empty()) ImGui::SetTooltip("%s", row.problem.c_str());
                ImGui::SameLine(0, 0);
                image(ui, designMini(ui, row.design), {32, 32});
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(s.design(row.design).name.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(row.size.c_str());
                for (size_t i = 0; i < 3; ++i) {
                    ImGui::TableSetColumnIndex(static_cast<int>(3 + i));
                    ImGui::TextUnformatted(formatNumber(row.cost.v[i]).c_str());
                }
            }
            ImGui::EndTable();
        }
        if (rows_.empty()) ImGui::TextColored(kDim, "No design of ours with this hull can be built now.");
        const bool reportOpen = ImGui::IsPopupOpen("Design Report###designreport");
        if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) || (!reportOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            ImGui::CloseCurrentPopup();
        if (chosen) ImGui::CloseCurrentPopup();
        report_.draw(ui);
        ImGui::EndPopup();
        return chosen;
    }

private:
    std::vector<Row> rows_;
    bool request_ = false;
    DesignReportPopup report_;
};

class ScrapScreen final : public Screen {
public:
    explicit ScrapScreen(const ScreenArgs& args)
        : planet_(args.planet), anchor_(args.vehicle), facilities_(args.text == kFacilitiesTab && args.planet.valid()) {
        // A tagged group opens the window on its sector (spec 06 §7 Q52).
        if (!args.vehicle.valid() && !args.planet.valid() && args.location) where_ = args.location;
    }

    bool draw(UiContext& ui) override {
        locate(ui);
        Dialog d(ui, facilities_ ? "Scrap Facilities###scrap" : "Scrap \\ Analyze \\ Mothball \\ Self-Destruct###scrap", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Colony* colony = where_ ? coloniesHere(ui) : nullptr;
        if (!colony) facilities_ = false;
        // Only listed vehicles stay selected: none in a fleet, none cloaked
        // (spec 03 §15, §19 Q74).
        std::erase_if(selected_, [&](game::VehicleId id) {
            const game::Vehicle* v = ownVehicle(ui, id);
            return !v || !where_ || v->location != *where_ || !listed(*v);
        });
        if (colony) std::erase_if(slots_, [&](size_t i) { return i >= colony->facilities.size(); });

        d.beginContent();
        if (!where_) ImGui::TextColored(kDim, "You have no ships.");
        else if (facilities_) facilitiesContent(ui, *colony);
        else vehiclesContent(ui);

        d.beginButtons();
        if (colony) {
            if (d.tab("Vehicles", !facilities_)) facilities_ = false;
            if (d.tab("Facilities", facilities_)) facilities_ = true;
            d.spacer();
        }
        if (facilities_) facilityButtons(ui, d, *colony);
        else vehicleButtons(ui, d);
        if (d.close()) return false;
        popups(ui);
        return d.keepOpen();
    }

private:
    // The sector: the given vehicle's, else the given colony's, else the
    // first own ship's. The window opens with nothing selected (spec 06 §1.3).
    void locate(UiContext& ui) {
        if (where_) return;
        if (const game::Vehicle* v = ownVehicle(ui, anchor_)) where_ = v->location;
        if (!where_ && ownColony(ui, planet_)) where_ = game::locationOf(ui.state().galaxy, planet_);
        if (!where_)
            if (const game::Vehicle* v = ownVehicle(ui, firstOwnShip(ui))) where_ = v->location;
    }

    const game::Colony* coloniesHere(const UiContext& ui) {
        const auto colonies = ownColoniesAt(ui, *where_);
        if (colonies.empty()) return nullptr;
        for (const game::Colony* c : colonies)
            if (c->planet == planet_) return c;
        planet_ = colonies.front()->planet;
        return colonies.front();
    }

    // The window lists only own vehicles in the sector that are in no fleet
    // and not cloaked (spec 03 §15, §19 Q74, confirmed: binary).
    static bool listed(const game::Vehicle& v) { return !v.fleet.valid() && v.status != game::VehicleStatus::Cloaked; }

    // The listed vehicles in the window's order (Order By Name or By Class).
    std::vector<const game::Vehicle*> listedVehicles(const UiContext& ui) const {
        std::vector<const game::Vehicle*> out;
        if (!where_) return out;
        for (const game::Vehicle* v : ownVehiclesAt(ui, *where_))
            if (listed(*v)) out.push_back(v);
        sortScrapList(ui.state(), out, byClass_);
        return out;
    }

    // The selected vehicles in the window's list order: the actions go to
    // them one after another in that order (spec 03 §15).
    std::vector<const game::Vehicle*> selection(const UiContext& ui) const {
        std::vector<const game::Vehicle*> out;
        for (const game::Vehicle* v : listedVehicles(ui))
            if (isSelected(v->id)) out.push_back(v);
        return out;
    }

    bool isSelected(game::VehicleId id) const { return std::find(selected_.begin(), selected_.end(), id) != selected_.end(); }

    // ---- Vehicles ----

    void vehiclesContent(UiContext& ui) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        ImGui::TextColored(kDim, "Location: %s", sectorName(s, *where_, ui.session.player()).c_str());
        const float listW = ImGui::GetContentRegionAvail().x * 0.50f;
        const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f;
        beginPanel(ui, "##vehicles", "Selected Vehicles", ImVec2(listW, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const game::Vehicle* v : listedVehicles(ui)) {
            RowStyle st;
            st.lamp = isSelected(v->id) ? Lamp::On : Lamp::Off;
            // "<design> Class" under the name, and the status icons (spec 06 §1.3).
            const bool unit = isUnitVehicle(r, s, *v);
            const std::string detail = s.design(v->design).name + " Class";
            const RowClick c = row(ui, static_cast<int>(v->id.value), unit ? unitMini(ui, *v) : vehicleMini(ui, *v),
                                   v->count > 1 ? std::format("{} (x{})", v->name, v->count) : v->name, detail, st);
            const ImVec2 corner{ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y};
            const std::vector<int> cells = vehicleStatusCells(r, s, *v);
            for (size_t i = 0; i < cells.size() && i < 6; ++i) {
                const Sprite icon = ui.art.statusIcon(cells[i] + 1);
                if (!icon) continue;
                const float x = corner.x - ui.px(18) * float(i + 1) - ui.px(4), y = corner.y + ui.px(2);
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(icon.tex.value)), {x, y}, {x + ui.px(16), y + ui.px(16)},
                             {icon.uv.min.x, icon.uv.min.y}, {icon.uv.max.x, icon.uv.max.y});
            }
            if (c.left) {
                if (isSelected(v->id)) std::erase(selected_, v->id);
                else selected_.push_back(v->id);
            }
            if (c.right) report_.vehicle(v->id);
        }
        endPanel(ui, "Click a vehicle to toggle it.");
        ImGui::SameLine(0, ui.px(20));

        const auto sel = selection(ui);
        const ScrapWindowState state = scrapWindowState(r, s, ui.session.player(), sel);
        ImGui::BeginGroup();
        heading(ui, "Statistics");
        ImGui::Separator();
        constexpr float col = 170;
        // The sums count only while their button is lit (spec 06 §1.3).
        ImGui::TextColored(kLabelBlue, "Scrap Value");
        ImGui::SameLine(ui.px(col));
        resources(ui, state.scrapValue, true);
        // The last selected vehicle's words (spec 03 §15, spec 06 §1.3).
        labelValue(ui, "Research Potential", std::string(state.researchPotential), col);
        labelValue(ui, "Status", state.status.empty() ? std::string("-") : std::string(state.status), col);
        ImGui::TextColored(kLabelBlue, "Cost to Unmothball");
        ImGui::SameLine(ui.px(col));
        resources(ui, state.unmothballCost, true);
        const bool none = sel.empty();
        labelValue(ui, "Can Self-Destruct", none ? "-" : state.canSelfDestruct ? "Yes" : "No", col);
        labelValue(ui, "Can Be Fired On", none ? "-" : state.canBeFiredOn ? "Yes" : "No", col);
        labelValue(ui, "Space Yard In Sector", game::scrapYardAt(r, s, ui.session.player(), *where_) ? "Yes" : "No", col);
        ImGui::Dummy(ImVec2(0, ui.px(16)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        // Two fixed note lines (spec 06 §1.3), in our words.
        ImGui::TextColored(kDim, "%s", "Scrap, Analyze, Retrofit, Mothball and Unmothball need a space yard in the sector.");
        ImGui::TextColored(kDim, "%s", "Ships in a fleet and cloaked ships are not listed.");
        ImGui::Dummy(ImVec2(0, ui.px(8)));
        status_.draw(ui);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }

    // Each button is lit only when every selected vehicle qualifies (spec 06
    // §1.3, spec 03 §15, confirmed: binary). Order By Name and Order By Class
    // refill the list and drop the selection; Select All and Select None.
    void vehicleButtons(UiContext& ui, Dialog& d) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const auto sel = selection(ui);
        const ScrapWindowState state = scrapWindowState(r, s, ui.session.player(), sel);
        if (d.button("Scrap", state.scrap)) {
            ask(ui, Action::Scrap,
                std::format("Scrap {} for their raw materials? The empire gets back {} minerals, {} organics and {} radioactives.",
                            plural(static_cast<int>(sel.size()), "vehicle", "vehicles"), formatNumber(state.scrapValue.v[0]),
                            formatNumber(state.scrapValue.v[1]), formatNumber(state.scrapValue.v[2])));
        }
        if (d.button("Analyze", state.analyze))
            ask(ui, Action::Analyze, "Take the selected vehicles apart to study their technology? They are lost, and nothing is refunded.");
        if (d.button("Retrofit", state.retrofit)) openRetrofit(ui, sel);
        if (d.button("Mothball", state.mothball)) perform(ui, Action::Mothball);
        if (d.button("Unmothball", state.unmothball)) perform(ui, Action::Unmothball);
        if (d.button("Self-Destruct", state.selfDestruct)) ask(ui, Action::SelfDestruct, "Order the selected vehicles to destroy themselves?");
        if (d.button("Fire On", state.fireOn))
            ask(ui, Action::FireOn, "Order our own armed ships here to fire on the selected vehicles and destroy them?");
        d.spacer();
        if (d.tab("Order By Name", !byClass_) && byClass_) {
            byClass_ = false;
            selected_.clear();
        }
        if (d.tab("Order By Class", byClass_) && !byClass_) {
            byClass_ = true;
            selected_.clear();
        }
        const std::vector<const game::Vehicle*> all = listedVehicles(ui);
        if (d.button("Select All", !all.empty())) {
            selected_.clear();
            for (const game::Vehicle* v : all) selected_.push_back(v->id);
        }
        if (d.button("Select None", !selected_.empty())) selected_.clear();
    }

    void openRetrofit(UiContext& ui, const std::vector<const game::Vehicle*>& sel) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const game::Vehicle& first = *sel.front();
        std::vector<RetrofitPicker::Row> rows;
        for (game::DesignId id : retrofitChoices(r, s, ui.session.player(), first)) {
            RetrofitPicker::Row row;
            row.design = id;
            const ruleset::VehicleSize& hull = r.hull(s.design(id).hull);
            row.size = hull.shortName.empty() ? hull.name : hull.shortName;
            row.problem = game::retrofitProblem(r, s, ui.session.player(), first, id, &row.cost);
            rows.push_back(std::move(row));
        }
        retrofit_.open(std::move(rows));
    }

    // ---- Facilities ----

    void facilitiesContent(UiContext& ui, const game::Colony& c) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        ImGui::TextColored(kDim, "Planet: %s", s.galaxy.object(c.planet).name.c_str());
        const float listW = ImGui::GetContentRegionAvail().x * 0.52f;
        const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f;
        beginPanel(ui, "##facilities", "Facilities", ImVec2(listW, h));
        for (size_t i = 0; i < c.facilities.size(); ++i) {
            const ruleset::Facility& f = r.facility(c.facilities[i]);
            const bool on = std::find(slots_.begin(), slots_.end(), i) != slots_.end();
            RowStyle st;
            st.lamp = on ? Lamp::On : Lamp::Off;
            const game::Resources v = facilityScrapValue(r, s, c, i);
            const RowClick rc = row(ui, static_cast<int>(i), ui.art.facility(f.picture), f.name,
                                    std::format("Scrap value {} / {} / {}", formatNumber(v.v[0]), formatNumber(v.v[1]), formatNumber(v.v[2])), st);
            if (rc.left) {
                if (on) std::erase(slots_, i);
                else slots_.push_back(i);
            }
        }
        if (c.facilities.empty()) ImGui::TextColored(kDim, "No facilities.");
        endPanel(ui, "Click a facility to toggle it.");
        ImGui::SameLine(0, ui.px(20));
        game::Resources value;
        for (size_t i : slots_) value += facilityScrapValue(r, s, c, i);
        ImGui::BeginGroup();
        heading(ui, "Statistics");
        ImGui::Separator();
        constexpr float col = 150;
        labelValue(ui, "Facilities", std::format("{} / {}", c.facilities.size(), game::facilitySlots(r, s, c)), col);
        labelValue(ui, "Selected", std::to_string(slots_.size()), col);
        ImGui::TextColored(kLabelBlue, "Scrap Value");
        ImGui::SameLine(ui.px(col));
        resources(ui, value, true);
        ImGui::Dummy(ImVec2(0, ui.px(16)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui.px(300));
        ImGui::TextColored(kDim, "Scrapped facilities return part of their cost at once, more with resource reclamation in the sector.");
        ImGui::Dummy(ImVec2(0, ui.px(8)));
        status_.draw(ui);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }

    void facilityButtons(UiContext& ui, Dialog& d, const game::Colony& c) {
        // The facility check list never asks, whatever "confirm scrapping" says (spec 06 §7 Q46).
        if (d.button("Scrap Facilities", !slots_.empty())) perform(ui, Action::ScrapFacilities);
        (void)c;
        if (d.button("Select All", !c.facilities.empty())) {
            slots_.clear();
            for (size_t i = 0; i < c.facilities.size(); ++i) slots_.push_back(i);
        }
        if (d.button("Select None", !slots_.empty())) slots_.clear();
    }

    // ---- Popups ----

    enum class Action { Scrap, Analyze, Mothball, Unmothball, SelfDestruct, FireOn, ScrapFacilities };

    // Scrap, Analyze, Self-Destruct and Fire On ask a "Confirm Action" Yes/No
    // first while the Empire Options' "confirm scrapping" is on; otherwise
    // they act at once. Retrofit, Mothball, Unmothball and the facility check
    // lists never ask (spec 06 §7 Q46).
    void ask(UiContext& ui, Action a, std::string text) {
        if (ui.options().confirmScrap) {
            confirmAction_ = a;
            confirm_.open("Confirm Action", std::move(text));
        } else {
            perform(ui, a);
        }
    }

    // Gives the action to every selected vehicle, one after another in list
    // order. A turn-based game carries each out at once, testing it again
    // (so one can change the next: the last armed vehicle cannot be fired
    // on); a simultaneous game leaves it as each vehicle's only order (spec 03
    // §15). Vehicles that fail are left alone.
    void act(UiContext& ui, game::ScrapAction a, game::DesignId design = {}) {
        std::vector<game::VehicleId> ids;
        for (const game::Vehicle* v : selection(ui)) ids.push_back(v->id);
        int done = 0;
        std::string why;
        for (game::VehicleId id : ids) {
            const game::CommandResult res = ui.session.issue(scrapCommand(a, id, design));
            if (res.ok) ++done;
            else if (why.empty()) why = res.error;
        }
        if (done == 0) {
            if (!why.empty()) status_.error(why);
            return;
        }
        static constexpr std::string_view kDone[] = {"scrapped", "analyzed", "mothballed", "back in service", "retrofitted", "destroyed", "destroyed"};
        static constexpr std::string_view kLater[] = {"be scrapped", "be analyzed", "be mothballed", "be unmothballed", "be retrofitted",
                                                      "self-destruct", "be fired on"};
        const size_t i = static_cast<size_t>(a);
        const std::string vehicles = plural(done, "vehicle", "vehicles");
        status_.ok(ui.state().options.simultaneous ? std::format("{} will {} when {} first {} this turn.", vehicles, kLater[i], done == 1 ? "it" : "they",
                                                                  done == 1 ? "acts" : "act")
                                                   : std::format("{} {}.", vehicles, kDone[i]));
    }

    static game::Command scrapCommand(game::ScrapAction a, game::VehicleId id, game::DesignId design) {
        switch (a) {
            case game::ScrapAction::Scrap: return game::cmd::Scrap{id, {}, -1};
            case game::ScrapAction::Analyze: return game::cmd::Analyze{id};
            case game::ScrapAction::Mothball: return game::cmd::Mothball{id, true};
            case game::ScrapAction::Unmothball: return game::cmd::Mothball{id, false};
            case game::ScrapAction::Retrofit: return game::cmd::Retrofit{id, design};
            case game::ScrapAction::SelfDestruct: return game::cmd::SelfDestruct{id};
            case game::ScrapAction::FireOn: return game::cmd::FireOn{id};
        }
        return game::cmd::Scrap{id, {}, -1};
    }

    void perform(UiContext& ui, Action a) {
        switch (a) {
            case Action::Scrap: act(ui, game::ScrapAction::Scrap); break;
            case Action::Analyze: act(ui, game::ScrapAction::Analyze); break;
            case Action::Mothball: act(ui, game::ScrapAction::Mothball); break;
            case Action::Unmothball: act(ui, game::ScrapAction::Unmothball); break;
            case Action::SelfDestruct: act(ui, game::ScrapAction::SelfDestruct); break;
            case Action::FireOn: act(ui, game::ScrapAction::FireOn); break;
            case Action::ScrapFacilities: {
                const game::Colony* c = ownColony(ui, planet_);
                if (!c) break;
                std::vector<size_t> slots = slots_;
                std::sort(slots.rbegin(), slots.rend());  // highest slot first keeps the others valid
                int done = 0;
                for (size_t i : slots)
                    if (status_.issue(ui, game::cmd::Scrap{{}, planet_, static_cast<int32_t>(i)})) ++done;
                slots_.clear();
                if (done > 0) status_.ok(std::format("{} facilit{} scrapped.", done, done == 1 ? "y" : "ies"));
                break;
            }
        }
    }

    void popups(UiContext& ui) {
        if (confirm_.draw(ui)) perform(ui, confirmAction_);
        if (auto design = retrofit_.draw(ui)) act(ui, game::ScrapAction::Retrofit, *design);
        report_.draw(ui);
    }

    std::optional<game::Location> where_;
    game::ObjectId planet_;
    game::VehicleId anchor_;
    bool facilities_ = false;
    bool byClass_ = false;   // Order By Class; Order By Name is the default
    std::vector<game::VehicleId> selected_;
    std::vector<size_t> slots_;
    Status status_;
    Confirm confirm_;
    Action confirmAction_ = Action::Scrap;
    RetrofitPicker retrofit_;
    shipui::ReportPopup report_;
};

// ---- Abandon Planet -------------------------------------------------------------------------------

// The Abandon Planet order (Ctrl+A, spec 06 §1.3, §2.8; spec 02 §5): the
// player confirms; the order is refused when more people live there than
// Settings allow; otherwise, when the planet has facilities, the player
// chooses whether to scrap them for a refund or leave them for a later owner.
// The answer becomes the colony's Abandon order (cmd::AbandonPlanet): a
// turn-based game carries it out at once, a simultaneous one during the
// turn's movement, and until then it shows in the colony's orders.
class AbandonPlanetScreen final : public Screen {
public:
    explicit AbandonPlanetScreen(const ScreenArgs& args) : planet_(args.planet) {}
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        const game::Colony* c = ownColony(ui, planet_);
        if (!c && step_ != Step::Notice) return false;
        const std::string name = ui.state().galaxy.object(planet_).name;
        switch (step_) {
            case Step::Confirm:
                if (const auto a = yesNoBox(ui, "Abandon Planet###abandon1",
                                            std::format("Abandon {}? Its people leave.", name))) {
                    if (!*a) return false;
                    const int64_t limit = ui.rules().setting("Maximum Population For Abandon Planet Order", 50);
                    if (c->totalPopulation() > limit) {
                        notice_ = std::format("{} cannot be abandoned: more than {}M people live there.", name, limit);
                        step_ = Step::Notice;
                    } else if (!c->facilities.empty()) {
                        step_ = Step::Scrap;
                    } else {
                        abandon(ui, false);
                    }
                }
                break;
            case Step::Scrap:
                if (const auto a = yesNoBox(ui, "Abandon Planet###abandon2",
                                            std::format("Scrap the {} facilit{} on {} first? Scrapping returns part of their cost now; "
                                                        "otherwise they stay, and the planet stays your colony with nobody living there.",
                                                        c->facilities.size(), c->facilities.size() == 1 ? "y" : "ies", name)))
                    abandon(ui, *a);
                break;
            case Step::Notice:
                if (noticeBox(ui, "Abandon Planet###abandon3", notice_)) return false;
                break;
            case Step::Done: break;
        }
        return step_ != Step::Done;
    }

private:
    enum class Step { Confirm, Scrap, Notice, Done };

    void abandon(UiContext& ui, bool scrap) {
        const game::CommandResult r = ui.session.issue(game::cmd::AbandonPlanet{planet_, scrap});
        if (r.ok) {
            step_ = Step::Done;
        } else {
            notice_ = r.error;
            step_ = Step::Notice;
        }
    }

    // A small message box in the middle of the frame (spec 06 §3.4 keys).
    static std::optional<bool> yesNoBox(UiContext& ui, const char* id, const std::string& text) {
        std::optional<bool> answer;
        if (!beginBox(ui, id)) return answer;
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::Spacing();
        const std::optional<bool> key = yesNoKey();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Yes", ImVec2(w, ui.px(26))) || key == true) answer = true;
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(w, ui.px(26))) || key == false) answer = false;
        ImGui::End();
        return answer;
    }

    static bool noticeBox(UiContext& ui, const char* id, const std::string& text) {
        if (!beginBox(ui, id)) return false;
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::Spacing();
        const bool ok = ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey();
        ImGui::End();
        return ok;
    }

    static bool beginBox(UiContext& ui, const char* id) {
        ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size({420, 0}), ImGuiCond_Always);
        const bool open = ImGui::Begin(id, nullptr,
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags | ui.windowFlags());
        if (!open) {
            ImGui::End();
            return false;
        }
        if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
        ui.tagWindow(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
        return true;
    }

    game::ObjectId planet_;
    Step step_ = Step::Confirm;
    std::string notice_;
};

} // namespace

std::unique_ptr<Screen> makeScrap(const ScreenArgs& args) { return std::make_unique<ScrapScreen>(args); }
std::unique_ptr<Screen> makeAbandonPlanet(const ScreenArgs& args) { return std::make_unique<AbandonPlanetScreen>(args); }

} // namespace opense4::client::classic
