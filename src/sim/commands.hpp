#pragma once

// Player intent is expressed as commands, validated against the rules before
// they touch the state. Human players and AIs go through the same path, and
// commands are small value types that can later be serialized for
// play-by-email / network games (SE4's classic PBEM mode).

#include "sim/state.hpp"

#include <expected>
#include <string>
#include <variant>
#include <vector>

namespace opense4::sim {

namespace cmd {
struct MoveShip {
    ShipId ship;
    Location destination;
};
struct Colonize {
    ShipId ship;
    PlanetId planet;
};
struct StopShip {
    ShipId ship;
};
struct BuildShip {
    PlanetId planet;
    DesignId design;
    int count = 1;
};
struct BuildFacility {
    PlanetId planet;
    FacilityIndex facility;
};
struct CancelConstruction {
    PlanetId planet;
    int index = 0;  // position in the queue; spent resources are refunded
};
struct SetResearchQueue {
    std::vector<TechIndex> queue;
};
} // namespace cmd

using Command = std::variant<cmd::MoveShip, cmd::Colonize, cmd::StopShip, cmd::BuildShip, cmd::BuildFacility,
                             cmd::CancelConstruction, cmd::SetResearchQueue>;

using CommandResult = std::expected<void, std::string>;

CommandResult applyCommand(GameState& s, const Content& c, EmpireId issuer, const Command& command);

} // namespace opense4::sim
