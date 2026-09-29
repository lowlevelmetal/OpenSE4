#pragma once

// Reader for the moddable "Key := Value" text data format used by classic
// Space Empires data sets (and by our own content, so mods interoperate).
//
// A data file is free-form documentation, then a data section between lines
// starting with *BEGIN* and *END*. Each data line is "Key := Value"; other
// lines in the data section (separators, blanks) are ignored. A record begins
// every time the file's first key appears again. Files without a *BEGIN*
// marker are plain lists: one entry per non-blank line.

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::datafile {

struct Field {
    std::string key;    // trimmed
    std::string value;  // trimmed, may be empty
    int line = 0;       // 1-based line number in the file
};

struct Record {
    std::vector<Field> fields;
    int line = 0;  // line of the first field

    // First field whose key matches (ASCII case-insensitive, whitespace-normalized).
    const Field* find(std::string_view key) const;
};

struct DataFile {
    std::string name;             // file name for diagnostics, e.g. "Components.txt"
    bool hasDataSection = false;  // false: a plain list file
    std::vector<Record> records;  // data section records
    std::vector<std::string> entries;  // plain list entries (hasDataSection == false)
};

DataFile parse(std::string_view text, std::string name);
std::expected<DataFile, std::string> load(const std::filesystem::path& path);

// Key comparison used everywhere: case-insensitive, runs of spaces/tabs collapse.
bool keysEqual(std::string_view a, std::string_view b);
// Canonical form under that comparison (lowercase, single spaces, trimmed), for hash maps.
std::string normalizeKey(std::string_view key);

// The files are Latin-1; the rest of the program speaks UTF-8.
std::string latin1ToUtf8(std::string_view text);

} // namespace opense4::datafile
