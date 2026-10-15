#include "game/game_plus_intercom.h"

#include <algorithm>
#include <cmath>

namespace pt::game::intercom {
namespace {

constexpr uint32_t kRate = 48000;
constexpr float kPi = 3.14159265358979f;

struct Rng {
    explicit Rng(uint32_t seed) : x(seed ? seed : 0x9E3779B9u) {}
    uint32_t Next() {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    }
    float Unit() { return static_cast<float>(Next() >> 8) / 16777216.0f; }
    float Signed() { return Unit() * 2.0f - 1.0f; }
    uint32_t x;
};

struct Builder {
    Curve curve;
    float t = 0.0f;
    void To(float dt, float level) {
        t += dt;
        curve.keys.push_back({t, level});
    }
    void Hold(float dt) { To(dt, curve.keys.empty() ? 0.0f : curve.keys.back().level); }
};

struct LowPass {
    float a = 0.0f;
    float y = 0.0f;
    explicit LowPass(float hz) : a(1.0f - std::exp(-2.0f * kPi * hz / kRate)) {}
    float Step(float x) { return y += a * (x - y); }
};
struct HighPass {
    LowPass low;
    explicit HighPass(float hz) : low(hz) {}
    float Step(float x) { return x - low.Step(x); }
};

void Normalize(std::vector<float>& s, float gain) {
    float peak = 0.0f;
    for (float v : s) peak = std::max(peak, std::abs(v));
    const float k = peak > 1.0e-9f ? gain / peak : 0.0f;
    for (float& v : s) v *= k;
}

LisaDatePcm ToPcm(const std::vector<float>& s) {
    LisaDatePcm pcm;
    pcm.channels = 1;
    pcm.rate = kRate;
    pcm.samples.resize(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        pcm.samples[i] = static_cast<int16_t>(std::lround(std::clamp(s[i], -1.0f, 1.0f) * 32767.0f));
    }
    return pcm;
}

uint32_t HashText(std::string_view text) {
    uint32_t h = 2166136261u;
    for (char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}

}

float Curve::Level(float t) const {
    if (keys.empty() || t <= 0.0f || t >= keys.back().t) return 0.0f;
    const auto next = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const Key& k) { return v < k.t; });
    if (next == keys.begin()) return 0.0f;
    const Key& b = *next;
    const Key& a = *(next - 1);
    const float span = b.t - a.t;
    return span <= 1.0e-6f ? b.level : a.level + (b.level - a.level) * (t - a.t) / span;
}

Curve RingCurve(uint32_t seed, float* lit) {
    Rng r(Mix(seed, 0x52494E47u));
    Builder b;
    b.curve.keys.push_back({0.0f, 0.0f});
    const float first = 0.25f + 0.1f * r.Unit();
    b.To(0.06f, first);
    b.Hold(0.10f + 0.06f * r.Unit());
    b.To(0.06f, 0.0f);
    b.Hold(0.55f + 0.15f * r.Unit());
    b.To(0.06f, 0.5f + 0.1f * r.Unit());
    b.Hold(0.18f);
    b.To(0.07f, 0.08f);
    b.Hold(0.30f + 0.1f * r.Unit());
    b.To(0.15f, 1.0f);
    b.Hold(0.70f + 0.2f * r.Unit());
    b.To(0.06f, 0.0f);
    b.Hold(0.80f + 0.3f * r.Unit());
    b.To(0.20f, 0.55f);
    b.To(0.45f, 0.92f);
    const float duration = kLitMin + (kLitMax - kLitMin) * r.Unit();
    const float end = b.t + duration;
    const float dip = b.t + duration * (0.35f + 0.25f * r.Unit());
    bool dipped = false;
    for (;;) {
        if (!dipped && b.t >= dip && b.t + 0.38f <= end - 0.3f) {
            dipped = true;
            b.To(0.08f, 0.45f);
            b.Hold(0.12f);
            b.To(0.18f, 0.93f);
            continue;
        }
        const float step = 0.45f + 0.25f * r.Unit();
        if (b.t + step > end - 0.3f) break;
        b.To(step, 0.86f + 0.14f * r.Unit());
    }
    b.To(end - b.t, 0.95f);
    if (lit) *lit = duration;
    b.To(0.06f, 0.0f);
    b.Hold(0.40f + 0.1f * r.Unit());
    b.To(0.06f, 0.7f);
    b.Hold(0.22f);
    b.To(0.06f, 0.0f);
    b.Hold(0.55f + 0.1f * r.Unit());
    b.To(0.06f, 0.4f);
    b.Hold(0.12f);
    b.To(0.08f, 0.0f);
    b.Hold(0.10f);
    return b.curve;
}

Curve CrackleCurve(uint32_t seed) {
    Rng r(Mix(seed, 0x43524B4Cu));
    Builder b;
    b.curve.keys.push_back({0.0f, 0.0f});
    b.To(0.05f, 0.45f + 0.1f * r.Unit());
    b.Hold(0.08f);
    b.To(0.06f, 0.05f);
    b.Hold(0.23f + 0.08f * r.Unit());
    b.To(0.05f, 0.3f);
    b.Hold(0.08f);
    b.To(0.06f, 0.0f);
    b.Hold(0.05f);
    return b.curve;
}

int MaxSwingsPerSecond(const Curve& curve, float swing) {
    const float step = 0.001f;
    std::vector<float> events;
    bool up = false;
    float extreme = curve.Level(0.0f);
    for (float t = 0.0f; t <= curve.Duration() + step; t += step) {
        const float v = curve.Level(t);
        if (up) {
            extreme = std::max(extreme, v);
            if (v < extreme - swing) {
                events.push_back(t);
                up = false;
                extreme = v;
            }
        } else {
            extreme = std::min(extreme, v);
            if (v > extreme + swing) {
                events.push_back(t);
                up = true;
                extreme = v;
            }
        }
    }
    int most = 0;
    size_t from = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        while (events[i] - events[from] >= 1.0f) ++from;
        most = std::max(most, static_cast<int>(i - from + 1));
    }
    return most;
}

LisaDatePcm ChimePcm(float gain) {
    struct Tone {
        float hz, start, tau, length, droop_cents;
    };
    static constexpr Tone kTones[2] = {{659.26f, 0.0f, 0.42f, 0.75f, 0.0f}, {523.25f, 0.62f, 1.05f, 2.05f, -18.0f}};
    static constexpr float kPartials[4][2] = {{1.0f, 1.0f}, {2.0f, 0.22f}, {3.0f, 0.07f}, {4.16f, 0.05f}};
    static constexpr float kDetune[2] = {-7.0f, 5.0f};
    const size_t total = static_cast<size_t>(2.75f * kRate);
    std::vector<float> s(total, 0.0f);
    Rng r(0x50494E47u);
    for (const Tone& tone : kTones) {
        const size_t begin = static_cast<size_t>(tone.start * kRate);
        const size_t count = std::min(total - begin, static_cast<size_t>(tone.length * kRate));
        for (const auto& [ratio, amp] : kPartials) {
            for (float detune : kDetune) {
                double phase = 0.0;
                for (size_t i = 0; i < count; ++i) {
                    const float t = static_cast<float>(i) / kRate;
                    const float cents = detune + tone.droop_cents * (t / tone.length);
                    const double hz = tone.hz * ratio * std::exp2(cents / 1200.0f);
                    phase += 2.0 * 3.141592653589793 * hz / kRate;
                    const float attack = std::min(1.0f, t / 0.006f);
                    const float release = std::min(1.0f, (tone.length - t) / 0.06f);
                    const float env = attack * release * std::exp(-t / tone.tau * (1.0f + 0.6f * (ratio - 1.0f)));
                    s[begin + i] += 0.5f * amp * env * static_cast<float>(std::sin(phase));
                }
            }
        }
        for (size_t i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / kRate;
            s[begin + i] += 0.012f * std::exp(-t / tone.tau) * r.Signed();
        }
    }
    HighPass high(280.0f);
    LowPass low(3200.0f);
    for (float& v : s) v = std::tanh(1.3f * low.Step(high.Step(v)));
    Normalize(s, gain);
    return ToPcm(s);
}

LisaDatePcm HumPcm(const Curve& curve, uint32_t seed, float gain) {
    Rng r(Mix(seed, 0x48554D21u));
    const size_t total = static_cast<size_t>((curve.Duration() + 0.1f) * kRate);
    std::vector<float> hum(total, 0.0f);
    std::vector<float> pops(total, 0.0f);
    HighPass fizz_high(1500.0f);
    LowPass fizz_low(5000.0f);
    float env = 0.0f;
    float previous = 0.0f;
    for (size_t i = 0; i < total; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float target = curve.Level(t);
        const float k = target > env ? 1.0f - std::exp(-1.0f / (0.008f * kRate)) : 1.0f - std::exp(-1.0f / (0.025f * kRate));
        env += k * (target - env);
        const float w = 2.0f * kPi * t;
        float v = 0.5f * std::sin(w * 100.0f) + 0.28f * std::sin(w * 200.0f + 0.4f) + 0.16f * std::sin(w * 300.0f + 1.1f) +
                  0.08f * std::sin(w * 50.0f);
        const float buzz = std::pow(std::abs(std::sin(w * 50.0f)), 6.0f);
        v += 0.35f * buzz * fizz_low.Step(fizz_high.Step(r.Signed()));
        v *= 0.9f + 0.1f * std::sin(w * 0.7f);
        hum[i] = v * std::pow(env, 1.5f);
        const bool rise = previous < 0.2f && target >= 0.2f;
        const bool fall = previous >= 0.2f && target < 0.2f;
        if (rise || fall) {
            const float strength = rise ? 1.0f : 0.55f;
            const size_t length = static_cast<size_t>(0.04f * kRate);
            for (size_t j = 0; j < length && i + j < total; ++j) {
                pops[i + j] += strength * std::exp(-static_cast<float>(j) / (0.012f * kRate)) * r.Signed();
            }
        }
        previous = target;
    }
    Normalize(hum, 0.45f);
    HighPass pop_high(700.0f);
    for (float& v : pops) v = pop_high.Step(v);
    Normalize(pops, 1.0f);
    std::vector<float> s(total);
    for (size_t i = 0; i < total; ++i) s[i] = hum[i] + pops[i];
    Normalize(s, gain);
    return ToPcm(s);
}

LisaDatePcm CracklePcm(const Curve& curve, uint32_t seed, float gain) {
    Rng r(Mix(seed, 0x53544154u));
    const float tail = 0.25f;
    const size_t total = static_cast<size_t>((curve.Duration() + tail) * kRate);
    std::vector<float> s(total, 0.0f);
    HighPass high(900.0f);
    LowPass low(6000.0f);
    float grain = 0.0f;
    size_t grain_left = 0;
    for (size_t i = 0; i < total; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float env = t > curve.Duration() ? 0.15f * std::exp(-(t - curve.Duration()) / 0.06f) : std::max(curve.Level(t), 0.15f);
        if (grain_left == 0 && r.Unit() < 70.0f / kRate) {
            grain = 0.4f + 0.6f * r.Unit();
            grain_left = static_cast<size_t>((0.002f + 0.006f * r.Unit()) * kRate);
        }
        const float crackle = grain_left > 0 ? grain : 0.0f;
        if (grain_left > 0) --grain_left;
        float v = low.Step(high.Step(r.Signed())) * (0.35f + 1.6f * crackle) * env;
        if (t < 0.04f) v += 0.4f * std::sin(2.0f * kPi * 120.0f * t) * (1.0f - t / 0.04f);
        s[i] = v;
    }
    Normalize(s, gain);
    return ToPcm(s);
}

float PeakDb(const LisaDatePcm& pcm) {
    int peak = 0;
    for (int16_t v : pcm.samples) peak = std::max(peak, std::abs(static_cast<int>(v)));
    return peak > 0 ? 20.0f * std::log10(static_cast<float>(peak) / 32767.0f) : -120.0f;
}

float RmsDb(const LisaDatePcm& pcm) {
    double sum = 0.0;
    for (int16_t v : pcm.samples) sum += static_cast<double>(v) * v;
    const double rms = pcm.samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(pcm.samples.size())) / 32767.0;
    return rms > 0.0 ? 20.0f * static_cast<float>(std::log10(rms)) : -120.0f;
}

uint32_t Mix(uint32_t a, uint32_t b) {
    uint32_t h = a ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

bool RingRoll(uint32_t run_seed, std::string_view floor, int pass, float chance) {
    const uint32_t h = Mix(Mix(run_seed, HashText(floor)), static_cast<uint32_t>(pass));
    return static_cast<float>(h >> 8) / 16777216.0f < chance;
}

float RingChance(std::string_view floor) {
    return floor == "f050" || floor == "f090" || floor == "f160" ? 0.2f : 0.1f;
}

}
