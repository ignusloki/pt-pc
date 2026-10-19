#pragma once

#include "engine/render/gpu_types.h"

namespace pt {

struct GpuMesh;

struct RtCaster {
    const GpuMesh* mesh = nullptr;
    uint32_t submesh = 0;
    glm::mat4 transform{1.0f};
    uint32_t material = 0;
    uint32_t skin_base = gpu::kInvalid;
    uint8_t mask = 3;
    bool alpha_test = false;
    bool double_sided = false;
    bool fitting = false;
    uint32_t light_fitting = 0;
    bool mirror_hidden = false;
};

inline uint32_t RtRecordFlags(const RtCaster& c) {
    return (c.fitting ? 1u | (c.alpha_test ? 2u : 0u) : 0u) | (c.skin_base != gpu::kInvalid ? 4u : 0u) |
           (c.alpha_test ? 8u : 0u) | ((c.light_fitting & 63u) << 4) | (c.mirror_hidden ? 1024u : 0u);
}

}
