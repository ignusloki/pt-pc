#pragma once
#include <cstdint>
#include <span>
namespace pt {
struct TextureMip {
    uint32_t width = 0;
    uint32_t height = 0;
    std::span<const uint8_t> data;
};
}
