#include "engine/render/d3d12/present/sl_dlssg_presenter.h"

#ifdef _WIN32

#include <cstdlib>
#include <format>
#include <utility>

#include "engine/core/log.h"
#include "engine/render/upscale/streamline.h"

namespace pt::d3d12 {

namespace {

DXGI_FORMAT FormatOf(ID3D12Resource* r) {
    return r ? r->GetDesc().Format : DXGI_FORMAT_UNKNOWN;
}

bool EnvOff(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] == '0';
}

}

bool SlDlssgPresenter::Available(std::string& reason) {
    reason.clear();
    if (!streamline::ActiveD3D12()) {
        reason = "Streamline loads at the start that has DLSS Frame Generation selected; restart to use it";
        return false;
    }
    std::string why;
    if (!streamline::FrameGenSupported(&why)) {
        reason = std::format("Streamline: {}", why);
        return false;
    }
    return true;
}

bool SlDlssgPresenter::GetBuffers(std::string& error) {
    buffers_.assign(desc_.buffer_count, nullptr);
    for (uint32_t i = 0; i < desc_.buffer_count; ++i) {
        if (FAILED(swapchain_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i])))) {
            error = "the DLSS Frame Generation swapchain's back buffers could not be read";
            return false;
        }
        buffers_[i]->SetName(L"pt dlss-g back buffer");
    }
    return true;
}

bool SlDlssgPresenter::ModeOff() {
    Options off = mode_;
    off.on = false;
    return SetMode(off);
}

bool SlDlssgPresenter::SetMode(const Options& wanted) {
    if (wanted == mode_) {
        return true;
    }
    streamline::D3D12FrameGenOptions o;
    o.on = wanted.on;
    o.frames_to_generate = 1;
    o.back_buffers = desc_.buffer_count;
    o.color_width = wanted.color_w;
    o.color_height = wanted.color_h;
    o.color_format = static_cast<uint32_t>(wanted.color);
    o.hudless_format = static_cast<uint32_t>(wanted.hudless);
    o.ui_format = static_cast<uint32_t>(wanted.ui);
    o.mvec_depth_width = wanted.input_w;
    o.mvec_depth_height = wanted.input_h;
    o.mvec_format = static_cast<uint32_t>(wanted.motion);
    o.depth_format = static_cast<uint32_t>(wanted.depth);
    o.ui_recomposition = wanted.recompose;
    std::string error;
    if (!streamline::D3D12FrameGenSetOptions(o, &error)) {
        if (!mode_error_logged_) {
            LogError("frame generation: DLSS Frame Generation (DirectX 12) slDLSSGSetOptions({}) failed: {}; the previous mode remains",
                     wanted.on ? "on" : "off", error);
            mode_error_logged_ = true;
        }
        return false;
    }
    if (wanted.on != mode_.on) {
        LogInfo("frame generation: DLSS Frame Generation (DirectX 12) mode {}", wanted.on ? "on" : "off");
    }
    mode_ = wanted;
    return true;
}

bool SlDlssgPresenter::Create(const SwapchainDesc& desc, std::string& error) {
    std::string reason;
    if (!Available(reason)) {
        error = reason;
        return false;
    }
    const UINT flags = device_.TearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    if (swapchain_ && desc.window == desc_.window) {
        ModeOff();
        device_.WaitIdle();
        buffers_.clear();
        const HRESULT hr = swapchain_->ResizeBuffers(desc.buffer_count, desc.width, desc.height, desc.format, flags);
        if (FAILED(hr)) {
            error = std::format("ResizeBuffers on the DLSS Frame Generation swapchain failed ({:#010x})", static_cast<uint32_t>(hr));
            Destroy();
            return false;
        }
        desc_ = desc;
        if (!GetBuffers(error)) {
            Destroy();
            return false;
        }
        LogInfo("frame generation: DLSS Frame Generation (DirectX 12) swapchain resized to {}x{}", desc.width, desc.height);
        return true;
    }
    Destroy();
    ComPtr<IDXGIFactory4> native;
    if (!device_.CreateFactory(native)) {
        error = "a DXGI factory for DLSS Frame Generation could not be made";
        return false;
    }
    void* raw = native.Detach();
    if (!streamline::UpgradeInterface(&raw)) {
        static_cast<IUnknown*>(raw)->Release();
        error = "Streamline could not upgrade the DXGI factory to its proxy";
        return false;
    }
    factory_.Attach(static_cast<IDXGIFactory4*>(raw));
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
    ComPtr<IDXGISwapChain1> swapchain1;
    const HRESULT hr = factory_->CreateSwapChainForHwnd(device_.Queue(), desc.window, &sc, nullptr, nullptr, &swapchain1);
    if (FAILED(hr) || FAILED(swapchain1.As(&swapchain_))) {
        error = std::format("Streamline's DXGI proxy could not make the swapchain ({:#010x})", static_cast<uint32_t>(hr));
        Destroy();
        return false;
    }
    factory_->MakeWindowAssociation(desc.window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    desc_ = desc;
    flags_ = flags;
    if (!GetBuffers(error)) {
        Destroy();
        return false;
    }
    if (!fence_ && FAILED(device_.Get()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        error = "the DLSS Frame Generation present fence could not be created";
        Destroy();
        return false;
    }
    recorders_.resize(desc.buffer_count + 1);
    for (Recorder& rec : recorders_) {
        if (rec.allocator) {
            continue;
        }
        device_.Get()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&rec.allocator));
        if (rec.allocator) {
            device_.Get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, rec.allocator.Get(), nullptr, IID_PPV_ARGS(&rec.list));
        }
        if (!rec.list) {
            error = "the DLSS Frame Generation command lists could not be created";
            Destroy();
            return false;
        }
        rec.list->SetName(L"pt dlss-g present");
        rec.list->Close();
    }
    mode_ = Options{};
    failed_ = false;
    formats_logged_ = false;
    streamline::D3D12SetReflex(true);
    LogInfo("frame generation: DLSS Frame Generation (DirectX 12) swapchain {}x{} through Streamline's proxy, {} buffers, v-sync {}",
            desc.width, desc.height, desc.buffer_count, desc.vsync ? "on" : "off");
    return true;
}

void SlDlssgPresenter::Destroy() {
    if (swapchain_) {
        ModeOff();
        device_.WaitIdle();
        streamline::D3D12FrameGenFree();
        streamline::D3D12EndFrame();
        streamline::D3D12SetReflex(false);
        LogStats("destroyed");
    }
    buffers_.clear();
    swapchain_.Reset();
    factory_.Reset();
    mode_ = Options{};
}

void SlDlssgPresenter::FrameStart(uint32_t) {
    streamline::BeginFrame();
}

void SlDlssgPresenter::Marker(LatencyMarker marker, uint32_t) {
    switch (marker) {
    case LatencyMarker::SimulationEnd: streamline::SetMarker(streamline::Marker::SimulationEnd); break;
    case LatencyMarker::RenderSubmitStart: streamline::SetMarker(streamline::Marker::RenderSubmitStart); break;
    case LatencyMarker::RenderSubmitEnd: streamline::SetMarker(streamline::Marker::RenderSubmitEnd); break;
    }
}

uint32_t SlDlssgPresenter::Acquire() {
    return swapchain_ ? swapchain_->GetCurrentBackBufferIndex() : 0;
}

PresentResult SlDlssgPresenter::Present(const FrameInputs& in, uint64_t& consumed_value) {
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
        LogError("frame generation: the DLSS Frame Generation present list of {} frames ago never finished", recorders_.size());
        return PresentResult::Lost;
    }
    rec.allocator->Reset();
    rec.list->Reset(rec.allocator.Get(), nullptr);
    ID3D12GraphicsCommandList* list = rec.list.Get();
    const bool inputs = in.depth && in.motion && in.hudless && streamline::FrameToken() && streamline::ConstantsSet();
    bool generate = generating_ && inputs && !failed_;
    Options wanted = mode_;
    wanted.on = generate;
    if (generate) {
        streamline::D3D12FrameGenTags t;
        t.depth = in.depth;
        t.motion = in.motion;
        t.hudless = in.hudless;
        static const bool ui_tag = !EnvOff("PT_DLSSG_UI_TAG");
        static const bool recompose = !EnvOff("PT_DLSSG_UI_RECOMPOSITION");
        t.ui = ui_tag ? in.ui : nullptr;
        t.input_width = in.input_width;
        t.input_height = in.input_height;
        t.color_width = desc_.width;
        t.color_height = desc_.height;
        t.state = static_cast<uint32_t>(InputState());
        t.command_list = list;
        if (streamline::D3D12FrameGenTag(t)) {
            wanted.color_w = desc_.width;
            wanted.color_h = desc_.height;
            wanted.input_w = in.input_width;
            wanted.input_h = in.input_height;
            wanted.color = desc_.format;
            wanted.hudless = FormatOf(in.hudless);
            wanted.ui = FormatOf(static_cast<ID3D12Resource*>(t.ui));
            wanted.recompose = t.ui && recompose;
            wanted.motion = FormatOf(in.motion);
            wanted.depth = FormatOf(in.depth);
            if (!formats_logged_) {
                LogInfo("frame generation: DLSS Frame Generation (DirectX 12) inputs {}x{} (depth format {}, motion {}), output {}x{} "
                        "(colour {}, HUD-less {}, UI {}{})",
                        in.input_width, in.input_height, static_cast<int>(wanted.depth), static_cast<int>(wanted.motion), desc_.width,
                        desc_.height, static_cast<int>(wanted.color), static_cast<int>(wanted.hudless), static_cast<int>(wanted.ui),
                        wanted.recompose ? ", recomposed" : t.ui ? ", tagged" : ", not tagged");
                formats_logged_ = true;
            }
        } else {
            wanted.on = false;
        }
    }
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
    list->Close();
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    if (!SetMode(wanted) && wanted.on) {
        generate = false;
    }
    generate = mode_.on;
    const bool tear = !desc_.vsync && (flags_ & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    streamline::SetMarker(streamline::Marker::PresentStart);
    const HRESULT hr = swapchain_->Present(desc_.vsync ? 1 : 0, tear ? DXGI_PRESENT_ALLOW_TEARING : 0);
    streamline::SetMarker(streamline::Marker::PresentEnd);
    streamline::D3D12EndFrame();
    ++fence_value_;
    queue->Signal(fence_.Get(), fence_value_);
    rec.value = fence_value_;
    consumed_value = fence_value_;
    device_.DrainDebugMessages();
    ++presents_;
    if (generate) {
        ++generated_presents_;
        ++window_presents_;
        streamline::D3D12FrameGenState state;
        if (streamline::D3D12FrameGenGetState(state)) {
            shown_ += state.presented;
            window_shown_ += state.presented;
            if (!state.ok) {
                LogError("frame generation: DLSS Frame Generation (DirectX 12) status {:#x}, turned off for this session", state.status);
                failed_ = true;
                ModeOff();
            }
        }
        if (window_presents_ >= 120) {
            LogInfo("frame generation: DLSS Frame Generation (DirectX 12) {} frames shown for {} rendered ({:.2f}x)", window_shown_,
                    window_presents_, static_cast<double>(window_shown_) / static_cast<double>(window_presents_));
            window_presents_ = window_shown_ = 0;
        }
    }
    if (hr == S_OK) {
        return PresentResult::Ok;
    }
    if (hr == DXGI_STATUS_OCCLUDED) {
        ++occluded_;
        return PresentResult::Occluded;
    }
    if (present_errors_++ < 8) {
        LogWarn("frame generation: DLSS Frame Generation (DirectX 12) Present returned {:#010x}", static_cast<uint32_t>(hr));
    }
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || device_.Removed()) {
        device_.LogDred();
        return PresentResult::Lost;
    }
    return PresentResult::Failed;
}

void SlDlssgPresenter::LogStats(const char* why) {
    LogInfo("frame generation: DLSS Frame Generation (DirectX 12) {} presents, {} with generation, {} frames shown ({:.2f}x), {} occluded ({})",
            presents_, generated_presents_, shown_,
            generated_presents_ ? static_cast<double>(shown_) / static_cast<double>(generated_presents_) : 0.0, occluded_, why);
}

}

#endif
