#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "game/lisa_date/lisa_date_audio.h"

namespace pt::game::intercom {

struct Curve {
    struct Key {
        float t = 0.0f;
        float level = 0.0f;
    };
    std::vector<Key> keys;
    float Level(float t) const;
    float Duration() const { return keys.empty() ? 0.0f : keys.back().t; }
};

constexpr float kChimeSeconds = 2.6f;
constexpr float kLitMin = 6.0f;
constexpr float kLitMax = 10.0f;

Curve RingCurve(uint32_t seed, float* lit = nullptr);
Curve CrackleCurve(uint32_t seed);

int MaxSwingsPerSecond(const Curve& curve, float swing = 0.2f);

LisaDatePcm ChimePcm(float gain);
LisaDatePcm HumPcm(const Curve& curve, uint32_t seed, float gain);
LisaDatePcm CracklePcm(const Curve& curve, uint32_t seed, float gain);
float PeakDb(const LisaDatePcm& pcm);
float RmsDb(const LisaDatePcm& pcm);

bool RingRoll(uint32_t run_seed, std::string_view floor, int pass, float chance);
float RingChance(std::string_view floor);
uint32_t Mix(uint32_t a, uint32_t b);

struct Play {
    struct Site {
        uint32_t stage = 0;
        bool found = false;
        glm::mat4 monitor{1.0f};
        glm::mat4 area{1.0f};
        bool has_area = false;
    };
    enum class Phase { Idle, Chime, Lit, Crackle };
    uint32_t run_seed = 0;
    int force = -1;
    bool model_ready = false;
    std::vector<Site> sites;
    std::string floor;
    int pass = -1;
    bool rolled = false;
    bool ring = false;
    bool rung = false;
    bool inside = false;
    bool chase_told = false;
    Phase phase = Phase::Idle;
    float time = 0.0f;
    Curve curve;
    float lit_seconds = 0.0f;
    uint32_t seq_seed = 0;
    uint32_t site_stage = 0;
    glm::mat4 monitor{1.0f};
    glm::mat4 area{1.0f};
    bool has_area = false;
    bool ring_after = false;
    float level = 0.0f;
    uint32_t chime_id = 0;
    uint32_t hum_id = 0;
    uint32_t crackle_id = 0;
    double clock = 0.0;
    double told_at = -1.0;
    bool warp_told = false;
    int warp_phase = -1;
    int ocho_state = -1;
    bool chase_pending = false;
};

}
