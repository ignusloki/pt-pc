#pragma once

#ifdef _WIN32

#include <memory>

#include "engine/render/upscale/upscale.h"

namespace pt::gpu::d3d12 {

std::unique_ptr<UpscaleBackend> CreateUpscaleBackend(UpscalerKind kind, gpu::Device& device);

}

#endif
