#include "engine/render/vk/vk_command_list.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>

#include "engine/render/render_util.h"

namespace pt::gpu::vk {
namespace {

VkPipelineBindPoint NativeBindPoint(BindPoint point) {
    return static_cast<VkPipelineBindPoint>(point);
}

VkImageSubresourceLayers NativeLayers(const ImageSubresource& s) {
    return {s.aspect.bits, s.mip, s.base_layer, s.layers};
}

VkBufferImageCopy NativeCopy(const BufferImageCopy& r) {
    VkBufferImageCopy c{};
    c.bufferOffset = r.buffer_offset;
    c.bufferRowLength = r.row_length;
    c.bufferImageHeight = r.image_height;
    c.imageSubresource = NativeLayers(r.image);
    c.imageOffset = {r.image_offset[0], r.image_offset[1], r.image_offset[2]};
    c.imageExtent = {r.extent.width, r.extent.height, r.extent.depth};
    return c;
}

}

void CommandList::UseTargets(std::span<const TargetUse> uses) {
    VkImageMemoryBarrier2 barriers[16];
    uint32_t count = 0;
    for (const TargetUse& use : uses) {
        if (!use.target || !use.target->Valid() || count == std::size(barriers)) {
            continue;
        }
        if (use.target->layout == use.layout &&
            (use.layout == Layout::ShaderReadOnly || use.layout == Layout::DepthReadOnly)) {
            continue;
        }
        VkImageMemoryBarrier2& b = barriers[count++];
        b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.oldLayout = Native(use.target->layout);
        b.newLayout = Native(use.layout);
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = Native(use.target->image.image);
        b.subresourceRange = {use.target->aspect.bits, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        use.target->layout = use.layout;
    }
    if (count == 0) {
        return;
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = count;
    dependency.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(cmd_, &dependency);
}

void CommandList::Barrier(const ImageBarrier& barrier) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = static_cast<VkPipelineStageFlags2>(barrier.src_stage);
    b.srcAccessMask = barrier.src_access;
    b.dstStageMask = static_cast<VkPipelineStageFlags2>(barrier.dst_stage);
    b.dstAccessMask = barrier.dst_access;
    b.oldLayout = Native(barrier.old_layout);
    b.newLayout = Native(barrier.new_layout);
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = Native(barrier.image);
    b.subresourceRange = {barrier.aspect.bits, barrier.base_mip, barrier.mip_levels, barrier.base_layer, barrier.layers};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd_, &dependency);
}

void CommandList::BufferBarrier(BufferHandle buffer, PipelineStage src_stage, uint64_t src_access, PipelineStage dst_stage, uint64_t dst_access) {
    VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    b.srcStageMask = static_cast<VkPipelineStageFlags2>(src_stage);
    b.srcAccessMask = src_access;
    b.dstStageMask = static_cast<VkPipelineStageFlags2>(dst_stage);
    b.dstAccessMask = dst_access;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = Native(buffer);
    b.offset = 0;
    b.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd_, &dependency);
}

void CommandList::GlobalBarrier(PipelineStage src_stage, uint64_t src_access, PipelineStage dst_stage, uint64_t dst_access) {
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = static_cast<VkPipelineStageFlags2>(src_stage);
    b.srcAccessMask = src_access;
    b.dstStageMask = static_cast<VkPipelineStageFlags2>(dst_stage);
    b.dstAccessMask = dst_access;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd_, &dependency);
}

void CommandList::BeginPass(Rect2D area, std::span<const ColorOutput> colors, RenderTarget* depth, bool depth_read_only, bool clear_depth) {
    VkRenderingAttachmentInfo attachments[8];
    uint32_t count = 0;
    for (const ColorOutput& c : colors) {
        VkRenderingAttachmentInfo& a = attachments[count++];
        a = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = Native(c.target->image.view);
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = c.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        static_assert(sizeof(a.clearValue.color) == sizeof(c.clear_value));
        std::memcpy(&a.clearValue.color, &c.clear_value, sizeof(c.clear_value));
    }
    VkRenderingAttachmentInfo depth_info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (depth) {
        depth_info.imageView = Native(depth->image.view);
        depth_info.imageLayout = depth_read_only ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_info.loadOp = clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_info.storeOp = depth_read_only ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE;
        depth_info.clearValue.depthStencil = {0.0f, 0};
    }
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = Native(area);
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = count;
    rendering.pColorAttachments = attachments;
    rendering.pDepthAttachment = depth ? &depth_info : nullptr;
    vkCmdBeginRendering(cmd_, &rendering);
    SetViewport(area);
    vkCmdSetCullMode(cmd_, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(cmd_, VK_FRONT_FACE_COUNTER_CLOCKWISE);
}

void CommandList::BeginRendering(Rect2D area, std::span<const Attachment> colors) {
    VkRenderingAttachmentInfo attachments[8];
    uint32_t count = 0;
    for (const Attachment& c : colors) {
        if (count == 8) {
            break;
        }
        VkRenderingAttachmentInfo& a = attachments[count++];
        a = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = Native(c.view);
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = static_cast<VkAttachmentLoadOp>(c.load);
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        std::memcpy(&a.clearValue.color, &c.clear_value, sizeof(c.clear_value));
    }
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = Native(area);
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = count;
    rendering.pColorAttachments = attachments;
    vkCmdBeginRendering(cmd_, &rendering);
}

void CommandList::EndPass() {
    vkCmdEndRendering(cmd_);
}

void CommandList::SetViewport(Rect2D area) {
    const VkRect2D rect = Native(area);
    VkViewport viewport{static_cast<float>(rect.offset.x), static_cast<float>(rect.offset.y), static_cast<float>(rect.extent.width),
                        static_cast<float>(rect.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd_, 0, 1, &viewport);
    vkCmdSetScissor(cmd_, 0, 1, &rect);
}

void CommandList::SetCullMode(CullMode mode) {
    vkCmdSetCullMode(cmd_, Native(mode));
}

void CommandList::SetFrontFace(FrontFace face) {
    vkCmdSetFrontFace(cmd_, Native(face));
}

void CommandList::SetDepthBias(float constant, float clamp, float slope) {
    vkCmdSetDepthBias(cmd_, constant, clamp, slope);
}

void CommandList::SetDepthBounds(float min, float max) {
    vkCmdSetDepthBounds(cmd_, min, max);
}

void CommandList::BindPipeline(BindPoint point, PipelineHandle pipeline) {
    vkCmdBindPipeline(cmd_, NativeBindPoint(point), Native(pipeline));
}

void CommandList::BindSets(BindPoint point, PipelineLayoutHandle layout, uint32_t first_set, std::span<const BindingSetHandle> sets) {
    VkDescriptorSet native[8];
    const uint32_t count = static_cast<uint32_t>(std::min(sets.size(), std::size(native)));
    for (uint32_t i = 0; i < count; ++i) {
        native[i] = Native(sets[i]);
    }
    vkCmdBindDescriptorSets(cmd_, NativeBindPoint(point), Native(layout), first_set, count, native, 0, nullptr);
}

void CommandList::PushConstants(PipelineLayoutHandle layout, ShaderStageFlags stages, uint32_t offset, uint32_t size, const void* data) {
    vkCmdPushConstants(cmd_, Native(layout), stages.bits, offset, size, data);
}

void CommandList::BindVertexBuffer(BufferHandle buffer, uint64_t offset) {
    const VkBuffer native = Native(buffer);
    const VkDeviceSize native_offset = offset;
    vkCmdBindVertexBuffers(cmd_, 0, 1, &native, &native_offset);
}

void CommandList::BindIndexBuffer(BufferHandle buffer, uint64_t offset, IndexType type) {
    vkCmdBindIndexBuffer(cmd_, Native(buffer), offset, static_cast<VkIndexType>(type));
}

void CommandList::Draw(uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance) {
    vkCmdDraw(cmd_, vertices, instances, first_vertex, first_instance);
}

void CommandList::DrawIndexed(uint32_t indices, uint32_t instances, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
    vkCmdDrawIndexed(cmd_, indices, instances, first_index, vertex_offset, first_instance);
}

void CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    vkCmdDispatch(cmd_, x, y, z);
}

void CommandList::CopyImage(const ImageResource& src, Layout src_layout, const ImageResource& dst, Layout dst_layout,
                            std::span<const ImageCopy> regions) {
    std::vector<VkImageCopy> native(regions.size());
    const uint32_t count = static_cast<uint32_t>(regions.size());
    for (uint32_t i = 0; i < count; ++i) {
        const ImageCopy& r = regions[i];
        native[i] = {};
        native[i].srcSubresource = NativeLayers(r.src);
        native[i].srcOffset = {r.src_offset[0], r.src_offset[1], r.src_offset[2]};
        native[i].dstSubresource = NativeLayers(r.dst);
        native[i].dstOffset = {r.dst_offset[0], r.dst_offset[1], r.dst_offset[2]};
        native[i].extent = {r.extent.width, r.extent.height, r.extent.depth};
    }
    vkCmdCopyImage(cmd_, Native(src.image), Native(src_layout), Native(dst.image), Native(dst_layout), count, native.data());
}

void CommandList::CopyImageToBuffer(const ImageResource& src, Layout src_layout, const BufferResource& dst,
                                    std::span<const BufferImageCopy> regions) {
    std::vector<VkBufferImageCopy> native(regions.size());
    const uint32_t count = static_cast<uint32_t>(regions.size());
    for (uint32_t i = 0; i < count; ++i) {
        native[i] = NativeCopy(regions[i]);
    }
    vkCmdCopyImageToBuffer(cmd_, Native(src.image), Native(src_layout), Native(dst.buffer), count, native.data());
}

void CommandList::CopyBufferToImage(const BufferResource& src, const ImageResource& dst, Layout dst_layout,
                                    std::span<const BufferImageCopy> regions) {
    std::vector<VkBufferImageCopy> native(regions.size());
    const uint32_t count = static_cast<uint32_t>(regions.size());
    for (uint32_t i = 0; i < count; ++i) {
        native[i] = NativeCopy(regions[i]);
    }
    vkCmdCopyBufferToImage(cmd_, Native(src.buffer), Native(dst.image), Native(dst_layout), count, native.data());
}

void CommandList::CopyBuffer(const BufferResource& src, uint64_t src_offset, const BufferResource& dst, uint64_t dst_offset, uint64_t size) {
    const VkBufferCopy region{src_offset, dst_offset, size};
    vkCmdCopyBuffer(cmd_, Native(src.buffer), Native(dst.buffer), 1, &region);
}

void CommandList::ClearColorImage(const ImageResource& image, Layout layout, const ClearColor& value, uint32_t mip_levels, uint32_t layers) {
    VkClearColorValue native;
    static_assert(sizeof(native) == sizeof(value));
    std::memcpy(&native, &value, sizeof(value));
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, layers};
    vkCmdClearColorImage(cmd_, Native(image.image), Native(layout), &native, 1, &range);
}

void CommandList::FillBuffer(const BufferResource& buffer, uint64_t offset, uint64_t size, uint32_t value) {
    vkCmdFillBuffer(cmd_, Native(buffer.buffer), offset, size, value);
}

void CommandList::ResetQueries(QueryPoolHandle pool, uint32_t first, uint32_t count) {
    vkCmdResetQueryPool(cmd_, Native(pool), first, count);
}

void CommandList::WriteTimestamp(QueryPoolHandle pool, uint32_t index) {
    vkCmdWriteTimestamp2(cmd_, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, Native(pool), index);
}

void CommandList::BeginLabel(const char* name) {
    if (pt::g_checkpoints && vkCmdSetCheckpointNV) {
        vkCmdSetCheckpointNV(cmd_, name);
    }
    if (vkCmdBeginDebugUtilsLabelEXT) {
        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        vkCmdBeginDebugUtilsLabelEXT(cmd_, &label);
    }
}

void CommandList::EndLabel() {
    if (vkCmdEndDebugUtilsLabelEXT) {
        vkCmdEndDebugUtilsLabelEXT(cmd_);
    }
}

}
