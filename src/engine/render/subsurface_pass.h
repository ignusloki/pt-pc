#pragma once

#include <functional>
#include <memory>

#include "engine/render/gpu/gpu_device.h"
#include "engine/render/render_util.h"
#include "engine/render/vk_context.h"

namespace pt {

class SubsurfacePass {
public:
    bool Init(gpu::Device& device, gpu::BindingLayoutHandle textures_layout, gpu::BindingLayoutHandle frame_layout, gpu::Format light_format,
              gpu::SamplerHandle point_clamp);
    void Shutdown();
    bool CreateTargets(gpu::Extent2D extent);
    bool HasTargets() const { return copy_.Valid() && temp_.Valid(); }
    void DestroyTargets();
    void Record(gpu::CommandList& list, RenderTarget& diffuse, uint32_t view_index, const std::function<void()>& bind_sets);

private:
    gpu::Device* device_ = nullptr;
    gpu::Format format_ = gpu::Format::Undefined;
    gpu::SamplerHandle sampler_;
    gpu::BindingLayoutHandle set_layout_;
    gpu::BindingPoolHandle pool_;
    gpu::BindingSetHandle sets_[2] = {};
    gpu::PipelineLayoutHandle layout_;
    gpu::PipelineHandle pipeline_;
    RenderTarget copy_;
    RenderTarget temp_;
};

}
