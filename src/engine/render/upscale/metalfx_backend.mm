#include "engine/render/upscale/upscale.h"

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include <vulkan/vulkan_metal.h>

#include <deque>
#include <string>

#include "engine/core/log.h"

namespace pt {
namespace {

/* Events are never reused: resetting a MoltenVK event and waiting on it again from the GPU deadlocks. A pair is destroyed once both
   command buffers are well past it. */
struct EventPair {
    VkEvent inputs_ready = VK_NULL_HANDLE;
    VkEvent output_ready = VK_NULL_HANDLE;
    id<MTLSharedEvent> mtl_inputs_ready;
    id<MTLSharedEvent> mtl_output_ready;
    id<MTLCommandBuffer> upscale_cb;
    uint64_t dispatch = 0;
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

constexpr uint64_t kPairRetireDistance = 16;
constexpr double kWatchdogSeconds = 2.0;

/* The upscale runs on its own Metal queue, ordered against the frame's command buffer by two events: the frame signals inputs_ready where
   the upscale belongs and waits on output_ready; the MetalFX command buffer waits on the first and signals the second. */
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
        [last_cb_ waitUntilCompleted];  // bounded by the watchdog
        for (EventPair& pair : pairs_) {
            DestroyPair(pair);
        }
        pairs_.clear();
        scaler_ = nil;
        staging_ = nil;
        last_cb_ = nil;
        key_ = {};
        failed_ = false;
        dispatches_ = 0;
    }

private:
    bool Encode(const UpscaleDispatch& d) {
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
        EventPair* pair = NewPair();
        if (!pair) {
            return false;
        }
        ++dispatches_;

        const VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT,
                                      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
        vkCmdSetEvent(d.cmd, pair->inputs_ready, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        vkCmdWaitEvents(d.cmd, 1, &pair->output_ready, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 1, &barrier, 0,
                        nullptr, 0, nullptr);

        id<MTLCommandBuffer> cb = [queue_ commandBuffer];
        [cb encodeWaitForEvent:pair->mtl_inputs_ready value:1];
        id<MTLTexture> target = OutputTarget(scaler, output, d.display);
        scaler.colorTexture = color;
        scaler.depthTexture = depth;
        scaler.motionTexture = motion;
        scaler.outputTexture = target;
        scaler.exposureTexture = exposure;
        scaler.inputContentWidth = std::min<NSUInteger>(d.render.width, color.width);
        scaler.inputContentHeight = std::min<NSUInteger>(d.render.height, color.height);
        scaler.jitterOffsetX = d.jitter.x;
        scaler.jitterOffsetY = d.jitter.y;
        scaler.motionVectorScaleX = d.motion_scale.x;
        scaler.motionVectorScaleY = d.motion_scale.y;
        scaler.reset = d.reset || dispatches_ == 1;
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
        [cb encodeSignalEvent:pair->mtl_output_ready value:1];
        // The handler keeps the scaler and target alive until the GPU is done with them, even if the backend replaces them first.
        [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
            (void)scaler;
            (void)target;
            if (done.error) {
                LogError("metalfx: upscale failed on the GPU: {}", done.error.localizedDescription.UTF8String);
            }
        }];
        [cb commit];
        pair->upscale_cb = cb;
        last_cb_ = cb;
        ArmWatchdog(cb, pair->mtl_inputs_ready);
        return true;
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

    id<MTLSharedEvent> SharedEvent(VkEvent& event) {
        VkExportMetalObjectCreateInfoEXT export_create{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
        export_create.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_SHARED_EVENT_BIT_EXT;
        VkEventCreateInfo create{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO, &export_create};
        if (vkCreateEvent(ctx_.device, &create, nullptr, &event) != VK_SUCCESS) {
            return nil;
        }
        VkExportMetalSharedEventInfoEXT shared{VK_STRUCTURE_TYPE_EXPORT_METAL_SHARED_EVENT_INFO_EXT};
        shared.event = event;
        VkExportMetalObjectsInfoEXT info{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT, &shared};
        export_objects_(ctx_.device, &info);
        return shared.mtlSharedEvent;
    }

    EventPair* NewPair() {
        while (!pairs_.empty()) {
            EventPair& oldest = pairs_.front();
            const bool done = !oldest.upscale_cb || oldest.upscale_cb.status >= MTLCommandBufferStatusCompleted;
            if (!done || dispatches_ - oldest.dispatch < kPairRetireDistance) {
                break;
            }
            DestroyPair(oldest);
            pairs_.pop_front();
        }
        EventPair pair;
        pair.mtl_inputs_ready = SharedEvent(pair.inputs_ready);
        pair.mtl_output_ready = SharedEvent(pair.output_ready);
        if (!pair.mtl_inputs_ready || !pair.mtl_output_ready) {
            LogError("metalfx: cannot create shared events");
            DestroyPair(pair);
            return nullptr;
        }
        pair.dispatch = dispatches_;
        pairs_.push_back(pair);
        return &pairs_.back();
    }

    void DestroyPair(EventPair& pair) {
        if (pair.inputs_ready) {
            vkDestroyEvent(ctx_.device, pair.inputs_ready, nullptr);
        }
        if (pair.output_ready) {
            vkDestroyEvent(ctx_.device, pair.output_ready, nullptr);
        }
    }

    // A frame command buffer that is recorded but never submitted would leave the upscale queue waiting forever.
    static void ArmWatchdog(id<MTLCommandBuffer> cb, id<MTLSharedEvent> inputs_ready) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(kWatchdogSeconds * NSEC_PER_SEC)),
                       dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                           if (cb.status < MTLCommandBufferStatusCompleted && inputs_ready.signaledValue == 0) {
                               LogWarn("metalfx: frame never reached the upscale, releasing its queue");
                               inputs_ready.signaledValue = 1;
                           }
                       });
    }

    vk::Context& ctx_;
    PFN_vkExportMetalObjectsEXT export_objects_ = nullptr;
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id<MTLFXTemporalScaler> scaler_;
    id<MTLTexture> staging_;
    id<MTLCommandBuffer> last_cb_;
    ScalerKey key_;
    bool failed_ = false;
    std::deque<EventPair> pairs_;
    uint64_t dispatches_ = 0;
};

}

std::unique_ptr<UpscaleBackend> CreateMetalFxBackend(vk::Context& ctx) {
    return std::make_unique<MetalFxBackend>(ctx);
}

}
