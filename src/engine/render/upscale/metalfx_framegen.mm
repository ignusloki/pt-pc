#include "engine/render/upscale/frame_generation.h"

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#include <vulkan/vulkan_metal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/log.h"

namespace pt {

// MTLFXFrameInterpolator came with the macOS 26 SDK. With an older SDK (the Intel builds use 15.x) the game builds without MetalFX frame
// generation: the factory below returns nothing and the menu shows it as not in this build.
#if __has_include(<MetalFX/MTLFXFrameInterpolator.h>)
namespace {

using Clock = std::chrono::steady_clock;

/* MetalFX frame interpolation. The renderer draws into stand-in swapchain images, a copy without the UI (Present) and the UI alone with
   coverage alpha (UiLayer). At present time the interpolator builds the frame halfway between the previous and the current UI-free
   copies, with the current UI on top, and both frames go to the real swapchain in that order. The real swapchain runs FIFO so each frame
   holds the screen for one refresh, which is the pacing.

   The interpolator reads this frame's depth and motion, which the next frame overwrites, so it is encoded on MoltenVK's own Metal queue:
   committed after the frame's Vulkan submissions and before the next frame's, it runs between them. That order needs MoltenVK to commit
   each vkQueueSubmit before returning, its default; with asynchronous submits frame generation stays unavailable. */
class MetalFxFrameGeneration final : public FrameGeneration, public vk::SwapchainHooks {
public:
    explicit MetalFxFrameGeneration(vk::Context& ctx) : ctx_(ctx), only_generated_(std::getenv("PT_FG_ONLY_GENERATED") != nullptr) {}
    ~MetalFxFrameGeneration() override { Shutdown(); }

    bool Available(std::string& reason) override {
        if (!checked_) {
            checked_ = true;
            available_ = Check(reason_);
            LogInfo("frame generation: MetalFX {}{}", available_ ? "available" : "unavailable: ", available_ ? "" : reason_);
        }
        reason = reason_;
        return available_;
    }

    bool Update(bool wanted) override {
        const bool owned = ctx_.SwapchainOwner() == this;
        if (wanted) {
            if (!owned) {
                const bool recreate = ctx_.swapchain_hooks != this;
                ctx_.swapchain_hooks = this;
                generating_ = false;
                return recreate;
            }
            generating_ = real_ != VK_NULL_HANDLE;
            return false;
        }
        generating_ = false;
        const bool hooked = ctx_.swapchain_hooks == this;
        if (hooked) {
            ctx_.swapchain_hooks = nullptr;
        }
        // A swapchain this still owns goes back to the renderer at the rebuild, also after Disable already cleared the hook.
        return hooked || owned;
    }

    bool Generating() const override { return generating_; }

    void Prepare(const FrameGenPrepare& p) override {
        if (!generating_) {
            return;
        }
        prepare_ = p;
        prepared_ = true;
    }

    const vk::Image* Present(uint32_t image_index) override {
        if (ctx_.SwapchainOwner() != this || image_index >= frames_.size()) {
            return nullptr;
        }
        // The renderer writes the UI-free copy and the UI layer only when this returns them, so the flags say what present can use.
        frames_[image_index].hudless_written = generating_;
        return generating_ ? &frames_[image_index].hudless : nullptr;
    }

    const vk::Image* UiLayer(uint32_t image_index) override {
        if (ctx_.SwapchainOwner() != this || image_index >= frames_.size() || !generating_) {
            return nullptr;
        }
        return &frames_[image_index].ui;
    }

    void Shutdown() override {
        if (!ctx_.device) {
            return;
        }
        LogSummary();
        if (ctx_.swapchain_hooks == this) {
            ctx_.swapchain_hooks = nullptr;
        }
        if (ctx_.SwapchainOwner() == this) {
            vkDeviceWaitIdle(ctx_.device);
            ctx_.DestroySwapchain();
        }
        DestroyFrames();
        if (pool_) {
            vkDestroyCommandPool(ctx_.device, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
        interpolator_ = nil;
        generating_ = false;
    }

    VkResult CreateSwapchain(const VkSwapchainCreateInfoKHR& requested, VkSwapchainKHR& handle) override {
        DestroyFrames();
        VkSwapchainCreateInfoKHR info = requested;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.oldSwapchain = VK_NULL_HANDLE;
        VkResult result = vkCreateSwapchainKHR(ctx_.device, &info, nullptr, &real_);
        if (result != VK_SUCCESS) {
            real_ = VK_NULL_HANDLE;
            Disable("the frame generation swapchain could not be created");
            return result;
        }
        uint32_t count = 0;
        vkGetSwapchainImagesKHR(ctx_.device, real_, &count, nullptr);
        real_images_.resize(count);
        vkGetSwapchainImagesKHR(ctx_.device, real_, &count, real_images_.data());
        format_ = info.imageFormat;
        extent_ = info.imageExtent;
        if (!CreateFrames(count) || !CreateCommands()) {
            DestroyFrames();
            vkDestroySwapchainKHR(ctx_.device, real_, nullptr);
            real_ = VK_NULL_HANDLE;
            Disable("the frame generation swapchain could not be created");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        have_previous_ = false;
        LogInfo("frame generation: MetalFX presenting {}x{} through {} swapchain images, always with v-sync", extent_.width, extent_.height, count);
        handle = real_;
        return VK_SUCCESS;
    }

    void DestroySwapchain(VkSwapchainKHR handle) override {
        vkDeviceWaitIdle(ctx_.device);
        DestroyFrames();
        // The interpolator holds the last frames' textures, which would otherwise outlive the images they belong to.
        interpolator_ = nil;
        key_ = {};
        if (handle) {
            vkDestroySwapchainKHR(ctx_.device, handle, nullptr);
        }
        real_ = VK_NULL_HANDLE;
    }

    VkResult SwapchainImages(VkSwapchainKHR, uint32_t* count, VkImage* images) override {
        if (images) {
            for (uint32_t i = 0; i < std::min<uint32_t>(*count, static_cast<uint32_t>(frames_.size())); ++i) {
                images[i] = frames_[i].proxy.image;
            }
        }
        *count = static_cast<uint32_t>(frames_.size());
        return VK_SUCCESS;
    }

    VkResult AcquireNextImage(VkSwapchainKHR, VkSemaphore semaphore, uint32_t* index) override {
        if (frames_.empty()) {
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        *index = next_proxy_;
        next_proxy_ = (next_proxy_ + 1) % static_cast<uint32_t>(frames_.size());
        // Present() sets it again when the renderer writes this frame's UI-free copy; a frame drawn without one is not interpolated.
        frames_[*index].hudless_written = false;
        // Stand-in images are free as soon as the queue reaches the frame, so the semaphore is signalled right away.
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = semaphore;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        return vkQueueSubmit2(ctx_.queue, 1, &submit, VK_NULL_HANDLE);
    }

    VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR& info) override {
        const uint32_t index = info.pImageIndices[0];
        if (index >= frames_.size()) {
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        Slot& slot = slots_[slot_index_];
        slot_index_ = (slot_index_ + 1) % slots_.size();
        ctx_.CheckDeviceLost(vkWaitForFences(ctx_.device, 1, &slot.fence, VK_TRUE, UINT64_MAX), "frame generation slot wait");
        vkResetFences(ctx_.device, 1, &slot.fence);
        VkSemaphore rendered = info.waitSemaphoreCount > 0 ? info.pWaitSemaphores[0] : VK_NULL_HANDLE;

        Frame& frame = frames_[index];
        const Clock::time_point start = Clock::now();
        bool interpolate = false;
        if (@available(macOS 26.0, *)) {
            interpolate = generating_ && prepared_ && frame.hudless_written && have_previous_ && previous_ < frames_.size() &&
                          frames_[previous_].hudless_written && Interpolate(frame, frames_[previous_]);
        }
        VkResult result = VK_SUCCESS;
        if (interpolate) {
            result = PresentCopy(queue, slot.generated, slot.acquired[0], rendered, interp_.image, VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE);
            rendered = VK_NULL_HANDLE;
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                Drain(VK_NULL_HANDLE, slot.fence);
                return result;
            }
            ++generated_frames_;
        }
        VkResult real = VK_SUCCESS;
        if (only_generated_ && interpolate) {
            Drain(VK_NULL_HANDLE, slot.fence);
        } else {
            real = PresentCopy(queue, slot.real, slot.acquired[1], rendered, frame.proxy.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.fence);
        }
        ++real_frames_;
        // History carries over only from a frame the interpolator saw as its current color.
        reset_ = !interpolate;
        if (frame.hudless_written) {
            previous_ = index;
            have_previous_ = true;
        } else {
            have_previous_ = false;
        }
        prepared_ = false;
        const Clock::time_point now = Clock::now();
        present_ms_.push_back(std::chrono::duration<float, std::milli>(now - start).count());
        if (last_present_.time_since_epoch().count() != 0) {
            intervals_.push_back(std::chrono::duration<float, std::milli>(now - last_present_).count());
        }
        last_present_ = now;
        return real != VK_SUCCESS ? real : result;
    }

    VkImageLayout PresentLayout() const override { return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; }

private:
    struct Frame {
        vk::Image proxy;
        vk::Image hudless;
        vk::Image ui;
        bool hudless_written = false;
    };

    struct Slot {
        VkCommandBuffer generated = VK_NULL_HANDLE;
        VkCommandBuffer real = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::array<VkSemaphore, 2> acquired{};
    };

    // Leaves the swapchain to the renderer at its next rebuild and shows the reason in the menu, as the FSR 3 path does.
    void Disable(const char* reason) {
        LogError("frame generation: MetalFX turned off: {}", reason);
        available_ = false;
        reason_ = reason;
        generating_ = false;
        if (ctx_.swapchain_hooks == this) {
            ctx_.swapchain_hooks = nullptr;
        }
    }

    bool Check(std::string& reason) {
        if (@available(macOS 26.0, *)) {
        } else {
            reason = "needs macOS 26 or newer";
            return false;
        }
        const char* submits = std::getenv("MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS");
        if (submits && submits[0] == '0') {
            reason = "needs MoltenVK's synchronous queue submits";
            return false;
        }
        export_objects_ = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(vkGetDeviceProcAddr(ctx_.device, "vkExportMetalObjectsEXT"));
        if (!export_objects_) {
            reason = "MoltenVK does not offer VK_EXT_metal_objects";
            return false;
        }
        VkExportMetalDeviceInfoEXT device_info{VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT};
        VkExportMetalCommandQueueInfoEXT queue_info{VK_STRUCTURE_TYPE_EXPORT_METAL_COMMAND_QUEUE_INFO_EXT, &device_info};
        queue_info.queue = ctx_.queue;
        VkExportMetalObjectsInfoEXT objects{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT, &queue_info};
        export_objects_(ctx_.device, &objects);
        device_ = device_info.mtlDevice;
        queue_ = queue_info.mtlCommandQueue;
        if (!device_ || !queue_) {
            reason = "cannot reach MoltenVK's Metal device and queue";
            return false;
        }
        if (@available(macOS 26.0, *)) {
            if (![MTLFXFrameInterpolatorDescriptor supportsDevice:device_]) {
                reason = "this GPU has no MetalFX frame interpolator";
                return false;
            }
        }
        reason.clear();
        return true;
    }

    id<MTLTexture> Texture(VkImage image) const {
        if (!image) {
            return nil;
        }
        VkExportMetalTextureInfoEXT texture{VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT};
        texture.image = image;
        texture.plane = VK_IMAGE_ASPECT_PLANE_0_BIT;
        VkExportMetalObjectsInfoEXT info{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT, &texture};
        export_objects_(ctx_.device, &info);
        return texture.mtlTexture;
    }

    bool Interpolate(const Frame& current, const Frame& previous) API_AVAILABLE(macos(26.0)) {
        id<MTLTexture> color = Texture(current.hudless.image);
        id<MTLTexture> previous_color = Texture(previous.hudless.image);
        id<MTLTexture> ui = Texture(current.ui.image);
        id<MTLTexture> depth = Texture(prepare_.depth.image);
        id<MTLTexture> motion = Texture(prepare_.motion.image);
        id<MTLTexture> output = Texture(interp_.image);
        if (!color || !previous_color || !ui || !depth || !motion || !output) {
            if (!error_logged_) {
                LogError("frame generation: a Vulkan image has no Metal texture");
                error_logged_ = true;
            }
            return false;
        }
        const Key key{color.pixelFormat, ui.pixelFormat, depth.pixelFormat, motion.pixelFormat, output.pixelFormat,
                      depth.width, depth.height, output.width, output.height};
        if (!interpolator_ || !(key == key_)) {
            MTLFXFrameInterpolatorDescriptor* desc = [MTLFXFrameInterpolatorDescriptor new];
            desc.colorTextureFormat = key.color;
            desc.outputTextureFormat = key.output;
            desc.uiTextureFormat = key.ui;
            desc.depthTextureFormat = key.depth;
            desc.motionTextureFormat = key.motion;
            desc.inputWidth = key.input_width;
            desc.inputHeight = key.input_height;
            desc.outputWidth = key.output_width;
            desc.outputHeight = key.output_height;
            interpolator_ = [desc newFrameInterpolatorWithDevice:device_];
            key_ = key;
            reset_ = true;
            if (!interpolator_) {
                LogError("frame generation: no MetalFX frame interpolator for {}x{} -> {}x{}", key.input_width, key.input_height,
                         key.output_width, key.output_height);
                Disable("MetalFX cannot interpolate this frame size");
                return false;
            }
            LogInfo("frame generation: MetalFX frame interpolator for {}x{} -> {}x{}", key.input_width, key.input_height, key.output_width,
                    key.output_height);
        }
        id<MTLFXFrameInterpolator> fi = interpolator_;
        fi.colorTexture = color;
        fi.prevColorTexture = previous_color;
        fi.depthTexture = depth;
        fi.motionTexture = motion;
        fi.uiTexture = ui;
        fi.uiTextureComposited = NO;
        fi.outputTexture = output;
        // The upscaler's scale, which maps motion to render pixels: the size of the depth and motion the interpolator reads.
        fi.motionVectorScaleX = prepare_.motion_scale.x;
        fi.motionVectorScaleY = prepare_.motion_scale.y;
        fi.deltaTime = std::clamp(prepare_.frame_ms, 0.1f, 250.0f) * 0.001f;
        fi.nearPlane = prepare_.near_plane;
        fi.farPlane = kFarPlane;
        fi.fieldOfView = prepare_.fov_y * (180.0f / 3.14159265f);
        fi.aspectRatio = static_cast<float>(output.width) / static_cast<float>(std::max<NSUInteger>(output.height, 1));
        fi.jitterOffsetX = prepare_.jitter.x;
        fi.jitterOffsetY = prepare_.jitter.y;
        fi.depthReversed = YES;
        fi.shouldResetHistory = reset_ || prepare_.reset;
        id<MTLCommandBuffer> cb = [queue_ commandBuffer];
        if (!cb) {
            return false;
        }
        cb.label = @"MetalFX frame interpolation";
        const std::shared_ptr<GpuTime> gpu = gpu_;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
            const double us = (done.GPUEndTime - done.GPUStartTime) * 1e6;
            if (us > 0.0) {
                gpu->sum_us += static_cast<uint64_t>(us);
                ++gpu->count;
            }
        }];
        [fi encodeToCommandBuffer:cb];
        [cb commit];
        return true;
    }

    // Copies an image into the next real swapchain image and presents it.
    VkResult PresentCopy(VkQueue queue, VkCommandBuffer cmd, VkSemaphore acquired, VkSemaphore rendered, VkImage source, VkImageLayout source_layout,
                         VkFence fence) {
        uint32_t real_index = 0;
        VkResult result = vkAcquireNextImageKHR(ctx_.device, real_, UINT64_MAX, acquired, VK_NULL_HANDLE, &real_index);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            Drain(rendered, fence);
            return result;
        }
        VkImage target = real_images_[real_index];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        Barrier(cmd, source, source_layout, source_layout, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        Barrier(cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstSubresource = copy.srcSubresource;
        copy.extent = {extent_.width, extent_.height, 1};
        vkCmdCopyImage(cmd, source, source_layout, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        Barrier(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
        // Later frames write the source again; this orders those writes after the copy.
        Barrier(cmd, source, source_layout, source_layout, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0);
        vkEndCommandBuffer(cmd);
        VkSemaphoreSubmitInfo waits[2]{};
        uint32_t wait_count = 0;
        waits[wait_count++] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, acquired, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 0};
        if (rendered) {
            waits[wait_count++] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, rendered, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0};
        }
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, ready_[real_index], 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0};
        VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, cmd, 0};
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = wait_count;
        submit.pWaitSemaphoreInfos = waits;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        ctx_.CheckDeviceLost(vkQueueSubmit2(queue, 1, &submit, fence), "frame generation submit");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &ready_[real_index];
        present.swapchainCount = 1;
        present.pSwapchains = &real_;
        present.pImageIndices = &real_index;
        const VkResult presented = vkQueuePresentKHR(queue, &present);
        return presented != VK_SUCCESS ? presented : result;
    }

    // When a present gives up early: consumes the renderer's semaphore and signals the slot's fence, so neither is left pending.
    void Drain(VkSemaphore rendered, VkFence fence) {
        if (!rendered && !fence) {
            return;
        }
        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, rendered, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0};
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = rendered ? 1 : 0;
        submit.pWaitSemaphoreInfos = &wait;
        vkQueueSubmit2(ctx_.queue, 1, &submit, fence);
    }

    static void Barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage,
                        VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = src_stage;
        b.srcAccessMask = src_access;
        b.dstStageMask = dst_stage;
        b.dstAccessMask = dst_access;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    bool CreateFrames(uint32_t count) {
        const VkExtent3D extent{extent_.width, extent_.height, 1};
        const VkImageUsageFlags color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        frames_.resize(count);
        for (Frame& f : frames_) {
            if (!ctx_.CreateImage(f.proxy, format_, extent, color | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
                !ctx_.CreateImage(f.hudless, format_, extent, color, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT, false, true) ||
                !ctx_.CreateImage(f.ui, format_, extent, color, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT, false, true)) {
                return false;
            }
        }
        if (!ctx_.CreateImage(interp_, format_, extent, color | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT, false, true)) {
            return false;
        }
        ready_.resize(count);
        for (VkSemaphore& s : ready_) {
            VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if (vkCreateSemaphore(ctx_.device, &info, nullptr, &s) != VK_SUCCESS) {
                s = VK_NULL_HANDLE;
                return false;
            }
        }
        // The interpolator writes interp_ outside Vulkan, so it lives in GENERAL from the start.
        ctx_.Submit([&](VkCommandBuffer cmd) {
            Barrier(cmd, interp_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
        });
        next_proxy_ = 0;
        return true;
    }

    bool CreateCommands() {
        if (!pool_) {
            VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            info.queueFamilyIndex = ctx_.queue_family;
            if (vkCreateCommandPool(ctx_.device, &info, nullptr, &pool_) != VK_SUCCESS) {
                return false;
            }
        }
        for (Slot& slot : slots_) {
            VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool = pool_;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 2;
            VkCommandBuffer buffers[2]{};
            if (vkAllocateCommandBuffers(ctx_.device, &alloc, buffers) != VK_SUCCESS) {
                return false;
            }
            slot.generated = buffers[0];
            slot.real = buffers[1];
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (vkCreateFence(ctx_.device, &fence, nullptr, &slot.fence) != VK_SUCCESS) {
                slot.fence = VK_NULL_HANDLE;
                return false;
            }
            for (VkSemaphore& s : slot.acquired) {
                VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                if (vkCreateSemaphore(ctx_.device, &info, nullptr, &s) != VK_SUCCESS) {
                    s = VK_NULL_HANDLE;
                    return false;
                }
            }
        }
        slot_index_ = 0;
        return true;
    }

    void DestroyFrames() {
        if (!ctx_.device) {
            return;
        }
        for (Frame& f : frames_) {
            ctx_.DestroyImage(f.proxy);
            ctx_.DestroyImage(f.hudless);
            ctx_.DestroyImage(f.ui);
        }
        frames_.clear();
        ctx_.DestroyImage(interp_);
        for (VkSemaphore s : ready_) {
            if (s) {
                vkDestroySemaphore(ctx_.device, s, nullptr);
            }
        }
        ready_.clear();
        for (Slot& slot : slots_) {
            if (slot.fence) {
                vkDestroyFence(ctx_.device, slot.fence, nullptr);
            }
            for (VkSemaphore s : slot.acquired) {
                if (s) {
                    vkDestroySemaphore(ctx_.device, s, nullptr);
                }
            }
            if (pool_ && slot.generated) {
                VkCommandBuffer buffers[2] = {slot.generated, slot.real};
                vkFreeCommandBuffers(ctx_.device, pool_, 2, buffers);
            }
            slot = Slot{};
        }
        real_images_.clear();
        have_previous_ = false;
    }

    struct Stats {
        double mean = 0.0;
        float p99 = 0.0f;
    };

    // Mean and 99th percentile, leaving out the warm-up frames.
    static Stats Summarize(const std::vector<float>& samples) {
        std::vector<float> sorted(samples.begin() + kWarmupFrames, samples.end());
        std::sort(sorted.begin(), sorted.end());
        double sum = 0.0;
        for (float v : sorted) {
            sum += v;
        }
        return {sum / static_cast<double>(sorted.size()), sorted[sorted.size() * 99 / 100]};
    }

    void LogSummary() {
        if (intervals_.size() >= 2 * kWarmupFrames && present_ms_.size() >= 2 * kWarmupFrames) {
            const Stats interval = Summarize(intervals_);
            const Stats present = Summarize(present_ms_);
            const uint64_t gpu_count = gpu_->count;
            LogInfo("frame generation: MetalFX {} generated and {} rendered frames; rendered frame interval {:.2f} ms mean, {:.2f} ms p99",
                    generated_frames_, real_frames_, interval.mean, interval.p99);
            LogInfo("frame generation: present call {:.2f} ms mean, {:.2f} ms p99; interpolation {:.2f} ms of GPU time per frame", present.mean,
                    present.p99, gpu_count ? static_cast<double>(gpu_->sum_us) / static_cast<double>(gpu_count) * 0.001 : 0.0);
        }
        intervals_.clear();
        present_ms_.clear();
    }

    struct Key {
        MTLPixelFormat color = MTLPixelFormatInvalid;
        MTLPixelFormat ui = MTLPixelFormatInvalid;
        MTLPixelFormat depth = MTLPixelFormatInvalid;
        MTLPixelFormat motion = MTLPixelFormatInvalid;
        MTLPixelFormat output = MTLPixelFormatInvalid;
        NSUInteger input_width = 0;
        NSUInteger input_height = 0;
        NSUInteger output_width = 0;
        NSUInteger output_height = 0;
        bool operator==(const Key&) const = default;
    };

    // The scene uses a reversed infinite projection (depth = near / z); a far plane this distant stands in for infinity.
    static constexpr float kFarPlane = 100000.0f;
    // Frames at the start of a session left out of the timing summary.
    static constexpr size_t kWarmupFrames = 60;

    vk::Context& ctx_;
    PFN_vkExportMetalObjectsEXT export_objects_ = nullptr;
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id interpolator_;
    Key key_;
    bool checked_ = false;
    bool available_ = false;
    std::string reason_;
    bool generating_ = false;
    bool prepared_ = false;
    bool reset_ = true;
    bool error_logged_ = false;
    // PT_FG_ONLY_GENERATED shows only generated frames, for checking them.
    const bool only_generated_ = false;
    FrameGenPrepare prepare_;

    VkSwapchainKHR real_ = VK_NULL_HANDLE;
    std::vector<VkImage> real_images_;
    std::vector<VkSemaphore> ready_;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<Frame> frames_;
    vk::Image interp_;
    uint32_t next_proxy_ = 0;
    uint32_t previous_ = 0;
    bool have_previous_ = false;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::array<Slot, 3> slots_{};
    size_t slot_index_ = 0;

    uint64_t generated_frames_ = 0;
    uint64_t real_frames_ = 0;
    std::vector<float> intervals_;
    std::vector<float> present_ms_;
    Clock::time_point last_present_{};
    // Filled by Metal's completion handlers on their own thread, which can outlive this object by a moment.
    struct GpuTime {
        std::atomic<uint64_t> sum_us{0};
        std::atomic<uint64_t> count{0};
    };
    std::shared_ptr<GpuTime> gpu_ = std::make_shared<GpuTime>();
};

}

std::unique_ptr<FrameGeneration> CreateMetalFxFrameGeneration(vk::Context& ctx) {
    return std::make_unique<MetalFxFrameGeneration>(ctx);
}
#else
std::unique_ptr<FrameGeneration> CreateMetalFxFrameGeneration(vk::Context&) {
    return nullptr;
}
#endif

}
