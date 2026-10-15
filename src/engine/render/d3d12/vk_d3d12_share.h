#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/render/upscale/frame_generation.h"

namespace pt {

std::unique_ptr<FrameGeneration> CreateXefgFrameGeneration(vk::Context& ctx);

std::vector<const char*> XefgDeviceExtensions();

constexpr uint32_t kXefgMinWindowsBuild = 19043;
inline bool XefgWindowsBuildSupported(uint32_t build) {
    return build >= kXefgMinWindowsBuild;
}

uint32_t XefgDxgiFormat(VkFormat format);

const char* XefgNoteKey(const std::string& reason);

struct XefgMatrices {
    float view[16];
    float projection[16];
};
XefgMatrices XefgCameraMatrices(const FrameGenPrepare& p);

}
