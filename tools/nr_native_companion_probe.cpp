#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <cstdint>
#include "nr_native_compat_probe.h"
#include "nr_native_cumodule.h"

using InitFn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version,
                                           const NVSDK_NGX_Parameter*);
static HMODULE runtime;
static InitFn initialize;
static decltype(&NVSDK_NGX_D3D12_CreateFeature) create_feature;
static decltype(&NVSDK_NGX_D3D12_EvaluateFeature) evaluate_feature;
static decltype(&NVSDK_NGX_D3D12_ReleaseFeature) release_feature;
static decltype(&NVSDK_NGX_D3D12_Shutdown1) shutdown_device;
static volatile LONG last_result;

extern "C" __declspec(dllexport) void NativeNrSetCodeDir(const wchar_t* dir) {
    nr_cumodule::replacement_dir = dir ? dir : L"";
}
extern "C" __declspec(dllexport) int NativeNrEnableCompatibility() {
    nr_trace::render_tid = GetCurrentThreadId();
    const char* code_env = std::getenv("PT_NR_CUBIN_DIR");
    const bool legacy_code = !nr_cumodule::replacement_dir.empty() || (code_env && *code_env);
    const int state = nr_compat_probe::Enable(legacy_code);
    if (state < 0) return state;
    const int layer = nr_cumodule::Enable();
    return layer < 0 ? layer - 100 : state;
}
extern "C" __declspec(dllexport) int NativeNrDisableCompatibility() {
    nr_cumodule::Disable();
    return nr_compat_probe::Disable();
}

extern "C" __declspec(dllexport) void NativeNrGetDiagnostics(char* text, unsigned capacity) {
    if (!text || !capacity) return;
    nr_cumodule::in_nr_create = false;
    unsigned driver = 0;
    char branch[64]{};
    int driver_status = -1;
    if (const auto module = GetModuleHandleW(L"nvapi64.dll")) {
        if (const auto query = reinterpret_cast<nr_compat_probe::Query>(GetProcAddress(module, "nvapi_QueryInterface"))) {
            if (const auto version = reinterpret_cast<int(__cdecl*)(unsigned*, char*)>(query(0x2926aaad)))
                driver_status = version(&driver, branch);
        }
    }
    std::snprintf(text, capacity,
                  "scope=nr_create_thread driver=%u.%02u driver_status=%d initial_arch=0x%x implementation=0x%x arch_layer=%d code_layer=%d code_dir=%d "
                  "modules=%ld replaced=%ld functions=%ld failures=%ld last_module=%ld last_function=%ld last_failure=%ld",
                  driver / 100, driver % 100, driver_status, nr_compat_probe::actual.architecture,
                  nr_compat_probe::actual.implementation, nr_compat_probe::active, nr_cumodule::module_hook.active,
                  !nr_cumodule::replacement_dir.empty(), nr_cumodule::nr_create.modules, nr_cumodule::nr_create.replaced,
                  nr_cumodule::nr_create.functions, nr_cumodule::nr_create.failures, nr_cumodule::nr_create.last_module,
                  nr_cumodule::nr_create.last_function, nr_cumodule::nr_create.last_failure);
}

#define COMPLETE_CALL(expression) \
    const auto result = (expression); \
    InterlockedExchange(&last_result, static_cast<LONG>(result)); \
    return result

extern "C" __declspec(dllexport) int NativeNrLoad(const wchar_t* path) {
    if (runtime) {
        wchar_t loaded[MAX_PATH]{};
        if (!GetModuleFileNameW(runtime, loaded, MAX_PATH)) return static_cast<int>(GetLastError());
        return _wcsicmp(loaded, path) == 0 ? 0 : ERROR_ALREADY_INITIALIZED;
    }
    runtime = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!runtime) return static_cast<int>(GetLastError());
    nr_hooks::nr_image = runtime;
    initialize = reinterpret_cast<InitFn>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_Init_Ext"));
    create_feature = reinterpret_cast<decltype(create_feature)>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_CreateFeature"));
    evaluate_feature = reinterpret_cast<decltype(evaluate_feature)>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_EvaluateFeature"));
    release_feature = reinterpret_cast<decltype(release_feature)>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_ReleaseFeature"));
    shutdown_device = reinterpret_cast<decltype(shutdown_device)>(GetProcAddress(runtime, "NVSDK_NGX_D3D12_Shutdown1"));
    return initialize && create_feature && evaluate_feature && release_feature && shutdown_device ? 0 : ERROR_PROC_NOT_FOUND;
}

extern "C" __declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NativeNrInit(
    unsigned long long app, const wchar_t* data, ID3D12Device* device, NVSDK_NGX_Version version,
    const NVSDK_NGX_Parameter* params) {
    if (!initialize) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const nr_hooks::Scope scope;
    COMPLETE_CALL(initialize(app, data, device, version, params));
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NativeNrCreate(
    ID3D12GraphicsCommandList* list, NVSDK_NGX_Feature feature, NVSDK_NGX_Parameter* params, NVSDK_NGX_Handle** handle) {
    if (!create_feature) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const nr_hooks::Scope scope;
    nr_trace::Summary("nr_create");
    nr_cumodule::nr_create = {};
    nr_cumodule::in_nr_create = true;
    const auto result = create_feature(list, feature, params, handle);
    nr_cumodule::in_nr_create = false;
    InterlockedExchange(&last_result, static_cast<LONG>(result));
    return result;
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NativeNrEvaluate(
    ID3D12GraphicsCommandList* list, const NVSDK_NGX_Handle* handle, const NVSDK_NGX_Parameter* params,
    PFN_NVSDK_NGX_ProgressCallback progress) {
    if (!evaluate_feature) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const nr_hooks::Scope scope;
    nr_cumodule::eval_nr_launches = nr_cumodule::eval_pre_block = nr_cumodule::eval_adapted = 0;
    const auto result = evaluate_feature(list, handle, params, progress);
    if (nr_trace::on) {
        static long evaluates = 0, last = -1, last_pre = -1, last_adapted = -1;
        ++evaluates;
        if (nr_cumodule::eval_nr_launches != last || nr_cumodule::eval_pre_block != last_pre || nr_cumodule::eval_adapted != last_adapted)
            std::printf("hooktrace evaluate t=%.3f n=%ld tid=%lu nr_launches=%ld pre_block=%ld adapted=%ld (previous %ld/%ld/%ld)\n", nr_trace::Now(),
                        evaluates, GetCurrentThreadId(), nr_cumodule::eval_nr_launches, nr_cumodule::eval_pre_block, nr_cumodule::eval_adapted, last,
                        last_pre, last_adapted);
        last = nr_cumodule::eval_nr_launches; last_pre = nr_cumodule::eval_pre_block; last_adapted = nr_cumodule::eval_adapted;
        if (evaluates % 600 == 0) nr_trace::Summary("evaluate");
    }
    InterlockedExchange(&last_result, static_cast<LONG>(result));
    return result;
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NativeNrRelease(NVSDK_NGX_Handle* handle) {
    if (!release_feature) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const nr_hooks::Scope scope;
    COMPLETE_CALL(release_feature(handle));
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result NVSDK_CONV NativeNrShutdown(ID3D12Device* device) {
    if (!shutdown_device) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const nr_hooks::Scope scope;
    const auto result = shutdown_device(device);
    nr_cumodule::ForgetNr();
    InterlockedExchange(&last_result, static_cast<LONG>(result));
    return result;
}
