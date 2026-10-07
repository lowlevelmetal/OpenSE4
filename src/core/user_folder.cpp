#include "core/user_folder.hpp"

#include "core/environment.hpp"

#include <array>
#include <format>
#include <fstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#endif

namespace opense4::core {

namespace fs = std::filesystem;

namespace {

// What every user folder makes for itself: not copied from one to another.
constexpr std::array<std::string_view, 2> kOwnLogs{"opense4.log", "opense4.previous.log"};
constexpr std::string_view kModCache = "ModCache";
constexpr std::string_view kWriteTest = ".opense4-write-test";

// One component of a path against another: letter case ignored on Windows,
// whose file systems ignore it too.
bool sameName(const fs::path& a, const fs::path& b) {
#if defined(_WIN32)
    return _wcsicmp(a.native().c_str(), b.native().c_str()) == 0;
#else
    return a == b;
#endif
}

// Absolute, without "." and "..", links resolved as far as the path exists, and
// without a trailing separator.
fs::path normalFull(const fs::path& p) {
    std::error_code ec;
    fs::path full = fs::absolute(p, ec);
    if (ec) full = p;
    fs::path canonical = fs::weakly_canonical(full, ec);
    if (ec) canonical = full.lexically_normal();
    if (!canonical.has_filename() && canonical.has_relative_path()) canonical = canonical.parent_path();
    return canonical;
}

// Writes a short file and removes it: whether `folder` takes new files.
std::optional<std::string> writeProblem(const fs::path& folder) {
    const fs::path probe = folder / kWriteTest;
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out || !(out << "OpenSE4\n") || !out.flush()) return std::format("cannot write in {}", folder.string());
    }
    std::error_code ec;
    fs::remove(probe, ec);
    return std::nullopt;
}

} // namespace

OsFamily currentOs() {
#if defined(_WIN32)
    return OsFamily::Windows;
#elif defined(__APPLE__)
    return OsFamily::MacOS;
#else
    return OsFamily::Unix;
#endif
}

UserFolder resolveUserFolder(const std::optional<std::string>& environmentValue, const fs::path& programFolder,
                             const std::function<fs::path()>& systemFolder) {
    // UTF-8, as every narrow string that becomes a path (core/environment.hpp).
    if (environmentValue && !environmentValue->empty()) return {fs::path(*environmentValue), UserFolderSource::Environment};
    if (!programFolder.empty() && portableMarkerPresent(programFolder)) return {portableFolder(programFolder), UserFolderSource::Portable};
    fs::path system = systemFolder ? systemFolder() : fs::path();
    if (system.empty()) {
        std::error_code ec;
        system = fs::current_path(ec) / kPortableFolderName;
    }
    return {system, UserFolderSource::System};
}

fs::path systemUserFolderFor(OsFamily os, const VariableLookup& lookup) {
    auto variable = [&](std::string_view name) {
        const std::optional<std::string> v = lookup ? lookup(name) : std::nullopt;
        return v.value_or(std::string());
    };
    switch (os) {
        case OsFamily::Windows:
            if (const std::string appData = variable("APPDATA"); !appData.empty()) return fs::path(appData) / "OpenSE4";
            break;
        case OsFamily::MacOS:
            if (const std::string home = variable("HOME"); !home.empty()) return fs::path(home) / "Library" / "Application Support" / "OpenSE4";
            break;
        case OsFamily::Unix:
            if (const std::string data = variable("XDG_DATA_HOME"); !data.empty()) return fs::path(data) / "OpenSE4";
            if (const std::string home = variable("HOME"); !home.empty()) return fs::path(home) / ".local" / "share" / "OpenSE4";
            break;
    }
    return {};
}

fs::path systemUserFolder() {
    return systemUserFolderFor(currentOs(), [](std::string_view name) { return environment(std::string(name).c_str()); });
}

fs::path programFolderFor(OsFamily os, const fs::path& executable) {
    if (executable.empty()) return {};
    const fs::path folder = executable.parent_path();
    if (os == OsFamily::MacOS) {
        // OpenSE4.app/Contents/MacOS/opense4: beside the bundle, which must stay as it was signed.
        const fs::path contents = folder.parent_path();
        const fs::path bundle = contents.parent_path();
        if (folder.filename() == "MacOS" && contents.filename() == "Contents" && bundle.extension() == ".app") return bundle.parent_path();
    }
    return folder;
}

fs::path executablePath() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (int tries = 0; tries < 8; ++tries) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) return {};
        if (n < buffer.size()) {
            buffer.resize(n);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);   // cut short: a longer path
    }
    return {};
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code ec;
    const fs::path p = fs::canonical(fs::path(buffer.c_str()), ec);
    return ec ? fs::path(buffer.c_str()) : p;
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : p;
#endif
}

fs::path programFolder() {
    static const fs::path folder = programFolderFor(currentOs(), executablePath());
    return folder;
}

UserFolder userFolder(const std::function<fs::path()>& systemFolder) {
    return resolveUserFolder(environment(std::string(kUserDirVariable).c_str()), programFolder(), systemFolder);
}

fs::path portableMarker(const fs::path& programFolder) { return programFolder / kPortableMarkerName; }
fs::path portableFolder(const fs::path& programFolder) { return programFolder / kPortableFolderName; }

bool portableMarkerPresent(const fs::path& programFolder) {
    std::error_code ec;
    return fs::is_regular_file(portableMarker(programFolder), ec);
}

std::optional<std::string> portableProblem(const fs::path& programFolder) {
    if (programFolder.empty()) return std::string("the system did not say which folder OpenSE4 runs from");
    // The marker goes beside the program, the files into userdata.
    if (auto problem = writeProblem(programFolder)) return problem;
    std::error_code ec;
    const fs::path folder = portableFolder(programFolder);
    fs::create_directories(folder, ec);
    if (ec) return std::format("cannot create {}: {}", folder.string(), ec.message());
    return writeProblem(folder);
}

std::expected<void, std::string> setPortable(const fs::path& programFolder, bool on) {
    if (programFolder.empty()) return std::unexpected(std::string("the system did not say which folder OpenSE4 runs from"));
    const fs::path marker = portableMarker(programFolder);
    std::error_code ec;
    if (!on) {
        fs::remove(marker, ec);
        if (ec || fs::exists(marker)) return std::unexpected(std::format("cannot remove {}: {}", marker.string(), ec ? ec.message() : "it is still there"));
        return {};
    }
    fs::create_directories(portableFolder(programFolder), ec);
    if (ec) return std::unexpected(std::format("cannot create {}: {}", portableFolder(programFolder).string(), ec.message()));
    // A few lines for whoever opens it; what it holds is never read. Windows'
    // own editors want CRLF line ends.
    const char* eol = currentOs() == OsFamily::Windows ? "\r\n" : "\n";
    std::ofstream out(marker, std::ios::binary | std::ios::trunc);
    out << "This file makes this copy of OpenSE4 portable: it keeps its saved games, settings," << eol
        << "logs, mods and history in the folder \"userdata\" beside it, instead of in your" << eol
        << "user folder." << eol << eol
        << "To use your user folder again, delete this file, or untick \"Keep saves and settings" << eol
        << "in OpenSE4's folder\" in Settings, Files. The files in userdata stay where they are." << eol;
    if (!out.flush()) return std::unexpected(std::format("cannot write {}", marker.string()));
    return {};
}

CopyReport copyUserFiles(const fs::path& from, const fs::path& to) {
    CopyReport report;
    std::error_code ec;
    if (!fs::is_directory(from, ec)) return report;   // nothing there yet
    const fs::path source = normalFull(from), target = normalFull(to);
    if (sameName(source, target)) return report;
    auto failed = [&](const fs::path& p, const std::error_code& e) {
        ++report.failed;
        if (report.firstError.empty()) report.firstError = std::format("{}: {}", p.string(), e.message());
    };
    fs::create_directories(target, ec);
    if (ec) {
        failed(target, ec);
        return report;
    }
    const bool targetInside = pathInside(target, source);
    fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        failed(source, ec);
        return report;
    }
    const fs::recursive_directory_iterator end{};
    for (; !ec && it != end; it.increment(ec)) {
        const fs::path& path = it->path();
        const fs::path relative = path.lexically_relative(source);
        const fs::path first = relative.empty() ? fs::path() : *relative.begin();
        const bool topLevel = it.depth() == 0;
        std::error_code e;
        const bool directory = it->is_directory(e) && !it->is_symlink(e);
        // The mods' cache and the logs belong to each folder; the copy's own target too.
        if ((topLevel && directory && sameName(first, kModCache)) || (targetInside && pathInside(path, target))) {
            if (directory) it.disable_recursion_pending();
            continue;
        }
        if (topLevel && !directory) {
            bool own = sameName(first, kWriteTest);
            for (std::string_view name : kOwnLogs) own = own || sameName(first, name);
            if (own) continue;
        }
        const fs::path destination = target / relative;
        if (directory) {
            fs::create_directories(destination, e);
            if (e) failed(destination, e);
            continue;
        }
        if (!it->is_regular_file(e)) continue;   // a link to a folder, a device: not a player's file
        if (fs::exists(destination, e)) {
            ++report.kept;
            continue;
        }
        fs::create_directories(destination.parent_path(), e);
        if (!e) fs::copy_file(path, destination, fs::copy_options::none, e);
        if (e) failed(destination, e);
        else ++report.copied;
    }
    if (ec) failed(source, ec);   // the folder could not be read to its end
    return report;
}

bool pathInside(const fs::path& path, const fs::path& folder) {
    const fs::path p = normalFull(path), f = normalFull(folder);
    auto pi = p.begin();
    for (auto fi = f.begin(); fi != f.end(); ++fi, ++pi) {
        if (pi == p.end() || !sameName(*pi, *fi)) return false;
    }
    return true;
}

std::string_view userFolderSourceName(UserFolderSource s) {
    switch (s) {
        case UserFolderSource::Environment: return kUserDirVariable;
        case UserFolderSource::Portable: return "portable";
        case UserFolderSource::System: return "system";
    }
    return "?";
}

} // namespace opense4::core
