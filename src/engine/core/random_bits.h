#pragma once

#include <cstdint>

namespace pt {

inline uint32_t XorShiftOnce(uint32_t x) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 5;
    return x;
}

}
