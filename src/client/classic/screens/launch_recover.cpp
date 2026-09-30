// Launch \ Recover Units (order U, docs/spec/06 §1.3, spec 03 §12). Left:
// the own ships and colonies in the sector with the units in their cargo;
// click a unit type to launch it. Right: the own unit groups in space; click
// one to recover it into the ship selected on the left.
//
// The classic game launches at once (turn-based games). Each click gives a
// Launch Units or Recover Units order placed at the head of the ship's (or
// planet's) orders: a turn-based game carries it out at once; a simultaneous
// one during the turn's movement phase, and the pending ones are listed and
// can be taken back. Colonies launch and recover without any bay: up to 1000 units of each
// kind per turn (spec 03 §12). Launch / Recover Remotely (I / O) give the same
// orders to a ship for another sector picked on the map.

#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

class LaunchRecoverScreen final : public Screen {
public:
    explicit LaunchRecoverScreen(const ScreenArgs& args) {
        holder_.vehicle = args.vehicle;
        holder_.planet = args.vehicle.valid() ? game::ObjectId{} : args.planet;
    }

    bool draw(UiContext& ui) override {
        locate(ui);
        Dialog d(ui, screenTitle(ScreenId::LaunchRecover), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const std::vector<Holder> holders = holdersHere(ui);
        if (std::find(holders.begin(), holders.end(), holder_) == holders.end()) {
            // Prefer a ship that can launch.
            holder_ = holders.empty() ? Holder{} : holders.front();
            for (const Holder& h : holders)
                if (const game::Vehicle* v = ownVehicle(ui, h.vehicle); v && launchRates(ui.rules(), ui.state(), *v).any()) {
                    holder_ = h;
                    break;
                }
        }

        d.beginContent();
        if (!where_) {
            ImGui::TextColored(kDim, "You have no ships.");
        } else {
            ImGui::TextColored(kDim, "Location: %s", sectorName(ui.state(), *where_, ui.session.player()).c_str());
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
            const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f - ui.px(130);
            carriersPanel(ui, holders, ImVec2(w, h));
            ImGui::SameLine();
            spacePanel(ui, ImVec2(w, h));
            pendingPanel(ui);
            status_.draw(ui);
        }

        d.beginButtons();
        stepButtons(d, step_);
        d.spacer();
        const game::Vehicle* carrier = ownVehicle(ui, holder_.vehicle);
        if (d.button("Launch Remotely", carrier != nullptr)) openRemote(ui, game::OrderKind::LaunchUnits, *carrier);
        if (d.button("Recover Remotely", carrier != nullptr)) openRemote(ui, game::OrderKind::RecoverUnits, *carrier);
        if (d.close()) return false;
        report_.draw(ui);
        if (auto pick = remote_.draw(ui); pick && ownVehicle(ui, remoteVehicle_)) {
            game::Order o{remoteKind_};
            o.design = pick->design;
            o.amount = pick->amount;
            pickLocationForOrder(ui, orderOwner(ui.state(), remoteVehicle_), o, std::format("{}: pick where", game::displayName(remoteKind_)));
            return false;  // the main window takes the pick
        }
        return d.keepOpen();
    }

private:
    void locate(UiContext& ui) {
        if (where_) return;
        if (const game::Vehicle* v = ownVehicle(ui, holder_.vehicle)) where_ = v->location;
        else if (ownColony(ui, holder_.planet)) where_ = game::locationOf(ui.state().galaxy, holder_.planet);
        else if (const game::Vehicle* first = ownVehicle(ui, firstOwnShip(ui))) {
            where_ = first->location;
            holder_ = {};  // picked from the sector's carriers below
        }
    }

    // Ships with launchers or units aboard, and colonies holding units.
    std::vector<Holder> holdersHere(const UiContext& ui) const {
        std::vector<Holder> out;
        if (!where_) return out;
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        for (const game::Colony* c : ownColoniesAt(ui, *where_))
            if (!c->cargo.units.empty()) out.push_back(Holder{{}, c->planet});
        for (const game::Vehicle* v : ownVehiclesAt(ui, *where_)) {
            if (isUnitVehicle(r, s, *v)) continue;
            if (launchRates(r, s, *v).any() || !v->cargo.units.empty() || v->id == holder_.vehicle) out.push_back(Holder{v->id, {}});
        }
        return out;
    }

    // Immediate launch / recover orders already given for this ship or planet.
    std::vector<game::Order> pendingOf(const UiContext& ui, const Holder& h) const {
        std::vector<game::Order> out;
        const auto* orders = ordersOf(ui.state(), ownerOf(ui, h));
        if (!orders || !where_) return out;
        for (const game::Order& o : *orders) {
            if (!immediateKind(o.kind)) break;
            if ((o.kind == game::OrderKind::LaunchUnits || o.kind == game::OrderKind::RecoverUnits) && o.location == *where_) out.push_back(o);
        }
        return out;
    }

    int64_t pendingCount(const std::vector<game::Order>& pending, game::OrderKind kind, game::DesignId design, game::VehicleId group = {}) const {
        int64_t n = 0;
        for (const game::Order& o : pending)
            if (o.kind == kind && o.design == design && (!group.valid() || o.vehicle == group)) n += std::max(0, o.amount);
        return n;
    }

    static std::string ratesText(const LaunchRates& lr) {
        std::string out;
        auto add = [&](int n, const char* what) {
            if (n < 0) return;
            out += std::format("{}{} {}", out.empty() ? "Launch per turn: " : ", ", n, what);
        };
        add(lr.fighters, "fighters");
        add(lr.satellites, "satellites");
        add(lr.mines, "mines");
        add(lr.drones, "drones");
        return out.empty() ? std::string("No launch bays") : out;
    }

    void carriersPanel(UiContext& ui, const std::vector<Holder>& holders, ImVec2 size) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        beginPanel(ui, "##carriers", "Units in sector", size);
        int n = 0;
        for (const Holder& h : holders) {
            ImGui::PushID(n++);
            RowStyle st;
            st.lamp = h == holder_ ? Lamp::On : Lamp::Off;
            st.selected = h == holder_;
            const game::Cargo* cargo = nullptr;
            std::vector<game::Order> pending;
            RowClick c;
            if (const game::Vehicle* v = ownVehicle(ui, h.vehicle)) {
                c = row(ui, 0, vehicleMini(ui, *v), v->name, ratesText(launchRates(r, s, *v)), st);
                if (c.right) report_.vehicle(v->id);
                cargo = &v->cargo;
                pending = pendingOf(ui, h);
            } else if (const game::Colony* col = ownColony(ui, h.planet)) {
                c = row(ui, 0, colonySprite(ui, h.planet), s.galaxy.object(h.planet).name, "Launch per turn: 1000 of each kind", st);
                if (c.right) report_.planet(h.planet);
                cargo = &col->cargo;
                pending = pendingOf(ui, h);
            }
            if (c.left) holder_ = h;
            int k = 0;
            for (const game::UnitStack& u : cargo ? cargo->units : std::vector<game::UnitStack>{}) {
                const int64_t launching = pendingCount(pending, game::OrderKind::LaunchUnits, u.design);
                RowStyle is;
                is.indent = 26;
                is.height = 32;
                is.picture = 26;
                const game::Vehicle* v = ownVehicle(ui, h.vehicle);
                is.enabled = v ? canLaunch(r, s, *v, u.design) : isUnitDesign(r, s, u.design) && launchableKind(r, s, u.design);
                std::string detail = std::format("{} in cargo", formatNumber(u.count));
                if (launching > 0) detail += std::format(", launching {}", formatNumber(launching));
                if (row(ui, 100 + k++, designMini(ui, u.design), s.design(u.design).name, detail, is).left && is.enabled)
                    launch(ui, h, u.design, u.count - launching);
            }
            ImGui::PopID();
        }
        if (holders.empty()) ImGui::TextColored(kDim, "No ship here carries or launches units.");
        endPanel(ui, "Click a unit type to launch it.");
    }

    void spacePanel(UiContext& ui, ImVec2 size) {
        const game::GameState& s = ui.state();
        const bool holder = holderValid(ui, holder_);
        const std::vector<game::Order> pending = holder ? pendingOf(ui, holder_) : std::vector<game::Order>{};
        beginPanel(ui, "##space", "Units in space", size);
        int shown = 0;
        for (const game::Vehicle* v : where_ ? ownVehiclesAt(ui, *where_) : std::vector<const game::Vehicle*>{}) {
            if (!isUnitVehicle(ui.rules(), s, *v)) continue;
            ++shown;
            const int64_t recovering = pendingCount(pending, game::OrderKind::RecoverUnits, v->design, v->id);
            std::string detail = std::format("{} unit{}", v->count, v->count == 1 ? "" : "s");
            if (recovering > 0) detail += std::format(", recovering {}", formatNumber(recovering));
            RowStyle st;
            st.enabled = holder;
            const RowClick c = row(ui, static_cast<int>(v->id.value), unitMini(ui, *v), v->name, detail, st);
            if (c.left && holder) recover(ui, holder_, *v, v->count - recovering);
            if (c.right) report_.vehicle(v->id);
        }
        if (shown == 0) ImGui::TextColored(kDim, "No units of yours in space here.");
        endPanel(ui, "Click a group to bring it aboard the selected ship or planet.");
    }

    void pendingPanel(UiContext& ui) {
        const bool holder = holderValid(ui, holder_);
        const std::string caption =
            holder ? std::format("Orders given for {} (click to take one back)", ownerName(ui, ownerOf(ui, holder_))) : std::string("Orders given");
        beginPanel(ui, "##pending", caption, ImVec2(0, ui.px(100)));
        const std::vector<game::Order> pending = holder ? pendingOf(ui, holder_) : std::vector<game::Order>{};
        for (size_t i = 0; i < pending.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(describeOrder(ui, pending[i]).c_str())) takeBack(ui, holder_, pending[i]);
            ImGui::PopID();
        }
        if (pending.empty()) ImGui::TextColored(kDim, "None. Launches and recoveries happen when the turn is processed.");
        endPanel(ui);
    }

    // Fighters, satellites, mines and drones can be launched; troops and platforms never.
    static bool launchableKind(const game::Rules& r, const game::GameState& s, game::DesignId d) {
        const auto t = r.hull(s.design(d).hull).type;
        return t == ruleset::VehicleType::Fighter || t == ruleset::VehicleType::Satellite || t == ruleset::VehicleType::Mine ||
               t == ruleset::VehicleType::Drone;
    }

    bool holderValid(const UiContext& ui, const Holder& h) const {
        return ownVehicle(ui, h.vehicle) != nullptr || (!h.vehicle.valid() && ownColony(ui, h.planet) != nullptr);
    }

    OrderOwner ownerOf(const UiContext& ui, const Holder& h) const {
        if (h.vehicle.valid()) return orderOwner(ui.state(), h.vehicle);
        OrderOwner o;
        o.planet = h.planet;
        return o;
    }

    std::string holderName(const UiContext& ui, const Holder& h) const {
        if (const game::Vehicle* v = ownVehicle(ui, h.vehicle)) return v->name;
        return h.planet.valid() ? ui.state().galaxy.object(h.planet).name : std::string{};
    }

    void launch(UiContext& ui, const Holder& from, game::DesignId design, int64_t available) {
        if (available <= 0) {
            status_.error("All of those are already being launched.");
            return;
        }
        game::Order o{game::OrderKind::LaunchUnits, where_.value_or(game::Location{})};
        o.design = design;
        o.amount = static_cast<int>(stepAmount(step_, available));
        status_.issue(ui, withImmediate(ui.state(), ownerOf(ui, from), o),
                      std::format("{} will launch {} {}.", holderName(ui, from), o.amount, ui.state().design(design).name));
    }

    void recover(UiContext& ui, const Holder& into, const game::Vehicle& group, int64_t available) {
        if (available <= 0) {
            status_.error("That group is already being recovered.");
            return;
        }
        game::Order o{game::OrderKind::RecoverUnits, group.location};
        o.design = group.design;
        o.vehicle = group.id;
        o.amount = static_cast<int>(stepAmount(step_, available));
        status_.issue(ui, withImmediate(ui.state(), ownerOf(ui, into), o),
                      std::format("{} will recover {} {}.", holderName(ui, into), o.amount, ui.state().design(group.design).name));
    }

    void takeBack(UiContext& ui, const Holder& h, const game::Order& order) {
        const OrderOwner owner = ownerOf(ui, h);
        const auto* current = ordersOf(ui.state(), owner);
        if (!current) return;
        std::vector<game::Order> orders = *current;
        if (auto it = std::find(orders.begin(), orders.end(), order); it != orders.end()) orders.erase(it);
        status_.issue(ui, setOrders(owner, std::move(orders), repeatOf(ui.state(), owner)), "Order taken back.");
    }

    void openRemote(UiContext& ui, game::OrderKind kind, const game::Vehicle& carrier) {
        const game::GameState& s = ui.state();
        remoteKind_ = kind;
        remoteVehicle_ = carrier.id;
        std::vector<game::DesignId> designs;
        for (const auto& u : carrier.cargo.units) designs.push_back(u.design);
        for (game::DesignId id : ui.me().designs)
            if (isUnitDesign(ui.rules(), s, id) && !s.design(id).obsolete && std::find(designs.begin(), designs.end(), id) == designs.end())
                designs.push_back(id);
        std::vector<TypeAmountPicker::Choice> choices;
        for (game::DesignId id : designs) choices.push_back({s.design(id).name, id, designMini(ui, id)});
        const bool launching = kind == game::OrderKind::LaunchUnits;
        remote_.open(launching ? "Launch Units Remotely" : "Recover Units Remotely",
                     std::format("{} {}: choose the unit type, then pick the sector on the map.", ownerName(ui, orderOwner(s, carrier.id)),
                                 launching ? "launches units at another sector" : "recovers units at another sector"),
                     std::move(choices));
    }

    std::optional<game::Location> where_;
    Holder holder_;
    Step step_ = Step::All;
    Status status_;
    ReportPopup report_;
    TypeAmountPicker remote_;
    game::OrderKind remoteKind_ = game::OrderKind::LaunchUnits;
    game::VehicleId remoteVehicle_;
};

} // namespace

std::unique_ptr<Screen> makeLaunchRecover(const ScreenArgs& args) { return std::make_unique<LaunchRecoverScreen>(args); }

} // namespace opense4::client::classic
