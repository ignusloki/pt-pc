#ifdef _WIN32

#include "engine/render/d3d12/backend/d3d12_internal.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include "engine/core/log.h"

namespace pt::gpu::d3d12 {
namespace {

uint32_t ViewIndex(DescriptorHeap& heap, D3D12_CPU_DESCRIPTOR_HANDLE cpu) {
    const SIZE_T increment = heap.Cpu(1).ptr - heap.Cpu(0).ptr;
    return static_cast<uint32_t>((cpu.ptr - heap.Cpu(0).ptr) / increment);
}

}

bool DeviceImpl::InitImGui(Format target_format) {
    ImGui_ImplDX12_InitInfo info;
    info.Device = bridge_.Get();
    info.CommandQueue = bridge_.Queue();
    info.NumFramesInFlight = static_cast<int>(options_.frames_in_flight);
    info.RTVFormat = ToDxgi(target_format);
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.UserData = this;
    info.SrvDescriptorHeap = views_.Heap();
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* i, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto* self = static_cast<DeviceImpl*>(i->UserData);
        const uint32_t index = self->Views().Allocate(1);
        *cpu = self->Views().Cpu(index);
        *gpu = self->Views().Gpu(index);
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* i, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
        auto* self = static_cast<DeviceImpl*>(i->UserData);
        self->Views().Free(ViewIndex(self->Views(), cpu), 1);
    };
    if (!ImGui_ImplDX12_Init(&info)) {
        LogError("d3d12 backend: ImGui_ImplDX12_Init failed");
        return false;
    }
    imgui_ = true;
    return true;
}

void DeviceImpl::ImGuiNewFrame() {
    if (imgui_) {
        ImGui_ImplDX12_NewFrame();
    }
}

void DeviceImpl::RenderImGui(gpu::CommandList& list, ImDrawData* data) {
    if (!imgui_ || !data) {
        return;
    }
    auto& cl = static_cast<CommandListImpl&>(list);
    ImGui_ImplDX12_RenderDrawData(data, cl.List());
    cl.ResumeAfterNative();
}

void DeviceImpl::ShutdownImGui() {
    if (imgui_) {
        WaitIdle();
        ImGui_ImplDX12_Shutdown();
        imgui_ = false;
    }
}

}

#endif
