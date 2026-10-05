#pragma once

// The game folder's files as the rules read them: the data files, the
// computer players' tables (Ai/ and the race folders) and the design-name
// lists. Paths are relative to the game folder (the one holding Data/,
// Pictures/ and Ai/), with '/' separators, and match in any letter case, as
// Windows matches them. The install alone is one implementation
// (openInstallFiles); mods layer their files and data patches over it
// (mods/data_set.hpp, docs/sdk/packages-and-data.md).

#include "datafile/datafile.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opense4::ruleset {

struct FileEntry {
    std::string name;  // as spelled on disk (by the topmost layer that has it)
    bool directory = false;
};

class GameFiles {
public:
    virtual ~GameFiles() = default;

    // The game folder; empty for a bare data folder (test data sets).
    virtual const std::filesystem::path& root() const = 0;
    // The data folder (the one holding Components.txt).
    virtual const std::filesystem::path& dataDir() const = 0;
    // A data file by name ("Components.txt"), with every patch applied.
    virtual std::expected<datafile::DataFile, std::string> dataFile(std::string_view name) const = 0;
    // Any other game file by its path in the game folder: an AI table, a race
    // file, a design-name list. Patched where patches apply.
    virtual std::expected<datafile::DataFile, std::string> file(std::string_view relative) const = 0;
    // Where on disk a game file is (the topmost layer's copy), for files read
    // as they are, such as the design-name lists; nullopt when there is none.
    virtual std::optional<std::filesystem::path> path(std::string_view relative) const = 0;
    // A folder's game files and subfolders, sorted by name in any case (the
    // spelling as written breaks ties); empty when there is no such folder.
    virtual std::vector<FileEntry> list(std::string_view relativeDir) const = 0;
    // Tells file sets apart, for caches of tables read from them.
    virtual const std::string& cacheKey() const = 0;
    // Hashes the install's game files besides the data folder (the AI tables,
    // race files and design-name lists): part of game::dataSetIdentity, which
    // adds the mods (their own identities cover what they change).
    virtual uint64_t fingerprint() const = 0;
};

// Whether a path in the game folder is a game file, one the rules read: the
// data folder's files, everything under Ai/ and Dsgnname/, and the text files
// of the race folders (Pictures/Races/, Pictures/RaceNeutral/). Pictures,
// sounds, music and fonts are not.
bool isGameFile(std::string_view relative);
// Whether a folder may hold game files (for indexing only those).
bool mayHoldGameFiles(std::string_view relativeDir);

// The lowercase, '/'-separated form of a relative path, as indexes key it.
std::string indexKey(std::string_view relative);

// A case-insensitive index of files, by their path in the game folder.
class FileIndex {
public:
    FileIndex() = default;
    // Every game file (isGameFile) under `root`.
    static FileIndex ofGameFiles(const std::filesystem::path& root);

    // `relative` is the file's path in the game folder as written; a path
    // already present (in any case) keeps its first entry.
    void add(std::string_view relative, std::filesystem::path real);
    const std::filesystem::path* find(std::string_view relative) const;
    // The entries of a folder, merged into `out` (lowercase name -> entry):
    // existing names are replaced, so later layers win.
    void listInto(std::string_view relativeDir, std::map<std::string, FileEntry>& out) const;
    // Every file: (path as written, real path), sorted by the lowercase path.
    std::vector<std::pair<std::string, std::filesystem::path>> files() const;
    bool empty() const { return files_.empty(); }

private:
    struct Entry {
        std::string written;
        std::filesystem::path real;
    };
    std::unordered_map<std::string, Entry> files_;                     // lowercase path -> file
    std::map<std::string, std::map<std::string, FileEntry>> folders_;  // lowercase folder -> lowercase name -> entry
};

// Sorts entries as GameFiles::list() returns them.
std::vector<FileEntry> sortedEntries(const std::map<std::string, FileEntry>& entries);

// Reads a file into memory with CR LF and lone CR as LF: the form the
// identities hash, the same on every platform.
std::string readNormalized(const std::filesystem::path& file);
// GameFiles::fingerprint of an install's index: every game file outside the
// data folder, by lowercase path, with its content line ends normalized.
uint64_t hashGameFiles(const FileIndex& index);

// The install alone, as the original reads it (no mods). `dataDir` defaults
// to the game folder's Data folder.
std::shared_ptr<const GameFiles> openInstallFiles(std::filesystem::path root, std::filesystem::path dataDir = {});

} // namespace opense4::ruleset
