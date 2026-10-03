#pragma once

// Play by e-mail from the game client (docs/MULTIPLAYER.md, "Play by e-mail"):
// the player opens the turn file the host sent (.turn: the game as the
// player's empire knows it, never the whole game), gives the empire's
// password, plays the turn and saves the orders file (.plr) that goes back
// to the host, signed with the password. The files are read and written with
// the library in src/net/pbem.hpp; this part has no UI so it can be tested
// headless.
//
// Simultaneous games: the player gives orders; the .plr holds the commands,
// which the host checks and applies with everyone else's.
//
// Turn-based games: the turn file holds the turn of the player whose turn it
// is. Every command is carried out at once on the player's copy as a preview
// (the session's turn-based flow, game::applyLive), and the .plr holds all
// of them in the order given; the host carries them out on the whole game,
// which decides battles and whatever the player could not see.

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

// A turn file as opened for play.
struct PbemGame {
    std::filesystem::path gameFile;    // the turn file
    game::SaveInfo info;
    // The game as the player's turn begins, as the player's empire knows it.
    game::GameState state;
    game::EmpireId empire;             // whose turn file it is
    std::string verifier;              // that empire's password verifier
    uint64_t viewChecksum = 0;         // game::stateChecksum(state), checked on loading
};

// Loads a turn file for play: the data set must be the one it was made with
// (as the host checks), its view must match its checksum, and the game must
// not be over. The host's own game file (.gam, the whole game) is refused.
std::expected<PbemGame, std::string> loadPbemGame(const game::Rules& rules, const std::filesystem::path& gameFile);

// One empire of the game file, for the player's choice.
struct PbemEmpireChoice {
    game::EmpireId id;
    std::string name;
    std::string player;       // the player's name from the game file (may be empty)
    bool playable = false;    // the turn file's empire (living and human)
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
    uint64_t startChecksum = 0;        // the turn file's view checksum (game::stateChecksum as the turn began)
    // An empire whose verifier is of OpenSE4 0.6: the old password's hash,
    // which the .plr shows this once; passwordHash is then the new password's.
    std::string legacyPasswordHash;
};

// Checks that `empire` may play this turn with `password`: the turn file's
// empire, living and human, whose password matches, and in a turn-based game
// the empire whose turn it is. `ordersDir` empty: the turn file's folder
// (inferred). A game of OpenSE4 0.6 (pbemNeedsNewPassword) also needs a
// `newPassword`, other than the old one, which counts from this turn on.
std::expected<PbemTurn, std::string> beginPbemTurn(const PbemGame& g, game::EmpireId empire, std::string_view password,
                                                   std::filesystem::path ordersDir = {}, std::string_view newPassword = {});
// The empire's password is of OpenSE4 0.6: this turn moves it to a new one.
bool pbemNeedsNewPassword(const PbemGame& g);

// Writes the player's .plr for the turn, signed with the password:
// `commands` are the commands given (turn-based: every command in the order
// given, refused ones included, since the host carries them all out).
std::expected<std::filesystem::path, std::string> writePbemOrders(const PbemTurn& t, std::span<const game::Command> commands);

// A turn in progress can be saved and finished later (spec 05 §9.2: saving
// mid-turn is allowed). The draft is kept in its own folder, so it is not sent
// by mistake: a .plr named like the orders file, unsigned. Opening the same
// turn again replays its commands.
std::expected<std::filesystem::path, std::string> writePbemDraft(const PbemTurn& t, const std::filesystem::path& dir,
                                                                 std::span<const game::Command> commands);
// The commands of the draft saved for this very turn (the same game, turn and
// empire, made from the same turn file), if any.
std::optional<std::vector<game::Command>> readPbemDraft(const PbemTurn& t, const std::filesystem::path& dir);
void removePbemDraft(const PbemTurn& t, const std::filesystem::path& dir);

// Where the client looks for turn files first (<user data>/pbem, created on
// demand), and the .turn files in a folder, newest first. Drafts go in
// <user data>/pbem/drafts.
std::filesystem::path pbemDir();
std::filesystem::path pbemDraftsDir();
std::vector<std::filesystem::path> listTurnFiles(const std::filesystem::path& dir);

} // namespace opense4::client::classic
