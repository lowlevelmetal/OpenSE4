#pragma once

// The process environment and command line as UTF-8, the encoding of every
// narrow string in OpenSE4 (docs/ENGINE.md, "Same on every platform"). On
// Windows the C library hands out both in the ANSI code page, which is UTF-8
// only where the programs' manifest sets it (Windows 10 1903 and later): on
// Windows 7 a user folder such as C:\Users\José would otherwise reach
// std::filesystem as bytes that are not UTF-8. Elsewhere they pass unchanged.

#include <optional>
#include <string>
#include <vector>

namespace opense4::core {

// The variable's value, or nothing when it is not set.
std::optional<std::string> environment(const char* name);

// argv[0..argc) of main() as UTF-8. Characters outside the ANSI code page were
// already lost by the C library on Windows versions before 10 1903.
std::vector<std::string> utf8Arguments(int argc, char** argv);

} // namespace opense4::core
