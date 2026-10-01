// Combat Simulator (docs/spec/06 §1.10.4, docs/spec/04 §17): set up a mock
// battle from the player's designs, enemy designs they have seen and the
// objects of the home system, split between ten virtual empires "Race 1" to
// "Race 10"; then fight it in the Tactical Combat window or watch the
// strategies fight it in the Strategic Combat window. The battle runs on a
// sandbox (game/simulator.hpp); the real game never changes.
//
// Left: the combat vehicles, one row per ship, unit group or object, by side
// then the neutral objects; a left-click removes one. Top right: the items;
// a left-click adds one to the side picked below it (one ship, or one unit to
// the side's group of that kind), a right-click opens its report. Bottom
// right: the owner for new items. With Tactical the simulator closes while
// the battle is fought and opens again afterwards with the same setup; with
// Strategic the Strategic Combat window opens over it. Cancel discards the
// setup.

#include "client/classic/reports.hpp"
#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/combat_logic.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/widgets.hpp"

#include "game/design.hpp"
#include "game/query.hpp"
#include "game/simulator.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <optional>

namespace opense4::client::classic {

namespace {

using game::combat::SimulatorFleet;
using game::combat::SimulatorItem;
using game::combat::SimulatorSetup;
using game::combat::SimulatorSide;

constexpr size_t kSides = size_t(game::combat::kSimulatorMaxSides);

Vec2 largeOrigin() { return {(kFrameW - 780.0f) * 0.5f, (kFrameH - 475.0f) * 0.5f}; }

// The ten sides, always listed: side 1 played by hand, the others by the computer at first.
SimulatorSetup defaultSetup(game::EmpireId viewer) {
    SimulatorSetup s;
    s.viewer = viewer;
    for (size_t k = 0; k < kSides; ++k) s.sides.push_back(SimulatorSide{std::format("Race {}", k + 1), k > 0});
    return s;
}

// The setup kept while a tactical simulation is fought (spec 06 §1.10.4).
std::optional<SimulatorSetup>& savedSetup() {
    static std::optional<SimulatorSetup> s;
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
SimulatorSetup demoSetup(const game::Rules& r, const game::GameState& s, game::EmpireId viewer) {
    SimulatorSetup setup = defaultSetup(viewer);
    std::vector<game::DesignId> ships;
    for (game::DesignId d : game::combat::simulatorDesigns(r, s, viewer, true))
        if (s.design(d).owner == viewer && armedShip(r, s, d)) ships.push_back(d);
    if (ships.size() > 3) ships.resize(3);
    for (int side = 0; side < 2; ++side)
        for (game::DesignId d : ships)
            for (int n = 0; n < 2; ++n) setup.items.push_back({SimulatorItem::Kind::Design, d, {}, side, 1});
    setup.seed = 1;
    return setup;
}

// Starts the battle: tactical (the player drives the sides not under computer
// control) or strategic (every side on its strategies). Opens its window.
bool begin(UiContext& ui, SimulatorSetup setup, bool tactical, std::string& message) {
    if (!tactical)
        for (SimulatorSide& side : setup.sides) side.computer = true;
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
    ui.session.startTactical(std::move(fight));
    ui.open(tactical ? ScreenId::TacticalCombat : ScreenId::StrategicCombat);
    return true;
}

class CombatSimulatorScreen final : public Screen {
public:
    explicit CombatSimulatorScreen(const std::string& mode) : demo_(mode == "demo") {
        if (mode == "again" && savedSetup()) setup_ = std::move(*savedSetup());
        savedSetup().reset();
    }
    bool modal() const override { return true; }

    bool draw(UiContext& ui) override {
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        if (!setup_.viewer.valid()) setup_ = demo_ ? demoSetup(ui.rules(), s, me) : defaultSetup(me);
        // The simulation fought by the strategies is watched over this window.
        if (ui.session.tactical()) return true;
        Dialog d(ui, "Combat Simulator", DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        d.beginContent();
        vehicles(ui);
        items(ui);
        owners(ui);
        if (!message_.empty()) {
            ImGui::SetCursorScreenPos(ui.at(largeOrigin() + Vec2{17, 450}));
            ImGui::TextColored(ImVec4(1, 0.72f, 0.45f, 1), "%s", message_.c_str());
        }

        d.beginButtons();
        if (d.tab("Tactical", tactical_)) tactical_ = true;
        if (d.tab("Strategic", !tactical_)) tactical_ = false;
        // No Obsolete: kept with the empire; hides only the player's own obsolete designs.
        if (d.check("No Obsolete", ui.options().simulatorNoObsolete)) {
            game::InterfaceOptions o = ui.options();
            o.simulatorNoObsolete = !o.simulatorNoObsolete;
            ui.setOptions(o);
        }
        if (d.button("Strategies")) ui.open(ScreenId::Strategies);
        if (d.button("Computer Control")) ImGui::OpenPopup("Player Computer Control##sim");
        if (d.button("Fleets For Plr")) ImGui::OpenPopup("Fleets##sim");
        if (d.button("Change Cargo")) ImGui::OpenPopup("Change Cargo##sim");
        const bool beginClicked = d.button("Begin");
        ui.tagItem("combat-simulator:begin");
        if (beginClicked) {
            if (tactical_) savedSetup() = setup_;
            if (begin(ui, setup_, tactical_, message_)) {
                if (tactical_) return false;   // it opens again when the battle is over
            } else {
                savedSetup().reset();
            }
        }
        computerPopup(ui);
        fleetsPopup(ui);
        cargoPopup(ui);
        designPopup(ui);
        report_.draw(ui);
        // Cancel discards the setup (Esc too).
        if (d.close(true, "Cancel")) {
            savedSetup().reset();
            return false;
        }
        return d.keepOpen();
    }

private:
    Sprite designSprite(UiContext& ui, game::DesignId id) const {
        const game::GameState& s = ui.state();
        if (!id.valid() || id.index() >= s.designs.size()) return {};
        const game::Design& d = s.design(id);
        const std::string style = d.owner.valid() && d.owner.index() < s.empires.size() ? s.empire(d.owner).race.style : ui.me().race.style;
        return ui.art.shipMini(style, ui.rules().hull(d.hull));
    }

    // The flag a side shows: that of the empire its first item belongs to (the
    // empire it copies), else the player's (inferred).
    std::string sideStyle(UiContext& ui, int side) const {
        const game::GameState& s = ui.state();
        for (const SimulatorItem& i : setup_.items) {
            if (i.side != side) continue;
            game::EmpireId owner;
            if (i.kind == SimulatorItem::Kind::Design && i.design.valid() && i.design.index() < s.designs.size()) owner = s.design(i.design).owner;
            else if (const game::Colony* c = i.kind == SimulatorItem::Kind::Planet ? s.colony(i.planet) : nullptr) owner = c->owner;
            else continue;
            if (owner.valid() && owner.index() < s.empires.size()) return s.empire(owner).race.style;
        }
        return ui.me().race.style;
    }

    // "Combat vehicles" at (17,75), 295x370: 36 px rows with flag, picture and name.
    void vehicles(UiContext& ui) {
        const game::GameState& s = ui.state();
        const Vec2 o = largeOrigin();
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{17, 58}));
        ImGui::TextColored(kLabelBlue, "Combat Vehicles");
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{17, 75}));
        ImGui::BeginChild("##vehicles", ui.size({295, 370}), ImGuiChildFlags_Borders);
        const std::vector<SimulatorRow> rows = simulatorRows(ui.rules(), s, setup_);
        std::optional<size_t> remove;
        for (size_t k = 0; k < rows.size(); ++k) {
            const SimulatorRow& r = rows[k];
            ImGui::PushID(int(k));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##row", false, 0, ImVec2(0, ui.px(36)))) remove = k;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float x = p.x + ui.px(2);
            if (r.side >= 0)
                if (Sprite flag = ui.art.flag(sideStyle(ui, r.side), false)) {
                    drawSprite(dl, flag, {x, p.y + ui.px(11)}, {x + ui.px(20), p.y + ui.px(25)});
                }
            x += ui.px(24);
            Sprite pic = r.object.valid() && r.object.index() < s.galaxy.objects.size() ? objectSprite(ui, s.galaxy.object(r.object)) : designSprite(ui, r.design);
            if (pic) drawSprite(dl, pic, {x, p.y}, {x + ui.px(36), p.y + ui.px(36)});
            x += ui.px(40);
            const std::string label = r.side < 0   ? std::format("{} (neutral)", r.name)
                                      : r.units > 0 ? std::format("{} x{}  (Race {})", r.name, r.units, r.side + 1)
                                                    : std::format("{}  (Race {})", r.name, r.side + 1);
            dl->AddText({x, p.y + (ui.px(36) - ImGui::GetTextLineHeight()) * 0.5f}, IM_COL32_WHITE, label.c_str());
            ImGui::PopID();
        }
        if (rows.empty()) dimText("Click items on the right to add them to the chosen race; click a vehicle here to remove it.");
        ImGui::EndChild();
        if (remove) {
            simulatorRemove(setup_, rows[*remove]);
            message_.clear();
        }
    }

    // "Items" at (322,75), 250x230: designs and home-system objects.
    void items(UiContext& ui) {
        const game::GameState& s = ui.state();
        const game::EmpireId me = ui.session.player();
        const Vec2 o = largeOrigin();
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{322, 58}));
        ImGui::TextColored(kLabelBlue, "Items");
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{322, 75}));
        ImGui::BeginChild("##items", ui.size({250, 230}), ImGuiChildFlags_Borders);
        const bool noObsolete = ui.options().simulatorNoObsolete;
        auto row = [&](const std::string& id, const Sprite& pic, const std::string& name) -> int {
            ImGui::PushID(id.c_str());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable("##item", false, 0, ImVec2(0, ui.px(24)));
            const bool right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (pic) drawSprite(dl, pic, {p.x + ui.px(2), p.y}, {p.x + ui.px(26), p.y + ui.px(24)});
            dl->AddText({p.x + ui.px(30), p.y + (ui.px(24) - ImGui::GetTextLineHeight()) * 0.5f}, IM_COL32_WHITE, name.c_str());
            ImGui::PopID();
            return clicked ? 1 : right ? 2 : 0;
        };
        for (game::DesignId id : game::combat::simulatorDesigns(ui.rules(), s, me, false)) {
            const game::Design& d = s.design(id);
            if (noObsolete && d.owner == me && d.obsolete) continue;
            const std::string name = d.owner == me || !d.owner.valid() ? d.name : std::format("{} ({})", d.name, s.empire(d.owner).name);
            const int click = row(std::format("d{}", id.value), designSprite(ui, id), name);
            if (click == 1) add(ui, SimulatorItem{SimulatorItem::Kind::Design, id, {}, current_});
            if (click == 2) {
                designReport_ = id;
                ImGui::OpenPopup("Design Report##sim");
            }
        }
        for (game::ObjectId obj : game::combat::simulatorPlanets(s, me)) {
            const game::SpaceObject& so = s.galaxy.object(obj);
            const int click = row(std::format("o{}", obj.value), objectSprite(ui, so), so.name);
            if (click == 1) add(ui, SimulatorItem{SimulatorItem::Kind::Planet, {}, obj, current_});
            if (click == 2) report_.openPlanet(obj);
        }
        ImGui::EndChild();
    }

    // "Owner for item" at (322,325), 250x120: Race 1 to Race 10 with their flags.
    void owners(UiContext& ui) {
        const Vec2 o = largeOrigin();
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{322, 308}));
        ImGui::TextColored(kLabelBlue, "Owner For Item");
        ImGui::SetCursorScreenPos(ui.at(o + Vec2{322, 325}));
        ImGui::BeginChild("##owners", ui.size({250, 120}), ImGuiChildFlags_Borders);
        for (int k = 0; k < int(setup_.sides.size()); ++k) {
            ImGui::PushID(k);
            bool on = current_ == k;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (lampToggle(ui, std::format("      {}", setup_.sides[size_t(k)].name).c_str(), &on) && on) current_ = k;
            if (Sprite flag = ui.art.flag(sideStyle(ui, k), false))
                drawSprite(ImGui::GetWindowDrawList(), flag, {p.x + ui.px(22), p.y + ui.px(2)}, {p.x + ui.px(40), p.y + ui.px(14)});
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void add(UiContext& ui, SimulatorItem item) {
        message_.clear();
        if (!simulatorAdd(ui.rules(), ui.state(), setup_, std::move(item))) message_ = "That is in the battle already.";
    }

    // The Player Computer Control list for the ten sides.
    void computerPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({300, 0}));
        if (!ImGui::BeginPopupModal("Player Computer Control##sim", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextColored(kLabelBlue, "A lit lamp: the computer plays that race.");
        for (size_t k = 0; k < setup_.sides.size(); ++k) {
            bool on = setup_.sides[k].computer;
            if (lampToggle(ui, setup_.sides[k].name.c_str(), &on)) setup_.sides[k].computer = on;
        }
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Fleets For Plr: the chosen race's ships and the one fleet they may form
    // there, with its formation and strategy (inferred: one fleet per race; the
    // original opens Fleet Transfer for the race).
    void fleetsPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({420, 0}));
        if (!ImGui::BeginPopupModal("Fleets##sim", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        const game::GameState& s = ui.state();
        heading(ui, std::format("Fleet of {}", setup_.sides[size_t(current_)].name).c_str());
        int fleet = -1;
        for (size_t f = 0; f < setup_.fleets.size(); ++f)
            if (setup_.fleets[f].side == current_) fleet = int(f);
        const std::vector<std::string> names = game::combat::simulatorItemNames(ui.rules(), s, setup_);
        bool any = false;
        for (size_t k = 0; k < setup_.items.size(); ++k) {
            SimulatorItem& i = setup_.items[k];
            if (i.side != current_ || i.kind != SimulatorItem::Kind::Design || game::isUnitType(ui.rules().hull(s.design(i.design).hull).type)) continue;
            any = true;
            bool in = fleet >= 0 && i.fleet == fleet;
            ImGui::PushID(int(k));
            if (lampToggle(ui, names[k].c_str(), &in)) {
                if (in && fleet < 0) {
                    setup_.fleets.push_back(SimulatorFleet{current_, std::format("{} Fleet", setup_.sides[size_t(current_)].name), 0, 0});
                    fleet = int(setup_.fleets.size()) - 1;
                }
                i.fleet = in ? fleet : -1;
            }
            ImGui::PopID();
        }
        if (!any) dimText("This race has no ships.");
        if (fleet >= 0) {
            SimulatorFleet& f = setup_.fleets[size_t(fleet)];
            const auto& formations = ui.rules().data().formations;
            ImGui::SetNextItemWidth(ui.px(200));
            if (ImGui::BeginCombo("Formation", f.formation < formations.size() ? formations[f.formation].name.c_str() : "None")) {
                for (uint32_t k = 0; k < formations.size(); ++k)
                    if (ImGui::Selectable(formations[k].name.c_str(), f.formation == k)) f.formation = k;
                ImGui::EndCombo();
            }
            // The fleet's strategy is one of its side's list: the strategies of the
            // empire the side copies, the owner of its first item (game/simulator.hpp).
            const auto& list = s.empire(sideSource(ui, current_)).strategies;
            ImGui::SetNextItemWidth(ui.px(200));
            if (ImGui::BeginCombo("Strategy", f.strategy < list.size() ? list[f.strategy].name.c_str() : "Default")) {
                for (uint32_t k = 0; k < list.size(); ++k)
                    if (ImGui::Selectable(list[k].name.c_str(), f.strategy == k)) f.strategy = k;
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // The real empire a side copies: the owner of its first design or colony
    // (neutral objects do not count), else the player's.
    game::EmpireId sideSource(const UiContext& ui, int side) const {
        const game::GameState& s = ui.state();
        for (const SimulatorItem& item : setup_.items) {
            if (item.side != side) continue;
            if (item.kind == SimulatorItem::Kind::Design && item.design.index() < s.designs.size()) {
                const game::EmpireId owner = s.design(item.design).owner;
                return owner.valid() && owner.index() < s.empires.size() ? owner : ui.session.player();
            }
            if (item.kind == SimulatorItem::Kind::Planet && game::combat::simulatorColony(s, item)) return s.colony(item.planet)->owner;
        }
        return ui.session.player();
    }

    // Change Cargo: the units any ship, base or colony of the battle carries
    // (inferred: our own picker; the original opens Cargo Transfer).
    void cargoPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({440, 0}));
        if (!ImGui::BeginPopupModal("Change Cargo##sim", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        const game::GameState& s = ui.state();
        const std::vector<std::string> names = game::combat::simulatorItemNames(ui.rules(), s, setup_);
        std::vector<size_t> holders;
        for (size_t k = 0; k < setup_.items.size(); ++k) {
            const SimulatorItem& i = setup_.items[k];
            if (i.kind == SimulatorItem::Kind::Planet && !game::combat::simulatorColony(s, i)) continue;
            if (game::combat::simulatorCargoCapacity(ui.rules(), s, i) > 0) holders.push_back(k);
        }
        if (holders.empty()) {
            dimText("Nothing in the battle has cargo space.");
        } else {
            if (std::find(holders.begin(), holders.end(), cargoHolder_) == holders.end()) cargoHolder_ = holders.front();
            ImGui::SetNextItemWidth(ui.px(300));
            if (ImGui::BeginCombo("Holder", names[cargoHolder_].c_str())) {
                for (size_t k : holders)
                    if (ImGui::Selectable(std::format("{} (Race {})##h{}", names[k], setup_.items[k].side + 1, k).c_str(), k == cargoHolder_)) cargoHolder_ = k;
                ImGui::EndCombo();
            }
            SimulatorItem& i = setup_.items[cargoHolder_];
            if (i.kind == SimulatorItem::Kind::Planet && !i.replaceCargo) {
                // Start from the colony's own units.
                if (const game::Colony* c = s.colony(i.planet)) i.cargo = c->cargo.units;
                i.replaceCargo = true;
            }
            dimText(std::format("{} of {} kT used", game::combat::simulatorCargoUsed(ui.rules(), s, i), game::combat::simulatorCargoCapacity(ui.rules(), s, i))
                        .c_str());
            for (game::DesignId d : game::combat::simulatorCargoDesigns(ui.rules(), s, ui.session.player(), ui.options().simulatorNoObsolete)) {
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
        }
        if (ImGui::Button("OK", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // A design's report (right-click on an item): its hull and parts.
    void designPopup(UiContext& ui) {
        ImGui::SetNextWindowSize(ui.size({360, 0}));
        if (!ImGui::BeginPopupModal("Design Report##sim", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
        const game::GameState& s = ui.state();
        if (designReport_.valid() && designReport_.index() < s.designs.size()) {
            const game::Design& d = s.design(designReport_);
            image(ui, designSprite(ui, designReport_), {36, 36});
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(d.name.c_str());
            dimText(ui.rules().hull(d.hull).name.c_str());
            ImGui::EndGroup();
            std::map<std::string, int> parts;
            for (const game::DesignEntry& e : d.entries) ++parts[ui.rules().component(e.component).name];
            for (const auto& [name, n] : parts) ImGui::TextUnformatted(n > 1 ? std::format("{} x{}", name, n).c_str() : name.c_str());
        }
        if (ImGui::Button("Close", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    bool demo_ = false;
    SimulatorSetup setup_;
    int current_ = 0;
    bool tactical_ = true;
    std::string message_;
    size_t cargoHolder_ = 0;
    game::DesignId designReport_;
    ReportPopup report_;
};

} // namespace

std::unique_ptr<Screen> makeCombatSimulator(const ScreenArgs& args) { return std::make_unique<CombatSimulatorScreen>(args.text); }

bool startDemoSimulation(UiContext& ui, bool tactical) {
    const SimulatorSetup setup = demoSetup(ui.rules(), ui.state(), ui.session.player());
    std::string message;
    if (!begin(ui, setup, tactical, message)) return false;
    // A couple of turns in, played by the strategies, so there is something to see.
    TacticalFight* f = ui.session.tactical();
    for (int phase = 0; phase < 2 && f->battle->awaitingOrders(); ++phase)
        f->battle->submit(game::combat::TacticalOrder{game::combat::TacticalOrder::Kind::AutoPhase, f->battle->phaseEmpire()});
    f->seen = f->battle->record().events.size();
    return true;
}

} // namespace opense4::client::classic
