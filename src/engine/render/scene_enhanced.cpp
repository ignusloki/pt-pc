// Adapted from hartapfel/pt-pc-hdr dd90825 (MIT).
#include "engine/render/scene_renderer.h"
#include "engine/render/exposure_math.h"
#include "engine/assets/ftex.h"
#include "engine/fs/vfs.h"
#include "engine/core/log.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <map>
namespace pt {
namespace {
float Hermite(float t,float s0,float s1) {const float t2=t*t,t3=t2*t;return 3.0f*t2-2.0f*t3+s0*(t3-2.0f*t2+t)+s1*(t3-t2);}
}

bool SceneRenderer::BuildEnhancedColorLut(const std::string& path, const ScreenSettings& screen, std::vector<float>& out) {
    std::vector<glm::vec4> filter;
    if (!path.empty() && vfs_) {
        FtexTexture ftex;
        if (LoadFtex(vfs_->Textures(), path, ftex) && ftex.width == 16 && ftex.height == 16 && ftex.depth == 16 && !ftex.mips.empty() &&
            ftex.mips[0].size() >= 16 * 16 * 16 * 4) {
            filter.resize(16 * 16 * 16);
            const uint8_t* data = ftex.mips[0].data();
            for (size_t i = 0; i < filter.size(); ++i) {
                glm::vec4 v(data[i * 4 + 2], data[i * 4 + 1], data[i * 4 + 0], data[i * 4 + 3]);
                v /= 255.0f;
                if (ftex.Srgb()) {
                    for (int c = 0; c < 3; ++c) {
                        v[c] = v[c] <= 0.04045f ? v[c] / 12.92f : std::pow((v[c] + 0.055f) / 1.055f, 2.4f);
                    }
                }
                filter[i] = v;
            }
        } else {
            LogWarn("scene renderer: color LUT {} unavailable, using identity", path);
        }
    }
    auto fetch = [&](int x, int y, int z) {
        if (filter.empty()) {
            return glm::vec4(x / 15.0f, y / 15.0f, z / 15.0f, 1.0f);
        }
        return filter[(static_cast<size_t>(z) * 16 + y) * 16 + x];
    };
    auto sample = [&](glm::vec3 coord) {
        coord = glm::clamp(coord, glm::vec3(0.0f), glm::vec3(15.0f));
        const glm::ivec3 i0 = glm::min(glm::ivec3(glm::floor(coord)), glm::ivec3(14));
        const glm::vec3 f = coord - glm::vec3(i0);
        glm::vec4 result(0.0f);
        for (int dz = 0; dz < 2; ++dz) {
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const float w = (dx ? f.x : 1.0f - f.x) * (dy ? f.y : 1.0f - f.y) * (dz ? f.z : 1.0f - f.z);
                    result += w * fetch(i0.x + dx, i0.y + dy, i0.z + dz);
                }
            }
        }
        return result;
    };
    const int size = graphics.lut_sampling == 1 ? 64 : 16;
    out.assign(size * size * size * 4, 0);
    for (int g = 0; g < size; ++g) {
        for (int b = 0; b < size; ++b) {
            for (int r = 0; r < size; ++r) {
                const glm::vec4 v = sample(glm::vec3(r, g, b) * (15.0f / static_cast<float>(size-1)) * screen.color_scale);
                float* p = &out[(static_cast<size_t>(g) * size * size + b * size + r) * 4];
                for (int c = 0; c < 4; ++c) {
                    p[c] = Hermite(std::clamp(v[c], 0.0f, 1.0f), screen.start_slope, screen.end_slope);
                }
            }
        }
    }
    return !filter.empty();
}

void SceneRenderer::UpdateEnhancedColorLut(const ScreenSettings& screen) {
    const std::string key = std::format("{}|{:.4f}|{:.4f}|{:.4f}|{:.4f}|{:.4f}|{}", screen.lut_path, screen.color_scale.x, screen.color_scale.y,
                                        screen.color_scale.z, screen.start_slope, screen.end_slope, graphics.lut_sampling);
    if (key == color_lut_key_) {
        return;
    }
    std::vector<float> lut;
    BuildEnhancedColorLut(screen.lut_path, screen, lut);
    gpu_->WaitIdle();
    std::swap(color_lut_, color_lut_prev_);
    UploadImage(color_lut_, gpu::Format::R32G32B32A32Sfloat, {graphics.lut_sampling == 1 ? 4096u : 256u, graphics.lut_sampling == 1 ? 64u : 16u, 1}, lut.data(), lut.size() * sizeof(float));
    if (color_lut_key_.empty()) {
        UploadImage(color_lut_prev_, gpu::Format::R32G32B32A32Sfloat, {graphics.lut_sampling == 1 ? 4096u : 256u, graphics.lut_sampling == 1 ? 64u : 16u, 1}, lut.data(), lut.size() * sizeof(float));
    }
    color_lut_prev_key_ = color_lut_key_;
    color_lut_key_ = key;
    if (extent_.width > 0) {
        WriteImageDescriptors();
    }
}

void SceneRenderer::ReadEnhancedMeasurements(FrameSlot& slot, float dt, const ExposureSettings& settings) {
    const bool advance_exposure = std::isfinite(dt) && dt > 0.0f;
    if (advance_exposure) exposure_time_ += std::min(dt, 0.1f);
    if (slot.timed) {
        ReadTimestamps(slot);
    }
    if (slot.light_timed && !slot.light_names.empty()) {
        static std::map<std::string, std::pair<double, uint32_t>> sums;
        static uint32_t frames = 0;
        const uint32_t n = static_cast<uint32_t>(slot.light_names.size());
        std::vector<uint64_t> stamps(2 * n);
        if (gpu_->ReadTimestamps(slot.light_queries, 0, 2 * n, stamps.data())) {
            const double period = gpu_->Info().timestamp_period_ns * 1.0e-6;
            for (uint32_t i = 0; i < n; ++i) {
                auto& [ms, count] = sums[slot.light_names[i]];
                ms += stamps[2 * i + 1] >= stamps[2 * i] ? static_cast<double>(stamps[2 * i + 1] - stamps[2 * i]) * period : 0.0;
                ++count;
            }
            if (++frames % 240 == 0) {
                std::vector<std::pair<double, std::string>> order;
                double total = 0.0;
                for (const auto& [name, v] : sums) {
                    order.emplace_back(v.first / frames, std::format("{} ({} frames)", name, v.second));
                    total += v.first / frames;
                }
                std::sort(order.rbegin(), order.rend());
                std::string text;
                for (size_t i = 0; i < std::min<size_t>(order.size(), 24); ++i) text += std::format("; {:.3f} {}", order[i].first, order[i].second);
                LogInfo("light timing: {:.3f} ms a frame over {} lights{}", total, sums.size(), text);
                sums.clear();
                frames = 0;
            }
        }
        slot.light_timed = false;
    }
    if (slot.reflection_sampled) {
        gpu_->InvalidateMapped(slot.luminance, 0, ~0ull);
        const glm::vec2* entries = static_cast<const glm::vec2*>(slot.luminance.mapped);
        const glm::vec2 rg = entries[gpu::kReflectionReadback];
        const glm::vec2 b = entries[gpu::kReflectionReadback + 1];
        if (b.y > 0.5f) {
            stats_.reflection_readback = glm::vec3(rg, b.x);
            ++stats_.reflection_readbacks;
        }
        slot.reflection_sampled = false;
    }
    if (!slot.measured || slot.luminance_groups == 0) {
        return;
    }
    gpu_->InvalidateMapped(slot.luminance, 0, ~0ull);
    const float measured = exposure_math::Meter({static_cast<const glm::vec2*>(slot.luminance.mapped), slot.luminance_groups});
    slot.measured = false;
    if (measured < 0.0f) {
        return;
    }
    stats_.luminance = measured;
    if (!advance_exposure) {
        return;
    }
    if (!adaptation_valid_ || !toggles.adaptation || !lighting_ || !lighting_->valid || settings.pinned) return;
    // Remove the readback frame's full pre-exposure, including compensation.
    // An absolute scene target avoids feeding delayed exposed values back in.
    if (!std::isfinite(slot.exposure_used) || slot.exposure_used <= 0.0f) return;
    const float raw = measured / slot.exposure_used;
    if (!std::isfinite(raw)) return;
    const float target = exposure_math::TargetEv(raw, settings.key, settings.min_ev, settings.max_ev);
    ev_ = exposure_math::Adapt(std::clamp(ev_, settings.min_ev, settings.max_ev), target, dt, settings.speed);
    if (static const bool ev_trace = std::getenv("PT_EV_TRACE") != nullptr; ev_trace) {
        LogInfo("ev trace: measured {:.6f} raw {:.6f} sample_exposure {:.6f} target {:.5f} ev {:.5f} dt {:.6f} time {:.6f} sample_time {:.6f}",
                measured, raw, slot.exposure_used, target, ev_, dt, exposure_time_, slot.exposure_sample_time);
    }
}
}
