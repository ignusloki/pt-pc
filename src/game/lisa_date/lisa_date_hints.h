#pragma once

#include <algorithm>
#include <cmath>

namespace pt::game {

inline constexpr int kLisaRushesNeeded = 3;

inline float LisaHintTintStep(int rush) {
    const float x = static_cast<float>(std::clamp(rush, 0, kLisaRushesNeeded)) / static_cast<float>(kLisaRushesNeeded);
    return 0.88f * std::pow(x, 1.6f);
}

inline float LisaHintStingSeconds(int rush) {
    constexpr float kSeconds[kLisaRushesNeeded] = {1.05f, 1.79f, 2.17f};
    return kSeconds[std::clamp(rush, 1, kLisaRushesNeeded) - 1];
}

inline float LisaHintStingGain(int rush) {
    const float x = kLisaRushesNeeded > 1 ? static_cast<float>(std::clamp(rush, 1, kLisaRushesNeeded) - 1) / static_cast<float>(kLisaRushesNeeded - 1) : 1.0f;
    return 0.28f + 0.21f * x;
}

}
