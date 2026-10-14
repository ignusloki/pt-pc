#include "engine/voice/microphone.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <thread>

#include "engine/core/log.h"
#include "engine/platform/sdl_diag.h"
#include "engine/platform/android.h"

namespace pt {

struct Microphone::PendingOpen {
    std::mutex mutex;
    std::thread worker;
    bool done = false;
    bool cancelled = false;
    SDL_AudioStream* stream = nullptr;
};

Microphone::~Microphone() {
    Close();
    ReapOpenWorkers(true);
}

void Microphone::ReapOpenWorkers(bool wait) {
    for (auto it = cancelled_opens_.begin(); it != cancelled_opens_.end();) {
        const auto state = *it;
        bool done = false;
        {
            std::lock_guard lock(state->mutex);
            done = state->done;
        }
        if (wait || done) {
            if (state->worker.joinable()) state->worker.join();
            it = cancelled_opens_.erase(it);
        } else {
            ++it;
        }
    }
}

bool Microphone::Open(int sample_rate, const std::string& device_name) {
    Close();
    ReapOpenWorkers(true);
    sample_rate_ = sample_rate;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LogError("microphone: SDL audio init failed: {} (audio drivers built in: {})", SDL_GetError(), SdlCompiledAudioDrivers());
        return false;
    }
    stream_ = OpenStream(sample_rate, device_name);
    return stream_ != nullptr;
}

Microphone::AsyncState Microphone::OpenAsync(int sample_rate, const std::string& device_name) {
    ReapOpenWorkers(false);
    if (stream_) {
        return AsyncState::Opened;
    }
    if (!pending_) {
        // A cancelled driver call can still be opening its endpoint. Wait without blocking the game before starting another.
        if (!cancelled_opens_.empty()) return AsyncState::Pending;
        sample_rate_ = sample_rate;
        if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LogError("microphone: SDL audio init failed: {} (audio drivers built in: {})", SDL_GetError(), SdlCompiledAudioDrivers());
            return AsyncState::Failed;
        }
        pending_ = std::make_shared<PendingOpen>();
        pending_->worker = std::thread([state = pending_, sample_rate, device_name] {
            SDL_AudioStream* stream = OpenStream(sample_rate, device_name);
            std::lock_guard lock(state->mutex);
            if (state->cancelled) {
                if (stream) SDL_DestroyAudioStream(stream);
                stream = nullptr;
            }
            state->stream = stream;
            state->done = true;
        });
        return AsyncState::Pending;
    }
    // Keep the state alive until the guard releases its mutex, even after the worker has exited.
    const auto state = pending_;
    {
        std::lock_guard lock(state->mutex);
        if (!state->done) return AsyncState::Pending;
        stream_ = state->stream;
        state->stream = nullptr;
        pending_.reset();
    }
    if (state->worker.joinable()) state->worker.join();
    return stream_ ? AsyncState::Opened : AsyncState::Failed;
}

SDL_AudioStream* Microphone::OpenStream(int sample_rate, const std::string& device_name) {
#ifdef __ANDROID__
    return OpenAndroidPhoneMicrophone(sample_rate);
#else
    const auto started = std::chrono::steady_clock::now();
    SDL_AudioDeviceID device = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    const std::string& desired_device = device_name;
    if (!desired_device.empty()) {
        auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        };
        const std::string wanted = lower(desired_device);
        int count = 0;
        SDL_AudioDeviceID* devices = SDL_GetAudioRecordingDevices(&count);
        for (int i = 0; devices && i < count; ++i) {
            const char* name = SDL_GetAudioDeviceName(devices[i]);
            if (name && lower(name).find(wanted) != std::string::npos) {
                device = devices[i];
                LogInfo("microphone: using {}", name);
                break;
            }
        }
        SDL_free(devices);
        if (device == SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
            LogWarn("microphone: no device matches '{}', using default", desired_device);
        }
    }
    SDL_AudioSpec spec{SDL_AUDIO_S16, 1, sample_rate};
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(device, &spec, nullptr, nullptr);
    if (!stream) {
        LogWarn("microphone: no recording device: {}", SDL_GetError());
        LogSdlAudioDevices(true);
        return nullptr;
    }
    SDL_ResumeAudioStreamDevice(stream);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    LogInfo("microphone: recording at {} Hz (opened in {:.2f} s)", sample_rate, seconds);
    LogSdlAudioDevices(true);
    return stream;
#endif
}

void Microphone::Close() {
    if (pending_) {
        // the worker drops the stream it opens after this
        std::lock_guard lock(pending_->mutex);
        pending_->cancelled = true;
        if (pending_->stream) SDL_DestroyAudioStream(pending_->stream);
        pending_->stream = nullptr;
        cancelled_opens_.push_back(pending_);
    }
    pending_.reset();
    ReapOpenWorkers(false);
    SetMonitor(false);
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

bool Microphone::SetMonitor(bool enabled) {
    if (!enabled || !stream_) {
        if (monitor_) SDL_DestroyAudioStream(monitor_);
        monitor_ = nullptr;
        return !enabled;
    }
    if (monitor_) return true;
    SDL_AudioSpec spec{SDL_AUDIO_S16, 1, sample_rate_};
    monitor_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!monitor_) {
        LogWarn("microphone: monitoring unavailable: {}", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(monitor_);
    return true;
}

size_t Microphone::Read(std::vector<int16_t>& out) {
    out.clear();
    if (!stream_) {
        return 0;
    }
#ifdef __ANDROID__
    PumpAndroidPhoneMicrophone(stream_);
#endif
    const int available = SDL_GetAudioStreamAvailable(stream_);
    if (available <= 0) {
        return 0;
    }
    out.resize(static_cast<size_t>(available) / sizeof(int16_t));
    const int got = SDL_GetAudioStreamData(stream_, out.data(), static_cast<int>(out.size() * sizeof(int16_t)));
    out.resize(got > 0 ? static_cast<size_t>(got) / sizeof(int16_t) : 0);
    if (monitor_ && !out.empty()) {
        if (SDL_GetAudioStreamQueued(monitor_) > sample_rate_) SDL_ClearAudioStream(monitor_);
        SDL_PutAudioStreamData(monitor_, out.data(), static_cast<int>(out.size() * sizeof(int16_t)));
    }
    return out.size();
}

}
