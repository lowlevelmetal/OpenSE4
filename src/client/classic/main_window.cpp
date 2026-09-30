#include "client/classic/main_window.hpp"

#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace opense4::client::classic {

namespace {

// Classic main-window geometry at 1024×768 (docs/spec/06 §2, spec 07).
constexpr Rect kStatusBar{{0, 0}, {1024, 32}};
constexpr Rect kCommandPanel{{0, 32}, {958, 106}};
constexpr Rect kSystemBackground{{3, 105}, {663, 765}};
constexpr Vec2 kSectorOrigin{8, 110};
constexpr float kSectorSize = 50.0f;
constexpr float kSpriteSize = 36.0f;
constexpr Rect kReportPanel{{668, 108}, {958, 470}};
constexpr Rect kGalaxyPanel{{668, 497}, {1018, 762}};

const Color kFrameBlue = Color::hex(0x2c4f9e);
const Color kFrameBright = Color::hex(0x5b86e0);
const Color kSelectYellow = Color::hex(0xffd040);
const ImVec4 kLabelBlue{0.44f, 0.61f, 1.0f, 1.0f};

// Command buttons: (icon in Main.bmp, window, hotkey label). Two rows of six.
struct CommandButton {
    int icon;
    std::optional<ScreenId> screen;
    const char* tooltip;
};
constexpr std::array<CommandButton, 12> kCommands{{
    {0, ScreenId::GameMenu, "Game Menu (F2)"},
    {1, ScreenId::Designs, "Designs (F3)"},
    {2, ScreenId::Planets, "Planets (F4)"},
    {5, ScreenId::Colonies, "Colonies (F5)"},
    {3, ScreenId::Ships, "Ships \\ Units (F6)"},
    {4, ScreenId::Queues, "Construction Queues (F7)"},
    {6, ScreenId::Research, "Research (F8)"},
    {7, ScreenId::Empires, "Empires (F9)"},
    {8, ScreenId::Log, "Log (F10)"},
    {9, ScreenId::EmpireStatus, "Empire Status (F11)"},
    {10, ScreenId::Help, "Help (F1)"},
    {11, std::nullopt, "End Turn (F12)"},
}};

// Order icons in Orders.bmp: (band, column). Bands are 4 state rows of 34 px.
Sprite orderIcon(Art& art, int band, int column, int state) {
    return art.region("Pictures/Game/Buttons/Orders.bmp", column * 34, (band * 4 + state) * 34, 34, 34, false);
}

Vec2 sectorCenter(game::Sector s) { return kSectorOrigin + Vec2{kSectorSize * (float(s.x) + 0.5f), kSectorSize * (float(s.y) + 0.5f)}; }

std::optional<game::Sector> sectorAt(Vec2 p) {
    if (!kSystemBackground.contains(p)) return std::nullopt;
    const Vec2 rel = (p - kSectorOrigin) / kSectorSize;
    if (rel.x < 0 || rel.y < 0) return std::nullopt;
    const game::Sector s{int(std::floor(rel.x)), int(std::floor(rel.y))};
    return s.valid() ? std::optional(s) : std::nullopt;
}

Color colorOf(uint32_t rgb) { return Color::hex(rgb); }

Vec2 galaxyPos(const game::Galaxy& g, const game::StarSystem& s) {
    const Rect inner = kGalaxyPanel.expanded(-8.0f);
    const float sx = inner.size().x / float(std::max(1, g.width)), sy = inner.size().y / float(std::max(1, g.height));
    return inner.min + Vec2{(float(s.position.x) + 0.5f) * sx, (float(s.position.y) + 0.5f) * sy};
}

} // namespace

// ---- Selection ---------------------------------------------------------------------------

void MainWindow::reset(UiContext& ui) {
    clearSelection();
    const game::GameState& s = ui.state();
    for (const auto& c : s.colonies)
        if (c && c->owner == ui.session.player() && c->homeworld) {
            selectPlanet(ui, c->planet);
            return;
        }
    if (!s.galaxy.systems.empty()) shown_ = s.galaxy.systems.front().id;
}

void MainWindow::clearSelection() {
    sector_.reset();
    object_.reset();
    vehicle_.reset();
    fleet_.reset();
    listMode_ = false;
}

std::vector<game::ObjectId> MainWindow::objectsAt(const UiContext& ui, game::Sector sec) const {
    std::vector<game::ObjectId> out;
    const game::GameState& s = ui.state();
    if (!shown_.valid() || !ui.me().hasExplored(shown_)) return out;
    for (game::ObjectId id : s.galaxy.system(shown_).objects)
        if (s.galaxy.object(id).sector == sec) out.push_back(id);
    return out;
}

std::vector<const game::Vehicle*> MainWindow::vehiclesAt(const UiContext& ui, game::Location where) const {
    std::vector<const game::Vehicle*> out;
    for (const game::Vehicle& v : ui.state().vehicles)
        if (v.location == where && knownVehicle(ui, v)) out.push_back(&v);
    return out;
}

void MainWindow::selectSector(UiContext& ui, game::Sector sec, bool cycle) {
    const auto objects = objectsAt(ui, sec);
    const auto vehicles = vehiclesAt(ui, {shown_, sec});
    const size_t total = objects.size() + vehicles.size();
    const bool same = sector_ && *sector_ == sec;
    clearSelection();
    if (total == 0) return;
    sector_ = sec;
    if (total == 1) {
        if (!objects.empty()) object_ = objects.front();
        else vehicle_ = vehicles.front()->id;
        return;
    }
    (void)cycle;
    (void)same;
    listMode_ = true;
}

void MainWindow::selectVehicle(UiContext& ui, game::VehicleId id) {
    const game::Vehicle* v = ui.state().vehicle(id);
    if (!v) return;
    clearSelection();
    shown_ = v->location.system;
    sector_ = v->location.sector;
    vehicle_ = id;
    if (v->fleet.valid() && v->owner == ui.session.player()) fleet_ = v->fleet;
}

void MainWindow::selectPlanet(UiContext& ui, game::ObjectId p) {
    const game::SpaceObject& o = ui.state().galaxy.object(p);
    clearSelection();
    shown_ = o.system;
    sector_ = o.sector;
    object_ = p;
}

const game::Vehicle* MainWindow::selectedVehicle(const UiContext& ui) const {
    if (!vehicle_) return nullptr;
    return ui.state().vehicle(*vehicle_);
}

const game::Colony* MainWindow::selectedColony(const UiContext& ui) const {
    if (!object_) return nullptr;
    return ui.state().colony(*object_);
}

void MainWindow::cycleVehicle(UiContext& ui, int dir, bool idleOnly) {
    std::vector<game::VehicleId> list;
    for (const game::Vehicle& v : ui.state().vehicles)
        if (v.owner == ui.session.player() && !game::isUnitType(game::vehicleType(ui.rules(), ui.state(), v)) &&
            (!idleOnly || (v.orders.empty() && (!v.fleet.valid() || ui.state().fleet(v.fleet)->orders.empty()))))
            list.push_back(v.id);
    if (list.empty()) return;
    auto it = vehicle_ ? std::find(list.begin(), list.end(), *vehicle_) : list.end();
    size_t i = it == list.end() ? 0 : size_t(it - list.begin());
    if (it != list.end()) i = (i + list.size() + size_t(dir + int(list.size()))) % list.size();
    selectVehicle(ui, list[i]);
}

void MainWindow::cycleFleet(UiContext& ui, int dir) {
    std::vector<game::FleetId> list;
    for (const game::Fleet& f : ui.state().fleets)
        if (f.owner == ui.session.player()) list.push_back(f.id);
    if (list.empty()) return;
    auto it = fleet_ ? std::find(list.begin(), list.end(), *fleet_) : list.end();
    size_t i = it == list.end() ? 0 : (size_t(it - list.begin()) + list.size() + size_t(dir + int(list.size()))) % list.size();
    const game::Fleet* f = ui.state().fleet(list[i]);
    if (f && !f->members.empty()) selectVehicle(ui, f->leader.valid() ? f->leader : f->members.front());
}

void MainWindow::cycleColony(UiContext& ui, int dir) {
    std::vector<game::ObjectId> list;
    for (const auto& c : ui.state().colonies)
        if (c && c->owner == ui.session.player()) list.push_back(c->planet);
    if (list.empty()) return;
    auto it = object_ ? std::find(list.begin(), list.end(), *object_) : list.end();
    size_t i = it == list.end() ? 0 : (size_t(it - list.begin()) + list.size() + size_t(dir + int(list.size()))) % list.size();
    selectPlanet(ui, list[i]);
}

void MainWindow::applyRequests(UiContext& ui) {
    UiRequests& rq = ui.requests;
    if (rq.selectVehicle) selectVehicle(ui, *rq.selectVehicle);
    if (rq.selectPlanet) selectPlanet(ui, *rq.selectPlanet);
    if (rq.focus) {
        shown_ = rq.focus->system;
        selectSector(ui, rq.focus->sector, false);
    }
    if (rq.showSystem) {
        shown_ = *rq.showSystem;
        clearSelection();
    }
    if (rq.pickLocation) {
        pick_ = Pick::Callback;
        pickCallback_ = std::move(rq.pickLocation);
        pickPrompt_ = rq.pickPrompt.empty() ? "Pick a location" : rq.pickPrompt;
    }
    rq.selectVehicle.reset();
    rq.selectPlanet.reset();
    rq.focus.reset();
    rq.showSystem.reset();
    rq.pickLocation = nullptr;
    rq.pickPrompt.clear();
}

// ---- Orders --------------------------------------------------------------------------------

void MainWindow::giveOrder(UiContext& ui, game::Order o) {
    const game::Vehicle* v = selectedVehicle(ui);
    if (!v || v->owner != ui.session.player()) return;
    std::vector<game::Order> orders;
    bool repeat = false;
    if (const game::Fleet* f = ui.state().fleet(v->fleet)) {
        orders = f->orders;
        repeat = f->repeatOrders;
    } else {
        orders = v->orders;
        repeat = v->repeatOrders;
    }
    orders.push_back(o);
    replaceOrders(ui, std::move(orders), repeat);
}

void MainWindow::replaceOrders(UiContext& ui, std::vector<game::Order> orders, bool repeat) {
    const game::Vehicle* v = selectedVehicle(ui);
    if (!v || v->owner != ui.session.player()) return;
    game::cmd::SetOrders c;
    if (v->fleet.valid()) c.fleet = v->fleet;
    else c.vehicle = v->id;
    c.orders = std::move(orders);
    c.repeat = repeat;
    ui.session.issue(c);
}

void MainWindow::startPick(UiContext&, Pick p, std::string prompt) {
    pick_ = p;
    pickPrompt_ = std::move(prompt);
    patrol_.clear();
}

void MainWindow::completePick(UiContext& ui, game::Location where, std::optional<game::ObjectId> object) {
    const game::GameState& s = ui.state();
    const Pick p = pick_;
    pick_ = Pick::None;
    switch (p) {
        case Pick::None: return;
        case Pick::MoveTo: giveOrder(ui, game::Order{game::OrderKind::MoveTo, where}); return;
        case Pick::Warp:
            for (game::ObjectId id : s.galaxy.system(where.system).objects)
                if (s.galaxy.object(id).kind == game::ObjectKind::WarpPoint && s.galaxy.object(id).sector == where.sector) {
                    game::Order o{game::OrderKind::Warp, where};
                    o.object = id;
                    giveOrder(ui, o);
                    return;
                }
            return;
        case Pick::Colonize: {
            for (game::ObjectId id : game::planetsAt(s, where))
                if (!s.colony(id) && (!object || *object == id)) {
                    game::Order o{game::OrderKind::Colonize, where};
                    o.object = id;
                    giveOrder(ui, o);
                    return;
                }
            return;
        }
        case Pick::Attack: {
            for (const game::Vehicle* v : vehiclesAt(ui, where))
                if (v->owner != ui.session.player()) {
                    game::Order o{game::OrderKind::Attack, where};
                    o.vehicle = v->id;
                    giveOrder(ui, o);
                    return;
                }
            for (game::ObjectId id : game::planetsAt(s, where))
                if (const game::Colony* c = s.colony(id); c && c->owner != ui.session.player()) {
                    game::Order o{game::OrderKind::Attack, where};
                    o.object = id;
                    giveOrder(ui, o);
                    return;
                }
            return;
        }
        case Pick::Patrol:
            patrol_.push_back(where);
            pick_ = Pick::Patrol;  // keep collecting until Enter
            return;
        case Pick::LoadCargo:
        case Pick::DropCargo: {
            game::Order o{p == Pick::LoadCargo ? game::OrderKind::LoadCargo : game::OrderKind::DropCargo, where};
            o.design = pickDesign_;
            o.amount = -1;
            giveOrder(ui, o);
            return;
        }
        case Pick::Callback:
            if (pickCallback_) pickCallback_(where);
            pickCallback_ = nullptr;
            return;
    }
}

std::vector<MainWindow::OrderButton> MainWindow::availableOrders(UiContext& ui) {
    std::vector<OrderButton> out;
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    Art& art = ui.art;
    const game::EmpireId me = ui.session.player();

    if (const game::Vehicle* v = selectedVehicle(ui); v && v->owner == me) {
        const game::VehicleId id = v->id;
        const game::Fleet* fleet = s.fleet(v->fleet);
        const game::DesignStats st = game::computeDesignStats(r, nullptr, s.design(v->design));
        const bool mobile = st.movement > 0 || fleet;
        const auto abilities = game::vehicleAbilities(r, s, *v);
        const bool hasWarp = !s.galaxy.warpPoints(v->location.system).empty();
        const std::vector<game::Order>& current = fleet ? fleet->orders : v->orders;
        const bool repeat = fleet ? fleet->repeatOrders : v->repeatOrders;
        auto simple = [&](game::OrderKind k) { return [this, &ui, k] { giveOrder(ui, game::Order{k}); }; };
        out.push_back({"Move", "Move To (M)", orderIcon(art, 0, 0, 0), mobile, [this, &ui] { startPick(ui, Pick::MoveTo, "Move To: pick a destination"); }});
        out.push_back({"Wpt", "Move To Waypoint", orderIcon(art, 0, 1, 0), mobile, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::SelectWaypoint, a);
                       }});
        out.push_back({"Warp", "Warp (W)", orderIcon(art, 0, 5, 0), mobile && hasWarp, [this, &ui] { startPick(ui, Pick::Warp, "Warp: pick a warp point"); }});
        out.push_back({"Attack", "Attack (A)", orderIcon(art, 0, 4, 0), st.armed(), [this, &ui] { startPick(ui, Pick::Attack, "Attack: pick a target"); }});
        out.push_back({"Colonize", "Colonize (C)", orderIcon(art, 0, 2, 0), st.canColonizeRock || st.canColonizeIce || st.canColonizeGas,
                       [this, &ui] { startPick(ui, Pick::Colonize, "Colonize: pick a planet"); }});
        out.push_back({"Explore", "Explore (E)", orderIcon(art, 0, 3, 0), mobile, simple(game::OrderKind::Explore)});
        out.push_back({"Supply", "Resupply (S)", orderIcon(art, 1, 12, 0), mobile, simple(game::OrderKind::Resupply)});
        out.push_back({"Repair", "Repair (R)", orderIcon(art, 0, 9, 0), mobile, simple(game::OrderKind::Repair)});
        out.push_back({"Sentry", "Sentry (Y)", orderIcon(art, 0, 12, 0), true, simple(game::OrderKind::Sentry)});
        out.push_back({"Patrol", "Set Patrol (P): pick points, then Enter", orderIcon(art, 0, 13, 0), mobile,
                       [this, &ui] { startPick(ui, Pick::Patrol, "Patrol: pick points, Enter to finish"); }});
        out.push_back({"Repeat", repeat ? "Repeat Orders: on (K)" : "Repeat Orders: off (K)", orderIcon(art, 0, 14, repeat ? 2 : 0), true,
                       [this, &ui, current, repeat] { replaceOrders(ui, current, !repeat); }});
        out.push_back({"Clear", "Clear Orders (Del)", orderIcon(art, 0, 10, 0), !current.empty(), [this, &ui] { replaceOrders(ui, {}, false); }});
        out.push_back({"Fleet", "Fleet Transfer (F)", orderIcon(art, 0, 6, 0), true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::FleetTransfer, a);
                       }});
        const bool cargo = game::vehicleCargoCapacity(r, s, *v) > 0;
        out.push_back({"Load", "Load Cargo (L)", orderIcon(art, 0, 16, 0), cargo, [this, &ui] {
                           pickDesign_ = {};
                           startPick(ui, Pick::LoadCargo, "Load population: pick where");
                       }});
        out.push_back({"Drop", "Drop Cargo (D)", orderIcon(art, 0, 15, 0), cargo && !v->cargo.empty(), [this, &ui] {
                           pickDesign_ = {};
                           startPick(ui, Pick::DropCargo, "Drop population: pick where");
                       }});
        out.push_back({"Cargo", "Cargo Transfer (T)", orderIcon(art, 1, 9, 0), cargo, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::CargoTransfer, a);
                       }});
        out.push_back({"Units", "Launch \\ Recover Units (U)", orderIcon(art, 1, 10, 0), cargo, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::LaunchRecover, a);
                       }});
        const bool cloak = game::hasAbility(abilities, game::AbilityKind::CloakLevel);
        if (cloak) {
            out.push_back({"Cloak", "Cloak (Z)", orderIcon(art, 1, 21, 0), v->status != game::VehicleStatus::Cloaked, simple(game::OrderKind::Cloak)});
            out.push_back({"Decloak", "Decloak (X)", orderIcon(art, 1, 22, 0), v->status == game::VehicleStatus::Cloaked,
                           simple(game::OrderKind::Decloak)});
        }
        if (game::hasAbility(abilities, game::AbilityKind::MineSweeping))
            out.push_back({"Sweep", "Sweep Mines (Ctrl+M)", orderIcon(art, 0, 17, 0), true, simple(game::OrderKind::SweepMines)});
        if (game::vehicleHasSpaceYard(r, s, *v))
            out.push_back({"Queue", "Build Queue (Q)", Sprite{}, true, [&ui, id] {
                               ScreenArgs a;
                               a.vehicle = id;
                               ui.open(ScreenId::SetQueue, a);
                           }});
        out.push_back({"Stellar", "Stellar Manipulation (B)", orderIcon(art, 0, 23, 0), true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::StellarManipulation, a);
                       }});
        out.push_back({"Scrap", "Scrap / Analyze / Mothball (G)", orderIcon(art, 0, 18, 0), true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::Scrap, a);
                       }});
        out.push_back({"Orders", "View Orders (V)", orderIcon(art, 2, 1, 0), true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::ViewOrders, a);
                       }});
        out.push_back({"Name", "Change Name (N)", orderIcon(art, 2, 0, 0), true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::Rename, a);
                       }});
        out.push_back({"Minister", v->minister ? "Minister Control: on" : "Minister Control: off", orderIcon(art, 0, 22, v->minister ? 2 : 0), true,
                       [&ui, id, on = !v->minister] {
                           game::cmd::SetMinister c;
                           c.vehicle = id;
                           c.on = on;
                           ui.session.issue(c);
                       }});
    } else if (const game::Colony* c = selectedColony(ui); c && c->owner == me) {
        const game::ObjectId planet = c->planet;
        out.push_back({"Queue", "Set Construction Queue (Q)", Sprite{}, c->totalPopulation() > 0, [&ui, planet] {
                           ScreenArgs a;
                           a.planet = planet;
                           ui.open(ScreenId::SetQueue, a);
                       }});
        out.push_back({"Cargo", "Cargo Transfer (T)", orderIcon(art, 1, 9, 0), true, [&ui, planet] {
                           ScreenArgs a;
                           a.planet = planet;
                           ui.open(ScreenId::CargoTransfer, a);
                       }});
        out.push_back({"Scrap", "Scrap Facilities", orderIcon(art, 1, 18, 0), !c->facilities.empty(), [&ui, planet] {
                           ScreenArgs a;
                           a.planet = planet;
                           ui.open(ScreenId::Scrap, a);
                       }});
        out.push_back({"Name", "Change Name (N)", orderIcon(art, 2, 0, 0), true, [&ui, planet] {
                           ScreenArgs a;
                           a.planet = planet;
                           ui.open(ScreenId::Rename, a);
                       }});
        out.push_back({"Minister", c->minister ? "Minister Control: on" : "Minister Control: off", orderIcon(art, 0, 22, c->minister ? 2 : 0), true,
                       [&ui, planet, on = !c->minister] {
                           game::cmd::SetMinister m;
                           m.planet = planet;
                           m.on = on;
                           ui.session.issue(m);
                       }});
        out.push_back({"Abandon", "Abandon Planet (Ctrl+A)", orderIcon(art, 1, 23, 0),
                       c->totalPopulation() <= r.setting("Maximum Population For Abandon Planet Order", 50) && !c->homeworld,
                       [&ui, planet] { ui.session.issue(game::cmd::AbandonPlanet{planet}); }});
    }
    return out;
}

// ---- Frame update ------------------------------------------------------------------------

void MainWindow::update(UiContext& ui, bool blocked) {
    if (!shown_.valid() && !ui.state().galaxy.systems.empty()) reset(ui);
    // Selections can vanish when a turn is processed.
    if (vehicle_ && !ui.state().vehicle(*vehicle_)) clearSelection();
    if (fleet_ && !ui.state().fleet(*fleet_)) fleet_.reset();

    statusBar(ui);
    commandPanel(ui);
    reportPanel(ui);
    overlayText(ui);
    if (!blocked) {
        mouse(ui);
        hotkeys(ui);
    }
}

void MainWindow::statusBar(UiContext& ui) {
    const game::Empire& e = ui.me();
    ImGui::SetNextWindowPos(ui.at(kStatusBar.min));
    ImGui::SetNextWindowSize(ui.size(kStatusBar.size()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.px(6), ui.px(5)));
    ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PushFont(ui.fonts.medium, 14.0f * ui.k());
    image(ui, ui.art.flag(e.race.style), {30, 20});
    ImGui::SameLine();
    ImGui::TextUnformatted(std::format("{} {}", e.name, e.empireType).c_str());
    ImGui::SameLine(ui.px(300));
    ImGui::TextColored(kLabelBlue, "Game Date");
    ImGui::SameLine();
    ImGui::TextUnformatted(formatDate(ui.state().turn).c_str());
    ImGui::SameLine(ui.px(520));
    resources(ui, e.stockpile);
    if (ui.session.waitingForOthers()) {
        ImGui::SameLine(ui.px(880));
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Waiting...");
    }
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar();
}

void MainWindow::commandPanel(UiContext& ui) {
    ImGui::SetNextWindowPos(ui.at(kCommandPanel.min));
    ImGui::SetNextWindowSize(ui.size(kCommandPanel.size()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("##commands", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PushFont(ui.fonts.medium, 11.0f * ui.k());
    const ImVec2 origin = ImGui::GetWindowPos();

    // Command buttons.
    for (size_t i = 0; i < kCommands.size(); ++i) {
        const float x = 14.0f + float(i % 6) * 36.0f, y = 4.0f + float(i / 6) * 36.0f;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + ui.px(x), origin.y + ui.px(y)));
        const CommandButton& b = kCommands[i];
        const bool hovered = ImGui::IsMouseHoveringRect(ImGui::GetCursorScreenPos(),
                                                        ImVec2(ImGui::GetCursorScreenPos().x + ui.px(34), ImGui::GetCursorScreenPos().y + ui.px(34)));
        Sprite s = ui.art.commandButton(b.icon, hovered ? 1 : 0);
        ImGui::PushID(int(i));
        bool clicked;
        if (s) {
            clicked = ImGui::InvisibleButton("cmd", ui.size({34, 34}));
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                 ImVec2(s.uv.min.x, s.uv.min.y), ImVec2(s.uv.max.x, s.uv.max.y));
        } else {
            clicked = ImGui::Button(b.tooltip, ui.size({34, 34}));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", b.tooltip);
        ImGui::PopID();
        if (clicked) {
            if (b.screen) ui.open(*b.screen);
            else ui.requests.endTurn = true;
        }
    }

    // Order strip: 2 rows of 14, paged.
    const auto orders = availableOrders(ui);
    constexpr int kPerPage = 28;
    const int pages = std::max(1, int((orders.size() + kPerPage - 1) / kPerPage));
    orderPage_ = std::clamp(orderPage_, 0, pages - 1);
    const float stripX = 240.0f;
    for (int slot = 0; slot < kPerPage; ++slot) {
        const size_t idx = size_t(orderPage_ * kPerPage + slot);
        const float x = stripX + 16.0f + float(slot % 14) * 36.0f, y = 4.0f + float(slot / 14) * 36.0f;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + ui.px(x), origin.y + ui.px(y)));
        if (idx >= orders.size()) {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetCursorScreenPos(),
                                                ImVec2(ImGui::GetCursorScreenPos().x + ui.px(34), ImGui::GetCursorScreenPos().y + ui.px(34)),
                                                IM_COL32(44, 79, 158, 110));
            continue;
        }
        const OrderButton& b = orders[idx];
        ImGui::PushID(int(idx));
        ImGui::BeginDisabled(!b.enabled);
        bool clicked;
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const bool hovered = b.enabled && ImGui::IsMouseHoveringRect(p0, ImVec2(p0.x + ui.px(34), p0.y + ui.px(34)));
        if (b.icon) {
            clicked = ImGui::InvisibleButton("order", ui.size({34, 34}));
            // Hover and disabled looks come from the other state rows of the same column.
            Sprite s = b.icon;
            const float rowH = s.uv.max.y - s.uv.min.y;
            const float shift = !b.enabled ? 3 * rowH : hovered ? rowH : 0.0f;
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                 ImVec2(s.uv.min.x, s.uv.min.y + shift), ImVec2(s.uv.max.x, s.uv.max.y + shift));
        } else {
            clicked = ImGui::Button(b.label, ui.size({34, 34}));
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", b.tooltip);
        ImGui::PopID();
        if (clicked && b.action) b.action();
    }
    if (pages > 1) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + ui.px(stripX), origin.y + ui.px(4)));
        if (ImGui::Button("<", ui.size({14, 70}))) orderPage_ = (orderPage_ + pages - 1) % pages;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + ui.px(stripX + 16 + 14 * 36), origin.y + ui.px(4)));
        if (ImGui::Button(">", ui.size({14, 70}))) orderPage_ = (orderPage_ + 1) % pages;
    }

    // Selection cycles: ship, fleet, colony.
    const float cx = 790.0f;
    const std::array<const char*, 3> names{"Ship", "Fleet", "Colony"};
    for (int row = 0; row < 3; ++row) {
        const float y = 4.0f + float(row) * 24.0f;
        ImGui::PushID(100 + row);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + ui.px(cx), origin.y + ui.px(y)));
        const bool prev = ImGui::Button("<", ui.size({24, 22}));
        ImGui::SetCursorScreenPos(ImVec2(origin.x + ui.px(cx + 26), origin.y + ui.px(y + 3)));
        ImGui::TextUnformatted(names[size_t(row)]);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + ui.px(cx + 90), origin.y + ui.px(y)));
        const bool next = ImGui::Button(">", ui.size({24, 22}));
        ImGui::PopID();
        if (prev || next) {
            const int dir = next ? 1 : -1;
            if (row == 0) cycleVehicle(ui, dir, false);
            else if (row == 1) cycleFleet(ui, dir);
            else cycleColony(ui, dir);
        }
    }

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void MainWindow::reportPanel(UiContext& ui) {
    const game::GameState& s = ui.state();
    ImGui::SetNextWindowPos(ui.at(kReportPanel.min + Vec2{4, 4}));
    ImGui::SetNextWindowSize(ui.size(kReportPanel.size() - Vec2{8, 8}));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.px(4), ui.px(4)));
    ImGui::Begin("##report", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PushFont(ui.fonts.regular, 13.0f * ui.k());

    const bool single = object_ || vehicle_;
    if (single && sector_ && (objectsAt(ui, *sector_).size() + vehiclesAt(ui, {shown_, *sector_}).size()) > 1) {
        if (ImGui::SmallButton("^ List")) {
            object_.reset();
            vehicle_.reset();
            fleet_.reset();
            listMode_ = true;
        }
    }

    if (vehicle_) {
        if (const game::Vehicle* v = s.vehicle(*vehicle_)) {
            if (fleet_) {
                if (const game::Fleet* f = s.fleet(*fleet_)) {
                    fleetReport(ui, *f);
                    ImGui::Separator();
                }
            }
            vehicleReport(ui, *v, tab_);
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui.px(28)));
            tab_ = reportTabs(ui, tab_ == ReportTab::Facilities ? ReportTab::Components : tab_, false);
        }
    } else if (object_) {
        const game::SpaceObject& o = s.galaxy.object(*object_);
        if (o.kind == game::ObjectKind::Planet || o.kind == game::ObjectKind::Asteroids) {
            planetReport(ui, *object_, tab_);
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui.px(28)));
            tab_ = reportTabs(ui, tab_ == ReportTab::Components ? ReportTab::Facilities : tab_, true);
        } else {
            objectReport(ui, *object_);
        }
    } else if (listMode_ && sector_) {
        // Everything in the sector: planets first, then vehicles.
        for (game::ObjectId id : objectsAt(ui, *sector_)) {
            const game::SpaceObject& o = s.galaxy.object(id);
            ImGui::PushID(int(id.value));
            image(ui, objectSprite(ui, o), {30, 30});
            ImGui::SameLine();
            if (ImGui::Selectable(o.name.c_str(), false, 0, ImVec2(0, ui.px(30)))) {
                clearSelection();
                sector_ = o.sector;
                object_ = id;
            }
            ImGui::PopID();
        }
        for (const game::Vehicle* v : vehiclesAt(ui, {shown_, *sector_})) {
            ImGui::PushID(int(v->id.value) + 1000000);
            image(ui, vehicleMini(ui, *v), {30, 30});
            ImGui::SameLine();
            const std::string label = std::format("{}\n{}", v->name, vehicleSummary(ui, *v));
            if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, ui.px(30)))) selectVehicle(ui, v->id);
            ImGui::PopID();
        }
    } else if (shown_.valid()) {
        systemReport(ui, shown_);
    }
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar();
}

void MainWindow::overlayText(UiContext& ui) {
    const game::GameState& s = ui.state();
    if (!shown_.valid()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float k = ui.k();
    ImFont* font = ui.fonts.medium;
    auto text = [&](Vec2 p, float size, ImU32 c, const std::string& str) { dl->AddText(font, size * k, ui.at(p), c, str.c_str()); };
    const game::StarSystem& sys = s.galaxy.system(shown_);
    text({14, 114}, 16, IM_COL32_WHITE, sys.name);
    if (!ui.me().hasExplored(shown_)) {
        text({250, 420}, 18, IM_COL32(150, 160, 190, 255), "Unexplored system");
    } else {
        std::map<game::Sector, int> counts;
        for (game::ObjectId id : sys.objects) ++counts[s.galaxy.object(id).sector];
        for (const auto& [sector, n] : counts)
            if (n > 1) text(sectorCenter(sector) + Vec2{10, 8}, 11, IM_COL32_WHITE, std::to_string(n));
        const auto& known = ui.me().knowledge.knownWarpLink;
        for (game::ObjectId id : s.galaxy.warpPoints(sys.id)) {
            const game::SpaceObject& wp = s.galaxy.object(id);
            if (!wp.destination.valid() || id.index() >= known.size() || !known[id.index()]) continue;
            const std::string& dest = s.galaxy.system(s.galaxy.object(wp.destination).system).name;
            const ImVec2 size = font->CalcTextSizeA(11 * k, FLT_MAX, 0.0f, dest.c_str());
            const ImVec2 c = ui.at(sectorCenter(wp.sector) + Vec2{0, 18});
            dl->AddText(font, 11 * k, ImVec2{c.x - size.x * 0.5f, c.y}, IM_COL32(184, 200, 255, 255), dest.c_str());
        }
    }
    // Ship counts in sectors with several visible vehicles.
    std::map<game::Sector, int> shipCounts;
    for (const game::Vehicle& v : s.vehicles)
        if (v.location.system == shown_ && knownVehicle(ui, v)) ++shipCounts[v.location.sector];
    for (const auto& [sector, n] : shipCounts)
        if (n > 1) text(sectorCenter(sector) + Vec2{10, -22}, 11, IM_COL32(255, 230, 140, 255), std::to_string(n));

    if (pick_ != Pick::None) {
        const std::string prompt = pickPrompt_ + "   (Esc to cancel)";
        text({14, 745}, 14, IM_COL32(255, 220, 90, 255), prompt);
    }
    // Hovered system name on the galaxy panel.
    if (ImGui::IsMousePosValid()) {
        const ImGuiIO& io = ImGui::GetIO();
        const Vec2 p = ui.map.fromFb(Vec2{io.MousePos.x, io.MousePos.y} * ui.fbScale);
        if (kGalaxyPanel.contains(p)) {
            for (const game::StarSystem& sy : s.galaxy.systems)
                if (distance(galaxyPos(s.galaxy, sy), p) < 6.0f && ui.me().hasExplored(sy.id)) text({676, 740}, 12, IM_COL32(220, 230, 255, 255), sy.name);
        }
    }
}

void MainWindow::mouse(UiContext& ui) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse || !ImGui::IsMousePosValid()) return;
    const Vec2 p = ui.map.fromFb(Vec2{io.MousePos.x, io.MousePos.y} * ui.fbScale);
    const game::GameState& s = ui.state();
    hover_ = sectorAt(p);
    const bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (!left && !right) return;

    if (kGalaxyPanel.contains(p)) {
        if (right) {
            ui.open(ScreenId::GalaxyMap);
            return;
        }
        std::optional<game::SystemId> best;
        float bestDist = 10.0f;
        for (const game::StarSystem& sys : s.galaxy.systems)
            if (const float d = distance(galaxyPos(s.galaxy, sys), p); d < bestDist) {
                bestDist = d;
                best = sys.id;
            }
        if (best) {
            shown_ = *best;
            if (pick_ == Pick::None) clearSelection();
        }
        return;
    }

    if (auto sec = sectorAt(p)) {
        const game::Location where{shown_, *sec};
        if (pick_ != Pick::None) {
            if (right && pick_ == Pick::Patrol) {
                std::vector<game::Order> orders;
                for (const auto& l : patrol_) orders.push_back(game::Order{game::OrderKind::MoveTo, l});
                if (!orders.empty()) replaceOrders(ui, std::move(orders), true);
                pick_ = Pick::None;
                return;
            }
            completePick(ui, where, std::nullopt);
            return;
        }
        // Right click on a sector with an own mobile vehicle selected: Move To (an OpenSE4 shortcut).
        if (right) {
            if (const game::Vehicle* v = selectedVehicle(ui); v && v->owner == ui.session.player() && v->location != where) {
                giveOrder(ui, game::Order{game::OrderKind::MoveTo, where});
                return;
            }
        }
        selectSector(ui, *sec, true);
    }
}

void MainWindow::hotkeys(UiContext& ui) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;

    // Windows (F1..F12).
    const std::array<std::pair<ImGuiKey, ScreenId>, 11> windows{{{ImGuiKey_F1, ScreenId::Help},
                                                                  {ImGuiKey_F2, ScreenId::GameMenu},
                                                                  {ImGuiKey_F3, ScreenId::Designs},
                                                                  {ImGuiKey_F4, ScreenId::Planets},
                                                                  {ImGuiKey_F5, ScreenId::Colonies},
                                                                  {ImGuiKey_F6, ScreenId::Ships},
                                                                  {ImGuiKey_F7, ScreenId::Queues},
                                                                  {ImGuiKey_F8, ScreenId::Research},
                                                                  {ImGuiKey_F9, ScreenId::Empires},
                                                                  {ImGuiKey_F10, ScreenId::Log},
                                                                  {ImGuiKey_F11, ScreenId::EmpireStatus}}};
    for (const auto& [key, screen] : windows)
        if (pressed(key)) ui.open(screen);
    if (pressed(ImGuiKey_F12)) ui.requests.endTurn = true;

    if (pressed(ImGuiKey_Escape)) {
        if (pick_ != Pick::None) pick_ = Pick::None;
        else clearSelection();
    }
    if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) {
        if (pick_ == Pick::Patrol) {
            std::vector<game::Order> orders;
            for (const auto& l : patrol_) orders.push_back(game::Order{game::OrderKind::MoveTo, l});
            if (!orders.empty()) replaceOrders(ui, std::move(orders), true);
            pick_ = Pick::None;
        } else if (pick_ == Pick::None) {
            ui.requests.endTurn = true;
        }
    }

    // Selection cycling.
    if (pressed(ImGuiKey_Space) || (ctrl && pressed(ImGuiKey_N))) cycleVehicle(ui, 1, !ctrl);
    if (ctrl && pressed(ImGuiKey_B)) cycleVehicle(ui, -1, false);
    if (ctrl && pressed(ImGuiKey_F)) cycleFleet(ui, 1);
    if (ctrl && pressed(ImGuiKey_D)) cycleFleet(ui, -1);
    if (ctrl && pressed(ImGuiKey_C)) cycleColony(ui, 1);
    if (ctrl && pressed(ImGuiKey_X)) cycleColony(ui, -1);
    if (ctrl && pressed(ImGuiKey_L)) showMovementLines_ = !showMovementLines_;

    // Waypoints: Alt+0..9 sets at the selected sector; Ctrl+0..9 moves there.
    for (int i = 0; i < 10; ++i) {
        const ImGuiKey key = static_cast<ImGuiKey>(ImGuiKey_0 + i);
        if (!pressed(key)) continue;
        if (alt && sector_) {
            game::Waypoint w;
            w.name = std::format("Waypoint {}", i);
            w.location = {shown_, *sector_};
            w.set = true;
            ui.session.issue(game::cmd::SetWaypoint{i, w});
        } else if (ctrl) {
            game::Order o{game::OrderKind::MoveToWaypoint};
            o.amount = i;
            giveOrder(ui, o);
        }
    }
    if (ctrl || alt || shift) return;

    // Orders by hotkey: find the matching button so the same enable rules apply.
    const std::array<std::pair<ImGuiKey, const char*>, 22> orderKeys{{{ImGuiKey_M, "Move"},
                                                                      {ImGuiKey_W, "Warp"},
                                                                      {ImGuiKey_A, "Attack"},
                                                                      {ImGuiKey_C, "Colonize"},
                                                                      {ImGuiKey_S, "Supply"},
                                                                      {ImGuiKey_R, "Repair"},
                                                                      {ImGuiKey_Delete, "Clear"},
                                                                      {ImGuiKey_Backspace, "Clear"},
                                                                      {ImGuiKey_F, "Fleet"},
                                                                      {ImGuiKey_Q, "Queue"},
                                                                      {ImGuiKey_T, "Cargo"},
                                                                      {ImGuiKey_U, "Units"},
                                                                      {ImGuiKey_L, "Load"},
                                                                      {ImGuiKey_D, "Drop"},
                                                                      {ImGuiKey_Y, "Sentry"},
                                                                      {ImGuiKey_E, "Explore"},
                                                                      {ImGuiKey_P, "Patrol"},
                                                                      {ImGuiKey_K, "Repeat"},
                                                                      {ImGuiKey_B, "Stellar"},
                                                                      {ImGuiKey_V, "Orders"},
                                                                      {ImGuiKey_G, "Scrap"},
                                                                      {ImGuiKey_N, "Name"}}};
    std::optional<std::vector<OrderButton>> buttons;
    for (const auto& [key, label] : orderKeys) {
        if (!pressed(key)) continue;
        if (!buttons) buttons = availableOrders(ui);
        for (const OrderButton& b : *buttons)
            if (std::string_view(b.label) == label && b.enabled && b.action) {
                b.action();
                break;
            }
    }
    if (pressed(ImGuiKey_Z) || pressed(ImGuiKey_X)) {
        if (!buttons) buttons = availableOrders(ui);
        const char* want = pressed(ImGuiKey_Z) ? "Cloak" : "Decloak";
        for (const OrderButton& b : *buttons)
            if (std::string_view(b.label) == want && b.enabled && b.action) b.action();
    }
}

// ---- World rendering -----------------------------------------------------------------------

void MainWindow::render(gfx::Renderer2D& r, UiContext& ui) {
    // Frame chrome.
    r.rect(Rect{{0, 0}, {kFrameW, kFrameH}}, Color::hex(0x02040a));
    r.rect(kStatusBar, Color::hex(0x060c1c));
    r.rect(kCommandPanel, Color::hex(0x0a1224));
    for (const Rect& panel : {kStatusBar, kCommandPanel, kSystemBackground, kReportPanel, kGalaxyPanel}) r.rectOutline(panel, 1.5f, kFrameBright);
    r.rect(Rect{{960, 108}, {1022, 470}}, Color::hex(0x07101f));
    r.rect(kReportPanel.expanded(-1.0f), Color::hex(0x040914));
    r.rectOutline(Rect{{0, 0}, {kFrameW, kFrameH}}, 2.0f, kFrameBlue);
    drawSystem(r, ui);
    drawGalaxy(r, ui);
}

void MainWindow::drawSystem(gfx::Renderer2D& r, UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::Rules& rules = ui.rules();
    if (!shown_.valid()) return;
    const game::StarSystem& sys = s.galaxy.system(shown_);
    const bool explored = ui.me().hasExplored(shown_);
    if (Sprite bg = ui.art.systemBackground(rules.data().systemTypes[sys.type.index()].backgroundBitmap))
        r.sprite(bg.tex, kSystemBackground, bg.uv);
    else r.rect(kSystemBackground, Color::hex(0x000000));

    auto spriteAt = [&](const Sprite& sp, Vec2 c, float size, Color tint = {}) {
        const Rect dst = Rect::fromCenter(c, {size * 0.5f, size * 0.5f});
        if (sp) r.sprite(sp.tex, dst, sp.uv, tint);
        return static_cast<bool>(sp);
    };

    if (explored) {
        std::map<game::Sector, std::vector<game::ObjectId>> bySector;
        for (game::ObjectId id : sys.objects) bySector[s.galaxy.object(id).sector].push_back(id);
        for (const auto& [sector, ids] : bySector) {
            game::ObjectId shown = ids.front();
            for (game::ObjectId id : ids)
                if (s.galaxy.object(id).kind == game::ObjectKind::Star) shown = id;
            const game::SpaceObject& o = s.galaxy.object(shown);
            const Vec2 c = sectorCenter(sector);
            if (!spriteAt(objectSprite(ui, o), c, kSpriteSize))
                r.disc(c, 12.0f, o.kind == game::ObjectKind::Star ? Color::hex(0xffe080) : Color::hex(0x8090a0));
            for (game::ObjectId id : ids)
                if (const game::Colony* col = s.colony(id)) {
                    // Population bar in the owner's colour at the top right.
                    const int64_t maxPop = std::max<int64_t>(1, game::maxPopulation(rules, s, *col));
                    const float fill = float(std::min<int64_t>(col->totalPopulation(), maxPop)) / float(maxPop);
                    const Rect bar = Rect::fromPosSize(c + Vec2{12, -20}, {8, 3});
                    r.rect(bar, Color::hex(0x202020));
                    r.rect(Rect::fromPosSize(bar.min, {8 * std::max(0.15f, fill), 3}), colorOf(s.empire(col->owner).color));
                }
        }
    }

    // Vehicles: one sprite per sector (the largest hull), flags when several empires share it.
    std::map<game::Sector, std::vector<const game::Vehicle*>> ships;
    for (const game::Vehicle& v : s.vehicles)
        if (v.location.system == shown_ && knownVehicle(ui, v)) ships[v.location.sector].push_back(&v);
    for (auto& [sector, list] : ships) {
        const Vec2 c = sectorCenter(sector);
        const bool hasObject = explored && !objectsAt(ui, sector).empty();
        const Vec2 at = hasObject ? c + Vec2{-12, 12} : c;
        const float size = hasObject ? 24.0f : kSpriteSize;
        std::sort(list.begin(), list.end(), [&](const game::Vehicle* a, const game::Vehicle* b) {
            return rules.hull(s.design(a->design).hull).tonnage > rules.hull(s.design(b->design).hull).tonnage;
        });
        bool multi = false;
        for (const game::Vehicle* v : list) multi = multi || v->owner != list.front()->owner;
        if (multi) {
            float dx = 0;
            std::vector<game::EmpireId> owners;
            for (const game::Vehicle* v : list)
                if (std::find(owners.begin(), owners.end(), v->owner) == owners.end()) owners.push_back(v->owner);
            for (game::EmpireId e : owners) {
                if (!spriteAt(ui.art.flag(s.empire(e).race.style, false), at + Vec2{dx - 8, 0}, 14))
                    r.rect(Rect::fromCenter(at + Vec2{dx - 8, 0}, {6, 4}), colorOf(s.empire(e).color));
                dx += 16;
            }
        } else if (!spriteAt(vehicleMini(ui, *list.front()), at, size)) {
            r.triangle(at + Vec2{0, -9}, at + Vec2{-7, 7}, at + Vec2{7, 7}, colorOf(s.empire(list.front()->owner).color));
        }
    }

    // Movement line for the selected own vehicle.
    if (const game::Vehicle* v = selectedVehicle(ui); v && showMovementLines_ && v->owner == ui.session.player()) {
        const game::Fleet* f = s.fleet(v->fleet);
        const auto& orders = f ? f->orders : v->orders;
        game::Location from = v->location;
        for (const game::Order& o : orders) {
            game::Location to = o.location;
            if (o.kind == game::OrderKind::Warp || o.kind == game::OrderKind::Colonize)
                if (o.object.valid()) to = game::locationOf(s.galaxy, o.object);
            if (!to.system.valid()) continue;
            if (from.system == shown_ && to.system == shown_)
                r.dashedLine(sectorCenter(from.sector), sectorCenter(to.sector), 1.5f, 6.0f, 4.0f, Color::hex(0x60ff80, 0.85f));
            from = to;
        }
    }

    // Selection brackets and pick hover.
    auto brackets = [&](game::Sector sec, Color col) {
        const Vec2 c = sectorCenter(sec);
        const float h = kSectorSize * 0.5f - 2.0f, l = 8.0f;
        for (float sx : {-1.0f, 1.0f})
            for (float sy : {-1.0f, 1.0f}) {
                const Vec2 corner = c + Vec2{sx * h, sy * h};
                r.line(corner, corner - Vec2{sx * l, 0}, 2.0f, col);
                r.line(corner, corner - Vec2{0, sy * l}, 2.0f, col);
            }
    };
    if (sector_) brackets(*sector_, kSelectYellow.withAlpha(0.75f + 0.25f * float(std::sin(ui.time * 5.0))));
    if (pick_ != Pick::None && hover_) brackets(*hover_, Color::hex(0x60ff80));
    for (const auto& w : ui.me().waypoints)
        if (w.set && w.location.system == shown_) r.ring(sectorCenter(w.location.sector), 5.0f, 1.5f, Color::hex(0x40d0ff));
}

void MainWindow::drawGalaxy(gfx::Renderer2D& r, UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::Galaxy& g = s.galaxy;
    const game::Empire& me = ui.me();
    r.rect(kGalaxyPanel, Color::hex(0x02050c));
    const Rect inner = kGalaxyPanel.expanded(-8.0f);
    const float sx = inner.size().x / float(std::max(1, g.width)), sy = inner.size().y / float(std::max(1, g.height));
    for (int x = 0; x <= g.width; x += 2)
        r.line({inner.min.x + float(x) * sx, inner.min.y}, {inner.min.x + float(x) * sx, inner.max.y}, 1.0f, Color::hex(0x10224a, 0.8f));
    for (int y = 0; y <= g.height; y += 2)
        r.line({inner.min.x, inner.min.y + float(y) * sy}, {inner.max.x, inner.min.y + float(y) * sy}, 1.0f, Color::hex(0x10224a, 0.8f));

    const auto& known = me.knowledge.knownWarpLink;
    for (const game::SpaceObject& o : g.objects) {
        if (o.kind != game::ObjectKind::WarpPoint || !o.destination.valid()) continue;
        const bool linkKnown = o.id.index() < known.size() && known[o.id.index()];
        const Vec2 a = galaxyPos(g, g.system(o.system));
        if (linkKnown) {
            if (o.destination < o.id) continue;
            r.line(a, galaxyPos(g, g.system(g.object(o.destination).system)), 1.0f, Color::hex(0x4868a8, 0.9f));
        } else if (me.hasExplored(o.system)) {
            // A stub toward the unknown destination.
            const Vec2 b = galaxyPos(g, g.system(g.object(o.destination).system));
            const Vec2 d = b - a;
            const float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
            r.line(a, a + d * (7.0f / len), 1.0f, Color::hex(0x4868a8, 0.9f));
        }
    }

    // Presence per system: own colour, another empire's colour, or several.
    std::vector<std::vector<game::EmpireId>> presence(g.systems.size());
    auto mark = [&](game::SystemId sys, game::EmpireId e) {
        auto& list = presence[sys.index()];
        if (std::find(list.begin(), list.end(), e) == list.end()) list.push_back(e);
    };
    for (const game::Vehicle& v : s.vehicles)
        if (v.owner.valid() && knownVehicle(ui, v)) mark(v.location.system, v.owner);
    for (const auto& c : s.colonies)
        if (c && (c->owner == me.id || me.hasExplored(g.object(c->planet).system))) mark(g.object(c->planet).system, c->owner);

    for (const game::StarSystem& sys : g.systems) {
        const Vec2 p = galaxyPos(g, sys);
        const auto& who = presence[sys.id.index()];
        Color c = me.hasExplored(sys.id) ? Color::hex(0xc8d0dc) : Color::hex(0x505866);
        if (who.size() == 1) c = colorOf(s.empire(who.front()).color);
        if (who.size() > 1) {
            r.triangle(p + Vec2{0, -4.5f}, p + Vec2{-4, 3.5f}, p + Vec2{4, 3.5f}, Color::hex(0xffffff));
        } else {
            r.ring(p, 3.2f, 1.5f, c);
        }
        if (sys.id == shown_) {
            r.disc(p, 2.0f, kSelectYellow);
            r.ring(p, 6.0f, 1.5f, kSelectYellow);
        }
    }
}

} // namespace opense4::client::classic
