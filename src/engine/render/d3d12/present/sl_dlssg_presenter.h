#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/d3d12/present/d3d12_device.h"
#include "engine/render/d3d12/present/swapchain_owner.h"

namespace pt::d3d12 {

class SlDlssgPresenter final : public SwapchainOwner {
public:
    explicit SlDlssgPresenter(Device& device) : device_(device) {}
    ~SlDlssgPresenter() override { Destroy(); }

    static bool Available(std::string& reason);

    bool Create(const SwapchainDesc& desc, std::string& error) override;
    void Destroy() override;
    bool Alive() const override { return swapchain_ != nullptr; }
    void FrameStart(uint32_t frame_id) override;
    void Marker(LatencyMarker marker, uint32_t frame_id) override;
    uint32_t Acquire() override;
    PresentResult Present(const FrameInputs& inputs, uint64_t& consumed_value) override;
    ID3D12Fence* ConsumedFence() const override { return fence_.Get(); }
    D3D12_RESOURCE_STATES InputState() const override { return D3D12_RESOURCE_STATE_COMMON; }
    void SetGenerating(bool on) override { generating_ = on; }
    void LogStats(const char* why) override;

private:
    struct Recorder {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        uint64_t value = 0;
    };
    struct Options {
        bool on = false;
        uint32_t color_w = 0, color_h = 0, input_w = 0, input_h = 0;
        DXGI_FORMAT color = DXGI_FORMAT_UNKNOWN, hudless = DXGI_FORMAT_UNKNOWN, ui = DXGI_FORMAT_UNKNOWN, motion = DXGI_FORMAT_UNKNOWN,
                    depth = DXGI_FORMAT_UNKNOWN;
        bool recompose = false;
        bool operator==(const Options&) const = default;
    };

    bool GetBuffers(std::string& error);
    bool SetMode(const Options& wanted);
    bool ModeOff();

    Device& device_;
    ComPtr<IDXGIFactory4> factory_;
    ComPtr<IDXGISwapChain3> swapchain_;
    std::vector<ComPtr<ID3D12Resource>> buffers_;
    std::vector<Recorder> recorders_;
    size_t next_recorder_ = 0;
    ComPtr<ID3D12Fence> fence_;
    uint64_t fence_value_ = 0;
    SwapchainDesc desc_{};
    UINT flags_ = 0;
    Options mode_{};
    bool generating_ = false;
    bool failed_ = false;
    bool mode_error_logged_ = false;
    bool formats_logged_ = false;
    uint64_t presents_ = 0;
    uint64_t generated_presents_ = 0;
    uint64_t shown_ = 0;
    uint64_t occluded_ = 0;
    uint64_t window_presents_ = 0;
    uint64_t window_shown_ = 0;
    int present_errors_ = 0;
};

}

#endif
