#include "core/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace opense4::log {

namespace {
std::atomic<Level> gMinLevel{Level::Info};
std::mutex gMutex;

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

void write(Level level, std::string_view message) {
    if (level < gMinLevel.load()) return;
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    const double t = duration<double>(steady_clock::now() - start).count();
    std::lock_guard lock(gMutex);
    std::fprintf(stderr, "[%8.3f %s] %.*s\n", t, levelTag(level), static_cast<int>(message.size()), message.data());
}

} // namespace opense4::log
