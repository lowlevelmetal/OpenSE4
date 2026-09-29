#pragma once

// Typed access to one record with precise diagnostics ("Components.txt:1234
// [Rock Colony] ..."). Tracks which fields were read, so loaders can report
// fields they don't understand (typos in mods, or data we don't support yet).

#include "datafile/datafile.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace opense4::datafile {

struct Diagnostics {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    // Field keys present in data but never read by a loader: "File.txt: Key" -> count.
    std::map<std::string, int> unreadFields;

    bool ok() const { return errors.empty(); }
};

enum class Need { Required, Optional };

class RecordReader {
public:
    RecordReader(const DataFile& file, const Record& record, Diagnostics& diag);
    ~RecordReader();  // records unread fields
    RecordReader(const RecordReader&) = delete;
    RecordReader& operator=(const RecordReader&) = delete;

    std::string context() const;  // "Components.txt:1234 [Rock Colony]"
    void error(std::string_view message) const;
    void warn(std::string_view message) const;

    bool has(std::string_view key) const;
    std::string str(std::string_view key, Need need = Need::Required);
    int64_t integer(std::string_view key, Need need = Need::Required, int64_t fallback = 0);
    int int32(std::string_view key, Need need = Need::Required, int fallback = 0);
    bool boolean(std::string_view key, Need need = Need::Required, bool fallback = false);
    // "Ship\Base\Drone" -> {"Ship", "Base", "Drone"}
    std::vector<std::string> list(std::string_view key, char separator = '\\', Need need = Need::Required);
    // "10 10 0 0 ..." -> {10, 10, 0, 0, ...}
    std::vector<int> intList(std::string_view key, Need need = Need::Required);

    // Every field not read so far, in file order; marks them read.
    std::vector<std::pair<std::string, std::string>> remaining();

    // Keys numbered from 1: format("Ability {} Type", n).
    template <class... Args>
    static std::string key(std::format_string<Args...> fmt, Args&&... args) {
        return std::format(fmt, std::forward<Args>(args)...);
    }

private:
    const Field* take(std::string_view key, Need need);

    const DataFile& file_;
    const Record& record_;
    Diagnostics& diag_;
    std::vector<bool> read_;
};

// Parse helpers shared by loaders.
std::optional<int64_t> parseInteger(std::string_view text);
std::optional<bool> parseBoolean(std::string_view text);

} // namespace opense4::datafile
