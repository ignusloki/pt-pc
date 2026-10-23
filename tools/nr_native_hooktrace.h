#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <tuple>
#include <vector>

namespace nr_trace {
enum Hook { kModule, kFunction, kLaunch, kLaunchEx, kArch, kCount };
inline const char* const kNames[kCount] = {"create_module", "create_function", "launch_chain", "launch_chain_ex", "arch_info"};
inline bool on = [] { const char* v = std::getenv("PT_NR_HOOK_TRACE"); return v && *v == '1'; }();
inline DWORD render_tid;
struct Counter { volatile LONG calls, nr_calls, other_thread; };
inline Counter counters[kCount];
inline std::mutex seen_lock;
inline std::vector<std::tuple<int, HMODULE, DWORD, bool>> seen;
inline LARGE_INTEGER start{}, freq{};

inline double Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); start = t; }
    return double(t.QuadPart - start.QuadPart) / double(freq.QuadPart);
}

inline void ModuleName(const void* address, HMODULE* module, char* name, unsigned capacity, unsigned* offset) {
    *module = nullptr;
    std::snprintf(name, capacity, "?");
    *offset = 0;
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      static_cast<LPCWSTR>(address), module) && *module) {
        char path[MAX_PATH]{};
        GetModuleFileNameA(*module, path, MAX_PATH);
        const char* base = std::strrchr(path, '\\');
        std::snprintf(name, capacity, "%s", base ? base + 1 : path);
        *offset = static_cast<unsigned>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(*module));
    }
}

inline void Enter(int hook, const void* caller, bool nr) {
    if (!on) return;
    const DWORD tid = GetCurrentThreadId();
    InterlockedIncrement(&counters[hook].calls);
    if (nr) InterlockedIncrement(&counters[hook].nr_calls);
    if (render_tid && tid != render_tid) InterlockedIncrement(&counters[hook].other_thread);
    HMODULE module;
    char name[128];
    unsigned offset;
    ModuleName(caller, &module, name, sizeof(name), &offset);
    std::lock_guard guard(seen_lock);
    for (const auto& [h, m, t, n] : seen)
        if (h == hook && m == module && t == tid && n == nr) return;
    seen.emplace_back(hook, module, tid, nr);
    std::printf("hooktrace first t=%.3f hook=%s caller=%s+0x%x tid=%lu render_thread=%d nr=%d\n", Now(), kNames[hook], name, offset, tid,
                tid == render_tid, nr);
}

inline void Summary(const char* where) {
    if (!on) return;
    for (int i = 0; i < kCount; ++i)
        std::printf("hooktrace summary t=%.3f at=%s hook=%s calls=%ld nr_calls=%ld other_thread=%ld\n", Now(), where, kNames[i], counters[i].calls,
                    counters[i].nr_calls, counters[i].other_thread);
}
}
