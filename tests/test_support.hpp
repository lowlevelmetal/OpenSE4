#pragma once

#include "sim/content.hpp"
#include "sim/setup.hpp"

#include <doctest/doctest.h>

#include <string>

namespace opense4::test {

// The real game data, loaded once per test binary.
inline const sim::Content& content() {
    static const sim::Content c = [] {
        auto loaded = sim::loadContent(OPENSE4_DATA_DIR);
        if (!loaded) {
            std::string all;
            for (const auto& e : loaded.error()) all += e + "\n";
            FAIL("failed to load game data:\n" << all);
        }
        return std::move(*loaded);
    }();
    return c;
}

inline sim::NewGame newGame(uint64_t seed, int systems = 30, int empires = 4, bool allAi = false,
                            sim::GalaxyShape shape = sim::GalaxyShape::Spiral) {
    sim::GameSetup setup;
    setup.galaxy.seed = seed;
    setup.galaxy.systemCount = systems;
    setup.galaxy.shape = shape;
    setup.galaxy.sectorRadius = content().rules.sectorRadius;
    setup.empireCount = empires;
    setup.allAi = allAi;
    auto game = sim::createGame(content(), setup);
    REQUIRE_MESSAGE(game.has_value(), (game ? std::string{} : game.error()));
    return std::move(*game);
}

} // namespace opense4::test
