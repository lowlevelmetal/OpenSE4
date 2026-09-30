#include "client/classic/screens/ships_logic.hpp"

#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace opense4::client::classic::shipui {

using game::AbilityKind;
using game::StellarAction;

// ---- Orders ---------------------------------------------------------------------------------

OrderOwner orderOwner(const game::GameState& s, game::VehicleId id) {
    OrderOwner o;
    const game::Vehicle* v = s.vehicle(id);
    if (!v) return o;
    if (v->fleet.valid() && s.fleet(v->fleet)) o.fleet = v->fleet;
    else o.vehicle = id;
    return o;
}

const std::vector<game::Order>* ordersOf(const game::GameState& s, OrderOwner o) {
    if (o.planet.valid()) {
        const game::Colony* c = s.colony(o.planet);
        return c ? &c->orders : nullptr;
    }
    if (o.fleet.valid()) {
        const game::Fleet* f = s.fleet(o.fleet);
        return f ? &f->orders : nullptr;
    }
    const game::Vehicle* v = s.vehicle(o.vehicle);
    return v ? &v->orders : nullptr;
}

bool repeatOf(const game::GameState& s, OrderOwner o) {
    if (o.planet.valid()) return false;
    if (o.fleet.valid()) {
        const game::Fleet* f = s.fleet(o.fleet);
        return f && f->repeatOrders;
    }
    const game::Vehicle* v = s.vehicle(o.vehicle);
    return v && v->repeatOrders;
}

game::cmd::SetOrders setOrders(OrderOwner o, std::vector<game::Order> orders, bool repeat) {
    game::cmd::SetOrders c;
    if (o.planet.valid()) c.planet = o.planet;
    else if (o.fleet.valid()) c.fleet = o.fleet;
    else c.vehicle = o.vehicle;
    c.orders = std::move(orders);
    c.repeat = repeat;
    return c;
}

game::cmd::SetOrders withAppended(const game::GameState& s, OrderOwner o, const game::Order& order) {
    std::vector<game::Order> orders;
    if (const auto* cur = ordersOf(s, o)) orders = *cur;
    orders.push_back(order);
    return setOrders(o, std::move(orders), repeatOf(s, o));
}

bool immediateKind(game::OrderKind k) {
    using game::OrderKind;
    return k == OrderKind::LaunchUnits || k == OrderKind::RecoverUnits || k == OrderKind::StellarManipulation || k == OrderKind::UseComponent;
}

void insertImmediate(std::vector<game::Order>& orders, const game::Order& order, game::Location here) {
    auto it = std::find_if(orders.begin(), orders.end(), [&](const game::Order& o) {
        const bool atHere = !o.location.system.valid() || o.location == here ||
                            (o.kind == game::OrderKind::StellarManipulation && o.amount == static_cast<int>(game::StellarAction::OpenWarpPoint));
        return !immediateKind(o.kind) || !atHere;
    });
    orders.insert(it, order);
}

std::optional<game::Location> ownerLocation(const game::GameState& s, OrderOwner o) {
    if (o.planet.valid()) {
        if (!s.colony(o.planet)) return std::nullopt;
        return game::locationOf(s.galaxy, o.planet);
    }
    game::VehicleId id = o.vehicle;
    if (const game::Fleet* f = s.fleet(o.fleet)) id = f->leader.valid() ? f->leader : f->members.empty() ? game::VehicleId{} : f->members.front();
    if (const game::Vehicle* v = s.vehicle(id)) return v->location;
    return std::nullopt;
}

game::cmd::SetOrders withImmediate(const game::GameState& s, OrderOwner o, const game::Order& order) {
    std::vector<game::Order> orders;
    if (const auto* cur = ordersOf(s, o)) orders = *cur;
    insertImmediate(orders, order, ownerLocation(s, o).value_or(order.location));
    return setOrders(o, std::move(orders), repeatOf(s, o));
}

size_t moveOrder(std::vector<game::Order>& orders, size_t index, int delta) {
    if (index >= orders.size()) return index;
    const auto target = static_cast<int64_t>(index) + delta;
    const size_t to = static_cast<size_t>(std::clamp<int64_t>(target, 0, static_cast<int64_t>(orders.size()) - 1));
    if (to == index) return index;
    const game::Order o = orders[index];
    orders.erase(orders.begin() + static_cast<std::ptrdiff_t>(index));
    orders.insert(orders.begin() + static_cast<std::ptrdiff_t>(to), o);
    return to;
}

// ---- Steps ----------------------------------------------------------------------------------

int64_t stepAmount(Step step, int64_t available) {
    if (available <= 0) return 0;
    switch (step) {
        case Step::One: return 1;
        case Step::Five: return std::min<int64_t>(5, available);
        case Step::Ten: return std::min<int64_t>(10, available);
        case Step::All: return available;
    }
    return 0;
}

const char* stepLabel(Step step) {
    switch (step) {
        case Step::One: return "Move One";
        case Step::Five: return "Move Five";
        case Step::Ten: return "Move Ten";
        case Step::All: return "Move All";
    }
    return "";
}

// ---- Units --------------------------------------------------------------------------------

bool isUnitVehicle(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    return game::isUnitType(game::vehicleType(r, s, v));
}

bool isUnitDesign(const game::Rules& r, const game::GameState& s, game::DesignId d) {
    if (!d.valid() || d.index() >= s.designs.size()) return false;
    return game::isUnitType(r.hull(s.design(d).hull).type);
}

LaunchRates launchRates(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    const auto abilities = game::vehicleAbilities(r, s, v);
    LaunchRates out;
    auto rate = [&](AbilityKind k) {
        if (!game::hasAbility(abilities, k)) return -1;
        int64_t n = 0;
        for (const auto& a : abilities)
            if (a.kind == k) n += a.value2;
        return static_cast<int>(n);
    };
    out.fighters = rate(AbilityKind::LaunchRecoverFighters);
    out.satellites = rate(AbilityKind::LaunchRecoverSatellites);
    out.mines = rate(AbilityKind::LayMines);
    out.drones = rate(AbilityKind::LaunchDrones);
    return out;
}

bool canLaunch(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, game::DesignId unit) {
    if (!isUnitDesign(r, s, unit)) return false;
    const LaunchRates rates = launchRates(r, s, v);
    switch (r.hull(s.design(unit).hull).type) {
        case ruleset::VehicleType::Fighter: return rates.fighters >= 0;
        case ruleset::VehicleType::Satellite: return rates.satellites >= 0;
        case ruleset::VehicleType::Mine: return rates.mines >= 0;
        case ruleset::VehicleType::Drone: return rates.drones >= 0;
        default: return false;
    }
}

// ---- Scrap window ---------------------------------------------------------------------------

game::Resources scrapValue(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) { return game::scrapRefund(r, s, v); }

game::Resources unmothballCost(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) { return game::unmothballCharge(r, s, v); }

game::Resources facilityScrapValue(const game::Rules& r, const game::GameState& s, const game::Colony& c, size_t slot) {
    if (slot >= c.facilities.size()) return {};
    int64_t pct = r.setting("Scrap Facility Percent Returned", 30);
    pct = std::max<int64_t>(pct, game::reclamationPercentAt(r, s, c.owner, game::locationOf(s.galaxy, c.planet)));
    return game::Resources::from(r.facility(c.facilities[slot]).cost).percentRounded(pct);  // round(cost × %), spec 02 §6.6
}

std::optional<size_t> selfDestructEntry(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    const game::Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (game::entryIntact(r, s, v, i) && game::hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::SelfDestruct))
            return i;
    return std::nullopt;
}

bool vehicleArmed(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    if (v.status == game::VehicleStatus::Mothballed) return false;
    const game::Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (r.component(d.entries[i].component).isWeapon() && game::entryIntact(r, s, v, i)) return true;
    return false;
}

bool canBeFiredOn(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, const std::vector<game::VehicleId>& selection) {
    for (const game::Vehicle& other : s.vehicles) {
        if (other.id == v.id || other.owner != v.owner || other.location != v.location) continue;
        if (std::find(selection.begin(), selection.end(), other.id) != selection.end()) continue;
        if (vehicleArmed(r, s, other)) return true;
    }
    return false;
}

ResearchPotential researchPotential(const game::Rules& r, const game::GameState& s, const game::Empire& e,
                                    const std::vector<const game::Vehicle*>& vehicles) {
    ResearchPotential p;
    for (const game::Vehicle* v : vehicles) {
        if (!v) continue;
        for (const game::DesignEntry& en : s.design(v->design).entries) {
            ++p.total;
            if (!r.componentAvailable(e, en.component)) ++p.unknown;
        }
    }
    return p;
}

const char* researchPotentialLabel(ResearchPotential p) {
    if (p.unknown == 0 || p.total == 0) return "None";
    const int pct = p.unknown * 100 / p.total;
    return pct >= 50 ? "High" : pct >= 20 ? "Moderate" : "Low";
}

game::Resources vehicleMaintenance(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    if (v.status == game::VehicleStatus::Mothballed || !v.owner.valid()) return {};
    const game::Empire& e = s.empire(v.owner);
    int64_t pct = r.setting("Empire Starting Percent Maint Cost", 25) - (e.race.characteristic(game::Characteristic::MaintenanceAptitude) - 100);
    if (const ruleset::Culture* c = r.culture(e.race)) pct -= c->maintenance;
    pct = std::max<int64_t>(5, pct);
    // Design modifier and the best system reduction from own colonies in the system.
    const int64_t designMod = game::sumValue1(game::vehicleAbilities(r, s, v), AbilityKind::ModifiedMaintenanceCost);
    int64_t systemCut = 0;
    for (game::ObjectId id : s.galaxy.system(v.location.system).objects)
        if (const game::Colony* c = s.colony(id); c && c->owner == v.owner)
            systemCut = std::max(systemCut, game::bestValue1(game::colonyAbilities(r, s, *c), AbilityKind::ReducedMaintenanceSystem));
    const int64_t factor = std::max<int64_t>(0, 100 - systemCut - designMod);
    const game::Resources cost = game::computeDesignStats(r, nullptr, s.design(v.design)).cost;
    game::Resources out;
    for (size_t i = 0; i < 3; ++i) out.v[i] = cost.v[i] * pct * factor / 10000;
    if (game::vehicleType(r, s, v) == ruleset::VehicleType::Base)
        for (auto& x : out.v) x /= 2;
    for (auto& x : out.v) x *= std::max(1, v.count);
    return out;
}

DryRun dryRun(const game::Rules& r, const game::GameState& s, game::EmpireId e, const game::Command& c) {
    game::GameState copy = s;
    DryRun out;
    out.result = game::apply(r, copy, e, c);
    if (out.result.ok) out.cost = s.empire(e).stockpile - copy.empire(e).stockpile;
    return out;
}

// ---- Stellar manipulation -------------------------------------------------------------------

namespace {

constexpr std::array<StellarInfo, static_cast<size_t>(StellarAction::Count)> kStellar{{
    {StellarAction::CreatePlanet, "Create Planet", AbilityKind::CreatePlanetSize, "PlanetCreate", 20},
    {StellarAction::DestroyPlanet, "Destroy Planet", AbilityKind::DestroyPlanetSize, "PlanetDestroy", 20},
    {StellarAction::CreateStar, "Create Star", AbilityKind::CreateStar, "StarCreate", 20},
    {StellarAction::DestroyStar, "Destroy Star", AbilityKind::DestroyStar, "StarDestroy", 20},
    {StellarAction::OpenWarpPoint, "Open Warp Point", AbilityKind::OpenWarpPointDistance, "WPOpen", 20},
    {StellarAction::CloseWarpPoint, "Close Warp Point", AbilityKind::CloseWarpPoint, "WpClose", 20},
    {StellarAction::CreateStorm, "Create Storm", AbilityKind::CreateStorm, "StormCreate", 20},
    {StellarAction::DestroyStorm, "Destroy Storm", AbilityKind::DestroyStorm, "StormDestroy", 20},
    {StellarAction::CreateNebulae, "Create Nebulae", AbilityKind::CreateNebulae, "NebulaeCreate", 20},
    {StellarAction::DestroyNebulae, "Destroy Nebulae", AbilityKind::DestroyNebulae, "NebulaeDestroy", 20},
    {StellarAction::CreateBlackHole, "Create Black Hole", AbilityKind::CreateBlackHole, "BlackHoleCreate", 20},
    {StellarAction::DestroyBlackHole, "Destroy Black Hole", AbilityKind::DestroyBlackHole, "BlackHoleDestroy", 20},
    {StellarAction::CreateConstructedPlanet, "Construct", AbilityKind::CreateConstructedPlanet, "Ring", 16},
}};

} // namespace

const StellarInfo& stellarInfo(StellarAction a) {
    const auto i = static_cast<size_t>(a);
    return kStellar[i < kStellar.size() ? i : 0];
}

StellarCheck checkStellar(const game::Rules& r, const game::GameState& s, const game::Vehicle& v, StellarAction a) {
    StellarCheck c;
    const StellarInfo& info = stellarInfo(a);
    c.hasAbility = game::hasAbility(game::vehicleAbilities(r, s, v), info.ability);
    if (!c.hasAbility) {
        c.reason = std::format("This vehicle has no working component with {}.", game::identifier(info.ability));
        return c;
    }
    // The checks the turn makes when it carries out the order (docs/spec/01 §9):
    // the button is enabled only when they pass now. Open Warp Point's
    // destination is picked on the map afterwards.
    game::Order o = stellarOrder(v, a, {});
    if (a == StellarAction::OpenWarpPoint) o.location = {};
    if (std::string why = game::movement::stellarProblem(r, s, v.id, o, &c.target); !why.empty()) {
        c.reason = why + ".";
        return c;
    }
    switch (a) {
        case StellarAction::CreatePlanet: c.reason = "The asteroid field becomes a planet."; break;
        case StellarAction::DestroyPlanet: c.reason = "The planet becomes an asteroid field."; break;
        case StellarAction::CreateStar: c.reason = "A new star forms in this sector."; break;
        case StellarAction::DestroyStar:
            c.destroysSystem = true;
            c.reason = "The shockwave destroys everything in the system except warp points, this ship included.";
            break;
        case StellarAction::OpenWarpPoint:
            c.needsDestination = true;
            c.reason = "Pick a sector of the destination system on the map.";
            break;
        case StellarAction::CloseWarpPoint: c.reason = "Both ends of the warp point disappear."; break;
        case StellarAction::CreateStorm: c.reason = "A storm forms in this sector."; break;
        case StellarAction::DestroyStorm: c.reason = "The storm is dispersed."; break;
        case StellarAction::CreateNebulae:
            c.destroysSystem = true;
            c.reason = "The star becomes a nebula; everything in the system is destroyed, this ship included.";
            break;
        case StellarAction::DestroyNebulae: c.reason = "The nebula is cleared from the system."; break;
        case StellarAction::CreateBlackHole:
            c.destroysSystem = true;
            c.reason = "The star collapses; everything in the system is destroyed, this ship included.";
            break;
        case StellarAction::DestroyBlackHole: c.reason = "The black hole is removed from the system."; break;
        case StellarAction::CreateConstructedPlanet: c.reason = "A world is built around the star."; break;
        case StellarAction::Count: return c;
    }
    c.possible = true;
    return c;
}

game::Order stellarOrder(const game::Vehicle& v, StellarAction a, game::ObjectId target) {
    game::Order o{game::OrderKind::StellarManipulation, v.location};
    o.object = target;
    o.amount = static_cast<int>(a);
    return o;
}

} // namespace opense4::client::classic::shipui
