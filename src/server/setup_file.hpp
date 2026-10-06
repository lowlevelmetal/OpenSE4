#pragma once

// Game setup files for the dedicated server (TOML; docs/MULTIPLAYER.md has
// the full format):
//
//     name = "Frontier"
//     seed = 1234
//     master_password = "secret"
//     [options]
//     systems = 30
//     ai = "example.admiral:Admiral"   # the computer empires' player (default: the built-in AI)
//     [[empire]]
//     race = "Terran"
//     kind = "human"
//     player = "alice"
//     password = "pw"
//     [[empire]]
//     kind = "computer"
//     ai = "builtin"                   # this one's own (docs/sdk/ai-protocol.md §1)
//
// Unknown keys are errors, to catch typos.

#include "game/rules.hpp"
#include "game/setup.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace opense4::server {

struct SetupEmpire {
    game::EmpireSetup setup;      // passwordHash stays empty: the game makes the verifier (net/auth.hpp)
    std::string player;           // login name in network games
    std::string password;         // the empire's password, as written
    std::string passwordVerifier; // or its verifier in the game `game_id` (net::passwordVerifier)
};

struct SetupFile {
    std::string gameName;
    // `mods = [...]`: the mods to play with, each a package's path (relative
    // to the setup file's folder) or the id of one in the mods folder, in
    // load order (docs/sdk/packages-and-data.md). Read before the data set
    // loads (setupFileMods), as the rules depend on them.
    std::vector<std::string> mods;
    std::optional<uint64_t> seed;
    // The game's id, which salts its passwords: needed for verifiers made in
    // advance (opense4-server password-verifier --game-id=N).
    std::optional<uint64_t> gameId;
    std::string masterPassword;          // master_password, as written
    std::string masterPasswordVerifier;  // or master_password_verifier (needs game_id)
    // A network or e-mail game is simultaneous unless the file says
    // `simultaneous = false` (docs/MULTIPLAYER.md); a new local game is
    // turn-based (GameOptions).
    game::GameOptions options = [] {
        game::GameOptions o;
        o.simultaneous = true;
        return o;
    }();
    std::vector<SetupEmpire> empires;
    // `ai`: who plays the computer empires that do not name their own
    // (EmpireSetup::controller; neutral empires only by their own); nullopt:
    // the built-in AI.
    std::optional<game::Controller> ai;
};

// Only the `mods` of a setup file, with relative paths made relative to its
// folder: what the data set needs before the rest can be read.
std::expected<std::vector<std::string>, std::string> setupFileMods(const std::filesystem::path& file);
std::expected<std::vector<std::string>, std::string> parseSetupMods(std::string_view text, const std::string& sourceName,
                                                                   const std::filesystem::path& baseDir);

// Parses a setup file; `rules` checks race and quadrant names.
std::expected<SetupFile, std::string> loadSetupFile(const std::filesystem::path& file, const game::Rules& rules);
std::expected<SetupFile, std::string> parseSetup(std::string_view text, const std::string& sourceName, const game::Rules& rules);

} // namespace opense4::server
