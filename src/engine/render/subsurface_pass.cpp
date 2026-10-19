#include "engine/render/subsurface_pass.h"

#include <algorithm>

#include "engine/core/log.h"
#include "engine/render/gpu_types.h"
#include "engine/render/vk/vk_device.h"

namespace pt {
namespace {

constexpr gpu::ShaderStageFlags kPushStages = gpu::ShaderStage::Vertex | gpu::ShaderStage::Fragment | gpu::ShaderStage::Compute;

}

bool SubsurfacePass::Init(gpu::Device& device, gpu::BindingLayoutHandle textures_layout, gpu::BindingLayoutHandle frame_layout,
                          gpu::Format light_format, gpu::SamplerHandle point_clamp) {
    device_ = &device;
    format_ = light_format;
    sampler_ = point_clamp;
    gpu::BindingLayoutDesc layout_desc;
    layout_desc.bindings = {{0, gpu::BindingType::CombinedImageSampler, 1, gpu::ShaderStage::Fragment},
                            {1, gpu::BindingType::CombinedImageSampler, 1, gpu::ShaderStage::Fragment}};
    set_layout_ = device.CreateBindingLayout(layout_desc);
    if (!set_layout_) {
        return false;
    }
    gpu::BindingPoolDesc pool_desc;
    pool_desc.sizes = {{gpu::BindingType::CombinedImageSampler, 4}};
    pool_desc.max_sets = 2;
    pool_ = device.CreateBindingPool(pool_desc);
    if (!pool_) {
        return false;
    }
    const gpu::BindingLayoutHandle layouts[2] = {set_layout_, set_layout_};
    if (!device.AllocateBindingSets(pool_, layouts, sets_)) {
        return false;
    }
    gpu::PipelineLayoutDesc pipeline_layout;
    pipeline_layout.sets = {textures_layout, frame_layout, set_layout_};
    pipeline_layout.push_bytes = 128;
    pipeline_layout.push_stages = kPushStages;
    layout_ = device.CreatePipelineLayout(pipeline_layout);
    if (!layout_) {
        return false;
    }
    gpu::GraphicsPipelineDesc desc;
    desc.layout = layout_;
    desc.fragment = "subsurface.frag";
    desc.colors = {format_};
    pipeline_ = device.CreateGraphicsPipeline(desc);
    if (!pipeline_) {
        LogError("subsurface: pipeline failed");
        return false;
    }
    return true;
}

void SubsurfacePass::Shutdown() {
    if (!device_) {
        return;
    }
    DestroyTargets();
    device_->DestroyPipeline(pipeline_);
    device_->DestroyPipelineLayout(layout_);
    device_->DestroyBindingPool(pool_);
    device_->DestroyBindingLayout(set_layout_);
    pipeline_ = {};
    layout_ = {};
    pool_ = {};
    sets_[0] = sets_[1] = {};
    set_layout_ = {};
    device_ = nullptr;
}

bool SubsurfacePass::CreateTargets(gpu::Extent2D extent) {
    DestroyTargets();
    gpu::ImageDesc desc;
    desc.format = format_;
    desc.extent = {std::max(extent.width, 1u), std::max(extent.height, 1u), 1};
    desc.name = "subsurface";
    desc.usage = gpu::ImageUsage::TransferDst | gpu::ImageUsage::Sampled;
    const bool copy_ok = device_->CreateImage(desc, copy_.image);
    desc.usage = gpu::ImageUsage::ColorAttachment | gpu::ImageUsage::Sampled;
    if (!copy_ok || !device_->CreateImage(desc, temp_.image)) {
        LogError("subsurface: cannot create targets {}x{}", extent.width, extent.height);
        DestroyTargets();
        return false;
    }
    device_->Submit([&](gpu::CommandList& list) {
        list.UseTargets({{&copy_, gpu::Layout::ShaderReadOnly}, {&temp_, gpu::Layout::ShaderReadOnly}});
    });
    const gpu::ImageBinding copy{copy_.image.view, gpu::Layout::ShaderReadOnly, sampler_, &copy_.image};
    const gpu::ImageBinding temp{temp_.image.view, gpu::Layout::ShaderReadOnly, sampler_, &temp_.image};
    const gpu::ImageBinding* infos[4] = {&copy, &copy, &copy, &temp};
    gpu::BindingWrite writes[4];
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i].set = sets_[i / 2];
        writes[i].binding = i % 2;
        writes[i].type = gpu::BindingType::CombinedImageSampler;
        writes[i].images = std::span(infos[i], 1);
    }
    device_->WriteBindings(writes);
    return true;
}

void SubsurfacePass::DestroyTargets() {
    if (!device_) {
        return;
    }
    device_->DestroyImage(copy_.image);
    device_->DestroyImage(temp_.image);
    copy_ = RenderTarget{};
    temp_ = RenderTarget{};
}

void SubsurfacePass::Record(gpu::CommandList& list, RenderTarget& diffuse, uint32_t view_index, const std::function<void()>& bind_sets) {
    if (!pipeline_ || !copy_.Valid()) {
        return;
    }
    list.UseTargets({{&diffuse, gpu::Layout::TransferSrc}, {&copy_, gpu::Layout::TransferDst}});
    gpu::ImageCopy region;
    region.extent = {copy_.Extent().width, copy_.Extent().height, 1};
    list.CopyImage(diffuse.image, gpu::Layout::TransferSrc, copy_.image, gpu::Layout::TransferDst, std::span(&region, 1));
    list.UseTargets({{&diffuse, gpu::Layout::ShaderReadOnly}, {&copy_, gpu::Layout::ShaderReadOnly}});
    struct Step {
        RenderTarget* target;
        uint32_t mode;
    };
    const Step steps[2] = {{&temp_, 1u}, {&diffuse, 2u}};
    for (const Step& step : steps) {
        list.UseTargets({{step.target, gpu::Layout::ColorAttachment}});
        list.BeginPass(step.target->Extent(), {{step.target, false, {}}});
        list.BindPipeline(gpu::BindPoint::Graphics, pipeline_);
        bind_sets();
        list.BindSets(gpu::BindPoint::Graphics, layout_, 2, std::span(&sets_[step.mode - 1], 1));
        gpu::PassPush push;
        push.ids = glm::uvec4(view_index, step.mode, 0, 0);
        list.PushConstants(layout_, kPushStages, 0, sizeof(push), &push);
        list.Draw(3, 1, 0, 0);
        list.EndPass();
        list.UseTargets({{step.target, gpu::Layout::ShaderReadOnly}});
    }
}

}
