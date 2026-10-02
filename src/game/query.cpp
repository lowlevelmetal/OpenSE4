#include "game/query.hpp"

#include "datafile/datafile.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>
#include <string>

namespace opense4::game {

std::vector<ObjectId> planetsAt(const GameState& s, Location where) {
    std::vector<ObjectId> out;
    if (!where.system.valid() || where.system.index() >= s.galaxy.systems.size()) return out;
    for (ObjectId o : s.galaxy.system(where.system).objects) {
        const SpaceObject& obj = s.galaxy.object(o);
        if ((obj.kind == ObjectKind::Planet || obj.kind == ObjectKind::Asteroids) && obj.sector == where.sector) out.push_back(o);
    }
    return out;
}

uint64_t objectOrderKey(const GameState& s, ObjectId planet) { return s.galaxy.object(planet).slot; }

uint64_t objectOrderKey(const Vehicle& v) { return v.slot; }

std::string fleetJoinProblem(const Rules& r, const GameState& s, const Vehicle& v) {
    switch (vehicleType(r, s, v)) {
        case ruleset::VehicleType::Ship:
        case ruleset::VehicleType::Fighter: return {};
        case ruleset::VehicleType::Base: return r.settingFlag("Bases Can Join Fleets", false) ? std::string{} : std::string("Bases cannot join fleets");
        default: return std::format("{} cannot join a fleet", v.name);
    }
}

const Colony* ownColonyAt(const GameState& s, EmpireId empire, Location where) {
    for (ObjectId o : planetsAt(s, where))
        if (const Colony* c = s.colony(o); c && c->owner == empire) return c;
    return nullptr;
}

std::vector<ParsedAbility> colonyAbilities(const Rules& r, const GameState& s, const Colony& c) {
    std::vector<ParsedAbility> out;
    for (const auto& a : s.galaxy.object(c.planet).abilities) out.push_back(parseAbility(a));
    for (uint32_t f : c.facilities)
        for (const auto& a : r.facilityAbilities(f)) out.push_back(a);
    return out;
}

bool colonyHasSpaceYard(const Rules& r, const Colony& c) {
    return std::any_of(c.facilities.begin(), c.facilities.end(),
                       [&](uint32_t f) { return hasAbility(r.facilityAbilities(f), AbilityKind::SpaceYard); });
}

bool colonyHasWorkingYard(const Rules& r, const Colony& c) { return !c.cloaked && colonyHasSpaceYard(r, c); }

bool vehicleHasSpaceYard(const Rules& r, const GameState& s, const Vehicle& v) {
    if (v.status == VehicleStatus::Mothballed) return false;
    const Design& d = s.design(v.design);
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (entryIntact(r, s, v, i) && hasAbility(r.componentAbilities(d.entries[i].component), AbilityKind::SpaceYard)) return true;
    return false;
}

bool spaceYardAt(const Rules& r, const GameState& s, EmpireId empire, Location where) {
    if (const Colony* c = ownColonyAt(s, empire, where); c && colonyHasWorkingYard(r, *c)) return true;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == empire && v.location == where && vehicleHasSpaceYard(r, s, v)) return true;
    return false;
}

int reclamationPercentAt(const Rules& r, const GameState& s, EmpireId empire, Location where) {
    int64_t best = 0;
    if (const Colony* c = ownColonyAt(s, empire, where)) best = bestValue1(colonyAbilities(r, s, *c), AbilityKind::ResourceReclamation);
    for (const Vehicle& v : s.vehicles)
        if (v.owner == empire && v.location == where)
            best = std::max(best, bestValue1(vehicleAbilities(r, s, v), AbilityKind::ResourceReclamation));
    return static_cast<int>(best);
}

const ruleset::PlanetSize* planetSize(const Rules& r, const SpaceObject& planet, bool) {
    const auto& sizes = r.data().planetSizes;
    for (const auto& ps : sizes)
        if (datafile::keysEqual(ps.name, planet.size)) return &ps;
    const std::string_view physical = planet.kind == ObjectKind::Asteroids ? "Asteroids" : "Planet";
    for (const auto& ps : sizes)
        if (!ps.constructed && datafile::keysEqual(ps.physicalType, physical) && datafile::keysEqual(ps.stellarSize, planet.size))
            return &ps;
    return nullptr;
}

bool breathable(const GameState& s, const Colony& c) {
    const SpaceObject& planet = s.galaxy.object(c.planet);
    if (c.population.empty()) return datafile::keysEqual(s.empire(c.owner).race.atmosphere, planet.atmosphere);
    for (const PopulationGroup& p : c.population)
        if (p.race.valid() && !datafile::keysEqual(s.empire(p.race).race.atmosphere, planet.atmosphere)) return false;
    return true;
}

namespace {

// The `Planet Storage Space` trait: (100 + Val1) % of a colony's facility slots,
// population and cargo, truncated (spec 02 §2, confirmed: binary).
int64_t withStorageTrait(const Rules& r, const GameState& s, const Colony& c, int64_t amount) {
    const int64_t pct = r.traitValue(s.empire(c.owner).race, "Planet Storage Space");
    return pct == 0 ? amount : xmath::pctTrunc(amount, 100 + pct);
}

} // namespace

int facilitySlots(const Rules& r, const GameState& s, const Colony& c) {
    const ruleset::PlanetSize* ps = planetSize(r, s.galaxy.object(c.planet));
    if (!ps) return 0;
    return static_cast<int>(withStorageTrait(r, s, c, breathable(s, c) ? ps->maxFacilities : ps->maxFacilitiesDomed));
}

int64_t maxPopulation(const Rules& r, const GameState& s, const Colony& c) {
    const ruleset::PlanetSize* ps = planetSize(r, s.galaxy.object(c.planet));
    if (!ps) return 0;
    return withStorageTrait(r, s, c, breathable(s, c) ? ps->maxPopulation : ps->maxPopulationDomed);
}

int64_t colonyCargoCapacity(const Rules& r, const GameState& s, const Colony& c) {
    const ruleset::PlanetSize* ps = planetSize(r, s.galaxy.object(c.planet));
    int64_t total = ps ? (breathable(s, c) ? ps->maxCargo : ps->maxCargoDomed) : 0;
    for (const ruleset::Ability& a : s.galaxy.object(c.planet).abilities)
        if (const ParsedAbility p = parseAbility(a); p.kind == AbilityKind::CargoStorage) total += p.value1;  // the planet's own
    for (uint32_t f : c.facilities) total += sumValue1(r.facilityAbilities(f), AbilityKind::CargoStorage);
    return withStorageTrait(r, s, c, total);
}

bool hostile(const GameState& s, EmpireId a, EmpireId b) {
    if (a == b || !a.valid() || !b.valid()) return false;
    return treatyIsHostile(s.empire(a).relation(b).treaty);
}

bool allied(const GameState& s, EmpireId a, EmpireId b) {
    if (a == b) return true;
    if (!a.valid() || !b.valid()) return false;
    return treatyAllowsResupply(s.empire(a).relation(b).treaty);
}

// Everything except ships and bases is a unit (spec 03 §1).
bool isUnitType(ruleset::VehicleType t) { return t != ruleset::VehicleType::Ship && t != ruleset::VehicleType::Base; }

int shipCount(const Rules& r, const GameState& s, EmpireId e) {
    int n = 0;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && !isUnitType(vehicleType(r, s, v))) ++n;
    return n;
}

int unitCount(const Rules& r, const GameState& s, EmpireId e) {
    int n = 0;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && isUnitType(vehicleType(r, s, v))) n += v.count;
    auto cargo = [&](const Cargo& c) {
        for (const UnitStack& u : c.units) n += u.count;
    };
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e) cargo(v.cargo);
    for (const auto& c : s.colonies)
        if (c && c->owner == e) cargo(c->cargo);
    return n;
}

DesignId addDesign(GameState& s, Design d) {
    d.id = DesignId{s.designs.size()};
    const DesignId id = d.id;
    if (d.owner.valid()) s.empire(d.owner).designs.push_back(id);
    s.designs.push_back(std::move(d));
    return id;
}

} // namespace opense4::game
