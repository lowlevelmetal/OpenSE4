#pragma once

// Race presets of a classic install: Pictures/Races/<Race>/<Race>_AI_General.txt
// (and RaceNeutral/). Each preset has identity text plus three build tiers of
// characteristics and advanced traits (docs/spec/02 §1.10).

#include "ruleset/files.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace opense4::ruleset {

struct RaceTier {
    std::vector<std::pair<std::string, int>> characteristics;  // name -> percent
    std::vector<std::string> traits;
};

struct RacePreset {
    std::string folder;        // art style, e.g. "Terran"
    bool neutral = false;
    std::string name;
    std::string description;   // the short blurb shown when picking an empire
    std::string empireName, empireType, emperorName, emperorTitle;
    std::string biology, society, history;
    std::string demeanor, culture, happinessType, planetType, atmosphere, designNameFile;
    std::vector<RaceTier> tiers;
    int personalityGroup = 0;  // from <Race>_AI_Settings.txt, if present
};

// gameRoot is the directory holding Data/ and Pictures/.
std::vector<RacePreset> loadRacePresets(const std::filesystem::path& gameRoot);
// The same from a game folder's files (the install, perhaps with mods).
std::vector<RacePreset> loadRacePresets(const GameFiles& files);

} // namespace opense4::ruleset
