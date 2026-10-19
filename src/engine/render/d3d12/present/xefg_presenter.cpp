#include "engine/render/d3d12/present/xefg_presenter.h"

#if defined(_WIN32) && defined(PT_WITH_XEFG)

#include <xell/xell_d3d12.h>
#include <xess_fg/xefg_swapchain_d3d12.h>
#include <xess_fg/xefg_swapchain_debug.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>

#include "engine/core/log.h"
#include "engine/core/resource_path.h"

namespace pt::d3d12 {
namespace {

using Clock = std::chrono::steady_clock;

const char* ResultName(xefg_swapchain_result_t r) {
    switch (r) {
    case XEFG_SWAPCHAIN_RESULT_SUCCESS: return "success";
    case XEFG_SWAPCHAIN_RESULT_WARNING_OLD_DRIVER: return "warning: old driver";
    case XEFG_SWAPCHAIN_RESULT_WARNING_TOO_FEW_FRAMES: return "warning: too few frames";
    case XEFG_SWAPCHAIN_RESULT_WARNING_FRAMES_ID_MISMATCH: return "warning: frame id mismatch";
    case XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS: return "warning: missing present status";
    case XEFG_SWAPCHAIN_RESULT_WARNING_RESOURCE_SIZES_MISMATCH: return "warning: resource sizes mismatch";
    case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DEVICE: return "unsupported device";
    case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DRIVER: return "unsupported driver";
    case XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED: return "uninitialized";
    case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT: return "invalid argument";
    case XEFG_SWAPCHAIN_RESULT_ERROR_DEVICE_OUT_OF_MEMORY: return "out of device memory";
    case XEFG_SWAPCHAIN_RESULT_ERROR_DEVICE: return "device error";
    case XEFG_SWAPCHAIN_RESULT_ERROR_NOT_IMPLEMENTED: return "not implemented";
    case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_CONTEXT: return "invalid context";
    case XEFG_SWAPCHAIN_RESULT_ERROR_OPERATION_IN_PROGRESS: return "operation in progress";
    case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED: return "unsupported";
    case XEFG_SWAPCHAIN_RESULT_ERROR_CANT_LOAD_LIBRARY: return "cannot load library";
    case XEFG_SWAPCHAIN_RESULT_ERROR_MISMATCH_INPUT_RESOURCES: return "mismatched input resources";
    case XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_OUTPUT_RESOURCES: return "incorrect output resources";
    case XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_INPUT_RESOURCES: return "incorrect input resources";
    case XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_UNSUPPORTED: return "latency reduction unsupported";
    case XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_FUNCTION_MISSING: return "latency reduction function missing";
    case XEFG_SWAPCHAIN_RESULT_ERROR_HRESULT_FAILURE: return "HRESULT failure";
    case XEFG_SWAPCHAIN_RESULT_ERROR_DXGI_INVALID_CALL: return "DXGI invalid call";
    case XEFG_SWAPCHAIN_RESULT_ERROR_POINTER_STILL_IN_USE: return "pointer still in use";
    case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_DESCRIPTOR_HEAP: return "invalid descriptor heap";
    case XEFG_SWAPCHAIN_RESULT_ERROR_WRONG_CALL_ORDER: return "wrong call order";
    default: return "unknown error";
    }
}

std::string Code(xefg_swapchain_result_t r) {
    return std::format("{} ({})", ResultName(r), static_cast<int>(r));
}

void FgLog(const char* message, xefg_swapchain_logging_level_t level, void*) {
    if (level >= XEFG_SWAPCHAIN_LOGGING_LEVEL_ERROR) {
        LogError("xefg: {}", message ? message : "");
    } else if (level == XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING) {
        LogWarn("xefg: {}", message ? message : "");
    } else {
        LogInfo("xefg: {}", message ? message : "");
    }
}

void XellLog(const char* message, xell_logging_level_t level) {
    if (level >= XELL_LOGGING_LEVEL_ERROR) {
        LogError("xell: {}", message ? message : "");
    } else if (level == XELL_LOGGING_LEVEL_WARNING) {
        LogWarn("xell: {}", message ? message : "");
    } else {
        LogInfo("xell: {}", message ? message : "");
    }
}

int LogLevel() {
    const char* e = std::getenv("PT_XEFG_LOG");
    if (!e) {
        return 2;
    }
    const std::string v(e);
    return v == "debug" ? 0 : v == "info" ? 1 : v == "error" ? 3 : 2;
}

template <typename Fn>
bool Resolve(HMODULE module, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name)));
    return out != nullptr;
}

constexpr const wchar_t* kFgDll = L"libxess_fg.dll";
constexpr const wchar_t* kXellDll = L"libxell.dll";

}

struct XefgPresenter::Api {
    HMODULE fg_module = nullptr;
    HMODULE xell_module = nullptr;
    decltype(&xefgSwapChainD3D12CreateContext) create = nullptr;
    decltype(&xefgSwapChainD3D12InitFromSwapChainDesc) init = nullptr;
    decltype(&xefgSwapChainD3D12GetSwapChainPtr) swapchain = nullptr;
    decltype(&xefgSwapChainD3D12TagFrameResource) tag_resource = nullptr;
    decltype(&xefgSwapChainTagFrameConstants) tag_constants = nullptr;
    decltype(&xefgSwapChainSetEnabled) set_enabled = nullptr;
    decltype(&xefgSwapChainSetPresentId) set_present_id = nullptr;
    decltype(&xefgSwapChainGetLastPresentStatus) last_status = nullptr;
    decltype(&xefgSwapChainSetLoggingCallback) set_logging = nullptr;
    decltype(&xefgSwapChainDestroy) destroy = nullptr;
    decltype(&xefgSwapChainSetLatencyReduction) set_latency = nullptr;
    decltype(&xefgSwapChainGetVersion) version = nullptr;
    decltype(&xefgSwapChainGetProperties) properties = nullptr;
    decltype(&xefgSwapChainSetUiCompositionState) ui_composition = nullptr;
    decltype(&xefgSwapChainEnableDebugFeature) debug_feature = nullptr;
    decltype(&xellD3D12CreateContext) xell_create = nullptr;
    decltype(&xellDestroyContext) xell_destroy = nullptr;
    decltype(&xellSetSleepMode) xell_sleep_mode = nullptr;
    decltype(&xellSleep) xell_sleep = nullptr;
    decltype(&xellAddMarkerData) xell_marker = nullptr;
    decltype(&xellGetVersion) xell_version = nullptr;
    decltype(&xellSetLoggingCallback) xell_logging = nullptr;
};

XefgPresenter::XefgPresenter(Device& device) : device_(device) {}

XefgPresenter::~XefgPresenter() {
    Destroy();
    delete api_;
}

bool XefgPresenter::LibrariesPresent(std::string& error) {
    std::error_code ec;
    for (const wchar_t* name : {kFgDll, kXellDll}) {
        if (!std::filesystem::exists(ExecutableDir() / name, ec)) {
            error = std::format("{} is missing next to pt.exe", name == kFgDll ? "libxess_fg.dll" : "libxell.dll");
            return false;
        }
    }
    return true;
}

bool XefgPresenter::Load(std::string& error) {
    if (api_) {
        return true;
    }
    if (!LibrariesPresent(error)) {
        return false;
    }
    auto* api = new Api;
    api->xell_module = LoadLibraryW((ExecutableDir() / kXellDll).c_str());
    api->fg_module = LoadLibraryW((ExecutableDir() / kFgDll).c_str());
    if (!api->xell_module || !api->fg_module) {
        error = std::format("libxess_fg.dll or libxell.dll could not be loaded (error {})", GetLastError());
        delete api;
        return false;
    }
    HMODULE fg = api->fg_module;
    HMODULE xl = api->xell_module;
    const bool ok = Resolve(fg, "xefgSwapChainD3D12CreateContext", api->create) && Resolve(fg, "xefgSwapChainD3D12InitFromSwapChainDesc", api->init) &&
                    Resolve(fg, "xefgSwapChainD3D12GetSwapChainPtr", api->swapchain) &&
                    Resolve(fg, "xefgSwapChainD3D12TagFrameResource", api->tag_resource) &&
                    Resolve(fg, "xefgSwapChainTagFrameConstants", api->tag_constants) && Resolve(fg, "xefgSwapChainSetEnabled", api->set_enabled) &&
                    Resolve(fg, "xefgSwapChainSetPresentId", api->set_present_id) &&
                    Resolve(fg, "xefgSwapChainGetLastPresentStatus", api->last_status) &&
                    Resolve(fg, "xefgSwapChainSetLoggingCallback", api->set_logging) && Resolve(fg, "xefgSwapChainDestroy", api->destroy) &&
                    Resolve(fg, "xefgSwapChainSetLatencyReduction", api->set_latency) && Resolve(fg, "xefgSwapChainGetVersion", api->version) &&
                    Resolve(fg, "xefgSwapChainGetProperties", api->properties) &&
                    Resolve(fg, "xefgSwapChainSetUiCompositionState", api->ui_composition) &&
                    Resolve(fg, "xefgSwapChainEnableDebugFeature", api->debug_feature) && Resolve(xl, "xellD3D12CreateContext", api->xell_create) &&
                    Resolve(xl, "xellDestroyContext", api->xell_destroy) && Resolve(xl, "xellSetSleepMode", api->xell_sleep_mode) &&
                    Resolve(xl, "xellSleep", api->xell_sleep) && Resolve(xl, "xellAddMarkerData", api->xell_marker) &&
                    Resolve(xl, "xellGetVersion", api->xell_version) && Resolve(xl, "xellSetLoggingCallback", api->xell_logging);
    if (!ok) {
        error = "libxess_fg.dll or libxell.dll lacks the XeSS SDK 3 entry points";
        delete api;
        return false;
    }
    xefg_swapchain_version_t fg_version{};
    xell_version_t xell_version{};
    api->version(&fg_version);
    api->xell_version(&xell_version);
    LogInfo("xefg: libxess_fg.dll {}.{}.{}, libxell.dll {}.{}.{}", fg_version.major, fg_version.minor, fg_version.patch, xell_version.major,
            xell_version.minor, xell_version.patch);
    api_ = api;
    return true;
}

bool XefgPresenter::CreateContexts(std::string& error) {
    auto* xell = static_cast<xell_context_handle_t>(nullptr);
    const xell_result_t xr = api_->xell_create(device_.Get(), &xell);
    if (xr != XELL_RESULT_SUCCESS || !xell) {
        error = std::format("XeLL context creation failed ({})", static_cast<int>(xr));
        return false;
    }
    xell_ = xell;
    api_->xell_logging(xell, static_cast<xell_logging_level_t>(LogLevel()), &XellLog);
    xell_sleep_params_t sleep{};
    sleep.bLowLatencyMode = 1;
    const xell_result_t mode = api_->xell_sleep_mode(xell, &sleep);
    if (mode != XELL_RESULT_SUCCESS) {
        LogWarn("xell: low latency mode refused ({})", static_cast<int>(mode));
    }
    auto* fg = static_cast<xefg_swapchain_handle_t>(nullptr);
    const xefg_swapchain_result_t r = api_->create(device_.Get(), &fg);
    if (r < 0 || !fg) {
        error = "XeSS-FG context creation failed: " + Code(r);
        return false;
    }
    fg_ = fg;
    api_->set_logging(fg, static_cast<xefg_swapchain_logging_level_t>(LogLevel()), &FgLog, nullptr);
    const xefg_swapchain_result_t latency = api_->set_latency(fg, xell);
    if (latency < 0) {
        error = "XeSS-FG refused XeLL: " + Code(latency);
        return false;
    }
    xefg_swapchain_properties_t props{};
    if (api_->properties(fg, &props) >= 0) {
        max_interpolations_ = props.maxSupportedInterpolations;
    }
    return true;
}

bool XefgPresenter::GetBuffers(std::string& error) {
    buffers_.assign(desc_.buffer_count, nullptr);
    for (uint32_t i = 0; i < desc_.buffer_count; ++i) {
        const HRESULT hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i]));
        if (FAILED(hr)) {
            error = std::format("the XeSS-FG swapchain gave no back buffer {} ({:#010x})", i, static_cast<uint32_t>(hr));
            buffers_.clear();
            return false;
        }
    }
    return true;
}

bool XefgPresenter::Create(const SwapchainDesc& desc, std::string& error) {
    if (!api_ && !Load(error)) {
        return false;
    }
    const UINT flags = device_.TearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    if (swapchain_ && desc.window == desc_.window) {
        device_.WaitIdle();
        buffers_.clear();
        const HRESULT hr = swapchain_->ResizeBuffers(desc.buffer_count, desc.width, desc.height, desc.format, flags);
        if (FAILED(hr)) {
            error = std::format("ResizeBuffers on the XeSS-FG swapchain failed ({:#010x})", static_cast<uint32_t>(hr));
            Destroy();
            return false;
        }
        desc_ = desc;
        if (!GetBuffers(error)) {
            Destroy();
            return false;
        }
        LogInfo("xefg: swapchain resized to {}x{}, {} buffers, v-sync {}", desc.width, desc.height, desc.buffer_count, desc.vsync ? "on" : "off");
        return true;
    }
    Destroy();
    if (!CreateContexts(error)) {
        Destroy();
        return false;
    }
    auto* fg = static_cast<xefg_swapchain_handle_t>(fg_);
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
    xefg_swapchain_d3d12_init_params_t params{};
    params.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH;
    params.maxInterpolatedFrames = 1;
    params.uiMode = desc.ui_layer ? XEFG_SWAPCHAIN_UI_MODE_HUDLESS_UITEXTURE : XEFG_SWAPCHAIN_UI_MODE_BACKBUFFER_HUDLESS;
    xefg_swapchain_result_t r = api_->init(fg, desc.window, &sc, nullptr, device_.Queue(), device_.Factory(), &params);
    if (r < 0) {
        error = "the XeSS-FG swapchain could not be created: " + Code(r);
        Destroy();
        return false;
    }
    r = api_->swapchain(fg, IID_PPV_ARGS(&swapchain_));
    if (r < 0 || !swapchain_) {
        error = "XeSS-FG gave no swapchain: " + Code(r);
        Destroy();
        return false;
    }
    device_.Factory()->MakeWindowAssociation(desc.window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    if (const xefg_swapchain_result_t ui = api_->ui_composition(fg, XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_ENABLED); ui < 0) {
        LogWarn("xefg: UI composition refused: {}", Code(ui));
    }
    if (std::getenv("PT_XEFG_DEBUG")) {
        api_->debug_feature(fg, XEFG_SWAPCHAIN_DEBUG_FEATURE_TAG_INTERPOLATED_FRAMES, 1, nullptr);
    }
    if (std::getenv("PT_XEFG_ONLY_GENERATED")) {
        api_->debug_feature(fg, XEFG_SWAPCHAIN_DEBUG_FEATURE_SHOW_ONLY_INTERPOLATION, 1, nullptr);
    }
    desc_ = desc;
    swapchain_flags_ = flags;
    if (!GetBuffers(error)) {
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
    enabled_ = false;
    SetGenerating(true);
    presents_ = frames_shown_ = generated_presents_ = 0;
    native_counted_ = false;
    window_start_ = Clock::now();
    LogInfo("xefg: XeSS Frame Generation swapchain {}x{}, {} buffers, format {}, v-sync {}, tearing {}, up to {} generated frame{} "
            "(1 used), {}",
            desc.width, desc.height, desc.buffer_count, static_cast<int>(desc.format), desc.vsync ? "on" : "off", flags ? "allowed" : "off",
            max_interpolations_, max_interpolations_ == 1 ? "" : "s",
            desc.ui_layer ? "film grain and UI laid over the generated frames" : "UI composited from the back buffer");
    return true;
}

void XefgPresenter::Destroy() {
    if (!api_) {
        return;
    }
    if (swapchain_ || fg_) {
        device_.WaitIdle();
        LogStats("destroyed");
    }
    buffers_.clear();
    swapchain_.Reset();
    if (fg_) {
        const xefg_swapchain_result_t r = api_->destroy(static_cast<xefg_swapchain_handle_t>(fg_));
        if (r < 0) {
            LogWarn("xefg: context destruction returned {}", Code(r));
        }
        fg_ = nullptr;
    }
    if (xell_) {
        api_->xell_destroy(static_cast<xell_context_handle_t>(xell_));
        xell_ = nullptr;
    }
    enabled_ = false;
}

void XefgPresenter::FrameStart(uint32_t frame_id) {
    if (!xell_) {
        return;
    }
    auto* xell = static_cast<xell_context_handle_t>(xell_);
    api_->xell_sleep(xell, frame_id);
    api_->xell_marker(xell, frame_id, XELL_SIMULATION_START);
}

void XefgPresenter::Marker(LatencyMarker marker, uint32_t frame_id) {
    if (!xell_) {
        return;
    }
    const xell_latency_marker_type_t type = marker == LatencyMarker::SimulationEnd       ? XELL_SIMULATION_END
                                            : marker == LatencyMarker::RenderSubmitStart ? XELL_RENDERSUBMIT_START
                                                                                         : XELL_RENDERSUBMIT_END;
    api_->xell_marker(static_cast<xell_context_handle_t>(xell_), frame_id, type);
}

uint32_t XefgPresenter::Acquire() {
    return swapchain_ ? swapchain_->GetCurrentBackBufferIndex() : 0;
}

void XefgPresenter::SetGenerating(bool on) {
    if (!fg_ || on == enabled_) {
        return;
    }
    const xefg_swapchain_result_t r = api_->set_enabled(static_cast<xefg_swapchain_handle_t>(fg_), on ? 1u : 0u);
    if (r < 0) {
        LogWarn("xefg: {} frame generation failed: {}", on ? "enabling" : "pausing", Code(r));
        return;
    }
    enabled_ = on;
    LogInfo("xefg: frame generation {}", on ? "on" : "paused (menu)");
}

PresentResult XefgPresenter::Present(const FrameInputs& in, uint64_t& consumed_value) {
    consumed_value = fence_value_;
    if (!swapchain_ || !in.color) {
        return PresentResult::Failed;
    }
    auto* fg = static_cast<xefg_swapchain_handle_t>(fg_);
    ID3D12CommandQueue* queue = device_.Queue();
    if (in.wait_fence) {
        queue->Wait(in.wait_fence, in.wait_value);
    }
    Recorder& rec = recorders_[next_recorder_];
    next_recorder_ = (next_recorder_ + 1) % recorders_.size();
    if (!device_.WaitFence(fence_.Get(), rec.value, 5000)) {
        LogError("xefg: the present command list of {} frames ago never finished", recorders_.size());
        return PresentResult::Lost;
    }
    rec.allocator->Reset();
    rec.list->Reset(rec.allocator.Get(), nullptr);
    ID3D12GraphicsCommandList* list = rec.list.Get();
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
        list->CopyResource(back, in.color);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
    }
    const bool generate = enabled_ && in.depth && in.motion && in.input_width > 0 && in.input_height > 0;
    if (generate) {
        auto tag = [&](xefg_swapchain_resource_type_t type, ID3D12Resource* resource, uint32_t w, uint32_t h) {
            if (!resource) {
                return;
            }
            xefg_swapchain_d3d12_resource_data_t data{};
            data.type = type;
            data.validity = XEFG_SWAPCHAIN_RV_ONLY_NOW;
            data.resourceBase = {0, 0};
            data.resourceSize = {w, h};
            data.pResource = resource;
            data.incomingState = InputState();
            const xefg_swapchain_result_t r = api_->tag_resource(fg, list, in.frame_id, &data);
            if (r < 0 && result_logs_ < 10) {
                ++result_logs_;
                LogWarn("xefg: tagging resource {} failed: {}", static_cast<int>(type), Code(r));
            }
        };
        tag(XEFG_SWAPCHAIN_RES_DEPTH, in.depth, in.input_width, in.input_height);
        tag(XEFG_SWAPCHAIN_RES_MOTION_VECTOR, in.motion, in.input_width, in.input_height);
        tag(XEFG_SWAPCHAIN_RES_HUDLESS_COLOR, in.hudless, desc_.width, desc_.height);
        tag(XEFG_SWAPCHAIN_RES_UI, in.ui, desc_.width, desc_.height);
        ++tagged_;
    }
    list->Close();
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    ++fence_value_;
    queue->Signal(fence_.Get(), fence_value_);
    rec.value = fence_value_;
    consumed_value = fence_value_;
    if (generate) {
        xefg_swapchain_frame_constant_data_t constants{};
        std::memcpy(constants.viewMatrix, in.constants.view, sizeof(constants.viewMatrix));
        std::memcpy(constants.projectionMatrix, in.constants.projection, sizeof(constants.projectionMatrix));
        constants.jitterOffsetX = in.constants.jitter[0];
        constants.jitterOffsetY = in.constants.jitter[1];
        constants.motionVectorScaleX = in.constants.motion_scale[0];
        constants.motionVectorScaleY = in.constants.motion_scale[1];
        constants.resetHistory = in.constants.reset ? 1u : 0u;
        constants.frameRenderTime = in.constants.frame_ms;
        api_->tag_constants(fg, in.frame_id, &constants);
    }
    auto* xell = static_cast<xell_context_handle_t>(xell_);
    api_->xell_marker(xell, in.frame_id, XELL_PRESENT_START);
    api_->set_present_id(fg, in.frame_id);
    const bool tear = !desc_.vsync && (swapchain_flags_ & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    const HRESULT hr = swapchain_->Present(desc_.vsync ? 1 : 0, tear ? DXGI_PRESENT_ALLOW_TEARING : 0);
    api_->xell_marker(xell, in.frame_id, XELL_PRESENT_END);
    device_.DrainDebugMessages();
    xefg_swapchain_present_status_t status{};
    if (api_->last_status(fg, &status) >= 0) {
        ++presents_;
        ++total_presents_;
        frames_shown_ += status.framesPresented;
        total_shown_ += status.framesPresented;
        if (status.framesPresented > 1) {
            ++generated_presents_;
        }
        if (status.frameGenResult != last_result_) {
            if (result_logs_ < 20) {
                ++result_logs_;
                LogInfo("xefg: generation result {} (frame {}, {} frame{} queued, generation {})", Code(status.frameGenResult), in.frame_id,
                        status.framesPresented, status.framesPresented == 1 ? "" : "s", status.isFrameGenEnabled ? "on" : "off");
            }
            last_result_ = status.frameGenResult;
        }
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
        LogWarn("xefg: Present returned {:#010x}", static_cast<uint32_t>(hr));
    }
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || device_.Removed()) {
        return PresentResult::Lost;
    }
    return PresentResult::Failed;
}

void XefgPresenter::LogStats(const char* why) {
    const double seconds = std::chrono::duration<double>(Clock::now() - window_start_).count();
    if (presents_ > 0 && seconds > 0.0) {
        DXGI_FRAME_STATISTICS stats{};
        std::string native;
        if (swapchain_ && SUCCEEDED(swapchain_->GetFrameStatistics(&stats))) {
            if (native_counted_) {
                native = std::format(", DXGI present count +{}", stats.PresentCount - native_present_count_);
            }
            native_present_count_ = stats.PresentCount;
            native_counted_ = true;
        }
        LogInfo("xefg: {}: {} rendered frames presented, {} frames shown ({:.2f} per rendered frame, {} with a generated frame); "
                "{:.1f} fps rendered, {:.1f} fps shown{}; {} with generation inputs, {} occluded",
                why, presents_, frames_shown_, static_cast<double>(frames_shown_) / static_cast<double>(presents_), generated_presents_,
                static_cast<double>(presents_) / seconds, static_cast<double>(frames_shown_) / seconds, native, tagged_, occluded_);
    }
    presents_ = frames_shown_ = generated_presents_ = occluded_ = tagged_ = 0;
    window_start_ = Clock::now();
}

}

#elif defined(_WIN32)

namespace pt::d3d12 {

XefgPresenter::XefgPresenter(Device& device) : device_(device) {}
XefgPresenter::~XefgPresenter() = default;
bool XefgPresenter::LibrariesPresent(std::string& error) {
    error = "XeSS Frame Generation is not built into this executable";
    return false;
}
bool XefgPresenter::Load(std::string& error) { return LibrariesPresent(error); }
bool XefgPresenter::Create(const SwapchainDesc&, std::string& error) { return LibrariesPresent(error); }
void XefgPresenter::Destroy() {}
void XefgPresenter::FrameStart(uint32_t) {}
void XefgPresenter::Marker(LatencyMarker, uint32_t) {}
uint32_t XefgPresenter::Acquire() { return 0; }
PresentResult XefgPresenter::Present(const FrameInputs&, uint64_t& consumed_value) {
    consumed_value = 0;
    return PresentResult::Failed;
}
void XefgPresenter::SetGenerating(bool) {}
void XefgPresenter::LogStats(const char*) {}

}

#endif
