#define STBI_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "engine/assets/image_decode.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <utility>

namespace pt {
namespace {

// Writing forward also works in place: each output texel precedes the next unread source block.
void HalfRgba(const uint8_t* source, uint8_t* out, uint32_t width, uint32_t height, const float (&to_linear)[256], bool srgb) {
    const uint32_t nw = std::max(1u, width / 2), nh = std::max(1u, height / 2);
    for (uint32_t y = 0; y < nh; ++y) {
        for (uint32_t x = 0; x < nw; ++x) {
            for (uint32_t c = 0; c < 4; ++c) {
                float sum = 0.0f;
                for (uint32_t dy = 0; dy < 2; ++dy) {
                    for (uint32_t dx = 0; dx < 2; ++dx) {
                        const uint8_t v = source[(size_t(std::min(y * 2 + dy, height - 1)) * width + std::min(x * 2 + dx, width - 1)) * 4 + c];
                        sum += c < 3 ? to_linear[v] : v / 255.0f;
                    }
                }
                float f = sum * 0.25f;
                if (srgb && c < 3) f = f <= 0.0031308f ? f * 12.92f : 1.055f * std::pow(f, 1.0f / 2.4f) - 0.055f;
                out[(size_t(y) * nw + x) * 4 + c] = static_cast<uint8_t>(std::clamp(std::lround(f * 255.0f), 0l, 255l));
            }
        }
    }
}

}

bool DecodeImageMips(std::span<const uint8_t> encoded, bool srgb, uint32_t max_dimension,
                     std::vector<std::vector<uint8_t>>& levels, uint32_t& width, uint32_t& height, std::string& error) {
    if (max_dimension == 0 || encoded.empty() || encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        error = "invalid image size or mip limit";
        return false;
    }
    const int size = static_cast<int>(encoded.size());
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(encoded.data(), size, &w, &h, &channels)) {
        error = stbi_failure_reason() ? stbi_failure_reason() : "unrecognized image";
        return false;
    }
    if (w <= 0 || h <= 0 || w > static_cast<int>(kMaxImageDimension) || h > static_cast<int>(kMaxImageDimension)) {
        error = std::format("{} x {} is outside 1 to {} px", w, h, kMaxImageDimension);
        return false;
    }
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(encoded.data(), size, &w, &h, &channels, 4), stbi_image_free);
    if (!pixels) {
        error = stbi_failure_reason() ? stbi_failure_reason() : "image decode failed";
        return false;
    }
    float to_linear[256];
    for (int i = 0; i < 256; ++i) {
        const float f = i / 255.0f;
        to_linear[i] = srgb ? (f <= 0.04045f ? f / 12.92f : std::pow((f + 0.055f) / 1.055f, 2.4f)) : f;
    }
    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
    levels.clear();
    while (std::max(width, height) > max_dimension) {
        HalfRgba(pixels.get(), pixels.get(), width, height, to_linear, srgb);
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    levels.emplace_back(pixels.get(), pixels.get() + size_t(width) * height * 4);
    pixels.reset();
    uint32_t lw = width, lh = height;
    while (lw > 1 || lh > 1) {
        const uint32_t nw = std::max(1u, lw / 2), nh = std::max(1u, lh / 2);
        std::vector<uint8_t> next(size_t(nw) * nh * 4);
        HalfRgba(levels.back().data(), next.data(), lw, lh, to_linear, srgb);
        levels.push_back(std::move(next));
        lw = nw;
        lh = nh;
    }
    return true;
}

}
