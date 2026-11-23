#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pt {

inline constexpr uint32_t kMaxImageDimension = 8192;

// Produces the RGBA8 mip chain starting at the first level that fits max_dimension.
bool DecodeImageMips(std::span<const uint8_t> encoded, bool srgb, uint32_t max_dimension,
                     std::vector<std::vector<uint8_t>>& levels, uint32_t& width, uint32_t& height, std::string& error);

}
