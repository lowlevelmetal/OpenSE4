#pragma once

// The files the Load Game dialog lists and how it sorts them, and the names
// Save Game takes (docs/spec/06 §1.1, §1.2). Headless; the dialog is
// file_dialog.hpp.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

struct FileEntry {
    std::filesystem::path path;
    std::string name;   // the file name without its extension
    std::filesystem::file_time_type modified;
};

// The files of a folder with this extension (any case), newest first, then by name.
std::vector<FileEntry> listFiles(const std::filesystem::path& dir, std::string_view extension);

// The dialog's columns as sort keys: the name (A to Z, as shown, in
// capitals) and the date (newest first).
enum class FileSortKey : uint8_t { Name, Date };
// The usual five-key sort history (spec 06 §7 Q24): the last header clicked
// first, the ones clicked before it breaking ties; it starts with the name
// alone (spec 06 §1.1, confirmed: binary).
struct FileSortHistory {
    std::vector<FileSortKey> keys{FileSortKey::Name};
    // A click on a header: every slot moves down one, the fifth drops off.
    void click(FileSortKey k);
};
// Sorts `files` by the history's keys, then by file name (a total order).
void sortFiles(std::vector<FileEntry>& files, const FileSortHistory& history);

// Whether a typed save name can be a file name everywhere: after the spaces
// around it are dropped it is not empty, holds only printable ASCII without
// \ / : * ? " < > |, does not end with a dot and is not a name Windows keeps
// for a device (CON, NUL, COM1 ...). Save Game refuses another with "Invalid
// File Name." (spec 06 §1.2, confirmed: binary for the refusal; which names
// are ours).
bool validSaveName(std::string_view name);
// The name as saved: without the spaces around it.
std::string trimmedSaveName(std::string_view name);
// A file's date and time as the dialog shows it: "9/30/2026 1:38:30 PM" (local time).
std::string fileDate(std::filesystem::file_time_type t);

} // namespace opense4::client::classic
