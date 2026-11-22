#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <ostream>

namespace pt {

// Use the same spatial grid for the drawable and the macOS window. Each stream declares
// its own colour units; a black flash or a missing band can be compared across both.
struct FrameSignature {
    static constexpr unsigned Columns = 8, Rows = 6;
    unsigned samples = 0, black = 0, invalid = 0;
    uint64_t hash = 14695981039346656037ull;
    double red = 0, green = 0, blue = 0;
    float peak = 0;
    std::array<double, Columns * Rows> cells{};
    std::array<unsigned, Columns * Rows> counts{};

    void Add(unsigned cell, uint64_t rgb_bits, float r, float g, float b, float black_limit) {
        ++samples;
        hash = (hash ^ rgb_bits) * 1099511628211ull;
        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) { ++invalid; return; }
        const float level = .2126f * r + .7152f * g + .0722f * b;
        red += r; green += g; blue += b;
        peak = std::max(peak, level);
        black += std::max({r, g, b}) <= black_limit;
        cells[cell] += level;
        ++counts[cell];
    }
    static constexpr const char* Header = "samples,black_pixels,invalid_pixels,mean_r,mean_g,mean_b,peak_luma,rgb_hash,grid_luma";
    void Write(std::ostream& stream) const {
        const unsigned valid = std::max(1u, samples - invalid);
        stream << std::format("{},{},{},{:.6f},{:.6f},{:.6f},{:.6f},{:016x},", samples, black, invalid,
                              red / valid, green / valid, blue / valid, peak, hash);
        for (unsigned i = 0; i < cells.size(); ++i) {
            if (i) stream << ' ';
            stream << std::format("{:.4f}", cells[i] / std::max(1u, counts[i]));
        }
        stream << '\n';
    }
};

}
