#include <cstdio>
#include <Availability.h>

#if defined(PT_WITH_METALFX) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
#import <MetalFX/MetalFX.h>
#include "engine/render/upscale/metalfx_hdr.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
uint16_t Half(float value) {
    const __fp16 h = value;
    uint16_t bits;
    std::memcpy(&bits, &h, sizeof(bits));
    return bits;
}
float Float(uint16_t bits) {
    __fp16 h;
    std::memcpy(&h, &bits, sizeof(h));
    return h;
}
// Expected display colour is independent of MetalFX and of the transport's choice of alpha encoding.
float Display(float color, float gain, float brightness) {
    return std::pow(std::clamp(std::pow(std::max(color, 0.0f), 1.0f / brightness), 0.0f, 1.0f), 2.2f) *
           std::pow(std::clamp(gain, 1.0f, 8.0f), 2.2f);
}
} // namespace
#endif

int main() {
#if defined(PT_WITH_METALFX) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
    @autoreleasepool {
        if (@available(macOS 26.0, *)) {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device || ![MTLFXFrameInterpolatorDescriptor supportsDevice:device]) {
                std::puts("SKIP MetalFX HDR interpolation: this GPU has no frame interpolator");
                return 0;
            }
            id<MTLCommandQueue> queue = [device newCommandQueue];
            pt::MetalFxHdrTransport hdr;
            int failures = 0, checks = 0;
            const std::array<std::array<float, 4>, 6> colors = {{{1, 1, 1, 1.5f}, {.25f, .5f, .75f, 2},
                {.1f, .04f, .02f, 1}, {0, 0, 0, 1}, {.5f, .5f, .5f, 1.25f}, {1, .75f, .25f, 8}}};
            // Reuse the production transport across an output resize, then switch brightness with history resets.
            for (NSUInteger height : {128u, 192u}) {
                const NSUInteger width = 192;
                MTLFXFrameInterpolatorDescriptor* desc = [MTLFXFrameInterpolatorDescriptor new];
                desc.inputWidth = desc.outputWidth = width;
                desc.inputHeight = desc.outputHeight = height;
                desc.colorTextureFormat = desc.outputTextureFormat = MTLPixelFormatRGBA16Float;
                desc.depthTextureFormat = MTLPixelFormatDepth32Float;
                desc.motionTextureFormat = MTLPixelFormatRG16Float;
                id<MTLFXFrameInterpolator> fx = [desc newFrameInterpolatorWithDevice:device];
                [desc release];
                if (!fx || !hdr.Init(device, width, height, fx.colorTextureUsage, fx.outputTextureUsage)) return 1;
                auto texture = [&](MTLPixelFormat format, MTLTextureUsage usage, bool shared) {
                    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
                    d.usage = usage;
                    d.storageMode = shared ? MTLStorageModeShared : MTLStorageModePrivate;
                    return [device newTextureWithDescriptor:d];
                };
                id<MTLTexture> current = texture(MTLPixelFormatRGBA16Float, MTLTextureUsageShaderRead, true);
                id<MTLTexture> previous = texture(MTLPixelFormatRGBA16Float, MTLTextureUsageShaderRead, true);
                id<MTLTexture> output = texture(MTLPixelFormatRGBA16Float, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite, false);
                id<MTLTexture> motion = texture(MTLPixelFormatRG16Float, fx.motionTextureUsage, true);
                id<MTLTexture> depth = texture(MTLPixelFormatDepth32Float, fx.depthTextureUsage | MTLTextureUsageRenderTarget, false);
                id<MTLBuffer> readback = [device newBufferWithLength:width*height*8 options:MTLResourceStorageModeShared];
                if (!queue || !current || !previous || !output || !motion || !depth || !readback) return 1;
                std::vector<uint16_t> pixels(width*height*4), zero_motion(width*height*2, 0);
                for (NSUInteger y = 0; y < height; ++y) for (NSUInteger x = 0; x < width; ++x) {
                    const auto& color = colors[(y/(height/2))*3+x/64];
                    for (int c = 0; c < 4; ++c) pixels[(y*width+x)*4+c] = Half(color[c]);
                }
                const MTLRegion region = MTLRegionMake2D(0, 0, width, height);
                [current replaceRegion:region mipmapLevel:0 withBytes:pixels.data() bytesPerRow:width*8];
                [previous replaceRegion:region mipmapLevel:0 withBytes:pixels.data() bytesPerRow:width*8];
                [motion replaceRegion:region mipmapLevel:0 withBytes:zero_motion.data() bytesPerRow:width*4];
                id<MTLCommandBuffer> init = [queue commandBuffer];
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.depthAttachment.texture = depth;
                pass.depthAttachment.loadAction = MTLLoadActionClear;
                pass.depthAttachment.storeAction = MTLStoreActionStore;
                pass.depthAttachment.clearDepth = 0.5;
                [[init renderCommandEncoderWithDescriptor:pass] endEncoding];
                [init commit]; [init waitUntilCompleted];
                fx.colorTexture = hdr.Current(); fx.prevColorTexture = hdr.Previous(); fx.outputTexture = hdr.Interpolated();
                fx.depthTexture = depth; fx.motionTexture = motion;
                fx.motionVectorScaleX = width; fx.motionVectorScaleY = height;
                fx.deltaTime = 1.0f/30.0f; fx.nearPlane = .05f; fx.farPlane = 100000;
                fx.fieldOfView = 60; fx.aspectRatio = float(width)/height;
                for (float brightness : {.5f, 1.0f, 2.0f}) {
                    const int before = failures;
                    for (int frame = 0; frame < 8; ++frame) {
                        id<MTLCommandBuffer> command = [queue commandBuffer];
                        if (!hdr.EncodeInputs(command, current, previous, brightness)) return 1;
                        fx.shouldResetHistory = frame == 0;
                        [fx encodeToCommandBuffer:command];
                        if (!hdr.DecodeOutput(command, current, previous, output, brightness)) return 1;
                        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
                        [blit copyFromTexture:output sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                            sourceSize:MTLSizeMake(width,height,1) toBuffer:readback destinationOffset:0
                            destinationBytesPerRow:width*8 destinationBytesPerImage:width*height*8];
                        [blit endEncoding]; [command commit]; [command waitUntilCompleted];
                        if (command.status == MTLCommandBufferStatusError) {
                            std::fprintf(stderr, "MetalFX HDR GPU failure: %s\n", command.error.description.UTF8String);
                            return 1;
                        }
                        if (frame < 4) continue; // Verify the interpolator after the alpha-preserving warm-up ends.
                        const auto* result = static_cast<const uint16_t*>(readback.contents);
                        for (size_t i = 0; i < colors.size(); ++i) {
                            const NSUInteger x = (i%3)*64+32, y = (i/3)*(height/2)+height/4;
                            const auto* pixel = result+(y*width+x)*4;
                            const float gain = Float(pixel[3]);
                            bool good = std::isfinite(gain) && gain >= 1 && gain <= 8;
                            for (int c = 0; c < 3; ++c) {
                                const float expected = Display(colors[i][c], colors[i][3], brightness);
                                const float actual = Display(Float(pixel[c]), gain, brightness);
                                good = good && std::isfinite(actual) && std::abs(expected-actual) <= .02f*std::max(expected, .01f);
                                // Static images must also preserve the grain/UI input split, not only total brightness.
                                good = good && std::abs(Float(pixel[c])-colors[i][c]) < .012f;
                            }
                            ++checks;
                            if (!good) {
                                ++failures;
                                std::printf("FAIL %lux%lu brightness %.1f frame %d patch %zu: rgba %.5f %.5f %.5f %.5f\n",
                                            width,height,brightness,frame,i,Float(pixel[0]),Float(pixel[1]),Float(pixel[2]),gain);
                            }
                        }
                    }
                    std::printf("%s MetalFX HDR %lux%lu brightness %.1f: white, colour, dark, fade and maximum gain after warm-up\n",
                                failures==before ? "PASS" : "FAIL",width,height,brightness);
                }
                [fx release]; [current release]; [previous release]; [output release];
                [motion release]; [depth release]; [readback release];
            }
            std::printf("MetalFX HDR transport: %d checks, %d failures\n",checks,failures);
            [queue release]; [device release];
            return failures ? 1 : 0;
        }
    }
#endif
    std::puts("SKIP MetalFX HDR interpolation: requires a macOS 26 SDK and runtime with MetalFX enabled");
    return 0;
}
