#pragma once

// Play by e-mail from the game client (docs/MULTIPLAYER.md, "Play by e-mail"):
// the player opens the game file the host sent (.gam), picks their empire,
// gives its password, plays the turn and saves the orders file (.plr) that
// goes back to the host. The files are read and written with the library in
// src/net/pbem.hpp; this part has no UI so it can be tested headless.
//
// Simultaneous games: the player gives orders; the .plr holds the commands
// (net::pbem::writePlayerOrders), which the host checks and applies with
// everyone else's.
//
// Turn-based games: the game file holds the turn of the player whose turn it
// is. Every command is carried out at once on the player's copy (the
// session's turn-based flow, game::applyLive), and the .plr holds all of
// them in the order given, with the checksums of the game before the first
// and after the last (net::pbem::writePlayerTurn); the host replays them.

#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "game/state.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// A game file as opened for play.
struct PbemGame {
    std::filesystem::path gameFile;
    game::SaveInfo info;
    // The game as the player's turn begins. A turn-based game file saved
    // between player turns is first played on to the next human, as the host
    // does before it reads the orders (net::pbem::processTurn).
    game::GameState state;
};

// Loads a game file for play: the data set must be the one it was made with
// (as the host checks), and the game must not be over.
std::expected<PbemGame, std::string> loadPbemGame(const game::Rules& rules, const std::filesystem::path& gameFile);

// One empire of the game file, for the player's choice.
struct PbemEmpireChoice {
    game::EmpireId id;
    std::string name;
    std::string player;       // the player's name from the game file (may be empty)
    bool playable = false;    // a living human empire
    bool password = false;    // it has a password
    bool yourTurn = true;     // turn-based games: it is this empire's turn
};
std::vector<PbemEmpireChoice> pbemEmpires(const PbemGame& g);
// Turn-based games: the empire whose turn it is (invalid when none).
game::EmpireId pbemActivePlayer(const PbemGame& g);

// The player's side of one turn.
struct PbemTurn {
    std::filesystem::path gameFile;
    std::filesystem::path ordersDir;   // where the .plr goes
    game::SaveInfo info;
    game::EmpireId empire;
    uint32_t turn = 0;
    std::string passwordHash;          // net::hashPassword() of the password given
    bool turnBased = false;
    uint64_t startChecksum = 0;        // turn-based: game::stateChecksum() as the turn began
};

// Checks that `empire` may play this turn with `password`: a living human
// empire whose password matches, and in a turn-based game the empire whose
// turn it is. `ordersDir` empty: the game file's folder (inferred).
std::expected<PbemTurn, std::string> beginPbemTurn(const PbemGame& g, game::EmpireId empire, std::string_view password,
                                                   std::filesystem::path ordersDir = {});

// Writes the player's .plr for the turn: `commands` are the commands given
// (turn-based: every command in the order given, refused ones included, since
// the host replays them all) and `now` is the player's game after the last.
std::expected<std::filesystem::path, std::string> writePbemOrders(const PbemTurn& t, const game::GameState& now,
                                                                  std::span<const game::Command> commands);

// A turn in progress can be saved and finished later (spec 05 §9.2: saving
// mid-turn is allowed). The draft is kept in its own folder, so it is not sent
// by mistake: a .plr named like the orders file, without the password hash.
// Opening the same turn again replays its commands.
std::expected<std::filesystem::path, std::string> writePbemDraft(const PbemTurn& t, const std::filesystem::path& dir,
                                                                 const game::GameState& now, std::span<const game::Command> commands);
// The commands of the draft saved for this very turn (the same game, turn and
// empire; in a turn-based game made from the same game file), if any.
std::optional<std::vector<game::Command>> readPbemDraft(const PbemTurn& t, const std::filesystem::path& dir);
void removePbemDraft(const PbemTurn& t, const std::filesystem::path& dir);

// Where the client looks for game files first (<user data>/pbem, created on
// demand), and the .gam files in a folder, newest first. Drafts go in
// <user data>/pbem/drafts.
std::filesystem::path pbemDir();
std::filesystem::path pbemDraftsDir();
std::vector<std::filesystem::path> listGameFiles(const std::filesystem::path& dir);

} // namespace opense4::client::classic
