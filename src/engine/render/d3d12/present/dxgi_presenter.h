#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/d3d12/present/d3d12_device.h"
#include "engine/render/d3d12/present/swapchain_owner.h"

namespace pt::d3d12 {

class DxgiPresenter final : public SwapchainOwner {
public:
    explicit DxgiPresenter(Device& device) : device_(device) {}
    ~DxgiPresenter() override { Destroy(); }

    bool Create(const SwapchainDesc& desc, std::string& error) override;
    void Destroy() override;
    bool Alive() const override { return swapchain_ != nullptr; }
    void FrameStart(uint32_t) override {}
    void Marker(LatencyMarker, uint32_t) override {}
    uint32_t Acquire() override;
    PresentResult Present(const FrameInputs& inputs, uint64_t& consumed_value) override;
    ID3D12Fence* ConsumedFence() const override { return fence_.Get(); }
    D3D12_RESOURCE_STATES InputState() const override { return D3D12_RESOURCE_STATE_COMMON; }
    void SetGenerating(bool) override {}
    void LogStats(const char* why) override;

    ID3D12Resource* BackBuffer(uint32_t index) const { return index < buffers_.size() ? buffers_[index].Get() : nullptr; }

private:
    struct Recorder {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        uint64_t value = 0;
    };

    bool GetBuffers(std::string& error);
    void SetColorSpace();

    Device& device_;
    ComPtr<IDXGISwapChain3> swapchain_;
    std::vector<ComPtr<ID3D12Resource>> buffers_;
    std::vector<Recorder> recorders_;
    size_t next_recorder_ = 0;
    ComPtr<ID3D12Fence> fence_;
    uint64_t fence_value_ = 0;
    SwapchainDesc desc_{};
    UINT flags_ = 0;
    uint64_t presents_ = 0;
    uint64_t occluded_ = 0;
    int present_errors_ = 0;
};

}

#endif
