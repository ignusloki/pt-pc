#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "engine/render/texture_mip.h"
namespace pt {
bool DecodeBcLevels(VkFormat& format, std::span<const TextureMip> mips,
                    std::vector<std::vector<uint8_t>>& storage, std::vector<TextureMip>& out);
}
