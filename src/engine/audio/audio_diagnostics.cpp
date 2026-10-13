#include "engine/audio/audio_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace pt::audio {

bool AudioDiagnosticsEnabled() {
    const char* value = std::getenv("PT_AUDIO_DIAGNOSTICS");
    return value && std::string_view(value) == "1";
}

double AudioChannelLevel::Rms() const {
    return samples ? std::sqrt(square_sum / static_cast<double>(samples)) : 0;
}

void AudioSignalLevels::Add(const float* interleaved, uint32_t frames, uint32_t channel_count) {
    if (!frames || !channel_count || channel_count > channels.size()) return;
    max_channels = std::max(max_channels, channel_count);
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const float* source = interleaved + static_cast<size_t>(frame) * channel_count;
        for (uint32_t channel = 0; channel < channel_count; ++channel) {
            auto& level = channels[channel];
            ++level.samples;
            const float sample = source[channel];
            if (!std::isfinite(sample)) {
                ++level.nonfinite;
                continue;
            }
            level.square_sum += static_cast<double>(sample) * sample;
            const float peak = std::abs(sample);
            level.peak = std::max(level.peak, peak);
            if (peak > 1) ++level.above_full_scale;
        }
        if (channel_count >= 2 && std::isfinite(source[0]) && std::isfinite(source[1])) {
            left_right_sum += static_cast<double>(source[0]) * source[1];
        }
    }
}

double AudioSignalLevels::LeftRightCorrelation() const {
    const double product = channels[0].square_sum * channels[1].square_sum;
    return product > 0 ? std::clamp(left_right_sum / std::sqrt(product), -1.0, 1.0) : 0;
}

} // namespace pt::audio
