#include "game/movement.hpp"

#include "game/design.hpp"
#include "game/turn.hpp"

#include <format>

namespace opense4::game::movement {

// Stub: implemented by the vehicles work package (docs/spec/03).

std::optional<Path> findPath(const Rules&, const GameState&, EmpireId, Location, Location) { return std::nullopt; }
int etaTurns(const Rules&, const GameState&, const Vehicle&, Location) { return -1; }
void startTurn(TurnContext&) {}
void runMovementAndCombat(TurnContext&) {}
void runColonization(TurnContext&) {}
void runUpkeep(TurnContext&) {}

Vehicle& spawnVehicle(const Rules& r, GameState& s, EmpireId owner, DesignId design, Location where, int autoWaypoint) {
    Design& d = s.design(design);
    Vehicle v;
    v.owner = owner;
    v.design = design;
    v.name = std::format("{} {}", d.name, ++d.built);
    v.location = where;
    v.damage.assign(d.entries.size(), 0);
    v.builtTurn = s.turn;
    v.supply = computeDesignStats(r, nullptr, d).supplyCapacity;
    const Empire& e = s.empire(owner);
    if (autoWaypoint >= 0 && static_cast<size_t>(autoWaypoint) < e.waypoints.size() && e.waypoints[static_cast<size_t>(autoWaypoint)].set)
        v.orders.push_back(Order{OrderKind::MoveTo, e.waypoints[static_cast<size_t>(autoWaypoint)].location});
    return s.addVehicle(std::move(v));
}

} // namespace opense4::game::movement
