// Cargo Transfer (order T, docs/spec/06 §1.3, spec 03 §11): two lists of the
// own cargo holders in a sector (vehicles with cargo space and the colony).
// Select a holder in each list, then click a cargo item under one to move it
// to the one selected in the other list, one, five, ten or all at a time.
//
// The classic game offers this window only in turn-based games. Our engine
// resolves every game simultaneously, so the immediate transfer stays
// available and the deferred Load / Drop Cargo orders are offered as well.

#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

struct Item {
    game::DesignId unit;   // invalid = population
    game::EmpireId race;
    int64_t count = 0;
};

class CargoTransferScreen final : public Screen {
public:
    explicit CargoTransferScreen(const ScreenArgs& args) {
        from_.vehicle = args.vehicle;
        from_.planet = args.vehicle.valid() ? game::ObjectId{} : args.planet;
    }

    bool draw(UiContext& ui) override {
        locate(ui);
        Dialog d(ui, screenTitle(ScreenId::CargoTransfer), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const std::vector<Holder> holders = holdersHere(ui);
        auto present = [&](const Holder& h) { return std::find(holders.begin(), holders.end(), h) != holders.end(); };
        if (!present(from_)) from_ = holders.empty() ? Holder{} : holders.front();
        if (!present(to_) || to_ == from_) {
            to_ = {};
            for (const Holder& h : holders)
                if (!(h == from_)) {
                    to_ = h;
                    break;
                }
        }

        d.beginContent();
        if (!where_) {
            ImGui::TextColored(kDim, "You have nothing that can carry cargo.");
        } else {
            ImGui::TextColored(kDim, "Location: %s", sectorName(ui.state(), *where_).c_str());
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
            const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 4.4f;
            holderPanel(ui, "##from", "Cargo From", holders, from_, to_, ImVec2(w, h));
            ImGui::SameLine();
            holderPanel(ui, "##to", "Cargo To", holders, to_, from_, ImVec2(w, h));
            status_.draw(ui);
        }

        d.beginButtons();
        stepButtons(d, step_);
        d.spacer();
        const game::Vehicle* orderVehicle = deferredVehicle(ui);
        const bool canOrder = orderVehicle && game::vehicleCargoCapacity(ui.rules(), ui.state(), *orderVehicle) > 0;
        if (d.button("Load Cargo Order", canOrder)) openOrderPicker(ui, game::OrderKind::LoadCargo, *orderVehicle);
        if (d.button("Drop Cargo Order", canOrder)) openOrderPicker(ui, game::OrderKind::DropCargo, *orderVehicle);
        if (d.close()) return false;
        report_.draw(ui);
        if (auto pick = orderPicker_.draw(ui); pick && ownVehicle(ui, orderVehicle_)) {
            game::Order o{orderKind_};
            o.design = pick->design;
            o.amount = pick->amount;
            pickLocationForOrder(ui, orderOwner(ui.state(), orderVehicle_), o,
                                 std::format("{}: pick where", game::displayName(orderKind_)));
            return false;  // the main window takes the pick
        }
        return d.keepOpen();
    }

private:
    void locate(UiContext& ui) {
        if (where_) return;
        const game::GameState& s = ui.state();
        if (const game::Vehicle* v = ownVehicle(ui, from_.vehicle)) where_ = v->location;
        else if (ownColony(ui, from_.planet)) where_ = game::locationOf(s.galaxy, from_.planet);
        else if (auto home = homeworld(ui)) {
            where_ = game::locationOf(s.galaxy, *home);
            from_ = Holder{{}, *home};
        } else if (const game::Vehicle* first = ownVehicle(ui, firstOwnShip(ui))) {
            where_ = first->location;
            from_ = Holder{first->id, {}};
        }
    }

    std::vector<Holder> holdersHere(const UiContext& ui) const {
        std::vector<Holder> out;
        if (!where_) return out;
        for (const game::Colony* c : ownColoniesAt(ui, *where_)) out.push_back(Holder{{}, c->planet});
        for (const game::Vehicle* v : ownVehiclesAt(ui, *where_)) {
            if (isUnitVehicle(ui.rules(), ui.state(), *v)) continue;
            if (game::vehicleCargoCapacity(ui.rules(), ui.state(), *v) > 0 || !v->cargo.empty()) out.push_back(Holder{v->id, {}});
        }
        return out;
    }

    std::vector<Item> itemsOf(const UiContext& ui, const Holder& h) const {
        std::vector<Item> out;
        auto add = [&](const game::Cargo& c, const std::vector<game::PopulationGroup>& population) {
            for (const auto& p : population)
                if (p.millions > 0) out.push_back(Item{{}, p.race, p.millions});
            for (const auto& u : c.units)
                if (u.count > 0) out.push_back(Item{u.design, {}, u.count});
        };
        if (const game::Vehicle* v = ownVehicle(ui, h.vehicle)) add(v->cargo, v->cargo.population);
        else if (const game::Colony* c = ownColony(ui, h.planet)) add(c->cargo, c->population);
        return out;
    }

    int64_t countOf(const UiContext& ui, const Holder& h, const Item& item) const {
        for (const Item& i : itemsOf(ui, h))
            if (i.unit == item.unit && i.race == item.race) return i.count;
        return 0;
    }

    void holderPanel(UiContext& ui, const char* id, const char* caption, const std::vector<Holder>& holders, Holder& mine, const Holder& other,
                     ImVec2 size) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const int64_t popMass = r.setting("Population Mass", 5);
        beginPanel(ui, id, caption, size);
        int n = 0;
        for (const Holder& h : holders) {
            ImGui::PushID(n++);
            RowStyle st;
            st.lamp = h == mine ? Lamp::On : Lamp::Off;
            st.selected = h == mine;
            RowClick c;
            if (const game::Vehicle* v = ownVehicle(ui, h.vehicle)) {
                const std::string space = std::format("{}kT / {}kT", formatNumber(game::cargoSpaceUsed(r, s, v->cargo)),
                                                      formatNumber(game::vehicleCargoCapacity(r, s, *v)));
                c = row(ui, 0, vehicleMini(ui, *v), v->name, space, st);
                if (c.right) report_.vehicle(v->id);
            } else if (const game::Colony* col = ownColony(ui, h.planet)) {
                const std::string space = std::format("{}kT / {}kT, population {}M / {}M", formatNumber(game::cargoSpaceUsed(r, s, col->cargo)),
                                                      formatNumber(game::colonyCargoCapacity(r, s, *col)), formatNumber(col->totalPopulation()),
                                                      formatNumber(game::maxPopulation(r, s, *col)));
                c = row(ui, 0, colonySprite(ui, h.planet), s.galaxy.object(h.planet).name, space, st);
                if (c.right) report_.planet(h.planet);
            }
            if (c.left) mine = h;
            if (h == mine) {
                int k = 0;
                for (const Item& item : itemsOf(ui, h)) {
                    RowStyle is;
                    is.indent = 26;
                    is.height = 32;
                    is.picture = 26;
                    std::string title, detail;
                    Sprite pic;
                    if (item.unit.valid()) {
                        const game::Design& d = s.design(item.unit);
                        title = d.name;
                        detail = std::format("{} unit{} ({}kT)", formatNumber(item.count), item.count == 1 ? "" : "s",
                                             formatNumber(item.count * std::max(1, r.hull(d.hull).tonnage)));
                        pic = designMini(ui, item.unit);
                    } else {
                        const std::string race = item.race.valid() ? s.empire(item.race).race.name : std::string("Unknown");
                        title = race + " Population";
                        detail = std::format("{}M ({}kT)", formatNumber(item.count), formatNumber(item.count * popMass));
                        pic = ui.art.populationMini(item.race.valid() ? s.empire(item.race).race.style : "");
                    }
                    if (row(ui, 100 + k++, pic, title, detail, is).left) transfer(ui, h, other, item);
                }
            }
            ImGui::PopID();
        }
        if (holders.empty()) ImGui::TextColored(kDim, "Nothing here can hold cargo.");
        endPanel(ui, "Click cargo to send it to the holder picked in the other list.");
    }

    void transfer(UiContext& ui, const Holder& from, const Holder& to, const Item& item) {
        const game::GameState& s = ui.state();
        if (!to.valid() || to == from) {
            status_.error("Select a different destination in the other list first.");
            return;
        }
        game::cmd::TransferCargo c;
        c.fromVehicle = from.vehicle;
        c.fromPlanet = from.planet;
        c.toVehicle = to.vehicle;
        c.toPlanet = to.planet;
        c.unitDesign = item.unit;
        c.populationRace = item.race;
        c.amount = stepAmount(step_, item.count);
        const int64_t before = countOf(ui, to, item);
        if (!status_.issue(ui, c)) return;
        const int64_t moved = countOf(ui, to, item) - before;
        const std::string what = item.unit.valid() ? std::format("{} {}", formatNumber(moved), s.design(item.unit).name)
                                                   : std::format("{}M {} population", formatNumber(moved),
                                                                 item.race.valid() ? s.empire(item.race).race.name : std::string("unknown"));
        status_.ok(std::format("Moved {}.", what));
    }

    // Deferred orders go to the vehicle selected in Cargo From, else the one in Cargo To.
    const game::Vehicle* deferredVehicle(const UiContext& ui) const {
        if (const game::Vehicle* v = ownVehicle(ui, from_.vehicle)) return v;
        return ownVehicle(ui, to_.vehicle);
    }

    void openOrderPicker(UiContext& ui, game::OrderKind kind, const game::Vehicle& v) {
        const game::GameState& s = ui.state();
        orderKind_ = kind;
        orderVehicle_ = v.id;
        std::vector<TypeAmountPicker::Choice> choices;
        choices.push_back({ui.me().race.name + " Population", {}, ui.art.populationMini(ui.me().race.style)});
        std::vector<game::DesignId> designs;
        for (game::DesignId id : ui.me().designs)
            if (isUnitDesign(ui.rules(), s, id) && !s.design(id).obsolete) designs.push_back(id);
        for (const auto& u : v.cargo.units)
            if (std::find(designs.begin(), designs.end(), u.design) == designs.end()) designs.push_back(u.design);
        for (game::DesignId id : designs) choices.push_back({s.design(id).name, id, designMini(ui, id)});
        const bool load = kind == game::OrderKind::LoadCargo;
        const std::string owner = ownerName(ui, orderOwner(s, v.id));
        orderPicker_.open(load ? "Load Cargo" : "Drop Cargo",
                          std::format("{} {}: choose the cargo, then pick the location on the map. The order is carried out "
                                      "when the turn is processed.",
                                      load ? "Load cargo onto" : "Drop cargo from", owner),
                          std::move(choices));
    }

    std::optional<game::Location> where_;
    Holder from_, to_;
    Step step_ = Step::Ten;
    Status status_;
    ReportPopup report_;
    TypeAmountPicker orderPicker_;
    game::OrderKind orderKind_ = game::OrderKind::LoadCargo;
    game::VehicleId orderVehicle_;
};

} // namespace

std::unique_ptr<Screen> makeCargoTransfer(const ScreenArgs& args) { return std::make_unique<CargoTransferScreen>(args); }

} // namespace opense4::client::classic
