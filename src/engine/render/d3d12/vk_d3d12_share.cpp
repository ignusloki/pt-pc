#include "engine/render/d3d12/vk_d3d12_share.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>

namespace pt {

uint32_t XefgDxgiFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM: return 87;
    case VK_FORMAT_R8G8B8A8_UNORM: return 28;
    case VK_FORMAT_R32_SFLOAT: return 41;
    case VK_FORMAT_R16G16_SFLOAT: return 34;
    default: return 0;
    }
}

const char* XefgNoteKey(const std::string& reason) {
    if (reason.starts_with("needs Windows 10")) {
        return "pc_note_xefg_windows";
    }
    if (reason.starts_with("needs a DirectX 12 GPU")) {
        return "pc_note_xefg_gpu";
    }
    if (reason.starts_with("the graphics driver cannot share")) {
        return "pc_note_xefg_interop";
    }
    if (reason.starts_with("DLSS Frame Generation is loaded")) {
        return "pc_note_xefg_restart";
    }
    if (reason.starts_with("could not start")) {
        return "pc_note_xefg_failed";
    }
    if (reason.find("missing next to pt.exe") != std::string::npos || reason.find("not built into") != std::string::npos ||
        reason.starts_with("Windows only")) {
        return "pc_note_frame_generation_missing";
    }
    return "";
}

XefgMatrices XefgCameraMatrices(const FrameGenPrepare& p) {
    XefgMatrices m{};
    const glm::mat4 view = glm::lookAt(p.position, p.position + p.forward, p.up);
    const float aspect = p.render.height ? static_cast<float>(p.render.width) / static_cast<float>(p.render.height) : 1.0f;
    const float f = 1.0f / std::tan(p.fov_y * 0.5f);
    glm::mat4 projection(0.0f);
    projection[0][0] = f / aspect;
    projection[1][1] = f;
    projection[2][3] = -1.0f;
    projection[3][2] = p.near_plane;
    std::memcpy(m.view, &view[0][0], sizeof(m.view));
    std::memcpy(m.projection, &projection[0][0], sizeof(m.projection));
    return m;
}

}

#if defined(_WIN32) && defined(PT_WITH_XEFG)

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cstdlib>
#include <format>

#include "engine/core/log.h"
#include "engine/render/d3d12/present/d3d12_device.h"
#include "engine/render/d3d12/present/xefg_presenter.h"
#include "engine/render/renderer.h"
#include "engine/render/upscale/streamline.h"

namespace pt {
namespace {

using d3d12::ComPtr;

constexpr VkExternalMemoryHandleTypeFlagBits kMemoryHandle = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
constexpr VkExternalSemaphoreHandleTypeFlagBits kFenceHandle = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
constexpr VkImageUsageFlags kColorUsage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

void Barrier(VkCommandBuffer cmd, VkImage image, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkImageLayout old_layout,
             VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout new_layout, uint32_t src_family = VK_QUEUE_FAMILY_IGNORED,
             uint32_t dst_family = VK_QUEUE_FAMILY_IGNORED, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = old_layout;
    b.newLayout = new_layout;
    b.srcQueueFamilyIndex = src_family;
    b.dstQueueFamilyIndex = dst_family;
    b.image = image;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void BufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkAccessFlags2 src_access, VkAccessFlags2 dst_access) {
    VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.srcAccessMask = src_access;
    b.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.dstAccessMask = dst_access;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = buffer;
    b.size = VK_WHOLE_SIZE;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

HWND WindowHandle() {
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    HWND hwnd = nullptr;
    for (int i = 0; windows && i < count && !hwnd; ++i) {
        hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(windows[i]), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    }
    SDL_free(windows);
    return hwnd;
}

struct SharedImage {
    ComPtr<ID3D12Resource> resource;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    vk::Image image;
    bool Valid() const { return image.image != VK_NULL_HANDLE; }
};

class XefgFrameGeneration final : public FrameGeneration, public vk::SwapchainHooks {
public:
    explicit XefgFrameGeneration(vk::Context& ctx) : ctx_(ctx) {}
    ~XefgFrameGeneration() override { Shutdown(); }

    bool Available(std::string& reason) override {
        if (!checked_) {
            checked_ = true;
            available_ = Check(reason_);
            LogInfo("frame generation: Intel XeSS Frame Generation {}{}", available_ ? "available" : "unavailable: ", available_ ? "" : reason_);
        }
        reason = reason_;
        return available_;
    }

    bool Update(bool wanted) override {
        const bool owned = ctx_.SwapchainOwner() == this;
        bool recreate = false;
        if (wanted && !owned && ctx_.swapchain.color_space != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            if (!hdr_refused_) {
                LogWarn("frame generation: XeSS Frame Generation off while the native HDR output is on (the bridge presents SDR)");
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
            const bool on = !UpscaleHost::Get().MenuOpen();
            if (presenter_) {
                presenter_->SetGenerating(on);
            }
            generating_ = on && presenter_ && presenter_->Alive();
            return false;
        }
        generating_ = false;
        if (ctx_.swapchain_hooks == this) {
            ctx_.swapchain_hooks = nullptr;
            recreate = true;
        }
        if (!owned && presenter_ && presenter_->Alive()) {
            device_.WaitIdle();
            DestroySlots();
            presenter_->Destroy();
        }
        return recreate;
    }

    bool Generating() const override { return generating_; }

    FrameStartAction FrameStart(bool) override {
        if (ctx_.SwapchainOwner() == this && presenter_ && presenter_->Alive() && !frame_open_) {
            OpenFrame();
        }
        return FrameStartAction::Continue;
    }

    void Prepare(const FrameGenPrepare& p) override {
        if (!generating_ || ctx_.SwapchainOwner() != this || current_slot_ >= slots_.size()) {
            return;
        }
        if (p.depth.format != VK_FORMAT_D32_SFLOAT || p.motion.format != VK_FORMAT_R16G16_SFLOAT || p.depth.extent.width != p.motion.extent.width ||
            p.depth.extent.height != p.motion.extent.height) {
            if (!input_warned_) {
                LogWarn("xefg: unexpected inputs (depth format {}, motion format {}, {}x{} and {}x{}): no generation",
                        static_cast<int>(p.depth.format), static_cast<int>(p.motion.format), p.depth.extent.width, p.depth.extent.height,
                        p.motion.extent.width, p.motion.extent.height);
                input_warned_ = true;
            }
            return;
        }
        if (p.depth.extent.width != input_extent_.width || p.depth.extent.height != input_extent_.height) {
            if (!CreateInputs(p.depth.extent)) {
                Disable("the shared depth and motion images could not be created");
                return;
            }
        }
        Slot& s = slots_[current_slot_];
        VkCommandBuffer cmd = p.cmd;
        const VkExtent2D e = input_extent_;
        const VkImageLayout depth_layout = p.depth.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : p.depth.layout;
        const VkImageLayout motion_layout = p.motion.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : p.motion.layout;
        constexpr VkAccessFlags2 any = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        Barrier(cmd, p.depth.image, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, depth_layout, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                VK_IMAGE_ASPECT_DEPTH_BIT);
        Barrier(cmd, p.motion.image, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, motion_layout, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Barrier(cmd, s.depth.image.image, VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        Barrier(cmd, s.motion.image.image, VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        BufferBarrier(cmd, staging_.buffer, VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        region.imageExtent = {e.width, e.height, 1};
        vkCmdCopyImageToBuffer(cmd, p.depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging_.buffer, 1, &region);
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstSubresource = copy.srcSubresource;
        copy.extent = {e.width, e.height, 1};
        vkCmdCopyImage(cmd, p.motion.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.motion.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        BufferBarrier(cmd, staging_.buffer, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vkCmdCopyBufferToImage(cmd, staging_.buffer, s.depth.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        Barrier(cmd, p.depth.image, VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, any,
                depth_layout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, VK_IMAGE_ASPECT_DEPTH_BIT);
        Barrier(cmd, p.motion.image, VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, any,
                motion_layout);
        for (SharedImage* image : {&s.depth, &s.motion}) {
            Barrier(cmd, image->image.image, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_GENERAL, ctx_.queue_family, VK_QUEUE_FAMILY_EXTERNAL);
        }
        const XefgMatrices m = XefgCameraMatrices(p);
        std::memcpy(constants_.view, m.view, sizeof(m.view));
        std::memcpy(constants_.projection, m.projection, sizeof(m.projection));
        constants_.jitter[0] = p.jitter.x;
        constants_.jitter[1] = p.jitter.y;
        constants_.motion_scale[0] = p.motion_scale.x;
        constants_.motion_scale[1] = p.motion_scale.y;
        constants_.reset = p.reset || reset_;
        constants_.frame_ms = p.frame_ms;
        reset_ = false;
        s.inputs = true;
        prepared_ = true;
    }

    const vk::Image* Present(uint32_t image_index) override {
        if (ctx_.SwapchainOwner() != this || image_index >= slots_.size()) {
            prepared_ = false;
            return nullptr;
        }
        Slot& s = slots_[image_index];
        s.hud = generating_ && prepared_ && s.inputs;
        prepared_ = false;
        return s.hud ? &s.hudless.image : nullptr;
    }

    const vk::Image* UiLayer(uint32_t image_index) override {
        if (ctx_.SwapchainOwner() != this || image_index >= slots_.size() || !slots_[image_index].hud || !slots_[image_index].layer.image.image) {
            return nullptr;
        }
        slots_[image_index].layer_drawn = true;
        return &slots_[image_index].layer.image;
    }

    void Shutdown() override {
        if (!ctx_.device) {
            return;
        }
        if (ctx_.swapchain_hooks == this) {
            ctx_.swapchain_hooks = nullptr;
        }
        if (ctx_.SwapchainOwner() == this) {
            vkDeviceWaitIdle(ctx_.device);
            ctx_.DestroySwapchain();
        }
        if (device_.Valid()) {
            device_.WaitIdle();
        }
        DestroySlots();
        DestroyInputs();
        presenter_.reset();
        if (timeline_) {
            vkDestroySemaphore(ctx_.device, timeline_, nullptr);
            timeline_ = VK_NULL_HANDLE;
        }
        if (pool_) {
            vkDestroyCommandPool(ctx_.device, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
        shared_fence_.Reset();
        device_.Destroy();
        started_ = false;
        generating_ = false;
    }

    VkResult CreateSwapchain(const VkSwapchainCreateInfoKHR& info, VkSwapchainKHR& handle) override {
        if (!Start()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        const uint32_t format = XefgDxgiFormat(info.imageFormat);
        const HWND window = WindowHandle();
        if (!format || info.imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR || !window) {
            Disable(!window ? "the window has no Win32 handle" : "the swapchain format cannot be presented through XeSS Frame Generation");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        device_.WaitIdle();
        DestroySlots();
        d3d12::SwapchainDesc desc;
        desc.window = window;
        desc.width = info.imageExtent.width;
        desc.height = info.imageExtent.height;
        desc.format = static_cast<DXGI_FORMAT>(format);
        desc.buffer_count = std::clamp(info.minImageCount, 2u, 8u);
        desc.vsync = info.presentMode == VK_PRESENT_MODE_FIFO_KHR || info.presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        static const bool layer_wanted = [] {
            const char* e = std::getenv("PT_FG_UI_LAYER");
            return !e || std::atoi(e) != 0;
        }();
        desc.ui_layer = layer_wanted;
        std::string error;
        if (!presenter_->Create(desc, error)) {
            Disable(error.c_str());
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        slots_.resize(desc.buffer_count);
        for (uint32_t i = 0; i < desc.buffer_count; ++i) {
            Slot& s = slots_[i];
            if (!Share(info.imageFormat, info.imageExtent, kColorUsage, std::format(L"pt frame {}", i).c_str(), s.color) ||
                !Share(info.imageFormat, info.imageExtent, kColorUsage, std::format(L"pt hud-less {}", i).c_str(), s.hudless) ||
                (desc.ui_layer &&
                 !Share(Renderer::kUiLayerFormat, info.imageExtent, kColorUsage, std::format(L"pt ui layer {}", i).c_str(), s.layer))) {
                DestroySlots();
                presenter_->Destroy();
                Disable("the shared frame images could not be created");
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool = pool_;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            vkAllocateCommandBuffers(ctx_.device, &alloc, &s.release);
        }
        DestroyInputs();
        next_slot_ = 0;
        current_slot_ = UINT32_MAX;
        reset_ = true;
        handle = reinterpret_cast<VkSwapchainKHR>(this);
        LogInfo("xefg: bridge swapchain {}x{}, {} shared images (format {}), v-sync {}", desc.width, desc.height, desc.buffer_count,
                static_cast<int>(info.imageFormat), desc.vsync ? "on" : "off");
        return VK_SUCCESS;
    }

    void DestroySwapchain(VkSwapchainKHR) override {
        vkDeviceWaitIdle(ctx_.device);
        device_.WaitIdle();
        DestroySlots();
        if (ctx_.swapchain_hooks != this && presenter_) {
            DestroyInputs();
            presenter_->Destroy();
        }
        generating_ = false;
    }

    VkResult SwapchainImages(VkSwapchainKHR, uint32_t* count, VkImage* images) override {
        if (!images) {
            *count = static_cast<uint32_t>(slots_.size());
            return VK_SUCCESS;
        }
        const uint32_t n = std::min<uint32_t>(*count, static_cast<uint32_t>(slots_.size()));
        for (uint32_t i = 0; i < n; ++i) {
            images[i] = slots_[i].color.image.image;
        }
        *count = n;
        return n < slots_.size() ? VK_INCOMPLETE : VK_SUCCESS;
    }

    VkResult AcquireNextImage(VkSwapchainKHR, VkSemaphore semaphore, uint32_t* index) override {
        if (!presenter_ || !presenter_->Alive() || slots_.empty()) {
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        if (!frame_open_) {
            OpenFrame();
        }
        const uint32_t slot = next_slot_;
        if (!device_.WaitFence(presenter_->ConsumedFence(), slots_[slot].consumed, 2000)) {
            if (acquire_timeouts_++ < 5) {
                LogWarn("xefg: shared image {} still in use by the D3D12 queue after 2 s", slot);
            }
            return VK_TIMEOUT;
        }
        presenter_->Marker(d3d12::LatencyMarker::SimulationEnd, frame_id_);
        presenter_->Marker(d3d12::LatencyMarker::RenderSubmitStart, frame_id_);
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = semaphore;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        const VkResult result = vkQueueSubmit2(ctx_.queue, 1, &submit, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) {
            return result;
        }
        *index = slot;
        current_slot_ = slot;
        next_slot_ = (slot + 1) % static_cast<uint32_t>(slots_.size());
        slots_[slot].hud = false;
        slots_[slot].layer_drawn = false;
        slots_[slot].inputs = false;
        prepared_ = false;
        return VK_SUCCESS;
    }

    VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR& info) override {
        const uint32_t index = info.swapchainCount ? info.pImageIndices[0] : UINT32_MAX;
        if (index >= slots_.size() || !presenter_ || !presenter_->Alive()) {
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        Slot& s = slots_[index];
        presenter_->Marker(d3d12::LatencyMarker::RenderSubmitEnd, frame_id_);
        vkResetCommandBuffer(s.release, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(s.release, &begin);
        Barrier(s.release, s.color.image.image, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, PresentLayout(),
                VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_GENERAL, ctx_.queue_family, VK_QUEUE_FAMILY_EXTERNAL);
        if (s.hud) {
            Barrier(s.release, s.hudless.image.image, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_GENERAL, ctx_.queue_family,
                    VK_QUEUE_FAMILY_EXTERNAL);
        }
        const bool layer = s.hud && s.layer_drawn;
        if (layer) {
            Barrier(s.release, s.layer.image.image, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_GENERAL, ctx_.queue_family,
                    VK_QUEUE_FAMILY_EXTERNAL);
        }
        vkEndCommandBuffer(s.release);
        std::vector<VkSemaphoreSubmitInfo> waits(info.waitSemaphoreCount, {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO});
        for (uint32_t i = 0; i < info.waitSemaphoreCount; ++i) {
            waits[i].semaphore = info.pWaitSemaphores[i];
            waits[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = timeline_;
        signal.value = ++timeline_value_;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo cmd{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd.commandBuffer = s.release;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
        submit.pWaitSemaphoreInfos = waits.data();
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        const VkResult submitted = vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE);
        if (submitted != VK_SUCCESS) {
            return submitted;
        }
        d3d12::FrameInputs in;
        in.color = s.color.resource.Get();
        in.hudless = s.hud ? s.hudless.resource.Get() : nullptr;
        in.ui = layer ? s.layer.resource.Get() : nullptr;
        in.depth = s.hud ? s.depth.resource.Get() : nullptr;
        in.motion = s.hud ? s.motion.resource.Get() : nullptr;
        in.input_width = input_extent_.width;
        in.input_height = input_extent_.height;
        in.constants = constants_;
        in.wait_fence = shared_fence_.Get();
        in.wait_value = timeline_value_;
        in.frame_id = frame_id_;
        uint64_t consumed = 0;
        const d3d12::PresentResult result = presenter_->Present(in, consumed);
        s.consumed = consumed;
        frame_open_ = false;
        if (result == d3d12::PresentResult::Lost) {
            std::string why;
            device_.Removed(&why);
            Disable(why.empty() ? "the DirectX 12 present failed" : why.c_str());
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        return VK_SUCCESS;
    }

    VkImageLayout PresentLayout() const override { return VK_IMAGE_LAYOUT_GENERAL; }

private:
    struct Slot {
        SharedImage color;
        SharedImage hudless;
        SharedImage layer;
        bool layer_drawn = false;
        SharedImage depth;
        SharedImage motion;
        VkCommandBuffer release = VK_NULL_HANDLE;
        uint64_t consumed = 0;
        bool hud = false;
        bool inputs = false;
    };

    bool Check(std::string& reason) {
        const uint32_t build = d3d12::WindowsBuild();
        if (!XefgWindowsBuildSupported(build)) {
            reason = std::format("needs Windows 10 version 21H1 (build {}) or newer; this is build {}", kXefgMinWindowsBuild, build);
            return false;
        }
        if (!ctx_.surface) {
            reason = "needs a window";
            return false;
        }
        if (streamline::Active()) {
            reason = "DLSS Frame Generation is loaded for this start; restart to switch to XeSS";
            return false;
        }
        for (const char* name : XefgDeviceExtensions()) {
            if (!UpscaleHost::Get().Enabled(name, true)) {
                reason = std::format("the graphics driver cannot share images or fences between Vulkan and DirectX 12 ({} missing)", name);
                return false;
            }
        }
        if (!InteropSupported(reason)) {
            return false;
        }
        if (!d3d12::XefgPresenter::LibrariesPresent(reason)) {
            return false;
        }
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &id;
        vkGetPhysicalDeviceProperties2(ctx_.physical, &properties);
        if (!id.deviceLUIDValid) {
            reason = "the graphics driver cannot share images or fences between Vulkan and DirectX 12 (no adapter LUID)";
            return false;
        }
        std::string error;
        if (!device_.Create(id.deviceLUID, error)) {
            reason = "needs a DirectX 12 GPU with Shader Model 6.4 (" + error + ")";
            return false;
        }
        if (!device_.ShaderModel(D3D_SHADER_MODEL_6_4)) {
            reason = std::format("needs a DirectX 12 GPU with Shader Model 6.4 ({} has less)", device_.AdapterName());
            device_.Destroy();
            return false;
        }
        reason.clear();
        return true;
    }

    bool InteropSupported(std::string& reason) {
        for (VkFormat format : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16_SFLOAT}) {
            VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
            external.handleType = kMemoryHandle;
            VkPhysicalDeviceImageFormatInfo2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
            query.pNext = &external;
            query.format = format;
            query.type = VK_IMAGE_TYPE_2D;
            query.tiling = VK_IMAGE_TILING_OPTIMAL;
            query.usage = kColorUsage;
            VkExternalImageFormatProperties external_props{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
            VkImageFormatProperties2 props{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
            props.pNext = &external_props;
            const VkResult r = vkGetPhysicalDeviceImageFormatProperties2(ctx_.physical, &query, &props);
            if (r != VK_SUCCESS || !(external_props.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
                reason = std::format("the graphics driver cannot share images between Vulkan and DirectX 12 (format {}: {})",
                                     static_cast<int>(format), vk::ResultName(r));
                return false;
            }
        }
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkPhysicalDeviceExternalSemaphoreInfo query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};
        query.pNext = &type;
        query.handleType = kFenceHandle;
        VkExternalSemaphoreProperties props{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
        vkGetPhysicalDeviceExternalSemaphoreProperties(ctx_.physical, &query, &props);
        if (!(props.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)) {
            reason = "the graphics driver cannot share fences between Vulkan and DirectX 12 (D3D12 fence import)";
            return false;
        }
        return true;
    }

    bool Start() {
        if (started_) {
            return true;
        }
        std::string reason;
        if (!Available(reason)) {
            return false;
        }
        if (!device_.Valid() || device_.Removed()) {
            VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties.pNext = &id;
            vkGetPhysicalDeviceProperties2(ctx_.physical, &properties);
            std::string error;
            if (!device_.Create(id.deviceLUID, error)) {
                Disable(error.c_str());
                return false;
            }
        }
        if (!presenter_) {
            presenter_ = std::make_unique<d3d12::XefgPresenter>(device_);
        }
        std::string error;
        if (!presenter_->Load(error)) {
            Disable(error.c_str());
            return false;
        }
        HANDLE handle = nullptr;
        if (!device_.CreateSharedFence(shared_fence_, handle)) {
            Disable("the shared fence could not be created");
            return false;
        }
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        create.pNext = &type;
        VkResult r = vkCreateSemaphore(ctx_.device, &create, nullptr, &timeline_);
        if (r == VK_SUCCESS) {
            VkImportSemaphoreWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
            import.semaphore = timeline_;
            import.handleType = kFenceHandle;
            import.handle = handle;
            r = vkImportSemaphoreWin32HandleKHR(ctx_.device, &import);
        }
        CloseHandle(handle);
        if (r != VK_SUCCESS) {
            LogError("xefg: the D3D12 fence could not be imported as a timeline semaphore ({})", vk::ResultName(r));
            Disable("the shared fence could not be imported into Vulkan");
            return false;
        }
        timeline_value_ = 0;
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = ctx_.queue_family;
        vkCreateCommandPool(ctx_.device, &pool, nullptr, &pool_);
        started_ = true;
        LogInfo("xefg: bridge started on {}: shared D3D12 fence imported as a timeline semaphore", device_.AdapterName());
        return true;
    }

    bool Share(VkFormat format, VkExtent2D extent, VkImageUsageFlags usage, const wchar_t* name, SharedImage& out) {
        HANDLE handle = nullptr;
        const DXGI_FORMAT dxgi = format == VK_FORMAT_R16G16B16A16_SFLOAT ? DXGI_FORMAT_R16G16B16A16_FLOAT : static_cast<DXGI_FORMAT>(XefgDxgiFormat(format));
        if (!device_.CreateSharedTexture(dxgi, extent.width, extent.height, name, out.resource, handle)) {
            return false;
        }
        VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        external.handleTypes = kMemoryHandle;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.pNext = &external;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {extent.width, extent.height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult r = vkCreateImage(ctx_.device, &info, nullptr, &out.image.image);
        VkMemoryWin32HandlePropertiesKHR handle_props{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
        if (r == VK_SUCCESS) {
            r = vkGetMemoryWin32HandlePropertiesKHR(ctx_.device, kMemoryHandle, handle, &handle_props);
        }
        if (r == VK_SUCCESS) {
            VkMemoryDedicatedRequirements dedicated_req{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
            VkMemoryRequirements2 req{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
            req.pNext = &dedicated_req;
            VkImageMemoryRequirementsInfo2 req_info{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
            req_info.image = out.image.image;
            vkGetImageMemoryRequirements2(ctx_.device, &req_info, &req);
            VkPhysicalDeviceMemoryProperties memory{};
            vkGetPhysicalDeviceMemoryProperties(ctx_.physical, &memory);
            const uint32_t bits = req.memoryRequirements.memoryTypeBits & handle_props.memoryTypeBits;
            uint32_t type = UINT32_MAX;
            for (uint32_t i = 0; i < memory.memoryTypeCount && type == UINT32_MAX; ++i) {
                if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                    type = i;
                }
            }
            for (uint32_t i = 0; i < memory.memoryTypeCount && type == UINT32_MAX; ++i) {
                if (bits & (1u << i)) {
                    type = i;
                }
            }
            VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
            dedicated.image = out.image.image;
            VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
            import.pNext = &dedicated;
            import.handleType = kMemoryHandle;
            import.handle = handle;
            VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            alloc.pNext = &import;
            alloc.allocationSize = req.memoryRequirements.size;
            alloc.memoryTypeIndex = type;
            r = type == UINT32_MAX ? VK_ERROR_FEATURE_NOT_PRESENT : vkAllocateMemory(ctx_.device, &alloc, nullptr, &out.memory);
        }
        CloseHandle(handle);
        if (r == VK_SUCCESS) {
            r = vkBindImageMemory(ctx_.device, out.image.image, out.memory, 0);
        }
        if (r == VK_SUCCESS) {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = out.image.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            r = vkCreateImageView(ctx_.device, &view, nullptr, &out.image.view);
        }
        if (r != VK_SUCCESS) {
            LogError("xefg: importing the shared {}x{} image (format {}) into Vulkan failed ({})", extent.width, extent.height,
                     static_cast<int>(format), vk::ResultName(r));
            Unshare(out);
            return false;
        }
        out.image.format = format;
        out.image.extent = {extent.width, extent.height, 1};
        out.image.usage = usage;
        return true;
    }

    void Unshare(SharedImage& image) {
        if (image.image.view) {
            vkDestroyImageView(ctx_.device, image.image.view, nullptr);
        }
        if (image.image.image) {
            vkDestroyImage(ctx_.device, image.image.image, nullptr);
        }
        if (image.memory) {
            vkFreeMemory(ctx_.device, image.memory, nullptr);
        }
        image = SharedImage{};
    }

    bool CreateInputs(VkExtent2D extent) {
        vkDeviceWaitIdle(ctx_.device);
        device_.WaitIdle();
        DestroyInputs();
        const VkImageUsageFlags usage = kColorUsage;
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (!Share(VK_FORMAT_R32_SFLOAT, extent, usage, std::format(L"pt depth {}", i).c_str(), slots_[i].depth) ||
                !Share(VK_FORMAT_R16G16_SFLOAT, extent, usage, std::format(L"pt motion {}", i).c_str(), slots_[i].motion)) {
                DestroyInputs();
                return false;
            }
        }
        if (!ctx_.CreateBuffer(staging_, static_cast<VkDeviceSize>(extent.width) * extent.height * 4,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false)) {
            DestroyInputs();
            return false;
        }
        input_extent_ = extent;
        reset_ = true;
        LogInfo("xefg: shared depth and motion images {}x{}", extent.width, extent.height);
        return true;
    }

    void DestroyInputs() {
        for (Slot& s : slots_) {
            Unshare(s.depth);
            Unshare(s.motion);
        }
        if (staging_.buffer) {
            ctx_.DestroyBuffer(staging_);
        }
        staging_ = {};
        input_extent_ = {};
    }

    void DestroySlots() {
        DestroyInputs();
        for (Slot& s : slots_) {
            Unshare(s.color);
            Unshare(s.hudless);
            Unshare(s.layer);
            if (s.release) {
                vkFreeCommandBuffers(ctx_.device, pool_, 1, &s.release);
            }
        }
        slots_.clear();
        current_slot_ = UINT32_MAX;
    }

    void OpenFrame() {
        ++frame_id_;
        frame_open_ = true;
        presenter_->FrameStart(frame_id_);
    }

    void Disable(const char* why) {
        LogError("frame generation: XeSS Frame Generation turned off: {}", why);
        available_ = false;
        checked_ = true;
        reason_ = std::string("could not start and was turned off (") + why + ")";
        generating_ = false;
        if (ctx_.swapchain_hooks == this) {
            ctx_.swapchain_hooks = nullptr;
        }
    }

    vk::Context& ctx_;
    d3d12::Device device_;
    std::unique_ptr<d3d12::XefgPresenter> presenter_;
    ComPtr<ID3D12Fence> shared_fence_;
    VkSemaphore timeline_ = VK_NULL_HANDLE;
    uint64_t timeline_value_ = 0;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    vk::Buffer staging_;
    std::vector<Slot> slots_;
    VkExtent2D input_extent_{};
    uint32_t next_slot_ = 0;
    uint32_t current_slot_ = UINT32_MAX;
    uint32_t frame_id_ = 0;
    d3d12::FrameConstants constants_;
    std::string reason_;
    int acquire_timeouts_ = 0;
    bool checked_ = false;
    bool available_ = false;
    bool started_ = false;
    bool generating_ = false;
    bool prepared_ = false;
    bool frame_open_ = false;
    bool reset_ = true;
    bool hdr_refused_ = false;
    bool input_warned_ = false;
};

}

std::unique_ptr<FrameGeneration> CreateXefgFrameGeneration(vk::Context& ctx) {
    return std::make_unique<XefgFrameGeneration>(ctx);
}

std::vector<const char*> XefgDeviceExtensions() {
    return {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
}

}

#else

namespace pt {

std::unique_ptr<FrameGeneration> CreateXefgFrameGeneration(vk::Context&) {
    return nullptr;
}

std::vector<const char*> XefgDeviceExtensions() {
    return {};
}

}

#endif
