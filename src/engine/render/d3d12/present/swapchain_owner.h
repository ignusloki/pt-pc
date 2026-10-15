#pragma once

#ifdef _WIN32

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <string>

namespace pt::d3d12 {

struct SwapchainDesc {
    HWND window = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    uint32_t buffer_count = 3;
    bool vsync = true;
    bool ui_layer = false;
    DXGI_COLOR_SPACE_TYPE color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    float max_nits = 0.0f;
    float max_frame_average_nits = 0.0f;
};

struct FrameConstants {
    float view[16] = {};
    float projection[16] = {};
    float jitter[2] = {};
    float motion_scale[2] = {1.0f, 1.0f};
    bool reset = false;
    float frame_ms = 0.0f;
};

enum class LatencyMarker { SimulationEnd, RenderSubmitStart, RenderSubmitEnd };

struct FrameInputs {
    ID3D12Resource* color = nullptr;
    ID3D12Resource* hudless = nullptr;
    ID3D12Resource* ui = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    uint32_t input_width = 0;
    uint32_t input_height = 0;
    FrameConstants constants;
    struct Camera {
        float near_plane = 0.05f;
        float fov_y = 1.0f;
        float position[3] = {};
        float up[3] = {0.0f, 1.0f, 0.0f};
        float right[3] = {1.0f, 0.0f, 0.0f};
        float forward[3] = {0.0f, 0.0f, 1.0f};
    } camera;
    ID3D12Fence* wait_fence = nullptr;
    uint64_t wait_value = 0;
    uint32_t frame_id = 0;
};

enum class PresentResult { Ok, Occluded, Lost, Failed };

class SwapchainOwner {
public:
    virtual ~SwapchainOwner() = default;
    virtual bool Create(const SwapchainDesc& desc, std::string& error) = 0;
    virtual void Destroy() = 0;
    virtual bool Alive() const = 0;
    virtual void FrameStart(uint32_t frame_id) = 0;
    virtual void Marker(LatencyMarker marker, uint32_t frame_id) = 0;
    virtual uint32_t Acquire() = 0;
    virtual PresentResult Present(const FrameInputs& inputs, uint64_t& consumed_value) = 0;
    virtual ID3D12Fence* ConsumedFence() const = 0;
    virtual D3D12_RESOURCE_STATES InputState() const = 0;
    virtual void SetGenerating(bool on) = 0;
    virtual void LogStats(const char* why) = 0;
};

}

#endif
