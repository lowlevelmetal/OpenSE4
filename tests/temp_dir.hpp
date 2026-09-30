#pragma once

// Scratch directories for tests. Each one gets a name no other TempDir uses,
// in this process or in another test binary running at the same time
// (a random per-process part plus a counter, and the directory must not exist
// yet). It is removed with everything in it when the object goes away.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <random>
#include <string_view>
#include <system_error>

namespace opense4::test {

class TempDir {
public:
    explicit TempDir(std::string_view tag) {
        namespace fs = std::filesystem;
        static std::atomic<uint64_t> counter{0};
        static const uint64_t salt = [] {
            std::random_device rd;
            const auto now = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            return ((static_cast<uint64_t>(rd()) << 32) ^ rd()) ^ now;
        }();
        std::error_code ec;
        const fs::path base = fs::temp_directory_path(ec);
        for (int tries = 0; tries < 1000; ++tries) {
            path_ = base / std::format("opense4_{}_{:016x}_{}", tag, salt, counter.fetch_add(1));
            if (fs::create_directory(path_, ec)) break;  // new, so ours alone
            if (ec) break;                               // cannot create: the test will report it
        }
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(const std::filesystem::path& rel) const { return path_ / rel; }

private:
    std::filesystem::path path_;
};

} // namespace opense4::test
