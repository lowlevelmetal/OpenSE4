#pragma once

// The original's command line (docs/spec/05 §9.2, confirmed: binary):
// `<game file> <password> [<player number> [<mod folder>]]`.
//
// - Without a player number, with 0 or with text that is not a number, the
//   program is the host: with the game's host (master) password it processes
//   the turn, saves the game and quits, with no window; a wrong password quits
//   with exit code 1.
// - With a player number N and empire N's password, the data files are
//   checked, then the game opens on player N's turn; a mismatch ("Invalid
//   Data Files") or a wrong password quits with exit code 2.
// - Passwords are compared without the spaces around them and without regard
//   to letter case. The fourth argument names a mod folder; `-nd` or `/nd`
//   suppresses every message box of the run (a question counts as No) and
//   also takes its place among the arguments.
// - A game file that is not found, or no name, quits at once with exit code
//   0 and no box.
//
// OpenSE4's files differ from the original's (spec 05 §9.2 "Our client
// differs"): the host's file is a play-by-e-mail game file, processed as
// `opense4-server pbem process` processes it (net::pbem::processGameFile); a
// player signs in to a turn file (.turn, played as --pbem plays it), to an
// OpenSE4 saved game, or to a saved game of the original (imported).
//
// No window here: ClassicMode signs in (classic_mode.cpp), and the host's
// processing runs before any window exists (client/app.cpp).

#include "client/classic/pbem_play.hpp"
#include "client/classic/session.hpp"
#include "game/rules.hpp"
#include "mods/mod_set.hpp"
#include "net/pbem.hpp"
#include "net/secure.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// The original's exit codes (spec 05 §9.2): a game file not found (the
// program quits without a word), a refused host (a wrong host password;
// OpenSE4 also uses it for a game file it cannot host) and a refused sign-in
// (a wrong password or other data files; OpenSE4 also uses it for the empires
// it cannot hand over: another empire's turn file, a turn-based game in which
// it is not that empire's turn, a number past the last empire).
inline constexpr int kGameFileNotFound = 0;
inline constexpr int kHostRefused = 1;
inline constexpr int kSignInRefused = 2;

// The `nd` switch: "-nd" or "/nd", in any letter case. It is also one of the
// positional arguments where it stands (spec 05 §9.2, confirmed: binary):
// first, it is the game file's name; third, "not a number", so the program
// hosts.
bool isNoDialogsSwitch(std::string_view argument);

// The third argument as the original reads it: a whole number of 1 or more is
// a player number; nothing, 0, or text that is not a number is 0 (the host).
int playerNumberArgument(std::string_view text);

// The arguments that are not options, read as the original reads them.
struct CommandLineGame {
    std::string gameFile;
    std::string password;   // empty when only the game file is given
    int player = 0;         // 1-based; 0: the host
    std::string modFolder;  // the fourth argument (empty: none)
    bool host() const { return player == 0; }
};
// None without such arguments (the program starts as usual).
std::optional<CommandLineGame> commandLineGame(std::span<const std::string> positional);

// The original's comparison of two passwords in the clear (the original's
// saved games keep them so): the spaces around them and letter case ignored.
bool samePassword(std::string_view a, std::string_view b);

// The command line's mod folder (the fourth argument) as a path: a folder of
// the game folder `gameRoot` when there is one by that name, else the name as
// given (a path).
std::filesystem::path modFolderPath(const std::string& folder, const std::filesystem::path& gameRoot);
// The same when that folder exists; none for no name or a folder that does not
// exist, which changes nothing: the original then finds every file in the
// game's own folders (spec 01 §1 "Mods").
std::optional<std::filesystem::path> commandLineModFolder(const std::string& folder, const std::filesystem::path& gameRoot);

// What a game file is, from its contents.
enum class GameFileKind {
    OpenSE4Game,   // an OpenSE4 saved game: local, hotseat, a network host's or a PBEM host's
    TurnFile,      // a PBEM player's turn file (.turn)
    ClassicGame,   // a saved game of the original
    Unknown,       // none of these, or a file that cannot be read
};
GameFileKind gameFileKind(const std::filesystem::path& file);

// Finds the game file: the name as given (from the current folder), then, for
// a relative name, in each of `folders` in turn. None when it is nowhere, or
// empty.
std::optional<std::filesystem::path> findGameFile(std::string_view name, std::span<const std::filesystem::path> folders);
// Where the command line's game file is looked for after the current folder
// (inferred): the mod folder (modFolderPath, when given), the saves folder
// (savesDir) and the play-by-e-mail folder (pbemDir).
std::vector<std::filesystem::path> gameFileFolders(const CommandLineGame& line, const std::filesystem::path& gameRoot);

// ---- Signing in as player N ----------------------------------------------------------------------

enum class SignInVerdict {
    SignedIn,
    WrongPassword,     // the original quits without a word
    InvalidDataFiles,  // the original's "Invalid Data Files" box
    NotPlayable,       // OpenSE4: the empire cannot play now (another's turn file, not its turn in a turn-based game, no such empire)
    Unreadable,        // the file does not load
};
struct SignInCheck {
    SignInVerdict verdict = SignInVerdict::SignedIn;
    std::string why;   // what went wrong, in words (empty when signed in)
    bool ok() const { return verdict == SignInVerdict::SignedIn; }
};

// An OpenSE4 saved game, or a saved game of the original (imported), for
// empire `player` (1-based) with `password`: the password first, then the
// data files (for OpenSE4's files the data set's identity in the header and
// the game's mods, as Login To Game checks them; for the original's the seven
// data-set checksums, spec 08 §3.2, unless the file is older than 1.42, so a
// file whose checksums are all 0 never matches). Nothing else is checked, as
// in the original (spec 05 §9.2, confirmed: binary): a destroyed empire, or
// one the computer plays, opens too. OpenSE4 still refuses a turn-based game
// in which it is another empire's turn (it cannot hand that turn over) and a
// number past the last empire (the original's result is not known). Signed
// in, the session is that empire's and asks nothing more (no Login To Game,
// no Next Player window for this turn).
struct SavedGameSignIn {
    SignInCheck check;
    std::unique_ptr<ClassicSession> session;
};
SavedGameSignIn signInToSavedGame(std::shared_ptr<const game::Rules> rules, const std::filesystem::path& file, int player,
                                  std::string_view password);

// A PBEM turn file for empire `player` with `password`: the password (against
// the empire's verifier, in the forms passwordForms gives), the data set and
// mods the game was made with, then the turn as beginPbemTurn opens it (the
// host key the game trusts, the turn file's empire, its turn). Signed in, the
// game and the turn are ready for ClassicSession::pbem.
struct TurnFileSignIn {
    SignInCheck check;
    std::optional<PbemGame> game;
    std::optional<PbemTurn> turn;
};
TurnFileSignIn signInToTurnFile(const game::Rules& rules, const std::filesystem::path& file, int player, std::string_view password,
                                const std::filesystem::path& ordersDir = {}, const PbemTrust& trust = {});

// ---- Hosting -------------------------------------------------------------------------------------

struct HostOutcome {
    int exitCode = 0;
    bool wrongPassword = false;        // the original quits without a word
    std::string error;                 // why the turn was not processed (empty: it was)
    std::vector<std::string> report;   // net::pbem::reportLines of the turn processed
};

// The host's step from the command line: a PBEM game file (one with a game
// id) whose master password is `password` (as the original compares them; no
// master password: an empty one) is processed from the .plr files in
// `ordersDir` (empty: the game file's folder), saved, and its new turn files
// written beside it, as `opense4-server pbem process` does. Anything else is
// refused with kHostRefused: a wrong password, a file of another kind, a
// local game, a game the data set does not fit.
HostOutcome hostTurn(const game::Rules& rules, const std::filesystem::path& gameFile, std::string_view password,
                     const std::filesystem::path& ordersDir, const net::secure::PbemHostKeys& host);

// The data set to host `gameFile` with: the game folder of `dataDir` with
// `mods` when `modsGiven` (--mod, --no-mods, the command line's mod folder),
// else with the mods the game recorded, found by id and identity in
// `folders`, as `opense4-server pbem process` finds them.
std::expected<std::shared_ptr<const game::Rules>, std::string> rulesForHosting(const std::filesystem::path& dataDir, const std::filesystem::path& gameFile,
                                                                                const std::vector<std::string>& mods, bool modsGiven,
                                                                                const mods::ModFolders& folders, const mods::OpenOptions& open);

} // namespace opense4::client::classic
