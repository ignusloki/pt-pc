// Adapted from hartapfel/pt-pc-hdr dd90825 (MIT). Original processing stays in scene_post.cpp.
#include "engine/render/scene_renderer.h"
#include "engine/render/motion_math.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
namespace pt {
namespace {
glm::vec4 BloomPolynomial(float extraction) {
    glm::vec4 p(0.0f);
    const float e = std::clamp(extraction, 1.0f, 3.99999f);
    const int i = static_cast<int>(std::floor(e));
    const float f = e - static_cast<float>(i);
    p[i - 1] = 1.0f - f;
    p[i] = f;
    return p;
}

}

void SceneRenderer::RecordEnhancedPost(gpu::CommandList& cmd, const SceneLighting& lighting, float dt) {
    const ScreenSettings& screen = lighting.screen;
    const ExposureSettings& exposure = lighting.exposure;
    const bool hdr_output = hdr_scene_active_;
    gpu::PassPush push;
    push.ids.w = post_view_;
    push.f1.x = 0.0f;

    const char* native_order_override = std::getenv("PT_AA_NATIVE_ORDER");
    const bool native_aa_order = toggles.fxaa && !up_.enabled && native_order_override && std::string(native_order_override) == "1";
    if (native_aa_order) {
        cmd.UseTargets({{&hdr_, gpu::Layout::ShaderReadOnly}, {&hdr_copy_, gpu::Layout::ColorAttachment}});
        cmd.BeginPass(output_extent_, {{&hdr_copy_, false, {}}});
        gpu::PassPush aa;
        aa.ids = glm::uvec4(gpu::kImgHdr, 1, 0, post_view_);
        Fullscreen(cmd, fxaa_, aa);
        cmd.EndPass();
        cmd.UseTargets({{&hdr_copy_, gpu::Layout::ShaderReadOnly}, {&hdr_, gpu::Layout::ColorAttachment}});
        cmd.BeginPass(output_extent_, {{&hdr_, false, {}}});
        aa.ids = glm::uvec4(gpu::kImgHdrCopy, 2, 0, post_view_);
        Fullscreen(cmd, fxaa_, aa);
        cmd.EndPass();
        cmd.UseTargets({{&hdr_, gpu::Layout::ShaderReadOnly}});
    }

    const bool bloom = toggles.bloom && lighting.valid && std::abs(exposure.bloom_size) >= 1.0e-5f;
    if (bloom) {
        cmd.UseTargets({{&hdr_, gpu::Layout::ShaderReadOnly}, {&bloom_[0], gpu::Layout::ColorAttachment}});
        cmd.BeginPass(bloom_[0].Extent(), {{&bloom_[0], false, {}}});
        push.f0 = BloomPolynomial(exposure.bloom_extraction);
        push.f1 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        Fullscreen(cmd, bright_, push);
        cmd.EndPass();
        static const uint32_t kSlots[3] = {gpu::kImgBloomA, gpu::kImgBloomB, gpu::kImgBloomSum};
        auto blur = [&](int src, int dst, gpu::PipelineHandle pipeline, const glm::vec4& f0, bool clear) {
            cmd.UseTargets({{&bloom_[src], gpu::Layout::ShaderReadOnly}, {&bloom_[dst], gpu::Layout::ColorAttachment}});
            cmd.BeginPass(bloom_[dst].Extent(), {{&bloom_[dst], clear, {}}});
            push.ids.x = kSlots[src];
            push.f0 = f0;
            Fullscreen(cmd, pipeline, push);
            cmd.EndPass();
        };
        const float size = exposure.bloom_size;
        const float weight = exposure.bloom_weight;
        if (graphics.bloom_type == 1) {
            const int passes = std::clamp(static_cast<int>(std::floor(size * 10.0f)), 1, 64);
            // Match the original chain's total gain (first Kawase pair applies
            // weight twice, accumulation once), independently of filter shape.
            const float gain = size >= 0.1f && size < 0.2f ? 1.0f : weight*weight*weight;
            for (int i = 0; i < passes; ++i) {
                const float radius = std::max(0.5f, (static_cast<float>(i) + 0.5f)*0.5f);
                blur(0, 1, gaussian_, glm::vec4(radius, 0, 0, 0), false);
                blur(1, 0, gaussian_, glm::vec4(0, radius, 0, 0), false);
                blur(0, 2, bloom_add_, glm::vec4(0, gain/static_cast<float>(passes), 0, 0), i == 0);
            }
        } else if (size >= 0.1f && size < 0.2f) {
            blur(0, 1, gaussian_, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), false);
            blur(1, 2, gaussian_, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), false);
        } else {
            const int passes = std::clamp(static_cast<int>(std::floor(size * 10.0f)), 1, 64);
            static const bool separate_add = std::getenv("PT_BLOOM_SEPARATE_ADD") != nullptr;
            const glm::vec4 saved_f1 = push.f1;
            if (bloom_compute_ && !separate_add) {
                RecordBloomDispatches(cmd, passes, weight);
            } else {
            for (int i = 0; i < passes; ++i) {
                const glm::vec4 kawase(static_cast<float>(i) + 0.5f, i == 0 ? weight : 1.0f, 1.0f, 0.0f);
                blur(0, 1, kawase_, kawase, false);
                if (separate_add) {
                    blur(1, 0, kawase_, kawase, false);
                    blur(0, 2, bloom_add_, glm::vec4(0.0f, weight / static_cast<float>(passes), 0.0f, 0.0f), i == 0);
                    continue;
                }
                cmd.UseTargets({{&bloom_[1], gpu::Layout::ShaderReadOnly},
                                 {&bloom_[0], gpu::Layout::ColorAttachment},
                                 {&bloom_[2], gpu::Layout::ColorAttachment}});
                cmd.BeginPass(bloom_[0].Extent(), {{&bloom_[0], false, {}}, {&bloom_[2], i == 0, {}}});
                push.ids.x = kSlots[1];
                push.f0 = kawase;
                push.f1 = glm::vec4(weight / static_cast<float>(passes), 0.0f, 0.0f, 0.0f);
                Fullscreen(cmd, kawase_sum_, push);
                cmd.EndPass();
            }
            }
            push.f1 = saved_f1;
        }
        cmd.UseTargets({{&bloom_[2], gpu::Layout::ShaderReadOnly}, {&bloom_[0], gpu::Layout::ShaderReadOnly}});
    }
    Stamp(cmd, 11);

    cmd.UseTargets({{&flare_, gpu::Layout::ColorAttachment}, {&depth_, gpu::Layout::DepthReadOnly}});
    cmd.BeginPass(output_extent_, {{&flare_, true, {}}});
    if (vfx_filter && vr_eye_ < 0) {
        vfx_filter(FilterContext(cmd, 1u));
        BindSets(cmd, gpu::BindPoint::Graphics);
    }
    cmd.EndPass();
    cmd.UseTargets({{&flare_, gpu::Layout::ShaderReadOnly}});

    int current = 0;
    cmd.UseTargets({{&hdr_, gpu::Layout::ShaderReadOnly}, {&ldr_[0], gpu::Layout::ColorAttachment}});
    cmd.BeginPass(output_extent_, {{&ldr_[0], false, {}}});
    push.ids.x = 0;
    const float lut_blend = std::clamp(screen.lut_blend, 0.0f, 1.0f);
    push.f0 = glm::vec4(bloom ? graphics.bloom_strength : 0.0f, lut_blend, lighting.valid ? 1.0f : renderer_->exposure, 0.0f);
    static const bool tonemap_test = std::getenv("PT_TONEMAP_TEST") != nullptr;
    if (tonemap_test) {
        push.f0.x = 0.0f;
        push.f0.y = 0.0f;
        push.f0.w = 0.0f;
    }
    gpu::PassPush tone = push;
    tone.f1 = glm::vec4(graphics.deband && !hdr_output ? 1.0f : 0.0f, hdr_output ? 1.0f : 0.0f, static_cast<float>(graphics.lut_sampling), 0.0f);
    Fullscreen(cmd, tonemap_, tone);
    cmd.EndPass();
    cmd.UseTargets({{&ldr_[0], gpu::Layout::ShaderReadOnly}});

    auto ldr_index = [](int i) { return i == 0 ? gpu::kImgLdrA : gpu::kImgLdrB; };
    auto run = [&](gpu::PipelineHandle pipeline, const gpu::PassPush& p) {
        RenderTarget& dst = ldr_[1 - current];
        cmd.UseTargets({{&ldr_[current], gpu::Layout::ShaderReadOnly}, {&dst, gpu::Layout::ColorAttachment}});
        cmd.BeginPass(output_extent_, {{&dst, false, {}}});
        Fullscreen(cmd, pipeline, p);
        cmd.EndPass();
        cmd.UseTargets({{&dst, gpu::Layout::ShaderReadOnly}});
        current = 1 - current;
    };

    if (toggles.fxaa && !up_.enabled && !native_aa_order) {
        push.ids.x = ldr_index(current);
        run(fxaa_, push);
    }
    if (toggles.depth_of_field && screen.depth_of_field && lighting.valid) {
        RecordEnhancedDepthOfField(cmd, screen, current);
    }
    // Optical integration precedes the creative LUT in both indoor and street
    // scenes. The street camera may smooth velocity, not change colour space.
    if (toggles.motion_blur && screen.motion_blur && lighting.valid) {
        RecordEnhancedMotionBlur(cmd, screen, current);
    }
    if (subliminal_string_state_.value > 0.0f && subliminal_string_) {
        gpu::PassPush s;
        s.ids = glm::uvec4(subliminal_string_state_.texture, ldr_index(current), 0, 0);
        const float across = output_extent_.height ? (static_cast<float>(output_extent_.width) / static_cast<float>(output_extent_.height)) / (16.0f / 9.0f) : 1.0f;
        s.f0 = glm::vec4(subliminal_string_state_.position, subliminal_string_state_.value, across);
        s.f1 = glm::vec4(bloom && !tonemap_test ? graphics.bloom_strength : 0.0f, lighting.valid ? 1.0f : renderer_->exposure, hdr_output ? 1.0f : 0.0f, 0.0f);
        run(subliminal_string_, s);
    }
    if (toggles.color_lut) {
        gpu::PassPush lut;
        lut.ids = glm::uvec4(ldr_index(current), 1, 0, post_view_);
        lut.f0 = glm::vec4(0.0f, lut_blend, 1.0f, 1.0f);
        lut.f1 = tone.f1;
        run(tonemap_, lut);
    }

    if (backdrop_state_.opacity > 0.0f && backdrop_state_.texture && backdrop_) {
        cmd.UseTargets({{&depth_, gpu::Layout::DepthReadOnly}});
        gpu::PassPush b;
        b.ids = glm::uvec4(ldr_index(current), backdrop_state_.texture, 0, main_view_.index);
        b.f0 = glm::vec4(backdrop_state_.opacity, backdrop_state_.darken, backdrop_state_.tear, backdrop_state_.time);
        b.f1 = glm::vec4(backdrop_state_.tint, backdrop_state_.aspect);
        b.f2 = glm::vec4(output_extent_.height ? static_cast<float>(output_extent_.width) / static_cast<float>(output_extent_.height) : 16.0f / 9.0f,
                         hdr_output ? 1.0f : 0.0f, 0.0f, 0.0f);
        b.m = glm::mat4(0.0f);
        b.m[0] = glm::vec4(backdrop_state_.zoom, backdrop_state_.center, backdrop_state_.focus);
        run(backdrop_, b);
    }

    if (date_active_) {
        if (!date_history_valid_) {
            date_live_frames_ = 0;
            date_still_frames_ = 0;
        }
        float weight = 1.0f;
        if (date_still_now_) {
            if (date_still_frames_ == 0) date_still_frames_ = std::min(date_live_frames_, 8u);
            ++date_still_frames_;
            weight = 1.0f / static_cast<float>(std::min(date_still_frames_, 64u));
        } else {
            date_still_frames_ = 0;
            ++date_live_frames_;
            weight = std::max(1.0f / static_cast<float>(std::min(date_live_frames_, 1000u)), 0.125f);
        }
        const uint32_t from = date_history_;
        const uint32_t to = 1u - from;
        cmd.UseTargets({{&mb_blur_[from], gpu::Layout::ShaderReadOnly}});
        gpu::PassPush r;
        r.ids = glm::uvec4(ldr_index(current), gpu::kImgMbBlurA + from, 0u, post_view_);
        r.f0 = glm::vec4(weight, date_still_now_ ? 0.0f : 1.0f, 0.0f, 0.0f);
        PostPass(cmd, mb_blur_[to], date_resolve_, r);
        gpu::PassPush o;
        o.ids = glm::uvec4(gpu::kImgMbBlurA + to, 0u, 1u, post_view_);
        run(date_resolve_out_, o);
        date_history_ = to;
        date_history_valid_ = true;
    }

    const bool blur = toggles.full_screen_blur && screen.full_screen_blur && lighting.valid;
    if (blur) {
        if (!history_valid_) {
            cmd.UseTargets({{&ldr_[current], gpu::Layout::TransferSrc}, {&history_, gpu::Layout::TransferDst}});
            CopyToHistory(cmd, ldr_[current]);
            cmd.UseTargets({{&ldr_[current], gpu::Layout::ShaderReadOnly}});
        }
        cmd.UseTargets({{&history_, gpu::Layout::ShaderReadOnly}});
        push.ids.x = ldr_index(current);
        push.ids.y = gpu::kImgHistory;
        const float share = std::clamp(dt * 30.0f, 0.05f, 2.0f);
        const float rate = std::pow(std::clamp(screen.blur_blend_rate, 0.0f, 0.999f), share);
        push.f0 = glm::vec4(screen.blur_fetch_band * std::sqrt(share), rate, 0.0f, 0.0f);
        run(fsblur_, push);
        cmd.UseTargets({{&ldr_[current], gpu::Layout::TransferSrc}, {&history_, gpu::Layout::TransferDst}});
        CopyToHistory(cmd, ldr_[current]);
        cmd.UseTargets({{&ldr_[current], gpu::Layout::ShaderReadOnly}, {&history_, gpu::Layout::ShaderReadOnly}});
        history_valid_ = true;
    } else {
        history_valid_ = false;
    }
    if (screen.colour_banding_canceller && lighting.valid) {
        cmd.UseTargets({{&depth_, gpu::Layout::DepthReadOnly}});
        push.ids = glm::uvec4(ldr_index(current), 0, 0, main_view_.index);
        push.f1.z = hdr_output ? 1.0f : 0.0f;
        run(banding_, push);
    }

    RenderTarget output;
    output.image = renderer_->SceneColor();
    output.layout = gpu::Layout::ColorAttachment;
    cmd.UseTargets({{&output, gpu::Layout::ColorAttachment}, {&depth_, gpu::Layout::DepthReadOnly}});
    cmd.BeginPass(output_extent_, {{&output, false, {}}});
    push.ids = glm::uvec4(ldr_index(current), 0, 0, post_view_);
    const bool grain = toggles.film_grain && screen.film_grain && lighting.valid;
    const bool distortion = toggles.distortion && screen.screen_distortion && lighting.valid;
    push.f0 = glm::vec4(distortion ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    push.f1 = glm::vec4(graphics.sharpening_type == 0 ? graphics.clarity : 0.0f, graphics.deband && !hdr_output ? 1.0f : 0.0f, hdr_output ? 1.0f : 0.0f, 0.0f);
    Fullscreen(cmd, screen_fx_, push);
    renderer_->grain[0] = grain ? 1.0f : 0.0f;
    renderer_->grain[1] = (screen.grain_alt ? 1.0f : 0.0f) + (graphics.film_grain_type == 1 ? 2.0f : 0.0f);
    renderer_->grain[2] = screen.grain_strength * graphics.film_grain;
    renderer_->grain[3] = graphics.sharpening_type == 1 ? graphics.clarity : 0.0f;
    renderer_->grain_offset[0] = screen.grain_offset.x;
    renderer_->grain_offset[1] = screen.grain_offset.y;
    if (vfx_filter && vr_eye_ < 0) {
        SceneFilterContext context = FilterContext(cmd, 2u);
        context.color_format = renderer_->SceneColorFormat();
        context.source = ldr_[current].image.view;
        vfx_filter(context);
        BindSets(cmd, gpu::BindPoint::Graphics);
    }
    cmd.EndPass();
    (void)dt;
}

bool SceneRenderer::RecordEnhancedDepthOfField(gpu::CommandList& cmd, const ScreenSettings& screen, int& current) {
    const float aperture = screen.aperture;
    if (!std::isfinite(aperture) || aperture <= 0.0f ||
        ((screen.dof_flags & 2u) == 0 && aperture >= 100.0f)) return false;
    const float f = screen.focal_length * 0.001f;
    const float d = std::max(screen.focus_distance, 1.0e-5f);
    const float s = std::max(0.0f, f * f / aperture / std::max(d - f, 1.0e-5f)) * 26666.666f;
    if (!std::isfinite(s) || s <= 1.0e-6f) return false;
    const float near_limit = std::min(99.0f * s, 20.0f);
    const float near_scale = std::min(s / near_limit, 1.0f);
    const uint32_t scene = current == 0 ? gpu::kImgLdrA : gpu::kImgLdrB;
    // Keep the authored lens/focus and nominal 1080p footprint. All optical
    // buffers are now half resolution; legacy descriptor names remain stable.
    const auto extent = dof_quarter_[0].Extent();
    const float resolution = static_cast<float>(output_extent_.height) / 1080.0f;
    // Match the saturated two-pass hex filter's second moment with a disk:
    // E[r^2] was 2*.75*ringRadius^2; a disk has E[r^2]=R^2/2.
    const float far_radius = std::min(s, 10.0f) * 0.4330127f * resolution;
    const float near_radius = near_limit * 0.3464102f * resolution;
    gpu::PassPush push;
    push.ids = glm::uvec4(0, 0, 0, post_view_);
    push.f0 = glm::vec4(d, near_limit, near_scale, 1.0f);
    cmd.UseTargets({{&depth_, gpu::Layout::DepthReadOnly}});
    PostPass(cmd, ldr_[current], dof_ratio_, push);
    push = gpu::PassPush{};
    push.f2 = glm::vec4(extent.width, extent.height, 1.0f / extent.width, 1.0f / extent.height);
    push.ids = glm::uvec4(scene, 0, 0, post_view_);
    push.f0.x = far_radius;
    PostPass(cmd, dof_quarter_[0], dof_down_, push);
    push.ids = glm::uvec4(gpu::kImgDofQuarterA, 0, 0, post_view_);
    PostPass(cmd, dof_quarter_[1], dof_blur_, push);
    push.ids = glm::uvec4(scene, 0, 1, post_view_);
    push.f0.x = near_radius;
    PostPass(cmd, dof_eighth_[0], dof_down_, push);
    push.ids = glm::uvec4(gpu::kImgDofEighthA, 0, 1, post_view_);
    PostPass(cmd, dof_eighth_[1], dof_blur_, push);
    push = gpu::PassPush{};
    push.ids = glm::uvec4(scene, gpu::kImgDofEighthB, gpu::kImgDofQuarterB, post_view_);
    push.f0 = glm::vec4(far_radius * 2.0f, (screen.dof_flags & 1u) ? 0.0f : 1.0f, 0.0f, 0.0f);
    PostPass(cmd, ldr_[1 - current], dof_blend_, push);
    current = 1 - current;
    return true;
}

bool SceneRenderer::RecordEnhancedMotionBlur(gpu::CommandList& cmd, const ScreenSettings& screen, int& current) {
    const float shutter = screen.shutter_speed;
    if (!(shutter >= 0.001f)) {
        return false;
    }
    // Authored shutter values are seconds. Preserve the 60 Hz reference strength
    // also for the legacy fixed-shutter flag instead of tying it to render FPS.
    const float scale = motion_math::ShutterScale(shutter, static_cast<float>(frame_time_));
    // mb_blur_ also holds persistent date-scene history. Keep optical scratch
    // separate so enabling cinematic motion blur cannot overwrite that history.
    RenderTarget& bake = mb_original_bake_;
    RenderTarget* blur = mb_original_blur_;
    const uint32_t bake_image = gpu::kImgMbOriginalBake;
    const uint32_t blur_image = gpu::kImgMbOriginalBlurA;
    gpu::PassPush push;
    push.ids = glm::uvec4(gpu::kImgObjectVelocity, 0, 0, post_view_);
    push.f0 = glm::vec4(1920.0f / 128.0f, 1080.0f / 128.0f, std::min(1.0f, static_cast<float>(frame_time_) * 30.0f), graphics.camera_motion_blur ? 1.0f : 0.0f);
    push.m = blur_previous_view_projection_;
    cmd.UseTargets({{&depth_, gpu::Layout::DepthReadOnly}});
    PostPass(cmd, velocity_, mb_velocity_, push);
    push = gpu::PassPush{};
    for (uint32_t i = 0; i < 5; ++i) {
        const gpu::Extent2D e = mb_tile_[i].Extent();
        push.ids = glm::uvec4(i == 0 ? static_cast<uint32_t>(gpu::kImgVelocity) : gpu::kImgMbTile + i - 1, 0, 0, post_view_);
        push.f0 = glm::vec4(1.0f / e.width, 1.0f / e.height, 0.0f, 0.0f);
        PostPass(cmd, mb_tile_[i], mb_tile_pipeline_, push);
    }
    push.ids = glm::uvec4(gpu::kImgMbTile + 4, 0, 1, post_view_);
    PostPass(cmd, mb_neighbour_, mb_tile_pipeline_, push);
    push.ids = glm::uvec4(gpu::kImgVelocity, gpu::kImgMbNeighbour, 0, post_view_);
    push.f0 = glm::vec4(0.5f, 0.5f, 1.0f, scale);
    PostPass(cmd, bake, mb_bake_pipeline_, push);
    const uint32_t picture = current == 0 ? gpu::kImgLdrA : gpu::kImgLdrB;
    // Both display modes filter extended-sRGB post buffers in linear light.
    gpu::PassPush decode;
    decode.ids = glm::uvec4(picture, 0, 0, post_view_);
    decode.f0 = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    decode.f1.z = 1.0f;
    PostPass(cmd, blur[1], mb_decode_, decode);
    push.ids = glm::uvec4(blur_image + 1, bake_image, 1u, post_view_);
    PostPass(cmd, blur[0], mb_mcguire_, push);
    push.ids = glm::uvec4(blur_image, bake_image, 1u, post_view_);
    push.f0 = glm::vec4(0.5f, 0.5f, 0.16666667f, scale);
    PostPass(cmd, blur[1], mb_mcguire_, push);
    push.ids = glm::uvec4(blur_image + 1, picture, 0, post_view_);
    push.f0 = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    push.f1 = glm::vec4(0.0f, 0.0f, 2.0f, 0.0f);
    cmd.UseTargets({{&ldr_[current], gpu::Layout::ShaderReadOnly}});
    PostPass(cmd, ldr_[1 - current], mb_decode_, push);
    current = 1 - current;
    return true;
}
}
