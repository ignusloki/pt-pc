#include "engine/platform/start_watch.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "engine/core/crash_report.h"
#include "engine/core/log.h"
#include "engine/platform/os.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#else
#include <unistd.h>
#endif

namespace pt::start_watch {
namespace {

using Clock = std::chrono::steady_clock;

std::mutex g_mutex;
std::condition_variable g_wake;
Config g_config;
bool g_armed = false;
bool g_finished = false;
std::string g_step;
Clock::time_point g_step_start;
bool g_step_reported = false;
std::atomic<bool> g_in_step{false};
char g_exit_step[64] = "";

unsigned long ProcessId() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

std::string LocalTimeText() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &tm);
    return text;
}

void WriteMarker(const std::string& step) {
    if (g_config.marker.empty()) return;
    std::ofstream out(g_config.marker, std::ios::trunc);
    out << "api=" << g_config.api << "\nstep=" << step << "\npid=" << ProcessId() << "\ntime=" << LocalTimeText()
        << "\nlayers_off=" << (g_config.layers_off ? 1 : 0)
        << "\n; pt.exe is starting its graphics. If this file stays, the start ended in this step; the next start says so.\n";
}

void RemoveMarker() {
    if (g_config.marker.empty()) return;
    std::error_code ec;
    std::filesystem::remove(g_config.marker, ec);
}

int WatchdogSeconds() {
    if (const char* e = std::getenv("PT_START_WATCHDOG_SECONDS"); e && *e) {
        const int v = std::atoi(e);
        if (v > 0) return v;
    }
    return 30;
}

#ifdef _WIN32
std::wstring Wide(const std::string& text) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), n);
    return out;
}
std::string Narrow(const wchar_t* text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    return out;
}
#endif

void ReportHang(const std::string& step, int seconds) {
    LogWarn("start: '{}' has not finished after {} s (the watchdog; PT_START_WATCHDOG_SECONDS sets the limit)", step, seconds);
    const std::string reason = "the start has waited " + std::to_string(seconds) + " s in '" + step + "'";
    WriteHangDump(reason.c_str());
#ifdef _WIN32
    if (!g_config.hang_title || !g_config.hang_text) return;
    const std::wstring title = Wide(g_config.hang_title());
    const std::wstring text = Wide(g_config.hang_text(seconds, step));
    LogInfo("start: hang message shown");
    const int answer = MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_OKCANCEL | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
    std::lock_guard lock(g_mutex);
    if (g_finished) {
        LogInfo("start: hang message closed; the step had finished meanwhile");
        return;
    }
    if (answer == IDCANCEL) {
        LogExit(1, "closed by the player while the start hung");
        TerminateProcess(GetCurrentProcess(), 1);
    }
    LogInfo("start: the player keeps waiting");
#endif
}

void Watchdog() {
    const int limit = WatchdogSeconds();
    std::unique_lock lock(g_mutex);
    while (!g_finished) {
        g_wake.wait_for(lock, std::chrono::milliseconds(500));
        if (g_finished || g_step.empty() || g_step_reported) continue;
        const auto waited = std::chrono::duration<double>(Clock::now() - g_step_start).count();
        if (waited < limit) continue;
        g_step_reported = true;
        const std::string step = g_step;
        lock.unlock();
        ReportHang(step, static_cast<int>(waited));
        lock.lock();
    }
}

void OnExit() {
    std::lock_guard lock(g_mutex);
    if (g_armed && !g_finished) {
        g_finished = true;
        g_in_step = false;
        RemoveMarker();
    }
}

#ifdef _WIN32
[[noreturn]] void Overflow(volatile int depth) {
    volatile char pad[4096];
    pad[0] = static_cast<char>(depth);
    Overflow(depth + pad[0] + 1);
}

void NTAPI ProcessDetach(PVOID, DWORD reason, PVOID) {
    if (reason != DLL_PROCESS_DETACH || !g_in_step.load() || ExitLogged()) return;
    std::string line = "exit: the process ended inside start step '";
    line += g_exit_step;
    line += "' without the game's own exit (ExitProcess from a library or the driver); no dump is possible";
    LogTryWrite(LogLevel::Error, line);
}
#endif

}

#ifdef _WIN32
extern "C" {
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:pt_start_watch_tls")
#pragma section(".CRT$XLP", long, read)
__declspec(allocate(".CRT$XLP")) extern const PIMAGE_TLS_CALLBACK pt_start_watch_tls = ProcessDetach;
}
#endif

Previous TakePrevious(const std::filesystem::path& marker) {
    Previous p;
    std::error_code ec;
    if (marker.empty() || !std::filesystem::exists(marker, ec)) return p;
    p.found = true;
    std::ifstream in(marker);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == ';' || eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "api") p.api = value;
        else if (key == "step") p.step = value;
        else if (key == "time") p.time = value;
        else if (key == "pid") p.pid = std::strtoul(value.c_str(), nullptr, 10);
        else if (key == "layers_off") p.layers_off = value == "1";
    }
    in.close();
    std::filesystem::remove(marker, ec);
    if (p.step.empty()) p.step = "?";
    return p;
}

bool LoaderStep(std::string_view step) {
    return step == "window" || step == "instance" || step == "devices" || step == "device";
}

void Arm(const Config& config) {
    {
        std::lock_guard lock(g_mutex);
        if (g_armed) return;
        g_config = config;
        g_armed = true;
    }
    std::atexit(OnExit);
    std::thread(Watchdog).detach();
}

bool Step(std::string_view step, std::string_view detail) {
    {
        std::lock_guard lock(g_mutex);
        if (g_armed && !g_finished) {
            g_step = std::string(step);
            g_step_start = Clock::now();
            g_step_reported = false;
            WriteMarker(g_step);
            const size_t n = std::min(step.size(), sizeof(g_exit_step) - 1);
            std::copy_n(step.data(), n, g_exit_step);
            g_exit_step[n] = 0;
            g_in_step = true;
        }
    }
    if (detail.empty()) LogInfo("start: {}", step);
    else LogInfo("start: {} ({})", step, detail);
    return TestFault(step);
}

void Done(std::string_view note) {
    std::string step;
    double took = 0.0;
    {
        std::lock_guard lock(g_mutex);
        step = g_step;
        if (!g_step.empty()) took = std::chrono::duration<double>(Clock::now() - g_step_start).count();
        g_step.clear();
    }
    if (step.empty()) return;
    if (note.empty()) LogInfo("start: {} done in {:.3f} s", step, took);
    else LogInfo("start: {} done in {:.3f} s ({})", step, took, note);
    CheckCrashFilter(step.c_str());
}

void Finish() {
    {
        std::lock_guard lock(g_mutex);
        if (!g_armed || g_finished) return;
        g_finished = true;
        g_in_step = false;
        g_step.clear();
        RemoveMarker();
    }
    g_wake.notify_all();
    LogInfo("start: the graphics start is over (start marker removed, watchdog off)");
}

bool TestFault(std::string_view step) {
    static const std::string spec = [] {
        const char* e = std::getenv("PT_TEST_START_FAULT");
        return std::string(e ? e : "");
    }();
    const size_t colon = spec.find(':');
    if (colon == std::string::npos || std::string_view(spec).substr(0, colon) != step) return true;
    const std::string kind = spec.substr(colon + 1);
    LogInfo("start test: {} in '{}' (PT_TEST_START_FAULT)", kind, step);
#ifdef _WIN32
    if (kind == "crash") {
        *static_cast<volatile int*>(nullptr) = 1;
    } else if (kind == "hang") {
        Sleep(INFINITE);
    } else if (kind == "exit") {
        ExitProcess(7);
    } else if (kind == "failfast") {
        __fastfail(7);
    } else if (kind == "stack") {
        Overflow(0);
    }
#endif
    return kind != "fail";
}

std::vector<std::string> RecentErrorReports(double hours, int limit) {
    std::vector<std::string> out;
#ifdef _WIN32
    HMODULE wevt = LoadLibraryExW(L"wevtapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wevt) return out;
    using Query = HANDLE(WINAPI*)(HANDLE, LPCWSTR, LPCWSTR, DWORD);
    using Next = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE*, DWORD, DWORD, DWORD*);
    using Render = BOOL(WINAPI*)(HANDLE, HANDLE, DWORD, DWORD, PVOID, DWORD*, DWORD*);
    using Close = BOOL(WINAPI*)(HANDLE);
    const auto query = reinterpret_cast<Query>(reinterpret_cast<void*>(GetProcAddress(wevt, "EvtQuery")));
    const auto next = reinterpret_cast<Next>(reinterpret_cast<void*>(GetProcAddress(wevt, "EvtNext")));
    const auto render = reinterpret_cast<Render>(reinterpret_cast<void*>(GetProcAddress(wevt, "EvtRender")));
    const auto close = reinterpret_cast<Close>(reinterpret_cast<void*>(GetProcAddress(wevt, "EvtClose")));
    if (!query || !next || !render || !close) return out;
    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::string exe_name = Narrow(std::filesystem::path(exe).filename().c_str());
    const std::wstring xpath = L"*[System[(EventID=1000 or EventID=1001 or EventID=1002) and TimeCreated[timediff(@SystemTime) <= " +
                               std::to_wstring(static_cast<long long>(hours * 3600000.0)) + L"]]]";
    HANDLE results = query(nullptr, L"Application", xpath.c_str(), 0x1 | 0x200);
    if (!results) return out;
    HANDLE events[16];
    DWORD returned = 0;
    std::vector<wchar_t> buffer(16384);
    int scanned = 0;
    while (static_cast<int>(out.size()) < limit && scanned < 200 && next(results, 16, events, 2000, 0, &returned)) {
        for (DWORD i = 0; i < returned; ++i) {
            ++scanned;
            DWORD used = 0, count = 0;
            if (!render(nullptr, events[i], 1, static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), buffer.data(), &used, &count)) {
                close(events[i]);
                continue;
            }
            close(events[i]);
            const std::string xml = Narrow(buffer.data());
            if (static_cast<int>(out.size()) >= limit) continue;
            std::string lower_xml = xml, lower_exe = exe_name;
            for (char& c : lower_xml) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : lower_exe) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower_exe.empty() || lower_xml.find(">" + lower_exe + "<") == std::string::npos) continue;
            const auto attr = [&](const char* tag, const char* name) {
                const size_t at = xml.find(std::string("<") + tag);
                if (at == std::string::npos) return std::string();
                const size_t a = xml.find(std::string(name) + "='", at);
                if (a == std::string::npos) return std::string();
                const size_t b = xml.find('\'', a + std::strlen(name) + 2);
                return xml.substr(a + std::strlen(name) + 2, b - a - std::strlen(name) - 2);
            };
            std::string id;
            if (const size_t a = xml.find("<EventID"); a != std::string::npos) {
                const size_t b = xml.find('>', a), c = xml.find('<', b);
                id = xml.substr(b + 1, c - b - 1);
            }
            std::string line = "event " + id + " at " + attr("TimeCreated", "SystemTime") + ":";
            size_t pos = 0;
            int fields = 0;
            while ((pos = xml.find("<Data", pos)) != std::string::npos && fields < 16) {
                const size_t close_tag = xml.find('>', pos);
                if (close_tag == std::string::npos) break;
                if (xml[close_tag - 1] == '/') {
                    pos = close_tag;
                    continue;
                }
                const size_t end = xml.find("</Data>", close_tag);
                if (end == std::string::npos) break;
                std::string value = xml.substr(close_tag + 1, end - close_tag - 1);
                std::string name;
                if (const size_t n = xml.find("Name='", pos); n != std::string::npos && n < close_tag) {
                    name = xml.substr(n + 6, xml.find('\'', n + 6) - n - 6);
                }
                if (!value.empty()) {
                    line += " " + (name.empty() ? std::string() : name + "=") + value + ";";
                    ++fields;
                }
                pos = end;
            }
            out.push_back(line);
        }
    }
    close(results);
#else
    (void)hours;
    (void)limit;
#endif
    return out;
}

}
