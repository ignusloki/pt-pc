#include "engine/platform/dualshock_audio.h"

#include <SDL3/SDL.h>
#include <sbc.h>
#include <algorithm>
#include <climits>

namespace pt {

void SealDs4BluetoothReport(std::span<uint8_t> report) {
    if (report.size() < 4) return;
    uint32_t crc = 0xFFFFFFFFu;
    auto feed = [&](uint8_t byte) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    };
    feed(0xA2);
    for (size_t i = 0; i < report.size() - 4; ++i) feed(report[i]);
    crc = ~crc;
    for (size_t i = 0; i < 4; ++i) report[report.size() - 4 + i] = static_cast<uint8_t>(crc >> (i * 8));
}

std::array<uint8_t, 78> BuildDs4SpeakerControl(bool enabled) {
    std::array<uint8_t, 78> report{};
    report[0] = 0x11;
    report[1] = 0xC4;
    report[2] = enabled ? 0xA0 : 0x00;
    report[3] = 0x80;
    report[24] = enabled ? 0x4F : 0x00;
    SealDs4BluetoothReport(report);
    return report;
}

struct DualShockAudioEncoder::State {
    State() {
        const SDL_AudioSpec input{SDL_AUDIO_F32, 2, 48000};
        const SDL_AudioSpec output{SDL_AUDIO_S16, 2, 32000};
        stream = SDL_CreateAudioStream(&input, &output);
    }
    ~State() { if (stream) SDL_DestroyAudioStream(stream); }
    SDL_AudioStream* stream = nullptr;
    sbc_t encoder{};
    const sbc_frame format{false, SBC_FREQ_32K, SBC_MODE_DUAL_CHANNEL, SBC_BAM_LOUDNESS, 16, 8, 25};
    std::array<uint8_t, kDs4AudioReportSize> report{};
    uint16_t frame_number = 0;
    unsigned frames = 0;
};

DualShockAudioEncoder::DualShockAudioEncoder() : state_(std::make_unique<State>()) {}
DualShockAudioEncoder::~DualShockAudioEncoder() = default;
bool DualShockAudioEncoder::Ready() const { return state_->stream != nullptr; }
void DualShockAudioEncoder::Clear() {
    auto& s = *state_;
    if (s.stream) SDL_ClearAudioStream(s.stream);
    sbc_reset(&s.encoder);
    s.frames = 0;
}

bool DualShockAudioEncoder::Push(std::span<const float> stereo, const std::function<bool(std::span<const uint8_t>)>& submit) {
    auto& s = *state_;
    if (!s.stream || stereo.empty() || stereo.size() % 2 || stereo.size_bytes() > INT_MAX || !submit) return false;
    if (!SDL_PutAudioStreamData(s.stream, stereo.data(), static_cast<int>(stereo.size_bytes()))) return false;
    std::array<int16_t, 256> pcm{};
    while (SDL_GetAudioStreamAvailable(s.stream) >= static_cast<int>(sizeof(pcm))) {
        if (SDL_GetAudioStreamData(s.stream, pcm.data(), sizeof(pcm)) != sizeof(pcm)) return false;
        if (s.frames == 0) {
            s.report.fill(0);
            s.report[0] = 0x17;
            s.report[1] = 0x40;
            s.report[2] = 0xA0;
            s.report[3] = static_cast<uint8_t>(s.frame_number);
            s.report[4] = static_cast<uint8_t>(s.frame_number >> 8);
            s.report[5] = 0x02;
        }
        if (sbc_encode(&s.encoder, pcm.data(), 2, pcm.data() + 1, 2, &s.format,
                       s.report.data() + 6 + s.frames * kDs4SbcFrameSize, kDs4SbcFrameSize) != 0) return false;
        ++s.frame_number;
        if (++s.frames == 4) {
            SealDs4BluetoothReport(s.report);
            s.frames = 0;
            if (!submit(s.report)) return false;
        }
    }
    return true;
}

}
