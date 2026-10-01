// Combat Simulator (docs/spec/06 §1.2, docs/spec/04 §17): set up a mock
// battle from the player's designs, enemy designs they have seen and sample
// planets of the home system, split between virtual empires; then fight it in
// the Tactical Combat window, or watch the strategies fight it in the
// Strategic Combat window when every side is computer-controlled. The battle runs on
// a sandbox (game/simulator.hpp); the real game never changes.

#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include "game/design.hpp"
#include "game/query.hpp"
#include "game/setup.hpp"
#include "game/simulator.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace opense4::client::classic {

namespace {

using game::combat::SimulatorFleet;
using game::combat::SimulatorItem;
using game::combat::SimulatorSetup;
using game::combat::SimulatorSide;

// Up to 10 virtual empires (confirmed: binary, spec 04 §17); the names are ours.
constexpr size_t kMaxSides = size_t(game::combat::kSimulatorMaxSides);
constexpr std::array<const char*, kMaxSides> kSideNames{"Side 1", "Side 2", "Side 3", "Side 4", "Side 5",
                                                        "Side 6", "Side 7", "Side 8", "Side 9", "Side 10"};

SimulatorSetup defaultSetup(game::EmpireId viewer) {
    SimulatorSetup s;
    s.viewer = viewer;
    s.sides = {SimulatorSide{kSideNames[0], false}, SimulatorSide{kSideNames[1], true}};
    return s;
}

bool armedShip(const game::Rules& r, const game::GameState& s, game::DesignId d) {
    const game::Design& design = s.design(d);
    if (r.hull(design.hull).type != ruleset::VehicleType::Ship) return false;
    for (const game::DesignEntry& e : design.entries)
        if (r.component(e.component).isWeapon() && r.component(e.component).weapon.kind != ruleset::WeaponKind::Warhead) return true;
    return false;
}

// The player's armed ships against copies of themselves (automation and screenshots).
SimulatorSetup demoSetup(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, bool tactical) {
    SimulatorSetup setup = defaultSetup(viewer);
    setup.sides[0].computer = !tactical;
    std::vector<game::DesignId> ships;
    for (game::DesignId d : game::combat::simulatorDesigns(r, s, viewer, true))
        if (s.design(d).owner == viewer && armedShip(r, s, d)) ships.push_back(d);
    if (ships.size() > 3) ships.resize(3);
    for (size_t side = 0; side < 2; ++side)
        for (game::DesignId d : ships) setup.items.push_back({SimulatorItem::Kind::Design, d, {}, int(side), 2});
    setup.seed = 1;
    return setup;
}

bool begin(UiContext& ui, const SimulatorSetup& setup, std::string& message) {
    message = game::combat::simulatorProblem(ui.rules(), ui.state(), setup);
    if (!message.empty()) return false;
    game::combat::Simulation sim = game::combat::buildSimulation(ui.rules(), ui.state(), setup);
    std::vector<game::EmpireId> players = sim.players;
    auto battle = std::make_unique<game::combat::TacticalBattle>(game::combat::startSimulation(ui.rules(), std::move(sim)));
    if (!battle->started()) {
        message = "Nobody on the field can see an enemy.";
        return false;
    }
    TacticalFight fight;
    fight.kind = TacticalFight::Kind::Simulation;
    fight.battle = std::move(battle);
    fight.players = std::move(players);
    fight.title = "Combat Simulator";
    // With every side computer-controlled the strategies fight it: watched in Strategic Combat (spec 06 §1.6).
    const bool strategic = fight.players.empty();
    ui.session.startTactical(std::move(fight));
    ui.open(strategic ? ScreenId::StrategicCombat : ScreenId::TacticalCombat);
    return true;
}

class CombatSimulatorScreen final : public Screen {
public:
    explicit CombatSimulatorScreen(bool demo) : demo_(demo) {}
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        if (!setup_.viewer.valid()) setup_ = demo_ ? demoSetup(ui.rules(), s, me, true) : defaultSetup(me);
        Dialog d(ui, "Combat Simulator", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        sideTabs(ui);
        const float half = (ImGui::GetContentRegionAvail().x - ui.px(8)) * 0.5f;
        const float listH = ui.px(250);
        designList(ui, ImVec2(half, listH));
        ImGui::SameLine(0, ui.px(8));
        itemList(ui, ImVec2(half, listH));
        details(ui);
        if (!message_.empty()) ImGui::TextColored(ImVec4(1, 0.72f, 0.45f, 1), "%s", message_.c_str());

        d.beginButtons();
        if (d.tab("Tactical", tactical_)) setTactical(true);
        if (d.tab("Strategic", !tactical_)) setTactical(false);
        if (d.check("No Obsolete", hideObsolete_)) hideObsolete_ = !hideObsolete_;
        if (d.button("Strategies")) ui.open(ScreenId::Strategies);
        if (d.button("Computer Control")) ImGui::OpenPopup("##computer");
        if (d.button("Fleets for Plr", selectedDesign(ui))) toggleFleet(ui);
        if (d.button("Change Cargo", selected_ >= 0)) ImGui::OpenPopup("##cargo");
        if (d.button("Remove", selected_ >= 0)) removeSelected();
        if (d.button("Add Side", setup_.sides.size() < kMaxSides))
            setup_.sides.push_back(SimulatorSide{kSideNames[setup_.sides.size()], true});
        d.spacer();
        if (d.button("Begin", !setup_.items.empty())) begin(ui, setup_, message_);
        computerPopup(ui);
        cargoPopup(ui);
        if (d.close()) return false;
        return d.keepOpen();
    }

private:
    void setTactical(bool on) {
        tactical_ = on;
        if (!on)
            for (SimulatorSide& side : setup_.sides) side.computer = true;
        else if (std::all_of(setup_.sides.begin(), setup_.sides.end(), [](const SimulatorSide& s) { return s.computer; }))
            setup_.sides.front().computer = false;
    }

    // The owner picker: which virtual empire new items go to.
    void sideTabs(UiContext& ui) {
        // Up to ten sides: five tabs a row.
        for (size_t k = 0; k < setup_.sides.size(); ++k) {
            if (k % 5 != 0) ImGui::SameLine(0, ui.px(6));
            const std::string label = std::format("{}{}", setup_.sides[k].name, setup_.sides[k].computer ? "" : " (you)");
            if (classicButton(ui, label.c_str(), {122, 22}, 1, current_ == int(k))) current_ = int(k);
        }
    }

    void designList(UiContext& ui, ImVec2 size) {
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        ImGui::BeginChild("##designs", size, ImGuiChildFlags_Borders);
        heading(ui, "Designs");
        const std::vector<game::DesignId> designs = game::combat::simulatorDesigns(ui.rules(), s, me, hideObsolete_);
        bool enemies = false;
        for (game::DesignId id : designs) {
            const game::Design& d = s.design(id);
            if (d.owner != me && !enemies) {
                enemies = true;
                ImGui::Spacing();
                heading(ui, "Enemy designs seen");
            }
            const std::string label = std::format("{}  ({}){}##d{}", d.name, ui.rules().hull(d.hull).name,
                                                  d.owner != me && d.owner.valid() ? std::format(" - {}", s.empire(d.owner).name) : std::string{}, id.value);
            if (ImGui::Selectable(label.c_str())) add(SimulatorItem{SimulatorItem::Kind::Design, id, {}, current_, 1});
        }
        ImGui::Spacing();
        heading(ui, "Sample planets (home system)");
        for (game::ObjectId o : game::combat::simulatorPlanets(s, me)) {
            const bool used = std::any_of(setup_.items.begin(), setup_.items.end(),
                                          [&](const SimulatorItem& i) { return i.kind == SimulatorItem::Kind::Planet && i.planet == o; });
            const std::string label = std::format("{}##p{}", s.galaxy.object(o).name, o.value);
            if (ImGui::Selectable(label.c_str(), false, used ? ImGuiSelectableFlags_Disabled : 0))
                add(SimulatorItem{SimulatorItem::Kind::Planet, {}, o, current_, 1});
        }
        ImGui::EndChild();
    }

    void add(SimulatorItem item) {
        message_.clear();
        if (item.kind == SimulatorItem::Kind::Design)
            for (size_t k = 0; k < setup_.items.size(); ++k) {
                SimulatorItem& i = setup_.items[k];
                if (i.kind == item.kind && i.design == item.design && i.side == item.side && i.cargo.empty() && i.fleet < 0) {
                    i.count = std::min(i.count + 1, game::combat::kSimulatorMaxCount);
                    selected_ = int(k);
                    return;
                }
            }
        setup_.items.push_back(std::move(item));
        selected_ = int(setup_.items.size()) - 1;
    }

    std::string itemName(UiContext& ui, const SimulatorItem& i) const {
        const game::GameState& s = ui.state();
        if (i.kind == SimulatorItem::Kind::Planet) return s.galaxy.object(i.planet).name;
        return i.design.index() < s.designs.size() ? s.design(i.design).name : std::string("?");
    }

    void itemList(UiContext& ui, ImVec2 size) {
        ImGui::BeginChild("##items", size, ImGuiChildFlags_Borders);
        heading(ui, "Vehicles in combat");
        for (size_t side = 0; side < setup_.sides.size(); ++side) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(imColor(game::defaultEmpireColor(side))), "%s%s", setup_.sides[side].name.c_str(),
                               setup_.sides[side].computer ? " (computer)" : " (you)");
            for (size_t k = 0; k < setup_.items.size(); ++k) {
                const SimulatorItem& i = setup_.items[k];
                if (i.side != int(side)) continue;
                std::string label = std::format("   {} x{}", itemName(ui, i), i.count);
                if (i.fleet >= 0 && size_t(i.fleet) < setup_.fleets.size()) label += std::format("  [{}]", setup_.fleets[size_t(i.fleet)].name);
                if (!i.cargo.empty() || i.replaceCargo) label += "  +cargo";
                if (ImGui::Selectable(std::format("{}##i{}", label, k).c_str(), selected_ == int(k))) selected_ = int(k);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    selected_ = int(k);
                    removeSelected();
                    break;
                }
            }
        }
        if (setup_.items.empty()) dimText("Click designs on the left to add them to the chosen side.");
        ImGui::EndChild();
    }

    // Count, fleet formation and strategy, and cargo of the selected item. A
    // ship outside a fleet fights with its design's strategy, from the list of
    // the design's real owner (spec 04 §17).
    void details(UiContext& ui) {
        if (selected_ < 0 || size_t(selected_) >= setup_.items.size()) {
            dimText("Right-click an item to remove it.");
            return;
        }
        const game::GameState& s = ui.state();
        const game::Empire& me = ui.me();
        SimulatorItem& i = setup_.items[size_t(selected_)];
        ImGui::TextUnformatted(itemName(ui, i).c_str());
        if (i.kind == SimulatorItem::Kind::Design) {
            ImGui::SameLine(ui.px(200));
            ImGui::SetNextItemWidth(ui.px(110));
            ImGui::SliderInt("##count", &i.count, 1, game::combat::kSimulatorMaxCount, "%d");
            if (const game::Design& d = s.design(i.design); d.owner == me.id && d.strategy < me.strategies.size()) {
                ImGui::SameLine();
                dimText(me.strategies[d.strategy].name.c_str());
            }
        }
        if (i.fleet >= 0 && size_t(i.fleet) < setup_.fleets.size()) {
            SimulatorFleet& f = setup_.fleets[size_t(i.fleet)];
            ImGui::TextColored(kLabelBlue, "%s", f.name.c_str());
            ImGui::SameLine(ui.px(200));
            ImGui::SetNextItemWidth(ui.px(150));
            const auto& formations = ui.rules().data().formations;
            if (ImGui::BeginCombo("##formation", f.formation < formations.size() ? formations[f.formation].name.c_str() : "No formation")) {
                for (uint32_t k = 0; k < formations.size(); ++k)
                    if (ImGui::Selectable(formations[k].name.c_str(), f.formation == k)) f.formation = k;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ui.px(150));
            const std::string current = f.strategy < me.strategies.size() ? me.strategies[f.strategy].name : std::string("Default");
            if (ImGui::BeginCombo("##fleetstrategy", current.c_str())) {
                for (uint32_t k = 0; k < me.strategies.size(); ++k)
                    if (ImGui::Selectable(me.strategies[k].name.c_str(), f.strategy == k)) f.strategy = k;
                ImGui::EndCombo();
            }
        }
        const int64_t room = game::combat::simulatorCargoCapacity(ui.rules(), s, i);
        if (room > 0) dimText(std::format("Cargo {} of {} kT", game::combat::simulatorCargoUsed(ui.rules(), s, i), room).c_str());
    }

    bool selectedDesign(UiContext& ui) const {
        if (selected_ < 0 || size_t(selected_) >= setup_.items.size()) return false;
        const SimulatorItem& i = setup_.items[size_t(selected_)];
        return i.kind == SimulatorItem::Kind::Design && !game::isUnitType(ui.rules().hull(ui.state().design(i.design).hull).type);
    }

    // Puts the selected ships into their side's fleet (the side's last one, or a new one), or takes them out.
    void toggleFleet(UiContext& ui) {
        SimulatorItem& i = setup_.items[size_t(selected_)];
        if (i.fleet >= 0) {
            i.fleet = -1;
            return;
        }
        for (int f = int(setup_.fleets.size()) - 1; f >= 0; --f)
            if (setup_.fleets[size_t(f)].side == i.side) {
                i.fleet = f;
                return;
            }
        setup_.fleets.push_back(SimulatorFleet{i.side, std::format("{} Fleet", setup_.sides[size_t(i.side)].name), 0, 0});
        i.fleet = int(setup_.fleets.size()) - 1;
        (void)ui;
    }

    void removeSelected() {
        if (selected_ < 0 || size_t(selected_) >= setup_.items.size()) return;
        setup_.items.erase(setup_.items.begin() + selected_);
        selected_ = -1;
    }

    void computerPopup(UiContext& ui) {
        if (!ImGui::BeginPopup("##computer")) return;
        heading(ui, "Computer control");
        for (size_t k = 0; k < setup_.sides.size(); ++k) {
            bool on = setup_.sides[k].computer;
            if (lampToggle(ui, setup_.sides[k].name.c_str(), &on)) setup_.sides[k].computer = on;
        }
        dimText("Sides you control are fought by hand in a tactical run.");
        ImGui::EndPopup();
    }

    void cargoPopup(UiContext& ui) {
        if (!ImGui::BeginPopup("##cargo")) return;
        if (selected_ < 0 || size_t(selected_) >= setup_.items.size()) {
            ImGui::EndPopup();
            return;
        }
        const game::GameState& s = ui.state();
        SimulatorItem& i = setup_.items[size_t(selected_)];
        if (i.kind == SimulatorItem::Kind::Planet && !i.replaceCargo) {
            // Start from the colony's own units.
            if (const game::Colony* c = s.colony(i.planet)) i.cargo = c->cargo.units;
            i.replaceCargo = true;
        }
        heading(ui, std::format("Cargo of {}", itemName(ui, i)).c_str());
        dimText(std::format("{} of {} kT used", game::combat::simulatorCargoUsed(ui.rules(), s, i),
                            game::combat::simulatorCargoCapacity(ui.rules(), s, i)).c_str());
        for (game::DesignId d : game::combat::simulatorCargoDesigns(ui.rules(), s, ui.session.player(), hideObsolete_)) {
            ImGui::PushID(int(d.value));
            auto it = std::find_if(i.cargo.begin(), i.cargo.end(), [&](const game::UnitStack& u) { return u.design == d; });
            int count = it == i.cargo.end() ? 0 : it->count;
            ImGui::TextUnformatted(s.design(d).name.c_str());
            ImGui::SameLine(ui.px(220));
            if (ImGui::SmallButton("-") && count > 0) --count;
            ImGui::SameLine();
            ImGui::Text("%3d", count);
            ImGui::SameLine();
            if (ImGui::SmallButton("+")) ++count;
            ImGui::SameLine();
            if (ImGui::SmallButton("+10")) count += 10;
            if (it == i.cargo.end() && count > 0) i.cargo.push_back({d, count});
            else if (it != i.cargo.end()) {
                if (count > 0) it->count = count;
                else i.cargo.erase(it);
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    bool demo_ = false;
    SimulatorSetup setup_;
    int current_ = 0;
    int selected_ = -1;
    bool tactical_ = true;
    bool hideObsolete_ = true;
    std::string message_;
};

} // namespace

std::unique_ptr<Screen> makeCombatSimulator(const ScreenArgs& args) { return std::make_unique<CombatSimulatorScreen>(args.text == "demo"); }

bool startDemoSimulation(UiContext& ui, bool tactical) {
    const SimulatorSetup setup = demoSetup(ui.rules(), ui.state(), ui.session.player(), tactical);
    std::string message;
    if (!begin(ui, setup, message)) return false;
    // A couple of turns in, played by the strategies, so there is something to see.
    TacticalFight* f = ui.session.tactical();
    for (int phase = 0; phase < 2 && f->battle->awaitingOrders(); ++phase)
        f->battle->submit(game::combat::TacticalOrder{game::combat::TacticalOrder::Kind::AutoPhase, f->battle->phaseEmpire()});
    f->seen = f->battle->record().events.size();
    return true;
}

} // namespace opense4::client::classic
