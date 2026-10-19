#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "game/game_plus_intercom.h"

namespace {

int failures = 0;

void Check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    failures += ok ? 0 : 1;
}

}

int main() {
    using namespace pt::game::intercom;
    int most_swings = 0;
    float shortest = 1e9f, longest = 0.0f;
    bool false_light = true, ends_dark = true, ramps = true;
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        float lit = 0.0f;
        const Curve curve = RingCurve(seed * 2654435761u, &lit);
        most_swings = std::max(most_swings, MaxSwingsPerSecond(curve));
        shortest = std::min(shortest, lit);
        longest = std::max(longest, lit);
        bool full = false, cut = false;
        for (float t = 0.0f; t < 4.0f; t += 0.005f) {
            const float v = curve.Level(t);
            if (v >= 0.999f) full = true;
            if (full && v <= 0.001f) cut = true;
        }
        false_light = false_light && full && cut;
        ends_dark = ends_dark && curve.Level(curve.Duration() - 0.01f) < 0.01f && curve.Level(0.0f) == 0.0f;
        for (size_t i = 1; i < curve.keys.size(); ++i) {
            const float dt = curve.keys[i].t - curve.keys[i - 1].t;
            const float dv = std::abs(curve.keys[i].level - curve.keys[i - 1].level);
            ramps = ramps && (dv < 0.02f || dv / dt <= 20.0f);
        }
    }
    Check(most_swings <= 4, "ring flicker: at most 4 swings in any second over 400 seeds (" + std::to_string(most_swings) + ")");
    Check(shortest >= kLitMin && longest <= kLitMax, "ring lit for 6 to 10 s (" + std::to_string(shortest) + " to " + std::to_string(longest) + ")");
    Check(false_light, "ring: a false full light that cuts out before the lit stretch");
    Check(ends_dark, "ring: starts and ends dark");
    Check(ramps, "ring: every change of level ramps over 50 ms or more");
    int crackle_swings = 0;
    float crackle_length = 0.0f;
    for (uint32_t seed = 1; seed <= 100; ++seed) {
        const Curve c = CrackleCurve(seed);
        crackle_swings = std::max(crackle_swings, MaxSwingsPerSecond(c));
        crackle_length = std::max(crackle_length, c.Duration());
    }
    Check(crackle_swings <= 4 && crackle_length < 1.0f, "tell: under a second, at most 4 swings");

    const auto chime = ChimePcm(0.30f);
    const auto curve = RingCurve(7);
    const auto hum = HumPcm(curve, 7, 0.16f);
    const auto crackle = CracklePcm(CrackleCurve(7), 7, 0.28f);
    std::printf("levels: chime peak %.1f rms %.1f dBFS, hum peak %.1f rms %.1f, crackle peak %.1f rms %.1f (%u, %u, %u samples)\n", PeakDb(chime),
                RmsDb(chime), PeakDb(hum), RmsDb(hum), PeakDb(crackle), RmsDb(crackle), static_cast<unsigned>(chime.samples.size()),
                static_cast<unsigned>(hum.samples.size()), static_cast<unsigned>(crackle.samples.size()));
    Check(std::abs(PeakDb(chime) - (-10.46f)) < 0.2f && std::abs(PeakDb(hum) - (-15.92f)) < 0.2f && std::abs(PeakDb(crackle) - (-11.06f)) < 0.2f,
          "sounds peak at their gains");
    Check(chime.rate == 48000 && chime.channels == 1 && chime.samples.size() > 48000 * 2, "chime: 48 kHz mono, over 2 s");
    Check(hum.samples.size() >= static_cast<size_t>(curve.Duration() * 48000), "hum: as long as the curve");

    bool same = true;
    int rings = 0, haunted = 0;
    const int loops = 4000;
    for (int pass = 1; pass <= loops; ++pass) {
        same = same && RingRoll(99, "f020", pass, 0.25f) == RingRoll(99, "f020", pass, 0.25f);
        rings += RingRoll(1234u + pass * 7u, "f030", pass, RingChance("f030")) ? 1 : 0;
        haunted += RingRoll(1234u + pass * 7u, "f160", pass, RingChance("f160")) ? 1 : 0;
    }
    std::printf("roll: %d of %d loops ring on f030, %d on f160\n", rings, loops, haunted);
    Check(same, "roll: the same for the same seed and loop");
    Check(rings > loops * 0.07 && rings < loops * 0.13, "roll: about one loop in ten");
    Check(haunted > loops * 0.16 && haunted < loops * 0.24, "roll: about one in five on f160");
    std::printf("%s (%d failed)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
