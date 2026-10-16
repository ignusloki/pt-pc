#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "engine/render/macos_hdr.h"

namespace pt::vk {
namespace {

CAMetalLayer* MetalLayer(NSView* view) {
    if ([view.layer isKindOfClass:CAMetalLayer.class]) return static_cast<CAMetalLayer*>(view.layer);
    for (NSView* child in view.subviews) {
        if (CAMetalLayer* layer = MetalLayer(child)) return layer;
    }
    return nil;
}

CAMetalLayer* WindowLayer(SDL_Window* window) {
    if (!window || !NSThread.isMainThread) return nil;
    const auto props = SDL_GetWindowProperties(window);
    NSWindow* cocoa = (__bridge NSWindow*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    const auto tag = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_COCOA_METAL_VIEW_TAG_NUMBER, 0);
    NSView* metal = tag ? [cocoa.contentView viewWithTag:static_cast<NSInteger>(tag)] : nil;
    return MetalLayer(metal ?: cocoa.contentView);
}

// HDR10 MDCV and CLLI messages, in the byte order expected by CAEDRMetadata (ISO/IEC 23008-2).
struct DisplayInfo {
    uint16_t primaries[3][2]; // green, blue, red
    uint16_t white[2];
    uint32_t maximum;
    uint32_t minimum;
};
struct ContentInfo {
    uint16_t maximum;
    uint16_t average;
};
static_assert(sizeof(DisplayInfo) == 24 && sizeof(ContentInfo) == 4);

uint16_t Unorm16(float value) {
    return CFSwapInt16HostToBig(static_cast<uint16_t>(std::clamp(std::lround(value), 0l, 65535l)));
}

}

bool SetMacHdrMetadata(SDL_Window* window, VkColorSpaceKHR color_space, const VkHdrMetadataEXT* metadata) {
    // Normalized PQ surfaces already have an absolute luminance encoding; only floating point scRGB needs this adapter.
    if (color_space == VK_COLOR_SPACE_HDR10_ST2084_EXT) return true;
    @autoreleasepool {
        CAMetalLayer* layer = WindowLayer(window);
        if (!layer) return false;
        if (color_space != VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT || !metadata) {
            layer.EDRMetadata = nil;
            layer.wantsExtendedDynamicRangeContent = NO;
            return true;
        }

        DisplayInfo display{};
        const VkXYColorEXT primaries[] = {metadata->displayPrimaryGreen, metadata->displayPrimaryBlue, metadata->displayPrimaryRed};
        for (int i = 0; i < 3; ++i) {
            display.primaries[i][0] = Unorm16(primaries[i].x * 50000.0f);
            display.primaries[i][1] = Unorm16(primaries[i].y * 50000.0f);
        }
        display.white[0] = Unorm16(metadata->whitePoint.x * 50000.0f);
        display.white[1] = Unorm16(metadata->whitePoint.y * 50000.0f);
        display.maximum = CFSwapInt32HostToBig(static_cast<uint32_t>(std::lround(metadata->maxLuminance * 10000.0f)));
        display.minimum = CFSwapInt32HostToBig(static_cast<uint32_t>(std::lround(metadata->minLuminance * 10000.0f)));
        const ContentInfo content{Unorm16(metadata->maxContentLightLevel), Unorm16(metadata->maxFrameAverageLightLevel)};

        // MoltenVK 1.4.2 supplies opticalOutputScale=1 to Core Animation. composite.frag writes nits / 80, so 1 would
        // describe an intended 200-nit white (2.5 in the buffer) as 2.5 nits. Preserve the metadata with the correct scale.
        layer.EDRMetadata = [CAEDRMetadata HDR10MetadataWithDisplayInfo:[NSData dataWithBytes:&display length:sizeof(display)]
            contentInfo:[NSData dataWithBytes:&content length:sizeof(content)] opticalOutputScale:80.0f];
        layer.wantsExtendedDynamicRangeContent = YES;
        return layer.EDRMetadata != nil;
    }
}

}
