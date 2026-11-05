#pragma once
#include <algorithm>
#include <cmath>

namespace pt::motion_math {
// Both displacement and shutter exposure refer to the same rendered interval.
// Keep the original half-shutter convention used by P.T.'s velocity filters.
inline float ShutterScale(float shutter_seconds, float motion_interval) {
    if (!std::isfinite(shutter_seconds) || !std::isfinite(motion_interval) ||
        shutter_seconds <= 0.0f || motion_interval <= 0.0f) return 0.0f;
    return 0.5f * shutter_seconds / std::clamp(motion_interval, 1.0e-4f, 1.0f);
}
}
