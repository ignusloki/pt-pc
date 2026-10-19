#include "engine/fs/qar.h"
#include "engine/render/texture_manager.h"
#include "engine/render/vk_context.h"
#include "engine/render/vk.h"
#include "engine/render/vk/vk_device.h"
#include "engine/render/gpu/gpu_vk_native.h"
#include <cstdio>
#include <fstream>
#include <thread>
#include <chrono>
#include <cmath>
#include <array>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: pt_texture_descriptor_test <game> <texture-stem>\n"); return 2; }
    int failures = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { std::printf("FAIL: %s\n", name); ++failures; } };
    pt::QarArchive qar;
    if (!qar.Open(std::filesystem::path(argv[1]) / "texture.qar")) return 2;
    pt::FtexTexture source;
    if (!pt::LoadFtex(qar, argv[2], source)) return 2;
    pt::vk::Context ctx;
    if (!ctx.Init(nullptr, true)) return 2;
    pt::TextureManager textures;
    pt::gpu::vk::Device device(ctx);
    if (!textures.Init(device)) return 2;
    bool loaded = false;
    uint32_t index = textures.LoadFox(qar, argv[2], &loaded);
    check(loaded, "original uploaded");
    pt::vk::Buffer buffer;
    if (!ctx.CreateBuffer(buffer, 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)) return 2;
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 1; set_info.pBindings = &binding;
    VkDescriptorSetLayout result_layout;
    vkCreateDescriptorSetLayout(ctx.device, &set_info, nullptr, &result_layout);
    const VkDescriptorSetLayout layouts[] = {pt::gpu::vk::Native(textures.BindingLayout()), result_layout};
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 2; layout_info.pSetLayouts = layouts;
    layout_info.pushConstantRangeCount = 1; layout_info.pPushConstantRanges = &push;
    VkPipelineLayout layout;
    vkCreatePipelineLayout(ctx.device, &layout_info, nullptr, &layout);
    std::ifstream shader(PT_TEXTURE_DESCRIPTOR_SHADER, std::ios::binary | std::ios::ate);
    const size_t shader_size = static_cast<size_t>(shader.tellg());
    std::vector<uint32_t> code(shader_size / 4);
    shader.seekg(0); shader.read(reinterpret_cast<char*>(code.data()), shader_size);
    if (!shader) return 2;
    VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module_info.codeSize = shader_size; module_info.pCode = code.data();
    VkShaderModule module;
    vkCreateShaderModule(ctx.device, &module_info, nullptr, &module);
    VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pipeline_info.stage.module = module; pipeline_info.stage.pName = "main";
    pipeline_info.layout = layout;
    VkPipeline pipeline;
    if (vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) return 2;
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1; pool_info.poolSizeCount = 1; pool_info.pPoolSizes = &pool_size;
    VkDescriptorPool pool;
    vkCreateDescriptorPool(ctx.device, &pool_info, nullptr, &pool);
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = pool; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &result_layout;
    VkDescriptorSet result;
    vkAllocateDescriptorSets(ctx.device, &allocation, &result);
    VkDescriptorBufferInfo buffer_info{buffer.buffer, 0, 32};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = result; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &buffer_info;
    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
    auto sample = [&](uint32_t scale, const char* name) {
        ctx.Submit([&](VkCommandBuffer cmd) {
            const VkDescriptorSet sets[] = {pt::gpu::vk::Native(textures.BindingSet()), result};
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2, sets, 0, nullptr);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &index);
            vkCmdDispatch(cmd, 1, 1, 1);
            VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            memory.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; memory.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            memory.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; memory.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dep.memoryBarrierCount = 1; dep.pMemoryBarriers = &memory;
            vkCmdPipelineBarrier2(cmd, &dep);
        });
        vmaInvalidateAllocation(ctx.allocator, buffer.allocation, 0, 32);
        const auto* size = static_cast<const uint32_t*>(buffer.mapped);
        check(size[0] == source.width * scale && size[1] == source.height * scale, name);
        std::printf("%s: %u x %u\n", name, size[0], size[1]);
        check(textures.Find(argv[2]) == index, "material texture index stable");
    };
    sample(1, "original descriptor");
    textures.SetAnisotropy(16); sample(1, "original descriptor after anisotropy");
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; semaphore_info.pNext = &type;
    VkSemaphore gate;
    if (vkCreateSemaphore(ctx.device, &semaphore_info, nullptr, &gate) != VK_SUCCESS) return 2;
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = gate; wait.value = 1; wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; submit.waitSemaphoreInfoCount = 1; submit.pWaitSemaphoreInfos = &wait;
    vkQueueSubmit2(ctx.queue, 1, &submit, VK_NULL_HANDLE);
    std::thread release_gate([&] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO}; signal.semaphore = gate; signal.value = 1;
        vkSignalSemaphore(ctx.device, &signal);
    });
    std::array<uint32_t, 16> indices;
    for (uint32_t i = 0; i < indices.size(); ++i) {
        std::array<uint8_t, 64> pixels;
        for (size_t j = 0; j < pixels.size(); j += 4) {
            pixels[j] = static_cast<uint8_t>(16 + i * 8); pixels[j + 1] = 64; pixels[j + 2] = 192; pixels[j + 3] = 255;
        }
        pt::TextureMip mip{4, 4, pixels};
        indices[i] = textures.Create("delayed-upload-" + std::to_string(i), pt::gpu::Format::R8G8B8A8Unorm, std::span(&mip, 1));
    }
    if (!std::getenv("PT_TEXTURE_UPLOAD_WAIT")) {
        uint64_t value = 1;
        vkGetSemaphoreCounterValue(ctx.device, gate, &value);
        check(value == 0, "uploads returned before GPU gate opened");
    }
    for (uint32_t i = 0; i < indices.size(); ++i) {
        index = indices[i];
        ctx.Submit([&](VkCommandBuffer cmd) {
            const VkDescriptorSet sets[] = {pt::gpu::vk::Native(textures.BindingSet()), result};
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2, sets, 0, nullptr);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &index);
            vkCmdDispatch(cmd, 1, 1, 1);
            VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            memory.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; memory.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            memory.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT; memory.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dep.memoryBarrierCount = 1; dep.pMemoryBarriers = &memory;
            vkCmdPipelineBarrier2(cmd, &dep);
        });
        vmaInvalidateAllocation(ctx.allocator, buffer.allocation, 0, 32);
        const auto* colour = static_cast<const float*>(buffer.mapped) + 4;
        check(std::abs(colour[0] - (16 + i * 8) / 255.0f) < 0.001f && std::abs(colour[1] - 64 / 255.0f) < 0.001f &&
              std::abs(colour[2] - 192 / 255.0f) < 0.001f && colour[3] == 1.0f, "delayed upload sampled exact colour");
    }
    release_gate.join();
    vkDestroySemaphore(ctx.device, gate, nullptr);
    std::printf("delayed texture uploads: 16 colours checked\n");
    vkDeviceWaitIdle(ctx.device);
    vkDestroyPipeline(ctx.device, pipeline, nullptr); vkDestroyShaderModule(ctx.device, module, nullptr);
    vkDestroyDescriptorPool(ctx.device, pool, nullptr); vkDestroyPipelineLayout(ctx.device, layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, result_layout, nullptr);
    ctx.DestroyBuffer(buffer); textures.Shutdown(); ctx.Shutdown();
    std::printf("texture descriptors: %d failures\n", failures);
    return failures ? 1 : 0;
}
