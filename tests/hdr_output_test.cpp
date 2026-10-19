#include "engine/render/hdr_output.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
        failures += !ok;
    };

    const std::array<VkSurfaceFormatKHR, 3> all_formats = {{{VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT},
                                                            {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
                                                            {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT}}};
    VkSurfaceFormatKHR selected{};
    bool hdr = false;
    check(pt::vk::ChooseSwapchainSurfaceFormat(all_formats, true, true, selected, hdr) && hdr &&
              selected.format == VK_FORMAT_R16G16B16A16_SFLOAT && selected.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
          "scRGB is preferred when both HDR formats are available");
    check(pt::vk::ChooseSwapchainSurfaceFormat(all_formats, true, true, selected, hdr, false) && hdr &&
              selected.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 && selected.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT,
          "DLSS frame generation uses HDR10 instead of unsupported scRGB");
    const std::array<VkSurfaceFormatKHR, 2> scrgb_only = {all_formats[1], all_formats[2]};
    check(pt::vk::ChooseSwapchainSurfaceFormat(scrgb_only, true, true, selected, hdr, false) && !hdr &&
              selected.format == VK_FORMAT_B8G8R8A8_UNORM,
          "DLSS frame generation falls back to SDR when HDR10 is unavailable");
    const std::array<VkSurfaceFormatKHR, 2> hdr10_formats = {all_formats[0], all_formats[1]};
    check(pt::vk::ChooseSwapchainSurfaceFormat(hdr10_formats, true, true, selected, hdr) && hdr &&
              selected.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32,
          "HDR10 is selected when scRGB is unavailable");
    check(pt::vk::ChooseSwapchainSurfaceFormat(hdr10_formats, true, false, selected, hdr) && !hdr &&
              selected.format == VK_FORMAT_B8G8R8A8_UNORM,
          "swapchain hooks fall back to SDR");
    const std::array<VkSurfaceFormatKHR, 1> unrestricted = {{{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}}};
    check(pt::vk::ChooseSwapchainSurfaceFormat(unrestricted, false, false, selected, hdr) &&
              selected.format == VK_FORMAT_B8G8R8A8_UNORM,
          "undefined surface format resolves to a concrete SDR format");
    check(!pt::vk::ChooseSwapchainSurfaceFormat({}, true, true, selected, hdr), "empty format list is rejected safely");

    bool transfer_ok = true;
    for (float nits : {0.0f, 0.1f, 80.0f, 203.0f, 1000.0f, 10000.0f}) {
        const float roundtrip = pt::render::PqDecodeNits(pt::render::PqEncodeNits(nits));
        transfer_ok &= std::abs(roundtrip - nits) <= std::max(0.02f, nits * 0.0001f);
    }
    check(transfer_ok, "PQ encode and decode round-trip from black through 10000 nits");
    check(pt::render::LinearToSrgb8(0.0f) == 0 && pt::render::LinearToSrgb8(1.0f) == 255,
          "linear-to-sRGB PNG transfer has exact black and white endpoints");

    {
        bool identity = true, monotonic = true, bounded = true, smooth = true;
        for (float ratio : {1.2f, 2.0f, 5.0f, 10.0f, 50.0f}) {
            float previous = 0.0f;
            for (float m = 0.0f; m <= 20.0f; m += 0.01f) {
                const float s = pt::render::HdrShoulder(m, ratio);
                if (m <= 1.0f) identity &= s == m;
                monotonic &= s >= previous - 1.0e-5f;
                bounded &= s <= ratio + 1.0e-4f;
                const float width = std::min(pt::render::kHdrSourceWhite - 1.0f, 2.97f * (ratio - 1.0f));
                smooth &= std::abs(s - previous) <= 1.6f * std::max(1.0f, (ratio - 1.0f) / width) * 0.01f + 1.0e-4f;
                previous = s;
            }
        }
        check(identity, "highlight curve leaves the SDR range untouched");
        check(monotonic, "highlight curve never falls");
        check(bounded, "highlight curve never passes the peak");
        check(smooth, "highlight curve has no jump");
        check(std::abs(pt::render::HdrShoulder(1.0f, 5.0f) - 1.0f) < 1.0e-6f && pt::render::HdrShoulder(3.0f, 5.0f) == 5.0f &&
                  pt::render::HdrShoulder(100.0f, 5.0f) == 5.0f,
              "highlight curve starts at paper white and holds the peak from the source white");
        check(pt::render::HdrShoulder(7.0f, 1.0f) == 1.0f, "no headroom, no highlights");
        check(pt::render::ResolveHdrPeak(0.0f, 0.0f) == 1000.0f && pt::render::ResolveHdrPeak(0.0f, 800.0f) == 800.0f &&
                  pt::render::ResolveHdrPeak(1500.0f, 800.0f) == 1500.0f && pt::render::ResolveHdrPeak(0.0f, 20000.0f) == 4000.0f &&
                  pt::render::ResolveHdrPeak(100.0f, 0.0f) == 400.0f,
              "peak: the setting, else the display's, else 1000, within 400 to 4000");
        check(pt::render::ClampHdrPaperWhite(10.0f) == 80.0f && pt::render::ClampHdrPaperWhite(900.0f) == 500.0f &&
                  pt::render::ClampHdrPaperWhite(200.0f) == 200.0f,
              "paper white is kept within 80 to 500");
    }

    pt::render::PixelRect rect;
    check(pt::render::MakePixelCrop(3840, 2160, {0.25f, 0.25f, 0.5f, 0.5f}, rect) && rect.x == 960 && rect.y == 540 &&
              rect.width == 1920 && rect.height == 1080,
          "normalized photo crop maps to the requested pixel rectangle");
    check(!pt::render::MakePixelCrop(3840, 2160, {0.5f, 0.5f, 0.0f, 0.5f}, rect), "empty screenshot crop is rejected");
    check(!pt::render::MakePixelCrop(3840, 2160, {0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f}, rect),
          "non-finite screenshot crop is rejected");
    return failures ? 1 : 0;
}
