#pragma once

// Low-level state mutations shared by game setup, commands and turn processing.
// They keep derived invariants (sorted ship list, cached design stats, ...).

#include "sim/state.hpp"

#include <span>

namespace opense4::sim {

DesignId addDesign(GameState& s, const Content& c, EmpireId owner, std::string name, std::string role, HullIndex hull,
                   std::span<const ComponentIndex> components);

ShipId spawnShip(GameState& s, EmpireId owner, DesignId design, Location at);

void foundColony(GameState& s, PlanetId planet, EmpireId owner, int64_t population);

// Returns true if the system was not explored before.
bool markExplored(GameState& s, EmpireId e, SystemId sys);

void addEvent(GameState& s, EmpireId e, EventKind kind, std::string text, std::optional<Location> loc = std::nullopt);

} // namespace opense4::sim
