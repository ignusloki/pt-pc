#pragma once

#import <Metal/Metal.h>

namespace pt {

// MetalFX interpolates RGB, but does not retain the renderer's custom HDR-gain alpha.
// Transport display-encoded HDR colour in RGB, then rebuild the compositor's input.
class MetalFxHdrTransport {
public:
    ~MetalFxHdrTransport();
    bool Init(id<MTLDevice> device, NSUInteger width, NSUInteger height, MTLTextureUsage input_usage,
              MTLTextureUsage output_usage);
    void Release();
    bool EncodeInputs(id<MTLCommandBuffer> command, id<MTLTexture> current, id<MTLTexture> previous, float brightness);
    bool DecodeOutput(id<MTLCommandBuffer> command, id<MTLTexture> current, id<MTLTexture> previous,
                      id<MTLTexture> output, float brightness);
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
