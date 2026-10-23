#pragma once
#include <array>
#include <cstring>
#include <cstdio>
#include "nr_native_hooks.h"
#include "nr_native_hooktrace.h"

namespace nr_compat_probe {
struct Architecture { unsigned version, architecture, implementation, revision; };
static_assert(sizeof(Architecture) == 16);
using Query = void*(__cdecl*)(unsigned);
using GetArchitecture = int(__cdecl*)(void*, Architecture*);
inline void* gpu;
inline Architecture actual;
inline void* target;
inline GetArchitecture original;
inline bool active;
inline volatile LONG calls, passed;

__declspec(noinline) inline int __cdecl Describe(void* handle, Architecture* result) {
    const bool nr = nr_hooks::NrCaller(_ReturnAddress(), true);
    nr_trace::Enter(nr_trace::kArch, _ReturnAddress(), nr);
    if (!nr) {
        InterlockedIncrement(&passed);
        return original(handle, result);
    }
    InterlockedIncrement(&calls);
    if (!result) return -5;
    if (handle != gpu) return -8;
    const unsigned version = result->version;
    if (version != 0x10010 && version != 0x20010) return -9;
    *result = actual;
    result->version = version;
    result->architecture = 0x1b0;
    result->implementation = 3;
    result->revision = 0xa1;
    return 0;
}

inline int Enable(bool legacy_code = false) {
    if (active) return 1;
    const auto module = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return -1;
    const auto query = reinterpret_cast<Query>(GetProcAddress(module, "nvapi_QueryInterface"));
    if (!query) return -2;
    const auto initialize = reinterpret_cast<int(__cdecl*)()>(query(0x0150e828));
    const auto enumerate = reinterpret_cast<int(__cdecl*)(void**, unsigned*)>(query(0xe5ac921f));
    const auto describe = reinterpret_cast<GetArchitecture>(query(0xd8265d24));
    if (!initialize || !enumerate || !describe || initialize() != 0) return -3;
    void* handles[64]{};
    unsigned count = 0;
    if (enumerate(handles, &count) != 0 || count != 1) return -4;
    gpu = handles[0];
    if (!target) {
        actual.version = 0x20010;
        if (describe(gpu, &actual) != 0) return -5;
        std::printf("compat_actual architecture=0x%x implementation=0x%x revision=0x%x gpu_count=%u\n",
                    actual.architecture, actual.implementation, actual.revision, count);
    }
    const unsigned group = actual.architecture & ~15u;
    if (group >= 0x1b0) {
        if (!legacy_code) return 0;
    } else if (group != 0x170 && group != 0x190) return -6;
    const MH_STATUS status = nr_hooks::Install(reinterpret_cast<void*>(describe), reinterpret_cast<void*>(&Describe),
                                               reinterpret_cast<void**>(&original));
    if (status != MH_OK) {
        std::printf("compat_hook_failed status=%s\n", nr_hooks::Status(status));
        return -7;
    }
    target = reinterpret_cast<void*>(describe);
    active = true;
    Architecture observed{}; observed.version = actual.version;
    if (describe(gpu, &observed) != 0 || observed.architecture != 0x1b0) return -8;
    std::printf("compat_private=1 reported_architecture=0x1b0 to=nr_runtime_only nvapi_query_id=0xd8265d24 hooks=minhook file_changes=0\n");
    return 1;
}

inline int Disable() {
    if (!active) return 0;
    if (nr_hooks::Uninstall(target) != MH_OK) return -1;
    active = false;
    std::printf("compat_restored=1 nr_calls=%ld passed_to_original=%ld\n", calls, passed);
    return 0;
}
}
