#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <glm/vec2.hpp>

namespace pt::exposure_math {
// Exact weighted percentiles over the existing small GPU readback, without
// histogram-bin quantization. Arithmetic averaging retains the game's diffuse
// meter calibration; trimming limits isolated fireflies and dark outliers.
inline float Meter(std::span<const glm::vec2> texels) {
    std::array<glm::vec2, 1024> samples;
    size_t count = 0;
    double weight = 0.0;
    for (const auto& v : texels) {
        if (count == samples.size()) break;
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || v.x < 0.0f || v.y <= 0.0f) continue;
        samples[count++] = v;
        weight += v.y;
    }
    if (weight <= 0.0) return -1.0f;
    std::sort(samples.begin(), samples.begin() + count, [](const auto& a, const auto& b) { return a.x < b.x; });
    const double low = weight * 0.02, high = weight * 0.98;
    double position = 0.0, sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double end = position + samples[i].y;
        const double included = std::max(0.0, std::min(end, high) - std::max(position, low));
        sum += included * samples[i].x;
        position = end;
    }
    return static_cast<float>(sum / (high - low));
}
inline float TargetEv(float raw, float key, float min_ev, float max_ev) {
    return std::clamp(std::log2(std::max(key, 1.0e-8f)) - std::log2(std::max(raw, 1.0e-8f)), min_ev, max_ev);
}
// Exact integration of constant-stop-speed movement joining an exponential
// section with continuous first derivative. Positive EV brightens this engine.
// See Epic's 4.25 exposure analysis. Rates are tuning choices, not eye physiology.
inline float Adapt(float current, float target, float dt, float authored_speed) {
    if (!std::isfinite(target) || !std::isfinite(dt) || dt <= 0.0f || !std::isfinite(authored_speed)) return current;
    const double speed = (target < current ? 3.0 : 1.5) * std::clamp(authored_speed / 1.6f, 0.0f, 8.0f);
    constexpr double transition = 1.5;
    double remaining = std::abs(static_cast<double>(target) - current);
    double time = std::min(static_cast<double>(dt), 0.1);
    if (speed <= 0.0 || remaining == 0.0) return current;
    if (remaining > transition) {
        const double linear_time = std::min(time, (remaining - transition) / speed);
        remaining -= linear_time * speed;
        time -= linear_time;
    }
    remaining *= std::exp(-time * speed / transition);
    return static_cast<float>(target + std::copysign(remaining, static_cast<double>(current) - target));
}
}
