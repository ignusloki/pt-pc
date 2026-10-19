#pragma once
#ifdef _WIN32
#include <memory>
#include "engine/render/upscale/upscale.h"

namespace pt::gpu::d3d12 {
using PostUpscaleNeural = pt::PostUpscaleNeural;
std::unique_ptr<PostUpscaleNeural> CreatePostUpscaleNeural(gpu::Device& device);
}
#endif
