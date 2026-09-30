#include "game/serialize.hpp"

namespace opense4::game {

// Stub: implemented by the serialization/networking work package.

std::vector<uint8_t> serializeState(const GameState&) { return {}; }
std::expected<GameState, std::string> deserializeState(std::span<const uint8_t>) { return std::unexpected("not implemented"); }
std::vector<uint8_t> serializeOrders(const EmpireOrders&) { return {}; }
std::expected<EmpireOrders, std::string> deserializeOrders(std::span<const uint8_t>) { return std::unexpected("not implemented"); }
uint64_t stateChecksum(const GameState&) { return 0; }
std::expected<void, std::string> saveGame(const std::filesystem::path&, const GameState&, const SaveInfo&) {
    return std::unexpected("not implemented");
}
std::expected<std::pair<GameState, SaveInfo>, std::string> loadGame(const std::filesystem::path&) { return std::unexpected("not implemented"); }

} // namespace opense4::game
