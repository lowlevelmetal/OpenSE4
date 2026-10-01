// The combat simulator (docs/spec/04 §17): a sandbox copy of the game with
// virtual empires and the chosen designs and planets, fought as a tactical
// or strategic battle. See simulator.hpp.

#include "game/simulator.hpp"

#include "game/combat.hpp"
#include "game/combat_detail.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/setup.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <map>
#include <tuple>

namespace opense4::game::combat {

namespace {

using ruleset::VehicleType;

VehicleType hullType(const Rules& r, const GameState& s, DesignId d) { return r.hull(s.design(d).hull).type; }

bool validDesign(const GameState& s, DesignId d) { return d.valid() && d.index() < s.designs.size(); }

// Designs that can stand on the combat map (no mines, troops or platforms).
bool fieldable(VehicleType t) { return t != VehicleType::Mine && t != VehicleType::Troop && t != VehicleType::WeaponPlatform; }

bool usable(const GameState& s, EmpireId viewer, DesignId d) {
    if (!validDesign(s, d)) return false;
    const Design& design = s.design(d);
    return design.owner == viewer || knowsDesign(s.empire(viewer).knowledge, d);
}

SystemId homeSystem(const GameState& s, EmpireId viewer) {
    if (viewer.valid() && viewer.index() < s.empires.size() && s.empire(viewer).homeSystem.index() < s.galaxy.systems.size()) return s.empire(viewer).homeSystem;
    for (const auto& c : s.colonies)
        if (c && c->owner == viewer && c->homeworld) return s.galaxy.object(c->planet).system;
    return {};
}

// The home planet's sector, where the battle stands (spec 04 §17).
std::optional<Location> homeSector(const GameState& s, EmpireId viewer) {
    for (const auto& c : s.colonies)
        if (c && c->owner == viewer && c->homeworld) return locationOf(s.galaxy, c->planet);
    return std::nullopt;
}

// Start positions by side number, as if arriving from a neighbouring sector
// (confirmed: binary): 1 north, 2 south, 3 west, 4 east, 5 north-west, 6
// south-west, 7 north-east, 8 south-east; sides 9 and 10 start in the middle.
constexpr std::array<std::pair<int, int>, 8> kSideArrival{{{0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}}};

} // namespace

std::vector<DesignId> simulatorDesigns(const Rules& r, const GameState& s, EmpireId viewer, bool hideObsolete) {
    std::vector<DesignId> out;
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return out;
    auto consider = [&](DesignId d) {
        if (!validDesign(s, d) || (hideObsolete && s.design(d).obsolete) || !fieldable(hullType(r, s, d))) return;
        if (std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
    };
    for (DesignId d : s.empire(viewer).designs) consider(d);
    for (DesignId d : seenDesignIds(s.empire(viewer).knowledge)) consider(d);
    return out;
}

std::vector<DesignId> simulatorCargoDesigns(const Rules& r, const GameState& s, EmpireId viewer, bool hideObsolete) {
    std::vector<DesignId> out;
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return out;
    for (DesignId d : s.empire(viewer).designs) {
        if (!validDesign(s, d) || (hideObsolete && s.design(d).obsolete)) continue;
        const VehicleType t = hullType(r, s, d);
        if (isUnitType(t) && t != VehicleType::Mine) out.push_back(d);
    }
    return out;
}

std::vector<ObjectId> simulatorPlanets(const GameState& s, EmpireId viewer) {
    std::vector<ObjectId> out;
    const SystemId home = homeSystem(s, viewer);
    if (!home.valid()) return out;
    for (ObjectId o : s.galaxy.system(home).objects)
        if (const Colony* c = s.colony(o); c && c->owner == viewer) out.push_back(o);
    return out;
}

int64_t simulatorCargoCapacity(const Rules& r, const GameState& s, const SimulatorItem& item) {
    if (item.kind == SimulatorItem::Kind::Planet) {
        const Colony* c = s.colony(item.planet);
        return c ? colonyCargoCapacity(r, s, *c) : 0;
    }
    if (!validDesign(s, item.design)) return 0;
    Vehicle v;
    v.design = item.design;
    v.damage.assign(s.design(item.design).entries.size(), 0);
    return vehicleCargoCapacity(r, s, v);
}

int64_t simulatorCargoUsed(const Rules& r, const GameState& s, const SimulatorItem& item) {
    Cargo c;
    if (item.kind == SimulatorItem::Kind::Planet && !item.replaceCargo) {
        if (const Colony* colony = s.colony(item.planet)) c.units = colony->cargo.units;
    } else {
        c.units = item.cargo;
    }
    return cargoSpaceUsed(r, s, c);
}

std::string simulatorProblem(const Rules& r, const GameState& s, const SimulatorSetup& setup) {
    const EmpireId viewer = setup.viewer;
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return "No empire to simulate for.";
    if (setup.sides.size() < 2) return "A battle needs at least two sides.";
    if (setup.sides.size() > static_cast<size_t>(kSimulatorMaxSides)) return std::format("At most {} sides.", kSimulatorMaxSides);
    const std::vector<ObjectId> samples = simulatorPlanets(s, viewer);
    const size_t strategies = std::max<size_t>(1, s.empire(viewer).strategies.size());
    std::vector<int> used(setup.sides.size(), 0);
    int vehicles = 0;
    for (const SimulatorFleet& f : setup.fleets) {
        if (f.side < 0 || static_cast<size_t>(f.side) >= setup.sides.size()) return std::format("Fleet {} belongs to no side.", f.name);
        if (f.strategy >= strategies) return std::format("Fleet {} has no such strategy.", f.name);
    }
    for (const SimulatorItem& item : setup.items) {
        if (item.side < 0 || static_cast<size_t>(item.side) >= setup.sides.size()) return "An item belongs to no side.";
        if (item.count < 1 || item.count > kSimulatorMaxCount) return std::format("Between 1 and {} of each item.", kSimulatorMaxCount);
        if (item.strategy >= strategies) return "An item has no such strategy.";
        if (item.kind == SimulatorItem::Kind::Planet) {
            if (std::find(samples.begin(), samples.end(), item.planet) == samples.end()) return "Sample planets come from the home system.";
            if (item.count != 1) return "Each sample planet can be used once.";
            if (item.fleet >= 0) return "Planets are not in fleets.";
            for (const SimulatorItem& other : setup.items)
                if (&other != &item && other.kind == SimulatorItem::Kind::Planet && other.planet == item.planet)
                    return "Each sample planet can be used once.";
        } else {
            if (!usable(s, viewer, item.design)) return "Only your designs and enemy designs you have seen can be used.";
            const VehicleType t = hullType(r, s, item.design);
            if (!fieldable(t)) return t == VehicleType::Mine ? "Minefields cannot be added." : "Troops and weapon platforms go in cargo.";
            if (item.fleet >= 0) {
                if (static_cast<size_t>(item.fleet) >= setup.fleets.size() || setup.fleets[static_cast<size_t>(item.fleet)].side != item.side)
                    return "An item's fleet belongs to another side.";
                if (isUnitType(t)) return "Units are not in fleets.";
            }
            vehicles += isUnitType(t) ? 1 : item.count;
        }
        for (const UnitStack& u : item.cargo) {
            if (u.count < 1) return "Cargo needs at least one unit.";
            if (!validDesign(s, u.design) || s.design(u.design).owner != viewer || !isUnitType(hullType(r, s, u.design)) ||
                hullType(r, s, u.design) == VehicleType::Mine)
                return "Cargo holds your own fighters, satellites, drones, troops or weapon platforms.";
        }
        if (!item.cargo.empty() || item.replaceCargo) {
            if (simulatorCargoUsed(r, s, item) > simulatorCargoCapacity(r, s, item)) return "The cargo does not fit.";
        }
        ++used[static_cast<size_t>(item.side)];
    }
    if (vehicles > kSimulatorMaxVehicles) return std::format("At most {} ships and groups.", kSimulatorMaxVehicles);
    if (std::count_if(used.begin(), used.end(), [](int n) { return n > 0; }) < 2) return "At least two sides need something to fight with.";
    return {};
}

Simulation buildSimulation(const Rules& r, const GameState& real, const SimulatorSetup& setup) {
    Simulation sim;
    sim.state = real;
    GameState& sb = sim.state;
    const EmpireId viewer = setup.viewer;
    if (setup.seed != 0) sb.rng = Rng(setup.seed);

    // The home sector's interference and disruption apply; the system modifier
    // totals are never worked out (spec 04 §17). The battle itself is fought in
    // an empty system of the home system's type (for its picture), so that
    // nothing else in the home sector takes part (inferred).
    if (const std::optional<Location> home = homeSector(real, viewer)) {
        sim.interference = detail::sensorInterference(real, *home);
        sim.disruption = detail::shieldDisruption(real, *home);
    }
    StarSystem arena;
    arena.id = SystemId{sb.galaxy.systems.size()};
    arena.name = "Combat Simulator";
    arena.physicalType = "Normal";
    if (const SystemId home = homeSystem(real, viewer); home.valid()) arena.type = real.galaxy.system(home).type;
    else if (!real.galaxy.systems.empty()) arena.type = real.galaxy.systems.front().type;
    sb.galaxy.systems.push_back(arena);
    sim.where = Location{arena.id, Sector{kSystemCenter, kSystemCenter}};

    // One empire per side: a copy of the real empire that owns the side's first
    // item (the viewer's for a planet or an empty side), at war with every
    // other side. Every side uses the viewer's strategies (inferred).
    for (size_t k = 0; k < setup.sides.size(); ++k) {
        EmpireId owner = viewer;
        for (const SimulatorItem& item : setup.items)
            if (item.side == static_cast<int>(k)) {
                if (item.kind == SimulatorItem::Kind::Design && validDesign(real, item.design) && real.design(item.design).owner.valid() &&
                    real.design(item.design).owner.index() < real.empires.size())
                    owner = real.design(item.design).owner;
                break;
            }
        Empire e = real.empire(owner);
        e.strategies = real.empire(viewer).strategies;
        e.id = EmpireId{sb.empires.size()};
        e.name = setup.sides[k].name.empty() ? std::format("Side {}", k + 1) : setup.sides[k].name;
        e.kind = setup.sides[k].computer ? PlayerKind::Computer : PlayerKind::Human;
        e.alive = true;
        e.color = defaultEmpireColor(k);
        e.designs.clear();
        e.log.clear();
        e.historyEvents.clear();
        e.history.clear();
        e.claimedSystems.clear();
        sim.sides.push_back(e.id);
        if (!setup.sides[k].computer) sim.players.push_back(e.id);
        sb.empires.push_back(std::move(e));
    }
    for (Empire& e : sb.empires) {
        e.relations.resize(sb.empires.size());
        Knowledge& known = e.knowledge;
        const size_t systems = sb.galaxy.systems.size();
        if (known.explored.size() < systems) known.explored.resize(systems, 0);
        if (known.present.size() < systems) known.present.resize(systems, 0);
        if (known.lastSeen.size() < systems) known.lastSeen.resize(systems, 0);
        if (known.notes.size() < systems) known.notes.resize(systems);
    }
    for (EmpireId a : sim.sides)
        for (EmpireId b : sim.sides)
            if (a != b) {
                Relation& rel = sb.empire(a).relation(b);
                rel.contact = true;
                rel.treaty = Treaty::War;
            }

    // Each side's own copies of the designs it uses, with the item's strategy.
    std::map<std::tuple<size_t, uint32_t, uint32_t>, DesignId> copies;
    auto copyOf = [&](size_t side, DesignId d, uint32_t strategy) {
        const auto key = std::tuple{side, d.value, strategy};
        if (auto it = copies.find(key); it != copies.end()) return it->second;
        Design c = real.design(d);
        c.owner = sim.sides[side];
        c.strategy = strategy;
        c.obsolete = false;
        c.built = c.lost = 0;
        c.enemyTonnageDestroyed = 0;
        const DesignId id = addDesign(sb, std::move(c));
        copies.emplace(key, id);
        return id;
    };
    auto copyCargo = [&](size_t side, const std::vector<UnitStack>& units) {
        std::vector<UnitStack> out;
        for (const UnitStack& u : units)
            if (u.count > 0) out.push_back({copyOf(side, u.design, 0), u.count});
        return out;
    };

    // Vehicles, in item order; fleets gather their members.
    std::vector<std::vector<VehicleId>> fleetMembers(setup.fleets.size());
    for (const SimulatorItem& item : setup.items) {
        const size_t side = static_cast<size_t>(item.side);
        const EmpireId owner = sim.sides[side];
        if (item.kind == SimulatorItem::Kind::Planet) {
            // The sample planet, moved into the battle sector, colonised by the side.
            SpaceObject obj = real.galaxy.object(item.planet);
            obj.sector = sim.where.sector;
            const ObjectId planet = sb.addObject(std::move(obj), arena.id);
            Colony c = *real.colony(item.planet);
            c.planet = planet;
            c.owner = owner;
            c.orders.clear();
            c.queue = {};
            c.militia = -1;
            c.homeworld = false;
            for (PopulationGroup& g : c.population) g.race = owner;
            c.cargo.units = copyCargo(side, item.replaceCargo ? item.cargo : c.cargo.units);
            sb.colonies[c.planet.index()] = std::move(c);
            continue;
        }
        const DesignId d = copyOf(side, item.design, item.strategy);
        const VehicleType type = r.hull(sb.design(d).hull).type;
        const bool units = isUnitType(type);
        if (units && type != VehicleType::Drone) {
            // Units in space form one group per (owner, kind, sector), mixing designs (spec 03 §12).
            Vehicle* group = nullptr;
            for (Vehicle& g : sb.vehicles)
                if (g.owner == owner && g.location == sim.where && g.count > 0 && vehicleType(r, sb, g) == type) {
                    group = &g;
                    break;
                }
            if (group) {
                addGroupUnits(sb, *group, d, item.count);
                group->supply = initialSupply(r, sb, *group);
                if (item.fleet >= 0) {
                    auto& members = fleetMembers[static_cast<size_t>(item.fleet)];
                    if (std::find(members.begin(), members.end(), group->id) == members.end()) members.push_back(group->id);
                }
                continue;
            }
        }
        const int vehicles = units ? 1 : item.count;
        for (int n = 0; n < vehicles; ++n) {
            Vehicle v;
            v.owner = owner;
            v.design = d;
            v.name = vehicles > 1 ? std::format("{} {}", sb.design(d).name, n + 1) : sb.design(d).name;
            v.location = sim.where;
            v.count = units ? item.count : 1;
            v.damage.assign(sb.design(d).entries.size(), 0);
            v.builtTurn = sb.turn;
            v.cargo.units = copyCargo(side, item.cargo);
            v.supply = initialSupply(r, sb, v);
            const VehicleId id = sb.addVehicle(std::move(v)).id;
            if (item.fleet >= 0) fleetMembers[static_cast<size_t>(item.fleet)].push_back(id);
        }
    }
    // Start positions by side number; a side that owns a planet or a base starts in the middle.
    for (size_t k = 0; k < sim.sides.size() && k < kSideArrival.size(); ++k) {
        bool middle = false;
        for (const SimulatorItem& item : setup.items)
            if (item.side == static_cast<int>(k) &&
                (item.kind == SimulatorItem::Kind::Planet || r.hull(real.design(item.design).hull).type == VehicleType::Base))
                middle = true;
        if (middle) continue;
        const auto [dx, dy] = kSideArrival[k];
        for (Vehicle& v : sb.vehicles)
            if (v.owner == sim.sides[k] && v.location == sim.where) {
                v.cameFrom = Location{sim.where.system, Sector{sim.where.sector.x + dx, sim.where.sector.y + dy}};
                v.cameFromTurn = sb.turn;
            }
    }
    for (size_t f = 0; f < setup.fleets.size(); ++f) {
        if (fleetMembers[f].empty()) continue;
        const SimulatorFleet& spec = setup.fleets[f];
        Fleet fleet;
        fleet.owner = sim.sides[static_cast<size_t>(spec.side)];
        fleet.name = spec.name.empty() ? std::format("Fleet {}", f + 1) : spec.name;
        fleet.members = fleetMembers[f];
        fleet.leader = fleetMembers[f].front();
        fleet.formation = spec.formation < r.data().formations.size() ? spec.formation : 0;
        fleet.strategy = spec.strategy;
        const FleetId id = sb.addFleet(std::move(fleet)).id;
        for (VehicleId v : fleetMembers[f]) sb.vehicle(v)->fleet = id;
    }
    return sim;
}

TacticalBattle startSimulation(const Rules& r, Simulation sim) {
    // No minefields in the simulator: nobody entered, so no mines strike. Only
    // side 1 gets hand control back when Auto is released (spec 04 §4).
    TacticalBattle::Setup setup{sim.where, std::vector<VehicleId>{}, sim.players};
    if (!sim.sides.empty()) setup.release = std::vector<EmpireId>{sim.sides.front()};
    setup.interference = sim.interference;
    setup.disruption = sim.disruption;
    return TacticalBattle(r, std::move(sim.state), std::move(setup));
}

} // namespace opense4::game::combat
