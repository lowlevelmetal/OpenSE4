#include "client/classic/main_window.hpp"

#include "client/app_settings.hpp"
#include "client/audio.hpp"
#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/settings.hpp"

#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <tuple>

namespace opense4::client::classic {

namespace {

// Classic main-window geometry at 1024×768, measured on the original
// (docs/spec/06 §2, docs/spec/07 §UI): the frame is drawn from the
// Screens/1024X768 strips, the panels sit between them.
constexpr float kSectorSize = 50.0f;
constexpr float kSpriteSize = 36.0f;
constexpr const char* kScreens = "Pictures/Game/Screens/1024X768/";

// Panel rectangles for the frame's current extent. Classic 4:3 (left 0,
// right 1024) gives the original layout; a wider frame keeps the system view
// and moves the right-hand panels to the right edge, and with enough room puts
// a full-height galaxy map beside the report.
struct Geometry {
    float left = 0, right = kFrameW;
    Rect statusBar{{0, 0}, {1024, 29}};
    Rect commandPanel{{0, 29}, {957, 106}};
    Rect systemBackground{{3, 105}, {663, 765}};
    Vec2 sectorOrigin{8, 110};
    Rect reportPanel{{671, 117}, {957, 470}};
    Rect galaxyPanel{{671, 479}, {1016, 760}};
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
        g.systemBackground = Rect{{left + 3, 105}, {left + 663, 765}};
        g.sectorOrigin = {left + 8, 110};
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
            g.galaxyPanel = Rect{{x0, 479}, {right - 8, 760}};
        }
    }
    geo = g;
}

const Color kSelectYellow = Color::hex(0xffd040);

// Command buttons: (icon in Main.bmp, window, tooltip). Two rows of six, in the
// original's order (verified against the running game).
struct CommandButton {
    int icon;
    std::optional<ScreenId> screen;
    const char* tooltip;
};
constexpr std::array<CommandButton, 12> kCommands{{
    {0, ScreenId::GameMenu, "Game Menu (F2)"},
    {4, ScreenId::Designs, "Designs (F3)"},
    {2, ScreenId::Planets, "Planets (F4)"},
    {1, ScreenId::Colonies, "Colonies (F5)"},
    {3, ScreenId::Ships, "Ships \\ Units (F6)"},
    {9, ScreenId::Queues, "Construction Queues (F7)"},
    {6, ScreenId::Research, "Research (F8)"},
    {7, ScreenId::Empires, "Empires (F9)"},
    {8, ScreenId::Log, "Log (F10)"},
    {5, ScreenId::EmpireStatus, "Empire Status (F11)"},
    {10, ScreenId::Help, "Help (F1)"},
    {11, std::nullopt, "End Turn (F12)"},
}};

// The order strip: 20 columns × 2 rows at fixed places, each order dimmed when
// it does not apply (the original's layout; manual names in comments).
// `key` matches OrderButton::label; the icon is (band, column) in Orders.bmp,
// whose bands hold four 34-px state rows: normal, hover, lit, dim.
struct OrderSlot {
    const char* key;
    int band, column;
    const char* tooltip;  // shown when the order is not available
};
constexpr std::array<std::array<OrderSlot, 2>, 20> kOrderStrip{{
    {{{"Move", 0, 0, "Move To"}, {"Warp", 0, 5, "Warp"}}},
    {{{"Wpt", 0, 1, "Move To Waypoint"}, {"Colonize", 0, 2, "Colonize"}}},
    {{{"Attack", 0, 4, "Attack"}, {"Fleet", 0, 6, "Fleet Transfer"}}},
    {{{"Supply", 0, 8, "Resupply At Nearest"}, {"Repair", 0, 9, "Repair At Nearest"}}},
    {{{"Clear", 0, 10, "Clear Orders"}, {"Queue", 0, 11, "Build Queue"}}},
    {{{"Cargo", 2, 10, "Cargo Transfer"}, {"Units", 2, 9, "Launch \\ Recover Units"}}},
    {{{"Load", 0, 19, "Load Cargo"}, {"Drop", 0, 20, "Drop Cargo"}}},
    {{{"LaunchRemote", 0, 15, "Launch Units Remotely"}, {"RecoverRemote", 0, 16, "Recover Units Remotely"}}},
    {{{"Sentry", 0, 12, "Sentry"}, {"Explore", 0, 3, "Explore"}}},
    {{{"Patrol", 0, 13, "Set Patrol"}, {"Repeat", 0, 14, "Repeat Orders"}}},
    {{{"Stellar", 1, 5, "Stellar Manipulation"}, {"Name", 2, 0, "Change Name"}}},
    {{{"Scrap", 0, 18, "Scrap \\ Analyze \\ Mothball"}, {"Strategy", 1, 0, "Change Formation \\ Strategy"}}},
    {{{"Orders", 2, 1, "View Orders"}, {"Sweep", 1, 11, "Sweep Mines"}}},
    {{{"ScrapFacilities", 1, 18, "Scrap Facilities"}, {"Jettison", 1, 17, "Jettison Cargo"}}},
    {{{"Cloak", 1, 21, "Cloak"}, {"Decloak", 1, 22, "Decloak"}}},
    {{{"UseComponent", 1, 12, "Use Component"}, {"UseFacility", 1, 13, "Use Facility"}}},
    {{{"Abandon", 1, 3, "Abandon Planet"}, {"Convert", 2, 2, "Convert Resources"}}},
    {{{"", 2, 8, ""}, {"Minister", 0, 22, "Toggle Minister Control"}}},
    {{{"ReplayPlay", 2, 4, "Play Movement Log"}, {"ReplayShip", 2, 5, "Play Movement Log For Ship"}}},
    {{{"ReplayStep", 2, 6, "Play Movement Log Stepped"}, {"ReplayRewind", 2, 7, "Rewind Movement Log"}}},
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

Vec2 sectorCenter(game::Sector s) { return geo.sectorOrigin + Vec2{kSectorSize * (float(s.x) + 0.5f), kSectorSize * (float(s.y) + 0.5f)}; }
// A point on the system grid in sector units (ship_glides.hpp).
Vec2 gridPoint(Vec2 cell) { return geo.sectorOrigin + cell * kSectorSize; }

std::optional<game::Sector> sectorAt(Vec2 p) {
    if (!geo.systemBackground.contains(p)) return std::nullopt;
    const Vec2 rel = (p - geo.sectorOrigin) / kSectorSize;
    if (rel.x < 0 || rel.y < 0) return std::nullopt;
    const game::Sector s{int(std::floor(rel.x)), int(std::floor(rel.y))};
    return s.valid() ? std::optional(s) : std::nullopt;
}

Color colorOf(uint32_t rgb) { return Color::hex(rgb); }

// Where the quadrant is drawn in the galaxy panel.
struct GalaxyTransform {
    Vec2 origin;
    float sx = 1, sy = 1;
};
GalaxyTransform galaxyTransform(const game::Galaxy& g) {
    // The quadrant keeps its proportions, centred in the panel (as the original draws it).
    const Rect inner = geo.galaxyPanel.expanded(-4.0f);
    GalaxyTransform t{inner.min, inner.size().x / float(std::max(1, g.width)), inner.size().y / float(std::max(1, g.height))};
    const float s1 = std::min(t.sx, t.sy);
    t.origin += (inner.size() - Vec2{s1 * float(g.width), s1 * float(g.height)}) * 0.5f;
    t.sx = t.sy = s1;
    return t;
}

Vec2 galaxyPos(const game::Galaxy& g, const game::StarSystem& s) {
    const GalaxyTransform t = galaxyTransform(g);
    return t.origin + Vec2{(float(s.position.x) + 0.5f) * t.sx, (float(s.position.y) + 0.5f) * t.sy};
}

} // namespace

// ---- Selection ---------------------------------------------------------------------------

void MainWindow::reset(UiContext& ui) {
    clearSelection();
    showMovementLines_ = settings().showMovementLines;
    const game::GameState& s = ui.state();
    // Every starting planet is a capital: the one in the home system first.
    const game::EmpireId me = ui.session.player();
    const game::SystemId home = me.valid() && me.index() < s.empires.size() ? s.empire(me).homeSystem : game::SystemId{};
    for (const bool inHome : {true, false})
        for (const auto& c : s.colonies)
            if (c && c->owner == me && c->homeworld && (!inHome || s.galaxy.object(c->planet).system == home)) {
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
    const ClassicSettings& prefs = settings();
    const game::Vehicle* current = selectedVehicle(ui);
    std::vector<game::VehicleId> list;
    for (const game::Vehicle& v : ui.state().vehicles) {
        if (v.owner != ui.session.player() || game::isUnitType(game::vehicleType(ui.rules(), ui.state(), v))) continue;
        // "Next ship": in a turn-based game the ships that still have movement
        // points, in a simultaneous game those without orders (spec 03 §17).
        if (idleOnly && ui.session.turnBased() && v.movement <= 0) continue;
        if (idleOnly && !ui.session.turnBased() && !(v.orders.empty() && (!v.fleet.valid() || game::fleetOrders(ui.state(), *ui.state().fleet(v.fleet)).empty())))
            continue;
        if (prefs.cycleSkipsDamaged && game::vehicleDamageTaken(ui.state(), v) > 0) continue;
        // "Stop once per location": skip other ships in the sector we are leaving.
        if (prefs.cycleOncePerLocation && current && v.id != current->id && v.location == current->location) continue;
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
        // The fleet's orders: copies in its members' lists (spec 03 §8).
        orders = game::fleetOrders(ui.state(), *f);
        repeat = game::fleetRepeats(ui.state(), *f);
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
    const game::EmpireId me = ui.session.player();

    if (const game::Vehicle* v = selectedVehicle(ui); v && v->owner == me) {
        const game::VehicleId id = v->id;
        const game::Fleet* fleet = s.fleet(v->fleet);
        const game::DesignStats st = game::computeDesignStats(r, nullptr, s.design(v->design));
        const bool mobile = st.movement > 0 || fleet;
        const auto abilities = game::vehicleAbilities(r, s, *v);
        const bool hasWarp = !s.galaxy.warpPoints(v->location.system).empty();
        const std::vector<game::Order>& current = fleet ? game::fleetOrders(s, *fleet) : v->orders;
        const bool repeat = fleet ? game::fleetRepeats(s, *fleet) : v->repeatOrders;
        auto simple = [&](game::OrderKind k) { return [this, &ui, k] { giveOrder(ui, game::Order{k}); }; };
        out.push_back({"Move", "Move To (M)", mobile, [this, &ui] { startPick(ui, Pick::MoveTo, "Move To: pick a destination"); }});
        out.push_back({"Wpt", "Move To Waypoint", mobile, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::SelectWaypoint, a);
                       }});
        out.push_back({"Warp", "Warp (W)", mobile && hasWarp, [this, &ui] { startPick(ui, Pick::Warp, "Warp: pick a warp point"); }});
        out.push_back({"Attack", "Attack (A)", st.armed(), [this, &ui] { startPick(ui, Pick::Attack, "Attack: pick a target"); }});
        out.push_back({"Colonize", "Colonize (C)", st.canColonizeRock || st.canColonizeIce || st.canColonizeGas,
                       [this, &ui] { startPick(ui, Pick::Colonize, "Colonize: pick a planet"); }});
        out.push_back({"Explore", "Explore (E)", mobile, simple(game::OrderKind::Explore)});
        out.push_back({"Supply", "Resupply (S)", mobile, simple(game::OrderKind::Resupply)});
        out.push_back({"Repair", "Repair (R)", mobile, simple(game::OrderKind::Repair)});
        out.push_back({"Sentry", "Sentry (Y)", true, simple(game::OrderKind::Sentry)});
        out.push_back({"Patrol", "Set Patrol (P): pick points, then Enter", mobile,
                       [this, &ui] { startPick(ui, Pick::Patrol, "Patrol: pick points, Enter to finish"); }});
        out.push_back({"Repeat", repeat ? "Repeat Orders: on (K)" : "Repeat Orders: off (K)", true,
                       [this, &ui, current, repeat] { replaceOrders(ui, current, !repeat); }});
        out.back().lit = repeat;
        out.push_back({"Clear", "Clear Orders (Del)", !current.empty(), [this, &ui] { replaceOrders(ui, {}, false); }});
        out.push_back({"Fleet", "Fleet Transfer (F)", true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::FleetTransfer, a);
                       }});
        const bool cargo = game::vehicleCargoCapacity(r, s, *v) > 0;
        out.push_back({"Load", "Load Cargo (L)", cargo, [this, &ui] {
                           pickDesign_ = {};
                           startPick(ui, Pick::LoadCargo, "Load population: pick where");
                       }});
        out.push_back({"Drop", "Drop Cargo (D)", cargo && !v->cargo.empty(), [this, &ui] {
                           pickDesign_ = {};
                           startPick(ui, Pick::DropCargo, "Drop population: pick where");
                       }});
        out.push_back({"Cargo", "Cargo Transfer (T)", cargo, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::CargoTransfer, a);
                       }});
        out.push_back({"Units", "Launch \\ Recover Units (U)", cargo, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::LaunchRecover, a);
                       }});
        const bool cloak = game::hasAbility(abilities, game::AbilityKind::CloakLevel);
        if (cloak) {
            out.push_back({"Cloak", "Cloak (Z)", v->status != game::VehicleStatus::Cloaked, simple(game::OrderKind::Cloak)});
            out.push_back({"Decloak", "Decloak (X)", v->status == game::VehicleStatus::Cloaked,
                           simple(game::OrderKind::Decloak)});
        }
        if (game::hasAbility(abilities, game::AbilityKind::MineSweeping))
            out.push_back({"Sweep", "Sweep Mines (Ctrl+M)", true, simple(game::OrderKind::SweepMines)});
        if (game::vehicleHasSpaceYard(r, s, *v))
            out.push_back({"Queue", "Build Queue (Q)", true, [&ui, id] {
                               ScreenArgs a;
                               a.vehicle = id;
                               ui.open(ScreenId::SetQueue, a);
                           }});
        out.push_back({"Stellar", "Stellar Manipulation (B)", true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::StellarManipulation, a);
                       }});
        out.push_back({"Scrap", "Scrap / Analyze / Mothball (G)", true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::Scrap, a);
                       }});
        out.push_back({"Orders", "View Orders (V)", true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::ViewOrders, a);
                       }});
        out.push_back({"Name", "Change Name (N)", true, [&ui, id] {
                           ScreenArgs a;
                           a.vehicle = id;
                           ui.open(ScreenId::Rename, a);
                       }});
        out.push_back({"Minister", v->minister ? "Minister Control: on" : "Minister Control: off", true,
                       [&ui, id, on = !v->minister] {
                           game::cmd::SetMinister c;
                           c.vehicle = id;
                           c.on = on;
                           ui.session.issue(c);
                       }});
        out.back().lit = v->minister;
    } else if (const game::Colony* c = selectedColony(ui); c && c->owner == me) {
        const game::ObjectId planet = c->planet;
        out.push_back({"Queue", "Set Construction Queue (Q)", c->totalPopulation() > 0, [&ui, planet] {
                           ScreenArgs a;
                           a.planet = planet;
                           ui.open(ScreenId::SetQueue, a);
                       }});
        out.push_back({"Cargo", "Cargo Transfer (T)", true, [&ui, planet] {
                           ScreenArgs a;
                           a.planet = planet;
                           ui.open(ScreenId::CargoTransfer, a);
                       }});
        for (const char* key : {"Scrap", "ScrapFacilities"})
            out.push_back({key, "Scrap Facilities", !c->facilities.empty(), [&ui, planet] {
                               ScreenArgs a;
                               a.planet = planet;
                               ui.open(ScreenId::Scrap, a);
                           }});
        out.push_back({"Units", "Launch \\ Recover Units (U)", true, [&ui, planet] {
                           ScreenArgs a;
                           a.planet = planet;
                           ui.open(ScreenId::LaunchRecover, a);
                       }});
        out.push_back({"Name", "Change Name (N)", true, [&ui, planet] {
                           ScreenArgs a;
                           a.planet = planet;
                           ui.open(ScreenId::Rename, a);
                       }});
        out.push_back({"Minister", c->minister ? "Minister Control: on" : "Minister Control: off", true,
                       [&ui, planet, on = !c->minister] {
                           game::cmd::SetMinister m;
                           m.planet = planet;
                           m.on = on;
                           ui.session.issue(m);
                       }});
        out.back().lit = c->minister;
        out.push_back({"Abandon", "Abandon Planet (Ctrl+A)",
                       c->totalPopulation() <= r.setting("Maximum Population For Abandon Planet Order", 50) && !c->homeworld,
                       [&ui, planet] { ui.session.issue(game::cmd::AbandonPlanet{planet}); }});
    }
    return out;
}

// ---- Frame update ------------------------------------------------------------------------

void MainWindow::update(UiContext& ui, bool blocked) {
    layOut(ui.map.left, ui.map.right);
    trackMovement(ui);
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
        const int state = hovered ? (ImGui::IsMouseDown(ImGuiMouseButton_Left) ? 2 : 1) : 0;
        drawAt(ui, dl, ui.art.commandButton(b.icon, state), at, {34, 34});
        if (hovered) ImGui::SetTooltip("%s", b.tooltip);
        if (clicked) {
            if (b.screen) {
                audio().play("cmdbtn");
                ui.open(*b.screen);
            } else {
                ui.requests.endTurn = true;
            }
        }
    }

    // Order strip: every order at its own place, dimmed when it does not apply.
    const auto orders = availableOrders(ui);
    for (size_t col = 0; col < kOrderStrip.size(); ++col)
        for (size_t row = 0; row < 2; ++row) {
            const OrderSlot& slot = kOrderStrip[col][row];
            const Vec2 at{x0 + 246 + float(col) * 34, 36 + float(row) * 34};
            const OrderButton* b = nullptr;
            for (const OrderButton& o : orders)
                if (*slot.key && std::string_view(o.label) == slot.key) b = &o;
            const bool enabled = b && b->enabled;
            ImGui::PushID(int(col * 2 + row) + 100);
            const auto [clicked, hovered] = hit("order", at, {34, 34});
            ImGui::PopID();
            const int state = !enabled ? 3 : b->lit ? 2 : hovered ? 1 : 0;
            drawAt(ui, dl, orderCell(ui.art, slot.band, slot.column, state), at, {34, 34});
            if (hovered && *slot.tooltip) ImGui::SetTooltip("%s", b ? b->tooltip : slot.tooltip);
            if (clicked && enabled && b->action) {
                audio().play("ordbtn");
                b->action();
            }
        }
    // Page arrows at both ends; every order fits at this size, so they stay dim.
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
        if (prevHover || nextHover) ImGui::SetTooltip("%s", row == 0 ? "Previous \\ next ship" : row == 1 ? "Previous \\ next fleet" : "Previous \\ next colony");
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

void MainWindow::reportPanel(UiContext& ui) {
    const game::GameState& s = ui.state();
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
    if (vehicle_) {
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
    } else if (object_) {
        const game::SpaceObject& o = s.galaxy.object(*object_);
        if (o.kind == game::ObjectKind::Planet || o.kind == game::ObjectKind::Asteroids) {
            planetReport(ui, *object_, tab_);
            tabsFor = planetTabs = true;
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
            if (ImGui::Selectable(objectName(s, id, ui.session.player()).c_str(), false, 0, ImVec2(0, ui.px(30)))) {
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
    ImGui::PopClipRect();
    ImGui::EndChild();
    if (tabsFor) {
        ImGui::SetCursorPos(ImVec2(ui.px(-4), ui.px(tabsY)));
        const ReportTab current = planetTabs ? (tab_ == ReportTab::Components ? ReportTab::Facilities : tab_)
                                             : (tab_ == ReportTab::Facilities ? ReportTab::Components : tab_);
        tab_ = reportTabs(ui, current, planetTabs);
    }
    if (several) {
        // Back to the list of everything in the sector.
        ImGui::SetCursorPos(ImVec2(ui.px(250), ui.px(tabsY - 18)));
        ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
        if (ImGui::SmallButton("List")) {
            object_.reset();
            vehicle_.reset();
            fleet_.reset();
            listMode_ = true;
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
    const game::StarSystem& sys = s.galaxy.system(shown_);
    const Vec2 sysMin = geo.systemBackground.min;
    dl->AddText(ui.fonts.bold, ui.fontPx(kTitleSize), ui.at(Vec2{geo.left + 13, 120}), IM_COL32_WHITE, sys.name.c_str());
    if (!ui.me().hasExplored(shown_)) {
        text(sysMin + Vec2{247, 315}, 18, IM_COL32(150, 160, 190, 255), "Unexplored system");
    } else {
        std::map<game::Sector, int> counts;
        for (game::ObjectId id : sys.objects) ++counts[s.galaxy.object(id).sector];
        for (const auto& [sector, n] : counts)
            if (n > 1) text(sectorCenter(sector) + Vec2{10, 8}, 11, IM_COL32_WHITE, std::to_string(n));
        for (game::ObjectId id : settings().showWarpPointNames ? s.galaxy.warpPoints(sys.id) : std::vector<game::ObjectId>{}) {
            // A warp point is named after its destination once we have explored it
            // (docs/spec/01 §5.4, sight::warpPointName).
            const game::SpaceObject& wp = s.galaxy.object(id);
            if (!wp.destination.valid()) continue;
            const game::SystemId to = s.galaxy.object(wp.destination).system;
            if (!s.options.omnipresent && !ui.me().hasExplored(to)) continue;
            const std::string& dest = s.galaxy.system(to).name;
            const ImVec2 size = font->CalcTextSizeA(11 * k, FLT_MAX, 0.0f, dest.c_str());
            const ImVec2 c = ui.at(sectorCenter(wp.sector) + Vec2{0, 18});
            dl->AddText(font, 11 * k, ImVec2{c.x - size.x * 0.5f, c.y}, IM_COL32(184, 200, 255, 255), dest.c_str());
        }
    }
    // Ship counts in sectors with several visible vehicles.
    std::map<game::Sector, int> shipCounts;
    for (const game::Vehicle& v : s.vehicles)
        if (v.location.system == shown_ && knownVehicle(ui, v) && !glides_.find(v.id, ui.time)) ++shipCounts[v.location.sector];
    for (const auto& [sector, n] : shipCounts)
        if (n > 1) text(sectorCenter(sector) + Vec2{10, -22}, 11, IM_COL32(255, 230, 140, 255), std::to_string(n));

    if (pick_ != Pick::None) {
        const std::string prompt = pickPrompt_ + "   (Esc to cancel)";
        text(sysMin + Vec2{11, 640}, 14, IM_COL32(255, 220, 90, 255), prompt);
    }
    // Hovered system name on the galaxy panel.
    if (ImGui::IsMousePosValid()) {
        const ImGuiIO& io = ImGui::GetIO();
        const Vec2 p = ui.map.fromFb(Vec2{io.MousePos.x, io.MousePos.y} * ui.fbScale);
        if (geo.galaxyPanel.contains(p)) {
            for (const game::StarSystem& sy : s.galaxy.systems)
                if (distance(galaxyPos(s.galaxy, sy), p) < 6.0f && ui.me().hasExplored(sy.id)) text(Vec2{geo.galaxyPanel.min.x + 8, geo.galaxyPanel.max.y - 22}, 12, IM_COL32(220, 230, 255, 255), sy.name);
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

    if (geo.galaxyPanel.contains(p)) {
        if (right) {
            ScreenArgs args;
            args.location = game::Location{shown_, {}};
            ui.open(ScreenId::GalaxyMap, args);
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
        // Right click on a sector with an own mobile vehicle selected: Move To (an OpenSE4 shortcut, optional).
        if (right && appSettings().controls.rightClickMoves) {
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
    const Bindings& keys = appSettings().controls.bindings;
    auto pressed = [&](Action a) { return keys.pressed(a); };

    // Windows (F1..F12 by default).
    const std::array<std::pair<Action, ScreenId>, 12> windows{{{Action::Help, ScreenId::Help},
                                                                {Action::GameMenu, ScreenId::GameMenu},
                                                                {Action::Designs, ScreenId::Designs},
                                                                {Action::Planets, ScreenId::Planets},
                                                                {Action::Colonies, ScreenId::Colonies},
                                                                {Action::Ships, ScreenId::Ships},
                                                                {Action::Queues, ScreenId::Queues},
                                                                {Action::Research, ScreenId::Research},
                                                                {Action::Empires, ScreenId::Empires},
                                                                {Action::Log, ScreenId::Log},
                                                                {Action::EmpireStatus, ScreenId::EmpireStatus},
                                                                {Action::Settings, ScreenId::Settings}}};
    for (const auto& [action, screen] : windows)
        if (pressed(action)) ui.open(screen);

    if (pressed(Action::Cancel)) {
        if (pick_ != Pick::None) pick_ = Pick::None;
        else clearSelection();
    }
    if (pressed(Action::EndTurn)) {
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
    if (pressed(Action::NextIdleShip)) cycleVehicle(ui, 1, true);
    if (pressed(Action::NextShip)) cycleVehicle(ui, 1, false);
    if (pressed(Action::PreviousShip)) cycleVehicle(ui, -1, false);
    if (pressed(Action::NextFleet)) cycleFleet(ui, 1);
    if (pressed(Action::PreviousFleet)) cycleFleet(ui, -1);
    if (pressed(Action::NextColony)) cycleColony(ui, 1);
    if (pressed(Action::PreviousColony)) cycleColony(ui, -1);
    if (pressed(Action::MovementLines)) showMovementLines_ = !showMovementLines_;

    // Waypoints: Alt+0..9 sets at the selected sector; Ctrl+0..9 moves there.
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
        } else if (ctrl) {
            game::Order o{game::OrderKind::MoveToWaypoint};
            o.amount = i;
            giveOrder(ui, o);
        }
    }

    // Orders: find the matching button so the same enable rules apply.
    const std::array<std::pair<Action, const char*>, 22> orderKeys{{{Action::MoveTo, "Move"},
                                                                     {Action::Warp, "Warp"},
                                                                     {Action::Attack, "Attack"},
                                                                     {Action::Colonize, "Colonize"},
                                                                     {Action::Resupply, "Supply"},
                                                                     {Action::Repair, "Repair"},
                                                                     {Action::ClearOrders, "Clear"},
                                                                     {Action::FleetTransfer, "Fleet"},
                                                                     {Action::BuildQueue, "Queue"},
                                                                     {Action::CargoTransfer, "Cargo"},
                                                                     {Action::LaunchRecover, "Units"},
                                                                     {Action::LoadCargo, "Load"},
                                                                     {Action::DropCargo, "Drop"},
                                                                     {Action::Sentry, "Sentry"},
                                                                     {Action::Explore, "Explore"},
                                                                     {Action::Patrol, "Patrol"},
                                                                     {Action::RepeatOrders, "Repeat"},
                                                                     {Action::StellarManipulation, "Stellar"},
                                                                     {Action::ViewOrders, "Orders"},
                                                                     {Action::Scrap, "Scrap"},
                                                                     {Action::Rename, "Name"},
                                                                     {Action::Cloak, "Cloak"}}};
    std::optional<std::vector<OrderButton>> buttons;
    auto run = [&](const char* label) {
        if (!buttons) buttons = availableOrders(ui);
        for (const OrderButton& b : *buttons)
            if (std::string_view(b.label) == label && b.enabled && b.action) {
                b.action();
                return;
            }
    };
    for (const auto& [action, label] : orderKeys)
        if (pressed(action)) run(label);
    if (pressed(Action::Decloak)) run("Decloak");
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
    std::vector<ShipGlides::Seen> visible;
    for (const game::Vehicle& v : ui.state().vehicles)
        if (knownVehicle(ui, v)) visible.push_back({v.id, v.location});
    glides_.track(ui.time, shown_, settings().animateShipMovement, visible);
}

void MainWindow::drawSystem(gfx::Renderer2D& r, UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::Rules& rules = ui.rules();
    if (!shown_.valid()) return;
    const game::StarSystem& sys = s.galaxy.system(shown_);
    const bool explored = ui.me().hasExplored(shown_);
    if (Sprite bg = ui.art.systemBackground(rules.data().systemTypes[sys.type.index()].backgroundBitmap))
        r.sprite(bg.tex, geo.systemBackground, bg.uv);
    else r.rect(geo.systemBackground, Color::hex(0x000000));

    auto spriteAt = [&](const Sprite& sp, Vec2 c, float size, Color tint = {}) {
        const Rect dst = Rect::fromCenter(c, {size * 0.5f, size * 0.5f});
        if (sp) r.sprite(sp.tex, dst, sp.uv, tint);
        return static_cast<bool>(sp);
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
            if (!spriteAt(objectSprite(ui, o), c, kSpriteSize))
                r.disc(c, 12.0f, o.kind == game::ObjectKind::Star ? Color::hex(0xffe080) : Color::hex(0x8090a0));
            // Colonies carry the owner's small flag at the top right; planets we could
            // colonise get the classic star (green: breathable, red: needs domes).
            for (game::ObjectId id : ids) {
                if (const game::Colony* col = s.colony(id)) {
                    const Sprite flag = ui.art.flag(s.empire(col->owner).race.style, false);
                    if (!spriteAt(flag, c + Vec2{11, -12}, 14))
                        r.rect(Rect::fromPosSize(c + Vec2{4, -17}, {14, 10}), colorOf(s.empire(col->owner).color));
                } else if (s.galaxy.object(id).kind == game::ObjectKind::Planet &&
                           colonizeProblem(rules, s, ui.session.player(), id, tech).empty()) {
                    const bool breathe = breathableBy(s, ui.session.player(), s.galaxy.object(id));
                    const Sprite star = ui.art.region("Pictures/Game/General.bmp", breathe ? 237 : 261, 16, 7, 7);
                    if (star) r.sprite(star.tex, Rect::fromPosSize(c + Vec2{11, -17}, {7, 7}), star.uv);
                }
            }
        }
    }

    // Vehicles: one sprite per sector (the largest hull), flags when several empires share it.
    // A ship still gliding to its square is drawn on the way instead (trackMovement).
    auto largestFirst = [&](std::vector<const game::Vehicle*>& list) {
        std::sort(list.begin(), list.end(), [&](const game::Vehicle* a, const game::Vehicle* b) {
            return rules.hull(s.design(a->design).hull).tonnage > rules.hull(s.design(b->design).hull).tonnage;
        });
    };
    // Where a ship sprite sits in a square, and its size: smaller and off-centre when
    // the square also holds a planet or another object.
    auto anchor = [&](Vec2 cell) {
        const game::Sector sector{static_cast<int>(cell.x), static_cast<int>(cell.y)};
        const bool hasObject = explored && sector.valid() && !objectsAt(ui, sector).empty();
        return std::pair{hasObject ? cell + Vec2{-12, 12} / kSectorSize : cell, hasObject ? 24.0f : kSpriteSize};
    };
    const double now = ui.time;
    std::map<game::Sector, std::vector<const game::Vehicle*>> ships;
    std::map<std::tuple<float, float, float, float, double>, std::vector<const game::Vehicle*>> gliding;
    for (const game::Vehicle& v : s.vehicles) {
        if (v.location.system != shown_ || !knownVehicle(ui, v)) continue;
        if (const ShipGlides::Glide* g = glides_.find(v.id, now))
            gliding[{g->from.x, g->from.y, g->to.x, g->to.y, g->start}].push_back(&v);  // a fleet glides as one
        else
            ships[v.location.sector].push_back(&v);
    }
    for (auto& [key, list] : gliding) {
        const ShipGlides::Glide& g = *glides_.find(list.front()->id, now);
        // Offset and size follow the glide from the old square's look to the new one's.
        const Vec2 p = ShipGlides::position(g, now);
        const float e = distance(g.from, g.to) > 0.0f ? distance(g.from, p) / distance(g.from, g.to) : 1.0f;
        const auto [fromAt, fromSize] = anchor(g.from);
        const auto [toAt, toSize] = anchor(g.to);
        const Vec2 at = gridPoint(p + lerp(fromAt - g.from, toAt - g.to, e));
        const float size = fromSize + (toSize - fromSize) * e;
        largestFirst(list);
        if (!spriteAt(vehicleMini(ui, *list.front()), at, size))
            r.triangle(at + Vec2{0, -9}, at + Vec2{-7, 7}, at + Vec2{7, 7}, colorOf(s.empire(list.front()->owner).color));
    }
    for (auto& [sector, list] : ships) {
        const Vec2 c = sectorCenter(sector);
        const bool hasObject = explored && !objectsAt(ui, sector).empty();
        const Vec2 at = hasObject ? c + Vec2{-12, 12} : c;
        const float size = hasObject ? 24.0f : kSpriteSize;
        largestFirst(list);
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

    // Selection brackets and pick hover.
    auto brackets = [&](game::Sector sec, Color col) {
        // Small corner marks around the sprite, as the original draws the selection.
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
    if (settings().showWaypointMarkers)
        for (const auto& w : ui.me().waypoints)
            if (w.set && w.location.system == shown_) r.ring(sectorCenter(w.location.sector), 5.0f, 1.5f, Color::hex(0x40d0ff));
}

void MainWindow::drawGalaxy(gfx::Renderer2D& r, UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::Galaxy& g = s.galaxy;
    const game::Empire& me = ui.me();
    r.rect(geo.galaxyPanel, Color::hex(0x000000));
    // A fine grid, one line per galaxy unit.
    const GalaxyTransform t = galaxyTransform(g);
    const Vec2 extent{t.sx * float(g.width), t.sy * float(g.height)};
    const Color grid = Color::hex(palette::kGrid);
    for (int x = 0; x <= g.width; ++x)
        r.line({t.origin.x + float(x) * t.sx, t.origin.y}, {t.origin.x + float(x) * t.sx, t.origin.y + extent.y}, 1.0f, grid);
    for (int y = 0; y <= g.height; ++y)
        r.line({t.origin.x, t.origin.y + float(y) * t.sy}, {t.origin.x + extent.x, t.origin.y + float(y) * t.sy}, 1.0f, grid);

    // Only warp links the empire knows are drawn.
    const auto& known = me.knowledge.knownWarpLink;
    for (const game::SpaceObject& o : g.objects) {
        if (o.kind != game::ObjectKind::WarpPoint || !o.destination.valid() || o.destination < o.id) continue;
        if (o.id.index() >= known.size() || !known[o.id.index()]) continue;
        r.line(galaxyPos(g, g.system(o.system)), galaxyPos(g, g.system(g.object(o.destination).system)), 1.0f, Color::hex(0x4868a8, 0.9f));
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
        if (who.size() > 1) {
            r.triangle(p + Vec2{0, -4.5f}, p + Vec2{-4, 3.5f}, p + Vec2{4, 3.5f}, Color::hex(0xffffff));
        } else if (who.size() == 1) {
            r.disc(p, 3.5f, colorOf(s.empire(who.front()).color));
            r.ring(p, 3.5f, 1.0f, Color::hex(0xffffff, 0.7f));
        } else {
            r.ring(p, 3.0f, 1.0f, Color::hex(0xa0a0a0));
        }
        if (sys.id == shown_) r.ring(p, 6.0f, 1.0f, kSelectYellow);
    }
}

} // namespace opense4::client::classic
