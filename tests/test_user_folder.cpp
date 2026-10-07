// Where OpenSE4 keeps a player's own files (core/user_folder.hpp): the order
// of the rules (OPENSE4_USER_DIR, a portable copy's marker, the system's
// folder), each platform's folder, the Files page's switch (the marker made
// and removed), copying a player's files without deleting or replacing any,
// the refusal of a folder that cannot be written, and a program copied into a
// scratch folder finding the folder beside it.

#include "core/user_folder.hpp"
#include "sdk/process.hpp"
#include "temp_dir.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace opense4;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// A lookup over a fixed set of variables.
core::VariableLookup variables(std::map<std::string, std::string> values) {
    return [values = std::move(values)](std::string_view name) -> std::optional<std::string> {
        const auto it = values.find(std::string(name));
        if (it == values.end()) return std::nullopt;
        return it->second;
    };
}

bool samePath(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::weakly_canonical(a, ec) == fs::weakly_canonical(b, ec);
}

} // namespace

TEST_CASE("user folder: OPENSE4_USER_DIR first, then a portable copy's userdata, then the system's folder") {
    const test::TempDir dir("user_folder_order");
    const fs::path program = dir.path() / "OpenSE4";
    fs::create_directories(program);
    int asked = 0;
    const auto system = [&] {
        ++asked;
        return dir.path() / "system" / "OpenSE4";
    };

    // No marker: the system's folder.
    core::UserFolder f = core::resolveUserFolder(std::nullopt, program, system);
    CHECK(f.source == core::UserFolderSource::System);
    CHECK(f.path == dir.path() / "system" / "OpenSE4");
    CHECK(asked == 1);
    // A set but empty variable counts as not set.
    f = core::resolveUserFolder(std::string(), program, system);
    CHECK(f.source == core::UserFolderSource::System);

    // portable.txt beside the program: userdata beside it, and the system is not asked
    // (SDL's preference folder would be made by asking).
    write(program / "portable.txt", "");
    CHECK(core::portableMarkerPresent(program));
    asked = 0;
    f = core::resolveUserFolder(std::nullopt, program, system);
    CHECK(f.source == core::UserFolderSource::Portable);
    CHECK(f.path == program / "userdata");
    CHECK(asked == 0);

    // OPENSE4_USER_DIR wins over the marker (the tests rely on it).
    f = core::resolveUserFolder(std::string((dir.path() / "scratch").string()), program, system);
    CHECK(f.source == core::UserFolderSource::Environment);
    CHECK(f.path == dir.path() / "scratch");
    CHECK(asked == 0);

    // A folder named portable.txt is no marker; nor is a marker elsewhere.
    fs::remove(program / "portable.txt");
    fs::create_directories(program / "portable.txt");
    CHECK_FALSE(core::portableMarkerPresent(program));
    CHECK(core::resolveUserFolder(std::nullopt, program, system).source == core::UserFolderSource::System);
    CHECK(core::resolveUserFolder(std::nullopt, fs::path(), system).source == core::UserFolderSource::System);

    // A system that names no folder: userdata in the working folder.
    f = core::resolveUserFolder(std::nullopt, fs::path(), [] { return fs::path(); });
    CHECK(f.source == core::UserFolderSource::System);
    CHECK(f.path == fs::current_path() / "userdata");
    CHECK(core::userFolderSourceName(core::UserFolderSource::Environment) == "OPENSE4_USER_DIR");
}

TEST_CASE("user folder: each platform's own folder for a user's files") {
    using core::OsFamily;
    CHECK(core::systemUserFolderFor(OsFamily::Windows, variables({{"APPDATA", "C:/Users/Ann/AppData/Roaming"}, {"HOME", "/home/ann"}})) ==
          fs::path("C:/Users/Ann/AppData/Roaming") / "OpenSE4");
    CHECK(core::systemUserFolderFor(OsFamily::Windows, variables({{"HOME", "/home/ann"}})).empty());
    CHECK(core::systemUserFolderFor(OsFamily::MacOS, variables({{"HOME", "/Users/ann"}, {"XDG_DATA_HOME", "/x"}})) ==
          fs::path("/Users/ann") / "Library" / "Application Support" / "OpenSE4");
    CHECK(core::systemUserFolderFor(OsFamily::Unix, variables({{"HOME", "/home/ann"}, {"XDG_DATA_HOME", "/data/ann"}})) ==
          fs::path("/data/ann") / "OpenSE4");
    CHECK(core::systemUserFolderFor(OsFamily::Unix, variables({{"HOME", "/home/ann"}, {"XDG_DATA_HOME", ""}})) ==
          fs::path("/home/ann") / ".local" / "share" / "OpenSE4");
    CHECK(core::systemUserFolderFor(OsFamily::Unix, variables({})).empty());
    CHECK(core::systemUserFolderFor(OsFamily::Unix, nullptr).empty());
}

TEST_CASE("user folder: beside the program, or beside a macOS application bundle") {
    using core::OsFamily;
    CHECK(core::programFolderFor(OsFamily::Unix, "/opt/games/OpenSE4/opense4") == fs::path("/opt/games/OpenSE4"));
    CHECK(core::programFolderFor(OsFamily::Windows, fs::path("D:/Games/OpenSE4/opense4.exe")) == fs::path("D:/Games/OpenSE4"));
    CHECK(core::programFolderFor(OsFamily::MacOS, "/Applications/Games/OpenSE4.app/Contents/MacOS/opense4") ==
          fs::path("/Applications/Games"));
    // A plain folder on macOS (a build folder), or a bundle's look-alike elsewhere.
    CHECK(core::programFolderFor(OsFamily::MacOS, "/Users/ann/OpenSE4/opense4") == fs::path("/Users/ann/OpenSE4"));
    CHECK(core::programFolderFor(OsFamily::Unix, "/x/OpenSE4.app/Contents/MacOS/opense4") == fs::path("/x/OpenSE4.app/Contents/MacOS"));
    CHECK(core::programFolderFor(OsFamily::Unix, fs::path()).empty());
    // This process knows where it runs from.
    CHECK_FALSE(core::executablePath().empty());
    CHECK(core::programFolder() == core::programFolderFor(core::currentOs(), core::executablePath()));
}

TEST_CASE("user folder: the setting makes and removes the marker, and the folder follows; the files stay") {
    const test::TempDir dir("user_folder_switch");
    const fs::path program = dir.path() / "OpenSE4";
    fs::create_directories(program);
    const auto system = [&] { return dir.path() / "system"; };
    CHECK_FALSE(core::portableProblem(program).has_value());

    REQUIRE(core::setPortable(program, true).has_value());
    CHECK(core::portableMarkerPresent(program));
    CHECK(fs::is_directory(program / "userdata"));
    CHECK(read(program / "portable.txt").find("portable") != std::string::npos);
    CHECK(core::resolveUserFolder(std::nullopt, program, system).path == program / "userdata");
    // Nothing left behind by the write tests.
    CHECK_FALSE(fs::exists(program / ".opense4-write-test"));
    CHECK_FALSE(fs::exists(program / "userdata" / ".opense4-write-test"));

    write(program / "userdata" / "saves" / "Mine.gam", "game");
    REQUIRE(core::setPortable(program, false).has_value());
    CHECK_FALSE(core::portableMarkerPresent(program));
    CHECK(read(program / "userdata" / "saves" / "Mine.gam") == "game");
    CHECK(core::resolveUserFolder(std::nullopt, program, system).path == dir.path() / "system");
    // Off again is no error; nor is on twice.
    CHECK(core::setPortable(program, false).has_value());
    CHECK(core::setPortable(program, true).has_value());
    CHECK(core::setPortable(program, true).has_value());
    CHECK_FALSE(core::setPortable(fs::path(), true).has_value());
}

TEST_CASE("user folder: copying a player's files keeps both folders' files and never deletes one") {
    const test::TempDir dir("user_folder_copy");
    const fs::path from = dir.path() / "system", to = dir.path() / "OpenSE4" / "userdata";
    write(from / "settings.toml", "[graphics]\n");
    write(from / "classic_settings.toml", "music = true\n");
    write(from / "saves" / "Alpha.gam", "alpha");
    write(from / "saves" / "Beta.gam", "beta (old)");
    write(from / "saves" / "Space Empires IV" / "ForSE4.gam", "classic");
    write(from / "History" / "plr_1_log.txt", "log");
    write(from / "Mods" / "my-mod" / "mod.toml", "id = \"x\"\n");
    write(from / "opense4.log", "this run's log");
    write(from / "opense4.previous.log", "the last run's log");
    write(from / "ModCache" / "abc" / "data.bin", "cache");
    write(to / "saves" / "Beta.gam", "beta (new)");

    const core::CopyReport r = core::copyUserFiles(from, to);
    CHECK(r.copied == 6);
    CHECK(r.kept == 1);
    CHECK(r.failed == 0);
    CHECK(read(to / "saves" / "Alpha.gam") == "alpha");
    CHECK(read(to / "saves" / "Beta.gam") == "beta (new)");   // the new folder's own file is kept
    CHECK(read(to / "saves" / "Space Empires IV" / "ForSE4.gam") == "classic");
    CHECK(read(to / "History" / "plr_1_log.txt") == "log");
    CHECK(read(to / "Mods" / "my-mod" / "mod.toml") == "id = \"x\"\n");
    CHECK(fs::exists(to / "settings.toml"));
    // Each folder's own logs and mod cache stay where they are.
    CHECK_FALSE(fs::exists(to / "opense4.log"));
    CHECK_FALSE(fs::exists(to / "opense4.previous.log"));
    CHECK_FALSE(fs::exists(to / "ModCache"));
    // Nothing in the old folder changed.
    CHECK(read(from / "saves" / "Beta.gam") == "beta (old)");
    CHECK(read(from / "opense4.log") == "this run's log");
    CHECK(fs::exists(from / "ModCache" / "abc" / "data.bin"));

    // Again: everything is there already.
    const core::CopyReport again = core::copyUserFiles(from, to);
    CHECK(again.copied == 0);
    CHECK(again.kept == 7);

    // A copy into a folder inside the source leaves that folder out (a program
    // kept in the user folder itself), and a folder onto itself copies nothing.
    const fs::path inner = from / "userdata";
    const core::CopyReport nested = core::copyUserFiles(from, inner);
    CHECK(nested.copied == 7);
    CHECK_FALSE(fs::exists(inner / "userdata"));
    CHECK(core::copyUserFiles(from, from / ".").copied == 0);
    // Nothing to copy from.
    CHECK(core::copyUserFiles(dir.path() / "none", to).copied == 0);
}

TEST_CASE("user folder: paths inside a folder") {
    const test::TempDir dir("user_folder_inside");
    const fs::path game = dir.path() / "Space Empires IV";
    CHECK(core::pathInside(game / "SaveGame", game));
    CHECK(core::pathInside(game, game));
    CHECK(core::pathInside(game / "x" / ".." / "SaveGame", game));
    CHECK_FALSE(core::pathInside(dir.path() / "Space Empires IV Saves", game));
    CHECK_FALSE(core::pathInside(dir.path(), game));
#if defined(_WIN32)
    CHECK(core::pathInside(dir.path() / "SPACE EMPIRES IV" / "SaveGame", game));   // letter case does not count on Windows
#endif
}

#if !defined(_WIN32)
TEST_CASE("user folder: a program folder that cannot be written is refused, and nothing is made in it") {
    const test::TempDir dir("user_folder_readonly");
    const fs::path program = dir.path() / "Program Files" / "OpenSE4";
    fs::create_directories(program);
    fs::permissions(program, fs::perms::owner_read | fs::perms::owner_exec | fs::perms::group_read | fs::perms::group_exec |
                                 fs::perms::others_read | fs::perms::others_exec);
    // The superuser writes anywhere: nothing to check then.
    const bool root = geteuid() == 0;
    if (!root) {
        const auto problem = core::portableProblem(program);
        REQUIRE(problem.has_value());
        CHECK(problem->find("cannot write") != std::string::npos);
        CHECK_FALSE(core::setPortable(program, true).has_value());
        CHECK_FALSE(fs::exists(program / "userdata"));
        CHECK_FALSE(fs::exists(program / "portable.txt"));
    }
    fs::permissions(program, fs::perms::owner_all);
}
#endif

TEST_CASE("user folder: a copy of a program finds the folder beside it, in a process of its own") {
    const test::TempDir dir("user_folder_probe");
    const fs::path program = dir.path() / "OpenSE4 copy";
    const fs::path probe = fs::path(OPENSE4_USER_FOLDER_PROBE_EXE);
    fs::create_directories(program);
    const fs::path exe = program / probe.filename();
    REQUIRE(fs::copy_file(probe, exe));
    const fs::path home = dir.path() / "home";
    fs::create_directories(home);
    // The program's run: the system's folder under the scratch home on every
    // platform; OPENSE4_USER_DIR empty, which counts as not set.
    auto run = [&](const std::string& userDir) {
        sdk::ProcessOptions po;
        if (const char* runner = std::getenv("OPENSE4_TEST_RUNNER"); runner && *runner) {   // a cross build's emulator
            std::istringstream words{std::string(runner)};
            for (std::string w; words >> w;) po.args.push_back(w);
        }
        po.args.push_back(exe.string());
        po.environment = {{"OPENSE4_USER_DIR", userDir},
                          {"HOME", home.string()},
                          {"XDG_DATA_HOME", (home / "data").string()},
                          {"APPDATA", (home / "AppData").string()}};
        po.output = dir.path() / "output.txt";
        auto p = sdk::Process::start(po);
        REQUIRE(p.has_value());
        const auto code = p->wait(std::chrono::seconds(60));
        REQUIRE(code.has_value());
        CHECK(*code == 0);
        std::vector<std::string> lines;
        std::istringstream in(read(po.output));
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        REQUIRE(lines.size() >= 2);
        return std::pair{lines[0], fs::path(lines[1])};
    };
    const auto plain = run("");
    CHECK(plain.first == "system");
    CHECK(core::pathInside(plain.second, home));

    write(program / "portable.txt", "");
    const auto portable = run("");
    CHECK(portable.first == "portable");
    CHECK(samePath(portable.second, program / "userdata"));

    const auto named = run((dir.path() / "scratch").string());
    CHECK(named.first == "OPENSE4_USER_DIR");
    CHECK(samePath(named.second, dir.path() / "scratch"));
}
