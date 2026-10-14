#include <vulkan/vulkan.h>

#include <iomanip>
#include <iostream>
#include <vector>

// Standalone allocation probe for the 1.0.7 renderer. No engine changes or game assets are needed.
int main() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "P.T. Android GPU probe";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&instance_info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cout << "{\"instance_result\":" << result << "}\n";
        return 1;
    }
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> physical_devices(count);
    vkEnumeratePhysicalDevices(instance, &count, physical_devices.data());
    if (physical_devices.empty()) {
        std::cout << "{\"error\":\"no physical GPU\"}\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }
    VkPhysicalDevice physical = physical_devices.front();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, queues.data());
    uint32_t family = queue_count;
    for (uint32_t i = 0; i < queue_count; ++i) {
        if ((queues[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            family = i;
            break;
        }
    }
    if (family == queue_count) {
        std::cout << "{\"error\":\"no graphics and compute queue\"}\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }
    VkPhysicalDeviceVulkan13Features have13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features have12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    have12.pNext = &have13;
    VkPhysicalDeviceFeatures2 have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    have.pNext = &have12;
    vkGetPhysicalDeviceFeatures2(physical, &have);

    VkPhysicalDeviceVulkan13Features want13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    want13.dynamicRendering = VK_TRUE;
    want13.synchronization2 = VK_TRUE;
    want13.shaderDemoteToHelperInvocation = VK_TRUE;
    VkPhysicalDeviceVulkan12Features want12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    want12.pNext = &want13;
    want12.descriptorIndexing = VK_TRUE;
    want12.runtimeDescriptorArray = VK_TRUE;
    want12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    want12.descriptorBindingPartiallyBound = VK_TRUE;
    want12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    want12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    want12.timelineSemaphore = VK_TRUE;
    want12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    want12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    want12.scalarBlockLayout = VK_TRUE;
    VkPhysicalDeviceFeatures2 want{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    want.pNext = &want12;
    want.features.samplerAnisotropy = VK_TRUE;
    want.features.fillModeNonSolid = VK_TRUE;
    want.features.shaderInt16 = VK_TRUE;
    want.features.shaderClipDistance = VK_TRUE;
    want.features.independentBlend = have.features.independentBlend;
    want.features.textureCompressionASTC_LDR = have.features.textureCompressionASTC_LDR;
    // Full BC support is optional here. Individual formats are queried before they are used.
    want.features.textureCompressionBC = VK_FALSE;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = &want;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    VkDevice device = VK_NULL_HANDLE;
    result = vkCreateDevice(physical, &device_info, nullptr, &device);
    std::cout << "{\"gpu\":" << std::quoted(properties.deviceName)
              << ",\"api_version\":[" << VK_API_VERSION_MAJOR(properties.apiVersion) << ','
              << VK_API_VERSION_MINOR(properties.apiVersion) << ',' << VK_API_VERSION_PATCH(properties.apiVersion)
              << "],\"full_bc_supported\":" << have.features.textureCompressionBC
              << ",\"full_bc_enabled\":false,\"astc_enabled\":" << want.features.textureCompressionASTC_LDR
              << ",\"engine_device_result\":" << result;
    if (result != VK_SUCCESS) {
        std::cout << "}\n";
        vkDestroyInstance(instance, nullptr);
        return 1;
    }
    bool okay = true;
    constexpr uint32_t textures = 8192, cubes = 64;
    VkDescriptorSetLayoutBinding bindings[3] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textures, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cubes, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    const VkDescriptorBindingFlags bindless = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    VkDescriptorBindingFlags binding_flags[3] = {bindless, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, bindless};
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flags.bindingCount = 3;
    flags.pBindingFlags = binding_flags;
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.pNext = &flags;
    layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layout_info.bindingCount = 3;
    layout_info.pBindings = bindings;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    result = vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &layout);
    std::cout << ",\"texture_table_layout_result\":" << result;
    okay &= result == VK_SUCCESS;
    VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textures + cubes},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    result = vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
    std::cout << ",\"texture_table_pool_result\":" << result;
    okay &= result == VK_SUCCESS;
    if (layout && pool) {
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        result = vkAllocateDescriptorSets(device, &allocate, &set);
        std::cout << ",\"texture_table_allocate_result\":" << result;
        okay &= result == VK_SUCCESS;
    }

    struct Format { const char* name; VkFormat value; bool required; };
    const Format formats[] = {
        {"BC1_UNORM", VK_FORMAT_BC1_RGBA_UNORM_BLOCK, true}, {"BC1_SRGB", VK_FORMAT_BC1_RGBA_SRGB_BLOCK, true},
        {"BC2_UNORM", VK_FORMAT_BC2_UNORM_BLOCK, true}, {"BC2_SRGB", VK_FORMAT_BC2_SRGB_BLOCK, true},
        {"BC3_UNORM", VK_FORMAT_BC3_UNORM_BLOCK, true}, {"BC3_SRGB", VK_FORMAT_BC3_SRGB_BLOCK, true},
        {"BC5_UNORM", VK_FORMAT_BC5_UNORM_BLOCK, false}, {"BC7_UNORM", VK_FORMAT_BC7_UNORM_BLOCK, false},
        {"BC7_SRGB", VK_FORMAT_BC7_SRGB_BLOCK, false}, {"RG8_UNORM", VK_FORMAT_R8G8_UNORM, true},
        {"BGRA8_UNORM", VK_FORMAT_B8G8R8A8_UNORM, true}, {"BGRA8_SRGB", VK_FORMAT_B8G8R8A8_SRGB, true},
        {"RGBA8_UNORM", VK_FORMAT_R8G8B8A8_UNORM, true}, {"RGBA8_SRGB", VK_FORMAT_R8G8B8A8_SRGB, true},
        {"R32_FLOAT", VK_FORMAT_R32_SFLOAT, true},
        {"ASTC_4x4_UNORM", VK_FORMAT_ASTC_4x4_UNORM_BLOCK, true}, {"ASTC_4x4_SRGB", VK_FORMAT_ASTC_4x4_SRGB_BLOCK, true},
        {"ASTC_6x6_UNORM", VK_FORMAT_ASTC_6x6_UNORM_BLOCK, true}, {"ASTC_6x6_SRGB", VK_FORMAT_ASTC_6x6_SRGB_BLOCK, true},
    };
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    std::cout << ",\"formats\":[";
    bool first = true;
    for (const auto& format : formats) {
        if (!first) std::cout << ',';
        first = false;
        VkFormatProperties capabilities{};
        vkGetPhysicalDeviceFormatProperties(physical, format.value, &capabilities);
        VkImageFormatProperties image_properties{};
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        result = vkGetPhysicalDeviceImageFormatProperties(physical, format.value, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                           usage, 0, &image_properties);
        std::cout << "{\"name\":" << std::quoted(format.name) << ",\"format_query_result\":" << result
                  << ",\"optimal_tiling_features\":" << capabilities.optimalTilingFeatures;
        if (result != VK_SUCCESS) {
            okay &= !format.required;
            std::cout << ",\"supported\":false}";
            continue;
        }
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = format.value;
        image_info.extent = {32, 32, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = usage;
        VkImage image = VK_NULL_HANDLE;
        result = vkCreateImage(device, &image_info, nullptr, &image);
        std::cout << ",\"image_create_result\":" << result;
        okay &= !format.required || result == VK_SUCCESS;
        VkDeviceMemory allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        bool supported = result == VK_SUCCESS;
        if (supported) {
            VkMemoryRequirements required{};
            vkGetImageMemoryRequirements(device, image, &required);
            uint32_t type = memory.memoryTypeCount;
            for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
                if ((required.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                    type = i;
                    break;
                }
            }
            VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocate.allocationSize = required.size;
            allocate.memoryTypeIndex = type;
            result = type < memory.memoryTypeCount ? vkAllocateMemory(device, &allocate, nullptr, &allocation) : VK_ERROR_FEATURE_NOT_PRESENT;
            std::cout << ",\"memory_allocate_result\":" << result << ",\"allocation_bytes\":" << required.size;
            supported &= result == VK_SUCCESS;
            if (supported) {
                result = vkBindImageMemory(device, image, allocation, 0);
                std::cout << ",\"memory_bind_result\":" << result;
                supported &= result == VK_SUCCESS;
            }
            if (supported) {
                VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                view_info.image = image;
                view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view_info.format = format.value;
                view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                result = vkCreateImageView(device, &view_info, nullptr, &view);
                std::cout << ",\"image_view_result\":" << result;
                supported &= result == VK_SUCCESS;
            }
        }
        std::cout << ",\"supported\":" << (supported ? "true" : "false") << '}';
        okay &= !format.required || supported;
        if (view) vkDestroyImageView(device, view, nullptr);
        if (image) vkDestroyImage(device, image, nullptr);
        if (allocation) vkFreeMemory(device, allocation, nullptr);
    }
    std::cout << "],\"pass\":" << (okay ? "true" : "false")
              << ",\"scope\":\"Device, texture table and small 2D image allocations; game rendering is not tested\"}\n";
    if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
    if (layout) vkDestroyDescriptorSetLayout(device, layout, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return okay ? 0 : 1;
}
