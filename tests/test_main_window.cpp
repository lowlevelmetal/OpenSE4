// Logic behind the classic main window: when order buttons are lit
// (client/classic/order_rules.hpp), status icons (status_icons.hpp), the map
// colours and symbols and the headings of minis (map_style.hpp).

#include "engine_fixture.hpp"

#include "assets/assets.hpp"
#include "client/classic/map_style.hpp"
#include "client/classic/order_rules.hpp"
#include "client/classic/session.hpp"
#include "client/classic/ship_glides.hpp"
#include "client/classic/status_icons.hpp"
#include "client/input.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "temp_dir.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>

using namespace opense4;
using namespace opense4::game;
using namespace opense4::test;
using namespace opense4::client::classic;

namespace {

constexpr EmpireId kMe{0u};

bool lit(const LitOrders& l, OrderId o) { return l[static_cast<size_t>(o)]; }

std::vector<OrderId> litList(const LitOrders& l) {
    std::vector<OrderId> out;
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i]) out.push_back(static_cast<OrderId>(i));
    return out;
}

bool has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

DesignId design(GameState& s, const Rules& r, std::string_view name, std::string_view hull, std::initializer_list<std::string_view> parts) {
    return addTestDesign(s, r, kMe, name, hull, parts);
}

const std::initializer_list<std::string_view> kShipBasics{"Test Bridge", "Test Life Support", "Test Crew Quarters"};

} // namespace

// ---- Order buttons (docs/spec/06 §2.8) ----

TEST_CASE("main window: nothing lit while locked, for nothing, and for others' objects") {
    OrderFacts f;
    f.target = OrderTarget::Ship;
    f.locked = true;
    f.turnBased = false;
    CHECK(litList(litOrders(f)).empty());

    f = {};
    CHECK(litList(litOrders(f)).empty());
    f.target = OrderTarget::Other;
    f.mobile = f.hasOrders = true;
    CHECK(litList(litOrders(f)).empty());
}

TEST_CASE("main window: the movement log buttons follow the turn style only") {
    OrderFacts f;
    f.turnBased = false;
    for (OrderTarget t : {OrderTarget::Nothing, OrderTarget::Other, OrderTarget::Ship, OrderTarget::Colony}) {
        f.target = t;
        const LitOrders l = litOrders(f);
        for (OrderId o : {OrderId::ReplayPlay, OrderId::ReplayShip, OrderId::ReplayStep, OrderId::ReplayRewind}) CHECK(lit(l, o));
    }
    f.turnBased = true;
    CHECK_FALSE(lit(litOrders(f), OrderId::ReplayPlay));
}

TEST_CASE("main window: a ship's buttons") {
    OrderFacts f;
    f.target = OrderTarget::Ship;
    LitOrders l = litOrders(f);
    // Always, for any own ship.
    for (OrderId o : {OrderId::Scrap, OrderId::ChangeName, OrderId::FleetTransfer, OrderId::DropCargo, OrderId::CargoTransfer,
                      OrderId::Minister, OrderId::LaunchRecover, OrderId::Sentry})
        CHECK(lit(l, o));
    // Immobile, no orders, no abilities.
    for (OrderId o : {OrderId::MoveTo, OrderId::Attack, OrderId::Warp, OrderId::ClearOrders, OrderId::ViewOrders, OrderId::RepeatOrders,
                      OrderId::Colonize, OrderId::BuildQueue, OrderId::LoadCargo, OrderId::Jettison, OrderId::StellarManipulation,
                      OrderId::Cloak, OrderId::Decloak, OrderId::UseComponent, OrderId::Strategy, OrderId::MoveToWaypoint})
        CHECK_FALSE(lit(l, o));

    f.mobile = true;
    l = litOrders(f);
    CHECK(lit(l, OrderId::Attack));  // even unarmed
    CHECK(lit(l, OrderId::MoveTo));
    CHECK_FALSE(lit(l, OrderId::Warp));  // the hull must be able to warp
    CHECK_FALSE(lit(l, OrderId::MoveToWaypoint));
    f.canWarp = f.waypointSet = f.hasOrders = true;
    l = litOrders(f);
    CHECK(lit(l, OrderId::Warp));
    CHECK(lit(l, OrderId::MoveToWaypoint));
    CHECK(lit(l, OrderId::ViewOrders));

    f.spaceYard = true;
    CHECK(lit(litOrders(f), OrderId::BuildQueue));
    f.cloaked = true;
    CHECK_FALSE(lit(litOrders(f), OrderId::BuildQueue));

    f.lowSupply = true;
    CHECK_FALSE(lit(litOrders(f), OrderId::Sentry));

    f.turnBased = false;
    l = litOrders(f);
    CHECK_FALSE(lit(l, OrderId::LaunchRecover));
    CHECK(lit(l, OrderId::CargoTransfer));  // both turn styles
}

TEST_CASE("main window: mothballed ships, fleets, colonies and unit groups") {
    OrderFacts f;
    f.target = OrderTarget::MothballedShip;
    f.mobile = true;
    CHECK(litList(litOrders(f)) == std::vector<OrderId>{OrderId::Scrap});
    f.hasOrders = true;
    CHECK(litList(litOrders(f)) == std::vector<OrderId>{OrderId::ClearOrders, OrderId::Scrap, OrderId::ViewOrders});

    f = {};
    f.target = OrderTarget::Fleet;
    LitOrders l = litOrders(f);
    for (OrderId o : {OrderId::Strategy, OrderId::LoadCargo, OrderId::LaunchRemote, OrderId::RecoverRemote, OrderId::Sentry})
        CHECK(lit(l, o));
    CHECK_FALSE(lit(l, OrderId::Warp));
    f.mobile = true;
    CHECK(lit(litOrders(f), OrderId::Warp));

    f = {};
    f.target = OrderTarget::Colony;
    l = litOrders(f);
    for (OrderId o : {OrderId::BuildQueue, OrderId::Scrap, OrderId::AbandonPlanet, OrderId::CargoTransfer, OrderId::ChangeName})
        CHECK(lit(l, o));
    for (OrderId o : {OrderId::ScrapFacilities, OrderId::Jettison, OrderId::UseFacility, OrderId::ConvertResources, OrderId::ViewOrders})
        CHECK_FALSE(lit(l, o));
    f.facilities = f.cargoHolds = f.emergency = f.conversion = true;
    l = litOrders(f);
    for (OrderId o : {OrderId::ScrapFacilities, OrderId::Jettison, OrderId::LaunchRemote, OrderId::UseFacility, OrderId::ConvertResources})
        CHECK(lit(l, o));

    f = {};
    f.target = OrderTarget::Fighters;
    f.mobile = f.canWarp = true;
    l = litOrders(f);
    CHECK(lit(l, OrderId::MoveTo));
    CHECK_FALSE(lit(l, OrderId::Warp));  // never for fighters

    f = {};
    f.target = OrderTarget::Mines;
    CHECK(litList(litOrders(f)) == std::vector<OrderId>{OrderId::Scrap, OrderId::Minister});
    f.target = OrderTarget::Drones;
    CHECK(litList(litOrders(f)) == std::vector<OrderId>{OrderId::Attack, OrderId::Scrap, OrderId::Minister});
}

TEST_CASE("main window: a tagged group") {
    OrderFacts f;
    f.target = OrderTarget::Tagged;
    LitOrders l = litOrders(f);
    for (OrderId o : {OrderId::MoveTo, OrderId::Warp, OrderId::Explore, OrderId::Resupply, OrderId::Repair, OrderId::Patrol,
                      OrderId::Attack, OrderId::Scrap, OrderId::Minister, OrderId::ClearOrders})
        CHECK(lit(l, o));
    CHECK_FALSE(lit(l, OrderId::MoveToWaypoint));
    CHECK_FALSE(lit(l, OrderId::FleetTransfer));
    f.waypointSet = true;
    CHECK(lit(litOrders(f), OrderId::MoveToWaypoint));
    f.droneTagged = true;
    CHECK(litList(litOrders(f)) == std::vector<OrderId>{OrderId::Attack, OrderId::Minister});
}

TEST_CASE("main window: order facts from the game") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const Colony& home = homeworld(s, kMe);
    const Location where = locationOf(s.galaxy, home.planet);
    const DesignId scout = design(s, r, "Runner", "Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine"});
    const DesignId station = design(s, r, "Hulk", "Test Frigate", kShipBasics);  // no engine: speed 0
    const VehicleId a = addTestVehicle(s, r, scout, where).id;
    const VehicleId b = addTestVehicle(s, r, station, where).id;

    OrderSelection sel;
    sel.vehicle = a;
    OrderFacts f = orderFacts(r, s, kMe, sel, true, false);
    CHECK(f.target == OrderTarget::Ship);
    CHECK(f.mobile);
    CHECK(f.canWarp);
    CHECK_FALSE(f.hasOrders);
    CHECK_FALSE(f.canColonize);
    LitOrders l = litOrders(f);
    CHECK(lit(l, OrderId::Warp));
    CHECK_FALSE(lit(l, OrderId::MoveToWaypoint));

    sel.vehicle = b;
    f = orderFacts(r, s, kMe, sel, true, false);
    CHECK_FALSE(f.mobile);
    CHECK_FALSE(lit(litOrders(f), OrderId::MoveTo));

    // Another empire's ship: everything dim.
    s.vehicle(b)->owner = EmpireId{1u};
    f = orderFacts(r, s, kMe, sel, true, false);
    CHECK(f.target == OrderTarget::Other);
    s.vehicle(b)->owner = kMe;

    // A fleet: its speed is the slowest member's.
    REQUIRE(apply(r, s, kMe, cmd::CreateFleet{"Alpha", {a, b}}).ok);
    sel.vehicle = a;
    sel.fleet = s.vehicle(a)->fleet;
    f = orderFacts(r, s, kMe, sel, true, false);
    CHECK(f.target == OrderTarget::Fleet);
    CHECK_FALSE(f.mobile);
    CHECK(lit(litOrders(f), OrderId::Strategy));

    // A tagged group: waypoints count.
    Waypoint w;
    w.set = true;
    w.location = where;
    REQUIRE(apply(r, s, kMe, cmd::SetWaypoint{2, w}).ok);
    const std::vector<VehicleId> tagged{a, b};
    OrderSelection group;
    group.tagged = tagged;
    f = orderFacts(r, s, kMe, group, true, false);
    CHECK(f.target == OrderTarget::Tagged);
    CHECK(f.waypointSet);
    CHECK_FALSE(f.cloak);   // nobody can cloak
    CHECK_FALSE(f.decloak);

    // The homeworld: a colony with facilities.
    OrderSelection colony;
    colony.planet = home.planet;
    f = orderFacts(r, s, kMe, colony, false, false);
    CHECK(f.target == OrderTarget::Colony);
    CHECK(f.facilities);
    l = litOrders(f);
    CHECK(lit(l, OrderId::ScrapFacilities));
    CHECK(lit(l, OrderId::AbandonPlanet));  // the homeworld is not special
    CHECK_FALSE(lit(l, OrderId::LaunchRecover));  // simultaneous
}

TEST_CASE("main window: order names and the lessons' slot keys") {
    CHECK(orderName(OrderId::MoveTo) == "Move To");
    CHECK(orderSlotKey(OrderId::MoveTo) == "Move");
    CHECK(orderSlotKey(OrderId::ReplayRewind) == "ReplayRewind");
    for (size_t i = 0; i < kOrderCount; ++i) {
        CHECK_FALSE(orderName(static_cast<OrderId>(i)).empty());
        CHECK_FALSE(orderSlotKey(static_cast<OrderId>(i)).empty());
    }
}

// ---- Status icons (docs/spec/06 §4.4) ----

TEST_CASE("main window: status icons of ships, colonies and fleets") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    Colony& home = homeworld(s, kMe);
    const Location where = locationOf(s.galaxy, home.planet);
    const DesignId d = design(s, r, "Hauler", "Test Frigate", {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine",
                                                               "Test Supply Pod", "Test Supply Pod", "Test Supply Pod",
                                                               "Test Cargo Bay"});
    Vehicle& v = addTestVehicle(s, r, d, where);
    namespace cell = status_cell;
    std::vector<int> icons = vehicleStatusCells(r, s, v);
    CHECK(icons.empty());

    v.supply = 0;
    v.orders.push_back(Order{OrderKind::Sentry});
    v.repeatOrders = true;
    v.minister = true;
    v.cargo.population.push_back({kMe, 5});
    icons = vehicleStatusCells(r, s, v);
    CHECK(icons == std::vector<int>{cell::kNoSupply, cell::kSentry, cell::kRepeatOrders, cell::kMinister, cell::kPopulation});
    v.supply = 1;
    CHECK(vehicleStatusCells(r, s, v).front() == cell::kLowSupply);

    // Damage, and the homeworld's yard does not repair (no Component Repair here).
    v.damage.assign(s.design(d).entries.size(), 0);
    v.damage[0] = 1;
    icons = vehicleStatusCells(r, s, v);
    CHECK(has(icons, cell::kDamaged));
    CHECK_FALSE(has(icons, cell::kRepairHere));
    home.facilities.push_back(facilityIndex(r, "Test Repair Yard"));
    CHECK(has(vehicleStatusCells(r, s, v), cell::kRepairHere));

    // Mothballed: no supply icon, the mothball icon.
    v.status = VehicleStatus::Mothballed;
    v.supply = 0;
    icons = vehicleStatusCells(r, s, v);
    CHECK_FALSE(has(icons, cell::kNoSupply));
    CHECK(has(icons, cell::kMothballed));

    // The colony: yard first; repair only without a yard; building with a queue.
    icons = colonyStatusCells(r, s, home, true);
    REQUIRE_FALSE(icons.empty());
    CHECK(icons.front() == cell::kSpaceYard);
    CHECK_FALSE(has(icons, cell::kCanRepair));
    CHECK_FALSE(has(icons, cell::kNotConnected));
    QueueItem item;
    item.kind = QueueItem::Kind::Facility;
    item.facility = facilityIndex(r, "Test Mine");
    home.queue.items.push_back(item);
    home.queue.onHold = true;  // still building (inferred: the queue is not empty)
    home.minister = true;
    icons = colonyStatusCells(r, s, home, false);
    CHECK(icons[0] == cell::kSpaceYard);
    CHECK(icons[1] == cell::kMinister);
    CHECK(icons[2] == cell::kBuilding);
    CHECK(icons.back() == cell::kNotConnected);

    Fleet f;
    f.minister = true;
    CHECK(fleetStatusCells(s, f) == std::vector<int>{cell::kMinister});
}

// ---- Map colours and symbols (docs/spec/06 §2.6) ----

TEST_CASE("main window: galaxy symbols") {
    using namespace map_style;
    const EmpireId me{1u}, a{0u}, b{3u};
    CHECK(presenceSymbol(false, {}, me).rgb == kUnexplored);
    CHECK(presenceSymbol(true, {}, me).rgb == kExplored);
    const EmpireId one[] = {b};
    Symbol sym = presenceSymbol(true, one, me);
    CHECK(sym.shape == Shape::Ring);
    CHECK(sym.empire == b);
    const EmpireId withMe[] = {a, me, b};
    sym = presenceSymbol(true, withMe, me);
    CHECK(sym.shape == Shape::Triangle);
    CHECK(sym.empire == me);
    const EmpireId others[] = {b, a};
    CHECK(presenceSymbol(true, others, me).empire == b);  // the highest-numbered

    CHECK(avoidSymbol(true, true, me).empire == me);
    CHECK_FALSE(avoidSymbol(false, true, me).empire.has_value());
    const EmpireId two[] = {a, b};
    sym = claimedSymbol(true, two);
    CHECK(sym.shape == Shape::Disc);
    CHECK(sym.rgb == kYellow);
    CHECK(sym.outerRing);
    CHECK(facilitySymbol(true, true, true).rgb == kGreen);
    CHECK(facilitySymbol(true, true, false).rgb == kYellow);
    CHECK(facilitySymbol(false, false, true).rgb == kUnexplored);

    // Always 68 × 47 cells.
    CHECK(gridCell(342, 284).w == 5);
    CHECK(gridCell(342, 284).h == 6);
    CHECK(gridCell(544, 376).w == 8);
    CHECK(gridCell(544, 376).h == 8);
}

TEST_CASE("main window: headings of minis and the hover name's corner") {
    using map_style::headingStep;
    const Sector c{6, 6};
    CHECK(headingStep(c, {6, 5}) == 0);
    CHECK(headingStep(c, {7, 5}) == 1);
    CHECK(headingStep(c, {7, 6}) == 2);
    CHECK(headingStep(c, {7, 7}) == 3);
    CHECK(headingStep(c, {6, 7}) == 4);
    CHECK(headingStep(c, {5, 7}) == 5);
    CHECK(headingStep(c, {5, 6}) == 6);
    CHECK(headingStep(c, {5, 5}) == 7);
    // atan(2/5) is about 21.8°: still up; atan(3/7) is about 23.2°: 45°.
    CHECK(headingStep({0, 5}, {2, 0}) == 0);
    CHECK(headingStep({0, 7}, {3, 0}) == 1);

    using map_style::nameCorner;
    auto p = nameCorner(100, 100, 5, 6, 40, 10, 300, 200);
    CHECK(p.x == 105);
    CHECK(p.y == 90);  // above-right
    p = nameCorner(100, 2, 5, 6, 40, 10, 300, 200);
    CHECK(p.y == 8);   // below-right
    p = nameCorner(290, 100, 5, 6, 40, 10, 300, 200);
    CHECK(p.x == 250);  // above-left
}

// ---- Keys (docs/spec/06 §3) ----

TEST_CASE("main window: every order key belongs to one order, and the keys of spec 06 are bound") {
    const opense4::client::Bindings b;
    using opense4::client::Action;
    using opense4::client::KeyChord;
    // The action table is in Action order (actionInfo indexes it).
    for (size_t i = 0; i < opense4::client::kActionCount; ++i)
        CHECK(opense4::client::actionInfos()[i].action == static_cast<Action>(i));
    // Each order has its own key, and each Orders key runs an order (or tags a minefield).
    std::vector<Action> seen;
    for (size_t i = 0; i < kOrderCount; ++i)
        if (const auto a = orderAction(static_cast<OrderId>(i))) {
            CHECK(std::find(seen.begin(), seen.end(), *a) == seen.end());
            seen.push_back(*a);
        }
    for (const opense4::client::ActionInfo& a : opense4::client::actionInfos())
        if (std::string_view(a.group) == "Orders" || std::string_view(a.group) == "Movement log")
            CHECK((std::find(seen.begin(), seen.end(), a.action) != seen.end() || a.action == Action::TagMinefield ||
                   a.action == Action::UntagMinefield));
    // The keys §3.1 confirms.
    auto key = [&](OrderId o) { return b.chords(*orderAction(o))[0]; };
    CHECK(key(OrderId::Strategy) == KeyChord{ImGuiKey_H});
    CHECK(key(OrderId::Jettison) == KeyChord{ImGuiKey_J});
    CHECK(key(OrderId::LaunchRemote) == KeyChord{ImGuiKey_I});
    CHECK(key(OrderId::RecoverRemote) == KeyChord{ImGuiKey_O});
    CHECK(key(OrderId::SweepMines) == KeyChord{ImGuiKey_M, true});
    CHECK(key(OrderId::AbandonPlanet) == KeyChord{ImGuiKey_A, true});
    CHECK(key(OrderId::ConvertResources) == KeyChord{ImGuiKey_V, true});
    CHECK(key(OrderId::MoveToWaypoint) == KeyChord{ImGuiKey_W, true});
    CHECK(key(OrderId::Minister) == KeyChord{ImGuiKey_Y, true});
    CHECK(key(OrderId::UseComponent) == KeyChord{ImGuiKey_Z, true});
    CHECK(key(OrderId::UseFacility) == KeyChord{ImGuiKey_J, true});
    CHECK(key(OrderId::ScrapFacilities) == KeyChord{ImGuiKey_K, true});
    CHECK(key(OrderId::ReplayPlay) == KeyChord{ImGuiKey_P, true});
    CHECK(b.chords(Action::TagAll)[0] == KeyChord{ImGuiKey_A, false, true});
    CHECK(b.chords(Action::ToggleSound)[0] == KeyChord{ImGuiKey_S, true});
    // Ctrl+H and Shift+F1 stay with the lesson panel and the manual.
    CHECK(b.chords(Action::LessonText)[0] == KeyChord{ImGuiKey_H, true});
    CHECK(b.chords(Action::ContextHelp)[0] == KeyChord{ImGuiKey_F1, false, true});
}

// ---- Minis turned to their heading (docs/spec/06 §2.4) ----

TEST_CASE("main window: minis turn nearest-neighbour, and the client follows headings") {
    assets::Image img;
    img.width = img.height = 4;
    img.rgba.assign(4 * 4 * 4, 0);
    auto px = [](const assets::Image& i, int x, int y) { return &i.rgba[size_t((y * i.width + x) * 4)]; };
    img.rgba[size_t(1 * 4)] = 255;  // a red pixel at the top
    img.rgba[size_t(1 * 4 + 3)] = 255;
    const assets::Image right = assets::rotateNearest(img, 90.0, true);
    CHECK(px(right, 3, 1)[0] == 255);  // now at the right
    const assets::Image diagonal = assets::rotateNearest(img, 45.0, false);
    // The corners come from outside the picture: black, opaque when not keyed.
    CHECK(px(diagonal, 0, 0)[3] == 255);
    CHECK(px(diagonal, 0, 0)[0] == 0);

    ShipGlides g;
    const VehicleId ship{3u};
    const SystemId sys{0u}, other{1u};
    auto see = [&](Location at, double t) {
        const ShipGlides::Seen seen[] = {{ship, at}};
        g.track(t, sys, false, seen);
    };
    see({sys, {5, 5}}, 0.0);
    CHECK(g.heading(ship) == 0);  // a new ship faces up
    see({sys, {6, 6}}, 1.0);
    CHECK(g.heading(ship) == 3);  // down-right
    see({other, {0, 6}}, 2.0);
    CHECK(g.heading(ship) == 3);  // a warp keeps it
    see({other, {0, 5}}, 3.0);
    CHECK(g.heading(ship) == 0);
}

TEST_CASE("main window: the movement log replay") {
    MovementReplay r;
    CHECK_FALSE(r.available());
    const VehicleId ship{1u};
    const SystemId sys{2u};
    r.newTurn({{ship, Location{sys, {0, 0}}}});
    CHECK(r.available());
    CHECK_FALSE(r.position(ship, {sys, {10, 0}}, sys, 0.0).has_value());  // not replaying
    r.rewind();
    REQUIRE(r.position(ship, {sys, {10, 0}}, sys, 0.0).has_value());
    CHECK(r.position(ship, {sys, {10, 0}}, sys, 0.0)->x == doctest::Approx(0.5));
    r.step();
    CHECK(r.position(ship, {sys, {10, 0}}, sys, 0.0)->x == doctest::Approx(1.5));  // one day of ten
    r.play(100.0);
    r.update(100.0 + MovementReplay::kDays * MovementReplay::kSecondsPerDay);
    CHECK_FALSE(r.active());
}

// ---- Files the game writes (docs/spec/06 §6.1) ----

TEST_CASE("main window: History files travel with saves") {
    namespace fs = std::filesystem;
    const opense4::test::TempDir tmp("history");
    const fs::path history = tmp.path() / "History";
    const fs::path saves = tmp.path() / "saves";
    fs::create_directories(history);
    fs::create_directories(saves);
    CHECK(historyFileName(EmpireId{0u}, "stats.txt") == "plr_1_stats.txt");
    auto write = [](const fs::path& p, std::string_view text) {
        std::ofstream out(p);
        out << text;
    };
    write(history / "plr_1_stats.txt", "one");
    write(history / "plr_2_events.txt", "two");
    const fs::path save = saves / "Foo.gam";
    write(save, "game");
    copyHistoryNextTo(save, history);
    CHECK(fs::exists(saves / "Foo_plr_1_stats.txt"));
    CHECK(fs::exists(saves / "Foo_plr_2_events.txt"));

    // Loading empties History/ and refills it from the save's companions.
    write(history / "plr_3_log.txt", "stale");
    restoreHistoryFrom(save, history);
    CHECK(fs::exists(history / "plr_1_stats.txt"));
    CHECK(fs::exists(history / "plr_2_events.txt"));
    CHECK_FALSE(fs::exists(history / "plr_3_log.txt"));
}

// ---- Ship names (docs/spec/06 §6, §7 Q18) ----

TEST_CASE("main window: new ships take the next four-digit serial of their design") {
    const Rules& r = engineRules();
    GameState s = newEngineGame();
    const Location where = locationOf(s.galaxy, homeworld(s, kMe).planet);
    const DesignId d = design(s, r, "Hood", "Test Frigate", kShipBasics);
    CHECK(nextVehicleName(s, s.design(d)) == "Hood 0001");
    Vehicle& a = addTestVehicle(s, r, d, where);
    a.name = "Hood 0001";
    Vehicle& b = addTestVehicle(s, r, d, where);
    b.name = "Hood 0007";
    CHECK(nextVehicleName(s, s.design(d)) == "Hood 0008");
    b.name = "Renamed";  // the highest serial is gone: its number is used again
    CHECK(nextVehicleName(s, s.design(d)) == "Hood 0002");
    // The starting ships of a new game: two scouts of one design are 0001 and 0002.
    for (const Vehicle& v : s.vehicles)
        if (v.owner == kMe && v.design != d) CHECK((v.name.ends_with(" 0001") || v.name.ends_with(" 0002")));
}
