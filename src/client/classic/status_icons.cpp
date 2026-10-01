#include "client/classic/status_icons.hpp"

#include "game/abilities.hpp"
#include "game/design.hpp"
#include "game/query.hpp"

#include <algorithm>
#include <array>

namespace opense4::client::classic {

namespace {

using game::AbilityKind;
using ruleset::VehicleType;
namespace cell = status_cell;

// The seven cargo cells, in the order they are drawn.
void cargoCells(const game::Rules& r, const game::GameState& s, const game::Cargo& c, std::vector<int>& out) {
    auto holds = [&](VehicleType t) {
        return std::any_of(c.units.begin(), c.units.end(),
                           [&](const game::UnitStack& u) { return u.count > 0 && r.hull(s.design(u.design).hull).type == t; });
    };
    const std::array<std::pair<VehicleType, int>, 6> units{{{VehicleType::Troop, cell::kTroops},
                                                            {VehicleType::Fighter, cell::kFighters},
                                                            {VehicleType::Mine, cell::kMines},
                                                            {VehicleType::Satellite, cell::kSatellites},
                                                            {VehicleType::WeaponPlatform, cell::kPlatforms},
                                                            {VehicleType::Drone, cell::kDrones}}};
    for (const auto& [type, c2] : units)
        if (holds(type)) out.push_back(c2);
    if (c.totalPopulation() > 0) out.push_back(cell::kPopulation);
}

bool damagedParts(const game::Vehicle& v) {
    return std::any_of(v.damage.begin(), v.damage.end(), [](int d) { return d > 0; });
}

bool remoteMiner(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    const auto abilities = game::vehicleAbilities(r, s, v);
    return game::hasAbility(abilities, AbilityKind::RemoteResourceGenMinerals) || game::hasAbility(abilities, AbilityKind::RemoteResourceGenOrganics) ||
           game::hasAbility(abilities, AbilityKind::RemoteResourceGenRadioactives);
}

// A repair source of the vehicle's owner in its sector: a colony or a vehicle with Component Repair.
bool repairSourceHere(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    for (game::ObjectId p : game::planetsAt(s, v.location))
        if (const game::Colony* c = s.colony(p); c && c->owner == v.owner && game::hasAbility(game::colonyAbilities(r, s, *c), AbilityKind::ComponentRepair))
            return true;
    for (const game::Vehicle& o : s.vehicles)
        if (o.owner == v.owner && o.location == v.location && game::hasAbility(game::vehicleAbilities(r, s, o), AbilityKind::ComponentRepair))
            return true;
    return false;
}

} // namespace

std::vector<int> vehicleStatusCells(const game::Rules& r, const game::GameState& s, const game::Vehicle& v) {
    std::vector<int> out;
    const bool mothballed = v.status == game::VehicleStatus::Mothballed;
    const bool cloaked = v.status == game::VehicleStatus::Cloaked;
    const VehicleType type = game::vehicleType(r, s, v);
    // 1. Supplies: out at 0, else low strictly below the warning level; not for
    // mothballed ships, with no exception for ships that use no supply or have
    // unlimited supply. A fighter group is low below a tenth of the level, and
    // only while it holds a fighter (§7 Q54). Other unit groups carry no
    // supplies, so they show neither (inferred).
    const int64_t warning = r.setting("Supply Amount for Low Supply Warning", 1000);
    if (!mothballed && (!game::isUnitType(type) || type == VehicleType::Fighter)) {
        const bool fighters = type == VehicleType::Fighter;
        if (v.supply <= 0) out.push_back(cell::kNoSupply);
        else if (fighters ? v.count >= 1 && v.supply < warning / 10 : v.supply < warning) out.push_back(cell::kLowSupply);
    }
    // 2-3. Damage, and a repair source in the sector.
    if (damagedParts(v)) {
        out.push_back(cell::kDamaged);
        if (repairSourceHere(r, s, v)) out.push_back(cell::kRepairHere);
    }
    // 4. Sentry first.
    if (!v.orders.empty() && v.orders.front().kind == game::OrderKind::Sentry) out.push_back(cell::kSentry);
    // 5. Cloaked.
    if (cloaked) out.push_back(cell::kCloaked);
    // 6. A working space yard (not cloaked; a mothballed yard ship still shows
    // it), else repair (§7 Q50).
    const bool yard = game::vehicleHasSpaceYard(r, s, v) && !cloaked;
    if (yard) out.push_back(cell::kSpaceYard);
    else if (game::hasAbility(game::vehicleAbilities(r, s, v), AbilityKind::ComponentRepair)) out.push_back(cell::kCanRepair);
    // 7-9.
    if (v.repeatOrders) out.push_back(cell::kRepeatOrders);
    if (v.minister) out.push_back(cell::kMinister);
    if (mothballed) out.push_back(cell::kMothballed);
    // 10. Building: not mothballed, a working yard, an item in the queue (held or not).
    if (yard && !mothballed && !v.queue.items.empty()) out.push_back(cell::kBuilding);
    // 11. Cargo.
    cargoCells(r, s, v.cargo, out);
    // 12. Remote mining: in a sector with an uncolonized planet or asteroid
    // field, only the first object there with the ability, whatever its owner
    // (in the game's vehicle order; §4.4, §7 Q50).
    if (remoteMiner(r, s, v)) {
        bool target = false;
        for (game::ObjectId p : game::planetsAt(s, v.location)) target = target || !s.colony(p);
        bool first = true;
        for (const game::Vehicle& o : s.vehicles) {
            if (o.id == v.id) break;
            if (o.location == v.location && o.count > 0 && remoteMiner(r, s, o)) first = false;
        }
        if (target && first) out.push_back(cell::kRemoteMining);
    }
    return out;
}

std::vector<int> colonyStatusCells(const game::Rules& r, const game::GameState& s, const game::Colony& c, bool connected) {
    std::vector<int> out;
    const auto abilities = game::colonyAbilities(r, s, c);
    const bool yard = game::colonyHasSpaceYard(r, c);
    // Colonies do not cloak in our engine (cell 9 would come first).
    if (yard) out.push_back(cell::kSpaceYard);
    if (c.minister) out.push_back(cell::kMinister);
    // Building: every colony builds (facilities at least), so a non-empty queue (inferred).
    if (!c.queue.items.empty()) out.push_back(cell::kBuilding);
    if (game::hasAbility(abilities, AbilityKind::AncientRuins) || game::hasAbility(abilities, AbilityKind::AncientRuinsUnique))
        out.push_back(cell::kRuins);
    if (!game::breathable(s, c)) out.push_back(cell::kDomed);
    if (!yard && game::hasAbility(abilities, AbilityKind::ComponentRepair)) out.push_back(cell::kCanRepair);
    cargoCells(r, s, c.cargo, out);
    if (!connected) out.push_back(cell::kNotConnected);
    return out;
}

std::vector<int> planetStatusCells(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::ObjectId planet) {
    if (const game::Colony* c = s.colony(planet); c && c->owner == viewer) {
        // The not-connected icon needs the economy's answer; callers that have it use colonyStatusCells.
        return colonyStatusCells(r, s, *c, true);
    }
    std::vector<int> out;
    if (!planet.valid() || planet.index() >= s.galaxy.objects.size()) return out;
    const auto abilities = game::parseAbilities(s.galaxy.object(planet).abilities);
    if (game::hasAbility(abilities, AbilityKind::AncientRuins) || game::hasAbility(abilities, AbilityKind::AncientRuinsUnique))
        out.push_back(cell::kRuins);
    return out;
}

std::vector<int> fleetStatusCells(const game::GameState& s, const game::Fleet& f) {
    std::vector<int> out;
    if (f.minister) out.push_back(cell::kMinister);
    // Cloaked: any member belonging to the fleet's owner (§4.4).
    for (game::VehicleId id : f.members)
        if (const game::Vehicle* v = s.vehicle(id); v && v->owner == f.owner && v->status == game::VehicleStatus::Cloaked) {
            out.push_back(cell::kCloaked);
            break;
        }
    return out;
}

} // namespace opense4::client::classic
