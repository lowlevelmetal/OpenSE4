// std::filesystem with UTF-8 narrow strings in Windows builds with libc++ (the
// llvm-mingw release toolchain, cmake/WindowsCompat.cmake).
//
// Every narrow string in OpenSE4 is UTF-8, paths included (docs/ENGINE.md, "Same on
// every platform"). libc++ converts between a path and a narrow string with the
// ANSI code page. The programs' manifest makes that UTF-8 on Windows 10 1903 and
// later; Windows 7, 8 and 8.1 ignore it and keep the system's legacy code page, so
// there a UTF-8 name from SDL or a settings file (C:\Users\José) would name another
// folder, and path::string() of a name outside that code page would throw. GCC's
// libstdc++ uses UTF-8 on Windows whatever the code page. The linker sends libc++'s
// two conversion functions here (--wrap), so libc++ does the same.

#include "compat/libcxx_utf8_paths.hpp"

#include <atomic>
#include <climits>
#include <filesystem>
#include <string>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

std::atomic<unsigned> gConversions{0};

[[noreturn]] void invalid(const char* what) {
    throw std::filesystem::filesystem_error(what, std::make_error_code(std::errc::illegal_byte_sequence));
}

} // namespace

namespace opense4::compat {

unsigned utf8PathConversions() { return gConversions.load(); }

} // namespace opense4::compat

// The replacements, under the names the linker gives them: __wrap_ and the mangled
// name of each libc++ function. Same contract as libc++'s: the length in the
// other encoding, written to `out` when `outlen` is enough; called with no buffer
// first to learn the length. Malformed input throws, as there.
extern "C" {

// std::__fs::filesystem::__char_to_wide(const std::string&, wchar_t*, size_t)
size_t __wrap__ZNSt3__14__fs10filesystem14__char_to_wideERKNS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEEPwy(
    const std::string& in, wchar_t* out, size_t outlen);
size_t __wrap__ZNSt3__14__fs10filesystem14__char_to_wideERKNS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEEPwy(
    const std::string& in, wchar_t* out, size_t outlen) {
    ++gConversions;
    if (in.empty()) return 0;
    if (in.size() > INT_MAX || outlen > INT_MAX) invalid("path too long");
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(), static_cast<int>(in.size()), out,
                                      static_cast<int>(outlen));
    if (n <= 0) invalid("a narrow path that is not UTF-8");
    return static_cast<size_t>(n);
}

// std::__fs::filesystem::__wide_to_char(const std::wstring&, char*, size_t)
size_t __wrap__ZNSt3__14__fs10filesystem14__wide_to_charERKNS_12basic_stringIwNS_11char_traitsIwEENS_9allocatorIwEEEEPcy(
    const std::wstring& in, char* out, size_t outlen);
size_t __wrap__ZNSt3__14__fs10filesystem14__wide_to_charERKNS_12basic_stringIwNS_11char_traitsIwEENS_9allocatorIwEEEEPcy(
    const std::wstring& in, char* out, size_t outlen) {
    ++gConversions;
    if (in.empty()) return 0;
    if (in.size() > INT_MAX || outlen > INT_MAX) invalid("path too long");
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, in.data(), static_cast<int>(in.size()), out,
                                      static_cast<int>(outlen), nullptr, nullptr);
    if (n <= 0) invalid("a path that is not valid UTF-16");
    return static_cast<size_t>(n);
}

} // extern "C"
