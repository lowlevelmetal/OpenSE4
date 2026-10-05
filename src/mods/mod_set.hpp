#pragma once

// Mod sets: the mods a game is played with, in load order
// (docs/sdk/packages-and-data.md "Load order"), and the player's choice of
// them: the mods folder, --mod on the command line, a server's setup file
// and the classic settings file.

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
    std::vector<Package> packages;  // by folder name
    std::vector<std::string> problems;

    const Package* find(std::string_view id) const;
};
ModLibrary scanModsFolder(const std::filesystem::path& dir, const OpenOptions& options = {});

// The player's choice of mods. Each entry of `mods` is a path to a package
// (folder or .zip; relative paths from `baseDir`) or the id of one in `modsDir`.
struct ModChoice {
    std::vector<std::string> mods;     // in the player's order
    std::filesystem::path modsDir;     // where ids are looked up
    std::filesystem::path baseDir;     // what relative paths are relative to (empty: the current folder)
    OpenOptions open;
};
std::expected<ModSet, std::vector<std::string>> selectMods(const ModChoice& choice);

// The mods a game recorded (GameState::mods), found by id in `modsDir` and
// checked against the recorded identity: what a host loading a saved game
// needs. Only game-affecting mods are needed; asset mods are taken when found.
std::expected<ModSet, std::vector<std::string>> modsForGame(std::span<const ruleset::ModRecord> recorded, const std::filesystem::path& modsDir,
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
