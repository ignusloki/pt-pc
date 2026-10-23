#pragma once
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace nr_hooks {
inline std::atomic<bool> initialized{false};
inline std::atomic<void*> nr_image{nullptr};
inline thread_local int nr_scope = 0;
struct Scope {
    Scope() { ++nr_scope; }
    ~Scope() { --nr_scope; }
};

inline void* ImageOf(const void* address) {
    void* base = nullptr;
    return address ? RtlPcToFileHeader(const_cast<void*>(address), &base) : nullptr;
}

inline bool NrCaller(const void* caller, bool look_up) {
    if (nr_scope > 0) return true;
    void* image = ImageOf(caller);
    if (!image) return false;
    if (image == &__ImageBase) return true;
    if (image == nr_image.load(std::memory_order_relaxed)) return true;
    if (!look_up) return false;
    if (const HMODULE runtime = GetModuleHandleW(L"nvngx_dlssnr.dll"); runtime && image == runtime) {
        nr_image.store(image, std::memory_order_relaxed);
        return true;
    }
    return false;
}

inline const char* Status(MH_STATUS status) { return MH_StatusToString(status); }

inline MH_STATUS Install(void* target, void* detour, void** original) {
    if (!initialized.load()) {
        const MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return status;
        initialized = true;
    }
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_ERROR_ALREADY_CREATED) status = MH_OK;
    if (status != MH_OK) return status;
    status = MH_EnableHook(target);
    return status == MH_ERROR_ENABLED ? MH_OK : status;
}

inline MH_STATUS Uninstall(void* target) {
    const MH_STATUS status = MH_DisableHook(target);
    return status == MH_ERROR_DISABLED || status == MH_ERROR_NOT_CREATED ? MH_OK : status;
}
}
