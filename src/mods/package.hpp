#pragma once

// A mod package: a folder or a .zip with a manifest (mod.toml), or a classic
// mod, a folder of replacement data files and pictures without one
// (docs/MODDING_SDK.md §3, docs/sdk/packages-and-data.md).
//
//     better-carriers/
//       mod.toml          the manifest
//       data/             patches (*.toml), replacement data files (*.txt), generators (*.py),
//                         and AI tables, race files and design names in the game folder's layout
//       assets/           pictures, sounds, music, fonts and pointers in the game folder's layout
//       ai/  scripts/  ui/  text/  tests/
//
// A package's identity hashes its manifest and every file outside assets/,
// ui/ and text/ (text files with their line ends as LF), so the same files
// give the same identity on every computer; changing a picture, a panel or a
// translation does not change it.

#include "mods/manifest.hpp"
#include "ruleset/mods.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::mods {

// What a package holds (docs/MODDING_SDK.md §2), as bits.
enum Tier : unsigned {
    kTierAssets = 1u << 0,     // pictures, sounds, music, fonts
    kTierData = 1u << 1,       // data patches and files, AI tables
    kTierAi = 1u << 2,         // computer players (Python)
    kTierScripts = 1u << 3,    // rules hooks (Python)
    kTierInterface = 1u << 4,  // interface extensions
    kTierText = 1u << 5,       // strings and translations
};
// "assets, data": the tiers, for messages.
std::string tierNames(unsigned tiers);
// Tiers that change how a game plays: every player of a game needs the same mods with them.
inline constexpr unsigned kGameTiers = kTierData | kTierAi | kTierScripts;

struct PackageFile {
    std::string path;            // in the package, '/'-separated, as spelled on disk
    std::filesystem::path real;  // on disk (a .zip's unpacked copy)
    uint64_t size = 0;
};

// A file of the package as the game folder has it.
struct GameMount {
    std::string installPath;     // "Data/Components.txt", "Ai/Default_AI_Research.txt"
    std::string packagePath;     // where it is in the package ("data/Components.txt")
    std::filesystem::path real;
};

struct Package {
    std::filesystem::path source;  // as given: the folder or the .zip
    std::filesystem::path root;    // the folder read (a .zip's unpacked copy)
    bool classic = false;          // no manifest: a classic mod
    bool zipped = false;
    Manifest manifest;             // a classic mod's is made up: id "classic.<folder>", version 0
    std::vector<PackageFile> files;  // sorted by path
    unsigned tiers = 0;
    std::string hash;              // the identity: 32 hex digits
    std::vector<std::string> warnings;  // things opense4-sdk check reports

    const std::string& id() const { return manifest.id; }
    bool affectsGame() const { return (tiers & kGameTiers) != 0; }
    ruleset::ModRecord record() const;
    std::string label() const;     // "example.better-carriers 1.2.0"
    // The game files (data folder files, AI tables, race files, design
    // names) it puts in the game folder: a later mod's copy wins. Part of the identity.
    std::vector<GameMount> gameFiles() const;
    // The folder that mirrors the game folder's layout with its pictures,
    // sounds, music, fonts and pointers; empty when it has none.
    std::filesystem::path assetRoot() const;
    // data/*.toml (patches) and data/*.py (generators), by name: the order they apply in.
    std::vector<const PackageFile*> dataScripts() const;
    const PackageFile* file(std::string_view path) const;  // any case
};

struct OpenOptions {
    // Where .zip packages are unpacked (once per archive content); empty: a
    // folder under the system's temporary folder.
    std::filesystem::path cacheDir;
};

// Reads a package: a folder or a .zip (with mod.toml at its top, or in its one
// top folder). Without mod.toml it is a classic mod.
std::expected<Package, std::string> openPackage(const std::filesystem::path& path, const OpenOptions& options = {});

// The identity of a package's files (see the top of this file); exposed for tests and the SDK.
std::string packageHash(const std::vector<PackageFile>& files, bool classic);

// Whether a package path is a text file whose line ends are normalized for the identity.
bool isTextPath(std::string_view path);

// The file opense4-sdk pack adds at a package's top with its identity; not
// part of the identity itself. A package whose files no longer match it gets a warning.
inline constexpr std::string_view kIdentityFile = "mod.identity";
std::string identityFileText(const Package& p);

} // namespace opense4::mods
