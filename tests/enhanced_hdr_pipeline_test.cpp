// Exercise the production composite shader, including the FP16 intermediate.
// No game assets or HDR monitor are required; this validates emitted values.
#include "engine/render/hdr_output.h"
#include "engine/render/render_util.h"
#include "engine/render/vk_context.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

int main() {
    using namespace pt;
    vk::Context ctx;
    if (!ctx.Init(nullptr, false)) return 2;
    constexpr uint32_t width = 19;
    constexpr uint32_t bytes = width * sizeof(glm::vec4);
    const std::array<glm::vec3, width> scene{{{0, 0, 0}, {.01f, .01f, .01f}, {.18f, .18f, .18f},
        {1, 1, 1}, {10, 2, 1}, {20, 4, 2}, {.2f, .4f, .6f}, {1000, 1000, 1000},
        {.00001f, .001f, .0031308f}, {.003f, .0032f, .004f}, {.0001f, .0002f, .0003f}, {1000, 1000, 1000}, {.2f,.4f,.6f}, {.2f,.4f,.6f}, {.2f,.4f,.6f}, {.2f,.4f,.6f}, {.2f,.4f,.6f}, {1e11f,2e10f,1e10f}, {1000,1000,1000}}};
    std::array<glm::vec4, width> encoded{}, top{};
    for (uint32_t i = 0; i < width; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float v = scene[i][c];
            encoded[i][c] = v <= .0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - .055f;
        }
        encoded[i].a = .25f; // Alpha must not be interpreted as highlight gain.
    }
    top[width - 1] = glm::vec4(1.0f); // Opaque white UI over an extreme scene highlight.
    vk::Image input, ui;
    RenderTarget intermediate, output;
    vk::Buffer upload, readback;
    if (!ctx.CreateImage(input, VK_FORMAT_R32G32B32A32_SFLOAT, {width, 1, 1}, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !ctx.CreateImage(ui, VK_FORMAT_R32G32B32A32_SFLOAT, {width, 1, 1}, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !ctx.CreateImage(intermediate.image, VK_FORMAT_R16G16B16A16_SFLOAT, {width, 1, 1}, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
        !ctx.CreateImage(output.image, VK_FORMAT_R32G32B32A32_SFLOAT, {width, 1, 1}, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
        !ctx.CreateBuffer(upload, bytes * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true) ||
        !ctx.CreateBuffer(readback, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) return 2;
    std::memcpy(upload.mapped, encoded.data(), bytes);
    std::memcpy(static_cast<char*>(upload.mapped) + bytes, top.data(), bytes);
    vmaFlushAllocation(ctx.allocator, upload.allocation, 0, bytes * 2);
    ctx.Submit([&](VkCommandBuffer cmd) {
        for (uint32_t i = 0; i < 2; ++i) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.image = i ? ui.image : input.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{};
            copy.bufferOffset = bytes * i;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {width, 1, 1};
            vkCmdCopyBufferToImage(cmd, upload.buffer, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    });
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler;
    if (vkCreateSampler(ctx.device, &sampler_info, nullptr, &sampler) != VK_SUCCESS) return 2;
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t i = 0; i < 3; ++i) bindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 3; set_info.pBindings = bindings.data();
    VkDescriptorSetLayout set_layout;
    if (vkCreateDescriptorSetLayout(ctx.device, &set_info, nullptr, &set_layout) != VK_SUCCESS) return 2;
    VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 1; layout_info.pSetLayouts = &set_layout;
    layout_info.pushConstantRangeCount = 1; layout_info.pPushConstantRanges = &range;
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(ctx.device, &layout_info, nullptr, &layout) != VK_SUCCESS) return 2;
    PipelineDesc desc;
    desc.fragment = "enhanced_composite.frag";
    desc.layout = layout;
    desc.colors = {VK_FORMAT_R16G16B16A16_SFLOAT};
    VkPipeline first = CreateGraphicsPipeline(ctx.device, desc);
    desc.colors = {VK_FORMAT_R32G32B32A32_SFLOAT};
    VkPipeline second = CreateGraphicsPipeline(ctx.device, desc);
    if (!first || !second) return 2;
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 2; pool_info.poolSizeCount = 1; pool_info.pPoolSizes = &pool_size;
    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(ctx.device, &pool_info, nullptr, &pool) != VK_SUCCESS) return 2;
    const VkDescriptorSetLayout layouts[] = {set_layout, set_layout};
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = pool; alloc.descriptorSetCount = 2; alloc.pSetLayouts = layouts;
    VkDescriptorSet sets[2];
    if (vkAllocateDescriptorSets(ctx.device, &alloc, sets) != VK_SUCCESS) return 2;
    for (uint32_t s = 0; s < 2; ++s) {
        for (uint32_t b = 0; b < 3; ++b) {
            VkDescriptorImageInfo info{sampler, b == 0 ? (s ? vk::FromGpu(intermediate.image).view : input.view) : ui.view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = sets[s]; write.dstBinding = b; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &info;
            vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
        }
    }
    int failures = 0;
    for (float paper_white : {200.0f, 400.0f}) {
    std::array<std::array<glm::vec3,width>,18> blowout_reference{};
    for (int effect = 0; effect < 15; ++effect) {
    for (float eotf : {0.0f, 1.0f, 2.0f}) {
      for (float output_mode : {1.0f, 2.0f}) {
        for (float peak_nits : {400.0f, 1000.0f, 2000.0f}) {
          for (float brightness : {.5f, 1.0f, 2.0f}) {
            if (effect != 0 && brightness != 1.0f) continue;
            bool ok = true;
            const int seeds = effect == 2 ? 64 : 1;
            double grain_mean = 0.0, grain_square = 0.0;
            for (int seed = 0; seed < seeds; ++seed) {
            ctx.Submit([&](VkCommandBuffer cmd) {
                std::array<float, 32> push{};
                push[0] = eotf; push[1] = brightness;
                push[15] = output_mode; push[16] = paper_white; push[17] = peak_nits;
                push[18] = push[19] = 1.0f;
                push[8] = effect >= 2 && effect <= 4 ? 1.0f : 0.0f; push[10] = 1.0f;
                push[9] = effect == 2 ? 2.0f : effect == 4 ? 1.0f : 0.0f;
                push[11] = effect == 1 ? 1.0f : 0.0f;
                push[12] = seed * .137f; push[13] = seed * .291f;
                if (effect >= 5) {
                    push[20] = effect == 7 ? 1.0f : 0.0f;
                    push[21] = push[22] = push[23] = push[25] = push[27] = 1.0f;
                    push[24] = 2.0f;
                    push[26] = effect == 6 ? 0.0f : 1.0f;
                    push[11] = effect == 7 ? 1.0f : 0.0f;
                    push[8] = effect == 7 ? 1.0f : 0.0f;
                    push[9] = 2.0f;
                    if (effect >= 12) { push[24]=1; push[28]=float(effect-12)*.5f; }
                    if (effect == 8) { push[20]=-4; push[21]=0; push[22]=0; push[23]=2; push[24]=0; push[25]=0; }
                    if (effect == 9) { push[20]=4; push[21]=2; push[22]=2; push[23]=0; push[25]=2; }
                    if (effect == 10) { push[20]=4; push[21]=0; push[22]=0; push[23]=2; push[25]=0; }
                    if (effect == 11) { push[20]=-4; push[21]=2; push[22]=2; push[23]=0; push[25]=2; }

                }
                for (uint32_t stage = 0; stage < 2; ++stage) {
                    RenderTarget* target = stage ? &output : &intermediate;
                    UseTargets(cmd, {{target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}});
                    BeginPass(cmd, {width, 1}, {{target, false, {}}});
                    SetViewport(cmd, {{0, 0}, {width, 1}});
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, stage ? second : first);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &sets[stage], 0, nullptr);
                    push[2] = float(stage);
                    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push.data());
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRendering(cmd);
                    UseTargets(cmd, {{target, stage ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
                }
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {width, 1, 1};
                vkCmdCopyImageToBuffer(cmd, vk::FromGpu(output.image).image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
                VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory, 0, nullptr, 0, nullptr);
            });
            vmaInvalidateAllocation(ctx.allocator, readback.allocation, 0, bytes);
            const auto* actual = static_cast<const glm::vec4*>(readback.mapped);
            for (uint32_t i = 0; i < width; ++i) {
                if (effect >= 5) {
                    glm::vec3 rgb = output_mode == 1.0f ? glm::vec3(actual[i])*80.0f :
                        glm::inverse(glm::mat3(.627404f,.069097f,.0163916f,.329283f,.919540f,.0880132f,.0433136f,.0113612f,.895595f)) *
                        glm::vec3(render::PqDecodeNits(actual[i].r),render::PqDecodeNits(actual[i].g),render::PqDecodeNits(actual[i].b));
                    if (effect >= 12) {
                        const int key=int(eotf)*6+(int(output_mode)-1)*3+(peak_nits==400?0:peak_nits==1000?1:2);
                        if (effect==12) blowout_reference[key][i]=rgb;
                        else if (i != width-1) {
                            const auto before=blowout_reference[key][i];
                            const float y=glm::dot(before,glm::vec3(.2126f,.7152f,.0722f));
                            const auto expected=glm::mix(before,glm::vec3(y),float(effect-12)*.5f);
                            for(int c=0;c<3;++c) ok &= std::abs(rgb[c]-expected[c])<std::max(.015f,std::abs(expected[c])*.008f);
                        }
                    }
                    const glm::vec3 target = effect == 6 ? rgb : glm::mat3(.822461969f,.033194199f,.017082631f,
                        .177538031f,.966805801f,.072397441f,0,0,.910519929f)*rgb;
                    for (int c=0;c<3;++c) {
                        ok &= std::isfinite(target[c]) && target[c] >= -.15f && target[c] <= peak_nits*1.004f;
                        if (i==0) ok &= std::abs(rgb[c]) < .0001f;
                        if (i==width-1) ok &= std::abs(rgb[c]-paper_white) < paper_white*.004f;
                    }
                    if ((effect == 5 || effect == 7) && output_mode==1.0f && i==4)
                        ok &= rgb.g < 0.0f || rgb.b < 0.0f; // Wide color survived both transfers and FP16.
                    ok &= actual[i].a==1.0f;
                    continue;
                }
                const auto decode = [&](float v) { return eotf > .5f ? std::pow(v, eotf > 1.5f ? 2.4f : 2.2f) : v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
                const auto encode = [&](float v) { return eotf > .5f ? std::pow(v, 1.f / (eotf > 1.5f ? 2.4f : 2.2f)) : v <= .0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - .055f; };
                glm::vec3 expected;
                for (int c = 0; c < 3; ++c) expected[c] = decode(std::min(std::pow(encoded[i][c], 1.f / brightness),65504.f));
                const float m = std::max({expected.r, expected.g, expected.b});
                if (m > 0.0f) expected *= render::EnhancedHdrShoulder(m, peak_nits / paper_white) / m;
                if (effect >= 3 && i == 2) {
                    // At this UV/seed the legacy noise texture fixture is black.
                    glm::vec3 code(encode(expected.r), encode(expected.g), encode(expected.b));
                    float y = glm::dot(code, glm::vec3(.299f,.587f,.114f));
                    float p = std::pow(y,.7f);
                    float alpha = effect == 4 ? std::pow(y,.08f) * (1.f-std::pow(y,.12f)) * .45f : p * (1.f-p) * .45f;
                    code *= 1.f - std::clamp(alpha,0.f,1.f);
                    expected = glm::vec3(decode(code.r),decode(code.g),decode(code.b));
                }
                if (i == width - 1) expected = glm::vec3(1.0f);
                expected *= paper_white;
                if (output_mode == 2.0f) {
                    expected = glm::mat3(.627404f, .069097f, .0163916f, .329283f, .919540f, .0880132f, .0433136f, .0113612f, .895595f) * expected;
                }
                for (int c = 0; c < 3; ++c) {
                    const float nits = output_mode == 1.0f ? actual[i][c] * 80.0f : render::PqDecodeNits(actual[i][c]);
                    bool match = std::isfinite(nits) && nits >= 0.0f && nits <= peak_nits * 1.004f;
                    if (effect == 0 || i == 0 || i == width - 1 || (effect >= 3 && i == 2) || (effect == 1 && i >= 13 && i <= 15))
                        match &= std::abs(nits - expected[c]) < std::max(.0001f, expected[c] * .004f);
                    if (effect == 2 && expected[c] > .0001f && i != width - 1) {
                        float ratio = nits / expected[c];
                        match &= ratio > .45f && ratio < 1.55f;
                        if (i == 2 && c == 0) { grain_mean += ratio - 1.0; grain_square += (ratio - 1.0) * (ratio - 1.0); }
                    }
                    // Both effects apply a common RGB gain in BT.709.
                    if ((effect == 1 || effect == 2) && output_mode == 1.0f && expected.r > 0 && c > 0)
                        match &= std::abs(nits * expected.r - actual[i].r * 80.0f * expected[c]) < std::max(.00001f, expected.r * expected[c] * .008f);
                    if (!match) std::printf("pixel %u channel %d: %.5f nits, expected %.5f\n", i, c, nits, expected[c]);
                    ok &= match;
                }
                ok &= actual[i].a == 1.0f;
            }
            }
            if (effect == 2) ok &= std::abs(grain_mean / seeds) < .03 && grain_square / seeds > .00001;
            std::printf("effect %d brightness %.1f: ", effect, brightness);
            std::printf("%s production composite: %s, EOTF %.0f, peak %.0f nits, SDR/shadows/RGB highlights/UI\n", ok ? "PASS" : "FAIL", output_mode == 1.0f ? "scRGB" : "HDR10", eotf, peak_nits);
            failures += !ok;
          }
        }
    }
    }
    }
    }
    vkDeviceWaitIdle(ctx.device);
    vkDestroyPipeline(ctx.device, first, nullptr); vkDestroyPipeline(ctx.device, second, nullptr);
    vkDestroyDescriptorPool(ctx.device, pool, nullptr); vkDestroyPipelineLayout(ctx.device, layout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, set_layout, nullptr); vkDestroySampler(ctx.device, sampler, nullptr);
    ctx.DestroyImage(input); ctx.DestroyImage(ui); ctx.DestroyImage(intermediate.image); ctx.DestroyImage(output.image);
    ctx.DestroyBuffer(upload); ctx.DestroyBuffer(readback); ctx.Shutdown();
    return failures ? 1 : 0;
}
