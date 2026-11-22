#include "engine/render/upscale/metalfx_hdr.h"

#include "engine/core/log.h"

namespace pt {
namespace {
constexpr const char* kShader = R"(
#include <metal_stdlib>
using namespace metal;
kernel void pack_hdr(texture2d<float, access::read> current [[texture(0)]],
                     texture2d<float, access::read> previous [[texture(1)]],
                     texture2d<float, access::write> packed_current [[texture(2)]],
                     texture2d<float, access::write> packed_previous [[texture(3)]],
                     uint2 p [[thread_position_in_grid]]) {
    if (p.x >= current.get_width() || p.y >= current.get_height()) return;
    packed_current.write(float4(current.read(p).rgb, 1.0f), p);
    packed_previous.write(float4(previous.read(p).rgb, 1.0f), p);
}

kernel void unpack_hdr(texture2d<float, access::read> interpolated [[texture(0)]],
                       texture2d<float, access::write> output [[texture(1)]],
                       uint2 p [[thread_position_in_grid]]) {
    if (p.x >= output.get_width() || p.y >= output.get_height()) return;
    output.write(float4(interpolated.read(p).rgb, 1.0f), p);
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

bool MetalFxHdrTransport::EncodeInputs(id<MTLCommandBuffer> command, id<MTLTexture> current, id<MTLTexture> previous) {
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder || !pack_) { [encoder endEncoding]; return false; }
    [encoder setTexture:current atIndex:0]; [encoder setTexture:previous atIndex:1];
    [encoder setTexture:current_ atIndex:2]; [encoder setTexture:previous_ atIndex:3];
    Dispatch(encoder, pack_, current_);
    return true;
}

bool MetalFxHdrTransport::DecodeOutput(id<MTLCommandBuffer> command, id<MTLTexture> output) {
    // Exported Vulkan textures may have untracked hazards; order all MetalFX writes before the conversion.
    [command encodeSignalEvent:barrier_ value:++barrier_value_];
    [command encodeWaitForEvent:barrier_ value:barrier_value_];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder || !unpack_) { [encoder endEncoding]; return false; }
    [encoder setTexture:interpolated_ atIndex:0];
    [encoder setTexture:output atIndex:1];
    Dispatch(encoder, unpack_, output);
    return true;
}
} // namespace pt
