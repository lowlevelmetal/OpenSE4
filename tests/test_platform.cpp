// The platform layer under every narrow string: environment variables,
// command-line arguments and file names are UTF-8 on every platform and every
// Windows version (core/environment.hpp, compat/libcxx_utf8_paths.cpp;
// docs/ENGINE.md, "Same on every platform").

#include "core/environment.hpp"
#if defined(OPENSE4_LIBCXX_UTF8_PATHS)
#include "compat/libcxx_utf8_paths.hpp"
#endif
#include "temp_dir.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace opense4;

namespace {

const std::string kJose = "Jos\xC3\xA9";               // José, in Windows' Western code page too
const std::string kNihon = "\xE6\x97\xA5\xE6\x9C\xAC";  // 日本, in none of the Western ones

} // namespace

TEST_CASE("platform: environment variables are read as UTF-8") {
#ifdef _WIN32
    REQUIRE(_wputenv_s(L"OPENSE4_TEST_UTF8", L"José 日本") == 0);
#else
    REQUIRE(setenv("OPENSE4_TEST_UTF8", (kJose + " " + kNihon).c_str(), 1) == 0);
#endif
    CHECK(core::environment("OPENSE4_TEST_UTF8") == kJose + " " + kNihon);
    CHECK_FALSE(core::environment("OPENSE4_TEST_SURELY_NOT_SET").has_value());
}

TEST_CASE("platform: command-line arguments pass as UTF-8") {
    char program[] = "opense4-server";
    char option[] = "--port=6720";
    char* argv[] = {program, option};
    CHECK(core::utf8Arguments(2, argv) == std::vector<std::string>{"opense4-server", "--port=6720"});
}

TEST_CASE("platform: narrow file names are UTF-8") {
    CHECK(std::filesystem::path(kJose).u8string() == u8"José");
    CHECK(std::filesystem::path(u8"日本").string() == kNihon);

    // A file made under a UTF-8 name is listed under it.
    test::TempDir dir("utf8");
    std::ofstream(dir / (kNihon + ".txt")) << "x";
    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path()))
        if (entry.path().filename().string() == kNihon + ".txt") found = true;
    CHECK(found);

#if defined(OPENSE4_LIBCXX_UTF8_PATHS)
    // libc++ on Windows: through compat/libcxx_utf8_paths.cpp, so the result does not
    // depend on the ANSI code page (UTF-8 only from Windows 10 1903 on).
    const unsigned before = compat::utf8PathConversions();
    CHECK(std::filesystem::path(kJose).wstring() == L"José");
    CHECK(std::filesystem::path(L"日本").string() == kNihon);
    CHECK(compat::utf8PathConversions() >= before + 2);
    CHECK_THROWS_AS(std::filesystem::path(std::string("Jos\xE9")), std::filesystem::filesystem_error);
#endif
}
