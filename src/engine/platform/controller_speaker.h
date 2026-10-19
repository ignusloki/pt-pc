#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine/audio/controller_haptics.h"
#include "engine/audio/controller_pcm_capture.h"

struct SDL_AudioStream;
struct SDL_hid_device;
struct SDL_Gamepad;

namespace pt {

struct ControllerAudioEndpointName {
    uint32_t id = 0;
    std::string_view name;
    int channels = 0;
};

struct ControllerSpeakerEndpointName {
    std::string_view name;
    int channels = 0;
};

enum class SonyPadAudio : uint8_t { None, DualShock4, DualSense };
SonyPadAudio SonyPadAudioModel(uint16_t vendor, uint16_t product);

bool ControllerEndpointChannelsFit(SonyPadAudio model, int channels);

std::optional<uint32_t> MatchUniqueControllerAudioEndpoint(std::span<const ControllerSpeakerEndpointName> endpoints,
                                                            std::span<const ControllerAudioEndpointName> sdl_devices,
                                                            SonyPadAudio model);

bool IsSonyControllerEndpointName(SonyPadAudio model, std::string_view name);
std::optional<uint32_t> MatchControllerEndpointByName(std::span<const ControllerAudioEndpointName> sdl_devices, SonyPadAudio model);

std::optional<uint32_t> MatchControllerEndpointOverride(std::span<const ControllerAudioEndpointName> sdl_devices, std::string_view wanted,
                                                        SonyPadAudio model);

bool SpreadControllerSpeakerMono(std::span<const float> mono, std::span<float> out, size_t frames, int channels);

bool InterleaveDualSensePcm(std::span<const float> speaker_stereo, std::span<const float> actuator_stereo,
                            std::span<float> quad, size_t frames);
bool DownmixControllerSpeakerMono(std::span<const float> stereo, std::span<float> mono, size_t frames);
bool ScaleControllerPcm(std::span<const float> authored_stereo, std::span<float> speaker_stereo,
                        std::span<float> haptics_source_stereo, size_t frames, bool speaker_enabled, bool haptics_enabled,
                        float speaker_gain, float haptics_gain);

constexpr size_t kDualSenseBtHapticsReportSize = 142;
constexpr size_t kDualSenseBtHapticsFrames = 32;
constexpr int kDualSenseBtHapticsRate = 3000;
void BuildDualSenseBtHapticsReport(std::span<const int8_t> stereo_s8, uint8_t counter, std::span<uint8_t> report);

enum class ControllerPcmRoute : uint8_t { None, DualShock4Mono, DualSenseQuad, DualSenseBluetoothHaptics };

class ControllerSpeakerOutput {
public:
    ControllerSpeakerOutput();
    ~ControllerSpeakerOutput();
    ControllerSpeakerOutput(const ControllerSpeakerOutput&) = delete;
    ControllerSpeakerOutput& operator=(const ControllerSpeakerOutput&) = delete;

    bool OpenForGamepad(SDL_Gamepad* gamepad, std::string* reason = nullptr);
    void Close();
    void ClearPending();
    bool IsOpen() const;
    ControllerPcmRoute Route() const { return route_; }
    const std::string& DeviceName() const { return device_name_; }

    bool WriteMono(const float* speaker_mono, uint32_t frames);
    bool WriteDualSense(const float* speaker_stereo, const float* actuator_stereo, uint32_t frames);
    bool WriteCapturedBlock(const audio::ControllerPcmBlock& block, bool speaker_enabled, bool dualsense_haptics_enabled,
                            float speaker_gain = 1.0f, float haptics_gain = 1.0f);

private:
    bool WriteBluetoothHaptics(const float* actuator_stereo, uint32_t frames);

    SDL_AudioStream* stream_ = nullptr;
    SDL_hid_device* hid_ = nullptr;
    struct HidWriter;
    std::unique_ptr<HidWriter> hid_writer_;
    std::array<float, 2> bt_sum_{};
    uint32_t bt_summed_ = 0;
    std::array<int8_t, kDualSenseBtHapticsFrames * 2> bt_frames_{};
    uint32_t bt_frame_count_ = 0;
    uint8_t bt_counter_ = 0;
    ControllerPcmRoute route_ = ControllerPcmRoute::None;
    int ds4_channels_ = 1;
    std::string device_name_;
    bool owns_audio_subsystem_ = false;
    audio::ControllerHapticFilter haptic_filter_;
    std::array<float, audio::ControllerPcmBlock::kMaxFrames> mono_scratch_{};
    std::array<float, audio::ControllerPcmBlock::kMaxFrames * 2> ds4_scratch_{};
    std::array<float, audio::ControllerPcmBlock::kMaxFrames * 2> actuator_scratch_{};
    std::array<float, audio::ControllerPcmBlock::kMaxFrames * 2> speaker_scratch_{};
    std::array<float, audio::ControllerPcmBlock::kMaxFrames * 2> haptics_source_scratch_{};
};

}
