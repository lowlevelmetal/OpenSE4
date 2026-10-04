#include "core/environment.hpp"

#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace opense4::core {

#ifdef _WIN32
namespace {

std::string toUtf8(const wchar_t* text, int length) {
    if (length == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), n, nullptr, nullptr);
    return out;
}

} // namespace

std::optional<std::string> environment(const char* name) {
    std::wstring wide;
    for (const char* c = name; *c; ++c) wide.push_back(static_cast<unsigned char>(*c));  // ASCII names
    const DWORD size = GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
    if (size == 0) return std::nullopt;
    std::wstring value(size, L'\0');
    const DWORD length = GetEnvironmentVariableW(wide.c_str(), value.data(), size);
    if (length >= size) return std::nullopt;  // changed meanwhile
    return toUtf8(value.data(), static_cast<int>(length));
}

std::vector<std::string> utf8Arguments(int argc, char** argv) {
    std::vector<std::string> out;
    for (int i = 0; i < argc; ++i) {
        const int length = static_cast<int>(std::char_traits<char>::length(argv[i]));
        const int n = length ? MultiByteToWideChar(CP_ACP, 0, argv[i], length, nullptr, 0) : 0;
        std::wstring wide(static_cast<size_t>(n > 0 ? n : 0), L'\0');
        if (n > 0) MultiByteToWideChar(CP_ACP, 0, argv[i], length, wide.data(), n);
        out.push_back(toUtf8(wide.data(), static_cast<int>(wide.size())));
    }
    return out;
}
#else
std::optional<std::string> environment(const char* name) {
    if (const char* value = std::getenv(name)) return std::string(value);
    return std::nullopt;
}

std::vector<std::string> utf8Arguments(int argc, char** argv) { return std::vector<std::string>(argv, argv + argc); }
#endif

} // namespace opense4::core
