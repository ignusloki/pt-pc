#include "engine/platform/dualshock_audio.h"
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    unsigned failed = 0;
    auto check = [&](bool ok, const char* name) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", name); failed += !ok; };
    const auto crc_ok = [](std::span<const uint8_t> r) {
        const uint8_t prefix = 0xA2;
        auto crc = crc32(0, &prefix, 1);
        crc = crc32(crc, r.data(), static_cast<uInt>(r.size() - 4));
        uint32_t stored = 0;
        for (unsigned i = 0; i < 4; ++i) stored |= uint32_t(r[r.size() - 4 + i]) << (8 * i);
        return crc == stored;
    };
    const auto control = pt::BuildDs4SpeakerControl(true);
    const auto mute = pt::BuildDs4SpeakerControl(false);
    check(control[0] == 0x11 && control[1] == 0xC4 && control[2] == 0xA0 && control[3] == 0x80 && control[24] == 0x4F &&
              std::all_of(control.begin() + 4, control.begin() + 24, [](auto c) { return c == 0; }) && crc_ok(control),
          "speaker arm flags preserve rumble, LED, microphone and headphone state; independent CRC");
    check(mute[0] == 0x11 && mute[2] == 0 && mute[3] == 0x80 && mute[24] == 0 && crc_ok(mute), "speaker close mutes only the internal speaker");
    std::vector<float> pcm(48000 * 2);
    for (size_t i = 0; i < pcm.size() / 2; ++i) pcm[i * 2] = pcm[i * 2 + 1] = 0.25f * std::sin(float(i) * 440.0f * 6.28318530718f / 48000.0f);
    using Packet = std::array<uint8_t, pt::kDs4AudioReportSize>;
    auto encode = [&](bool split) {
        pt::DualShockAudioEncoder encoder;
        std::vector<Packet> packets;
        check(encoder.Ready(), "SBC source resampler created without opening an audio device");
        const auto submit = [&](std::span<const uint8_t> r) {
            Packet p{}; std::copy(r.begin(), r.end(), p.begin()); packets.push_back(p); return true;
        };
        bool ok = true;
        for (size_t offset = 0; offset < pcm.size();) {
            const size_t size = std::min(pcm.size() - offset, split ? size_t(512) : pcm.size());
            ok &= encoder.Push(std::span(pcm).subspan(offset, size), submit);
            offset += size;
        }
        check(ok && packets.size() >= 60 && packets.size() <= 62, "one second of game PCM yields the expected 32 kHz audio duration");
        return packets;
    };
    const auto packets = encode(true), whole = encode(false);
    check(packets == whole, "256-frame game blocks and a continuous stream produce identical SBC packets");
    bool layout = !packets.empty();
    for (size_t i = 0; i < packets.size(); ++i) {
        const auto& r = packets[i];
        layout &= r[0] == 0x17 && r[1] == 0x40 && r[2] == 0xA0 && r[5] == 0x02 &&
                  (uint16_t(r[3]) | (uint16_t(r[4]) << 8)) == i * 4 && crc_ok(r);
        for (size_t f = 0; f < 4; ++f) layout &= r[6 + f * 112] == 0x9C && r[7 + f * 112] == 0x75 && r[8 + f * 112] == 25;
        for (size_t p = 454; p < 458; ++p) layout &= r[p] == 0;
    }
    check(layout, "native DS4 SBC header, speaker target, frame counters, padding and independent Bluetooth CRC");
    pt::DualShockAudioEncoder encoder;
    unsigned count = 0;
    auto submit = [&](std::span<const uint8_t>) { ++count; return true; };
    check(!encoder.Push(std::span(pcm).first(3), submit) && !encoder.Push({}, submit), "malformed stereo blocks rejected");
    check(encoder.Push(std::span(pcm).first(512), submit) && count == 0, "partial packets are buffered");
    encoder.Clear();
    std::vector<float> silence(48000 * 2);
    auto cleared = [&](std::span<const uint8_t> r) {
        for (unsigned f = 0; f < 4; ++f) {
            if (!std::equal(r.begin() + 6, r.begin() + 118, r.begin() + 6 + f * 112)) return false;
        }
        ++count; return true;
    };
    check(encoder.Push(silence, cleared) && count >= 60, "clearing discards pending cry PCM and resets encoder history");
    encoder.Clear();
    check(!encoder.Push(pcm, [](std::span<const uint8_t>) { return false; }), "transport failure propagates to the caller");
    if (argc == 2) {
        FILE* out = std::fopen(argv[1], "wb");
        check(out != nullptr, "external decoder fixture opened");
        if (out) { for (const auto& r : packets) std::fwrite(r.data() + 6, 1, 4 * 112, out); std::fclose(out); }
    }
    std::printf("%u failures\n", failed);
    return failed ? 1 : 0;
}
