#include "client/classic/screens/file_list.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <format>

namespace opense4::client::classic {

namespace {

// A file time as system_clock time: what std::chrono::clock_cast gives, which
// libc++ lacks. Like clock_cast, it uses the file clock's to_sys (libstdc++,
// libc++) or else its to_utc (MSVC) and the UTC clock's to_sys.
template <class Clock, class Duration>
std::chrono::system_clock::time_point toSystemClock(std::chrono::time_point<Clock, Duration> t) {
    using Sys = std::chrono::system_clock::duration;
    if constexpr (requires { Clock::to_sys(t); }) {
        return std::chrono::time_point_cast<Sys>(Clock::to_sys(t));
    } else {
        const auto utc = Clock::to_utc(t);
        return std::chrono::time_point_cast<Sys>(decltype(utc)::clock::to_sys(utc));
    }
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upperAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::vector<FileEntry> listFiles(const std::filesystem::path& dir, std::string_view extension) {
    std::vector<FileEntry> out;
    std::error_code ec;
    const std::string want = lowerAscii(std::string(extension));
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) || lowerAscii(entry.path().extension().string()) != want) continue;
        out.push_back({entry.path(), entry.path().stem().string(), entry.last_write_time(ec)});
    }
    // Newest first; files of the same moment by name (a total order: the
    // directory's own order differs between platforms).
    std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
        return a.modified != b.modified ? a.modified > b.modified : a.path.filename() < b.path.filename();
    });
    return out;
}

void FileSortHistory::click(FileSortKey k) {
    keys.insert(keys.begin(), k);
    if (keys.size() > 5) keys.resize(5);
}

void sortFiles(std::vector<FileEntry>& files, const FileSortHistory& history) {
    std::stable_sort(files.begin(), files.end(), [&](const FileEntry& a, const FileEntry& b) {
        for (const FileSortKey k : history.keys) {
            if (k == FileSortKey::Name) {
                const std::string x = upperAscii(a.name), y = upperAscii(b.name);
                if (x != y) return x < y;
            } else if (a.modified != b.modified) {
                return a.modified > b.modified;
            }
        }
        return a.path.filename() < b.path.filename();
    });
}

std::string trimmedSaveName(std::string_view name) {
    size_t a = 0, b = name.size();
    while (a < b && name[a] == ' ') ++a;
    while (b > a && name[b - 1] == ' ') --b;
    return std::string(name.substr(a, b - a));
}

bool validSaveName(std::string_view typed) {
    const std::string name = trimmedSaveName(typed);
    if (name.empty() || name.back() == '.') return false;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7e || std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) return false;
    }
    // Windows keeps these for devices, with any extension.
    const std::string stem = upperAscii(name.substr(0, name.find('.')));
    static constexpr std::string_view kDevices[] = {"CON", "PRN", "AUX", "NUL"};
    for (const std::string_view d : kDevices)
        if (trimmedSaveName(stem) == d) return false;
    if (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9') return false;
    return true;
}

std::string fileDate(std::filesystem::file_time_type t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(toSystemClock(t));
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    const int hour = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
    return std::format("{}/{}/{} {}:{:02}:{:02} {}", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900, hour, tm.tm_min, tm.tm_sec,
                       tm.tm_hour < 12 ? "AM" : "PM");
}

} // namespace opense4::client::classic
