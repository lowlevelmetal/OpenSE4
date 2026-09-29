#include "sim/mutate.hpp"

#include "sim/rules.hpp"

#include <format>

namespace opense4::sim {

DesignId addDesign(GameState& s, const Content& c, EmpireId owner, std::string name, std::string role, HullIndex hull,
                   std::span<const ComponentIndex> components) {
    Design d;
    d.id = DesignId{s.designs.size()};
    d.owner = owner;
    d.name = std::move(name);
    d.role = std::move(role);
    d.hull = hull;
    d.components.assign(components.begin(), components.end());
    d.stats = computeDesignStats(c, hull, d.components);
    s.designs.push_back(std::move(d));
    s.empire(owner).designs.push_back(s.designs.back().id);
    return s.designs.back().id;
}

ShipId spawnShip(GameState& s, EmpireId owner, DesignId designId, Location at) {
    Design& design = s.design(designId);
    Ship ship;
    ship.id = ShipId{s.nextShipId++};
    ship.owner = owner;
    ship.design = designId;
    ship.name = std::format("{} {}", design.name, ++design.built);
    ship.location = at;
    ship.movesLeft = design.stats.speed;
    s.ships.push_back(std::move(ship));  // ids increase monotonically, so the list stays sorted
    return s.ships.back().id;
}

void foundColony(GameState& s, PlanetId planetId, EmpireId owner, int64_t population) {
    Planet& p = s.planet(planetId);
    p.colony = Colony{};
    p.colony->owner = owner;
    p.colony->population = population;
    p.colony->foundedTurn = s.turn;
}

bool markExplored(GameState& s, EmpireId e, SystemId sys) {
    uint8_t& flag = s.empire(e).explored[sys.index()];
    const bool fresh = flag == 0;
    flag = 1;
    return fresh;
}

void addEvent(GameState& s, EmpireId e, EventKind kind, std::string text, std::optional<Location> loc) {
    s.events.push_back(GameEvent{kind, e, std::move(text), loc});
}

} // namespace opense4::sim
