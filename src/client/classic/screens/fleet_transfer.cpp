// Fleet Transfer (order F, docs/spec/06 §1.3, spec 03 §9-10): the vehicles
// in a sector that are in no fleet, and the fleets there with their members.
// Left-click moves a vehicle into the selected fleet or out of its fleet.

#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "game/design.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

class FleetTransferScreen final : public Screen {
public:
    explicit FleetTransferScreen(const ScreenArgs& args) : anchor_(args.vehicle), selected_(args.fleet), sandbox_(args.text == kSimulatorWindow) {}

    ~FleetTransferScreen() override {
        if (sandbox_) simulatorSandboxClosed();
    }
    FleetTransferScreen(const FleetTransferScreen&) = delete;
    FleetTransferScreen& operator=(const FleetTransferScreen&) = delete;

    bool draw(UiContext& ui) override {
        // Opened by the Combat Simulator: the same window over its sandbox.
        if (sandbox_) return drawInSimulatorSandbox(ui, [this](UiContext& sub) { return drawIn(sub); });
        return drawIn(ui);
    }

private:
    bool drawIn(UiContext& ui) {
        locate(ui);
        Dialog d(ui, screenTitle(ScreenId::FleetTransfer), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        const std::vector<const game::Vehicle*> loose = looseVehicles(ui);
        const std::vector<const game::Fleet*> fleets = fleetsHere(ui);
        if (!s.fleet(selected_) || std::none_of(fleets.begin(), fleets.end(), [&](const game::Fleet* f) { return f->id == selected_; }))
            selected_ = fleets.empty() ? game::FleetId{} : fleets.front()->id;
        const game::Fleet* fleet = s.fleet(selected_);

        d.beginContent();
        if (!where_) {
            ImGui::TextColored(kDim, "You have no ships.");
        } else {
            ImGui::TextColored(kDim, "Location: %s", sectorName(s, *where_, ui.session.player()).c_str());
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
            const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 4.4f;
            vehiclesPanel(ui, loose, ImVec2(w, h));
            ui.tagItem("fleet-transfer:ships");
            ImGui::SameLine();
            fleetsPanel(ui, fleets, ImVec2(w, h));
            ui.tagItem("fleet-transfer:fleets");
            applyClicks(ui);
            status_.draw(ui);
        }

        d.beginButtons();
        fleet = s.fleet(selected_);  // clicks in the lists may have changed it
        const std::vector<ruleset::Formation>& formations = ui.rules().data().formations;
        const auto& strategies = ui.me().strategies;
        if (d.button("Create Fleet", !loose.empty())) {
            const game::Vehicle* first = startVehicle(ui, loose);
            createPrompt_.open("Create Fleet", std::format("Name of the new fleet. It starts with {}; add more ships afterwards.", first->name),
                               std::format("Fleet {}", s.nextFleetId + 1));
        }
        ui.tagItem("fleet-transfer:create-fleet");
        if (d.button("Formation", fleet && !formations.empty())) {
            std::vector<ListPicker::Item> items;
            for (const auto& f : formations) items.push_back({f.name, f.description, true, {}});
            formationPicker_.open("Formation", std::move(items), static_cast<int>(fleet->formation));
        }
        if (d.button("Strategy", fleet && !strategies.empty())) {
            std::vector<ListPicker::Item> items;
            for (const auto& st : strategies) items.push_back({st.name, {}, true, {}});
            strategyPicker_.open("Strategy", std::move(items), static_cast<int>(fleet->strategy));
        }
        d.spacer();
        // Existing Fleets, in the simulator as in the real game: the real
        // empire's Ships\Units window with its saved switches and tab, for
        // viewing only (spec 06 §1.10.4, §7 Q79).
        if (d.button("Existing Fleets")) {
            ScreenArgs a;
            a.text = kViewOnly;
            ui.open(ScreenId::Ships, a);
        }
        d.spacer();
        if (d.button("Add All", fleet && !loose.empty())) {
            int added = 0;
            for (const game::Vehicle* v : loose)
                if (status_.issue(ui, game::cmd::JoinFleet{selected_, v->id})) ++added;
            if (added > 0) status_.ok(std::format("{} vehicle{} joined the fleet.", added, added == 1 ? "" : "s"));
        }
        if (d.button("Remove All", fleet != nullptr)) {
            const std::vector<game::VehicleId> members = fleet->members;
            int removed = 0;
            for (game::VehicleId id : members)
                if (status_.issue(ui, game::cmd::LeaveFleet{id})) ++removed;
            if (removed > 0) status_.ok(std::format("{} vehicle{} left the fleet.", removed, removed == 1 ? "" : "s"));
        }
        if (d.button("Disband Fleet", fleet != nullptr)) status_.issue(ui, game::cmd::DisbandFleet{selected_}, "Fleet disbanded.");
        if (d.button("Rename Fleet", fleet != nullptr)) renamePrompt_.open("Rename Fleet", "New name of the fleet:", fleet->name);
        if (d.close()) return false;
        popups(ui, loose);
        return d.keepOpen();
    }

private:
    // The sector: the given vehicle's, else the given fleet's, else the first own ship's.
    void locate(UiContext& ui) {
        const game::GameState& s = ui.state();
        if (where_) return;
        if (!ownVehicle(ui, anchor_)) {
            anchor_ = {};
            if (const game::Fleet* f = s.fleet(selected_); f && f->owner == ui.session.player() && !f->members.empty())
                anchor_ = f->leader.valid() ? f->leader : f->members.front();
        }
        if (!anchor_.valid()) anchor_ = firstOwnShip(ui);
        if (const game::Vehicle* v = ownVehicle(ui, anchor_)) {
            where_ = v->location;
            if (!selected_.valid()) selected_ = v->fleet;
        }
    }

    std::vector<const game::Vehicle*> looseVehicles(const UiContext& ui) const {
        std::vector<const game::Vehicle*> out;
        if (!where_) return out;
        for (const game::Vehicle* v : ownVehiclesAt(ui, *where_))
            if (!v->fleet.valid()) out.push_back(v);
        return out;
    }

    std::vector<const game::Fleet*> fleetsHere(const UiContext& ui) const {
        std::vector<const game::Fleet*> out;
        if (!where_) return out;
        const game::GameState& s = ui.state();
        for (const game::Fleet& f : s.fleets) {
            if (f.owner != ui.session.player()) continue;
            const bool here = std::any_of(f.members.begin(), f.members.end(), [&](game::VehicleId id) {
                const game::Vehicle* v = s.vehicle(id);
                return v && v->location == *where_;
            });
            if (here) out.push_back(&f);
        }
        return out;
    }

    // The engine creates fleets with at least one member: the vehicle the
    // window was opened for when it is free, else the first free one.
    const game::Vehicle* startVehicle(const UiContext&, const std::vector<const game::Vehicle*>& loose) const {
        for (const game::Vehicle* v : loose)
            if (v->id == anchor_) return v;
        return loose.front();
    }

    void vehiclesPanel(UiContext& ui, const std::vector<const game::Vehicle*>& loose, ImVec2 size) {
        const game::GameState& s = ui.state();
        beginPanel(ui, "##loose", "Vehicles in sector", size);
        for (const game::Vehicle* v : loose) {
            RowStyle st;
            st.selected = v->id == anchor_;
            const RowClick c = row(ui, static_cast<int>(v->id.value), vehicleMini(ui, *v), v->name, s.design(v->design).name + " Class", st);
            if (c.left) join_ = v->id;
            if (c.right) report_.vehicle(v->id);
        }
        if (loose.empty()) ImGui::TextColored(kDim, "No ship here is outside a fleet.");
        endPanel(ui, "Click a ship to put it in the selected fleet.");
    }

    void fleetsPanel(UiContext& ui, const std::vector<const game::Fleet*>& fleets, ImVec2 size) {
        const game::GameState& s = ui.state();
        const auto& formations = ui.rules().data().formations;
        const auto& strategies = ui.me().strategies;
        beginPanel(ui, "##fleets", "Fleets in sector", size);
        for (const game::Fleet* f : fleets) {
            RowStyle st;
            st.lamp = f->id == selected_ ? Lamp::On : Lamp::Off;
            st.selected = f->id == selected_;
            std::string detail;
            if (f->formation < formations.size()) detail = formations[f->formation].name;
            if (f->strategy < strategies.size()) detail += (detail.empty() ? "" : ", ") + strategies[f->strategy].name;
            const RowClick c = row(ui, 1000000 + static_cast<int>(f->id.value), fleetMini(ui), f->name, detail, st);
            if (c.left) selected_ = f->id;
            if (c.right) report_.fleet(f->id);
            for (game::VehicleId id : f->members) {
                const game::Vehicle* v = s.vehicle(id);
                if (!v) continue;
                RowStyle ms;
                ms.indent = 26;
                ms.height = 32;
                ms.picture = 26;
                const std::string name = id == f->leader ? v->name + "  (leader)" : v->name;
                const RowClick mc = row(ui, static_cast<int>(id.value), vehicleMini(ui, *v), name, s.design(v->design).name + " Class", ms);
                if (mc.left) leave_ = id;
                if (mc.right) report_.vehicle(id);
            }
        }
        if (fleets.empty()) ImGui::TextColored(kDim, "No fleets here. Use Create Fleet.");
        endPanel(ui, "Click a fleet to select it; click a member to take it out.");
    }

    // Clicks change fleets, so they are applied after the lists are drawn.
    void applyClicks(UiContext& ui) {
        const game::GameState& s = ui.state();
        if (join_) {
            const game::Vehicle* v = s.vehicle(*join_);
            if (!s.fleet(selected_)) status_.error("Create a fleet first, or select one on the right.");
            else if (v) status_.issue(ui, game::cmd::JoinFleet{selected_, v->id}, std::format("{} joined the fleet.", v->name));
        }
        if (leave_)
            if (const game::Vehicle* v = s.vehicle(*leave_))
                status_.issue(ui, game::cmd::LeaveFleet{v->id}, std::format("{} left the fleet.", v->name));
        join_.reset();
        leave_.reset();
    }

    void popups(UiContext& ui, const std::vector<const game::Vehicle*>& loose) {
        const game::GameState& s = ui.state();
        if (auto name = createPrompt_.draw(ui); name && !loose.empty()) {
            const game::VehicleId first = startVehicle(ui, loose)->id;
            game::cmd::CreateFleet c;
            c.name = *name;
            c.members = {first};
            if (status_.issue(ui, c, std::format("Fleet {} created.", *name)))
                if (const game::Vehicle* v = ui.state().vehicle(first)) selected_ = v->fleet;
        }
        if (auto name = renamePrompt_.draw(ui)) {
            game::cmd::Rename c;
            c.fleet = selected_;
            c.name = *name;
            status_.issue(ui, c);
        }
        if (auto i = formationPicker_.draw(ui))
            if (const game::Fleet* f = s.fleet(selected_))
                status_.issue(ui, game::cmd::SetFleetOptions{f->id, static_cast<uint32_t>(*i), f->strategy});
        if (auto i = strategyPicker_.draw(ui))
            if (const game::Fleet* f = s.fleet(selected_))
                status_.issue(ui, game::cmd::SetFleetOptions{f->id, f->formation, static_cast<uint32_t>(*i)});
        report_.draw(ui);
    }

    game::VehicleId anchor_;
    game::FleetId selected_;
    std::optional<game::VehicleId> join_, leave_;
    std::optional<game::Location> where_;
    Status status_;
    TextPrompt createPrompt_, renamePrompt_;
    ListPicker formationPicker_, strategyPicker_;
    ReportPopup report_;
    bool sandbox_ = false;   // opened by the Combat Simulator's Fleets For Plr
};

} // namespace

std::unique_ptr<Screen> makeFleetTransfer(const ScreenArgs& args) { return std::make_unique<FleetTransferScreen>(args); }

} // namespace opense4::client::classic
