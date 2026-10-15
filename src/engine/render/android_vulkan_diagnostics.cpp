#include "engine/render/android_vulkan_diagnostics.h"

#if defined(__ANDROID__) || defined(PT_VULKAN_DIAGNOSTICS_TEST)
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

#if defined(__ANDROID__) && !defined(PT_VULKAN_DIAGNOSTICS_TEST)
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <filesystem>
#include <fstream>
#include "engine/core/log.h"
#include "pt_version.h"
#endif

namespace pt::vk {
namespace {

std::string Version(uint32_t value) {
    return std::to_string(VK_API_VERSION_MAJOR(value)) + "." + std::to_string(VK_API_VERSION_MINOR(value)) + "." +
           std::to_string(VK_API_VERSION_PATCH(value));
}

template <typename T>
T Proc(PFN_vkGetInstanceProcAddr get_proc, VkInstance instance, const char* name) {
    return reinterpret_cast<T>(get_proc(instance, name));
}

void Result(std::ostream& out, const char* operation, VkResult result) {
    out << operation << ".result=" << static_cast<int>(result) << '\n';
    out.flush();
}

template <typename T, typename F>
std::vector<T> Enumerate(std::ostream& out, const char* operation, F call, bool& complete) {
    complete = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t count = 0;
        VkResult result = call(&count, nullptr);
        Result(out, operation, result);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) return {};
        std::vector<T> values(count);
        // A zero count is a complete empty result, not a request for another count-only query.
        if (!count && result == VK_SUCCESS) { complete = true; return values; }
        if (!count) continue;
        result = call(&count, values.data());
        Result(out, operation, result);
        if (result == VK_SUCCESS) {
            values.resize(std::min<size_t>(count, values.size()));
            complete = true;
            return values;
        }
        if (result != VK_INCOMPLETE) return {};
    }
    out << operation << ".complete=false (enumeration kept changing; capabilities unknown)\n";
    return {};
}

struct InstanceOwner {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroy = nullptr;
    PFN_vkDestroySurfaceKHR destroy_surface = nullptr;
    ~InstanceOwner() {
        if (surface && destroy_surface) destroy_surface(instance, surface, nullptr);
        if (instance && destroy) destroy(instance, nullptr);
    }
};

template <typename T, typename Root>
void Chain(Root& root, T& value, bool available) {
    if (available) { value.pNext = root.pNext; root.pNext = &value; }
}

void Feature(std::ostream& out, const char* name, VkBool32 value, bool queried = true) {
    out << "feature." << name << '=';
    if (queried) out << (value ? '1' : '0');
    else out << "not_queried";
    out << '\n';
}

}

void WriteVulkanCapabilities(std::ostream& out, PFN_vkGetInstanceProcAddr get_proc, const DiagnosticSurface& surface) {
    out << "P.T. Vulkan capability report; schema=1\n"
        << "mode=query_only; no logical device, image allocation or shader execution\n"
        << "renderer.required_api=1.3\nrenderer.texture_descriptors=8192\nrenderer.cube_descriptors=64\n"
        << "renderer.fragment_image_sampler_descriptors=8256 (other pipeline/pool limits also apply)\n";
    out.flush();
    if (!get_proc) { out << "error=Vulkan loader entry point unavailable\n"; return; }
    auto create_instance = Proc<PFN_vkCreateInstance>(get_proc, VK_NULL_HANDLE, "vkCreateInstance");
    auto enumerate_version = Proc<PFN_vkEnumerateInstanceVersion>(get_proc, VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (enumerate_version) {
        const VkResult result = enumerate_version(&loader_version);
        Result(out, "vkEnumerateInstanceVersion", result);
        if (result != VK_SUCCESS) loader_version = VK_API_VERSION_1_0;
    }
    out << "loader.api_version=" << Version(loader_version) << '\n';
    if (!create_instance) { out << "error=vkCreateInstance unavailable\n"; return; }
    // Deliberately independent of the renderer's 1.3 request, so older drivers can report their capabilities.
    const uint32_t requested_version = loader_version >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
    out << "diagnostic.instance_api=" << Version(requested_version) << '\n';
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "P.T. Android Vulkan diagnostics";
    app.apiVersion = requested_version;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(surface.extensions.size());
    info.ppEnabledExtensionNames = surface.extensions.data();
    InstanceOwner owner;
    VkResult result = create_instance(&info, nullptr, &owner.instance);
    Result(out, "vkCreateInstance.with_surface_extensions", result);
    bool surface_extensions = result == VK_SUCCESS && !surface.extensions.empty();
    if (result != VK_SUCCESS && !surface.extensions.empty()) {
        owner.instance = VK_NULL_HANDLE;
        info.enabledExtensionCount = 0;
        info.ppEnabledExtensionNames = nullptr;
        result = create_instance(&info, nullptr, &owner.instance);
        Result(out, "vkCreateInstance.without_surface_extensions", result);
    }
    if (result != VK_SUCCESS) { out << "report.complete=false (instance creation failed)\n"; return; }

#define PT_DIAG_PROC(name) const auto name = Proc<PFN_##name>(get_proc, owner.instance, #name)
    PT_DIAG_PROC(vkEnumeratePhysicalDevices);
    PT_DIAG_PROC(vkGetPhysicalDeviceProperties);
    PT_DIAG_PROC(vkGetPhysicalDeviceFeatures);
    PT_DIAG_PROC(vkGetPhysicalDeviceQueueFamilyProperties);
    PT_DIAG_PROC(vkEnumerateDeviceExtensionProperties);
    PT_DIAG_PROC(vkGetPhysicalDeviceMemoryProperties);
    PT_DIAG_PROC(vkGetPhysicalDeviceFormatProperties);
    PT_DIAG_PROC(vkGetPhysicalDeviceImageFormatProperties);
    PT_DIAG_PROC(vkGetPhysicalDeviceFeatures2);
    PT_DIAG_PROC(vkGetPhysicalDeviceProperties2);
    PT_DIAG_PROC(vkGetPhysicalDeviceSurfaceSupportKHR);
    PT_DIAG_PROC(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    PT_DIAG_PROC(vkGetPhysicalDeviceSurfaceFormatsKHR);
    PT_DIAG_PROC(vkGetPhysicalDeviceSurfacePresentModesKHR);
    owner.destroy = Proc<PFN_vkDestroyInstance>(get_proc, owner.instance, "vkDestroyInstance");
    owner.destroy_surface = Proc<PFN_vkDestroySurfaceKHR>(get_proc, owner.instance, "vkDestroySurfaceKHR");
#undef PT_DIAG_PROC
    if (!owner.destroy || !vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceProperties || !vkGetPhysicalDeviceFeatures ||
        !vkGetPhysicalDeviceQueueFamilyProperties || !vkEnumerateDeviceExtensionProperties || !vkGetPhysicalDeviceMemoryProperties ||
        !vkGetPhysicalDeviceFormatProperties || !vkGetPhysicalDeviceImageFormatProperties) {
        out << "error=required Vulkan 1.0 query entry point missing\nreport.complete=false\n";
        return;
    }
    if (surface_extensions && surface.create && owner.destroy_surface) {
        const bool created = surface.create(surface.user, owner.instance, &owner.surface);
        out << "surface.created=" << created << '\n';
    } else out << "surface.created=not_attempted\n";
    bool devices_complete;
    const auto devices = Enumerate<VkPhysicalDevice>(out, "vkEnumeratePhysicalDevices",
        [&](uint32_t* count, VkPhysicalDevice* values) { return vkEnumeratePhysicalDevices(owner.instance, count, values); }, devices_complete);
    out << "physical_devices.count=" << devices.size() << "\nphysical_devices.enumeration_complete=" << devices_complete << '\n';
    for (size_t device_index = 0; device_index < devices.size(); ++device_index) {
        const VkPhysicalDevice device = devices[device_index];
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        out << "\n[device " << device_index << "]\nname=" << props.deviceName << "\napi_version=" << Version(props.apiVersion)
            << "\napi_version.raw=" << props.apiVersion << "\ndriver_version.raw=" << props.driverVersion
            << "\nvendor_id=" << props.vendorID << "\ndevice_id=" << props.deviceID << "\ndevice_type=" << props.deviceType
            << "\nrenderer.version_gate=" << (props.apiVersion >= VK_API_VERSION_1_3 ? "pass" : "reject_below_1.3") << '\n';
        bool extensions_complete;
        const auto extensions = Enumerate<VkExtensionProperties>(out, "vkEnumerateDeviceExtensionProperties",
            [&](uint32_t* count, VkExtensionProperties* values) { return vkEnumerateDeviceExtensionProperties(device, nullptr, count, values); },
            extensions_complete);
        out << "extensions.count=" << extensions.size() << "\nextensions.enumeration_complete=" << extensions_complete << '\n';
        for (const auto& extension : extensions) out << "extension." << extension.extensionName << '=' << extension.specVersion << '\n';
        const auto has = [&](const char* name) {
            return std::any_of(extensions.begin(), extensions.end(), [&](const auto& entry) { return std::strcmp(entry.extensionName, name) == 0; });
        };
        const auto exposed = [&](uint32_t core, const char* extension) { return props.apiVersion >= core || has(extension); };
        const bool indexing = exposed(VK_API_VERSION_1_2, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        const bool timeline = exposed(VK_API_VERSION_1_2, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        const bool scalar = exposed(VK_API_VERSION_1_2, VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME);
        const bool dynamic = exposed(VK_API_VERSION_1_3, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        const bool sync2 = exposed(VK_API_VERSION_1_3, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        const bool demote = exposed(VK_API_VERSION_1_3, VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
        const bool query2 = requested_version >= VK_API_VERSION_1_1 && vkGetPhysicalDeviceFeatures2;
        VkPhysicalDeviceDescriptorIndexingFeatures index{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
        VkPhysicalDeviceTimelineSemaphoreFeatures time{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
        VkPhysicalDeviceScalarBlockLayoutFeatures layout{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES};
        VkPhysicalDeviceDynamicRenderingFeatures render{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        VkPhysicalDeviceSynchronization2Features sync{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures demotion{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        Chain(features, index, indexing); Chain(features, time, timeline); Chain(features, layout, scalar);
        Chain(features, render, dynamic); Chain(features, sync, sync2); Chain(features, demotion, demote);
        out << "features2.query=" << (query2 ? "performed" : "unavailable") << '\n';
        out.flush();
        if (query2) vkGetPhysicalDeviceFeatures2(device, &features);
        else vkGetPhysicalDeviceFeatures(device, &features.features);
#define PT_DIAG_FEATURE(field) Feature(out, #field, features.features.field)
        PT_DIAG_FEATURE(samplerAnisotropy); PT_DIAG_FEATURE(fillModeNonSolid); PT_DIAG_FEATURE(shaderInt16);
        PT_DIAG_FEATURE(shaderClipDistance); PT_DIAG_FEATURE(textureCompressionBC); PT_DIAG_FEATURE(textureCompressionASTC_LDR);
        PT_DIAG_FEATURE(textureCompressionETC2); PT_DIAG_FEATURE(independentBlend);
        PT_DIAG_FEATURE(shaderStorageImageReadWithoutFormat); PT_DIAG_FEATURE(shaderStorageImageWriteWithoutFormat);
#undef PT_DIAG_FEATURE
#define PT_DIAG_INDEX(field) Feature(out, #field, index.field, query2 && indexing)
        PT_DIAG_INDEX(runtimeDescriptorArray); PT_DIAG_INDEX(shaderSampledImageArrayNonUniformIndexing);
        PT_DIAG_INDEX(descriptorBindingPartiallyBound); PT_DIAG_INDEX(descriptorBindingVariableDescriptorCount);
        PT_DIAG_INDEX(descriptorBindingSampledImageUpdateAfterBind); PT_DIAG_INDEX(descriptorBindingUpdateUnusedWhilePending);
        PT_DIAG_INDEX(descriptorBindingStorageBufferUpdateAfterBind); PT_DIAG_INDEX(shaderStorageBufferArrayNonUniformIndexing);
        PT_DIAG_INDEX(shaderStorageImageArrayNonUniformIndexing); PT_DIAG_INDEX(descriptorBindingStorageImageUpdateAfterBind);
#undef PT_DIAG_INDEX
        Feature(out, "timelineSemaphore", time.timelineSemaphore, query2 && timeline);
        Feature(out, "scalarBlockLayout", layout.scalarBlockLayout, query2 && scalar);
        Feature(out, "dynamicRendering", render.dynamicRendering, query2 && dynamic);
        Feature(out, "synchronization2", sync.synchronization2, query2 && sync2);
        Feature(out, "shaderDemoteToHelperInvocation", demotion.shaderDemoteToHelperInvocation, query2 && demote);
        out << "feature.descriptorIndexing=aggregate_not_used; individual flags above\n";

        const bool driver = exposed(VK_API_VERSION_1_2, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
        const bool core11 = props.apiVersion >= VK_API_VERSION_1_1;
        VkPhysicalDeviceDriverProperties driver_props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceDescriptorIndexingProperties index_props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
        VkPhysicalDeviceSubgroupProperties subgroups{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
        VkPhysicalDeviceMaintenance3Properties maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};
        VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        Chain(props2, driver_props, driver); Chain(props2, index_props, indexing);
        Chain(props2, subgroups, core11); Chain(props2, maintenance, core11);
        const bool properties2 = requested_version >= VK_API_VERSION_1_1 && vkGetPhysicalDeviceProperties2;
        out << "properties2.query=" << (properties2 ? "performed" : "unavailable") << '\n';
        out.flush();
        if (properties2) vkGetPhysicalDeviceProperties2(device, &props2);
        if (properties2 && driver) {
            out << "driver.name=" << driver_props.driverName << "\ndriver.info=" << driver_props.driverInfo
                << "\ndriver.id=" << driver_props.driverID << "\ndriver.conformance="
                << unsigned(driver_props.conformanceVersion.major) << '.' << unsigned(driver_props.conformanceVersion.minor) << '.'
                << unsigned(driver_props.conformanceVersion.subminor) << '.' << unsigned(driver_props.conformanceVersion.patch) << '\n';
        } else out << "driver.properties=not_queried\n";
#define PT_DIAG_LIMIT(field) out << "limit." #field "=" << props.limits.field << '\n'
        PT_DIAG_LIMIT(maxImageDimension2D); PT_DIAG_LIMIT(maxImageDimensionCube); PT_DIAG_LIMIT(maxImageArrayLayers);
        PT_DIAG_LIMIT(maxPushConstantsSize); PT_DIAG_LIMIT(maxBoundDescriptorSets); PT_DIAG_LIMIT(maxPerStageResources);
        PT_DIAG_LIMIT(maxPerStageDescriptorSamplers); PT_DIAG_LIMIT(maxPerStageDescriptorSampledImages);
        PT_DIAG_LIMIT(maxDescriptorSetSamplers); PT_DIAG_LIMIT(maxDescriptorSetSampledImages);
        PT_DIAG_LIMIT(maxDescriptorSetStorageBuffers); PT_DIAG_LIMIT(maxStorageBufferRange);
        PT_DIAG_LIMIT(maxComputeWorkGroupInvocations); PT_DIAG_LIMIT(maxComputeSharedMemorySize);
        PT_DIAG_LIMIT(maxSamplerAnisotropy); PT_DIAG_LIMIT(maxColorAttachments);
#undef PT_DIAG_LIMIT
        out << "limit.maxComputeWorkGroupSize=" << props.limits.maxComputeWorkGroupSize[0] << ','
            << props.limits.maxComputeWorkGroupSize[1] << ',' << props.limits.maxComputeWorkGroupSize[2] << '\n';
        if (properties2 && indexing) {
#define PT_DIAG_INDEX_LIMIT(field) out << "limit." #field "=" << index_props.field << '\n'
            PT_DIAG_INDEX_LIMIT(maxPerStageDescriptorUpdateAfterBindSamplers);
            PT_DIAG_INDEX_LIMIT(maxPerStageDescriptorUpdateAfterBindSampledImages);
            PT_DIAG_INDEX_LIMIT(maxDescriptorSetUpdateAfterBindSamplers);
            PT_DIAG_INDEX_LIMIT(maxDescriptorSetUpdateAfterBindSampledImages);
            PT_DIAG_INDEX_LIMIT(maxPerStageUpdateAfterBindResources);
            PT_DIAG_INDEX_LIMIT(maxUpdateAfterBindDescriptorsInAllPools);
            PT_DIAG_INDEX_LIMIT(maxDescriptorSetUpdateAfterBindStorageBuffers);
#undef PT_DIAG_INDEX_LIMIT
        } else out << "limit.update_after_bind=not_queried\n";
        if (properties2 && core11) {
            out << "limit.maxPerSetDescriptors=" << maintenance.maxPerSetDescriptors << '\n'
                << "limit.maxMemoryAllocationSize=" << maintenance.maxMemoryAllocationSize << '\n'
                << "subgroup.size=" << subgroups.subgroupSize << "\nsubgroup.stages=" << subgroups.supportedStages
                << "\nsubgroup.operations=" << subgroups.supportedOperations << '\n';
        }
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(device, &memory);
        for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
            out << "memory.heap[" << i << "].bytes=" << memory.memoryHeaps[i].size << " flags=" << memory.memoryHeaps[i].flags << '\n';
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            out << "memory.type[" << i << "].heap=" << memory.memoryTypes[i].heapIndex << " flags=" << memory.memoryTypes[i].propertyFlags << '\n';
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        if (queue_count) vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues.data());
        queues.resize(std::min<size_t>(queue_count, queues.size()));
        out << "queue_families.count=" << queues.size() << '\n';
        for (size_t i = 0; i < queues.size(); ++i) {
            out << "queue[" << i << "].flags=" << queues[i].queueFlags << " count=" << queues[i].queueCount
                << " graphics=" << bool(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                << " compute=" << bool(queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) << '\n';
            if (owner.surface && vkGetPhysicalDeviceSurfaceSupportKHR) {
                VkBool32 present = VK_FALSE;
                result = vkGetPhysicalDeviceSurfaceSupportKHR(device, static_cast<uint32_t>(i), owner.surface, &present);
                out << "queue[" << i << "].present.result=" << static_cast<int>(result) << " supported=";
                if (result == VK_SUCCESS) out << present;
                else out << "unknown";
                out << '\n';
            } else out << "queue[" << i << "].present=not_queried\n";
        }
        if (owner.surface && vkGetPhysicalDeviceSurfaceCapabilitiesKHR && vkGetPhysicalDeviceSurfaceFormatsKHR &&
            vkGetPhysicalDeviceSurfacePresentModesKHR) {
            VkSurfaceCapabilitiesKHR caps{};
            result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, owner.surface, &caps);
            Result(out, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
            if (result == VK_SUCCESS) out << "surface.extent=" << caps.currentExtent.width << 'x' << caps.currentExtent.height
                << "\nsurface.usage_flags=" << caps.supportedUsageFlags << "\nsurface.transforms=" << caps.supportedTransforms << '\n';
            bool complete;
            const auto formats = Enumerate<VkSurfaceFormatKHR>(out, "vkGetPhysicalDeviceSurfaceFormatsKHR",
                [&](uint32_t* count, VkSurfaceFormatKHR* values) { return vkGetPhysicalDeviceSurfaceFormatsKHR(device, owner.surface, count, values); }, complete);
            out << "surface.formats.complete=" << complete << '\n';
            for (const auto& format : formats) out << "surface.format=" << format.format << " color_space=" << format.colorSpace << '\n';
            const auto modes = Enumerate<VkPresentModeKHR>(out, "vkGetPhysicalDeviceSurfacePresentModesKHR",
                [&](uint32_t* count, VkPresentModeKHR* values) { return vkGetPhysicalDeviceSurfacePresentModesKHR(device, owner.surface, count, values); }, complete);
            out << "surface.present_modes.complete=" << complete << '\n';
            for (const auto mode : modes) out << "surface.present_mode=" << mode << '\n';
        }
        const VkFormat formats[] = {
            VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_BC1_RGB_SRGB_BLOCK, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
            VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC2_SRGB_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC3_SRGB_BLOCK,
            VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_BC4_SNORM_BLOCK, VK_FORMAT_BC5_UNORM_BLOCK, VK_FORMAT_BC5_SNORM_BLOCK,
            VK_FORMAT_BC6H_UFLOAT_BLOCK, VK_FORMAT_BC6H_SFLOAT_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC7_SRGB_BLOCK,
            VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_FORMAT_ASTC_4x4_SRGB_BLOCK, VK_FORMAT_ASTC_6x6_UNORM_BLOCK, VK_FORMAT_ASTC_6x6_SRGB_BLOCK,
            VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT};
        // Record numeric Vulkan format IDs and exact usages, including alternatives useful for texture conversion research.
        for (const VkFormat format : formats) {
            VkFormatProperties capabilities{};
            vkGetPhysicalDeviceFormatProperties(device, format, &capabilities);
            out << "format[" << format << "].optimal_features=" << capabilities.optimalTilingFeatures
                << " linear_features=" << capabilities.linearTilingFeatures << '\n';
            struct Usage { const char* name; VkImageUsageFlags flags; VkImageCreateFlags create; };
            constexpr VkImageUsageFlags sampled = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            const Usage usages[] = {{"sampled_upload", sampled, 0}, {"sampled_cube", sampled, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT},
                {"color_target", sampled | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0},
                {"storage_target", sampled | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0},
                {"depth_target", VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0}};
            for (const auto& usage : usages) {
                // Unsupported format/usage combinations are expected observations, not diagnostic failures.
                out << "format[" << format << "]." << usage.name << " usage=" << usage.flags << " create_flags=" << usage.create;
                out.flush();
                VkImageFormatProperties image{};
                result = vkGetPhysicalDeviceImageFormatProperties(device, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                                   usage.flags, usage.create, &image);
                out << " result=" << static_cast<int>(result);
                if (result == VK_SUCCESS) out << " max_extent=" << image.maxExtent.width << 'x' << image.maxExtent.height
                    << " max_mips=" << image.maxMipLevels << " max_layers=" << image.maxArrayLayers
                    << " samples=" << image.sampleCounts << " max_resource_bytes=" << image.maxResourceSize;
                out << '\n';
            }
        }
        out << "[end device " << device_index << "]\n";
        out.flush();
    }
    out << "report.finished=true\nThis query does not establish game compatibility or performance.\n";
}

#if defined(__ANDROID__) && !defined(PT_VULKAN_DIAGNOSTICS_TEST)
void CaptureAndroidVulkanCapabilities(SDL_Window* window) {
    try {
        const char* root = SDL_GetAndroidInternalStoragePath();
        if (!root) { LogWarn("android: GPU diagnostics storage unavailable: {}", SDL_GetError()); return; }
        const auto path = std::filesystem::path(root) / "profile" / "vulkan-capabilities.txt";
        std::ofstream report(path, std::ios::trunc);
        if (!report) { LogWarn("android: cannot write GPU capability report"); return; }
        report << "build=" << PT_VERSION << "\ncaptured_unix_seconds="
               << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() << '\n';
        Uint32 count = 0;
        const char* const* names = window ? SDL_Vulkan_GetInstanceExtensions(&count) : nullptr;
        DiagnosticSurface surface{{names, names ? count : 0}, window,
            [](void* user, VkInstance instance, VkSurfaceKHR* output) {
                const bool created = SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(user), instance, nullptr, output);
                if (!created) LogWarn("android: diagnostic Vulkan surface unavailable: {}", SDL_GetError());
                return created;
            }};
        WriteVulkanCapabilities(report, reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr()), surface);
        report.flush();
        LogInfo("android: GPU capability report written; included in launcher's Save diagnostic log");
    } catch (const std::exception& error) {
        LogWarn("android: GPU capability collection failed: {}", error.what());
    }
}
#endif

}
#endif
