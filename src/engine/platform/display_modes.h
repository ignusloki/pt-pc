#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace pt {

inline std::vector<glm::ivec2> UniqueDisplaySizes(std::span<const glm::ivec2> modes) {
    std::vector<glm::ivec2> sizes;
    for (const glm::ivec2 size : modes) {
        if (size.x > 0 && size.y > 0 && std::find(sizes.begin(), sizes.end(), size) == sizes.end()) sizes.push_back(size);
    }
    std::sort(sizes.begin(), sizes.end(), [](glm::ivec2 a, glm::ivec2 b) {
        const int64_t area_a = static_cast<int64_t>(a.x) * a.y;
        const int64_t area_b = static_cast<int64_t>(b.x) * b.y;
        return area_a == area_b ? a.x < b.x : area_a < area_b;
    });
    return sizes;
}

struct LimiterPacing {
    int min_refreshes = 0;
    int max_refreshes = 0;
};

inline LimiterPacing PacingOfLimit(double refresh_hz, int fps_limit) {
    if (refresh_hz <= 0.0 || fps_limit <= 0 || fps_limit >= refresh_hz * 0.99) return {};
    const double ratio = refresh_hz / fps_limit;
    const double nearest = std::round(ratio);
    if (std::abs(ratio - nearest) < 0.01 * ratio) return {static_cast<int>(nearest), static_cast<int>(nearest)};
    return {static_cast<int>(std::floor(ratio)), static_cast<int>(std::ceil(ratio))};
}

struct DisplayModeInfo {
    int w = 0;
    int h = 0;
    double hz = 0.0;
};

inline bool SameRefreshRate(double a, double b) { return std::abs(a - b) < 0.01; }

inline std::vector<double> RefreshRatesFor(std::span<const DisplayModeInfo> modes, glm::ivec2 size) {
    std::vector<double> rates;
    for (const DisplayModeInfo& m : modes) {
        if (m.w != size.x || m.h != size.y || m.hz <= 0.0) continue;
        if (std::none_of(rates.begin(), rates.end(), [&](double r) { return SameRefreshRate(r, m.hz); })) rates.push_back(m.hz);
    }
    std::sort(rates.begin(), rates.end(), [](double a, double b) { return a > b; });
    return rates;
}

inline int PickExclusiveMode(std::span<const DisplayModeInfo> modes, glm::ivec2 size, double wanted_hz) {
    int best = -1;
    for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
        const DisplayModeInfo& m = modes[i];
        if (m.w != size.x || m.h != size.y) continue;
        if (wanted_hz > 0.0 && SameRefreshRate(m.hz, wanted_hz)) return i;
        if (best < 0 || m.hz > modes[best].hz) best = i;
    }
    return best;
}

inline std::string RefreshRateText(double hz) {
    const double whole = std::round(hz);
    return std::abs(hz - whole) < 0.005 ? std::format("{:.0f} Hz", whole) : std::format("{:.2f} Hz", hz);
}

inline glm::ivec2 ClosestDisplaySize(std::span<const glm::ivec2> modes, glm::ivec2 requested) {
    if (modes.empty()) return requested;
    const auto score = [requested](glm::ivec2 size) {
        const int64_t dx = static_cast<int64_t>(size.x) - requested.x;
        const int64_t dy = static_cast<int64_t>(size.y) - requested.y;
        return dx * dx + dy * dy;
    };
    return *std::min_element(modes.begin(), modes.end(), [&](glm::ivec2 a, glm::ivec2 b) { return score(a) < score(b); });
}

}
