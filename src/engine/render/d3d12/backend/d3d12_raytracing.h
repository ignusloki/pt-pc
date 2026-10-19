#pragma once

#ifdef _WIN32

#include <memory>

#include "engine/render/raytracing.h"

namespace pt::gpu::d3d12 {

std::unique_ptr<RayTracing> CreateRayTracing(gpu::Device& device);

}

#endif
