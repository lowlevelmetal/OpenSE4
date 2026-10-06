#pragma once

// A mod package's manifest, mod.toml (docs/MODDING_SDK.md §3,
// docs/sdk/packages-and-data.md):
//
//     [mod]
//     id = "example.better-carriers"   # unique; lowercase letters, digits, '.', '-', '_'
//     name = "Better Carriers"
//     version = "1.2.0"
//     api = 1                          # the SDK interface version it needs
//     authors = ["..."]
//     description = "..."
//
//     [requires]
//     "example.common-lib" = ">=1.0"
//
//     [load]
//     after = ["example.common-lib"]   # load-order hints; the player can reorder
//
//     [[ai.players]]                   # computer players (docs/sdk/ai-protocol.md §1)
//     name = "Admiral"
//     module = "admiral"               # a module or package under the mod's ai/ folder
//     class = "Admiral"
//     description = "..."
//
// Unknown tables and keys are errors, to catch typos.

#include "mods/version.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::mods {

// The SDK interface this OpenSE4 offers. A mod that needs a newer one is refused.
inline constexpr int kApiVersion = 1;

struct Requirement {
    std::string id;
    VersionRange range;
    int line = 0;  // in mod.toml
};

// A computer player the mod offers ([[ai.players]], docs/sdk/ai-protocol.md §1).
struct AiPlayer {
    std::string name;         // unique within the mod; the setup screens show it
    std::string module;       // a module or package under ai/: "admiral", "fleet.admiral"
    std::string className;    // a class of that module (`class` in mod.toml)
    std::string description;
    int line = 0;             // in mod.toml
};

struct Manifest {
    std::string id;
    std::string name;
    Version version;
    int api = 0;
    std::vector<std::string> authors;
    std::string description;
    std::vector<Requirement> requirements;  // [requires]
    std::vector<std::string> loadAfter;
    std::vector<AiPlayer> aiPlayers;        // [[ai.players]]

    const AiPlayer* aiPlayer(std::string_view name) const;
};

// A Python identifier as the script runtime takes them: ASCII letters,
// digits and '_', not starting with a digit.
bool validPythonName(std::string_view name);

// Lowercase letters, digits, '.', '-' and '_', 1 to 64 of them, starting with
// a letter or digit: "example.better-carriers".
bool validModId(std::string_view id);

// Parses mod.toml. `source` names it in messages ("mods/carriers/mod.toml");
// every problem is reported with its line.
std::expected<Manifest, std::vector<std::string>> parseManifest(std::string_view text, std::string_view source);

// The manifest as text (opense4-sdk new writes it).
std::string writeManifest(const Manifest& m);

} // namespace opense4::mods
