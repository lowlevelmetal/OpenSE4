#include "client/classic/main_window.hpp"

#include "client/app_settings.hpp"
#include "client/audio.hpp"
#include "client/classic/facility_markers.hpp"
#include "client/classic/map_style.hpp"
#include "client/classic/quadrant_map.hpp"
#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/status_icons.hpp"

#include "game/abilities.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
#include "learn/ids.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <tuple>

namespace opense4::client::classic {

namespace {

// Classic main-window geometry at 1024×768 (docs/spec/06 §2.1, §2.4,
// confirmed: binary): the frame is drawn from the Screens/1024X768 strips,
// the panels sit between them.
constexpr float kSectorSize = 50.0f;   // a system-panel cell
constexpr float kSpriteSize = 36.0f;   // sprites are never scaled
constexpr const char* kScreens = "Pictures/Game/Screens/1024X768/";

// Panel rectangles for the frame's current extent. Classic 4:3 (left 0,
// right 1024) gives the original layout; a wider frame keeps the system view
// and moves the right-hand panels to the right edge, and with enough room puts
// a full-height galaxy map beside the report.
struct Geometry {
    float left = 0, right = kFrameW;
    Rect statusBar{{0, 0}, {1024, 29}};
    Rect commandPanel{{0, 29}, {957, 106}};
    // The system panel: 652×652 at (8,113); its 13 cells start after a 1 px margin.
    Rect systemPanel{{8, 113}, {660, 765}};
    Vec2 sectorOrigin{9, 114};
    Rect reportPanel{{671, 117}, {957, 470}};
    Rect galaxyPanel{{671, 479}, {1013, 763}};
    std::optional<Rect> strip = Rect{{958, 107}, {1020, 472}};  // the circuit-board filler
    std::optional<Rect> gap;   // more filler where a wider frame leaves room beside the report
    float divider = 655;       // x of the Middle strip between the system view and the right panels
    std::optional<float> wideDivider;  // second Middle strip (wide layout)
    bool wide = false;  // side-by-side report and galaxy map
};
Geometry geo;

void layOut(float left, float right) {
    Geometry g;
    g.left = left;
    g.right = right;
    const float extra = right - left - kFrameW;
    if (extra > 0.5f) {
        g.statusBar = Rect{{left, 0}, {right, 29}};
        g.commandPanel = Rect{{left, 29}, {right - 67, 106}};
        g.systemPanel = Rect{{left + 8, 113}, {left + 660, 765}};
        g.sectorOrigin = {left + 9, 114};
        g.divider = left + 655;
        const float x0 = left + 671, x1 = right - 67;
        if (right - 12 - x0 >= 640.0f) {
            // Report, then a full-height galaxy map up to the right edge.
            g.wide = true;
            g.reportPanel = Rect{{x0, 117}, {x0 + 286, 760}};
            g.wideDivider = x0 + 286;
            g.galaxyPanel = Rect{{x0 + 302, 117}, {right - 12, 760}};
            g.strip.reset();
        } else {
            g.reportPanel = Rect{{x1 - 286, 117}, {x1, 470}};
            g.strip = Rect{{x1 + 1, 107}, {x1 + 63, 472}};
            if (x1 - 286 - x0 > 4) g.gap = Rect{{x0 - 1, 107}, {x1 - 289, 472}};
            g.galaxyPanel = Rect{{x0, 479}, {right - 11, 763}};
        }
    }
    geo = g;
}

const Color kSelectYellow = Color::hex(0xffd040);

Color rgb(uint32_t c, float alpha = 1.0f) { return Color::hex(c, alpha); }
Color empireCol(const game::GameState& s, game::EmpireId e) { return rgb(empireRgb(s, e)); }

// The key of an action as the hover hint shows it: "(Key M)", "(Ctrl W)", "(Key F2)".
std::string hintKey(Action a) {
    const KeyChord& c = appSettings().controls.bindings.chords(a)[0];
    if (c.empty()) return {};
    std::string mods;
    if (c.ctrl) mods += "Ctrl ";
    if (c.shift) mods += "Shift ";
    if (c.alt) mods += "Alt ";
    return std::format("({}{})", mods.empty() ? "Key " : mods, ImGui::GetKeyName(c.key));
}

// Command buttons: (icon in Main.bmp, window, name, key). Two rows of six, in the
// original's order (verified against the running game).
struct CommandButton {
    int icon;
    std::optional<ScreenId> screen;   // none: End Turn
    const char* name;
    Action key;
};
constexpr std::array<CommandButton, 12> kCommands{{
    {0, ScreenId::GameMenu, "Game Menu", Action::GameMenu},
    {4, ScreenId::Designs, "Designs", Action::Designs},
    {2, ScreenId::Planets, "Planets", Action::Planets},
    {1, ScreenId::Colonies, "Colonies", Action::Colonies},
    {3, ScreenId::Ships, "Ships \\ Units", Action::Ships},
    {9, ScreenId::Queues, "Construction Queues", Action::Queues},
    {6, ScreenId::Research, "Research", Action::Research},
    {7, ScreenId::Empires, "Empires", Action::Empires},
    {8, ScreenId::Log, "Log", Action::Log},
    {5, ScreenId::EmpireStatus, "Empire Status", Action::EmpireStatus},
    {10, ScreenId::Help, "Help", Action::Help},
    {11, std::nullopt, "End Turn", Action::EndTurn},
}};

// The order strip: 20 columns × 2 rows at fixed places, filled column by
// column (spec 07 §UI); the icon is (band, column) in Orders.bmp, whose bands
// hold four 34-px state rows: normal, hover, lit, dim. One place is empty.
struct OrderSlot {
    std::optional<OrderId> order;
    int band, column;
};
constexpr std::array<std::array<OrderSlot, 2>, 20> kOrderStrip{{
    {{{OrderId::MoveTo, 0, 0}, {OrderId::Warp, 0, 5}}},
    {{{OrderId::MoveToWaypoint, 0, 1}, {OrderId::Colonize, 0, 2}}},
    {{{OrderId::Attack, 0, 4}, {OrderId::FleetTransfer, 0, 6}}},
    {{{OrderId::Resupply, 0, 8}, {OrderId::Repair, 0, 9}}},
    {{{OrderId::ClearOrders, 0, 10}, {OrderId::BuildQueue, 0, 11}}},
    {{{OrderId::CargoTransfer, 2, 10}, {OrderId::LaunchRecover, 2, 9}}},
    {{{OrderId::LoadCargo, 0, 19}, {OrderId::DropCargo, 0, 20}}},
    {{{OrderId::LaunchRemote, 0, 15}, {OrderId::RecoverRemote, 0, 16}}},
    {{{OrderId::Sentry, 0, 12}, {OrderId::Explore, 0, 3}}},
    {{{OrderId::Patrol, 0, 13}, {OrderId::RepeatOrders, 0, 14}}},
    {{{OrderId::StellarManipulation, 1, 5}, {OrderId::ChangeName, 2, 0}}},
    {{{OrderId::Scrap, 0, 18}, {OrderId::Strategy, 1, 0}}},
    {{{OrderId::ViewOrders, 2, 1}, {OrderId::SweepMines, 1, 11}}},
    {{{OrderId::ScrapFacilities, 1, 18}, {OrderId::Jettison, 1, 17}}},
    {{{OrderId::Cloak, 1, 21}, {OrderId::Decloak, 1, 22}}},
    {{{OrderId::UseComponent, 1, 12}, {OrderId::UseFacility, 1, 13}}},
    {{{OrderId::AbandonPlanet, 1, 3}, {OrderId::ConvertResources, 2, 2}}},
    {{{std::nullopt, 2, 8}, {OrderId::Minister, 0, 22}}},
    {{{OrderId::ReplayPlay, 2, 4}, {OrderId::ReplayShip, 2, 5}}},
    {{{OrderId::ReplayStep, 2, 6}, {OrderId::ReplayRewind, 2, 7}}},
}};

Sprite orderCell(Art& art, int band, int column, int state) {
    return art.region("Pictures/Game/Buttons/Orders.bmp", column * 34, (band * 4 + state) * 34, 34, 34, false);
}

// Draws a sprite into an ImGui draw list over a frame-pixel rectangle.
void drawAt(UiContext& ui, ImDrawList* dl, const Sprite& s, Vec2 min, Vec2 size) {
    if (!s) return;
    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), ui.at(min), ui.at(min + size), {s.uv.min.x, s.uv.min.y},
                 {s.uv.max.x, s.uv.max.y});
}

// A sector's cell and its centre: (9 + 50c, 114 + 50r) and (34 + 50c, 139 + 50r) at 1024×768.
Vec2 cellOrigin(game::Sector s) { return geo.sectorOrigin + Vec2{kSectorSize * float(s.x), kSectorSize * float(s.y)}; }
Vec2 sectorCenter(game::Sector s) { return cellOrigin(s) + Vec2{kSectorSize * 0.5f, kSectorSize * 0.5f}; }
// A point on the system grid in sector units (ship_glides.hpp).
Vec2 gridPoint(Vec2 cell) { return geo.sectorOrigin + cell * kSectorSize; }

// A click maps to (x − panel x − margin) div cell, the same for the row;
// anything outside 0..12 is ignored (the margin counts as row or column 0).
std::optional<game::Sector> sectorAt(Vec2 p) {
    if (!geo.systemPanel.contains(p)) return std::nullopt;
    const Vec2 rel = (p - geo.sectorOrigin) / kSectorSize;
    const game::Sector s{static_cast<int>(rel.x), static_cast<int>(rel.y)};  // truncated toward zero
    return s.valid() ? std::optional(s) : std::nullopt;
}

// The galaxy panel: always 68 × 47 cells from its top-left corner (§2.6).
struct GalaxyGrid {
    Vec2 origin;
    float cw = 1, ch = 1;
};
GalaxyGrid galaxyGrid() {
    const map_style::GridCell c = map_style::gridCell(int(geo.galaxyPanel.size().x), int(geo.galaxyPanel.size().y));
    if (!geo.wide) return {geo.galaxyPanel.min, float(c.w), float(c.h)};
    // Our tall panel beside the report (wide frames): square cells, centred.
    const float cell = float(std::min(c.w, c.h));
    const Vec2 extent{cell * map_style::kGridColumns, cell * map_style::kGridRows};
    return {geo.galaxyPanel.min + (geo.galaxyPanel.size() - extent) * 0.5f, cell, cell};
}
Vec2 galaxyCell(const GalaxyGrid& g, const game::StarSystem& s) {
    return g.origin + Vec2{float(s.position.x) * g.cw, float(s.position.y) * g.ch};
}
Vec2 galaxyCenter(const GalaxyGrid& g, const game::StarSystem& s) { return galaxyCell(g, s) + Vec2{g.cw * 0.5f, g.ch * 0.5f}; }

// The minis turned to their heading: hulls that use engines, fighter and drone groups (§2.4).
bool turnsToHeading(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    const ruleset::VehicleSize& hull = r.hull(s.design(v.design).hull);
    return hull.usesEngines || hull.type == ruleset::VehicleType::Fighter || hull.type == ruleset::VehicleType::Drone;
}

} // namespace

// ---- Selection ---------------------------------------------------------------------------

void MainWindow::reset(UiContext& ui) {
    // The homeworld selected here is not a selection the player made.
    const uint64_t made = selections_;
    clearSelection();
    const game::GameState& s = ui.state();
    // Every starting planet is a capital: the one in the home system first.
    const game::EmpireId me = ui.session.player();
    const game::SystemId home = me.valid() && me.index() < s.empires.size() ? s.empire(me).homeSystem : game::SystemId{};
    for (const bool inHome : {true, false})
        for (const auto& c : s.colonies)
            if (c && c->owner == me && c->homeworld && (!inHome || s.galaxy.object(c->planet).system == home)) {
                selectPlanet(ui, c->planet);
                selections_ = made;
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
    ++selections_;
    const auto objects = objectsAt(ui, sec);
    const auto vehicles = vehiclesAt(ui, {shown_, sec});
    const size_t total = objects.size() + vehicles.size();
    const bool same = sector_ && *sector_ == sec;
    clearSelection();
    if (!same) tagged_.clear();  // clicking another sector clears the tags (§2.5)
    (void)cycle;
    if (total == 0) {
        sector_ = sec;  // empty space: the system report, with the sector marked
        return;
    }
    sector_ = sec;
    if (total == 1) {
        if (!objects.empty()) object_ = objects.front();
        else selectVehicle(ui, vehicles.front()->id);
        return;
    }
    listMode_ = true;
}

void MainWindow::selectVehicle(UiContext& ui, game::VehicleId id) {
    const game::Vehicle* v = ui.state().vehicle(id);
    if (!v) return;
    ++selections_;
    const bool sameSector = sector_ && *sector_ == v->location.sector && shown_ == v->location.system;
    clearSelection();
    if (!sameSector) tagged_.clear();
    shown_ = v->location.system;
    sector_ = v->location.sector;
    vehicle_ = id;
    if (v->fleet.valid() && v->owner == ui.session.player()) fleet_ = v->fleet;
}

void MainWindow::selectPlanet(UiContext& ui, game::ObjectId p) {
    ++selections_;
    const game::SpaceObject& o = ui.state().galaxy.object(p);
    clearSelection();
    tagged_.clear();
    shown_ = o.system;
    sector_ = o.sector;
    object_ = p;
}

const game::Vehicle* MainWindow::selectedVehicle(const UiContext& ui) const {
    if (!vehicle_) return nullptr;
    return ui.state().vehicle(*vehicle_);
}

std::vector<std::string> MainWindow::selectionKinds(const UiContext& ui) const {
    std::vector<std::string> out;
    const game::GameState& s = ui.state();
    if (vehicle_) {
        if (const game::Vehicle* v = s.vehicle(*vehicle_)) {
            const ruleset::VehicleType type = game::vehicleType(ui.rules(), s, *v);
            out.emplace_back(type == ruleset::VehicleType::Ship ? "ship" : type == ruleset::VehicleType::Base ? "base" : "unit");
            if (fleet_ && v->owner == ui.session.player()) out.emplace_back("fleet");
        }
    } else if (object_) {
        const game::SpaceObject& o = s.galaxy.object(*object_);
        if (o.kind == game::ObjectKind::Planet || o.kind == game::ObjectKind::Asteroids) {
            out.emplace_back("planet");
            if (const game::Colony* c = s.colony(*object_); c && c->owner == ui.session.player()) out.emplace_back("colony");
        } else if (game::isStarKind(o.kind)) {
            out.emplace_back("star");
        } else if (o.kind == game::ObjectKind::WarpPoint) {
            out.emplace_back("warp-point");
        }
    } else if (listMode_ && sector_) {
        out.emplace_back("sector");
    } else if (shown_.valid()) {
        out.emplace_back("system");
    }
    return out;
}

const game::Colony* MainWindow::selectedColony(const UiContext& ui) const {
    if (!object_) return nullptr;
    return ui.state().colony(*object_);
}

void MainWindow::cycleVehicle(UiContext& ui, int dir, bool idleOnly) {
    // The Next/Previous switches of the Empire Options (spec 06 §1.9, §2.3). "Skip
    // ships under construction" has nothing to skip: our ships appear finished.
    const game::InterfaceOptions& prefs = ui.options();
    const game::Vehicle* current = selectedVehicle(ui);
    std::vector<game::VehicleId> list;
    for (const game::Vehicle& v : ui.state().vehicles) {
        if (v.owner != ui.session.player() || game::isUnitType(game::vehicleType(ui.rules(), ui.state(), v))) continue;
        // "Next ship": in a turn-based game the ships that still have movement
        // points, in a simultaneous game those without orders (spec 03 §17).
        if (idleOnly && ui.session.turnBased() && v.movement <= 0) continue;
        if (idleOnly && !ui.session.turnBased() && !(v.orders.empty() && (!v.fleet.valid() || game::fleetOrders(ui.state(), *ui.state().fleet(v.fleet)).empty())))
            continue;
        if (prefs.skipDamaged && game::vehicleDamageTaken(ui.state(), v) > 0) continue;
        if (prefs.skipInFleets && v.fleet.valid() && !(current && v.id == current->id)) continue;
        // "Stop once per location": skip other ships in the sector we are leaving.
        if (prefs.stopOncePerLocation && current && v.id != current->id && v.location == current->location) continue;
        list.push_back(v.id);
    }
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
    if (const game::Vehicle* leader = f ? game::fleetLeader(ui.state(), *f) : nullptr) selectVehicle(ui, leader->id);
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

bool MainWindow::tagged(game::VehicleId v) const { return std::find(tagged_.begin(), tagged_.end(), v) != tagged_.end(); }

void MainWindow::toggleTag(UiContext& ui, game::VehicleId id) {
    const game::GameState& s = ui.state();
    const game::Vehicle* v = s.vehicle(id);
    if (!v || v->owner != ui.session.player()) return;
    std::vector<game::VehicleId> group{id};
    if (const game::Fleet* f = s.fleet(v->fleet)) group = f->members;  // a fleet is tagged whole
    const bool on = !tagged(id);
    for (game::VehicleId m : group) {
        std::erase(tagged_, m);
        if (on) tagged_.push_back(m);
    }
}

void MainWindow::tagAll(UiContext& ui) {
    if (!sector_) return;
    tagged_.clear();
    for (const game::Vehicle* v : vehiclesAt(ui, {shown_, *sector_}))
        if (v->owner == ui.session.player() && !tagged(v->id)) toggleTag(ui, v->id);
    if (!tagged_.empty()) {
        clearSelection();
        sector_ = ui.state().vehicle(tagged_.front())->location.sector;
        listMode_ = true;
    }
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
        tagged_.clear();
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

LitOrders MainWindow::litNow(UiContext& ui) const {
    OrderSelection sel;
    sel.tagged = tagged_;
    if (vehicle_) sel.vehicle = vehicle_;
    if (fleet_) sel.fleet = fleet_;
    if (!vehicle_ && object_) {
        const game::SpaceObject& o = ui.state().galaxy.object(*object_);
        if (o.kind == game::ObjectKind::Planet || o.kind == game::ObjectKind::Asteroids) sel.planet = object_;
        else sel.other = true;
    }
    if (!vehicle_ && !object_ && (listMode_ || shown_.valid())) sel.other = true;
    // Locked while the turn is being ended or the player waits for others.
    const bool locked = ui.session.waitingForOthers() || (ui.session.turnBased() && !ui.session.myTurn());
    return litOrders(orderFacts(ui.rules(), ui.state(), ui.session.player(), sel, ui.session.turnBased(), locked));
}

std::vector<MainWindow::OrderOwner> MainWindow::orderOwners(UiContext& ui) const {
    const game::GameState& s = ui.state();
    const game::EmpireId me = ui.session.player();
    std::vector<OrderOwner> out;
    auto addVehicle = [&](game::VehicleId id) {
        const game::Vehicle* v = s.vehicle(id);
        if (!v || v->owner != me) return;
        OrderOwner o;
        if (v->fleet.valid()) o.fleet = v->fleet;
        else o.vehicle = id;
        for (const OrderOwner& x : out)
            if (x.fleet == o.fleet && x.vehicle == o.vehicle) return;
        out.push_back(o);
    };
    if (!tagged_.empty()) {
        for (game::VehicleId id : tagged_) addVehicle(id);
    } else if (vehicle_) {
        addVehicle(*vehicle_);
    } else if (const game::Colony* c = selectedColony(ui); c && c->owner == me) {
        out.push_back(OrderOwner{{}, {}, c->planet});
    }
    return out;
}

void MainWindow::giveOrder(UiContext& ui, game::Order o) {
    const game::GameState& s = ui.state();
    for (const OrderOwner& w : orderOwners(ui)) {
        game::cmd::SetOrders c;
        c.vehicle = w.vehicle;
        c.fleet = w.fleet;
        c.planet = w.planet;
        if (const game::Fleet* f = s.fleet(w.fleet)) {
            // The fleet's orders: copies in its members' lists (spec 03 §8).
            c.orders = game::fleetOrders(s, *f);
            c.repeat = game::fleetRepeats(s, *f);
        } else if (const game::Vehicle* v = s.vehicle(w.vehicle)) {
            c.orders = v->orders;
            c.repeat = v->repeatOrders;
        } else if (const game::Colony* col = s.colony(w.planet)) {
            c.orders = col->orders;
            c.repeat = col->repeatOrders;
        }
        c.orders.push_back(o);
        const game::CommandResult r = ui.session.issue(c);
        if (!r.ok) note(ui, r.error);
    }
    orderDone();
}

void MainWindow::replaceOrders(UiContext& ui, std::vector<game::Order> orders, bool repeat) {
    for (const OrderOwner& w : orderOwners(ui)) {
        game::cmd::SetOrders c;
        c.vehicle = w.vehicle;
        c.fleet = w.fleet;
        c.planet = w.planet;
        c.orders = orders;
        c.repeat = repeat;
        const game::CommandResult r = ui.session.issue(c);
        if (!r.ok) note(ui, r.error);
    }
    orderDone();
}

void MainWindow::orderDone() { tagged_.clear(); }

void MainWindow::startPick(UiContext&, Pick p, std::string prompt) {
    pick_ = p;
    pickPrompt_ = std::move(prompt);
    patrol_.clear();
}

void MainWindow::note(UiContext& ui, std::string text) {
    note_ = std::move(text);
    noteUntil_ = ui.time + 5.0;
}

void MainWindow::openFor(UiContext& ui, ScreenId id) {
    ScreenArgs a;
    if (const game::Vehicle* v = selectedVehicle(ui)) {
        a.vehicle = v->id;
        if (fleet_ && (id == ScreenId::Rename)) {
            a.vehicle = {};
            a.fleet = *fleet_;
        }
    } else if (object_) {
        a.planet = *object_;
    }
    ui.open(id, a);
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
            note(ui, "There is no warp point there.");
            return;
        case Pick::Colonize: {
            for (game::ObjectId id : game::planetsAt(s, where))
                if (!s.colony(id) && (!object || *object == id)) {
                    game::Order o{game::OrderKind::Colonize, where};
                    o.object = id;
                    giveOrder(ui, o);
                    return;
                }
            note(ui, "There is no planet to colonize there.");
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
            note(ui, "There is nothing to attack there.");
            return;
        }
        case Pick::Patrol:
            patrol_.push_back(where);
            pick_ = Pick::Patrol;  // keep collecting until Enter or a right click
            return;
        case Pick::LoadCargo:
        case Pick::DropCargo:
        case Pick::LaunchRemote:
        case Pick::RecoverRemote: {
            static constexpr std::array<game::OrderKind, 4> kKinds{game::OrderKind::LoadCargo, game::OrderKind::DropCargo,
                                                                   game::OrderKind::LaunchUnits, game::OrderKind::RecoverUnits};
            game::Order o{kKinds[static_cast<size_t>(p) - static_cast<size_t>(Pick::LoadCargo)], where};
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

void MainWindow::finishPatrol(UiContext& ui) {
    std::vector<game::Order> orders;
    for (const auto& l : patrol_) orders.push_back(game::Order{game::OrderKind::MoveTo, l});
    pick_ = Pick::None;
    if (orders.empty()) return;
    // A patrol of one point is just a move (spec 03 §8 Set Patrol).
    if (orders.size() == 1) giveOrder(ui, orders.front());
    else replaceOrders(ui, std::move(orders), true);
}

void MainWindow::chooseCargo(UiContext& ui, Pick p) {
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::EmpireId me = ui.session.player();
    static constexpr std::array<const char*, 4> kTitles{"Load Cargo", "Drop Cargo", "Launch Units Remotely", "Recover Units Remotely"};
    static constexpr std::array<const char*, 4> kPrompts{"Load Cargo: pick where", "Drop Cargo: pick where", "Launch Units: pick where",
                                                         "Recover Units: pick where"};
    const size_t k = static_cast<size_t>(p) - static_cast<size_t>(Pick::LoadCargo);
    Chooser c;
    c.title = kTitles[k];
    c.note = "Pick what, then the sector.";
    auto add = [&](std::string label, game::DesignId d) {
        c.items.push_back({std::move(label), [this, &ui, p, d, k] {
                               pickDesign_ = d;
                               startPick(ui, p, kPrompts[k]);
                           }});
    };
    // What the selection carries (Drop, Launch) or could carry (Load).
    game::Cargo carried;
    bool population = false;
    for (const OrderOwner& w : orderOwners(ui)) {
        auto take = [&](const game::Cargo& cargo) {
            population = population || cargo.totalPopulation() > 0;
            for (const game::UnitStack& u : cargo.units)
                if (std::none_of(carried.units.begin(), carried.units.end(), [&](const game::UnitStack& x) { return x.design == u.design; }))
                    carried.units.push_back(u);
        };
        if (const game::Fleet* f = s.fleet(w.fleet))
            for (game::VehicleId m : f->members) {
                if (const game::Vehicle* v = s.vehicle(m)) take(v->cargo);
            }
        else if (const game::Vehicle* v = s.vehicle(w.vehicle)) take(v->cargo);
        else if (const game::Colony* col = s.colony(w.planet)) take(col->cargo);
    }
    if (p == Pick::LoadCargo || (p == Pick::DropCargo && population)) add("Population", {});
    if (p == Pick::LoadCargo) {
        for (game::DesignId d : s.empire(me).designs)
            if (game::isUnitType(r.hull(s.design(d).hull).type) && !s.design(d).obsolete) add(s.design(d).name, d);
    } else if (p == Pick::RecoverRemote) {
        // A unit kind, not a design: fighters or satellites (spec 03 §8); any design of the kind names it.
        for (const ruleset::VehicleType kind : {ruleset::VehicleType::Fighter, ruleset::VehicleType::Satellite})
            for (game::DesignId d : s.empire(me).designs)
                if (r.hull(s.design(d).hull).type == kind) {
                    add(kind == ruleset::VehicleType::Fighter ? "Fighters" : "Satellites", d);
                    break;
                }
    } else {
        for (const game::UnitStack& u : carried.units)
            if (p == Pick::DropCargo || r.hull(s.design(u.design).hull).type != ruleset::VehicleType::Troop)
                add(s.design(u.design).name, u.design);
    }
    if (c.items.empty()) {
        note(ui, std::string(kTitles[k]) + ": nothing to choose.");
        return;
    }
    chooser_ = std::move(c);
}

void MainWindow::runOrder(UiContext& ui, OrderId id) {
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::Vehicle* v = selectedVehicle(ui);
    const game::Colony* colony = selectedColony(ui);
    auto simple = [&](game::OrderKind k) { giveOrder(ui, game::Order{k}); };
    switch (id) {
        case OrderId::MoveTo: startPick(ui, Pick::MoveTo, "Move To: pick a destination"); return;
        case OrderId::Warp: startPick(ui, Pick::Warp, "Warp: pick a warp point"); return;
        case OrderId::Colonize: startPick(ui, Pick::Colonize, "Colonize: pick a planet"); return;
        case OrderId::Attack: startPick(ui, Pick::Attack, "Attack: pick a target"); return;
        case OrderId::Patrol: startPick(ui, Pick::Patrol, "Patrol: pick points, then Enter or a right click"); return;
        case OrderId::MoveToWaypoint:
            if (tagged_.empty()) {
                openFor(ui, ScreenId::SelectWaypoint);
                return;
            } else {
                Chooser c;
                c.title = "Select Waypoint";
                const auto& wps = ui.me().waypoints;
                for (size_t i = 0; i < wps.size(); ++i)
                    if (wps[i].set)
                        c.items.push_back({std::format("{} - {}", i, wps[i].name), [this, &ui, i] {
                                               game::Order o{game::OrderKind::MoveToWaypoint};
                                               o.amount = int(i);
                                               giveOrder(ui, o);
                                           }});
                chooser_ = std::move(c);
            }
            return;
        case OrderId::Resupply: simple(game::OrderKind::Resupply); return;
        case OrderId::Repair: simple(game::OrderKind::Repair); return;
        case OrderId::Explore: simple(game::OrderKind::Explore); return;
        case OrderId::Sentry: simple(game::OrderKind::Sentry); return;
        case OrderId::SweepMines: simple(game::OrderKind::SweepMines); return;
        case OrderId::Cloak:
        case OrderId::Decloak:
            audio().play(id == OrderId::Cloak ? "cloakon" : "cloakoff");
            // A colony cloaks at once, never through its order list (spec 01 §6.9).
            if (colony && !v && tagged_.empty()) {
                const game::CommandResult res = ui.session.issue(game::cmd::CloakColony{colony->planet, id == OrderId::Cloak});
                if (!res.ok) note(ui, res.error);
                return;
            }
            simple(id == OrderId::Cloak ? game::OrderKind::Cloak : game::OrderKind::Decloak);
            return;
        case OrderId::ClearOrders: replaceOrders(ui, {}, false); return;
        case OrderId::RepeatOrders: {
            const auto owners = orderOwners(ui);
            if (owners.empty()) return;
            std::vector<game::Order> current;
            bool repeat = false;
            if (const game::Fleet* f = s.fleet(owners.front().fleet)) {
                current = game::fleetOrders(s, *f);
                repeat = game::fleetRepeats(s, *f);
            } else if (const game::Vehicle* x = s.vehicle(owners.front().vehicle)) {
                current = x->orders;
                repeat = x->repeatOrders;
            } else if (const game::Colony* c = s.colony(owners.front().planet)) {
                current = c->orders;
                repeat = c->repeatOrders;
            }
            replaceOrders(ui, std::move(current), !repeat);
            return;
        }
        case OrderId::FleetTransfer: openFor(ui, ScreenId::FleetTransfer); return;
        case OrderId::BuildQueue: openFor(ui, ScreenId::SetQueue); return;
        case OrderId::CargoTransfer: openFor(ui, ScreenId::CargoTransfer); return;
        case OrderId::LaunchRecover: openFor(ui, ScreenId::LaunchRecover); return;
        case OrderId::StellarManipulation: openFor(ui, ScreenId::StellarManipulation); return;
        case OrderId::ChangeName: openFor(ui, ScreenId::Rename); return;
        case OrderId::ViewOrders: openFor(ui, ScreenId::ViewOrders); return;
        case OrderId::ScrapFacilities: openFor(ui, ScreenId::Scrap); return;
        case OrderId::LoadCargo: chooseCargo(ui, Pick::LoadCargo); return;
        case OrderId::DropCargo: chooseCargo(ui, Pick::DropCargo); return;
        case OrderId::LaunchRemote: chooseCargo(ui, Pick::LaunchRemote); return;
        case OrderId::RecoverRemote: chooseCargo(ui, Pick::RecoverRemote); return;
        case OrderId::Scrap:
            if (tagged_.empty()) {
                openFor(ui, ScreenId::Scrap);
                return;
            } else {
                Chooser c;
                c.title = "Scrap";
                c.note = std::format("Scrap the {} tagged objects?", tagged_.size());
                c.items.push_back({"Scrap them", [this, &ui] {
                                       for (game::VehicleId t : std::vector<game::VehicleId>(tagged_)) {
                                           const game::CommandResult res = ui.session.issue(game::cmd::Scrap{t, {}, -1});
                                           if (!res.ok) note(ui, res.error);
                                       }
                                       orderDone();
                                   }});
                chooser_ = std::move(c);
            }
            return;
        case OrderId::Strategy: {
            const game::Fleet* f = fleet_ ? s.fleet(*fleet_) : nullptr;
            if (!f) return;
            Chooser c;
            c.title = "Formation \\ Strategy";
            c.note = f->name;
            const game::FleetId fid = f->id;
            const uint32_t formation = f->formation, strategy = f->strategy;
            c.items.push_back({"Formation", nullptr});
            const auto& formations = r.data().formations;
            for (uint32_t i = 0; i < formations.size(); ++i)
                c.items.push_back({formations[i].name, [&ui, fid, i, strategy] { ui.session.issue(game::cmd::SetFleetOptions{fid, i, strategy}); },
                                   i == formation});
            c.items.push_back({"Strategy", nullptr});
            const auto& strategies = ui.me().strategies;
            for (uint32_t i = 0; i < strategies.size(); ++i)
                c.items.push_back({strategies[i].name, [&ui, fid, formation, i] { ui.session.issue(game::cmd::SetFleetOptions{fid, formation, i}); },
                                   i == strategy});
            chooser_ = std::move(c);
            return;
        }
        // "Select Component": each position of the selected vehicle whose part
        // is intact and has Emergency Resupply or Emergency Energy; "Select
        // Facility": each facility position of the colony with either ability.
        // The order records the position (spec 03 §8).
        case OrderId::UseComponent: {
            if (!v) return;
            Chooser c;
            c.title = "Select Component";
            c.note = v->name;
            const game::Design& d = s.design(v->design);
            for (size_t i = 0; i < d.entries.size(); ++i) {
                const auto abilities = r.componentAbilities(d.entries[i].component);
                if (!game::entryIntact(r, s, *v, i) ||
                    (!game::hasAbility(abilities, game::AbilityKind::EmergencyResupply) && !game::hasAbility(abilities, game::AbilityKind::EmergencyEnergy)))
                    continue;
                c.items.push_back({r.component(d.entries[i].component).name, [this, &ui, i] {
                                       game::Order o{game::OrderKind::UseComponent};
                                       o.amount = int(i);
                                       giveOrder(ui, o);
                                   }});
            }
            if (!c.items.empty()) chooser_ = std::move(c);
            return;
        }
        case OrderId::UseFacility: {
            if (!colony) return;
            Chooser c;
            c.title = "Select Facility";
            c.note = s.galaxy.object(colony->planet).name;
            for (size_t i = 0; i < colony->facilities.size(); ++i) {
                const auto abilities = r.facilityAbilities(colony->facilities[i]);
                if (!game::hasAbility(abilities, game::AbilityKind::EmergencyResupply) && !game::hasAbility(abilities, game::AbilityKind::EmergencyEnergy))
                    continue;
                c.items.push_back({r.facility(colony->facilities[i]).name, [this, &ui, i] {
                                       game::Order o{game::OrderKind::UseFacility};
                                       o.amount = int(i);
                                       giveOrder(ui, o);
                                   }});
            }
            if (!c.items.empty()) chooser_ = std::move(c);
            return;
        }
        case OrderId::AbandonPlanet:
            // Always lit; the population limit is checked after the player confirms (§2.8).
            openFor(ui, ScreenId::AbandonPlanet);
            return;
        case OrderId::Minister: {
            if (!tagged_.empty()) {
                const game::Vehicle* first = s.vehicle(tagged_.front());
                const bool on = !(first && first->minister);
                for (game::VehicleId t : std::vector<game::VehicleId>(tagged_)) ui.session.issue(game::cmd::SetMinister{t, {}, false, on});
                orderDone();
            } else if (v) {
                ui.session.issue(game::cmd::SetMinister{v->id, {}, false, !v->minister});
            } else if (colony) {
                ui.session.issue(game::cmd::SetMinister{{}, colony->planet, false, !colony->minister});
            }
            return;
        }
        case OrderId::Jettison: openFor(ui, ScreenId::JettisonCargo); return;
        case OrderId::ConvertResources: openFor(ui, ScreenId::ConvertResources); return;
        case OrderId::ReplayPlay:
        case OrderId::ReplayShip:
            if (!replay_.available()) note(ui, "No movement to replay yet.");
            replay_.play(ui.time);
            return;
        case OrderId::ReplayStep: replay_.step(); return;
        case OrderId::ReplayRewind: replay_.rewind(); return;
        case OrderId::Count: return;
    }
}

// ---- Frame update ------------------------------------------------------------------------

void MainWindow::update(UiContext& ui, bool blocked) {
    layOut(ui.map.left, ui.map.right);
    trackMovement(ui);
    if (!shown_.valid() && !ui.state().galaxy.systems.empty()) reset(ui);
    // Selections can vanish when a turn is processed.
    if (vehicle_ && !ui.state().vehicle(*vehicle_)) clearSelection();
    if (fleet_ && !ui.state().fleet(*fleet_)) fleet_.reset();
    std::erase_if(tagged_, [&](game::VehicleId t) {
        const game::Vehicle* v = ui.state().vehicle(t);
        return !v || v->owner != ui.session.player();
    });

    hintName_.clear();
    hintKey_.clear();
    statusBar(ui);
    commandPanel(ui);
    reportPanel(ui);
    overlayText(ui);
    if (ui.lessonRunning) lessonButton(ui);
    // The panels lessons point at (docs/LEARNING.md "UI tags").
    ui.tagFrame("panel:system", geo.systemPanel);
    ui.tagFrame("panel:report", geo.reportPanel);
    ui.tagFrame("panel:galaxy", geo.galaxyPanel);
    // The picker takes the keys while it is open (Esc closes it and nothing else).
    const bool choosing = chooser_.has_value();
    if (choosing) drawChooser(ui);
    if (!blocked && !choosing) {
        mouse(ui);
        hotkeys(ui);
    } else {
        galaxyHover_.reset();
    }
}

void MainWindow::drawChooser(UiContext& ui) {
    const std::string title = chooser_->title + "##main-chooser";
    Dialog d(ui.painter(), title.c_str(), DialogSize::Report, 0.0f);
    bool close = false;
    std::function<void()> chosen;
    if (d.open()) {
        d.beginContent();
        if (!chooser_->note.empty()) ImGui::TextColored(kLabelBlue, "%s", chooser_->note.c_str());
        ImGui::BeginChild("##choices", ImVec2(0, ImGui::GetContentRegionAvail().y - ui.px(34)));
        for (size_t i = 0; i < chooser_->items.size(); ++i) {
            const Choice& c = chooser_->items[i];
            ImGui::PushID(int(i));
            if (!c.action) {
                heading(ui, c.label.c_str());
            } else if (ImGui::Selectable(c.label.c_str(), c.chosen, ImGuiSelectableFlags_None, ImVec2(0, ui.px(22)))) {
                chosen = c.action;
                close = true;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (classicButton(ui, "Cancel", {180, 26}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close = true;
    }
    if (close) {
        audio().play("close");
        chooser_.reset();
    }
    if (chosen) chosen();
}

void MainWindow::statusBar(UiContext& ui) {
    // Flag, empire, leader, game date and the treasury, at the original's places.
    const game::Empire& e = ui.me();
    const float x0 = geo.left;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    drawAt(ui, dl, ui.art.flag(e.race.style), {x0 + 15, 8}, {26, 18});
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    auto text = [&](float x, ImU32 color, const std::string& t) { dl->AddText(font, size, ui.at({x, 12}), color, t.c_str()); };
    text(x0 + 47, IM_COL32_WHITE, std::format("{} {}", e.name, e.empireType));
    text(x0 + 231, IM_COL32_WHITE, std::format("{} {}", e.leaderTitle, e.leaderName));
    text(x0 + 421, ImGui::GetColorU32(kLabelBlue), "Game Date");
    text(x0 + 501, IM_COL32_WHITE, formatDate(ui.state().turn));
    static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
    static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
    static constexpr std::array<float, 3> kX{627, 700, 771};
    for (size_t i = 0; i < 3; ++i) {
        const std::string amount = std::to_string(e.stockpile.v[i]);
        text(x0 + kX[i], imColor(kColors[i]), amount);
        const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, amount.c_str()).x / ui.k();
        drawAt(ui, dl, ui.art.icon16(kIcons[i]), {x0 + kX[i] + w + 1, 10}, {14, 14});
    }
    if (ui.session.waitingForOthers()) text(x0 + 860, IM_COL32(255, 204, 77, 255), "Waiting...");
    ui.tagFrame("status:empire", Rect{{x0 + 12, 4}, {x0 + 226, 26}});
    ui.tagFrame("status:leader", Rect{{x0 + 228, 4}, {x0 + 416, 26}});
    ui.tagFrame("status:date", Rect{{x0 + 418, 4}, {x0 + 590, 26}});
    ui.tagFrame("status:resources", Rect{{x0 + 622, 4}, {x0 + 850, 26}});
    for (size_t i = 0; i < 3; ++i) {
        static constexpr std::array<const char*, 3> kTags{"status:minerals", "status:organics", "status:radioactives"};
        ui.tagFrame(kTags[i], Rect{{x0 + kX[i] - 4, 4}, {x0 + (i + 1 < 3 ? kX[i + 1] - 4 : 850.0f), 26}});
    }
}

void MainWindow::lessonButton(UiContext& ui) {
    // The original re-opens its tutorial text with a T button in the status
    // bar (spec 06 §1.7); ours shows the lesson panel again.
    const Vec2 at{geo.right - 40, 4};
    ImGui::SetNextWindowPos(ui.at(at));
    ImGui::SetNextWindowSize(ui.size({24, 22}));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin("##lessonbutton", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (classicButton(ui, "T##lesson", {24, 22})) {
            audio().play("button");
            ui.requests.toggleLessonPanel = true;
        }
        ui.tagItem("status:lesson");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show or hide the lesson (%s)", chordName(appSettings().controls.bindings.chords(Action::LessonText)[0]).c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void MainWindow::commandPanel(UiContext& ui) {
    ImGui::SetNextWindowPos(ui.at(geo.commandPanel.min));
    ImGui::SetNextWindowSize(ui.size(geo.commandPanel.size() + Vec2{67, 0}));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##commands", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x0 = geo.left;
    auto hit = [&](const char* id, Vec2 min, Vec2 size) {
        ImGui::SetCursorScreenPos(ui.at(min));
        const bool clicked = ImGui::InvisibleButton(id, ui.size(size));
        return std::pair{clicked, ImGui::IsItemHovered()};
    };

    // Command buttons: 34-px cells from (13, 36).
    for (size_t i = 0; i < kCommands.size(); ++i) {
        const CommandButton& b = kCommands[i];
        const Vec2 at{x0 + 13 + float(i % 6) * 34, 36 + float(i / 6) * 34};
        ImGui::PushID(int(i));
        const auto [clicked, hovered] = hit("cmd", at, {34, 34});
        ImGui::PopID();
        ui.tagItem(b.screen ? "command:" + std::string(windowId(*b.screen)) : std::string("button:end-turn"));
        const int state = hovered ? (ImGui::IsMouseDown(ImGuiMouseButton_Left) ? 2 : 1) : 0;
        drawAt(ui, dl, ui.art.commandButton(b.icon, state), at, {34, 34});
        if (hovered) {
            hintName_ = b.name;
            hintKey_ = hintKey(b.key);
        }
        if (clicked) {
            // A left click on any command button plays cmdbtn; End Turn adds endturn when the turn ends (§5.5).
            audio().play("cmdbtn");
            if (b.screen) ui.open(*b.screen);
            else ui.requests.endTurn = true;
        }
    }

    // The order strip: every order at its own place, lit only when the selection
    // can carry it out (§2.8). At 1024×768 all 40 places fit on one page.
    const LitOrders lit = litNow(ui);
    const game::GameState& s = ui.state();
    const game::Vehicle* v = selectedVehicle(ui);
    const game::Colony* c = selectedColony(ui);
    const game::Fleet* f = fleet_ ? s.fleet(*fleet_) : nullptr;
    for (size_t col = 0; col < kOrderStrip.size(); ++col)
        for (size_t row = 0; row < 2; ++row) {
            const OrderSlot& slot = kOrderStrip[col][row];
            const Vec2 at{x0 + 246 + float(col) * 34, 36 + float(row) * 34};
            const bool enabled = slot.order && lit[static_cast<size_t>(*slot.order)];
            ImGui::PushID(int(col * 2 + row) + 100);
            const auto [clicked, hovered] = hit("order", at, {34, 34});
            ImGui::PopID();
            if (slot.order)
                if (const std::string_view tagId = learn::orderStripId(orderSlotKey(*slot.order)); !tagId.empty())
                    ui.tagItem("order:" + std::string(tagId));
            // Repeat Orders and Minister Control show their pressed state when on.
            bool on = false;
            if (enabled && slot.order == OrderId::RepeatOrders) on = f ? game::fleetRepeats(s, *f) : v && v->repeatOrders;
            if (enabled && slot.order == OrderId::Minister) on = tagged_.empty() && (v ? v->minister : c && c->minister);
            const int state = !enabled ? 3 : on ? 2 : hovered ? 1 : 0;
            drawAt(ui, dl, orderCell(ui.art, slot.band, slot.column, state), at, {34, 34});
            if (hovered && slot.order) {
                hintName_ = orderName(*slot.order);
                if (const auto a = orderAction(*slot.order)) hintKey_ = hintKey(*a);
            }
            if (clicked && enabled) {
                audio().play("ordbtn");
                runOrder(ui, *slot.order);
            }
        }
    ui.tagFrame("panel:commands", Rect{{x0 + 13, 36}, {x0 + 13 + 6 * 34, 36 + 2 * 34}});
    ui.tagFrame("panel:orders", Rect{{x0 + 246, 36}, {x0 + 246 + float(kOrderStrip.size()) * 34, 36 + 2 * 34}});
    // Page arrows at both ends; one page holds every order at this size, so they stay dim.
    for (const bool right : {false, true}) {
        const float ax = x0 + (right ? 927.0f : 231.0f);
        dl->AddRect(ui.at({ax - 2, 37}), ui.at({ax + 15, 104}), imColor(palette::kDisabled));
        drawAt(ui, dl, ui.art.region("Pictures/Game/Buttons/BigLeftRightArrows.bmp", right ? 14 : 0, 150, 14, 50, false), {ax, 44}, {14, 50});
    }

    // Selection cycles (ship, fleet, colony): the Nextprev.bmp selectors at the top right.
    const float sx = geo.right - 59;
    for (int row = 0; row < 3; ++row) {
        const Vec2 at{sx, 34 + float(row) * 24};
        ImGui::PushID(200 + row);
        const auto [prev, prevHover] = hit("prev", at, {16, 24});
        ImGui::PopID();
        ImGui::PushID(300 + row);
        const auto [next, nextHover] = hit("next", at + Vec2{32, 0}, {16, 24});
        ImGui::PopID();
        const int state = prevHover || nextHover ? 1 : 0;
        drawAt(ui, dl, ui.art.region("Pictures/Game/Buttons/Nextprev.bmp", row * 48, state * 24, 48, 24, false), at, {48, 24});
        static constexpr std::array<const char*, 3> kCycleTags{"cycle:ship", "cycle:fleet", "cycle:colony"};
        ui.tagFrame(kCycleTags[size_t(row)], Rect{at, at + Vec2{48, 24}});
        if (prev || next) {
            const int dir = next ? 1 : -1;
            if (row == 0) cycleVehicle(ui, dir, false);
            else if (row == 1) cycleFleet(ui, dir);
            else cycleColony(ui, dir);
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

namespace {

// Status icons in 20 px steps leftwards from `right`, six per row, then a second row (§2.5).
void iconStrip(UiContext& ui, ImDrawList* dl, const std::vector<int>& cells, ImVec2 rightTop) {
    for (size_t i = 0; i < cells.size(); ++i) {
        const Sprite s = ui.art.statusIcon(cells[i] + 1);
        if (!s) continue;
        const float x = rightTop.x - ui.px(20) * float(i % 6 + 1), y = rightTop.y + ui.px(20) * float(i / 6);
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), {x, y}, {x + ui.px(20), y + ui.px(20)}, {s.uv.min.x, s.uv.min.y},
                     {s.uv.max.x, s.uv.max.y});
    }
}

} // namespace

void MainWindow::reportPanel(UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::EmpireId me = ui.session.player();
    ImGui::SetNextWindowPos(ui.at(geo.reportPanel.min));
    ImGui::SetNextWindowSize(ui.size(geo.reportPanel.size()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##report", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float tabsY = geo.reportPanel.size().y - 31;
    bool tabsFor = false, planetTabs = false;
    const bool single = object_ || vehicle_;
    const bool several = single && sector_ && (objectsAt(ui, *sector_).size() + vehiclesAt(ui, {shown_, *sector_}).size()) > 1;
    // The report body scrolls above the tabs; the portrait, flag and name reach over the frame rail above it.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.size({4, 2}));
    ImGui::BeginChild("##body", ui.size({geo.reportPanel.size().x, tabsY - 1}), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    ImGui::PushClipRect(ui.at(geo.reportPanel.min - Vec2{8, 12}), ui.at(geo.reportPanel.min + Vec2{geo.reportPanel.size().x, tabsY}), false);
    if (vehicle_ && tagged_.empty()) {
        if (const game::Vehicle* v = s.vehicle(*vehicle_)) {
            if (fleet_) {
                if (const game::Fleet* f = s.fleet(*fleet_)) {
                    fleetReport(ui, *f);
                    ImGui::Separator();
                }
            }
            vehicleReport(ui, *v, tab_);
            tabsFor = true;
        }
    } else if (object_ && tagged_.empty()) {
        const game::SpaceObject& o = s.galaxy.object(*object_);
        if (o.kind == game::ObjectKind::Planet || o.kind == game::ObjectKind::Asteroids) {
            planetReport(ui, *object_, tab_);
            tabsFor = planetTabs = true;
        } else {
            objectReport(ui, *object_);
        }
    } else if ((listMode_ || !tagged_.empty()) && sector_) {
        // Everything in the sector: planets first, then vehicles. Each row: the
        // picture, the name, a ship's class, and status icons for own objects;
        // Shift+click tags a vehicle (§2.5).
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowH = ui.px(36);
        for (game::ObjectId id : objectsAt(ui, *sector_)) {
            const game::SpaceObject& o = s.galaxy.object(id);
            ImGui::PushID(int(id.value));
            image(ui, objectSprite(ui, o), {36, 36});
            ImGui::SameLine();
            if (ImGui::Selectable(objectName(s, id, me).c_str(), false, 0, ImVec2(0, rowH))) {
                clearSelection();
                tagged_.clear();
                sector_ = o.sector;
                object_ = id;
                ++selections_;
            }
            iconStrip(ui, dl, planetStatusCells(r, s, me, id), {ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y});
            ImGui::PopID();
        }
        for (const game::Vehicle* v : vehiclesAt(ui, {shown_, *sector_})) {
            ImGui::PushID(int(v->id.value) + 1000000);
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            image(ui, vehicleMini(ui, *v), {36, 36});
            ImGui::SameLine();
            const std::string label = std::format("{}\n{}", v->name, vehicleSummary(ui, *v));
            if (ImGui::Selectable(label.c_str(), tagged(v->id), 0, ImVec2(0, rowH))) {
                if (ImGui::GetIO().KeyShift) toggleTag(ui, v->id);
                else selectVehicle(ui, v->id);
            }
            if (v->owner == me) iconStrip(ui, dl, vehicleStatusCells(r, s, *v), {ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y});
            if (tagged(v->id)) {
                // The tag: a green arrow on the row.
                const float y = rowMin.y + rowH * 0.5f;
                dl->AddTriangleFilled({rowMin.x, y - ui.px(5)}, {rowMin.x + ui.px(7), y}, {rowMin.x, y + ui.px(5)}, IM_COL32(0, 224, 0, 255));
            }
            ImGui::PopID();
        }
        if (!tagged_.empty()) ImGui::TextColored(kDimText, "%zu tagged: orders go to all of them.", tagged_.size());
    } else if (shown_.valid()) {
        systemReport(ui, shown_);
    }
    ImGui::PopClipRect();
    ImGui::EndChild();
    if (tabsFor) {
        ImGui::SetCursorPos(ImVec2(ui.px(-4), ui.px(tabsY)));
        const ReportTab current = planetTabs ? (tab_ == ReportTab::Components ? ReportTab::Facilities : tab_)
                                             : (tab_ == ReportTab::Facilities ? ReportTab::Components : tab_);
        tab_ = reportTabs(ui, current, planetTabs);
        ui.tagFrame("panel:report-tabs", Rect{{geo.reportPanel.min.x - 4, geo.reportPanel.min.y + tabsY}, {geo.reportPanel.min.x + 284, geo.reportPanel.min.y + tabsY + 30}});
    }
    if (several && tagged_.empty()) {
        // Back to the list of everything in the sector.
        ImGui::SetCursorPos(ImVec2(ui.px(250), ui.px(tabsY - 18)));
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        if (ImGui::SmallButton("List")) {
            object_.reset();
            vehicle_.reset();
            fleet_.reset();
            listMode_ = true;
            ++selections_;
        }
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void MainWindow::overlayText(UiContext& ui) {
    const game::GameState& s = ui.state();
    if (!shown_.valid()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float k = ui.k();
    ImFont* font = ui.fonts.medium;
    auto text = [&](Vec2 p, float size, ImU32 c, const std::string& str) { dl->AddText(font, size * k, ui.at(p), c, str.c_str()); };
    auto centred = [&](ImFont* f, float size, float cx, float y, ImU32 c, const std::string& str) {
        const ImVec2 ts = f->CalcTextSizeA(size * k, FLT_MAX, 0.0f, str.c_str());
        const ImVec2 at = ui.at({cx, y});
        dl->AddText(f, size * k, ImVec2{at.x - ts.x * 0.5f, at.y}, c, str.c_str());
    };
    const game::StarSystem& sys = s.galaxy.system(shown_);
    const Rect& panel = geo.systemPanel;
    dl->AddText(ui.fonts.bold, ui.fontPx(kTitleSize), ui.at(Vec2{geo.left + 13, 120}), IM_COL32_WHITE, sys.name.c_str());
    const bool explored = ui.me().hasExplored(shown_);
    if (!explored) {
        centred(ui.fonts.bold, kTitleSize, panel.min.x + panel.size().x * 0.5f, panel.min.y + panel.size().y / 3.0f, IM_COL32_WHITE, "Unexplored");
    } else {
        for (game::ObjectId id : ui.options().warpPointNames ? s.galaxy.warpPoints(sys.id) : std::vector<game::ObjectId>{}) {
            // A warp point is named after its destination once we have explored it
            // (docs/spec/01 §5.4, sight::warpPointName).
            const game::SpaceObject& wp = s.galaxy.object(id);
            if (!wp.destination.valid()) continue;
            const game::SystemId to = s.galaxy.object(wp.destination).system;
            if (!s.options.omnipresent && !ui.me().hasExplored(to)) continue;
            centred(font, 11, sectorCenter(wp.sector).x, sectorCenter(wp.sector).y + 18, IM_COL32(184, 200, 255, 255), s.galaxy.system(to).name);
        }
        // Empire Options, System Display (spec 06 §1.9): planet names under the
        // planets, and the facility letter markers of our colonies above them
        // (placement inferred).
        const game::InterfaceOptions& opts = ui.options();
        for (game::ObjectId id : sys.objects) {
            const game::SpaceObject& o = s.galaxy.object(id);
            if (o.kind != game::ObjectKind::Planet) continue;
            if (opts.planetNames) centred(font, 11, sectorCenter(o.sector).x, sectorCenter(o.sector).y + 18, IM_COL32(220, 220, 220, 255), o.name);
            const game::Colony* col = s.colony(id);
            if (opts.facilityMarkers == 0 || !col || col->owner != ui.session.player()) continue;
            const std::string markers = facilityMarkers(ui.rules(), *col, opts.facilityMarkers);
            if (!markers.empty()) centred(font, 10, sectorCenter(o.sector).x, sectorCenter(o.sector).y - 25, IM_COL32(255, 255, 0, 255), markers);
        }
        // "Coordinate location": the sector under the pointer (inferred: after the system name).
        if (opts.coordinateLocation && hover_) {
            const std::string where = std::format("({},{})", hover_->x, hover_->y);
            const float nameW = ui.fonts.bold->CalcTextSizeA(ui.fontPx(kTitleSize), FLT_MAX, 0.0f, sys.name.c_str()).x / k;
            text(Vec2{geo.left + 13 + nameW + 10, 122}, 12, IM_COL32(200, 210, 230, 255), where);
        }
    }

    // Ship counts in the owner's colour: at the bottom right of a lone stack's
    // sprite, or beside each flag (§2.4). Mirrors the layout of drawSystem.
    std::map<game::Sector, std::vector<const game::Vehicle*>> ships;
    for (const game::Vehicle& v : s.vehicles)
        if (v.location.system == shown_ && knownVehicle(ui, v) && !glides_.find(v.id, ui.time) &&
            !replay_.position(v.id, v.location, shown_, ui.time))
            ships[v.location.sector].push_back(&v);
    for (const auto& [sector, list] : ships) {
        std::vector<std::pair<game::EmpireId, int>> owners;
        for (const game::Vehicle* v : list) {
            auto it = std::find_if(owners.begin(), owners.end(), [&](const auto& o) { return o.first == v->owner; });
            const int n = std::max(1, v->count);
            if (it == owners.end()) owners.emplace_back(v->owner, n);
            else it->second += n;
        }
        const Vec2 c = sectorCenter(sector);
        const bool flags = owners.size() > 1 || (explored && !objectsAt(ui, sector).empty());
        if (flags) {
            float dx = 0;
            for (const auto& [owner, n] : owners) {
                text(c + Vec2{-18 + dx + 15, 6}, 10, empireColor(s, owner), std::to_string(n));
                dx += 15 + 6 + 6 * float(std::to_string(n).size());
            }
        } else if (owners.front().second > 1) {
            const std::string n = std::to_string(owners.front().second);
            const ImVec2 ts = font->CalcTextSizeA(10 * k, FLT_MAX, 0.0f, n.c_str());
            const ImVec2 at = ui.at(c + Vec2{18, 18});
            dl->AddText(font, 10 * k, ImVec2{at.x - ts.x, at.y - ts.y}, empireColor(s, owners.front().first), n.c_str());
        }
    }

    // Waypoints 1–10 (the number in cyan) and the tagged minefields ("M"), §2.4.
    const ImU32 cyan = imColor(map_style::kWaypoint);
    const auto& wps = ui.me().waypoints;
    for (size_t i = 0; i < wps.size(); ++i)
        if (wps[i].set && wps[i].location.system == shown_) text(cellOrigin(wps[i].location.sector) + Vec2{3, 2}, 10, cyan, std::to_string(i));
    for (const game::Location& m : ui.me().taggedMinefields)
        if (m.system == shown_) text(cellOrigin(m.sector) + Vec2{40, 2}, 10, cyan, "M");

    // The hover hint (§2.3): the button's name and its key, centred at the top of the system panel.
    if (!hintName_.empty()) {
        const float cx = panel.min.x + panel.size().x * 0.5f;
        centred(ui.fonts.bold, kTitleSize, cx, panel.min.y + 10, IM_COL32_WHITE, hintName_);  // y 123..153 at 1024x768
        if (!hintKey_.empty()) centred(ui.fonts.small, kSmallSize, cx, panel.min.y + 28, IM_COL32_WHITE, hintKey_);  // 18 px below the name
    }

    if (pick_ != Pick::None) text(panel.min + Vec2{6, 632}, 14, IM_COL32(255, 220, 90, 255), pickPrompt_ + "   (Esc to cancel)");
    else if (!note_.empty() && ui.time < noteUntil_) text(panel.min + Vec2{6, 632}, 14, IM_COL32(255, 220, 90, 255), note_);
    if (replay_.active()) text(panel.min + Vec2{6, 612}, 12, cyan, std::format("Movement log: day {} of {}", int(replay_.progress(ui.time) * MovementReplay::kDays + 1e-6), MovementReplay::kDays));

    // The hovered system on the galaxy panel: its name in cyan at the first corner that fits (§2.6).
    if (galaxyHover_) {
        const GalaxyGrid g = galaxyGrid();
        const game::StarSystem& h = s.galaxy.system(*galaxyHover_);
        const Vec2 cell = galaxyCell(g, h);
        const float fs = 11;
        const ImVec2 ts = font->CalcTextSizeA(fs * k, FLT_MAX, 0.0f, h.name.c_str());
        const map_style::Point p = map_style::nameCorner(cell.x - geo.galaxyPanel.min.x, cell.y - geo.galaxyPanel.min.y, g.cw, g.ch,
                                                         ts.x / k + 2, ts.y / k, geo.galaxyPanel.size().x, geo.galaxyPanel.size().y);
        text(geo.galaxyPanel.min + Vec2{p.x + 1, p.y}, fs, imColor(map_style::kHover), h.name);
    }
}

std::optional<game::SystemId> MainWindow::galaxySystemAt(const UiContext& ui, Vec2 p, bool exploredOnly) const {
    const GalaxyGrid g = galaxyGrid();
    std::optional<game::SystemId> best;
    float bestDist = 3.0f * std::max(g.cw, g.ch);
    for (const game::StarSystem& sys : ui.state().galaxy.systems) {
        if (exploredOnly && !ui.me().hasExplored(sys.id)) continue;
        if (const float d = distance(galaxyCenter(g, sys), p); d < bestDist) {
            bestDist = d;
            best = sys.id;
        }
    }
    return best;
}

void MainWindow::mouse(UiContext& ui) {
    const ImGuiIO& io = ImGui::GetIO();
    galaxyHover_.reset();
    if (io.WantCaptureMouse || !ImGui::IsMousePosValid()) return;
    const Vec2 p = ui.map.fromFb(Vec2{io.MousePos.x, io.MousePos.y} * ui.fbScale);
    hover_ = sectorAt(p);
    // Hovering the galaxy panel names the nearest explored system.
    if (geo.galaxyPanel.contains(p)) galaxyHover_ = galaxySystemAt(ui, p, true);
    const bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (!left && !right) return;

    if (geo.galaxyPanel.contains(p)) {
        if (right) {
            ScreenArgs args;
            args.location = game::Location{shown_, {}};
            ui.open(ScreenId::GalaxyMap, args);
            return;
        }
        if (const auto best = galaxySystemAt(ui, p, false)) {
            shown_ = *best;
            if (pick_ == Pick::None) {
                clearSelection();
                tagged_.clear();
                ++selections_;   // the system, with nothing in it selected
            }
        }
        return;
    }

    if (auto sec = sectorAt(p)) {
        const game::Location where{shown_, *sec};
        if (pick_ != Pick::None) {
            if (right && pick_ == Pick::Patrol) {
                finishPatrol(ui);
                return;
            }
            completePick(ui, where, std::nullopt);
            return;
        }
        // Right click on a sector with an own mobile vehicle selected: Move To (an OpenSE4 shortcut, optional).
        if (right && appSettings().controls.rightClickMoves) {
            if (const game::Vehicle* v = selectedVehicle(ui); v && v->owner == ui.session.player() && v->location != where &&
                                                              litNow(ui)[static_cast<size_t>(OrderId::MoveTo)]) {
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
    const Bindings& keys = appSettings().controls.bindings;
    auto pressed = [&](Action a) { return keys.pressed(a); };

    // Windows (F1..F11 by default).
    for (const CommandButton& b : kCommands)
        if (b.screen && pressed(b.key)) ui.open(*b.screen);
    if (pressed(Action::Settings)) ui.open(ScreenId::Settings);

    if (pressed(Action::Cancel)) {
        if (pick_ != Pick::None) pick_ = Pick::None;
        else {
            clearSelection();
            tagged_.clear();
        }
    }
    if (pressed(Action::EndTurn)) {
        if (pick_ == Pick::Patrol) finishPatrol(ui);
        else if (pick_ == Pick::None) ui.requests.endTurn = true;
    }

    // Selection cycling and tags.
    if (pressed(Action::NextIdleShip)) cycleVehicle(ui, 1, true);
    if (pressed(Action::NextShip)) cycleVehicle(ui, 1, false);
    if (pressed(Action::PreviousShip)) cycleVehicle(ui, -1, false);
    if (pressed(Action::NextFleet)) cycleFleet(ui, 1);
    if (pressed(Action::PreviousFleet)) cycleFleet(ui, -1);
    if (pressed(Action::NextColony)) cycleColony(ui, 1);
    if (pressed(Action::PreviousColony)) cycleColony(ui, -1);
    if (pressed(Action::TagAll)) tagAll(ui);
    if (pressed(Action::ClearTags)) tagged_.clear();
    // Ctrl+L flips the Options window's "Display Ship Movement Lines" (spec 06 §1.9).
    if (pressed(Action::MovementLines)) {
        settings().showMovementLines = !settings().showMovementLines;
        saveSettings();
    }
    if (pressed(Action::ToggleSound)) {
        // The Sound On switch; music is not touched (§3.2).
        settings().soundOn = !settings().soundOn;
        saveSettings();
        note(ui, settings().soundOn ? "Sound effects on" : "Sound effects off");
    }
    // Tagged minefields (Ctrl+T / Ctrl+R) at the selected sector.
    if (sector_ && (pressed(Action::TagMinefield) || pressed(Action::UntagMinefield)))
        ui.session.issue(game::cmd::TagMinefield{{shown_, *sector_}, pressed(Action::TagMinefield)});

    // Waypoints: Alt+0..9 sets one at the selected sector; Ctrl+0..9 moves there
    // when Move To Waypoint is lit.
    const bool ctrl = io.KeyCtrl, alt = io.KeyAlt;
    for (int i = 0; i < 10; ++i) {
        const ImGuiKey key = static_cast<ImGuiKey>(ImGuiKey_0 + i);
        if (!ImGui::IsKeyPressed(key, false)) continue;
        if (alt && sector_) {
            game::Waypoint w;
            w.name = std::format("Waypoint {}", i);
            w.location = {shown_, *sector_};
            w.set = true;
            ui.session.issue(game::cmd::SetWaypoint{i, w});
        } else if (ctrl && litNow(ui)[static_cast<size_t>(OrderId::MoveToWaypoint)]) {
            game::Order o{game::OrderKind::MoveToWaypoint};
            o.amount = i;
            giveOrder(ui, o);
        }
    }

    // Orders: a key works only while its button is lit (§2.8).
    std::optional<LitOrders> lit;
    for (size_t i = 0; i < kOrderCount; ++i) {
        const OrderId o = static_cast<OrderId>(i);
        const auto a = orderAction(o);
        if (!a || !pressed(*a)) continue;
        if (!lit) lit = litNow(ui);
        if ((*lit)[i]) runOrder(ui, o);
    }
}

// ---- World rendering -----------------------------------------------------------------------

void MainWindow::render(gfx::Renderer2D& r, UiContext& ui) {
    layOut(ui.map.left, ui.map.right);
    trackMovement(ui);
    r.rect(Rect{{geo.left, 0}, {geo.right, kFrameH}}, Color::hex(0x000000));
    drawSystem(r, ui);
    drawGalaxy(r, ui);
    drawFrame(r, ui);
}

void MainWindow::drawFrame(gfx::Renderer2D& r, UiContext& ui) {
    // The original's frame strips, at their measured places; black is see-through.
    auto strip = [&](const char* name) { return ui.art.image(std::string(kScreens) + name); };
    auto put = [&](const Sprite& s, Vec2 at) {
        if (s) r.sprite(s.tex, Rect::fromPosSize(at, s.size), s.uv);
    };
    // A horizontal strip on a wider frame: its two halves at the ends, the middle column stretched between.
    auto wideStrip = [&](const Sprite& s, float y, float x0, float x1, float split) {
        if (!s) return;
        const float w = s.size.x;
        if (x1 - x0 <= w + 0.5f) {
            put(s, {x0, y});
            return;
        }
        auto part = [&](float u0, float u1, float dx0, float dx1) {
            const float su = s.uv.max.x - s.uv.min.x;
            const Rect uv{{s.uv.min.x + su * u0 / w, s.uv.min.y}, {s.uv.min.x + su * u1 / w, s.uv.max.y}};
            r.sprite(s.tex, Rect{{dx0, y}, {dx1, y + s.size.y}}, uv);
        };
        part(0, split, x0, x0 + split);
        part(split, split + 1, x0 + split, x1 - (w - split - 1));
        part(split + 1, w, x1 - (w - split - 1), x1);
    };
    const float L = geo.left, R = geo.right;
    wideStrip(strip("Top.bmp"), 0, L, R, 512);
    wideStrip(strip("Toptitle.bmp"), 29, L, R, 512);
    wideStrip(strip("Topsys.bmp"), 106, L, R, 700);
    wideStrip(strip("Bottom.bmp"), 760, L, R, 512);
    put(strip("Left.bmp"), {L, 0});
    const Sprite right = strip("Right.bmp");
    put(strip("Middle.bmp"), {geo.divider, 106});
    if (geo.wideDivider) put(strip("Middle.bmp"), {*geo.wideDivider, 106});
    if (right && geo.wide) {
        // Only the edge pipe below the command strip: the rest of the strip frames panels that moved.
        auto part = [&](float x0, float y0, float x1, float y1) {
            const Vec2 su = right.uv.max - right.uv.min;
            const Rect uv{right.uv.min + Vec2{su.x * x0 / 67, su.y * y0 / 768}, right.uv.min + Vec2{su.x * x1 / 67, su.y * y1 / 768}};
            r.sprite(right.tex, Rect{{R - 67 + x0, y0}, {R - 67 + x1, y1}}, uv);
        };
        part(0, 0, 67, 107);
        part(52, 107, 67, 768);
    } else {
        put(right, {R - 67, 0});
    }
    if (geo.strip) put(strip("RightFiller.bmp"), geo.strip->min);
    if (const Sprite filler = strip("RightFiller.bmp"); filler && geo.gap) {
        // Tiled from the right, clipped at the left edge of the gap.
        const Vec2 su = filler.uv.max - filler.uv.min;
        for (float x1 = geo.gap->max.x; x1 > geo.gap->min.x; x1 -= filler.size.x) {
            const float x0 = std::max(geo.gap->min.x, x1 - filler.size.x);
            const float h = std::min(filler.size.y, geo.gap->size().y);
            const float u0 = (filler.size.x - (x1 - x0)) / filler.size.x;
            r.sprite(filler.tex, Rect{{x0, geo.gap->min.y}, {x1, geo.gap->min.y + h}},
                     Rect{{filler.uv.min.x + su.x * u0, filler.uv.min.y}, {filler.uv.max.x, filler.uv.min.y + su.y * h / filler.size.y}});
        }
        r.line({geo.gap->max.x + 1, geo.gap->min.y}, {geo.gap->max.x + 1, geo.gap->max.y}, 1.0f, Color::hex(palette::kFrame));
    }
    if (!geo.wide) {
        // The rail over the galaxy map, stretched across it on a wider frame.
        const Sprite top = strip("Topgal.bmp");
        wideStrip(top, 470, geo.galaxyPanel.min.x - 9, R, top ? top.size.x * 0.5f : 0);
    }
}

// ---- Ship movement animation ---------------------------------------------------------------

void MainWindow::trackMovement(UiContext& ui) {
    // Once per frame: update() and render() both call this.
    if (ui.time == trackedAt_) return;
    trackedAt_ = ui.time;
    // A new turn: where everything was before it, for the movement log (simultaneous games).
    if (ui.state().turn != seenTurn_) {
        if (seenTurn_ != UINT32_MAX && !ui.session.turnBased()) replay_.newTurn(glides_.lastSeen());
        seenTurn_ = ui.state().turn;
    }
    replay_.update(ui.time);
    std::vector<ShipGlides::Seen> visible;
    for (const game::Vehicle& v : ui.state().vehicles)
        if (knownVehicle(ui, v)) visible.push_back({v.id, v.location});
    glides_.track(ui.time, shown_, settings().animateSystemMovement && !replay_.active(), visible);
}

void MainWindow::drawSystem(gfx::Renderer2D& r, UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::Rules& rules = ui.rules();
    if (!shown_.valid()) return;
    const game::StarSystem& sys = s.galaxy.system(shown_);
    const ruleset::SystemType& type = rules.data().systemTypes[sys.type.index()];
    const bool explored = ui.me().hasExplored(shown_);
    const Rect& panel = geo.systemPanel;
    // The background, copied 1:1 from its top-left corner: its right and bottom
    // edges are cut off (§2.4). An unexplored system shows the plain star field.
    r.rect(panel, Color::hex(0x000000));
    if (const Sprite bg = ui.art.systemBackground(explored ? std::string_view(type.backgroundBitmap) : std::string_view("Starmap"))) {
        const Vec2 size{std::min(panel.size().x, bg.size.x), std::min(panel.size().y, bg.size.y)};
        const Vec2 su = bg.uv.max - bg.uv.min;
        r.sprite(bg.tex, Rect::fromPosSize(panel.min, size), Rect{bg.uv.min, bg.uv.min + Vec2{su.x * size.x / bg.size.x, su.y * size.y / bg.size.y}});
    }
    // Grid lines with the empire option (§1.9): 14 each way on the cell boundaries.
    if (ui.options().systemGrid)
        for (int i = 0; i <= 13; ++i) {
            const float x = geo.sectorOrigin.x + kSectorSize * float(i), y = geo.sectorOrigin.y + kSectorSize * float(i);
            const Color c = rgb(map_style::kGrid);
            r.line({x, geo.sectorOrigin.y}, {x, geo.sectorOrigin.y + 13 * kSectorSize}, 1.0f, c);
            r.line({geo.sectorOrigin.x, y}, {geo.sectorOrigin.x + 13 * kSectorSize, y}, 1.0f, c);
        }
    // Sprites are copied opaque unless the system type masks black (Mask Background Objs).
    const bool keyed = type.maskBackgroundObjects;

    // Every sprite is drawn 36×36, centred in its cell, never scaled.
    auto spriteAt = [&](const Sprite& sp, Vec2 c) {
        if (sp) r.sprite(sp.tex, Rect::fromCenter(c, {kSpriteSize * 0.5f, kSpriteSize * 0.5f}), sp.uv);
        return static_cast<bool>(sp);
    };
    auto miniOf = [&](const game::Vehicle& v) {
        const std::string& style = v.owner.valid() ? s.empire(v.owner).race.style : std::string{};
        return ui.art.shipMini(style, rules.hull(s.design(v.design).hull), keyed, turnsToHeading(rules, s, v) ? glides_.heading(v.id) : 0);
    };

    if (explored) {
        const ColonizeTech tech = colonizeTech(rules, ui.me());
        std::map<game::Sector, std::vector<game::ObjectId>> bySector;
        for (game::ObjectId id : sys.objects) bySector[s.galaxy.object(id).sector].push_back(id);
        for (const auto& [sector, ids] : bySector) {
            game::ObjectId shown = ids.front();
            for (game::ObjectId id : ids)
                if (s.galaxy.object(id).kind == game::ObjectKind::Star) shown = id;
            const game::SpaceObject& o = s.galaxy.object(shown);
            const Vec2 c = sectorCenter(sector);
            if (!spriteAt(ui.art.planet(rules.data().sectorObjectTypes[o.sectorType].picture, keyed), c))
                r.disc(c, 12.0f, o.kind == game::ObjectKind::Star ? Color::hex(0xffe080) : Color::hex(0x8090a0));
            // Colonies carry the owner's small flag at the top right; planets we could
            // colonise get the classic star (green: breathable, red: needs domes).
            for (game::ObjectId id : ids) {
                // The planet is always drawn; a cloaked colony's mark only when it is seen (spec 01 §6.9).
                if (const game::Colony* col = s.colony(id); col && game::sight::colonyShown(rules, s, ui.session.player(), id)) {
                    const Sprite flag = ui.art.flag(s.empire(col->owner).race.style, false);
                    if (flag) r.sprite(flag.tex, Rect::fromPosSize(c + Vec2{4, -17}, {14, 10}), flag.uv);
                    else r.rect(Rect::fromPosSize(c + Vec2{4, -17}, {14, 10}), empireCol(s, col->owner));
                } else if (ui.options().colonizableMarkers && s.galaxy.object(id).kind == game::ObjectKind::Planet &&
                           colonizeProblem(rules, s, ui.session.player(), id, tech).empty()) {
                    const bool breathe = breathableBy(s, ui.session.player(), s.galaxy.object(id));
                    const Sprite star = ui.art.region("Pictures/Game/General.bmp", breathe ? 237 : 261, 16, 7, 7);
                    if (star) r.sprite(star.tex, Rect::fromPosSize(c + Vec2{11, -17}, {7, 7}), star.uv);
                }
            }
        }
    }

    // Vehicles (§2.4): a single-owner stack shows one ship sprite; a sector with
    // several empires, or ships beside a planet or other object, shows small
    // flags instead (their counts are drawn by overlayText). A ship gliding to
    // its square, or moved by the movement log, is drawn on the way instead.
    auto largestFirst = [&](std::vector<const game::Vehicle*>& list) {
        std::stable_sort(list.begin(), list.end(), [&](const game::Vehicle* a, const game::Vehicle* b) {
            return rules.hull(s.design(a->design).hull).tonnage > rules.hull(s.design(b->design).hull).tonnage;
        });
    };
    const double now = ui.time;
    std::map<game::Sector, std::vector<const game::Vehicle*>> ships;
    std::map<std::tuple<float, float, float, float, double>, std::vector<const game::Vehicle*>> gliding;
    std::map<std::pair<float, float>, std::vector<const game::Vehicle*>> replaying;
    for (const game::Vehicle& v : s.vehicles) {
        if (v.location.system != shown_ || !knownVehicle(ui, v)) continue;
        if (const auto at = replay_.position(v.id, v.location, shown_, now)) replaying[{at->x, at->y}].push_back(&v);
        else if (const ShipGlides::Glide* g = glides_.find(v.id, now))
            gliding[{g->from.x, g->from.y, g->to.x, g->to.y, g->start}].push_back(&v);  // a fleet glides as one
        else
            ships[v.location.sector].push_back(&v);
    }
    auto moving = [&](std::vector<const game::Vehicle*>& list, Vec2 at) {
        largestFirst(list);
        if (!spriteAt(miniOf(*list.front()), at))
            r.triangle(at + Vec2{0, -9}, at + Vec2{-7, 7}, at + Vec2{7, 7}, empireCol(s, list.front()->owner));
    };
    for (auto& [key, list] : gliding) moving(list, gridPoint(ShipGlides::position(*glides_.find(list.front()->id, now), now)));
    for (auto& [key, list] : replaying) moving(list, gridPoint(Vec2{key.first, key.second}));
    for (auto& [sector, list] : ships) {
        const Vec2 c = sectorCenter(sector);
        largestFirst(list);
        std::vector<game::EmpireId> owners;
        for (const game::Vehicle* v : list)
            if (std::find(owners.begin(), owners.end(), v->owner) == owners.end()) owners.push_back(v->owner);
        const bool flags = owners.size() > 1 || (explored && !objectsAt(ui, sector).empty());
        if (flags) {
            float dx = 0;
            for (game::EmpireId e : owners) {
                int n = 0;
                for (const game::Vehicle* v : list) n += v->owner == e ? std::max(1, v->count) : 0;
                const Rect box = Rect::fromPosSize(c + Vec2{-18 + dx, 6}, {14, 10});
                if (const Sprite flag = ui.art.flag(s.empire(e).race.style, false)) r.sprite(flag.tex, box, flag.uv);
                else r.rect(box, empireCol(s, e));
                dx += 15 + 6 + 6 * float(std::to_string(n).size());
            }
        } else if (!spriteAt(miniOf(*list.front()), c)) {
            r.triangle(c + Vec2{0, -9}, c + Vec2{-7, 7}, c + Vec2{7, 7}, empireCol(s, list.front()->owner));
        }
        // A cloaked ship: a ring around its cell in its empire's colour.
        for (const game::Vehicle* v : list)
            if (v->status == game::VehicleStatus::Cloaked) {
                r.ring(c, kSpriteSize * 0.5f + 2.0f, 1.0f, v->owner.valid() ? empireCol(s, v->owner) : rgb(map_style::kCloakNoColor));
                break;
            }
    }

    // Movement line for the selected own vehicle.
    if (const game::Vehicle* v = selectedVehicle(ui); v && settings().showMovementLines && v->owner == ui.session.player()) {
        const game::Fleet* f = s.fleet(v->fleet);
        const auto& orders = f ? game::fleetOrders(s, *f) : v->orders;
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

    // The selected location: four small yellow corner marks; the pick hover in green.
    auto brackets = [&](game::Sector sec, Color col) {
        const Vec2 c = sectorCenter(sec);
        const float h = kSpriteSize * 0.5f, l = 4.0f;
        for (float sx : {-1.0f, 1.0f})
            for (float sy : {-1.0f, 1.0f}) {
                const Vec2 corner = c + Vec2{sx * h, sy * h};
                r.line(corner, corner - Vec2{sx * l, 0}, 1.0f, col);
                r.line(corner, corner - Vec2{0, sy * l}, 1.0f, col);
            }
    };
    if (sector_) brackets(*sector_, kSelectYellow);
    if (pick_ != Pick::None && hover_) brackets(*hover_, Color::hex(0x60ff80));
    // Waypoints, always marked: a cyan frame around the sector (the number is drawn by overlayText).
    for (const auto& w : ui.me().waypoints)
        if (w.set && w.location.system == shown_)
            r.rectOutline(Rect::fromPosSize(cellOrigin(w.location.sector) + Vec2{1, 1}, {kSectorSize - 2, kSectorSize - 2}), 1.0f,
                          rgb(map_style::kWaypoint));
}

void MainWindow::drawGalaxy(gfx::Renderer2D& r, UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::Galaxy& g = s.galaxy;
    const game::Empire& me = ui.me();
    const GalaxyGrid grid = galaxyGrid();
    r.rect(geo.galaxyPanel, Color::hex(0x000000));
    // The 68 × 47 grid, with the empire option Show Grid Lines (§2.6).
    if (ui.options().galaxyGridLines) {
        const Color c = rgb(map_style::kGrid);
        const float w = grid.cw * float(map_style::kGridColumns), h = grid.ch * float(map_style::kGridRows);
        for (int x = 0; x <= map_style::kGridColumns; ++x)
            r.line(grid.origin + Vec2{grid.cw * float(x), 0}, grid.origin + Vec2{grid.cw * float(x), h}, 1.0f, c);
        for (int y = 0; y <= map_style::kGridRows; ++y)
            r.line(grid.origin + Vec2{0, grid.ch * float(y)}, grid.origin + Vec2{w, grid.ch * float(y)}, 1.0f, c);
    }
    // Warp lines from explored systems; a link to an unexplored one is a stub two cells long.
    if (ui.options().galaxyWarpLines)
        for (const game::SpaceObject& o : g.objects) {
            if (o.kind != game::ObjectKind::WarpPoint || !o.destination.valid() || !me.hasExplored(o.system)) continue;
            const game::SystemId to = g.object(o.destination).system;
            const Vec2 a = galaxyCenter(grid, g.system(o.system)), b = galaxyCenter(grid, g.system(to));
            if (me.hasExplored(to)) {
                r.line(a, b, 1.0f, rgb(map_style::kWarpLine));
            } else if (const float len = distance(a, b); len > 0.0f) {
                r.line(a, a + (b - a) * (std::min(len, 2.0f * grid.cw) / len), 1.0f, rgb(map_style::kWarpLine));
            }
        }

    // One symbol per system, filling its cell, black inside.
    const auto presence = systemPresence(ui);
    const float radius = std::max(1.5f, std::min(grid.cw, grid.ch) * 0.5f);
    for (const game::StarSystem& sys : g.systems) {
        const Vec2 cell = galaxyCell(grid, sys);
        const Vec2 c = galaxyCenter(grid, sys);
        const map_style::Symbol sym = map_style::presenceSymbol(me.hasExplored(sys.id), presence[sys.id.index()], me.id);
        const Color col = sym.empire ? empireCol(s, *sym.empire) : rgb(sym.rgb);
        const bool current = sys.id == shown_;
        if (sym.shape == map_style::Shape::Triangle) {
            r.triangle(cell + Vec2{0, grid.ch}, cell + Vec2{grid.cw, grid.ch}, cell + Vec2{grid.cw * 0.5f, 0}, col);
        } else {
            // The system shown in the system panel: the symbol filled with its colour.
            r.disc(c, radius, current ? col : Color::hex(0x000000));
            r.ring(c, radius, 1.0f, col);
        }
        if (current) r.ring(c, radius + 2.0f, 1.0f, col);
        if (galaxyHover_ && *galaxyHover_ == sys.id) r.ring(c, radius + 2.0f, 1.0f, rgb(map_style::kHover));
    }
}

} // namespace opense4::client::classic
