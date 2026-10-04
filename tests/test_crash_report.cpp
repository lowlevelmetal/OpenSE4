// Crash reports (client/crash_report.hpp) and the log's recent lines
// (core/log.hpp). On Linux and macOS a child process installs the handler
// and dies, of a segmentation fault or of an exception nothing caught; the
// report it leaves in its log is checked.

#include "client/crash_report.hpp"
#include "core/log.hpp"
#include "temp_dir.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace opense4;

namespace {

std::string readAll(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("crash report: the message names the log file and where the next start keeps it") {
    const std::filesystem::path log = std::filesystem::path("somewhere") / "opense4.log";
    const std::string text = client::crashMessage(log);
    CHECK(text.find(log.string()) != std::string::npos);
    CHECK(text.find("opense4.previous.log") != std::string::npos);
}

TEST_CASE("crash report: the last run's log is kept beside the new one") {
    const test::TempDir dir("crashlog");
    const std::filesystem::path log = dir.path() / "opense4.log";
    client::keepPreviousLog(log);   // nothing to keep
    CHECK_FALSE(std::filesystem::exists(dir.path() / "opense4.previous.log"));
    std::ofstream(log) << "the crashed run\n";
    std::ofstream(dir.path() / "opense4.previous.log") << "an older run\n";
    client::keepPreviousLog(log);
    CHECK_FALSE(std::filesystem::exists(log));
    CHECK(readAll(dir.path() / "opense4.previous.log") == "the crashed run\n");
}

TEST_CASE("crash report: the log keeps its last lines, oldest first") {
    for (size_t i = 0; i < log::kRecentLines + 5; ++i) log::info("recent line {}", i);
    const std::vector<std::string> lines = log::recentLines();
    REQUIRE(lines.size() == log::kRecentLines);
    CHECK(lines.front().ends_with("recent line 5"));
    CHECK(lines.back().ends_with(std::format("recent line {}", log::kRecentLines + 4)));
    log::info("{}", std::string(log::kRecentLineBytes * 2, 'x'));
    CHECK(log::recentLines().back().size() == log::kRecentLineBytes);
}

#ifndef _WIN32
namespace {

[[noreturn]] void breakWindow() { throw std::runtime_error("a battle window broke"); }

// Runs `crash` in a child with the handler installed and its log in `dir`;
// returns the child's wait status and the log.
std::pair<int, std::string> crashChild(const std::filesystem::path& dir, void (*crash)() noexcept) {
    const std::filesystem::path file = dir / "opense4.log";
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        log::setFile(file);
        log::info("the last thing before the crash");
        client::installCrashHandler({file, false});
        crash();
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    log::setFile({});   // the parent's own log is untouched
    return {status, readAll(file)};
}

} // namespace

TEST_CASE("crash report: an exception nothing caught") {
    const test::TempDir dir("crashexception");
    // Out of a noexcept function: the test runner around the child must not catch it.
    const auto [status, text] = crashChild(dir.path(), []() noexcept { breakWindow(); });
    CHECK(WIFSIGNALED(status));
    CHECK(text.find("==== OpenSE4 crash report ====") != std::string::npos);
    CHECK(text.find("Version: ") != std::string::npos);
    CHECK(text.find("an exception nothing caught: a battle window broke") != std::string::npos);
    CHECK(text.find("Last log lines:\n  [") != std::string::npos);
    CHECK(text.find("the last thing before the crash") != std::string::npos);
    CHECK(text.find("==== end of the crash report ====") != std::string::npos);
}

#if !defined(__SANITIZE_ADDRESS__) && !(defined(__has_feature) && __has_feature(address_sanitizer))
TEST_CASE("crash report: a segmentation fault") {
    const test::TempDir dir("crashsignal");
    const auto [status, text] = crashChild(dir.path(), []() noexcept { raise(SIGSEGV); });
    REQUIRE(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGSEGV);   // it still dies of the signal
    CHECK(text.find("What: signal 11 (SIGSEGV") != std::string::npos);
    CHECK(text.find("Stack:") != std::string::npos);
    CHECK(text.find("the last thing before the crash") != std::string::npos);
    CHECK(text.find("==== end of the crash report ====") != std::string::npos);
}
#endif
#endif
