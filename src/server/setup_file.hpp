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
    game::EmpireSetup setup;  // passwordHash holds hashPassword() of the empire's password
    std::string player;       // login name in network games
};

struct SetupFile {
    std::string gameName;
    std::optional<uint64_t> seed;
    std::string masterPasswordHash;  // hashPassword() of master_password
    game::GameOptions options;
    std::vector<SetupEmpire> empires;
};

// Parses a setup file; `rules` checks race and quadrant names.
std::expected<SetupFile, std::string> loadSetupFile(const std::filesystem::path& file, const game::Rules& rules);
std::expected<SetupFile, std::string> parseSetup(std::string_view text, const std::string& sourceName, const game::Rules& rules);

} // namespace opense4::server
