#pragma once

#ifdef _WIN32

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/d3d12/present/d3d12_device.h"
#include "engine/render/d3d12/present/swapchain_owner.h"

namespace pt::d3d12 {

class XefgPresenter final : public SwapchainOwner {
public:
    explicit XefgPresenter(Device& device);
    ~XefgPresenter() override;

    static bool LibrariesPresent(std::string& error);
    bool Load(std::string& error);

    bool Create(const SwapchainDesc& desc, std::string& error) override;
    void Destroy() override;
    bool Alive() const override { return swapchain_ != nullptr; }
    void FrameStart(uint32_t frame_id) override;
    void Marker(LatencyMarker marker, uint32_t frame_id) override;
    uint32_t Acquire() override;
    PresentResult Present(const FrameInputs& inputs, uint64_t& consumed_value) override;
    ID3D12Fence* ConsumedFence() const override { return fence_.Get(); }
    D3D12_RESOURCE_STATES InputState() const override { return D3D12_RESOURCE_STATE_COMMON; }
    void SetGenerating(bool on) override;
    void LogStats(const char* why) override;

private:
    struct Recorder {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        uint64_t value = 0;
    };
    struct Api;

    bool CreateContexts(std::string& error);
    bool GetBuffers(std::string& error);

    Device& device_;
    Api* api_ = nullptr;
    void* fg_ = nullptr;
    void* xell_ = nullptr;
    ComPtr<IDXGISwapChain4> swapchain_;
    std::vector<ComPtr<ID3D12Resource>> buffers_;
    std::vector<Recorder> recorders_;
    size_t next_recorder_ = 0;
    ComPtr<ID3D12Fence> fence_;
    uint64_t fence_value_ = 0;
    SwapchainDesc desc_{};
    UINT swapchain_flags_ = 0;
    bool enabled_ = false;
    uint32_t max_interpolations_ = 0;
    uint64_t presents_ = 0;
    uint64_t frames_shown_ = 0;
    uint64_t generated_presents_ = 0;
    uint64_t occluded_ = 0;
    uint64_t tagged_ = 0;
    uint64_t total_presents_ = 0;
    uint64_t total_shown_ = 0;
    int32_t last_result_ = 0;
    int result_logs_ = 0;
    int present_errors_ = 0;
    UINT native_present_count_ = 0;
    bool native_counted_ = false;
    std::chrono::steady_clock::time_point window_start_{};
};

}

#endif
