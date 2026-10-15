#include "engine/render/d3d12/backend/d3d12_internal.h"

#include <algorithm>

namespace pt::gpu::d3d12 {

DXGI_FORMAT ToDxgi(Format f) {
    switch (f) {
    case Format::Undefined: return DXGI_FORMAT_UNKNOWN;
    case Format::R8Unorm: return DXGI_FORMAT_R8_UNORM;
    case Format::R8G8Unorm: return DXGI_FORMAT_R8G8_UNORM;
    case Format::R8G8B8A8Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::R8G8B8A8Uint: return DXGI_FORMAT_R8G8B8A8_UINT;
    case Format::R8G8B8A8Srgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::B8G8R8A8Unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::B8G8R8A8Srgb: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case Format::A2B10G10R10UnormPack32: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case Format::R16Sfloat: return DXGI_FORMAT_R16_FLOAT;
    case Format::R16G16Sfloat: return DXGI_FORMAT_R16G16_FLOAT;
    case Format::R16G16B16A16Sfloat: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::R32Uint: return DXGI_FORMAT_R32_UINT;
    case Format::R32Sfloat: return DXGI_FORMAT_R32_FLOAT;
    case Format::R32G32Sfloat: return DXGI_FORMAT_R32G32_FLOAT;
    case Format::R32G32B32Sfloat: return DXGI_FORMAT_R32G32B32_FLOAT;
    case Format::R32G32B32A32Sfloat: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Format::D32Sfloat: return DXGI_FORMAT_D32_FLOAT;
    case Format::Bc1RgbUnormBlock:
    case Format::Bc1RgbaUnormBlock: return DXGI_FORMAT_BC1_UNORM;
    case Format::Bc1RgbSrgbBlock:
    case Format::Bc1RgbaSrgbBlock: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case Format::Bc2UnormBlock: return DXGI_FORMAT_BC2_UNORM;
    case Format::Bc2SrgbBlock: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case Format::Bc3UnormBlock: return DXGI_FORMAT_BC3_UNORM;
    case Format::Bc3SrgbBlock: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case Format::Bc4UnormBlock: return DXGI_FORMAT_BC4_UNORM;
    case Format::Bc4SnormBlock: return DXGI_FORMAT_BC4_SNORM;
    case Format::Bc5UnormBlock: return DXGI_FORMAT_BC5_UNORM;
    case Format::Bc5SnormBlock: return DXGI_FORMAT_BC5_SNORM;
    case Format::Bc6hUfloatBlock: return DXGI_FORMAT_BC6H_UF16;
    case Format::Bc6hSfloatBlock: return DXGI_FORMAT_BC6H_SF16;
    case Format::Bc7UnormBlock: return DXGI_FORMAT_BC7_UNORM;
    case Format::Bc7SrgbBlock: return DXGI_FORMAT_BC7_UNORM_SRGB;
    }
    return DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT ResourceFormat(Format f, bool sampled) {
    if (f == Format::D32Sfloat && sampled) {
        return DXGI_FORMAT_R32_TYPELESS;
    }
    return ToDxgi(f);
}

DXGI_FORMAT ShaderViewFormat(Format f) {
    return f == Format::D32Sfloat ? DXGI_FORMAT_R32_FLOAT : ToDxgi(f);
}

D3D12_RESOURCE_STATES StateFor(Layout layout) {
    constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    switch (layout) {
    case Layout::General: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case Layout::ColorAttachment: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case Layout::DepthAttachment:
    case Layout::DepthStencilAttachment: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case Layout::DepthReadOnly:
    case Layout::DepthStencilReadOnly: return D3D12_RESOURCE_STATE_DEPTH_READ | kRead;
    case Layout::ShaderReadOnly: return kRead;
    case Layout::TransferSrc: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case Layout::TransferDst: return D3D12_RESOURCE_STATE_COPY_DEST;
    case Layout::PresentSrc: return D3D12_RESOURCE_STATE_PRESENT;
    case Layout::Undefined:
    case Layout::Preinitialized: break;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

D3D12_COMPARISON_FUNC ToD3D(CompareOp op) {
    return static_cast<D3D12_COMPARISON_FUNC>(static_cast<uint32_t>(op) + 1);
}
static_assert(D3D12_COMPARISON_FUNC_GREATER_EQUAL == static_cast<int>(CompareOp::GreaterOrEqual) + 1);
static_assert(D3D12_COMPARISON_FUNC_ALWAYS == static_cast<int>(CompareOp::Always) + 1);

bool DescriptorHeap::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, bool shader_visible, const wchar_t* name) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = count;
    desc.Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)))) {
        return false;
    }
    heap_->SetName(name);
    cpu_ = heap_->GetCPUDescriptorHandleForHeapStart();
    gpu_ = shader_visible ? heap_->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    increment_ = device->GetDescriptorHandleIncrementSize(type);
    count_ = count;
    used_ = 0;
    free_.clear();
    free_[0] = count;
    return true;
}

uint32_t DescriptorHeap::Allocate(uint32_t count) {
    std::lock_guard lock(mutex_);
    for (auto it = free_.begin(); it != free_.end(); ++it) {
        if (it->second < count) {
            continue;
        }
        const uint32_t index = it->first;
        const uint32_t left = it->second - count;
        free_.erase(it);
        if (left > 0) {
            free_[index + count] = left;
        }
        used_ += count;
        return index;
    }
    return kNone;
}

void DescriptorHeap::Free(uint32_t index, uint32_t count) {
    if (index == kNone || count == 0) {
        return;
    }
    std::lock_guard lock(mutex_);
    used_ -= std::min(used_, count);
    auto next = free_.lower_bound(index);
    if (next != free_.end() && next->first == index + count) {
        count += next->second;
        next = free_.erase(next);
    }
    if (next != free_.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == index) {
            prev->second += count;
            return;
        }
    }
    free_[index] = count;
}

}
