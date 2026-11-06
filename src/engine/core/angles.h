#pragma once

#include <cmath>

namespace pt {

inline float WrapAngle(float a) {
    constexpr float kPi = 3.14159265f;
    a = std::fmod(a + kPi, 2.0f * kPi);
    if (a < 0.0f) {
        a += 2.0f * kPi;
    }
    return a - kPi;
}

}
