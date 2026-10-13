#pragma once

#include <array>
#include <cstdint>

namespace pt::audio {

// Opt-in for local listening tests. Collect numbers only; never keep or write PCM.
bool AudioDiagnosticsEnabled();

struct AudioChannelLevel {
    uint64_t samples = 0;
    uint64_t nonfinite = 0;
    uint64_t above_full_scale = 0;
    double square_sum = 0;
    float peak = 0;

    double Rms() const;
};

struct AudioSignalLevels {
    std::array<AudioChannelLevel, 8> channels{};
    uint32_t max_channels = 0;
    double left_right_sum = 0;

    void Add(const float* interleaved, uint32_t frames, uint32_t channel_count);
    double LeftRightCorrelation() const;
};

// Written only during the SDL callback. The game thread copies/resets it while
// holding the stream lock, then unlocks before formatting or writing the log.
struct AudioDiagnosticStats {
    AudioSignalLevels renderer;
    AudioSignalLevels before_limiter;
    AudioSignalLevels output;
    uint64_t frames = 0;
    uint64_t callbacks = 0;
    uint64_t spatial_frames = 0;
    uint64_t spatial_failed_blocks = 0;
    uint64_t stereo_fallback_frames = 0;
    uint64_t limited_frames = 0;
    uint64_t invalid_limiter_frames = 0;
    uint64_t put_errors = 0;
    uint64_t put_bytes = 0;
    uint64_t slow_callbacks = 0;
    uint64_t max_callback_ns = 0;
    uint32_t min_callback_frames = UINT32_MAX;
    uint32_t max_callback_frames = 0;
    float minimum_gain = 1;
    std::array<char, 256> last_put_error{};
};

} // namespace pt::audio
