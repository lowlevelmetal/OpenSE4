#include "client/crash_report.hpp"

#include "core/log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define OPENSE4_HAVE_BACKTRACE 1
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

// AddressSanitizer reports faults itself: its signal handlers stay.
#if defined(__SANITIZE_ADDRESS__)
#define OPENSE4_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OPENSE4_SANITIZED 1
#endif
#endif

#ifndef OPENSE4_CLIENT_VERSION
#define OPENSE4_CLIENT_VERSION "unknown"
#endif

namespace opense4::client {

namespace {

constexpr const char* kSystem =
#if defined(_WIN32)
    "Windows";
#elif defined(__APPLE__)
    "macOS";
#elif defined(__linux__)
    "Linux";
#else
    "an unknown system";
#endif

constexpr const char* kArch =
#if defined(__x86_64__) || defined(_M_X64)
    "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    "arm";
#elif defined(__i386__) || defined(_M_IX86)
    "x86";
#else
    "unknown processor";
#endif

constexpr const char* kCompiler =
#if defined(__clang__)
    "Clang " __clang_version__;
#elif defined(__GNUC__)
    "GCC " __VERSION__;
#elif defined(_MSC_VER)
    "MSVC";
#else
    "an unknown compiler";
#endif

// Made at installation, so that a crash only copies bytes.
char gHeader[1024];
size_t gHeaderSize = 0;
bool gShowMessage = false;
std::filesystem::path gLogFile;
std::atomic<bool> gReporting{false};

void put(std::string_view s) { log::crashWrite(s); }

// Numbers without the C library's formatting (not async-signal-safe).
[[maybe_unused]] void putHex(uint64_t v) {
    char buf[19] = "0x";
    char digits[16];
    int n = 0;
    do {
        digits[n++] = "0123456789abcdef"[v & 0xF];
        v >>= 4;
    } while (v != 0 && n < 16);
    for (int i = 0; i < n; ++i) buf[2 + i] = digits[n - 1 - i];
    log::crashWrite(buf, size_t(2 + n));
}

[[maybe_unused]] void putDec(long long v) {
    char buf[24];
    int n = 0;
    const bool negative = v < 0;
    unsigned long long u = negative ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
    do {
        buf[sizeof buf - 1 - size_t(n++)] = char('0' + int(u % 10));
        u /= 10;
    } while (u != 0 && n < 22);
    if (negative) buf[sizeof buf - 1 - size_t(n++)] = '-';
    log::crashWrite(buf + sizeof buf - size_t(n), size_t(n));
}

void putHeader() { log::crashWrite(gHeader, gHeaderSize); }

void putFooter() {
    put("Last log lines:\n");
    log::crashWriteRecentLines("  ");
    put("==== end of the crash report ====\n");
}

#ifdef _WIN32

wchar_t gMessage[2048];
// The faulting thread's stack (the report may be made on another thread).
DWORD64 gStackLow = 0, gStackHigh = 0;

// "module+0xoffset" for an address in a loaded module, else the address.
void putAddress(DWORD64 address) {
    HMODULE module = nullptr;
    wchar_t wide[MAX_PATH];
    char name[MAX_PATH];
    put("  ");
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(address)), &module) &&
        GetModuleFileNameW(module, wide, MAX_PATH) > 0) {
        const wchar_t* base = wide;
        for (const wchar_t* p = wide; *p; ++p)
            if (*p == L'\\' || *p == L'/') base = p + 1;
        const int n = WideCharToMultiByte(CP_UTF8, 0, base, -1, name, MAX_PATH, nullptr, nullptr);
        if (n > 1) log::crashWrite(name, size_t(n - 1));
        put("+");
        putHex(address - reinterpret_cast<uintptr_t>(module));
    } else {
        putHex(address);
    }
    put("\n");
}

void putStack(const CONTEXT* start) {
    put("Stack:\n");
#if defined(_M_X64) || defined(__x86_64__)
    // From the faulting instruction outward, with the unwind tables every x64 module carries.
    CONTEXT ctx;
    if (start) ctx = *start;
    else RtlCaptureContext(&ctx);
    DWORD64 low = gStackLow, high = gStackHigh;
    if (high == 0) {
        const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
        low = reinterpret_cast<uintptr_t>(tib->StackLimit);
        high = reinterpret_cast<uintptr_t>(tib->StackBase);
    }
    for (int frame = 0; frame < 64 && ctx.Rip != 0; ++frame) {
        putAddress(ctx.Rip);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fn) {
            // A leaf function: the return address is on top of the stack.
            if (ctx.Rsp < low || ctx.Rsp + 8 > high) break;
            ctx.Rip = *reinterpret_cast<const DWORD64*>(ctx.Rsp);
            ctx.Rsp += 8;
            continue;
        }
        void* handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
    }
#else
    (void)start;
    void* frames[64];
    const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) putAddress(reinterpret_cast<uintptr_t>(frames[i]));
#endif
}

const char* exceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer division by zero";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_IN_PAGE_ERROR: return "page error";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case 0xE06D7363: return "C++ exception (Visual C++)";
        case 0x20474343: return "C++ exception (GCC)";
        default: return "exception";
    }
}

void showMessage() {
    if (gShowMessage) MessageBoxW(nullptr, gMessage, L"OpenSE4", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND | MB_TASKMODAL);
}

void report(EXCEPTION_POINTERS* info) {
    putHeader();
    const EXCEPTION_RECORD* rec = info ? info->ExceptionRecord : nullptr;
    put("What: ");
    if (rec) {
        put(exceptionName(rec->ExceptionCode));
        put(" (");
        putHex(rec->ExceptionCode);
        put(")");
        if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && rec->NumberParameters >= 2) {
            put(rec->ExceptionInformation[0] == 0 ? ", reading " : rec->ExceptionInformation[0] == 8 ? ", executing " : ", writing ");
            putHex(rec->ExceptionInformation[1]);
        }
        put("\n");
    } else {
        put("an unknown fault\n");
    }
    putStack(info ? info->ContextRecord : nullptr);
    putFooter();
    showMessage();
}

DWORD WINAPI reportThread(void* info) {
    report(static_cast<EXCEPTION_POINTERS*>(info));
    return 0;
}

LONG WINAPI onException(EXCEPTION_POINTERS* info) {
    if (gReporting.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    gStackLow = reinterpret_cast<uintptr_t>(tib->StackLimit);
    gStackHigh = reinterpret_cast<uintptr_t>(tib->StackBase);
    // After a stack overflow little stack is left here: the report is made
    // on a thread of its own, which reads the faulting thread's stack.
    if (info && info->ExceptionRecord && info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW)
        if (HANDLE thread = CreateThread(nullptr, 256 * 1024, reportThread, info, 0, nullptr)) {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
            return EXCEPTION_EXECUTE_HANDLER;
        }
    report(info);
    return EXCEPTION_EXECUTE_HANDLER;   // the process ends
}

#else

char gExe[4096];
char gArgument[4096 + 32];
void putStack() {
#ifdef OPENSE4_HAVE_BACKTRACE
    put("Stack:\n");
    void* frames[64];
    const int n = backtrace(frames, 64);
    // Each frame as module(symbol+offset) [address]; written to both places.
    if (const int fd = log::crashFileDescriptor(); fd >= 0) backtrace_symbols_fd(frames, n, fd);
    backtrace_symbols_fd(frames, n, 2);
#else
    put("Stack: not available on this system\n");
#endif
}

#ifndef OPENSE4_SANITIZED

// Room for the handlers when the fault is a stack overflow.
alignas(16) char gAltStack[64 * 1024];

const char* signalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV, segmentation fault";
        case SIGBUS: return "SIGBUS, bus error";
        case SIGILL: return "SIGILL, illegal instruction";
        case SIGFPE: return "SIGFPE, arithmetic error";
        case SIGABRT: return "SIGABRT, abort";
        default: return "signal";
    }
}

// The message box from the program started again: a signal handler cannot open windows.
void showMessage() {
    if (!gShowMessage || gExe[0] == '\0') return;
    char* const argv[] = {gExe, gArgument, nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, gExe, nullptr, nullptr, argv, environ) == 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
}

void onSignal(int sig, siginfo_t* info, void*) {
    if (!gReporting.exchange(true)) {
        putHeader();
        put("What: signal ");
        putDec(sig);
        put(" (");
        put(signalName(sig));
        put(")");
        if (info && sig != SIGABRT) {
            put(", address ");
            putHex(reinterpret_cast<uintptr_t>(info->si_addr));
        }
        put("\n");
        putStack();
        putFooter();
        showMessage();
    }
    // Then the program dies of the signal, as it would have without the handler.
    signal(sig, SIG_DFL);
    raise(sig);
}

#endif // OPENSE4_SANITIZED

#endif

// An exception nothing caught (all platforms).
[[noreturn]] void onTerminate() {
    if (!gReporting.exchange(true)) {
        std::string what = "std::terminate was called without an exception";
        if (const std::exception_ptr e = std::current_exception()) {
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& x) {
                what = std::format("an exception nothing caught: {}", x.what());
            } catch (...) {
                what = "an exception nothing caught (not a std::exception)";
            }
        }
        putHeader();
        put("What: ");
        put(what);
        put("\n");
#ifdef _WIN32
        putStack(nullptr);
#else
        putStack();
#endif
        putFooter();
#ifdef _WIN32
        showMessage();
#else
        // Not in a signal handler: the message box can be shown here.
        if (gShowMessage) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenSE4", crashMessage(gLogFile).c_str(), nullptr);
#endif
    }
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 3);
#else
    signal(SIGABRT, SIG_DFL);
#endif
    std::abort();
}

} // namespace

std::string crashMessage(const std::filesystem::path& logFile) {
    return std::format("OpenSE4 has stopped because of an error.\n\n"
                       "A report was written to the end of\n{}\n\n"
                       "Please send that file with your bug report. When OpenSE4 starts again it keeps the file as {}.",
                       logFile.string(), (logFile.parent_path() / "opense4.previous.log").string());
}

void showCrashMessage(const std::filesystem::path& logFile) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenSE4", crashMessage(logFile).c_str(), nullptr);
}

void keepPreviousLog(const std::filesystem::path& logFile) {
    std::error_code ec;
    if (!std::filesystem::exists(logFile, ec)) return;
    std::filesystem::rename(logFile, logFile.parent_path() / "opense4.previous.log", ec);
}

void installCrashHandler(const CrashOptions& options) {
    const std::string header = std::format("==== OpenSE4 crash report ====\nVersion: {} ({}, {}, {})\n", OPENSE4_CLIENT_VERSION, kSystem, kArch, kCompiler);
    gHeaderSize = std::min(header.size(), sizeof gHeader);
    std::memcpy(gHeader, header.data(), gHeaderSize);
    gShowMessage = options.showMessage;
    gLogFile = options.logFile;
    std::set_terminate(onTerminate);
#ifdef _WIN32
    const std::string message = crashMessage(options.logFile);
    const int n = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, gMessage, int(std::size(gMessage)));
    if (n <= 0) gMessage[0] = L'\0';
    // Stack left for the filter after a stack overflow.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(onException);
#else
#if defined(__linux__)
    const ssize_t len = readlink("/proc/self/exe", gExe, sizeof gExe - 1);
    gExe[len > 0 ? len : 0] = '\0';
#elif defined(__APPLE__)
    uint32_t size = sizeof gExe;
    if (_NSGetExecutablePath(gExe, &size) != 0) gExe[0] = '\0';
#endif
    const std::string argument = "--crash-message=" + options.logFile.string();
    if (argument.size() < sizeof gArgument) std::memcpy(gArgument, argument.c_str(), argument.size() + 1);
    else gExe[0] = '\0';   // no message rather than a cut path
#ifndef OPENSE4_SANITIZED
#ifdef OPENSE4_HAVE_BACKTRACE
    // The first backtrace loads the unwinder; not in a signal handler.
    void* warm[4];
    backtrace(warm, 4);
#endif
    stack_t alt{};
    alt.ss_sp = gAltStack;
    alt.ss_size = sizeof gAltStack;
    sigaltstack(&alt, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = onSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, nullptr);
#endif
#endif
}

} // namespace opense4::client
