#pragma once

#include <volk.h>

#include <cstdint>
#include <type_traits>

#include "engine/render/gpu/gpu_resources.h"

struct VmaAllocation_T;

namespace pt::gpu::vk {

template <class VkT, class Tag>
inline VkT Native(Handle<Tag> h) {
    if constexpr (std::is_pointer_v<VkT>) {
        return reinterpret_cast<VkT>(static_cast<uintptr_t>(h.bits));
    } else {
        return static_cast<VkT>(h.bits);
    }
}

inline VkImage Native(ImageHandle h) { return Native<VkImage>(h); }
inline VkImageView Native(ImageViewHandle h) { return Native<VkImageView>(h); }
inline VkBuffer Native(BufferHandle h) { return Native<VkBuffer>(h); }
inline VkSampler Native(SamplerHandle h) { return Native<VkSampler>(h); }
inline VkDescriptorSetLayout Native(BindingLayoutHandle h) { return Native<VkDescriptorSetLayout>(h); }
inline VkDescriptorPool Native(BindingPoolHandle h) { return Native<VkDescriptorPool>(h); }
inline VkDescriptorSet Native(BindingSetHandle h) { return Native<VkDescriptorSet>(h); }
inline VkPipelineLayout Native(PipelineLayoutHandle h) { return Native<VkPipelineLayout>(h); }
inline VkPipeline Native(PipelineHandle h) { return Native<VkPipeline>(h); }
inline VkQueryPool Native(QueryPoolHandle h) { return Native<VkQueryPool>(h); }
inline VkAccelerationStructureKHR Native(AccelerationStructureHandle h) { return Native<VkAccelerationStructureKHR>(h); }
inline VmaAllocation_T* Native(AllocationHandle h) { return Native<VmaAllocation_T*>(h); }

template <class Tag, class VkT>
inline Handle<Tag> Wrap(VkT v) {
    if constexpr (std::is_pointer_v<VkT>) {
        return Handle<Tag>{static_cast<uint64_t>(reinterpret_cast<uintptr_t>(v))};
    } else {
        return Handle<Tag>{static_cast<uint64_t>(v)};
    }
}

inline VkFormat Native(Format f) { return static_cast<VkFormat>(f); }
inline VkImageLayout Native(Layout l) { return static_cast<VkImageLayout>(l); }
inline VkCompareOp Native(CompareOp op) { return static_cast<VkCompareOp>(op); }
inline VkCullModeFlags Native(CullMode m) { return static_cast<VkCullModeFlags>(m); }
inline VkFrontFace Native(FrontFace f) { return static_cast<VkFrontFace>(f); }
inline VkDescriptorType Native(BindingType t) { return static_cast<VkDescriptorType>(t); }
inline VkExtent2D Native(Extent2D e) { return {e.width, e.height}; }
inline VkExtent3D Native(Extent3D e) { return {e.width, e.height, e.depth}; }
inline Extent2D FromNative(VkExtent2D e) { return {e.width, e.height}; }
inline Extent3D FromNative(VkExtent3D e) { return {e.width, e.height, e.depth}; }
inline VkRect2D Native(Rect2D r) { return {{r.offset.x, r.offset.y}, {r.extent.width, r.extent.height}}; }
inline Format FromNative(VkFormat f) { return static_cast<Format>(f); }
inline Layout FromNative(VkImageLayout l) { return static_cast<Layout>(l); }

}
