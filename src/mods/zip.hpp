#pragma once

// Mod packages as .zip files (miniz): listing, unpacking with the checks a
// file from a stranger needs, and writing (opense4-sdk pack).

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace opense4::mods {

// What a package may hold at most, so that a hostile archive cannot fill the disk.
inline constexpr uint64_t kMaxZipBytes = uint64_t{1} << 30;          // the archive itself
inline constexpr uint64_t kMaxUnpackedBytes = uint64_t{2} << 30;     // everything in it, unpacked
inline constexpr size_t kMaxZipEntries = 100'000;

struct ZipEntry {
    std::string name;  // '/'-separated, as stored
    uint64_t size = 0;
    bool directory = false;
};

// Whether an entry's name is a safe relative path: no absolute paths, drive
// letters, backslashes, "." or ".." parts, or control characters.
bool safeEntryName(std::string_view name);

std::expected<std::vector<ZipEntry>, std::string> listZip(const std::filesystem::path& zip);
// Unpacks every entry under `dest` (created). Refuses unsafe names, too many
// entries and too much data, before writing anything.
std::expected<void, std::string> extractZip(const std::filesystem::path& zip, const std::filesystem::path& dest);

struct ZipInput {
    std::string name;                  // '/'-separated path in the archive
    std::vector<uint8_t> bytes;
};
// Writes an archive of these entries, in this order, without time stamps:
// the same files give the same bytes.
std::expected<void, std::string> writeZip(const std::filesystem::path& out, std::span<const ZipInput> entries);

} // namespace opense4::mods
