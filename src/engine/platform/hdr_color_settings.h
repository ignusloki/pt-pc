#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace pt {
enum class HdrColorPreset { Original, HdrLook, Custom };

// Shared persisted state and renderer ABI. Exposure is in stops; other grades
// are neutral at one except Blowout, which is off at zero. These controls affect HDR scene color, never UI or SDR.
struct HdrColorSettings {
    float exposure = 0.0f;
    float contrast = 1.0f;
    float highlights = 1.0f;
    float shadows = 1.0f;
    float saturation = 1.0f;
    float highlight_saturation = 1.0f;
    bool wide_gamut = true;
    float blowout = 0.0f;

    HdrColorPreset Preset() const {
        // UI steps and INI round trips can differ by a few float ULPs.
        const auto matches = [](float a, float b) { return std::abs(a - b) <= 1e-5f; };
        if (!wide_gamut || !matches(exposure, 0) || !matches(contrast, 1) || !matches(shadows, 1) ||
            !matches(highlight_saturation, 1) || !matches(blowout, 0)) return HdrColorPreset::Custom;
        if (matches(highlights, 1) && matches(saturation, 1)) return HdrColorPreset::Original;
        if (matches(highlights, 1.45f) && matches(saturation, 1.5f)) return HdrColorPreset::HdrLook;
        return HdrColorPreset::Custom;
    }

    void ApplyPreset(HdrColorPreset preset) {
        if (preset == HdrColorPreset::Custom) return;
        *this = {};
        if (preset == HdrColorPreset::HdrLook) {
            highlights = 1.45f;
            saturation = 1.5f;
        }
    }

    void Normalize() {
        exposure = std::isfinite(exposure) ? std::clamp(exposure, -4.0f, 4.0f) : 0.0f;
        for (float* value : {&contrast, &highlights, &shadows, &saturation, &highlight_saturation})
            *value = std::isfinite(*value) ? std::clamp(*value, 0.0f, 2.0f) : 1.0f;
        blowout = std::isfinite(blowout) ? std::clamp(blowout, 0.0f, 1.0f) : 0.0f;
    }
    std::array<float, 12> Push(bool active) const {
        return {exposure, contrast, highlights, shadows, saturation,
                highlight_saturation, active && wide_gamut ? 1.0f : 0.0f, active ? 1.0f : 0.0f,
                active ? blowout : 0.0f, 0.0f, 0.0f, 0.0f};
    }
};
}
