#pragma once

// Game setup files for the dedicated server (TOML; docs/MULTIPLAYER.md has
// the full format):
//
//     name = "Frontier"
//     seed = 1234
//     master_password = "secret"
//     [options]
//     systems = 30
//     [[empire]]
//     race = "Terran"
//     kind = "human"
//     player = "alice"
//     password = "pw"
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
};

// Parses a setup file; `rules` checks race and quadrant names.
std::expected<SetupFile, std::string> loadSetupFile(const std::filesystem::path& file, const game::Rules& rules);
std::expected<SetupFile, std::string> parseSetup(std::string_view text, const std::string& sourceName, const game::Rules& rules);

} // namespace opense4::server
