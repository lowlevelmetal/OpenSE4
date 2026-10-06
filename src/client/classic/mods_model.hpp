#pragma once

// Mods in the client (docs/sdk/packages-and-data.md, "Choosing mods in the
// game"): the mods the data set in use was built with, the Mods window's
// choice as a model without any drawing (tested directly), and what a saved
// game needs of the mods. The window itself is screens/mods.cpp.

#include "mods/mod_set.hpp"
#include "ruleset/mods.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {
class Rules;
}

namespace opense4::client::classic {

// The mods the data set in use was built with (ClassicMode sets them each
// time it reads the data), and where mods are looked for.
struct LoadedMods {
    std::vector<mods::Package> packages;   // in load order
    bool fromCommandLine = false;          // --mod or --no-mods chose them for this run
    // The settings' mods could not be loaded when OpenSE4 started: it started
    // without them, and the Mods window says why.
    std::vector<std::string> startProblems;
    mods::ModFolders folders;              // where ids are looked up: <user data>/Mods (or --mods-dir), then the bundled mods
    mods::OpenOptions open;                // where .zip mods are unpacked
    uint64_t generation = 0;               // counts the data sets read: what was set up for an earlier one is stale
};
LoadedMods& loadedMods();

// For the setup screens: "none", or the mods by name in load order and
// whether they change the game.
std::string modsSummary(const std::vector<mods::Package>& packages);

// Whether a message names the mod: its id as a word of its own.
bool mentionsMod(std::string_view text, std::string_view id);

// The ids of mods with computer players or rules scripts (what the original's
// saved games cannot hold).
std::vector<std::string> scriptedMods(const std::vector<mods::Package>& packages);

// The Mods window's choice: the mods in the folder, the ones the player
// enables and their order, and what is wrong with that choice.
class ModsChoice {
public:
    // `enabled`: the ids chosen so far (the settings'), in order; ids not in
    // the folder are kept as missing until the player removes them. `start`:
    // the choice changed() compares with (`enabled` when not given).
    ModsChoice(mods::ModLibrary library, std::vector<std::string> enabled, std::optional<std::vector<std::string>> start = std::nullopt);

    struct Row {
        std::string id;
        const mods::Package* package = nullptr;   // null: missing from the folder
        bool enabled = false;
        int order = 0;                            // 1-based among the enabled; 0 when off
    };
    // The enabled mods in the player's order (missing ones among them), then
    // the others by name.
    std::vector<Row> rows() const;
    const mods::ModLibrary& library() const { return manager_.library(); }
    // The ids the player chose, in order, the missing ones left out (what the settings keep).
    const std::vector<std::string>& enabled() const { return manager_.enabled(); }
    const std::vector<std::string>& missing() const { return missing_; }
    bool isEnabled(std::string_view id) const;

    void toggle(std::string_view id);
    void move(std::string_view id, int by);
    // Forgets an enabled id that is not in the folder.
    void dropMissing(std::string_view id);

    // The load order the choice gives, or every problem with it (missing
    // mods first, then the requirements, duplicates and circles).
    std::expected<mods::ModSet, std::vector<std::string>> resolve() const;
    // The problems that name this mod.
    std::vector<std::string> problemsOf(std::string_view id) const;
    // Different from the choice it started with (ids or order).
    bool changed() const;

private:
    mods::ModManager manager_;
    std::vector<std::string> missing_;
    std::vector<std::string> start_;
};

// What a saved game needs of the mods when the ones in use differ: each
// difference, and the game's mods as the mods folders have them (or why they
// do not have them all).
struct SavedGameMods {
    std::vector<std::string> differences;
    std::vector<ruleset::ModRecord> recorded;          // the game's mods, in its load order
    std::vector<std::string> ids;                      // the ids to enable for it (in that order), when all are there
    std::vector<std::string> unavailable;              // why not, otherwise
};
// nullopt when the game's mods are the ones in use (or the file is no
// OpenSE4 save: its own loading says what is wrong with it).
std::optional<SavedGameMods> savedGameMods(const std::filesystem::path& file, const game::Rules& rules, const LoadedMods& loaded);

} // namespace opense4::client::classic
