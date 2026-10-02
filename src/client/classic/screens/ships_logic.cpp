#include "client/classic/screens/ships_logic.hpp"

#include "client/classic/screens/colony_logic.hpp"

#include "game/design.hpp"
#include "game/economy.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/scrap.hpp"

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
        return f ? &game::fleetOrders(s, *f) : nullptr;  // its members' copies (spec 03 §8)
    }
    const game::Vehicle* v = s.vehicle(o.vehicle);
    return v ? &v->orders : nullptr;
}

bool repeatOf(const game::GameState& s, OrderOwner o) {
    if (o.planet.valid()) {
        const game::Colony* c = s.colony(o.planet);
        return c && c->repeatOrders;
    }
    if (o.fleet.valid()) {
        const game::Fleet* f = s.fleet(o.fleet);
        return f && game::fleetRepeats(s, *f);
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
    return k == OrderKind::LaunchUnits || k == OrderKind::RecoverUnits || k == OrderKind::StellarManipulation || k == OrderKind::UseComponent ||
           k == OrderKind::SelfDestruct;
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
        case Step::Hundred: return std::min<int64_t>(100, available);
        case Step::All: return available;
    }
    return 0;
}

const char* stepLabel(Step step) {
    switch (step) {
        case Step::One: return "Move One";
        case Step::Five: return "Move Five";
        case Step::Ten: return "Move Ten";
        case Step::Hundred: return "Move Hundred";
        case Step::All: return "Move All";
    }
    return "";
}

// ---- Jettison Cargo -----------------------------------------------------------------------

void JettisonLists::move(bool fromPresent, size_t line, Step step) {
    std::vector<JettisonLine>& from = fromPresent ? present : chosen;
    std::vector<JettisonLine>& to = fromPresent ? chosen : present;
    if (line >= from.size()) return;
    JettisonLine moved = from[line];
    moved.amount = std::min(stepAmount(step, from[line].amount), from[line].amount);
    if (moved.amount <= 0) return;
    from[line].amount -= moved.amount;
    if (from[line].amount <= 0) from.erase(from.begin() + static_cast<std::ptrdiff_t>(line));
    const auto it = std::find_if(to.begin(), to.end(), [&](const JettisonLine& l) { return l.unit == moved.unit && l.race == moved.race; });
    if (it != to.end()) it->amount += moved.amount;
    else to.push_back(moved);
}

JettisonLists jettisonLists(const game::Cargo& cargo) {
    JettisonLists out;
    for (const game::PopulationGroup& p : cargo.population)
        if (p.millions > 0) out.present.push_back({{}, p.race, p.millions});
    for (const game::UnitStack& u : cargo.units)
        if (u.count > 0) out.present.push_back({u.design, {}, u.count});
    return out;
}

std::optional<game::cmd::JettisonCargo> jettisonCommand(const JettisonLists& lists, game::VehicleId vehicle, game::ObjectId planet) {
    if (lists.chosen.empty()) return std::nullopt;
    game::cmd::JettisonCargo c;
    c.vehicle = vehicle;
    c.planet = vehicle.valid() ? game::ObjectId{} : planet;
    for (const JettisonLine& l : lists.chosen) {
        if (l.unit.valid()) c.units.push_back({l.unit, static_cast<int>(l.amount)});
        else c.population.push_back({l.race, l.amount});
    }
    return c;
}

bool canJettisonFrom(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::VehicleId vehicle, game::ObjectId planet) {
    if (vehicle.valid()) {
        const game::Vehicle* v = s.vehicle(vehicle);
        return v && v->owner == viewer && v->count > 0 && !isUnitVehicle(r, s, *v) && v->status != game::VehicleStatus::Mothballed;
    }
    const game::Colony* c = s.colony(planet);
    return c && c->owner == viewer;
}

// ---- Convert Resources --------------------------------------------------------------------

void ConversionWindow::add(game::Resource from) {
    const auto it = std::find_if(lines.begin(), lines.end(), [&](const ConversionLine& l) { return l.from == from && l.to == target; });
    if (it != lines.end()) it->amount += step;
    else lines.push_back({from, target, step});
}

void ConversionWindow::remove(size_t line) {
    if (line >= lines.size()) return;
    lines[line].amount = std::max<int64_t>(0, lines[line].amount - step);
    if (lines[line].amount == 0) lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(line));
}

void ConversionWindow::press(int64_t bigStep) { step = step == bigStep ? 1000 : bigStep; }

std::vector<game::Order> conversionOrders(const std::vector<ConversionLine>& lines) {
    std::vector<game::Order> out;
    for (const ConversionLine& l : lines) {
        const auto part = game::economy::conversionOrders(l.from, l.to, l.amount);
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

bool canConvertAt(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::ObjectId planet) {
    const game::Colony* c = s.colony(planet);
    return c && c->owner == viewer && game::economy::colonyConverts(r, s, *c);
}

// ---- The Ships\Units list's columns ------------------------------------------------------------

std::vector<ShipColumn> shipTabColumns(ShipsTab tab) {
    using C = ShipColumn;
    switch (tab) {
        case ShipsTab::General: return {C::Size, C::Type, C::Movement, C::Damage, C::Supplies};
        case ShipsTab::Orders: return {C::Class, C::Orders};
        case ShipsTab::Cargo: return {C::CargoSpace, C::CargoMax, C::CargoList};
        case ShipsTab::Fleet: return {C::Experience, C::Fleet};
        case ShipsTab::Maintenance: return {C::MineralsMaintenance, C::OrganicsMaintenance, C::RadioactivesMaintenance};
        case ShipsTab::Count: break;
    }
    return {};
}

std::optional<ShipColumn> shipColumnOf(int key) {
    if (key < 0 || key >= static_cast<int>(ShipColumn::Count)) return std::nullopt;
    return static_cast<ShipColumn>(key);
}

std::pair<int, int> destroyedComponents(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    if (!v.design.valid() || v.design.index() >= s.designs.size()) return {0, 0};
    const game::Design& d = s.design(v.design);
    int destroyed = 0;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (!game::entryIntact(r, s, v, i)) ++destroyed;
    return {destroyed, static_cast<int>(d.entries.size())};
}

ShipSortValues shipSortValues(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    ShipSortValues out;
    out.name = v.name;
    const bool valid = v.design.valid() && v.design.index() < s.designs.size();
    const bool unit = isUnitVehicle(r, s, v);
    if (valid) {
        const game::Design& d = s.design(v.design);
        out.hullNumber = unit ? 100 + std::max(1, v.count) : static_cast<int>(d.hull);
        out.type = d.designType;
        out.designName = d.name;
    }
    out.movement = v.movement;
    out.destroyed = destroyedComponents(r, s, v).first;
    out.supplies = game::vehicleHasUnlimitedSupply(r, s, v) ? kUnlimitedSupplyKey : v.supply;
    out.cargoUsed = game::cargoSpaceUsed(r, s, v.cargo);
    out.cargoCapacity = game::vehicleCargoCapacity(r, s, v);
    out.experience = int64_t{v.experience} * 10 + v.experienceTenths;
    if (const game::Fleet* f = s.fleet(v.fleet)) out.fleetNumber = static_cast<int>(f->id.index()) + 1;
    out.maintenance = vehicleMaintenance(r, s, v);
    return out;
}

namespace {

template <class T>
int ascending(const T& a, const T& b) {
    return a == b ? 0 : a < b ? -1 : 1;
}
// By character code (case matters), A to Z.
int byCode(const std::string& a, const std::string& b) {
    const int d = a.compare(b);
    return d == 0 ? 0 : d < 0 ? -1 : 1;
}

} // namespace

int compareShips(ShipColumn c, const ShipSortValues& a, const ShipSortValues& b) {
    using C = ShipColumn;
    switch (c) {
        case C::Picture:
        case C::Size: return ascending(a.hullNumber, b.hullNumber);
        case C::Name: return compareNames(a.name, b.name);
        case C::Type: return byCode(a.type, b.type);
        case C::Movement: return -ascending(a.movement, b.movement);
        case C::Damage: return -ascending(a.destroyed, b.destroyed);
        case C::Supplies: return ascending(a.supplies, b.supplies);
        case C::Class: return byCode(a.designName, b.designName);
        case C::Orders: return byCode(a.orders, b.orders);
        case C::CargoSpace: return -ascending(a.cargoUsed, b.cargoUsed);
        case C::CargoMax: return -ascending(a.cargoCapacity, b.cargoCapacity);
        case C::CargoList: return byCode(a.cargo, b.cargo);
        case C::Experience: return ascending(a.experience, b.experience);
        case C::Fleet: return -ascending(a.fleetNumber, b.fleetNumber);
        case C::MineralsMaintenance:
        case C::OrganicsMaintenance:
        case C::RadioactivesMaintenance: {
            const size_t i = static_cast<size_t>(c) - static_cast<size_t>(C::MineralsMaintenance);
            return -ascending(a.maintenance.v[i], b.maintenance.v[i]);
        }
        case C::Count: break;
    }
    return 0;
}

std::vector<size_t> shipRowOrder(const std::vector<ShipSortValues>& rows, const std::array<uint8_t, 5>& slots) {
    std::vector<size_t> order(rows.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    sortByKeys(order, sortKeys(slots, static_cast<int>(ShipColumn::Name)), [&](int key, size_t a, size_t b) {
        const std::optional<ShipColumn> c = shipColumnOf(key);
        return c ? compareShips(*c, rows[a], rows[b]) : 0;
    });
    return order;
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

bool canSelfDestruct(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) { return game::movement::canSelfDestruct(r, s, v); }

ScrapWindowState scrapWindowState(const game::Rules& r, const game::GameState& s, game::EmpireId e,
                                  const std::vector<const game::Vehicle*>& selection) {
    ScrapWindowState out;
    std::vector<const game::Vehicle*> sel;
    for (const game::Vehicle* v : selection)
        if (v) sel.push_back(v);
    if (sel.empty()) return out;
    using game::ScrapAction;
    auto every = [&](ScrapAction a) {
        return std::all_of(sel.begin(), sel.end(), [&](const game::Vehicle* v) { return game::scrapActionProblem(r, s, e, *v, a).empty(); });
    };
    out.scrap = every(ScrapAction::Scrap);
    out.analyze = every(ScrapAction::Analyze);
    out.mothball = every(ScrapAction::Mothball);
    out.unmothball = every(ScrapAction::Unmothball);
    out.selfDestruct = out.canSelfDestruct = every(ScrapAction::SelfDestruct);
    // Each sees the others: armed companions may be selected too (spec 03 §15).
    out.fireOn = out.canBeFiredOn = every(ScrapAction::FireOn);
    out.retrofit = std::all_of(sel.begin(), sel.end(), [&](const game::Vehicle* v) {
        return game::scrapListed(*v, e) && v->design == sel.front()->design && game::scrapYardAt(r, s, e, v->location);
    });
    // The last selected vehicle's word; "None" when any cannot be analyzed.
    if (out.analyze) out.researchPotential = game::researchPotentialWord(game::analyzePairs(r, s, e, *sel.back()).size());
    return out;
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
        case StellarAction::CreatePlanet: c.reason = "A new planet replaces the asteroid field."; break;
        case StellarAction::DestroyPlanet: c.reason = "A new asteroid field replaces the planet; its colony is lost."; break;
        case StellarAction::CreateStar: c.reason = "A new star forms in this sector."; break;
        case StellarAction::DestroyStar:
            c.destroysSystem = true;
            c.reason = "The shockwave turns every planet into an asteroid field and destroys everything else in the system but warp points, this ship included.";
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
