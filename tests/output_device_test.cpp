#include "engine/audio/output_device.h"
#include "engine/platform/controller_speaker.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

using pt::audio::ControllerEndpoint;
using pt::audio::OutputPick;
using pt::audio::PlaybackEndpoint;

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
        failures += !ok;
    };

    const std::vector<PlaybackEndpoint> reporter{
        {"耳机 (Realtek(R) Audio)", "{0.0.0.00000000}.{11111111-1111-1111-1111-111111111111}", 2, false},
        {"扬声器 (DualSense Wireless Controller)", "{0.0.0.00000000}.{22222222-2222-2222-2222-222222222222}", 4, true},
        {"LG TV (NVIDIA High Definition Audio)", "{0.0.0.00000000}.{33333333-3333-3333-3333-333333333333}", 2, false},
    };
    {
        const auto selection = pt::audio::SelectPlaybackEndpoint(reporter, "");
        check(selection.pick == OutputPick::SystemDefault && !selection.endpoint, "an empty setting opens the system default (SDL's default device)");
        check(selection.system_default == 1u, "the listed system default is the DualSense, as Windows set it");
        check(pt::audio::ControllerEndpointKind(reporter[*selection.system_default].name) == ControllerEndpoint::DualSense,
              "a system default on the DualSense is recognised as the controller's own endpoint (logged and shown on the row)");
    }
    {
        const auto selection = pt::audio::SelectPlaybackEndpoint(reporter, "耳机 (Realtek(R) Audio)");
        check(selection.pick == OutputPick::Chosen && selection.endpoint == 0u,
              "a chosen device opens that device, not the DualSense default");
        check(pt::audio::ControllerEndpointKind(reporter[*selection.endpoint].name) == ControllerEndpoint::None,
              "the chosen headphones are not a controller endpoint");
    }
    {
        const auto selection = pt::audio::SelectPlaybackEndpoint(reporter, "扬声器 (DualSense Wireless Controller)");
        check(selection.pick == OutputPick::Chosen && selection.endpoint == 1u, "the DualSense stays selectable as an output on purpose");
    }
    {
        const auto selection = pt::audio::SelectPlaybackEndpoint(reporter, "USB Headset (Arctis 7)");
        check(selection.pick == OutputPick::ChosenMissing && !selection.endpoint && selection.system_default == 1u,
              "a chosen device that is not connected falls back to the system default and keeps the choice");
    }
    {
        const auto selection = pt::audio::SelectPlaybackEndpoint(reporter, "耳机");
        check(selection.pick == OutputPick::ChosenMissing, "a partial name is not a match (the setting keeps the full listed name)");
    }
    {
        const std::vector<PlaybackEndpoint> none;
        const auto selection = pt::audio::SelectPlaybackEndpoint(none, "");
        check(selection.pick == OutputPick::SystemDefault && !selection.endpoint && !selection.system_default,
              "no listed device: the system default still opens (SDL decides)");
    }
    {
        const std::vector<PlaybackEndpoint> twins{{"Speakers (USB Audio)", "a", 2, false}, {"Speakers (2- USB Audio)", "b", 2, true}};
        const auto selection = pt::audio::SelectPlaybackEndpoint(twins, "Speakers (2- USB Audio)");
        check(selection.endpoint == 1u, "Windows' numbered twin devices are told apart by their full names");
    }

    check(pt::audio::ControllerEndpointKind("Speakers (DualSense Wireless Controller)") == ControllerEndpoint::DualSense &&
              pt::audio::ControllerEndpointKind("扬声器 (DualSense Wireless Controller)") == ControllerEndpoint::DualSense &&
              pt::audio::ControllerEndpointKind("DualSense Wireless Controller Analog Surround 4.0") == ControllerEndpoint::DualSense,
          "DualSense endpoints in English, Chinese Windows and PipeWire names");
    check(pt::audio::ControllerEndpointKind("Headset Earphone (Wireless Controller)") == ControllerEndpoint::DualShock4 &&
              pt::audio::ControllerEndpointKind("Wireless Controller Analog Stereo") == ControllerEndpoint::DualShock4,
          "DualShock 4 headset endpoints");
    check(pt::audio::ControllerEndpointKind("Speakers (H2015 Pro)") == ControllerEndpoint::None &&
              pt::audio::ControllerEndpointKind("耳机 (Realtek(R) Audio)") == ControllerEndpoint::None &&
              pt::audio::ControllerEndpointKind("") == ControllerEndpoint::None,
          "ordinary devices are not controllers");
    {
        const std::string text = pt::audio::DescribePlaybackEndpoints(reporter);
        check(text.find("'扬声器 (DualSense Wireless Controller)' (4 ch, id {0.0.0.00000000}.{22222222-2222-2222-2222-222222222222}, "
                        "system default, DualSense controller)") != std::string::npos &&
                  text.find("'耳机 (Realtek(R) Audio)' (2 ch, id {0.0.0.00000000}.{11111111-1111-1111-1111-111111111111})") != std::string::npos,
              "the log line gives each device's name, channels, endpoint id and flags");
    }

    check(!pt::DualSenseSpeakerPathWanted(false, false) && !pt::DualSenseSpeakerPathWanted(false, true),
          "speaker setting off: the pad stays on its headphone jack");
    check(pt::DualSenseSpeakerPathWanted(true, false), "speaker on, main mix on the headphones: the speaker plays the speaker sounds");
    check(!pt::DualSenseSpeakerPathWanted(true, true),
          "speaker on, main mix on the pad (the 1.0.8 report): the speaker path is not selected, it would play the whole game");
    check(pt::SameAudioEndpoint("扬声器 (DualSense Wireless Controller)", "扬声器 (DualSense Wireless Controller)") &&
              !pt::SameAudioEndpoint("耳机 (Realtek(R) Audio)", "扬声器 (DualSense Wireless Controller)") &&
              !pt::SameAudioEndpoint("", ""),
          "the main output and the pad's endpoint are compared by SDL's device name (no device is no match)");
    {
        const auto speaker = pt::BuildDualSenseAudioPathReport(true);
        const auto jack = pt::BuildDualSenseAudioPathReport(false);
        bool rest_zero = true;
        for (size_t i = 0; i < speaker.size(); ++i) {
            if (i == 0 || i == 1 || i == 6 || i == 8) continue;
            rest_zero = rest_zero && speaker[i] == 0 && jack[i] == 0;
        }
        check(speaker[0] == 0x02 && speaker[1] == 0xA0 && speaker[6] == 0x64 && speaker[8] == 0x30,
              "speaker report: audio control and speaker volume flags, volume 0x64, output path 3");
        check(jack[0] == 0x02 && jack[1] == 0x80 && jack[6] == 0x00 && jack[8] == 0x00,
              "jack report: only the audio control flag, output path 0 (the pad's own default)");
        check(rest_zero && speaker[2] == 0 && jack[2] == 0, "no other field of the report is flagged or written (rumble, lights, mic stay SDL's)");
    }
    {
        const std::vector<PlaybackEndpoint> ds4_usb{
            {"Speakers (Realtek(R) Audio)", "{0.0.0.00000000}.{aaaaaaaa-0000-0000-0000-000000000000}", 2, false},
            {"Headset Earphone (Wireless Controller)", "{0.0.0.00000000}.{bbbbbbbb-0000-0000-0000-000000000000}", 2, true},
        };
        const auto by_default = pt::audio::SelectPlaybackEndpoint(ds4_usb, "");
        check(by_default.pick == OutputPick::SystemDefault && by_default.system_default == 1u &&
                  pt::audio::ControllerEndpointKind(ds4_usb[1].name) == ControllerEndpoint::DualShock4,
              "DS4 over USB as the system default is recognised as the DS4's own endpoint (the same row label and log warning)");
        const auto chosen = pt::audio::SelectPlaybackEndpoint(ds4_usb, "Speakers (Realtek(R) Audio)");
        check(chosen.pick == OutputPick::Chosen && chosen.endpoint == 0u, "with a DS4 as the default, chosen speakers still open");
        const std::vector<PlaybackEndpoint> ds4_bt{{"Speakers (Realtek(R) Audio)", "{0.0.0.00000000}.{aaaaaaaa-0000-0000-0000-000000000000}", 2, true}};
        const auto bt = pt::audio::SelectPlaybackEndpoint(ds4_bt, "");
        check(bt.system_default == 0u && pt::audio::ControllerEndpointKind(ds4_bt[0].name) == ControllerEndpoint::None,
              "DS4 over Bluetooth: no controller endpoint in the list, the main mix stays on the PC's device");
    }
    check(pt::audio::ControllerEndpointKind("Headset Earphone (2- Wireless Controller)") == ControllerEndpoint::DualShock4 &&
              pt::audio::ControllerEndpointKind("耳机 (Wireless Controller)") == ControllerEndpoint::DualShock4 &&
              pt::audio::ControllerEndpointKind("扬声器 (DualSense Wireless Controller)") != ControllerEndpoint::DualShock4,
          "DS4 endpoint names (numbered twin, Chinese Windows) and a DualSense never taken for a DS4");
    check(pt::audio::ControllerEndpointKind("Headset Earphone (Xbox Wireless Controller)") == ControllerEndpoint::None &&
              pt::audio::ControllerEndpointKind("XBOX Wireless Controller") == ControllerEndpoint::None,
          "an Xbox pad's endpoint is not labelled as a Sony pad");
    using pt::ControllerPcmRoute;
    check(pt::ControllerSpeakerPcmAllowed(ControllerPcmRoute::DualShock4BluetoothSpeaker, true, false) &&
              !pt::ControllerSpeakerPcmAllowed(ControllerPcmRoute::DualShock4BluetoothSpeaker, false, true),
          "DS4 Bluetooth speaker: plays as requested, whatever any DualSense path state");
    check(pt::ControllerSpeakerPcmAllowed(ControllerPcmRoute::DualShock4Mono, true, false) &&
              !pt::ControllerSpeakerPcmAllowed(ControllerPcmRoute::DualShock4Mono, false, false),
          "DS4 USB endpoint route (test stand-in only): as requested");
    check(pt::ControllerSpeakerPcmAllowed(ControllerPcmRoute::DualSenseBluetoothHaptics, true, false),
          "DualSense Bluetooth haptics route: the request passes unchanged (it has no speaker)");
    check(!pt::ControllerSpeakerPcmAllowed(ControllerPcmRoute::DualSenseQuad, true, false) &&
              pt::ControllerSpeakerPcmAllowed(ControllerPcmRoute::DualSenseQuad, true, true) &&
              !pt::ControllerSpeakerPcmAllowed(ControllerPcmRoute::DualSenseQuad, false, true),
          "DualSense USB: the speaker PCM only while its speaker path is selected");
    check(pt::ControllerSpeakerAudioEnabled(pt::SonyPadAudio::DualShock4, false) &&
              !pt::ControllerSpeakerAudioEnabled(pt::SonyPadAudio::DualSense, false),
          "the DS4 speaker still starts by itself; the DualSense's still follows its setting");
    {
        const std::array hids{pt::ControllerSpeakerHidPath{"usb-ds4", false, true}, pt::ControllerSpeakerHidPath{"bt-ds4", true, false}};
        check(pt::MatchDs4SpeakerHidPath("bt-ds4", hids) == 1u && !pt::MatchDs4SpeakerHidPath("usb-ds4", hids),
              "DS4 Bluetooth speaker matching: the Bluetooth pad's own path, never from USB");
    }
    std::printf("%s\n", failures ? "output device test FAILED" : "output device test passed");
    return failures ? 1 : 0;
}
