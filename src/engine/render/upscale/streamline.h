#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "engine/render/vk.h"

namespace pt::streamline {

enum class Marker : uint32_t { SimulationStart = 0, SimulationEnd = 1, RenderSubmitStart = 2, RenderSubmitEnd = 3, PresentStart = 4, PresentEnd = 5 };

struct FrameConstants {
    glm::mat4 view_to_clip{1.0f};
    glm::mat4 world_to_clip{1.0f};
    glm::mat4 previous_world_to_clip{1.0f};
    glm::vec2 jitter{0.0f};
    glm::vec2 motion_scale{1.0f};
    glm::vec3 position{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    float near_plane = 0.05f;
    float fov_y = 1.0f;
    float aspect = 1.0f;
    bool reset = false;
};

enum class Api : uint8_t { Vulkan, D3D12 };

bool Start(const std::filesystem::path& data_dir, std::string& reason, Api api = Api::Vulkan);
bool Active();
bool ActiveD3D12();
PFN_vkGetInstanceProcAddr InstanceProcAddr();
void DeviceCreated(VkInstance instance, VkPhysicalDevice physical, VkDevice device);
void Shutdown();

bool DlssSupported();
bool RayReconstructionSupported(std::string* reason);
bool FrameGenSupported(std::string* reason);
bool FrameGenNeedsVsyncOff();
bool IsFrameGenLoaded(bool& loaded);
/* Disables sl.dlss_g's feature hooks for this run; the Streamline plug-in module remains process-loaded. */
bool DisableFrameGenFeature();

void BeginFrame();
void SetMarker(Marker marker);
void SetConstants(const FrameConstants& constants);
bool ConstantsSet();
void SetFrameLimit(uint32_t microseconds);

const void* FrameToken();
constexpr uint32_t kViewport = 0;

void DeviceCreatedD3D12(void* d3d12_device, const uint8_t (&luid)[8]);
bool UpgradeInterface(void** interface_pointer);

struct D3D12FrameGenTags {
    void* depth = nullptr;
    void* motion = nullptr;
    void* hudless = nullptr;
    void* ui = nullptr;
    uint32_t input_width = 0;
    uint32_t input_height = 0;
    uint32_t color_width = 0;
    uint32_t color_height = 0;
    uint32_t state = 0;
    void* command_list = nullptr;
};
bool D3D12FrameGenTag(const D3D12FrameGenTags& tags);
void D3D12FrameGenUntag();

struct D3D12FrameGenOptions {
    bool on = false;
    uint32_t frames_to_generate = 1;
    uint32_t back_buffers = 3;
    uint32_t color_width = 0;
    uint32_t color_height = 0;
    uint32_t color_format = 0;
    uint32_t hudless_format = 0;
    uint32_t ui_format = 0;
    uint32_t mvec_depth_width = 0;
    uint32_t mvec_depth_height = 0;
    uint32_t mvec_format = 0;
    uint32_t depth_format = 0;
    bool ui_recomposition = false;
};
bool D3D12FrameGenSetOptions(const D3D12FrameGenOptions& options, std::string* error);

struct D3D12FrameGenState {
    bool ok = true;
    uint32_t status = 0;
    uint32_t presented = 0;
    uint32_t max_frames = 0;
    bool vsync_available = false;
    void* inputs_fence = nullptr;
    uint64_t inputs_fence_value = 0;
};
bool D3D12FrameGenGetState(D3D12FrameGenState& state);
void D3D12FrameGenFree();
void D3D12EndFrame();
void D3D12SetReflex(bool on);

}
