#pragma once

#if defined(__ANDROID__) || defined(PT_VULKAN_DIAGNOSTICS_TEST)
#include <vulkan/vulkan_core.h>
#include <iosfwd>
#include <span>

struct SDL_Window;

namespace pt::vk {

struct DiagnosticSurface {
    std::span<const char* const> extensions;
    void* user = nullptr;
    bool (*create)(void*, VkInstance, VkSurfaceKHR*) = nullptr;
};

// Own instance/function pointers: this query never enables device features or replaces volk's renderer dispatch.
void WriteVulkanCapabilities(std::ostream& out, PFN_vkGetInstanceProcAddr get_proc, const DiagnosticSurface& surface = {});

#ifdef __ANDROID__
void CaptureAndroidVulkanCapabilities(SDL_Window* window);
#endif

}
#endif
