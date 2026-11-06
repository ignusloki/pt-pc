#pragma once

#include <cmath>
#include <glm/glm.hpp>

namespace pt::game {

inline float WrapFoxAngle(float a) {
    constexpr float kPi = 3.14159265f;
    const float t = a + kPi;
    return t >= 0.0f ? std::fmod(t, 2.0f * kPi) - kPi : kPi - std::fmod(-t, 2.0f * kPi);
}

inline float FoxYawOf(const glm::vec3& v) {
    if (std::abs(v.x) + std::abs(v.z) < 1e-6f) {
        return 0.0f;
    }
    return std::atan2(v.x, v.z);
}

}
