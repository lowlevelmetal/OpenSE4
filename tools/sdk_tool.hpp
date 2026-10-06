#pragma once

// opense4-sdk's commands for computer players (docs/sdk/bots-and-arena.md):
// test, run, arena, env-host, bot and python, beside the mod tools of
// sdk.cpp. The helpers here are shared by them.

#include "game/rules.hpp"
#include "game/setup.hpp"
#include "mods/mod_set.hpp"
#include "mods/package.hpp"

#include <expected>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::sdktool {

// ---- From sdk.cpp ------------------------------------------------------------------------------

// Where mods are found by id: `modsDir` (--mods-dir; empty: the mods folder of
// OpenSE4's user folder), then the mods that come with OpenSE4 unless the
// command line has --no-bundled-mods (which main() takes out and passes to setBundledMods).
mods::ModFolders modFolders(const std::string& modsDir);
void setBundledMods(bool on);

// A mod named on the command line: a folder or .zip, or the id of a mod in modFolders(modsDir).
std::expected<mods::Package, std::string> openMod(const std::string& what, const std::string& modsDir);
// `targets` with the mods they require (`others`, else found in the mods folder), in load order.
std::expected<mods::ModSet, std::vector<std::string>> modsWithDependencies(std::vector<mods::Package> targets, const std::vector<std::string>& others,
                                                                         const std::string& modsDir);

// ---- Shared ----------------------------------------------------------------------------------

int fail(std::string_view message, int code = 2);

// Options: --key=value, --key value, flags, positional arguments, and
// everything after "--" as it is.
struct Options {
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>, std::less<>> values;
    std::vector<std::string> rest;   // after "--"

    bool has(std::string_view key) const { return values.contains(key); }
    std::string get(std::string_view key, std::string fallback = {}) const;
    std::vector<std::string> all(std::string_view key) const;
    std::expected<int64_t, std::string> integer(std::string_view key, int64_t fallback, int64_t min, int64_t max) const;
};
std::expected<Options, std::string> parseOptions(const std::vector<std::string>& argv, size_t from, std::initializer_list<std::string_view> valued,
                                                 std::initializer_list<std::string_view> flags);

// The data set (--data or --classic-dir, else the installed game) with mods.
struct LoadedRules {
    std::unique_ptr<game::Rules> rules;
    std::filesystem::path root;      // the game folder
    std::filesystem::path dataDir;   // its data folder
    mods::ModSet mods;
};
std::expected<LoadedRules, std::string> loadRules(const std::string& dataArg, mods::ModSet mods);
// --mod (paths or ids, in load order) and --mods-dir, as the game and the server read them.
// (Defined in sdk_common.cpp, as modFolders and setBundledMods are.)
std::expected<mods::ModSet, std::string> chooseMods(const std::vector<std::string>& mods, const std::string& modsDir);
// --data, or --classic-dir as the client calls it.
std::string dataOption(const Options& o);

// How the games of the arena, the environment and `test` are set up.
struct GameChoice {
    std::filesystem::path setupFile;   // a server setup file (docs/MULTIPLAYER.md): options and empires
    int systems = 0;                   // 0: from the quadrant size
    int quadrantSize = 0;              // 0 small, 1 medium, 2 large
    std::string quadrant;              // a quadrant type of the data set (empty: the first)
    bool turnBased = false;
    std::vector<std::string> races;    // seat i plays races[i % size]; empty: drawn from the seed
};
// A game for `seats` empires with `seed`; empires come from the setup file
// when it has some (then it decides how many there are).
std::expected<game::GameSetup, std::string> makeGameSetup(const game::Rules& r, const GameChoice& choice, uint64_t seed, size_t seats);
// The empires a setup file has (0 without a file).
std::expected<size_t, std::string> setupFileEmpires(const game::Rules& r, const std::filesystem::path& file);

// Writes the `opense4` package OpenSE4 is built with into dir/opense4,
// for CPython bots; returns `dir`.
std::expected<std::filesystem::path, std::string> writePythonPackage(const std::filesystem::path& dir);
// Where `opense4-sdk python` and `bot` keep it: <user folder>/python/<version>.
std::filesystem::path pythonPackageHome();
// PYTHONPATH with `dir` in front of what the environment has.
std::string pythonPathWith(const std::filesystem::path& dir);

// ---- Commands ----------------------------------------------------------------------------------

int cmdTest(const std::vector<std::string>& argv);
int cmdRun(const std::vector<std::string>& argv);
int cmdArena(const std::vector<std::string>& argv);
int cmdArenaGame(const std::vector<std::string>& argv);
int cmdEnvHost(const std::vector<std::string>& argv);
int cmdBot(const std::vector<std::string>& argv);
int cmdPython(const std::vector<std::string>& argv);

} // namespace opense4::sdktool
