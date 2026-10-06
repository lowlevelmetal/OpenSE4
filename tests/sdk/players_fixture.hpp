#pragma once

// Helpers for the computer players' tests (tests/sdk/test_sdk_players*.cpp):
// the stand-in `opense4` package (tests/fixtures/sdk/package), the fixture
// mod with players written straight to the protocol
// (tests/fixtures/mods/ai-fixture), games on the engine's test rules with
// some empires played by them, and their memory.

#include "engine_fixture.hpp"
#include "mods/package.hpp"
#include "script/json.hpp"
#include "sdk/players.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace opense4::sdktest {

inline std::filesystem::path fixturePath() { return std::filesystem::path(OPENSE4_FIXTURE_DIR); }

inline std::string fileText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The stand-in package's files, as the session adds them.
inline std::vector<std::pair<std::string, std::string>> standInPackage() {
    const std::filesystem::path root = fixturePath() / "sdk" / "package";
    std::vector<std::pair<std::string, std::string>> out;
    for (const char* f : {"opense4/__init__.py", "opense4/_engine.py"}) out.emplace_back(f, fileText(root / f));
    return out;
}

inline mods::Package aiFixtureMod() {
    auto p = mods::openPackage(fixturePath() / "mods" / "ai-fixture");
    REQUIRE_MESSAGE(p.has_value(), (p ? std::string{} : p.error()));
    return std::move(*p);
}

inline constexpr const char* kFixtureMod = "test.ai-fixture";

inline game::Controller scriptPlayer(const char* name) {
    game::Controller c;
    c.kind = game::Controller::Kind::Script;
    c.mod = kFixtureMod;
    c.player = name;
    return c;
}

inline game::Controller externalPlayer(uint32_t slot) {
    game::Controller c;
    c.kind = game::Controller::Kind::External;
    c.slot = slot;
    return c;
}

// The SDK's sessions with the fixture mod, for one test: with the stand-in
// package unless the setup names another, or with OpenSE4's own package
// (`realPackage`).
struct InstalledPlayers {
    explicit InstalledPlayers(sdk::PlayerSetup setup = {}, bool realPackage = false) {
        if (setup.package.empty() && !realPackage) setup.package = standInPackage();
        setup.mods.push_back(aiFixtureMod());
        sdk::installPlayers(std::move(setup));
    }
    ~InstalledPlayers() { sdk::uninstallPlayers(); }
    InstalledPlayers(const InstalledPlayers&) = delete;
    InstalledPlayers& operator=(const InstalledPlayers&) = delete;
};

// Computer empires in a small quadrant, as the determinism tests play them,
// each with the given controller (builtin where none is given).
inline game::GameSetup playersSetup(uint64_t seed, bool simultaneous, std::vector<game::Controller> controllers, int systems = 14) {
    game::GameSetup setup;
    setup.seed = seed;
    setup.options.systemCount = systems;
    setup.options.simultaneous = simultaneous;
    setup.options.eventFrequency = 3;
    setup.options.maxEventSeverity = 3;
    setup.options.finiteResources = true;
    for (size_t i = 0; i < controllers.size(); ++i) {
        game::EmpireSetup e;
        e.name = std::format("Empire {}", i + 1);
        e.kind = game::PlayerKind::Computer;
        e.controller = controllers[i];
        setup.empires.push_back(std::move(e));
    }
    return setup;
}

inline game::GameState playersGame(uint64_t seed, bool simultaneous, std::vector<game::Controller> controllers, int systems = 14) {
    auto created = game::createGame(test::engineRules(), playersSetup(seed, simultaneous, std::move(controllers), systems));
    REQUIRE_MESSAGE(created.has_value(), (created ? std::string{} : created.error()));
    return std::move(*created);
}

// An empire's memory as a value (null when it has none).
inline script::Value memoryOf(const game::GameState& s, game::EmpireId e) {
    const std::string& text = s.empire(e).script.memory;
    if (text.empty()) return {};
    auto v = script::parseJson(text);
    REQUIRE_MESSAGE(v.has_value(), text);
    return *v;
}

inline void setMemory(game::GameState& s, game::EmpireId e, std::string_view json) {
    REQUIRE(script::parseJson(json).has_value());
    s.empire(e).script.memory = std::string(json);
}

inline int64_t intIn(const script::Value& v, std::string_view key) {
    const script::Value* x = v.find(key);
    REQUIRE_MESSAGE((x && x->isInt()), key);
    return x->asInt();
}

// The journal entries of one empire (and one call, when given).
inline std::vector<game::JournalEntry> journalOf(const game::GameState& s, game::EmpireId e, std::string_view call = {}) {
    std::vector<game::JournalEntry> out;
    for (const game::JournalEntry& j : s.journal.entries)
        if (j.empire == e && (call.empty() || j.call == call)) out.push_back(j);
    return out;
}

inline script::Value responseOf(const game::JournalEntry& j) {
    auto v = script::parseJson(j.response);
    REQUIRE_MESSAGE(v.has_value(), j.response);
    return *v;
}

} // namespace opense4::sdktest
