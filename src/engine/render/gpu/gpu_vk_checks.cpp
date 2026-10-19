#include <cstddef>
#include <type_traits>

#include "engine/render/gpu/gpu_enums.h"
#include "engine/render/gpu/gpu_vk_native.h"

namespace pt::gpu {
namespace {

#define PT_SAME(a, b)                                                                                                      \
    static_assert(static_cast<std::underlying_type_t<decltype(a)>>(a) == static_cast<std::underlying_type_t<decltype(a)>>(b), \
                  #a " differs from " #b)

PT_SAME(Format::Undefined, VK_FORMAT_UNDEFINED);
PT_SAME(Format::R8Unorm, VK_FORMAT_R8_UNORM);
PT_SAME(Format::R8G8Unorm, VK_FORMAT_R8G8_UNORM);
PT_SAME(Format::R8G8B8A8Unorm, VK_FORMAT_R8G8B8A8_UNORM);
PT_SAME(Format::R8G8B8A8Uint, VK_FORMAT_R8G8B8A8_UINT);
PT_SAME(Format::R8G8B8A8Srgb, VK_FORMAT_R8G8B8A8_SRGB);
PT_SAME(Format::B8G8R8A8Unorm, VK_FORMAT_B8G8R8A8_UNORM);
PT_SAME(Format::B8G8R8A8Srgb, VK_FORMAT_B8G8R8A8_SRGB);
PT_SAME(Format::A2B10G10R10UnormPack32, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
PT_SAME(Format::R16Sfloat, VK_FORMAT_R16_SFLOAT);
PT_SAME(Format::R16G16Sfloat, VK_FORMAT_R16G16_SFLOAT);
PT_SAME(Format::R16G16B16A16Sfloat, VK_FORMAT_R16G16B16A16_SFLOAT);
PT_SAME(Format::R32Uint, VK_FORMAT_R32_UINT);
PT_SAME(Format::R32Sfloat, VK_FORMAT_R32_SFLOAT);
PT_SAME(Format::R32G32Sfloat, VK_FORMAT_R32G32_SFLOAT);
PT_SAME(Format::R32G32B32Sfloat, VK_FORMAT_R32G32B32_SFLOAT);
PT_SAME(Format::R32G32B32A32Sfloat, VK_FORMAT_R32G32B32A32_SFLOAT);
PT_SAME(Format::D32Sfloat, VK_FORMAT_D32_SFLOAT);
PT_SAME(Format::Bc1RgbUnormBlock, VK_FORMAT_BC1_RGB_UNORM_BLOCK);
PT_SAME(Format::Bc1RgbSrgbBlock, VK_FORMAT_BC1_RGB_SRGB_BLOCK);
PT_SAME(Format::Bc1RgbaUnormBlock, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
PT_SAME(Format::Bc1RgbaSrgbBlock, VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
PT_SAME(Format::Bc2UnormBlock, VK_FORMAT_BC2_UNORM_BLOCK);
PT_SAME(Format::Bc2SrgbBlock, VK_FORMAT_BC2_SRGB_BLOCK);
PT_SAME(Format::Bc3UnormBlock, VK_FORMAT_BC3_UNORM_BLOCK);
PT_SAME(Format::Bc3SrgbBlock, VK_FORMAT_BC3_SRGB_BLOCK);
PT_SAME(Format::Bc4UnormBlock, VK_FORMAT_BC4_UNORM_BLOCK);
PT_SAME(Format::Bc4SnormBlock, VK_FORMAT_BC4_SNORM_BLOCK);
PT_SAME(Format::Bc5UnormBlock, VK_FORMAT_BC5_UNORM_BLOCK);
PT_SAME(Format::Bc5SnormBlock, VK_FORMAT_BC5_SNORM_BLOCK);
PT_SAME(Format::Bc6hUfloatBlock, VK_FORMAT_BC6H_UFLOAT_BLOCK);
PT_SAME(Format::Bc6hSfloatBlock, VK_FORMAT_BC6H_SFLOAT_BLOCK);
PT_SAME(Format::Bc7UnormBlock, VK_FORMAT_BC7_UNORM_BLOCK);
PT_SAME(Format::Bc7SrgbBlock, VK_FORMAT_BC7_SRGB_BLOCK);

PT_SAME(Layout::Undefined, VK_IMAGE_LAYOUT_UNDEFINED);
PT_SAME(Layout::General, VK_IMAGE_LAYOUT_GENERAL);
PT_SAME(Layout::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
PT_SAME(Layout::DepthStencilAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
PT_SAME(Layout::DepthStencilReadOnly, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
PT_SAME(Layout::ShaderReadOnly, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
PT_SAME(Layout::TransferSrc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
PT_SAME(Layout::TransferDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
PT_SAME(Layout::Preinitialized, VK_IMAGE_LAYOUT_PREINITIALIZED);
PT_SAME(Layout::DepthReadOnly, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
PT_SAME(Layout::DepthAttachment, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
PT_SAME(Layout::PresentSrc, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

PT_SAME(ImageUsage::TransferSrc, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
PT_SAME(ImageUsage::TransferDst, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
PT_SAME(ImageUsage::Sampled, VK_IMAGE_USAGE_SAMPLED_BIT);
PT_SAME(ImageUsage::Storage, VK_IMAGE_USAGE_STORAGE_BIT);
PT_SAME(ImageUsage::ColorAttachment, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
PT_SAME(ImageUsage::DepthStencilAttachment, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

PT_SAME(BufferUsage::TransferSrc, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
PT_SAME(BufferUsage::TransferDst, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
PT_SAME(BufferUsage::UniformBuffer, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
PT_SAME(BufferUsage::StorageBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
PT_SAME(BufferUsage::IndexBuffer, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
PT_SAME(BufferUsage::VertexBuffer, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
PT_SAME(BufferUsage::ShaderDeviceAddress, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
PT_SAME(BufferUsage::AccelerationStructureBuildInput, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
PT_SAME(BufferUsage::AccelerationStructureStorage, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);

PT_SAME(Aspect::Color, VK_IMAGE_ASPECT_COLOR_BIT);
PT_SAME(Aspect::Depth, VK_IMAGE_ASPECT_DEPTH_BIT);
PT_SAME(Aspect::Plane0, VK_IMAGE_ASPECT_PLANE_0_BIT);

PT_SAME(ShaderStage::Vertex, VK_SHADER_STAGE_VERTEX_BIT);
PT_SAME(ShaderStage::Fragment, VK_SHADER_STAGE_FRAGMENT_BIT);
PT_SAME(ShaderStage::Compute, VK_SHADER_STAGE_COMPUTE_BIT);
PT_SAME(ShaderStage::All, VK_SHADER_STAGE_ALL);

PT_SAME(BindingType::Sampler, VK_DESCRIPTOR_TYPE_SAMPLER);
PT_SAME(BindingType::CombinedImageSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
PT_SAME(BindingType::SampledImage, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
PT_SAME(BindingType::StorageImage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
PT_SAME(BindingType::UniformBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
PT_SAME(BindingType::StorageBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
PT_SAME(BindingType::AccelerationStructure, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);

PT_SAME(BindingFlag::UpdateAfterBind, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
PT_SAME(BindingFlag::UpdateUnusedWhilePending, VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT);
PT_SAME(BindingFlag::PartiallyBound, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);

PT_SAME(CompareOp::Never, VK_COMPARE_OP_NEVER);
PT_SAME(CompareOp::Less, VK_COMPARE_OP_LESS);
PT_SAME(CompareOp::Equal, VK_COMPARE_OP_EQUAL);
PT_SAME(CompareOp::LessOrEqual, VK_COMPARE_OP_LESS_OR_EQUAL);
PT_SAME(CompareOp::Greater, VK_COMPARE_OP_GREATER);
PT_SAME(CompareOp::NotEqual, VK_COMPARE_OP_NOT_EQUAL);
PT_SAME(CompareOp::GreaterOrEqual, VK_COMPARE_OP_GREATER_OR_EQUAL);
PT_SAME(CompareOp::Always, VK_COMPARE_OP_ALWAYS);

PT_SAME(CullMode::None, VK_CULL_MODE_NONE);
PT_SAME(CullMode::Front, VK_CULL_MODE_FRONT_BIT);
PT_SAME(CullMode::Back, VK_CULL_MODE_BACK_BIT);
PT_SAME(FrontFace::CounterClockwise, VK_FRONT_FACE_COUNTER_CLOCKWISE);
PT_SAME(FrontFace::Clockwise, VK_FRONT_FACE_CLOCKWISE);

PT_SAME(Filter::Nearest, VK_FILTER_NEAREST);
PT_SAME(Filter::Linear, VK_FILTER_LINEAR);
PT_SAME(MipmapMode::Nearest, VK_SAMPLER_MIPMAP_MODE_NEAREST);
PT_SAME(MipmapMode::Linear, VK_SAMPLER_MIPMAP_MODE_LINEAR);
PT_SAME(AddressMode::Repeat, VK_SAMPLER_ADDRESS_MODE_REPEAT);
PT_SAME(AddressMode::MirroredRepeat, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
PT_SAME(AddressMode::ClampToEdge, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
PT_SAME(AddressMode::ClampToBorder, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
PT_SAME(BorderColor::FloatTransparentBlack, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
PT_SAME(BorderColor::FloatOpaqueBlack, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
PT_SAME(BorderColor::FloatOpaqueWhite, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);

PT_SAME(IndexType::Uint16, VK_INDEX_TYPE_UINT16);
PT_SAME(IndexType::Uint32, VK_INDEX_TYPE_UINT32);
PT_SAME(BindPoint::Graphics, VK_PIPELINE_BIND_POINT_GRAPHICS);
PT_SAME(BindPoint::Compute, VK_PIPELINE_BIND_POINT_COMPUTE);
PT_SAME(LoadOp::Load, VK_ATTACHMENT_LOAD_OP_LOAD);
PT_SAME(LoadOp::Clear, VK_ATTACHMENT_LOAD_OP_CLEAR);
PT_SAME(LoadOp::DontCare, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
PT_SAME(ImageViewType::Type2D, VK_IMAGE_VIEW_TYPE_2D);
PT_SAME(ImageViewType::Type3D, VK_IMAGE_VIEW_TYPE_3D);
PT_SAME(ImageViewType::Cube, VK_IMAGE_VIEW_TYPE_CUBE);
PT_SAME(ImageViewType::Type2DArray, VK_IMAGE_VIEW_TYPE_2D_ARRAY);

PT_SAME(BlendFactor::Zero, VK_BLEND_FACTOR_ZERO);
PT_SAME(BlendFactor::One, VK_BLEND_FACTOR_ONE);
PT_SAME(BlendFactor::SrcColor, VK_BLEND_FACTOR_SRC_COLOR);
PT_SAME(BlendFactor::OneMinusSrcColor, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR);
PT_SAME(BlendFactor::DstColor, VK_BLEND_FACTOR_DST_COLOR);
PT_SAME(BlendFactor::OneMinusDstColor, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR);
PT_SAME(BlendFactor::SrcAlpha, VK_BLEND_FACTOR_SRC_ALPHA);
PT_SAME(BlendFactor::OneMinusSrcAlpha, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
PT_SAME(BlendFactor::DstAlpha, VK_BLEND_FACTOR_DST_ALPHA);
PT_SAME(BlendFactor::OneMinusDstAlpha, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA);
PT_SAME(BlendOp::Add, VK_BLEND_OP_ADD);
PT_SAME(BlendOp::Subtract, VK_BLEND_OP_SUBTRACT);
PT_SAME(BlendOp::ReverseSubtract, VK_BLEND_OP_REVERSE_SUBTRACT);
PT_SAME(BlendOp::Min, VK_BLEND_OP_MIN);
PT_SAME(BlendOp::Max, VK_BLEND_OP_MAX);
PT_SAME(ColorComponent::R, VK_COLOR_COMPONENT_R_BIT);
PT_SAME(ColorComponent::G, VK_COLOR_COMPONENT_G_BIT);
PT_SAME(ColorComponent::B, VK_COLOR_COMPONENT_B_BIT);
PT_SAME(ColorComponent::A, VK_COLOR_COMPONENT_A_BIT);

PT_SAME(PipelineStage::None, VK_PIPELINE_STAGE_2_NONE);
PT_SAME(PipelineStage::TopOfPipe, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
PT_SAME(PipelineStage::FragmentShader, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
PT_SAME(PipelineStage::ColorAttachmentOutput, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
PT_SAME(PipelineStage::ComputeShader, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
PT_SAME(PipelineStage::Transfer, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
PT_SAME(PipelineStage::BottomOfPipe, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
PT_SAME(PipelineStage::Host, VK_PIPELINE_STAGE_2_HOST_BIT);
PT_SAME(PipelineStage::AllCommands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
PT_SAME(PipelineStage::AccelerationStructureBuild, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
PT_SAME(PipelineStage::Copy, VK_PIPELINE_STAGE_2_COPY_BIT);

PT_SAME(Access::None, VK_ACCESS_2_NONE);
PT_SAME(Access::ShaderRead, VK_ACCESS_2_SHADER_READ_BIT);
PT_SAME(Access::ShaderWrite, VK_ACCESS_2_SHADER_WRITE_BIT);
PT_SAME(Access::ColorAttachmentRead, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
PT_SAME(Access::ColorAttachmentWrite, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
PT_SAME(Access::TransferRead, VK_ACCESS_2_TRANSFER_READ_BIT);
PT_SAME(Access::TransferWrite, VK_ACCESS_2_TRANSFER_WRITE_BIT);
PT_SAME(Access::HostRead, VK_ACCESS_2_HOST_READ_BIT);
PT_SAME(Access::MemoryRead, VK_ACCESS_2_MEMORY_READ_BIT);
PT_SAME(Access::MemoryWrite, VK_ACCESS_2_MEMORY_WRITE_BIT);
PT_SAME(Access::ShaderStorageRead, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
PT_SAME(Access::ShaderStorageWrite, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
PT_SAME(Access::AccelerationStructureRead, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
PT_SAME(Access::AccelerationStructureWrite, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
PT_SAME(Access::ShaderSampledRead, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

#undef PT_SAME

static_assert(sizeof(Format) == sizeof(VkFormat) && sizeof(Layout) == sizeof(VkImageLayout));
static_assert(sizeof(PipelineStage) == sizeof(VkPipelineStageFlags2) && sizeof(Access) == sizeof(VkAccessFlags2));
static_assert(sizeof(ImageUsageFlags) == sizeof(VkImageUsageFlags) && sizeof(ColorMask) == sizeof(VkColorComponentFlags));
static_assert(sizeof(CullMode) == sizeof(VkCullModeFlags) && sizeof(BindingType) == sizeof(VkDescriptorType));

static_assert(sizeof(Offset2D) == sizeof(VkOffset2D) && offsetof(Offset2D, y) == offsetof(VkOffset2D, y));
static_assert(sizeof(Extent2D) == sizeof(VkExtent2D) && offsetof(Extent2D, height) == offsetof(VkExtent2D, height));
static_assert(sizeof(Extent3D) == sizeof(VkExtent3D) && offsetof(Extent3D, depth) == offsetof(VkExtent3D, depth));
static_assert(sizeof(Rect2D) == sizeof(VkRect2D) && offsetof(Rect2D, extent) == offsetof(VkRect2D, extent));
static_assert(sizeof(ClearColor) == sizeof(VkClearColorValue));

static_assert(sizeof(ImageHandle) == sizeof(uint64_t) && sizeof(VkImage) <= sizeof(uint64_t));

static_assert(sizeof(decltype(vk::Native(Format::Undefined))) == sizeof(VkFormat));

}
}
