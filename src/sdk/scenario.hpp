#pragma once

// Scenarios (docs/sdk/rules.md "Scenarios"): a mod's `scenarios/<name>.toml`
// is a game's setup and its objectives, written in the lessons' condition
// language (docs/LEARNING.md "Conditions", src/learn/condition.hpp). A game
// started from one records it (GameState::scenario); at every victory check
// the rules session tests the objectives not yet met, and one that holds is
// logged, runs its action (a function the mod registers with
// @rules.objective) and, when it says so, ends the game.
//
//     title = "Hold the Gate"
//     summary = "Found three colonies before the raiders come."
//
//     [setup]
//     seed = 7
//     turn_style = "simultaneous"        # or "turn_based" (the default)
//     systems = 12
//     [[setup.empire]]
//     name = "Wardens"
//     kind = "human"                     # human, computer or neutral
//     [[setup.empire]]
//     name = "Raiders"
//     kind = "computer"
//
//     [options]                          # the mod's own options
//     raid_strength = 3
//
//     [[objective]]
//     name = "settled"
//     text = "Found three colonies"
//     empire = 0                         # whose: an empire's number, or "every" (the default)
//     when = { colonies = 3 }
//     victory = true                     # the empire that meets it wins
//     action = "reward"                  # optional: @rules.objective("reward")
//     by_turn = 40                       # optional: it can no longer be met after this turn

#include "game/rules.hpp"
#include "game/setup.hpp"
#include "game/state.hpp"
#include "learn/condition.hpp"
#include "mods/package.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::sdk {

struct ScenarioObjective {
    std::string name;
    std::string text;
    std::optional<uint32_t> empire;   // nullopt: every empire
    learn::Condition when;
    bool victory = false;
    std::string action;
    std::optional<uint32_t> byTurn;
    int line = 0;
};

struct Scenario {
    std::string mod;
    std::string name;
    std::string title;
    std::string summary;
    game::GameSetup setup;
    std::vector<std::pair<std::string, int64_t>> options;   // the mod's own options
    std::vector<ScenarioObjective> objectives;

    const ScenarioObjective* objective(std::string_view name) const;
};

// Reads a scenario's text; every problem names `file` and its line.
std::expected<Scenario, std::vector<std::string>> parseScenario(std::string_view text, std::string_view file, std::string_view mod,
                                                                std::string_view name);
// The scenarios a mod holds (scenarios/*.toml), by name.
std::vector<std::string> scenarioNames(const mods::Package& p);
std::expected<Scenario, std::vector<std::string>> loadScenario(const mods::Package& p, std::string_view name);

// A new game from a scenario of a mod the data set was loaded with (or
// given): its setup, the mod's options it sets, and the scenario recorded.
std::expected<game::GameState, std::string> startScenario(const game::Rules& r, const mods::Package& p, std::string_view name);

} // namespace opense4::sdk
