// Launch \ Recover Units (order U, docs/spec/06 §1.3, spec 03 §12) on the
// map, and the lists it shares with the same window in a tactical battle
// (launch_window.hpp).
//
// The window acts at once, in a turn-based game, on exactly the ship, base or
// colony clicked: a click on a unit under a carrier launches it from that
// carrier (cmd::LaunchUnitsNow), a click on a group in space recovers it into
// the carrier selected on the left (cmd::RecoverUnitsNow). No order is given
// and none is run, and nothing is logged (confirmed: binary). A launch or a
// recovery that cannot fully happen says why in a message box titled "Launch
// Warning" or "Recover Warning" (movement::checkLaunch, checkRecover), and
// does what it can. Colonies launch and recover without any bay: up to 1000
// units of each kind per turn. The window is offered in turn-based games only
// (spec 06 §2.8); in a simultaneous game the orders Launch Units Remotely (I)
// and Recover Units Remotely (O) do this during the turn.

#include "client/classic/screens/launch_window.hpp"

#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"
#include "client/script/items.hpp"

#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace launchui {

namespace {

using namespace shipui;

constexpr std::array<const char*, 4> kStepLabels{"Move One", "Move Five", "Move Ten", "Move All"};

} // namespace

Click drawLists(UiContext& ui, const Lists& lists, ImVec2 size) {
    Click click;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 half{(size.x - spacing) * 0.5f, size.y};

    // The carriers, each with its "Can Launch:" lines and the units aboard.
    beginPanel(ui, "##carriers", lists.carriersCaption, half);
    for (size_t c = 0; c < lists.carriers.size(); ++c) {
        const CarrierRow& carrier = lists.carriers[c];
        ImGui::PushID(static_cast<int>(c));
        RowStyle st;
        st.lamp = carrier.selected ? Lamp::On : Lamp::Off;
        st.selected = carrier.selected;
        const RowClick rc = row(ui, 0, carrier.picture, carrier.name, {}, st);
        script::reportItem(carrier.name);   // input scripts: rows by name
        if (rc.left) click.carrier = c;
        if (rc.right) click.carrierReport = c;
        ImGui::Indent(ui.px(26));
        ImGui::TextColored(kLabelBlue, "Can Launch:");
        for (const std::string& line : carrier.canLaunch)
            if (!line.empty()) ImGui::TextColored(kDim, "  %s", line.c_str());
        ImGui::Unindent(ui.px(26));
        for (size_t k = 0; k < carrier.units.size(); ++k) {
            const UnitLine& u = carrier.units[k];
            RowStyle is;
            is.indent = 26;
            is.height = 32;
            is.picture = 26;
            const RowClick uc = row(ui, 100 + static_cast<int>(k), u.picture, u.name, std::format("{} aboard", formatNumber(u.count)), is);
            script::reportItem(u.name);   // input scripts: units by design name
            if (uc.left) click.launch = std::pair{c, k};
        }
        ImGui::PopID();
    }
    if (lists.carriers.empty() && !lists.noCarriers.empty()) ImGui::TextColored(kDim, "%s", lists.noCarriers.c_str());
    endPanel(ui, "Click a unit to launch it.");
    ImGui::SameLine();

    // The groups in space; a mixed group lists its designs under it.
    beginPanel(ui, "##groups", lists.groupsCaption, half);
    for (size_t g = 0; g < lists.groups.size(); ++g) {
        const GroupRow& group = lists.groups[g];
        ImGui::PushID(1000 + static_cast<int>(g));
        const RowClick gc = row(ui, 0, group.picture, group.name, group.detail);
        script::reportItem(group.name);
        if (gc.left) click.recover = g;
        if (gc.right) click.groupReport = g;
        for (size_t k = 0; k < group.designs.size(); ++k) {
            const UnitLine& u = group.designs[k];
            RowStyle is;
            is.indent = 26;
            is.height = 32;
            is.picture = 26;
            const RowClick dc =
                row(ui, 100 + static_cast<int>(k), u.picture, u.name, std::format("{} unit{}", formatNumber(u.count), u.count == 1 ? "" : "s"), is);
            if (dc.left) click.recoverDesign = std::pair{g, k};
        }
        ImGui::PopID();
    }
    if (lists.groups.empty() && !lists.noGroups.empty()) ImGui::TextColored(kDim, "%s", lists.noGroups.c_str());
    endPanel(ui, "Click a group to take it aboard.");
    return click;
}

std::string canLaunchLine(int64_t left, int64_t rate, std::string_view kind, bool battle) {
    if (rate < 0) return {};
    return battle ? std::format("{} {}", formatNumber(left), kind) : std::format("{} / {} {}", formatNumber(left), formatNumber(rate), kind);
}

bool stepRadios(Dialog& d, uint8_t& step) {
    bool changed = false;
    for (uint8_t k = 0; k < kStepLabels.size(); ++k)
        if (d.tab(kStepLabels[k], step == k)) {
            changed = changed || step != k;
            step = k;
        }
    return changed;
}

int64_t stepCount(uint8_t step, int64_t available) {
    if (available <= 0) return 0;
    switch (step) {
        case 0: return 1;
        case 1: return std::min<int64_t>(5, available);
        case 2: return std::min<int64_t>(10, available);
        default: return available;
    }
}

std::optional<uint8_t>& pendingStep() {
    static std::optional<uint8_t> step;
    return step;
}

} // namespace launchui

namespace {

using namespace shipui;

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) out += (out.empty() ? "" : "\n\n") + l;
    return out;
}

class LaunchRecoverScreen final : public Screen {
public:
    explicit LaunchRecoverScreen(const ScreenArgs& args) {
        holder_.vehicle = args.vehicle;
        holder_.planet = args.vehicle.valid() ? game::ObjectId{} : args.planet;
    }

    bool draw(UiContext& ui) override {
        locate(ui);
        rememberStep(ui);
        Dialog d(ui, screenTitle(ScreenId::LaunchRecover), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const std::vector<Holder> holders = holdersHere(ui);
        if (std::find(holders.begin(), holders.end(), holder_) == holders.end()) holder_ = holders.empty() ? Holder{} : holders.front();
        std::vector<const game::Vehicle*> groups = groupsHere(ui);

        d.beginContent();
        launchui::Click click;
        if (!where_) {
            ImGui::TextColored(kDim, "You have no ships.");
        } else {
            ImGui::TextColored(kDim, "Location: %s", sectorName(ui.state(), *where_, ui.session.player()).c_str());
            if (!ui.session.turnBased()) ImGui::TextColored(kDim, "Units are launched and recovered here only in turn-based games.");
            const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f;
            click = launchui::drawLists(ui, lists(ui, holders, groups), ImVec2(ImGui::GetContentRegionAvail().x, h));
            status_.draw(ui);
        }

        d.beginButtons();
        uint8_t step = step_;
        if (launchui::stepRadios(d, step)) setStep(ui, step);
        d.spacer();
        const bool close = d.close();
        report_.draw(ui);
        notice_.draw(ui);
        if (close) return false;

        // The clicks act after the lists have drawn: they change what the lists show.
        if (click.carrier && *click.carrier < holders.size()) holder_ = holders[*click.carrier];
        if (click.launch && click.launch->first < holders.size()) {
            holder_ = holders[click.launch->first];
            launch(ui, holder_, click.launch->second);
        }
        if (click.recover && *click.recover < groups.size()) recover(ui, *groups[*click.recover], {});
        if (click.recoverDesign && click.recoverDesign->first < groups.size()) {
            const std::vector<game::UnitStack> stacks = game::groupStacks(*groups[click.recoverDesign->first]);
            if (click.recoverDesign->second < stacks.size()) recover(ui, *groups[click.recoverDesign->first], stacks[click.recoverDesign->second].design);
        }
        if (click.carrierReport && *click.carrierReport < holders.size()) {
            const Holder& h = holders[*click.carrierReport];
            if (h.vehicle.valid()) report_.vehicle(h.vehicle);
            else report_.planet(h.planet);
        }
        if (click.groupReport && *click.groupReport < groups.size()) report_.vehicle(groups[*click.groupReport]->id);
        return d.keepOpen();
    }

private:
    void locate(UiContext& ui) {
        if (where_) return;
        if (const game::Vehicle* v = ownVehicle(ui, holder_.vehicle)) where_ = v->location;
        else if (ownColony(ui, holder_.planet)) where_ = game::locationOf(ui.state().galaxy, holder_.planet);
        else if (auto home = homeworld(ui)) {
            // Opened with nothing (automation): the homeworld.
            where_ = game::locationOf(ui.state().galaxy, *home);
            holder_ = Holder{{}, *home};
        } else if (const game::Vehicle* first = ownVehicle(ui, firstOwnShip(ui))) {
            where_ = first->location;
            holder_ = {};  // the sector's first carrier, below
        }
    }

    // The amount radios start from the empire's options; a step a battle
    // could not keep is kept now.
    void rememberStep(UiContext& ui) {
        if (stepRead_) return;
        stepRead_ = true;
        step_ = std::min<uint8_t>(ui.options().unitsStep, 3);
        if (std::optional<uint8_t>& pending = launchui::pendingStep()) {
            step_ = *pending;
            setStep(ui, *pending);
        }
    }

    void setStep(UiContext& ui, uint8_t step) {
        step_ = step;
        game::InterfaceOptions o = ui.options();
        o.unitsStep = step;
        if (ui.setOptions(o)) launchui::pendingStep().reset();
        else launchui::pendingStep() = step;
    }

    // The carriers: own colonies and own ships and bases with a launch
    // ability, each with cargo space, in the system's object order
    // (launchCarriers, spec 06 §1.3, confirmed: binary).
    std::vector<Holder> holdersHere(const UiContext& ui) const {
        std::vector<Holder> out;
        if (!where_) return out;
        for (const shipui::LaunchCarrier& c : launchCarriers(ui.rules(), ui.state(), ui.session.player(), *where_))
            out.push_back(Holder{c.vehicle, c.planet});
        return out;
    }

    std::vector<const game::Vehicle*> groupsHere(const UiContext& ui) const {
        std::vector<const game::Vehicle*> out;
        if (!where_) return out;
        for (const game::Vehicle* v : ownVehiclesAt(ui, *where_))
            if (isUnitVehicle(ui.rules(), ui.state(), *v)) out.push_back(v);
        return out;
    }

    launchui::Lists lists(UiContext& ui, const std::vector<Holder>& holders, const std::vector<const game::Vehicle*>& groups) const {
        const game::GameState& s = ui.state();
        launchui::Lists out;
        out.carriersCaption = "Units Aboard";
        out.groupsCaption = "Units In Space";
        out.noCarriers = "Nothing of yours here carries or launches units.";
        out.noGroups = "No units of yours in space here.";
        static constexpr std::array<const char*, 4> kKinds{"Fighters", "Satellites", "Mines", "Drones"};
        for (const Holder& h : holders) {
            launchui::CarrierRow row;
            const game::Cargo* cargo = nullptr;
            if (const game::Vehicle* v = ownVehicle(ui, h.vehicle)) {
                row.name = v->name;
                row.picture = vehicleMini(ui, *v);
                cargo = &v->cargo;
            } else if (const game::Colony* col = ownColony(ui, h.planet)) {
                row.name = s.galaxy.object(h.planet).name;
                row.picture = colonySprite(ui, h.planet);
                cargo = &col->cargo;
            }
            const game::movement::LaunchCapacity cap = game::movement::launchCapacity(ui.rules(), s, h.vehicle, h.planet);
            for (size_t k = 0; k < kKinds.size(); ++k) row.canLaunch[k] = launchui::canLaunchLine(cap.left[k], cap.rate[k], kKinds[k], false);
            for (const game::UnitStack& u : cargo ? cargo->units : std::vector<game::UnitStack>{})
                if (u.count > 0) row.units.push_back({u.design, s.design(u.design).name, designMini(ui, u.design), u.count});
            row.selected = h == holder_;
            out.carriers.push_back(std::move(row));
        }
        for (const game::Vehicle* v : groups) {
            launchui::GroupRow row;
            row.name = v->name;
            row.picture = unitMini(ui, *v);
            const std::vector<game::UnitStack> stacks = game::groupStacks(*v);
            row.detail = std::format("{} unit{}", v->count, v->count == 1 ? "" : "s");
            if (stacks.size() > 1) {
                row.detail += std::format(" of {} designs", stacks.size());
                for (const game::UnitStack& u : stacks) row.designs.push_back({u.design, s.design(u.design).name, designMini(ui, u.design), u.count});
            }
            out.groups.push_back(std::move(row));
        }
        return out;
    }

    void launch(UiContext& ui, const Holder& from, size_t unit) {
        const game::GameState& s = ui.state();
        const game::Cargo* cargo = nullptr;
        if (const game::Vehicle* v = ownVehicle(ui, from.vehicle)) cargo = &v->cargo;
        else if (const game::Colony* c = ownColony(ui, from.planet)) cargo = &c->cargo;
        std::vector<game::UnitStack> units;
        for (const game::UnitStack& u : cargo ? cargo->units : std::vector<game::UnitStack>{})
            if (u.count > 0) units.push_back(u);
        if (unit >= units.size()) return;
        const game::UnitStack u = units[unit];
        const int amount = static_cast<int>(launchui::stepCount(step_, u.count));
        const game::movement::UnitTransfer check = game::movement::checkLaunch(ui.rules(), s, from.vehicle, from.planet, u.design, amount);
        if (!check.warnings.empty()) notice_.open(joined(check.warnings), "Launch Warning");
        if (check.moved <= 0) return;
        const game::CommandResult r = ui.session.issue(game::cmd::LaunchUnitsNow{from.vehicle, from.planet, u.design, amount});
        if (!r.ok) status_.error(r.error);
        else status_.clear();
    }

    void recover(UiContext& ui, const game::Vehicle& group, game::DesignId design) {
        if (!holder_.valid()) {
            status_.error("Select a ship, base or colony on the left first.");
            return;
        }
        const int64_t available = design.valid() ? game::groupUnits(group, design) : group.count;
        const int amount = static_cast<int>(launchui::stepCount(step_, available));
        const game::movement::UnitTransfer check =
            game::movement::checkRecover(ui.rules(), ui.state(), holder_.vehicle, holder_.planet, group.id, design, amount);
        if (!check.warnings.empty()) notice_.open(joined(check.warnings), "Recover Warning");
        if (check.moved <= 0) {
            if (check.warnings.empty()) status_.error("There is no room aboard.");
            return;
        }
        const game::CommandResult r = ui.session.issue(game::cmd::RecoverUnitsNow{holder_.vehicle, holder_.planet, group.id, design, amount});
        if (!r.ok) status_.error(r.error);
        else status_.clear();
    }

    std::optional<game::Location> where_;
    Holder holder_;
    uint8_t step_ = 2;
    bool stepRead_ = false;
    Status status_;
    shipui::ReportPopup report_;
    NoticePopup notice_;
};

} // namespace

std::unique_ptr<Screen> makeLaunchRecover(const ScreenArgs& args) { return std::make_unique<LaunchRecoverScreen>(args); }

} // namespace opense4::client::classic
