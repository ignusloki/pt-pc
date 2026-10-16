#pragma once

#include "engine/render/vk.h"

struct SDL_Window;

namespace pt::vk {

// Match scRGB's 80 nits per linear unit in the native layer metadata, or clear it for SDR.
// HDR10 uses MoltenVK's normalized PQ path. Called on the window's main thread.
bool SetMacHdrMetadata(SDL_Window* window, VkColorSpaceKHR color_space, const VkHdrMetadataEXT* metadata);

}
