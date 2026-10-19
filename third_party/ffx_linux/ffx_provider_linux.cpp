// The provider table of the Linux FSR library (cmake/FsrLinux.cmake), in place of the SDK's ffx-api/src/ffx_provider.cpp.
// Based on that file of the AMD FidelityFX SDK v1.1.4 (Copyright (C) 2024 Advanced Micro Devices, Inc., MIT licence,
// licenses/AMD_FidelityFX_SDK_LICENSE.txt). The original also asks the AMD Windows driver (amdxc64.dll, through a D3D12
// device) for newer driver-side providers and lists the frame generation providers; on Linux there is no such driver
// interface and frame generation is not built, so the table holds the FSR 3.1 upscaler only.

#include "ffx_provider.h"
#include "ffx_provider_fsr3upscale.h"

#include <FidelityFX/host/backends/vk/ffx_vk.h>

// the Vulkan backend's interface table points at the frame interpolation swapchain (FrameInterpolationSwapchainVK.cpp,
// Win32 threads and events), which this library leaves out; only frame generation calls it
FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*) {
    return FFX_ERROR_INVALID_ARGUMENT;
}

static const ffxProvider* const providers[] = {
    &ffxProvider_FSR3Upscale::Instance,
};
static constexpr size_t providerCount = sizeof(providers) / sizeof(providers[0]);

const ffxProvider* GetffxProvider(ffxStructType_t descType, uint64_t overrideId, void*) {
    for (size_t i = 0; i < providerCount; ++i) {
        if (providers[i]->CanProvide(descType) && (!overrideId || providers[i]->GetId() == overrideId)) {
            return providers[i];
        }
    }
    return nullptr;
}

const ffxProvider* GetAssociatedProvider(ffxContext* context) {
    const InternalContextHeader* hdr = (const InternalContextHeader*)(*context);
    return hdr->provider;
}

uint64_t GetProviderCount(ffxStructType_t descType, void* device) {
    return GetProviderVersions(descType, device, UINT64_MAX, nullptr, nullptr);
}

uint64_t GetProviderVersions(ffxStructType_t descType, void*, uint64_t capacity, uint64_t* versionIds, const char** versionNames) {
    uint64_t count = 0;
    for (size_t i = 0; i < providerCount && count < capacity; ++i) {
        if (providers[i]->CanProvide(descType)) {
            if (versionIds) versionIds[count] = providers[i]->GetId();
            if (versionNames) versionNames[count] = providers[i]->GetVersionName();
            ++count;
        }
    }
    return count;
}
