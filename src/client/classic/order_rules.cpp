#include "client/classic/order_rules.hpp"

#include "game/abilities.hpp"
#include "game/design.hpp"
#include "game/query.hpp"

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

// Supplies below the warning level; fighter groups warn at a tenth of it (as
// their Sentry does, spec 03 §8; inferred for the button).
bool lowOnSupply(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    if (game::vehicleHasUnlimitedSupply(r, s, v) || !game::vehicleUsesSupply(r, s, v)) return false;
    int64_t warning = r.setting("Supply Amount for Low Supply Warning", 1000);
    if (game::vehicleType(r, s, v) == ruleset::VehicleType::Fighter) warning /= 10;
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
    const bool canCloak = vehicleCanCloak(r, s, v);
    f.cloak = canCloak && !f.cloaked;
    f.decloak = canCloak && f.cloaked;
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

bool orderNotInEngine(OrderId o) { return o == OrderId::Jettison || o == OrderId::UseFacility || o == OrderId::ConvertResources; }

bool vehicleCanCloak(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    if (v.supply <= 0 && !game::vehicleHasUnlimitedSupply(r, s, v)) return false;
    const auto abilities = game::vehicleAbilities(r, s, v);
    for (size_t t = 0; t < game::kSightTypes; ++t)
        if (game::abilityPerSightType(abilities, AbilityKind::CloakLevel, static_cast<game::SightType>(t)) >= 2) return true;
    return false;
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
            f.hasOrders = !fleet->orders.empty();
            int speed = fleet->members.empty() ? 0 : 1 << 30;
            for (game::VehicleId id : fleet->members) {
                const game::Vehicle* v = s.vehicle(id);
                if (!v) continue;
                speed = std::min(speed, game::vehicleMaxMovement(r, s, *v));
                OrderFacts m;
                vehicleFacts(r, s, *v, m);
                f.canColonize = f.canColonize || m.canColonize;
                f.hasOrders = f.hasOrders || m.hasOrders;
                f.cloak = f.cloak || m.cloak;          // any member can cloak and is not cloaked
                f.decloak = f.decloak || m.cloaked;    // any member is cloaked
                f.cloaked = f.cloaked || m.cloaked;
            }
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
        // Colonies cannot cloak in our engine yet, so Cloak and Decloak stay dim.
        return f;
    }

    if (sel.other) f.target = OrderTarget::Other;
    return f;
}

} // namespace opense4::client::classic
