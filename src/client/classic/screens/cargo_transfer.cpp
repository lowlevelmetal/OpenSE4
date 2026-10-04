// Cargo Transfer (order T, docs/spec/06 §1.3, spec 03 §11), titled "Transfer
// Cargo": two lists of the own cargo holders in a sector (vehicles with cargo
// space and the colonies). The left list ("Cargo From") holds the selected
// ship, base or colony, and with a fleet member its fleet-mates there; the
// right list ("Cargo To") every other holder in the sector, other own
// colonies included (spec 03 §11, confirmed: binary). Select a holder in each
// list, then click a cargo item under one to move it to the one selected in
// the other list, one, five, ten, a hundred or all at a time: population
// between two colonies goes from population to population. The window has no
// order buttons (the Load and Drop Cargo orders have their own buttons, spec
// 06 §7 Q80) and works in both turn styles.
//
// Opened by the Combat Simulator's Change Cargo (§1.10.4, §7 Q80) it works on
// a sandbox of the setup: the left list holds every row of the Combat
// Vehicles list, all sides together, and the right list only the temporary
// Storehouse; cargo moves only between them.

#include "client/classic/screens/combat_logic.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"
#include "client/script/items.hpp"

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
    explicit CargoTransferScreen(const ScreenArgs& args) : sandbox_(args.text == kSimulatorWindow) {
        from_.vehicle = args.vehicle;
        from_.planet = args.vehicle.valid() ? game::ObjectId{} : args.planet;
    }

    ~CargoTransferScreen() override {
        if (sandbox_) simulatorSandboxClosed();
    }
    CargoTransferScreen(const CargoTransferScreen&) = delete;
    CargoTransferScreen& operator=(const CargoTransferScreen&) = delete;

    bool draw(UiContext& ui) override {
        // Opened by the Combat Simulator: the same window over its sandbox.
        if (sandbox_) return drawInSimulatorSandbox(ui, [this](UiContext& sub) { return drawIn(sub); });
        return drawIn(ui);
    }

private:
    bool drawIn(UiContext& ui) {
        const SimulatorSandbox* sim = sandbox_ ? simulatorCargoSandbox() : nullptr;
        if (!sim) locate(ui);
        Dialog d(ui, screenTitle(ScreenId::CargoTransfer), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        // The simulator's Change Cargo: the setup's holders against the Storehouse.
        std::vector<Holder> left, right;
        if (sim) {
            for (const SimulatorCargoHolder& h : sim->holders) left.push_back(Holder{h.vehicle, h.planet});
            if (sim->storehouse.valid()) right.push_back(Holder{{}, sim->storehouse});
        } else {
            for (const Holder& h : holdersHere(ui)) (fromSide(ui, h) ? left : right).push_back(h);
        }
        auto present = [](const std::vector<Holder>& list, const Holder& h) { return std::find(list.begin(), list.end(), h) != list.end(); };
        if (!present(left, from_)) from_ = left.empty() ? Holder{} : left.front();
        if (!present(right, to_) || (!sim && to_ == from_)) {
            to_ = {};
            for (const Holder& h : right)
                if (sim || !(h == from_)) {
                    to_ = h;
                    break;
                }
        }

        d.beginContent();
        if (!sim && !where_) {
            ImGui::TextColored(kDim, "You have nothing that can carry cargo.");
        } else {
            if (!sim) ImGui::TextColored(kDim, "Location: %s", sectorName(ui.state(), *where_, ui.session.player()).c_str());
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
            const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 4.4f;
            holderPanel(ui, "##from", "Cargo From", left, from_, to_, ImVec2(w, h), sim);
            ImGui::SameLine();
            holderPanel(ui, "##to", sim ? "Storehouse" : "Cargo To", right, to_, from_, ImVec2(w, h), sim);
            status_.draw(ui);
        }

        // Move One, Five, Ten, Hundred and All, and nothing else, in every mode (§7 Q80).
        d.beginButtons();
        stepButtons(d, step_, true);
        d.spacer();
        if (d.close()) return false;
        report_.draw(ui);
        return d.keepOpen();
    }

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
        origin_ = from_;
    }

    // Every own holder in the sector: the colonies, and the ships and bases
    // with cargo space or cargo; the selected holder always.
    std::vector<Holder> holdersHere(const UiContext& ui) const {
        std::vector<Holder> out;
        if (!where_) return out;
        for (const game::Colony* c : ownColoniesAt(ui, *where_)) out.push_back(Holder{{}, c->planet});
        for (const game::Vehicle* v : ownVehiclesAt(ui, *where_)) {
            if (isUnitVehicle(ui.rules(), ui.state(), *v)) continue;
            if (game::vehicleCargoCapacity(ui.rules(), ui.state(), *v) > 0 || !v->cargo.empty() || v->id == origin_.vehicle) out.push_back(Holder{v->id, {}});
        }
        return out;
    }

    // The left list's holders: the one the window opened with and, for a
    // fleet member, its fleet-mates at that place (spec 03 §11).
    bool fromSide(const UiContext& ui, const Holder& h) const {
        if (h == origin_) return true;
        const game::Vehicle* opened = ownVehicle(ui, origin_.vehicle);
        const game::Vehicle* v = ownVehicle(ui, h.vehicle);
        return opened && v && opened->fleet.valid() && v->fleet == opened->fleet && v->location == opened->location;
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
                     ImVec2 size, const SimulatorSandbox* sim) {
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
            if (sim && h.planet.valid() && !s.colony(h.planet)) {
                // A neutral object of the setup: listed, holding nothing.
                c = row(ui, 0, objectSprite(ui, s.galaxy.object(h.planet)), s.galaxy.object(h.planet).name, "holds nothing", st);
            } else if (const game::Vehicle* v = ownVehicle(ui, h.vehicle)) {
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
            if (const game::Vehicle* v = ownVehicle(ui, h.vehicle)) script::reportItem(v->name);   // input scripts: rows by name
            else if (h.planet.valid()) script::reportItem(s.galaxy.object(h.planet).name);
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
                    const bool clicked = row(ui, 100 + k++, pic, title, detail, is).left;
                    script::reportItem(title);   // input scripts: cargo lines by what they hold
                    if (clicked) transfer(ui, h, other, item);
                }
            }
            ImGui::PopID();
        }
        if (holders.empty()) ImGui::TextColored(kDim, sim ? "There is no Storehouse: you have no colony to copy." : "Nothing here can hold cargo.");
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

    std::optional<game::Location> where_;
    Holder origin_;   // the selected holder the window opened with
    Holder from_, to_;
    Step step_ = Step::Ten;
    Status status_;
    ReportPopup report_;
    bool sandbox_ = false;   // opened by the Combat Simulator's Change Cargo
};

// Jettison Cargo (order J, docs/spec/03 §8, spec 06 §1.3): "Cargo Present" on
// the left and "Cargo To Be Jettisoned" on the right; Move One, Five, Ten and
// All set how much a click moves (Move All when it opens). OK destroys the
// right-hand list at once, in both turn styles (cmd::JettisonCargo); Cancel
// drops nothing. Each race's population shows its own amount, and exactly
// the lines moved are dropped (OpenSE4's choice over the original window's
// two faults).
class JettisonScreen final : public Screen {
public:
    explicit JettisonScreen(const ScreenArgs& args) : vehicle_(args.vehicle), planet_(args.vehicle.valid() ? game::ObjectId{} : args.planet) {}

    bool draw(UiContext& ui) override {
        Dialog d(ui, screenTitle(ScreenId::JettisonCargo), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::GameState& s = ui.state();
        // Opened with nothing (automation): the homeworld.
        if (!vehicle_.valid() && !planet_.valid())
            if (auto home = homeworld(ui)) planet_ = *home;
        const bool usable = canJettisonFrom(ui.rules(), s, ui.session.player(), vehicle_, planet_);
        if (usable && !loaded_) {
            lists_ = jettisonLists(vehicle_.valid() ? s.vehicle(vehicle_)->cargo : s.colony(planet_)->cargo);
            loaded_ = true;
        }
        d.beginContent();
        if (!usable) {
            ImGui::TextColored(kDim, "Select one of your ships, bases or colonies first.");
        } else {
            const std::string holder = vehicle_.valid() ? s.vehicle(vehicle_)->name : s.galaxy.object(planet_).name;
            ImGui::TextColored(kDim, "%s", holder.c_str());
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
            const float h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.4f;
            linePanel(ui, "##present", "Cargo Present", true, ImVec2(w, h));
            ImGui::SameLine();
            linePanel(ui, "##chosen", "Cargo To Be Jettisoned", false, ImVec2(w, h));
            status_.draw(ui);
        }
        d.beginButtons();
        stepButtons(d, step_);
        d.spacer();
        if (d.button("OK", usable && !lists_.chosen.empty())) {
            if (const auto c = jettisonCommand(lists_, vehicle_, planet_); c && status_.issue(ui, *c)) return false;
        }
        if (d.close(true, "Cancel")) return false;
        return d.keepOpen();
    }

private:
    void linePanel(UiContext& ui, const char* id, const char* caption, bool present, ImVec2 size) {
        const game::GameState& s = ui.state();
        beginPanel(ui, id, caption, size);
        const std::vector<JettisonLine> lines = present ? lists_.present : lists_.chosen;
        for (size_t i = 0; i < lines.size(); ++i) {
            const JettisonLine& l = lines[i];
            std::string title, detail;
            Sprite pic;
            if (l.unit.valid()) {
                title = s.design(l.unit).name;
                detail = std::format("{} unit{}", formatNumber(l.amount), l.amount == 1 ? "" : "s");
                pic = designMini(ui, l.unit);
            } else {
                title = (l.race.valid() && l.race.index() < s.empires.size() ? s.empire(l.race).race.name : std::string("Unknown")) + " Population";
                detail = std::format("{}M", formatNumber(l.amount));
                pic = ui.art.populationMini(l.race.valid() && l.race.index() < s.empires.size() ? s.empire(l.race).race.style : "");
            }
            if (row(ui, static_cast<int>(i), pic, title, detail).left) lists_.move(present, i, step_);
        }
        if (lines.empty()) ImGui::TextColored(kDim, present ? "No cargo." : "Nothing chosen.");
        endPanel(ui, present ? "Click cargo to jettison it." : "Click a line to keep it.");
    }

    game::VehicleId vehicle_;
    game::ObjectId planet_;
    JettisonLists lists_;
    bool loaded_ = false;
    Step step_ = Step::All;
    Status status_;
};

} // namespace

std::unique_ptr<Screen> makeCargoTransfer(const ScreenArgs& args) { return std::make_unique<CargoTransferScreen>(args); }
std::unique_ptr<Screen> makeJettisonCargo(const ScreenArgs& args) { return std::make_unique<JettisonScreen>(args); }

} // namespace opense4::client::classic
