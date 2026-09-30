#pragma once

// Play by e-mail (and any other file sharing): docs/spec/05 §9.2 and
// docs/MULTIPLAYER.md. The host keeps the game file (.gam, a saveGame()
// file); each player sends an orders file (.plr) per turn; the host
// processes the turn from whatever .plr files arrived (the computer plays
// the missing empires), writes the new .gam and deletes the processed .plr
// files.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "game/state.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace opense4::net::pbem {

inline constexpr std::string_view kOrdersExtension = ".plr";
inline constexpr std::string_view kGameExtension = ".gam";

// One empire's orders for one turn, with what the host needs to check them.
struct OrdersFile {
    std::string gameName;
    uint64_t gameId = 0;
    game::EmpireId empire;
    uint32_t turn = 0;
    std::string passwordHash;     // hashPassword() of the empire's password
    game::EmpireOrders orders;
};

std::vector<uint8_t> encodeOrdersFile(const OrdersFile& f);
std::expected<OrdersFile, std::string> decodeOrdersFile(std::span<const uint8_t> bytes);
std::expected<void, std::string> writeOrdersFile(const std::filesystem::path& file, const OrdersFile& f);
std::expected<OrdersFile, std::string> readOrdersFile(const std::filesystem::path& file);

// "<game>_<NN>.plr" (NN = empire number, from 1), with unsafe characters replaced.
std::string ordersFileName(const game::SaveInfo& info, game::EmpireId empire);

// The player's side: writes this turn's orders next to (or into) `dir` and
// returns the file to send to the host.
std::expected<std::filesystem::path, std::string> writePlayerOrders(const std::filesystem::path& dir, const game::SaveInfo& info,
                                                                   const game::EmpireOrders& orders, std::string_view passwordHash);

struct ProcessOptions {
    std::string masterPasswordHash;  // hashPassword() of the master password (needed when the game has one)
    bool deleteProcessed = true;     // remove the .plr files that were used (the classic behavior)
    bool allowDataSetMismatch = false;
};

struct ProcessReport {
    uint32_t turnBefore = 0;
    uint32_t turnAfter = 0;
    std::vector<std::string> submitted;          // empire names with orders
    std::vector<std::string> playedByComputer;   // human empires without orders
    std::vector<std::string> warnings;           // skipped files and why
    std::vector<std::string> rejectedCommands;   // "Empire: Command: reason"
    std::vector<std::filesystem::path> used;     // .plr files that were processed
};

// Processes the current turn of `state` from the .plr files in `ordersDir`
// (validating game, turn, empire and password). Does not write anything.
std::expected<ProcessReport, std::string> processTurn(const game::Rules& rules, game::GameState& state, const game::SaveInfo& info,
                                                      const std::filesystem::path& ordersDir);

// The whole host step: load the .gam (checking the master password and the
// data set), process the turn, save the .gam in place, then delete the used
// .plr files.
std::expected<ProcessReport, std::string> processGameFile(const game::Rules& rules, const std::filesystem::path& gameFile,
                                                          const std::filesystem::path& ordersDir, const ProcessOptions& options);

} // namespace opense4::net::pbem
