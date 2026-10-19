#pragma once

#include <cstdint>
#include <initializer_list>
#include <span>

#include "engine/render/gpu/gpu_resources.h"

namespace pt::gpu {

class CommandList {
public:
    virtual ~CommandList() = default;

    virtual Backend GetBackend() const = 0;

    virtual void UseTargets(std::span<const TargetUse> uses) = 0;
    void UseTargets(std::initializer_list<TargetUse> uses) { UseTargets(std::span<const TargetUse>(uses.begin(), uses.size())); }
    virtual void Barrier(const ImageBarrier& barrier) = 0;
    virtual void BufferBarrier(BufferHandle buffer, PipelineStage src_stage, uint64_t src_access, PipelineStage dst_stage,
                               uint64_t dst_access) = 0;
    virtual void GlobalBarrier(PipelineStage src_stage, uint64_t src_access, PipelineStage dst_stage, uint64_t dst_access) = 0;

    virtual void BeginPass(Rect2D area, std::span<const ColorOutput> colors, RenderTarget* depth, bool depth_read_only, bool clear_depth) = 0;
    void BeginPass(Extent2D extent, std::initializer_list<ColorOutput> colors, RenderTarget* depth = nullptr, bool depth_read_only = false,
                   bool clear_depth = false) {
        BeginPass(Rect2D{{0, 0}, extent}, std::span<const ColorOutput>(colors.begin(), colors.size()), depth, depth_read_only, clear_depth);
    }
    virtual void EndPass() = 0;
    virtual void BeginRendering(Rect2D area, std::span<const Attachment> colors) = 0;

    virtual void SetViewport(Rect2D area) = 0;
    virtual void SetCullMode(CullMode mode) = 0;
    virtual void SetFrontFace(FrontFace face) = 0;
    virtual void SetDepthBias(float constant, float clamp, float slope) = 0;
    virtual void SetDepthBounds(float min, float max) = 0;

    virtual void BindPipeline(BindPoint point, PipelineHandle pipeline) = 0;
    virtual void BindSets(BindPoint point, PipelineLayoutHandle layout, uint32_t first_set, std::span<const BindingSetHandle> sets) = 0;
    virtual void PushConstants(PipelineLayoutHandle layout, ShaderStageFlags stages, uint32_t offset, uint32_t size, const void* data) = 0;
    virtual void BindVertexBuffer(BufferHandle buffer, uint64_t offset) = 0;
    virtual void BindIndexBuffer(BufferHandle buffer, uint64_t offset, IndexType type) = 0;

    virtual void Draw(uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance) = 0;
    virtual void DrawIndexed(uint32_t indices, uint32_t instances, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) = 0;
    virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    virtual void CopyImage(const ImageResource& src, Layout src_layout, const ImageResource& dst, Layout dst_layout,
                           std::span<const ImageCopy> regions) = 0;
    virtual void CopyImageToBuffer(const ImageResource& src, Layout src_layout, const BufferResource& dst,
                                   std::span<const BufferImageCopy> regions) = 0;
    virtual void CopyBufferToImage(const BufferResource& src, const ImageResource& dst, Layout dst_layout,
                                   std::span<const BufferImageCopy> regions) = 0;
    virtual void CopyBuffer(const BufferResource& src, uint64_t src_offset, const BufferResource& dst, uint64_t dst_offset, uint64_t size) = 0;
    virtual void ClearColorImage(const ImageResource& image, Layout layout, const ClearColor& value, uint32_t mip_levels, uint32_t layers) = 0;
    void ClearColorImage(const ImageResource& image, Layout layout, const ClearColor& value) { ClearColorImage(image, layout, value, ~0u, ~0u); }
    virtual void FillBuffer(const BufferResource& buffer, uint64_t offset, uint64_t size, uint32_t value) = 0;

    virtual void ResetQueries(QueryPoolHandle pool, uint32_t first, uint32_t count) = 0;
    virtual void WriteTimestamp(QueryPoolHandle pool, uint32_t index) = 0;
    virtual void BeginLabel(const char* name) = 0;
    virtual void EndLabel() = 0;
};

}
