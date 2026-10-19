#include "engine/platform/controller_speaker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
        failures += !ok;
    };

    const std::array endpoints{pt::ControllerSpeakerEndpointName{"DualSense USB", 4}};
    const std::array devices{pt::ControllerAudioEndpointName{10, "Speakers", 2},
                             pt::ControllerAudioEndpointName{11, "DualSense USB", 2},
                             pt::ControllerAudioEndpointName{12, "DualSense USB", 4}};
    {
        std::array<int8_t, 64> samples{};
        for (int i = 0; i < 64; ++i) samples[i] = static_cast<int8_t>(static_cast<uint8_t>(i * 3 - 90));
        std::array<uint8_t, pt::kDualSenseBtHapticsReportSize> report{};
        pt::BuildDualSenseBtHapticsReport(samples, 5, report);
        const uint32_t crc = report[138] | (report[139] << 8) | (report[140] << 16) | (static_cast<uint32_t>(report[141]) << 24);
        check(report[0] == 0x32 && report[2] == 0x91 && report[3] == 7 && report[4] == 0xFE && report[9] == 0xFF && report[10] == 5 &&
                  report[11] == 0x92 && report[12] == 64 && report[13] == static_cast<uint8_t>(-90) && crc == 0x6708FAF3u,
              "DualSense Bluetooth haptics report layout and CRC");
    }
    check(pt::MatchUniqueControllerAudioEndpoint(endpoints, devices, pt::SonyPadAudio::DualSense) == 12,
          "exact endpoint name and channel count select the SDL device associated by native identity");

    const std::array unrelated{pt::ControllerSpeakerEndpointName{"Speakers", 4}};
    check(!pt::MatchUniqueControllerAudioEndpoint(unrelated, devices, pt::SonyPadAudio::DualSense), "a name without a native identity match is rejected");

    const std::array ambiguous_devices{pt::ControllerAudioEndpointName{12, "DualSense USB", 4},
                                       pt::ControllerAudioEndpointName{13, "DualSense USB", 4}};
    check(!pt::MatchUniqueControllerAudioEndpoint(endpoints, ambiguous_devices, pt::SonyPadAudio::DualSense), "duplicate SDL names fail closed");

    const std::array ambiguous_endpoints{pt::ControllerSpeakerEndpointName{"DualSense USB", 4},
                                         pt::ControllerSpeakerEndpointName{"DualSense USB", 4}};
    check(!pt::MatchUniqueControllerAudioEndpoint(ambiguous_endpoints, devices, pt::SonyPadAudio::DualSense), "multiple endpoints in one container fail closed");

    check(!pt::MatchUniqueControllerAudioEndpoint(endpoints, devices, pt::SonyPadAudio::DualShock4), "incorrect channel layout is rejected");

    const std::array ds4_endpoints{pt::ControllerSpeakerEndpointName{"Headset Earphone (Wireless Controller)", 2}};
    const std::array reporter_devices{pt::ControllerAudioEndpointName{20, "Headset Earphone (Wireless Controller)", 2},
                                      pt::ControllerAudioEndpointName{21, "Digital Audio (S/PDIF) (High Definition Audio Device)", 2},
                                      pt::ControllerAudioEndpointName{22, "Corsair Headset (CORSAIR HS70 Pro Wireless Gaming Headset)", 2},
                                      pt::ControllerAudioEndpointName{23, "Focusrite  (2- Focusrite USB Audio)", 2},
                                      pt::ControllerAudioEndpointName{24, "Speakers (High Definition Audio Device)", 2}};
    check(pt::MatchUniqueControllerAudioEndpoint(ds4_endpoints, reporter_devices, pt::SonyPadAudio::DualShock4) == 20,
          "a DualShock 4's stereo endpoint in its container is taken");
    check(!pt::MatchUniqueControllerAudioEndpoint(ds4_endpoints, reporter_devices, pt::SonyPadAudio::DualSense),
          "a stereo endpoint is not a DualSense's");
    check(pt::ControllerEndpointChannelsFit(pt::SonyPadAudio::DualShock4, 1) && pt::ControllerEndpointChannelsFit(pt::SonyPadAudio::DualShock4, 2) &&
              !pt::ControllerEndpointChannelsFit(pt::SonyPadAudio::DualShock4, 4) && pt::ControllerEndpointChannelsFit(pt::SonyPadAudio::DualSense, 4) &&
              !pt::ControllerEndpointChannelsFit(pt::SonyPadAudio::DualSense, 2) && !pt::ControllerEndpointChannelsFit(pt::SonyPadAudio::None, 2),
          "channel counts that fit each pad");
    check(pt::SonyPadAudioModel(0x054C, 0x05C4) == pt::SonyPadAudio::DualShock4 && pt::SonyPadAudioModel(0x054C, 0x09CC) == pt::SonyPadAudio::DualShock4 &&
              pt::SonyPadAudioModel(0x054C, 0x0CE6) == pt::SonyPadAudio::DualSense && pt::SonyPadAudioModel(0x054C, 0x0DF2) == pt::SonyPadAudio::DualSense &&
              pt::SonyPadAudioModel(0x045E, 0x09CC) == pt::SonyPadAudio::None && pt::SonyPadAudioModel(0x054C, 0x0BA0) == pt::SonyPadAudio::None,
          "Sony pad models by vendor and product");

    check(pt::MatchControllerEndpointByName(reporter_devices, pt::SonyPadAudio::DualShock4) == 20,
          "the DS4's endpoint is found by name when its container has none");
    check(!pt::MatchControllerEndpointByName(reporter_devices, pt::SonyPadAudio::DualSense), "no DualSense endpoint among DS4 devices");
    const std::array mixed_devices{pt::ControllerAudioEndpointName{30, "Speakers (DualSense Wireless Controller)", 4},
                                   pt::ControllerAudioEndpointName{31, "Wireless Controller Analog Stereo", 2},
                                   pt::ControllerAudioEndpointName{32, "Speakers (Realtek(R) Audio)", 2}};
    check(pt::MatchControllerEndpointByName(mixed_devices, pt::SonyPadAudio::DualShock4) == 31 &&
              pt::MatchControllerEndpointByName(mixed_devices, pt::SonyPadAudio::DualSense) == 30,
          "a DualSense endpoint is never a DS4's and the other way round (Windows and PipeWire names)");
    const std::array two_ds4{pt::ControllerAudioEndpointName{40, "Headset Earphone (Wireless Controller)", 2},
                             pt::ControllerAudioEndpointName{41, "Headset Earphone (2- Wireless Controller)", 2}};
    check(!pt::MatchControllerEndpointByName(two_ds4, pt::SonyPadAudio::DualShock4), "two DS4 endpoints by name fail closed");
    const std::array ds4_quad{pt::ControllerAudioEndpointName{50, "Wireless Controller", 4}};
    check(!pt::MatchControllerEndpointByName(ds4_quad, pt::SonyPadAudio::DualShock4), "a DS4 name with a wrong channel count is rejected");
    const std::array no_pad{pt::ControllerAudioEndpointName{60, "Speakers (H2015 Pro)", 2}};
    check(!pt::MatchControllerEndpointByName(no_pad, pt::SonyPadAudio::DualShock4) && !pt::MatchControllerEndpointByName(no_pad, pt::SonyPadAudio::DualSense),
          "without a pad endpoint nothing is opened (the main mix plays as before)");

    check(pt::MatchControllerEndpointOverride(reporter_devices, "focusrite", pt::SonyPadAudio::DualShock4) == 23,
          "the override takes the one device whose name contains it, in any case");
    check(!pt::MatchControllerEndpointOverride(reporter_devices, "Audio Device", pt::SonyPadAudio::DualShock4), "an ambiguous override fails closed");
    check(!pt::MatchControllerEndpointOverride(reporter_devices, "focusrite", pt::SonyPadAudio::DualSense),
          "the override still needs a channel count that fits the pad");
    check(!pt::MatchControllerEndpointOverride(reporter_devices, "", pt::SonyPadAudio::DualShock4), "an empty override matches nothing");

    const std::array ds4_mono{0.25f, -0.5f};
    std::array<float, 4> ds4_pair{};
    check(pt::SpreadControllerSpeakerMono(ds4_mono, ds4_pair, 2, 2) && ds4_pair == std::array{0.25f, 0.25f, -0.5f, -0.5f},
          "the DS4's mono speaker signal goes to both channels of its pair");
    std::array<float, 2> ds4_single{};
    check(pt::SpreadControllerSpeakerMono(ds4_mono, ds4_single, 2, 1) && ds4_single == std::array{0.25f, -0.5f}, "a mono DS4 endpoint takes it as is");
    check(!pt::SpreadControllerSpeakerMono(ds4_mono, ds4_pair, 2, 4) && !pt::SpreadControllerSpeakerMono(ds4_mono, ds4_single, 2, 2),
          "spreading rejects bad channel counts and buffer sizes");

    const std::array speaker{0.1f, 0.2f, 0.3f, 0.4f};
    const std::array actuators{0.5f, 0.6f, 0.7f, 0.8f};
    std::array<float, 8> quad{};
    check(pt::InterleaveDualSensePcm(speaker, actuators, quad, 2) && quad == std::array{0.1f, 0.2f, 0.5f, 0.6f,
                                                                                       0.3f, 0.4f, 0.7f, 0.8f},
          "speaker and actuator PCM occupy separate DualSense channel pairs");
    std::array<float, 8> speaker_only{};
    check(pt::InterleaveDualSensePcm(speaker, {}, speaker_only, 2) &&
              speaker_only == std::array{0.1f, 0.2f, 0.0f, 0.0f, 0.3f, 0.4f, 0.0f, 0.0f},
          "missing actuator PCM mutes only the haptic channel pair");
    check(!pt::InterleaveDualSensePcm(speaker, actuators, quad, 1), "mismatched PCM frame counts are rejected");
    std::array<float, 2> mono{};
    check(pt::DownmixControllerSpeakerMono(speaker, mono, 2) && std::abs(mono[0] - 0.15f) < 1e-6f && std::abs(mono[1] - 0.35f) < 1e-6f,
          "DS4 speaker downmix averages authored stereo channels per frame");
    check(!pt::DownmixControllerSpeakerMono(speaker, std::span<float>(mono).first(1), 2),
          "DS4 downmix rejects an undersized output buffer");
    const std::array authored{0.4f, -0.3f, 0.2f, 0.1f};
    std::array<float, 4> speaker_scaled{};
    std::array<float, 4> haptics_source{};
    check(pt::ScaleControllerPcm(authored, speaker_scaled, haptics_source, 2, true, true, 0.2f, 0.8f) &&
              std::abs(speaker_scaled[0] - 0.08f) < 1e-6f && std::abs(speaker_scaled[1] + 0.06f) < 1e-6f &&
              std::abs(haptics_source[0] - 0.32f) < 1e-6f && std::abs(haptics_source[1] + 0.24f) < 1e-6f,
          "independent controller speaker and haptics gains scale their own channels");
    std::array<float, 4> speaker_louder{};
    std::array<float, 4> haptics_unchanged{};
    check(pt::ScaleControllerPcm(authored, speaker_louder, haptics_unchanged, 2, true, true, 1.0f, 0.8f) &&
              speaker_louder[0] == authored[0] && haptics_unchanged == haptics_source,
          "changing speaker volume leaves the actuator source unchanged");
    check(pt::ScaleControllerPcm(authored, speaker_scaled, haptics_source, 2, false, true, 1.0f, 0.8f) &&
              std::all_of(speaker_scaled.begin(), speaker_scaled.end(), [](float x) { return x == 0.0f; }) &&
              haptics_source == haptics_unchanged,
          "haptics-only mode silences DualSense speaker channels");
    check(pt::ScaleControllerPcm(authored, speaker_scaled, haptics_source, 2, true, false, 0.5f, 1.0f) &&
              std::all_of(haptics_source.begin(), haptics_source.end(), [](float x) { return x == 0.0f; }),
          "speaker-only mode silences DualSense actuator channels");
    check(!pt::ScaleControllerPcm(authored, std::span<float>(speaker_scaled).first(2), haptics_source, 2, true, true, 1.0f, 1.0f),
          "channel scaling rejects mismatched output spans");
    return failures ? 1 : 0;
}
