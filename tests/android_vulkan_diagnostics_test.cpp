#include "engine/render/android_vulkan_diagnostics.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Scenario {
    uint32_t loader = VK_API_VERSION_1_3, device = VK_API_VERSION_1_3;
    std::vector<const char*> extensions;
    bool changing_count = false, incomplete_returned = false, fail_surface_instance = false;
    VkResult devices_result = VK_SUCCESS, extensions_result = VK_SUCCESS, present_result = VK_SUCCESS;
    int instances = 0, destroyed = 0, surfaces_destroyed = 0, feature_queries = 0;
} scenario;

bool Exposed(uint32_t core, const char* extension) {
    return scenario.device >= core || std::find_if(scenario.extensions.begin(), scenario.extensions.end(),
        [&](const char* name) { return std::strcmp(extension, name) == 0; }) != scenario.extensions.end();
}
VKAPI_ATTR VkResult VKAPI_CALL EnumerateVersion(uint32_t* value) { *value = scenario.loader; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks*, VkInstance* instance) {
    Require(info->pApplicationInfo->apiVersion == std::min(scenario.loader, VK_API_VERSION_1_1), "diagnostics requested a newer API");
    ++scenario.instances;
    if (scenario.fail_surface_instance && info->enabledExtensionCount) return VK_ERROR_EXTENSION_NOT_PRESENT;
    *instance = reinterpret_cast<VkInstance>(uintptr_t(1));
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance, const VkAllocationCallbacks*) { ++scenario.destroyed; }
VKAPI_ATTR void VKAPI_CALL DestroySurface(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*) { ++scenario.surfaces_destroyed; }
VKAPI_ATTR VkResult VKAPI_CALL EnumerateDevices(VkInstance, uint32_t* count, VkPhysicalDevice* values) {
    if (scenario.devices_result != VK_SUCCESS) return scenario.devices_result;
    *count = 1;
    if (values) {
        values[0] = reinterpret_cast<VkPhysicalDevice>(uintptr_t(2));
        if (scenario.changing_count && !scenario.incomplete_returned) {
            scenario.incomplete_returned = true;
            return VK_INCOMPLETE;
        }
    }
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL GetProperties(VkPhysicalDevice, VkPhysicalDeviceProperties* props) {
    *props = {};
    props->apiVersion = scenario.device;
    props->driverVersion = 123;
    std::strcpy(props->deviceName, "simulated GPU");
    props->limits.maxImageDimension2D = 4096;
}
VKAPI_ATTR void VKAPI_CALL GetFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures* features) {
    *features = {};
    features->samplerAnisotropy = VK_TRUE;
}
VKAPI_ATTR void VKAPI_CALL GetFeatures2(VkPhysicalDevice device, VkPhysicalDeviceFeatures2* root) {
    Require(scenario.loader >= VK_API_VERSION_1_1, "features2 used on a 1.0 instance");
    ++scenario.feature_queries;
    GetFeatures(device, &root->features);
    for (auto* entry = static_cast<VkBaseOutStructure*>(root->pNext); entry; entry = entry->pNext) {
        switch (entry->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES:
            Require(Exposed(VK_API_VERSION_1_2, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME), "unsupported descriptor indexing queried");
            reinterpret_cast<VkPhysicalDeviceDescriptorIndexingFeatures*>(entry)->runtimeDescriptorArray = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
            Require(Exposed(VK_API_VERSION_1_2, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME), "unsupported timeline queried");
            reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(entry)->timelineSemaphore = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
            Require(Exposed(VK_API_VERSION_1_2, VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME), "unsupported scalar layout queried");
            reinterpret_cast<VkPhysicalDeviceScalarBlockLayoutFeatures*>(entry)->scalarBlockLayout = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES:
            Require(Exposed(VK_API_VERSION_1_3, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME), "unsupported dynamic rendering queried");
            reinterpret_cast<VkPhysicalDeviceDynamicRenderingFeatures*>(entry)->dynamicRendering = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES:
            Require(Exposed(VK_API_VERSION_1_3, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME), "unsupported synchronization2 queried");
            reinterpret_cast<VkPhysicalDeviceSynchronization2Features*>(entry)->synchronization2 = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES:
            Require(Exposed(VK_API_VERSION_1_3, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME), "unsupported demote queried");
            reinterpret_cast<VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures*>(entry)->shaderDemoteToHelperInvocation = VK_TRUE;
            break;
        default: throw std::runtime_error("unexpected/aggregate feature structure queried");
        }
    }
}
VKAPI_ATTR void VKAPI_CALL GetProperties2(VkPhysicalDevice device, VkPhysicalDeviceProperties2* root) {
    Require(scenario.loader >= VK_API_VERSION_1_1, "properties2 used on a 1.0 instance");
    GetProperties(device, &root->properties);
    for (auto* entry = static_cast<VkBaseOutStructure*>(root->pNext); entry; entry = entry->pNext) {
        switch (entry->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES:
            Require(Exposed(VK_API_VERSION_1_2, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME), "unsupported driver properties queried");
            std::strcpy(reinterpret_cast<VkPhysicalDeviceDriverProperties*>(entry)->driverName, "simulated driver");
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES:
            Require(Exposed(VK_API_VERSION_1_2, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME), "unsupported indexing properties queried");
            reinterpret_cast<VkPhysicalDeviceDescriptorIndexingProperties*>(entry)->maxDescriptorSetUpdateAfterBindSampledImages = 4096;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES:
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES:
            Require(scenario.device >= VK_API_VERSION_1_1, "unsupported 1.1 properties queried");
            break;
        default: throw std::runtime_error("unexpected property structure queried");
        }
    }
}
VKAPI_ATTR VkResult VKAPI_CALL EnumerateExtensions(VkPhysicalDevice, const char*, uint32_t* count, VkExtensionProperties* values) {
    if (scenario.extensions_result != VK_SUCCESS) return scenario.extensions_result;
    *count = static_cast<uint32_t>(scenario.extensions.size());
    if (values) for (size_t i = 0; i < scenario.extensions.size(); ++i) {
        values[i] = {};
        std::strcpy(values[i].extensionName, scenario.extensions[i]);
        values[i].specVersion = 1;
    }
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL GetQueues(VkPhysicalDevice, uint32_t* count, VkQueueFamilyProperties* queues) {
    *count = 1;
    if (queues) { queues[0] = {}; queues[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT; queues[0].queueCount = 1; }
}
VKAPI_ATTR void VKAPI_CALL GetMemory(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* props) { *props = {}; }
VKAPI_ATTR void VKAPI_CALL GetFormats(VkPhysicalDevice, VkFormat, VkFormatProperties* props) { *props = {}; }
VKAPI_ATTR VkResult VKAPI_CALL GetImageFormats(VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling, VkImageUsageFlags,
                                               VkImageCreateFlags, VkImageFormatProperties*) { return VK_ERROR_FORMAT_NOT_SUPPORTED; }
VKAPI_ATTR VkResult VKAPI_CALL GetSurfaceSupport(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32* present) {
    *present = VK_TRUE; // Must not be interpreted as supported when the result is an error.
    return scenario.present_result;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetProc(VkInstance, const char* name) {
#define MAP(vulkan, function) if (std::strcmp(name, #vulkan) == 0) return reinterpret_cast<PFN_vkVoidFunction>(&function)
    MAP(vkEnumerateInstanceVersion, EnumerateVersion); MAP(vkCreateInstance, CreateInstance); MAP(vkDestroyInstance, DestroyInstance);
    MAP(vkDestroySurfaceKHR, DestroySurface); MAP(vkEnumeratePhysicalDevices, EnumerateDevices);
    MAP(vkGetPhysicalDeviceProperties, GetProperties); MAP(vkGetPhysicalDeviceProperties2, GetProperties2);
    MAP(vkGetPhysicalDeviceFeatures, GetFeatures); MAP(vkGetPhysicalDeviceFeatures2, GetFeatures2);
    MAP(vkEnumerateDeviceExtensionProperties, EnumerateExtensions); MAP(vkGetPhysicalDeviceQueueFamilyProperties, GetQueues);
    MAP(vkGetPhysicalDeviceMemoryProperties, GetMemory); MAP(vkGetPhysicalDeviceFormatProperties, GetFormats);
    MAP(vkGetPhysicalDeviceImageFormatProperties, GetImageFormats); MAP(vkGetPhysicalDeviceSurfaceSupportKHR, GetSurfaceSupport);
#undef MAP
    Require(std::strcmp(name, "vkCreateDevice") != 0, "diagnostics attempted logical device creation");
    return nullptr;
}
std::string Capture(bool surface = false) {
    std::ostringstream report;
    const char* names[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
    pt::vk::DiagnosticSurface presentation;
    if (surface) {
        presentation.extensions = names;
        presentation.create = [](void*, VkInstance, VkSurfaceKHR* output) { *output = reinterpret_cast<VkSurfaceKHR>(uintptr_t(3)); return true; };
    }
    pt::vk::WriteVulkanCapabilities(report, GetProc, presentation);
    Require(scenario.destroyed == 1, "diagnostic instance was not destroyed");
    return report.str();
}
void Contains(const std::string& report, const char* text) { Require(report.find(text) != std::string::npos, text); }
}

int main() {
    try {
        scenario = {};
        scenario.loader = scenario.device = VK_API_VERSION_1_1;
        auto report = Capture();
        Contains(report, "renderer.version_gate=reject_below_1.3");
        Contains(report, "feature.dynamicRendering=not_queried");
        Contains(report, "feature.runtimeDescriptorArray=not_queried");
        Contains(report, "feature.fillModeNonSolid=0");
        Contains(report, "report.finished=true");

        scenario = {};
        scenario.loader = scenario.device = VK_API_VERSION_1_1;
        scenario.extensions = {VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
            VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME};
        report = Capture();
        Contains(report, "feature.runtimeDescriptorArray=1");
        Contains(report, "feature.dynamicRendering=1");
        Contains(report, "limit.maxDescriptorSetUpdateAfterBindSampledImages=4096");

        scenario = {};
        scenario.loader = scenario.device = VK_API_VERSION_1_2;
        scenario.changing_count = true;
        report = Capture();
        Require(scenario.incomplete_returned, "incomplete enumeration was not exercised");
        Contains(report, "physical_devices.enumeration_complete=1");
        Contains(report, "feature.timelineSemaphore=1");
        Contains(report, "feature.dynamicRendering=not_queried");

        scenario = {};
        scenario.present_result = VK_ERROR_SURFACE_LOST_KHR;
        report = Capture(true);
        Contains(report, "renderer.version_gate=pass");
        Contains(report, "feature.dynamicRendering=1");
        Contains(report, "driver.name=simulated driver");
        Contains(report, "supported=unknown");
        Require(scenario.surfaces_destroyed == 1, "diagnostic surface was not destroyed");

        scenario = {};
        scenario.fail_surface_instance = true;
        report = Capture(true);
        Require(scenario.instances == 2, "instance without surface extensions was not retried");
        Contains(report, "surface.created=not_attempted");
        Contains(report, "vkCreateInstance.without_surface_extensions.result=0");

        scenario = {};
        scenario.loader = scenario.device = VK_API_VERSION_1_1;
        scenario.extensions_result = VK_ERROR_INITIALIZATION_FAILED;
        report = Capture();
        Contains(report, "extensions.enumeration_complete=0");
        Contains(report, "feature.runtimeDescriptorArray=not_queried");

        scenario = {};
        scenario.loader = scenario.device = VK_API_VERSION_1_0;
        report = Capture();
        Require(scenario.feature_queries == 0, "1.0 queried features2");
        Contains(report, "features2.query=unavailable");
        Contains(report, "feature.samplerAnisotropy=1");

        scenario = {};
        scenario.devices_result = VK_ERROR_INITIALIZATION_FAILED;
        report = Capture();
        Contains(report, "physical_devices.enumeration_complete=0");
        Require(scenario.feature_queries == 0, "failed enumeration was ignored");

        std::ostringstream missing_loader;
        pt::vk::WriteVulkanCapabilities(missing_loader, nullptr);
        Contains(missing_loader.str(), "error=Vulkan loader entry point unavailable");
        std::cout << "PASS: 9 Vulkan capability diagnostic scenarios\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
