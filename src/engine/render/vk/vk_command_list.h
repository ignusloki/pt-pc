#pragma once

#include <volk.h>

#include "engine/render/gpu/gpu_command_list.h"
#include "engine/render/gpu/gpu_vk_native.h"

namespace pt::gpu::vk {

class CommandList final : public gpu::CommandList {
public:
    explicit CommandList(VkCommandBuffer cmd) : cmd_(cmd) {}

    VkCommandBuffer Buffer() const { return cmd_; }

    Backend GetBackend() const override { return Backend::Vulkan; }

    void UseTargets(std::span<const TargetUse> uses) override;
    using gpu::CommandList::UseTargets;
    void Barrier(const ImageBarrier& barrier) override;
    void BufferBarrier(BufferHandle buffer, PipelineStage src_stage, uint64_t src_access, PipelineStage dst_stage, uint64_t dst_access) override;
    void GlobalBarrier(PipelineStage src_stage, uint64_t src_access, PipelineStage dst_stage, uint64_t dst_access) override;

    void BeginPass(Rect2D area, std::span<const ColorOutput> colors, RenderTarget* depth, bool depth_read_only, bool clear_depth) override;
    using gpu::CommandList::BeginPass;
    void EndPass() override;
    void BeginRendering(Rect2D area, std::span<const Attachment> colors) override;

    void SetViewport(Rect2D area) override;
    void SetCullMode(CullMode mode) override;
    void SetFrontFace(FrontFace face) override;
    void SetDepthBias(float constant, float clamp, float slope) override;
    void SetDepthBounds(float min, float max) override;

    void BindPipeline(BindPoint point, PipelineHandle pipeline) override;
    void BindSets(BindPoint point, PipelineLayoutHandle layout, uint32_t first_set, std::span<const BindingSetHandle> sets) override;
    void PushConstants(PipelineLayoutHandle layout, ShaderStageFlags stages, uint32_t offset, uint32_t size, const void* data) override;
    void BindVertexBuffer(BufferHandle buffer, uint64_t offset) override;
    void BindIndexBuffer(BufferHandle buffer, uint64_t offset, IndexType type) override;

    void Draw(uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance) override;
    void DrawIndexed(uint32_t indices, uint32_t instances, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) override;
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

    void CopyImage(const ImageResource& src, Layout src_layout, const ImageResource& dst, Layout dst_layout,
                   std::span<const ImageCopy> regions) override;
    void CopyImageToBuffer(const ImageResource& src, Layout src_layout, const BufferResource& dst,
                           std::span<const BufferImageCopy> regions) override;
    void CopyBufferToImage(const BufferResource& src, const ImageResource& dst, Layout dst_layout,
                           std::span<const BufferImageCopy> regions) override;
    void CopyBuffer(const BufferResource& src, uint64_t src_offset, const BufferResource& dst, uint64_t dst_offset, uint64_t size) override;
    void ClearColorImage(const ImageResource& image, Layout layout, const ClearColor& value, uint32_t mip_levels, uint32_t layers) override;
    using gpu::CommandList::ClearColorImage;
    void FillBuffer(const BufferResource& buffer, uint64_t offset, uint64_t size, uint32_t value) override;

    void ResetQueries(QueryPoolHandle pool, uint32_t first, uint32_t count) override;
    void WriteTimestamp(QueryPoolHandle pool, uint32_t index) override;
    void BeginLabel(const char* name) override;
    void EndLabel() override;

private:
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
};

inline VkCommandBuffer Native(gpu::CommandList& list) {
    return static_cast<CommandList&>(list).Buffer();
}

}
