#include "engine/render/upscale/upscale.h"

#if !defined(PT_WITH_METALFX)

namespace pt {

std::unique_ptr<UpscaleBackend> CreateMetalfxBackend(vk::Context&) {
    return nullptr;
}

std::unique_ptr<FrameGeneration> CreateMetalfxFrameGeneration(vk::Context&) {
    return nullptr;
}

}

#endif
