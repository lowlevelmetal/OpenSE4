#pragma once

// A thread of its own for the script players of one engine call
// (docs/sdk/ai-protocol.md §2): every call into their interpreter runs on it,
// so the C stack the runtime needs (script::Limits::cStackBytes, more under
// the sanitizers) is there whichever thread plays the turn: the client's,
// the server's, a test's. The caller waits while it works.

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>

namespace opense4::sdk {

// The stack the players' thread gets: the interpreter's C stack limit and
// the engine's own frames (the services run the planners on it) with room
// to spare.
inline constexpr size_t kPlayerStackBytes = size_t{8} << 20;

// The script runtime holds one interpreter per process (docs/sdk/runtime.md):
// the sessions and the data generators take turns with it through this.
std::timed_mutex& interpreterSlot();

class Worker {
public:
    // Starts the thread; throws std::runtime_error when the system refuses it.
    explicit Worker(size_t stackBytes = kPlayerStackBytes);
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    // Runs `task` on the thread and waits for it to finish; an exception it
    // throws is thrown again here. One task at a time.
    void run(const std::function<void()>& task);

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace opense4::sdk
