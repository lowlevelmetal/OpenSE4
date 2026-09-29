#pragma once

#include "sim/state.hpp"

#include <expected>
#include <string>

namespace opense4::sim {

inline constexpr int kMaxSystems = 1000;
inline constexpr int kMinSectorRadius = 4;
inline constexpr int kMaxSectorRadius = 20;

struct GameSetup {
    GalaxySettings galaxy;
    int empireCount = 4;
    std::string playerRace = "human";  // race key for the human player
    bool allAi = false;                // no human player (tests, spectating)
};

struct NewGame {
    GameState state;
    EmpireId player;  // invalid when allAi
};

std::expected<NewGame, std::string> createGame(const Content& content, const GameSetup& setup);

} // namespace opense4::sim
