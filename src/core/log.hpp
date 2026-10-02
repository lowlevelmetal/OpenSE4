#pragma once

#include <filesystem>
#include <format>
#include <string_view>

namespace opense4::log {

enum class Level { Debug, Info, Warn, Error };

void setMinLevel(Level level);
void write(Level level, std::string_view message);
// Also writes every message to `file` (replaced; LF line endings on every
// platform), for players whose program has no console, as on Windows.
// False when it cannot be opened.
bool setFile(const std::filesystem::path& file);

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
