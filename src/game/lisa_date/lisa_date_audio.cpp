#include "engine/platform/os.h"
#include "game/lisa_date/lisa_date_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

#include "engine/audio/wem.h"
#include "engine/core/log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#undef L
#undef C
#undef R

namespace pt::game {
namespace {

std::vector<uint8_t> ReadAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void Put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void Put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void PutTag(std::vector<uint8_t>& out, const char* tag) {
    out.insert(out.end(), tag, tag + 4);
}

bool ReadWav(const std::vector<uint8_t>& bytes, LisaDatePcm& pcm) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return false;
    size_t pos = 12;
    bool fmt = false;
    while (pos + 8 <= bytes.size()) {
        uint32_t size = 0;
        std::memcpy(&size, bytes.data() + pos + 4, 4);
        const uint8_t* chunk = bytes.data() + pos + 8;
        const size_t available = std::min<size_t>(size, bytes.size() - pos - 8);
        if (std::memcmp(bytes.data() + pos, "fmt ", 4) == 0 && available >= 16) {
            uint16_t format = 0, channels = 0, bits = 0;
            uint32_t rate = 0;
            std::memcpy(&format, chunk, 2);
            std::memcpy(&channels, chunk + 2, 2);
            std::memcpy(&rate, chunk + 4, 4);
            std::memcpy(&bits, chunk + 14, 2);
            if ((format != 1 && format != 0xFFFE) || bits != 16 || channels == 0) return false;
            pcm.channels = channels;
            pcm.rate = rate;
            fmt = true;
        } else if (std::memcmp(bytes.data() + pos, "data", 4) == 0 && fmt) {
            pcm.samples.resize(available / 2);
            std::memcpy(pcm.samples.data(), chunk, pcm.samples.size() * 2);
            return !pcm.samples.empty();
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

}

bool LisaDateDecodeOgg(std::span<const uint8_t> ogg, LisaDatePcm& pcm) {
    pcm = {};
    if (ogg.empty() || ogg.size() > 0x7FFFFFFFu) return false;
    int channels = 0, rate = 0;
    short* out = nullptr;
    const int frames = stb_vorbis_decode_memory(ogg.data(), static_cast<int>(ogg.size()), &channels, &rate, &out);
    if (frames > 0 && out && channels > 0 && rate > 0) {
        pcm.channels = static_cast<uint32_t>(channels);
        pcm.rate = static_cast<uint32_t>(rate);
        pcm.samples.assign(out, out + static_cast<size_t>(frames) * static_cast<size_t>(channels));
    }
    if (out) std::free(out);
    return !pcm.samples.empty();
}

bool LisaDateDecodeMusic(const std::filesystem::path& dir, const std::string& name, LisaDatePcm& pcm, float max_seconds) {
    pcm = {};
    const std::vector<uint8_t> ogg = ReadAll(dir / (name + ".ogg"));
    if (!ogg.empty() && max_seconds > 0.0f && ogg.size() <= 0x7FFFFFFFu) {
        int error = 0;
        if (stb_vorbis* v = stb_vorbis_open_memory(ogg.data(), static_cast<int>(ogg.size()), &error, nullptr)) {
            const stb_vorbis_info info = stb_vorbis_get_info(v);
            if (info.channels > 0 && info.sample_rate > 0) {
                const size_t frames = static_cast<size_t>(std::ceil(max_seconds * static_cast<float>(info.sample_rate)));
                pcm.channels = static_cast<uint32_t>(info.channels);
                pcm.rate = info.sample_rate;
                pcm.samples.resize(frames * pcm.channels);
                const int got = stb_vorbis_get_samples_short_interleaved(v, info.channels, pcm.samples.data(), static_cast<int>(pcm.samples.size()));
                pcm.samples.resize(static_cast<size_t>(std::max(got, 0)) * pcm.channels);
            }
            stb_vorbis_close(v);
            if (!pcm.samples.empty()) {
                LogInfo("lisa date: music {}.ogg, its first {:.2f} s", name, static_cast<float>(pcm.samples.size() / pcm.channels) / static_cast<float>(pcm.rate));
                return true;
            }
            pcm = {};
        }
    }
    if (!ogg.empty()) {
        int channels = 0, rate = 0;
        short* out = nullptr;
        const int frames = stb_vorbis_decode_memory(ogg.data(), static_cast<int>(ogg.size()), &channels, &rate, &out);
        if (frames > 0 && out && channels > 0) {
            pcm.channels = static_cast<uint32_t>(channels);
            pcm.rate = static_cast<uint32_t>(rate);
            pcm.samples.assign(out, out + static_cast<size_t>(frames) * static_cast<size_t>(channels));
            std::free(out);
            LogInfo("lisa date: music {}.ogg, {} Hz, {} channels, {:.1f} s", name, rate, channels, static_cast<float>(frames) / static_cast<float>(rate));
            return true;
        }
        if (out) std::free(out);
        LogWarn("lisa date: {}.ogg did not decode", name);
    }
    if (ReadWav(ReadAll(dir / (name + ".wav")), pcm)) {
        LogInfo("lisa date: music {}.wav, {} Hz, {} channels, {:.1f} s", name, pcm.rate, pcm.channels,
                static_cast<float>(pcm.samples.size() / pcm.channels) / static_cast<float>(pcm.rate));
        return true;
    }
    LogWarn("lisa date: no {}.ogg or {}.wav in {}", name, name, pt::os::PathToUtf8(dir));
    return false;
}

LisaDatePcm LisaDateSlowed(const LisaDatePcm& pcm, float speed) {
    LisaDatePcm out;
    out.channels = pcm.channels;
    out.rate = pcm.rate;
    if (pcm.channels == 0 || speed <= 0.0f) return out;
    const size_t frames = pcm.samples.size() / pcm.channels;
    const size_t slow_frames = static_cast<size_t>(static_cast<double>(frames) / speed);
    out.samples.resize(slow_frames * pcm.channels);
    for (size_t f = 0; f < slow_frames; ++f) {
        const double at = static_cast<double>(f) * speed;
        const size_t a = std::min(frames - 1, static_cast<size_t>(at));
        const size_t b = std::min(frames - 1, a + 1);
        const double t = at - static_cast<double>(a);
        for (uint32_t c = 0; c < pcm.channels; ++c) {
            const double v = (1.0 - t) * pcm.samples[a * pcm.channels + c] + t * pcm.samples[b * pcm.channels + c];
            out.samples[f * pcm.channels + c] = static_cast<int16_t>(std::clamp(v, -32768.0, 32767.0));
        }
    }
    return out;
}

std::vector<uint8_t> LisaDateWav(const LisaDatePcm& pcm) {
    std::vector<uint8_t> out;
    if (pcm.channels == 0 || pcm.samples.empty()) return out;
    const uint32_t data_bytes = static_cast<uint32_t>(pcm.samples.size() * 2);
    PutTag(out, "RIFF");
    Put32(out, 4 + (8 + 16) + (8 + data_bytes));
    PutTag(out, "WAVE");
    PutTag(out, "fmt ");
    Put32(out, 16);
    Put16(out, 1);
    Put16(out, static_cast<uint16_t>(pcm.channels));
    Put32(out, pcm.rate);
    Put32(out, pcm.rate * pcm.channels * 2);
    Put16(out, static_cast<uint16_t>(pcm.channels * 2));
    Put16(out, 16);
    PutTag(out, "data");
    Put32(out, data_bytes);
    const size_t at = out.size();
    out.resize(at + data_bytes);
    std::memcpy(out.data() + at, pcm.samples.data(), data_bytes);
    return out;
}

LisaMusicLoop LisaMusicLoopFor(const LisaDatePcm& pcm, double speed) {
    if (pcm.channels == 0 || pcm.rate == 0 || speed <= 0.0) return {};
    const double rate = static_cast<double>(pcm.rate) / speed;
    LisaMusicLoop loop;
    loop.a = static_cast<uint64_t>(std::llround(kLisaMusicLoopA * rate));
    loop.b = static_cast<uint64_t>(std::llround(kLisaMusicLoopB * rate));
    loop.crossfade = static_cast<uint64_t>(std::llround(kLisaMusicCrossfade * rate));
    if (!loop.Valid() || loop.b > pcm.samples.size() / pcm.channels) return {};
    return loop;
}

void LisaMusicCrossfadeGains(uint64_t k, uint64_t n, float& out_gain, float& in_gain) {
    const double t = n ? std::min(1.0, static_cast<double>(k + 1) / static_cast<double>(n)) : 1.0;
    const double angle = t * 1.5707963267948966;
    out_gain = static_cast<float>(std::cos(angle));
    in_gain = static_cast<float>(std::sin(angle));
}

LisaMusicPlayer::LisaMusicPlayer(std::shared_ptr<const LisaDatePcm> pcm, LisaMusicLoop loop, std::shared_ptr<LisaMusicState> state)
    : pcm_(std::move(pcm)), loop_(loop), state_(std::move(state)) {
    if (!state_) state_ = std::make_shared<LisaMusicState>();
    total_ = pcm_ && pcm_->channels ? pcm_->samples.size() / pcm_->channels : 0;
    if (!loop_.Valid() || loop_.b > total_) loop_ = {};
}

uint32_t LisaMusicPlayer::Render(float* out, uint32_t frames) {
    constexpr float kScale = 1.0f / 32768.0f;
    const uint32_t channels = pcm_ ? pcm_->channels : 0;
    const bool looping = loop_.Valid();
    const uint64_t fade_at = looping ? loop_.b - loop_.crossfade : 0;
    const uint64_t back = loop_.b - loop_.a;
    uint32_t n = 0;
    while (n < frames && pos_ < total_) {
        if (looping && pos_ == fade_at) {
            crossing_ = !state_->release.load(std::memory_order_relaxed);
            if (!crossing_) state_->outro.store(true, std::memory_order_relaxed);
        }
        float* o = out + static_cast<size_t>(n) * channels;
        const int16_t* x = pcm_->samples.data() + pos_ * channels;
        if (crossing_ && pos_ >= fade_at) {
            float g_out = 1.0f, g_in = 0.0f;
            LisaMusicCrossfadeGains(pos_ - fade_at, loop_.crossfade, g_out, g_in);
            const int16_t* y = x - back * channels;
            for (uint32_t c = 0; c < channels; ++c) o[c] = (static_cast<float>(x[c]) * g_out + static_cast<float>(y[c]) * g_in) * kScale;
        } else {
            for (uint32_t c = 0; c < channels; ++c) o[c] = static_cast<float>(x[c]) * kScale;
        }
        ++pos_;
        ++played_;
        ++n;
        if (crossing_ && pos_ == loop_.b) {
            pos_ = loop_.a;
            crossing_ = false;
            state_->seams.fetch_add(1, std::memory_order_relaxed);
        }
    }
    state_->position.store(pos_, std::memory_order_relaxed);
    if (pos_ >= total_) state_->ended.store(true, std::memory_order_relaxed);
    return n;
}

void LisaMusicPlayer::Seek(uint64_t played) {
    played_ = played;
    if (!loop_.Valid() || played < loop_.b) {
        pos_ = std::min(played, total_);
    } else {
        pos_ = loop_.a + (played - loop_.a) % (loop_.b - loop_.a);
    }
    crossing_ = loop_.Valid() && pos_ >= loop_.b - loop_.crossfade && pos_ < loop_.b && !state_->release.load(std::memory_order_relaxed);
    state_->position.store(pos_, std::memory_order_relaxed);
}

uint64_t LisaMusicPlayer::FramesToEnd(const LisaMusicLoop& loop, uint64_t total, uint64_t pos, bool crossing) {
    if (pos >= total) return 0;
    if (loop.Valid() && crossing && pos < loop.b) return (loop.b - pos) + (total - loop.a);
    return total - pos;
}

namespace {

class LisaMusicReader final : public audio::MediaReader {
public:
    explicit LisaMusicReader(LisaMusicPlayer player) : player_(std::move(player)) {}
    uint32_t Read(float* out, uint32_t frames) override { return player_.Render(out, frames); }
    bool Seek(uint64_t frame) override {
        player_.Seek(frame);
        return true;
    }
    uint64_t Position() const override { return player_.Played(); }

private:
    LisaMusicPlayer player_;
};

}

std::shared_ptr<const audio::Media> LisaMusicMedia(std::shared_ptr<const LisaDatePcm> pcm, LisaMusicLoop loop,
                                                   std::shared_ptr<LisaMusicState> state) {
    if (!pcm || pcm->channels == 0 || pcm->rate == 0 || pcm->samples.empty()) return nullptr;
    const uint64_t total = pcm->samples.size() / pcm->channels;
    if (!loop.Valid() || loop.b > total) loop = {};
    const uint64_t frames = loop.Valid() ? (uint64_t{1} << 40) : total;
    const uint32_t channels = pcm->channels;
    const uint32_t rate = pcm->rate;
    return audio::Media::CreateGenerated(channels, rate, frames, [pcm = std::move(pcm), loop, state = std::move(state)]() {
        return std::make_unique<LisaMusicReader>(LisaMusicPlayer(pcm, loop, state));
    });
}

LisaDatePcm LisaDateExcerpt(const LisaDatePcm& music, float seconds, float gain, float fade) {
    LisaDatePcm out;
    out.channels = music.channels;
    out.rate = music.rate;
    if (music.channels == 0 || music.rate == 0) return out;
    const size_t frames = std::min(music.samples.size() / music.channels, static_cast<size_t>(seconds * static_cast<float>(music.rate)));
    const float fade_in = 0.02f * static_cast<float>(music.rate);
    const float fade_out = std::max(1.0f, fade * static_cast<float>(music.rate));
    out.samples.resize(frames * music.channels);
    for (size_t f = 0; f < frames; ++f) {
        const float t = static_cast<float>(f);
        const float left = static_cast<float>(frames - f);
        float e = gain * std::min(1.0f, t / fade_in);
        if (left < fade_out) {
            const float x = left / fade_out;
            e *= x * x;
        }
        for (uint32_t c = 0; c < music.channels; ++c) {
            const float v = static_cast<float>(music.samples[f * music.channels + c]) * e;
            out.samples[f * music.channels + c] = static_cast<int16_t>(std::clamp(v, -32768.0f, 32767.0f));
        }
    }
    return out;
}

namespace {

constexpr uint32_t kHintRate = 48000;
constexpr float kTwoPi = 6.28318531f;

void AddThump(std::vector<float>& out, float at, float from, float to, float decay, float amplitude) {
    const size_t start = static_cast<size_t>(at * kHintRate);
    const size_t length = static_cast<size_t>(decay * 6.0f * kHintRate);
    float phase = 0.0f;
    for (size_t i = 0; i < length && start + i < out.size(); ++i) {
        const float t = static_cast<float>(i) / kHintRate;
        const float freq = to + (from - to) * std::exp(-t / 0.04f);
        phase += kTwoPi * freq / kHintRate;
        const float envelope = std::min(1.0f, t / 0.006f) * std::exp(-t / decay);
        out[start + i] += amplitude * envelope * (std::sin(phase) + 0.45f * std::sin(2.0f * phase));
    }
}

LisaDatePcm ToPcm(const std::vector<float>& wave, float gain) {
    LisaDatePcm pcm;
    pcm.channels = 1;
    pcm.rate = kHintRate;
    pcm.samples.resize(wave.size());
    for (size_t i = 0; i < wave.size(); ++i) {
        pcm.samples[i] = static_cast<int16_t>(std::clamp(wave[i] * gain * 32767.0f, -32768.0f, 32767.0f));
    }
    return pcm;
}

}

LisaDatePcm LisaDateHeartbeat(float gain) {
    std::vector<float> wave(kHintRate / 2, 0.0f);
    AddThump(wave, 0.0f, 85.0f, 52.0f, 0.045f, 0.7f);
    AddThump(wave, 0.22f, 95.0f, 58.0f, 0.035f, 0.45f);
    return ToPcm(wave, gain);
}

LisaDatePcm LisaDateSadNote(float gain) {
    std::vector<float> wave(static_cast<size_t>(2.4f * kHintRate), 0.0f);
    float phase = 0.0f;
    for (size_t i = 0; i < wave.size(); ++i) {
        const float t = static_cast<float>(i) / kHintRate;
        const float bend = 1.0f - 0.0561f * std::clamp((t - 0.3f) / 1.4f, 0.0f, 1.0f);
        const float vibrato = 1.0f + 0.004f * std::min(1.0f, t / 0.8f) * std::sin(kTwoPi * 4.5f * t);
        phase += kTwoPi * 196.0f * bend * vibrato / kHintRate;
        const float envelope = std::min(1.0f, t / 0.04f) * std::exp(-t / 0.75f) * std::min(1.0f, (2.4f - t) / 0.2f);
        wave[i] = envelope * (0.8f * std::sin(phase) + 0.18f * std::sin(2.0f * phase) + 0.06f * std::sin(3.0f * phase));
    }
    return ToPcm(wave, gain);
}

LisaDatePcm LisaDateShutter(float gain) {
    std::vector<float> wave(static_cast<size_t>(0.22f * kHintRate), 0.0f);
    uint32_t seed = 0x5E1F1Eu;
    auto noise = [&] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
    };
    float last = 0.0f;
    for (size_t i = 0; i < wave.size(); ++i) {
        const float t = static_cast<float>(i) / kHintRate;
        const float n = noise();
        const float bright = n - last;
        last = n;
        float v = 0.0f;
        for (const auto& [at, level] : {std::pair(0.004f, 1.0f), std::pair(0.074f, 0.75f)}) {
            if (t >= at) v += level * bright * std::exp(-(t - at) / 0.004f);
        }
        if (t > 0.01f && t < 0.07f) v += 0.05f * n * std::sin(kTwoPi * 0.5f * (t - 0.01f) / 0.06f);
        wave[i] = v * 0.5f;
    }
    AddThump(wave, 0.004f, 420.0f, 260.0f, 0.008f, 0.35f);
    AddThump(wave, 0.074f, 380.0f, 240.0f, 0.008f, 0.28f);
    return ToPcm(wave, gain);
}

}
