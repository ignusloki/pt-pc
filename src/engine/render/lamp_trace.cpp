#include "engine/render/lamp_trace.h"

#include "engine/core/log.h"
#include "engine/render/camera.h"
#include "engine/render/renderer.h"
#include "engine/render/scene_renderer.h"
#include "engine/vfx/vfx_system.h"
#include <SDL3/SDL.h>
#if defined(__APPLE__) && !PT_IOS
#include "engine/render/macos_hdr.h"
#endif
#include <glm/gtc/packing.hpp>

namespace pt {

namespace {
// Keep the cold start and the two most recent segments even after a long play session.
uint64_t FileIndex(uint64_t segment) { return segment == 0 ? 0 : 1 + (segment - 1) % 2; }

// Inverse of the game's post-process LensSource for locating a scene-space light in the final composite.
glm::vec2 DisplayUv(glm::vec2 source, float aspect) {
    glm::vec2 uv = source;
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 p = (uv * 2.f - 1.f) * glm::vec2(aspect / (16.f / 9.f), 1.f);
        const float r = std::min(glm::length(p) * .52240777f, 1.f);
        const float z = 1.f - std::sin(1.5707964f * r);
        const glm::vec2 offset = p * (std::pow(r, 1.75f) * 84.f * 1.275f / (1080.f * std::sqrt(glm::dot(p, p) + z * z)));
        uv += (source - (uv * .9f + .05f + offset / glm::vec2(aspect, 1.f))) / .9f;
    }
    return uv;
}
}

LampTrace::LampTrace(std::filesystem::path directory) : directory_(std::move(directory)) {}

void LampTrace::Scene(uint64_t tick, const std::string& floor, const Camera& camera, const SceneLighting& lights,
                      const RenderStats& stats, const vfx::RenderList& effects, float aspect, bool lens, float paper, SDL_Window* window,
                      bool enhanced, bool menu, bool paused) {
    scene_.sequence = ++sequence_;
    scene_.paper = paper;
    scene_.aspect = aspect;
    const double now = LogClock();
    const float headroom = window ? SDL_GetFloatProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.f) : 0.f;
#if defined(__APPLE__) && !PT_IOS
    if (window && (now - display_sample_time_ >= 1.0 || display_menu_ != menu)) {
        vk::LogMacHdrDisplayState(window, menu);
        display_sample_time_ = now;
        display_menu_ = menu;
    }
#endif
    float power = 0;
    for (const auto& light : lights.lights) {
        power += glm::dot(light.intensity, glm::vec3(.2126f, .7152f, .0722f));
    }
    scene_.metadata = std::format("{:.4f},{},{},{:.4f},{:.4f},{:.4f},{:.3f},{:.3f},{:.2f},{:.4f},{:.5g},{},{:.7g},{:.4f},{:.4f},{:.4f},{},{},{},{:.6g}",
                                  now, tick, floor, camera.position.x, camera.position.y, camera.position.z,
                                  glm::degrees(camera.yaw), glm::degrees(camera.pitch), glm::degrees(camera.fov_y),
                                  stats.ev, stats.exposure, lights.lights.size(), power, headroom, stats.jitter_pixels.x, stats.jitter_pixels.y,
                                  enhanced ? "enhanced" : "original", int(menu), int(paused), stats.luminance);
    scene_.count = 0;
    auto add = [&](std::string label, glm::vec2 uv, float depth = 0, float alpha = 0, const SceneLight* light = nullptr) {
        if (scene_.count < kRegions && uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1) {
            scene_.regions[scene_.count++] = {std::move(label), uv, depth, alpha, light ? light->id : 0,
                                              light ? glm::dot(light->intensity, glm::vec3(.2126f, .7152f, .0722f)) : 0.f};
        }
    };
    add("center", {.5f, .5f});
    add("left-wall", {.25f, .5f});
    add("right-wall", {.75f, .5f});
    add("upper-wall", {.5f, .32f});
    // The fluorescent tube over the start door has no optical flare. Its two opposing scene lights
    // bracket the tube. Project their midpoint to follow the luminous surface as the player looks around.
    if (floor == "f030") {
        const SceneLight* top = nullptr;
        const SceneLight* bottom = nullptr;
        for (const auto& light : lights.lights) {
            if (light.name == "pt14_start|pt14_start_light|SL_wall0005") top = &light;
            if (light.name == "pt14_start|pt14_start_light|SL_wall0004") bottom = &light;
        }
        if (top && bottom) {
            const auto vp = camera.Projection(aspect) * camera.View();
            const auto fixture = (top->position + bottom->position) * .5f;
            auto project = [&](const char* name, glm::vec3 position) {
                const glm::vec4 clip = vp * glm::vec4(position, 1.f);
                if (clip.w <= camera.near_plane) return;
                const glm::vec2 uv = glm::vec2(clip) / clip.w * .5f + .5f;
                add(name, lens ? DisplayUv(uv, aspect) : uv, clip.w, 0, top);
            };
            project("door-tube", fixture);
            project("door-wall", fixture + glm::vec3(0, .16f, 0));
        }
    }
    glm::vec2 previous(-2.f);
    unsigned sources = 0;
    for (const auto& draw : effects.draws) {
        if (draw.layer != vfx::Layer::Flare || !draw.count || draw.first >= effects.quads.size()) continue;
        const auto& quad = effects.quads[draw.first];
        glm::vec2 uv(quad.extra.z, quad.extra.w);
        if (uv == previous || uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) continue;
        previous = uv;
        const float depth = quad.params.z;
        const float scale = depth * std::tan(camera.fov_y * .5f);
        const glm::vec3 position = camera.position + depth * camera.Forward() + scale * ((uv.x * 2.f - 1.f) * aspect * camera.Right()
                                                                                       + (1.f - uv.y * 2.f) * camera.Up());
        const SceneLight* nearest = nullptr;
        float distance = 1.25f;
        for (const auto& light : lights.lights) {
            const float d = glm::distance(light.position, position);
            if (!light.name.empty() && d < distance) { nearest = &light; distance = d; }
        }
        if (nearest && named_lights_.insert(nearest->id).second)
            LogInfo("lamp diagnostics: nearby scene light {:x} = {}", nearest->id, nearest->name);
        if (lens) uv = DisplayUv(uv, aspect);
        const std::string name = std::format("flare{}", sources++);
        add(name, uv, depth, quad.color.a, nearest);
        add(name + "-left", uv - glm::vec2(.065f, 0), depth, quad.color.a, nearest);
        add(name + "-right", uv + glm::vec2(.065f, 0), depth, quad.color.a, nearest);
        add(name + "-below", uv + glm::vec2(0, .11f), depth, quad.color.a, nearest);
        if (sources == 2) break;
    }
}

void LampTrace::Capture(vk::Context& ctx, VkCommandBuffer cmd, VkImage image, VkFormat format, VkExtent2D extent,
                        uint32_t slot, bool interpolated, bool repeat, VkImageLayout layout) {
    if (failed_ || !scene_.count || !image || !extent.width || !extent.height) return;
    if (format != VK_FORMAT_R16G16B16A16_SFLOAT) {
        if (!warned_) LogWarn("lamp diagnostics: output format {} is outside the scRGB luminance capture", int(format));
        warned_ = true;
        return;
    }
    auto& r = slots_[slot][interpolated];
    constexpr unsigned stride = kSide * kSide * 8;
    if (!r.buffer.buffer && !ctx.CreateBuffer(r.buffer, kRegions * stride, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
        LogWarn("lamp diagnostics: readback allocation failed");
        failed_ = true;
        return;
    }
    r.sample = scene_;
    r.extent = extent;
    r.kind = interpolated ? (repeat ? "repeat" : "generated") : "rendered";
    r.side = std::min({kSide, extent.width, extent.height});
    // Fit the normalized scene coordinates to the same centered viewport as the compositor.
    const float window_aspect = float(extent.width) / extent.height;
    const glm::vec2 used(std::min(1.f, r.sample.aspect / window_aspect), std::min(1.f, window_aspect / r.sample.aspect));
    for (unsigned i = 0; i < r.sample.count; ++i) {
        const auto uv = (r.sample.regions[i].uv - .5f) * used + .5f;
        auto origin = [&](float center, unsigned size) { return std::clamp(int(center * size) - int(r.side / 2), 0, int(size - r.side)); };
        auto& copy = r.copies[i];
        copy.bufferOffset = i * stride;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {origin(uv.x, extent.width), origin(uv.y, extent.height), 0};
        copy.imageExtent = {r.side, r.side, 1};
    }
    const bool copied = layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    const auto stage = copied ? VK_PIPELINE_STAGE_2_TRANSFER_BIT : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const auto access = copied ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    vk::ImageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, stage, access,
                     layout, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r.buffer.buffer, r.sample.count, r.copies.data());
    vk::ImageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stage, access, layout);
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

void LampTrace::Write(vk::Context& ctx, Readback& r) {
    if (!r.kind || failed_) return;
    const uint64_t segment = (r.sample.sequence - 1) / 2048;
    if (segment_ != segment) {
        csv_.close();
        csv_.open(directory_ / std::format("frames-{}.csv", FileIndex(segment)));
        if (!csv_) {
            failed_ = true;
            LogWarn("lamp diagnostics: cannot open frame log");
            return;
        }
        segment_ = segment;
        csv_ << "sequence,scene_seconds,tick,floor,cam_x,cam_y,cam_z,yaw,pitch,fov,ev,exposure,light_count,light_power,display_headroom,"
                "jitter_x,jitter_y,pipeline,menu_open,paused,metered_luminance,kind,width,height,region,x,y,side,source_depth,source_alpha,"
                "nearby_light_id,nearby_light_intensity,mean_nits,peak_nits,paper_nits,pixels_over_paper,mean_pixel_change_nits,pixels_changed_over_20_nits\n";
    }
    vmaInvalidateAllocation(ctx.allocator, r.buffer.allocation, 0, VK_WHOLE_SIZE);
    for (unsigned i = 0; i < r.sample.count; ++i) {
        const auto& region = r.sample.regions[i];
        const auto& copy = r.copies[i];
        const auto* pixels = reinterpret_cast<const uint16_t*>(static_cast<const char*>(r.buffer.mapped) + copy.bufferOffset);
        auto& last = previous_[region.name];
        const bool same_patch = last.side == r.side && last.offset.x == copy.imageOffset.x && last.offset.y == copy.imageOffset.y &&
                                (last.sequence == r.sample.sequence || last.sequence + 1 == r.sample.sequence);
        double sum = 0, pixel_change = 0;
        unsigned changed = 0;
        float peak = 0;
        unsigned bright = 0;
        for (unsigned j = 0; j < r.side * r.side; ++j, pixels += 4) {
            float nits = 80.f * (.2126f * glm::unpackHalf1x16(pixels[0]) + .7152f * glm::unpackHalf1x16(pixels[1]) + .0722f * glm::unpackHalf1x16(pixels[2]));
            sum += nits;
            if (same_patch) {
                const float difference = std::abs(last.nits[j] - nits);
                pixel_change += difference;
                changed += difference > 20.f;
            }
            last.nits[j] = nits;
            peak = std::max(peak, nits);
            bright += nits > r.sample.paper;
        }
        last.offset = copy.imageOffset;
        last.side = r.side;
        last.sequence = r.sample.sequence;
        csv_ << std::format("{},{},{},{},{},{},{},{},{},{:.4f},{:.4f},{:x},{:.7g},{:.4f},{:.4f},{:.2f},{},", r.sample.sequence, r.sample.metadata,
                            r.kind, r.extent.width, r.extent.height, region.name, copy.imageOffset.x, copy.imageOffset.y,
                            r.side, region.depth, region.alpha, region.light_id, region.light_intensity, sum / (r.side * r.side), peak, r.sample.paper, bright);
        if (same_patch) csv_ << std::format("{:.4f},{}\n", pixel_change / (r.side * r.side), changed);
        else csv_ << ",\n";
    }
    if (r.sample.sequence % 16 == 0) csv_.flush();
    r.kind = nullptr;
}

void LampTrace::FinishSlot(vk::Context& ctx, uint32_t slot) {
    // Compare the two images in submission order. presents-*.csv records the driver's display order.
    Write(ctx, slots_[slot][1]);
    Write(ctx, slots_[slot][0]);
    scene_.count = 0;
}

void LampTrace::Displayed(const vk::Context& ctx, const VkPastPresentationTimingGOOGLE& timing) {
    if (presents_) presents_ << std::format("displayed,{},{},,,,,,,{},{},{}\n", timing.presentID, ctx.swapchain_generation,
                                             timing.actualPresentTime, timing.earliestPresentTime, timing.presentMargin);
}

void LampTrace::Queued(const vk::Context& ctx, uint32_t id, uint32_t image, const char* kind, double submitted, VkResult result) {
    if ((id - 1) % 4096 == 0) {
        presents_.close();
        presents_.open(directory_ / std::format("presents-{}.csv", FileIndex((id - 1) / 4096)));
        presents_ << "event,present_id,swapchain,sequence,kind,image,present_mode,submit_seconds,result,actual_ns,earliest_ns,margin_ns\n";
        if (id == 1) LogInfo("lamp diagnostics: display timing {}; presentation IDs are paired with scene frames", ctx.display_timing ? "available" : "unavailable");
    }
    presents_ << std::format("queued,{},{},{},{},{},{},{:.6f},{},,,\n", id, ctx.swapchain_generation, scene_.count ? scene_.sequence : 0,
                             kind, image, int(ctx.swapchain.present_mode), submitted, int(result));
    if (id % 32 == 0) presents_.flush();
}

void LampTrace::Shutdown(vk::Context& ctx) {
    static_assert(Renderer::kFramesInFlight == 2);
    const unsigned oldest = slots_[0][0].sample.sequence <= slots_[1][0].sample.sequence ? 0 : 1;
    for (unsigned i : {oldest, 1u - oldest}) {
        FinishSlot(ctx, i);
        for (auto& r : slots_[i]) if (r.buffer.buffer) ctx.DestroyBuffer(r.buffer);
    }
    csv_.close();
    presents_.close();
}

}
