#include "sdk/process.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace opense4::sdk {

std::string executableName(std::string_view base) {
#ifdef _WIN32
    return std::string(base) + ".exe";
#else
    return std::string(base);
#endif
}

#ifdef _WIN32

namespace {

std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string lastErrorText() {
    const DWORD e = GetLastError();
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0,
                   reinterpret_cast<LPSTR>(&msg), 0, nullptr);
    std::string s = msg ? msg : std::format("error {}", e);
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '.')) s.pop_back();
    return s;
}

// One argument as the C runtime's command-line parser reads it back.
void appendQuoted(std::wstring& line, const std::wstring& arg) {
    if (!line.empty()) line += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += arg;
        return;
    }
    line += L'"';
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') line.append(slashes * 2 + 1, L'\\');
        else line.append(slashes, L'\\');
        slashes = 0;
        line += c;
    }
    line.append(slashes * 2, L'\\');
    line += L'"';
}

bool sameName(const std::wstring& entry, const std::wstring& name) {
    if (entry.size() <= name.size() || entry[name.size()] != L'=') return false;
    return CompareStringOrdinal(entry.data(), static_cast<int>(name.size()), name.data(), static_cast<int>(name.size()), TRUE) == CSTR_EQUAL;
}

} // namespace

struct Process::Impl {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    DWORD pid = 0;
    std::optional<int> exit;

    ~Impl() {
        if (process) CloseHandle(process);
        if (job) CloseHandle(job);   // kills what is left (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
    }
};

std::expected<Process, std::string> Process::start(const ProcessOptions& o) {
    std::wstring line;
    if (!o.shellCommand.empty()) {
        // /s: cmd strips the outer quotes and keeps the rest as written.
        line = L"cmd.exe /d /s /c \"" + wide(o.shellCommand) + L"\"";
    } else {
        if (o.args.empty()) return std::unexpected(std::string("no program to start"));
        for (const std::string& a : o.args) appendQuoted(line, wide(a));
    }

    // The environment: the parent's, with the given variables set.
    std::vector<std::wstring> env;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) env.emplace_back(p);
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : o.environment) {
        const std::wstring n = wide(name);
        std::erase_if(env, [&](const std::wstring& e) { return sameName(e, n); });
        env.push_back(n + L"=" + wide(value));
    }
    std::wstring envBlock;
    for (const std::wstring& e : env) {
        envBlock += e;
        envBlock += L'\0';
    }
    envBlock += L'\0';

    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE out = INVALID_HANDLE_VALUE;
    HANDLE in = INVALID_HANDLE_VALUE;
    if (!o.output.empty()) {
        out = CreateFileW(o.output.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) return std::unexpected(std::format("{}: {}", o.output.string(), lastErrorText()));
        in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    }
    struct Handles {
        HANDLE a, b;
        ~Handles() {
            if (a != INVALID_HANDLE_VALUE) CloseHandle(a);
            if (b != INVALID_HANDLE_VALUE) CloseHandle(b);
        }
    } handles{out, in};

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(STARTUPINFOW);
    std::vector<char> attributeBuffer;
    HANDLE inherited[2] = {out, in};
    const bool redirect = out != INVALID_HANDLE_VALUE && in != INVALID_HANDLE_VALUE;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;
    if (redirect) {
        // Only the two handles go to the child: not the parent's sockets.
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attributeBuffer.resize(size);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
        if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size) ||
            !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
            return std::unexpected(std::string("could not limit the handles the child inherits: ") + lastErrorText());
        si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdOutput = out;
        si.StartupInfo.hStdError = out;
        si.StartupInfo.hStdInput = in;
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    PROCESS_INFORMATION pi{};
    const std::wstring dir = o.workingDir.empty() ? std::wstring() : o.workingDir.wstring();
    const BOOL made = CreateProcessW(nullptr, line.data(), nullptr, nullptr, redirect ? TRUE : FALSE, flags, envBlock.data(),
                                     dir.empty() ? nullptr : dir.c_str(), &si.StartupInfo, &pi);
    if (redirect) DeleteProcThreadAttributeList(si.lpAttributeList);
    if (!made) {
        const std::string first = o.shellCommand.empty() ? o.args.front() : std::string("cmd.exe");
        return std::unexpected(std::format("could not start {}: {}", first, lastErrorText()));
    }
    Process p;
    p.impl_ = std::make_unique<Impl>();
    p.impl_->process = pi.hProcess;
    p.impl_->pid = pi.dwProcessId;
    // A job ends the child's own children with it. Windows 7 allows no job
    // within a job: a parent already in one keeps only the child itself.
    if (HANDLE job = CreateJobObjectW(nullptr, nullptr)) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) && AssignProcessToJobObject(job, pi.hProcess))
            p.impl_->job = job;
        else
            CloseHandle(job);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    return p;
}

std::optional<int> Process::poll() {
    if (!impl_) return std::nullopt;
    if (impl_->exit) return impl_->exit;
    if (WaitForSingleObject(impl_->process, 0) != WAIT_OBJECT_0) return std::nullopt;
    DWORD code = 0;
    GetExitCodeProcess(impl_->process, &code);
    impl_->exit = static_cast<int>(code);
    return impl_->exit;
}

std::optional<int> Process::wait(std::optional<std::chrono::milliseconds> timeout) {
    if (!impl_) return std::nullopt;
    if (impl_->exit) return impl_->exit;
    const DWORD ms = timeout ? static_cast<DWORD>(std::clamp<int64_t>(timeout->count(), 0, 0x7fffffff)) : INFINITE;
    if (WaitForSingleObject(impl_->process, ms) != WAIT_OBJECT_0) return std::nullopt;
    return poll();
}

void Process::kill() {
    if (!impl_) return;
    if (impl_->job) TerminateJobObject(impl_->job, 1);
    else if (!poll()) TerminateProcess(impl_->process, 1);
    wait(std::chrono::milliseconds(5000));
}

int64_t Process::id() const { return impl_ ? static_cast<int64_t>(impl_->pid) : -1; }

std::filesystem::path executableDir() {
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(n);
    return std::filesystem::path(buffer).parent_path();
}

#else // POSIX

namespace {

// The program to run: as given when it names a folder, else found on PATH.
std::string findProgram(const std::string& name, const std::vector<std::pair<std::string, std::string>>& env) {
    if (name.find('/') != std::string::npos) return name;
    std::string path;
    if (const char* p = std::getenv("PATH")) path = p;
    for (const auto& [k, v] : env)
        if (k == "PATH") path = v;
    if (path.empty()) path = "/usr/local/bin:/usr/bin:/bin";
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find(':', start), path.size());
        const std::string dir = end > start ? path.substr(start, end - start) : std::string(".");
        const std::string candidate = dir + "/" + name;
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
        start = end + 1;
    }
    return name;
}

} // namespace

struct Process::Impl {
    pid_t pid = -1;
    std::optional<int> exit;
};

std::expected<Process, std::string> Process::start(const ProcessOptions& o) {
    std::vector<std::string> args;
    if (!o.shellCommand.empty()) args = {"/bin/sh", "-c", o.shellCommand};
    else args = o.args;
    if (args.empty()) return std::unexpected(std::string("no program to start"));
    const std::string program = findProgram(args.front(), o.environment);

    // Everything the child needs is made before fork: after it, only calls
    // that are safe in a child of a threaded program.
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    std::vector<std::string> envStrings;
    for (char** e = environ; e && *e; ++e) {
        const std::string_view entry(*e);
        const std::string_view name = entry.substr(0, entry.find('='));
        const bool replaced = std::any_of(o.environment.begin(), o.environment.end(), [&](const auto& kv) { return kv.first == name; });
        if (!replaced) envStrings.emplace_back(entry);
    }
    for (const auto& [name, value] : o.environment) envStrings.push_back(name + "=" + value);
    std::vector<char*> envp;
    for (std::string& e : envStrings) envp.push_back(e.data());
    envp.push_back(nullptr);
    const std::string dir = o.workingDir.empty() ? std::string() : o.workingDir.string();

    int out = -1;
    if (!o.output.empty()) {
        out = ::open(o.output.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (out < 0) return std::unexpected(std::format("{}: {}", o.output.string(), std::strerror(errno)));
    }
    int status[2] = {-1, -1};   // the child reports a failed exec through it
    if (::pipe(status) != 0) {
        if (out >= 0) ::close(out);
        return std::unexpected(std::format("pipe: {}", std::strerror(errno)));
    }
    ::fcntl(status[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(status[1], F_SETFD, FD_CLOEXEC);

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int e = errno;
        if (out >= 0) ::close(out);
        ::close(status[0]);
        ::close(status[1]);
        return std::unexpected(std::format("fork: {}", std::strerror(e)));
    }
    if (pid == 0) {
        ::setpgid(0, 0);
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        ::signal(SIGPIPE, SIG_DFL);
        int err = 0;
        if (!dir.empty() && ::chdir(dir.c_str()) != 0) err = errno;
        if (!err && out >= 0) {
            const int devnull = ::open("/dev/null", O_RDONLY);
            if (devnull >= 0) ::dup2(devnull, 0);
            if (::dup2(out, 1) < 0 || ::dup2(out, 2) < 0) err = errno;
        }
        if (!err) {
            ::execve(program.c_str(), argv.data(), envp.data());
            err = errno;
        }
        [[maybe_unused]] const ssize_t w = ::write(status[1], &err, sizeof err);
        ::_exit(127);
    }
    ::setpgid(pid, pid);   // also here, so that kill() reaches the group whichever runs first
    ::close(status[1]);
    if (out >= 0) ::close(out);
    int childError = 0;
    ssize_t got = 0;
    do {
        got = ::read(status[0], &childError, sizeof childError);
    } while (got < 0 && errno == EINTR);
    ::close(status[0]);
    Process p;
    p.impl_ = std::make_unique<Impl>();
    p.impl_->pid = pid;
    if (got == static_cast<ssize_t>(sizeof childError)) {
        p.wait();
        return std::unexpected(std::format("could not start {}: {}", args.front(), std::strerror(childError)));
    }
    return p;
}

std::optional<int> Process::poll() {
    if (!impl_ || impl_->pid <= 0) return std::nullopt;
    if (impl_->exit) return impl_->exit;
    int st = 0;
    const pid_t r = ::waitpid(impl_->pid, &st, WNOHANG);
    if (r == 0) return std::nullopt;
    if (r < 0) impl_->exit = -1;
    else if (WIFEXITED(st)) impl_->exit = WEXITSTATUS(st);
    else if (WIFSIGNALED(st)) impl_->exit = 128 + WTERMSIG(st);
    else return std::nullopt;
    return impl_->exit;
}

std::optional<int> Process::wait(std::optional<std::chrono::milliseconds> timeout) {
    if (!impl_ || impl_->pid <= 0) return std::nullopt;
    if (impl_->exit) return impl_->exit;
    if (!timeout) {
        int st = 0;
        pid_t r = 0;
        do {
            r = ::waitpid(impl_->pid, &st, 0);
        } while (r < 0 && errno == EINTR);
        if (r < 0) impl_->exit = -1;
        else if (WIFEXITED(st)) impl_->exit = WEXITSTATUS(st);
        else impl_->exit = WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1;
        return impl_->exit;
    }
    const auto until = std::chrono::steady_clock::now() + *timeout;
    for (;;) {
        if (auto code = poll()) return code;
        if (std::chrono::steady_clock::now() >= until) return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void Process::kill() {
    if (!impl_ || impl_->pid <= 0) return;
    // The whole group: a shell's command, a bot's own helpers.
    ::kill(-impl_->pid, SIGTERM);
    if (!wait(std::chrono::milliseconds(1000))) {
        ::kill(-impl_->pid, SIGKILL);
        wait();
    }
    ::kill(-impl_->pid, SIGKILL);   // what the group still holds after its leader ended
}

int64_t Process::id() const { return impl_ ? static_cast<int64_t>(impl_->pid) : -1; }

std::filesystem::path executableDir() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code ec;
    auto p = std::filesystem::canonical(buffer.c_str(), ec);
    return ec ? std::filesystem::path(buffer.c_str()).parent_path() : p.parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : p.parent_path();
#endif
}

#endif

Process::Process() = default;
Process::Process(Process&& other) noexcept = default;
Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        if (impl_ && !poll()) kill();
        impl_ = std::move(other.impl_);
    }
    return *this;
}
Process::~Process() {
    try {
        if (impl_ && !poll()) kill();
    } catch (...) {
    }
}
bool Process::valid() const { return impl_ != nullptr; }

} // namespace opense4::sdk
