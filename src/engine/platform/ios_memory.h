#pragma once
#include <cstdint>

namespace pt {
// Unknown memory uses the conservative profile; larger devices keep the existing limit.
constexpr uint32_t IosTextureLimitForMemory(uint64_t bytes) {
    return bytes <= 4ull * 1024 * 1024 * 1024 ? 512u : 1024u;
}
uint32_t IosTextureLimit();
}
