#pragma once

// Binary save format for GameState and order lists (the `.gam` and `.plr`
// equivalents, docs/spec/05 §9.5). Little-endian, versioned, checksummed.

#include "game/commands.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace opense4::game {

inline constexpr uint32_t kSaveVersion = 1;

std::vector<uint8_t> serializeState(const GameState& s);
std::expected<GameState, std::string> deserializeState(std::span<const uint8_t> bytes);

std::vector<uint8_t> serializeOrders(const EmpireOrders& o);
std::expected<EmpireOrders, std::string> deserializeOrders(std::span<const uint8_t> bytes);

// Stable hash of the whole state (desync detection).
uint64_t stateChecksum(const GameState& s);

// Save files carry a small header (data set identity, turn, empire names).
struct SaveInfo {
    std::string gameName;
    std::string dataSet;         // e.g. install data directory name or mod name
    uint32_t turn = 0;
    std::vector<std::string> empires;
};
std::expected<void, std::string> saveGame(const std::filesystem::path& file, const GameState& s, const SaveInfo& info);
std::expected<std::pair<GameState, SaveInfo>, std::string> loadGame(const std::filesystem::path& file);

} // namespace opense4::game
