#pragma once

// Helpers for the SDK's tests (tests/sdk): a game folder put together from
// our own fixtures in a scratch directory, small mods written on the spot,
// and the fixture mods of tests/fixtures/mods. Nothing here reads the
// player's installed game.

#include "mods/data_set.hpp"
#include "mods/mod_set.hpp"
#include "temp_dir.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::test {

inline std::filesystem::path fixtureDir() { return std::filesystem::path(OPENSE4_FIXTURE_DIR); }
inline std::filesystem::path fixtureMod(std::string_view name) { return fixtureDir() / "mods" / name; }

inline void writeText(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

inline std::string readText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A data file of the format, with our own header line.
inline std::string dataText(std::string_view body) { return std::format("Written by opense4 tests.\n*BEGIN*\n{}\n*END*\n", body); }

// A game folder: the fixture data set as its Data folder, and whatever AI
// tables and race files a test writes.
struct GameFolder {
    TempDir dir;
    std::filesystem::path root;

    explicit GameFolder(std::string_view tag) : dir(std::format("sdk_{}", tag)), root(dir.path()) {
        std::filesystem::create_directories(root / "Data");
        for (const auto& e : std::filesystem::directory_iterator(fixtureDir() / "minimal_dataset"))
            std::filesystem::copy_file(e.path(), root / "Data" / e.path().filename());
    }
    std::filesystem::path data() const { return root / "Data"; }
    void write(const std::filesystem::path& rel, std::string_view body) const { writeText(root / rel, dataText(body)); }
    void writePlain(const std::filesystem::path& rel, std::string_view body) const { writeText(root / rel, body); }
};

// A mod written on the spot: mod.toml and any other files.
struct ModDir {
    TempDir dir;
    std::filesystem::path root;

    ModDir(std::string_view tag, std::string_view id, std::string_view version = "1.0.0", std::string_view extra = {})
        : dir(std::format("sdk_mod_{}", tag)), root(dir.path() / std::string(id)) {
        writeText(root / "mod.toml", std::format("[mod]\nid = \"{}\"\nname = \"{}\"\nversion = \"{}\"\napi = 1\n{}", id, id, version, extra));
    }
    void file(const std::filesystem::path& rel, std::string_view text) const { writeText(root / rel, text); }
    mods::Package open() const {
        auto p = mods::openPackage(root);
        REQUIRE_MESSAGE(p, (p ? std::string{} : p.error()));
        return *p;
    }
};

inline mods::Package openFixtureMod(std::string_view name) {
    auto p = mods::openPackage(fixtureMod(name));
    REQUIRE_MESSAGE(p, (p ? std::string{} : p.error()));
    return *p;
}

inline mods::ModSet modSet(std::vector<mods::Package> packages) {
    auto set = mods::resolveModSet(std::move(packages));
    REQUIRE_MESSAGE(set, (set ? std::string{} : set.error().front()));
    return *set;
}

// Every error of a load as one text, for CHECK_MESSAGE and searching.
inline std::string allErrors(const datafile::Diagnostics& d) {
    std::string out;
    for (const std::string& e : d.errors) out += e + "\n";
    return out;
}

inline bool hasError(const datafile::Diagnostics& d, std::string_view part) {
    return std::any_of(d.errors.begin(), d.errors.end(), [&](const std::string& e) { return e.find(part) != std::string::npos; });
}

} // namespace opense4::test
