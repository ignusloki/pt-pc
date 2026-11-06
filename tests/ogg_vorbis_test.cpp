#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/audio/ogg_vorbis.h"
#include "game/lisa_date/lisa_date_audio.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {
std::vector<uint8_t> Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    assert(in);
    return {std::istreambuf_iterator<char>(in), {}};
}
}

int main() {
    using pt::audio::DecodeOggVorbis;
    struct Fixture { const char* name; uint32_t rate, channels; size_t frames; };
    const std::filesystem::path root = PT_AUDIO_FIXTURES;
    for (const auto& fixture : {Fixture{"mono", 8000, 1, 2400}, Fixture{"stereo", 48000, 2, 14400},
                               Fixture{"surround", 48000, 6, 14400}}) {
        const auto bytes = Read(root / (std::string(fixture.name) + ".ogg"));
        std::vector<int16_t> full;
        uint32_t channels = 0, rate = 0;
        assert(DecodeOggVorbis(bytes, full, channels, rate));
        assert(channels == fixture.channels && rate == fixture.rate && full.size() == fixture.frames * channels);
        pt::game::LisaDatePcm music;
        assert(pt::game::LisaDateDecodeOgg(bytes, music));
        assert(music.samples == full && music.channels == channels && music.rate == rate);
        assert(pt::game::LisaDateDecodeMusic(root, fixture.name, music));
        assert(music.samples == full && music.channels == channels && music.rate == rate);
        // Each plane encodes a different sine: check sample order and decoded content,
        // allowing codec loss. Vorbis's sixth plane is bandwidth-limited LFE.
        for (uint32_t c = 0; c < std::min(channels, 5u); ++c) {
            double squared = 0;
            for (size_t f = 0; f < fixture.frames; ++f) {
                const double expected = 12000 * std::sin(2 * 3.141592653589793 * (220 + c * 110) * f / rate);
                const double delta = full[f * channels + c] - expected;
                squared += delta * delta;
            }
            assert(std::sqrt(squared / fixture.frames) < 300);
        }
        assert(std::all_of(full.begin(), full.end(), [](int16_t s) { return std::abs(static_cast<int>(s)) < 15000; }));
        for (float seconds : {0.00001f, 0.01f, 0.07f, 0.3f, 2.0f}) {
            std::vector<int16_t> prefix;
            assert(DecodeOggVorbis(bytes, prefix, channels, rate, seconds));
            const size_t frames = std::min(fixture.frames, static_cast<size_t>(std::ceil(seconds * static_cast<float>(rate))));
            assert(prefix.size() == frames * channels);
            assert(std::equal(prefix.begin(), prefix.end(), full.begin()));
            assert(pt::game::LisaDateDecodeMusic(root, fixture.name, music, seconds));
            assert(music.samples == prefix && music.channels == channels && music.rate == rate);
        }
        for (float seconds : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            std::vector<int16_t> result;
            assert(DecodeOggVorbis(bytes, result, channels, rate, seconds) && result == full);
        }
        auto chained = bytes;
        auto next = Read(root / (std::string(fixture.channels == 1 ? "stereo" : "mono") + ".ogg"));
        chained.insert(chained.end(), next.begin(), next.end());
        std::vector<int16_t> result;
        assert(DecodeOggVorbis(chained, result, channels, rate));
        assert(result == full && channels == fixture.channels && rate == fixture.rate);
        auto corrupt = bytes;
        corrupt[0] = 0;
        assert(!DecodeOggVorbis(corrupt, result, channels, rate));
        assert(result.empty() && channels == 0 && rate == 0);
        for (size_t length : {size_t{0}, size_t{1}, size_t{26}, size_t{64}}) {
            result = {123}; channels = rate = 123;
            assert(!DecodeOggVorbis(std::span(bytes).first(length), result, channels, rate));
            assert(result.empty() && channels == 0 && rate == 0);
        }
        std::printf("PASS %s: metadata, samples, bounded prefix, chaining, malformed input\n", fixture.name);
    }
}
