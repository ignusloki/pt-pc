#include "engine/render/render_util.h"

#include "engine/render/pipeline_cache_store.h"

#include <cstddef>
#include <cstring>
#include <iterator>

#include "engine/core/log.h"
#include "engine/render/mesh.h"
#include "engine/render/vk/vk_command_list.h"

namespace pt {

bool g_checkpoints = false;

void UseTargets(VkCommandBuffer cmd, std::initializer_list<TargetUse> uses) {
    gpu::TargetUse fixed[32];
    std::vector<gpu::TargetUse> more;
    gpu::TargetUse* converted = fixed;
    if (uses.size() > std::size(fixed)) {
        more.resize(uses.size());
        converted = more.data();
    }
    size_t count = 0;
    for (const TargetUse& use : uses) {
        converted[count++] = {use.target, gpu::vk::FromNative(use.layout)};
    }
    gpu::vk::CommandList(cmd).UseTargets(std::span<const gpu::TargetUse>(converted, count));
}

void BeginLabel(VkCommandBuffer cmd, const char* name) {
    gpu::vk::CommandList(cmd).BeginLabel(name);
}

void EndLabel(VkCommandBuffer cmd) {
    gpu::vk::CommandList(cmd).EndLabel();
}

void SetViewport(VkCommandBuffer cmd, VkRect2D area) {
    gpu::vk::CommandList(cmd).SetViewport({{area.offset.x, area.offset.y}, {area.extent.width, area.extent.height}});
}

void BeginPass(VkCommandBuffer cmd, VkRect2D area, std::span<const ColorOutput> colors, RenderTarget* depth, bool depth_read_only, bool clear_depth) {
    gpu::ColorOutput converted[8];
    size_t count = 0;
    for (const ColorOutput& c : colors) {
        gpu::ColorOutput& out = converted[count++];
        out.target = c.target;
        out.clear = c.clear;
        static_assert(sizeof(out.clear_value) == sizeof(c.clear_value));
        std::memcpy(&out.clear_value, &c.clear_value, sizeof(c.clear_value));
    }
    gpu::vk::CommandList(cmd).BeginPass({{area.offset.x, area.offset.y}, {area.extent.width, area.extent.height}},
                                        std::span<const gpu::ColorOutput>(converted, count), depth, depth_read_only, clear_depth);
}

void BeginPass(VkCommandBuffer cmd, VkExtent2D extent, std::initializer_list<ColorOutput> colors, RenderTarget* depth, bool depth_read_only,
               bool clear_depth) {
    BeginPass(cmd, VkRect2D{{0, 0}, extent}, std::span<const ColorOutput>(colors.begin(), colors.size()), depth, depth_read_only, clear_depth);
}

VkPipeline CreateGraphicsPipeline(VkDevice device, const PipelineDesc& desc) {
    VkShaderModule vert = vk::LoadShaderModule(device, desc.vertex);
    VkShaderModule frag = desc.fragment ? vk::LoadShaderModule(device, desc.fragment) : VK_NULL_HANDLE;
    if (!vert || (desc.fragment && !frag)) {
        if (vert) {
            vkDestroyShaderModule(device, vert, nullptr);
        }
        return VK_NULL_HANDLE;
    }
    const VkPipeline pipeline = CreateGraphicsPipeline(device, desc, vert, frag);
    vkDestroyShaderModule(device, vert, nullptr);
    if (frag) {
        vkDestroyShaderModule(device, frag, nullptr);
    }
    return pipeline;
}

VkPipeline CreateGraphicsPipeline(VkDevice device, const PipelineDesc& desc, VkShaderModule vert, VkShaderModule frag) {
    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv0)},
        {4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv1)},
        {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
        {6, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(Vertex, joints)},
        {7, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, weights)},
        {8, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv2)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (desc.mesh_input) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributes));
        vertex_input.pVertexAttributeDescriptions = attributes;
    }
    const VkVertexInputBindingDescription custom_binding{0, desc.vertex_stride, VK_VERTEX_INPUT_RATE_VERTEX};
    if (!desc.mesh_input && !desc.vertex_attributes.empty()) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &custom_binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(desc.vertex_attributes.size());
        vertex_input.pVertexAttributeDescriptions = desc.vertex_attributes.data();
    }
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cull;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable = desc.depth_bias ? VK_TRUE : VK_FALSE;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = desc.depth_compare;
    depth.depthBoundsTestEnable = desc.depth_bounds ? VK_TRUE : VK_FALSE;
    depth.minDepthBounds = 0.0f;
    depth.maxDepthBounds = 1.0f;
    std::vector<VkPipelineColorBlendAttachmentState> blends(desc.colors.size());
    for (size_t i = 0; i < blends.size(); ++i) {
        VkPipelineColorBlendAttachmentState& b = blends[i];
        b = {};
        b.colorWriteMask = i < desc.write_masks.size() ? desc.write_masks[i] : desc.write_mask;
        b.colorBlendOp = VK_BLEND_OP_ADD;
        b.alphaBlendOp = VK_BLEND_OP_ADD;
        switch (i < desc.blends.size() ? desc.blends[i] : desc.blend) {
        case BlendMode::None: break;
        case BlendMode::Alpha:
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case BlendMode::PremultipliedFadeAlpha:
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case BlendMode::Additive:
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case BlendMode::ProbeLerp:
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case BlendMode::AdditiveAlpha:
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case BlendMode::Multiply:
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case BlendMode::ProbeAccumulate:
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            break;
        }
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = static_cast<uint32_t>(blends.size());
    blend.pAttachments = blends.data();
    VkDynamicState dynamic_states[6] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE};
    uint32_t dynamic_count = 4;
    if (desc.depth_bias) dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_DEPTH_BIAS;
    if (desc.depth_bounds) dynamic_states[dynamic_count++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = dynamic_count;
    dynamic.pDynamicStates = dynamic_states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = static_cast<uint32_t>(desc.colors.size());
    rendering.pColorAttachmentFormats = desc.colors.data();
    rendering.depthAttachmentFormat = desc.depth;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = frag ? 2u : 1u;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = desc.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!vk::Check(vk::CreateGraphicsPipelinesCached(device, 1, &info, nullptr, &pipeline, desc.fragment ? desc.fragment : desc.vertex), desc.fragment ? desc.fragment : desc.vertex)) {
        pipeline = VK_NULL_HANDLE;
    }
    return pipeline;
}

VkPipeline CreateComputePipeline(VkDevice device, VkPipelineLayout layout, const char* shader) {
    VkShaderModule module = vk::LoadShaderModule(device, shader);
    if (!module) {
        return VK_NULL_HANDLE;
    }
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = "main";
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!vk::Check(vk::CreateComputePipelinesCached(device, 1, &info, nullptr, &pipeline, shader), shader)) {
        pipeline = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(device, module, nullptr);
    return pipeline;
}

}
