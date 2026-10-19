#include "engine/render/scene_renderer.h"
#include "engine/core/log.h"

namespace pt {

bool SceneRenderer::EnsureFsrRrTargets() {
    if (!fsr_rr_prepare_ || !fsr_rr_resolve_ || !fsr_rr_motion_trace_ || !fsr_rr_velocity_) return false;
    bool ready = true;
    for (const RenderTarget* t : {&fsr_rr_input_[0], &fsr_rr_input_[1], &fsr_rr_input_[2], &fsr_rr_input_[3],
                                &fsr_rr_depth_, &fsr_rr_motion_, &fsr_rr_normal_}) ready &= t->Valid() && t->Extent() == extent_;
    if (ready) return true;
    const auto usage = gpu::ImageUsage::ColorAttachment | gpu::ImageUsage::Sampled | gpu::ImageUsage::TransferSrc;
    for (RenderTarget* t : {&fsr_rr_input_[0], &fsr_rr_input_[1], &fsr_rr_input_[2], &fsr_rr_input_[3],
                          &fsr_rr_depth_, &fsr_rr_motion_, &fsr_rr_normal_}) {
        gpu_->DestroyImage(t->image);
        *t = {};
        const auto format = t == &fsr_rr_depth_ ? gpu::Format::R32Sfloat : gpu::Format::R16G16B16A16Sfloat;
        if (!CreateTarget(*t, format, extent_, usage, gpu::Aspect::Color)) return false;
    }
    LogInfo("fsr rr: radiance and geometry inputs {}x{}", extent_.width, extent_.height);
    return true;
}

void SceneRenderer::RecordFsrRayRegeneration(gpu::CommandList& cmd, const ViewSetup& view) {
    for (auto& t : fsr_rr_input_) cmd.UseTargets({{&t, gpu::Layout::ColorAttachment}});
    cmd.UseTargets({{&fsr_rr_depth_, gpu::Layout::ColorAttachment}, {&fsr_rr_normal_, gpu::Layout::ColorAttachment}, {&depth_, gpu::Layout::DepthReadOnly},
                    {&motion_, gpu::Layout::ShaderReadOnly}});
    BindSets(cmd, gpu::BindPoint::Graphics);
    const auto rt_set = rt_->Set(renderer_->FrameIndex());
    cmd.BindSets(gpu::BindPoint::Graphics, rt_->Layout(), 2, {&rt_set, 1});
    cmd.BeginPass(extent_, {{&fsr_rr_input_[0], false, {}}, {&fsr_rr_input_[1], false, {}},
                            {&fsr_rr_input_[2], false, {}}, {&fsr_rr_input_[3], false, {}},
                            {&fsr_rr_depth_, false, {}}, {&fsr_rr_normal_, false, {}}});
    gpu::PassPush push;
    push.ids = glm::uvec4(view.index, pt_indirect_on_ ? 1u : 0u, 0u, 0u);
    push.m = previous_view_projection_;
    Fullscreen(cmd, fsr_rr_prepare_, push);
    cmd.EndPass();
    RecordUpscaleMotion(cmd, view, true);
    for (auto& t : fsr_rr_input_) cmd.UseTargets({{&t, gpu::Layout::ShaderReadOnly}});
    cmd.UseTargets({{&fsr_rr_depth_, gpu::Layout::ShaderReadOnly}, {&fsr_rr_motion_, gpu::Layout::ShaderReadOnly},
                    {&fsr_rr_normal_, gpu::Layout::ShaderReadOnly}});
    RenderTarget* output[] = {&rt_full_images_[10], &rt_full_images_[15], &rt_full_images_[11], &rt_full_images_[16]};
    for (auto* t : output) cmd.UseTargets({{t, gpu::Layout::General}});
    cmd.GlobalBarrier(gpu::PipelineStage::ComputeShader | gpu::PipelineStage::FragmentShader,
                      gpu::Access::ShaderStorageWrite | gpu::Access::ShaderStorageRead,
                      gpu::PipelineStage::ComputeShader, gpu::Access::ShaderStorageWrite | gpu::Access::ShaderStorageRead);
    const auto wrap = [](const RenderTarget& t) {
        UpscaleImage image;
        image.handle = t.image.image;
        image.extent = gpu::vk::Native(t.Extent());
        return image;
    };
    RayDenoiseDispatch d;
    d.list = &cmd;
    for (unsigned i = 0; i < 4; ++i) {
        d.input[i] = wrap(fsr_rr_input_[i]);
        d.output[i] = wrap(*output[i]);
    }
    d.depth = wrap(fsr_rr_depth_);
    d.motion = wrap(fsr_rr_motion_);
    d.normal_roughness = wrap(fsr_rr_normal_);
    d.diffuse_albedo = wrap(rr_diffuse_albedo_);
    d.specular_albedo = wrap(rr_specular_albedo_);
    d.render = gpu::vk::Native(extent_);
    d.jitter = up_.jitter_pixels;
    d.eye = camera_.position;
    d.world_to_view = camera_.View();
    d.view_to_clip = camera_.Projection(static_cast<float>(output_extent_.width) / output_extent_.height);
    d.near_plane = camera_.near_plane;
    d.frame_index = frame_counter_;
    d.reset = up_.reset;
    bool ok = false;
    if (fsr_rr_input_check_) {
        static bool logged = false;
        if (!logged) {
            LogInfo("fsr rr input check: native four-signal pass-through, no AMD inference");
            logged = true;
        }
        for (unsigned i = 0; i < 4; ++i) {
            cmd.UseTargets({{&fsr_rr_input_[i], gpu::Layout::TransferSrc}, {output[i], gpu::Layout::TransferDst}});
            gpu::ImageCopy copy;
            copy.src = {gpu::Aspect::Color, 0, 0, 1}; copy.dst = copy.src;
            copy.extent = {extent_.width, extent_.height, 1};
            cmd.CopyImage(fsr_rr_input_[i].image, gpu::Layout::TransferSrc, output[i]->image, gpu::Layout::TransferDst, {&copy, 1});
            cmd.UseTargets({{output[i], gpu::Layout::General}, {&fsr_rr_input_[i], gpu::Layout::ShaderReadOnly}});
        }
        ok = true;
    } else ok = up_.backend->Denoise(d);
    BindSets(cmd, gpu::BindPoint::Graphics);
    BindSets(cmd, gpu::BindPoint::Compute);
    if (!ok) return;
    cmd.GlobalBarrier(gpu::PipelineStage::ComputeShader, gpu::Bits(gpu::Access::ShaderStorageWrite),
                      gpu::PipelineStage::FragmentShader, gpu::Bits(gpu::Access::ShaderStorageRead));
    cmd.UseTargets({{&diffuse_, gpu::Layout::ColorAttachment}, {&specular_, gpu::Layout::ColorAttachment}});
    cmd.BeginPass(extent_, {{&diffuse_, false, {}}, {&specular_, false, {}}});
    cmd.BindSets(gpu::BindPoint::Graphics, rt_->Layout(), 2, {&rt_set, 1});
    Fullscreen(cmd, fsr_rr_resolve_, push);
    cmd.EndPass();
    cmd.UseTargets({{&diffuse_, gpu::Layout::ShaderReadOnly}, {&specular_, gpu::Layout::ShaderReadOnly}});
}

}
