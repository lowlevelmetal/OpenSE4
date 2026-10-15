#include "client/classic/order_rules.hpp"

#include "game/abilities.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/sight.hpp"

#include <algorithm>

namespace opense4::client::classic {

namespace {

struct OrderText {
    std::string_view name;
    std::string_view slotKey;
};

// In OrderId order.
constexpr std::array<OrderText, kOrderCount> kOrderText{{
    {"Move To", "Move"},
    {"Warp", "Warp"},
    {"Move To Waypoint", "Wpt"},
    {"Colonize", "Colonize"},
    {"Attack", "Attack"},
    {"Fleet Transfer", "Fleet"},
    {"Resupply At Nearest", "Supply"},
    {"Repair At Nearest", "Repair"},
    {"Clear Orders", "Clear"},
    {"Build Queue", "Queue"},
    {"Cargo Transfer", "Cargo"},
    {"Launch \\ Recover Units", "Units"},
    {"Load Cargo", "Load"},
    {"Drop Cargo", "Drop"},
    {"Launch Units Remotely", "LaunchRemote"},
    {"Recover Units Remotely", "RecoverRemote"},
    {"Sentry", "Sentry"},
    {"Explore", "Explore"},
    {"Set Patrol", "Patrol"},
    {"Repeat Orders", "Repeat"},
    {"Stellar Manipulation", "Stellar"},
    {"Change Name", "Name"},
    {"Scrap \\ Analyze \\ Mothball", "Scrap"},
    {"Change Formation \\ Strategy", "Strategy"},
    {"View Orders", "Orders"},
    {"Sweep Mines", "Sweep"},
    {"Scrap Facilities", "ScrapFacilities"},
    {"Jettison Cargo", "Jettison"},
    {"Cloak", "Cloak"},
    {"Decloak", "Decloak"},
    {"Use Component", "UseComponent"},
    {"Use Facility", "UseFacility"},
    {"Abandon Planet", "Abandon"},
    {"Convert Resources", "Convert"},
    {"Toggle Minister Control", "Minister"},
    {"Play Movement Log", "ReplayPlay"},
    {"Play Movement Log For All Ships", "ReplayShip"},
    {"Step Movement Log", "ReplayStep"},
    {"Rewind Movement Log", "ReplayRewind"},
}};

using game::AbilityKind;

constexpr std::array<AbilityKind, 13> kStellarAbilities{
    AbilityKind::OpenWarpPointDistance, AbilityKind::CloseWarpPoint, AbilityKind::CreatePlanetSize, AbilityKind::DestroyPlanetSize,
    AbilityKind::CreateStar,           AbilityKind::DestroyStar,    AbilityKind::CreateStorm,      AbilityKind::DestroyStorm,
    AbilityKind::CreateNebulae,        AbilityKind::DestroyNebulae, AbilityKind::CreateBlackHole,  AbilityKind::DestroyBlackHole,
    AbilityKind::CreateConstructedPlanet};

bool anyOf(std::span<const game::ParsedAbility> list, std::initializer_list<AbilityKind> kinds) {
    for (AbilityKind k : kinds)
        if (game::hasAbility(list, k)) return true;
    return false;
}

bool stellarAbility(std::span<const game::ParsedAbility> list) {
    return std::any_of(kStellarAbilities.begin(), kStellarAbilities.end(), [&](AbilityKind k) { return game::hasAbility(list, k); });
}

bool anyWaypoint(const game::Empire& e) {
    return std::any_of(e.waypoints.begin(), e.waypoints.end(), [](const game::Waypoint& w) { return w.set; });
}

// Supplies below the warning level; fighter and drone groups warn at a tenth
// of it while they hold a unit, as their Sentry does (spec 06 §4.4, §7 Q54,
// Q61; for a drone group's button not traced, inferred).
bool lowOnSupply(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    if (game::vehicleHasUnlimitedSupply(r, s, v) || !game::vehicleUsesSupply(r, s, v)) return false;
    const int64_t warning = r.setting("Supply Amount for Low Supply Warning", 1000);
    const ruleset::VehicleType t = game::vehicleType(r, s, v);
    if (t == ruleset::VehicleType::Fighter || t == ruleset::VehicleType::Drone) return v.count >= 1 && v.supply < warning / 10;
    return v.supply < warning;
}

bool mobileVehicle(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) { return game::vehicleMaxMovement(r, s, v) > 0; }

// The facts of one vehicle (a ship, a base or a unit group).
void vehicleFacts(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, OrderFacts& f) {
    const auto abilities = game::vehicleAbilities(r, s, v);
    f.mobile = mobileVehicle(r, s, v);
    f.canWarp = game::vehicleType(r, s, v) != ruleset::VehicleType::Fighter;
    f.hasOrders = !v.orders.empty();
    f.canColonize = anyOf(abilities, {AbilityKind::ColonizeRock, AbilityKind::ColonizeIce, AbilityKind::ColonizeGas});
    f.spaceYard = game::vehicleHasSpaceYard(r, s, v);
    f.cloaked = v.status == game::VehicleStatus::Cloaked;
    // Cloak when it can cloak and is not cloaked; Decloak whenever it is
    // cloaked, even when it could no longer cloak (spec 06 §2.8, confirmed:
    // binary), as for a colony.
    f.cloak = vehicleCanCloak(r, s, v) && !f.cloaked;
    f.decloak = f.cloaked;
    f.cargoCapacity = game::vehicleCargoCapacity(r, s, v) > 0;
    f.cargoHolds = !v.cargo.empty();
    f.mineSweeping = game::hasAbility(abilities, AbilityKind::MineSweeping);
    f.lowSupply = lowOnSupply(r, s, v);
    f.stellar = stellarAbility(abilities);
    f.emergency = anyOf(abilities, {AbilityKind::EmergencyResupply, AbilityKind::EmergencyEnergy});
}

OrderTarget vehicleTarget(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    using ruleset::VehicleType;
    switch (game::vehicleType(r, s, v)) {
        case VehicleType::Ship:
        case VehicleType::Base: return v.status == game::VehicleStatus::Mothballed ? OrderTarget::MothballedShip : OrderTarget::Ship;
        case VehicleType::Fighter: return OrderTarget::Fighters;
        case VehicleType::Satellite: return OrderTarget::Satellites;
        case VehicleType::Mine: return OrderTarget::Mines;
        case VehicleType::Drone: return OrderTarget::Drones;
        default: return OrderTarget::Other;  // troops and platforms only travel as cargo
    }
}

} // namespace

std::string_view orderName(OrderId o) { return o < OrderId::Count ? kOrderText[static_cast<size_t>(o)].name : std::string_view{}; }

std::string_view orderSlotKey(OrderId o) { return o < OrderId::Count ? kOrderText[static_cast<size_t>(o)].slotKey : std::string_view{}; }

std::optional<Action> orderAction(OrderId o) {
    switch (o) {
        case OrderId::MoveTo: return Action::MoveTo;
        case OrderId::Warp: return Action::Warp;
        case OrderId::MoveToWaypoint: return Action::MoveToWaypoint;
        case OrderId::Colonize: return Action::Colonize;
        case OrderId::Attack: return Action::Attack;
        case OrderId::FleetTransfer: return Action::FleetTransfer;
        case OrderId::Resupply: return Action::Resupply;
        case OrderId::Repair: return Action::Repair;
        case OrderId::ClearOrders: return Action::ClearOrders;
        case OrderId::BuildQueue: return Action::BuildQueue;
        case OrderId::CargoTransfer: return Action::CargoTransfer;
        case OrderId::LaunchRecover: return Action::LaunchRecover;
        case OrderId::LoadCargo: return Action::LoadCargo;
        case OrderId::DropCargo: return Action::DropCargo;
        case OrderId::LaunchRemote: return Action::LaunchRemote;
        case OrderId::RecoverRemote: return Action::RecoverRemote;
        case OrderId::Sentry: return Action::Sentry;
        case OrderId::Explore: return Action::Explore;
        case OrderId::Patrol: return Action::Patrol;
        case OrderId::RepeatOrders: return Action::RepeatOrders;
        case OrderId::StellarManipulation: return Action::StellarManipulation;
        case OrderId::ChangeName: return Action::Rename;
        case OrderId::Scrap: return Action::Scrap;
        case OrderId::Strategy: return Action::Strategy;
        case OrderId::ViewOrders: return Action::ViewOrders;
        case OrderId::SweepMines: return Action::SweepMines;
        case OrderId::ScrapFacilities: return Action::ScrapFacilities;
        case OrderId::Jettison: return Action::Jettison;
        case OrderId::Cloak: return Action::Cloak;
        case OrderId::Decloak: return Action::Decloak;
        case OrderId::UseComponent: return Action::UseComponent;
        case OrderId::UseFacility: return Action::UseFacility;
        case OrderId::AbandonPlanet: return Action::AbandonPlanet;
        case OrderId::ConvertResources: return Action::ConvertResources;
        case OrderId::Minister: return Action::Minister;
        case OrderId::ReplayPlay: return Action::ReplayPlay;
        case OrderId::ReplayShip: return Action::ReplayShip;
        case OrderId::ReplayStep: return Action::ReplayStep;
        case OrderId::ReplayRewind: return Action::ReplayRewind;
        case OrderId::Count: break;
    }
    return std::nullopt;
}

bool vehicleCanCloak(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    if (v.supply <= 0 && !game::vehicleHasUnlimitedSupply(r, s, v)) return false;
    const auto abilities = game::vehicleAbilities(r, s, v);
    for (size_t t = 0; t < game::kSightTypes; ++t)
        if (game::abilityPerSightType(abilities, AbilityKind::CloakLevel, static_cast<game::SightType>(t)) >= 2) return true;
    return false;
}

bool shipCycleCandidate(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, const game::Vehicle& v, bool turnBased) {
    if (v.owner != viewer || v.count <= 0 || game::isUnitType(game::vehicleType(r, s, v))) return false;
    if (v.status == game::VehicleStatus::Mothballed || game::vehicleMaxMovement(r, s, v) <= 0) return false;
    if (turnBased ? v.movement <= 0 : !v.orders.empty()) return false;
    return v.orders.empty() || v.orders.front().kind != game::OrderKind::Sentry;
}

std::optional<game::VehicleId> cycleShip(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, int direction,
                                         const ShipCycleOptions& options, ShipCycleMemory& memory) {
    const int dir = direction < 0 ? -1 : 1;
    if (memory.player != viewer) {
        // Another player's turn on this computer: the place is forgotten too.
        memory = {};
        memory.player = viewer;
    }
    if (memory.direction != dir) {
        memory.direction = dir;
        memory.visited.clear();
    }
    const std::vector<game::VehicleId> list = game::vehiclesInObjectOrder(s);
    const auto n = static_cast<std::ptrdiff_t>(list.size());
    if (n == 0) return std::nullopt;
    auto slotAt = [&](std::ptrdiff_t i) {
        const game::Vehicle* v = s.vehicle(list[static_cast<size_t>(i)]);
        return v ? v->slot : 0u;
    };
    // Where the walk starts: just past the remembered place. With nothing
    // remembered, forward from the list's start, and back from one before its
    // end, so that the last object of the list is looked at last.
    std::ptrdiff_t at = dir > 0 ? -1 : n;   // the walk's first step leaves this index
    if (memory.place || dir < 0) {
        uint32_t place = 0;
        if (memory.place) {
            place = *memory.place;
        } else {
            const std::vector<game::ObjectRef> all = game::objectOrder(s);
            place = all.empty() ? 0 : all.back().slot;
        }
        if (dir > 0) {
            at = -1;
            while (at + 1 < n && slotAt(at + 1) <= place) ++at;
        } else {
            at = n;
            while (at - 1 >= 0 && slotAt(at - 1) >= place) --at;
        }
    }
    auto visited = [&](const game::Location& l) { return std::find(memory.visited.begin(), memory.visited.end(), l) != memory.visited.end(); };
    bool wrapped = false;
    for (std::ptrdiff_t step = 1; step <= n; ++step) {
        const std::ptrdiff_t raw = at + dir * step;
        if (!wrapped && (raw < 0 || raw >= n)) {
            // Past either end of the list: the places stopped at are forgotten.
            wrapped = true;
            memory.visited.clear();
        }
        const std::ptrdiff_t i = ((raw % n) + n) % n;
        const game::Vehicle* v = s.vehicle(list[static_cast<size_t>(i)]);
        if (!v || !shipCycleCandidate(r, s, viewer, *v, options.turnBased)) continue;
        // "Skip ships under construction" has nothing to skip: our ships appear finished.
        if (options.skipDamaged && game::vehicleDamageTaken(s, *v) > 0) continue;
        if (options.skipInFleets && v->fleet.valid()) continue;
        if (options.stopOncePerLocation) {
            if (visited(v->location)) continue;
            if (memory.visited.size() < kShipCycleStops) memory.visited.push_back(v->location);
        }
        memory.place = v->slot;
        return v->id;
    }
    return std::nullopt;
}

std::optional<game::VehicleId> cycleFleet(const game::GameState& s, game::EmpireId viewer, int direction, FleetCycleMemory& memory) {
    if (memory.player != viewer) memory = FleetCycleMemory{viewer, std::nullopt};
    const int dir = direction < 0 ? -1 : 1;
    // The viewer's fleets by fleet number (OpenSE4's fleet ids keep the order fleets were made in).
    std::vector<const game::Fleet*> fleets;
    for (const game::Fleet& f : s.fleets)
        if (f.owner == viewer) fleets.push_back(&f);
    std::sort(fleets.begin(), fleets.end(), [](const game::Fleet* a, const game::Fleet* b) { return a->id.value < b->id.value; });
    const auto n = static_cast<std::ptrdiff_t>(fleets.size());
    if (n == 0) return std::nullopt;
    // The first member, in object order, that still has movement left this turn.
    auto mover = [&](const game::Fleet& f) -> std::optional<game::VehicleId> {
        const game::Vehicle* best = nullptr;
        for (game::VehicleId id : f.members)
            if (const game::Vehicle* v = s.vehicle(id); v && v->owner == viewer && v->fleet == f.id && v->count > 0 && v->movement > 0)
                if (!best || v->slot < best->slot) best = v;
        return best ? std::optional(best->id) : std::nullopt;
    };
    // Just past the fleet chosen last, round the end; from the first (or, back, the last) with none.
    std::ptrdiff_t at = dir > 0 ? -1 : n;
    if (memory.last) {
        if (dir > 0) {
            while (at + 1 < n && fleets[static_cast<size_t>(at + 1)]->id.value <= memory.last->value) ++at;
        } else {
            while (at - 1 >= 0 && fleets[static_cast<size_t>(at - 1)]->id.value >= memory.last->value) --at;
        }
    }
    for (std::ptrdiff_t step = 1; step <= n; ++step) {
        const std::ptrdiff_t i = (((at + dir * step) % n) + n) % n;
        const game::Fleet& f = *fleets[static_cast<size_t>(i)];
        if (const auto v = mover(f)) {
            memory.last = f.id;
            return v;
        }
    }
    return std::nullopt;
}

std::optional<game::ObjectId> cycleColony(const game::GameState& s, game::EmpireId viewer, int direction, ColonyCycleMemory& memory) {
    if (memory.player != viewer) memory = ColonyCycleMemory{viewer, std::nullopt};
    const int dir = direction < 0 ? -1 : 1;
    // By planet name, character by character (letter case counts), in the
    // game's colony order where two names are the same.
    std::vector<std::pair<std::string, game::ObjectId>> colonies;
    for (const auto& c : s.colonies)
        if (c && c->owner == viewer) colonies.emplace_back(s.galaxy.object(c->planet).name, c->planet);
    std::stable_sort(colonies.begin(), colonies.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const auto n = static_cast<std::ptrdiff_t>(colonies.size());
    if (n == 0) return std::nullopt;
    auto where = [&](std::ptrdiff_t i) { return game::locationOf(s.galaxy, colonies[static_cast<size_t>(i)].second); };
    // From the colonies of the remembered sector: past the last of them going
    // forward, before the first going back (inferred, spec 06 §7 Q125).
    std::ptrdiff_t at = dir > 0 ? -1 : n;
    if (memory.last) {
        for (std::ptrdiff_t i = 0; i < n; ++i)
            if (where(i) == *memory.last) {
                if (dir > 0) at = i;
                else if (at == n) at = i;
            }
    }
    const std::ptrdiff_t i = (((at + dir) % n) + n) % n;
    memory.last = where(i);
    return colonies[static_cast<size_t>(i)].second;
}

LitOrders litOrders(const OrderFacts& f) {
    LitOrders lit{};
    if (f.locked) return lit;
    auto on = [&](OrderId o, bool when = true) {
        if (when) lit[static_cast<size_t>(o)] = true;
    };
    // The movement log: every simultaneous game, whatever is selected.
    for (OrderId o : {OrderId::ReplayPlay, OrderId::ReplayShip, OrderId::ReplayStep, OrderId::ReplayRewind}) on(o, !f.turnBased);
    const bool orders = f.hasOrders;
    switch (f.target) {
        case OrderTarget::Nothing:
        case OrderTarget::Other: break;
        case OrderTarget::Ship:
            for (OrderId o : {OrderId::Scrap, OrderId::ChangeName, OrderId::FleetTransfer, OrderId::DropCargo, OrderId::CargoTransfer,
                              OrderId::Minister})
                on(o);
            on(OrderId::LaunchRecover, f.turnBased);
            for (OrderId o : {OrderId::ClearOrders, OrderId::ViewOrders, OrderId::RepeatOrders}) on(o, orders);
            for (OrderId o : {OrderId::MoveTo, OrderId::Explore, OrderId::Resupply, OrderId::Repair, OrderId::Patrol, OrderId::Attack})
                on(o, f.mobile);
            on(OrderId::Warp, f.mobile && f.canWarp);
            on(OrderId::MoveToWaypoint, f.mobile && f.waypointSet);
            on(OrderId::Colonize, f.canColonize);
            on(OrderId::BuildQueue, f.spaceYard && !f.cloaked);
            on(OrderId::LoadCargo, f.cargoCapacity);
            on(OrderId::RecoverRemote, f.cargoCapacity);
            on(OrderId::LaunchRemote, f.cargoHolds);
            on(OrderId::Jettison, f.cargoHolds);
            on(OrderId::SweepMines, f.mineSweeping);
            on(OrderId::Sentry, !f.lowSupply);
            on(OrderId::StellarManipulation, f.stellar);
            on(OrderId::Cloak, f.cloak);
            on(OrderId::Decloak, f.decloak);
            on(OrderId::UseComponent, f.emergency);
            break;  // Change Formation\Strategy: never for one ship
        case OrderTarget::MothballedShip:
            on(OrderId::Scrap);
            on(OrderId::ClearOrders, orders);
            on(OrderId::ViewOrders, orders);
            break;
        case OrderTarget::Fleet:
            for (OrderId o : {OrderId::ChangeName, OrderId::FleetTransfer, OrderId::Strategy, OrderId::Scrap, OrderId::LoadCargo, OrderId::DropCargo,
                              OrderId::CargoTransfer, OrderId::Sentry, OrderId::LaunchRemote, OrderId::RecoverRemote, OrderId::Minister})
                on(o);
            on(OrderId::LaunchRecover, f.turnBased);
            for (OrderId o : {OrderId::MoveTo, OrderId::Warp, OrderId::Explore, OrderId::Resupply, OrderId::Repair, OrderId::Patrol,
                              OrderId::Attack})
                on(o, f.mobile);
            on(OrderId::MoveToWaypoint, f.mobile && f.waypointSet);
            on(OrderId::Colonize, f.canColonize);
            for (OrderId o : {OrderId::ClearOrders, OrderId::ViewOrders, OrderId::RepeatOrders}) on(o, orders);
            on(OrderId::Cloak, f.cloak);
            on(OrderId::Decloak, f.decloak);
            break;
        case OrderTarget::Colony:
            for (OrderId o : {OrderId::ChangeName, OrderId::CargoTransfer, OrderId::BuildQueue, OrderId::Scrap, OrderId::Minister,
                              OrderId::AbandonPlanet})
                on(o);
            on(OrderId::LaunchRecover, f.turnBased);
            for (OrderId o : {OrderId::ClearOrders, OrderId::ViewOrders, OrderId::RepeatOrders}) on(o, orders);
            on(OrderId::Jettison, f.cargoHolds);
            on(OrderId::LaunchRemote, f.cargoHolds);
            on(OrderId::RecoverRemote, f.cargoCapacity);
            on(OrderId::ScrapFacilities, f.facilities);
            on(OrderId::Cloak, f.cloak);
            on(OrderId::Decloak, f.decloak);
            on(OrderId::UseFacility, f.emergency);
            on(OrderId::ConvertResources, f.conversion);
            break;
        case OrderTarget::Fighters:
            for (OrderId o : {OrderId::FleetTransfer, OrderId::Scrap, OrderId::Minister}) on(o);
            for (OrderId o : {OrderId::MoveTo, OrderId::Explore, OrderId::Resupply, OrderId::Repair, OrderId::Patrol, OrderId::Attack})
                on(o, f.mobile);
            on(OrderId::MoveToWaypoint, f.mobile && f.waypointSet);
            for (OrderId o : {OrderId::ClearOrders, OrderId::ViewOrders, OrderId::RepeatOrders}) on(o, orders);
            on(OrderId::Sentry, !f.lowSupply);
            on(OrderId::Cloak, f.cloak);
            on(OrderId::Decloak, f.decloak);
            break;
        case OrderTarget::Satellites:
            on(OrderId::Scrap);
            on(OrderId::Minister);
            on(OrderId::ClearOrders, orders);
            on(OrderId::ViewOrders, orders);
            on(OrderId::Cloak, f.cloak);
            on(OrderId::Decloak, f.decloak);
            break;
        case OrderTarget::Mines:
            on(OrderId::Scrap);
            on(OrderId::Minister);
            on(OrderId::ClearOrders, orders);
            on(OrderId::ViewOrders, orders);
            break;
        case OrderTarget::Drones:
            on(OrderId::Scrap);
            on(OrderId::Minister);
            on(OrderId::Attack);
            on(OrderId::ViewOrders, orders);
            on(OrderId::Cloak, f.cloak);
            on(OrderId::Decloak, f.decloak);
            break;
        case OrderTarget::Tagged:
            if (f.droneTagged) {
                on(OrderId::Attack);
                on(OrderId::Minister);
                break;
            }
            for (OrderId o : {OrderId::MoveTo, OrderId::Warp, OrderId::Explore, OrderId::Resupply, OrderId::Repair, OrderId::Patrol,
                              OrderId::Attack, OrderId::Scrap, OrderId::Minister, OrderId::ClearOrders})
                on(o);
            on(OrderId::MoveToWaypoint, f.waypointSet);
            on(OrderId::Cloak, f.cloak);
            on(OrderId::Decloak, f.decloak);
            break;
    }
    return lit;
}

OrderFacts orderFacts(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, const OrderSelection& sel, bool turnBased,
                      bool locked) {
    OrderFacts f;
    f.turnBased = turnBased;
    f.locked = locked;
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return f;
    f.waypointSet = anyWaypoint(s.empire(viewer));

    if (!sel.tagged.empty()) {
        f.target = OrderTarget::Tagged;
        bool allCanCloak = true, noneCloaked = true, allCloaked = true;
        for (game::VehicleId id : sel.tagged) {
            const game::Vehicle* v = s.vehicle(id);
            if (!v || v->owner != viewer) continue;
            if (game::vehicleType(r, s, *v) == ruleset::VehicleType::Drone) f.droneTagged = true;
            const bool cloaked = v->status == game::VehicleStatus::Cloaked;
            allCanCloak = allCanCloak && vehicleCanCloak(r, s, *v);
            noneCloaked = noneCloaked && !cloaked;
            allCloaked = allCloaked && cloaked;
        }
        f.cloak = allCanCloak && noneCloaked;
        f.decloak = allCloaked;
        return f;
    }

    if (sel.fleet) {
        const game::Fleet* fleet = s.fleet(*sel.fleet);
        if (fleet && fleet->owner == viewer) {
            f.target = OrderTarget::Fleet;
            f.hasOrders = !game::fleetOrders(s, *fleet).empty();
            // The members at the fleet's location only make its speed and
            // its Colonize, Cloak and Decloak (spec 06 §2.8, confirmed: binary).
            const std::vector<game::VehicleId> group = game::fleetGroup(s, *fleet);
            int speed = group.empty() ? 0 : 1 << 30;
            for (game::VehicleId id : group) {
                const game::Vehicle* v = s.vehicle(id);
                if (!v) continue;
                speed = std::min(speed, game::vehicleMaxMovement(r, s, *v));
                OrderFacts m;
                vehicleFacts(r, s, *v, m);
                f.canColonize = f.canColonize || m.canColonize;
                f.cloak = f.cloak || m.cloak;          // any member can cloak and is not cloaked
                f.decloak = f.decloak || m.cloaked;    // any member is cloaked
                f.cloaked = f.cloaked || m.cloaked;
            }
            // Clear, View and Repeat Orders: any member has orders, wherever it is.
            for (game::VehicleId id : fleet->members)
                if (const game::Vehicle* v = s.vehicle(id)) f.hasOrders = f.hasOrders || !v->orders.empty();
            f.mobile = speed > 0;
            f.canWarp = f.mobile;
            return f;
        }
    }

    if (sel.vehicle) {
        const game::Vehicle* v = s.vehicle(*sel.vehicle);
        if (!v) return f;
        if (v->owner != viewer) {
            f.target = OrderTarget::Other;
            return f;
        }
        f.target = vehicleTarget(r, s, *v);
        vehicleFacts(r, s, *v, f);
        return f;
    }

    if (sel.planet) {
        const game::Colony* c = s.colony(*sel.planet);
        if (!c || c->owner != viewer) {
            f.target = OrderTarget::Other;
            return f;
        }
        f.target = OrderTarget::Colony;
        const auto abilities = game::colonyAbilities(r, s, *c);
        f.hasOrders = !c->orders.empty();
        f.cargoCapacity = game::colonyCargoCapacity(r, s, *c) > 0;
        f.cargoHolds = !c->cargo.empty();
        f.facilities = !c->facilities.empty();
        f.emergency = anyOf(abilities, {AbilityKind::EmergencyResupply, AbilityKind::EmergencyEnergy});
        f.conversion = game::hasAbility(abilities, AbilityKind::ResourceConversion);
        // Cloak when the colony can cloak and is not cloaked; Decloak whenever
        // it is cloaked, even when it can no longer cloak (spec 01 §6.9).
        f.cloaked = c->cloaked;
        f.cloak = game::sight::colonyCanCloak(*c) && !c->cloaked;
        f.decloak = c->cloaked;
        return f;
    }

    if (sel.other) f.target = OrderTarget::Other;
    return f;
}

} // namespace opense4::client::classic
