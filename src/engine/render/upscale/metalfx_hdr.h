#pragma once

#import <Metal/Metal.h>

namespace pt {

// Both HDR pipelines store highlights in extended RGB. Keep those channels intact
// through interpolation and restore opaque alpha before compositing the HUD.
class MetalFxHdrTransport {
public:
    ~MetalFxHdrTransport();
    bool Init(id<MTLDevice> device, NSUInteger width, NSUInteger height, MTLTextureUsage input_usage,
              MTLTextureUsage output_usage);
    void Release();
    bool EncodeInputs(id<MTLCommandBuffer> command, id<MTLTexture> current, id<MTLTexture> previous);
    bool DecodeOutput(id<MTLCommandBuffer> command, id<MTLTexture> output);
    id<MTLTexture> Current() const { return current_; }
    id<MTLTexture> Previous() const { return previous_; }
    id<MTLTexture> Interpolated() const { return interpolated_; }

private:
    id<MTLComputePipelineState> pack_ = nil;
    id<MTLComputePipelineState> unpack_ = nil;
    id<MTLTexture> current_ = nil;
    id<MTLTexture> previous_ = nil;
    id<MTLTexture> interpolated_ = nil;
    id<MTLSharedEvent> barrier_ = nil;
    uint64_t barrier_value_ = 0;
};

} // namespace pt
