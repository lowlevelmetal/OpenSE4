#pragma once

// A data set with mods (docs/sdk/packages-and-data.md): the install's game
// files with each mod's layered over them in load order, and every mod's
// data patches applied to the parsed records before the typed loaders read
// them. GameData is the ruleset::GameFiles the rules then read their data
// files, AI tables, race files and design names from; nothing is written into
// the install.
//
// For each mod in load order: its replacement files (data/*.txt for the data
// folder, data/Ai/..., data/Pictures/Races/... and data/Dsgnname/... in the
// game folder's layout) take the place of the earlier ones, then its patches
// (data/*.toml and data/*.py, by name) apply. Once every mod is applied, the
// records a patch removed are checked for references (and the referring
// records removed with cascade = true); after the typed load, what a patch
// wrote that no reader reads is reported.

#include "mods/apply.hpp"
#include "mods/generator.hpp"
#include "mods/mod_set.hpp"
#include "mods/patch.hpp"
#include "ruleset/files.hpp"
#include "ruleset/ruleset.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace opense4::mods {

struct LoadOptions {
    // Runs data/*.py generators; null: defaultGeneratorRunner() (they are errors).
    std::shared_ptr<GeneratorRunner> generators;
};

class GameData final : public ruleset::GameFiles {
public:
    // `gameRoot` may be empty (a bare data folder); `dataDir` defaults to its
    // Data folder. Problems go to `diag`, each naming the mod, file, line and record.
    static std::shared_ptr<GameData> build(std::filesystem::path gameRoot, std::filesystem::path dataDir, ModSet mods, const LoadOptions& options,
                                           datafile::Diagnostics& diag);

    const std::filesystem::path& root() const override { return root_; }
    const std::filesystem::path& dataDir() const override { return dataDir_; }
    std::expected<datafile::DataFile, std::string> dataFile(std::string_view name) const override;
    std::expected<datafile::DataFile, std::string> file(std::string_view relative) const override;
    std::optional<std::filesystem::path> path(std::string_view relative) const override;
    std::vector<ruleset::FileEntry> list(std::string_view relativeDir) const override;
    const std::string& cacheKey() const override { return key_; }
    uint64_t fingerprint() const override;

    const ModSet& mods() const { return mods_; }
    const std::vector<ruleset::DeclaredAbility>& declaredAbilities() const { return declared_; }
    // The patched data files the loader reads, by name.
    std::vector<const datafile::DataFile*> dataFiles() const;
    // Every computer players' table file: its path in the game folder and the patched file.
    std::vector<std::pair<std::string, const datafile::DataFile*>> aiFiles() const;
    // After the typed load (`load` holds its diagnostics): fields a patch
    // wrote that no reader reads and no file of the table has, as errors in `out`.
    void checkAfterLoad(const datafile::Diagnostics& load, datafile::Diagnostics& out) const;

private:
    GameData() = default;
    struct Layer {
        std::string name;
        ruleset::FileIndex index;  // the mod's game files, by their path in the game folder
    };
    struct AiFile {
        std::string written;  // the path as spelled
        std::string table;    // "Research"
        datafile::DataFile file;
    };
    struct Known {
        std::set<std::string> keys;      // datafile::normalizeKey of every key a file of the table uses
        std::set<std::string> patterns;  // their datafile::keyPattern
    };

    void applyMods(const LoadOptions& options, std::vector<std::string>& errors);
    void loadBase(std::vector<std::string>& errors);
    void learn(const TableSpec& table, const datafile::DataFile& file);
    datafile::DataFile* dataFileOf(const TableSpec& table, bool create);
    void apply(const PatchSet& set, std::vector<std::string>& errors);
    void declare(const PatchSet& set, std::vector<std::string>& errors);
    void checkReferences(std::vector<std::string>& errors);

    std::filesystem::path root_, dataDir_;
    ModSet mods_;
    ruleset::FileIndex install_;
    std::vector<Layer> layers_;                       // one per mod, in load order
    std::map<std::string, datafile::DataFile> data_;  // lowercase file name -> patched
    std::map<std::string, AiFile> ai_;                // lowercase path -> patched
    std::map<std::string, Known> known_;              // table name -> its keys
    std::vector<Removal> removals_;
    std::vector<ruleset::DeclaredAbility> declared_;
    std::string key_;
    mutable std::once_flag fingerprintOnce_;
    mutable uint64_t fingerprint_ = 0;
};

struct LoadedDataSet {
    std::optional<ruleset::Ruleset> ruleset;  // absent when a required data file could not be read
    datafile::Diagnostics diagnostics;
    std::shared_ptr<const GameData> data;
};

// The install's data set with these mods: built, patched, loaded and checked.
// The ruleset keeps its files (Ruleset::files) and its mods (Ruleset::mods).
LoadedDataSet loadDataSet(const std::filesystem::path& gameRoot, const std::filesystem::path& dataDir, const ModSet& mods,
                          const LoadOptions& options = {});

// The AI table a path in the game folder holds, where the computer players
// look for tables (Ai/, Ai/<style>/, Pictures/Races/<race>/,
// Pictures/RaceNeutral/<race>/): "Research", or nullopt.
std::optional<std::string> aiTableAt(std::string_view relative);

} // namespace opense4::mods
