#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "engine/audio/audio_diagnostics.h"

namespace pt::audio {

using AudioRenderFunction = std::function<void(float*, uint32_t, uint32_t)>;
constexpr uint32_t kSpatialMaxFrames = 512;

class SpatialProcessor {
public:
    virtual ~SpatialProcessor() = default;
    virtual bool Process(const float* surround, uint32_t frames, float* stereo) = 0;
    virtual int32_t ErrorCode() const = 0;
};

std::unique_ptr<SpatialProcessor> CreateSpatialProcessor(uint32_t sample_rate, std::string& reason);

// One shared gain for both ears preserves the spatial renderer's left/right balance.
class StereoPeakLimiter {
public:
    explicit StereoPeakLimiter(uint32_t sample_rate);
    void Process(float* stereo, uint32_t frames, AudioDiagnosticStats* diagnostics = nullptr);

private:
    float gain_ = 1.0f;
    float release_ = 0.0f;
};

// Owns fixed buffers; the AU callback only pulls prepared PCM and never advances the engine.
class SpatialPlayback {
public:
    SpatialPlayback(std::unique_ptr<SpatialProcessor> processor, uint32_t sample_rate);
    void RenderBlock(float* stereo, uint32_t frames, const AudioRenderFunction& render,
                     AudioDiagnosticStats* diagnostics = nullptr);
    bool Failed() const { return failed_.load(); }
    int32_t ErrorCode() const { return error_.load(); }

private:
    std::unique_ptr<SpatialProcessor> processor_;
    std::array<float, kSpatialMaxFrames * 8> surround_{};
    StereoPeakLimiter limiter_;
    std::atomic<bool> failed_{false};
    std::atomic<int32_t> error_{0};
};

} // namespace pt::audio
