#pragma once

// Crash reports. When the program dies of a fault (an access violation, a
// segmentation fault, an abort, ...) or of an exception nothing caught, a
// short report is appended to the log file (opense4.log in the user data
// folder): the version and platform, what happened, the stack as module and
// offset where the platform gives one (Windows, and Linux and macOS with
// glibc's or the system's backtrace), and the last log lines. A message box
// then says where the file is, so that a player can send it with a bug
// report. The next start keeps that log as opense4.previous.log
// (keepPreviousLog), so it is not lost when the game is started again first.
//
// Windows: an unhandled-exception filter (SetUnhandledExceptionFilter) with
// a stack guarantee for stack overflows. Linux and macOS: handlers for
// SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGABRT on an alternate stack; they
// write with async-signal-safe calls only, and the message box is shown by
// the program started again (`opense4 --crash-message=FILE`), since a
// signal handler cannot safely open a window. All platforms: a terminate
// handler names the uncaught exception. Builds with AddressSanitizer keep
// the sanitizer's own signal handlers.

#include <filesystem>
#include <string>

namespace opense4::client {

struct CrashOptions {
    std::filesystem::path logFile;   // where the report goes (the log's file, log::setFile)
    // The message box after the report; off for automation (scripts,
    // screenshots, headless runs), which must never wait on a window.
    bool showMessage = true;
};

void installCrashHandler(const CrashOptions& options);

// Renames the last run's log (`logFile`) to opense4.previous.log beside it,
// replacing an older one, before this run's log replaces it.
void keepPreviousLog(const std::filesystem::path& logFile);

// The message box's text.
std::string crashMessage(const std::filesystem::path& logFile);
// `opense4 --crash-message=FILE`: shows that message box and returns (the
// crashed program starts it on Linux and macOS).
void showCrashMessage(const std::filesystem::path& logFile);

} // namespace opense4::client
