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
#include <algorithm>
#include <cmath>

namespace pt {

namespace {
// Keep the cold start and the two most recent segments even after a long play session.
uint64_t FileIndex(uint64_t segment) { return segment == 0 ? 0 : 1 + (segment - 1) % 2; }

float HalfRgbLuma(const uint16_t* rgb) {
    return .2126f * glm::unpackHalf1x16(rgb[0]) + .7152f * glm::unpackHalf1x16(rgb[1]) + .0722f * glm::unpackHalf1x16(rgb[2]);
}

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

LampTrace::LampTrace(std::filesystem::path directory) : directory_(std::move(directory)), events_(directory_ / "events.csv") {
    events_ << "seconds,sequence,floor,pipeline,menu_open,event\n";
}

void LampTrace::Mark(const char* reason) {
    events_ << std::format("{:.6f},{},{},{},{},{}\n", LogClock(), sequence_, floor_, enhanced_ ? "enhanced" : "original", int(menu_), reason);
    events_.flush();
    LogInfo("lamp marker: {} at sequence {} ({}, {})", reason, sequence_, floor_, enhanced_ ? "Enhanced" : "Original");
}

void LampTrace::Scene(uint64_t tick, const std::string& floor, const Camera& camera, const SceneLighting& lights,
                      const RenderStats& stats, const vfx::RenderList& effects, float aspect, bool lens, const Renderer& renderer,
                      SDL_Window* window, bool menu, bool paused) {
    scene_.sequence = ++sequence_;
    const bool enhanced = renderer.EnhancedRendering();
    const bool changed_scene = floor_ != floor || enhanced_ != enhanced;
    const bool changed_menu = menu_ != menu;
    if (floor_ != floor) fixture_origin_.reset();
    floor_ = floor;
    enhanced_ = enhanced;
    menu_ = menu;
    if (changed_scene) Mark("scene");
    if (changed_menu) Mark(menu ? "settings-open" : "settings-closed");
    scene_.paper = renderer.HdrPaperWhite();
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
    scene_.lens_info = std::format("{:.5f},{:.4f},{:.4f},{},{},{:.4f},{:.4f},{}", lights.screen.focus_distance, lights.screen.focal_length,
                                   lights.screen.aperture, int(lights.screen.depth_of_field), lights.screen.dof_flags,
                                   lights.exposure.bloom_size, glm::degrees(camera.roll), int(lens));
    auto add = [&](std::string label, glm::vec2 uv, float depth = 0, float alpha = 0, const SceneLight* light = nullptr,
                   int probe = -1, glm::vec2 source_uv = {}) {
        if (scene_.count < kRegions && uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1) {
            scene_.regions[scene_.count++] = {std::move(label), uv, depth, alpha, light ? light->id : 0,
                                              light ? glm::dot(light->intensity, glm::vec3(.2126f, .7152f, .0722f)) : 0.f, source_uv, probe};
        }
    };
    add("center", {.5f, .5f});
    add("left-wall", {.25f, .5f});
    add("right-wall", {.75f, .5f});
    add("upper-wall", {.5f, .32f});
    const auto vp = camera.Projection(aspect) * camera.View();
    auto fixture_probe = [&](const char* name, int probe, glm::vec3 position, const SceneLight* light) {
        const glm::vec4 clip = vp * glm::vec4(position, 1.f);
        if (clip.w <= camera.near_plane) return;
        const glm::vec2 uv = glm::vec2(clip) / clip.w * .5f + .5f;
        add(name, lens ? DisplayUv(uv, aspect) : uv, clip.w, 0, light, probe, uv);
    };
    // The fluorescent tube over the start door has no optical flare. Its two opposing scene lights
    // bracket the tube. Project their midpoint to follow the luminous surface as the player looks around.
    const SceneLight* primary = nullptr;
    const SceneLight* secondary = nullptr;
    if (floor == "f030") {
        for (const auto& light : lights.lights) {
            if (light.name == "pt14_start|pt14_start_light|SL_wall0005") primary = &light;
            if (light.name == "pt14_start|pt14_start_light|SL_wall0004") secondary = &light;
        }
        if (primary && secondary) fixture_origin_ = (primary->position + secondary->position) * .5f;
        if (fixture_origin_) {
            fixture_probe("door-tube", 0, *fixture_origin_, primary);
            fixture_probe("door-halo", 1, *fixture_origin_ + glm::vec3(0, .075f, 0), primary);
            fixture_probe("door-wall", 2, *fixture_origin_ + glm::vec3(0, .16f, 0), primary);
        }
    } else if (floor == "f040") {
        for (const auto& light : lights.lights) {
            if (light.name != "pt14_hallway|pt14_hallway_light_ID60|1SL_wall_front0000") continue;
            // The flare is 5 cm behind this light and 2 cm higher in the opening hallway.
            // Keeping that anchor follows the same sconce even when its flare is culled.
            primary = &light;
            fixture_origin_ = light.position - .05f * light.direction + glm::vec3(0, .02f, 0);
            break;
        }
        if (fixture_origin_) {
            fixture_probe("hall-glass", 0, *fixture_origin_, primary);
            fixture_probe("hall-halo", 1, *fixture_origin_ + glm::vec3(0, .13f, 0), primary);
            fixture_probe("hall-wall", 2, *fixture_origin_ + glm::vec3(0, .32f, 0), primary);
        }
    }
    const auto power_of = [](const SceneLight* light) { return light ? glm::dot(light->intensity, glm::vec3(.2126f, .7152f, .0722f)) : 0.f; };
    scene_.lens_info += std::format(",{},{},{:.7g},{:.7g}", int(primary != nullptr), int(secondary != nullptr), power_of(primary), power_of(secondary));
    scene_.lens_info += std::format(",{:.0f},{:.6g},{:.5f},{:.5f}", renderer.grain[0], renderer.grain[2], renderer.grain_offset[0], renderer.grain_offset[1]);
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

void LampTrace::CaptureStages(Renderer& renderer, const std::array<gpu::RenderTarget*, StageCount>& targets) {
    if (failed_ || stage_failed_ || std::none_of(scene_.regions.begin(), scene_.regions.begin() + scene_.count,
                                                 [](const Region& region) { return region.probe >= 0; })) return;
    auto& ctx = renderer.Context();
    auto& slot = stage_slots_[renderer.FrameIndex()];
    if (slot.count) return;
    constexpr unsigned stride = kSide * kSide * 8;
    if (!slot.buffer.buffer && !ctx.CreateBuffer(slot.buffer, stride * StageCount * kProbes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
        LogWarn("lamp diagnostics: cannot allocate stage readback");
        stage_failed_ = true;
        return;
    }
    slot.sample = scene_;
    auto& cmd = renderer.List();
    for (unsigned s = 0; s < targets.size(); ++s) {
        auto* target = targets[s];
        if (!target || !target->Valid() || target->layout == gpu::Layout::Undefined) continue;
        const bool unorm = target->image.format == gpu::Format::R8Unorm;
        if (!unorm && target->image.format != gpu::Format::R16G16B16A16Sfloat) continue;
        const auto extent = target->Extent();
        const float scale = float(extent.height) / std::max(1u, renderer.RenderExtent().height);
        const unsigned side = std::clamp<unsigned>(std::lround(kSide * scale), 1, std::min({extent.width, extent.height, kSide}));
        std::array<VkBufferImageCopy, kProbes> copies{};
        unsigned count = 0;
        for (unsigned i = 0; i < scene_.count; ++i) {
            const Region& region = scene_.regions[i];
            if (region.probe < 0) continue;
            const auto origin = [side](float uv, unsigned length) { return std::clamp(int(uv * length) - int(side / 2), 0, int(length - side)); };
            auto& copy = copies[count++];
            copy.bufferOffset = slot.count * stride;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            const glm::vec2 uv = s == BeforeComposite ? region.uv : region.source_uv;
            copy.imageOffset = {origin(uv.x, extent.width), origin(uv.y, extent.height), 0};
            copy.imageExtent = {side, side, 1};
            slot.patches[slot.count++] = {s, unsigned(region.probe), i, side, copy.imageOffset, {extent.width, extent.height}, unorm};
        }
        const auto back = target->layout;
        cmd.UseTargets({{target, gpu::Layout::TransferSrc}});
        vkCmdCopyImageToBuffer(renderer.Cmd(), gpu::vk::Native(target->image.image), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               slot.buffer.buffer, count, copies.data());
        cmd.UseTargets({{target, back}});
    }
    cmd.GlobalBarrier(gpu::PipelineStage::Transfer, gpu::Bits(gpu::Access::TransferWrite), gpu::PipelineStage::Host, gpu::Bits(gpu::Access::HostRead));
}

void LampTrace::WriteStages(vk::Context& ctx, StageReadback& r) {
    if (!r.count) return;
    const auto count = r.count;
    r.count = 0;
    OpenSegment(r.sample.sequence);
    if (failed_) return;
    constexpr std::array<const char*, StageCount> names{"diffuse-light", "scene-hdr", "reactive-mask", "upscale-resolve", "bloom", "optical-flare", "pre-composite"};
    vmaInvalidateAllocation(ctx.allocator, r.buffer.allocation, 0, VK_WHOLE_SIZE);
    for (unsigned i = 0; i < count; ++i) {
        const auto& p = r.patches[i];
        const auto& region = r.sample.regions[p.region];
        const auto* bytes = static_cast<const uint8_t*>(r.buffer.mapped) + i * kSide * kSide * 8;
        auto& last = previous_stage_[p.stage][p.probe];
        auto& name = previous_region_[p.stage][p.probe];
        const bool comparable = name == region.name && last.sequence + 1 == r.sample.sequence && last.side == p.side &&
                                last.offset.x == p.offset.x && last.offset.y == p.offset.y;
        double total = 0, change = 0;
        float peak = 0;
        for (unsigned j = 0; j < p.side * p.side; ++j) {
            const float v = p.unorm ? bytes[j] / 255.f : HalfRgbLuma(reinterpret_cast<const uint16_t*>(bytes) + 4 * j);
            total += v;
            peak = std::max(peak, v);
            change += std::abs(v - last.values[j]);
            last.values[j] = v;
        }
        last.sequence = r.sample.sequence;
        last.offset = p.offset;
        last.side = p.side;
        name = region.name;
        stages_ << std::format("{},{},{},{},{},{},{},{},{},{:.7g},{:.7g},", r.sample.sequence, names[p.stage], region.name,
                               p.offset.x, p.offset.y, p.side, p.extent.width, p.extent.height, p.unorm ? "mask_0_1" : "pass_rgb_luma",
                               total / (p.side * p.side), peak);
        if (comparable) stages_ << std::format("{:.7g}", change / (p.side * p.side));
        stages_ << '\n';
    }
    if (r.sample.sequence % 16 == 0) stages_.flush();
}

void LampTrace::OpenSegment(uint64_t sequence) {
    const uint64_t segment = (sequence - 1) / 2048;
    if (segment_ != segment && !failed_) {
        csv_.close();
        stages_.close();
        tiles_.close();
        const auto index = FileIndex(segment);
        csv_.open(directory_ / std::format("frames-{}.csv", index));
        stages_.open(directory_ / std::format("stages-{}.csv", index));
        tiles_.open(directory_ / std::format("tiles-{}.pgm", index), std::ios::binary | std::ios::out | std::ios::trunc);
        if (!csv_ || !stages_ || !tiles_) {
            failed_ = true;
            LogWarn("lamp diagnostics: cannot open frame log");
            return;
        }
        segment_ = segment;
        csv_ << "sequence,scene_seconds,tick,floor,cam_x,cam_y,cam_z,yaw,pitch,fov,ev,exposure,light_count,light_power,display_headroom,"
                "jitter_x,jitter_y,pipeline,menu_open,paused,metered_luminance,kind,width,height,region,x,y,side,source_depth,source_alpha,"
                "nearby_light_id,nearby_light_intensity,mean_nits,peak_nits,paper_nits,pixels_over_paper,mean_pixel_change_nits,pixels_changed_over_20_nits,"
                "focus_m,focal_length_mm,aperture,scene_dof,dof_flags,bloom_size,camera_roll,lens_distortion,"
                "fixture_primary_present,fixture_secondary_present,fixture_primary_power,fixture_secondary_power,"
                "grain_enabled,grain_strength,grain_offset_x,grain_offset_y\n";
        stages_ << "sequence,stage,region,x,y,side,width,height,unit,mean,peak,mean_abs_pixel_step\n";
    }
}

void LampTrace::Write(vk::Context& ctx, Readback& r) {
    if (!r.kind || failed_) return;
    OpenSegment(r.sample.sequence);
    if (failed_) return;
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
            const float nits = 80.f * HalfRgbLuma(pixels);
            sum += nits;
            if (same_patch) {
                const float difference = std::abs(last.values[j] - nits);
                pixel_change += difference;
                changed += difference > 20.f;
            }
            last.values[j] = nits;
            peak = std::max(peak, nits);
            bright += nits > r.sample.paper;
        }
        last.offset = copy.imageOffset;
        last.side = r.side;
        last.sequence = r.sample.sequence;
        csv_ << std::format("{},{},{},{},{},{},{},{},{},{:.4f},{:.4f},{:x},{:.7g},{:.4f},{:.4f},{:.2f},{},", r.sample.sequence, r.sample.metadata,
                            r.kind, r.extent.width, r.extent.height, region.name, copy.imageOffset.x, copy.imageOffset.y,
                            r.side, region.depth, region.alpha, region.light_id, region.light_intensity, sum / (r.side * r.side), peak, r.sample.paper, bright);
        if (same_patch) csv_ << std::format("{:.4f},{}", pixel_change / (r.side * r.side), changed);
        else csv_ << ',';
        csv_ << ',' << r.sample.lens_info << '\n';
        if (region.probe >= 0 && r.side >= 2) {
            // Concatenated 16-bit grayscale Netpbm images. Two-by-two downsampling retains the lamp contour.
            const unsigned side = r.side / 2;
            std::array<unsigned char, kSide * kSide / 2> tile;
            for (unsigned y = 0; y < side; ++y) {
                for (unsigned x = 0; x < side; ++x) {
                    const unsigned p = 2 * (y * r.side + x);
                    const float sum4 = last.values[p] + last.values[p + 1] + last.values[p + r.side] + last.values[p + r.side + 1];
                    const auto value = uint16_t(std::clamp(std::isfinite(sum4) ? sum4 + .5f : 0.f, 0.f, 65535.f));
                    tile[2 * (y * side + x)] = static_cast<unsigned char>(value >> 8);
                    tile[2 * (y * side + x) + 1] = static_cast<unsigned char>(value);
                }
            }
            tiles_ << std::format("P5\n# {} {} {} nits=code/4\n{} {}\n65535\n", r.sample.sequence, r.kind, region.name, side, side);
            tiles_.write(reinterpret_cast<const char*>(tile.data()), 2 * side * side);
        }
    }
    if (r.sample.sequence % 16 == 0) { csv_.flush(); tiles_.flush(); }
    r.kind = nullptr;
}

void LampTrace::FinishSlot(vk::Context& ctx, uint32_t slot) {
    // Compare the two images in submission order. presents-*.csv records the driver's display order.
    Write(ctx, slots_[slot][1]);
    Write(ctx, slots_[slot][0]);
    WriteStages(ctx, stage_slots_[slot]);
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
        if (stage_slots_[i].buffer.buffer) ctx.DestroyBuffer(stage_slots_[i].buffer);
    }
    csv_.close();
    presents_.close();
    stages_.close();
    tiles_.close();
}

}
