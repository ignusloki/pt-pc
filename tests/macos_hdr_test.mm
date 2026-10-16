#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/render/vk_context.h"

namespace {

int failures = 0;
using MetadataConstructor = id (*)(id, SEL, NSData*, NSData*, float);
MetadataConstructor original_constructor;
float observed_scale = 0.0f;
int observed_calls = 0;
NSData* observed_display;
NSData* observed_content;

void Expect(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    failures += !ok;
}

id ObserveMetadata(id receiver, SEL selector, NSData* display, NSData* content, float scale) {
    ++observed_calls;
    observed_scale = scale;
    observed_display = [display copy];
    observed_content = [content copy];
    return original_constructor(receiver, selector, display, content, scale);
}

struct MetadataObserver {
    Method method;
    MetadataObserver() {
        method = class_getClassMethod(CAEDRMetadata.class, @selector(HDR10MetadataWithDisplayInfo:contentInfo:opticalOutputScale:));
        original_constructor = reinterpret_cast<MetadataConstructor>(method_getImplementation(method));
        method_setImplementation(method, reinterpret_cast<IMP>(ObserveMetadata));
    }
    ~MetadataObserver() {
        method_setImplementation(method, reinterpret_cast<IMP>(original_constructor));
    }
};

void ResetObservation() {
    observed_calls = 0;
    observed_scale = 0;
    observed_display = nil;
    observed_content = nil;
}

uint16_t ContentValue(size_t offset) {
    if (observed_content.length < offset + sizeof(uint16_t)) return 0;
    uint16_t value;
    std::memcpy(&value, static_cast<const uint8_t*>(observed_content.bytes) + offset, sizeof(value));
    return CFSwapInt16BigToHost(value);
}

float MasteringPeak() {
    if (observed_display.length != 24) return 0;
    uint32_t value;
    std::memcpy(&value, static_cast<const uint8_t*>(observed_display.bytes) + 16, sizeof(value));
    return CFSwapInt32BigToHost(value) / 10000.0f;
}

CAMetalLayer* WindowLayer(SDL_Window* window) {
    const auto props = SDL_GetWindowProperties(window);
    NSWindow* native = (__bridge NSWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    const auto tag = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_COCOA_METAL_VIEW_TAG_NUMBER, 0);
    NSView* metal = [native.contentView viewWithTag:static_cast<NSInteger>(tag)];
    return [metal.layer isKindOfClass:CAMetalLayer.class] ? static_cast<CAMetalLayer*>(metal.layer) : nil;
}

bool Create(pt::vk::Context& ctx, bool hdr, const char* label) {
    const bool created = ctx.CreateSwapchain(128, 128, true, hdr);
    Expect(created, label);
    return created;
}

int Run() {
    id<MTLDevice> metal = MTLCreateSystemDefaultDevice();
    if (!metal) {
        std::puts("SKIP macOS HDR presentation: no Metal device (GPU-less host)");
        return 0;
    }
    std::printf("macOS HDR presentation: Metal device %s\n", metal.name.UTF8String);
    std::fflush(stdout);
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("FAIL SDL video: %s\n", SDL_GetError());
        return 1;
    }
    if (const std::string library = pt::vk::VulkanLibraryPath(); !library.empty())
        SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, library.c_str());
    SDL_Window* window = SDL_CreateWindow("P.T. HDR regression", 128, 128, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::printf("FAIL SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    pt::vk::Context ctx;
    if (!ctx.Init(window, false, false)) {
        std::puts("FAIL Vulkan context");
        ctx.Shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    {
        // Observe the public Apple constructor in this test process: CAEDRMetadata does not expose its scale as a property.
        MetadataObserver observer;
        if (Create(ctx, false, "a default SDR start succeeds")) {
            Expect(WindowLayer(window).EDRMetadata == nil, "a default SDR start has no HDR metadata");
        }
        if (Create(ctx, true, "native HDR swapchain starts")) {
            CAMetalLayer* layer = WindowLayer(window);
            Expect(layer != nil, "inspect the actual SDL Vulkan Metal layer");
            Expect(ctx.swapchain.color_space == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT &&
                       ctx.swapchain.format == VK_FORMAT_R16G16B16A16_SFLOAT, "HDR uses floating point scRGB");
            Expect(layer.wantsExtendedDynamicRangeContent && layer.EDRMetadata != nil, "HDR metadata reaches the display layer");
            Expect(observed_calls > 0 && observed_scale == 80.0f, "final metadata scale is 80 nits per linear unit, not MoltenVK's 1");
            Expect(observed_display.length == 24 && observed_content.length == 4, "mastering and content-light messages keep their standard sizes");

            for (int paper : {80, 200, 300, 500}) {
                ResetObservation();
                ctx.hdr_paper_nits = static_cast<float>(paper);
                ctx.hdr_peak_setting = 1500;
                ctx.SetHdrMetadata();
                const float white = paper / 80.0f;
                Expect(observed_calls > 0 && std::abs(white * observed_scale - paper) < 0.001f,
                       "paper white's scRGB buffer value is described in the intended nits");
                Expect(ContentValue(2) == paper && ContentValue(0) == 1500 && MasteringPeak() == 1500,
                       "level changes preserve paper white, content peak and mastering peak");
            }

            const bool has_extension = ctx.hdr_metadata;
            ctx.hdr_metadata = false;
            ResetObservation();
            ctx.SetHdrMetadata();
            Expect(observed_calls == 1 && observed_scale == 80.0f, "native metadata also works when VK_EXT_hdr_metadata is unavailable");
            ctx.hdr_metadata = has_extension;

            ResetObservation();
            if (Create(ctx, false, "HDR switches to SDR without restarting")) {
                layer = WindowLayer(window);
                Expect(ctx.swapchain.color_space == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "SDR uses its normal color space");
                Expect(layer.EDRMetadata == nil && !layer.wantsExtendedDynamicRangeContent, "SDR clears the previous HDR metadata and EDR request");
                Expect(observed_calls == 0, "SDR does not attach HDR metadata");
            }
            ResetObservation();
            if (Create(ctx, true, "SDR switches back to HDR")) {
                Expect(observed_calls > 0 && observed_scale == 80.0f && WindowLayer(window).EDRMetadata != nil,
                       "HDR re-enables metadata with the correct scale");
            }
            Expect(SDL_SetWindowSize(window, 160, 144) && SDL_SyncWindow(window), "native resize completes");
            ResetObservation();
            if (Create(ctx, true, "HDR swapchain is recreated after resize")) {
                Expect(observed_calls > 0 && observed_scale == 80.0f, "resizing retains the HDR brightness scale");
            }
            // Returning through SDR more than once catches metadata left behind by replacement swapchains.
            if (Create(ctx, false, "final return to SDR")) {
                Expect(WindowLayer(window).EDRMetadata == nil, "replacement SDR swapchain has no stale HDR metadata");
            }
        }
    }
    ctx.Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("macOS HDR presentation: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}

}

int main() {
    @autoreleasepool {
        return Run();
    }
}
