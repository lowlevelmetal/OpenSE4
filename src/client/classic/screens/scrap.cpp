// Scrap \ Analyze \ Mothball \ Self-Destruct (order G, docs/spec/06 §1.3,
// spec 03 §14-15, spec 02 §6): the own vehicles in a sector, toggled into a
// selection, with what scrapping, unmothballing and the other actions would
// give or cost. With an own colony in the sector a Facilities tab lists its
// facilities for Scrap Facilities.

#include "client/classic/reports.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

void tooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

std::string yesNo(int yes, int total) { return yes == 0 ? "No" : yes == total ? "Yes" : std::format("{} of {}", yes, total); }

class ScrapScreen final : public Screen {
public:
    explicit ScrapScreen(const ScreenArgs& args) : planet_(args.planet), facilities_(!args.vehicle.valid() && args.planet.valid()) {
        if (args.vehicle.valid()) selected_.push_back(args.vehicle);
    }

    bool draw(UiContext& ui) override {
        locate(ui);
        Dialog d(ui, facilities_ ? "Scrap Facilities###scrap" : "Scrap \\ Analyze \\ Mothball \\ Self-Destruct###scrap", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Colony* colony = where_ ? coloniesHere(ui) : nullptr;
        if (!colony) facilities_ = false;
        std::erase_if(selected_, [&](game::VehicleId id) {
            const game::Vehicle* v = ownVehicle(ui, id);
            return !v || !where_ || v->location != *where_;
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
    void locate(UiContext& ui) {
        if (where_) return;
        if (!selected_.empty())
            if (const game::Vehicle* v = ownVehicle(ui, selected_.front())) where_ = v->location;
        if (!where_ && ownColony(ui, planet_)) where_ = game::locationOf(ui.state().galaxy, planet_);
        if (!where_)
            if (const game::Vehicle* v = ownVehicle(ui, firstOwnShip(ui))) {
                where_ = v->location;
                selected_.push_back(v->id);
            }
    }

    const game::Colony* coloniesHere(const UiContext& ui) {
        const auto colonies = ownColoniesAt(ui, *where_);
        if (colonies.empty()) return nullptr;
        for (const game::Colony* c : colonies)
            if (c->planet == planet_) return c;
        planet_ = colonies.front()->planet;
        return colonies.front();
    }

    std::vector<const game::Vehicle*> selection(const UiContext& ui) const {
        std::vector<const game::Vehicle*> out;
        for (game::VehicleId id : selected_)
            if (const game::Vehicle* v = ownVehicle(ui, id)) out.push_back(v);
        return out;
    }

    bool isSelected(game::VehicleId id) const { return std::find(selected_.begin(), selected_.end(), id) != selected_.end(); }

    // ---- Vehicles ----

    void vehiclesContent(UiContext& ui) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        ImGui::TextColored(kDim, "Location: %s", sectorName(s, *where_, ui.session.player()).c_str());
        const float listW = ImGui::GetContentRegionAvail().x * 0.46f;
        const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f;
        beginPanel(ui, "##vehicles", "Selected Vehicles", ImVec2(listW, h));
        for (const game::Vehicle* v : ownVehiclesAt(ui, *where_)) {
            RowStyle st;
            st.lamp = isSelected(v->id) ? Lamp::On : Lamp::Off;
            std::string detail = groupDesigns(s, *v, 2);
            if (v->status == game::VehicleStatus::Mothballed) detail += ", mothballed";
            const RowClick c = row(ui, static_cast<int>(v->id.value), isUnitVehicle(r, s, *v) ? unitMini(ui, *v) : vehicleMini(ui, *v),
                                   v->count > 1 ? std::format("{} (x{})", v->name, v->count) : v->name, detail, st);
            if (c.left) {
                if (isSelected(v->id)) std::erase(selected_, v->id);
                else selected_.push_back(v->id);
            }
            if (c.right) report_.vehicle(v->id);
        }
        endPanel(ui, "Click a vehicle to toggle it.");
        ImGui::SameLine(0, ui.px(20));

        const auto sel = selection(ui);
        game::Resources value, unmothball;
        int mothballed = 0, normal = 0, cloaked = 0, destructible = 0, fireable = 0;
        for (const game::Vehicle* v : sel) {
            value += scrapValue(r, s, *v);
            if (v->status == game::VehicleStatus::Mothballed) {
                ++mothballed;
                unmothball += unmothballCost(r, s, *v);
            } else if (v->status == game::VehicleStatus::Cloaked) {
                ++cloaked;
            } else {
                ++normal;
            }
            if (selfDestructEntry(r, s, *v)) ++destructible;
            if (canBeFiredOn(r, s, *v, selected_)) ++fireable;
        }
        const int n = static_cast<int>(sel.size());
        const char* status = n == 0 ? "-" : mothballed == n ? "Mothballed" : cloaked == n ? "Cloaked" : normal == n ? "Normal" : "Mixed";
        ImGui::BeginGroup();
        heading(ui, "Statistics");
        ImGui::Separator();
        constexpr float col = 170;
        ImGui::TextColored(kLabelBlue, "Scrap Value");
        ImGui::SameLine(ui.px(col));
        resources(ui, value, true);
        labelValue(ui, "Research Potential", n == 0 ? "-" : researchPotentialLabel(researchPotential(r, s, ui.me(), sel)), col);
        labelValue(ui, "Status", status, col);
        ImGui::TextColored(kLabelBlue, "Cost to Unmothball");
        ImGui::SameLine(ui.px(col));
        resources(ui, unmothball, true);
        labelValue(ui, "Can Self-Destruct", n == 0 ? "-" : yesNo(destructible, n), col);
        labelValue(ui, "Can Be Fired On", n == 0 ? "-" : yesNo(fireable, n), col);
        labelValue(ui, "Space Yard In Sector", game::spaceYardAt(r, s, ui.session.player(), *where_) ? "Yes" : "No", col);
        ImGui::Dummy(ImVec2(0, ui.px(16)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui.px(330));
        ImGui::TextColored(kDim, "Scrapping, retrofitting and mothballing need a space yard in the sector. Self-destruct orders are "
                                 "carried out when the turn is processed.");
        ImGui::Dummy(ImVec2(0, ui.px(8)));
        status_.draw(ui);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }

    void vehicleButtons(UiContext& ui, Dialog& d) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const auto sel = selection(ui);
        const bool yard = where_ && game::spaceYardAt(r, s, ui.session.player(), *where_);
        const bool any = !sel.empty();
        const bool sameHull = any && std::all_of(sel.begin(), sel.end(), [&](const game::Vehicle* v) {
            return s.design(v->design).hull == s.design(sel.front()->design).hull;
        });
        const bool anyNormal = std::any_of(sel.begin(), sel.end(), [](const game::Vehicle* v) { return v->status != game::VehicleStatus::Mothballed; });
        const bool anyMothballed = std::any_of(sel.begin(), sel.end(), [](const game::Vehicle* v) { return v->status == game::VehicleStatus::Mothballed; });
        const bool anyDestruct = std::any_of(sel.begin(), sel.end(), [&](const game::Vehicle* v) { return selfDestructEntry(r, s, *v).has_value(); });

        if (d.button("Scrap", yard && any)) {
            game::Resources value;
            for (const game::Vehicle* v : sel) value += scrapValue(r, s, *v);
            confirmAction_ = Action::Scrap;
            confirm_.open("Scrap", std::format("Scrap {} vehicle{}? The empire gets back {} minerals, {} organics and {} radioactives.", sel.size(),
                                               sel.size() == 1 ? "" : "s", formatNumber(value.v[0]), formatNumber(value.v[1]), formatNumber(value.v[2])));
        }
        d.button("Analyze", false);
        tooltip("Analyzing captured technology is not supported by the engine yet.");
        if (d.button("Retrofit", yard && sameHull)) openRetrofit(ui, sel);
        if (d.button("Mothball", yard && anyNormal)) {
            int done = 0;
            for (const game::Vehicle* v : sel)
                if (v->status != game::VehicleStatus::Mothballed && status_.issue(ui, game::cmd::Mothball{v->id, true})) ++done;
            if (done > 0) status_.ok(std::format("{} vehicle{} mothballed.", done, done == 1 ? "" : "s"));
        }
        if (d.button("Unmothball", yard && anyMothballed)) {
            // Copy the ids: each command changes the state.
            std::vector<game::VehicleId> ids;
            for (const game::Vehicle* v : sel)
                if (v->status == game::VehicleStatus::Mothballed) ids.push_back(v->id);
            int done = 0;
            for (game::VehicleId id : ids)
                if (status_.issue(ui, game::cmd::Mothball{id, false})) ++done;
            if (done > 0) status_.ok(std::format("{} vehicle{} back in service.", done, done == 1 ? "" : "s"));
        }
        if (d.button("Self-Destruct", anyDestruct)) {
            confirmAction_ = Action::SelfDestruct;
            confirm_.open("Self-Destruct", "Order the selected vehicles to self-destruct? They are destroyed when the turn is processed.");
        }
        d.button("Fire On", false);
        tooltip("Destroying your own vehicles by gunfire is not supported by the engine yet.");
    }

    void openRetrofit(UiContext& ui, const std::vector<const game::Vehicle*>& sel) {
        const game::GameState& s = ui.state();
        retrofitDesigns_.clear();
        std::vector<ListPicker::Item> items;
        const game::Vehicle& first = *sel.front();
        const uint32_t hull = s.design(first.design).hull;
        for (game::DesignId id : ui.me().designs) {
            const game::Design& d = s.design(id);
            if (d.hull != hull || id == first.design) continue;
            const DryRun dr = dryRun(ui.rules(), s, ui.session.player(), game::cmd::Retrofit{first.id, id});
            std::string detail = dr.result.ok ? std::format("Cost {} / {} / {} each", formatNumber(dr.cost.v[0]), formatNumber(dr.cost.v[1]),
                                                            formatNumber(dr.cost.v[2]))
                                              : dr.result.error;
            if (d.obsolete) detail += " (obsolete)";
            items.push_back({d.name, detail, dr.result.ok, designMini(ui, id)});
            retrofitDesigns_.push_back(id);
        }
        retrofit_.open("Retrofit To", std::move(items));
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
        if (d.button("Scrap Facilities", !slots_.empty())) {
            confirmAction_ = Action::ScrapFacilities;
            confirm_.open("Scrap Facilities", std::format("Scrap {} facilit{} on {}?", slots_.size(), slots_.size() == 1 ? "y" : "ies",
                                                          ui.state().galaxy.object(c.planet).name));
        }
        if (d.button("Select All", !c.facilities.empty())) {
            slots_.clear();
            for (size_t i = 0; i < c.facilities.size(); ++i) slots_.push_back(i);
        }
        if (d.button("Select None", !slots_.empty())) slots_.clear();
    }

    // ---- Popups ----

    void popups(UiContext& ui) {
        const game::GameState& s = ui.state();
        if (confirm_.draw(ui)) {
            switch (confirmAction_) {
                case Action::Scrap: {
                    std::vector<game::VehicleId> ids = selected_;
                    int done = 0;
                    for (game::VehicleId id : ids)
                        if (status_.issue(ui, game::cmd::Scrap{id, {}, -1})) ++done;
                    if (done > 0) status_.ok(std::format("{} vehicle{} scrapped.", done, done == 1 ? "" : "s"));
                    break;
                }
                case Action::SelfDestruct: {
                    int done = 0;
                    for (game::VehicleId id : std::vector<game::VehicleId>(selected_)) {
                        const game::Vehicle* v = ownVehicle(ui, id);
                        if (!v) continue;
                        const auto entry = selfDestructEntry(ui.rules(), s, *v);
                        if (!entry) continue;
                        // A per-vehicle action: the order goes to the vehicle even inside a fleet.
                        game::Order o{game::OrderKind::UseComponent, v->location};
                        o.amount = static_cast<int>(*entry);
                        OrderOwner owner;
                        owner.vehicle = id;
                        if (status_.issue(ui, withImmediate(s, owner, o))) ++done;
                    }
                    if (done > 0) status_.ok(std::format("{} vehicle{} will self-destruct when the turn is processed.", done, done == 1 ? "" : "s"));
                    break;
                }
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
        if (auto i = retrofit_.draw(ui); i && *i < retrofitDesigns_.size()) {
            const game::DesignId target = retrofitDesigns_[*i];
            int done = 0;
            for (game::VehicleId id : std::vector<game::VehicleId>(selected_))
                if (status_.issue(ui, game::cmd::Retrofit{id, target})) ++done;
            if (done > 0) status_.ok(std::format("{} vehicle{} retrofitted to {}.", done, done == 1 ? "" : "s", s.design(target).name));
        }
        report_.draw(ui);
    }

    enum class Action { Scrap, SelfDestruct, ScrapFacilities };

    std::optional<game::Location> where_;
    game::ObjectId planet_;
    bool facilities_ = false;
    std::vector<game::VehicleId> selected_;
    std::vector<size_t> slots_;
    Status status_;
    Confirm confirm_;
    Action confirmAction_ = Action::Scrap;
    ListPicker retrofit_;
    std::vector<game::DesignId> retrofitDesigns_;
    ReportPopup report_;
};

} // namespace

std::unique_ptr<Screen> makeScrap(const ScreenArgs& args) { return std::make_unique<ScrapScreen>(args); }

} // namespace opense4::client::classic
