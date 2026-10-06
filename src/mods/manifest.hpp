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
//     [rules]                          # rules scripts (docs/sdk/rules.md)
//     players_see_mod_data = true      # computer players see their own things' mod data
//     [[rules.options]] / [[rules.orders]] / [[rules.events]] /
//     [[rules.intel_projects]] / [[rules.victory]]
//
// Unknown tables and keys are errors, to catch typos.

#include "mods/version.hpp"
#include "script/value.hpp"

#include <cstdint>
#include <expected>
#include <optional>
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

// ---- Rules declarations ([rules], docs/sdk/rules.md) -----------------------------------------------

// A game option a mod declares ([[rules.options]]): a whole number in a
// range, or a switch (0 or 1).
struct ModOptionDecl {
    std::string name;         // lowercase letters, digits and '_'; unique in the mod
    std::string label;        // what the setup screens show
    std::string description;
    bool isSwitch = false;    // type = "bool" (else "int")
    int64_t min = 0, max = 0;
    int64_t defaultValue = 0;
    int line = 0;
};

// An argument of a mod's order ([[rules.orders.args]]).
struct ModArgDecl {
    std::string name;
    // "int", "bool", "text", or what an id names: "empire", "system",
    // "object", "colony", "vehicle", "fleet", "design".
    std::string type;
    std::optional<int64_t> min, max;   // int
    script::Value defaultValue;        // null: the argument must be given (an id: may be null)
    bool hasDefault = false;
    int line = 0;
};

// An order a mod declares ([[rules.orders]]; cmd::ModCommand).
struct ModOrderDecl {
    std::string name;
    std::string label;
    std::string description;
    // What it is given to: "vehicle", "fleet", "colony" (one of the
    // empire's), "empire" (another empire) or "self" (the empire itself).
    std::string appliesTo = "self";
    std::vector<ModArgDecl> args;
    int line = 0;
};

// An event a mod declares ([[rules.events]]): rolled every game turn after
// the classic events, its effect the mod's.
struct ModEventDecl {
    std::string name;
    std::string label;
    int chance = 0;            // percent per game turn, 0 to 100
    std::string target = "none";   // "empire", "colony", "vehicle", "system" or "none"
    uint32_t firstTurn = 0;    // not rolled before this game turn
    std::string option;        // a switch of the mod's options that turns it on; empty: always
    int line = 0;
};

// An intelligence project type a mod carries out ([[rules.intel_projects]]):
// IntelProjects.txt records of this Type are the mod's projects.
struct ModIntelDecl {
    std::string type;
    std::string description;
    int line = 0;
};

// A victory condition a mod declares ([[rules.victory]]).
struct ModVictoryDecl {
    std::string name;
    std::string label;         // the reason the game ends, as the end screens show it
    std::string option;        // a switch of the mod's options that turns it on; empty: always
    int line = 0;
};

struct RulesDecl {
    bool playersSeeModData = false;
    // The modules under scripts/ to import, in order; empty: every module
    // directly under scripts/ (files and packages), by name.
    std::vector<std::string> modules;
    std::vector<ModOptionDecl> options;
    std::vector<ModOrderDecl> orders;
    std::vector<ModEventDecl> events;
    std::vector<ModIntelDecl> intelProjects;
    std::vector<ModVictoryDecl> victories;

    const ModOptionDecl* option(std::string_view name) const;
    const ModOrderDecl* order(std::string_view name) const;
    const ModEventDecl* event(std::string_view name) const;
    const ModVictoryDecl* victory(std::string_view name) const;
    bool empty() const;
};

// The id kinds a mod order's argument may name.
bool isModArgType(std::string_view type);
// The things a mod order may be given to.
bool isModOrderTarget(std::string_view appliesTo);
// A declared name: lowercase letters, digits and '_', starting with a letter, at most 64.
bool validRulesName(std::string_view name);

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
    RulesDecl rules;                        // [rules]

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
