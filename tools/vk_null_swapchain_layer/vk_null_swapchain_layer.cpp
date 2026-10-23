
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#define PT_LAYER_EXPORT extern "C" __declspec(dllexport)
#else
#define PT_LAYER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

struct SwapchainEntry {
    void* reserved[6];
    uint32_t image_count = 0;
};

std::mutex g_mutex;
PFN_vkGetInstanceProcAddr g_next_gipa = nullptr;
PFN_vkGetDeviceProcAddr g_next_gdpa = nullptr;
PFN_vkCreateSwapchainKHR g_next_create_swapchain = nullptr;
PFN_vkDestroySwapchainKHR g_next_destroy_swapchain = nullptr;
std::unordered_map<uint64_t, SwapchainEntry*> g_swapchains;
uint32_t g_destroys = 0;

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* info, const VkAllocationCallbacks* allocator,
                                                  VkSwapchainKHR* swapchain) {
    const VkResult result = g_next_create_swapchain(device, info, allocator, swapchain);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(g_mutex);
        SwapchainEntry* entry = new SwapchainEntry{};
        entry->image_count = info->minImageCount;
        g_swapchains[reinterpret_cast<uint64_t>(*swapchain)] = entry;
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        SwapchainEntry* entry = g_swapchains[reinterpret_cast<uint64_t>(swapchain)];
        const volatile uint32_t images = entry->image_count;
        (void)images;
        g_swapchains.erase(reinterpret_cast<uint64_t>(swapchain));
        delete entry;
        ++g_destroys;
    }
    std::fprintf(stderr, "pt_null_swapchain_layer: destroyed swapchain %llx (%u so far)\n",
                 static_cast<unsigned long long>(reinterpret_cast<uint64_t>(swapchain)), g_destroys);
    g_next_destroy_swapchain(device, swapchain, allocator);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* name);

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* allocator, VkInstance* instance) {
    auto* link = static_cast<const VkLayerInstanceCreateInfo*>(info->pNext);
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO)) {
        link = static_cast<const VkLayerInstanceCreateInfo*>(link->pNext);
    }
    if (!link) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const_cast<VkLayerInstanceCreateInfo*>(link)->u.pLayerInfo = link->u.pLayerInfo->pNext;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE, "vkCreateInstance"));
    const VkResult result = create(info, allocator, instance);
    if (result == VK_SUCCESS) {
        g_next_gipa = next;
        std::fprintf(stderr, "pt_null_swapchain_layer: instance created through the test layer\n");
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator,
                                            VkDevice* device) {
    auto* link = static_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO)) {
        link = static_cast<const VkLayerDeviceCreateInfo*>(link->pNext);
    }
    if (!link) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkGetInstanceProcAddr next_gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr next_gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    const_cast<VkLayerDeviceCreateInfo*>(link)->u.pLayerInfo = link->u.pLayerInfo->pNext;
    auto create = reinterpret_cast<PFN_vkCreateDevice>(next_gipa(VK_NULL_HANDLE, "vkCreateDevice"));
    const VkResult result = create(physical, info, allocator, device);
    if (result == VK_SUCCESS) {
        g_next_gdpa = next_gdpa;
        g_next_create_swapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(next_gdpa(*device, "vkCreateSwapchainKHR"));
        g_next_destroy_swapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(next_gdpa(*device, "vkDestroySwapchainKHR"));
    }
    return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* name) {
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&CreateSwapchainKHR);
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&DestroySwapchainKHR);
    return g_next_gdpa ? g_next_gdpa(device, name) : nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* name) {
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddr);
    if (std::strcmp(name, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&CreateInstance);
    if (std::strcmp(name, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&CreateDevice);
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&CreateSwapchainKHR);
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&DestroySwapchainKHR);
    return g_next_gipa ? g_next_gipa(instance, name) : nullptr;
}

}

PT_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL PtNullSwapchainNegotiate(VkNegotiateLayerInterface* version) {
    if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (version->loaderLayerInterfaceVersion > 2) {
        version->loaderLayerInterfaceVersion = 2;
    }
    version->pfnGetInstanceProcAddr = &GetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = &GetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
