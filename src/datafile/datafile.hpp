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

// Records and fields that a mod's data patch wrote say so in `origin`, for
// diagnostics ("mod example.carriers, data/carriers.toml:12"); it is empty for
// what the file itself holds (docs/sdk/packages-and-data.md).
struct Field {
    std::string key;    // trimmed
    std::string value;  // trimmed, may be empty
    int line = 0;       // 1-based line number in the file
    std::string raw;    // the value as written, untrimmed (without the line end)
    std::string origin; // the patch that wrote it; empty: the file
};

struct Record {
    std::vector<Field> fields;
    int line = 0;  // line of the first field
    std::string origin;  // the patch that added the record; empty: the file

    // First field whose key matches (ASCII case-insensitive, whitespace-normalized).
    const Field* find(std::string_view key) const;
};

struct DataFile {
    std::string name;             // file name for diagnostics, e.g. "Components.txt"
    std::string origin;           // a mod's file that replaced the install's ("mod x: data/Components.txt"); empty: the install's
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
// Back to Latin-1 for writing a data file; characters beyond it become '?'.
std::string utf8ToLatin1(std::string_view text);

// The key with every run of digits replaced by '#', in normalizeKey form:
// "Ability 3 Val 1" -> "ability # val #". Numbered keys of one list share it.
std::string keyPattern(std::string_view key);

// The file in the data format again (Latin-1, CRLF line ends, as the classic
// files are): a short header, then *BEGIN*, the records and *END*; a plain
// list file is one entry per line.
std::string write(const DataFile& file, std::string_view header = {});

} // namespace opense4::datafile
