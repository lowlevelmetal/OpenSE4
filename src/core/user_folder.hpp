#pragma once

// Where OpenSE4 keeps a player's own files: settings, saved games, logs, the
// mods folder, history, host keys (docs/SETUP.md "Where OpenSE4 keeps its own
// files"). Every program of ours (the game, the dedicated server, opense4-sdk)
// follows the same rules, in this order:
//
//   1. the folder the environment variable OPENSE4_USER_DIR names (the tests
//      use a scratch folder), when it is set and not empty;
//   2. a portable copy: when the file portable.txt lies beside the program,
//      the folder "userdata" beside it;
//   3. the system's folder for a user's application data: %APPDATA%\OpenSE4
//      on Windows, ~/Library/Application Support/OpenSE4 on macOS, and
//      $XDG_DATA_HOME/OpenSE4 or ~/.local/share/OpenSE4 elsewhere;
//   4. "userdata" in the working folder when the system names none.
//
// "Beside the program" is the executable's folder, or for a macOS application
// bundle (OpenSE4.app/Contents/MacOS/opense4) the folder that holds the
// bundle, since the bundle itself must not change. The game's Settings →
// Files page turns a copy portable or back by creating or removing the marker
// (setPortable); copyUserFiles copies a player's files from one folder to
// another and never deletes or overwrites any.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace opense4::core {

inline constexpr std::string_view kUserDirVariable = "OPENSE4_USER_DIR";
inline constexpr std::string_view kPortableMarkerName = "portable.txt";
inline constexpr std::string_view kPortableFolderName = "userdata";

enum class UserFolderSource : uint8_t {
    Environment,  // OPENSE4_USER_DIR
    Portable,     // userdata beside the program, which has portable.txt beside it
    System,       // the system's folder for application data
};

struct UserFolder {
    std::filesystem::path path;
    UserFolderSource source = UserFolderSource::System;
};

// The platform whose rules apply: its own, or another one's in tests.
enum class OsFamily : uint8_t { Windows, MacOS, Unix };
OsFamily currentOs();

// An environment variable's value (UTF-8), or nothing when it is not set.
using VariableLookup = std::function<std::optional<std::string>(std::string_view name)>;

// The rules above, apart from this process: `environmentValue` is the value of
// OPENSE4_USER_DIR, `programFolder` the folder where portable.txt is looked
// for (empty: none), `systemFolder` the system's folder, asked for only when
// neither of the others decides (it may create it, as SDL's does).
UserFolder resolveUserFolder(const std::optional<std::string>& environmentValue, const std::filesystem::path& programFolder,
                             const std::function<std::filesystem::path()>& systemFolder);

// The system's folder for OpenSE4's files on `os`, from the variables `lookup`
// gives (APPDATA; HOME; XDG_DATA_HOME, HOME); empty when they do not say.
std::filesystem::path systemUserFolderFor(OsFamily os, const VariableLookup& lookup);
// The same for this platform and process.
std::filesystem::path systemUserFolder();

// The folder "beside the program" for an executable at `executable` on `os`:
// its own folder, or the folder holding its macOS application bundle.
std::filesystem::path programFolderFor(OsFamily os, const std::filesystem::path& executable);
// This process's executable (empty when the system does not say), and its
// program folder.
std::filesystem::path executablePath();
std::filesystem::path programFolder();

// This process's user folder by the rules above; `systemFolder` gives the
// system's folder (by default systemUserFolder; the game asks SDL).
UserFolder userFolder(const std::function<std::filesystem::path()>& systemFolder = systemUserFolder);

// portable.txt and userdata in a program folder.
std::filesystem::path portableMarker(const std::filesystem::path& programFolder);
std::filesystem::path portableFolder(const std::filesystem::path& programFolder);
bool portableMarkerPresent(const std::filesystem::path& programFolder);

// Why the copy of OpenSE4 in `programFolder` cannot keep its files there (the
// folder, or its userdata folder, cannot be written: a copy installed into
// Program Files, a system folder of Linux), or nothing when it can. It tries:
// creates the userdata folder and a file in it, and removes the file.
std::optional<std::string> portableProblem(const std::filesystem::path& programFolder);

// Makes the copy in `programFolder` portable (creates the userdata folder and
// portable.txt, with a few lines that say what it does) or not (removes
// portable.txt; the userdata folder and its files stay).
std::expected<void, std::string> setPortable(const std::filesystem::path& programFolder, bool on);

// What copyUserFiles did.
struct CopyReport {
    size_t copied = 0;   // files copied
    size_t kept = 0;     // files the destination already had: left as they were
    size_t failed = 0;   // files that could not be copied
    std::string firstError;
};

// Copies a player's files from the user folder `from` into `to`: every file
// and folder, keeping their places, except the logs (opense4.log,
// opense4.previous.log) and the mods' cache (ModCache), which each folder
// makes for itself. A file `to` already has is kept as it is; nothing in
// `from` is changed or removed. A `to` inside `from` (or the other way round)
// is left out of the copy.
CopyReport copyUserFiles(const std::filesystem::path& from, const std::filesystem::path& to);

// Whether `path` is `folder` or lies inside it (paths made absolute and
// normal first; letter case ignored on Windows).
bool pathInside(const std::filesystem::path& path, const std::filesystem::path& folder);

// "OPENSE4_USER_DIR", "portable", "system": for logs and the help text.
std::string_view userFolderSourceName(UserFolderSource s);

} // namespace opense4::core
