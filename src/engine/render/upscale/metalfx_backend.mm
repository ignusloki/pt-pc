#include "engine/render/upscale/upscale.h"

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include <vulkan/vulkan_metal.h>

#include <atomic>
#include <memory>
#include <string>

#include "engine/core/log.h"
#include "engine/render/renderer.h"

namespace pt {
namespace {

struct TimelineSync {
    VkDevice device = VK_NULL_HANDLE;
    VkSemaphore inputs_ready = VK_NULL_HANDLE;
    VkSemaphore output_ready = VK_NULL_HANDLE;
    id<MTLSharedEvent> mtl_inputs_ready;
    id<MTLSharedEvent> mtl_output_ready;
    std::atomic<bool> failed{false};

    ~TimelineSync() {
        if (inputs_ready) vkDestroySemaphore(device, inputs_ready, nullptr);
        if (output_ready) vkDestroySemaphore(device, output_ready, nullptr);
    }
};

struct ScalerKey {
    MTLPixelFormat color = MTLPixelFormatInvalid;
    MTLPixelFormat depth = MTLPixelFormatInvalid;
    MTLPixelFormat motion = MTLPixelFormatInvalid;
    MTLPixelFormat output = MTLPixelFormatInvalid;
    NSUInteger input_width = 0;
    NSUInteger input_height = 0;
    NSUInteger output_width = 0;
    NSUInteger output_height = 0;
    bool operator==(const ScalerKey&) const = default;
};

// MetalFX takes exposure as a 1x1 R16Float texture; the scene's exposure image is R32_SFLOAT and shared with the other upscalers.
constexpr const char* kExposureShader = R"(
#include <metal_stdlib>
using namespace metal;
kernel void exposure_to_half(texture2d<float, access::read> source [[texture(0)]],
                             texture2d<half, access::write> target [[texture(1)]]) {
    target.write(half4(half(source.read(uint2(0, 0)).r)), uint2(0, 0));
}
)";

/* Vulkan submits the scene and signals inputs_ready; Metal waits for that value, upscales, and signals output_ready. A second Vulkan
   submission waits for that output before resolving and presenting. Timeline values are never reset or reused. */
class MetalFxBackend final : public UpscaleBackend {
public:
    explicit MetalFxBackend(vk::Context& ctx) : ctx_(ctx) {}
    ~MetalFxBackend() override { Release(); }

    UpscalerKind Kind() const override { return UpscalerKind::MetalFx; }

    bool Supported(std::string& reason) override {
        if (!UpscaleHost::Get().Enabled(VK_EXT_METAL_OBJECTS_EXTENSION_NAME, true)) {
            reason = "MoltenVK does not offer VK_EXT_metal_objects";
            return false;
        }
        reason.clear();
        return true;
    }

    bool Available(std::string& reason) override {
        if (!Supported(reason)) {
            return false;
        }
        export_objects_ = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(vkGetDeviceProcAddr(ctx_.device, "vkExportMetalObjectsEXT"));
        if (!export_objects_) {
            reason = "vkExportMetalObjectsEXT is missing";
            return false;
        }
        VkExportMetalDeviceInfoEXT device_info{VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT};
        VkExportMetalObjectsInfoEXT info{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT, &device_info};
        export_objects_(ctx_.device, &info);
        device_ = device_info.mtlDevice;
        if (!device_ || ![MTLFXTemporalScalerDescriptor supportsDevice:device_]) {
            reason = "this GPU has no MetalFX temporal scaler";
            return false;
        }
        if (!CreateExposureConversion(reason)) {
            return false;
        }
        if (!sync_ && !CreateTimelines(reason)) {
            return false;
        }
        queue_ = [device_ newCommandQueue];
        queue_.label = @"MetalFX upscale";
        reason.clear();
        return true;
    }

    bool Create(VkCommandBuffer, const UpscaleCreate& create) override {
        Release();
        // The scaler needs the depth and motion formats, which arrive with the first dispatch.
        LogInfo("metalfx: temporal scaler for {}x{} -> {}x{}", create.render.width, create.render.height, create.display.width,
                create.display.height);
        return queue_ != nil;
    }

    bool Dispatch(const UpscaleDispatch& d) override {
        @autoreleasepool {
            return Encode(d);
        }
    }

    void Release() override {
        // A recorded frame may be abandoned; its Metal command buffer is not committed until Vulkan submits the inputs.
        if (last_cb_.status >= MTLCommandBufferStatusCommitted) {
            [last_cb_ waitUntilCompleted];
        }
        scaler_ = nil;
        staging_ = nil;
        last_cb_ = nil;
        key_ = {};
        failed_ = false;
        dispatches_ = 0;
        if (sync_) sync_->failed.store(false);
    }

private:
    bool Encode(const UpscaleDispatch& d) {
        if (!d.renderer || !sync_ || sync_->failed.load()) {
            return false;
        }
        id<MTLTexture> color = Texture(d.color);
        id<MTLTexture> depth = Texture(d.depth);
        id<MTLTexture> motion = Texture(d.motion);
        id<MTLTexture> output = Texture(d.output);
        id<MTLTexture> exposure = Texture(d.exposure);
        if (!color || !depth || !motion || !output) {
            LogError("metalfx: a Vulkan image has no Metal texture");
            return false;
        }
        const ScalerKey key{color.pixelFormat, depth.pixelFormat, motion.pixelFormat, output.pixelFormat,
                            color.width, color.height, d.display.width, d.display.height};
        id<MTLFXTemporalScaler> scaler = Scaler(key);
        if (!scaler) {
            return false;
        }
        const auto sync = sync_;
        const uint64_t value = ++next_value_;
        id<MTLCommandBuffer> cb = [queue_ commandBuffer];
        id<MTLTexture> target = OutputTarget(scaler, output, d.display);
        if (!cb || !target) {
            LogError("metalfx: cannot allocate the command buffer or output texture");
            return false;
        }
        [cb encodeWaitForEvent:sync->mtl_inputs_ready value:value];
        if (exposure) {
            id<MTLComputeCommandEncoder> convert = [cb computeCommandEncoder];
            [convert setComputePipelineState:exposure_pipeline_];
            [convert setTexture:exposure atIndex:0];
            [convert setTexture:exposure_half_ atIndex:1];
            [convert dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
            [convert endEncoding];
        }
        scaler.colorTexture = color;
        scaler.depthTexture = depth;
        scaler.motionTexture = motion;
        scaler.outputTexture = target;
        scaler.exposureTexture = exposure ? exposure_half_ : nil;
        scaler.inputContentWidth = std::min<NSUInteger>(d.render.width, color.width);
        scaler.inputContentHeight = std::min<NSUInteger>(d.render.height, color.height);
        scaler.jitterOffsetX = d.jitter.x;
        scaler.jitterOffsetY = d.jitter.y;
        scaler.motionVectorScaleX = d.motion_scale.x;
        scaler.motionVectorScaleY = d.motion_scale.y;
        scaler.reset = d.reset || dispatches_ == 0;
        scaler.depthReversed = YES;  // the scene uses reversed infinite depth (FSR is created with DEPTH_INVERTED)
        scaler.preExposure = d.pre_exposure;
        [scaler encodeToCommandBuffer:cb];
        if (target != output) {
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(d.display.width, d.display.height, 1) toTexture:output destinationSlice:0
                 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
        }
        [cb encodeSignalEvent:sync->mtl_output_ready value:value];
        // The handler keeps the scaler and target alive until the GPU is done with them, even if the backend replaces them first.
        [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
            (void)scaler;
            (void)target;
            if (done.error) {
                LogError("metalfx: upscale failed on the GPU: {}", done.error.localizedDescription.UTF8String);
                sync->failed.store(true);
                // Failed GPU work must not leave Vulkan waiting forever. Future dispatches use the existing resolve fallback.
                if (sync->mtl_output_ready.signaledValue < value) sync->mtl_output_ready.signaledValue = value;
            }
        }];
        if (!d.renderer->QueueHandoff(d.cmd, sync->inputs_ready, value, sync->output_ready, value, [cb, sync] {
                (void)sync;  // keep the semaphores alive until this frame's fence retires its submissions
                [cb commit];
            })) {
            return false;
        }
        last_cb_ = cb;
        if (dispatches_++ == 0) LogInfo("metalfx: timeline semaphore queue handoff enabled");
        return true;
    }

    bool CreateExposureConversion(std::string& reason) {
        NSError* error = nil;
        id<MTLLibrary> library = [device_ newLibraryWithSource:@(kExposureShader) options:nil error:&error];
        id<MTLFunction> function = [library newFunctionWithName:@"exposure_to_half"];
        exposure_pipeline_ = function ? [device_ newComputePipelineStateWithFunction:function error:&error] : nil;
        if (!exposure_pipeline_) {
            reason = std::string("cannot build the exposure conversion: ") + (error ? error.localizedDescription.UTF8String : "no function");
            return false;
        }
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Float width:1 height:1 mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        desc.storageMode = MTLStorageModePrivate;
        exposure_half_ = [device_ newTextureWithDescriptor:desc];
        return exposure_half_ != nil;
    }

    id<MTLTexture> Texture(const UpscaleImage& image) const {
        if (!image.Valid()) {
            return nil;
        }
        VkExportMetalTextureInfoEXT texture{VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT};
        texture.image = image.image;
        texture.plane = VK_IMAGE_ASPECT_PLANE_0_BIT;
        VkExportMetalObjectsInfoEXT info{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT, &texture};
        export_objects_(ctx_.device, &info);
        return texture.mtlTexture;
    }

    id<MTLFXTemporalScaler> Scaler(const ScalerKey& key) {
        if (scaler_ && key_ == key) {
            return scaler_;
        }
        if (failed_ && key_ == key) {
            return nil;  // no synchronous rebuild every frame
        }
        MTLFXTemporalScalerDescriptor* desc = [MTLFXTemporalScalerDescriptor new];
        desc.colorTextureFormat = key.color;
        desc.depthTextureFormat = key.depth;
        desc.motionTextureFormat = key.motion;
        desc.outputTextureFormat = key.output;
        desc.inputWidth = key.input_width;
        desc.inputHeight = key.input_height;
        desc.outputWidth = key.output_width;
        desc.outputHeight = key.output_height;
        desc.autoExposureEnabled = NO;
        desc.inputContentPropertiesEnabled = YES;
        desc.inputContentMinScale = 1.0f;
        desc.inputContentMaxScale = 3.0f;
        if (@available(macOS 15, *)) {
            desc.requiresSynchronousInitialization = YES;
        }
        id<MTLFXTemporalScaler> scaler = [desc newTemporalScalerWithDevice:device_];
        key_ = key;
        if (!scaler) {
            failed_ = true;
            LogError("metalfx: no temporal scaler for {}x{} -> {}x{} (color {}, depth {}, motion {}, output {})", key.input_width,
                     key.input_height, key.output_width, key.output_height, static_cast<unsigned>(key.color),
                     static_cast<unsigned>(key.depth), static_cast<unsigned>(key.motion), static_cast<unsigned>(key.output));
            return nil;
        }
        failed_ = false;
        scaler_ = scaler;
        staging_ = nil;
        return scaler_;
    }

    // MetalFX writes its output with the usage it asks for; when the output image lacks it, upscale into staging and blit across.
    id<MTLTexture> OutputTarget(id<MTLFXTemporalScaler> scaler, id<MTLTexture> output, VkExtent2D display) {
        const MTLTextureUsage needed = scaler.outputTextureUsage;
        if ((output.usage & needed) == needed && output.width == display.width && output.height == display.height) {
            return output;
        }
        if (!staging_) {
            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:output.pixelFormat
                                                                                            width:display.width
                                                                                           height:display.height
                                                                                        mipmapped:NO];
            desc.usage = needed | MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModePrivate;
            staging_ = [device_ newTextureWithDescriptor:desc];
            LogInfo("metalfx: output image lacks usage 0x{:x}, upscaling through a staging texture", static_cast<unsigned>(needed));
        }
        return staging_;
    }

    id<MTLSharedEvent> SharedTimeline(VkSemaphore& semaphore) {
        VkExportMetalObjectCreateInfoEXT export_create{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
        export_create.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_SHARED_EVENT_BIT_EXT;
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, &export_create};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type.initialValue = 0;
        VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
        if (vkCreateSemaphore(ctx_.device, &create, nullptr, &semaphore) != VK_SUCCESS) {
            return nil;
        }
        VkExportMetalSharedEventInfoEXT shared{VK_STRUCTURE_TYPE_EXPORT_METAL_SHARED_EVENT_INFO_EXT};
        shared.semaphore = semaphore;
        VkExportMetalObjectsInfoEXT info{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT, &shared};
        export_objects_(ctx_.device, &info);
        return shared.mtlSharedEvent;
    }

    bool CreateTimelines(std::string& reason) {
        auto sync = std::make_shared<TimelineSync>();
        sync->device = ctx_.device;
        sync->mtl_inputs_ready = SharedTimeline(sync->inputs_ready);
        sync->mtl_output_ready = SharedTimeline(sync->output_ready);
        if (!sync->mtl_inputs_ready || !sync->mtl_output_ready) {
            reason = "cannot create exportable timeline semaphores";
            return false;
        }
        sync_ = std::move(sync);
        return true;
    }

    vk::Context& ctx_;
    PFN_vkExportMetalObjectsEXT export_objects_ = nullptr;
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id<MTLComputePipelineState> exposure_pipeline_;
    id<MTLTexture> exposure_half_;  // written and read only on queue_, which runs its command buffers in order
    id<MTLFXTemporalScaler> scaler_;
    id<MTLTexture> staging_;
    id<MTLCommandBuffer> last_cb_;
    ScalerKey key_;
    bool failed_ = false;
    std::shared_ptr<TimelineSync> sync_;
    uint64_t next_value_ = 0;
    uint64_t dispatches_ = 0;
};

}

std::unique_ptr<UpscaleBackend> CreateMetalFxBackend(vk::Context& ctx) {
    return std::make_unique<MetalFxBackend>(ctx);
}

}
