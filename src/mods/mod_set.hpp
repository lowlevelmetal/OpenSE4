#pragma once

// Mod sets: the mods a game is played with, in load order
// (docs/sdk/packages-and-data.md "Load order"), and the player's choice of
// them: the mods folder, --mod on the command line, a server's setup file
// and the classic settings file. Mods are found by id in the player's mods
// folder, then among the mods that come with OpenSE4 (the bundled folder).

#include "mods/package.hpp"
#include "ruleset/mods.hpp"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace opense4::mods {

struct ModSet {
    std::vector<Package> packages;  // in load order: a later mod wins

    bool empty() const { return packages.empty(); }
    std::vector<ruleset::ModRecord> records() const;
    // The identity of its game-affecting mods (ruleset::modSetIdentity).
    std::string identity() const;
};

// Orders the enabled packages: each after the mods it requires and the ones
// its `[load] after` names (when enabled), otherwise as the player ordered
// them. Every problem is reported: a duplicate id, a required mod that is not
// enabled or has a version outside the range, a cycle.
std::expected<ModSet, std::vector<std::string>> resolveModSet(std::vector<Package> enabled);

// The packages in a mods folder: each subfolder and .zip is one. Packages
// that cannot be read are listed in `problems`.
struct ModLibrary {
    std::vector<Package> packages;  // by folder name; the player's before the bundled ones
    std::vector<std::string> problems;
    // Bundled mods that a mod of the player's folder with the same id replaces.
    std::vector<Package> replaced;

    const Package* find(std::string_view id) const;
};
ModLibrary scanModsFolder(const std::filesystem::path& dir, const OpenOptions& options = {});

// Where mods are found by id (docs/sdk/packages-and-data.md "Where mods are
// found"): the player's mods folder first, then the mods that come with
// OpenSE4. A mod of the player's folder replaces a bundled one with its id.
struct ModFolders {
    std::filesystem::path user;      // <user data>/Mods, or --mods-dir
    std::filesystem::path bundled;   // bundledModsFolder(); empty: none (--no-bundled-mods)

    // For messages: "the mods folder <user>", and " or the mods that come with OpenSE4 (<bundled>)".
    std::string describe() const;
};

// The bundled folder's list of the mods it holds: in the source tree's mods/
// folder, the names of the folders that ship with OpenSE4, one a line ('#'
// starts a comment). The release packages copy those folders into mods/
// beside the programs (tools/package_release.sh), without the list.
inline constexpr std::string_view kBundledListFile = "bundled.txt";
std::vector<std::string> readBundledList(const std::filesystem::path& file);

// The mods of a bundled folder, marked bundled: those its bundled.txt names
// when it has one (the source tree's mods/), else every mod in it (a release's).
ModLibrary scanBundledMods(const std::filesystem::path& dir, const OpenOptions& options = {});
// The player's mods, then the bundled ones whose ids the player's folder has
// not (the others go to `replaced`).
ModLibrary scanMods(const ModFolders& folders, const OpenOptions& options = {});

// The folder of the mods that come with OpenSE4, for programs in `programDir`:
// mods/ beside them (a release package, the Windows install) when it is a
// folder; else, in a developer build (OPENSE4_DEV_PATHS), the source tree's
// mods/, of which only the folders its bundled.txt names count. Empty when
// there is neither.
std::filesystem::path bundledModsFolder(const std::filesystem::path& programDir);
// The same with the source tree's mods/ folder given (empty: none); for tests.
std::filesystem::path bundledModsFolderFor(const std::filesystem::path& programDir, const std::filesystem::path& sourceMods);

// The player's choice of mods. Each entry of `mods` is a path to a package
// (folder or .zip; relative paths from `baseDir`) or the id of one in `folders`.
struct ModChoice {
    std::vector<std::string> mods;     // in the player's order
    ModFolders folders;                // where ids are looked up
    std::filesystem::path baseDir;     // what relative paths are relative to (empty: the current folder)
    OpenOptions open;
};
std::expected<ModSet, std::vector<std::string>> selectMods(const ModChoice& choice);

// The mods a game recorded (GameState::mods), found by id in `folders` and
// checked against the recorded identity: what a host loading a saved game
// needs. A mod of the player's folder replaces a bundled one with its id here
// too. Only game-affecting mods are needed; asset mods are taken when found.
std::expected<ModSet, std::vector<std::string>> modsForGame(std::span<const ruleset::ModRecord> recorded, const ModFolders& folders,
                                                            const OpenOptions& options = {});

// The folder of mods under a user data folder: <user data>/Mods.
std::filesystem::path modsFolderIn(const std::filesystem::path& userDataDir);
// Where .zip mods are unpacked: <user data>/ModCache.
std::filesystem::path modCacheIn(const std::filesystem::path& userDataDir);

// A headless model of the mod manager a setup screen shows: the mods there
// are, which are on and in what order, and what resolving them gives.
class ModManager {
public:
    explicit ModManager(ModLibrary library, std::vector<std::string> enabled = {});

    const ModLibrary& library() const { return library_; }
    const std::vector<std::string>& enabled() const { return enabled_; }  // ids, in the player's order
    bool isEnabled(std::string_view id) const;
    void enable(std::string_view id);   // at the end
    void disable(std::string_view id);
    void move(std::string_view id, int by);  // earlier (< 0) or later (> 0) in the order
    // The load order the enabled mods give, or what is wrong with them.
    std::expected<ModSet, std::vector<std::string>> resolve() const;
    // What a package holds, for the manager's list: "assets, data; changes the game".
    static std::string summary(const Package& p);

private:
    ModLibrary library_;
    std::vector<std::string> enabled_;
};

} // namespace opense4::mods
