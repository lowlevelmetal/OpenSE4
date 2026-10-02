#pragma once

// Play by e-mail (and any other file sharing): docs/spec/05 §9.2 and
// docs/MULTIPLAYER.md. The host keeps the game file (.gam, a saveGame()
// file); each player sends an orders file (.plr) per turn; the host
// processes the turn from whatever .plr files arrived (the computer plays
// the missing empires), writes the new .gam and deletes the processed .plr
// files.
//
// Turn-based games (spec 05 §8, §9.1): the game goes from player to player.
// The .gam holds the turn of the player whose turn it is, already started.
// That player plays it on their copy (commands carried out at once,
// game::applyLive) and sends a .plr with every command in the order given,
// Attack Sector answers included, plus the checksums of the game before and
// after. The host replays the commands the same way, ends the player's turn
// (game::endPlayerTurn: the computer players move, the next human's turn
// starts) and writes the .gam for the next player.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "game/state.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <utility>
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
    // Turn-based games: game::stateChecksum() of the game as the player's
    // turn began (the .gam received) and after the last command. 0 in
    // simultaneous games.
    uint64_t startChecksum = 0;
    uint64_t endChecksum = 0;
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

// Turn-based games: the player's turn as played on their copy of the game.
// `commands` holds every command in the order given; `startChecksum` and
// `endChecksum` are game::stateChecksum() of the game before the first and
// after the last one.
std::expected<std::filesystem::path, std::string> writePlayerTurn(const std::filesystem::path& dir, const game::SaveInfo& info,
                                                                 uint64_t startChecksum, const game::EmpireOrders& commands,
                                                                 uint64_t endChecksum, std::string_view passwordHash);

struct ProcessOptions {
    std::string masterPasswordHash;  // hashPassword() of the master password (needed when the game has one)
    bool deleteProcessed = true;     // remove the .plr files that were used (the classic behavior)
    bool allowDataSetMismatch = false;
    // Reset Passwords (spec 06 §1.9, simultaneous games): these empires get a
    // new six-digit password (net::resetPassword), written in once the turn's
    // orders have been read; the report lists them for the host only.
    std::vector<game::EmpireId> resetPasswords;
};

struct ProcessReport {
    uint32_t turnBefore = 0;
    uint32_t turnAfter = 0;
    std::vector<std::string> submitted;          // empire names with orders
    std::vector<std::string> playedByComputer;   // human empires without orders
    std::vector<std::string> warnings;           // skipped files and why
    std::vector<std::string> rejectedCommands;   // "Empire: Command: reason"
    std::vector<std::filesystem::path> used;     // .plr files that were processed
    // Turn-based games: one player's turn was played (the one in `submitted`
    // or `playedByComputer`); the empire whose turn it is now (send it the
    // game), empty when none (the game is over).
    bool turnBased = false;
    std::string next;
    game::EmpireId nextEmpire;
    // Reset Passwords: each empire and its new password (show the host only).
    std::vector<std::pair<game::EmpireId, std::string>> passwordResets;
};

// Processes the current turn of `state` from the .plr files in `ordersDir`
// (validating game, turn, empire and password). Does not write anything.
// Turn-based games: the turn of the player whose turn it is, from its .plr
// (which must have been made from this very game), or played by the
// computer when none came; then the game plays on to the next human.
std::expected<ProcessReport, std::string> processTurn(const game::Rules& rules, game::GameState& state, const game::SaveInfo& info,
                                                      const std::filesystem::path& ordersDir);

// The whole host step: load the .gam (checking the master password and the
// data set), process the turn, save the .gam in place, then delete the used
// .plr files.
std::expected<ProcessReport, std::string> processGameFile(const game::Rules& rules, const std::filesystem::path& gameFile,
                                                          const std::filesystem::path& ordersDir, const ProcessOptions& options);

} // namespace opense4::net::pbem
