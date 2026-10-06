#pragma once

// Child processes for the SDK's tools (docs/sdk/bots-and-arena.md): the
// arena's games and the external bots it starts, the training environment's
// engine, the client `opense4-sdk run` starts. POSIX (fork and exec, each
// child in a process group of its own) and Windows (CreateProcessW, each
// child in a job object), so that ending a child ends what it started too.
//
// Nothing here is used while a turn resolves: the engine stays free of
// processes and clocks.

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opense4::sdk {

struct ProcessOptions {
    // The program and its arguments (UTF-8). A program named without a
    // folder is looked for on PATH.
    std::vector<std::string> args;
    // Or a command line for the system's shell (sh -c on POSIX, cmd /c on
    // Windows), as a user writes it; `args` is then not used.
    std::string shellCommand;
    // Variables set in the child, over the parent's environment.
    std::vector<std::pair<std::string, std::string>> environment;
    // The child's working folder (empty: the parent's).
    std::filesystem::path workingDir;
    // Where its standard output and errors go, both to one file, which is
    // created or emptied (empty: the parent's own).
    std::filesystem::path output;
};

class Process {
public:
    Process();
    ~Process();   // ends the child if it still runs, and waits for it
    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    static std::expected<Process, std::string> start(const ProcessOptions& options);

    bool valid() const;
    // The exit code once the child has ended (on POSIX 128 + the signal for
    // one a signal ended); nothing while it runs.
    std::optional<int> poll();
    // Waits until the child ends, at most `timeout` (none: as long as it
    // takes). The exit code, or nothing when the time ran out.
    std::optional<int> wait(std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    // Ends the child and every process it started, at once.
    void kill();
    int64_t id() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// The folder of the running program, where OpenSE4's other programs are.
std::filesystem::path executableDir();

// "opense4-sdk" on POSIX, "opense4-sdk.exe" on Windows.
std::string executableName(std::string_view base);

} // namespace opense4::sdk
