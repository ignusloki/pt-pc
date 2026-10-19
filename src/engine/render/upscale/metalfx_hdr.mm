#include "engine/render/upscale/metalfx_hdr.h"

#include <algorithm>

#include "engine/core/log.h"

namespace pt {
namespace {
constexpr const char* kShader = R"(
#include <metal_stdlib>
using namespace metal;

float3 hdr_color(float4 value, float brightness) {
    // Keep the display transfer here: linear-light half floats lose dark channels at low brightness.
    return pow(clamp(value.rgb, 0.0f, 1.0f), float3(1.0f / brightness)) * clamp(value.a, 1.0f, 8.0f);
}

kernel void pack_hdr(texture2d<float, access::read> current [[texture(0)]],
                     texture2d<float, access::read> previous [[texture(1)]],
                     texture2d<float, access::write> packed_current [[texture(2)]],
                     texture2d<float, access::write> packed_previous [[texture(3)]],
                     constant float& brightness [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {
    if (p.x >= current.get_width() || p.y >= current.get_height()) return;
    packed_current.write(float4(hdr_color(current.read(p), brightness), 1.0f), p);
    packed_previous.write(float4(hdr_color(previous.read(p), brightness), 1.0f), p);
}

kernel void unpack_hdr(texture2d<float, access::read> interpolated [[texture(0)]],
                       texture2d<float, access::read> current [[texture(1)]],
                       texture2d<float, access::read> previous [[texture(2)]],
                       texture2d<float, access::write> output [[texture(3)]],
                       constant float& brightness [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {
    if (p.x >= output.get_width() || p.y >= output.get_height()) return;
    float3 color = max(interpolated.read(p).rgb, 0.0f);
    // Retain the original split for a static image, including the existing grain/UI behaviour.
    // This reference only chooses an encoding: the interpolated RGB carries all HDR brightness.
    float reference = 0.5f * (clamp(current.read(p).a, 1.0f, 8.0f) + clamp(previous.read(p).a, 1.0f, 8.0f));
    float required = max(color.r, max(color.g, color.b));
    float gain = clamp(max(reference, required), 1.0f, 8.0f);
    float3 rgb = pow(clamp(color / gain, 0.0f, 1.0f), float3(brightness));
    output.write(float4(rgb, gain), p);
})";

void Dispatch(id<MTLComputeCommandEncoder> encoder, id<MTLComputePipelineState> pipeline, id<MTLTexture> output) {
    [encoder setComputePipelineState:pipeline];
    [encoder dispatchThreads:MTLSizeMake(output.width, output.height, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
    [encoder endEncoding];
}
} // namespace

MetalFxHdrTransport::~MetalFxHdrTransport() { Release(); }

void MetalFxHdrTransport::Release() {
    [pack_ release]; pack_ = nil;
    [unpack_ release]; unpack_ = nil;
    [current_ release]; current_ = nil;
    [previous_ release]; previous_ = nil;
    [interpolated_ release]; interpolated_ = nil;
    [barrier_ release]; barrier_ = nil;
    barrier_value_ = 0;
}

bool MetalFxHdrTransport::Init(id<MTLDevice> device, NSUInteger width, NSUInteger height, MTLTextureUsage input_usage,
                             MTLTextureUsage output_usage) {
    if (current_ && current_.width == width && current_.height == height) return true;
    Release();
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:kShader] options:nil error:&error];
    id<MTLFunction> pack = [library newFunctionWithName:@"pack_hdr"];
    id<MTLFunction> unpack = [library newFunctionWithName:@"unpack_hdr"];
    if (pack) pack_ = [device newComputePipelineStateWithFunction:pack error:&error];
    if (unpack) unpack_ = [device newComputePipelineStateWithFunction:unpack error:&error];
    [pack release]; [unpack release]; [library release];
    auto texture = [&](MTLTextureUsage usage) {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                  width:width height:height mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = usage | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        return [device newTextureWithDescriptor:desc];
    };
    current_ = texture(input_usage);
    previous_ = texture(input_usage);
    interpolated_ = texture(output_usage);
    barrier_ = [device newSharedEvent];
    if (!pack_ || !unpack_ || !current_ || !previous_ || !interpolated_ || !barrier_) {
        LogError("frame generation: HDR colour transport unavailable: {}", error ? error.description.UTF8String : "Metal allocation failed");
        Release();
        return false;
    }
    return true;
}

bool MetalFxHdrTransport::EncodeInputs(id<MTLCommandBuffer> command, id<MTLTexture> current, id<MTLTexture> previous, float brightness) {
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder || !pack_) { [encoder endEncoding]; return false; }
    brightness = std::clamp(brightness, 0.1f, 4.0f);
    [encoder setTexture:current atIndex:0]; [encoder setTexture:previous atIndex:1];
    [encoder setTexture:current_ atIndex:2]; [encoder setTexture:previous_ atIndex:3];
    [encoder setBytes:&brightness length:sizeof(brightness) atIndex:0];
    Dispatch(encoder, pack_, current_);
    return true;
}

bool MetalFxHdrTransport::DecodeOutput(id<MTLCommandBuffer> command, id<MTLTexture> current, id<MTLTexture> previous,
                                     id<MTLTexture> output, float brightness) {
    // Exported Vulkan textures may have untracked hazards; order all MetalFX writes before the conversion.
    [command encodeSignalEvent:barrier_ value:++barrier_value_];
    [command encodeWaitForEvent:barrier_ value:barrier_value_];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder || !unpack_) { [encoder endEncoding]; return false; }
    brightness = std::clamp(brightness, 0.1f, 4.0f);
    [encoder setTexture:interpolated_ atIndex:0];
    [encoder setTexture:current atIndex:1]; [encoder setTexture:previous atIndex:2];
    [encoder setTexture:output atIndex:3];
    [encoder setBytes:&brightness length:sizeof(brightness) atIndex:0];
    Dispatch(encoder, unpack_, output);
    return true;
}
} // namespace pt
