#include "engine/render/d3d12/backend/fsr_ray_regeneration.h"
#include "engine/render/d3d12/backend/d3d12_native.h"
#include "engine/core/log.h"
#include "engine/render/upscale/upscale_platform.h"
#include <cstring>
#include <cstdlib>
#include <format>
#include <filesystem>
#include <vector>

#if defined(PT_WITH_FSR_RR)
#include <windows.h>
#include <ffx_api_loader.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_denoiser.h>
#endif

namespace pt::gpu::d3d12 {

struct FsrRayRegeneration::Impl {
    explicit Impl(gpu::Device& d) : device(d) {}
    gpu::Device& device;
    std::string reason;
    bool checked = false;
    bool supported = false;
    bool failed = false;
    bool dispatched = false;
#if defined(PT_WITH_FSR_RR)
    HMODULE module = nullptr;
    ffxFunctions fn{};
    ffxContext context = nullptr;
    uint64_t version = 0;
    std::string version_name;
    glm::vec3 previous_eye{};
    uint32_t previous_frame = 0;

    FfxApiResource Resource(const UpscaleImage& image, bool output = false) {
        if (!image.handle) return {};
        const auto state = TrackedState(image.handle);
        uint32_t ffx_state = FFX_API_RESOURCE_STATE_COMMON;
        if (state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ffx_state = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
        else if ((state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) && (state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            ffx_state = FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ;
        else if (state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ffx_state = FFX_API_RESOURCE_STATE_COMPUTE_READ;
        else if (state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ffx_state = FFX_API_RESOURCE_STATE_PIXEL_READ;
        return ffxApiGetResourceDX12(Native(image.handle), ffx_state, output ? FFX_API_RESOURCE_USAGE_UAV : 0);
    }
#endif
};

FsrRayRegeneration::FsrRayRegeneration(gpu::Device& device) : impl_(std::make_unique<Impl>(device)) {}
FsrRayRegeneration::~FsrRayRegeneration() {
    Release();
#if defined(PT_WITH_FSR_RR)
    if (impl_->module) FreeLibrary(impl_->module);
#endif
}

bool FsrRayRegeneration::Available(std::string& reason) {
    auto& p = *impl_;
    if (!p.checked) {
        p.checked = true;
        p.reason = "FSR Ray Regeneration needs an AMD Radeon GPU with DirectX 12";
#if defined(PT_WITH_FSR_RR)
        if (p.device.Info().vendor_id == 0x1002) {
            p.reason = "AMD's driver offers no FSR Ray Regeneration for this GPU (AMD lists the Radeon RX 9000 series and Windows 11)";
            const auto path = ExecutableDir() / L"amd_fidelityfx_loader_dx12.dll";
            if (!std::filesystem::exists(ExecutableDir() / L"amd_fidelityfx_denoiser_dx12.dll")) {
                p.reason = "amd_fidelityfx_denoiser_dx12.dll is missing next to pt.exe";
            } else {
                p.module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if (p.module) {
                    ffxLoadFunctions(&p.fn, p.module);
                    if (p.fn.Query && p.fn.CreateContext && p.fn.DestroyContext && p.fn.Dispatch) {
                        uint64_t count = 0;
                        ffxQueryDescGetVersions q{};
                        q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
                        q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_DENOISER;
                        q.device = NativeDevice(p.device);
                        q.outputCount = &count;
                        const auto counted = p.fn.Query(nullptr, &q.header);
                        if (counted != FFX_API_RETURN_OK || !count) {
                            LogInfo("fsr rr: amd_fidelityfx_denoiser_dx12.dll providers for this GPU: none (query {})", counted);
                        }
                        if (counted == FFX_API_RETURN_OK && count && count < 256) {
                            std::vector<uint64_t> ids(count);
                            std::vector<const char*> names(count);
                            q.versionIds = ids.data(); q.versionNames = names.data();
                            if (p.fn.Query(nullptr, &q.header) == FFX_API_RETURN_OK) {
                                std::string listed;
                                for (uint64_t i = 0; i < count; ++i) listed += std::format("{}{}", i ? ", " : "", names[i] ? names[i] : "?");
                                LogInfo("fsr rr: amd_fidelityfx_denoiser_dx12.dll providers for this GPU: {}", listed);
                                p.version = ids[0];
                                p.version_name = names[0] ? names[0] : "1.2.0";
                                p.supported = true;
                                p.reason.clear();
                            }
                        }
                    }
                } else p.reason = std::format("FSR Ray Regeneration loader failed ({})", GetLastError());
            }
        }
#else
        p.reason = "FSR Ray Regeneration was not built";
#endif
        LogInfo("fsr rr: {}", p.supported ? "provider available, checked again at context creation" : p.reason);
    }
    reason = p.reason;
    return p.supported && !p.failed;
}

bool FsrRayRegeneration::Create(VkExtent2D size) {
    Release();
    auto& p = *impl_;
    std::string reason;
    if (!Available(reason)) return false;
#if defined(PT_WITH_FSR_RR)
    ffxCreateContextDescDenoiser c{};
    c.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_DENOISER;
    c.version = FFX_DENOISER_VERSION;
    c.maxRenderSize = {size.width, size.height};
    c.signalFlags = FFX_DENOISER_SIGNAL_DIRECT_DIFFUSE | FFX_DENOISER_SIGNAL_DIRECT_SPECULAR |
                    FFX_DENOISER_SIGNAL_INDIRECT_DIFFUSE | FFX_DENOISER_SIGNAL_INDIRECT_SPECULAR;
    if (std::getenv("PT_UPSCALE_CHECK")) c.flags = FFX_DENOISER_ENABLE_VALIDATION;
    ffxCreateBackendDX12Desc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = NativeDevice(p.device);
    ffxOverrideVersion version{};
    version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    version.versionId = p.version;
    c.header.pNext = &backend.header;
    backend.header.pNext = &version.header;
    const auto result = p.fn.CreateContext(&p.context, &c.header, nullptr);
    if (result != FFX_API_RETURN_OK || !p.context) {
        p.reason = std::format("FSR Ray Regeneration context creation failed ({})", result);
        p.failed = true;
        if (p.context) p.fn.DestroyContext(&p.context, nullptr);
        p.context = nullptr;
        LogWarn("fsr rr: {}; reverting to the port denoiser", p.reason);
        return false;
    }
    p.dispatched = false;
    LogInfo("fsr rr: Ray Regeneration {} created at {}x{}, four radiance signals", p.version_name, size.width, size.height);
    return true;
#else
    return false;
#endif
}

bool FsrRayRegeneration::Dispatch(const RayDenoiseDispatch& d) {
#if defined(PT_WITH_FSR_RR)
    auto& p = *impl_;
    if (!p.context || p.failed || !d.list) return false;
    ffxDispatchDescDenoiser r{};
    r.header.type = FFX_API_DISPATCH_DESC_TYPE_DENOISER;
    r.commandList = Native(*d.list);
    r.linearDepth = p.Resource(d.depth);
    r.motionVectors = p.Resource(d.motion);
    r.normals = p.Resource(d.normal_roughness);
    r.diffuseAlbedo = p.Resource(d.diffuse_albedo);
    r.specularAlbedo = p.Resource(d.specular_albedo);
    r.motionVectorScale = {1.0f, 1.0f, 1.0f};
    r.jitterOffsets = {d.jitter.x, d.jitter.y};
    const bool reset = d.reset || !p.dispatched || d.frame_index != p.previous_frame + 1u ||
                       glm::distance(p.previous_eye, d.eye) > 2.0f;
    const glm::vec3 delta = reset ? glm::vec3(0.0f) : p.previous_eye - d.eye;
    r.cameraPositionDelta = {delta.x, delta.y, delta.z};
    std::memcpy(&r.view, &d.world_to_view, sizeof(r.view));
    std::memcpy(&r.projection, &d.view_to_clip, sizeof(r.projection));
    r.linearDepthBounds = {d.near_plane, 200.0f};
    r.renderSize = {d.render.width, d.render.height};
    r.frameIndex = d.frame_index;
    r.flags = FFX_DENOISER_DISPATCH_NON_GAMMA_ALBEDO | (reset ? FFX_DENOISER_DISPATCH_RESET : 0u);
    ffxDispatchDescDenoiserDirectDiffuse dd{};
    ffxDispatchDescDenoiserDirectSpecular ds{};
    ffxDispatchDescDenoiserIndirectDiffuse id{};
    ffxDispatchDescDenoiserIndirectSpecular is{};
    dd.header.type = FFX_API_DISPATCH_DESC_TYPE_DENOISER_DIRECT_DIFFUSE;
    ds.header.type = FFX_API_DISPATCH_DESC_TYPE_DENOISER_DIRECT_SPECULAR;
    id.header.type = FFX_API_DISPATCH_DESC_TYPE_DENOISER_INDIRECT_DIFFUSE;
    is.header.type = FFX_API_DISPATCH_DESC_TYPE_DENOISER_INDIRECT_SPECULAR;
    r.header.pNext = &dd.header; dd.header.pNext = &ds.header; ds.header.pNext = &id.header; id.header.pNext = &is.header;
    FfxApiDenoiserSignal* signals[] = {&dd.signal, &ds.signal, &id.signal, &is.signal};
    for (unsigned i = 0; i < 4; ++i) {
        signals[i]->input = p.Resource(d.input[i]);
        signals[i]->output = p.Resource(d.output[i], true);
    }
    const auto result = p.fn.Dispatch(&p.context, &r.header);
    ResumeAfterNative(*d.list);
    if (result != FFX_API_RETURN_OK) {
        p.failed = true;
        p.reason = std::format("FSR Ray Regeneration dispatch failed ({})", result);
        LogError("fsr rr: {}; next frame uses the port denoiser", p.reason);
        return false;
    }
    if (!p.dispatched) LogInfo("fsr rr: first denoising dispatch succeeded");
    p.dispatched = true;
    p.previous_eye = d.eye;
    p.previous_frame = d.frame_index;
    return true;
#else
    return false;
#endif
}

void FsrRayRegeneration::Release() {
#if defined(PT_WITH_FSR_RR)
    auto& p = *impl_;
    if (p.context) {
        p.device.WaitIdle();
        p.fn.DestroyContext(&p.context, nullptr);
        p.context = nullptr;
    }
    p.dispatched = false;
#endif
}

void FsrRayRegeneration::Disable(const std::string& reason) {
    impl_->failed = true;
    impl_->reason = reason;
    LogWarn("fsr rr: {}; reverting to the port denoiser", reason);
}

}
