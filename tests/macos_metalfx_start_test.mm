#define VK_USE_PLATFORM_METAL_EXT 1
#import <Metal/Metal.h>
#include "engine/render/upscale/frame_generation.h"

#include <cmath>
#include <cstdio>

#if defined(PT_WITH_METALFX) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
namespace {
id<MTLTexture> Texture(pt::vk::Context& ctx, VkImage image) {
    auto export_metal = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(vkGetDeviceProcAddr(ctx.device, "vkExportMetalObjectsEXT"));
    VkExportMetalTextureInfoEXT texture{VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT};
    texture.image = image;
    texture.plane = VK_IMAGE_ASPECT_PLANE_0_BIT;
    VkExportMetalObjectsInfoEXT objects{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT, &texture};
    export_metal(ctx.device, &objects);
    return texture.mtlTexture;
}

// Feed two identical, lit images through the game's Vulkan/MetalFX producer. Nothing
// overlays the result here, so an unwritten first generated image is visible directly.
bool CheckFirstPair(pt::vk::Context& ctx, pt::FrameGeneration& fg, const pt::vk::Image& depth, const pt::vk::Image& motion) {
    fg.Update(true);
    if (!fg.Generating()) return false;
    auto depth_texture = Texture(ctx, depth.image);
    auto motion_texture = Texture(ctx, motion.image);
    id<MTLCommandQueue> queue = [depth_texture.device newCommandQueue];
    if (!queue || !motion_texture) return false;
    const auto extent = ctx.swapchain.extent;
    for (unsigned frame = 0; frame < 8; ++frame) {
        const auto* target = fg.Present(frame % 2);
        if (!target) return false;
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = Texture(ctx, target->image);
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(2.0, 0.5, 0.25, 1.0);
        pass.colorAttachments[1].texture = motion_texture;
        pass.colorAttachments[1].loadAction = MTLLoadActionClear;
        pass.colorAttachments[1].storeAction = MTLStoreActionStore;
        pass.depthAttachment.texture = depth_texture;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        pass.depthAttachment.clearDepth = 0.5;
        id<MTLCommandBuffer> fill = [queue commandBuffer];
        [[fill renderCommandEncoderWithDescriptor:pass] endEncoding];
        [fill commit];
        [fill waitUntilCompleted];
        if (fill.status != MTLCommandBufferStatusCompleted) return false;
        pt::FrameGenPrepare p;
        p.render = extent;
        p.depth.image = depth.image;
        p.motion.image = motion.image;
        ctx.Submit([&](VkCommandBuffer cmd) { p.cmd = cmd; fg.Prepare(p); });
        VkSemaphore input{};
        uint64_t input_value{};
        if (!fg.InputReadySignal(input, input_value)) return false;
        VkSemaphoreSignalInfo ready{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr, input, input_value};
        if (vkSignalSemaphore(ctx.device, &ready) != VK_SUCCESS) return false;
        const auto* result = fg.GeneratedImage();
        if (!result) continue;
        id<MTLBuffer> pixel = [queue.device newBufferWithLength:256 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> read = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [read blitCommandEncoder];
        [blit copyFromTexture:Texture(ctx, result->image) sourceSlice:0 sourceLevel:0
                 sourceOrigin:MTLOriginMake(extent.width / 2, extent.height / 2, 0) sourceSize:MTLSizeMake(1, 1, 1)
                     toBuffer:pixel destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:256];
        [blit endEncoding];
        [read commit];
        [read waitUntilCompleted];
        if (!pixel || read.status != MTLCommandBufferStatusCompleted) return false;
        const auto* rgba = static_cast<const __fp16*>(pixel.contents);
        std::printf("first MetalFX HDR output RGB %.3f %.3f %.3f (expected 2.000 0.500 0.250)\n",
                    double(rgba[0]), double(rgba[1]), double(rgba[2]));
        return std::abs(rgba[0] - 2.0f) < .05f && std::abs(rgba[1] - .5f) < .02f && std::abs(rgba[2] - .25f) < .01f;
    }
    return false;
}
} // namespace
#endif

int TestFirstMetalFxFrame(pt::vk::Context& ctx) {
#if defined(PT_WITH_METALFX) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
    auto fg = pt::CreateMetalfxFrameGeneration(ctx);
    std::string reason;
    if (!fg || !fg->Available(reason)) {
        std::printf("SKIP first MetalFX frame: %s\n", reason.c_str());
        return 0;
    }
    pt::vk::Image depth, motion;
    const VkExtent3D extent{ctx.swapchain.extent.width, ctx.swapchain.extent.height, 1};
    int failures = 0;
    if (ctx.CreateImage(depth, VK_FORMAT_D32_SFLOAT, extent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        1, 1, VK_IMAGE_ASPECT_DEPTH_BIT) &&
        ctx.CreateImage(motion, VK_FORMAT_R16G16_SFLOAT, extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        for (const char* phase : {"cold start", "re-enable after Settings"}) {
            if (!CheckFirstPair(ctx, *fg, depth, motion)) {
                std::printf("FAIL first generated image at %s\n", phase);
                ++failures;
            }
            fg->Update(false);
        }
    } else {
        ++failures;
    }
    fg.reset();
    ctx.DestroyImage(motion);
    ctx.DestroyImage(depth);
    return failures;
#else
    return 0;
#endif
}
