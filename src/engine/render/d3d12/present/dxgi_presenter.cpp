#include "engine/render/d3d12/present/dxgi_presenter.h"

#ifdef _WIN32

#include <format>
#include <utility>

#include "engine/core/log.h"

namespace pt::d3d12 {

bool DxgiPresenter::GetBuffers(std::string& error) {
    buffers_.assign(desc_.buffer_count, nullptr);
    for (uint32_t i = 0; i < desc_.buffer_count; ++i) {
        if (FAILED(swapchain_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i])))) {
            error = "the swapchain's back buffers could not be read";
            return false;
        }
        buffers_[i]->SetName(L"pt back buffer");
    }
    return true;
}

bool DxgiPresenter::Create(const SwapchainDesc& desc, std::string& error) {
    const UINT flags = device_.TearingSupported() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    if (swapchain_ && desc.window == desc_.window) {
        device_.WaitIdle();
        buffers_.clear();
        const HRESULT hr = swapchain_->ResizeBuffers(desc.buffer_count, desc.width, desc.height, desc.format, flags);
        if (FAILED(hr)) {
            error = std::format("ResizeBuffers failed ({:#010x})", static_cast<uint32_t>(hr));
            Destroy();
            return false;
        }
        desc_ = desc;
        if (!GetBuffers(error)) {
            Destroy();
            return false;
        }
        SetColorSpace();
        LogInfo("dxgi: swapchain resized to {}x{}, {} buffers, v-sync {}", desc.width, desc.height, desc.buffer_count, desc.vsync ? "on" : "off");
        return true;
    }
    Destroy();
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
    HRESULT hr = device_.Factory()->CreateSwapChainForHwnd(device_.Queue(), desc.window, &sc, nullptr, nullptr, &swapchain1);
    if (FAILED(hr) || FAILED(swapchain1.As(&swapchain_))) {
        error = std::format("the DXGI swapchain could not be created ({:#010x})", static_cast<uint32_t>(hr));
        Destroy();
        return false;
    }
    device_.Factory()->MakeWindowAssociation(desc.window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    desc_ = desc;
    flags_ = flags;
    if (!GetBuffers(error)) {
        Destroy();
        return false;
    }
    SetColorSpace();
    if (!fence_ && FAILED(device_.Get()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        error = "the present fence could not be created";
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
            error = "the present command lists could not be created";
            Destroy();
            return false;
        }
        rec.list->SetName(L"pt present copy");
        rec.list->Close();
    }
    presents_ = occluded_ = 0;
    LogInfo("dxgi: swapchain {}x{}, {} buffers, format {}, v-sync {}, tearing {}", desc.width, desc.height, desc.buffer_count,
            static_cast<int>(desc.format), desc.vsync ? "on" : "off", flags ? "allowed" : "off");
    return true;
}

void DxgiPresenter::SetColorSpace() {
    ComPtr<IDXGISwapChain4> sc4;
    if (FAILED(swapchain_.As(&sc4))) {
        return;
    }
    UINT support = 0;
    if (FAILED(sc4->CheckColorSpaceSupport(desc_.color_space, &support)) || !(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
        LogWarn("dxgi: colour space {} not supported for presentation", static_cast<int>(desc_.color_space));
        return;
    }
    sc4->SetColorSpace1(desc_.color_space);
    if (desc_.color_space == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
        return;
    }
    DXGI_HDR_METADATA_HDR10 meta{};
    const auto chroma = [](float v) { return static_cast<UINT16>(v * 50000.0f + 0.5f); };
    meta.RedPrimary[0] = chroma(0.708f);
    meta.RedPrimary[1] = chroma(0.292f);
    meta.GreenPrimary[0] = chroma(0.170f);
    meta.GreenPrimary[1] = chroma(0.797f);
    meta.BluePrimary[0] = chroma(0.131f);
    meta.BluePrimary[1] = chroma(0.046f);
    meta.WhitePoint[0] = chroma(0.3127f);
    meta.WhitePoint[1] = chroma(0.3290f);
    meta.MaxMasteringLuminance = static_cast<UINT>(desc_.max_nits);
    meta.MinMasteringLuminance = 1;
    meta.MaxContentLightLevel = static_cast<UINT16>(desc_.max_nits);
    meta.MaxFrameAverageLightLevel = static_cast<UINT16>(desc_.max_frame_average_nits);
    if (desc_.max_nits > 0.0f) {
        sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(meta), &meta);
    }
    LogInfo("dxgi: HDR colour space {}, metadata peak {:.0f} nits", desc_.color_space == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 ? "scRGB" : "HDR10",
            desc_.max_nits);
}

void DxgiPresenter::Destroy() {
    if (swapchain_) {
        device_.WaitIdle();
        LogStats("destroyed");
    }
    buffers_.clear();
    swapchain_.Reset();
}

uint32_t DxgiPresenter::Acquire() {
    return swapchain_ ? swapchain_->GetCurrentBackBufferIndex() : 0;
}

PresentResult DxgiPresenter::Present(const FrameInputs& in, uint64_t& consumed_value) {
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
        LogError("dxgi: the present command list of {} frames ago never finished", recorders_.size());
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
    list->Close();
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    ++fence_value_;
    queue->Signal(fence_.Get(), fence_value_);
    rec.value = fence_value_;
    consumed_value = fence_value_;
    const bool tear = !desc_.vsync && (flags_ & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    const HRESULT hr = swapchain_->Present(desc_.vsync ? 1 : 0, tear ? DXGI_PRESENT_ALLOW_TEARING : 0);
    device_.DrainDebugMessages();
    ++presents_;
    if (hr == S_OK) {
        return PresentResult::Ok;
    }
    if (hr == DXGI_STATUS_OCCLUDED) {
        ++occluded_;
        return PresentResult::Occluded;
    }
    if (present_errors_++ < 8) {
        LogWarn("dxgi: Present returned {:#010x}", static_cast<uint32_t>(hr));
    }
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || device_.Removed()) {
        device_.LogDred();
        return PresentResult::Lost;
    }
    return PresentResult::Failed;
}

void DxgiPresenter::LogStats(const char* why) {
    LogInfo("dxgi: {} presents, {} occluded ({})", presents_, occluded_, why);
}

}

#endif
