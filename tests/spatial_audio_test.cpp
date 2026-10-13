#include "engine/audio/spatial_audio.h"
#include "engine/audio/audio_engine.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace {
using namespace pt::audio;

bool Bounded(const std::vector<float>& samples, float maximum = 0.98001f) {
    return std::all_of(samples.begin(), samples.end(), [=](float v) { return std::isfinite(v) && std::abs(v) <= maximum; });
}

class FailedProcessor final : public SpatialProcessor {
public:
    bool Process(const float*, uint32_t, float*) override { ++calls; return false; }
    int32_t ErrorCode() const override { return -123; }
    int calls = 0;
};

class LoudProcessor final : public SpatialProcessor {
public:
    bool Process(const float* in, uint32_t frames, float* out) override {
        for (uint32_t frame = 0; frame < frames; ++frame) {
            out[frame * 2] = 8 * in[frame * 8] + in[frame * 8 + 4];
            out[frame * 2 + 1] = 8 * in[frame * 8 + 1] + in[frame * 8 + 5];
        }
        return true;
    }
    int32_t ErrorCode() const override { return 0; }
};

bool DiagnosticTests() {
    AudioSignalLevels signal;
    const float samples[]{0.5f, -0.25f, 0.5f, -0.25f};
    signal.Add(samples, 2, 2);
    if (signal.channels[0].Rms() != 0.5 || signal.channels[1].Rms() != 0.25 ||
        signal.LeftRightCorrelation() != -1 || signal.channels[0].samples != 2) return false;
    const float invalid[]{std::numeric_limits<float>::quiet_NaN(), 1.5f};
    signal.Add(invalid, 1, 2);
    if (signal.channels[0].nonfinite != 1 || signal.channels[1].above_full_scale != 1 ||
        !std::isfinite(signal.channels[0].Rms())) return false;

    SpatialPlayback plain(std::make_unique<LoudProcessor>(), 48000);
    SpatialPlayback measured(std::make_unique<LoudProcessor>(), 48000);
    const AudioRenderFunction render = [](float* out, uint32_t frames, uint32_t channels) {
        std::fill_n(out, frames * channels, 0.0f);
        for (uint32_t frame = 0; frame < frames; ++frame) {
            out[frame * channels] = 0.25f;
            out[frame * channels + 1] = 0.125f;
            out[frame * channels + 4] = 0.5f;
        }
    };
    // Cross fixed-block boundaries, and verify measurement never changes the PCM.
    std::vector<float> reference(1300 * 2), output(reference.size());
    AudioDiagnosticStats stats;
    plain.RenderBlock(reference.data(), 1300, render);
    measured.RenderBlock(output.data(), 1300, render, &stats);
    stats.output.Add(output.data(), 1300, 2);
    if (reference != output || stats.spatial_frames != 1300 || stats.renderer.channels[4].samples != 1300 ||
        stats.renderer.channels[4].Rms() != 0.5 || stats.before_limiter.channels[0].peak != 2.5f ||
        stats.before_limiter.channels[0].above_full_scale != 1300 || stats.output.channels[0].peak > 0.98001f ||
        stats.output.channels[0].above_full_scale != 0 || stats.limited_frames != 1300 || stats.minimum_gain > 0.393f) return false;

    SpatialPlayback failed(std::make_unique<FailedProcessor>(), 48000);
    AudioDiagnosticStats failure;
    const AudioRenderFunction silent = [](float* out, uint32_t frames, uint32_t channels) {
        std::fill_n(out, frames * channels, 0.0f);
    };
    failed.RenderBlock(output.data(), 13, silent, &failure);
    failed.RenderBlock(output.data(), 13, silent, &failure);
    return failure.spatial_failed_blocks == 1 && failure.spatial_frames == 0 && failure.stereo_fallback_frames == 13 &&
           failure.renderer.channels[0].samples == 26 && failure.renderer.channels[7].samples == 13;
}

bool SharedTests() {
    StereoPeakLimiter limiter(48000);
    std::vector<float> signal(20000 * 2);
    for (size_t frame = 0; frame < signal.size() / 2; ++frame) {
        signal[frame * 2] = frame < 1000 ? 3.5f : 0.1f;
        signal[frame * 2 + 1] = signal[frame * 2] * 0.25f;
    }
    limiter.Process(signal.data(), 20000);
    if (!Bounded(signal) || std::abs(signal.back() - 0.025f) > 0.00001f) return false;
    for (size_t frame = 0; frame < signal.size() / 2; ++frame) {
        if (std::abs(signal[frame * 2 + 1] - signal[frame * 2] * 0.25f) > 0.000001f) return false;
    }
    float invalid[] = {std::numeric_limits<float>::infinity(), 0.1f};
    limiter.Process(invalid, 1);
    if (invalid[0] != 0 || invalid[1] != 0) return false;

    auto failed = std::make_unique<FailedProcessor>();
    auto* observer = failed.get();
    SpatialPlayback playback(std::move(failed), 48000);
    uint32_t produced = 0, calls = 0, last_channels = 0;
    const AudioRenderFunction render = [&](float* out, uint32_t frames, uint32_t channels) {
        ++calls;
        last_channels = channels;
        produced += frames;
        std::fill_n(out, frames * channels, 0.0f);
        for (uint32_t frame = 0; frame < frames; ++frame) out[frame * channels] = 0.25f;
    };
    std::vector<float> output(13 * 2);
    playback.RenderBlock(output.data(), 0, render);
    if (calls != 0) return false;
    playback.RenderBlock(output.data(), 13, render);
    if (!playback.Failed() || playback.ErrorCode() != -123 || calls != 1 || produced != 13 ||
        last_channels != 8 || observer->calls != 1 || !Bounded(output) || output[0] != 0.25f || output[1] != 0) return false;
    playback.RenderBlock(output.data(), 13, render);
    if (calls != 2 || produced != 26 || last_channels != 2 || observer->calls != 1 || output[0] != 0.25f) return false;
    return true;
}

std::vector<float> NativeSignal(const std::vector<uint32_t>& requests, bool correlated, uint64_t& produced, bool measure = false) {
    std::string reason;
    auto native = CreateSpatialProcessor(48000, reason);
    if (!native) { std::printf("native create failed: %s\n", reason.c_str()); return {}; }
    SpatialPlayback playback(std::move(native), 48000);
    std::vector<float> output(96000 * 2);
    produced = 0;
    const AudioRenderFunction render = [&](float* out, uint32_t frames, uint32_t channels) {
        if (channels != 8) return; // Failure switches to two channels; the frame-count check then fails.
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (uint32_t channel = 0; channel < 8; ++channel) {
                const double frequency = correlated ? 440.0 : 173.0 + channel * 107.0;
                const double amplitude = correlated ? 0.9 : 0.1;
                out[frame * 8 + channel] = channel == 3 || produced + frame < 4096 ? 0.0f :
                    static_cast<float>(amplitude * std::sin((produced + frame) * frequency * 6.283185307179586 / 48000.0));
            }
        }
        produced += frames;
    };
    uint32_t done = 0;
    size_t request = 0;
    AudioDiagnosticStats stats;
    while (done < 96000) {
        const uint32_t frames = std::min(requests[request++ % requests.size()], 96000u - done);
        playback.RenderBlock(output.data() + done * 2, frames, render, measure ? &stats : nullptr);
        if (playback.Failed()) { std::printf("native process failed: %d\n", playback.ErrorCode()); return {}; }
        done += frames;
    }
    if (measure && (stats.spatial_frames != produced || stats.spatial_failed_blocks ||
                    stats.renderer.channels[7].samples != produced || stats.before_limiter.channels[0].samples != produced)) return {};
    return output;
}

bool NativeTests() {
    uint64_t produced_a = 0, produced_b = 0, produced_loud = 0;
    const auto reference = NativeSignal({256}, false, produced_a);
    const auto irregular = NativeSignal({1, 7, 127, 255, 511, 512, 997, 4096}, false, produced_b);
    const auto loud = NativeSignal({1, 7, 127, 255, 511, 512, 997, 4096}, true, produced_loud);
    uint64_t produced_measured = 0;
    const auto measured = NativeSignal({256}, false, produced_measured, true);
    if (reference.empty() || irregular.empty() || loud.empty() || produced_a != 96000 || produced_b != 96000 ||
        produced_loud != 96000 || !Bounded(reference) || !Bounded(irregular) || !Bounded(loud) ||
        produced_measured != 96000 || reference != measured) return false;
    float difference = 0;
    for (size_t i = 0; i < reference.size(); ++i) difference = std::max(difference, std::abs(reference[i] - irregular[i]));
    const float peak = *std::max_element(loud.begin(), loud.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
    std::printf("native variable-buffer max difference: %.9g; protected stress peak: %.9g\n", difference, std::abs(peak));
    if (difference > 0.00002f || std::abs(peak) < 0.9f) return false;

    for (int channel = 0; channel < 8; ++channel) {
        std::string reason;
        auto processor = CreateSpatialProcessor(48000, reason);
        if (!processor) return false;
        SpatialPlayback playback(std::move(processor), 48000);
        uint64_t offset = 0;
        std::array<float, kSpatialMaxFrames * 2> result{};
        double energy[2]{};
        const AudioRenderFunction render = [&](float* out, uint32_t frames, uint32_t channels) {
            std::fill_n(out, frames * channels, 0.0f);
            for (uint32_t frame = 0; frame < frames; ++frame) {
                if (offset + frame == 4096) out[frame * channels + channel] = 0.25f;
            }
            offset += frames;
        };
        for (uint32_t done = 0; done < 8192; done += 256) {
            playback.RenderBlock(result.data(), 256, render);
            if (playback.Failed()) return false;
            for (uint32_t i = 0; i < 512; ++i) energy[i % 2] += result[i] * result[i];
        }
        const bool left = channel == 0 || channel == 4 || channel == 6;
        const bool right = channel == 1 || channel == 5 || channel == 7;
        if (energy[0] + energy[1] == 0 || (left && energy[0] <= energy[1]) || (right && energy[1] <= energy[0])) return false;
        std::printf("SDL speaker %d: left %.8f right %.8f\n", channel, energy[0], energy[1]);
    }

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    AudioOutput output;
    const bool opened = output.Open(48000, OutputMode::SpatialHeadphones,
        [](float* out, uint32_t frames, uint32_t channels) { std::fill_n(out, frames * channels, 0.0f); });
    SDL_Delay(50);
    const bool active = opened && output.IsOpen() && output.ActiveMode() == OutputMode::SpatialHeadphones &&
                        output.Channels() == 8 && !output.SpatialFallback();
    output.Close();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return active;
}

} // namespace

int main(int argc, char** argv) {
    if (!SharedTests()) { std::puts("FAIL: limiter or single-render fallback"); return 1; }
    std::puts("PASS: stereo-linked protection, finite output, zero frames and single-render fallback");
    if (!DiagnosticTests()) { std::puts("FAIL: audio diagnostic measurements"); return 1; }
    std::puts("PASS: diagnostic levels, clipping/fallback counters and unchanged PCM with measurement enabled");
    if (argc == 2 && std::string_view(argv[1]) == "--native") {
        if (!NativeTests()) { std::puts("FAIL: native spatial rendering"); return 1; }
        std::puts("PASS: native spatial rendering, speaker directions and SDL lifecycle");
    }
    return 0;
}
