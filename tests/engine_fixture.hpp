#pragma once

// A complete, original rules set for engine tests: the galaxy data of
// fixtures/minimal_dataset plus programmatically built technology,
// components, facilities and hulls covering every vehicle class and the
// abilities the engine implements. All names are invented for the tests.

#include "game/rules.hpp"
#include "game/setup.hpp"
#include "game/state.hpp"

#include <doctest/doctest.h>

#include <format>
#include <string_view>

namespace doctest {
template <>
struct StringMaker<opense4::game::Resources> {
    static String convert(const opense4::game::Resources& r) {
        return std::format("{{{}, {}, {}}}", r.v[0], r.v[1], r.v[2]).c_str();
    }
};
} // namespace doctest

namespace opense4::test {

// The shared rules (built once).
const game::Rules& engineRules();
// Builds a fresh ruleset (for tests that need to tweak data).
ruleset::Ruleset buildEngineRuleset();

// A new game on the test rules: `empires` empires (the first human, the rest
// computer unless allHuman), `systems` systems.
game::GameState newEngineGame(uint64_t seed = 7, int empires = 2, int systems = 12, bool allHuman = true);

// Lookups by name (REQUIRE on failure).
uint32_t componentIndex(const game::Rules& r, std::string_view name);
uint32_t facilityIndex(const game::Rules& r, std::string_view name);
uint32_t hullIndex(const game::Rules& r, std::string_view name);
ruleset::TechAreaId techArea(const game::Rules& r, std::string_view name);

// Adds a design built from component names (no validation) and returns its id.
game::DesignId addTestDesign(game::GameState& s, const game::Rules& r, game::EmpireId owner, std::string_view name, std::string_view hull,
                             std::initializer_list<std::string_view> components);
// Spawns a vehicle of a design at a location.
game::Vehicle& addTestVehicle(game::GameState& s, const game::Rules& r, game::DesignId design, game::Location where);

// The homeworld colony of an empire.
game::Colony& homeworld(game::GameState& s, game::EmpireId e);

// Gives every living empire two scouts and a colony ship for its own planet
// type, of test designs of its own ("<empire> Scout", "<empire> Colonizer"),
// at its homeworld. A new game has no ships (spec 01 §3.6); tests written
// for a game with a few ships at home start from this.
void addHomeShips(game::GameState& s, const game::Rules& r);

} // namespace opense4::test
