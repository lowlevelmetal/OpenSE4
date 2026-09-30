// Small order dialogs (docs/spec/06 §1.3): View Orders (V), Select Waypoint
// (Move To Waypoint) and Change Name (N).

#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

// The orders of the selected vehicle, or of its fleet when it is in one.
OrderOwner ownerFromArgs(const UiContext& ui, game::VehicleId vehicle, game::FleetId fleet) {
    if (const game::Fleet* f = ui.state().fleet(fleet); f && f->owner == ui.session.player()) {
        OrderOwner o;
        o.fleet = fleet;
        return o;
    }
    if (ownVehicle(ui, vehicle)) return orderOwner(ui.state(), vehicle);
    return orderOwner(ui.state(), firstOwnShip(ui));
}

// ---- View Orders ------------------------------------------------------------------------------

class ViewOrdersScreen final : public Screen {
public:
    explicit ViewOrdersScreen(const ScreenArgs& args) : vehicle_(args.vehicle), fleet_(args.fleet) {}

    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        const OrderOwner owner = ownerFromArgs(ui, vehicle_, fleet_);
        Dialog d(ui, screenTitle(ScreenId::ViewOrders), DialogSize::Picker, 170.0f);
        if (!d.open()) return d.keepOpen();
        const std::vector<game::Order>* list = ordersOf(s, owner);
        const std::vector<game::Order> orders = list ? *list : std::vector<game::Order>{};
        const bool repeat = repeatOf(s, owner);
        if (selected_ >= static_cast<int>(orders.size())) selected_ = static_cast<int>(orders.size()) - 1;
        game::DesignId design;
        if (const game::Vehicle* v = s.vehicle(owner.vehicle)) design = v->design;

        d.beginContent();
        if (!owner.valid()) {
            ImGui::TextColored(kDim, "You have no ships.");
        } else {
            heading(ui, ownerName(ui, owner).c_str());
            if (auto where = ownerLocation(s, owner)) ImGui::TextColored(kDim, "At %s", sectorName(s, *where, ui.session.player()).c_str());
            if (owner.fleet.valid()) ImGui::TextColored(kDim, "Fleet orders: every member follows them.");
            ImGui::Dummy(ImVec2(0, ui.px(4)));
            beginPanel(ui, "##orders", "Orders", ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 4.2f));
            for (size_t i = 0; i < orders.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const std::string label = std::format("{}. {}", i + 1, describeOrder(ui, orders[i], design));
                if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == selected_, ImGuiSelectableFlags_None, ImVec2(0, ui.px(24))))
                    selected_ = static_cast<int>(i);
                ImGui::PopID();
            }
            if (orders.empty()) ImGui::TextColored(kDim, "No orders.");
            endPanel(ui, "Orders run from the top. With Repeat on, a finished order moves to the end.");
            ImGui::TextColored(kLabelBlue, "Repeat Orders:");
            ImGui::SameLine();
            ImGui::TextUnformatted(repeat ? "On" : "Off");
            status_.draw(ui);
        }

        d.beginButtons();
        const bool has = selected_ >= 0 && static_cast<size_t>(selected_) < orders.size();
        auto update = [&](std::vector<game::Order> changed, bool rep) { status_.issue(ui, setOrders(owner, std::move(changed), rep)); };
        if (d.button("Move Up", has && selected_ > 0)) {
            std::vector<game::Order> o = orders;
            selected_ = static_cast<int>(moveOrder(o, static_cast<size_t>(selected_), -1));
            update(std::move(o), repeat);
        }
        if (d.button("Move Down", has && static_cast<size_t>(selected_) + 1 < orders.size())) {
            std::vector<game::Order> o = orders;
            selected_ = static_cast<int>(moveOrder(o, static_cast<size_t>(selected_), 1));
            update(std::move(o), repeat);
        }
        if (d.button("Delete Order", has)) {
            std::vector<game::Order> o = orders;
            o.erase(o.begin() + selected_);
            update(std::move(o), repeat);
        }
        if (d.button("Clear Orders", !orders.empty())) {
            update({}, false);
            selected_ = -1;
        }
        d.spacer();
        if (d.check("Repeat Orders", repeat, owner.valid())) update(orders, !repeat);
        if (d.close()) return false;
        return d.keepOpen();
    }

private:
    game::VehicleId vehicle_;
    game::FleetId fleet_;
    int selected_ = -1;
    Status status_;
};

// ---- Select Waypoint -------------------------------------------------------------------------

// Opened with a vehicle: gives Move To Waypoint. Opened with a planet, or with
// a vehicle and text "queue": sets that construction queue's automatic
// Move To waypoint instead (Set Construction Queue -> Set Move To).
class SelectWaypointScreen final : public Screen {
public:
    explicit SelectWaypointScreen(const ScreenArgs& args)
        : vehicle_(args.vehicle), fleet_(args.fleet), planet_(args.planet), queue_(args.text == "queue" || (!args.vehicle.valid() && args.planet.valid())) {}

    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        Dialog d(ui, screenTitle(ScreenId::SelectWaypoint), DialogSize::Picker, 0.0f);
        if (!d.open()) return d.keepOpen();
        const OrderOwner owner = queue_ ? OrderOwner{} : ownerFromArgs(ui, vehicle_, fleet_);
        if (queue_) ImGui::TextColored(kDim, "New vehicles from this queue will move to the waypoint you pick.");
        else if (owner.valid()) ImGui::TextColored(kDim, "Move %s to:", ownerName(ui, owner).c_str());
        beginPanel(ui, "##waypoints", "Waypoints", ImVec2(0, ImGui::GetContentRegionAvail().y - ui.px(34) - ImGui::GetTextLineHeightWithSpacing() * 2));
        bool any = false;
        std::optional<int> chosen;
        const auto& wps = ui.me().waypoints;
        for (size_t i = 0; i < wps.size(); ++i) {
            if (!wps[i].set) continue;
            any = true;
            ImGui::PushID(static_cast<int>(i));
            const std::string label = std::format("{} - {} ({})", i, wps[i].name, s.galaxy.system(wps[i].location.system).name);
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, ui.px(24)))) chosen = static_cast<int>(i);
            ImGui::PopID();
        }
        if (!any) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kDim, "No waypoints yet. Set one with Alt+0..9 on a selected sector, or in Empire Status -> Waypoints.");
            ImGui::PopTextWrapPos();
        }
        endPanel(ui, "Click a waypoint to choose it.");
        status_.draw(ui);
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui.px(26) - ImGui::GetStyle().WindowPadding.y));
        if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) ||
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
            d.requestClose();
        if (chosen && choose(ui, owner, *chosen)) d.requestClose();
        return d.keepOpen();
    }

private:
    bool choose(UiContext& ui, OrderOwner owner, int slot) {
        if (queue_) {
            game::cmd::QueueTarget t;
            t.planet = vehicle_.valid() ? game::ObjectId{} : planet_;
            t.vehicle = vehicle_;
            const game::ConstructionQueue* q = nullptr;
            if (const game::Vehicle* v = ownVehicle(ui, t.vehicle)) q = &v->queue;
            else if (const game::Colony* c = ownColony(ui, t.planet)) q = &c->queue;
            if (!q) {
                status_.error("That construction queue is not yours.");
                return false;
            }
            return status_.issue(ui, game::cmd::QueueFlags{t, q->onHold, q->repeat, q->emergency, slot});
        }
        if (!owner.valid()) {
            status_.error("No ship to give the order to.");
            return false;
        }
        game::Order o{game::OrderKind::MoveToWaypoint};
        o.amount = slot;
        return status_.issue(ui, withAppended(ui.state(), owner, o));
    }

    game::VehicleId vehicle_;
    game::FleetId fleet_;
    game::ObjectId planet_;
    bool queue_ = false;
    Status status_;
};

// ---- Change Name ------------------------------------------------------------------------------

class RenameScreen final : public Screen {
public:
    explicit RenameScreen(const ScreenArgs& args) : args_(args) {}

    bool draw(UiContext& ui) override {
        if (!resolved_) resolve(ui);
        Dialog d(ui, screenTitle(ScreenId::Rename), DialogSize::Prompt, 0.0f);
        if (!d.open()) return d.keepOpen();
        if (!command_) {
            ImGui::TextColored(kDim, "Nothing to rename.");
        } else {
            ImGui::TextColored(kLabelBlue, "%s", what_.c_str());
            if (focus_) {
                ImGui::SetKeyboardFocusHere();
                focus_ = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool enter = ImGui::InputText("##name", buffer_, sizeof(buffer_), ImGuiInputTextFlags_EnterReturnsTrue);
            status_.draw(ui);
            if (enter && apply(ui)) d.requestClose();
        }
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui.px(26) - ImGui::GetStyle().WindowPadding.y));
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginDisabled(!command_ || buffer_[0] == '\0');
        if (ImGui::Button("OK", ImVec2(w, ui.px(26))) && apply(ui)) d.requestClose();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(w, ui.px(26))) ||
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
            d.requestClose();
        return d.keepOpen();
    }

private:
    // The first valid target: vehicle, fleet, planet, design; else the first own ship.
    void resolve(UiContext& ui) {
        resolved_ = true;
        const game::GameState& s = ui.state();
        std::string current;
        game::cmd::Rename c;
        if (const game::Vehicle* v = ownVehicle(ui, args_.vehicle)) {
            c.vehicle = v->id;
            current = v->name;
            what_ = "New name for the ship " + v->name + ":";
        } else if (const game::Fleet* f = s.fleet(args_.fleet); f && f->owner == ui.session.player()) {
            c.fleet = f->id;
            current = f->name;
            what_ = "New name for the fleet " + f->name + ":";
        } else if (ownColony(ui, args_.planet)) {
            c.planet = args_.planet;
            current = s.galaxy.object(args_.planet).name;
            what_ = "New name for the planet " + current + ":";
        } else if (args_.design.valid() && args_.design.index() < s.designs.size() && s.design(args_.design).owner == ui.session.player()) {
            c.design = args_.design;
            current = s.design(args_.design).name;
            what_ = "New name for the design " + current + ":";
        } else if (const game::Vehicle* first = ownVehicle(ui, firstOwnShip(ui))) {
            c.vehicle = first->id;
            current = first->name;
            what_ = "New name for the ship " + first->name + ":";
        } else {
            return;
        }
        command_ = c;
        const size_t n = std::min(current.size(), sizeof(buffer_) - 1);
        std::copy_n(current.begin(), n, buffer_);
        buffer_[n] = '\0';
    }

    bool apply(UiContext& ui) {
        if (!command_ || buffer_[0] == '\0') return false;
        game::cmd::Rename c = *command_;
        c.name = buffer_;
        return status_.issue(ui, c);
    }

    ScreenArgs args_;
    bool resolved_ = false;
    bool focus_ = true;
    std::optional<game::cmd::Rename> command_;
    std::string what_;
    char buffer_[65]{};
    Status status_;
};

} // namespace

std::unique_ptr<Screen> makeViewOrders(const ScreenArgs& args) { return std::make_unique<ViewOrdersScreen>(args); }
std::unique_ptr<Screen> makeSelectWaypoint(const ScreenArgs& args) { return std::make_unique<SelectWaypointScreen>(args); }
std::unique_ptr<Screen> makeRename(const ScreenArgs& args) { return std::make_unique<RenameScreen>(args); }

} // namespace opense4::client::classic
