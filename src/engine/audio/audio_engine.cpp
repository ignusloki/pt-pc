#include "engine/audio/audio_engine.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "engine/audio/channel_layout.h"
#include "engine/core/log.h"
#include "engine/platform/sdl_diag.h"

namespace pt::audio {
namespace {

std::atomic<uint64_t> g_diagnostic_session{0};
constexpr uint64_t kDiagnosticIntervalNs = 5'000'000'000;
constexpr std::array<const char*, 8> kChannelNames{"FL", "FR", "FC", "LFE", "BL", "BR", "SL", "SR"};

std::string LevelSummary(const AudioSignalLevels& signal) {
    std::string text;
    for (uint32_t channel = 0; channel < signal.max_channels; ++channel) {
        const auto& level = signal.channels[channel];
        text += std::format("{}{}(rms={:.6f},peak={:.6f},over1={},invalid={})", channel ? " " : "",
                            kChannelNames[channel], level.Rms(), level.peak, level.above_full_scale, level.nonfinite);
    }
    return text.empty() ? "no samples" : text;
}

std::string DeviceName(SDL_AudioDeviceID device) {
    const char* name = SDL_GetAudioDeviceName(device);
    return name ? name : "unavailable";
}

} // namespace

AudioOutput::AudioOutput() = default;

AudioOutput::~AudioOutput() {
    Close();
}

bool AudioOutput::Open(uint32_t sample_rate, OutputMode mode, RenderFunction render) {
    Close();
    diagnostics_enabled_ = AudioDiagnosticsEnabled();
    requested_mode_ = mode;
    sample_rate_ = sample_rate;
    diagnostic_stats_ = {};
    total_diagnostic_frames_ = total_put_errors_ = 0;
    device_diagnostics_.clear();
    if (diagnostics_enabled_) {
        diagnostic_session_ = ++g_diagnostic_session;
        next_diagnostic_ns_ = SDL_GetTicksNS() + kDiagnosticIntervalNs;
        LogInfo("audio diag [{}]: opening selected={} rate={} Hz; metrics only, no audio recording",
                diagnostic_session_, OutputModeName(mode), sample_rate);
    }
    const bool surround = mode == OutputMode::Surround;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LogError("audio: SDL audio init failed (surround requested: {}): {} (audio drivers built in: {})", surround,
                 SDL_GetError(), SdlCompiledAudioDrivers());
        return false;
    }
    render_ = std::move(render);
    if (mode == OutputMode::SpatialHeadphones) {
        std::string reason;
        if (auto processor = CreateSpatialProcessor(sample_rate, reason)) {
            spatial_ = std::make_unique<SpatialPlayback>(std::move(processor), sample_rate);
            LogInfo("audio: native spatial headphones initialized (fixed rendering, 7.1 input, stereo output)");
        } else {
            spatial_fallback_.store(true);
            LogWarn("audio: spatial headphones unavailable; using original stereo: {}", reason);
        }
    }
    SDL_AudioSpec preferred_spec{};
    const bool queried = SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &preferred_spec, nullptr);
    const int preferred_channels = queried ? preferred_spec.channels : -1;
    if (!queried) {
        LogWarn("audio: could not query preferred playback format (surround requested: {}): {}", surround, SDL_GetError());
    }
    LogInfo("audio: opening playback (surround requested: {}, preferred channels: {})", surround, preferred_channels);

    std::string last_error;
    const auto probes = AudioOutputProbeOrder(preferred_channels, surround);
    for (const uint32_t probe_channels : probes) {
        if (probe_channels == 0) break;

        const SDL_AudioSpec requested_spec{SDL_AUDIO_F32, static_cast<int>(probe_channels), static_cast<int>(sample_rate)};
        const SDL_AudioDeviceID device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &requested_spec);
        if (!device) {
            last_error = SDL_GetError();
            LogWarn("audio: {}-channel device open failed (surround requested: {}, preferred channels: {}): {}",
                    probe_channels, surround, preferred_channels, last_error);
            continue;
        }

        SDL_AudioSpec opened_spec{};
        if (!SDL_GetAudioDeviceFormat(device, &opened_spec, nullptr)) {
            last_error = SDL_GetError();
            LogWarn("audio: could not query opened {}-channel device (surround requested: {}): {}", probe_channels,
                    surround, last_error);
            SDL_CloseAudioDevice(device);
            continue;
        }

        const uint32_t app_channels = AudioOutputChannelsForDevice(opened_spec.channels, surround);
        if (surround && probe_channels > 2 && app_channels == 2) {
            LogInfo("audio: {}-channel request opened as {} channels; probing next layout", probe_channels,
                    opened_spec.channels);
            SDL_CloseAudioDevice(device);
            continue;
        }

        const SDL_AudioSpec app_spec{SDL_AUDIO_F32, static_cast<int>(app_channels), static_cast<int>(sample_rate)};
        SDL_AudioStream* stream = SDL_CreateAudioStream(&app_spec, nullptr);
        if (!stream) {
            last_error = SDL_GetError();
            LogWarn("audio: could not create {}-channel stream for {}-channel device: {}", app_channels,
                    opened_spec.channels, last_error);
            SDL_CloseAudioDevice(device);
            continue;
        }
        if (!SDL_BindAudioStream(device, stream)) {
            last_error = SDL_GetError();
            LogWarn("audio: could not bind {}-channel stream to {}-channel device: {}", app_channels,
                    opened_spec.channels, last_error);
            SDL_DestroyAudioStream(stream);
            SDL_CloseAudioDevice(device);
            continue;
        }

        SDL_AudioSpec bound_src{};
        SDL_AudioSpec bound_dst{};
        if (!SDL_GetAudioStreamFormat(stream, &bound_src, &bound_dst)) {
            last_error = SDL_GetError();
            LogWarn("audio: could not query bound stream format: {}", last_error);
            SDL_UnbindAudioStream(stream);
            SDL_DestroyAudioStream(stream);
            SDL_CloseAudioDevice(device);
            continue;
        }
        const uint32_t bound_app_channels = AudioOutputChannelsForDevice(bound_dst.channels, surround);
        if (surround && probe_channels > 2 && bound_app_channels == 2) {
            LogInfo("audio: bound {}-channel stream has {} output channels; probing next layout", app_channels,
                    bound_dst.channels);
            SDL_UnbindAudioStream(stream);
            SDL_DestroyAudioStream(stream);
            SDL_CloseAudioDevice(device);
            continue;
        }
        if (bound_src.channels != static_cast<int>(bound_app_channels)) {
            const SDL_AudioSpec adjusted_src{SDL_AUDIO_F32, static_cast<int>(bound_app_channels), static_cast<int>(sample_rate)};
            if (!SDL_SetAudioStreamFormat(stream, &adjusted_src, nullptr)) {
                last_error = SDL_GetError();
                LogWarn("audio: could not match stream input to {} actual output channels: {}", bound_app_channels,
                        last_error);
                SDL_UnbindAudioStream(stream);
                SDL_DestroyAudioStream(stream);
                SDL_CloseAudioDevice(device);
                continue;
            }
        }
        if (!SDL_SetAudioStreamGetCallback(stream, Callback, this)) {
            last_error = SDL_GetError();
            LogWarn("audio: could not install playback callback: {}", last_error);
            SDL_UnbindAudioStream(stream);
            SDL_DestroyAudioStream(stream);
            SDL_CloseAudioDevice(device);
            continue;
        }

        device_id_ = device;
        stream_ = stream;
        channels_.store(spatial_ ? 8 : bound_app_channels);
        active_mode_.store(spatial_ ? OutputMode::SpatialHeadphones : bound_app_channels > 2 ? OutputMode::Surround : OutputMode::Stereo);
        if (!SDL_ResumeAudioDevice(device_id_)) {
            last_error = SDL_GetError();
            LogError("audio: could not resume playback device (surround requested: {}, preferred channels: {}, requested channels: {}, actual channels: {}, renderer channels: {}): {}",
                     surround, preferred_channels, probe_channels, bound_dst.channels, Channels(), last_error);
            Close();
            LogSdlAudioDevices(false);
            return false;
        }
        LogInfo("audio: playback at {} Hz (surround requested: {}, preferred channels: {}, requested channels: {}, actual channels: {}, renderer channels: {})",
                sample_rate, surround, preferred_channels, probe_channels, bound_dst.channels, Channels());
        LogInfo("audio: requested output {}, active output {}", OutputModeName(mode), OutputModeName(ActiveMode()));
        LogSdlAudioDevices(false);
        if (diagnostics_enabled_) {
            LogDeviceDiagnostics();
            LogInfo("audio diag [{}]: active={} fallback={} renderer_channels={} SDL_input_channels={}; "
                    "a spatial headphone render is deliberately delivered to the device as stereo",
                    diagnostic_session_, OutputModeName(ActiveMode()), SpatialFallback(), Channels(), bound_app_channels);
            if (spatial_) {
                LogInfo("audio diag [{}]: backend=Apple_AUSpatialMixer input=7.1_MPEG_7_1_C output=headphones "
                        "source=ambience_bed interaural_delay=on extra_distance_attenuation=off reverb_blend=0 "
                        "limiter_ceiling=0.98 limiter_release_ms=50 maximum_block_frames={}",
                        diagnostic_session_, kSpatialMaxFrames);
            }
        }
        return true;
    }

    LogError("audio: cannot open playback device (surround requested: {}, preferred channels: {}, last error: {})", surround,
             preferred_channels, last_error.empty() ? SDL_GetError() : last_error);
    render_ = {};
    spatial_.reset();
    channels_ = 2;
    LogSdlAudioDevices(false);
    return false;
}

void AudioOutput::Close() {
    if (stream_) {
        // Wait for an in-flight callback before disposing its spatial processor or engine render function.
        SDL_SetAudioStreamGetCallback(stream_, nullptr, nullptr);
        ReportPendingFailure();
        if (diagnostics_enabled_) LogDiagnosticStats(diagnostic_stats_, true);
        SDL_UnbindAudioStream(stream_);
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    if (device_id_ != 0) {
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
    }
    render_ = {};
    spatial_.reset();
    channels_ = 2;
    active_mode_.store(OutputMode::Stereo);
    spatial_fallback_.store(false);
    runtime_failure_pending_.store(false);
    diagnostics_enabled_ = false;
}

void AudioOutput::ReportPendingFailure() {
    if (runtime_failure_pending_.exchange(false)) {
        LogWarn("audio: spatial processor failed ({}); continuing with original stereo", spatial_->ErrorCode());
    }
}

void AudioOutput::LogDeviceDiagnostics() {
    SDL_AudioSpec source{}, destination{}, device{};
    int buffer_frames = 0;
    const std::string opened_name = DeviceName(device_id_);
    const std::string default_name = DeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    const bool stream_ok = SDL_GetAudioStreamFormat(stream_, &source, &destination);
    const bool device_ok = SDL_GetAudioDeviceFormat(device_id_, &device, &buffer_frames);
    const std::string description = std::format(
        "driver={} logical_device_id={} playing_device=\"{}\" default_playback=\"{}\" "
        "stream_input={}Hz/{}ch/{} stream_output={}Hz/{}ch/{} device={}Hz/{}ch/{} buffer_frames={} "
        "buffer_ms={:.3f} stream_query_ok={} device_query_ok={}",
        SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unavailable", device_id_, opened_name, default_name,
        source.freq, source.channels, SDL_GetAudioFormatName(source.format),
        destination.freq, destination.channels, SDL_GetAudioFormatName(destination.format),
        device.freq, device.channels, SDL_GetAudioFormatName(device.format), buffer_frames,
        device.freq > 0 ? 1000.0 * buffer_frames / device.freq : 0, stream_ok, device_ok);
    if (description != device_diagnostics_) {
        LogInfo("audio diag [{}]: {}: {}", diagnostic_session_, device_diagnostics_.empty() ? "device" : "device/format changed",
                description);
        device_diagnostics_ = description;
    }
}

void AudioOutput::LogDiagnosticStats(const AudioDiagnosticStats& stats, bool final) {
    total_diagnostic_frames_ += stats.frames;
    total_put_errors_ += stats.put_errors;
    const double reduction_db = stats.minimum_gain > 0 ? -20.0 * std::log10(stats.minimum_gain) : 0;
    double input_energy = 0;
    for (const auto& channel : stats.renderer.channels) input_energy += channel.square_sum;
    const double rear_energy = stats.renderer.channels[4].square_sum + stats.renderer.channels[5].square_sum;
    const double side_energy = stats.renderer.channels[6].square_sum + stats.renderer.channels[7].square_sum;
    LogInfo("audio diag [{}]: {} selected={} active={} fallback={} interval_frames={} total_frames={} audio_seconds={:.3f} "
            "callbacks={} callback_frames={}..{} max_callback_ms={:.3f} callbacks_over_audio_budget={} "
            "spatial_frames={} failed_spatial_blocks={} stereo_fallback_frames={} submitted_bytes={} put_errors={} total_put_errors={}",
            diagnostic_session_, final ? "closing" : "interval", OutputModeName(requested_mode_), OutputModeName(ActiveMode()),
            SpatialFallback(), stats.frames, total_diagnostic_frames_, static_cast<double>(total_diagnostic_frames_) / sample_rate_,
            stats.callbacks, stats.callbacks ? stats.min_callback_frames : 0, stats.max_callback_frames,
            stats.max_callback_ns / 1.0e6, stats.slow_callbacks, stats.spatial_frames, stats.spatial_failed_blocks,
            stats.stereo_fallback_frames, stats.put_bytes, stats.put_errors, total_put_errors_);
    LogInfo("audio diag [{}]: renderer {} rear_energy_percent={:.2f} side_energy_percent={:.2f}",
            diagnostic_session_, LevelSummary(stats.renderer), input_energy > 0 ? rear_energy * 100 / input_energy : 0,
            input_energy > 0 ? side_energy * 100 / input_energy : 0);
    if (stats.before_limiter.max_channels) {
        LogInfo("audio diag [{}]: before_limiter {}", diagnostic_session_, LevelSummary(stats.before_limiter));
    }
    LogInfo("audio diag [{}]: output {} LR_normalized_correlation={:.5f} limited_frames={} min_limiter_gain={:.6f} "
            "max_gain_reduction_db={:.3f} invalid_limiter_frames={}",
            diagnostic_session_, LevelSummary(stats.output), stats.output.LeftRightCorrelation(), stats.limited_frames,
            stats.minimum_gain, reduction_db, stats.invalid_limiter_frames);
    if (stats.put_errors) {
        LogWarn("audio diag [{}]: SDL stream write failed {} times: {}", diagnostic_session_, stats.put_errors,
                stats.last_put_error.data());
    }
}

bool AudioOutput::ReportDiagnostics() {
    if (!diagnostics_enabled_ || !stream_) return false;
    const uint64_t now = SDL_GetTicksNS();
    if (now < next_diagnostic_ns_) return false;
    next_diagnostic_ns_ = now + kDiagnosticIntervalNs;
    AudioDiagnosticStats snapshot;
    // SDL holds this same lock during callbacks. Only copy/reset while locked;
    // every query, string allocation and log write takes place after unlocking.
    if (!SDL_LockAudioStream(stream_)) return false;
    snapshot = diagnostic_stats_;
    diagnostic_stats_ = {};
    SDL_UnlockAudioStream(stream_);
    LogDeviceDiagnostics();
    LogDiagnosticStats(snapshot, false);
    LogInfo("audio diag [{}]: pending_stream_bytes={}", diagnostic_session_, SDL_GetAudioStreamQueued(stream_));
    return true;
}

void AudioOutput::Callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* output = static_cast<AudioOutput*>(user);
    if (additional <= 0 || !output->render_) {
        return;
    }
    // The spatial path feeds stereo bytes to SDL while the game renderer produces eight channels.
    const uint32_t channels = output->spatial_ ? 2 : output->Channels();
    const uint32_t frames = static_cast<uint32_t>(additional) / (channels * sizeof(float));
    if (frames == 0) {
        return;
    }
    auto* diagnostics = output->diagnostics_enabled_ ? &output->diagnostic_stats_ : nullptr;
    const uint64_t started = diagnostics ? SDL_GetTicksNS() : 0;
    if (diagnostics) {
        ++diagnostics->callbacks;
        diagnostics->frames += frames;
        diagnostics->min_callback_frames = std::min(diagnostics->min_callback_frames, frames);
        diagnostics->max_callback_frames = std::max(diagnostics->max_callback_frames, frames);
    }
    const auto put = [&](const float* samples, uint32_t count) {
        const int bytes = static_cast<int>(count * channels * sizeof(float));
        if (diagnostics) diagnostics->output.Add(samples, count, channels);
        const bool ok = SDL_PutAudioStreamData(stream, samples, bytes);
        if (diagnostics) {
            if (ok) diagnostics->put_bytes += bytes;
            else {
                ++diagnostics->put_errors;
                SDL_strlcpy(diagnostics->last_put_error.data(), SDL_GetError(), diagnostics->last_put_error.size());
            }
        }
    };
    if (output->spatial_) {
        for (uint32_t done = 0; done < frames;) {
            const uint32_t count = std::min(kSpatialMaxFrames, frames - done);
            output->spatial_->RenderBlock(output->spatial_buffer_.data(), count, output->render_, diagnostics);
            if (output->spatial_->Failed() && !output->spatial_fallback_.exchange(true)) {
                output->active_mode_.store(OutputMode::Stereo);
                output->channels_.store(2);
                output->runtime_failure_pending_.store(true);
            }
            put(output->spatial_buffer_.data(), count);
            done += count;
        }
    } else {
        output->buffer_.resize(static_cast<size_t>(frames) * channels);
        output->render_(output->buffer_.data(), frames, channels);
        if (diagnostics) {
            diagnostics->renderer.Add(output->buffer_.data(), frames, channels);
            if (output->SpatialFallback()) diagnostics->stereo_fallback_frames += frames;
        }
        put(output->buffer_.data(), frames);
    }
    if (diagnostics) {
        const uint64_t elapsed = SDL_GetTicksNS() - started;
        diagnostics->max_callback_ns = std::max(diagnostics->max_callback_ns, elapsed);
        if (elapsed * static_cast<uint64_t>(output->sample_rate_) > static_cast<uint64_t>(frames) * 1'000'000'000) {
            ++diagnostics->slow_callbacks;
        }
    }
}

}
