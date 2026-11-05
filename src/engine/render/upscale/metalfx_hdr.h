#pragma once

#import <Metal/Metal.h>

namespace pt {

// MetalFX interpolates RGB, but does not retain Original's custom HDR-gain alpha.
// Original reconstructs that representation; Enhanced carries signed extended RGB
// directly and restores coverage alpha. Neither path clips the other one's data.
class MetalFxHdrTransport {
public:
    ~MetalFxHdrTransport();
    bool Init(id<MTLDevice> device, NSUInteger width, NSUInteger height, MTLTextureUsage input_usage,
              MTLTextureUsage output_usage);
    void Release();
    bool EncodeInputs(id<MTLCommandBuffer> command, id<MTLTexture> current, id<MTLTexture> previous, float brightness, bool extended_rgb = false);
    bool DecodeOutput(id<MTLCommandBuffer> command, id<MTLTexture> current, id<MTLTexture> previous,
                      id<MTLTexture> output, float brightness, bool extended_rgb = false);
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
