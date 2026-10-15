#include "engine/render/upscale/frame_generation.h"

#if defined(PT_WITH_FSR) && defined(_WIN32)

#include <windows.h>

#include <SDL3/SDL_timer.h>
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_api_loader.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/vk/ffx_api_vk.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <vector>

#include "engine/core/log.h"
#include "engine/render/renderer.h"
#include "engine/render/upscale/upscale_platform.h"

namespace pt {
namespace {

using Clock = std::chrono::steady_clock;

FfxApiResource ImageResource(VkImage image, VkFormat format, VkExtent2D extent, uint32_t state, uint32_t usage) {
    FfxApiResource r{};
    r.resource = reinterpret_cast<void*>(image);
    r.state = state;
    r.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    r.description.format = ffxApiGetSurfaceFormatVK(format);
    r.description.width = extent.width;
    r.description.height = extent.height;
    r.description.depth = 1;
    r.description.mipCount = 1;
    r.description.flags = FFX_API_RESOURCE_FLAGS_NONE;
    r.description.usage = usage;
    return r;
}

VkQueueInfoFFXAPI QueueInfo(VkQueue queue, uint32_t family, PFN_vkQueueSubmitFFXAPI submit = nullptr) {
    VkQueueInfoFFXAPI info{};
    info.queue = queue;
    info.familyIndex = family;
    info.submitFunc = submit;
    return info;
}

VkQueue g_present_queue = VK_NULL_HANDLE;
std::mutex g_queue_mutex;
std::atomic<uint64_t> g_dropped_signals{0};

const VkTimelineSemaphoreSubmitInfo* TimelineInfo(const VkSubmitInfo& submit) {
    for (auto* s = static_cast<const VkBaseInStructure*>(submit.pNext); s; s = s->pNext) {
        if (s->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
            return reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(s);
        }
    }
    return nullptr;
}

VkResult SubmitToPresentQueue(uint32_t count, const VkSubmitInfo* submits, VkFence fence) {
    std::vector<VkSubmitInfo> infos(submits, submits + count);
    std::vector<VkTimelineSemaphoreSubmitInfo> timelines(count);
    std::vector<std::vector<VkSemaphore>> semaphores(count);
    std::vector<std::vector<uint64_t>> values(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkSubmitInfo& info = infos[i];
        if (info.commandBufferCount != 0 || info.signalSemaphoreCount == 0) {
            continue;
        }
        const VkTimelineSemaphoreSubmitInfo* timeline = TimelineInfo(info);
        for (uint32_t j = 0; j < info.signalSemaphoreCount; ++j) {
            const uint64_t value = timeline && j < timeline->signalSemaphoreValueCount ? timeline->pSignalSemaphoreValues[j] : 0;
            if (value != 0) {
                semaphores[i].push_back(info.pSignalSemaphores[j]);
                values[i].push_back(value);
            }
        }
        if (semaphores[i].size() == info.signalSemaphoreCount) {
            continue;
        }
        g_dropped_signals += info.signalSemaphoreCount - semaphores[i].size();
        info.signalSemaphoreCount = static_cast<uint32_t>(semaphores[i].size());
        info.pSignalSemaphores = semaphores[i].data();
        if (timeline) {
            timelines[i] = *timeline;
            timelines[i].signalSemaphoreValueCount = info.signalSemaphoreCount;
            timelines[i].pSignalSemaphoreValues = values[i].data();
            info.pNext = &timelines[i];
        }
    }
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    return vkQueueSubmit(g_present_queue, count, infos.data(), fence);
}

PFN_vkQueuePresentKHR g_ffx_queue_present = nullptr;
PFN_vkQueueWaitIdle g_ffx_queue_wait_idle = nullptr;
PFN_vkDeviceWaitIdle g_ffx_device_wait_idle = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL FfxQueuePresent(VkQueue queue, const VkPresentInfoKHR* info) {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    return g_ffx_queue_present(queue, info);
}

VKAPI_ATTR VkResult VKAPI_CALL FfxQueueWaitIdle(VkQueue queue) {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    return g_ffx_queue_wait_idle(queue);
}

VKAPI_ATTR VkResult VKAPI_CALL FfxDeviceWaitIdle(VkDevice device) {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    return g_ffx_device_wait_idle(device);
}

template <typename Fn>
bool PatchImport(HMODULE module, const char* name, Fn hook, Fn& original) {
    auto* base = reinterpret_cast<uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress == 0) {
        return false;
    }
    for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); desc->Name != 0; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), "vulkan-1.dll") != 0 || desc->OriginalFirstThunk == 0) {
            continue;
        }
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                continue;
            }
            const auto* entry = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(entry->Name), name) != 0) {
                continue;
            }
            if (reinterpret_cast<Fn>(slots->u1.Function) == hook) {
                return true;
            }
            DWORD protect = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &protect)) {
                return false;
            }
            original = reinterpret_cast<Fn>(slots->u1.Function);
            slots->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
            VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), protect, &protect);
            return true;
        }
    }
    return false;
}

class FsrFrameGeneration;

FsrFrameGeneration* g_presenting = nullptr;
PFN_vkDeviceWaitIdle g_device_wait_idle = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL WaitIdleAfterPresents(VkDevice device);

class FsrFrameGeneration final : public FrameGeneration, public vk::SwapchainHooks {
public:
    void WaitForPresents() {
        if (!swapchain_context_ || !swapchain_alive_) {
            return;
        }
        ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK wait{};
        wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
        fn_.Dispatch(&swapchain_context_, &wait.header);
    }

    FsrFrameGeneration(vk::Context& ctx, const FrameGenQueues& queues) : ctx_(ctx), queues_(queues) {
        if (std::getenv("PT_FG_DEBUG")) {
            debug_flags_ = FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES | FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;
        }
        only_generated_ = std::getenv("PT_FG_ONLY_GENERATED") != nullptr;
        if (const char* capture = std::getenv("PT_FG_CAPTURE")) {
            const std::string text(capture);
            const size_t comma = text.rfind(',');
            capture_dir_ = text.substr(0, comma);
            capture_count_ = comma == std::string::npos ? 32u : static_cast<uint32_t>(std::strtoul(text.c_str() + comma + 1, nullptr, 10));
        }
    }

    ~FsrFrameGeneration() override { Shutdown(); }
    bool hdr_refused_ = false;

    bool Available(std::string& reason) override {
        if (!checked_) {
            checked_ = true;
            available_ = Check(reason_);
            LogInfo("frame generation: {}{}", available_ ? "available" : "unavailable: ", available_ ? "" : reason_);
        }
        reason = reason_;
        return available_;
    }

    bool Update(bool wanted) override {
        const bool owned = ctx_.SwapchainOwner() == this;
        bool recreate = false;
        if (wanted && !owned && ctx_.swapchain.color_space != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            if (!hdr_refused_) {
                LogWarn("frame generation: off while the native HDR output is on (its swapchain is SDR only)");
                hdr_refused_ = true;
            }
            wanted = false;
        }
        if (wanted) {
            if (!owned) {
                if (ctx_.swapchain_hooks != this) {
                    ctx_.swapchain_hooks = this;
                    recreate = true;
                }
                generating_ = false;
                return recreate;
            }
            const VkExtent2D extent = ctx_.swapchain.extent;
            if (!fg_context_ || extent.width != display_.width || extent.height != display_.height || ctx_.swapchain.format != format_ ||
                hudless_.size() != ctx_.swapchain.images.size()) {
                CreateGenerationContext();
            }
            generating_ = fg_context_ != nullptr;
            return false;
        }
        generating_ = false;
        if (ctx_.swapchain_hooks == this) {
            ctx_.swapchain_hooks = nullptr;
            recreate = true;
        }
        if (!owned && (fg_context_ || swapchain_context_)) {
            LogSummary();
            DestroyContexts();
        }
        return recreate;
    }

    bool Generating() const override { return generating_; }

    void Prepare(const FrameGenPrepare& p) override {
        if (!generating_ || !fg_context_) {
            return;
        }
        if (p.render.width > display_.width || p.render.height > display_.height) {
            if (!oversize_logged_) {
                LogWarn("frame generation: render size {}x{} is larger than the context's {}x{}, no generation until the swapchain follows",
                        p.render.width, p.render.height, display_.width, display_.height);
                oversize_logged_ = true;
            }
            return;
        }
        ffxDispatchDescFrameGenerationPrepareCameraInfo camera{};
        camera.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
        for (int i = 0; i < 3; ++i) {
            camera.cameraPosition[i] = p.position[i];
            camera.cameraUp[i] = p.up[i];
            camera.cameraRight[i] = p.right[i];
            camera.cameraForward[i] = p.forward[i];
        }
        ffxDispatchDescFrameGenerationPrepare d{};
        d.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
        d.header.pNext = &camera.header;
        d.frameID = frame_id_;
        d.flags = debug_flags_;
        d.commandList = p.cmd;
        d.renderSize = {p.render.width, p.render.height};
        d.jitterOffset = {p.jitter.x, p.jitter.y};
        d.motionVectorScale = {p.motion_scale.x, p.motion_scale.y};
        d.frameTimeDelta = p.frame_ms;
        d.cameraNear = FLT_MAX;
        d.cameraFar = p.near_plane;
        d.cameraFovAngleVertical = p.fov_y;
        d.viewSpaceToMetersFactor = 1.0f;
        d.depth = ImageResource(p.depth.image, p.depth.format, p.depth.extent, FFX_API_RESOURCE_STATE_COMPUTE_READ,
                                FFX_API_RESOURCE_USAGE_DEPTHTARGET);
        d.motionVectors = ImageResource(p.motion.image, p.motion.format, p.motion.extent, FFX_API_RESOURCE_STATE_COMPUTE_READ,
                                        FFX_API_RESOURCE_USAGE_READ_ONLY);
        if (capture_armed_ && captured_motion_.size() < capture_count_ / 2 + 1) {
            vk::Buffer buffer;
            const VkExtent2D e{p.motion.extent.width, p.motion.extent.height};
            if (ctx_.CreateBuffer(buffer, VkDeviceSize(e.width) * e.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
                constexpr VkPipelineStageFlags2 all = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                constexpr VkAccessFlags2 rw = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                vk::ImageBarrier(p.cmd, p.motion.image, VK_IMAGE_ASPECT_COLOR_BIT, all, rw, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, all, rw,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {e.width, e.height, 1};
                vkCmdCopyImageToBuffer(p.cmd, p.motion.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &region);
                vk::ImageBarrier(p.cmd, p.motion.image, VK_IMAGE_ASPECT_COLOR_BIT, all, rw, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, all, rw,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                captured_motion_.push_back({buffer, e.width, e.height, false, frame_id_, 0.0});
            }
        }
        const ffxReturnCode_t result = fn_.Dispatch(&fg_context_, &d.header);
        if (result != FFX_API_RETURN_OK) {
            if (!error_logged_) {
                LogError("frame generation: prepare failed ({})", result);
                error_logged_ = true;
            }
            return;
        }
        prepared_ = true;
        if (p.reset) {
            reset_ = true;
        }
    }

    const vk::Image* Present(uint32_t image_index) override {
        if (ctx_.SwapchainOwner() != this || !fg_context_) {
            prepared_ = false;
            return nullptr;
        }
        const bool generate = generating_ && prepared_;
        const vk::Image* hudless = generate && image_index < hudless_.size() ? &hudless_[image_index] : nullptr;
        const bool layer_mode = hudless && image_index < layers_.size();
        if (layer_registered_) {
            RegisterUiResource(nullptr);
        }
        ffxConfigureDescFrameGeneration c{};
        c.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        c.swapChain = reinterpret_cast<void*>(ctx_.swapchain.handle);
        c.frameGenerationCallback = &FsrFrameGeneration::Generate;
        c.frameGenerationCallbackUserContext = this;
        if (!capture_dir_.empty()) {
            if (!capture_armed_ && frame_id_ % 30 == 0 && std::filesystem::exists(std::filesystem::path(capture_dir_) / "go")) {
                capture_armed_ = true;
                LogInfo("frame generation: capturing {} presented frames to {}", capture_count_, capture_dir_);
            }
            c.presentCallback = &FsrFrameGeneration::CapturePresent;
            c.presentCallbackUserContext = this;
        }
        c.frameGenerationEnabled = generate;
        c.allowAsyncWorkloads = false;
        if (hudless && !layer_mode) {
            c.HUDLessColor = ImageResource(hudless->image, hudless->format, {hudless->extent.width, hudless->extent.height},
                                           FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_RENDERTARGET);
        }
        c.flags = debug_flags_;
        c.onlyPresentGenerated = only_generated_;
        c.generationRect = {0, 0, static_cast<int32_t>(display_.width), static_cast<int32_t>(display_.height)};
        c.frameID = frame_id_;
        const ffxReturnCode_t result = fn_.Configure(&fg_context_, &c.header);
        if (result != FFX_API_RETURN_OK && !error_logged_) {
            LogError("frame generation: configure failed ({})", result);
            error_logged_ = true;
        }
        if (!generate) {
            reset_ = true;
        }
        ++frame_id_;
        prepared_ = false;
        const Clock::time_point now = Clock::now();
        if (generate && last_present_.time_since_epoch().count() != 0) {
            intervals_.push_back(std::chrono::duration<float, std::milli>(now - last_present_).count());
        }
        last_present_ = now;
        return hudless;
    }

    const vk::Image* UiLayer(uint32_t image_index) override {
        if (!generating_ || image_index >= layers_.size()) {
            return nullptr;
        }
        RegisterUiResource(&layers_[image_index]);
        return &layers_[image_index];
    }

    bool UiLayerOverRendered() const override { return true; }

    void Shutdown() override {
        if (!ctx_.device) {
            return;
        }
        LogSummary();
        if (ctx_.swapchain_hooks == this) {
            ctx_.swapchain_hooks = nullptr;
        }
        WriteCapture();
        if (ctx_.SwapchainOwner() == this) {
            vkDeviceWaitIdle(ctx_.device);
            ctx_.DestroySwapchain();
        }
        DestroyContexts();
        if (module_) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
        generating_ = false;
    }

    VkResult CreateSwapchain(const VkSwapchainCreateInfoKHR& requested, VkSwapchainKHR& handle) override {
        VkSwapchainCreateInfoKHR info = requested;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (const char* mode = std::getenv("PT_FG_PRESENT_MODE")) {
            info.presentMode = std::strcmp(mode, "immediate") == 0 ? VK_PRESENT_MODE_IMMEDIATE_KHR
                               : std::strcmp(mode, "mailbox") == 0 ? VK_PRESENT_MODE_MAILBOX_KHR
                                                                   : VK_PRESENT_MODE_FIFO_KHR;
        }
        if (!swapchain_context_) {
            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            ffxCreateContextDescFrameGenerationSwapChainVK desc{};
            desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
            desc.physicalDevice = ctx_.physical;
            desc.device = ctx_.device;
            desc.swapchain = &swapchain;
            desc.allocator = nullptr;
            desc.createInfo = info;
            desc.gameQueue = QueueInfo(ctx_.queue, ctx_.queue_family);
            desc.asyncComputeQueue = QueueInfo(queues_.compute.queue, queues_.compute.family);
            desc.presentQueue = QueueInfo(queues_.present.queue, queues_.present.family, &SubmitToPresentQueue);
            desc.imageAcquireQueue = QueueInfo(queues_.acquire.queue, queues_.acquire.family);
            g_present_queue = queues_.present.queue;
            const ffxReturnCode_t result = fn_.CreateContext(&swapchain_context_, &desc.header, nullptr);
            if (result != FFX_API_RETURN_OK || !swapchain) {
                LogError("frame generation: swapchain context creation failed ({})", result);
                swapchain_context_ = nullptr;
                Disable("the frame generation swapchain could not be created");
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            replace_ = {};
            replace_.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
            fn_.Query(&swapchain_context_, &replace_.header);
            if (!g_device_wait_idle) {
                g_device_wait_idle = vkDeviceWaitIdle;
                vkDeviceWaitIdle = WaitIdleAfterPresents;
            }
            g_presenting = this;
            swapchain_alive_ = true;
            handle = swapchain;
            LogInfo("frame generation: swapchain {}x{}, {} images, present mode {}", info.imageExtent.width, info.imageExtent.height,
                    info.minImageCount, static_cast<int>(info.presentMode));
            return VK_SUCCESS;
        }
        const VkResult result = replace_.pOutCreateSwapchainFFXAPI(ctx_.device, &info, nullptr, &handle, swapchain_context_);
        swapchain_alive_ = result == VK_SUCCESS;
        if (result != VK_SUCCESS) {
            LogError("frame generation: swapchain creation failed ({})", static_cast<int>(result));
            Disable("the frame generation swapchain could not be created");
        }
        return result;
    }

    void DestroySwapchain(VkSwapchainKHR handle) override {
        if (swapchain_context_ && replace_.pOutDestroySwapchainFFXAPI) {
            if (layer_registered_) {
                RegisterUiResource(nullptr);
            }
            WaitForPresents();
            swapchain_alive_ = false;
            replace_.pOutDestroySwapchainFFXAPI(ctx_.device, handle, nullptr, swapchain_context_);
        }
    }

    VkResult SwapchainImages(VkSwapchainKHR handle, uint32_t* count, VkImage* images) override {
        return replace_.pOutGetSwapchainImagesKHR(ctx_.device, handle, count, images);
    }

    VkResult AcquireNextImage(VkSwapchainKHR handle, VkSemaphore semaphore, uint32_t* index) override {
        const Clock::time_point start = Clock::now();
        VkResult result = replace_.pOutAcquireNextImageKHR(ctx_.device, handle, UINT64_MAX, semaphore, VK_NULL_HANDLE, index);
        if (result == VK_NOT_READY) {
            ++acquire_waits_;
            while (result == VK_NOT_READY && Clock::now() - start < std::chrono::seconds(1)) {
                SDL_DelayNS(100000);
                result = replace_.pOutAcquireNextImageKHR(ctx_.device, handle, UINT64_MAX, semaphore, VK_NULL_HANDLE, index);
            }
            const float waited = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
            acquire_wait_ms_ = std::max(acquire_wait_ms_, waited);
            if (acquire_waits_ <= 5) {
                LogInfo("frame generation: acquire waited {:.2f} ms for a free buffer (frame {})", waited, frame_id_);
            }
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && acquire_errors_++ < 5) {
            LogWarn("frame generation: image acquisition returned {}", static_cast<int>(result));
        }
        return result;
    }

    VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR& info) override {
        if (!present_fence_) {
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCreateFence(ctx_.device, &fence, nullptr, &present_fence_);
        }
        if (present_fence_) {
            vkWaitForFences(ctx_.device, 1, &present_fence_, VK_TRUE, UINT64_MAX);
            vkResetFences(ctx_.device, 1, &present_fence_);
        }
        const VkResult result = replace_.pOutQueuePresentKHR(queue, &info);
        if (present_fence_) {
            vkQueueSubmit(ctx_.queue, 0, nullptr, present_fence_);
        }
        if (result != VK_SUCCESS && present_errors_++ < 8) {
            LogWarn("frame generation: present returned {} (frame {})", static_cast<int>(result), frame_id_);
        }
        return result;
    }

    VkImageLayout PresentLayout() const override { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }

private:
    void Disable(const char* reason) {
        available_ = false;
        reason_ = reason;
        generating_ = false;
        if (ctx_.swapchain_hooks == this) {
            ctx_.swapchain_hooks = nullptr;
        }
    }

    static VkImageLayout CaptureLayout(uint32_t state) {
        switch (state) {
            case FFX_API_RESOURCE_STATE_COMPUTE_READ:
            case FFX_API_RESOURCE_STATE_PIXEL_READ:
            case FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case FFX_API_RESOURCE_STATE_COPY_SRC: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            case FFX_API_RESOURCE_STATE_COPY_DEST: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case FFX_API_RESOURCE_STATE_PRESENT: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            case FFX_API_RESOURCE_STATE_RENDER_TARGET: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            default: return VK_IMAGE_LAYOUT_GENERAL;
        }
    }

    static ffxReturnCode_t CapturePresent(ffxCallbackDescFrameGenerationPresent* params, void* user) {
        auto* self = static_cast<FsrFrameGeneration*>(user);
        auto cmd = static_cast<VkCommandBuffer>(params->commandList);
        auto source = static_cast<VkImage>(params->currentBackBuffer.resource);
        auto target = static_cast<VkImage>(params->outputSwapChainBuffer.resource);
        const VkImageLayout source_layout = CaptureLayout(params->currentBackBuffer.state);
        const VkImageLayout target_layout = CaptureLayout(params->outputSwapChainBuffer.state);
        const uint32_t w = params->currentBackBuffer.description.width;
        const uint32_t h = params->currentBackBuffer.description.height;
        constexpr VkPipelineStageFlags2 all = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        constexpr VkAccessFlags2 rw = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        vk::ImageBarrier(cmd, source, VK_IMAGE_ASPECT_COLOR_BIT, all, rw, source_layout, all, rw, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vk::ImageBarrier(cmd, target, VK_IMAGE_ASPECT_COLOR_BIT, all, rw, target_layout, all, rw, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstSubresource = copy.srcSubresource;
        copy.extent = {w, h, 1};
        vkCmdCopyImage(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        if (self->capture_armed_ && self->captured_.size() < self->capture_count_) {
            vk::Buffer buffer;
            if (self->ctx_.CreateBuffer(buffer, VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {w, h, 1};
                vkCmdCopyImageToBuffer(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &region);
                self->captured_.push_back({buffer, w, h, params->isGeneratedFrame, params->frameID,
                                           std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count()});
                auto ui = static_cast<VkImage>(params->currentUI.resource);
                vk::Buffer ui_buffer;
                if (ui && self->ctx_.CreateBuffer(ui_buffer, VkDeviceSize(w) * h * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
                    const VkImageLayout ui_layout = CaptureLayout(params->currentUI.state);
                    vk::ImageBarrier(cmd, ui, VK_IMAGE_ASPECT_COLOR_BIT, all, rw, ui_layout, all, rw, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                    vkCmdCopyImageToBuffer(cmd, ui, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ui_buffer.buffer, 1, &region);
                    vk::ImageBarrier(cmd, ui, VK_IMAGE_ASPECT_COLOR_BIT, all, rw, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, all, rw, ui_layout);
                    self->captured_ui_.push_back({ui_buffer, w, h, params->isGeneratedFrame, self->captured_.size() - 1, 0.0});
                }
            }
        }
        vk::ImageBarrier(cmd, source, VK_IMAGE_ASPECT_COLOR_BIT, all, rw, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, all, rw, source_layout);
        vk::ImageBarrier(cmd, target, VK_IMAGE_ASPECT_COLOR_BIT, all, rw, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, all, rw, target_layout);
        return FFX_API_RETURN_OK;
    }

    void WriteCapture() {
        if (captured_.empty() && captured_motion_.empty() && captured_ui_.empty()) {
            return;
        }
        WaitForPresents();
        vkDeviceWaitIdle(ctx_.device);
        std::ofstream index(std::filesystem::path(capture_dir_) / "frames.txt");
        for (size_t i = 0; i < captured_.size(); ++i) {
            Captured& c = captured_[i];
            vmaInvalidateAllocation(ctx_.allocator, c.buffer.allocation, 0, VK_WHOLE_SIZE);
            const std::filesystem::path path = std::filesystem::path(capture_dir_) / std::format("present-{:04d}.bgra", i);
            std::ofstream out(path, std::ios::binary);
            out.write(static_cast<const char*>(c.buffer.mapped), static_cast<std::streamsize>(size_t(c.width) * c.height * 4));
            index << i << " " << c.width << " " << c.height << " " << (c.generated ? 1 : 0) << " " << c.frame << " " << c.ms << "\n";
            ctx_.DestroyBuffer(c.buffer);
        }
        LogInfo("frame generation: {} captured frames written to {}", captured_.size(), capture_dir_);
        captured_.clear();
        for (Captured& c : captured_motion_) {
            vmaInvalidateAllocation(ctx_.allocator, c.buffer.allocation, 0, VK_WHOLE_SIZE);
            std::ofstream out(std::filesystem::path(capture_dir_) / std::format("motion-{:06d}-{}x{}.rg16f", c.frame, c.width, c.height),
                              std::ios::binary);
            out.write(static_cast<const char*>(c.buffer.mapped), static_cast<std::streamsize>(size_t(c.width) * c.height * 4));
            ctx_.DestroyBuffer(c.buffer);
        }
        captured_motion_.clear();
        for (Captured& c : captured_ui_) {
            vmaInvalidateAllocation(ctx_.allocator, c.buffer.allocation, 0, VK_WHOLE_SIZE);
            std::ofstream out(std::filesystem::path(capture_dir_) / std::format("ui-{:04d}.rgba16f", c.frame), std::ios::binary);
            out.write(static_cast<const char*>(c.buffer.mapped), static_cast<std::streamsize>(size_t(c.width) * c.height * 8));
            ctx_.DestroyBuffer(c.buffer);
        }
        captured_ui_.clear();
    }

    static ffxReturnCode_t Generate(ffxDispatchDescFrameGeneration* params, void* user) {
        auto* self = static_cast<FsrFrameGeneration*>(user);
        if (self->reset_) {
            params->reset = true;
            self->reset_ = false;
        }
        ++self->generated_;
        return self->fn_.Dispatch(&self->fg_context_, &params->header);
    }

    bool Check(std::string& reason) {
        /* GPU check before the window check, so a GPU that can never run it says so even in a headless run. */
        if (!FsrFrameGenHardware(ctx_.physical, ctx_.properties, reason)) {
            return false;
        }
        if (!ctx_.surface) {
            reason = "needs a window";
            return false;
        }
        if (!queues_.present.queue || !queues_.acquire.queue) {
            reason = "the GPU has no second queue to present from";
            return false;
        }
        const std::filesystem::path path = ExecutableDir() / L"amd_fidelityfx_vk.dll";
        module_ = LoadLibraryW(path.c_str());
        if (!module_) {
            reason = "amd_fidelityfx_vk.dll is missing next to pt.exe";
            return false;
        }
        ffxLoadFunctions(&fn_, module_);
        if (!fn_.CreateContext || !fn_.DestroyContext || !fn_.Dispatch || !fn_.Query || !fn_.Configure) {
            reason = "amd_fidelityfx_vk.dll has no FidelityFX API exports";
            return false;
        }
        /* The SDK's presenter thread presents while the game thread may wait for idle; Vulkan wants both externally synchronised, so the DLL's imports get our mutex. */
        if (!PatchImport(module_, "vkQueuePresentKHR", &FfxQueuePresent, g_ffx_queue_present) ||
            !PatchImport(module_, "vkQueueWaitIdle", &FfxQueueWaitIdle, g_ffx_queue_wait_idle) ||
            !PatchImport(module_, "vkDeviceWaitIdle", &FfxDeviceWaitIdle, g_ffx_device_wait_idle)) {
            reason = "amd_fidelityfx_vk.dll does not import its present and wait functions from vulkan-1.dll";
            return false;
        }
        uint64_t count = 0;
        ffxQueryDescGetVersions versions{};
        versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        versions.outputCount = &count;
        if (fn_.Query(nullptr, &versions.header) != FFX_API_RETURN_OK || count == 0) {
            reason = "no FSR frame generation in amd_fidelityfx_vk.dll";
            return false;
        }
        reason.clear();
        return true;
    }

    void CreateGenerationContext() {
        DestroyGenerationContext();
        display_ = ctx_.swapchain.extent;
        format_ = ctx_.swapchain.format;
        ffxCreateBackendVKDesc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend.vkDevice = ctx_.device;
        backend.vkPhysicalDevice = ctx_.physical;
        backend.vkDeviceProcAddr = vkGetDeviceProcAddr;
        ffxCreateContextDescFrameGenerationHudless hudless{};
        hudless.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
        hudless.hudlessBackBufferFormat = ffxApiGetSurfaceFormatVK(format_);
        backend.header.pNext = &hudless.header;
        ffxCreateContextDescFrameGeneration desc{};
        desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        desc.header.pNext = &backend.header;
        desc.flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
        if (std::getenv("PT_UPSCALE_CHECK")) {
            desc.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
        }
        desc.displaySize = {display_.width, display_.height};
        desc.maxRenderSize = desc.displaySize;
        desc.backBufferFormat = ffxApiGetSurfaceFormatVK(format_);
        const auto create_start = std::chrono::steady_clock::now();
        const ffxReturnCode_t result = fn_.CreateContext(&fg_context_, &desc.header, nullptr);
        if (result != FFX_API_RETURN_OK) {
            LogError("frame generation: context creation failed ({})", result);
            fg_context_ = nullptr;
            return;
        }
        LogInfo("frame generation: context for {}x{} created in {:.0f} ms", display_.width, display_.height,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - create_start).count());
        hudless_.resize(ctx_.swapchain.images.size());
        for (vk::Image& image : hudless_) {
            if (!ctx_.CreateImage(image, format_, {display_.width, display_.height, 1},
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
                LogError("frame generation: hud-less target creation failed");
                DestroyGenerationContext();
                return;
            }
        }
        static const bool layer_wanted = [] {
            const char* e = std::getenv("PT_FG_UI_LAYER");
            return !e || std::atoi(e) != 0;
        }();
        if (layer_wanted) {
            layers_.resize(ctx_.swapchain.images.size());
            for (vk::Image& image : layers_) {
                if (!ctx_.CreateImage(image, Renderer::kUiLayerFormat, {display_.width, display_.height, 1},
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
                    LogWarn("frame generation: UI layer target creation failed, the UI is taken from the HUD-less difference");
                    for (vk::Image& made : layers_) {
                        ctx_.DestroyImage(made);
                    }
                    layers_.clear();
                    break;
                }
            }
        }
        reset_ = true;
        LogInfo("frame generation: FSR 3 frame generation for {}x{}, {} hud-less targets{}", display_.width, display_.height, hudless_.size(),
                layers_.empty() ? "" : ", film grain and UI laid over every frame");
    }

    void DestroyGenerationContext() {
        if (!fg_context_ && hudless_.empty()) {
            return;
        }
        vkDeviceWaitIdle(ctx_.device);
        if (fg_context_) {
            fn_.DestroyContext(&fg_context_, nullptr);
            fg_context_ = nullptr;
        }
        for (vk::Image& image : hudless_) {
            ctx_.DestroyImage(image);
        }
        hudless_.clear();
        if (layer_registered_) {
            RegisterUiResource(nullptr);
        }
        for (vk::Image& image : layers_) {
            ctx_.DestroyImage(image);
        }
        layers_.clear();
    }

    void RegisterUiResource(const vk::Image* image) {
        if (!swapchain_context_ || !swapchain_alive_) {
            layer_registered_ = false;
            return;
        }
        ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceVK ui{};
        ui.header.type = FFX_API_CONFIGURE_DESC_TYPE_FGSWAPCHAIN_REGISTERUIRESOURCE_VK;
        if (image) {
            ui.uiResource = ImageResource(image->image, image->format, {image->extent.width, image->extent.height},
                                          FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
            ui.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA | FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING;
        }
        const ffxReturnCode_t result = fn_.Configure(&swapchain_context_, &ui.header);
        if (result != FFX_API_RETURN_OK && !ui_error_logged_) {
            LogError("frame generation: UI resource registration failed ({})", result);
            ui_error_logged_ = true;
        }
        layer_registered_ = image != nullptr && result == FFX_API_RETURN_OK;
    }

    void DestroyContexts() {
        DestroyGenerationContext();
        if (swapchain_context_) {
            vkDeviceWaitIdle(ctx_.device);
            if (g_presenting == this) {
                g_presenting = nullptr;
            }
            fn_.DestroyContext(&swapchain_context_, nullptr);
            swapchain_context_ = nullptr;
            replace_ = {};
        }
        if (present_fence_) {
            vkDeviceWaitIdle(ctx_.device);
            vkDestroyFence(ctx_.device, present_fence_, nullptr);
            present_fence_ = VK_NULL_HANDLE;
        }
    }

    void LogSummary() {
        if (intervals_.empty()) {
            return;
        }
        std::vector<float> sorted = intervals_;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0.0;
        for (float v : sorted) {
            sum += v;
        }
        const float mean = static_cast<float>(sum / static_cast<double>(sorted.size()));
        const float p50 = sorted[sorted.size() / 2];
        const float p99 = sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)];
        LogInfo("frame generation: {} rendered, {} generated; interval mean {:.2f} median {:.2f} p99 {:.2f} max {:.2f} ms; "
                "{} acquire waits (longest {:.2f} ms), {} skipped-present signals dropped",
                sorted.size() + 1, generated_, mean, p50, p99, sorted.back(), acquire_waits_, acquire_wait_ms_, g_dropped_signals.exchange(0));
        intervals_.clear();
        generated_ = 0;
        acquire_waits_ = 0;
        acquire_wait_ms_ = 0.0f;
    }

    vk::Context& ctx_;
    FrameGenQueues queues_;
    HMODULE module_ = nullptr;
    ffxFunctions fn_{};
    ffxContext swapchain_context_ = nullptr;
    ffxContext fg_context_ = nullptr;
    ffxQueryDescSwapchainReplacementFunctionsVK replace_{};
    std::vector<vk::Image> hudless_;
    std::vector<vk::Image> layers_;
    bool layer_registered_ = false;
    bool ui_error_logged_ = false;
    VkExtent2D display_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    uint64_t frame_id_ = 0;
    uint64_t generated_ = 0;
    uint64_t acquire_waits_ = 0;
    float acquire_wait_ms_ = 0.0f;
    int acquire_errors_ = 0;
    int present_errors_ = 0;
    VkFence present_fence_ = VK_NULL_HANDLE;
    uint32_t debug_flags_ = 0;
    bool only_generated_ = false;
    bool checked_ = false;
    bool available_ = false;
    std::string reason_;
    bool generating_ = false;
    bool prepared_ = false;
    bool reset_ = true;
    bool error_logged_ = false;
    bool oversize_logged_ = false;
    bool swapchain_alive_ = false;
    Clock::time_point last_present_{};
    std::vector<float> intervals_;
    struct Captured {
        vk::Buffer buffer;
        uint32_t width = 0;
        uint32_t height = 0;
        bool generated = false;
        uint64_t frame = 0;
        double ms = 0.0;
    };
    std::string capture_dir_;
    uint32_t capture_count_ = 0;
    bool capture_armed_ = false;
    std::vector<Captured> captured_;
    std::vector<Captured> captured_motion_;
    std::vector<Captured> captured_ui_;
};

VKAPI_ATTR VkResult VKAPI_CALL WaitIdleAfterPresents(VkDevice device) {
    if (g_presenting) {
        g_presenting->WaitForPresents();
    }
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    return g_device_wait_idle(device);
}

}

std::unique_ptr<FrameGeneration> CreateFrameGeneration(vk::Context& ctx, const FrameGenQueues& queues) {
    return std::make_unique<FsrFrameGeneration>(ctx, queues);
}

}

#else

namespace pt {

std::unique_ptr<FrameGeneration> CreateFrameGeneration(vk::Context&, const FrameGenQueues&) {
    return nullptr;
}

}

#endif
