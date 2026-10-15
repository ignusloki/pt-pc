#include "engine/platform/controller_speaker.h"

#include <algorithm>
#include <cmath>

namespace pt {

namespace {

constexpr uint16_t kSonyVendor = 0x054C;

bool ContainsNoCase(std::string_view text, std::string_view wanted) {
    if (wanted.empty() || wanted.size() > text.size()) return false;
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (size_t i = 0; i + wanted.size() <= text.size(); ++i) {
        size_t k = 0;
        while (k < wanted.size() && lower(text[i + k]) == lower(wanted[k])) ++k;
        if (k == wanted.size()) return true;
    }
    return false;
}

}

SonyPadAudio SonyPadAudioModel(uint16_t vendor, uint16_t product) {
    if (vendor != kSonyVendor) return SonyPadAudio::None;
    if (product == 0x05C4 || product == 0x09CC) return SonyPadAudio::DualShock4;
    if (product == 0x0CE6 || product == 0x0DF2) return SonyPadAudio::DualSense;
    return SonyPadAudio::None;
}

bool ControllerEndpointChannelsFit(SonyPadAudio model, int channels) {
    switch (model) {
        case SonyPadAudio::DualShock4: return channels == 1 || channels == 2;
        case SonyPadAudio::DualSense: return channels == 4;
        default: return false;
    }
}

std::optional<uint32_t> MatchUniqueControllerAudioEndpoint(std::span<const ControllerSpeakerEndpointName> endpoints,
                                                           std::span<const ControllerAudioEndpointName> sdl_devices,
                                                           SonyPadAudio model) {
    std::optional<uint32_t> match;
    for (const ControllerSpeakerEndpointName& endpoint : endpoints) {
        if (!ControllerEndpointChannelsFit(model, endpoint.channels)) {
            continue;
        }
        for (const ControllerAudioEndpointName& device : sdl_devices) {
            if (device.channels != endpoint.channels || device.name != endpoint.name) {
                continue;
            }
            if (match) {
                return std::nullopt;
            }
            match = device.id;
        }
    }
    return match;
}

bool IsSonyControllerEndpointName(SonyPadAudio model, std::string_view name) {
    switch (model) {
        case SonyPadAudio::DualShock4: return ContainsNoCase(name, "Wireless Controller") && !ContainsNoCase(name, "DualSense");
        case SonyPadAudio::DualSense: return ContainsNoCase(name, "DualSense");
        default: return false;
    }
}

std::optional<uint32_t> MatchControllerEndpointByName(std::span<const ControllerAudioEndpointName> sdl_devices, SonyPadAudio model) {
    std::optional<uint32_t> match;
    for (const ControllerAudioEndpointName& device : sdl_devices) {
        if (!IsSonyControllerEndpointName(model, device.name) || !ControllerEndpointChannelsFit(model, device.channels)) continue;
        if (match) return std::nullopt;
        match = device.id;
    }
    return match;
}

std::optional<uint32_t> MatchControllerEndpointOverride(std::span<const ControllerAudioEndpointName> sdl_devices, std::string_view wanted,
                                                        SonyPadAudio model) {
    std::optional<uint32_t> match;
    for (const ControllerAudioEndpointName& device : sdl_devices) {
        if (!ContainsNoCase(device.name, wanted) || !ControllerEndpointChannelsFit(model, device.channels)) continue;
        if (match) return std::nullopt;
        match = device.id;
    }
    return match;
}

bool SpreadControllerSpeakerMono(std::span<const float> mono, std::span<float> out, size_t frames, int channels) {
    if (frames == 0 || channels < 1 || channels > 2 || mono.size() != frames || out.size() != frames * static_cast<size_t>(channels)) {
        return false;
    }
    for (size_t frame = 0; frame < frames; ++frame) {
        for (int c = 0; c < channels; ++c) out[frame * channels + c] = mono[frame];
    }
    return true;
}

bool InterleaveDualSensePcm(std::span<const float> speaker_stereo, std::span<const float> actuator_stereo,
                            std::span<float> quad, size_t frames) {
    if (frames == 0 || frames > SIZE_MAX / 4 || speaker_stereo.size() != frames * 2 ||
        (!actuator_stereo.empty() && actuator_stereo.size() != frames * 2) || quad.size() != frames * 4) {
        return false;
    }
    for (size_t frame = 0; frame < frames; ++frame) {
        quad[frame * 4] = speaker_stereo[frame * 2];
        quad[frame * 4 + 1] = speaker_stereo[frame * 2 + 1];
        quad[frame * 4 + 2] = actuator_stereo.empty() ? 0.0f : actuator_stereo[frame * 2];
        quad[frame * 4 + 3] = actuator_stereo.empty() ? 0.0f : actuator_stereo[frame * 2 + 1];
    }
    return true;
}

bool DownmixControllerSpeakerMono(std::span<const float> stereo, std::span<float> mono, size_t frames) {
    if (frames == 0 || frames > SIZE_MAX / 2 || stereo.size() != frames * 2 || mono.size() != frames) {
        return false;
    }
    for (size_t frame = 0; frame < frames; ++frame) {
        mono[frame] = (stereo[frame * 2] + stereo[frame * 2 + 1]) * 0.5f;
    }
    return true;
}

bool ScaleControllerPcm(std::span<const float> authored_stereo, std::span<float> speaker_stereo,
                       std::span<float> haptics_source_stereo, size_t frames, bool speaker_enabled, bool haptics_enabled,
                       float speaker_gain, float haptics_gain) {
    if (frames == 0 || frames > SIZE_MAX / 2 || authored_stereo.size() != frames * 2 || speaker_stereo.size() != frames * 2 ||
        haptics_source_stereo.size() != frames * 2) {
        return false;
    }
    speaker_gain = std::isfinite(speaker_gain) ? std::clamp(speaker_gain, 0.0f, 2.0f) : 0.0f;
    haptics_gain = std::isfinite(haptics_gain) ? std::clamp(haptics_gain, 0.0f, 1.0f) : 0.0f;
    const float speaker_level = speaker_enabled ? speaker_gain : 0.0f;
    const float haptics_level = haptics_enabled ? haptics_gain : 0.0f;
    for (size_t i = 0; i < frames * 2; ++i) {
        speaker_stereo[i] = authored_stereo[i] * speaker_level;
        haptics_source_stereo[i] = authored_stereo[i] * haptics_level;
    }
    return true;
}

}
