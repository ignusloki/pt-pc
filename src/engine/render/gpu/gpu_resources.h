#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "engine/render/gpu/gpu_enums.h"

namespace pt::gpu {

struct ImageTag;
struct ImageViewTag;
struct BufferTag;
struct AllocationTag;

template <class Tag>
struct Handle {
    uint64_t bits = 0;
    constexpr Handle() = default;
    constexpr explicit Handle(uint64_t b) : bits(b) {}
    constexpr Handle(std::nullptr_t) {}
    constexpr explicit operator bool() const { return bits != 0; }
    constexpr bool operator==(const Handle&) const = default;
    constexpr bool operator==(std::nullptr_t) const { return bits == 0; }
};

struct SamplerTag;
struct BindingLayoutTag;
struct BindingPoolTag;
struct BindingSetTag;
struct PipelineLayoutTag;
struct PipelineTag;
struct QueryPoolTag;
struct AccelerationStructureTag;

using ImageHandle = Handle<ImageTag>;
using ImageViewHandle = Handle<ImageViewTag>;
using BufferHandle = Handle<BufferTag>;
using AllocationHandle = Handle<AllocationTag>;
using SamplerHandle = Handle<SamplerTag>;
using BindingLayoutHandle = Handle<BindingLayoutTag>;
using BindingPoolHandle = Handle<BindingPoolTag>;
using BindingSetHandle = Handle<BindingSetTag>;
using PipelineLayoutHandle = Handle<PipelineLayoutTag>;
using PipelineHandle = Handle<PipelineTag>;
using QueryPoolHandle = Handle<QueryPoolTag>;
using AccelerationStructureHandle = Handle<AccelerationStructureTag>;

struct ImageDesc {
    Format format = Format::Undefined;
    Extent3D extent{};
    ImageUsageFlags usage{};
    uint32_t mip_levels = 1;
    uint32_t layers = 1;
    AspectFlags aspect = Aspect::Color;
    bool cube = false;
    const char* name = nullptr;
};

struct ImageResource {
    ImageHandle image;
    ImageViewHandle view;
    AllocationHandle allocation;
    Format format = Format::Undefined;
    Extent3D extent{};
    uint32_t mip_levels = 1;
    uint32_t layers = 1;
    ImageUsageFlags usage{};
    bool Valid() const { return static_cast<bool>(image); }
};

struct BufferDesc {
    uint64_t size = 0;
    BufferUsageFlags usage{};
    bool host_visible = false;
    bool readback = false;
    const char* name = nullptr;
};

struct BufferResource {
    BufferHandle buffer;
    AllocationHandle allocation;
    void* mapped = nullptr;
    uint64_t size = 0;
    bool Valid() const { return static_cast<bool>(buffer); }
};

struct SamplerDesc {
    Filter mag = Filter::Linear;
    Filter min = Filter::Linear;
    MipmapMode mipmap = MipmapMode::Linear;
    AddressMode address_u = AddressMode::Repeat;
    AddressMode address_v = AddressMode::Repeat;
    AddressMode address_w = AddressMode::Repeat;
    float max_anisotropy = 1.0f;
    bool compare = false;
    CompareOp compare_op = CompareOp::Less;
    float min_lod = 0.0f;
    float max_lod = 1000.0f;
    BorderColor border = BorderColor::FloatTransparentBlack;
};

struct RenderTarget {
    ImageResource image;
    Layout layout = Layout::Undefined;
    AspectFlags aspect = Aspect::Color;

    bool Valid() const { return image.Valid(); }
    Extent2D Extent() const { return {image.extent.width, image.extent.height}; }
};

struct TargetUse {
    RenderTarget* target = nullptr;
    Layout layout = Layout::Undefined;
};

struct ColorOutput {
    RenderTarget* target = nullptr;
    bool clear = false;
    ClearColor clear_value{};
};

struct Attachment {
    ImageViewHandle view;
    LoadOp load = LoadOp::Load;
    ClearColor clear_value{};
};

struct ImageBarrier {
    ImageHandle image;
    AspectFlags aspect = Aspect::Color;
    PipelineStage src_stage = PipelineStage::AllCommands;
    uint64_t src_access = 0;
    Layout old_layout = Layout::Undefined;
    PipelineStage dst_stage = PipelineStage::AllCommands;
    uint64_t dst_access = 0;
    Layout new_layout = Layout::Undefined;
    uint32_t base_mip = 0;
    uint32_t mip_levels = ~0u;
    uint32_t base_layer = 0;
    uint32_t layers = ~0u;
};

struct ImageSubresource {
    AspectFlags aspect = Aspect::Color;
    uint32_t mip = 0;
    uint32_t base_layer = 0;
    uint32_t layers = 1;
};

struct ImageCopy {
    ImageSubresource src;
    int32_t src_offset[3] = {};
    ImageSubresource dst;
    int32_t dst_offset[3] = {};
    Extent3D extent{};
};

struct BufferImageCopy {
    uint64_t buffer_offset = 0;
    uint32_t row_length = 0;
    uint32_t image_height = 0;
    ImageSubresource image;
    int32_t image_offset[3] = {};
    Extent3D extent{};
};

struct BindingDesc {
    uint32_t binding = 0;
    BindingType type = BindingType::SampledImage;
    uint32_t count = 1;
    ShaderStageFlags stages = ShaderStage::All;
    BindingFlags flags{};
    bool writable = false;
};

struct BindingLayoutDesc {
    std::vector<BindingDesc> bindings;
    bool update_after_bind = false;
};

struct BindingPoolSize {
    BindingType type = BindingType::SampledImage;
    uint32_t count = 0;
};

struct BindingPoolDesc {
    std::vector<BindingPoolSize> sizes;
    uint32_t max_sets = 0;
    bool update_after_bind = false;
};

struct ImageBinding {
    ImageViewHandle view;
    Layout layout = Layout::ShaderReadOnly;
    SamplerHandle sampler;
    const ImageResource* image = nullptr;
};

struct BufferBinding {
    BufferHandle buffer;
    uint64_t offset = 0;
    uint64_t range = ~0ull;
};

struct BindingWrite {
    BindingSetHandle set;
    uint32_t binding = 0;
    uint32_t first = 0;
    BindingType type = BindingType::SampledImage;
    std::span<const ImageBinding> images;
    std::span<const BufferBinding> buffers;
    std::span<const SamplerHandle> samplers;
    AccelerationStructureHandle acceleration_structure;
};

struct PipelineLayoutDesc {
    std::vector<BindingLayoutHandle> sets;
    uint32_t push_bytes = 0;
    ShaderStageFlags push_stages = ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute;
};

enum class BlendMode : uint8_t { None, Alpha, PremultipliedFadeAlpha, Additive, ProbeLerp, ProbeAccumulate, Multiply, AdditiveAlpha };

struct BlendState {
    bool enable = false;
    BlendFactor src_color = BlendFactor::One;
    BlendFactor dst_color = BlendFactor::Zero;
    BlendOp color_op = BlendOp::Add;
    BlendFactor src_alpha = BlendFactor::One;
    BlendFactor dst_alpha = BlendFactor::Zero;
    BlendOp alpha_op = BlendOp::Add;
    ColorMask write_mask = kColorMaskAll;
};

struct GraphicsPipelineDesc {
    const char* vertex = "fullscreen.vert";
    const char* fragment = nullptr;
    PipelineLayoutHandle layout;
    std::vector<Format> colors;
    Format depth = Format::Undefined;
    bool mesh_input = false;
    struct Attribute {
        Format format = Format::Undefined;
        uint32_t offset = 0;
    };
    std::vector<Attribute> vertex_attributes;
    uint32_t vertex_stride = 0;
    bool depth_test = false;
    bool depth_write = false;
    CompareOp depth_compare = CompareOp::GreaterOrEqual;
    CullMode cull = CullMode::None;
    bool depth_bias = false;
    bool depth_bounds = false;
    BlendMode blend = BlendMode::None;
    std::vector<BlendMode> blends;
    ColorMask write_mask = kColorMaskAll;
    std::vector<ColorMask> write_masks;
    bool fixed_state = false;
    std::vector<BlendState> blend_states;
    bool depth_state = true;
};

struct ComputePipelineDesc {
    PipelineLayoutHandle layout;
    const char* shader = nullptr;
};

enum class Backend : uint8_t { Vulkan, D3D12 };

struct DeviceInfo {
    Backend backend = Backend::Vulkan;
    std::string adapter;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint8_t luid[8] = {};
    bool luid_valid = false;
    uint64_t dedicated_video_memory = 0;
    bool ray_query = false;
    bool depth_bounds = false;
    bool dynamic_depth_bias = false;
    double timestamp_period_ns = 1.0;
    uint32_t max_bindless_textures = 8192;
    float max_sampler_anisotropy = 16.0f;
};

}
