#include "core/log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace opense4::log {

namespace {
std::atomic<Level> gMinLevel{Level::Info};
std::mutex gMutex;

// The log file as a plain operating-system handle, so that a crash report
// can write to it from a signal handler or an exception filter (crashWrite).
#ifdef _WIN32
HANDLE gFile = INVALID_HANDLE_VALUE;
#else
int gFile = -1;
#endif

// The recent lines, in fixed storage that a crash report reads without allocating.
char gRecent[kRecentLines][kRecentLineBytes];
size_t gRecentSize[kRecentLines];
std::atomic<size_t> gRecentNext{0};

bool fileOpen() {
#ifdef _WIN32
    return gFile != INVALID_HANDLE_VALUE;
#else
    return gFile >= 0;
#endif
}

void closeFile() {
#ifdef _WIN32
    if (gFile != INVALID_HANDLE_VALUE) CloseHandle(gFile);
    gFile = INVALID_HANDLE_VALUE;
#else
    if (gFile >= 0) ::close(gFile);
    gFile = -1;
#endif
}

void writeFile(const char* text, size_t size) {
#ifdef _WIN32
    while (size > 0 && gFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 1u << 30));
        if (!WriteFile(gFile, text, chunk, &written, nullptr) || written == 0) return;
        text += written;
        size -= written;
    }
#else
    while (size > 0 && gFile >= 0) {
        const ssize_t n = ::write(gFile, text, size);
        if (n <= 0) return;
        text += n;
        size -= static_cast<size_t>(n);
    }
#endif
}

void writeStderr(const char* text, size_t size) {
#ifdef _WIN32
    std::fwrite(text, 1, size, stderr);   // the windowed build's stderr is the parent console, if any
    std::fflush(stderr);
#else
    while (size > 0) {
        const ssize_t n = ::write(2, text, size);
        if (n <= 0) return;
        text += n;
        size -= static_cast<size_t>(n);
    }
#endif
}

const char* levelTag(Level level) {
    switch (level) {
        case Level::Debug: return "debug";
        case Level::Info: return "info ";
        case Level::Warn: return "warn ";
        case Level::Error: return "error";
    }
    return "?";
}

void remember(std::string_view line) {
    const size_t slot = gRecentNext.load() % kRecentLines;
    const size_t n = std::min(line.size(), kRecentLineBytes);
    std::memcpy(gRecent[slot], line.data(), n);
    gRecentSize[slot] = n;
    gRecentNext.store(gRecentNext.load() + 1);
}
} // namespace

void setMinLevel(Level level) { gMinLevel = level; }

bool setFile(const std::filesystem::path& file) {
    std::lock_guard lock(gMutex);
    closeFile();
#ifdef _WIN32
    // Others may read it (and a crash report append to it) while it is open.
    gFile = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    gFile = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
#endif
    return fileOpen();
}

void write(Level level, std::string_view message) {
    if (level < gMinLevel.load()) return;
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    const double t = duration<double>(steady_clock::now() - start).count();
    const std::string line = std::format("[{:8.3f} {}] {}\n", t, levelTag(level), message);
    std::lock_guard lock(gMutex);
    writeStderr(line.data(), line.size());
    writeFile(line.data(), line.size());
    remember(std::string_view(line).substr(0, line.size() - 1));
}

std::vector<std::string> recentLines() {
    std::lock_guard lock(gMutex);
    std::vector<std::string> out;
    const size_t next = gRecentNext.load();
    for (size_t i = next > kRecentLines ? next - kRecentLines : 0; i < next; ++i)
        out.emplace_back(gRecent[i % kRecentLines], gRecentSize[i % kRecentLines]);
    return out;
}

void crashWrite(const char* text, size_t size) {
    writeStderr(text, size);
    writeFile(text, size);
}

void crashWrite(std::string_view text) { crashWrite(text.data(), text.size()); }

int crashFileDescriptor() {
#ifdef _WIN32
    return -1;
#else
    return gFile;
#endif
}

void crashWriteRecentLines(std::string_view indent) {
    const size_t next = gRecentNext.load();
    for (size_t i = next > kRecentLines ? next - kRecentLines : 0; i < next; ++i) {
        crashWrite(indent);
        crashWrite(gRecent[i % kRecentLines], std::min(gRecentSize[i % kRecentLines], kRecentLineBytes));
        crashWrite("\n", 1);
    }
}

} // namespace opense4::log
