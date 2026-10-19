#pragma once

#include <cstdint>
#include <type_traits>

struct VkExtent2D;
struct VkExtent3D;

namespace pt::gpu {

enum class Format : uint32_t {
    Undefined = 0,
    R8Unorm = 9,
    R8G8Unorm = 16,
    R8G8B8A8Unorm = 37,
    R8G8B8A8Uint = 41,
    R8G8B8A8Srgb = 43,
    B8G8R8A8Unorm = 44,
    B8G8R8A8Srgb = 50,
    A2B10G10R10UnormPack32 = 64,
    R16Sfloat = 76,
    R16G16Sfloat = 83,
    R16G16B16A16Sfloat = 97,
    R32Uint = 98,
    R32Sfloat = 100,
    R32G32Sfloat = 103,
    R32G32B32Sfloat = 106,
    R32G32B32A32Sfloat = 109,
    D32Sfloat = 126,
    Bc1RgbUnormBlock = 131,
    Bc1RgbSrgbBlock = 132,
    Bc1RgbaUnormBlock = 133,
    Bc1RgbaSrgbBlock = 134,
    Bc2UnormBlock = 135,
    Bc2SrgbBlock = 136,
    Bc3UnormBlock = 137,
    Bc3SrgbBlock = 138,
    Bc4UnormBlock = 139,
    Bc4SnormBlock = 140,
    Bc5UnormBlock = 141,
    Bc5SnormBlock = 142,
    Bc6hUfloatBlock = 143,
    Bc6hSfloatBlock = 144,
    Bc7UnormBlock = 145,
    Bc7SrgbBlock = 146,
};

enum class Layout : uint32_t {
    Undefined = 0,
    General = 1,
    ColorAttachment = 2,
    DepthStencilAttachment = 3,
    DepthStencilReadOnly = 4,
    ShaderReadOnly = 5,
    TransferSrc = 6,
    TransferDst = 7,
    Preinitialized = 8,
    DepthReadOnly = 1000241001,
    DepthAttachment = 1000241000,
    PresentSrc = 1000001002,
};

template <class E>
struct Flags {
    uint32_t bits = 0;
    constexpr Flags() = default;
    constexpr Flags(E e) : bits(static_cast<uint32_t>(e)) {}
    constexpr explicit Flags(uint32_t b) : bits(b) {}
    constexpr Flags operator|(Flags o) const { return Flags(bits | o.bits); }
    constexpr Flags operator&(Flags o) const { return Flags(bits & o.bits); }
    constexpr Flags& operator|=(Flags o) {
        bits |= o.bits;
        return *this;
    }
    constexpr bool Has(E e) const { return (bits & static_cast<uint32_t>(e)) == static_cast<uint32_t>(e); }
    constexpr explicit operator bool() const { return bits != 0; }
    constexpr bool operator==(const Flags&) const = default;
};

template <class E>
struct IsFlagBits : std::false_type {};

template <class E>
    requires IsFlagBits<E>::value
constexpr Flags<E> operator|(E a, E b) {
    return Flags<E>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

enum class ImageUsage : uint32_t {
    TransferSrc = 0x1,
    TransferDst = 0x2,
    Sampled = 0x4,
    Storage = 0x8,
    ColorAttachment = 0x10,
    DepthStencilAttachment = 0x20,
};
using ImageUsageFlags = Flags<ImageUsage>;
template <>
struct IsFlagBits<ImageUsage> : std::true_type {};

enum class BufferUsage : uint32_t {
    TransferSrc = 0x1,
    TransferDst = 0x2,
    UniformBuffer = 0x10,
    StorageBuffer = 0x20,
    IndexBuffer = 0x40,
    VertexBuffer = 0x80,
    ShaderDeviceAddress = 0x20000,
    AccelerationStructureBuildInput = 0x80000,
    AccelerationStructureStorage = 0x100000,
};
using BufferUsageFlags = Flags<BufferUsage>;
template <>
struct IsFlagBits<BufferUsage> : std::true_type {};

enum class Aspect : uint32_t {
    Color = 0x1,
    Depth = 0x2,
    Plane0 = 0x10,
};
using AspectFlags = Flags<Aspect>;
template <>
struct IsFlagBits<Aspect> : std::true_type {};

enum class ShaderStage : uint32_t {
    Vertex = 0x1,
    Fragment = 0x10,
    Compute = 0x20,
    All = 0x7FFFFFFF,
};
using ShaderStageFlags = Flags<ShaderStage>;
template <>
struct IsFlagBits<ShaderStage> : std::true_type {};

enum class BindingType : uint32_t {
    Sampler = 0,
    CombinedImageSampler = 1,
    SampledImage = 2,
    StorageImage = 3,
    UniformBuffer = 6,
    StorageBuffer = 7,
    AccelerationStructure = 1000150000,
};

enum class BindingFlag : uint32_t {
    UpdateAfterBind = 0x1,
    UpdateUnusedWhilePending = 0x2,
    PartiallyBound = 0x4,
};
using BindingFlags = Flags<BindingFlag>;
template <>
struct IsFlagBits<BindingFlag> : std::true_type {};

enum class CompareOp : uint32_t {
    Never = 0,
    Less = 1,
    Equal = 2,
    LessOrEqual = 3,
    Greater = 4,
    NotEqual = 5,
    GreaterOrEqual = 6,
    Always = 7,
};

enum class CullMode : uint32_t {
    None = 0,
    Front = 0x1,
    Back = 0x2,
};

enum class FrontFace : uint32_t {
    CounterClockwise = 0,
    Clockwise = 1,
};

enum class Filter : uint32_t {
    Nearest = 0,
    Linear = 1,
};

enum class MipmapMode : uint32_t {
    Nearest = 0,
    Linear = 1,
};

enum class AddressMode : uint32_t {
    Repeat = 0,
    MirroredRepeat = 1,
    ClampToEdge = 2,
    ClampToBorder = 3,
};

enum class BorderColor : uint32_t {
    FloatTransparentBlack = 0,
    FloatOpaqueBlack = 2,
    FloatOpaqueWhite = 4,
};

enum class IndexType : uint32_t {
    Uint16 = 0,
    Uint32 = 1,
};

enum class BindPoint : uint32_t {
    Graphics = 0,
    Compute = 1,
};

enum class LoadOp : uint32_t {
    Load = 0,
    Clear = 1,
    DontCare = 2,
};

enum class ImageViewType : uint32_t {
    Type2D = 1,
    Type3D = 2,
    Cube = 3,
    Type2DArray = 5,
};

enum class ColorComponent : uint32_t {
    R = 0x1,
    G = 0x2,
    B = 0x4,
    A = 0x8,
};
using ColorMask = Flags<ColorComponent>;
template <>
struct IsFlagBits<ColorComponent> : std::true_type {};
constexpr ColorMask kColorMaskAll = ColorMask(0xFu);

enum class BlendFactor : uint32_t {
    Zero = 0,
    One = 1,
    SrcColor = 2,
    OneMinusSrcColor = 3,
    DstColor = 4,
    OneMinusDstColor = 5,
    SrcAlpha = 6,
    OneMinusSrcAlpha = 7,
    DstAlpha = 8,
    OneMinusDstAlpha = 9,
};

enum class BlendOp : uint32_t {
    Add = 0,
    Subtract = 1,
    ReverseSubtract = 2,
    Min = 3,
    Max = 4,
};

enum class PipelineStage : uint64_t {
    None = 0,
    TopOfPipe = 0x1,
    FragmentShader = 0x80,
    ColorAttachmentOutput = 0x400,
    ComputeShader = 0x800,
    Transfer = 0x1000,
    BottomOfPipe = 0x2000,
    Host = 0x4000,
    AllCommands = 0x10000,
    AccelerationStructureBuild = 0x2000000,
    Copy = 0x100000000ull,
};

constexpr PipelineStage operator|(PipelineStage a, PipelineStage b) {
    return static_cast<PipelineStage>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

enum class Access : uint64_t {
    None = 0,
    ShaderRead = 0x20,
    ShaderWrite = 0x40,
    ColorAttachmentRead = 0x80,
    ColorAttachmentWrite = 0x100,
    TransferRead = 0x800,
    TransferWrite = 0x1000,
    HostRead = 0x2000,
    MemoryRead = 0x8000,
    MemoryWrite = 0x10000,
    AccelerationStructureRead = 0x200000,
    AccelerationStructureWrite = 0x400000,
    ShaderSampledRead = 0x100000000ull,
    ShaderStorageRead = 0x200000000ull,
    ShaderStorageWrite = 0x400000000ull,
};

constexpr uint64_t Bits(Access a) {
    return static_cast<uint64_t>(a);
}
constexpr uint64_t operator|(Access a, Access b) {
    return static_cast<uint64_t>(a) | static_cast<uint64_t>(b);
}

struct Offset2D {
    int32_t x = 0;
    int32_t y = 0;
};

struct Extent2D {
    uint32_t width = 0;
    uint32_t height = 0;
    constexpr Extent2D() = default;
    constexpr Extent2D(uint32_t w, uint32_t h) : width(w), height(h) {}
    constexpr bool operator==(const Extent2D&) const = default;
};

struct Extent3D {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    constexpr bool operator==(const Extent3D&) const = default;
};

struct Rect2D {
    Offset2D offset;
    Extent2D extent;
};

union ClearColor {
    float float32[4];
    int32_t int32[4];
    uint32_t uint32[4];
};

struct FormatInfo {
    uint32_t bytes = 0;
    bool block = false;
    bool depth = false;
    bool srgb = false;
};

constexpr FormatInfo Describe(Format f) {
    switch (f) {
    case Format::R8Unorm: return {1};
    case Format::R8G8Unorm: return {2};
    case Format::R8G8B8A8Unorm:
    case Format::R8G8B8A8Uint:
    case Format::B8G8R8A8Unorm:
    case Format::A2B10G10R10UnormPack32: return {4};
    case Format::R8G8B8A8Srgb:
    case Format::B8G8R8A8Srgb: return {4, false, false, true};
    case Format::R16Sfloat: return {2};
    case Format::R16G16Sfloat: return {4};
    case Format::R16G16B16A16Sfloat: return {8};
    case Format::R32Uint:
    case Format::R32Sfloat: return {4};
    case Format::R32G32Sfloat: return {8};
    case Format::R32G32B32Sfloat: return {12};
    case Format::R32G32B32A32Sfloat: return {16};
    case Format::D32Sfloat: return {4, false, true};
    case Format::Bc1RgbUnormBlock:
    case Format::Bc1RgbaUnormBlock:
    case Format::Bc4UnormBlock:
    case Format::Bc4SnormBlock: return {8, true};
    case Format::Bc1RgbSrgbBlock:
    case Format::Bc1RgbaSrgbBlock: return {8, true, false, true};
    case Format::Bc2UnormBlock:
    case Format::Bc3UnormBlock:
    case Format::Bc5UnormBlock:
    case Format::Bc5SnormBlock:
    case Format::Bc6hUfloatBlock:
    case Format::Bc6hSfloatBlock:
    case Format::Bc7UnormBlock: return {16, true};
    case Format::Bc2SrgbBlock:
    case Format::Bc3SrgbBlock:
    case Format::Bc7SrgbBlock: return {16, true, false, true};
    case Format::Undefined: break;
    }
    return {};
}

}
