#pragma once

#ifdef _WIN32

#include <windows.h>

#include <d3d12.h>

#include "engine/render/gpu/gpu_command_list.h"
#include "engine/render/gpu/gpu_device.h"

namespace pt::d3d12 {
class Device;
}

namespace pt::gpu::d3d12 {

ID3D12Device* NativeDevice(gpu::Device& device);
ID3D12CommandQueue* NativeQueue(gpu::Device& device);
pt::d3d12::Device& NativeBridgeDevice(gpu::Device& device);
ID3D12GraphicsCommandList* Native(gpu::CommandList& list);
ID3D12Resource* Native(ImageHandle image);
ID3D12Resource* Native(BufferHandle buffer);
D3D12_RESOURCE_STATES TrackedState(ImageHandle image);
void ResumeAfterNative(gpu::CommandList& list);

}

#endif
