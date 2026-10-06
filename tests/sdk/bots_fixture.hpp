#pragma once

// Helpers for the external bots' tests (tests/sdk/test_sdk_bots.cpp,
// test_sdk_arena.cpp): a bot that speaks the connection's JSON by hand
// (docs/sdk/ai-protocol.md §10), CPython when it is installed, and programs
// run to their end.

#include "temp_dir.hpp"

#include "net/socket.hpp"
#include "script/json.hpp"
#include "sdk/process.hpp"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace opense4::sdktest {

inline std::filesystem::path sourceRoot() { return std::filesystem::path(OPENSE4_DOCS_DIR).parent_path(); }
inline std::filesystem::path pythonDir() { return sourceRoot() / "python"; }
inline std::filesystem::path fixtureAiDir() { return std::filesystem::path(OPENSE4_FIXTURE_DIR) / "mods" / "ai-fixture" / "ai"; }

inline std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Ran {
    int code = -1;
    std::string out;
};

// Runs a program to its end (at most `timeout`), its output and errors in one text.
inline Ran runProgram(const std::vector<std::string>& args, const std::vector<std::pair<std::string, std::string>>& env = {},
                      std::chrono::seconds timeout = std::chrono::seconds(300)) {
    static test::TempDir outputs("sdk_bots_runs");
    static int n = 0;
    sdk::ProcessOptions po;
    po.args = args;
    po.environment = env;
    po.output = outputs.path() / std::format("run{}.txt", ++n);
    auto p = sdk::Process::start(po);
    if (!p) return {-1, p.error()};
    const auto code = p->wait(timeout);
    if (!code) {
        p->kill();
        return {-2, slurp(po.output) + "\n(timed out)"};
    }
    return {*code, slurp(po.output)};
}

// CPython 3.10 or newer, when installed (python3, else python).
inline std::optional<std::string> cpythonExe() {
    static const std::optional<std::string> found = [] () -> std::optional<std::string> {
        for (const char* exe : {"python3", "python"}) {
            const Ran r = runProgram({exe, "-c", "import sys; print('ok' if sys.version_info >= (3, 10) else 'old')"}, {}, std::chrono::seconds(30));
            if (r.code == 0 && r.out.starts_with("ok")) return std::string(exe);
        }
        return std::nullopt;
    }();
    return found;
}

// Cross-compiled tests run under an emulator (OPENSE4_TEST_RUNNER): the
// tests that start several programs of ours talking to each other skip there.
inline bool underEmulator() {
    const char* runner = std::getenv("OPENSE4_TEST_RUNNER");
    return runner && *runner;
}

// A bot that speaks the connection by hand.
struct RawBot {
    net::Socket socket;
    std::string buffer;

    explicit RawBot(uint16_t port) {
        auto c = net::connectTcp("127.0.0.1", port);
        REQUIRE_MESSAGE(c.has_value(), (c ? std::string{} : c.error()));
        socket = std::move(*c);
        net::PollItem item;
        item.socket = socket.native();
        item.wantWrite = true;
        net::pollSockets(std::span<net::PollItem>(&item, 1), 5000);
        REQUIRE(socket.connectError().empty());
    }

    void sendText(std::string_view text) {
        size_t sent = 0;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (sent < text.size() && std::chrono::steady_clock::now() < until) {
            const auto r = socket.send(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()) + sent, text.size() - sent));
            if (r.status == net::IoStatus::Ok) {
                sent += r.bytes;
                continue;
            }
            if (r.status != net::IoStatus::WouldBlock) return;
            net::PollItem item;
            item.socket = socket.native();
            item.wantWrite = true;
            net::pollSockets(std::span<net::PollItem>(&item, 1), 100);
        }
    }

    void send(const script::Value& v) { sendText(*script::toJson(v) + "\n"); }

    // The next message, or nothing when the connection closed or the time ran out.
    std::optional<script::Value> receive(int ms = 10000) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            if (const size_t nl = buffer.find('\n'); nl != std::string::npos) {
                const std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                auto v = script::parseJson(line);
                REQUIRE_MESSAGE(v.has_value(), line);
                return *v;
            }
            std::array<uint8_t, 65536> chunk{};
            const auto r = socket.receive(chunk);
            if (r.status == net::IoStatus::Ok) {
                buffer.append(reinterpret_cast<const char*>(chunk.data()), r.bytes);
                continue;
            }
            if (r.status != net::IoStatus::WouldBlock) return std::nullopt;
            if (std::chrono::steady_clock::now() >= until) return std::nullopt;
            net::PollItem item;
            item.socket = socket.native();
            item.wantRead = true;
            net::pollSockets(std::span<net::PollItem>(&item, 1), 50);
        }
    }

    // Whether the host closed the connection (within `ms`).
    bool closed(int ms = 5000) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            std::array<uint8_t, 4096> chunk{};
            const auto r = socket.receive(chunk);
            if (r.status == net::IoStatus::Closed || r.status == net::IoStatus::Error) return true;
            if (r.status == net::IoStatus::Ok) {
                buffer.append(reinterpret_cast<const char*>(chunk.data()), r.bytes);
                continue;
            }
            if (std::chrono::steady_clock::now() >= until) return false;
            net::PollItem item;
            item.socket = socket.native();
            item.wantRead = true;
            net::pollSockets(std::span<net::PollItem>(&item, 1), 50);
        }
    }

    // Says hello; the host's answer.
    script::Value hello(const std::string& token, std::optional<int64_t> slot, int api = 1) {
        script::ValueMap h;
        h.emplace_back("api", script::Value(api));
        h.emplace_back("token", script::Value(token));
        h.emplace_back("slot", slot ? script::Value(*slot) : script::Value());
        h.emplace_back("name", script::Value("raw"));
        send(script::Value(script::ValueMap{{"hello", script::Value(std::move(h))}}));
        auto answer = receive();
        REQUIRE(answer.has_value());
        return *answer;
    }
};

} // namespace opense4::sdktest
