#include "engine/render/scene_renderer.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

#include "engine/core/log.h"
#include "engine/render/render_util.h"

namespace pt {
namespace {

enum FullImage : uint32_t {
    kUnshadowed = 0,
    kShadowed = 1,
    kRatioHistory0 = 2,
    kDepthHistory0 = 4,
    kRatioSpatial = 6,
    kGiRaw = 7,
    kGiHistory0 = 8,
    kGiPing = 10,
    kGiPong = 11,
    kGlossyRaw = 12,
    kGlossyHistory0 = 13,
    kGlossyPing = 15,
    kGlossyPong = 16,
};

float EnvFloat(const char* name, float fallback, float lo, float hi) {
    const char* value = std::getenv(name);
    return value ? std::clamp(static_cast<float>(std::atof(value)), lo, hi) : fallback;
}

constexpr float kGiReach = 8.0f;
constexpr float kGlossyReach = 30.0f;
constexpr float kGlossyFade = 12.0f;
constexpr float kGlossyRoughness = 0.75f;
constexpr float kGiFrames = 24.0f;
constexpr float kGlossyFrames = 12.0f;
}

void SceneRenderer::CreateFullPipelines(gpu::Format light_format, bool depth_bounds) {
    gpu::GraphicsPipelineDesc volume;
    volume.layout = rt_->Layout();
    volume.vertex = "volume.vert";
    volume.fragment = "light_full.frag";
    volume.colors = {gpu::Format::R16G16B16A16Sfloat, light_format, gpu::Format::R16G16B16A16Sfloat};
    volume.depth = kDepthTargetFormat;
    volume.depth_test = true;
    volume.depth_compare = gpu::CompareOp::LessOrEqual;
    volume.blends = {gpu::BlendMode::AdditiveAlpha, gpu::BlendMode::Additive, gpu::BlendMode::AdditiveAlpha};
    volume.depth_bounds = depth_bounds;
    light_full_ = gpu_->CreateGraphicsPipeline(volume);
    rt_classify_ = gpu_->CreateComputePipeline({rt_->Layout(), "rt_light_classify.comp"});
    rt_gi_trace_ = gpu_->CreateComputePipeline({rt_->Layout(), "rt_gi.comp"});
    rt_glossy_trace_ = gpu_->CreateComputePipeline({rt_->Layout(), "rt_glossy.comp"});
    rt_full_filter_ = gpu_->CreateComputePipeline({rt_->Layout(), "rt_full_filter.comp"});
    gpu::GraphicsPipelineDesc resolve;
    resolve.layout = rt_->Layout();
    resolve.fragment = "rt_full_resolve.frag";
    resolve.colors = {light_format, light_format};
    resolve.blends = {gpu::BlendMode::Additive, gpu::BlendMode::Multiply};
    rt_full_resolve_ = gpu_->CreateGraphicsPipeline(resolve);
    resolve.blends = {gpu::BlendMode::None, gpu::BlendMode::Additive};
    resolve.write_masks = {gpu::ColorMask{}, gpu::kColorMaskAll};
    rt_full_resolve_glossy_ = gpu_->CreateGraphicsPipeline(resolve);
    resolve.blends = {gpu::BlendMode::Additive, gpu::BlendMode::None};
    resolve.write_masks = {gpu::kColorMaskAll, gpu::ColorMask{}};
    rt_full_resolve_gi_ = gpu_->CreateGraphicsPipeline(resolve);
    if (!light_full_ || !rt_classify_ || !rt_gi_trace_ || !rt_glossy_trace_ || !rt_full_filter_ || !rt_full_resolve_ || !rt_full_resolve_glossy_ ||
        !rt_full_resolve_gi_) {
        LogError("ray tracing: the Full tier's passes could not be created, the tier stays off");
        DestroyFullPipelines();
    }
    rt_gi_strength_ = EnvFloat("PT_RT_GI_STRENGTH", rt_gi_strength_, 0.0f, 8.0f);
    rt_gi_static_ = EnvFloat("PT_RT_GI_STATIC", rt_gi_static_, 0.0f, 4.0f);
    rt_glossy_strength_ = EnvFloat("PT_RT_GLOSSY_STRENGTH", rt_glossy_strength_, 0.0f, 8.0f);
    rt_handy_fill_ = EnvFloat("PT_RT_HANDY_FILL", rt_handy_fill_, 0.0f, 1.0f);
}

void SceneRenderer::DestroyFullPipelines() {
    for (gpu::PipelineHandle* p : {&light_full_, &rt_classify_, &rt_gi_trace_, &rt_glossy_trace_, &rt_full_filter_, &rt_full_resolve_, &rt_full_resolve_glossy_,
                          &rt_full_resolve_gi_}) {
        if (*p) {
            gpu_->DestroyPipeline(*p);
            *p = {};
        }
    }
}

void SceneRenderer::DestroyFullTargets() {
    for (RenderTarget& t : rt_full_images_) {
        gpu_->DestroyImage(t.image);
        t = RenderTarget{};
    }
    rt_full_history_ = false;
}

bool SceneRenderer::EnsureFullTargets() {
    const vk::AllocTag tag("ray tracing");
    if (rt_full_images_[0].Valid() && rt_full_images_[0].Extent().width == extent_.width && rt_full_images_[0].Extent().height == extent_.height &&
        rt_full_render_size_ == rr_frame_) {
        return true;
    }
    if (rt_full_failed_extent_.width == extent_.width && rt_full_failed_extent_.height == extent_.height &&
        std::chrono::steady_clock::now() - rt_full_failed_at_ < std::chrono::seconds(5)) {
        return false;
    }
    gpu_->WaitIdle();
    DestroyFullTargets();
    const gpu::Extent2D half = rr_frame_ ? extent_ : gpu::Extent2D{(extent_.width + 1) / 2, (extent_.height + 1) / 2};
    rt_full_render_size_ = rr_frame_;
    gpu::ImageViewHandle views[RayTracing::kFullImages]{};
    for (uint32_t i = 0; i < RayTracing::kFullImages; ++i) {
        const bool depth_history = i == kDepthHistory0 || i == kDepthHistory0 + 1;
        const gpu::Format format = depth_history ? gpu::Format::R32Sfloat : gpu::Format::R16G16B16A16Sfloat;
        gpu::ImageUsageFlags usage = gpu::ImageUsage::Storage | gpu::ImageUsage::TransferDst;
        if (i == kUnshadowed || i == kShadowed) {
            usage |= gpu::ImageUsage::ColorAttachment;
        }
        if (!CreateTarget(rt_full_images_[i], format, i >= kGiRaw ? half : extent_, usage, gpu::Aspect::Color)) {
            LogError("ray tracing: the Full tier's images could not be made at {}x{}", extent_.width, extent_.height);
            rt_full_failed_extent_ = extent_;
            rt_full_failed_at_ = std::chrono::steady_clock::now();
            DestroyFullTargets();
            return false;
        }
        views[i] = rt_full_images_[i].image.view;
    }
    gpu_->Submit([&](gpu::CommandList& cmd) {
        for (RenderTarget& t : rt_full_images_) {
            cmd.UseTargets({{&t, gpu::Layout::General}});
            const gpu::ClearColor value{};
            cmd.ClearColorImage(t.image, gpu::Layout::General, value, 1, 1);
        }
        for (uint32_t slot = 0; slot < Renderer::kFramesInFlight; ++slot) {
            const gpu::BufferResource& state = rt_->LightState(slot);
            cmd.FillBuffer(state, 0, ~0ull, 0);
        }
    });
    rt_->SetFullImages(views);
    rt_full_history_ = false;
    LogInfo("ray tracing: Full tier images {}x{} (bounce light and glossy reflections {}x{})", extent_.width, extent_.height, half.width, half.height);
    return true;
}

void SceneRenderer::SetupFullLights() {
    if (rt_path_) {
        SetupPathLights();
        return;
    }
    if (!RtFullAny()) {
        return;
    }
    const uint32_t samples = raytracing.soft_shadows ? 4u : 1u;
    static const bool authored_strength = [] {
        const char* v = std::getenv("PT_RT_FULL_STRENGTH");
        return v && std::string_view(v) == "authored";
    }();
    for (uint32_t i = 0; i < light_count_ && i < light_sources_.size(); ++i) {
        gpu::Light& g = frame_->lights[i];
        const SceneLight& l = *light_sources_[i];
        const bool hidden = (l.hidden_views & 1u) != 0;
        const bool handy_fill = l.id >= kHandyReflectionId && l.id < kHandyReflectionId + 4;
        const bool dynamic = l.id == kHandyLightId || l.name.empty() || l.priority == 0x80;
        g.shadow_cone.w = hidden ? -1.0f : handy_fill ? 0.0f : dynamic ? 1.0f : rt_gi_static_;
        if (handy_fill && rt_gi_active_) {
            g.scales.y *= rt_handy_fill_;
        }
        if (hidden || handy_fill) {
            g.shadow_cone.z = 0.0f;
            continue;
        }
        g.shadow_cone.z = authored_strength && l.cast_shadow ? g.scales.z : 1.0f;
        if (!l.cast_shadow && l.type == LightType::Spot) {
            g.shadow_cone.x = -2.0f;
            g.shadow_cone.y = 1.0f;
        }
        if (rt_full_shadows_) {
            const uint32_t casters = l.id == kMirrorLightId ? 32u : 128u;
            g.info.x = static_cast<int>(casters | (samples << 16) | (l.cast_shadow ? 1u << 24 : 0u));
        }
    }
}

void SceneRenderer::RecordRtFullTrace(gpu::CommandList& cmd, const ViewSetup& view) {
    const bool cut = glm::distance(rt_full_previous_eye_, view.eye) > kCameraCutDistance;
    const uint32_t previous = rt_full_written_;
    const uint32_t history = previous | (rt_full_history_ && !cut ? 0u : 2u);
    const gpu::Extent2D half{(extent_.width + 1) / 2, (extent_.height + 1) / 2};
    if (static const bool log_frames = std::getenv("PT_RT_LOG") != nullptr; log_frames) {
        LogInfo("ray tracing: full tier frame {}: history {} (eye moved {:.3f} m), reads {}", frame_counter_, (history & 2u) == 0,
                glm::distance(rt_full_previous_eye_, view.eye), previous);
    }
    BindSets(cmd, gpu::BindPoint::Compute);
    const gpu::BindingSetHandle rt_set = rt_->Set(renderer_->FrameIndex());
    cmd.BindSets(gpu::BindPoint::Compute, rt_->Layout(), 2, {&rt_set, 1});
    if (rt_path_) {
        cmd.UseTargets({{&depth_, gpu::Layout::DepthReadOnly}, {&normal_, gpu::Layout::ShaderReadOnly},
                         {&material_, gpu::Layout::ShaderReadOnly}, {&albedo_, gpu::Layout::ShaderReadOnly}});
    } else {
        cmd.UseTargets({{&depth_, gpu::Layout::DepthReadOnly}, {&normal_, gpu::Layout::ShaderReadOnly},
                         {&material_, gpu::Layout::ShaderReadOnly}});
    }
    gpu::PassPush push;
    push.ids = glm::uvec4(view.index, light_count_, static_cast<uint32_t>(frame_counter_ & 0xFFFFFFu), probe_count_);
    push.m = rt_full_previous_;
    push.f1 = glm::vec4(rt_full_previous_jitter_, 0.0f, 0.0f);
    if (light_count_ > 0) {
        cmd.BindPipeline(gpu::BindPoint::Compute, rt_classify_);
        PushConstants(cmd, &push, sizeof(push));
        cmd.Dispatch(light_count_, 1, 1);
    }
    cmd.GlobalBarrier(gpu::PipelineStage::ComputeShader, gpu::Bits(gpu::Access::ShaderStorageWrite), gpu::PipelineStage::ComputeShader | gpu::PipelineStage::FragmentShader, gpu::Bits(gpu::Access::ShaderStorageRead));
    Stamp(cmd, 18);
    if (rt_path_) {
        RecordPathTrace(cmd, view);
    } else {
        for (uint32_t s = 25; s <= 28; ++s) {
            Stamp(cmd, s);
        }
    }
    const uint32_t half_x = (half.width + 7) / 8;
    const uint32_t half_y = (half.height + 7) / 8;
    auto filter = [&](uint32_t mode, std::initializer_list<uint32_t> images) {
        for (uint32_t i : images) {
            cmd.UseTargets({{&rt_full_images_[i], gpu::Layout::General}});
        }
        cmd.BindPipeline(gpu::BindPoint::Compute, rt_full_filter_);
        push.ids.z = mode;
        push.ids.w = history;
        push.f1 = glm::vec4(rt_full_previous_jitter_, 0.0f, 0.0f);
        static const float gi_frames = EnvFloat("PT_RT_GI_FRAMES", kGiFrames, 1.0f, 256.0f);
        static const float glossy_frames = EnvFloat("PT_RT_GLOSSY_FRAMES", kGlossyFrames, 1.0f, 256.0f);
        static const float filter_debug = EnvFloat("PT_RT_FILTER_DEBUG", 0.0f, 0.0f, 1.0f);
        static const float gi_debug = EnvFloat("PT_RT_GI_DEBUG", 0.0f, 0.0f, 1.0f);
        push.f0 = glm::vec4(gi_frames, glossy_frames, filter_debug, gi_debug);
        PushConstants(cmd, &push, sizeof(push));
        cmd.Dispatch(half_x, half_y, 1);
    };
    if (rt_gi_active_) {
        cmd.BindPipeline(gpu::BindPoint::Compute, rt_gi_trace_);
        push.ids.z = static_cast<uint32_t>(frame_counter_ & 0xFFFFFFu);
        static const float gi_debug = EnvFloat("PT_RT_GI_DEBUG", 0.0f, 0.0f, 1.0f);
        push.f0 = glm::vec4(kGiReach, 3.0f, gi_debug, 0.0f);
        PushConstants(cmd, &push, sizeof(push));
        cmd.Dispatch(half_x, half_y, 1);
        filter(3, {kGiRaw, kGiHistory0, kGiHistory0 + 1});
        filter(4, {kGiHistory0, kGiHistory0 + 1});
        filter(5, {kGiPing});
        filter(6, {kGiPong});
        cmd.UseTargets({{&rt_full_images_[kGiPing], gpu::Layout::General}});
    }
    Stamp(cmd, 19);
    if (rt_glossy_active_) {
        cmd.BindPipeline(gpu::BindPoint::Compute, rt_glossy_trace_);
        push.ids.z = static_cast<uint32_t>(frame_counter_ & 0xFFFFFFu);
        const bool floors = toggles.local_reflections && lighting_->screen.local_reflections;
        push.f0 = glm::vec4(kGlossyReach, kGlossyFade, kGlossyRoughness, floors ? 1.0f : 0.0f);
        push.f1 = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        PushConstants(cmd, &push, sizeof(push));
        cmd.Dispatch(half_x, half_y, 1);
        filter(7, {kGlossyRaw, kGlossyHistory0, kGlossyHistory0 + 1});
        filter(8, {kGlossyHistory0, kGlossyHistory0 + 1});
        filter(9, {kGlossyPing});
        cmd.UseTargets({{&rt_full_images_[kGlossyPong], gpu::Layout::General}});
    }
    Stamp(cmd, 20);
    BindSets(cmd, gpu::BindPoint::Graphics);
    rt_full_traced_ = true;
}

void SceneRenderer::RecordRtFullResolve(gpu::CommandList& cmd, const ViewSetup& view) {
    const gpu::View& v = frame_->views[view.index];
    const bool cut = glm::distance(rt_full_previous_eye_, view.eye) > kCameraCutDistance;
    const uint32_t previous = rt_full_written_;
    const uint32_t history = previous | (rt_full_history_ && !cut ? 0u : 2u);
    BindSets(cmd, gpu::BindPoint::Compute);
    const gpu::BindingSetHandle rt_set = rt_->Set(renderer_->FrameIndex());
    cmd.BindSets(gpu::BindPoint::Compute, rt_->Layout(), 2, {&rt_set, 1});
    cmd.UseTargets({{&rt_full_images_[kUnshadowed], gpu::Layout::General}, {&rt_full_images_[kShadowed], gpu::Layout::General},
                     {&depth_, gpu::Layout::DepthReadOnly}, {&normal_, gpu::Layout::ShaderReadOnly},
                     {&material_, gpu::Layout::ShaderReadOnly}});
    gpu::PassPush push;
    push.ids = glm::uvec4(view.index, 0u, 0u, history);
    push.m = rt_full_previous_;
    push.f1 = glm::vec4(rt_full_previous_jitter_, 0.0f, 0.0f);
    const uint32_t groups_x = (extent_.width + 7) / 8;
    const uint32_t groups_y = (extent_.height + 7) / 8;
    cmd.BindPipeline(gpu::BindPoint::Compute, rt_full_filter_);
    const bool ratio = rt_full_shadows_ || rt_path_;
    if (ratio) {
        push.ids.z = 0;
        PushConstants(cmd, &push, sizeof(push));
        cmd.Dispatch(groups_x, groups_y, 1);
        cmd.UseTargets({{&rt_full_images_[kRatioSpatial], gpu::Layout::General}});
    }
    push.ids.z = ratio ? 1u : 2u;
    PushConstants(cmd, &push, sizeof(push));
    cmd.Dispatch(groups_x, groups_y, 1);
    const uint32_t written = previous ^ 1u;
    cmd.UseTargets({{&rt_full_images_[kRatioHistory0 + written], gpu::Layout::General},
                     {&rt_full_images_[kDepthHistory0 + written], gpu::Layout::General},
                     {&rt_full_images_[kGiPing], gpu::Layout::General},
                     {&rt_full_images_[kGlossyPong], gpu::Layout::General},
                     {&diffuse_, gpu::Layout::ColorAttachment},
                     {&specular_, gpu::Layout::ColorAttachment}});
    BindSets(cmd, gpu::BindPoint::Graphics);
    if (rt_path_) {
        RecordPathResolve(cmd, view, written);
    } else if (rt_full_shadows_) {
        cmd.BeginPass(ViewArea(view), {{&diffuse_, false, {}}, {&specular_, false, {}}});
        cmd.BindSets(gpu::BindPoint::Graphics, rt_->Layout(), 2, {&rt_set, 1});
        push.ids = glm::uvec4(view.index, 0u, 0u, written ? 4u : 0u);
        Fullscreen(cmd, rt_full_resolve_, push);
        cmd.EndPass();
    }
    rt_full_written_ = written;
    rt_full_history_ = true;
    rt_full_previous_ = v.view_projection;
    rt_full_previous_jitter_ = glm::vec2(v.jitter);
    rt_full_previous_eye_ = view.eye;
}

void SceneRenderer::RecordRtFullBounce(gpu::CommandList& cmd, const ViewSetup& view) {
    if (!rt_gi_active_ && !rt_glossy_active_) {
        return;
    }
    cmd.UseTargets({{&rt_full_images_[kGiPing], gpu::Layout::General}, {&rt_full_images_[kGlossyPong], gpu::Layout::General},
                     {&depth_, gpu::Layout::DepthReadOnly}, {&normal_, gpu::Layout::ShaderReadOnly},
                     {&material_, gpu::Layout::ShaderReadOnly}, {&diffuse_, gpu::Layout::ColorAttachment},
                     {&specular_, gpu::Layout::ColorAttachment}});
    BindSets(cmd, gpu::BindPoint::Graphics);
    cmd.BeginPass(ViewArea(view), {{&diffuse_, false, {}}, {&specular_, false, {}}});
    const gpu::BindingSetHandle rt_set = rt_->Set(renderer_->FrameIndex());
    cmd.BindSets(gpu::BindPoint::Graphics, rt_->Layout(), 2, {&rt_set, 1});
    gpu::PassPush push;
    push.f0 = glm::vec4(rt_gi_strength_, rt_glossy_strength_, kGlossyRoughness, 0.0f);
    if (rt_gi_active_) {
        push.ids = glm::uvec4(view.index, 0u, 2u, 0u);
        Fullscreen(cmd, rt_full_resolve_gi_, push);
    }
    if (rt_glossy_active_) {
        push.ids = glm::uvec4(view.index, 0u, 1u, 0u);
        Fullscreen(cmd, rt_full_resolve_glossy_, push);
    }
    cmd.EndPass();
    cmd.UseTargets({{&diffuse_, gpu::Layout::ShaderReadOnly}, {&specular_, gpu::Layout::ShaderReadOnly}});
}

}
