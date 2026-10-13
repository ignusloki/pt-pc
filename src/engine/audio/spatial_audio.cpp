#include "engine/audio/spatial_audio.h"

#include <algorithm>
#include <cmath>

namespace pt::audio {

#ifndef __APPLE__
std::unique_ptr<SpatialProcessor> CreateSpatialProcessor(uint32_t, std::string& reason) {
    reason = "spatial headphones are not included in this platform build";
    return nullptr;
}
#endif

StereoPeakLimiter::StereoPeakLimiter(uint32_t sample_rate)
    : release_(std::exp(-1.0f / (0.05f * static_cast<float>(std::max(sample_rate, 1u))))) {}

void StereoPeakLimiter::Process(float* stereo, uint32_t frames, AudioDiagnosticStats* diagnostics) {
    constexpr float ceiling = 0.98f;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        float& left = stereo[frame * 2];
        float& right = stereo[frame * 2 + 1];
        if (!std::isfinite(left) || !std::isfinite(right)) {
            if (diagnostics) ++diagnostics->invalid_limiter_frames;
            left = right = 0.0f;
        }
        const float peak = std::max(std::abs(left), std::abs(right));
        const float required = peak > ceiling ? ceiling / peak : 1.0f;
        gain_ = std::min(required, 1.0f - (1.0f - gain_) * release_);
        if (diagnostics) {
            if (gain_ < 0.9999f) ++diagnostics->limited_frames;
            diagnostics->minimum_gain = std::min(diagnostics->minimum_gain, gain_);
        }
        left *= gain_;
        right *= gain_;
    }
}

SpatialPlayback::SpatialPlayback(std::unique_ptr<SpatialProcessor> processor, uint32_t sample_rate)
    : processor_(std::move(processor)), limiter_(sample_rate) {
    failed_.store(processor_ == nullptr);
}

void SpatialPlayback::RenderBlock(float* stereo, uint32_t frames, const AudioRenderFunction& render,
                                  AudioDiagnosticStats* diagnostics) {
    if (frames == 0) return;
    if (frames > kSpatialMaxFrames) {
        for (uint32_t done = 0; done < frames;) {
            const uint32_t count = std::min(kSpatialMaxFrames, frames - done);
            RenderBlock(stereo + static_cast<size_t>(done) * 2, count, render, diagnostics);
            done += count;
        }
        return;
    }
    if (Failed()) {
        render(stereo, frames, 2);
        if (diagnostics) {
            diagnostics->renderer.Add(stereo, frames, 2);
            diagnostics->stereo_fallback_frames += frames;
        }
        return;
    }
    render(surround_.data(), frames, 8);
    if (diagnostics) diagnostics->renderer.Add(surround_.data(), frames, 8);
    if (!processor_->Process(surround_.data(), frames, stereo)) {
        if (diagnostics) ++diagnostics->spatial_failed_blocks;
        error_.store(processor_->ErrorCode());
        // The engine already advanced. Fold this block, then use the original stereo mix on later blocks.
        constexpr float fold = 0.70710678f;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const float* source = surround_.data() + frame * 8;
            stereo[frame * 2] = source[0] + fold * (source[2] + source[4] + source[6]);
            stereo[frame * 2 + 1] = source[1] + fold * (source[2] + source[5] + source[7]);
        }
        failed_.store(true);
    } else if (diagnostics) {
        diagnostics->spatial_frames += frames;
    }
    if (diagnostics) diagnostics->before_limiter.Add(stereo, frames, 2);
    limiter_.Process(stereo, frames, diagnostics);
}

} // namespace pt::audio
