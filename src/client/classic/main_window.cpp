#include "client/classic/main_window.hpp"

#include "client/app_settings.hpp"
#include "client/audio.hpp"
#include "client/classic/facility_markers.hpp"
#include "client/classic/map_style.hpp"
#include "client/classic/pointers.hpp"
#include "client/classic/quadrant_map.hpp"
#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/status_icons.hpp"
#include "client/script/items.hpp"

#include "game/abilities.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"
#include "learn/ids.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <tuple>

namespace opense4::client::classic {

namespace {

// Classic main-window geometry (docs/spec/06 §2.1, §2.1.1, §2.4, confirmed:
// binary) for the layout in use (layout.hpp): the frame is drawn from the
// layout's Screens strips, the panels sit between them.
constexpr float kSpriteSize = 36.0f;   // sprites are never scaled

// Panel rectangles for the frame's current extent. The classic frame (left 0,
// right 1024 or 800) gives the original layout; a wider 1024x768 frame keeps
// the system view and moves the right-hand panels to the right edge, and with
// enough room puts a full-height galaxy map beside the report (OpenSE4's).
struct Geometry {
    const LayoutGeometry* layout = &geometryFor(ScreenLayout::Large);
    float left = 0, right = kFrameW;
    Rect commandArea{{0, 29}, {1024, 106}};  // command buttons, order strip and selectors
    Rect systemPanel{{8, 113}, {660, 765}};
    Vec2 sectorOrigin{9, 114};
    float cell = 50.0f;
    Rect reportPanel{{671, 117}, {957, 470}};
    Rect galaxyPanel{{671, 479}, {1013, 763}};
    Vec2 selectors{965, 34};
    std::optional<Rect> strip = Rect{{958, 107}, {1020, 472}};  // the circuit-board filler
    std::optional<Rect> gap;   // more filler where a wider frame leaves room beside the report
    float divider = 655;       // x of the Middle strip between the system view and the right panels
    std::optional<float> wideDivider;  // second Middle strip (wide layout)
    bool wide = false;  // side-by-side report and galaxy map
    bool extended = false;  // a wider frame than the layout's
};
Geometry geo;

void layOut(float left, float right) {
    Geometry g;
    const LayoutGeometry& l = layoutGeometry();
    g.layout = &l;
    g.left = left;
    g.right = right;
    // The classic places (§2.1). Our report panel starts 4 px right of and 8 px
    // below the spec's region, under the frame rail its portrait reaches over.
    const bool small = l.layout == ScreenLayout::Small;
    g.commandArea = small ? Rect{{0, 29}, {l.commandPanel.max.x, 106}} : Rect{{0, 29}, {l.frame.x, 106}};
    g.systemPanel = l.systemPanel;
    g.cell = l.cell;
    g.sectorOrigin = l.systemPanel.min + Vec2{l.margin, l.margin};
    g.reportPanel = Rect{l.reportPanel.min + Vec2{4, 8}, l.reportPanel.max};
    g.galaxyPanel = l.galaxyPanel;
    g.selectors = l.selectors;
    g.strip.reset();
    for (int i = 0; i < l.pieceCount; ++i) {
        const FramePiece& p = l.pieces[size_t(i)];
        if (std::string_view(p.file) == "RightFiller.bmp") g.strip = Rect{p.at, {1020, 472}};
        if (std::string_view(p.file) == "Middle.bmp") g.divider = p.at.x;
    }
    const float extra = right - left - l.frame.x;
    if (!small && extra > 0.5f) {
        g.extended = true;
        g.commandArea = Rect{{left, 29}, {right, 106}};
        g.systemPanel = Rect{{left + 8, 113}, {left + 660, 765}};
        g.sectorOrigin = {left + 9, 114};
        g.divider = left + 655;
        g.selectors = {right - 59, 34};
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
    {3, ScreenId::Ships, "Ships\\Units", Action::Ships},
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

// A sector's cell and its centre: (16 + 36c, 121 + 36r) at 800x600, (9 + 50c, 114 + 50r) at 1024x768.
Vec2 cellOrigin(game::Sector s) { return geo.sectorOrigin + Vec2{geo.cell * float(s.x), geo.cell * float(s.y)}; }
Vec2 sectorCenter(game::Sector s) { return cellOrigin(s) + Vec2{geo.cell * 0.5f, geo.cell * 0.5f}; }
// The 36x36 sprite square of a sector, centred in its cell (offset 0 or 7).
Vec2 spriteSquare(game::Sector s) { return cellOrigin(s) + Vec2{(geo.cell - kSpriteSize) * 0.5f, (geo.cell - kSpriteSize) * 0.5f}; }
// A point on the system grid in sector units (ship_glides.hpp).
Vec2 gridPoint(Vec2 cell) { return geo.sectorOrigin + cell * geo.cell; }

// A click maps to (x − panel x − margin) div cell, the same for the row;
// anything outside 0..12 is ignored (the margin counts as row or column 0).
std::optional<game::Sector> sectorAt(Vec2 p) {
    if (!geo.systemPanel.contains(p)) return std::nullopt;
    const Vec2 rel = (p - geo.sectorOrigin) / geo.cell;
    const game::Sector s{static_cast<int>(rel.x), static_cast<int>(rel.y)};  // truncated toward zero
    return s.valid() ? std::optional(s) : std::nullopt;
}

// The galaxy panel's map: always 68 × 47 cells (§2.6), in the layout's map
// area: 4 px cells from the panel's (7,0) at 800x600, 5 px from (0,20) at 1024x768.
struct GalaxyGrid {
    Vec2 origin;
    float cw = 1, ch = 1;
};
GalaxyGrid galaxyGrid() {
    if (geo.wide) {
        // Our tall panel beside the report (wide frames): square cells, centred.
        const map_style::GridCell c = map_style::gridCell(int(geo.galaxyPanel.size().x), int(geo.galaxyPanel.size().y));
        const float cell = float(std::min(c.w, c.h));
        const Vec2 extent{cell * map_style::kGridColumns, cell * map_style::kGridRows};
        return {geo.galaxyPanel.min + (geo.galaxyPanel.size() - extent) * 0.5f, cell, cell};
    }
    const LayoutGeometry& l = *geo.layout;
    const float cell = l.galaxyCell;
    Vec2 origin = geo.galaxyPanel.min + l.galaxyMap.min;
    // A wider panel (an extended frame) centres the map across it.
    if (geo.galaxyPanel.size().x > l.galaxyPanel.size().x + 0.5f)
        origin.x = geo.galaxyPanel.min.x + std::floor((geo.galaxyPanel.size().x - cell * map_style::kGridColumns) * 0.5f);
    return {origin, cell, cell};
}
Vec2 galaxyCell(const GalaxyGrid& g, const game::StarSystem& s) {
    return g.origin + Vec2{float(s.position.x) * g.cw, float(s.position.y) * g.ch};
}
Vec2 galaxyCenter(const GalaxyGrid& g, const game::StarSystem& s) { return galaxyCell(g, s) + Vec2{g.cw * 0.5f, g.ch * 0.5f}; }

// The minis turned to their heading: hulls that use engines, fighter and drone groups (§2.4).
bool isReplayOrder(OrderId o) {
    return o == OrderId::ReplayPlay || o == OrderId::ReplayShip || o == OrderId::ReplayStep || o == OrderId::ReplayRewind;
}

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
    reportFromList_ = false;
}

std::vector<game::ObjectId> MainWindow::objectsAt(const UiContext& ui, game::Sector sec) const {
    // Only the stellar objects the player sees: a colony's cloak, a storm or
    // a nebula can hide a planet, which is then neither clickable nor listed
    // (spec 01 §6.9 "What other players see", spec 06 §2.4).
    return shownStellarObjects(ui.rules(), ui.state(), ui.session.player(), shown_, sec);
}

bool MainWindow::selectedSectorMarked(const UiContext& ui) const {
    if (!sector_ || !shown_.valid()) return false;
    if (!objectsAt(ui, *sector_).empty()) return true;
    if (replay_.active()) {
        for (const game::Vehicle& v : replay_.vehicles())
            if (v.location == game::Location{shown_, *sector_} && replaySeen_.contains(v.id)) return true;
        return false;
    }
    return !vehiclesAt(ui, {shown_, *sector_}).empty();
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

std::string_view MainWindow::pickingId() const {
    switch (pick_) {
        case Pick::None: return {};
        case Pick::MoveTo: return "move-to";
        case Pick::Warp: return "warp";
        case Pick::Colonize: return "colonize";
        case Pick::Attack: return "attack";
        case Pick::Patrol: return "patrol";
        case Pick::LoadCargo: return "load-cargo";
        case Pick::DropCargo: return "drop-cargo";
        case Pick::LaunchRemote: return "launch-units";
        case Pick::RecoverRemote: return "recover-units";
        case Pick::Callback: return "location";
    }
    return {};
}

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
            if (!tagged_.empty()) {
                // A tagged group: the ordinary Scrap window for the sector of the
                // first tagged object; the tags pre-select nothing (§7 Q52).
                if (const game::Vehicle* first = s.vehicle(tagged_.front())) {
                    ScreenArgs a;
                    a.vehicle = first->id;
                    a.location = first->location;
                    orderDone();
                    ui.open(ScreenId::Scrap, a);
                }
                return;
            }
            openFor(ui, ScreenId::Scrap);
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
                // Each tagged object's own flag flips, so a mixed group stays mixed (§7 Q52).
                for (game::VehicleId t : std::vector<game::VehicleId>(tagged_))
                    if (const game::Vehicle* x = s.vehicle(t)) ui.session.issue(game::cmd::SetMinister{t, {}, false, !x->minister});
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
        case OrderId::ReplayStep:
        case OrderId::ReplayRewind: startReplay(ui, id); return;
        case OrderId::Count: return;
    }
}

// ---- Frame update ------------------------------------------------------------------------

void MainWindow::update(UiContext& ui, bool blocked) {
    layOut(ui.map.left, ui.map.right);
    trackMovement(ui);
    // The ending windows, each as it comes at a turn's start, one after
    // another (finale.hpp, spec 06 §7 Q83).
    if (const auto endings = finale_.update(ui.state(), ui.session.player(), ui.session.kind()); !endings.empty()) {
        ScreenArgs args;
        for (FinaleKind k : endings) args.text += (args.text.empty() ? "" : ",") + std::string(finaleArgName(k));
        ui.open(ScreenId::Finale, std::move(args));
    }
    prepareSectors(ui);
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
    // Every window and question is modal (spec 06 §1, §3.4): while one is open,
    // or the main window's own picker, the command buttons, order strip,
    // selectors and report panel take no input (no click, no hover hint), and
    // neither do the map panels and the keys (below).
    inputBlocked_ = blocked || chooser_.has_value();
    statusBar(ui);
    commandPanel(ui);
    reportPanel(ui);
    overlayText(ui);
    statusButtons(ui);
    // The panels lessons point at (docs/LEARNING.md "UI tags").
    ui.tagFrame("panel:system", geo.systemPanel);
    ui.tagFrame("panel:report", geo.reportPanel);
    ui.tagFrame("panel:galaxy", geo.galaxyPanel);
    // And the homeworld's sector, while its system is shown.
    for (const auto& c : ui.state().colonies)
        if (c && c->owner == ui.session.player() && c->homeworld)
            if (const game::Location at = game::locationOf(ui.state().galaxy, c->planet); at.system == shown_)
                ui.tagFrame("sector:home", Rect::fromPosSize(cellOrigin(at.sector), {geo.cell, geo.cell}));
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
        // A question of the main window's, modal like every window, and never
        // covered by a tutorial's input lock.
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ui.promptWindow();
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
    // Flag, empire, leader, game date and the treasury at the original's places
    // (§2.2): x from the status bar's left edge (11), text in Futurist Medium
    // with its cell's top at y 4.
    const game::Empire& e = ui.me();
    const LayoutGeometry& l = *geo.layout;
    const float x0 = geo.left;
    const float textY = 9 + kTextLead;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    drawAt(ui, dl, ui.art.flag(e.race.style), {x0 + 15, 8}, {26, 18});
    ImFont* font = ui.fonts.medium ? ui.fonts.medium : ImGui::GetFont();
    const float size = ui.fontPx(kTextSize);
    auto width = [&](const std::string& t) { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str()).x / ui.k(); };
    auto text = [&](float x, ImU32 color, const std::string& t) { dl->AddText(font, size, ui.at({x, textY}), color, t.c_str()); };
    text(x0 + 47, IM_COL32_WHITE, std::format("{} {}", e.name, e.empireType));
    text(x0 + 231, IM_COL32_WHITE, std::format("{} {}", e.leaderTitle, e.leaderName));
    const float dateX = x0 + l.gameDateX;
    text(dateX, imColor(palette::kLabel), "Game Date");
    text(dateX + width("Game Date "), IM_COL32_WHITE, formatDate(ui.state().turn));
    // Each stockpile ends 2 px left of its 16 px icon.
    static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
    static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
    for (size_t i = 0; i < 3; ++i) {
        const float iconX = x0 + l.resourceIconX[i];
        const std::string amount = std::to_string(e.stockpile.v[i]);
        text(iconX - 2 - width(amount), imColor(kColors[i]), amount);
        drawAt(ui, dl, ui.art.icon16(kIcons[i]), {iconX, 9}, {16, 16});
    }
    if (ui.session.waitingForOthers() && l.layout == ScreenLayout::Large) text(geo.right - 164, IM_COL32(255, 204, 77, 255), "Waiting...");
    ui.tagFrame("status:empire", Rect{{x0 + 12, 4}, {x0 + 226, 26}});
    ui.tagFrame("status:leader", Rect{{x0 + 228, 4}, {dateX - 4, 26}});
    ui.tagFrame("status:date", Rect{{dateX - 2, 4}, {x0 + l.resourceIconX[0] - 70, 26}});
    ui.tagFrame("status:resources", Rect{{x0 + l.resourceIconX[0] - 66, 4}, {x0 + l.resourceIconX[2] + 18, 26}});
    for (size_t i = 0; i < 3; ++i) {
        static constexpr std::array<const char*, 3> kTags{"status:minerals", "status:organics", "status:radioactives"};
        ui.tagFrame(kTags[i], Rect{{x0 + l.resourceIconX[i] - 66, 4}, {x0 + l.resourceIconX[i] + 18, 26}});
    }
}

void MainWindow::statusButtons(UiContext& ui) {
    // The 20x20 buttons of Close.bmp (minimize, close, "T"; rows: normal, under
    // the pointer, held): minimize 2 px from the status bar's right end and 2 px
    // below its top, the T button just left of it while a lesson runs (§2.2;
    // ours shows the lesson panel again, docs/LEARNING.md).
    const float barRight = geo.right - 11;
    ImGui::SetNextWindowPos(ui.at({barRight - 44, 7}));
    ImGui::SetNextWindowSize(ui.size({44, 20}));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin("##statusbuttons", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto button = [&](const char* id, int column, Vec2 at) {
            ImGui::SetCursorScreenPos(ui.at(at));
            const bool clicked = ImGui::InvisibleButton(id, ui.size({20, 20}));
            const int state = ImGui::IsItemActive() ? 2 : ImGui::IsItemHovered() ? 1 : 0;
            drawAt(ui, dl, ui.art.region("Pictures/Game/Buttons/Close.bmp", column * 20, state * 20, 20, 20, false), at, {20, 20});
            return clicked;
        };
        if (button("##minimize", 0, {barRight - 22, 7}) && ui.app) ui.app->minimize();
        if (ui.lessonRunning) {
            if (button("##lesson", 2, {barRight - 42, 7})) {
                audio().play("button");
                ui.requests.toggleLessonPanel = true;
            }
            ui.tagItem("status:lesson");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void MainWindow::commandPanel(UiContext& ui) {
    const LayoutGeometry& l = *geo.layout;
    ImGui::SetNextWindowPos(ui.at(geo.commandArea.min));
    ImGui::SetNextWindowSize(ui.size(geo.commandArea.size()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##commands", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus | blockedFlags());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x0 = geo.left;
    // While the movement log plays the command buttons and selectors are disabled (§7 Q51).
    const bool replaying = replay_.active();
    auto hit = [&](const char* id, Vec2 min, Vec2 size, bool enabled = true) {
        ImGui::SetCursorScreenPos(ui.at(min));
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::InvisibleButton(id, ui.size(size));
        ImGui::EndDisabled();
        return std::pair{clicked && enabled, enabled && ImGui::IsItemHovered()};
    };

    // Command buttons: 34-px cells from (13, 36).
    for (size_t i = 0; i < kCommands.size(); ++i) {
        const CommandButton& b = kCommands[i];
        const Vec2 at{x0 + 13 + float(i % 6) * 34, 36 + float(i / 6) * 34};
        ImGui::PushID(int(i));
        const auto [clicked, hovered] = hit("cmd", at, {34, 34}, !replaying);
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
    // can carry it out (§2.8). At 1024x768 all 40 places fit on one page; at
    // 800x600 five columns show at a time, on four pages the arrows turn (§2.3).
    const LitOrders lit = litNow(ui);
    const game::GameState& s = ui.state();
    const game::Vehicle* v = selectedVehicle(ui);
    const game::Colony* c = selectedColony(ui);
    const game::Fleet* f = fleet_ ? s.fleet(*fleet_) : nullptr;
    orderPage_ = std::clamp(orderPage_, 0, l.orderPages - 1);
    const float stripX = x0 + l.orderStrip.x + 16;
    const std::array<Vec2, 2> pagerAt{Vec2{x0 + l.pagerLeft, 44}, Vec2{x0 + l.pagerRight, 44}};
    // Orders on another page: their lesson tags outline the arrow that leads there, the
    // shorter way round (ours); the lesson says to press it (UiTag::pager).
    std::array<std::vector<std::string>, 2> pagerTags;
    for (size_t col = 0; col < kOrderStrip.size(); ++col)
        for (size_t row = 0; row < 2; ++row) {
            const OrderSlot& slot = kOrderStrip[col][row];
            const OrderPlace place = orderPlace(l, int(col * 2 + row));
            const std::string_view tagId = slot.order ? learn::orderStripId(orderSlotKey(*slot.order)) : std::string_view{};
            if (place.page != orderPage_) {
                if (!tagId.empty()) {
                    const int forward = (place.page - orderPage_ + l.orderPages) % l.orderPages;
                    pagerTags[forward <= l.orderPages / 2 ? 1 : 0].push_back("order:" + std::string(tagId));
                }
                continue;
            }
            const Vec2 at{stripX + float(place.column) * 34, 36 + float(place.row) * 34};
            const bool enabled = slot.order && lit[static_cast<size_t>(*slot.order)];
            ImGui::PushID(int(col * 2 + row) + 100);
            const auto [clicked, hovered] = hit("order", at, {34, 34});
            ImGui::PopID();
            if (!tagId.empty()) ui.tagItem("order:" + std::string(tagId));
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
    ui.tagFrame("panel:orders", Rect{{stripX, 36}, {stripX + float(l.orderColumns) * 34, 36 + 2 * 34}});
    // Page arrows at both ends: they turn the pages at 800x600 (wrapping) and are dim at 1024x768.
    const bool paging = l.orderPages > 1;
    for (const int side : {0, 1}) {
        const Vec2 at = pagerAt[size_t(side)];
        ImGui::PushID(400 + side);
        const auto [clicked, hovered] = hit("pager", at, {14, 50}, paging);
        ImGui::PopID();
        for (const std::string& tag : pagerTags[size_t(side)]) ui.tagPager(tag);
        const int state = !paging ? 3 : ImGui::IsItemActive() ? 2 : hovered ? 1 : 0;
        dl->AddRect(ui.at(at - Vec2{2, 7}), ui.at(at + Vec2{15, 60}), imColor(palette::kDisabled));
        drawAt(ui, dl, ui.art.region("Pictures/Game/Buttons/BigLeftRightArrows.bmp", side * 14, state * 50, 14, 50, false), at, {14, 50});
        if (clicked) orderPage_ = turnOrderPage(l, orderPage_, side == 0 ? -1 : 1);
    }

    // Selection cycles (ship, fleet, colony): the Nextprev.bmp selectors at the panel's top right.
    for (int row = 0; row < 3; ++row) {
        const Vec2 at = geo.selectors + Vec2{0, float(row) * 24};
        ImGui::PushID(200 + row);
        const auto [prev, prevHover] = hit("prev", at, {16, 24}, !replaying);
        ImGui::PopID();
        ImGui::PushID(300 + row);
        const auto [next, nextHover] = hit("next", at + Vec2{32, 0}, {16, 24}, !replaying);
        ImGui::PopID();
        const int state = replaying ? 3 : prevHover || nextHover ? 1 : 0;
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
                                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus | blockedFlags());
    const float tabsY = geo.reportPanel.size().y - 31;
    bool tabsFor = false, planetTabs = false;
    const bool single = object_ || vehicle_;
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
                reportFromList_ = true;
                ++selections_;
            }
            if (const game::Colony* col = s.colony(id); col && col->owner == me) ui.tagItem("report:colony");   // for lessons
            if (script::collectingItems()) {
                // Input scripts name the rows by kind: report:colony (the player's), report:planet, report:object.
                const game::Colony* col = s.colony(id);
                const bool planet = o.kind == game::ObjectKind::Planet || o.kind == game::ObjectKind::Asteroids;
                script::reportItem(col && col->owner == me ? "report:colony" : planet ? "report:planet" : "report:object");
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
                if (ImGui::GetIO().KeyShift) {
                    toggleTag(ui, v->id);
                } else {
                    selectVehicle(ui, v->id);
                    reportFromList_ = true;
                }
            }
            script::reportItem(v->owner == me ? "report:ship" : "report:other");   // input scripts: rows by kind
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
    if (reportFromList_ && single && tagged_.empty()) {
        // Back to the list of everything in the sector: the up-arrow button of
        // DetailUp.bmp, one 33x21 cell flush with the report's top right
        // corner, at (257,0) of the 290x361 report; its rows are normal, under
        // the pointer, held and disabled. Only a report opened from the sector's
        // list has it. A click selects the shown object's sector again: several
        // visible objects bring the list back, one its report (spec 06 §7 Q98,
        // confirmed: binary). Drawn in the report's body, which takes the
        // pointer there; disabled while the movement log replays, as the
        // selectors are (inferred).
        const Vec2 at = geo.reportPanel.min + Vec2{257, 0};
        const ImVec2 keep = ImGui::GetCursorScreenPos();
        const bool enabled = !replay_.active();
        ImGui::SetCursorScreenPos(ui.at(at));
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::InvisibleButton("##toList", ui.size({33, 21}));
        ImGui::EndDisabled();
        const int state = !enabled ? 3 : ImGui::IsItemActive() ? 2 : ImGui::IsItemHovered() ? 1 : 0;
        drawAt(ui, ImGui::GetWindowDrawList(), ui.art.region("Pictures/Game/Buttons/DetailUp.bmp", 0, state * 21, 33, 21, false), at, {33, 21});
        ImGui::SetCursorScreenPos(keep);
        ImGui::Dummy(ImVec2(0, 0));
        if (clicked && enabled) {
            std::optional<game::Location> where;
            if (const game::Vehicle* v = vehicle_ ? s.vehicle(*vehicle_) : nullptr) where = v->location;
            else if (object_) where = game::Location{s.galaxy.object(*object_).system, s.galaxy.object(*object_).sector};
            if (where) {
                shown_ = where->system;
                selectSector(ui, where->sector, false);
            }
        }
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
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void MainWindow::overlayText(UiContext& ui) {
    // Text in the system panel (§2.4 "Text in the system panel"): fonts of
    // §5.4, transparent backgrounds, places given as the text cell's top (or
    // its bottom), so each is drawn below that by the face's internal leading.
    const game::GameState& s = ui.state();
    if (!shown_.valid()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float k = ui.k();
    ImFont* small = ui.fonts.small ? ui.fonts.small : ImGui::GetFont();
    ImFont* button = ui.fonts.bold ? ui.fonts.bold : ImGui::GetFont();
    ImFont* body = ui.fonts.medium ? ui.fonts.medium : ImGui::GetFont();
    ImFont* tiny = ui.fonts.tiny ? ui.fonts.tiny : small;
    const float tinySize = ui.fonts.tiny ? kTinySize : kSmallSize;
    auto widthOf = [&](ImFont* f, float size, const std::string& str) { return f->CalcTextSizeA(ui.fontPx(size), FLT_MAX, 0.0f, str.c_str()).x / k; };
    auto put = [&](ImFont* f, float size, Vec2 at, ImU32 c, const std::string& str) { dl->AddText(f, ui.fontPx(size), ui.at(at), c, str.c_str()); };
    // A Small text centred on x with its cell's bottom at `bottom`.
    auto smallAbove = [&](float cx, float bottom, ImU32 c, const std::string& str) {
        put(small, kSmallSize, {std::floor(cx - widthOf(small, kSmallSize, str) * 0.5f), bottom - kSmallCell + kSmallLead}, c, str);
    };
    const game::StarSystem& sys = s.galaxy.system(shown_);
    const Rect& panel = geo.systemPanel;
    const game::InterfaceOptions& opts = ui.options();

    // The system name: SE4 Text button, white, at (5,10) of the panel; during
    // the movement log replay the day follows it (§7 Q51).
    std::string title = sys.name;
    if (replay_.active()) title += std::format("  (Day {})", replay_.day());
    put(button, kTitleSize, panel.min + Vec2{5, 10 + kTitleLead}, IM_COL32_WHITE, title);
    const bool explored = ui.me().hasExplored(shown_);
    if (!explored) {
        const std::string word = "Unexplored";
        put(button, kTitleSize, {std::floor(panel.center().x - widthOf(button, kTitleSize, word) * 0.5f), panel.min.y + std::floor(panel.size().y / 3.0f) + kTitleLead},
            IM_COL32_WHITE, word);
    } else {
        // Warp point names (on by default): the explored destination's name in
        // Futurist small, white, centred, its bottom on the cell's bottom edge;
        // a second name in the same sector goes one line lower.
        std::map<game::Sector, int> warpLines, planetLines;
        if (opts.warpPointNames)
            for (game::ObjectId id : s.galaxy.warpPoints(sys.id)) {
                const game::SpaceObject& wp = s.galaxy.object(id);
                if (!wp.destination.valid()) continue;
                const game::SystemId to = s.galaxy.object(wp.destination).system;
                if (!s.options.omnipresent && !ui.me().hasExplored(to)) continue;
                const int line = warpLines[wp.sector]++;
                smallAbove(sectorCenter(wp.sector).x, cellOrigin(wp.sector).y + geo.cell + kSmallCell * float(line), IM_COL32_WHITE, s.galaxy.system(to).name);
            }
        // Planet names (off by default), stacked apart from the warp point names;
        // the planet in sector (0,0) never gets its name.
        if (opts.planetNames)
            for (game::ObjectId id : sys.objects) {
                const game::SpaceObject& o = s.galaxy.object(id);
                if (o.kind != game::ObjectKind::Planet || (o.sector.x == 0 && o.sector.y == 0)) continue;
                if (!game::sight::canSeePlanet(ui.rules(), s, ui.session.player(), id)) continue;  // a hidden planet has no name (spec 01 §6.9)
                const int line = planetLines[o.sector]++;
                smallAbove(sectorCenter(o.sector).x, cellOrigin(o.sector).y + geo.cell + kSmallCell * float(line), IM_COL32_WHITE, o.name);
            }
        // Facility letter markers (§2.4, §7 Q44): on colonies of ours and of our
        // Military Alliance and Partnership partners, in the owner's colour, in
        // the small face, packed right to left from the sprite square's bottom right.
        if (opts.facilityMarkers != 0)
            for (game::ObjectId id : sys.objects) {
                const game::SpaceObject& o = s.galaxy.object(id);
                const game::Colony* col = s.colony(id);
                if (o.kind != game::ObjectKind::Planet || !col || !showsFacilityMarkers(s, ui.session.player(), col->owner)) continue;
                // Only on a colony the player sees: a partner's colony needs sensors there (spec 01 §6.9).
                if (col->owner != ui.session.player() && !game::sight::canSeeColony(ui.rules(), s, ui.session.player(), id)) continue;
                if (replay_.active() && replay_.colonyOwner(id) != col->owner) continue;
                const std::vector<std::string> groups = facilityMarkerGroups(ui.rules(), *col, opts.facilityMarkers);
                if (groups.empty()) continue;
                std::vector<int> widths;
                for (const std::string& g : groups) widths.push_back(int(std::lround(widthOf(tiny, tinySize, g))));
                const std::vector<MarkerPlace> places = packFacilityMarkers(widths, int(kSpriteSize));
                const Vec2 square = spriteSquare(o.sector);
                const ImU32 color = empireColor(s, col->owner);
                for (size_t i = 0; i < groups.size(); ++i)
                    put(tiny, tinySize, square + Vec2{float(places[i].x), kSpriteSize - kTinyCell * float(places[i].line + 1)}, color, groups[i]);
            }
        // Coordinate location (on by default): the sector under the pointer, and
        // the range from the selected sector of this system while it is marked
        // (it holds an object we see, §7 Q64); "Range: 0" on that sector itself.
        if (opts.coordinateLocation && hover_) {
            const std::string line = map_style::coordinateLine(*hover_, selectedSectorMarked(ui) ? sector_ : std::nullopt);
            put(small, kSmallSize, geo.layout->coordinateLine + Vec2{geo.left, kSmallLead}, IM_COL32_WHITE, line);
        }
    }

    // Numbers in the sectors, in the small map face (§2.4 "Sector contents"):
    // vehicle counts in the owner's colour on a black box the size of the text,
    // a lone unit group's units in white, the number of stellar objects in white.
    auto count = [&](Vec2 at, ImU32 color, bool box, const std::string& n) {
        const float w = widthOf(tiny, tinySize, n);
        if (box) dl->AddRectFilled(ui.at(at), ui.at(at + Vec2{w, kTinyCell}), IM_COL32_BLACK);
        put(tiny, tinySize, at, color, n);
    };
    for (const ShownSector& sec : sectors_) {
        const SectorView& view = sec.view;
        const Vec2 square = spriteSquare(sec.sector);
        if (view.stellarCount > 1) count(square + Vec2{0, kSpriteSize - kTinyCell}, IM_COL32_WHITE, false, std::to_string(view.stellarCount));
        if (view.flags) {
            for (size_t i = 0; i < view.owners.size(); ++i)
                count(square + Vec2{14, float(view.flagStep) * float(i)}, empireColor(s, view.owners[i].empire), true, std::to_string(view.owners[i].count));
        } else if (view.count) {
            const std::string n = std::to_string(*view.count);
            const ImU32 color = view.unitCount ? IM_COL32_WHITE : empireColor(s, view.owners.front().empire);
            count(square + Vec2{kSpriteSize - widthOf(tiny, tinySize, n), kSpriteSize - kTinyCell}, color, !view.unitCount, n);
        }
    }

    // Waypoints 1–10 and the tagged minefields ("M"): Futurist small cyan at the
    // cell's bottom-right inner corner (§2.4); their frames are drawn with the panel.
    const ImU32 cyan = imColor(map_style::kWaypoint);
    auto corner = [&](game::Sector sec, const std::string& str) {
        const Vec2 cell = cellOrigin(sec);
        put(small, kSmallSize, {cell.x + geo.cell - 1 - widthOf(small, kSmallSize, str), cell.y + geo.cell - kSmallCell + kSmallLead}, cyan, str);
    };
    const auto& wps = ui.me().waypoints;
    for (size_t i = 0; i < wps.size(); ++i)
        if (wps[i].set && wps[i].location.system == shown_) corner(wps[i].location.sector, std::to_string(i));
    for (const game::Location& m : ui.me().taggedMinefields)
        if (m.system == shown_) corner(m.sector, "M");

    // The movement line (§2.4 "Movement lines"), last, over everything else in
    // the panel: a 1 px blue line through the sector centres, an 8 px ring on
    // each square entered, and white Tiny numbers, the turn each is reached.
    // While the movement log replays, the route stored when it began is drawn
    // over the vehicles moving underneath (§7 Q86, confirmed: binary).
    const game::movement::PlannedRoute* route = nullptr;
    if (explored && settings().showMovementLines) {
        if (replay_.active()) {
            if (replayLine_) route = &*replayLine_;
        } else if (const auto subject = movementLineSubject(ui.rules(), s, ui.session.player(), tagged_.empty() ? vehicle_ : std::nullopt,
                                                            tagged_.empty() ? fleet_ : std::nullopt)) {
            if (!line_ || line_->subject != *subject || line_->revision != ui.session.revision() || line_->turn != s.turn)
                line_ = MovementLineCache{*subject, ui.session.revision(), s.turn, movementLineRoute(ui.rules(), s, *subject)};
            route = &line_->route;
        }
    }
    if (route) {
        auto centre = [](game::Sector sec) {
            const Vec2 c = sectorCenter(sec);
            return PixelPoint{int(std::lround(c.x)), int(std::lround(c.y))};
        };
        const ImU32 blue = imColor(kMovementLineRgb), white = imColor(kMovementNumberRgb);
        auto pixel = [&](PixelPoint p) { dl->AddRectFilled(ui.at({float(p.x), float(p.y)}), ui.at({float(p.x + 1), float(p.y + 1)}), blue); };
        for (const LineMark& m : movementLineMarks(*route, shown_, centre)) {
            if (m.kind == LineMark::Kind::Ring) {
                for (const PixelPoint o : ringOffsets()) pixel({m.at.x + o.x, m.at.y + o.y});
            } else if (m.kind == LineMark::Kind::Segment) {
                for (const PixelPoint p : linePixels(m.at, m.to)) pixel(p);
            } else {
                // Centred: top-left at C - (w div 2, h div 2), w and h the text's size.
                const std::string n = std::to_string(m.number);
                const int w = int(std::lround(widthOf(tiny, tinySize, n))), h = int(kTinyCell);
                put(tiny, tinySize, {float(m.at.x - w / 2), float(m.at.y - h / 2)}, white, n);
            }
        }
    }

    // The hover hint (§2.3): the button's name and its key, centred in the hint area.
    if (!hintName_.empty()) {
        const Rect& hint = geo.layout->hint;
        const float cx = geo.left + hint.center().x;
        put(button, kTitleSize, {std::floor(cx - widthOf(button, kTitleSize, hintName_) * 0.5f), hint.min.y + kTitleLead}, IM_COL32_WHITE, hintName_);
        if (!hintKey_.empty())
            put(small, kSmallSize, {std::floor(cx - widthOf(small, kSmallSize, hintKey_) * 0.5f), hint.min.y + 18 + kSmallLead}, IM_COL32_WHITE, hintKey_);
    }

    // Our own notices (a pick waiting for its target, refused orders): above the coordinate line.
    const Vec2 noteAt{panel.min.x + 6, panel.max.y - 40};
    // A tutorial's lock keeps Esc for the step, so the prompt does not offer it then.
    if (pick_ != Pick::None) put(body, kTextSize, noteAt, IM_COL32(255, 220, 90, 255), ui.lessonLocked ? pickPrompt_ : pickPrompt_ + "   (Esc to cancel)");
    else if (!note_.empty() && ui.time < noteUntil_) put(body, kTextSize, noteAt, IM_COL32(255, 220, 90, 255), note_);

    // The hovered system on the galaxy panel: its name in cyan, in the Body face,
    // at the first corner that fits (§2.6, §5.4).
    if (galaxyHover_) {
        const GalaxyGrid g = galaxyGrid();
        const game::StarSystem& h = s.galaxy.system(*galaxyHover_);
        const Vec2 cell = galaxyCell(g, h);
        const float w = widthOf(body, kTextSize, h.name);
        const map_style::Point p = map_style::nameCorner(cell.x - geo.galaxyPanel.min.x, cell.y - geo.galaxyPanel.min.y, g.cw, g.ch, w + 2,
                                                         kTextCell - kTextLead, geo.galaxyPanel.size().x, geo.galaxyPanel.size().y);
        put(body, kTextSize, geo.galaxyPanel.min + Vec2{p.x + 1, p.y}, imColor(map_style::kHover), h.name);
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

    // The four movement-log keys start the replay whatever their buttons show
    // (§2.8); while it plays no other main-window key works (§3).
    for (const OrderId o : {OrderId::ReplayPlay, OrderId::ReplayShip, OrderId::ReplayStep, OrderId::ReplayRewind})
        if (const auto a = orderAction(o); a && pressed(*a)) {
            startReplay(ui, o);
            return;
        }
    if (replay_.active()) return;

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
        note(ui, settings().showMovementLines ? "Movement lines on" : "Movement lines off");  // OpenSE4's own, as for Ctrl+S
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
        if (!a || !pressed(*a) || isReplayOrder(o)) continue;
        if (!lit) lit = litNow(ui);
        if ((*lit)[i]) runOrder(ui, o);
    }
}

// ---- World rendering -----------------------------------------------------------------------

void MainWindow::render(gfx::Renderer2D& r, UiContext& ui) {
    layOut(ui.map.left, ui.map.right);
    trackMovement(ui);
    r.rect(Rect{{geo.left, 0}, {geo.right, frameH()}}, Color::hex(0x000000));
    drawSystem(r, ui);
    drawGalaxy(r, ui);
    drawFrame(r, ui);
}

void MainWindow::drawFrame(gfx::Renderer2D& r, UiContext& ui) {
    // The original's frame strips at their places (§2.1); black is see-through.
    const LayoutGeometry& l = *geo.layout;
    auto strip = [&](const char* name) { return ui.art.image(std::string(l.screens) + name); };
    auto put = [&](const Sprite& s, Vec2 at) {
        if (s) r.sprite(s.tex, Rect::fromPosSize(at, s.size), s.uv);
    };
    if (!geo.extended) {
        for (int i = 0; i < l.pieceCount; ++i) {
            const FramePiece& p = l.pieces[size_t(i)];
            const Sprite s = strip(p.file);
            // The 800x600 RightFiller is a 1x1 file the game skips.
            if (s && s.size.x > 1) put(s, p.at);
        }
        return;
    }
    // A wider 1024x768 frame (OpenSE4's extended layout): the strips stretched.
    // A horizontal strip: its two halves at the ends, the middle column stretched between.
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

// ---- Ship movement animation and the movement log replay -----------------------------------

void MainWindow::trackMovement(UiContext& ui) {
    // Once per frame: update() and render() both call this.
    if (ui.time == trackedAt_) return;
    trackedAt_ = ui.time;
    const game::GameState& s = ui.state();
    // A new turn: where the vehicles were seen before it (a network game's replay is rebuilt from it).
    if (s.turn != seenTurn_) {
        if (seenTurn_ != UINT32_MAX && !ui.session.turnBased()) {
            beforeTurn_ = glides_.lastSeen();
            beforeTurnHeadings_ = glides_.lastHeadings();
            beforeTurnFor_ = s.turn;
        }
        replay_.stop();
        replayLine_.reset();
        seenTurn_ = s.turn;
    }
    const bool wasReplaying = replay_.active();
    MovementReplay::Frame f;
    f.now = ui.time;
    f.shown = shown_;
    f.animate = settings().animateSystemMovement;
    f.cellPixels = geo.cell;
    f.seen = [this](game::VehicleId id) { return replaySeen_.contains(id); };
    f.turns = [&](game::VehicleId id) {
        const MovementLog* log = replay_.log();
        const auto it = log ? log->vehicles.find(id) : decltype(log->vehicles.end()){};
        return log && it != log->vehicles.end() && turnsToHeading(ui.rules(), s, it->second);
    };
    replay_.update(f);
    // Ctrl+U: the view follows the object, to its system before each day.
    if (const auto followed = replay_.following())
        for (const game::Vehicle& v : replay_.vehicles())
            if (v.id == *followed) {
                shown_ = v.location.system;
                sector_ = v.location.sector;
                break;
            }
    // The end of the replay brings back the current turn and the system shown
    // before it; the selected sector becomes sector 0 of that system and the
    // selection is refreshed: one visible object there opens its report (and
    // its movement line), none or several leave no line (spec 06 §7 Q86,
    // confirmed: binary). That is no selection the player made.
    if (wasReplaying && !replay_.active()) {
        if (replayShownBefore_) shown_ = *replayShownBefore_;
        replayShownBefore_.reset();
        replayLine_.reset();
        const uint64_t made = selections_;
        selectSector(ui, game::Sector{0, 0}, false);
        selections_ = made;
    }
    std::vector<ShipGlides::Seen> visible;
    for (const game::Vehicle& v : s.vehicles)
        if (knownVehicle(ui, v)) visible.push_back({v.id, v.location, v.heading, turnsToHeading(ui.rules(), s, v)});
    // The pause after each step: Settings.txt `System Ship Movement Delay
    // Milliseconds`, read as seconds as the original does (spec 06 §1.9,
    // §2.4); the movement log replay never pauses (spec 06 §7 Q62).
    const double pause = ShipGlides::stepPause(ui.rules().setting("System Ship Movement Delay Milliseconds", 0));
    glides_.track(ui.time, shown_, settings().animateSystemMovement && !replay_.active(), visible, geo.cell, pause);
}

void MainWindow::startReplay(UiContext& ui, OrderId id) {
    // The four keys start the replay whatever their buttons show (§2.8, §7
    // Q51); while a day's entries are animated they are ignored (§7 Q62).
    if (replay_.active() && replay_.animating()) return;
    const game::GameState& s = ui.state();
    const game::EmpireId me = ui.session.player();
    if (!replay_.available(s.turn)) {
        replaySeen_.clear();
        const game::GameState* start = ui.session.turnStart();
        if (ui.session.canReplayLastTurn() && start && start->turn + 1 == s.turn) {
            // Play the turn again from its start and record its 30 days.
            const BusyPointer busy;
            MovementRecorder recorder(*start);
            ui.session.replayLastTurn([&recorder](int day, const game::GameState& at) { recorder.day(day, at); },
                                      [&recorder](const game::MovementStep& st) { recorder.step(st); });
            auto log = std::make_shared<MovementLog>(recorder.take(s.turn));
            // What the viewer sees: its own objects, those it saw at the start and those it sees now.
            for (const auto& [vid, v] : log->vehicles)
                if (v.owner == me) replaySeen_.insert(vid);
            if (me.valid() && me.index() < start->empires.size())
                for (const game::VehicleId vid : start->empire(me).knowledge.visibleVehicles) replaySeen_.insert(vid);
            for (const game::VehicleId vid : ui.me().knowledge.visibleVehicles) replaySeen_.insert(vid);
            replay_.setLog(std::move(log));
        } else if (!ui.session.turnBased() && beforeTurnFor_ == s.turn && !beforeTurn_.empty()) {
            // Only our own view of the turn: rebuilt from where we saw everything (inferred).
            std::set<game::VehicleId> seenNow;
            for (const game::Vehicle& v : s.vehicles)
                if (knownVehicle(ui, v)) seenNow.insert(v.id);
            auto log = std::make_shared<MovementLog>(approximateLog(beforeTurn_, s, seenNow, s.turn, beforeTurnHeadings_));
            for (const auto& [vid, v] : log->vehicles) replaySeen_.insert(vid);
            replay_.setLog(std::move(log));
        }
    }
    if (!replay_.available(s.turn)) {
        note(ui, "Replay Unavailable: there is no movement log for this turn.");
        return;
    }
    const bool wasActive = replay_.active();
    switch (id) {
        case OrderId::ReplayPlay: replay_.play(); break;
        case OrderId::ReplayStep: replay_.step(); break;
        case OrderId::ReplayRewind: replay_.rewind(); break;
        case OrderId::ReplayShip: {
            const std::vector<game::VehicleId> movers = replay_.log()->movers(me);
            if (movers.empty()) {
                note(ui, "None of your ships moved this turn.");
                return;
            }
            replay_.playFollowing(movers);
            break;
        }
        default: return;
    }
    if (replay_.active() && !wasActive) {
        replayShownBefore_ = shown_;
        // The open report's route, worked out from the current turn before the
        // replay, stays drawn while the days play (spec 06 §7 Q86).
        replayLine_.reset();
        if (const auto subject = movementLineSubject(ui.rules(), s, me, tagged_.empty() ? vehicle_ : std::nullopt, tagged_.empty() ? fleet_ : std::nullopt))
            replayLine_ = movementLineRoute(ui.rules(), s, *subject);
        tagged_.clear();
    }
}

void MainWindow::prepareSectors(UiContext& ui) {
    // What each sector of the shown system draws this frame (sector_view.hpp):
    // the game's vehicles the player sees, or the replay's at its day; those
    // gliding to their square or animated by the replay are drawn on their way.
    sectors_.clear();
    if (!shown_.valid()) return;
    const game::GameState& s = ui.state();
    std::map<game::Sector, std::pair<std::vector<game::ObjectId>, std::vector<const game::Vehicle*>>> bySector;
    // The stellar objects the player sees (spec 01 §6.9): a hidden planet is
    // not drawn and does not count in the sector's number of stellar objects.
    for (game::ObjectId id : shownStellarObjects(ui.rules(), s, ui.session.player(), shown_)) bySector[s.galaxy.object(id).sector].first.push_back(id);
    if (replay_.active()) {
        for (const game::Vehicle& v : replay_.vehicles())
            if (v.location.system == shown_ && replaySeen_.contains(v.id) && !replay_.motion(v.id))
                bySector[v.location.sector].second.push_back(&v);
    } else {
        for (const game::Vehicle& v : s.vehicles)
            if (v.location.system == shown_ && knownVehicle(ui, v) && !glides_.find(v.id)) bySector[v.location.sector].second.push_back(&v);
    }
    for (auto& [sector, contents] : bySector) {
        ShownSector out;
        out.sector = sector;
        out.view = sectorView(ui.rules(), s, ui.session.player(), contents.first, contents.second, int(geo.cell));
        out.vehicles = std::move(contents.second);
        sectors_.push_back(std::move(out));
    }
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
    const bool grid = ui.options().systemGrid;
    if (grid)
        for (int i = 0; i <= 13; ++i) {
            const float x = geo.sectorOrigin.x + geo.cell * float(i), y = geo.sectorOrigin.y + geo.cell * float(i);
            const Color c = rgb(map_style::kGrid);
            r.line({x, geo.sectorOrigin.y}, {x, geo.sectorOrigin.y + 13 * geo.cell}, 1.0f, c);
            r.line({geo.sectorOrigin.x, y}, {geo.sectorOrigin.x + 13 * geo.cell, y}, 1.0f, c);
        }
    // Stellar objects are copied opaque unless the system type masks black (Mask
    // Background Objs), or at 800x600 while the grid shows; vehicles are always keyed.
    const bool keyed = type.maskBackgroundObjects || (geo.layout->keyWithGrid && grid);

    // Every sprite is drawn 36×36 in its sector's sprite square, never scaled.
    auto spriteAt = [&](const Sprite& sp, Vec2 topLeft) {
        if (sp) r.sprite(sp.tex, Rect::fromPosSize(topLeft, {kSpriteSize, kSpriteSize}), sp.uv);
        return static_cast<bool>(sp);
    };
    auto headingOf = [&](const game::Vehicle& v) {
        if (!turnsToHeading(rules, s, v)) return 0;
        // The engine keeps each vehicle's heading (saved with the game); the
        // replay its own from the start of the turn (§2.4, §7 Q62).
        return replay_.active() ? replay_.heading(v.id) : int(v.heading % 8);
    };
    auto miniOf = [&](const game::Vehicle& v, int heading) {
        const std::string& style = v.owner.valid() ? s.empire(v.owner).race.style : std::string{};
        return ui.art.shipMini(style, rules.hull(s.design(v.design).hull), true, heading);
    };
    auto placeholder = [&](Vec2 c, game::EmpireId owner) { r.triangle(c + Vec2{0, -9}, c + Vec2{-7, 7}, c + Vec2{7, 7}, empireCol(s, owner)); };

    if (explored) {
        const ColonizeTech tech = colonizeTech(rules, ui.me());
        for (const ShownSector& sec : sectors_) {
            if (!sec.view.stellar) continue;
            const game::SpaceObject& o = s.galaxy.object(*sec.view.stellar);
            const Vec2 square = spriteSquare(sec.sector);
            if (!spriteAt(ui.art.planet(rules.data().sectorObjectTypes[o.sectorType].picture, keyed), square))
                r.disc(square + Vec2{18, 18}, 12.0f, o.kind == game::ObjectKind::Star ? Color::hex(0xffe080) : Color::hex(0x8090a0));
        }
        // Colonies carry the owner's small flag at the top right; planets we could
        // colonise get the classic star (green: breathable, red: needs domes).
        for (game::ObjectId id : sys.objects) {
            const game::SpaceObject& o = s.galaxy.object(id);
            // A planet the player does not see gets no mark at all (spec 01 §6.9).
            if (!game::sight::canSeePlanet(rules, s, ui.session.player(), id)) continue;
            const Vec2 c = sectorCenter(o.sector);
            const game::Colony* col = s.colony(id);
            std::optional<game::EmpireId> owner = col ? std::optional(col->owner) : std::nullopt;
            if (replay_.active()) owner = replay_.colonyOwner(id);
            // The colony's mark only when the player sees the colony by the
            // detection rule; an unseen colony's planet looks empty and gets
            // the colonize star when its type fits (spec 01 §6.9, spec 06 §2.4).
            if (owner && *owner != ui.session.player() && !game::sight::canSeeColony(rules, s, ui.session.player(), id)) owner.reset();
            if (owner) {
                const Sprite flag = ui.art.flag(s.empire(*owner).race.style, false);
                if (flag) r.sprite(flag.tex, Rect::fromPosSize(c + Vec2{4, -17}, {14, 10}), flag.uv);
                else r.rect(Rect::fromPosSize(c + Vec2{4, -17}, {14, 10}), empireCol(s, *owner));
            } else if (ui.options().colonizableMarkers && o.kind == game::ObjectKind::Planet &&
                       colonizeProblem(rules, s, ui.session.player(), id, tech).empty()) {
                const bool breathe = breathableBy(s, ui.session.player(), o);
                const Sprite star = ui.art.region("Pictures/Game/General.bmp", breathe ? 237 : 261, 16, 7, 7);
                if (star) r.sprite(star.tex, Rect::fromPosSize(c + Vec2{11, -17}, {7, 7}), star.uv);
            }
        }
    }

    // Vehicles (§2.4 "Sector contents"): one owner without a stellar object
    // shows its largest vehicle (or the fleet's icon), otherwise the owners'
    // small flags stacked down the left edge; counts are drawn by overlayText.
    for (const ShownSector& sec : sectors_) {
        const SectorView& view = sec.view;
        const Vec2 square = spriteSquare(sec.sector);
        if (view.flags) {
            for (size_t i = 0; i < view.owners.size(); ++i) {
                const Rect box = Rect::fromPosSize(square + Vec2{0, float(view.flagStep) * float(i)}, {14, 10});
                if (const Sprite flag = ui.art.flag(s.empire(view.owners[i].empire).race.style, false)) r.sprite(flag.tex, box, flag.uv);
                else r.rect(box, empireCol(s, view.owners[i].empire));
            }
            continue;
        }
        if (!view.sprite) continue;
        const auto it = std::find_if(sec.vehicles.begin(), sec.vehicles.end(), [&](const game::Vehicle* v) { return v->id == *view.sprite; });
        if (it == sec.vehicles.end()) continue;
        const game::Vehicle& v = **it;
        const std::string& style = v.owner.valid() ? s.empire(v.owner).race.style : std::string{};
        const Sprite sprite = view.fleetIcon ? ui.art.groupMini(style, "Fleet", true) : miniOf(v, headingOf(v));
        if (!spriteAt(sprite, square)) placeholder(square + Vec2{18, 18}, v.owner);
        // A cloaked vehicle shown this way: a 1 px dotted circle inscribed in its square.
        if (view.cloakRing) {
            const Color c = v.owner.valid() ? empireCol(s, v.owner) : rgb(map_style::kCloakNoColor);
            const Vec2 centre = square + Vec2{kSpriteSize * 0.5f, kSpriteSize * 0.5f};
            constexpr int kDots = 56;
            for (int d = 0; d < kDots; ++d) {
                const float a = float(d) * 2.0f * std::numbers::pi_v<float> / float(kDots);
                const Vec2 p = centre + Vec2{std::cos(a), std::sin(a)} * (kSpriteSize * 0.5f - 0.5f);
                r.rect(Rect::fromPosSize({std::floor(p.x), std::floor(p.y)}, {1, 1}), c);
            }
        }
    }

    // Ships on their way: gliding to their new square, or moved by the replay
    // (turning in 5° steps, then sliding).
    auto largestFirst = [&](std::vector<const game::Vehicle*>& list) {
        std::stable_sort(list.begin(), list.end(), [&](const game::Vehicle* a, const game::Vehicle* b) {
            return rules.hull(s.design(a->design).hull).tonnage > rules.hull(s.design(b->design).hull).tonnage;
        });
    };
    // A mini at any angle (while turning): the quad turned about its centre, sampled like the art.
    auto turnedMini = [&](const game::Vehicle& v, Vec2 c, double angle) {
        const Sprite sp = miniOf(v, 0);
        if (!sp) {
            placeholder(c, v.owner);
            return;
        }
        const float a = float(angle) * std::numbers::pi_v<float> / 180.0f, ca = std::cos(a), sa = std::sin(a);
        auto at = [&](float x, float y) { return c + Vec2{x * ca - y * sa, x * sa + y * ca}; };
        const float h = kSpriteSize * 0.5f;
        r.spriteQuad(sp.tex, {at(-h, -h), at(h, -h), at(h, h), at(-h, h)}, sp.uv);
    };
    if (replay_.active()) {
        // Each entry on its own, the others of the day waiting where they start (§7 Q62).
        for (const game::Vehicle& v : replay_.vehicles()) {
            if (v.location.system != shown_ || !replaySeen_.contains(v.id)) continue;
            if (const auto m = replay_.motion(v.id)) turnedMini(v, gridPoint(m->at), turnsToHeading(rules, s, v) ? m->angle : 0.0);
        }
    } else {
        std::map<std::tuple<float, float, float, float, double>, std::vector<const game::Vehicle*>> gliding;
        for (const game::Vehicle& v : s.vehicles) {
            if (v.location.system != shown_ || !knownVehicle(ui, v)) continue;
            if (const ShipGlides::Glide* g = glides_.find(v.id)) gliding[{g->from.x, g->from.y, g->to.x, g->to.y, g->start}].push_back(&v);  // a fleet glides as one
        }
        for (auto& [key, list] : gliding) {
            largestFirst(list);
            const ShipGlides::Glide& g = *glides_.find(list.front()->id);
            turnedMini(*list.front(), gridPoint(ShipGlides::position(g)), turnsToHeading(rules, s, *list.front()) ? ShipGlides::angle(g) : 0.0);
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
    // The selected location: `Dialogs/Selection.bmp` (36x36, eight small yellow
    // marks) over the sector's sprite square with black transparent (spec 06
    // §2.4, observed), while the sector holds something we see (§7 Q64);
    // the four corner lines without the picture.
    if (selectedSectorMarked(ui)) {
        if (const Sprite mark = ui.art.image("Pictures/Game/Dialogs/Selection.bmp"))
            r.sprite(mark.tex, Rect::fromPosSize(spriteSquare(*sector_), {kSpriteSize, kSpriteSize}), mark.uv);
        else brackets(*sector_, kSelectYellow);
    }
    if (pick_ != Pick::None && hover_) brackets(*hover_, Color::hex(0x60ff80));
    // Waypoints and tagged minefields: a cyan 1 px rectangle on the cell's edges
    // (the number or "M" is drawn by overlayText).
    auto cellFrame = [&](game::Sector sec) { r.rectOutline(Rect::fromPosSize(cellOrigin(sec), {geo.cell, geo.cell}), 1.0f, rgb(map_style::kWaypoint)); };
    for (const auto& w : ui.me().waypoints)
        if (w.set && w.location.system == shown_) cellFrame(w.location.sector);
    for (const game::Location& m : ui.me().taggedMinefields)
        if (m.system == shown_) cellFrame(m.sector);
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

// ---- Input scripts ---------------------------------------------------------------------------

namespace {

// "a+!b+c": the words, each with whether it is negated; false on an empty word.
bool queryWords(std::string_view query, std::vector<std::pair<std::string, bool>>& out) {
    size_t start = 0;
    while (start <= query.size()) {
        const size_t plus = query.find('+', start);
        std::string_view w = query.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start);
        const bool negated = !w.empty() && w.front() == '!';
        if (negated) w.remove_prefix(1);
        if (w.empty()) return false;
        out.emplace_back(std::string(w), negated);
        if (plus == std::string_view::npos) break;
        start = plus + 1;
    }
    return !out.empty();
}

bool wholeNumbers(std::string_view s, std::vector<int>& out) {
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        const std::string_view part = s.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        int v = 0;
        const auto [p, ec] = std::from_chars(part.data(), part.data() + part.size(), v);
        if (part.empty() || ec != std::errc{} || p != part.data() + part.size()) return false;
        out.push_back(v);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return true;
}

} // namespace

std::vector<Rect> MainWindow::findSectors(const UiContext& ui, std::string_view query, std::string& error) const {
    std::vector<Rect> out;
    if (!shown_.valid()) {
        error = "the system view shows no system";
        return out;
    }
    auto rectOf = [](game::Sector sec) { return Rect::fromPosSize(cellOrigin(sec), {geo.cell, geo.cell}); };
    if (std::vector<int> xy; wholeNumbers(query, xy)) {
        const game::Sector sec{xy.size() == 2 ? xy[0] : -1, xy.size() == 2 ? xy[1] : -1};
        if (!sec.valid()) error = std::format("sector {} is outside the system (0,0 to 12,12)", query);
        else out.push_back(rectOf(sec));
        return out;
    }
    std::vector<std::pair<std::string, bool>> words;
    if (!queryWords(query, words)) {
        error = std::format("'{}' is not a sector query", query);
        return out;
    }
    const game::GameState& s = ui.state();
    const game::Rules& r = ui.rules();
    const game::EmpireId me = ui.session.player();
    const ColonizeTech tech = colonizeTech(r, ui.me());
    for (int y = 0; y < 13; ++y)
        for (int x = 0; x < 13; ++x) {
            const game::Sector sec{x, y};
            const std::vector<game::ObjectId> objects = objectsAt(ui, sec);
            const std::vector<const game::Vehicle*> vehicles = vehiclesAt(ui, {shown_, sec});
            bool all = true;
            for (const auto& [word, negated] : words) {
                bool holds = false;
                auto anyObject = [&](auto&& pred) { return std::any_of(objects.begin(), objects.end(), [&](game::ObjectId id) { return pred(s.galaxy.object(id)); }); };
                if (word == "any") holds = true;
                else if (word == "empty") holds = objects.empty() && vehicles.empty();
                else if (word == "home")
                    holds = anyObject([&](const game::SpaceObject& o) {
                        const game::Colony* c = s.colony(o.id);
                        return c && c->owner == me && c->homeworld;
                    });
                else if (word == "colony")
                    holds = anyObject([&](const game::SpaceObject& o) {
                        const game::Colony* c = s.colony(o.id);
                        return c && c->owner == me;
                    });
                else if (word == "planet")
                    holds = anyObject([](const game::SpaceObject& o) { return o.kind == game::ObjectKind::Planet || o.kind == game::ObjectKind::Asteroids; });
                else if (word == "colonizable")
                    holds = anyObject([&](const game::SpaceObject& o) {
                        return o.kind == game::ObjectKind::Planet && !s.colony(o.id) && colonizeProblem(r, s, me, o.id, tech).empty();
                    });
                else if (word == "star") holds = anyObject([](const game::SpaceObject& o) { return o.kind == game::ObjectKind::Star; });
                else if (word == "warp-point") holds = anyObject([](const game::SpaceObject& o) { return o.kind == game::ObjectKind::WarpPoint; });
                else if (word == "ship") holds = std::any_of(vehicles.begin(), vehicles.end(), [&](const game::Vehicle* v) { return v->owner == me; });
                else if (word == "enemy") holds = std::any_of(vehicles.begin(), vehicles.end(), [&](const game::Vehicle* v) { return v->owner != me; });
                else if (word == "selected") holds = sector_ && *sector_ == sec;
                else {
                    error = std::format("unknown sector word '{}' (empty, home, colony, planet, colonizable, star, warp-point, ship, enemy, "
                                        "selected, any; joined with +, negated with !)",
                                        word);
                    return {};
                }
                if (holds == negated) {
                    all = false;
                    break;
                }
            }
            if (all) out.push_back(rectOf(sec));
        }
    return out;
}

std::optional<game::Sector> MainWindow::sectorAtFrame(Vec2 p) const { return sectorAt(p); }

bool MainWindow::ownsWindow(ImGuiID window) {
    for (const char* name : {"##commands", "##report", "##statusbuttons"})
        if (ImHashStr(name) == window) return true;
    return false;
}

float MainWindow::galaxyCellSize() const {
    const GalaxyGrid g = galaxyGrid();
    return std::max(g.cw, g.ch);
}

std::vector<Vec2> MainWindow::findSystems(const UiContext& ui, std::string_view query, std::string& error) const {
    std::vector<Vec2> out;
    const GalaxyGrid grid = galaxyGrid();
    const game::GameState& s = ui.state();
    if (std::vector<int> id; wholeNumbers(query, id) && id.size() == 1) {
        if (id[0] < 0 || size_t(id[0]) >= s.galaxy.systems.size()) error = std::format("there is no system {}", query);
        else out.push_back(galaxyCenter(grid, s.galaxy.systems[size_t(id[0])]));
        return out;
    }
    std::vector<std::pair<std::string, bool>> words;
    if (!queryWords(query, words)) {
        error = std::format("'{}' is not a system query", query);
        return out;
    }
    for (const game::StarSystem& sys : s.galaxy.systems) {
        bool all = true;
        for (const auto& [word, negated] : words) {
            bool holds = false;
            if (word == "any") holds = true;
            else if (word == "home") holds = sys.id == ui.me().homeSystem;
            else if (word == "shown") holds = sys.id == shown_;
            else if (word == "explored") holds = ui.me().hasExplored(sys.id);
            else {
                error = std::format("unknown system word '{}' (home, shown, explored, any, or a system's number; joined with +, negated with !)", word);
                return {};
            }
            if (holds == negated) {
                all = false;
                break;
            }
        }
        if (all) out.push_back(galaxyCenter(grid, sys));
    }
    return out;
}

std::optional<game::SystemId> MainWindow::systemAtFrame(const UiContext& ui, Vec2 p) const {
    if (!geo.galaxyPanel.contains(p)) return std::nullopt;
    return galaxySystemAt(ui, p, false);
}

} // namespace opense4::client::classic
