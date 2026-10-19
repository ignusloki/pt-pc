#ifdef _WIN32

#include "engine/render/d3d12/backend/d3d12_internal.h"

#include <cfloat>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <vector>

#include "engine/core/log.h"
#include "engine/render/upscale/upscale.h"
#include "engine/render/upscale/upscale_platform.h"

#if defined(PT_WITH_FSR)
#include <ffx_api/dx12/ffx_api_dx12.h>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_framegeneration.h>
#endif

namespace pt::gpu::d3d12 {

#if defined(PT_WITH_FSR)
namespace {

using pt::d3d12::FrameInputs;
using pt::d3d12::LatencyMarker;
using pt::d3d12::PresentResult;
using pt::d3d12::SwapchainDesc;
using Clock = std::chrono::steady_clock;

constexpr uint32_t kAmdVendorId = 0x1002;

class FsrFgPresenter final : public pt::d3d12::SwapchainOwner {
public:
    explicit FsrFgPresenter(pt::d3d12::Device& device) : device_(device) {
        if (std::getenv("PT_FG_DEBUG")) {
            debug_flags_ = FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES | FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;
        }
        only_generated_ = std::getenv("PT_FG_ONLY_GENERATED") != nullptr;
    }
    ~FsrFgPresenter() override {
        Destroy();
        if (module_) {
            FreeLibrary(module_);
        }
    }

    bool Load(std::string& error) {
        if (module_) {
            return true;
        }
        module_ = LoadLibraryW((ExecutableDir() / L"amd_fidelityfx_dx12.dll").c_str());
        if (!module_) {
            error = "amd_fidelityfx_dx12.dll is missing next to pt.exe";
            return false;
        }
        ffxLoadFunctions(&fn_, module_);
        if (!fn_.CreateContext || !fn_.DestroyContext || !fn_.Dispatch || !fn_.Query || !fn_.Configure) {
            error = "amd_fidelityfx_dx12.dll has no FidelityFX API exports";
            return false;
        }
        uint64_t count = 0;
        ffxQueryDescGetVersions versions{};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        versions.device = device_.Get();
        versions.outputCount = &count;
        if (fn_.Query(nullptr, &versions.header) != FFX_API_RETURN_OK || count == 0) {
            error = "no FSR frame generation in amd_fidelityfx_dx12.dll";
            return false;
        }
        std::vector<uint64_t> ids(count);
        std::vector<const char*> names(count);
        versions.versionIds = ids.data();
        versions.versionNames = names.data();
        if (fn_.Query(nullptr, &versions.header) == FFX_API_RETURN_OK && count > 0) {
            LogInfo("fsr fg d3d12: amd_fidelityfx_dx12.dll frame generation {}", names[0] ? names[0] : "?");
        }
        return true;
    }

    bool Create(const SwapchainDesc& desc, std::string& error) override {
        Destroy();
        if (!Load(error)) {
            return false;
        }
        const UINT flags = device_.TearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        DXGI_SWAP_CHAIN_DESC1 sc{};
        sc.Width = desc.width;
        sc.Height = desc.height;
        sc.Format = desc.format;
        sc.SampleDesc.Count = 1;
        sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.BufferCount = desc.buffer_count;
        sc.Scaling = DXGI_SCALING_STRETCH;
        sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sc.Flags = flags;
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};
        fullscreen.Windowed = TRUE;
        IDXGISwapChain4* swapchain = nullptr;
        ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 create{};
        create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
        create.swapchain = &swapchain;
        create.hwnd = desc.window;
        create.desc = &sc;
        create.fullscreenDesc = &fullscreen;
        create.dxgiFactory = device_.Factory();
        create.gameQueue = device_.Queue();
        ffxReturnCode_t r = fn_.CreateContext(&swapchain_context_, &create.header, nullptr);
        if (r != FFX_API_RETURN_OK || !swapchain) {
            error = std::format("the FSR frame generation swapchain could not be created ({})", r);
            swapchain_context_ = nullptr;
            return false;
        }
        swapchain_.Attach(swapchain);
        device_.Factory()->MakeWindowAssociation(desc.window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        desc_ = desc;
        swapchain_flags_ = flags;
        buffers_.assign(desc.buffer_count, nullptr);
        for (uint32_t i = 0; i < desc.buffer_count; ++i) {
            if (FAILED(swapchain_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i])))) {
                error = std::format("the FSR frame generation swapchain gave no back buffer {}", i);
                Destroy();
                return false;
            }
        }
        ffxCreateBackendDX12Desc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backend.device = device_.Get();
        ffxCreateContextDescFrameGenerationHudless hudless{};
        hudless.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
        hudless.hudlessBackBufferFormat = ffxApiGetSurfaceFormatDX12(desc.format);
        backend.header.pNext = &hudless.header;
        ffxCreateContextDescFrameGeneration fg{};
        fg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        fg.header.pNext = &backend.header;
        fg.flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
        if (std::getenv("PT_UPSCALE_CHECK")) {
            fg.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
        }
        fg.displaySize = {desc.width, desc.height};
        fg.maxRenderSize = fg.displaySize;
        fg.backBufferFormat = ffxApiGetSurfaceFormatDX12(desc.format);
        r = fn_.CreateContext(&fg_context_, &fg.header, nullptr);
        if (r != FFX_API_RETURN_OK) {
            error = std::format("the FSR frame generation context could not be created ({})", r);
            fg_context_ = nullptr;
            Destroy();
            return false;
        }
        if (!fence_ && FAILED(device_.Get()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
            error = "the present fence could not be created";
            Destroy();
            return false;
        }
        recorders_.resize(desc.buffer_count + 1);
        for (Recorder& rec : recorders_) {
            if (!rec.allocator) {
                device_.Get()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&rec.allocator));
                if (rec.allocator) {
                    device_.Get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, rec.allocator.Get(), nullptr, IID_PPV_ARGS(&rec.list));
                }
                if (!rec.list) {
                    error = "the present command lists could not be created";
                    Destroy();
                    return false;
                }
                rec.list->Close();
            }
        }
        reset_ = true;
        LogInfo("fsr fg d3d12: FSR 3 frame generation swapchain {}x{}, {} buffers, format {}, v-sync {}, tearing {}, {}", desc.width, desc.height,
                desc.buffer_count, static_cast<int>(desc.format), desc.vsync ? "on" : "off", flags ? "allowed" : "off",
                desc.ui_layer ? "film grain and UI laid over every frame" : "UI taken from the HUD-less difference");
        return true;
    }

    void Destroy() override {
        if (!swapchain_context_ && !fg_context_) {
            return;
        }
        LogStats("destroyed");
        if (swapchain_context_) {
            ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait{};
            wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
            fn_.Dispatch(&swapchain_context_, &wait.header);
        }
        device_.WaitIdle();
        if (fg_context_) {
            fn_.DestroyContext(&fg_context_, nullptr);
            fg_context_ = nullptr;
        }
        if (swapchain_context_ && ui_registered_) {
            RegisterUi(nullptr);
        }
        buffers_.clear();
        swapchain_.Reset();
        if (swapchain_context_) {
            fn_.DestroyContext(&swapchain_context_, nullptr);
            swapchain_context_ = nullptr;
        }
        ui_registered_ = false;
    }

    bool Alive() const override { return swapchain_ != nullptr; }
    void FrameStart(uint32_t) override {}
    void Marker(LatencyMarker, uint32_t) override {}
    uint32_t Acquire() override { return swapchain_ ? swapchain_->GetCurrentBackBufferIndex() : 0; }
    ID3D12Fence* ConsumedFence() const override { return fence_.Get(); }
    D3D12_RESOURCE_STATES InputState() const override { return D3D12_RESOURCE_STATE_COMMON; }
    void SetGenerating(bool on) override {
        if (on != enabled_) {
            LogInfo("fsr fg d3d12: frame generation {}", on ? "on" : "paused (menu)");
        }
        enabled_ = on;
    }

    PresentResult Present(const FrameInputs& in, uint64_t& consumed_value) override {
        consumed_value = fence_value_;
        if (!swapchain_ || !in.color) {
            return PresentResult::Failed;
        }
        ID3D12CommandQueue* queue = device_.Queue();
        if (in.wait_fence) {
            queue->Wait(in.wait_fence, in.wait_value);
        }
        Recorder& rec = recorders_[next_recorder_];
        next_recorder_ = (next_recorder_ + 1) % recorders_.size();
        if (!device_.WaitFence(fence_.Get(), rec.value, 5000)) {
            LogError("fsr fg d3d12: the present command list of {} frames ago never finished", recorders_.size());
            return PresentResult::Lost;
        }
        rec.allocator->Reset();
        rec.list->Reset(rec.allocator.Get(), nullptr);
        ID3D12GraphicsCommandList* list = rec.list.Get();
        const bool generate = enabled_ && in.depth && in.motion && in.hudless && in.input_width > 0 && in.input_height > 0 &&
                              in.input_width <= desc_.width && in.input_height <= desc_.height;
        const bool layer = generate && in.ui && desc_.ui_layer;
        const uint32_t index = swapchain_->GetCurrentBackBufferIndex();
        ID3D12Resource* back = index < buffers_.size() ? buffers_[index].Get() : nullptr;
        if (back) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = back;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            list->ResourceBarrier(1, &barrier);
            list->CopyResource(back, layer ? in.hudless : in.color);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(1, &barrier);
        }
        bool prepared = false;
        if (generate) {
            ffxDispatchDescFrameGenerationPrepareCameraInfo camera{};
            camera.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
            for (int i = 0; i < 3; ++i) {
                camera.cameraPosition[i] = in.camera.position[i];
                camera.cameraUp[i] = in.camera.up[i];
                camera.cameraRight[i] = in.camera.right[i];
                camera.cameraForward[i] = in.camera.forward[i];
            }
            ffxDispatchDescFrameGenerationPrepare d{};
            d.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
            d.header.pNext = &camera.header;
            d.frameID = frame_id_;
            d.flags = debug_flags_;
            d.commandList = list;
            d.renderSize = {in.input_width, in.input_height};
            d.jitterOffset = {in.constants.jitter[0], in.constants.jitter[1]};
            d.motionVectorScale = {in.constants.motion_scale[0], in.constants.motion_scale[1]};
            d.frameTimeDelta = in.constants.frame_ms;
            d.cameraNear = FLT_MAX;
            d.cameraFar = in.camera.near_plane;
            d.cameraFovAngleVertical = in.camera.fov_y;
            d.viewSpaceToMetersFactor = 1.0f;
            d.depth = ffxApiGetResourceDX12(in.depth, FFX_API_RESOURCE_STATE_COMMON);
            d.depth.description.usage = FFX_API_RESOURCE_USAGE_DEPTHTARGET;
            d.depth.description.format = FFX_API_SURFACE_FORMAT_R32_FLOAT;
            d.motionVectors = ffxApiGetResourceDX12(in.motion, FFX_API_RESOURCE_STATE_COMMON);
            const ffxReturnCode_t r = fn_.Dispatch(&fg_context_, &d.header);
            prepared = r == FFX_API_RETURN_OK;
            if (!prepared && !error_logged_) {
                LogError("fsr fg d3d12: prepare failed ({})", r);
                error_logged_ = true;
            }
            if (in.constants.reset) {
                reset_ = true;
            }
        }
        list->Close();
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        ++fence_value_;
        queue->Signal(fence_.Get(), fence_value_);
        rec.value = fence_value_;
        consumed_value = fence_value_;
        const bool generating = generate && prepared;
        RegisterUi(generating && layer ? in.ui : nullptr);
        ffxConfigureDescFrameGeneration c{};
        c.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        c.swapChain = swapchain_.Get();
        c.frameGenerationCallback = &FsrFgPresenter::Generate;
        c.frameGenerationCallbackUserContext = this;
        c.frameGenerationEnabled = generating;
        c.allowAsyncWorkloads = false;
        if (generating && !layer) {
            c.HUDLessColor = ffxApiGetResourceDX12(in.hudless, FFX_API_RESOURCE_STATE_COMMON);
        }
        c.flags = debug_flags_;
        c.onlyPresentGenerated = only_generated_;
        c.generationRect = {0, 0, static_cast<int32_t>(desc_.width), static_cast<int32_t>(desc_.height)};
        c.frameID = frame_id_;
        const ffxReturnCode_t cr = fn_.Configure(&fg_context_, &c.header);
        if (cr != FFX_API_RETURN_OK && !error_logged_) {
            LogError("fsr fg d3d12: configure failed ({})", cr);
            error_logged_ = true;
        }
        if (!generating) {
            reset_ = true;
        }
        ++frame_id_;
        const bool tear = !desc_.vsync && (swapchain_flags_ & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
        const HRESULT hr = swapchain_->Present(desc_.vsync ? 1 : 0, tear ? DXGI_PRESENT_ALLOW_TEARING : 0);
        device_.DrainDebugMessages();
        ++presents_;
        if (generating) {
            ++with_inputs_;
        }
        if (Clock::now() - window_start_ > std::chrono::seconds(5)) {
            LogStats("last 5 s");
        }
        if (hr == S_OK) {
            return PresentResult::Ok;
        }
        if (hr == DXGI_STATUS_OCCLUDED) {
            ++occluded_;
            return PresentResult::Occluded;
        }
        if (present_errors_++ < 8) {
            LogWarn("fsr fg d3d12: Present returned {:#010x}", static_cast<uint32_t>(hr));
        }
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || device_.Removed()) {
            return PresentResult::Lost;
        }
        return PresentResult::Failed;
    }

    void LogStats(const char* why) override {
        if (presents_ > 0) {
            LogInfo("fsr fg d3d12: {}: {} rendered frames presented, {} with generation inputs, {} generated, {} occluded", why, presents_,
                    with_inputs_, generated_, occluded_);
        }
        presents_ = with_inputs_ = generated_ = occluded_ = 0;
        window_start_ = Clock::now();
    }

private:
    struct Recorder {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        uint64_t value = 0;
    };

    static ffxReturnCode_t Generate(ffxDispatchDescFrameGeneration* params, void* user) {
        auto* self = static_cast<FsrFgPresenter*>(user);
        if (self->reset_) {
            params->reset = true;
            self->reset_ = false;
        }
        ++self->generated_;
        return self->fn_.Dispatch(&self->fg_context_, &params->header);
    }

    void RegisterUi(ID3D12Resource* ui) {
        if (!swapchain_context_ || (!ui && !ui_registered_)) {
            return;
        }
        ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 desc{};
        desc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12;
        if (ui) {
            desc.uiResource = ffxApiGetResourceDX12(ui, FFX_API_RESOURCE_STATE_COMMON);
            desc.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA | FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING;
        }
        const ffxReturnCode_t r = fn_.Configure(&swapchain_context_, &desc.header);
        if (r != FFX_API_RETURN_OK && !ui_error_logged_) {
            LogError("fsr fg d3d12: UI resource registration failed ({})", r);
            ui_error_logged_ = true;
        }
        ui_registered_ = ui != nullptr && r == FFX_API_RETURN_OK;
    }

    pt::d3d12::Device& device_;
    HMODULE module_ = nullptr;
    ffxFunctions fn_{};
    ffxContext swapchain_context_ = nullptr;
    ffxContext fg_context_ = nullptr;
    ComPtr<IDXGISwapChain4> swapchain_;
    std::vector<ComPtr<ID3D12Resource>> buffers_;
    std::vector<Recorder> recorders_;
    size_t next_recorder_ = 0;
    ComPtr<ID3D12Fence> fence_;
    uint64_t fence_value_ = 0;
    SwapchainDesc desc_{};
    UINT swapchain_flags_ = 0;
    uint64_t frame_id_ = 1;
    uint32_t debug_flags_ = 0;
    bool only_generated_ = false;
    bool enabled_ = true;
    bool reset_ = true;
    bool ui_registered_ = false;
    bool error_logged_ = false;
    bool ui_error_logged_ = false;
    int present_errors_ = 0;
    uint64_t presents_ = 0;
    uint64_t with_inputs_ = 0;
    uint64_t generated_ = 0;
    uint64_t occluded_ = 0;
    Clock::time_point window_start_ = Clock::now();
};

}

bool FsrFrameGenAvailable(pt::d3d12::Device& device, std::string& reason) {
    if (!std::filesystem::exists(ExecutableDir() / L"amd_fidelityfx_dx12.dll")) {
        reason = "amd_fidelityfx_dx12.dll is missing next to pt.exe";
        return false;
    }
    DXGI_ADAPTER_DESC1 adapter{};
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
    if (device.Adapter() && SUCCEEDED(device.Adapter()->GetDesc1(&adapter)) && adapter.VendorId == kAmdVendorId &&
        SUCCEEDED(device.Get()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1))) &&
        AmdGcnGpu(adapter.VendorId, options1.WaveLaneCountMin, options1.WaveLaneCountMax) && !std::getenv("PT_FSR_FG_ALLOW_UNSUPPORTED")) {
        reason = std::format("needs an AMD Radeon RX 5000 series or newer GPU ({} is GCN: wave size {} to {}, PCI device {:#06x})",
                             device.AdapterName(), options1.WaveLaneCountMin, options1.WaveLaneCountMax, adapter.DeviceId);
        return false;
    }
    reason.clear();
    return true;
}

std::unique_ptr<pt::d3d12::SwapchainOwner> CreateFsrFgPresenter(pt::d3d12::Device& device, std::string& error) {
    auto presenter = std::make_unique<FsrFgPresenter>(device);
    if (!presenter->Load(error)) {
        return nullptr;
    }
    return presenter;
}

#else

bool FsrFrameGenAvailable(pt::d3d12::Device&, std::string& reason) {
    reason = "not built into this executable for DirectX 12";
    return false;
}

std::unique_ptr<pt::d3d12::SwapchainOwner> CreateFsrFgPresenter(pt::d3d12::Device&, std::string& error) {
    error = "not built into this executable for DirectX 12";
    return nullptr;
}

#endif

}

#endif
