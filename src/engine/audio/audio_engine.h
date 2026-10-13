#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/output_mode.h"
#include "engine/audio/spatial_audio.h"

struct SDL_AudioStream;

namespace pt::audio {

class AudioOutput {
public:
    using RenderFunction = std::function<void(float* interleaved, uint32_t frames, uint32_t channels)>;

    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool Open(uint32_t sample_rate, OutputMode mode, RenderFunction render);
    void Close();
    bool IsOpen() const { return stream_ != nullptr; }
    uint32_t Channels() const { return channels_.load(); }
    OutputMode ActiveMode() const { return active_mode_.load(); }
    bool SpatialFallback() const { return spatial_fallback_.load(); }
    void ReportPendingFailure();
    bool ReportDiagnostics();
    bool DiagnosticsEnabled() const { return diagnostics_enabled_; }
    uint64_t DiagnosticSession() const { return diagnostic_session_; }

private:
    static void Callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void LogDiagnosticStats(const AudioDiagnosticStats& stats, bool final);
    void LogDeviceDiagnostics();

    SDL_AudioStream* stream_ = nullptr;
    uint32_t device_id_ = 0;
    RenderFunction render_;
    std::vector<float> buffer_;
    std::unique_ptr<SpatialPlayback> spatial_;
    std::array<float, kSpatialMaxFrames * 2> spatial_buffer_{};
    std::atomic<uint32_t> channels_{2};
    std::atomic<OutputMode> active_mode_{OutputMode::Stereo};
    std::atomic<bool> spatial_fallback_{false};
    std::atomic<bool> runtime_failure_pending_{false};
    bool diagnostics_enabled_ = false;
    OutputMode requested_mode_ = OutputMode::Stereo;
    uint32_t sample_rate_ = 48000;
    uint64_t diagnostic_session_ = 0;
    uint64_t next_diagnostic_ns_ = 0;
    uint64_t total_diagnostic_frames_ = 0;
    uint64_t total_put_errors_ = 0;
    AudioDiagnosticStats diagnostic_stats_;
    std::string device_diagnostics_;
};

}
