#include "sim/rules.hpp"

#include "core/hash.hpp"

#include <format>

namespace opense4::sim {

bool meetsRequirements(const Empire& e, std::span<const TechRequirement> reqs) {
    for (const auto& r : reqs)
        if (e.techLevel(r.tech) < r.level) return false;
    return true;
}

bool canResearch(const Content& c, const Empire& e, TechIndex t) {
    const TechDef& def = c.tech(t);
    return e.techLevel(t) < def.maxLevel && meetsRequirements(e, def.prerequisites);
}

int64_t nextLevelCost(const Content& c, const Empire& e, TechIndex t) {
    return c.tech(t).costForLevel(e.techLevel(t) + 1);
}

bool isAvailable(const Content& c, const Empire& e, HullIndex h) { return meetsRequirements(e, c.hull(h).prerequisites); }
bool isAvailable(const Content& c, const Empire& e, ComponentIndex comp) {
    return meetsRequirements(e, c.component(comp).prerequisites);
}
bool isAvailable(const Content& c, const Empire& e, FacilityIndex f) {
    return meetsRequirements(e, c.facility(f).prerequisites);
}

DesignStats computeDesignStats(const Content& c, HullIndex hullIndex, std::span<const ComponentIndex> components) {
    DesignStats st;
    const HullDef& hull = c.hull(hullIndex);
    st.tonnageMax = hull.size;
    st.structure = hull.structure;
    st.cost = hull.cost;

    int thrust = 0;
    std::array<int, enumIndex(AbilityType::Count)> abilityTotals{};
    for (ComponentIndex ci : components) {
        const ComponentDef& comp = c.component(ci);
        st.tonnageUsed += comp.tonnage;
        st.structure += comp.structure;
        st.cost += comp.cost;
        for (const Ability& a : comp.abilities) {
            abilityTotals[enumIndex(a.type)] += a.amount;
            if (a.type == AbilityType::Colonize) {
                if (auto surface = parsePlanetSurface(a.param); surface && !st.canColonize(*surface))
                    st.colonizes.push_back(*surface);
            }
        }
        if (comp.hasAbility(AbilityType::Movement)) {
            ++st.engines;
            thrust += comp.abilityTotal(AbilityType::Movement);
        }
        if (comp.weapon) {
            ++st.weaponCount;
            st.weaponDamage += comp.weapon->damage;
        }
    }
    st.speed = thrust / hull.enginesPerMove;
    st.shields = abilityTotals[enumIndex(AbilityType::ShieldGeneration)];
    st.supply = abilityTotals[enumIndex(AbilityType::SupplyStorage)];
    st.cargo = abilityTotals[enumIndex(AbilityType::CargoStorage)];
    st.sensor = abilityTotals[enumIndex(AbilityType::Sensor)];

    if (st.tonnageUsed > st.tonnageMax)
        st.problems.push_back(std::format("Over tonnage: {} / {} kT", st.tonnageUsed, st.tonnageMax));
    if (st.engines > hull.maxEngines)
        st.problems.push_back(std::format("Too many engines: {} / {}", st.engines, hull.maxEngines));
    for (AbilityType required : c.rules.requiredShipAbilities)
        if (abilityTotals[enumIndex(required)] <= 0)
            st.problems.push_back(std::format("Missing required component: {}", displayName(required)));
    return st;
}

bool canBreathe(const Empire& e, const Planet& p) { return p.atmosphere == e.breathes; }

int64_t maxPopulation(const Content& c, const Empire& e, const Planet& p) {
    const int64_t base = c.rules.maxPopulation[enumIndex(p.size)];
    return canBreathe(e, p) ? base : base * c.rules.hostileAtmospherePopPercent / 100;
}

int facilitySlots(const Content& c, const Planet& p) { return c.rules.facilitySlots[enumIndex(p.size)]; }

int usedFacilitySlots(const Colony& col) {
    int queued = 0;
    for (const auto& item : col.queue)
        if (item.kind == ConstructionKind::Facility) ++queued;
    return static_cast<int>(col.facilities.size()) + queued;
}

bool hasSpaceYard(const Content& c, const Colony& col) {
    for (FacilityIndex f : col.facilities)
        if (c.facility(f).abilityTotal(AbilityType::SpaceYard) > 0) return true;
    return false;
}

int64_t constructionRate(const Content& c, const Colony& col) {
    int64_t yards = 0;
    for (FacilityIndex f : col.facilities) yards += c.facility(f).abilityTotal(AbilityType::SpaceYard);
    return std::max(yards, c.rules.baseColonyConstructionRate);
}

ColonyOutput colonyOutput(const Content& c, const GameState& s, const Planet& p) {
    ColonyOutput out;
    if (!p.colony) return out;
    const Colony& col = *p.colony;
    const int64_t maxPop = std::max<int64_t>(1, maxPopulation(c, s.empire(col.owner), p));
    out.efficiencyPercent = static_cast<int>(50 + 50 * std::min(col.population, maxPop) / maxPop);

    static constexpr std::array<std::pair<AbilityType, ResourceType>, 3> kProducers{{
        {AbilityType::ProduceMinerals, ResourceType::Minerals},
        {AbilityType::ProduceOrganics, ResourceType::Organics},
        {AbilityType::ProduceRadioactives, ResourceType::Radioactives},
    }};
    for (FacilityIndex f : col.facilities) {
        const FacilityDef& fac = c.facility(f);
        for (const auto& [ability, resource] : kProducers) {
            const int64_t amount = fac.abilityTotal(ability);
            if (amount > 0)
                out.resources[resource] += amount * p.value[enumIndex(resource)] / 100 * out.efficiencyPercent / 100;
        }
        out.research += int64_t{fac.abilityTotal(AbilityType::ProduceResearch)} * out.efficiencyPercent / 100;
    }
    return out;
}

std::string colonizeProblem(const GameState& s, const Ship& ship, const Planet& planet) {
    const DesignStats& st = s.statsOf(ship);
    if (st.colonizes.empty()) return "This ship has no colony module.";
    if (!st.canColonize(planet.surface))
        return std::format("This ship cannot colonize {} planets.", displayName(planet.surface));
    if (planet.colony) return "The planet is already colonized.";
    return {};
}

const WarpPoint* warpPointAt(const GameState& s, Location loc) {
    for (WarpPointId id : s.system(loc.system).warpPoints) {
        const WarpPoint& wp = s.warpPoint(id);
        if (wp.sector == loc.sector) return &wp;
    }
    return nullptr;
}

const Planet* planetAt(const GameState& s, Location loc) {
    for (PlanetId id : s.system(loc.system).planets) {
        const Planet& p = s.planet(id);
        if (p.sector == loc.sector) return &p;
    }
    return nullptr;
}

std::vector<const Ship*> shipsAt(const GameState& s, Location loc) {
    std::vector<const Ship*> out;
    for (const Ship& ship : s.ships)
        if (ship.location == loc) out.push_back(&ship);
    return out;
}

std::vector<const Ship*> shipsInSystem(const GameState& s, SystemId sys) {
    std::vector<const Ship*> out;
    for (const Ship& ship : s.ships)
        if (ship.location.system == sys) out.push_back(&ship);
    return out;
}

bool atWar(const GameState&, EmpireId a, EmpireId b) {
    // Diplomacy is not implemented yet: every empire is hostile to every other.
    return a != b;
}

bool hasPresence(const GameState& s, EmpireId e, SystemId sys) {
    for (const Ship& ship : s.ships)
        if (ship.owner == e && ship.location.system == sys) return true;
    for (PlanetId pid : s.system(sys).planets) {
        const Planet& p = s.planet(pid);
        if (p.colony && p.colony->owner == e) return true;
    }
    return false;
}

int64_t totalPopulation(const GameState& s, EmpireId e) {
    int64_t total = 0;
    for (const Planet& p : s.planets)
        if (p.colony && p.colony->owner == e) total += p.colony->population;
    return total;
}

int colonyCount(const GameState& s, EmpireId e) {
    int n = 0;
    for (const Planet& p : s.planets)
        if (p.colony && p.colony->owner == e) ++n;
    return n;
}

int shipCount(const GameState& s, EmpireId e) {
    int n = 0;
    for (const Ship& ship : s.ships)
        if (ship.owner == e) ++n;
    return n;
}

uint64_t stateChecksum(const GameState& s) {
    // Only fixed-width values are hashed, so checksums match across platforms.
    Hasher h;
    auto count = [&](size_t n) { h.add(static_cast<uint64_t>(n)); };
    auto location = [&](Location l) { h.add(l.system.value).add(l.sector.x).add(l.sector.y); };
    auto resources = [&](const Resources& r) {
        for (int64_t v : r.amounts) h.add(v);
    };

    h.add(s.turn).add(s.nextShipId);
    for (int i = 0; i < 4; ++i) h.add(s.rng.rawState()[i]);
    for (const Planet& p : s.planets) {
        h.add(p.id.value).add(p.colony.has_value());
        if (!p.colony) continue;
        h.add(p.colony->owner.value).add(p.colony->population).add(p.colony->foundedTurn);
        count(p.colony->facilities.size());
        for (FacilityIndex f : p.colony->facilities) h.add(f.value);
        count(p.colony->queue.size());
        for (const auto& item : p.colony->queue) {
            h.add(item.kind).add(item.design.value).add(item.facility.value);
            resources(item.cost);
            resources(item.spent);
        }
    }
    count(s.ships.size());
    for (const Ship& ship : s.ships) {
        h.add(ship.id.value).add(ship.owner.value).add(ship.design.value).add(ship.name);
        location(ship.location);
        h.add(ship.damage).add(ship.movesLeft).add(ship.order.type).add(ship.order.planet.value);
        location(ship.order.destination);
        count(ship.path.size());
        for (const Location& l : ship.path) location(l);
    }
    for (const Empire& e : s.empires) {
        h.add(e.id.value).add(e.alive).add(e.lastResearch).add(e.unspentResearch);
        resources(e.stockpile);
        resources(e.lastIncome);
        for (int lvl : e.techLevels) h.add(lvl);
        for (int64_t p : e.techProgress) h.add(p);
        count(e.researchQueue.size());
        for (TechIndex t : e.researchQueue) h.add(t.value);
        for (uint8_t x : e.explored) h.add(x);
        count(e.designs.size());
    }
    count(s.designs.size());
    for (const Design& d : s.designs) {
        h.add(d.id.value).add(d.owner.value).add(d.hull.value).add(d.obsolete).add(d.built).add(d.name);
        count(d.components.size());
        for (ComponentIndex ci : d.components) h.add(ci.value);
    }
    return h.value();
}

} // namespace opense4::sim
