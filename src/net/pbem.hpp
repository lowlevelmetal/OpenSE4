#pragma once

// Play by e-mail (and any other file sharing): docs/spec/05 §9.2 and
// docs/MULTIPLAYER.md. The host keeps the whole game (.gam, a saveGame()
// file) and never sends it. Each player whose turn it is gets a turn file
// (.turn) of their own: the game as their empire knows it
// (game::redactForEmpire), as the network host sends it, encrypted to the
// empire's password key (net/auth.hpp). The player sends back an orders file
// (.plr), signed with the empire's password and encrypted to the host's key
// (named in the turn file), so in a shared folder nobody reads another
// player's view or orders. The host processes the turn from whatever .plr
// files arrived (the computer plays the missing empires), writes the new .gam
// and the next turn files, and deletes the processed .plr files.
//
// Turn-based games (spec 05 §8, §9.1): the game goes from player to player
// through the host, one turn file at a time. The turn file holds the turn of
// the player whose turn it is, already started. The player's copy carries
// each command out at once (game::applyLive) as a preview, and the .plr
// lists every command in the order given, Attack Sector answers included.
// The host carries them out on the whole game, ends the player's turn
// (game::endPlayerTurn: the computer players move, the next human's turn
// starts) and writes the turn file for the next player. Battles and anything
// the player could not see come out as the whole game says, which the
// player's preview cannot know; the next turn file shows the result.
//
// Checks on both sides: a turn file carries its view's checksum, which the
// player's game verifies; an orders file carries the checksum of the view it
// was made from, which the host compares with the view it makes of its own
// game, so orders made from an old or another turn file are refused.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "game/state.hpp"
#include "net/auth.hpp"
#include "net/crypto.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace opense4::net::pbem {

inline constexpr std::string_view kOrdersExtension = ".plr";
inline constexpr std::string_view kGameExtension = ".gam";
inline constexpr std::string_view kTurnExtension = ".turn";

// ---- Turn files (host -> player) -----------------------------------------------------------------

// One empire's view of the game, for the player of that empire.
struct TurnFile {
    // In the clear: which game and empire it is for, and what the player's
    // game needs to make the empire's keys.
    game::SaveInfo info;           // the game's header (without the master password's verifier)
    game::EmpireId empire;         // whose view this is
    bool turnBased = false;        // a turn-based game (the file is for the player whose turn it is)
    std::string verifier;          // that empire's password verifier
    crypto::Key hostKey{};         // the host's key: the player's orders file is encrypted to it
    // The view (TurnView), encrypted to the empire's box key and bound to
    // everything above. In the clear when the empire has no password, or one
    // of OpenSE4 0.6 (no box key: that empire moves to a new one this turn).
    bool encrypted = false;
    crypto::Key ephemeral{};
    crypto::Mac mac{};
    std::vector<uint8_t> content;
};

// What a turn file holds for its player.
struct TurnView {
    uint64_t viewChecksum = 0;     // game::stateChecksum() of the view
    std::vector<uint8_t> view;     // game::serializeState() of the view
};

// A TurnView as the turn file's content holds it (before any encryption).
std::vector<uint8_t> encodeTurnView(const TurnView& v);
std::vector<uint8_t> encodeTurnFile(const TurnFile& f);
std::expected<TurnFile, std::string> decodeTurnFile(std::span<const uint8_t> bytes);
std::expected<TurnFile, std::string> readTurnFile(const std::filesystem::path& file);
// The view of a turn file, opened with the empire's password keys (none: a
// file in the clear). Fails for a wrong password or a changed file.
std::expected<TurnView, std::string> openTurnFile(const TurnFile& f, const std::optional<PasswordKeys>& keys);

// "<game>_<NN>.turn" (NN = empire number, from 1).
std::string turnFileName(const game::SaveInfo& info, game::EmpireId empire);

// Reading a game file to play a turn recalculates every colony (spec 01
// §6.9, §14 Q44); the host does it before it processes a turn and before it
// makes the turn files, so both see the same game.
void readForTurn(const game::Rules& rules, game::GameState& state);

// The empires that play the turn now: every living human empire (simultaneous
// games), or the human whose turn has started (turn-based games).
std::vector<game::EmpireId> empiresToPlay(const game::GameState& state);

// The view of the game the host sends `empire` (game::redactForEmpire): what
// the turn file holds, and what an orders file's startChecksum refers to.
game::GameState playerView(const game::Rules& rules, const game::GameState& state, game::EmpireId empire);

// Writes the turn files of every empire that plays now from the game file,
// into `dir`, naming `hostKey` (the public half of the host's key) for the
// orders; returns each empire and its file.
std::expected<std::vector<std::pair<game::EmpireId, std::filesystem::path>>, std::string>
writeTurnFiles(const game::Rules& rules, const std::filesystem::path& gameFile, const std::filesystem::path& dir, const crypto::Key& hostKey);

// ---- Orders files (player -> host) ---------------------------------------------------------------

// One empire's orders for one turn, signed with the empire's password. On
// the disk it is encrypted to the host's key (encodeOrdersFile).
struct OrdersFile {
    std::string gameName;
    uint64_t gameId = 0;
    game::EmpireId empire;
    uint32_t turn = 0;
    game::EmpireOrders orders;     // turn-based: every command in the order given
    uint64_t startChecksum = 0;    // TurnView::viewChecksum of the turn file the orders were made from
    uint64_t revision = 0;         // later files of the same turn have higher numbers (the time they were made)
    std::string verifier;          // the verifier of the password that signed (empty: none)
    crypto::Signature signature{}; // that password's signature of everything above (ordersDigest)
    // Only for an empire whose verifier is of OpenSE4 0.6 (the turn file
    // says so): the old password's hash, which the host checks once. The file
    // is signed with a new password, whose verifier then replaces the old one:
    // the old hash travelled in the clear in 0.6, so nothing is made of it.
    std::string legacyPasswordHash;
};

// The .plr on the disk: the orders file encrypted to the host's key, with
// what it is for in the clear.
std::vector<uint8_t> encodeOrdersFile(const OrdersFile& f, const crypto::Key& hostKey);
std::expected<OrdersFile, std::string> decodeOrdersFile(std::span<const uint8_t> bytes, const crypto::KeyPair& hostKey);
std::expected<void, std::string> writeOrdersFile(const std::filesystem::path& file, const OrdersFile& f, const crypto::Key& hostKey);
std::expected<OrdersFile, std::string> readOrdersFile(const std::filesystem::path& file, const crypto::KeyPair& hostKey);

// A turn in progress kept on the player's machine (unsigned, not encrypted).
std::expected<void, std::string> writeDraft(const std::filesystem::path& file, const OrdersFile& f);
std::expected<OrdersFile, std::string> readDraft(const std::filesystem::path& file);

// What the password signs: every field but the signature and the legacy hash.
crypto::Key ordersDigest(const OrdersFile& f);
// Fills in `verifier` and `signature` from the password keys (none: unsigned,
// an empire without a password), and the old password's hash of an OpenSE4
// 0.6 empire.
void signOrdersFile(OrdersFile& f, const std::optional<PasswordKeys>& keys, std::string_view legacyPasswordHash = {});

// "<game>_<NN>.plr" (NN = empire number, from 1), with unsafe characters replaced.
std::string ordersFileName(const game::SaveInfo& info, game::EmpireId empire);

// The player's side: signs, encrypts to `hostKey` and writes this turn's
// orders into `dir`; returns the file to send to the host. `startChecksum` is
// the turn file's view checksum; `keys` the password's (the new password's
// for an OpenSE4 0.6 empire, with `legacyPasswordHash` the old one's).
std::expected<std::filesystem::path, std::string> writePlayerOrders(const std::filesystem::path& dir, const game::SaveInfo& info,
                                                                   const game::EmpireOrders& orders, uint64_t startChecksum,
                                                                   const std::optional<PasswordKeys>& keys, const crypto::Key& hostKey,
                                                                   std::string_view legacyPasswordHash = {});

// ---- The host's processing -----------------------------------------------------------------------

struct ProcessOptions {
    crypto::KeyPair hostKey;         // the host's key: opens the orders files, named in the turn files
    std::string masterPassword;      // the master password (needed when the game has one)
    bool deleteProcessed = true;     // remove the .plr files that were used (the classic behavior)
    bool allowDataSetMismatch = false;
    // Where the next turn files go (empty: next to the game file).
    std::filesystem::path turnFilesDir;
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
    // or `playedByComputer`); the empire whose turn it is now (send it its
    // turn file), empty when none (the game is over).
    bool turnBased = false;
    std::string next;
    game::EmpireId nextEmpire;
    // The turn files written for the new turn: send each to its empire's player.
    std::vector<std::pair<game::EmpireId, std::filesystem::path>> turnFiles;
    // Reset Passwords: each empire and its new password (show the host only).
    std::vector<std::pair<game::EmpireId, std::string>> passwordResets;
};

// Processes the current turn of `state` (as read: readForTurn) from the
// .plr files in `ordersDir`, opened with the host's key, checking game, turn,
// empire, the turn file they were made from, the password's signature and
// the password values the orders set. Of two files for one empire the higher
// revision counts; two different moves of an OpenSE4 0.6 empire to a new
// password are both refused. Does not write anything.
// Turn-based games: the turn of the player whose turn it is, from its .plr,
// or played by the computer when none came; then the game plays on to the
// next human.
std::expected<ProcessReport, std::string> processTurn(const game::Rules& rules, game::GameState& state, const game::SaveInfo& info,
                                                      const std::filesystem::path& ordersDir, const crypto::KeyPair& hostKey);

// The whole host step: load the .gam (checking the master password and the
// data set), process the turn, save the .gam in place, write the turn files
// for the new turn, then delete the used .plr files.
std::expected<ProcessReport, std::string> processGameFile(const game::Rules& rules, const std::filesystem::path& gameFile,
                                                          const std::filesystem::path& ordersDir, const ProcessOptions& options);

} // namespace opense4::net::pbem
