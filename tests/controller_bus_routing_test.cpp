#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/audio/sound_package.h"
#include "engine/audio/sound_system.h"
#include "engine/audio/wwise_bank.h"
#include "engine/core/log.h"
#include "engine/fs/vfs.h"
#include "game/controller_speaker_events.h"

namespace {

constexpr uint32_t kBlock = 256;

struct Energy {
    double main = 0.0;
    double speaker = 0.0;
    double haptic = 0.0;
    uint32_t blocks = 0;
};

bool Contains(const auto& list, uint32_t id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

void Prepare(pt::audio::SoundSystem& sound, bool tap) {
    sound.SetRandomSeed(1);
    sound.RegisterObject(1, "Probe");
    sound.SetListener({0.0f, 1.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    sound.SetObjectTransform(1, {0.0f, 1.6f, 2.0f}, {0.0f, 0.0f, -1.0f});
    if (tap) {
        sound.SetControllerCaptureEvents(pt::game::kControllerSpeakerEvents);
        sound.SetControllerHapticEvents(pt::game::kControllerHapticOnlyEvents);
    } else {
        sound.SetControllerCaptureEvents({});
        sound.SetControllerHapticEvents({});
    }
}

void Render(pt::audio::SoundSystem& sound, float seconds, Energy& energy, std::vector<float>* main_out = nullptr) {
    std::vector<float> block(kBlock * 2);
    const uint32_t blocks = static_cast<uint32_t>(seconds * 48000.0f / kBlock);
    pt::audio::ControllerPcmBlock pad;
    for (uint32_t b = 0; b < blocks; ++b) {
        sound.Update(static_cast<float>(kBlock) / 48000.0f);
        sound.RenderOffline(block.data(), kBlock);
        for (float v : block) energy.main += static_cast<double>(v) * v;
        if (main_out) main_out->insert(main_out->end(), block.begin(), block.end());
        while (sound.TryReadControllerPcm(pad)) {
            ++energy.blocks;
            for (uint32_t i = 0; i < pad.frames * 2; ++i) {
                energy.speaker += static_cast<double>(pad.stereo[i]) * pad.stereo[i];
                energy.haptic += static_cast<double>(pad.haptic[i]) * pad.haptic[i];
            }
        }
    }
}

}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: pt_controller_bus_routing_test <game folder>\n");
        return 2;
    }
    pt::Vfs vfs;
    if (!vfs.Mount(argv[1])) {
        std::printf("FAIL: cannot mount %s\n", argv[1]);
        return 2;
    }
    pt::audio::SoundBankSet banks;
    std::string error;
    if (!banks.Load(vfs, pt::audio::SoundBankSet::DefaultPackages(), &error)) {
        std::printf("FAIL: banks: %s\n", error.c_str());
        return 2;
    }
    std::vector<uint32_t> events;
    for (const auto& bank : banks.Banks()) {
        for (const auto& object : bank->Objects()) {
            if (object->type == pt::audio::HircType::Event) events.push_back(object->id);
        }
    }
    std::sort(events.begin(), events.end());
    events.erase(std::unique(events.begin(), events.end()), events.end());

    pt::audio::SoundSystem sound;
    if (!sound.Init(vfs, false, false)) {
        std::printf("FAIL: sound init\n");
        return 2;
    }
    Prepare(sound, true);
    int failures = 0;
    int other = 0, other_heard = 0, speaker_heard = 0, haptic_heard = 0, speaker_listed = 0, haptic_listed = 0;
    uint32_t loudest_speaker = 0;
    double loudest_speaker_energy = 0.0;
    for (const uint32_t id : events) {
        Energy energy;
        sound.PostEventId(id, 1);
        Render(sound, 2.0f, energy);
        sound.StopAll(0.0f);
        Energy tail;
        Render(sound, 0.5f, tail);
        energy.speaker += tail.speaker;
        energy.haptic += tail.haptic;
        const bool speaker_event = Contains(pt::game::kControllerSpeakerEvents, id);
        const bool haptic_event = Contains(pt::game::kControllerHapticOnlyEvents, id);
        if (speaker_event) {
            ++speaker_listed;
            speaker_heard += energy.speaker > 0.0;
            if (energy.speaker > loudest_speaker_energy) {
                loudest_speaker_energy = energy.speaker;
                loudest_speaker = id;
            }
            std::printf("speaker event %08X: main %.4g, controller speaker %.4g, haptic %.4g\n", id, energy.main, energy.speaker, energy.haptic);
        } else if (haptic_event) {
            ++haptic_listed;
            haptic_heard += energy.haptic > 0.0;
            if (energy.speaker != 0.0) {
                std::printf("FAIL: the player's step %08X reached the controller speaker PCM (%.4g)\n", id, energy.speaker);
                ++failures;
            }
            std::printf("haptic-only event %08X: main %.4g, controller speaker %.4g, haptic %.4g\n", id, energy.main, energy.speaker, energy.haptic);
        } else {
            ++other;
            other_heard += energy.main > 0.0;
            if (energy.speaker != 0.0 || energy.haptic != 0.0) {
                std::printf("FAIL: event %08X of the main mix reached the controller stream (speaker %.4g, haptic %.4g)\n", id, energy.speaker,
                            energy.haptic);
                ++failures;
            }
        }
    }
    std::printf("%zu events: %d main-mix events (%d audible in the main mix) never reached the controller stream; %d of %d speaker events "
                "and %d of %d haptic-only events produced controller PCM when posted alone\n",
                events.size(), other, other_heard, speaker_heard, speaker_listed, haptic_heard, haptic_listed);
    if (other_heard < 50) {
        std::printf("FAIL: too few events were audible in the main mix for the check to mean anything\n");
        ++failures;
    }
    if (speaker_heard == 0) {
        std::printf("FAIL: no speaker event reached the controller stream\n");
        ++failures;
    }

    if (loudest_speaker != 0) {
        std::vector<float> with_tap, without_tap;
        for (const bool tap : {true, false}) {
            pt::audio::SoundSystem fresh;
            fresh.Init(vfs, false, false);
            Prepare(fresh, tap);
            fresh.PostEventId(loudest_speaker, 1);
            Energy energy;
            Render(fresh, 2.0f, energy, tap ? &with_tap : &without_tap);
            if (!tap && energy.blocks != 0) {
                std::printf("FAIL: controller PCM without selected events\n");
                ++failures;
            }
        }
        float max_diff = with_tap.size() == without_tap.size() ? 0.0f : 1.0f;
        for (size_t i = 0; i < std::min(with_tap.size(), without_tap.size()); ++i) max_diff = std::max(max_diff, std::abs(with_tap[i] - without_tap[i]));
        std::printf("%s the main mix of %08X is the same with and without the controller tap (max difference %g over %zu samples)\n",
                    max_diff == 0.0f ? "PASS" : "FAIL", loudest_speaker, max_diff, with_tap.size());
        failures += max_diff != 0.0f;
    }
    std::printf("controller bus routing: %d failures\n", failures);
    return failures ? 1 : 0;
}
