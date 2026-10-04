#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::log {

enum class Level { Debug, Info, Warn, Error };

void setMinLevel(Level level);
void write(Level level, std::string_view message);
// Also writes every message to `file` (replaced; LF line endings on every
// platform), for players whose program has no console, as on Windows.
// False when it cannot be opened.
bool setFile(const std::filesystem::path& file);

// The last lines written (those the level lets through: at most
// kRecentLines, oldest first, each cut to kRecentLineBytes).
inline constexpr size_t kRecentLines = 40;
inline constexpr size_t kRecentLineBytes = 300;
std::vector<std::string> recentLines();

// For a crash report (client/crash_report.hpp), from a signal handler or an
// exception filter: raw bytes to the log file and to stderr, and the recent
// lines, without locking or allocating (async-signal-safe). Best effort: a
// line being written at the moment of the crash may come out cut.
void crashWrite(const char* text, size_t size);
void crashWrite(std::string_view text);
void crashWriteRecentLines(std::string_view indent);
// POSIX: the log file's descriptor (for backtrace_symbols_fd); -1 without
// one, and always on Windows.
int crashFileDescriptor();

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace opense4::log
