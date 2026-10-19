#include "engine/render/upscale/streamline.h"

#if defined(PT_WITH_STREAMLINE)

#include <windows.h>

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_security.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>

#include "engine/core/log.h"
#include "engine/core/crash_report.h"
#include "engine/render/upscale/frame_generation.h"
#include "engine/render/upscale/upscale.h"
#include "engine/render/upscale/upscale_platform.h"

namespace {

struct Api {
    PFun_slInit* init = nullptr;
    PFun_slShutdown* shutdown = nullptr;
    PFun_slIsFeatureSupported* is_supported = nullptr;
    PFun_slIsFeatureLoaded* is_loaded = nullptr;
    PFun_slSetFeatureLoaded* set_loaded = nullptr;
    PFun_slEvaluateFeature* evaluate = nullptr;
    PFun_slSetTagForFrame* set_tag = nullptr;
    PFun_slGetFeatureRequirements* requirements = nullptr;
    PFun_slSetConstants* set_constants = nullptr;
    PFun_slGetFeatureFunction* feature_function = nullptr;
    PFun_slGetNewFrameToken* new_token = nullptr;
    PFun_slFreeResources* free_resources = nullptr;
    PFun_slSetD3DDevice* set_d3d_device = nullptr;
    PFun_slUpgradeInterface* upgrade = nullptr;
};

Api g_api;
HMODULE g_module = nullptr;
bool g_active = false;
pt::streamline::Api g_kind = pt::streamline::Api::Vulkan;
PFN_vkGetInstanceProcAddr g_instance_proc = nullptr;
sl::FrameToken* g_token = nullptr;
bool g_constants = false;
bool g_dlss = false;
bool g_rr = false;
std::string g_rr_reason = "Streamline is not loaded";
bool g_frame_gen = false;
bool g_vsync_off = false;
std::string g_frame_gen_reason = "Streamline is not loaded";
uint32_t g_frame_limit = UINT32_MAX;
bool g_reflex_d3d12 = false;
std::wstring g_log_dir;
bool g_verbose = false;
std::wstring g_plugin_dir;
const wchar_t* g_plugin_paths[1] = {};

const char* ResultName(sl::Result r) {
    switch (r) {
    case sl::Result::eOk: return "ok";
    case sl::Result::eErrorDriverOutOfDate: return "driver out of date";
    case sl::Result::eErrorOSOutOfDate: return "Windows out of date";
    case sl::Result::eErrorOSDisabledHWS: return "hardware-accelerated GPU scheduling is off";
    case sl::Result::eErrorNoSupportedAdapterFound: return "no supported adapter";
    case sl::Result::eErrorAdapterNotSupported: return "adapter not supported";
    case sl::Result::eErrorNoPlugins: return "no plugins";
    case sl::Result::eErrorFeatureMissing: return "feature missing";
    case sl::Result::eErrorFeatureNotSupported: return "feature not supported";
    case sl::Result::eErrorFeatureFailedToLoad: return "feature failed to load";
    case sl::Result::eErrorNGXFailed: return "NGX failed";
    case sl::Result::eErrorInitNotCalled: return "slInit not called";
    default: return "error";
    }
}

void Log(sl::LogType type, const char* message) {
    std::string text = message ? message : "";
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    if (type == sl::LogType::eError) {
        pt::LogError("streamline: {}", text);
    } else if (type == sl::LogType::eWarn) {
        pt::LogWarn("streamline: {}", text);
    } else if (g_verbose) {
        pt::LogInfo("streamline: {}", text);
    }
}

template <typename Fn>
bool Load(Fn*& out, const char* name) {
    out = reinterpret_cast<Fn*>(GetProcAddress(g_module, name));
    return out != nullptr;
}

sl::float4x4 Matrix(const glm::dmat4& m) {
    sl::float4x4 out;
    for (int r = 0; r < 4; ++r) {
        out.row[r] = sl::float4(static_cast<float>(m[r][0]), static_cast<float>(m[r][1]), static_cast<float>(m[r][2]), static_cast<float>(m[r][3]));
    }
    return out;
}

sl::float3 Vec(const glm::vec3& v) {
    return sl::float3(v.x, v.y, v.z);
}

}

extern "C" sl::Result slGetFeatureFunction(sl::Feature feature, const char* functionName, void*& function) {
    if (!g_api.feature_function) {
        return sl::Result::eErrorInitNotCalled;
    }
    return g_api.feature_function(feature, functionName, function);
}

namespace pt::streamline {

bool Start(const std::filesystem::path& data_dir, std::string& reason, Api api) {
    if (g_active) {
        return true;
    }
    const bool d3d12 = api == Api::D3D12;
    const std::filesystem::path dir = ExecutableDir();
    const std::filesystem::path interposer = dir / L"sl.interposer.dll";
    std::error_code ec;
    if (!std::filesystem::exists(interposer, ec)) {
        reason = "sl.interposer.dll is missing next to pt.exe";
        return false;
    }
    /* NVIDIA's signature is checked before LoadLibrary, as the Streamline guide asks; the interposer checks each plugin the same way. */
    if (!sl::security::verifyEmbeddedSignature(interposer.wstring().c_str())) {
        reason = "sl.interposer.dll does not carry NVIDIA's signature";
        return false;
    }
    g_module = LoadLibraryW(interposer.wstring().c_str());
    if (!g_module) {
        reason = "sl.interposer.dll could not be loaded";
        return false;
    }
    const bool ok = Load(g_api.init, "slInit") && Load(g_api.shutdown, "slShutdown") && Load(g_api.is_supported, "slIsFeatureSupported") &&
                    Load(g_api.is_loaded, "slIsFeatureLoaded") &&
                    Load(g_api.set_loaded, "slSetFeatureLoaded") && Load(g_api.evaluate, "slEvaluateFeature") &&
                    Load(g_api.set_tag, "slSetTagForFrame") && Load(g_api.requirements, "slGetFeatureRequirements") &&
                    Load(g_api.set_constants, "slSetConstants") && Load(g_api.feature_function, "slGetFeatureFunction") &&
                    Load(g_api.new_token, "slGetNewFrameToken") && Load(g_api.free_resources, "slFreeResources");
    g_instance_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(g_module, "vkGetInstanceProcAddr"));
    const bool d3d12_ok = !d3d12 || (Load(g_api.set_d3d_device, "slSetD3DDevice") && Load(g_api.upgrade, "slUpgradeInterface"));
    if (!ok || !d3d12_ok || (!d3d12 && !g_instance_proc)) {
        reason = "sl.interposer.dll has not the Streamline exports";
        g_api = {};
        return false;
    }
    std::filesystem::create_directories(data_dir, ec);
    g_log_dir = data_dir.wstring();
    g_plugin_dir = dir.wstring();
    g_plugin_paths[0] = g_plugin_dir.c_str();
    static sl::Feature features[5] = {sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL, sl::kFeatureDLSS_RR};
    static sl::Feature features_d3d12[3] = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    const bool rr_files = !d3d12 && std::filesystem::exists(dir / L"sl.dlss_d.dll", ec) && std::filesystem::exists(dir / L"nvngx_dlssd.dll", ec);
    g_rr_reason = d3d12 ? "runs through NGX on DirectX 12" : rr_files ? "not checked" : "sl.dlss_d.dll or nvngx_dlssd.dll is missing next to pt.exe";
    sl::Preferences pref{};
    pref.showConsole = false;
    g_verbose = std::getenv("PT_SL_VERBOSE") != nullptr;
    pref.logLevel = g_verbose ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
    pref.pathsToPlugins = g_plugin_paths;
    pref.numPathsToPlugins = 1;
    pref.pathToLogsAndData = g_log_dir.c_str();
    pref.logMessageCallback = Log;
    pref.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eAllowOTA | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    if (d3d12) {
        pref.flags |= sl::PreferenceFlags::eUseManualHooking;
        pref.flags &= ~sl::PreferenceFlags::eAllowOTA;
        pref.featuresToLoad = features_d3d12;
        pref.numFeaturesToLoad = static_cast<uint32_t>(std::size(features_d3d12));
    } else {
        pref.featuresToLoad = features;
        pref.numFeaturesToLoad = static_cast<uint32_t>(std::size(features)) - (rr_files ? 0u : 1u);
    }
    pref.engine = sl::EngineType::eCustom;
    pref.engineVersion = "1.0";
    pref.projectId = "3c9e5a1b-7d24-4f68-b0e3-5a2c8d91f47e";
    pref.renderAPI = d3d12 ? sl::RenderAPI::eD3D12 : sl::RenderAPI::eVulkan;
    const sl::Result result = g_api.init(pref, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        reason = std::format("slInit failed: {} ({})", ResultName(result), static_cast<int>(result));
        g_api = {};
        return false;
    }
    g_active = true;
    g_kind = api;
    LogInfo("streamline: 2.14.1 loaded for {} from {}, log in {}", d3d12 ? "DirectX 12 (manual hooking)" : "Vulkan", NarrowText(g_plugin_dir.c_str()),
            NarrowText(g_log_dir.c_str()));
    return true;
}

bool Active() {
    return g_active;
}

bool ActiveD3D12() {
    return g_active && g_kind == Api::D3D12;
}

PFN_vkGetInstanceProcAddr InstanceProcAddr() {
    return g_active && g_kind == Api::Vulkan ? g_instance_proc : nullptr;
}

void DeviceCreated(VkInstance, VkPhysicalDevice physical, VkDevice) {
    if (!g_active) {
        return;
    }
    sl::AdapterInfo adapter{};
    adapter.vkPhysicalDevice = physical;
    const sl::Result dlss = g_api.is_supported(sl::kFeatureDLSS, adapter);
    const sl::Result frame_gen = g_api.is_supported(sl::kFeatureDLSS_G, adapter);
    const sl::Result reflex = g_api.is_supported(sl::kFeatureReflex, adapter);
    const sl::Result pcl = g_api.is_supported(sl::kFeaturePCL, adapter);
    g_dlss = dlss == sl::Result::eOk;
    if (g_rr_reason == "not checked") {
        const sl::Result rr = g_api.is_supported(sl::kFeatureDLSS_RR, adapter);
        g_rr = rr == sl::Result::eOk;
        g_rr_reason = g_rr ? "" : ResultName(rr);
        LogInfo("streamline: DLSS Ray Reconstruction {}", ResultName(rr));
    }
    g_frame_gen = frame_gen == sl::Result::eOk && reflex == sl::Result::eOk && pcl == sl::Result::eOk;
    g_frame_gen_reason = frame_gen != sl::Result::eOk ? ResultName(frame_gen) : reflex != sl::Result::eOk ? "Reflex is not supported" : "";
    sl::FeatureRequirements requirements{};
    if (g_frame_gen && g_api.requirements(sl::kFeatureDLSS_G, requirements) == sl::Result::eOk) {
        g_vsync_off = requirements.flags & sl::FeatureRequirementFlags::eVSyncOffRequired;
    }
    LogInfo("streamline: DLSS {}, DLSS Frame Generation {}, Reflex {}, PCL {}{}", ResultName(dlss), ResultName(frame_gen), ResultName(reflex),
            ResultName(pcl), g_vsync_off ? ", frame generation needs v-sync off" : "");
    if (reflex == sl::Result::eOk) {
        sl::ReflexOptions options{};
        options.mode = sl::ReflexMode::eLowLatency;
        options.frameLimitUs = 0;
        slReflexSetOptions(options);
        g_frame_limit = 0;
    }
}

void Shutdown() {
    if (!g_active) {
        return;
    }
    const sl::Result result = g_api.shutdown();
    LogInfo("streamline: shut down ({})", ResultName(result));
    g_active = false;
    g_token = nullptr;
    g_dlss = g_frame_gen = g_rr = false;
    g_reflex_d3d12 = false;
    g_frame_limit = UINT32_MAX;
}

bool DlssSupported() {
    return g_active && g_dlss;
}

bool RayReconstructionSupported(std::string* reason) {
    if (reason) {
        *reason = g_rr ? std::string() : g_rr_reason;
    }
    return g_active && g_rr;
}

bool FrameGenSupported(std::string* reason) {
    if (reason) {
        *reason = g_frame_gen ? std::string() : g_frame_gen_reason;
    }
    return g_active && g_frame_gen;
}

bool FrameGenNeedsVsyncOff() {
    return g_vsync_off;
}

bool IsFrameGenLoaded(bool& loaded) {
    return g_active && g_api.is_loaded && g_api.is_loaded(sl::kFeatureDLSS_G, loaded) == sl::Result::eOk;
}

void BeginFrame() {
    if (!g_active) {
        return;
    }
    g_token = nullptr;
    g_constants = false;
    if (g_api.new_token(g_token, nullptr) != sl::Result::eOk || !g_token) {
        g_token = nullptr;
        return;
    }
    slReflexSleep(*g_token);
    SetMarker(Marker::SimulationStart);
}

void SetMarker(Marker marker) {
    if (g_active && g_token) {
        slPCLSetMarker(static_cast<sl::PCLMarker>(marker), *g_token);
    }
}

void SetConstants(const FrameConstants& c) {
    if (!g_active || !g_token) {
        return;
    }
    const glm::dmat4 view_to_clip(c.view_to_clip);
    const glm::dmat4 world_to_clip(c.world_to_clip);
    const glm::dmat4 previous(c.previous_world_to_clip);
    const glm::dmat4 clip_to_previous = previous * glm::inverse(world_to_clip);
    sl::Constants k{};
    k.cameraViewToClip = Matrix(view_to_clip);
    k.clipToCameraView = Matrix(glm::inverse(view_to_clip));
    k.clipToLensClip = Matrix(glm::dmat4(1.0));
    k.clipToPrevClip = Matrix(clip_to_previous);
    k.prevClipToClip = Matrix(glm::inverse(clip_to_previous));
    k.jitterOffset = sl::float2(c.jitter.x, c.jitter.y);
    k.mvecScale = sl::float2(c.motion_scale.x, c.motion_scale.y);
    k.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
    k.cameraPos = Vec(c.position);
    k.cameraUp = Vec(c.up);
    k.cameraRight = Vec(c.right);
    k.cameraFwd = Vec(c.forward);
    k.cameraNear = c.near_plane;
    k.cameraFar = 100000.0f;
    k.cameraFOV = c.fov_y;
    k.cameraAspectRatio = c.aspect;
    k.motionVectorsInvalidValue = 0.0f;
    k.depthInverted = sl::Boolean::eTrue;
    k.cameraMotionIncluded = sl::Boolean::eTrue;
    k.motionVectors3D = sl::Boolean::eFalse;
    k.reset = c.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    k.orthographicProjection = sl::Boolean::eFalse;
    k.motionVectorsDilated = sl::Boolean::eFalse;
    k.motionVectorsJittered = sl::Boolean::eFalse;
    const sl::ViewportHandle viewport(kViewport);
    if (g_api.set_constants(k, *g_token, viewport) == sl::Result::eOk) {
        g_constants = true;
    }
}

bool ConstantsSet() {
    return g_constants;
}

void SetFrameLimit(uint32_t microseconds) {
    if (!g_active || microseconds == g_frame_limit) {
        return;
    }
    if (g_kind == Api::D3D12 && !g_reflex_d3d12) {
        return;
    }
    sl::ReflexOptions options{};
    options.mode = sl::ReflexMode::eLowLatency;
    options.frameLimitUs = microseconds;
    if (slReflexSetOptions(options) == sl::Result::eOk) {
        g_frame_limit = microseconds;
    }
}

const void* FrameToken() {
    return g_token;
}

sl::Result Evaluate(sl::Feature feature, const sl::FrameToken& token, const sl::BaseStructure** inputs, uint32_t count, VkCommandBuffer cmd) {
    return g_active ? g_api.evaluate(feature, token, inputs, count, cmd) : sl::Result::eErrorInitNotCalled;
}

void FreeResources(sl::Feature feature) {
    if (g_active) {
        g_api.free_resources(feature, sl::ViewportHandle(kViewport));
    }
}

sl::Result Tag(const sl::ResourceTag* tags, uint32_t count, VkCommandBuffer cmd) {
    if (!g_active || !g_token) {
        return sl::Result::eErrorInitNotCalled;
    }
    return g_api.set_tag(*g_token, sl::ViewportHandle(kViewport), tags, count, cmd);
}

sl::Result SetFeatureLoaded(sl::Feature feature, bool loaded) {
    return g_active ? g_api.set_loaded(feature, loaded) : sl::Result::eErrorInitNotCalled;
}

bool DisableFrameGenFeature() {
    if (!g_active || !g_frame_gen) return false;
    const sl::Result result = g_api.set_loaded(sl::kFeatureDLSS_G, false);
    g_frame_gen = false;
    return result == sl::Result::eOk;
}

void DeviceCreatedD3D12(void* d3d12_device, const uint8_t (&luid)[8]) {
    if (!ActiveD3D12()) {
        return;
    }
    const sl::Result set = g_api.set_d3d_device(d3d12_device);
    if (set != sl::Result::eOk) {
        g_frame_gen = false;
        g_frame_gen_reason = std::format("slSetD3DDevice failed: {}", ResultName(set));
        LogError("streamline: {}", g_frame_gen_reason);
        return;
    }
    sl::AdapterInfo adapter{};
    uint8_t id[8];
    std::memcpy(id, luid, sizeof(id));
    adapter.deviceLUID = id;
    adapter.deviceLUIDSizeInBytes = sizeof(id);
    const sl::Result frame_gen = g_api.is_supported(sl::kFeatureDLSS_G, adapter);
    const sl::Result reflex = g_api.is_supported(sl::kFeatureReflex, adapter);
    const sl::Result pcl = g_api.is_supported(sl::kFeaturePCL, adapter);
    g_dlss = false;
    g_frame_gen = frame_gen == sl::Result::eOk && reflex == sl::Result::eOk && pcl == sl::Result::eOk;
    g_frame_gen_reason = frame_gen != sl::Result::eOk ? ResultName(frame_gen) : reflex != sl::Result::eOk ? "Reflex is not supported" : "";
    sl::FeatureRequirements requirements{};
    g_vsync_off = false;
    if (g_frame_gen && g_api.requirements(sl::kFeatureDLSS_G, requirements) == sl::Result::eOk) {
        g_vsync_off = requirements.flags & sl::FeatureRequirementFlags::eVSyncOffRequired;
    }
    LogInfo("streamline: DirectX 12 device set; DLSS Frame Generation {}, Reflex {}, PCL {}{}", ResultName(frame_gen), ResultName(reflex),
            ResultName(pcl), g_vsync_off ? ", frame generation needs v-sync off" : "");
}

void D3D12SetReflex(bool on) {
    if (!ActiveD3D12()) {
        return;
    }
    sl::ReflexOptions options{};
    options.mode = on ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff;
    options.frameLimitUs = 0;
    const sl::Result r = slReflexSetOptions(options);
    g_frame_limit = 0;
    g_reflex_d3d12 = on;
    LogInfo("streamline: Reflex {} for DLSS Frame Generation on DirectX 12 ({})", on ? "low latency" : "off", ResultName(r));
}

bool UpgradeInterface(void** interface_pointer) {
    if (!ActiveD3D12() || !interface_pointer || !*interface_pointer) {
        return false;
    }
    const sl::Result r = g_api.upgrade(interface_pointer);
    if (r != sl::Result::eOk) {
        LogError("streamline: slUpgradeInterface failed: {}", ResultName(r));
        return false;
    }
    return true;
}

bool D3D12FrameGenTag(const D3D12FrameGenTags& t) {
    if (!ActiveD3D12() || !g_token || !t.depth || !t.motion || !t.hudless) {
        return false;
    }
    sl::Resource depth(sl::ResourceType::eTex2d, t.depth, t.state);
    sl::Resource motion(sl::ResourceType::eTex2d, t.motion, t.state);
    sl::Resource hudless(sl::ResourceType::eTex2d, t.hudless, t.state);
    sl::Resource ui(sl::ResourceType::eTex2d, t.ui, t.state);
    const sl::Extent input{0, 0, t.input_width, t.input_height};
    const sl::Extent full{0, 0, t.color_width, t.color_height};
    sl::ResourceTag tags[] = {
        sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &input),
        sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &input),
        sl::ResourceTag(&hudless, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &full),
        sl::ResourceTag(&ui, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &full),
    };
    const uint32_t count = t.ui ? 4u : 3u;
    const sl::Result r = g_api.set_tag(*g_token, sl::ViewportHandle(kViewport), tags, count, static_cast<sl::CommandBuffer*>(t.command_list));
    static int logged = 0;
    if (r != sl::Result::eOk && logged++ < 4) {
        LogError("frame generation: DLSS Frame Generation (DirectX 12) input tags failed: {}", ResultName(r));
    }
    return r == sl::Result::eOk;
}

void D3D12FrameGenUntag() {
    if (!ActiveD3D12() || !g_token) {
        return;
    }
    const sl::ResourceTag tags[] = {
        sl::ResourceTag(nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent),
        sl::ResourceTag(nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent),
        sl::ResourceTag(nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent),
        sl::ResourceTag(nullptr, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent),
    };
    g_api.set_tag(*g_token, sl::ViewportHandle(kViewport), tags, static_cast<uint32_t>(std::size(tags)), nullptr);
}

void D3D12OnError(const sl::APIError& error) {
    static int logged = 0;
    if (logged++ < 5) {
        LogWarn("frame generation: DLSS Frame Generation (DirectX 12) present returned {:#010x}", static_cast<uint32_t>(error.hres));
    }
}

bool D3D12FrameGenSetOptions(const D3D12FrameGenOptions& o, std::string* error) {
    if (!ActiveD3D12()) {
        if (error) *error = "Streamline is not loaded for DirectX 12";
        return false;
    }
    sl::DLSSGOptions options{};
    options.mode = o.on ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    options.numFramesToGenerate = std::max(1u, o.frames_to_generate);
    options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
    options.numBackBuffers = o.back_buffers;
    options.colorWidth = o.color_width;
    options.colorHeight = o.color_height;
    options.colorBufferFormat = o.color_format;
    options.hudLessBufferFormat = o.hudless_format;
    options.uiBufferFormat = o.ui_format;
    options.mvecDepthWidth = o.mvec_depth_width;
    options.mvecDepthHeight = o.mvec_depth_height;
    options.mvecBufferFormat = o.mvec_format;
    options.depthBufferFormat = o.depth_format;
    options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
    options.enableUserInterfaceRecomposition = o.ui_recomposition ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    options.onErrorCallback = &D3D12OnError;
    const sl::Result r = slDLSSGSetOptions(sl::ViewportHandle(kViewport), options);
    if (r != sl::Result::eOk && error) {
        *error = ResultName(r);
    }
    return r == sl::Result::eOk;
}

bool D3D12FrameGenGetState(D3D12FrameGenState& out) {
    out = D3D12FrameGenState{};
    if (!ActiveD3D12()) {
        return false;
    }
    sl::DLSSGState state{};
    if (slDLSSGGetState(sl::ViewportHandle(kViewport), state, nullptr) != sl::Result::eOk) {
        return false;
    }
    out.ok = state.status == sl::DLSSGStatus::eOk;
    out.status = static_cast<uint32_t>(state.status);
    out.presented = state.numFramesActuallyPresented;
    out.max_frames = state.numFramesToGenerateMax;
    out.vsync_available = state.bIsVsyncSupportAvailable == sl::Boolean::eTrue;
    out.inputs_fence = state.inputsProcessingCompletionFence;
    out.inputs_fence_value = state.lastPresentInputsProcessingCompletionFenceValue;
    return true;
}

void D3D12FrameGenFree() {
    if (ActiveD3D12()) {
        g_api.free_resources(sl::kFeatureDLSS_G, sl::ViewportHandle(kViewport));
    }
}

void D3D12EndFrame() {
    if (ActiveD3D12()) {
        g_token = nullptr;
        g_constants = false;
    }
}

}

namespace pt {
namespace {

sl::Resource SlImage(const UpscaleImage& image, VkImageLayout layout) {
    sl::Resource r(sl::ResourceType::eTex2d, reinterpret_cast<void*>(image.image), nullptr, reinterpret_cast<void*>(image.view),
                   static_cast<uint32_t>(layout));
    r.width = image.extent.width;
    r.height = image.extent.height;
    r.nativeFormat = static_cast<uint32_t>(image.format);
    r.mipLevels = 1;
    r.arrayLayers = 1;
    r.flags = 0;
    r.usage = image.usage;
    return r;
}

sl::DLSSMode SlMode(UpscaleQuality quality) {
    switch (quality) {
    case UpscaleQuality::NativeAA: return sl::DLSSMode::eDLAA;
    case UpscaleQuality::Quality: return sl::DLSSMode::eMaxQuality;
    case UpscaleQuality::Balanced: return sl::DLSSMode::eBalanced;
    case UpscaleQuality::Performance: return sl::DLSSMode::eMaxPerformance;
    case UpscaleQuality::UltraPerformance: return sl::DLSSMode::eUltraPerformance;
    default: return sl::DLSSMode::eMaxQuality;
    }
}

class SlDlssBackend final : public UpscaleBackend {
public:
    explicit SlDlssBackend(vk::Context& ctx) : ctx_(ctx) {}
    ~SlDlssBackend() override { Release(); }

    UpscalerKind Kind() const override { return UpscalerKind::Dlss; }

    bool Supported(std::string& reason) override {
        if (!streamline::DlssSupported()) {
            reason = "Streamline's DLSS is not supported on this GPU";
            return false;
        }
        reason.clear();
        return true;
    }

    bool Available(std::string& reason) override { return Supported(reason); }

    bool RayReconstructionAvailable(std::string& reason) override { return streamline::RayReconstructionSupported(&reason); }

    bool Create(VkCommandBuffer, const UpscaleCreate& create) override {
        Release();
        if (create.ray_reconstruction) {
            return CreateRayReconstruction(create);
        }
        options_ = sl::DLSSOptions{};
        options_.mode = SlMode(create.quality);
        options_.outputWidth = create.display.width;
        options_.outputHeight = create.display.height;
        options_.colorBuffersHDR = sl::Boolean::eTrue;
        const auto preset = static_cast<sl::DLSSPreset>(DlssPresetHint(create.dlss_model));
        options_.dlaaPreset = options_.qualityPreset = options_.balancedPreset = options_.performancePreset = options_.ultraPerformancePreset =
            options_.ultraQualityPreset = preset;
        options_.useAutoExposure = DlssAutoExposure() ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        const sl::ViewportHandle viewport(streamline::kViewport);
        sl::Result result = slDLSSSetOptions(viewport, options_);
        if (result != sl::Result::eOk) {
            LogError("dlss (streamline): slDLSSSetOptions failed ({})", static_cast<int>(result));
            return false;
        }
        sl::DLSSOptimalSettings optimal{};
        if (slDLSSGetOptimalSettings(options_, optimal) == sl::Result::eOk) {
            const bool inside = create.render.width >= optimal.renderWidthMin && create.render.width <= optimal.renderWidthMax &&
                                create.render.height >= optimal.renderHeightMin && create.render.height <= optimal.renderHeightMax;
            LogInfo("dlss (streamline): SR {}x{} -> {}x{}, mode {}, model {} (preset {}), optimal render {}x{} (range {}x{} to {}x{}){}",
                    create.render.width, create.render.height, create.display.width, create.display.height, static_cast<int>(options_.mode),
                    DlssModelKey(create.dlss_model), static_cast<int>(preset), optimal.optimalRenderWidth, optimal.optimalRenderHeight,
                    optimal.renderWidthMin, optimal.renderHeightMin, optimal.renderWidthMax, optimal.renderHeightMax,
                    inside ? "" : ", the render size is outside it");
        }
        created_ = true;
        return true;
    }

    bool Dispatch(const UpscaleDispatch& d) override {
        const auto* token = static_cast<const sl::FrameToken*>(streamline::FrameToken());
        if (!created_ || !token || !streamline::ConstantsSet()) {
            if (!error_logged_) {
                LogError("dlss (streamline): missing frame token or constants");
                error_logged_ = true;
            }
            return false;
        }
        if (rr_) {
            return DispatchRayReconstruction(d, *token);
        }
        const sl::ViewportHandle viewport(streamline::kViewport);
        options_.preExposure = d.pre_exposure;
        options_.exposureScale = 1.0f;
        slDLSSSetOptions(viewport, options_);
        sl::Resource color = SlImage(d.color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sl::Resource output = SlImage(d.output, VK_IMAGE_LAYOUT_GENERAL);
        sl::Resource depth = SlImage(d.depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sl::Resource motion = SlImage(d.motion, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sl::Resource exposure = SlImage(d.exposure, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const sl::Extent render{0, 0, d.render.width, d.render.height};
        const sl::Extent display{0, 0, d.display.width, d.display.height};
        const sl::Extent one{0, 0, 1, 1};
        sl::ResourceTag tags[] = {
            sl::ResourceTag(&color, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &render),
            sl::ResourceTag(&output, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &display),
            sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &render),
            sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &render),
            sl::ResourceTag(&exposure, sl::kBufferTypeExposure, sl::ResourceLifecycle::eValidUntilEvaluate, &one),
        };
        /* No reactive mask tag: Streamline 2.14.1's Vulkan format table lacks R8_UNORM and complains four times a frame; the DLSS 4 models ignore the mask anyway. */
        std::vector<const sl::BaseStructure*> inputs{&viewport};
        for (const sl::ResourceTag& tag : tags) {
            inputs.push_back(&tag);
        }
        const sl::Result result = streamline::Evaluate(sl::kFeatureDLSS, *token, inputs.data(), static_cast<uint32_t>(inputs.size()), d.cmd);
        if (result != sl::Result::eOk) {
            if (!error_logged_) {
                LogError("dlss (streamline): slEvaluateFeature failed ({})", static_cast<int>(result));
                error_logged_ = true;
            }
            return false;
        }
        return true;
    }

    void Release() override {
        if (!created_) {
            return;
        }
        vkDeviceWaitIdle(ctx_.device);
        const sl::ViewportHandle viewport(streamline::kViewport);
        if (rr_) {
            rr_options_.mode = sl::DLSSMode::eOff;
            slDLSSDSetOptions(viewport, rr_options_);
            streamline::FreeResources(sl::kFeatureDLSS_RR);
        } else {
            options_.mode = sl::DLSSMode::eOff;
            slDLSSSetOptions(viewport, options_);
            streamline::FreeResources(sl::kFeatureDLSS);
        }
        created_ = false;
        rr_ = false;
    }

private:
    bool CreateRayReconstruction(const UpscaleCreate& create) {
        if (!streamline::RayReconstructionSupported(nullptr)) {
            return false;
        }
        rr_options_ = sl::DLSSDOptions{};
        rr_options_.mode = SlMode(create.quality);
        rr_options_.outputWidth = create.display.width;
        rr_options_.outputHeight = create.display.height;
        rr_options_.colorBuffersHDR = sl::Boolean::eTrue;
        rr_options_.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
        const sl::ViewportHandle viewport(streamline::kViewport);
        const sl::Result result = slDLSSDSetOptions(viewport, rr_options_);
        if (result != sl::Result::eOk) {
            LogError("dlss (streamline): slDLSSDSetOptions failed ({})", static_cast<int>(result));
            return false;
        }
        LogInfo("dlss (streamline): ray reconstruction {}x{} -> {}x{}, mode {}", create.render.width, create.render.height, create.display.width,
                create.display.height, static_cast<int>(rr_options_.mode));
        created_ = true;
        rr_ = true;
        return true;
    }

    bool DispatchRayReconstruction(const UpscaleDispatch& d, const sl::FrameToken& token) {
        if (!d.diffuse_albedo.Valid() || !d.specular_albedo.Valid() || !d.normal_roughness.Valid()) {
            if (!error_logged_) {
                LogError("dlss (streamline): ray reconstruction has no guide buffers this frame");
                error_logged_ = true;
            }
            return false;
        }
        const sl::ViewportHandle viewport(streamline::kViewport);
        rr_options_.preExposure = d.pre_exposure;
        rr_options_.exposureScale = 1.0f;
        const glm::dmat4 world_to_view(d.world_to_view);
        rr_options_.worldToCameraView = Matrix(world_to_view);
        rr_options_.cameraViewToWorld = Matrix(glm::inverse(world_to_view));
        slDLSSDSetOptions(viewport, rr_options_);
        const VkImageLayout read = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sl::Resource color = SlImage(d.color, read);
        sl::Resource output = SlImage(d.output, VK_IMAGE_LAYOUT_GENERAL);
        sl::Resource depth = SlImage(d.depth, read);
        sl::Resource motion = SlImage(d.motion, read);
        sl::Resource exposure = SlImage(d.exposure, read);
        sl::Resource diffuse_albedo = SlImage(d.diffuse_albedo, read);
        sl::Resource specular_albedo = SlImage(d.specular_albedo, read);
        sl::Resource normal_roughness = SlImage(d.normal_roughness, read);
        sl::Resource hit_distance = d.specular_hit_distance.Valid() ? SlImage(d.specular_hit_distance, read) : sl::Resource{};
        sl::Resource before_transparency = d.color_before_transparency.Valid() ? SlImage(d.color_before_transparency, read) : sl::Resource{};
        const sl::Extent render{0, 0, d.render.width, d.render.height};
        const sl::Extent display{0, 0, d.display.width, d.display.height};
        const sl::Extent one{0, 0, 1, 1};
        const auto now = sl::ResourceLifecycle::eValidUntilEvaluate;
        std::vector<sl::ResourceTag> tags{
            sl::ResourceTag(&color, sl::kBufferTypeScalingInputColor, now, &render),
            sl::ResourceTag(&output, sl::kBufferTypeScalingOutputColor, now, &display),
            sl::ResourceTag(&depth, sl::kBufferTypeDepth, now, &render),
            sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, now, &render),
            sl::ResourceTag(&exposure, sl::kBufferTypeExposure, now, &one),
            sl::ResourceTag(&diffuse_albedo, sl::kBufferTypeAlbedo, now, &render),
            sl::ResourceTag(&specular_albedo, sl::kBufferTypeSpecularAlbedo, now, &render),
            sl::ResourceTag(&normal_roughness, sl::kBufferTypeNormalRoughness, now, &render),
        };
        if (d.specular_hit_distance.Valid()) {
            tags.emplace_back(&hit_distance, sl::kBufferTypeSpecularHitDistance, now, &render);
        }
        if (d.color_before_transparency.Valid()) {
            tags.emplace_back(&before_transparency, sl::kBufferTypeColorBeforeTransparency, now, &render);
        }
        std::vector<const sl::BaseStructure*> inputs{&viewport};
        for (const sl::ResourceTag& tag : tags) {
            inputs.push_back(&tag);
        }
        const sl::Result result = streamline::Evaluate(sl::kFeatureDLSS_RR, token, inputs.data(), static_cast<uint32_t>(inputs.size()), d.cmd);
        if (result != sl::Result::eOk) {
            if (!error_logged_) {
                LogError("dlss (streamline): ray reconstruction slEvaluateFeature failed ({})", static_cast<int>(result));
                error_logged_ = true;
            }
            return false;
        }
        return true;
    }

    vk::Context& ctx_;
    sl::DLSSOptions options_{};
    sl::DLSSDOptions rr_options_{};
    bool rr_ = false;
    bool created_ = false;
    bool error_logged_ = false;
};

class DlssFrameGeneration final : public FrameGeneration {
public:
    explicit DlssFrameGeneration(vk::Context& ctx) : ctx_(ctx) {}
    ~DlssFrameGeneration() override { Shutdown(); }

    bool Available(std::string& reason) override {
        if (!ctx_.surface) {
            reason = "needs a window";
            return false;
        }
        if (!streamline::FrameGenSupported(&reason)) {
            reason = reason.empty() ? std::string("Streamline is not loaded") : std::format("Streamline: {}", reason);
            return false;
        }
        if (failed_) {
            reason = "DLSS Frame Generation stopped with an error, see pt.log";
            return false;
        }
        reason.clear();
        return true;
    }

    bool Update(bool wanted) override {
        std::string reason;
        wanted = wanted && Available(reason);
        if (!initialised_) {
            initialised_ = true;
            if (ctx_.surface != VK_NULL_HANDLE) {
                attachment_known_ = RefreshAttachment();
            } else {
                attachment_known_ = true;
            }
        }
        const bool vsync_off = wanted && streamline::FrameGenNeedsVsyncOff();
        const bool vsync_changed = vsync_off != ctx_.force_vsync_off;
        ctx_.force_vsync_off = vsync_off;
        target_ = wanted;
        generating_ = (wanted && attached_) || (preserve_active_mode_ && attached_ && mode_ == sl::DLSSGMode::eOn);
        return vsync_changed;
    }

    FrameStartAction FrameStart(bool swapchain_recreation_pending) override {
        if (!initialised_) {
            return FrameStartAction::Continue;
        }
        const bool menu_open = UpscaleHost::Get().MenuOpen();
        if (!attachment_known_) {
            const bool state_change_needed = target_ != attached_ || swapchain_recreation_pending ||
                                            (attached_ && menu_open && mode_ != sl::DLSSGMode::eOff);
            if (!state_change_needed) {
                return FrameStartAction::Continue;
            }
            if (!RefreshAttachment()) {
                LogAttachmentError();
                return KeepCurrentSwapchainOrFatal(
                    "Streamline could not report whether DLSS Frame Generation is loaded, and no safe swapchain remains. Restart P.T.");
            }
        }
        const bool transition_pending = target_ != attached_;

        if (attached_ && (transition_pending || swapchain_recreation_pending || (menu_open && mode_ != sl::DLSSGMode::eOff))) {
            if (!SetMode(sl::DLSSGMode::eOff)) {
                generating_ = attached_ && mode_ == sl::DLSSGMode::eOn;
                return KeepCurrentSwapchainOrFatal(
                    "DLSS Frame Generation could not be turned off before the swapchain change. Restart P.T. and try again.");
            }
            if (!target_) {
                generating_ = false;
            }
        }

        bool fsr_owner_changed = false;
        if (transition_pending) {
            FrameGeneration* fsr = UpscaleHost::Get().FrameGen();
            const auto* fsr_hook = fsr ? dynamic_cast<const vk::SwapchainHooks*>(fsr) : nullptr;
            if (fsr_hook && (ctx_.SwapchainOwner() == fsr_hook || ctx_.swapchain_hooks == fsr_hook)) {
                fsr_owner_changed = fsr->Update(false);
                if (ctx_.SwapchainOwner() == fsr_hook) {
                    vkDeviceWaitIdle(ctx_.device);
                    ctx_.DestroySwapchain();
                    fsr_owner_changed = true;
                }
                fsr->Update(false);
            }
            if (ctx_.SwapchainOwner() || ctx_.swapchain_hooks) {
                if (!owner_wait_logged_) {
                    LogWarn("frame generation: DLSS switch waiting for the active swapchain owner to detach");
                    owner_wait_logged_ = true;
                }
                return KeepCurrentSwapchainOrFatal(
                    "Another frame-generation backend still owns the swapchain during a DLSS transition. Restart P.T. and try again.");
            }
            owner_wait_logged_ = false;
        }

        if (menu_open && transition_pending) {
            if (swapchain_recreation_pending || fsr_owner_changed) {
                vkDeviceWaitIdle(ctx_.device);
                DestroyHudless(false);
                return fsr_owner_changed ? FrameStartAction::RecreateSwapchain : FrameStartAction::Continue;
            }
            return FrameStartAction::Continue;
        }

        if (!transition_pending) {
            if (attached_ && swapchain_recreation_pending) {
                vkDeviceWaitIdle(ctx_.device);
                DestroyHudless(false);
            }
            return FrameStartAction::Continue;
        }

        const bool wanted = target_;
        vkDeviceWaitIdle(ctx_.device);
        DestroyHudless(false);
        ctx_.DestroySwapchain();
        if (!wanted) {
            streamline::FreeResources(sl::kFeatureDLSS_G);
        }
        const sl::Result result = streamline::SetFeatureLoaded(sl::kFeatureDLSS_G, wanted);
        if (result == sl::Result::eOk) {
            attached_ = wanted;
            attachment_known_ = true;
        } else {
            attachment_known_ = RefreshAttachment();
            LogError("frame generation: slSetFeatureLoaded({}) failed ({}){}", wanted ? "enabled" : "disabled",
                     static_cast<int>(result), attachment_known_ ? "" : "; attachment state is unknown");
        }
        generating_ = target_ && attached_;
        mode_ = sl::DLSSGMode::eOff;
        if (wanted && !attached_) {
            Fail("sl.dlss_g hooks could not be enabled");
        }
        LogInfo("frame generation: DLSS Frame Generation transition requested {} (sl.dlss_g hooks {}: {}), attached {}", wanted ? "on" : "off",
                wanted ? "enabled" : "disabled", static_cast<int>(result), attached_ ? "yes" : "no");

        if (!attachment_known_) {
            return KeepCurrentSwapchainOrFatal(
                "Streamline could not confirm the DLSS Frame Generation load state after swapchain teardown. Restart P.T.");
        }
        if (wanted && !attached_) {
            return FrameStartAction::RecreateSwapchain;
        }
        if (attached_ != wanted) {
            return KeepCurrentSwapchainOrFatal(
                "Streamline did not apply the DLSS Frame Generation hook-state change after swapchain teardown. Restart P.T.");
        }
        return FrameStartAction::RecreateSwapchain;
    }

    bool SwapchainFailed() override {
        if (!attached_) {
            return false;
        }
        Fail("the swapchain was refused");
        if (!SetMode(sl::DLSSGMode::eOff)) {
            generating_ = false;
            if (!ctx_.swapchain.handle) {
                FatalError("DLSS Frame Generation could not be turned off after swapchain creation failed, and no usable swapchain remains. Restart P.T.",
                           ctx_.surface != VK_NULL_HANDLE);
            }
            return true;
        }
        vkDeviceWaitIdle(ctx_.device);
        DestroyHudless(false);
        ctx_.DestroySwapchain();
        streamline::FreeResources(sl::kFeatureDLSS_G);
        const sl::Result result = streamline::SetFeatureLoaded(sl::kFeatureDLSS_G, false);
        if (result == sl::Result::eOk) {
            attached_ = false;
            attachment_known_ = true;
        } else {
            attachment_known_ = RefreshAttachment();
            LogError("frame generation: could not disable DLSS-G hooks after swapchain refusal ({}){}", static_cast<int>(result),
                     attachment_known_ ? "" : "; attachment state is unknown");
        }
        generating_ = false;
        if (!attachment_known_ || attached_) {
            FatalError("DLSS Frame Generation could not be detached after swapchain creation failed, and no usable swapchain remains. Restart P.T.",
                       ctx_.surface != VK_NULL_HANDLE);
        }
        return true;
    }

    bool Generating() const override { return generating_; }

    void Prepare(const FrameGenPrepare& p) override {
        if (!generating_) {
            return;
        }
        sl::Resource depth = SlImage(p.depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        sl::Resource motion = SlImage(p.motion, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const sl::Extent render{0, 0, p.render.width, p.render.height};
        const sl::ResourceTag tags[] = {
            sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &render),
            sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &render),
        };
        const sl::Result result = streamline::Tag(tags, static_cast<uint32_t>(std::size(tags)), p.cmd);
        prepared_ = result == sl::Result::eOk && streamline::ConstantsSet();
        if (result != sl::Result::eOk && !error_logged_) {
            LogError("frame generation: DLSS Frame Generation input tags failed ({})", static_cast<int>(result));
            error_logged_ = true;
        }
    }

    uint32_t off_failures_ = 0;

    const vk::Image* Present(uint32_t image_index) override {
        if (!attached_) {
            prepared_ = false;
            return nullptr;
        }
        const bool keep_previous_mode = preserve_active_mode_ && mode_ == sl::DLSSGMode::eOn;
        bool generate = generating_ && prepared_ && (!UpscaleHost::Get().MenuOpen() || keep_previous_mode);
        if (!generate) {
            const bool has_current_inputs = prepared_;
            if (!SetMode(sl::DLSSGMode::eOff)) {
                if (mode_ == sl::DLSSGMode::eOn && has_current_inputs) {
                    generating_ = true;
                    preserve_active_mode_ = true;
                    generate = true;
                } else if (mode_ == sl::DLSSGMode::eOn) {
                    if (++off_failures_ <= 300) {
                        if (off_failures_ == 1) LogError("frame generation: could not be turned off, retrying every frame");
                    } else {
                        FatalError("DLSS Frame Generation remained on without current depth and motion inputs, and could not be turned off. Restart P.T.",
                                   ctx_.surface != VK_NULL_HANDLE);
                    }
                }
            } else {
                off_failures_ = 0;
            }
            if (!generate) {
                prepared_ = false;
                return nullptr;
            }
        }
        prepared_ = false;
        if (hudless_.size() != ctx_.swapchain.images.size() || extent_.width != ctx_.swapchain.extent.width ||
            extent_.height != ctx_.swapchain.extent.height || format_ != ctx_.swapchain.format) {
            CreateHudless();
        }
        if (image_index >= hudless_.size()) {
            SetModeOffOrFatal(
                "DLSS Frame Generation remained on without a valid HUD-less target. Restart P.T. and try again.");
            return nullptr;
        }
        const vk::Image& hud = hudless_[image_index];
        UpscaleImage image;
        image.image = hud.image;
        image.view = hud.view;
        image.format = hud.format;
        image.extent = {hud.extent.width, hud.extent.height};
        image.usage = hud.usage;
        sl::Resource resource = SlImage(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const sl::Extent full{0, 0, image.extent.width, image.extent.height};
        const sl::ResourceTag tag(&resource, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &full);
        const sl::Result tag_result = streamline::Tag(&tag, 1, VK_NULL_HANDLE);
        if (tag_result != sl::Result::eOk) {
            if (!error_logged_) {
                LogError("frame generation: DLSS HUD-less input tagging failed ({})", static_cast<int>(tag_result));
                error_logged_ = true;
            }
            SetModeOffOrFatal(
                "DLSS Frame Generation could not tag its HUD-less target, and could not safely turn the feature off. Restart P.T.");
            return nullptr;
        }
        SetMode(sl::DLSSGMode::eOn);
        CheckState();
        return &hud;
    }

    void Shutdown() override {
        if (!ctx_.device) {
            return;
        }
        if (attached_) {
            SetMode(sl::DLSSGMode::eOff);
        }
        DestroyHudless();
        generating_ = false;
        LogSummary();
    }

private:
    void SetModeOffOrFatal(const char* fatal_reason) {
        if (!SetMode(sl::DLSSGMode::eOff) && mode_ == sl::DLSSGMode::eOn) {
            FatalError(fatal_reason, ctx_.surface != VK_NULL_HANDLE);
        }
    }

    FrameStartAction KeepCurrentSwapchainOrFatal(const char* fatal_reason) {
        if (ctx_.swapchain.handle) {
            generating_ = attached_ && mode_ == sl::DLSSGMode::eOn;
            preserve_active_mode_ = generating_;
            return FrameStartAction::KeepCurrentSwapchain;
        }
        FatalError(fatal_reason, ctx_.surface != VK_NULL_HANDLE);
    }

    void Fail(const char* reason) {
        LogError("frame generation: DLSS Frame Generation turned off for this run: {}", reason);
        failed_ = true;
        target_ = false;
        UpscaleHost::Get().SetDlssFrameGenFailed(true);
    }

    bool RefreshAttachment() {
        bool loaded = attached_;
        if (!streamline::IsFrameGenLoaded(loaded)) {
            attachment_known_ = false;
            return false;
        }
        attached_ = loaded;
        attachment_known_ = true;
        return true;
    }

    void LogAttachmentError() {
        if (!attachment_error_logged_) {
            LogError("frame generation: Streamline DLSS-G attachment state could not be queried; swapchain work is deferred");
            attachment_error_logged_ = true;
        }
    }

    bool SetMode(sl::DLSSGMode mode) {
        if (mode == mode_) {
            return true;
        }
        sl::DLSSGOptions options{};
        options.mode = mode;
        options.numFramesToGenerate = 1;
        options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
        options.numBackBuffers = static_cast<uint32_t>(ctx_.swapchain.images.size());
        options.colorWidth = ctx_.swapchain.extent.width;
        options.colorHeight = ctx_.swapchain.extent.height;
        options.colorBufferFormat = static_cast<uint32_t>(ctx_.swapchain.format);
        options.hudLessBufferFormat = static_cast<uint32_t>(ctx_.swapchain.format);
        options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
        options.onErrorCallback = &DlssFrameGeneration::ApiError;
        const sl::Result result = slDLSSGSetOptions(sl::ViewportHandle(streamline::kViewport), options);
        if (result != sl::Result::eOk) {
            if (mode == sl::DLSSGMode::eOff && attached_ && mode_ == sl::DLSSGMode::eOn) {
                preserve_active_mode_ = true;
            }
            if (!mode_error_logged_) {
                LogError("frame generation: slDLSSGSetOptions({}) failed ({}); the previous mode remains active",
                         mode == sl::DLSSGMode::eOn ? "on" : "off", static_cast<int>(result));
                mode_error_logged_ = true;
            }
            return false;
        }
        LogInfo("frame generation: DLSS Frame Generation mode {}", mode == sl::DLSSGMode::eOn ? "on" : "off");
        mode_ = mode;
        preserve_active_mode_ = false;
        return true;
    }

    void CheckState() {
        sl::DLSSGState state{};
        if (slDLSSGGetState(sl::ViewportHandle(streamline::kViewport), state, nullptr) != sl::Result::eOk) {
            return;
        }
        presented_ += state.numFramesActuallyPresented;
        ++rendered_;
        if (state.status != sl::DLSSGStatus::eOk) {
            LogError("frame generation: DLSS Frame Generation status {:#x}, turned off", static_cast<uint32_t>(state.status));
            SetMode(sl::DLSSGMode::eOff);
            failed_ = true;
            generating_ = false;
        }
    }

    static void ApiError(const sl::APIError& error) {
        static int logged = 0;
        if (logged++ < 5) {
            LogWarn("frame generation: DLSS Frame Generation present/acquire returned {}", static_cast<int>(error.vkRes));
        }
    }

    void CreateHudless() {
        DestroyHudless();
        extent_ = ctx_.swapchain.extent;
        format_ = ctx_.swapchain.format;
        hudless_.resize(ctx_.swapchain.images.size());
        for (vk::Image& image : hudless_) {
            if (!ctx_.CreateImage(image, format_, {extent_.width, extent_.height, 1},
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
                LogError("frame generation: hud-less target creation failed");
                DestroyHudless();
                return;
            }
        }
        LogInfo("frame generation: DLSS Frame Generation for {}x{}, {} hud-less targets", extent_.width, extent_.height, hudless_.size());
    }

    void DestroyHudless(bool clear_tag = true) {
        if (hudless_.empty()) {
            return;
        }
        vkDeviceWaitIdle(ctx_.device);
        if (clear_tag) {
            const sl::ResourceTag tag(nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent);
            streamline::Tag(&tag, 1, VK_NULL_HANDLE);
        }
        for (vk::Image& image : hudless_) {
            ctx_.DestroyImage(image);
        }
        hudless_.clear();
    }

    void LogSummary() {
        if (rendered_ > 0) {
            LogInfo("frame generation: DLSS Frame Generation presented {} frames for {} rendered", presented_, rendered_);
            rendered_ = presented_ = 0;
        }
    }

    vk::Context& ctx_;
    std::vector<vk::Image> hudless_;
    VkExtent2D extent_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    sl::DLSSGMode mode_ = sl::DLSSGMode::eOff;
    bool initialised_ = false;
    bool target_ = false;
    bool attached_ = false;
    bool attachment_known_ = false;
    bool generating_ = false;
    bool owner_wait_logged_ = false;
    bool prepared_ = false;
    bool failed_ = false;
    bool error_logged_ = false;
    bool mode_error_logged_ = false;
    bool preserve_active_mode_ = false;
    bool attachment_error_logged_ = false;
    uint64_t rendered_ = 0;
    uint64_t presented_ = 0;
};

}

std::unique_ptr<UpscaleBackend> CreateStreamlineDlssBackend(vk::Context& ctx) {
    return std::make_unique<SlDlssBackend>(ctx);
}

std::unique_ptr<FrameGeneration> CreateDlssFrameGeneration(vk::Context& ctx) {
    return streamline::Active() ? std::make_unique<DlssFrameGeneration>(ctx) : nullptr;
}

}

#else

#include "engine/render/upscale/frame_generation.h"
#include "engine/render/upscale/upscale.h"

namespace pt::streamline {

bool Start(const std::filesystem::path&, std::string& reason, Api) {
    reason = "this build has no Streamline";
    return false;
}
bool Active() { return false; }
bool ActiveD3D12() { return false; }
void DeviceCreatedD3D12(void*, const uint8_t (&)[8]) {}
bool UpgradeInterface(void**) { return false; }
bool D3D12FrameGenTag(const D3D12FrameGenTags&) { return false; }
void D3D12FrameGenUntag() {}
bool D3D12FrameGenSetOptions(const D3D12FrameGenOptions&, std::string* error) {
    if (error) *error = "this build has no Streamline";
    return false;
}
bool D3D12FrameGenGetState(D3D12FrameGenState& state) {
    state = D3D12FrameGenState{};
    return false;
}
void D3D12FrameGenFree() {}
void D3D12EndFrame() {}
void D3D12SetReflex(bool) {}
PFN_vkGetInstanceProcAddr InstanceProcAddr() { return nullptr; }
void DeviceCreated(VkInstance, VkPhysicalDevice, VkDevice) {}
void Shutdown() {}
bool DlssSupported() { return false; }
bool RayReconstructionSupported(std::string* reason) {
    if (reason) *reason = "this build has no Streamline";
    return false;
}
bool FrameGenSupported(std::string* reason) {
    if (reason) *reason = "this build has no Streamline";
    return false;
}
bool FrameGenNeedsVsyncOff() { return false; }
bool IsFrameGenLoaded(bool&) { return false; }
bool DisableFrameGenFeature() { return false; }
void BeginFrame() {}
void SetMarker(Marker) {}
void SetConstants(const FrameConstants&) {}
bool ConstantsSet() { return false; }
void SetFrameLimit(uint32_t) {}
const void* FrameToken() { return nullptr; }

}

namespace pt {

std::unique_ptr<UpscaleBackend> CreateStreamlineDlssBackend(vk::Context&) {
    return nullptr;
}

std::unique_ptr<FrameGeneration> CreateDlssFrameGeneration(vk::Context&) {
    return nullptr;
}

}

#endif
