#include "engine/platform/os.h"

#include <cerrno>
#include <cstdlib>
#include <thread>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace pt::os {

#ifdef _WIN32

FILE* OpenFile(const std::filesystem::path& path, const char* mode) {
    std::wstring wide;
    for (const char* c = mode; *c; ++c) wide.push_back(static_cast<wchar_t>(*c));
    return _wfopen(path.c_str(), wide.c_str());
}

int SeekFile(FILE* file, int64_t offset, int origin) { return _fseeki64(file, offset, origin); }

bool SameFileName(const std::filesystem::path& a, const std::filesystem::path& b) {
    const std::wstring& x = a.native();
    const std::wstring& y = b.native();
    return CompareStringOrdinal(x.c_str(), static_cast<int>(x.size()), y.c_str(), static_cast<int>(y.size()), TRUE) == CSTR_EQUAL;
}

bool IsLinkReparseTag(uint32_t tag) { return IsReparseTagNameSurrogate(tag) != 0; }

bool IsLink(const std::filesystem::path& path) {
    WIN32_FIND_DATAW data{};
    std::filesystem::path query = path;
    // FindFirstFileW names the entry itself only without a trailing separator
    while (query.has_relative_path() && !query.has_filename()) query = query.parent_path();
    const HANDLE find = FindFirstFileExW(query.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) {
        // a drive root or an entry that cannot be listed: fall back to the attributes (no tag known, a reparse point counts)
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && query.has_relative_path();
    }
    FindClose(find);
    // dwReserved0 holds the reparse tag when FILE_ATTRIBUTE_REPARSE_POINT is set (WIN32_FIND_DATAW)
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsLinkReparseTag(data.dwReserved0);
}

std::filesystem::path ExecutableFile() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
        if (buffer.size() >= 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
}

std::string GetEnv(const char* name) {
    std::wstring key;
    for (const char* c = name; *c; ++c) key.push_back(static_cast<wchar_t>(*c));
    const DWORD size = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(key.c_str(), value.data(), size);
    if (!written || written >= size) return {};
    value.resize(written);
    return PathToUtf8(std::filesystem::path(value));
}

uint32_t ProcessId() { return GetCurrentProcessId(); }

namespace {
std::string WideToUtf8(const wchar_t* text, int length) {
    if (length <= 0) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size : 0, '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), size, nullptr, nullptr);
    return out;
}
}  // namespace

ForegroundWindow GetForegroundWindowInfo(bool details) {
    ForegroundWindow info;
    HWND window = GetForegroundWindow();
    if (!window) return info;
    info.handle = reinterpret_cast<uint64_t>(window);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    info.pid = pid;
    if (!details) return info;
    wchar_t text[256]{};
    info.title = WideToUtf8(text, GetWindowTextW(window, text, 256));
    info.window_class = WideToUtf8(text, GetClassNameW(window, text, 256));
    if (HANDLE process = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr) {
        std::wstring path(1024, L'\0');
        DWORD size = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
            path.resize(size);
            const size_t slash = path.find_last_of(L"\\/");
            const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
            info.process = WideToUtf8(name.c_str(), static_cast<int>(name.size()));
        }
        CloseHandle(process);
    }
    return info;
}

bool IsForegroundWindow(void* window) { return window && GetForegroundWindow() == static_cast<HWND>(window); }

bool ForegroundIsOwnProcess() {
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    if (window) GetWindowThreadProcessId(window, &pid);
    return pid != 0 && pid == GetCurrentProcessId();
}

bool ActivateWindow(void* handle) {
    HWND window = static_cast<HWND>(handle);
    if (!window) return false;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    if (GetForegroundWindow() == window) return true;
    if (!SetForegroundWindow(window)) {
        // Windows lets only the foreground thread's process (or one it allowed) take the foreground: join that thread's input
        // queue for the call
        HWND front = GetForegroundWindow();
        const DWORD front_thread = front ? GetWindowThreadProcessId(front, nullptr) : 0;
        const DWORD this_thread = GetCurrentThreadId();
        const bool attached = front_thread && front_thread != this_thread && AttachThreadInput(this_thread, front_thread, TRUE);
        BringWindowToTop(window);
        SetForegroundWindow(window);
        if (attached) AttachThreadInput(this_thread, front_thread, FALSE);
    }
    return GetForegroundWindow() == window;
}

void ClearWindow(void* handle) {
    HWND window = static_cast<HWND>(handle);
    RECT client{};
    if (!window || !GetClientRect(window, &client)) return;
    if (HDC dc = GetDC(window)) {
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        ReleaseDC(window, dc);
    }
}

void AllowForeground(uint32_t pid) { AllowSetForegroundWindow(pid ? pid : ASFW_ANY); }
void LowerCurrentThreadPriority() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); }

std::filesystem::path ExtendedLengthPath(const std::filesystem::path& path) {
    const std::wstring& native = path.native();
    if (native.size() < MAX_PATH || native.starts_with(L"\\\\?\\")) return path;
    // the prefix turns off the path parsing: absolute, normalized, backslashes only
    std::error_code ec;
    std::filesystem::path full = std::filesystem::absolute(path, ec);
    if (ec) return path;
    full = full.lexically_normal();
    full.make_preferred();
    const std::wstring& text = full.native();
    if (text.starts_with(L"\\\\")) return std::filesystem::path(L"\\\\?\\UNC\\" + text.substr(2));
    return std::filesystem::path(L"\\\\?\\" + text);
}

namespace {
// CreateProcessW's working folder: it must be shorter than MAX_PATH (even prefixed, ERROR_DIRECTORY); a longer one is left out
// and the child starts in this process's folder. Every program started here is given absolute paths.
const wchar_t* ChildWorkingDir(const std::filesystem::path& dir) {
    return dir.empty() || dir.native().size() >= MAX_PATH - 2 ? nullptr : dir.c_str();
}
}

ProcessResult RunProcess(const std::filesystem::path& program, const std::vector<std::string>& args, const std::filesystem::path& working_dir,
                         const std::filesystem::path& log, const std::atomic<bool>& cancel, std::chrono::milliseconds timeout) {
    ProcessResult result;
    // no shell or command interpreter is involved; each argument is quoted so that the child's CommandLineToArgvW (or its C
    // runtime) gives it back unchanged, a folder ending in a backslash or an argument with a quote included
    std::wstring command = QuoteWindowsArgument(program.wstring());
    for (const auto& arg : args) command += L" " + QuoteWindowsArgument(PathFromUtf8(arg).wstring());
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE log_file = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_file == INVALID_HANDLE_VALUE) return result;
    HANDLE input_handle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = log_file;
    startup.hStdError = log_file;
    startup.hStdInput = input_handle;
    PROCESS_INFORMATION process{};
    // the program at any length (ExtendedLengthPath) and its folder when CreateProcessW takes it (ChildWorkingDir)
    result.started = CreateProcessW(ExtendedLengthPath(program).c_str(), command.data(), nullptr, nullptr, TRUE,
                                    CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, ChildWorkingDir(working_dir), &startup, &process);
    CloseHandle(log_file);
    if (input_handle != INVALID_HANDLE_VALUE) CloseHandle(input_handle);
    if (!result.started) return result;
    CloseHandle(process.hThread);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (WaitForSingleObject(process.hProcess, 100) == WAIT_TIMEOUT) {
        if (cancel || std::chrono::steady_clock::now() > deadline) {
            result.cancelled = cancel;
            result.timed_out = !cancel;
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, INFINITE);
            break;
        }
    }
    DWORD exit = 1;
    GetExitCodeProcess(process.hProcess, &exit);
    CloseHandle(process.hProcess);
    result.exit_code = static_cast<int>(exit);
    return result;
}

std::wstring QuoteWindowsArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

bool StartDetached(const std::filesystem::path& program, const std::vector<std::string>& args, const std::filesystem::path& working_dir) {
    std::wstring command = QuoteWindowsArgument(program.wstring());
    for (const auto& arg : args) command += L" " + QuoteWindowsArgument(PathFromUtf8(arg).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(ExtendedLengthPath(program).c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr,
                        ChildWorkingDir(working_dir), &startup, &process))
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

ChildProcess::~ChildProcess() {
    if (handle_ != kNone) CloseHandle(reinterpret_cast<HANDLE>(handle_));
}

bool ChildProcess::Start(const std::filesystem::path& program, const std::vector<std::string>& args, const std::filesystem::path& working_dir) {
    if (handle_ != kNone) return false;
    std::wstring command = QuoteWindowsArgument(program.wstring());
    for (const auto& arg : args) command += L" " + QuoteWindowsArgument(PathFromUtf8(arg).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(ExtendedLengthPath(program).c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        ChildWorkingDir(working_dir), &startup, &process))
        return false;
    CloseHandle(process.hThread);
    handle_ = reinterpret_cast<intptr_t>(process.hProcess);
    return true;
}

bool ChildProcess::Running(int* exit_code) {
    if (handle_ == kNone) return false;
    const HANDLE process = reinterpret_cast<HANDLE>(handle_);
    if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return true;
    DWORD code = 1;
    GetExitCodeProcess(process, &code);
    if (exit_code) *exit_code = static_cast<int>(code);
    CloseHandle(process);
    handle_ = kNone;
    return false;
}

bool ProcessAlive(uint64_t pid) {
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

FileLock::FileLock(const std::filesystem::path& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    held_ = file != INVALID_HANDLE_VALUE;
    handle_ = reinterpret_cast<intptr_t>(file);
}

FileLock::~FileLock() {
    if (held_) CloseHandle(reinterpret_cast<HANDLE>(handle_));
}

#else

FILE* OpenFile(const std::filesystem::path& path, const char* mode) { return std::fopen(path.c_str(), mode); }

int SeekFile(FILE* file, int64_t offset, int origin) { return fseeko(file, static_cast<off_t>(offset), origin); }

bool SameFileName(const std::filesystem::path& a, const std::filesystem::path& b) { return a.native() == b.native(); }

bool IsLinkReparseTag(uint32_t) { return false; }

bool IsLink(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec));
}

std::filesystem::path ExecutableFile() {
#ifdef __linux__
    std::error_code ec;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : self;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (!size) return {};
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code ec;
    const auto self = std::filesystem::weakly_canonical(std::filesystem::path(buffer.data()), ec);
    return ec ? std::filesystem::path() : self;
#else
    return {};
#endif
}

std::string GetEnv(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

uint32_t ProcessId() { return static_cast<uint32_t>(getpid()); }

ForegroundWindow GetForegroundWindowInfo(bool) { return {}; }
bool IsForegroundWindow(void*) { return false; }
bool ForegroundIsOwnProcess() { return false; }
bool ActivateWindow(void*) { return false; }
void ClearWindow(void*) {}
void AllowForeground(uint32_t) {}
void LowerCurrentThreadPriority() {
#ifdef __linux__
    // who = 0 is the calling thread on Linux
    setpriority(PRIO_PROCESS, 0, 5);
#endif
}

ProcessResult RunProcess(const std::filesystem::path& program, const std::vector<std::string>& args, const std::filesystem::path& working_dir,
                         const std::filesystem::path& log, const std::atomic<bool>& cancel, std::chrono::milliseconds timeout) {
#ifdef __ANDROID__
    // APK libraries run in this process. Desktop executables/upscaling helpers are not supported on Android.
    return {};
#else
    ProcessResult result;
    const int log_fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (log_fd < 0) return result;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, log_fd, 1);
    posix_spawn_file_actions_adddup2(&actions, log_fd, 2);
    posix_spawn_file_actions_addchdir_np(&actions, working_dir.c_str());
    std::vector<std::string> owned{program.string()};
    owned.insert(owned.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& arg : owned) argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    result.started = posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ) == 0;
    posix_spawn_file_actions_destroy(&actions);
    close(log_fd);
    if (!result.started) return result;
    setpriority(PRIO_PROCESS, static_cast<id_t>(pid), 10);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    for (;;) {
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0) return result;
        if (cancel || std::chrono::steady_clock::now() > deadline) {
            result.cancelled = cancel;
            result.timed_out = !cancel;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return result;
#endif
}

std::wstring QuoteWindowsArgument(const std::wstring& arg) { return arg; }

bool StartDetached(const std::filesystem::path& program, const std::vector<std::string>& args, const std::filesystem::path& working_dir) {
#ifdef __ANDROID__
    return false; // Android activities own process startup.
#else
    std::vector<std::string> owned{program.string()};
    owned.insert(owned.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& arg : owned) argv.push_back(arg.data());
    argv.push_back(nullptr);
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // the child: its own session (it outlives the game), then the program; a second fork is not needed, init adopts it
        setsid();
        if (!working_dir.empty() && chdir(working_dir.c_str()) != 0) _exit(127);
        execv(program.c_str(), argv.data());
        _exit(127);
    }
    return true;
#endif
}

bool ProcessAlive(uint64_t pid) { return pid > 0 && (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM); }

std::filesystem::path ExtendedLengthPath(const std::filesystem::path& path) { return path; }

ChildProcess::~ChildProcess() = default;

bool ChildProcess::Start(const std::filesystem::path& program, const std::vector<std::string>& args, const std::filesystem::path& working_dir) {
#ifdef __ANDROID__
    return false; // Desktop preview executables are not APK entry points.
#else
    if (handle_ != kNone) return false;
    std::vector<std::string> owned{program.string()};
    owned.insert(owned.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& arg : owned) argv.push_back(arg.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    if (!working_dir.empty()) posix_spawn_file_actions_addchdir_np(&actions, working_dir.c_str());
    pid_t pid = -1;
    const bool started = posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ) == 0;
    posix_spawn_file_actions_destroy(&actions);
    if (!started) return false;
    handle_ = pid;
    return true;
#endif
}

bool ChildProcess::Running(int* exit_code) {
    if (handle_ == kNone) return false;
    int status = 0;
    const pid_t done = waitpid(static_cast<pid_t>(handle_), &status, WNOHANG);
    if (done == 0) return true;
    if (exit_code) *exit_code = done == static_cast<pid_t>(handle_) && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    handle_ = kNone;
    return false;
}

FileLock::FileLock(const std::filesystem::path& path) {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return;
    }
    held_ = true;
    handle_ = fd;
}

FileLock::~FileLock() {
    if (held_) close(static_cast<int>(handle_));
}

#endif

#ifdef _WIN32
std::optional<DisplayHdrInfo> WindowDisplayHdrInfo(void* hwnd) {
    if (!hwnd) return std::nullopt;
    // DXGI by hand (no import library): CreateDXGIFactory1, the adapters' outputs, IDXGIOutput6::GetDesc1's colour space for
    // the output whose monitor holds the window
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) return std::nullopt;
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    const auto create = reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
    if (!create) return std::nullopt;
    const HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTONEAREST);
    static const GUID kFactory1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
    static const GUID kOutput6 = {0x068346e8, 0xaaec, 0x4b84, {0xad, 0xd7, 0x13, 0x7f, 0x51, 0x3f, 0x77, 0xa1}};
    struct Unknown {
        virtual HRESULT __stdcall QueryInterface(REFIID, void**) = 0;
        virtual ULONG __stdcall AddRef() = 0;
        virtual ULONG __stdcall Release() = 0;
    };
    // the vtable slots used: IDXGIFactory1::EnumAdapters1 (12), IDXGIAdapter::EnumOutputs (7), IDXGIOutput6::GetDesc1 (27)
    struct OutputDesc1 {
        WCHAR name[32];
        RECT coordinates;
        BOOL attached;
        UINT rotation;
        HMONITOR monitor;
        UINT bits;
        UINT color_space;
        FLOAT red[2], green[2], blue[2], white[2];
        FLOAT min_luminance, max_luminance, max_full_frame;
    };
    using Slot = void**;
    auto call = [](void* object, int slot) { return (*reinterpret_cast<Slot*>(object))[slot]; };
    void* factory = nullptr;
    if (FAILED(create(kFactory1, &factory)) || !factory) return std::nullopt;
    std::optional<DisplayHdrInfo> result;
    for (UINT a = 0; !result; ++a) {
        void* adapter = nullptr;
        using EnumAdapters = HRESULT(__stdcall*)(void*, UINT, void**);
        if (FAILED(reinterpret_cast<EnumAdapters>(call(factory, 12))(factory, a, &adapter)) || !adapter) break;
        for (UINT o = 0; !result; ++o) {
            void* output = nullptr;
            using EnumOutputs = HRESULT(__stdcall*)(void*, UINT, void**);
            if (FAILED(reinterpret_cast<EnumOutputs>(call(adapter, 7))(adapter, o, &output)) || !output) break;
            void* output6 = nullptr;
            if (SUCCEEDED(static_cast<Unknown*>(output)->QueryInterface(kOutput6, &output6)) && output6) {
                OutputDesc1 desc{};
                using GetDesc1 = HRESULT(__stdcall*)(void*, OutputDesc1*);
                if (SUCCEEDED(reinterpret_cast<GetDesc1>(call(output6, 27))(output6, &desc)) && desc.monitor == monitor) {
                    // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 (12): the display runs in HDR
                    DisplayHdrInfo info;
                    info.hdr = desc.color_space == 12;
                    // the luminance range the display reports (its EDID or the HDR calibration); a nonsense value is no value
                    if (desc.max_luminance >= 80.0f && desc.max_luminance <= 10000.0f) info.max_luminance = desc.max_luminance;
                    if (desc.min_luminance >= 0.0f && desc.min_luminance < 10.0f) info.min_luminance = desc.min_luminance;
                    result = info;
                }
                static_cast<Unknown*>(output6)->Release();
            }
            static_cast<Unknown*>(output)->Release();
        }
        static_cast<Unknown*>(adapter)->Release();
    }
    static_cast<Unknown*>(factory)->Release();
    return result;
}
#else
std::optional<DisplayHdrInfo> WindowDisplayHdrInfo(void*) {
    return std::nullopt;
}
#endif

std::optional<bool> WindowDisplayHdr(void* hwnd) {
    const std::optional<DisplayHdrInfo> info = WindowDisplayHdrInfo(hwnd);
    return info ? std::optional<bool>(info->hdr) : std::nullopt;
}

}
