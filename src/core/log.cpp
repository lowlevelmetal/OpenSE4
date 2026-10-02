#include "core/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <mutex>

namespace opense4::log {

namespace {
std::atomic<Level> gMinLevel{Level::Info};
std::mutex gMutex;
std::ofstream gFile;

const char* levelTag(Level level) {
    switch (level) {
        case Level::Debug: return "debug";
        case Level::Info: return "info ";
        case Level::Warn: return "warn ";
        case Level::Error: return "error";
    }
    return "?";
}
} // namespace

void setMinLevel(Level level) { gMinLevel = level; }

bool setFile(const std::filesystem::path& file) {
    std::lock_guard lock(gMutex);
    if (gFile.is_open()) gFile.close();
    gFile.open(file, std::ios::binary | std::ios::trunc);
    return gFile.is_open();
}

void write(Level level, std::string_view message) {
    if (level < gMinLevel.load()) return;
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    const double t = duration<double>(steady_clock::now() - start).count();
    std::lock_guard lock(gMutex);
    std::fprintf(stderr, "[%8.3f %s] %.*s\n", t, levelTag(level), static_cast<int>(message.size()), message.data());
    if (gFile.is_open()) {
        gFile << std::format("[{:8.3f} {}] {}\n", t, levelTag(level), message);
        gFile.flush();
    }
}

} // namespace opense4::log
