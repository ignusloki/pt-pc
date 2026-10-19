#pragma once

#include <memory>
#include <string>
#include "engine/render/upscale/upscale.h"

namespace pt::gpu::d3d12 {

class FsrRayRegeneration {
public:
    explicit FsrRayRegeneration(gpu::Device& device);
    ~FsrRayRegeneration();
    bool Available(std::string& reason);
    bool Create(VkExtent2D size);
    bool Dispatch(const RayDenoiseDispatch& dispatch);
    void Disable(const std::string& reason);
    void Release();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
