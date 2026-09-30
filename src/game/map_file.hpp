#pragma once

// Map files (docs/spec/01 §12): a quadrant saved by Save Map (the Game Menu,
// or Game Setup's Quadrant page) and loaded by Game Setup instead of
// generating one. The original's map format is unknown, so this is our own
// text format (TOML), described in docs/MAPS.md. It holds:
//   - the systems with their galaxy positions, names, SystemTypes.txt type,
//     physical type and system-wide abilities;
//   - every object with its kind, SectType.txt record, name, sector,
//     attributes and abilities (planets also their conditions and values);
//   - the warp links;
//   - optional starting points, each for one player slot or common.
// Map files live under the user's data directory (<user data>/maps), never in
// the game install.

#include "game/galaxy.hpp"
#include "game/state.hpp"
#include "ruleset/ruleset.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {

inline constexpr int kMapFormatVersion = 1;
inline constexpr std::string_view kMapExtension = ".toml";

struct QuadrantMap {
    std::string name;
    Galaxy galaxy;
    std::vector<StartingPoint> startingPoints;
};

// The text of a map file.
std::string mapToText(const ruleset::Ruleset& rs, const QuadrantMap& map);

struct LoadedMap {
    QuadrantMap map;
    std::vector<std::string> warnings;  // things this data set lacks, replaced by the nearest match
};
// Reads a map file's text. Object and system ids are numbered in file order.
std::expected<LoadedMap, std::string> mapFromText(const ruleset::Ruleset& rs, std::string_view text);

std::expected<void, std::string> saveMapFile(const std::filesystem::path& file, const ruleset::Ruleset& rs, const QuadrantMap& map);
std::expected<LoadedMap, std::string> loadMapFile(const std::filesystem::path& file, const ruleset::Ruleset& rs);

// Save Map during a game (offered only with GameOptions::playersCanSaveMap):
// the current quadrant (systems and stellar objects; the galaxy holds no
// ships, units or empire knowledge) with the starting points the game still
// holds (GameState::startingPoints): a game started from a map keeps all its
// specific points and the common points no player took; a generated game has
// none. The empires' capitals are not written (spec 01 §12, §14 Q37,
// confirmed: binary).
QuadrantMap mapOfGame(const GameState& s, std::string name);

// A file name for a map name: letters, digits, spaces, '-' and '_' kept.
std::string mapFileStem(std::string_view name);

} // namespace opense4::game
