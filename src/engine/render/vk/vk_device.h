#pragma once

#include <map>
#include <mutex>
#include <string>

#include "engine/render/gpu/gpu_device.h"
#include "engine/render/vk/vk_command_list.h"
#include "engine/render/vk_context.h"

namespace pt::gpu::vk {

BufferResource ToGpu(const pt::vk::Buffer& buffer);
pt::vk::Buffer FromGpu(const BufferResource& buffer);

class Device final : public gpu::Device {
public:
    explicit Device(pt::vk::Context& ctx);

    pt::vk::Context& Context() { return ctx_; }

    const DeviceInfo& Info() const override { return info_; }

    bool CreateImage(const ImageDesc& desc, ImageResource& out) override;
    void DestroyImage(ImageResource& image) override;
    bool CreateBuffer(const BufferDesc& desc, BufferResource& out) override;
    void DestroyBuffer(BufferResource& buffer) override;
    void FlushMapped(const BufferResource& buffer, uint64_t offset, uint64_t size) override;
    void InvalidateMapped(const BufferResource& buffer, uint64_t offset, uint64_t size) override;
    uint64_t AllocatedBytes() const override;
    void DeviceLocalUsage(double& used, double& budget) const override;
    uint64_t ImageBytes(const ImageResource& image) const override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle sampler) override;

    BindingLayoutHandle CreateBindingLayout(const BindingLayoutDesc& desc) override;
    void DestroyBindingLayout(BindingLayoutHandle layout) override;
    BindingPoolHandle CreateBindingPool(const BindingPoolDesc& desc) override;
    void DestroyBindingPool(BindingPoolHandle pool) override;
    BindingSetHandle AllocateBindingSet(BindingPoolHandle pool, BindingLayoutHandle layout, uint32_t variable_count = 0) override;
    bool AllocateBindingSets(BindingPoolHandle pool, std::span<const BindingLayoutHandle> layouts, std::span<BindingSetHandle> out) override;
    void WriteBindings(std::span<const BindingWrite> writes) override;
    PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc& desc) override;
    void DestroyPipelineLayout(PipelineLayoutHandle layout) override;

    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override;
    bool CreateGraphicsPipelines(std::span<const GraphicsPipelineDesc> descs, std::span<PipelineHandle> out) override;
    bool PreloadShaders(std::span<const char* const> names) override;
    void ReleaseShaders(std::span<const char* const> names) override;
    void DestroyPipeline(PipelineHandle pipeline) override;

    QueryPoolHandle CreateTimestampPool(uint32_t count) override;
    void DestroyQueryPool(QueryPoolHandle pool) override;
    bool ReadTimestamps(QueryPoolHandle pool, uint32_t first, uint32_t count, uint64_t* ticks) override;

    void Submit(const std::function<void(gpu::CommandList&)>& record) override;
    void SubmitDeferred(const std::function<void(gpu::CommandList&)>& record, BufferResource staging) override;
    void ReapDeferred(bool wait) override;
    void WaitIdle() override;

    gpu::CommandList* BeginFrame(uint32_t slot) override;
    bool EndFrame(uint32_t slot, RenderTarget* final_image) override;

    bool ReadImage(RenderTarget& image, std::vector<uint8_t>& out) override;

private:
    VkPipeline CreatePipeline(const GraphicsPipelineDesc& desc, VkShaderModule vert, VkShaderModule frag);

    pt::vk::Context& ctx_;
    std::mutex modules_mutex_;
    std::map<std::string, VkShaderModule> modules_;
    DeviceInfo info_;
};

}
