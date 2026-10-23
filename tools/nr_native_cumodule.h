#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>
#include "nr_native_hooks.h"
#include "nr_native_hooktrace.h"

namespace nr_cumodule {
using QueryFn = void*(__cdecl*)(unsigned);
using CreateModuleFn = int(__cdecl*)(void* device, const void* blob, unsigned size, void** module);
using CreateFunctionFn = int(__cdecl*)(void* device, void* module, const char* name, void** function);

struct Dim3 { unsigned x, y, z; };
struct LaunchParams { void* function; Dim3 grid, block; unsigned shared; const void* params; unsigned size; };
struct LaunchParamsEx { void* function; Dim3 grid, block; unsigned shared; const void* params; unsigned size; void** kernel_params; };
using LaunchFn = int(__cdecl*)(void* list, const LaunchParams* kernels, unsigned count);
using LaunchExFn = int(__cdecl*)(void* list, const LaunchParamsEx* kernels, unsigned count);

struct Hook {
    void* target = nullptr;
    void* original = nullptr;
    bool active = false;
};
inline Hook module_hook, function_hook, launch_hook, launch_ex_hook;
inline std::mutex lock;
inline std::wstring replacement_dir;
inline volatile LONG modules, replaced, functions, failures, launches, adapted, nr_launches, forgotten;
struct CreateDiagnostics {
    LONG modules = 0, replaced = 0, functions = 0, failures = 0;
    LONG last_module = 0, last_function = 0, last_failure = 0;
};
inline thread_local bool in_nr_create;
inline thread_local CreateDiagnostics nr_create;

struct Adapted { unsigned new_size, old_size; };
inline constexpr unsigned kNewFields[][2] = {{144, 4}, {196, 4}};
struct NrFunction { void* handle; std::string name; bool adapt; };
inline std::vector<NrFunction> nr_functions;
inline constexpr Adapted kPreBlock{272, 264};
inline unsigned drop_offset = 1;
inline long launch_log_limit = 0;
inline thread_local long eval_nr_launches, eval_pre_block, eval_adapted;

inline const NrFunction* FindNr(void* function) {
    for (const auto& f : nr_functions)
        if (f.handle == function) return &f;
    return nullptr;
}

inline std::vector<unsigned char> OldLayout(const void* params, const Adapted& a) {
    const auto* bytes = static_cast<const unsigned char*>(params);
    std::vector<unsigned char> out;
    out.reserve(a.old_size);
    unsigned at = 0;
    for (const auto& field : kNewFields) {
        out.insert(out.end(), bytes + at, bytes + field[0]);
        at = field[0] + field[1];
    }
    out.insert(out.end(), bytes + at, bytes + a.new_size);
    return out;
}

inline uint64_t Fnv(const void* data, size_t size) {
    uint64_t hash = 1469598103934665603ull;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

__declspec(noinline) inline int __cdecl CreateModule(void* device, const void* blob, unsigned size, void** module) {
    const bool nr = nr_hooks::NrCaller(_ReturnAddress(), true);
    nr_trace::Enter(nr_trace::kModule, _ReturnAddress(), nr);
    const auto original = reinterpret_cast<CreateModuleFn>(module_hook.original);
    const LONG index = InterlockedIncrement(&modules);
    const uint64_t hash = blob ? Fnv(blob, size) : 0;
    std::vector<char> substitute;
    if (nr && !replacement_dir.empty()) {
        wchar_t name[64];
        std::swprintf(name, 64, L"\\%016llx.cubin", static_cast<unsigned long long>(hash));
        std::ifstream file(std::filesystem::path(replacement_dir + name), std::ios::binary);
        if (file) substitute.assign(std::istreambuf_iterator<char>(file), {});
    }
    const void* used = substitute.empty() ? blob : substitute.data();
    const unsigned used_size = substitute.empty() ? size : static_cast<unsigned>(substitute.size());
    const int result = original(device, used, used_size, module);
    if (!substitute.empty()) InterlockedIncrement(&replaced);
    if (result != 0) InterlockedIncrement(&failures);
    if (in_nr_create) {
        ++nr_create.modules;
        if (!substitute.empty()) ++nr_create.replaced;
        nr_create.last_module = result;
        if (result != 0) { ++nr_create.failures; nr_create.last_failure = result; }
    }
    std::printf("cumodule index=%ld size=%u hash=%016llx nr=%d replaced=%d replacement_size=%u result=%d module=%p\n", index, size,
                static_cast<unsigned long long>(hash), nr, !substitute.empty(), substitute.empty() ? 0u : used_size, result,
                module ? *module : nullptr);
    return result;
}

__declspec(noinline) inline int __cdecl CreateFunction(void* device, void* module, const char* name, void** function) {
    const bool nr = nr_hooks::NrCaller(_ReturnAddress(), true);
    nr_trace::Enter(nr_trace::kFunction, _ReturnAddress(), nr);
    const auto original = reinterpret_cast<CreateFunctionFn>(function_hook.original);
    InterlockedIncrement(&functions);
    const int result = original(device, module, name, function);
    if (result != 0) InterlockedIncrement(&failures);
    if (in_nr_create) {
        ++nr_create.functions;
        nr_create.last_function = result;
        if (result != 0) { ++nr_create.failures; nr_create.last_failure = result; }
    }
    if (result == 0 && function && *function) {
        std::lock_guard guard(lock);
        for (auto it = nr_functions.begin(); it != nr_functions.end();) {
            if (it->handle == *function) {
                InterlockedIncrement(&forgotten);
                it = nr_functions.erase(it);
            } else {
                ++it;
            }
        }
        if (nr && name) {
            const bool adapt = !replacement_dir.empty() && drop_offset != 0 && std::strncmp(name, "cc_tinlayout_fused_pre_block_", 29) == 0;
            nr_functions.push_back({*function, name, adapt});
        }
    }
    std::printf("cufunction module=%p name=%s nr=%d result=%d\n", module, name ? name : "(null)", nr, result);
    return result;
}

template <typename Params, typename Fn>
int Launch(const Hook& hook, const void* caller, void* list, const Params* kernels, unsigned count) {
    const auto original = reinterpret_cast<Fn>(hook.original);
    const LONG first = InterlockedAdd(&launches, static_cast<LONG>(count)) - static_cast<LONG>(count);
    const bool nr = nr_hooks::NrCaller(caller, false);
    nr_trace::Enter(&hook == &launch_hook ? nr_trace::kLaunch : nr_trace::kLaunchEx, caller, nr);
    if (!nr) {
        for (unsigned i = 0; i < count && first + static_cast<LONG>(i) < launch_log_limit; ++i)
            std::printf("launch %ld chain=%u/%u (not NR) grid=%u,%u,%u block=%u shared=%u params=%u\n", first + static_cast<LONG>(i), i, count,
                        kernels[i].grid.x, kernels[i].grid.y, kernels[i].grid.z, kernels[i].block.x, kernels[i].shared, kernels[i].size);
        return original(list, kernels, count);
    }
    std::lock_guard guard(lock);
    for (unsigned i = 0; i < count && first + static_cast<LONG>(i) < launch_log_limit; ++i) {
        const NrFunction* f = FindNr(kernels[i].function);
        const char* name = f ? f->name.c_str() : "?";
        std::printf("launch %ld chain=%u/%u %s grid=%u,%u,%u block=%u shared=%u params=%u\n", first + static_cast<LONG>(i), i, count, name,
                    kernels[i].grid.x, kernels[i].grid.y, kernels[i].grid.z, kernels[i].block.x, kernels[i].shared, kernels[i].size);
        if (std::strncmp(name, "cc_tinlayout_fused_pre_block_", 29) == 0 && kernels[i].params) {
            const auto* p = static_cast<const unsigned char*>(kernels[i].params);
            std::printf("params_hex");
            for (unsigned k = 0; k < kernels[i].size; k += 8) {
                unsigned long long v = 0;
                std::memcpy(&v, p + k, (kernels[i].size - k) < 8 ? (kernels[i].size - k) : 8);
                std::printf(" %u:%016llx", k, v);
            }
            std::printf("\n");
        }
    }
    std::vector<Params> copy;
    static std::deque<std::vector<unsigned char>> kept;
    for (unsigned i = 0; i < count; ++i) {
        const NrFunction* f = FindNr(kernels[i].function);
        if (!f) continue;
        InterlockedIncrement(&nr_launches);
        ++eval_nr_launches;
        if (std::strncmp(f->name.c_str(), "cc_tinlayout_fused_pre_block_", 29) == 0) ++eval_pre_block;
        if (!f->adapt || kernels[i].size != kPreBlock.new_size || !kernels[i].params) continue;
        if (copy.empty()) copy.assign(kernels, kernels + count);
        kept.push_back(OldLayout(kernels[i].params, kPreBlock));
        if (kept.size() > 4096) kept.pop_front();
        copy[i].params = kept.back().data();
        copy[i].size = static_cast<unsigned>(kept.back().size());
        ++eval_adapted;
        if (InterlockedIncrement(&adapted) <= 2)
            std::printf("launch_adapted function=%p grid=%u,%u,%u block=%u,%u,%u shared=%u size %u->%u drop=%u\n", kernels[i].function,
                        kernels[i].grid.x, kernels[i].grid.y, kernels[i].grid.z, kernels[i].block.x, kernels[i].block.y, kernels[i].block.z,
                        kernels[i].shared, kPreBlock.new_size, copy[i].size, 0u);
    }
    return original(list, copy.empty() ? kernels : copy.data(), count);
}

__declspec(noinline) inline int __cdecl LaunchChain(void* list, const LaunchParams* kernels, unsigned count) {
    return Launch<LaunchParams, LaunchFn>(launch_hook, _ReturnAddress(), list, kernels, count);
}
__declspec(noinline) inline int __cdecl LaunchChainEx(void* list, const LaunchParamsEx* kernels, unsigned count) {
    return Launch<LaunchParamsEx, LaunchExFn>(launch_ex_hook, _ReturnAddress(), list, kernels, count);
}

inline bool Arm(Hook& hook, void* target, void* detour, int error) {
    if (hook.active) return true;
    const MH_STATUS status = nr_hooks::Install(target, detour, &hook.original);
    if (status != MH_OK) {
        std::printf("cumodule_hook_failed error=%d status=%s\n", error, nr_hooks::Status(status));
        return false;
    }
    hook.target = target;
    hook.active = true;
    return true;
}

inline int Enable() {
    const auto library = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return -1;
    const auto query = reinterpret_cast<QueryFn>(GetProcAddress(library, "nvapi_QueryInterface"));
    if (!query) return -2;
    void* create_module = query(0xad1a677d);
    void* create_function = query(0xe2436e22);
    if (!create_module || !create_function) return -3;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (replacement_dir.empty()) {
        if (const char* dir = std::getenv("PT_NR_CUBIN_DIR")) replacement_dir = std::filesystem::path(dir).wstring();
    }
    if (const char* log = std::getenv("PT_NR_LAUNCH_LOG")) launch_log_limit = std::strtol(log, nullptr, 10);
    if (const char* drop = std::getenv("PT_NR_PARAM_DROP")) drop_offset = static_cast<unsigned>(std::strtoul(drop, nullptr, 10));
    if (!Arm(module_hook, create_module, reinterpret_cast<void*>(&CreateModule), -4)) return -4;
    if (!Arm(function_hook, create_function, reinterpret_cast<void*>(&CreateFunction), -5)) return -5;
    if (!replacement_dir.empty() || launch_log_limit > 0) {
        void* launch = query(0x24973538);
        void* launch_ex = query(0x846a9bf0);
        if (launch && !Arm(launch_hook, launch, reinterpret_cast<void*>(&LaunchChain), -6)) return -6;
        if (launch_ex && !Arm(launch_ex_hook, launch_ex, reinterpret_cast<void*>(&LaunchChainEx), -7)) return -7;
    }
    std::printf("cumodule_layer=1 hooks=minhook replacement_dir=%ls\n", replacement_dir.empty() ? L"(none)" : replacement_dir.c_str());
    return 1;
}

inline void ForgetNr() {
    std::lock_guard guard(lock);
    std::printf("cumodule_forget nr_functions=%zu\n", nr_functions.size());
    nr_functions.clear();
}

inline void Disable() {
    for (Hook* hook : {&module_hook, &function_hook, &launch_hook, &launch_ex_hook}) {
        if (hook->active && nr_hooks::Uninstall(hook->target) == MH_OK) hook->active = false;
    }
    ForgetNr();
    std::printf("cumodule_summary modules=%ld replaced=%ld functions=%ld failures=%ld launches=%ld nr_launches=%ld adapted=%ld forgotten=%ld\n",
                modules, replaced, functions, failures, launches, nr_launches, adapted, forgotten);
}
}
