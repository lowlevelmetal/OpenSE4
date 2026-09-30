#pragma once

// New game creation (docs/spec/01 §3, spec 02 §9): quadrant, empires with
// their races, homeworlds, starting technology, designs and ships.

#include "game/rules.hpp"
#include "game/state.hpp"

#include <expected>
#include <string>
#include <vector>

namespace opense4::game {

struct EmpireSetup {
    std::string name;             // empty: from the race preset
    std::string empireType;
    std::string leaderTitle;
    std::string leaderName;
    // Race: either a preset folder (Pictures/Races/<folder>) with a tier
    // (0..2, the preset's "Race Opt" sets) or a fully custom race.
    std::string preset;
    int presetTier = 0;
    std::optional<Race> customRace;
    uint32_t color = 0;           // 0: automatic
    PlayerKind kind = PlayerKind::Human;
    std::string passwordHash;
};

struct GameSetup {
    uint64_t seed = 1;
    GameOptions options;
    std::vector<EmpireSetup> empires;   // index = EmpireId
};

// Builds a race from a preset tier: characteristics, traits (by name), culture,
// happiness model, environment.
Race raceFromPreset(const Rules& r, const ruleset::RacePreset& preset, int tier);
const ruleset::RacePreset* findPreset(const Rules& r, std::string_view folderOrName);

// Racial points a race costs (spec 02 §8.1); used to validate custom races.
int racialPointCost(const Rules& r, const Race& race);

std::expected<GameState, std::string> createGame(const Rules& r, const GameSetup& setup);

// Starting technology levels per the tech-start option (spec 05 §1.2).
std::vector<int> startingTechLevels(const Rules& r, const GameOptions& o, const Race& race);

// Distinct empire colours for automatic assignment.
uint32_t defaultEmpireColor(size_t index);

} // namespace opense4::game
