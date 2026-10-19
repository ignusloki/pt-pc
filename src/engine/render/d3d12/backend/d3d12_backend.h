#pragma once

#ifdef _WIN32

#include <cstdint>
#include <memory>
#include <string>

#include "engine/render/gpu/gpu_device.h"

struct HWND__;
struct ImDrawData;

namespace pt::gpu::d3d12 {

struct CreateOptions {
#ifdef NDEBUG
    bool debug = false;
    bool dred = false;
#else
    bool debug = true;
    bool dred = true;
#endif
    uint32_t view_descriptors = 262144;
    uint32_t sampler_descriptors = 2048;
    uint32_t frames_in_flight = 2;
    bool use_adapter_luid = false;
    uint8_t adapter_luid[8] = {};
};

class D3D12Device;

enum class FrameGenOwner : uint8_t { None, Xess, Fsr, Dlss };
constexpr int kFrameGenOwners = 4;

struct FrameGenInputs {
    RenderTarget* hudless = nullptr;
    RenderTarget* ui = nullptr;
    RenderTarget* depth = nullptr;
    RenderTarget* motion = nullptr;
    uint32_t input_width = 0;
    uint32_t input_height = 0;
    float view[16] = {};
    float projection[16] = {};
    float jitter[2] = {};
    float motion_scale[2] = {1.0f, 1.0f};
    bool reset = false;
    float frame_ms = 16.6f;
    float near_plane = 0.05f;
    float fov_y = 1.0f;
    float position[3] = {};
    float up[3] = {0.0f, 1.0f, 0.0f};
    float right[3] = {1.0f, 0.0f, 0.0f};
    float forward[3] = {0.0f, 0.0f, 1.0f};
};

std::unique_ptr<D3D12Device> Create(const CreateOptions& options, std::string& error);

class D3D12Device : public gpu::Device {
public:
    virtual bool CreateSwapchain(HWND__* window, uint32_t width, uint32_t height, gpu::Format format, bool vsync, std::string& error,
                                 float hdr_peak_nits = 0.0f) = 0;
    virtual bool DisplayHdr(HWND__* window, float& peak_nits) = 0;
    virtual bool InitImGui(gpu::Format target_format) = 0;
    virtual void ImGuiNewFrame() = 0;
    virtual void RenderImGui(gpu::CommandList& list, ImDrawData* data) = 0;
    virtual void ShutdownImGui() = 0;
    virtual void DestroySwapchain() = 0;
    virtual bool FrameGenAvailable(FrameGenOwner owner, std::string& reason) = 0;
    virtual void SetFrameGenOwner(FrameGenOwner owner) = 0;
    virtual FrameGenOwner ActiveFrameGenOwner() const = 0;
    virtual void SetGenerating(bool on) = 0;
    virtual void SetFrameGenInputs(const FrameGenInputs& inputs) = 0;
    virtual void* NativeDevice() const = 0;
    virtual void* NativeQueue() const = 0;
    virtual bool WrapRenderTarget(void* resource, gpu::Format format, gpu::Extent2D extent, ImageResource& out) = 0;
    virtual bool CheckRootSignature(const std::string& shader_dir, std::string& report) const = 0;
    virtual uint32_t DebugErrors() = 0;
    virtual std::string RuntimeDescription() const = 0;
};

}

#endif
